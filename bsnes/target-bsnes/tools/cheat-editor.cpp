auto CheatDatabase::create() -> void {
  layout.setPadding(5_sx);
  selectAllButton.setText("Select All").onActivate([&] {
    for(auto item : cheatList.items()) item.setChecked(true);
  });
  unselectAllButton.setText("Unselect All").onActivate([&] {
    for(auto item : cheatList.items()) item.setChecked(false);
  });
  addCheatsButton.setText("Add Cheats").onActivate([&] {
    addCheats();
  });

  setSize({800_sx, 400_sx});
  setAlignment({0.5, 1.0});
  setDismissable();
}

auto CheatDatabase::findCheats() -> void {
  //hack to locate Super Game Boy cheat codes
  auto sha256a = emulator->hashes()(0, "none");
  auto sha256b = emulator->hashes()(1, "none");

  auto document = BML::unserialize(string::read(locate("Database/Cheat Codes.bml")));
  for(auto game : document.find("cartridge")) {
    if(game["sha256"].text() != sha256a
    && game["sha256"].text() != sha256b) continue;

    cheatList.reset();
    for(auto cheat : game.find("cheat")) {
      //convert old cheat format (address/data and address/compare/data)
      //to new cheat format (address=data and address=compare?data)
      auto code = cheat["code"].text();
      code.replace("/", "=", 1L);
      code.replace("/", "?", 1L);
      cheatList.append(ListViewItem()
        .setCheckable()
        .setText(cheat["description"].text())
        .setProperty("code", code)
      );
    }

    setTitle(game["name"].text());
    setVisible();
    return;
  }

  MessageDialog().setAlignment(*toolsWindow).setText("Sorry, no cheats were found for this game.").information();
}

auto CheatDatabase::addCheats() -> void {
  for(auto item : cheatList.items()) {
    if(item.checked()) {
      cheatEditor.addCheat({item.text(), item.property("code"), false});
    }
  }
  setVisible(false);
}

//

auto CheatWindow::create() -> void {
  layout.setPadding(5_sx);
  tableLayout.setSize({2, 2});
  tableLayout.cell(0).setAlignment({1.0, 0.5});
  tableLayout.cell(2).setAlignment({1.0, 0.0});
  nameLabel.setText("Name:");
  nameValue.onActivate([&] { if(acceptButton.enabled()) acceptButton.doActivate(); });
  nameValue.onChange([&] { doChange(); });
  codeLabel.setText("Code(s):");
  codeValue.setFont(Font().setFamily(Font::Mono));
  codeValue.onChange([&] { doChange(); });
  enableOption.setText("Enable");
  acceptButton.onActivate([&] { doAccept(); });
  cancelButton.setText("Cancel").onActivate([&] { setVisible(false); });

  setSize({400_sx, layout.minimumSize().height() + 100_sx});
  setDismissable();
}

auto CheatWindow::show(Cheat cheat) -> void {
  nameValue.setText(cheat.name);
  codeValue.setText(cheat.code.split("+").strip().merge("\n"));
  enableOption.setChecked(cheat.enable);
  doChange();
  setTitle(!cheat.name ? "Add Cheat" : "Edit Cheat");
  setAlignment(*toolsWindow);
  setVisible();
  setFocused();
  nameValue.setFocused();
  acceptButton.setText(!cheat.name ? "Add" : "Edit");
}

auto CheatWindow::doChange() -> void {
  bool valid = true;
  nameValue.setBackgroundColor(nameValue.text().strip() ? Color{} : (valid = false, Color{255, 224, 224}));
  codeValue.setBackgroundColor(codeValue.text().strip() ? Color{} : (valid = false, Color{255, 224, 224}));
  acceptButton.setEnabled(valid);
}

auto CheatWindow::doAccept() -> void {
  Cheat cheat = {nameValue.text().strip(), codeValue.text().split("\n").strip().merge("+"), enableOption.checked()};
  if(acceptButton.text() == "Add") {
    cheatEditor.addCheat(cheat);
  } else {
    cheatEditor.editCheat(cheat);
  }
  setVisible(false);
}

//

auto CheatEditor::create() -> void {
  setCollapsible();
  setVisible(false);

  cheatList.setBatchable();
  cheatList.setHeadered();
  cheatList.setSortable();
  cheatList.onActivate([&](auto cell) {
    //kind of a hack: toggling a cheat code twice quickly (onToggle) will call onActivate.
    //do not trigger the CheatWindow unless it's been at least two seconds since a cheat code was last toggled on or off.
    if(chrono::timestamp() - activateTimeout < 2) return;
    editButton.doActivate();
  });
  cheatList.onChange([&] {
    auto batched = cheatList.batched();
    editButton.setEnabled(batched.size() == 1);
    removeButton.setEnabled(batched.size() >= 1);
  });
  cheatList.onToggle([&](TableViewCell cell) {
    activateTimeout = chrono::timestamp();
    if(auto item = cell->parentTableViewItem()) {
      cheats[item->offset()].enable = cell.checked();
      synchronizeCodes();
    }
  });
  cheatList.onSort([&](TableViewColumn column) {
    column.setSorting(column.sorting() == Sort::Ascending ? Sort::Descending : Sort::Ascending);
  //cheatList.sort();
  });
  cheatList.onSize([&] {
    cheatList.resizeColumns();
  });
  findCheatsButton.setText("Find Cheats ...").onActivate([&] {
    cheatDatabase.findCheats();
  });
  enableCheats.setText("Enable Cheats").setToolTip(
    "Master enable for all cheat codes.\n"
    "When unchecked, no cheat codes will be active.\n\n"
    "Use this to bypass game areas that have problems with cheats."
  ).setChecked(settings.emulator.cheats.enable).onToggle([&] {
    settings.emulator.cheats.enable = enableCheats.checked();
    if(!enableCheats.checked()) {
      program.showMessage("All cheat codes disabled");
    } else {
      program.showMessage("Active cheat codes enabled");
    }
    synchronizeCodes();
  });
  addButton.setText("Add").onActivate([&] {
    cheatWindow.show();
  });
  editButton.setText("Edit").onActivate([&] {
    if(auto item = cheatList.selected()) {
      cheatWindow.show(cheats[item.offset()]);
    }
  });
  removeButton.setText("Remove").onActivate([&] {
    removeCheats();
  });

  //hide the "Find Cheats" button if the cheat code database isn't found
  if(!file::exists(locate("Database/Cheat Codes.bml"))) findCheatsButton.setVisible(false);
}

auto CheatEditor::refresh() -> void {
  cheatList.reset();
  cheatList.append(TableViewColumn());
  cheatList.append(TableViewColumn().setText("Name").setSorting(Sort::Ascending).setExpandable());
  for(auto& cheat : cheats) {
    TableViewItem item{&cheatList};
    item.append(TableViewCell().setCheckable().setChecked(cheat.enable));
    item.append(TableViewCell().setText(cheat.name));
  }
  cheatList.resizeColumns().doChange();
}

auto CheatEditor::addCheat(Cheat cheat) -> void {
  cheats.append(cheat);
  cheats.sort();
  refresh();
  for(uint index : range(cheats.size())) {
    if(cheats[index] == cheat) { cheatList.item(index).setSelected(); break; }
  }
  cheatList.doChange();
  synchronizeCodes();
}

auto CheatEditor::editCheat(Cheat cheat) -> void {
  if(auto item = cheatList.selected()) {
    cheats[item.offset()] = cheat;
    cheats.sort();
    refresh();
    for(uint index : range(cheats.size())) {
      if(cheats[index] == cheat) { cheatList.item(index).setSelected(); break; }
    }
    cheatList.doChange();
    synchronizeCodes();
  }
}

auto CheatEditor::removeCheats() -> void {
  if(auto batched = cheatList.batched()) {
    if(MessageDialog("Are you sure you want to permanently remove the selected cheat(s)?")
    .setAlignment(*toolsWindow).question() == "Yes") {
      for(auto& item : reverse(batched)) cheats.remove(item.offset());
      cheats.sort();
      refresh();
      synchronizeCodes();
    }
  }
}

auto CheatEditor::loadCheats() -> void {
  cheats.reset();
  auto location = program.cheatPath();
  auto document = BML::unserialize(string::read(location));
  for(auto cheat : document.find("cheat")) {
    cheats.append({cheat["name"].text(), cheat["code"].text(), (bool)cheat["enable"]});
  }
  cheats.sort();
  refresh();
  synchronizeCodes();
}

auto CheatEditor::saveCheats() -> void {
  string document;
  for(auto cheat : cheats) {
    document.append("cheat\n");
    document.append("  name: ", cheat.name, "\n");
    document.append("  code: ", cheat.code, "\n");
  if(cheat.enable)
    document.append("  enable\n");
    document.append("\n");
  }
  auto location = program.cheatPath();
  if(document) {
    file::write(location, document);
  } else {
    file::remove(location);
  }
}

auto CheatEditor::synchronizeCodes() -> void {
  vector<string> codes;
  if(enableCheats.checked()) {
    for(auto& cheat : cheats) {
      if(cheat.enable) codes.append(cheat.code);
    }
  }
  emulator->cheats(codes);
}
