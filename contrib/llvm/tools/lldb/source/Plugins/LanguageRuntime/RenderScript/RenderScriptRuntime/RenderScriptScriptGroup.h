//===-- RenderScriptScriptGroup.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RenderScriptScriptGroup_h_
#define liblldb_RenderScriptScriptGroup_h_

#include "lldb/Interpreter/CommandInterpreter.h"

lldb::CommandObjectSP NewCommandObjectRenderScriptScriptGroup(
    lldb_private::CommandInterpreter &interpreter);

#endif // liblldb_RenderScriptScriptGroup_h_
