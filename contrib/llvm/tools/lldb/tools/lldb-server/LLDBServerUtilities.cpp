//===-- LLDBServerUtilities.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LLDBServerUtilities.h"

#include "lldb/Core/StreamFile.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"

using namespace lldb;
using namespace lldb_private::lldb_server;
using namespace llvm;

static std::shared_ptr<raw_ostream> GetLogStream(StringRef log_file) {
  if (!log_file.empty()) {
    std::error_code EC;
    std::shared_ptr<raw_ostream> stream_sp = std::make_shared<raw_fd_ostream>(
        log_file, EC, sys::fs::F_Text | sys::fs::F_Append);
    if (!EC)
      return stream_sp;
    errs() << llvm::formatv(
        "Failed to open log file `{0}`: {1}\nWill log to stderr instead.\n",
        log_file, EC.message());
  }
  // No need to delete the stderr stream.
  return std::shared_ptr<raw_ostream>(&errs(), [](raw_ostream *) {});
}

bool LLDBServerUtilities::SetupLogging(const std::string &log_file,
                                       const StringRef &log_channels,
                                       uint32_t log_options) {

  auto log_stream_sp = GetLogStream(log_file);

  SmallVector<StringRef, 32> channel_array;
  log_channels.split(channel_array, ":", /*MaxSplit*/ -1, /*KeepEmpty*/ false);
  for (auto channel_with_categories : channel_array) {
    std::string error;
    llvm::raw_string_ostream error_stream(error);
    Args channel_then_categories(channel_with_categories);
    std::string channel(channel_then_categories.GetArgumentAtIndex(0));
    channel_then_categories.Shift(); // Shift off the channel

    bool success = Log::EnableLogChannel(
        log_stream_sp, log_options, channel,
        channel_then_categories.GetArgumentArrayRef(), error_stream);
    if (!success) {
      errs() << formatv("Unable to setup logging for channel \"{0}\": {1}",
                        channel, error_stream.str());
      return false;
    }
  }
  return true;
}
