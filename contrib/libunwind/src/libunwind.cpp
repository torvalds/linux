//===--------------------------- libunwind.cpp ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//
//  Implements unw_* functions from <libunwind.h>
//
//===----------------------------------------------------------------------===//

#include <libunwind.h>

#ifndef NDEBUG
#include <cstdlib> // getenv
#endif
#include <new>
#include <algorithm>

#include "libunwind_ext.h"
#include "config.h"

#include <stdlib.h>


#if !defined(__USING_SJLJ_EXCEPTIONS__)
#include "AddressSpace.hpp"
#include "UnwindCursor.hpp"

using namespace libunwind;

/// internal object to represent this processes address space
LocalAddressSpace LocalAddressSpace::sThisAddressSpace;

_LIBUNWIND_EXPORT unw_addr_space_t unw_local_addr_space =
    (unw_addr_space_t)&LocalAddressSpace::sThisAddressSpace;

/// record the registers and stack position of the caller
extern int unw_getcontext(unw_context_t *);
// note: unw_getcontext() implemented in assembly

/// Create a cursor of a thread in this process given 'context' recorded by
/// unw_getcontext().
_LIBUNWIND_EXPORT int unw_init_local(unw_cursor_t *cursor,
                                     unw_context_t *context) {
  _LIBUNWIND_TRACE_API("unw_init_local(cursor=%p, context=%p)",
                       static_cast<void *>(cursor),
                       static_cast<void *>(context));
#if defined(__i386__)
# define REGISTER_KIND Registers_x86
#elif defined(__x86_64__)
# define REGISTER_KIND Registers_x86_64
#elif defined(__powerpc64__)
# define REGISTER_KIND Registers_ppc64
#elif defined(__ppc__)
# define REGISTER_KIND Registers_ppc
#elif defined(__aarch64__)
# define REGISTER_KIND Registers_arm64
#elif defined(__arm__)
# define REGISTER_KIND Registers_arm
#elif defined(__or1k__)
# define REGISTER_KIND Registers_or1k
#elif defined(__riscv)
# define REGISTER_KIND Registers_riscv
#elif defined(__mips__) && defined(_ABIO32) && _MIPS_SIM == _ABIO32
# define REGISTER_KIND Registers_mips_o32
#elif defined(__mips64)
# define REGISTER_KIND Registers_mips_newabi
#elif defined(__mips__)
# warning The MIPS architecture is not supported with this ABI and environment!
#elif defined(__sparc__)
# define REGISTER_KIND Registers_sparc
#else
# error Architecture not supported
#endif
  // Use "placement new" to allocate UnwindCursor in the cursor buffer.
  new ((void *)cursor) UnwindCursor<LocalAddressSpace, REGISTER_KIND>(
                                 context, LocalAddressSpace::sThisAddressSpace);
#undef REGISTER_KIND
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  co->setInfoBasedOnIPRegister();

  return UNW_ESUCCESS;
}

#ifdef UNW_REMOTE
/// Create a cursor into a thread in another process.
_LIBUNWIND_EXPORT int unw_init_remote_thread(unw_cursor_t *cursor,
                                             unw_addr_space_t as,
                                             void *arg) {
  // special case: unw_init_remote(xx, unw_local_addr_space, xx)
  if (as == (unw_addr_space_t)&LocalAddressSpace::sThisAddressSpace)
    return unw_init_local(cursor, NULL); //FIXME

  // use "placement new" to allocate UnwindCursor in the cursor buffer
  switch (as->cpuType) {
  case CPU_TYPE_I386:
    new ((void *)cursor)
        UnwindCursor<RemoteAddressSpace<Pointer32<LittleEndian>>,
                     Registers_x86>(((unw_addr_space_i386 *)as)->oas, arg);
    break;
  case CPU_TYPE_X86_64:
    new ((void *)cursor)
        UnwindCursor<RemoteAddressSpace<Pointer64<LittleEndian>>,
                     Registers_x86_64>(((unw_addr_space_x86_64 *)as)->oas, arg);
    break;
  case CPU_TYPE_POWERPC:
    new ((void *)cursor)
        UnwindCursor<RemoteAddressSpace<Pointer32<BigEndian>>,
                     Registers_ppc>(((unw_addr_space_ppc *)as)->oas, arg);
    break;
  default:
    return UNW_EUNSPEC;
  }
  return UNW_ESUCCESS;
}


static bool is64bit(task_t task) {
  return false; // FIXME
}

/// Create an address_space object for use in examining another task.
_LIBUNWIND_EXPORT unw_addr_space_t unw_create_addr_space_for_task(task_t task) {
#if __i386__
  if (is64bit(task)) {
    unw_addr_space_x86_64 *as = new unw_addr_space_x86_64(task);
    as->taskPort = task;
    as->cpuType = CPU_TYPE_X86_64;
    //as->oas
  } else {
    unw_addr_space_i386 *as = new unw_addr_space_i386(task);
    as->taskPort = task;
    as->cpuType = CPU_TYPE_I386;
    //as->oas
  }
#else
// FIXME
#endif
}


/// Delete an address_space object.
_LIBUNWIND_EXPORT void unw_destroy_addr_space(unw_addr_space_t asp) {
  switch (asp->cpuType) {
#if __i386__ || __x86_64__
  case CPU_TYPE_I386: {
    unw_addr_space_i386 *as = (unw_addr_space_i386 *)asp;
    delete as;
  }
  break;
  case CPU_TYPE_X86_64: {
    unw_addr_space_x86_64 *as = (unw_addr_space_x86_64 *)asp;
    delete as;
  }
  break;
#endif
  case CPU_TYPE_POWERPC: {
    unw_addr_space_ppc *as = (unw_addr_space_ppc *)asp;
    delete as;
  }
  break;
  }
}
#endif // UNW_REMOTE


/// Get value of specified register at cursor position in stack frame.
_LIBUNWIND_EXPORT int unw_get_reg(unw_cursor_t *cursor, unw_regnum_t regNum,
                                  unw_word_t *value) {
  _LIBUNWIND_TRACE_API("unw_get_reg(cursor=%p, regNum=%d, &value=%p)",
                       static_cast<void *>(cursor), regNum,
                       static_cast<void *>(value));
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  if (co->validReg(regNum)) {
    *value = co->getReg(regNum);
    return UNW_ESUCCESS;
  }
  return UNW_EBADREG;
}


/// Set value of specified register at cursor position in stack frame.
_LIBUNWIND_EXPORT int unw_set_reg(unw_cursor_t *cursor, unw_regnum_t regNum,
                                  unw_word_t value) {
  _LIBUNWIND_TRACE_API("unw_set_reg(cursor=%p, regNum=%d, value=0x%" PRIxPTR ")",
                       static_cast<void *>(cursor), regNum, value);
  typedef LocalAddressSpace::pint_t pint_t;
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  if (co->validReg(regNum)) {
    co->setReg(regNum, (pint_t)value);
    // specical case altering IP to re-find info (being called by personality
    // function)
    if (regNum == UNW_REG_IP) {
      unw_proc_info_t info;
      // First, get the FDE for the old location and then update it.
      co->getInfo(&info);
      co->setInfoBasedOnIPRegister(false);
      // If the original call expects stack adjustment, perform this now.
      // Normal frame unwinding would have included the offset already in the
      // CFA computation.
      // Note: for PA-RISC and other platforms where the stack grows up,
      // this should actually be - info.gp. LLVM doesn't currently support
      // any such platforms and Clang doesn't export a macro for them.
      if (info.gp)
        co->setReg(UNW_REG_SP, co->getReg(UNW_REG_SP) + info.gp);
    }
    return UNW_ESUCCESS;
  }
  return UNW_EBADREG;
}


/// Get value of specified float register at cursor position in stack frame.
_LIBUNWIND_EXPORT int unw_get_fpreg(unw_cursor_t *cursor, unw_regnum_t regNum,
                                    unw_fpreg_t *value) {
  _LIBUNWIND_TRACE_API("unw_get_fpreg(cursor=%p, regNum=%d, &value=%p)",
                       static_cast<void *>(cursor), regNum,
                       static_cast<void *>(value));
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  if (co->validFloatReg(regNum)) {
    *value = co->getFloatReg(regNum);
    return UNW_ESUCCESS;
  }
  return UNW_EBADREG;
}


/// Set value of specified float register at cursor position in stack frame.
_LIBUNWIND_EXPORT int unw_set_fpreg(unw_cursor_t *cursor, unw_regnum_t regNum,
                                    unw_fpreg_t value) {
#if defined(_LIBUNWIND_ARM_EHABI)
  _LIBUNWIND_TRACE_API("unw_set_fpreg(cursor=%p, regNum=%d, value=%llX)",
                       static_cast<void *>(cursor), regNum, value);
#else
  _LIBUNWIND_TRACE_API("unw_set_fpreg(cursor=%p, regNum=%d, value=%g)",
                       static_cast<void *>(cursor), regNum, value);
#endif
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  if (co->validFloatReg(regNum)) {
    co->setFloatReg(regNum, value);
    return UNW_ESUCCESS;
  }
  return UNW_EBADREG;
}


/// Move cursor to next frame.
_LIBUNWIND_EXPORT int unw_step(unw_cursor_t *cursor) {
  _LIBUNWIND_TRACE_API("unw_step(cursor=%p)", static_cast<void *>(cursor));
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  return co->step();
}


/// Get unwind info at cursor position in stack frame.
_LIBUNWIND_EXPORT int unw_get_proc_info(unw_cursor_t *cursor,
                                        unw_proc_info_t *info) {
  _LIBUNWIND_TRACE_API("unw_get_proc_info(cursor=%p, &info=%p)",
                       static_cast<void *>(cursor), static_cast<void *>(info));
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  co->getInfo(info);
  if (info->end_ip == 0)
    return UNW_ENOINFO;
  else
    return UNW_ESUCCESS;
}


/// Resume execution at cursor position (aka longjump).
_LIBUNWIND_EXPORT int unw_resume(unw_cursor_t *cursor) {
  _LIBUNWIND_TRACE_API("unw_resume(cursor=%p)", static_cast<void *>(cursor));
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  co->jumpto();
  return UNW_EUNSPEC;
}


/// Get name of function at cursor position in stack frame.
_LIBUNWIND_EXPORT int unw_get_proc_name(unw_cursor_t *cursor, char *buf,
                                        size_t bufLen, unw_word_t *offset) {
  _LIBUNWIND_TRACE_API("unw_get_proc_name(cursor=%p, &buf=%p, bufLen=%lu)",
                       static_cast<void *>(cursor), static_cast<void *>(buf),
                       static_cast<unsigned long>(bufLen));
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  if (co->getFunctionName(buf, bufLen, offset))
    return UNW_ESUCCESS;
  else
    return UNW_EUNSPEC;
}


/// Checks if a register is a floating-point register.
_LIBUNWIND_EXPORT int unw_is_fpreg(unw_cursor_t *cursor, unw_regnum_t regNum) {
  _LIBUNWIND_TRACE_API("unw_is_fpreg(cursor=%p, regNum=%d)",
                       static_cast<void *>(cursor), regNum);
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  return co->validFloatReg(regNum);
}


/// Checks if a register is a floating-point register.
_LIBUNWIND_EXPORT const char *unw_regname(unw_cursor_t *cursor,
                                          unw_regnum_t regNum) {
  _LIBUNWIND_TRACE_API("unw_regname(cursor=%p, regNum=%d)",
                       static_cast<void *>(cursor), regNum);
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  return co->getRegisterName(regNum);
}


/// Checks if current frame is signal trampoline.
_LIBUNWIND_EXPORT int unw_is_signal_frame(unw_cursor_t *cursor) {
  _LIBUNWIND_TRACE_API("unw_is_signal_frame(cursor=%p)",
                       static_cast<void *>(cursor));
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  return co->isSignalFrame();
}

#ifdef __arm__
// Save VFP registers d0-d15 using FSTMIADX instead of FSTMIADD
_LIBUNWIND_EXPORT void unw_save_vfp_as_X(unw_cursor_t *cursor) {
  _LIBUNWIND_TRACE_API("unw_fpreg_save_vfp_as_X(cursor=%p)",
                       static_cast<void *>(cursor));
  AbstractUnwindCursor *co = (AbstractUnwindCursor *)cursor;
  return co->saveVFPAsX();
}
#endif


#if defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)
/// SPI: walks cached DWARF entries
_LIBUNWIND_EXPORT void unw_iterate_dwarf_unwind_cache(void (*func)(
    unw_word_t ip_start, unw_word_t ip_end, unw_word_t fde, unw_word_t mh)) {
  _LIBUNWIND_TRACE_API("unw_iterate_dwarf_unwind_cache(func=%p)",
                       reinterpret_cast<void *>(func));
  DwarfFDECache<LocalAddressSpace>::iterateCacheEntries(func);
}


/// IPI: for __register_frame()
void _unw_add_dynamic_fde(unw_word_t fde) {
  CFI_Parser<LocalAddressSpace>::FDE_Info fdeInfo;
  CFI_Parser<LocalAddressSpace>::CIE_Info cieInfo;
  const char *message = CFI_Parser<LocalAddressSpace>::decodeFDE(
                           LocalAddressSpace::sThisAddressSpace,
                          (LocalAddressSpace::pint_t) fde, &fdeInfo, &cieInfo);
  if (message == NULL) {
    // dynamically registered FDEs don't have a mach_header group they are in.
    // Use fde as mh_group
    unw_word_t mh_group = fdeInfo.fdeStart;
    DwarfFDECache<LocalAddressSpace>::add((LocalAddressSpace::pint_t)mh_group,
                                          fdeInfo.pcStart, fdeInfo.pcEnd,
                                          fdeInfo.fdeStart);
  } else {
    _LIBUNWIND_DEBUG_LOG("_unw_add_dynamic_fde: bad fde: %s", message);
  }
}

/// IPI: for __deregister_frame()
void _unw_remove_dynamic_fde(unw_word_t fde) {
  // fde is own mh_group
  DwarfFDECache<LocalAddressSpace>::removeAllIn((LocalAddressSpace::pint_t)fde);
}
#endif // defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)
#endif // !defined(__USING_SJLJ_EXCEPTIONS__)



// Add logging hooks in Debug builds only
#ifndef NDEBUG
#include <stdlib.h>

_LIBUNWIND_HIDDEN
bool logAPIs() {
  // do manual lock to avoid use of _cxa_guard_acquire or initializers
  static bool checked = false;
  static bool log = false;
  if (!checked) {
    log = (getenv("LIBUNWIND_PRINT_APIS") != NULL);
    checked = true;
  }
  return log;
}

_LIBUNWIND_HIDDEN
bool logUnwinding() {
  // do manual lock to avoid use of _cxa_guard_acquire or initializers
  static bool checked = false;
  static bool log = false;
  if (!checked) {
    log = (getenv("LIBUNWIND_PRINT_UNWINDING") != NULL);
    checked = true;
  }
  return log;
}

_LIBUNWIND_HIDDEN
bool logDWARF() {
  // do manual lock to avoid use of _cxa_guard_acquire or initializers
  static bool checked = false;
  static bool log = false;
  if (!checked) {
    log = (getenv("LIBUNWIND_PRINT_DWARF") != NULL);
    checked = true;
  }
  return log;
}

#endif // NDEBUG

