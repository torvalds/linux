//===-- CommandObjectStats.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CommandObjectStats.h"
#include "lldb/Host/Host.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;

class CommandObjectStatsEnable : public CommandObjectParsed {
public:
  CommandObjectStatsEnable(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "enable",
                            "Enable statistics collection", nullptr,
                            eCommandProcessMustBePaused) {}

  ~CommandObjectStatsEnable() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = GetSelectedOrDummyTarget();

    if (target->GetCollectingStats()) {
      result.AppendError("statistics already enabled");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    target->SetCollectingStats(true);
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }
};

class CommandObjectStatsDisable : public CommandObjectParsed {
public:
  CommandObjectStatsDisable(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "disable",
                            "Disable statistics collection", nullptr,
                            eCommandProcessMustBePaused) {}

  ~CommandObjectStatsDisable() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = GetSelectedOrDummyTarget();

    if (!target->GetCollectingStats()) {
      result.AppendError("need to enable statistics before disabling them");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    target->SetCollectingStats(false);
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }
};

class CommandObjectStatsDump : public CommandObjectParsed {
public:
  CommandObjectStatsDump(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "dump", "Dump statistics results",
                            nullptr, eCommandProcessMustBePaused) {}

  ~CommandObjectStatsDump() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = GetSelectedOrDummyTarget();

    uint32_t i = 0;
    for (auto &stat : target->GetStatistics()) {
      result.AppendMessageWithFormat(
          "%s : %u\n",
          lldb_private::GetStatDescription(static_cast<lldb_private::StatisticKind>(i))
              .c_str(),
          stat);
      i += 1;
    }
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }
};

CommandObjectStats::CommandObjectStats(CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "statistics",
                             "Print statistics about a debugging session",
                             "statistics <subcommand> [<subcommand-options>]") {
  LoadSubCommand("enable",
                 CommandObjectSP(new CommandObjectStatsEnable(interpreter)));
  LoadSubCommand("disable",
                 CommandObjectSP(new CommandObjectStatsDisable(interpreter)));
  LoadSubCommand("dump",
                 CommandObjectSP(new CommandObjectStatsDump(interpreter)));
}

CommandObjectStats::~CommandObjectStats() = default;
