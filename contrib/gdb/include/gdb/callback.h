/* Remote target system call callback support.
   Copyright 1997 Free Software Foundation, Inc.
   Contributed by Cygnus Solutions.

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

/* This interface isn't intended to be specific to any particular kind
   of remote (hardware, simulator, whatever).  As such, support for it
   (e.g. sim/common/callback.c) should *not* live in the simulator source
   tree, nor should it live in the gdb source tree.  */

/* There are various ways to handle system calls:

   1) Have a simulator intercept the appropriate trap instruction and
   directly perform the system call on behalf of the target program.
   This is the typical way of handling system calls for embedded targets.
   [Handling system calls for embedded targets isn't that much of an
   oxymoron as running compiler testsuites make use of the capability.]

   This method of system call handling is done when STATE_ENVIRONMENT
   is ENVIRONMENT_USER.

   2) Have a simulator emulate the hardware as much as possible.
   If the program running on the real hardware communicates with some sort
   of target manager, one would want to be able to run this program on the
   simulator as well.

   This method of system call handling is done when STATE_ENVIRONMENT
   is ENVIRONMENT_OPERATING.
*/

#ifndef CALLBACK_H
#define CALLBACK_H

/* ??? The reason why we check for va_start here should be documented.  */

#ifndef va_start
#include <ansidecl.h>
#ifdef ANSI_PROTOTYPES
#include <stdarg.h>
#else
#include <varargs.h>
#endif
#endif

/* Mapping of host/target values.  */
/* ??? For debugging purposes, one might want to add a string of the
   name of the symbol.  */

typedef struct {
  int host_val;
  int target_val;
} CB_TARGET_DEFS_MAP;

#define MAX_CALLBACK_FDS 10

/* Forward decl for stat/fstat.  */
struct stat;

typedef struct host_callback_struct host_callback;

struct host_callback_struct 
{
  int (*close) PARAMS ((host_callback *,int));
  int (*get_errno) PARAMS ((host_callback *));
  int (*isatty) PARAMS ((host_callback *, int));
  int (*lseek) PARAMS ((host_callback *, int, long , int));
  int (*open) PARAMS ((host_callback *, const char*, int mode));
  int (*read) PARAMS ((host_callback *,int,  char *, int));
  int (*read_stdin) PARAMS (( host_callback *, char *, int));
  int (*rename) PARAMS ((host_callback *, const char *, const char *));
  int (*system) PARAMS ((host_callback *, const char *));
  long (*time) PARAMS ((host_callback *, long *));
  int (*unlink) PARAMS ((host_callback *, const char *));
  int (*write) PARAMS ((host_callback *,int, const char *, int));
  int (*write_stdout) PARAMS ((host_callback *, const char *, int));
  void (*flush_stdout) PARAMS ((host_callback *));
  int (*write_stderr) PARAMS ((host_callback *, const char *, int));
  void (*flush_stderr) PARAMS ((host_callback *));
  int (*stat) PARAMS ((host_callback *, const char *, struct stat *));
  int (*fstat) PARAMS ((host_callback *, int, struct stat *));
  int (*ftruncate) PARAMS ((host_callback *, int, long));
  int (*truncate) PARAMS ((host_callback *, const char *, long));

  /* When present, call to the client to give it the oportunity to
     poll any io devices for a request to quit (indicated by a nonzero
     return value). */
  int (*poll_quit) PARAMS ((host_callback *));

  /* Used when the target has gone away, so we can close open
     handles and free memory etc etc.  */
  int (*shutdown) PARAMS ((host_callback *));
  int (*init)     PARAMS ((host_callback *));

  /* depreciated, use vprintf_filtered - Talk to the user on a console.  */
  void (*printf_filtered) PARAMS ((host_callback *, const char *, ...));

  /* Talk to the user on a console.  */
  void (*vprintf_filtered) PARAMS ((host_callback *, const char *, va_list));

  /* Same as vprintf_filtered but to stderr.  */
  void (*evprintf_filtered) PARAMS ((host_callback *, const char *, va_list));

  /* Print an error message and "exit".
     In the case of gdb "exiting" means doing a longjmp back to the main
     command loop.  */
  void (*error) PARAMS ((host_callback *, const char *, ...));

  int last_errno;		/* host format */

  int fdmap[MAX_CALLBACK_FDS];
  char fdopen[MAX_CALLBACK_FDS];
  char alwaysopen[MAX_CALLBACK_FDS];

  /* System call numbers.  */
  CB_TARGET_DEFS_MAP *syscall_map;
  /* Errno values.  */
  CB_TARGET_DEFS_MAP *errno_map;
  /* Flags to the open system call.  */
  CB_TARGET_DEFS_MAP *open_map;
  /* Signal numbers.  */
  CB_TARGET_DEFS_MAP *signal_map;
  /* Layout of `stat' struct.
     The format is a series of "name,length" pairs separated by colons.
     Empty space is indicated with a `name' of "space".
     All padding must be explicitly mentioned.
     Lengths are in bytes.  If this needs to be extended to bits,
     use "name.bits".
     Example: "st_dev,4:st_ino,4:st_mode,4:..."  */
  const char *stat_map;

  /* Marker for those wanting to do sanity checks.
     This should remain the last member of this struct to help catch
     miscompilation errors. */
#define HOST_CALLBACK_MAGIC 4705 /* teds constant */
  int magic;
};

extern host_callback default_callback;

/* Canonical versions of system call numbers.
   It's not intended to willy-nilly throw every system call ever heard
   of in here.  Only include those that have an important use.
   ??? One can certainly start a discussion over the ones that are currently
   here, but that will always be true.  */

/* These are used by the ANSI C support of libc.  */
#define	CB_SYS_exit	1
#define	CB_SYS_open	2
#define	CB_SYS_close	3
#define	CB_SYS_read	4
#define	CB_SYS_write	5
#define	CB_SYS_lseek	6
#define	CB_SYS_unlink	7
#define	CB_SYS_getpid	8
#define	CB_SYS_kill	9
#define CB_SYS_fstat    10
/*#define CB_SYS_sbrk	11 - not currently a system call, but reserved.  */

/* ARGV support.  */
#define CB_SYS_argvlen	12
#define CB_SYS_argv	13

/* These are extras added for one reason or another.  */
#define CB_SYS_chdir	14
#define CB_SYS_stat	15
#define CB_SYS_chmod 	16
#define CB_SYS_utime 	17
#define CB_SYS_time 	18

/* Struct use to pass and return information necessary to perform a
   system call.  */
/* FIXME: Need to consider target word size.  */

typedef struct cb_syscall {
  /* The target's value of what system call to perform.  */
  int func;
  /* The arguments to the syscall.  */
  long arg1, arg2, arg3, arg4;

  /* The result.  */
  long result;
  /* Some system calls have two results.  */
  long result2;
  /* The target's errno value, or 0 if success.
     This is converted to the target's value with host_to_target_errno.  */
  int errcode;

  /* Working space to be used by memory read/write callbacks.  */
  PTR p1;
  PTR p2;
  long x1,x2;

  /* Callbacks for reading/writing memory (e.g. for read/write syscalls).
     ??? long or unsigned long might be better to use for the `count'
     argument here.  We mimic sim_{read,write} for now.  Be careful to
     test any changes with -Wall -Werror, mixed signed comparisons
     will get you.  */
  int (*read_mem) PARAMS ((host_callback * /*cb*/, struct cb_syscall * /*sc*/,
			   unsigned long /*taddr*/, char * /*buf*/,
			   int /*bytes*/));
  int (*write_mem) PARAMS ((host_callback * /*cb*/, struct cb_syscall * /*sc*/,
			    unsigned long /*taddr*/, const char * /*buf*/,
			    int /*bytes*/));

  /* For sanity checking, should be last entry.  */
  int magic;
} CB_SYSCALL;

/* Magic number sanity checker.  */
#define CB_SYSCALL_MAGIC 0x12344321

/* Macro to initialize CB_SYSCALL.  Called first, before filling in
   any fields.  */
#define CB_SYSCALL_INIT(sc) \
do { \
  memset ((sc), 0, sizeof (*(sc))); \
  (sc)->magic = CB_SYSCALL_MAGIC; \
} while (0)

/* Return codes for various interface routines.  */

typedef enum {
  CB_RC_OK = 0,
  /* generic error */
  CB_RC_ERR,
  /* either file not found or no read access */
  CB_RC_ACCESS,
  CB_RC_NO_MEM
} CB_RC;

/* Read in target values for system call numbers, errno values, signals.  */
CB_RC cb_read_target_syscall_maps PARAMS ((host_callback *, const char *));

/* Translate target to host syscall function numbers.  */
int cb_target_to_host_syscall PARAMS ((host_callback *, int));

/* Translate host to target errno value.  */
int cb_host_to_target_errno PARAMS ((host_callback *, int));

/* Translate target to host open flags.  */
int cb_target_to_host_open PARAMS ((host_callback *, int));

/* Translate target signal number to host.  */
int cb_target_to_host_signal PARAMS ((host_callback *, int));

/* Translate host signal number to target.  */
int cb_host_to_target_signal PARAMS ((host_callback *, int));

/* Translate host stat struct to target.
   If stat struct ptr is NULL, just compute target stat struct size.
   Result is size of target stat struct or 0 if error.  */
int cb_host_to_target_stat PARAMS ((host_callback *, const struct stat *, PTR));

/* Perform a system call.  */
CB_RC cb_syscall PARAMS ((host_callback *, CB_SYSCALL *));

#endif
