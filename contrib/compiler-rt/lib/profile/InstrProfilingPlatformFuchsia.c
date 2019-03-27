/*===- InstrProfilingPlatformFuchsia.c - Profile data Fuchsia platform ----===*\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|*
\*===----------------------------------------------------------------------===*/
/*
 * This file implements the profiling runtime for Fuchsia and defines the
 * shared profile runtime interface. Each module (executable or DSO) statically
 * links in the whole profile runtime to satisfy the calls from its
 * instrumented code. Several modules in the same program might be separately
 * compiled and even use different versions of the instrumentation ABI and data
 * format. All they share in common is the VMO and the offset, which live in
 * exported globals so that exactly one definition will be shared across all
 * modules. Each module has its own independent runtime that registers its own
 * atexit hook to append its own data into the shared VMO which is published
 * via the data sink hook provided by Fuchsia's dynamic linker.
 */

#if defined(__Fuchsia__)

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

#include <zircon/process.h>
#include <zircon/sanitizer.h>
#include <zircon/syscalls.h>

#include "InstrProfiling.h"
#include "InstrProfilingInternal.h"
#include "InstrProfilingUtil.h"

/* VMO that contains the coverage data shared across all modules. This symbol
 * has default visibility and is exported in each module (executable or DSO)
 * that statically links in the profiling runtime.
 */
zx_handle_t __llvm_profile_vmo;
/* Current offset within the VMO where data should be written next. This symbol
 * has default visibility and is exported in each module (executable or DSO)
 * that statically links in the profiling runtime.
 */
uint64_t __llvm_profile_offset;

static const char ProfileSinkName[] = "llvm-profile";

static inline void lprofWrite(const char *fmt, ...) {
  char s[256];

  va_list ap;
  va_start(ap, fmt);
  int ret = vsnprintf(s, sizeof(s), fmt, ap);
  va_end(ap);

  __sanitizer_log_write(s, ret + 1);
}

static uint32_t lprofVMOWriter(ProfDataWriter *This, ProfDataIOVec *IOVecs,
                               uint32_t NumIOVecs) {
  /* Allocate VMO if it hasn't been created yet. */
  if (__llvm_profile_vmo == ZX_HANDLE_INVALID) {
    /* Get information about the current process. */
    zx_info_handle_basic_t Info;
    zx_status_t Status =
        _zx_object_get_info(_zx_process_self(), ZX_INFO_HANDLE_BASIC, &Info,
                            sizeof(Info), NULL, NULL);
    if (Status != ZX_OK)
      return -1;

    /* Create VMO to hold the profile data. */
    Status = _zx_vmo_create(0, 0, &__llvm_profile_vmo);
    if (Status != ZX_OK)
      return -1;

    /* Give the VMO a name including our process KOID so it's easy to spot. */
    char VmoName[ZX_MAX_NAME_LEN];
    snprintf(VmoName, sizeof(VmoName), "%s.%" PRIu64, ProfileSinkName,
             Info.koid);
    _zx_object_set_property(__llvm_profile_vmo, ZX_PROP_NAME, VmoName,
                            strlen(VmoName));

    /* Duplicate the handle since __sanitizer_publish_data consumes it. */
    zx_handle_t Handle;
    Status =
        _zx_handle_duplicate(__llvm_profile_vmo, ZX_RIGHT_SAME_RIGHTS, &Handle);
    if (Status != ZX_OK)
      return -1;

    /* Publish the VMO which contains profile data to the system. */
    __sanitizer_publish_data(ProfileSinkName, Handle);

    /* Use the dumpfile symbolizer markup element to write the name of VMO. */
    lprofWrite("LLVM Profile: {{{dumpfile:%s:%s}}}\n",
               ProfileSinkName, VmoName);
  }

  /* Compute the total length of data to be written. */
  size_t Length = 0;
  for (uint32_t I = 0; I < NumIOVecs; I++)
    Length += IOVecs[I].ElmSize * IOVecs[I].NumElm;

  /* Resize the VMO to ensure there's sufficient space for the data. */
  zx_status_t Status =
      _zx_vmo_set_size(__llvm_profile_vmo, __llvm_profile_offset + Length);
  if (Status != ZX_OK)
    return -1;

  /* Copy the data into VMO. */
  for (uint32_t I = 0; I < NumIOVecs; I++) {
    size_t Length = IOVecs[I].ElmSize * IOVecs[I].NumElm;
    if (IOVecs[I].Data) {
      Status = _zx_vmo_write(__llvm_profile_vmo, IOVecs[I].Data,
                             __llvm_profile_offset, Length);
      if (Status != ZX_OK)
        return -1;
    }
    __llvm_profile_offset += Length;
  }

  return 0;
}

static void initVMOWriter(ProfDataWriter *This) {
  This->Write = lprofVMOWriter;
  This->WriterCtx = NULL;
}

static int dump(void) {
  if (lprofProfileDumped()) {
    lprofWrite("Profile data not published: already written.\n");
    return 0;
  }

  /* Check if there is llvm/runtime version mismatch. */
  if (GET_VERSION(__llvm_profile_get_version()) != INSTR_PROF_RAW_VERSION) {
    lprofWrite("Runtime and instrumentation version mismatch : "
               "expected %d, but got %d\n",
               INSTR_PROF_RAW_VERSION,
               (int)GET_VERSION(__llvm_profile_get_version()));
    return -1;
  }

  /* Write the profile data into the mapped region. */
  ProfDataWriter VMOWriter;
  initVMOWriter(&VMOWriter);
  if (lprofWriteData(&VMOWriter, lprofGetVPDataReader(), 0) != 0)
    return -1;

  return 0;
}

COMPILER_RT_VISIBILITY
int __llvm_profile_dump(void) {
  int rc = dump();
  lprofSetProfileDumped();
  return rc;
}

static void dumpWithoutReturn(void) { dump(); }

/* This method is invoked by the runtime initialization hook
 * InstrProfilingRuntime.o if it is linked in.
 */
COMPILER_RT_VISIBILITY
void __llvm_profile_initialize_file(void) {}

COMPILER_RT_VISIBILITY
int __llvm_profile_register_write_file_atexit(void) {
  static bool HasBeenRegistered = false;

  if (HasBeenRegistered)
    return 0;

  lprofSetupValueProfiler();

  HasBeenRegistered = true;
  return atexit(dumpWithoutReturn);
}

#endif
