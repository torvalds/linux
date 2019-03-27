//===-- ObjCPlusPlusLanguage.cpp --------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ObjCPlusPlusLanguage.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Utility/ConstString.h"

using namespace lldb;
using namespace lldb_private;

bool ObjCPlusPlusLanguage::IsSourceFile(llvm::StringRef file_path) const {
  const auto suffixes = {".h", ".mm"};
  for (auto suffix : suffixes) {
    if (file_path.endswith_lower(suffix))
      return true;
  }
  return false;
}

void ObjCPlusPlusLanguage::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(), "Objective-C++ Language",
                                CreateInstance);
}

void ObjCPlusPlusLanguage::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString ObjCPlusPlusLanguage::GetPluginNameStatic() {
  static ConstString g_name("objcplusplus");
  return g_name;
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
lldb_private::ConstString ObjCPlusPlusLanguage::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t ObjCPlusPlusLanguage::GetPluginVersion() { return 1; }

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------
Language *ObjCPlusPlusLanguage::CreateInstance(lldb::LanguageType language) {
  switch (language) {
  case lldb::eLanguageTypeObjC_plus_plus:
    return new ObjCPlusPlusLanguage();
  default:
    return nullptr;
  }
}
