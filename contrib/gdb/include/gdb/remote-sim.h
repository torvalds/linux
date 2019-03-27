/* This file defines the interface between the simulator and gdb.

   Copyright 1993, 1994, 1996, 1997, 1998, 2000, 2002 Free Software
   Foundation, Inc.

This file is part of GDB.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#if !defined (REMOTE_SIM_H)
#define REMOTE_SIM_H 1

#ifdef __cplusplus
extern "C" {
#endif

/* This file is used when building stand-alone simulators, so isolate this
   file from gdb.  */

/* Pick up CORE_ADDR_TYPE if defined (from gdb), otherwise use same value as
   gdb does (unsigned int - from defs.h).  */

#ifndef CORE_ADDR_TYPE
typedef unsigned int SIM_ADDR;
#else
typedef CORE_ADDR_TYPE SIM_ADDR;
#endif


/* Semi-opaque type used as result of sim_open and passed back to all
   other routines.  "desc" is short for "descriptor".
   It is up to each simulator to define `sim_state'.  */

typedef struct sim_state *SIM_DESC;


/* Values for `kind' arg to sim_open.  */

typedef enum {
  SIM_OPEN_STANDALONE, /* simulator used standalone (run.c) */
  SIM_OPEN_DEBUG       /* simulator used by debugger (gdb) */
} SIM_OPEN_KIND;


/* Return codes from various functions.  */

typedef enum {
  SIM_RC_FAIL = 0,
  SIM_RC_OK = 1
} SIM_RC;


/* The bfd struct, as an opaque type.  */

struct bfd;


/* Main simulator entry points.  */


/* Create a fully initialized simulator instance.

   (This function is called when the simulator is selected from the
   gdb command line.)

   KIND specifies how the simulator shall be used.  Currently there
   are only two kinds: stand-alone and debug.

   CALLBACK specifies a standard host callback (defined in callback.h).

   ABFD, when non NULL, designates a target program.  The program is
   not loaded.

   ARGV is a standard ARGV pointer such as that passed from the
   command line.  The syntax of the argument list is is assumed to be
   ``SIM-PROG { SIM-OPTION } [ TARGET-PROGRAM { TARGET-OPTION } ]''.
   The trailing TARGET-PROGRAM and args are only valid for a
   stand-alone simulator.

   On success, the result is a non NULL descriptor that shall be
   passed to the other sim_foo functions.  While the simulator
   configuration can be parameterized by (in decreasing precedence)
   ARGV's SIM-OPTION, ARGV's TARGET-PROGRAM and the ABFD argument, the
   successful creation of the simulator shall not dependent on the
   presence of any of these arguments/options.

   Hardware simulator: The created simulator shall be sufficiently
   initialized to handle, with out restrictions any client requests
   (including memory reads/writes, register fetch/stores and a
   resume).

   Process simulator: that process is not created until a call to
   sim_create_inferior.  FIXME: What should the state of the simulator
   be? */

SIM_DESC sim_open PARAMS ((SIM_OPEN_KIND kind, struct host_callback_struct *callback, struct bfd *abfd, char **argv));


/* Destory a simulator instance.

   QUITTING is non-zero if we cannot hang on errors.

   This may involve freeing target memory and closing any open files
   and mmap'd areas.  You cannot assume sim_kill has already been
   called. */

void sim_close PARAMS ((SIM_DESC sd, int quitting));


/* Load program PROG into the simulators memory.

   If ABFD is non-NULL, the bfd for the file has already been opened.
   The result is a return code indicating success.

   Hardware simulator: Normally, each program section is written into
   memory according to that sections LMA using physical (direct)
   addressing.  The exception being systems, such as PPC/CHRP, which
   support more complicated program loaders.  A call to this function
   should not effect the state of the processor registers.  Multiple
   calls to this function are permitted and have an accumulative
   effect.

   Process simulator: Calls to this function may be ignored.

   FIXME: Most hardware simulators load the image at the VMA using
   virtual addressing.

   FIXME: For some hardware targets, before a loaded program can be
   executed, it requires the manipulation of VM registers and tables.
   Such manipulation should probably (?) occure in
   sim_create_inferior. */

SIM_RC sim_load PARAMS ((SIM_DESC sd, char *prog, struct bfd *abfd, int from_tty));


/* Prepare to run the simulated program.

   ABFD, if not NULL, provides initial processor state information.
   ARGV and ENV, if non NULL, are NULL terminated lists of pointers.

   Hardware simulator: This function shall initialize the processor
   registers to a known value.  The program counter and possibly stack
   pointer shall be set using information obtained from ABFD (or
   hardware reset defaults).  ARGV and ENV, dependant on the target
   ABI, may be written to memory.

   Process simulator: After a call to this function, a new process
   instance shall exist. The TEXT, DATA, BSS and stack regions shall
   all be initialized, ARGV and ENV shall be written to process
   address space (according to the applicable ABI) and the program
   counter and stack pointer set accordingly. */

SIM_RC sim_create_inferior PARAMS ((SIM_DESC sd, struct bfd *abfd, char **argv, char **env));


/* Fetch LENGTH bytes of the simulated program's memory.  Start fetch
   at virtual address MEM and store in BUF.  Result is number of bytes
   read, or zero if error.  */

int sim_read PARAMS ((SIM_DESC sd, SIM_ADDR mem, unsigned char *buf, int length));


/* Store LENGTH bytes from BUF into the simulated program's
   memory. Store bytes starting at virtual address MEM. Result is
   number of bytes write, or zero if error.  */

int sim_write PARAMS ((SIM_DESC sd, SIM_ADDR mem, unsigned char *buf, int length));


/* Fetch register REGNO storing its raw (target endian) value in the
   LENGTH byte buffer BUF.  Return the actual size of the register or
   zero if REGNO is not applicable.

   Legacy implementations ignore LENGTH and always return -1.

   If LENGTH does not match the size of REGNO no data is transfered
   (the actual register size is still returned). */

int sim_fetch_register PARAMS ((SIM_DESC sd, int regno, unsigned char *buf, int length));


/* Store register REGNO from the raw (target endian) value in BUF.
   Return the actual size of the register or zero if REGNO is not
   applicable.

   Legacy implementations ignore LENGTH and always return -1.

   If LENGTH does not match the size of REGNO no data is transfered
   (the actual register size is still returned). */

int sim_store_register PARAMS ((SIM_DESC sd, int regno, unsigned char *buf, int length));


/* Print whatever statistics the simulator has collected.

   VERBOSE is currently unused and must always be zero.  */

void sim_info PARAMS ((SIM_DESC sd, int verbose));


/* Run (or resume) the simulated program.

   STEP, when non-zero indicates that only a single simulator cycle
   should be emulated.

   SIGGNAL, if non-zero is a (HOST) SIGRC value indicating the type of
   event (hardware interrupt, signal) to be delivered to the simulated
   program.

   Hardware simulator: If the SIGRC value returned by
   sim_stop_reason() is passed back to the simulator via SIGGNAL then
   the hardware simulator shall correctly deliver the hardware event
   indicated by that signal.  If a value of zero is passed in then the
   simulation will continue as if there were no outstanding signal.
   The effect of any other SIGGNAL value is is implementation
   dependant.

   Process simulator: If SIGRC is non-zero then the corresponding
   signal is delivered to the simulated program and execution is then
   continued.  A zero SIGRC value indicates that the program should
   continue as normal. */

void sim_resume PARAMS ((SIM_DESC sd, int step, int siggnal));


/* Asynchronous request to stop the simulation.
   A nonzero return indicates that the simulator is able to handle
   the request */

int sim_stop PARAMS ((SIM_DESC sd));


/* Fetch the REASON why the program stopped.

   SIM_EXITED: The program has terminated. SIGRC indicates the target
   dependant exit status.

   SIM_STOPPED: The program has stopped.  SIGRC uses the host's signal
   numbering as a way of identifying the reaon: program interrupted by
   user via a sim_stop request (SIGINT); a breakpoint instruction
   (SIGTRAP); a completed single step (SIGTRAP); an internal error
   condition (SIGABRT); an illegal instruction (SIGILL); Access to an
   undefined memory region (SIGSEGV); Mis-aligned memory access
   (SIGBUS).  For some signals information in addition to the signal
   number may be retained by the simulator (e.g. offending address),
   that information is not directly accessable via this interface.

   SIM_SIGNALLED: The program has been terminated by a signal. The
   simulator has encountered target code that causes the the program
   to exit with signal SIGRC.

   SIM_RUNNING, SIM_POLLING: The return of one of these values
   indicates a problem internal to the simulator. */

enum sim_stop { sim_running, sim_polling, sim_exited, sim_stopped, sim_signalled };

void sim_stop_reason PARAMS ((SIM_DESC sd, enum sim_stop *reason, int *sigrc));


/* Passthru for other commands that the simulator might support.
   Simulators should be prepared to deal with any combination of NULL
   or empty CMD. */

void sim_do_command PARAMS ((SIM_DESC sd, char *cmd));

#ifdef __cplusplus
}
#endif

#endif /* !defined (REMOTE_SIM_H) */
