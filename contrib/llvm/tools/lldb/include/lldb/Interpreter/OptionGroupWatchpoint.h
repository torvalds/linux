//===-- OptionGroupWatchpoint.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionGroupWatchpoint_h_
#define liblldb_OptionGroupWatchpoint_h_

#include "lldb/Interpreter/Options.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// OptionGroupWatchpoint
//-------------------------------------------------------------------------

class OptionGroupWatchpoint : public OptionGroup {
public:
  OptionGroupWatchpoint();

  ~OptionGroupWatchpoint() override;

  static bool IsWatchSizeSupported(uint32_t watch_size);

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;
  Status SetOptionValue(uint32_t, const char *, ExecutionContext *) = delete;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  // Note:
  // eWatchRead == LLDB_WATCH_TYPE_READ; and
  // eWatchWrite == LLDB_WATCH_TYPE_WRITE
  typedef enum WatchType {
    eWatchInvalid = 0,
    eWatchRead,
    eWatchWrite,
    eWatchReadWrite
  } WatchType;

  WatchType watch_type;
  uint32_t watch_size;
  bool watch_type_specified;

private:
  DISALLOW_COPY_AND_ASSIGN(OptionGroupWatchpoint);
};

} // namespace lldb_private

#endif // liblldb_OptionGroupWatchpoint_h_
