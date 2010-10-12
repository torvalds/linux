// Copyright 2010 Tilera Corporation. All Rights Reserved.
//
//   This program is free software; you can redistribute it and/or
//   modify it under the terms of the GNU General Public License
//   as published by the Free Software Foundation, version 2.
//
//   This program is distributed in the hope that it will be useful, but
//   WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
//   NON INFRINGEMENT.  See the GNU General Public License for
//   more details.

//! @file
//!
//! Some low-level simulator definitions.
//!

#ifndef __ARCH_SIM_DEF_H__
#define __ARCH_SIM_DEF_H__


//! Internal: the low bits of the SIM_CONTROL_* SPR values specify
//! the operation to perform, and the remaining bits are
//! an operation-specific parameter (often unused).
//!
#define _SIM_CONTROL_OPERATOR_BITS 8


//== Values which can be written to SPR_SIM_CONTROL.

//! If written to SPR_SIM_CONTROL, stops profiling.
//!
#define SIM_CONTROL_PROFILER_DISABLE 0

//! If written to SPR_SIM_CONTROL, starts profiling.
//!
#define SIM_CONTROL_PROFILER_ENABLE 1

//! If written to SPR_SIM_CONTROL, clears profiling counters.
//!
#define SIM_CONTROL_PROFILER_CLEAR 2

//! If written to SPR_SIM_CONTROL, checkpoints the simulator.
//!
#define SIM_CONTROL_CHECKPOINT 3

//! If written to SPR_SIM_CONTROL, combined with a mask (shifted by 8),
//! sets the tracing mask to the given mask. See "sim_set_tracing()".
//!
#define SIM_CONTROL_SET_TRACING 4

//! If written to SPR_SIM_CONTROL, combined with a mask (shifted by 8),
//! dumps the requested items of machine state to the log.
//!
#define SIM_CONTROL_DUMP 5

//! If written to SPR_SIM_CONTROL, clears chip-level profiling counters.
//!
#define SIM_CONTROL_PROFILER_CHIP_CLEAR 6

//! If written to SPR_SIM_CONTROL, disables chip-level profiling.
//!
#define SIM_CONTROL_PROFILER_CHIP_DISABLE 7

//! If written to SPR_SIM_CONTROL, enables chip-level profiling.
//!
#define SIM_CONTROL_PROFILER_CHIP_ENABLE 8

//! If written to SPR_SIM_CONTROL, enables chip-level functional mode
//!
#define SIM_CONTROL_ENABLE_FUNCTIONAL 9

//! If written to SPR_SIM_CONTROL, disables chip-level functional mode.
//!
#define SIM_CONTROL_DISABLE_FUNCTIONAL 10

//! If written to SPR_SIM_CONTROL, enables chip-level functional mode.
//! All tiles must perform this write for functional mode to be enabled.
//! Ignored in naked boot mode unless --functional is specified.
//! WARNING: Only the hypervisor startup code should use this!
//!
#define SIM_CONTROL_ENABLE_FUNCTIONAL_BARRIER 11

//! If written to SPR_SIM_CONTROL, combined with a character (shifted by 8),
//! writes a string directly to the simulator output.  Written to once for
//! each character in the string, plus a final NUL.  Instead of NUL,
//! you can also use "SIM_PUTC_FLUSH_STRING" or "SIM_PUTC_FLUSH_BINARY".
//!
// ISSUE: Document the meaning of "newline", and the handling of NUL.
//
#define SIM_CONTROL_PUTC 12

//! If written to SPR_SIM_CONTROL, clears the --grind-coherence state for
//! this core.  This is intended to be used before a loop that will
//! invalidate the cache by loading new data and evicting all current data.
//! Generally speaking, this API should only be used by system code.
//!
#define SIM_CONTROL_GRINDER_CLEAR 13

//! If written to SPR_SIM_CONTROL, shuts down the simulator.
//!
#define SIM_CONTROL_SHUTDOWN 14

//! If written to SPR_SIM_CONTROL, combined with a pid (shifted by 8),
//! indicates that a fork syscall just created the given process.
//!
#define SIM_CONTROL_OS_FORK 15

//! If written to SPR_SIM_CONTROL, combined with a pid (shifted by 8),
//! indicates that an exit syscall was just executed by the given process.
//!
#define SIM_CONTROL_OS_EXIT 16

//! If written to SPR_SIM_CONTROL, combined with a pid (shifted by 8),
//! indicates that the OS just switched to the given process.
//!
#define SIM_CONTROL_OS_SWITCH 17

//! If written to SPR_SIM_CONTROL, combined with a character (shifted by 8),
//! indicates that an exec syscall was just executed. Written to once for
//! each character in the executable name, plus a final NUL.
//!
#define SIM_CONTROL_OS_EXEC 18

//! If written to SPR_SIM_CONTROL, combined with a character (shifted by 8),
//! indicates that an interpreter (PT_INTERP) was loaded.  Written to once
//! for each character in "ADDR:PATH", plus a final NUL, where "ADDR" is a
//! hex load address starting with "0x", and "PATH" is the executable name.
//!
#define SIM_CONTROL_OS_INTERP 19

//! If written to SPR_SIM_CONTROL, combined with a character (shifted by 8),
//! indicates that a dll was loaded.  Written to once for each character
//! in "ADDR:PATH", plus a final NUL, where "ADDR" is a hexadecimal load
//! address starting with "0x", and "PATH" is the executable name.
//!
#define SIM_CONTROL_DLOPEN 20

//! If written to SPR_SIM_CONTROL, combined with a character (shifted by 8),
//! indicates that a dll was unloaded.  Written to once for each character
//! in "ADDR", plus a final NUL, where "ADDR" is a hexadecimal load
//! address starting with "0x".
//!
#define SIM_CONTROL_DLCLOSE 21

//! If written to SPR_SIM_CONTROL, combined with a flag (shifted by 8),
//! indicates whether to allow data reads to remotely-cached
//! dirty cache lines to be cached locally without grinder warnings or
//! assertions (used by Linux kernel fast memcpy).
//!
#define SIM_CONTROL_ALLOW_MULTIPLE_CACHING 22

//! If written to SPR_SIM_CONTROL, enables memory tracing.
//!
#define SIM_CONTROL_ENABLE_MEM_LOGGING 23

//! If written to SPR_SIM_CONTROL, disables memory tracing.
//!
#define SIM_CONTROL_DISABLE_MEM_LOGGING 24

//! If written to SPR_SIM_CONTROL, changes the shaping parameters of one of
//! the gbe or xgbe shims. Must specify the shim id, the type, the units, and
//! the rate, as defined in SIM_SHAPING_SPR_ARG.
//!
#define SIM_CONTROL_SHAPING 25

//! If written to SPR_SIM_CONTROL, combined with character (shifted by 8),
//! requests that a simulator command be executed.  Written to once for each
//! character in the command, plus a final NUL.
//!
#define SIM_CONTROL_COMMAND 26

//! If written to SPR_SIM_CONTROL, indicates that the simulated system
//! is panicking, to allow debugging via --debug-on-panic.
//!
#define SIM_CONTROL_PANIC 27

//! If written to SPR_SIM_CONTROL, triggers a simulator syscall.
//! See "sim_syscall()" for more info.
//!
#define SIM_CONTROL_SYSCALL 32

//! If written to SPR_SIM_CONTROL, combined with a pid (shifted by 8),
//! provides the pid that subsequent SIM_CONTROL_OS_FORK writes should
//! use as the pid, rather than the default previous SIM_CONTROL_OS_SWITCH.
//!
#define SIM_CONTROL_OS_FORK_PARENT 33

//! If written to SPR_SIM_CONTROL, combined with a mPIPE shim number
//! (shifted by 8), clears the pending magic data section.  The cleared
//! pending magic data section and any subsequently appended magic bytes
//! will only take effect when the classifier blast programmer is run.
#define SIM_CONTROL_CLEAR_MPIPE_MAGIC_BYTES 34

//! If written to SPR_SIM_CONTROL, combined with a mPIPE shim number
//! (shifted by 8) and a byte of data (shifted by 16), appends that byte
//! to the shim's pending magic data section.  The pending magic data
//! section takes effect when the classifier blast programmer is run.
#define SIM_CONTROL_APPEND_MPIPE_MAGIC_BYTE 35

//! If written to SPR_SIM_CONTROL, combined with a mPIPE shim number
//! (shifted by 8), an enable=1/disable=0 bit (shifted by 16), and a
//! mask of links (shifted by 32), enable or disable the corresponding
//! mPIPE links.
#define SIM_CONTROL_ENABLE_MPIPE_LINK_MAGIC_BYTE 36

//== Syscall numbers for use with "sim_syscall()".

//! Syscall number for sim_add_watchpoint().
//!
#define SIM_SYSCALL_ADD_WATCHPOINT 2

//! Syscall number for sim_remove_watchpoint().
//!
#define SIM_SYSCALL_REMOVE_WATCHPOINT 3

//! Syscall number for sim_query_watchpoint().
//!
#define SIM_SYSCALL_QUERY_WATCHPOINT 4

//! Syscall number that asserts that the cache lines whose 64-bit PA
//! is passed as the second argument to sim_syscall(), and over a
//! range passed as the third argument, are no longer in cache.
//! The simulator raises an error if this is not the case.
//!
#define SIM_SYSCALL_VALIDATE_LINES_EVICTED 5


//== Bit masks which can be shifted by 8, combined with
//== SIM_CONTROL_SET_TRACING, and written to SPR_SIM_CONTROL.

//! @addtogroup arch_sim
//! @{

//! Enable --trace-cycle when passed to simulator_set_tracing().
//!
#define SIM_TRACE_CYCLES          0x01

//! Enable --trace-router when passed to simulator_set_tracing().
//!
#define SIM_TRACE_ROUTER          0x02

//! Enable --trace-register-writes when passed to simulator_set_tracing().
//!
#define SIM_TRACE_REGISTER_WRITES 0x04

//! Enable --trace-disasm when passed to simulator_set_tracing().
//!
#define SIM_TRACE_DISASM          0x08

//! Enable --trace-stall-info when passed to simulator_set_tracing().
//!
#define SIM_TRACE_STALL_INFO      0x10

//! Enable --trace-memory-controller when passed to simulator_set_tracing().
//!
#define SIM_TRACE_MEMORY_CONTROLLER 0x20

//! Enable --trace-l2 when passed to simulator_set_tracing().
//!
#define SIM_TRACE_L2_CACHE 0x40

//! Enable --trace-lines when passed to simulator_set_tracing().
//!
#define SIM_TRACE_LINES 0x80

//! Turn off all tracing when passed to simulator_set_tracing().
//!
#define SIM_TRACE_NONE 0

//! Turn on all tracing when passed to simulator_set_tracing().
//!
#define SIM_TRACE_ALL (-1)

//! @}

//! Computes the value to write to SPR_SIM_CONTROL to set tracing flags.
//!
#define SIM_TRACE_SPR_ARG(mask) \
  (SIM_CONTROL_SET_TRACING | ((mask) << _SIM_CONTROL_OPERATOR_BITS))


//== Bit masks which can be shifted by 8, combined with
//== SIM_CONTROL_DUMP, and written to SPR_SIM_CONTROL.

//! @addtogroup arch_sim
//! @{

//! Dump the general-purpose registers.
//!
#define SIM_DUMP_REGS          0x001

//! Dump the SPRs.
//!
#define SIM_DUMP_SPRS          0x002

//! Dump the ITLB.
//!
#define SIM_DUMP_ITLB          0x004

//! Dump the DTLB.
//!
#define SIM_DUMP_DTLB          0x008

//! Dump the L1 I-cache.
//!
#define SIM_DUMP_L1I           0x010

//! Dump the L1 D-cache.
//!
#define SIM_DUMP_L1D           0x020

//! Dump the L2 cache.
//!
#define SIM_DUMP_L2            0x040

//! Dump the switch registers.
//!
#define SIM_DUMP_SNREGS        0x080

//! Dump the switch ITLB.
//!
#define SIM_DUMP_SNITLB        0x100

//! Dump the switch L1 I-cache.
//!
#define SIM_DUMP_SNL1I         0x200

//! Dump the current backtrace.
//!
#define SIM_DUMP_BACKTRACE     0x400

//! Only dump valid lines in caches.
//!
#define SIM_DUMP_VALID_LINES   0x800

//! Dump everything that is dumpable.
//!
#define SIM_DUMP_ALL (-1 & ~SIM_DUMP_VALID_LINES)

// @}

//! Computes the value to write to SPR_SIM_CONTROL to dump machine state.
//!
#define SIM_DUMP_SPR_ARG(mask) \
  (SIM_CONTROL_DUMP | ((mask) << _SIM_CONTROL_OPERATOR_BITS))


//== Bit masks which can be shifted by 8, combined with
//== SIM_CONTROL_PROFILER_CHIP_xxx, and written to SPR_SIM_CONTROL.

//! @addtogroup arch_sim
//! @{

//! Use with with SIM_PROFILER_CHIP_xxx to control the memory controllers.
//!
#define SIM_CHIP_MEMCTL        0x001

//! Use with with SIM_PROFILER_CHIP_xxx to control the XAUI interface.
//!
#define SIM_CHIP_XAUI          0x002

//! Use with with SIM_PROFILER_CHIP_xxx to control the PCIe interface.
//!
#define SIM_CHIP_PCIE          0x004

//! Use with with SIM_PROFILER_CHIP_xxx to control the MPIPE interface.
//!
#define SIM_CHIP_MPIPE         0x008

//! Reference all chip devices.
//!
#define SIM_CHIP_ALL (-1)

//! @}

//! Computes the value to write to SPR_SIM_CONTROL to clear chip statistics.
//!
#define SIM_PROFILER_CHIP_CLEAR_SPR_ARG(mask) \
  (SIM_CONTROL_PROFILER_CHIP_CLEAR | ((mask) << _SIM_CONTROL_OPERATOR_BITS))

//! Computes the value to write to SPR_SIM_CONTROL to disable chip statistics.
//!
#define SIM_PROFILER_CHIP_DISABLE_SPR_ARG(mask) \
  (SIM_CONTROL_PROFILER_CHIP_DISABLE | ((mask) << _SIM_CONTROL_OPERATOR_BITS))

//! Computes the value to write to SPR_SIM_CONTROL to enable chip statistics.
//!
#define SIM_PROFILER_CHIP_ENABLE_SPR_ARG(mask) \
  (SIM_CONTROL_PROFILER_CHIP_ENABLE | ((mask) << _SIM_CONTROL_OPERATOR_BITS))



// Shim bitrate controls.

//! The number of bits used to store the shim id.
//!
#define SIM_CONTROL_SHAPING_SHIM_ID_BITS 3

//! @addtogroup arch_sim
//! @{

//! Change the gbe 0 bitrate.
//!
#define SIM_CONTROL_SHAPING_GBE_0 0x0

//! Change the gbe 1 bitrate.
//!
#define SIM_CONTROL_SHAPING_GBE_1 0x1

//! Change the gbe 2 bitrate.
//!
#define SIM_CONTROL_SHAPING_GBE_2 0x2

//! Change the gbe 3 bitrate.
//!
#define SIM_CONTROL_SHAPING_GBE_3 0x3

//! Change the xgbe 0 bitrate.
//!
#define SIM_CONTROL_SHAPING_XGBE_0 0x4

//! Change the xgbe 1 bitrate.
//!
#define SIM_CONTROL_SHAPING_XGBE_1 0x5

//! The type of shaping to do.
//!
#define SIM_CONTROL_SHAPING_TYPE_BITS 2

//! Control the multiplier.
//!
#define SIM_CONTROL_SHAPING_MULTIPLIER 0

//! Control the PPS.
//!
#define SIM_CONTROL_SHAPING_PPS 1

//! Control the BPS.
//!
#define SIM_CONTROL_SHAPING_BPS 2

//! The number of bits for the units for the shaping parameter.
//!
#define SIM_CONTROL_SHAPING_UNITS_BITS 2

//! Provide a number in single units.
//!
#define SIM_CONTROL_SHAPING_UNITS_SINGLE 0

//! Provide a number in kilo units.
//!
#define SIM_CONTROL_SHAPING_UNITS_KILO 1

//! Provide a number in mega units.
//!
#define SIM_CONTROL_SHAPING_UNITS_MEGA 2

//! Provide a number in giga units.
//!
#define SIM_CONTROL_SHAPING_UNITS_GIGA 3

// @}

//! How many bits are available for the rate.
//!
#define SIM_CONTROL_SHAPING_RATE_BITS \
  (32 - (_SIM_CONTROL_OPERATOR_BITS + \
         SIM_CONTROL_SHAPING_SHIM_ID_BITS + \
         SIM_CONTROL_SHAPING_TYPE_BITS + \
         SIM_CONTROL_SHAPING_UNITS_BITS))

//! Computes the value to write to SPR_SIM_CONTROL to change a bitrate.
//!
#define SIM_SHAPING_SPR_ARG(shim, type, units, rate) \
  (SIM_CONTROL_SHAPING | \
   ((shim) | \
   ((type) << (SIM_CONTROL_SHAPING_SHIM_ID_BITS)) | \
   ((units) << (SIM_CONTROL_SHAPING_SHIM_ID_BITS + \
                SIM_CONTROL_SHAPING_TYPE_BITS)) | \
   ((rate) << (SIM_CONTROL_SHAPING_SHIM_ID_BITS + \
               SIM_CONTROL_SHAPING_TYPE_BITS + \
               SIM_CONTROL_SHAPING_UNITS_BITS))) << _SIM_CONTROL_OPERATOR_BITS)


//== Values returned when reading SPR_SIM_CONTROL.
// ISSUE: These names should share a longer common prefix.

//! When reading SPR_SIM_CONTROL, the mask of simulator tracing bits
//! (SIM_TRACE_xxx values).
//!
#define SIM_TRACE_FLAG_MASK 0xFFFF

//! When reading SPR_SIM_CONTROL, the mask for whether profiling is enabled.
//!
#define SIM_PROFILER_ENABLED_MASK 0x10000


//== Special arguments for "SIM_CONTROL_PUTC".

//! Flag value for forcing a PUTC string-flush, including
//! coordinate/cycle prefix and newline.
//!
#define SIM_PUTC_FLUSH_STRING 0x100

//! Flag value for forcing a PUTC binary-data-flush, which skips the
//! prefix and does not append a newline.
//!
#define SIM_PUTC_FLUSH_BINARY 0x101


#endif //__ARCH_SIM_DEF_H__
