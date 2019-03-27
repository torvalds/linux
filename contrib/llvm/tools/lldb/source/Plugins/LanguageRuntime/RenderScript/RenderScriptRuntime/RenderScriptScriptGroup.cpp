//===-- RenderScriptScriptGroup.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"

#include "RenderScriptRuntime.h"
#include "RenderScriptScriptGroup.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_renderscript;

class CommandObjectRenderScriptScriptGroupBreakpointSet
    : public CommandObjectParsed {
public:
  CommandObjectRenderScriptScriptGroupBreakpointSet(
      CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "renderscript scriptgroup breakpoint set",
            "Place a breakpoint on all kernels forming a script group.",
            "renderscript scriptgroup breakpoint set <group_name>",
            eCommandRequiresProcess | eCommandProcessMustBeLaunched) {}

  ~CommandObjectRenderScriptScriptGroupBreakpointSet() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Stream &stream = result.GetOutputStream();
    RenderScriptRuntime *runtime = static_cast<RenderScriptRuntime *>(
        m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript));
    assert(runtime);
    auto &target = m_exe_ctx.GetTargetSP();
    bool stop_on_all = false;
    const llvm::StringRef long_stop_all("--stop-on-all"), short_stop_all("-a");
    std::vector<ConstString> sites;
    sites.reserve(command.GetArgumentCount());
    for (size_t i = 0; i < command.GetArgumentCount(); ++i) {
      const auto arg = command.GetArgumentAtIndex(i);
      if (long_stop_all == arg || short_stop_all == arg)
        stop_on_all = true;
      else
        sites.push_back(ConstString(arg));
    }
    for (const auto &name : sites) {
      runtime->PlaceBreakpointOnScriptGroup(target, stream, name, stop_on_all);
    }
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }
};

class CommandObjectRenderScriptScriptGroupBreakpoint
    : public CommandObjectMultiword {
public:
  CommandObjectRenderScriptScriptGroupBreakpoint(
      CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "renderscript scriptgroup breakpoint",
            "Renderscript scriptgroup breakpoint interaction.",
            "renderscript scriptgroup breakpoint set [--stop-on-all/-a]"
            "<scriptgroup name> ...",
            eCommandRequiresProcess | eCommandProcessMustBeLaunched) {
    LoadSubCommand(
        "set",
        CommandObjectSP(new CommandObjectRenderScriptScriptGroupBreakpointSet(
            interpreter)));
  }

  ~CommandObjectRenderScriptScriptGroupBreakpoint() override = default;
};

class CommandObjectRenderScriptScriptGroupList : public CommandObjectParsed {
public:
  CommandObjectRenderScriptScriptGroupList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "renderscript scriptgroup list",
                            "List all currently discovered script groups.",
                            "renderscript scriptgroup list",
                            eCommandRequiresProcess |
                                eCommandProcessMustBeLaunched) {}

  ~CommandObjectRenderScriptScriptGroupList() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Stream &stream = result.GetOutputStream();
    RenderScriptRuntime *runtime = static_cast<RenderScriptRuntime *>(
        m_exe_ctx.GetProcessPtr()->GetLanguageRuntime(
            eLanguageTypeExtRenderScript));
    assert(runtime);
    const RSScriptGroupList &groups = runtime->GetScriptGroups();
    // print script group count
    stream.Printf("%" PRIu64 " script %s", uint64_t(groups.size()),
                  (groups.size() == 1) ? "group" : "groups");
    stream.EOL();
    // print script group details
    stream.IndentMore();
    for (const RSScriptGroupDescriptorSP &g : groups) {
      if (g) {
        stream.Indent();
        // script group name
        stream.Printf("%s", g->m_name.AsCString());
        stream.EOL();
        // print out the kernels
        stream.IndentMore();
        for (const auto &k : g->m_kernels) {
          stream.Indent();
          stream.Printf(". %s", k.m_name.AsCString());
          stream.EOL();
        }
        stream.IndentLess();
      }
    }
    stream.IndentLess();
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }
};

class CommandObjectRenderScriptScriptGroup : public CommandObjectMultiword {
public:
  CommandObjectRenderScriptScriptGroup(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "renderscript scriptgroup",
                               "Command set for interacting with scriptgroups.",
                               nullptr, eCommandRequiresProcess |
                                            eCommandProcessMustBeLaunched) {
    LoadSubCommand(
        "breakpoint",
        CommandObjectSP(
            new CommandObjectRenderScriptScriptGroupBreakpoint(interpreter)));
    LoadSubCommand(
        "list", CommandObjectSP(
                    new CommandObjectRenderScriptScriptGroupList(interpreter)));
  }

  ~CommandObjectRenderScriptScriptGroup() override = default;
};

lldb::CommandObjectSP NewCommandObjectRenderScriptScriptGroup(
    lldb_private::CommandInterpreter &interpreter) {
  return CommandObjectSP(new CommandObjectRenderScriptScriptGroup(interpreter));
}
