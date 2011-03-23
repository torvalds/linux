/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/**
 * @file
 *
 * Provides an API for controlling the simulator at runtime.
 */

/**
 * @addtogroup arch_sim
 * @{
 *
 * An API for controlling the simulator at runtime.
 *
 * The simulator's behavior can be modified while it is running.
 * For example, human-readable trace output can be enabled and disabled
 * around code of interest.
 *
 * There are two ways to modify simulator behavior:
 * programmatically, by calling various sim_* functions, and
 * interactively, by entering commands like "sim set functional true"
 * at the tile-monitor prompt.  Typing "sim help" at that prompt provides
 * a list of interactive commands.
 *
 * All interactive commands can also be executed programmatically by
 * passing a string to the sim_command function.
 */

#ifndef __ARCH_SIM_H__
#define __ARCH_SIM_H__

#include <arch/sim_def.h>
#include <arch/abi.h>

#ifndef __ASSEMBLER__

#include <arch/spr_def.h>


/**
 * Return true if the current program is running under a simulator,
 * rather than on real hardware.  If running on hardware, other "sim_xxx()"
 * calls have no useful effect.
 */
static inline int
sim_is_simulator(void)
{
  return __insn_mfspr(SPR_SIM_CONTROL) != 0;
}


/**
 * Checkpoint the simulator state to a checkpoint file.
 *
 * The checkpoint file name is either the default or the name specified
 * on the command line with "--checkpoint-file".
 */
static __inline void
sim_checkpoint(void)
{
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_CHECKPOINT);
}


/**
 * Report whether or not various kinds of simulator tracing are enabled.
 *
 * @return The bitwise OR of these values:
 *
 * SIM_TRACE_CYCLES (--trace-cycles),
 * SIM_TRACE_ROUTER (--trace-router),
 * SIM_TRACE_REGISTER_WRITES (--trace-register-writes),
 * SIM_TRACE_DISASM (--trace-disasm),
 * SIM_TRACE_STALL_INFO (--trace-stall-info)
 * SIM_TRACE_MEMORY_CONTROLLER (--trace-memory-controller)
 * SIM_TRACE_L2_CACHE (--trace-l2)
 * SIM_TRACE_LINES (--trace-lines)
 */
static __inline unsigned int
sim_get_tracing(void)
{
  return __insn_mfspr(SPR_SIM_CONTROL) & SIM_TRACE_FLAG_MASK;
}


/**
 * Turn on or off different kinds of simulator tracing.
 *
 * @param mask Either one of these special values:
 *
 * SIM_TRACE_NONE (turns off tracing),
 * SIM_TRACE_ALL (turns on all possible tracing).
 *
 * or the bitwise OR of these values:
 *
 * SIM_TRACE_CYCLES (--trace-cycles),
 * SIM_TRACE_ROUTER (--trace-router),
 * SIM_TRACE_REGISTER_WRITES (--trace-register-writes),
 * SIM_TRACE_DISASM (--trace-disasm),
 * SIM_TRACE_STALL_INFO (--trace-stall-info)
 * SIM_TRACE_MEMORY_CONTROLLER (--trace-memory-controller)
 * SIM_TRACE_L2_CACHE (--trace-l2)
 * SIM_TRACE_LINES (--trace-lines)
 */
static __inline void
sim_set_tracing(unsigned int mask)
{
  __insn_mtspr(SPR_SIM_CONTROL, SIM_TRACE_SPR_ARG(mask));
}


/**
 * Request dumping of different kinds of simulator state.
 *
 * @param mask Either this special value:
 *
 * SIM_DUMP_ALL (dump all known state)
 *
 * or the bitwise OR of these values:
 *
 * SIM_DUMP_REGS (the register file),
 * SIM_DUMP_SPRS (the SPRs),
 * SIM_DUMP_ITLB (the iTLB),
 * SIM_DUMP_DTLB (the dTLB),
 * SIM_DUMP_L1I (the L1 I-cache),
 * SIM_DUMP_L1D (the L1 D-cache),
 * SIM_DUMP_L2 (the L2 cache),
 * SIM_DUMP_SNREGS (the switch register file),
 * SIM_DUMP_SNITLB (the switch iTLB),
 * SIM_DUMP_SNL1I (the switch L1 I-cache),
 * SIM_DUMP_BACKTRACE (the current backtrace)
 */
static __inline void
sim_dump(unsigned int mask)
{
  __insn_mtspr(SPR_SIM_CONTROL, SIM_DUMP_SPR_ARG(mask));
}


/**
 * Print a string to the simulator stdout.
 *
 * @param str The string to be written.
 */
static __inline void
sim_print(const char* str)
{
  for ( ; *str != '\0'; str++)
  {
    __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PUTC |
                 (*str << _SIM_CONTROL_OPERATOR_BITS));
  }
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PUTC |
               (SIM_PUTC_FLUSH_BINARY << _SIM_CONTROL_OPERATOR_BITS));
}


/**
 * Print a string to the simulator stdout.
 *
 * @param str The string to be written (a newline is automatically added).
 */
static __inline void
sim_print_string(const char* str)
{
  for ( ; *str != '\0'; str++)
  {
    __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PUTC |
                 (*str << _SIM_CONTROL_OPERATOR_BITS));
  }
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PUTC |
               (SIM_PUTC_FLUSH_STRING << _SIM_CONTROL_OPERATOR_BITS));
}


/**
 * Execute a simulator command string.
 *
 * Type 'sim help' at the tile-monitor prompt to learn what commands
 * are available.  Note the use of the tile-monitor "sim" command to
 * pass commands to the simulator.
 *
 * The argument to sim_command() does not include the leading "sim"
 * prefix used at the tile-monitor prompt; for example, you might call
 * sim_command("trace disasm").
 */
static __inline void
sim_command(const char* str)
{
  int c;
  do
  {
    c = *str++;
    __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_COMMAND |
                 (c << _SIM_CONTROL_OPERATOR_BITS));
  }
  while (c);
}



#ifndef __DOXYGEN__

/**
 * The underlying implementation of "_sim_syscall()".
 *
 * We use extra "and" instructions to ensure that all the values
 * we are passing to the simulator are actually valid in the registers
 * (i.e. returned from memory) prior to the SIM_CONTROL spr.
 */
static __inline long _sim_syscall0(int val)
{
  long result;
  __asm__ __volatile__ ("mtspr SIM_CONTROL, r0"
                        : "=R00" (result) : "R00" (val));
  return result;
}

static __inline long _sim_syscall1(int val, long arg1)
{
  long result;
  __asm__ __volatile__ ("{ and zero, r1, r1; mtspr SIM_CONTROL, r0 }"
                        : "=R00" (result) : "R00" (val), "R01" (arg1));
  return result;
}

static __inline long _sim_syscall2(int val, long arg1, long arg2)
{
  long result;
  __asm__ __volatile__ ("{ and zero, r1, r2; mtspr SIM_CONTROL, r0 }"
                        : "=R00" (result)
                        : "R00" (val), "R01" (arg1), "R02" (arg2));
  return result;
}

/* Note that _sim_syscall3() and higher are technically at risk of
   receiving an interrupt right before the mtspr bundle, in which case
   the register values for arguments 3 and up may still be in flight
   to the core from a stack frame reload. */

static __inline long _sim_syscall3(int val, long arg1, long arg2, long arg3)
{
  long result;
  __asm__ __volatile__ ("{ and zero, r3, r3 };"
                        "{ and zero, r1, r2; mtspr SIM_CONTROL, r0 }"
                        : "=R00" (result)
                        : "R00" (val), "R01" (arg1), "R02" (arg2),
                          "R03" (arg3));
  return result;
}

static __inline long _sim_syscall4(int val, long arg1, long arg2, long arg3,
                                  long arg4)
{
  long result;
  __asm__ __volatile__ ("{ and zero, r3, r4 };"
                        "{ and zero, r1, r2; mtspr SIM_CONTROL, r0 }"
                        : "=R00" (result)
                        : "R00" (val), "R01" (arg1), "R02" (arg2),
                          "R03" (arg3), "R04" (arg4));
  return result;
}

static __inline long _sim_syscall5(int val, long arg1, long arg2, long arg3,
                                  long arg4, long arg5)
{
  long result;
  __asm__ __volatile__ ("{ and zero, r3, r4; and zero, r5, r5 };"
                        "{ and zero, r1, r2; mtspr SIM_CONTROL, r0 }"
                        : "=R00" (result)
                        : "R00" (val), "R01" (arg1), "R02" (arg2),
                          "R03" (arg3), "R04" (arg4), "R05" (arg5));
  return result;
}

/**
 * Make a special syscall to the simulator itself, if running under
 * simulation. This is used as the implementation of other functions
 * and should not be used outside this file.
 *
 * @param syscall_num The simulator syscall number.
 * @param nr The number of additional arguments provided.
 *
 * @return Varies by syscall.
 */
#define _sim_syscall(syscall_num, nr, args...) \
  _sim_syscall##nr( \
    ((syscall_num) << _SIM_CONTROL_OPERATOR_BITS) | SIM_CONTROL_SYSCALL, \
    ##args)


/* Values for the "access_mask" parameters below. */
#define SIM_WATCHPOINT_READ    1
#define SIM_WATCHPOINT_WRITE   2
#define SIM_WATCHPOINT_EXECUTE 4


static __inline int
sim_add_watchpoint(unsigned int process_id,
                   unsigned long address,
                   unsigned long size,
                   unsigned int access_mask,
                   unsigned long user_data)
{
  return _sim_syscall(SIM_SYSCALL_ADD_WATCHPOINT, 5, process_id,
                     address, size, access_mask, user_data);
}


static __inline int
sim_remove_watchpoint(unsigned int process_id,
                      unsigned long address,
                      unsigned long size,
                      unsigned int access_mask,
                      unsigned long user_data)
{
  return _sim_syscall(SIM_SYSCALL_REMOVE_WATCHPOINT, 5, process_id,
                     address, size, access_mask, user_data);
}


/**
 * Return value from sim_query_watchpoint.
 */
struct SimQueryWatchpointStatus
{
  /**
   * 0 if a watchpoint fired, 1 if no watchpoint fired, or -1 for
   * error (meaning a bad process_id).
   */
  int syscall_status;

  /**
   * The address of the watchpoint that fired (this is the address
   * passed to sim_add_watchpoint, not an address within that range
   * that actually triggered the watchpoint).
   */
  unsigned long address;

  /** The arbitrary user_data installed by sim_add_watchpoint. */
  unsigned long user_data;
};


static __inline struct SimQueryWatchpointStatus
sim_query_watchpoint(unsigned int process_id)
{
  struct SimQueryWatchpointStatus status;
  long val = SIM_CONTROL_SYSCALL |
    (SIM_SYSCALL_QUERY_WATCHPOINT << _SIM_CONTROL_OPERATOR_BITS);
  __asm__ __volatile__ ("{ and zero, r1, r1; mtspr SIM_CONTROL, r0 }"
                        : "=R00" (status.syscall_status),
                          "=R01" (status.address),
                          "=R02" (status.user_data)
                        : "R00" (val), "R01" (process_id));
  return status;
}


/* On the simulator, confirm lines have been evicted everywhere. */
static __inline void
sim_validate_lines_evicted(unsigned long long pa, unsigned long length)
{
#ifdef __LP64__
  _sim_syscall(SIM_SYSCALL_VALIDATE_LINES_EVICTED, 2, pa, length);
#else
  _sim_syscall(SIM_SYSCALL_VALIDATE_LINES_EVICTED, 4,
               0 /* dummy */, (long)(pa), (long)(pa >> 32), length);
#endif
}


/* Return the current CPU speed in cycles per second. */
static __inline long
sim_query_cpu_speed(void)
{
  return _sim_syscall(SIM_SYSCALL_QUERY_CPU_SPEED, 0);
}

#endif /* !__DOXYGEN__ */




/**
 * Modify the shaping parameters of a shim.
 *
 * @param shim The shim to modify. One of:
 *   SIM_CONTROL_SHAPING_GBE_0
 *   SIM_CONTROL_SHAPING_GBE_1
 *   SIM_CONTROL_SHAPING_GBE_2
 *   SIM_CONTROL_SHAPING_GBE_3
 *   SIM_CONTROL_SHAPING_XGBE_0
 *   SIM_CONTROL_SHAPING_XGBE_1
 *
 * @param type The type of shaping. This should be the same type of
 * shaping that is already in place on the shim. One of:
 *   SIM_CONTROL_SHAPING_MULTIPLIER
 *   SIM_CONTROL_SHAPING_PPS
 *   SIM_CONTROL_SHAPING_BPS
 *
 * @param units The magnitude of the rate. One of:
 *   SIM_CONTROL_SHAPING_UNITS_SINGLE
 *   SIM_CONTROL_SHAPING_UNITS_KILO
 *   SIM_CONTROL_SHAPING_UNITS_MEGA
 *   SIM_CONTROL_SHAPING_UNITS_GIGA
 *
 * @param rate The rate to which to change it. This must fit in
 * SIM_CONTROL_SHAPING_RATE_BITS bits or a warning is issued and
 * the shaping is not changed.
 *
 * @return 0 if no problems were detected in the arguments to sim_set_shaping
 * or 1 if problems were detected (for example, rate does not fit in 17 bits).
 */
static __inline int
sim_set_shaping(unsigned shim,
                unsigned type,
                unsigned units,
                unsigned rate)
{
  if ((rate & ~((1 << SIM_CONTROL_SHAPING_RATE_BITS) - 1)) != 0)
    return 1;

  __insn_mtspr(SPR_SIM_CONTROL, SIM_SHAPING_SPR_ARG(shim, type, units, rate));
  return 0;
}

#ifdef __tilegx__

/** Enable a set of mPIPE links.  Pass a -1 link_mask to enable all links. */
static __inline void
sim_enable_mpipe_links(unsigned mpipe, unsigned long link_mask)
{
  __insn_mtspr(SPR_SIM_CONTROL,
               (SIM_CONTROL_ENABLE_MPIPE_LINK_MAGIC_BYTE |
                (mpipe << 8) | (1 << 16) | ((uint_reg_t)link_mask << 32)));
}

/** Disable a set of mPIPE links.  Pass a -1 link_mask to disable all links. */
static __inline void
sim_disable_mpipe_links(unsigned mpipe, unsigned long link_mask)
{
  __insn_mtspr(SPR_SIM_CONTROL,
               (SIM_CONTROL_ENABLE_MPIPE_LINK_MAGIC_BYTE |
                (mpipe << 8) | (0 << 16) | ((uint_reg_t)link_mask << 32)));
}

#endif /* __tilegx__ */


/*
 * An API for changing "functional" mode.
 */

#ifndef __DOXYGEN__

#define sim_enable_functional() \
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_ENABLE_FUNCTIONAL)

#define sim_disable_functional() \
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_DISABLE_FUNCTIONAL)

#endif /* __DOXYGEN__ */


/*
 * Profiler support.
 */

/**
 * Turn profiling on for the current task.
 *
 * Note that this has no effect if run in an environment without
 * profiling support (thus, the proper flags to the simulator must
 * be supplied).
 */
static __inline void
sim_profiler_enable(void)
{
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PROFILER_ENABLE);
}


/** Turn profiling off for the current task. */
static __inline void
sim_profiler_disable(void)
{
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PROFILER_DISABLE);
}


/**
 * Turn profiling on or off for the current task.
 *
 * @param enabled If true, turns on profiling. If false, turns it off.
 *
 * Note that this has no effect if run in an environment without
 * profiling support (thus, the proper flags to the simulator must
 * be supplied).
 */
static __inline void
sim_profiler_set_enabled(int enabled)
{
  int val =
    enabled ? SIM_CONTROL_PROFILER_ENABLE : SIM_CONTROL_PROFILER_DISABLE;
  __insn_mtspr(SPR_SIM_CONTROL, val);
}


/**
 * Return true if and only if profiling is currently enabled
 * for the current task.
 *
 * This returns false even if sim_profiler_enable() was called
 * if the current execution environment does not support profiling.
 */
static __inline int
sim_profiler_is_enabled(void)
{
  return ((__insn_mfspr(SPR_SIM_CONTROL) & SIM_PROFILER_ENABLED_MASK) != 0);
}


/**
 * Reset profiling counters to zero for the current task.
 *
 * Resetting can be done while profiling is enabled.  It does not affect
 * the chip-wide profiling counters.
 */
static __inline void
sim_profiler_clear(void)
{
  __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_PROFILER_CLEAR);
}


/**
 * Enable specified chip-level profiling counters.
 *
 * Does not affect the per-task profiling counters.
 *
 * @param mask Either this special value:
 *
 * SIM_CHIP_ALL (enables all chip-level components).
 *
 * or the bitwise OR of these values:
 *
 * SIM_CHIP_MEMCTL (enable all memory controllers)
 * SIM_CHIP_XAUI (enable all XAUI controllers)
 * SIM_CHIP_MPIPE (enable all MPIPE controllers)
 */
static __inline void
sim_profiler_chip_enable(unsigned int mask)
{
  __insn_mtspr(SPR_SIM_CONTROL, SIM_PROFILER_CHIP_ENABLE_SPR_ARG(mask));
}


/**
 * Disable specified chip-level profiling counters.
 *
 * Does not affect the per-task profiling counters.
 *
 * @param mask Either this special value:
 *
 * SIM_CHIP_ALL (disables all chip-level components).
 *
 * or the bitwise OR of these values:
 *
 * SIM_CHIP_MEMCTL (disable all memory controllers)
 * SIM_CHIP_XAUI (disable all XAUI controllers)
 * SIM_CHIP_MPIPE (disable all MPIPE controllers)
 */
static __inline void
sim_profiler_chip_disable(unsigned int mask)
{
  __insn_mtspr(SPR_SIM_CONTROL, SIM_PROFILER_CHIP_DISABLE_SPR_ARG(mask));
}


/**
 * Reset specified chip-level profiling counters to zero.
 *
 * Does not affect the per-task profiling counters.
 *
 * @param mask Either this special value:
 *
 * SIM_CHIP_ALL (clears all chip-level components).
 *
 * or the bitwise OR of these values:
 *
 * SIM_CHIP_MEMCTL (clear all memory controllers)
 * SIM_CHIP_XAUI (clear all XAUI controllers)
 * SIM_CHIP_MPIPE (clear all MPIPE controllers)
 */
static __inline void
sim_profiler_chip_clear(unsigned int mask)
{
  __insn_mtspr(SPR_SIM_CONTROL, SIM_PROFILER_CHIP_CLEAR_SPR_ARG(mask));
}


/*
 * Event support.
 */

#ifndef __DOXYGEN__

static __inline void
sim_event_begin(unsigned int x)
{
#if defined(__tile__) && !defined(__NO_EVENT_SPR__)
  __insn_mtspr(SPR_EVENT_BEGIN, x);
#endif
}

static __inline void
sim_event_end(unsigned int x)
{
#if defined(__tile__) && !defined(__NO_EVENT_SPR__)
  __insn_mtspr(SPR_EVENT_END, x);
#endif
}

#endif /* !__DOXYGEN__ */

#endif /* !__ASSEMBLER__ */

#endif /* !__ARCH_SIM_H__ */

/** @} */
