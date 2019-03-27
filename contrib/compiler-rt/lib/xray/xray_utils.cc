//===-- xray_utils.cc -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
//===----------------------------------------------------------------------===//
#include "xray_utils.h"

#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_common.h"
#include "xray_allocator.h"
#include "xray_defs.h"
#include "xray_flags.h"
#include <cstdio>
#include <errno.h>
#include <fcntl.h>
#include <iterator>
#include <stdlib.h>
#include <sys/types.h>
#include <tuple>
#include <unistd.h>
#include <utility>

#if SANITIZER_FUCHSIA
#include "sanitizer_common/sanitizer_symbolizer_fuchsia.h"

#include <inttypes.h>
#include <zircon/process.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#endif

namespace __xray {

#if SANITIZER_FUCHSIA
constexpr const char* ProfileSinkName = "llvm-xray";

LogWriter::~LogWriter() {
  _zx_handle_close(Vmo);
}

void LogWriter::WriteAll(const char *Begin, const char *End) XRAY_NEVER_INSTRUMENT {
  if (Begin == End)
    return;
  auto TotalBytes = std::distance(Begin, End);

  const size_t PageSize = flags()->xray_page_size_override > 0
                              ? flags()->xray_page_size_override
                              : GetPageSizeCached();
  if (RoundUpTo(Offset, PageSize) != RoundUpTo(Offset + TotalBytes, PageSize)) {
    // Resize the VMO to ensure there's sufficient space for the data.
    zx_status_t Status = _zx_vmo_set_size(Vmo, Offset + TotalBytes);
    if (Status != ZX_OK) {
      Report("Failed to resize VMO: %s\n", _zx_status_get_string(Status));
      return;
    }
  }

  // Write the data into VMO.
  zx_status_t Status = _zx_vmo_write(Vmo, Begin, Offset, TotalBytes);
  if (Status != ZX_OK) {
    Report("Failed to write: %s\n", _zx_status_get_string(Status));
    return;
  }
  Offset += TotalBytes;
}

void LogWriter::Flush() XRAY_NEVER_INSTRUMENT {
  // Nothing to do here since WriteAll writes directly into the VMO.
}

LogWriter *LogWriter::Open() XRAY_NEVER_INSTRUMENT {
  // Create VMO to hold the profile data.
  zx_handle_t Vmo;
  zx_status_t Status = _zx_vmo_create(0, 0, &Vmo);
  if (Status != ZX_OK) {
    Report("XRay: cannot create VMO: %s\n", _zx_status_get_string(Status));
    return nullptr;
  }

  // Get the KOID of the current process to use in the VMO name.
  zx_info_handle_basic_t Info;
  Status = _zx_object_get_info(_zx_process_self(), ZX_INFO_HANDLE_BASIC, &Info,
                               sizeof(Info), NULL, NULL);
  if (Status != ZX_OK) {
    Report("XRay: cannot get basic info about current process handle: %s\n",
           _zx_status_get_string(Status));
    return nullptr;
  }

  // Give the VMO a name including our process KOID so it's easy to spot.
  char VmoName[ZX_MAX_NAME_LEN];
  internal_snprintf(VmoName, sizeof(VmoName), "%s.%zu", ProfileSinkName,
                    Info.koid);
  _zx_object_set_property(Vmo, ZX_PROP_NAME, VmoName, strlen(VmoName));

  // Duplicate the handle since __sanitizer_publish_data consumes it and
  // LogWriter needs to hold onto it.
  zx_handle_t Handle;
  Status =_zx_handle_duplicate(Vmo, ZX_RIGHT_SAME_RIGHTS, &Handle);
  if (Status != ZX_OK) {
    Report("XRay: cannot duplicate VMO handle: %s\n",
           _zx_status_get_string(Status));
    return nullptr;
  }

  // Publish the VMO that receives the logging. Note the VMO's contents can
  // grow and change after publication. The contents won't be read out until
  // after the process exits.
  __sanitizer_publish_data(ProfileSinkName, Handle);

  // Use the dumpfile symbolizer markup element to write the name of the VMO.
  Report("XRay: " FORMAT_DUMPFILE "\n", ProfileSinkName, VmoName);

  LogWriter *LW = reinterpret_cast<LogWriter *>(InternalAlloc(sizeof(LogWriter)));
  new (LW) LogWriter(Vmo);
  return LW;
}

void LogWriter::Close(LogWriter *LW) {
  LW->~LogWriter();
  InternalFree(LW);
}
#else // SANITIZER_FUCHSIA
LogWriter::~LogWriter() {
  internal_close(Fd);
}

void LogWriter::WriteAll(const char *Begin, const char *End) XRAY_NEVER_INSTRUMENT {
  if (Begin == End)
    return;
  auto TotalBytes = std::distance(Begin, End);
  while (auto Written = write(Fd, Begin, TotalBytes)) {
    if (Written < 0) {
      if (errno == EINTR)
        continue; // Try again.
      Report("Failed to write; errno = %d\n", errno);
      return;
    }
    TotalBytes -= Written;
    if (TotalBytes == 0)
      break;
    Begin += Written;
  }
}

void LogWriter::Flush() XRAY_NEVER_INSTRUMENT {
  fsync(Fd);
}

LogWriter *LogWriter::Open() XRAY_NEVER_INSTRUMENT {
  // Open a temporary file once for the log.
  char TmpFilename[256] = {};
  char TmpWildcardPattern[] = "XXXXXX";
  auto **Argv = GetArgv();
  const char *Progname = !Argv ? "(unknown)" : Argv[0];
  const char *LastSlash = internal_strrchr(Progname, '/');

  if (LastSlash != nullptr)
    Progname = LastSlash + 1;

  int NeededLength = internal_snprintf(
      TmpFilename, sizeof(TmpFilename), "%s%s.%s",
      flags()->xray_logfile_base, Progname, TmpWildcardPattern);
  if (NeededLength > int(sizeof(TmpFilename))) {
    Report("XRay log file name too long (%d): %s\n", NeededLength, TmpFilename);
    return nullptr;
  }
  int Fd = mkstemp(TmpFilename);
  if (Fd == -1) {
    Report("XRay: Failed opening temporary file '%s'; not logging events.\n",
           TmpFilename);
    return nullptr;
  }
  if (Verbosity())
    Report("XRay: Log file in '%s'\n", TmpFilename);

  LogWriter *LW = allocate<LogWriter>();
  new (LW) LogWriter(Fd);
  return LW;
}

void LogWriter::Close(LogWriter *LW) {
  LW->~LogWriter();
  deallocate(LW);
}
#endif // SANITIZER_FUCHSIA

} // namespace __xray
