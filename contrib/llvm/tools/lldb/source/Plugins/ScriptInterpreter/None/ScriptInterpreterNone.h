//===-- ScriptInterpreterNone.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ScriptInterpreterNone_h_
#define liblldb_ScriptInterpreterNone_h_

#include "lldb/Interpreter/ScriptInterpreter.h"

namespace lldb_private {

class ScriptInterpreterNone : public ScriptInterpreter {
public:
  ScriptInterpreterNone(CommandInterpreter &interpreter);

  ~ScriptInterpreterNone() override;

  bool ExecuteOneLine(
      llvm::StringRef command, CommandReturnObject *result,
      const ExecuteScriptOptions &options = ExecuteScriptOptions()) override;

  void ExecuteInterpreterLoop() override;

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();

  static void Terminate();

  static lldb::ScriptInterpreterSP
  CreateInstance(CommandInterpreter &interpreter);

  static lldb_private::ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic();

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;
};

} // namespace lldb_private

#endif // liblldb_ScriptInterpreterNone_h_
