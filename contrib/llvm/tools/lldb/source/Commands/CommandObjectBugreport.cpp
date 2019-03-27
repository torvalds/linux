//===-- CommandObjectBugreport.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CommandObjectBugreport.h"

#include <cstdio>


#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionGroupOutputFile.h"
#include "lldb/Target/Thread.h"

using namespace lldb;
using namespace lldb_private;

//-------------------------------------------------------------------------
// "bugreport unwind"
//-------------------------------------------------------------------------

class CommandObjectBugreportUnwind : public CommandObjectParsed {
public:
  CommandObjectBugreportUnwind(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "bugreport unwind",
            "Create a bugreport for a bug in the stack unwinding code.",
            nullptr),
        m_option_group(), m_outfile_options() {
    m_option_group.Append(&m_outfile_options, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_1 | LLDB_OPT_SET_2 | LLDB_OPT_SET_3);
    m_option_group.Finalize();
  }

  ~CommandObjectBugreportUnwind() override {}

  Options *GetOptions() override { return &m_option_group; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    StringList commands;
    commands.AppendString("thread backtrace");

    Thread *thread = m_exe_ctx.GetThreadPtr();
    if (thread) {
      char command_buffer[256];

      uint32_t frame_count = thread->GetStackFrameCount();
      for (uint32_t i = 0; i < frame_count; ++i) {
        StackFrameSP frame = thread->GetStackFrameAtIndex(i);
        lldb::addr_t pc = frame->GetStackID().GetPC();

        snprintf(command_buffer, sizeof(command_buffer),
                 "disassemble --bytes --address 0x%" PRIx64, pc);
        commands.AppendString(command_buffer);

        snprintf(command_buffer, sizeof(command_buffer),
                 "image show-unwind --address 0x%" PRIx64, pc);
        commands.AppendString(command_buffer);
      }
    }

    const FileSpec &outfile_spec =
        m_outfile_options.GetFile().GetCurrentValue();
    if (outfile_spec) {

      uint32_t open_options =
          File::eOpenOptionWrite | File::eOpenOptionCanCreate |
          File::eOpenOptionAppend | File::eOpenOptionCloseOnExec;

      const bool append = m_outfile_options.GetAppend().GetCurrentValue();
      if (!append)
        open_options |= File::eOpenOptionTruncate;

      StreamFileSP outfile_stream = std::make_shared<StreamFile>();
      File &file = outfile_stream->GetFile();
      Status error =
          FileSystem::Instance().Open(file, outfile_spec, open_options);
      if (error.Fail()) {
        auto path = outfile_spec.GetPath();
        result.AppendErrorWithFormat("Failed to open file '%s' for %s: %s\n",
                                     path.c_str(), append ? "append" : "write",
                                     error.AsCString());
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      result.SetImmediateOutputStream(outfile_stream);
    }

    CommandInterpreterRunOptions options;
    options.SetStopOnError(false);
    options.SetEchoCommands(true);
    options.SetPrintResults(true);
    options.SetAddToHistory(false);
    m_interpreter.HandleCommands(commands, &m_exe_ctx, options, result);

    return result.Succeeded();
  }

private:
  OptionGroupOptions m_option_group;
  OptionGroupOutputFile m_outfile_options;
};

#pragma mark CommandObjectMultiwordBugreport

//-------------------------------------------------------------------------
// CommandObjectMultiwordBugreport
//-------------------------------------------------------------------------

CommandObjectMultiwordBugreport::CommandObjectMultiwordBugreport(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(
          interpreter, "bugreport",
          "Commands for creating domain-specific bug reports.",
          "bugreport <subcommand> [<subcommand-options>]") {

  LoadSubCommand(
      "unwind", CommandObjectSP(new CommandObjectBugreportUnwind(interpreter)));
}

CommandObjectMultiwordBugreport::~CommandObjectMultiwordBugreport() {}
