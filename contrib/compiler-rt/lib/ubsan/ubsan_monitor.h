//===-- ubsan_monitor.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Hooks which allow a monitor process to inspect UBSan's diagnostics.
//
//===----------------------------------------------------------------------===//

#ifndef UBSAN_MONITOR_H
#define UBSAN_MONITOR_H

#include "ubsan_diag.h"
#include "ubsan_value.h"

namespace __ubsan {

struct UndefinedBehaviorReport {
  const char *IssueKind;
  Location &Loc;
  InternalScopedString Buffer;

  UndefinedBehaviorReport(const char *IssueKind, Location &Loc,
                          InternalScopedString &Msg);
};

SANITIZER_INTERFACE_ATTRIBUTE void
RegisterUndefinedBehaviorReport(UndefinedBehaviorReport *UBR);

/// Called after a report is prepared. This serves to alert monitor processes
/// that a UB report is available.
extern "C" SANITIZER_INTERFACE_ATTRIBUTE void __ubsan_on_report(void);

/// Used by the monitor process to extract information from a UB report. The
/// data is only available until the next time __ubsan_on_report is called. The
/// caller is responsible for copying and preserving the data if needed.
extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__ubsan_get_current_report_data(const char **OutIssueKind,
                                const char **OutMessage,
                                const char **OutFilename, unsigned *OutLine,
                                unsigned *OutCol, char **OutMemoryAddr);

} // end namespace __ubsan

#endif // UBSAN_MONITOR_H
