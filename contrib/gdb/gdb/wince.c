/* Target-vector operations for controlling Windows CE child processes, for GDB.
   Copyright 1999, 2000, 2001 Free Software Foundation, Inc.
   Contributed by Cygnus Solutions, A Red Hat Company.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
 */

/* by Christopher Faylor (cgf@cygnus.com) */

/* We assume we're being built with and will be used for cygwin.  */

#ifdef SHx
#undef SH4
#define SH4			/* Just to get all of the CONTEXT defines. */
#endif

#include "defs.h"
#include "frame.h"		/* required by inferior.h */
#include "inferior.h"
#include "target.h"
#include "gdbcore.h"
#include "command.h"
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>

#include <windows.h>
#include <rapi.h>
#include <netdb.h>
#include <cygwin/in.h>
#include <cygwin/socket.h>

#include "buildsym.h"
#include "symfile.h"
#include "objfiles.h"
#include "gdb_string.h"
#include "gdbthread.h"
#include "gdbcmd.h"
#include <sys/param.h>
#include "wince-stub.h"
#include <time.h>
#include "regcache.h"
#ifdef MIPS
#include "mips-tdep.h"
#endif

/* The ui's event loop. */
extern int (*ui_loop_hook) (int signo);

/* If we're not using the old Cygwin header file set, define the
   following which never should have been in the generic Win32 API
   headers in the first place since they were our own invention... */
#ifndef _GNU_H_WINDOWS_H
#define FLAG_TRACE_BIT 0x100
#ifdef CONTEXT_FLOATING_POINT
#define CONTEXT_DEBUGGER0 (CONTEXT_FULL | CONTEXT_FLOATING_POINT)
#else
#define CONTEXT_DEBUGGER0 (CONTEXT_FULL)
#endif
#endif

#ifdef SH4
#define CONTEXT_DEBUGGER ((CONTEXT_DEBUGGER0 & ~(CONTEXT_SH4 | CONTEXT_FLOATING_POINT)) | CONTEXT_SH3)
#else
#define CONTEXT_DEBUGGER CONTEXT_DEBUGGER0
#endif
/* The string sent by cygwin when it processes a signal.
   FIXME: This should be in a cygwin include file. */
#define CYGWIN_SIGNAL_STRING "cygwin: signal"

#define CHECK(x)	check (x, __FILE__,__LINE__)
#define DEBUG_EXEC(x)	if (debug_exec)		printf x
#define DEBUG_EVENTS(x)	if (debug_events)	printf x
#define DEBUG_MEM(x)	if (debug_memory)	printf x
#define DEBUG_EXCEPT(x)	if (debug_exceptions)	printf x

static int connection_initialized = 0;	/* True if we've initialized a RAPI session. */

/* The directory where the stub and executable files are uploaded. */
static const char *remote_directory = "\\gdb";

/* The types automatic upload available. */
static enum
  {
    UPLOAD_ALWAYS = 0,
    UPLOAD_NEWER = 1,
    UPLOAD_NEVER = 2
  }
upload_when = UPLOAD_NEWER;

/* Valid options for 'set remoteupload'.  Note that options
   must track upload_when enum. */
static struct opts
  {
    const char *name;
    int abbrev;
  }
upload_options[3] =
{
  {
    "always", 1
  }
  ,
  {
    "newer", 3
  }
  ,
  {
    "never", 3
  }
};

static char *remote_upload = NULL;	/* Set by set remoteupload */
static int remote_add_host = 0;

/* Forward declaration */
extern struct target_ops child_ops;

static int win32_child_thread_alive (ptid_t);
void child_kill_inferior (void);

static int last_sig = 0;	/* Set if a signal was received from the
				   debugged process */

/* Thread information structure used to track information that is
   not available in gdb's thread structure. */
typedef struct thread_info_struct
  {
    struct thread_info_struct *next;
    DWORD id;
    HANDLE h;
    char *name;
    int suspend_count;
    int stepped;		/* True if stepped. */
    CORE_ADDR step_pc;
    unsigned long step_prev;
    CONTEXT context;
  }
thread_info;

static thread_info thread_head =
{NULL};
static thread_info * thread_rec (DWORD id, int get_context);

/* The process and thread handles for the above context. */

static DEBUG_EVENT current_event;	/* The current debug event from
					   WaitForDebugEvent */
static HANDLE current_process_handle;	/* Currently executing process */
static thread_info *current_thread;	/* Info on currently selected thread */
static thread_info *this_thread;	/* Info on thread returned by wait_for_debug_event */
static DWORD main_thread_id;	/* Thread ID of the main thread */

/* Counts of things. */
static int exception_count = 0;
static int event_count = 0;

/* User options. */
static int debug_exec = 0;	/* show execution */
static int debug_events = 0;	/* show events from kernel */
static int debug_memory = 0;	/* show target memory accesses */
static int debug_exceptions = 0;	/* show target exceptions */

/* An array of offset mappings into a Win32 Context structure.
   This is a one-to-one mapping which is indexed by gdb's register
   numbers.  It retrieves an offset into the context structure where
   the 4 byte register is located.
   An offset value of -1 indicates that Win32 does not provide this
   register in it's CONTEXT structure.  regptr will return zero for this
   register.

   This is used by the regptr function. */
#define context_offset(x) ((int)&(((PCONTEXT)NULL)->x))
static const int mappings[NUM_REGS + 1] =
{
#ifdef __i386__
  context_offset (Eax),
  context_offset (Ecx),
  context_offset (Edx),
  context_offset (Ebx),
  context_offset (Esp),
  context_offset (Ebp),
  context_offset (Esi),
  context_offset (Edi),
  context_offset (Eip),
  context_offset (EFlags),
  context_offset (SegCs),
  context_offset (SegSs),
  context_offset (SegDs),
  context_offset (SegEs),
  context_offset (SegFs),
  context_offset (SegGs),
  context_offset (FloatSave.RegisterArea[0 * 10]),
  context_offset (FloatSave.RegisterArea[1 * 10]),
  context_offset (FloatSave.RegisterArea[2 * 10]),
  context_offset (FloatSave.RegisterArea[3 * 10]),
  context_offset (FloatSave.RegisterArea[4 * 10]),
  context_offset (FloatSave.RegisterArea[5 * 10]),
  context_offset (FloatSave.RegisterArea[6 * 10]),
  context_offset (FloatSave.RegisterArea[7 * 10]),
#elif defined(SHx)
  context_offset (R0),
  context_offset (R1),
  context_offset (R2),
  context_offset (R3),
  context_offset (R4),
  context_offset (R5),
  context_offset (R6),
  context_offset (R7),
  context_offset (R8),
  context_offset (R9),
  context_offset (R10),
  context_offset (R11),
  context_offset (R12),
  context_offset (R13),
  context_offset (R14),
  context_offset (R15),
  context_offset (Fir),
  context_offset (PR),		/* Procedure Register */
  context_offset (GBR),		/* Global Base Register */
  context_offset (MACH),	/* Accumulate */
  context_offset (MACL),	/* Multiply */
  context_offset (Psr),
  context_offset (Fpul),
  context_offset (Fpscr),
  context_offset (FRegs[0]),
  context_offset (FRegs[1]),
  context_offset (FRegs[2]),
  context_offset (FRegs[3]),
  context_offset (FRegs[4]),
  context_offset (FRegs[5]),
  context_offset (FRegs[6]),
  context_offset (FRegs[7]),
  context_offset (FRegs[8]),
  context_offset (FRegs[9]),
  context_offset (FRegs[10]),
  context_offset (FRegs[11]),
  context_offset (FRegs[12]),
  context_offset (FRegs[13]),
  context_offset (FRegs[14]),
  context_offset (FRegs[15]),
  context_offset (xFRegs[0]),
  context_offset (xFRegs[1]),
  context_offset (xFRegs[2]),
  context_offset (xFRegs[3]),
  context_offset (xFRegs[4]),
  context_offset (xFRegs[5]),
  context_offset (xFRegs[6]),
  context_offset (xFRegs[7]),
  context_offset (xFRegs[8]),
  context_offset (xFRegs[9]),
  context_offset (xFRegs[10]),
  context_offset (xFRegs[11]),
  context_offset (xFRegs[12]),
  context_offset (xFRegs[13]),
  context_offset (xFRegs[14]),
  context_offset (xFRegs[15]),
#elif defined(MIPS)
  context_offset (IntZero),
  context_offset (IntAt),
  context_offset (IntV0),
  context_offset (IntV1),
  context_offset (IntA0),
  context_offset (IntA1),
  context_offset (IntA2),
  context_offset (IntA3),
  context_offset (IntT0),
  context_offset (IntT1),
  context_offset (IntT2),
  context_offset (IntT3),
  context_offset (IntT4),
  context_offset (IntT5),
  context_offset (IntT6),
  context_offset (IntT7),
  context_offset (IntS0),
  context_offset (IntS1),
  context_offset (IntS2),
  context_offset (IntS3),
  context_offset (IntS4),
  context_offset (IntS5),
  context_offset (IntS6),
  context_offset (IntS7),
  context_offset (IntT8),
  context_offset (IntT9),
  context_offset (IntK0),
  context_offset (IntK1),
  context_offset (IntGp),
  context_offset (IntSp),
  context_offset (IntS8),
  context_offset (IntRa),
  context_offset (Psr),
  context_offset (IntLo),
  context_offset (IntHi),
  -1,				/* bad */
  -1,				/* cause */
  context_offset (Fir),
  context_offset (FltF0),
  context_offset (FltF1),
  context_offset (FltF2),
  context_offset (FltF3),
  context_offset (FltF4),
  context_offset (FltF5),
  context_offset (FltF6),
  context_offset (FltF7),
  context_offset (FltF8),
  context_offset (FltF9),
  context_offset (FltF10),
  context_offset (FltF11),
  context_offset (FltF12),
  context_offset (FltF13),
  context_offset (FltF14),
  context_offset (FltF15),
  context_offset (FltF16),
  context_offset (FltF17),
  context_offset (FltF18),
  context_offset (FltF19),
  context_offset (FltF20),
  context_offset (FltF21),
  context_offset (FltF22),
  context_offset (FltF23),
  context_offset (FltF24),
  context_offset (FltF25),
  context_offset (FltF26),
  context_offset (FltF27),
  context_offset (FltF28),
  context_offset (FltF29),
  context_offset (FltF30),
  context_offset (FltF31),
  context_offset (Fsr),
  context_offset (Fir),
  -1,				/* fp */
#elif defined(ARM)
  context_offset (R0),
  context_offset (R1),
  context_offset (R2),
  context_offset (R3),
  context_offset (R4),
  context_offset (R5),
  context_offset (R6),
  context_offset (R7),
  context_offset (R8),
  context_offset (R9),
  context_offset (R10),
  context_offset (R11),
  context_offset (R12),
  context_offset (Sp),
  context_offset (Lr),
  context_offset (Pc),
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  context_offset (Psr),
#endif
  -1
};

/* Return a pointer into a CONTEXT field indexed by gdb register number.
   Return a pointer to an address pointing to zero if there is no
   corresponding CONTEXT field for the given register number.
 */
static ULONG *
regptr (LPCONTEXT c, int r)
{
  static ULONG zero = 0;
  ULONG *p;
  if (mappings[r] < 0)
    p = &zero;
  else
    p = (ULONG *) (((char *) c) + mappings[r]);
  return p;
}

/******************** Beginning of stub interface ********************/

/* Stub interface description:

   The Windows CE stub implements a crude RPC.  The hand-held device
   connects to gdb using port 7000.  gdb and the stub then communicate
   using packets where:

   byte 0:              command id (e.g. Create Process)

   byte 1-4:    DWORD

   byte 1-2:    WORD

   byte 1-2:    length
   byte 3-n:    arbitrary memory.

   The interface is deterministic, i.e., if the stub expects a DWORD then
   the gdb server should send a DWORD.
 */

/* Note:  In the functions below, the `huh' parameter is a string passed from the
   function containing a descriptive string concerning the current operation.
   This is used for error reporting.

   The 'what' parameter is a command id as found in wince-stub.h.

   Hopefully, the rest of the parameters are self-explanatory.
 */

static int s;			/* communication socket */

/* v-style interface for handling varying argyment list error messages.
   Displays the error message in a dialog box and exits when user clicks
   on OK. */
static void
vstub_error (LPCSTR fmt, va_list * args)
{
  char buf[4096];
  vsprintf (buf, fmt, args);
  s = -1;
  error ("%s", buf);
}

/* The standard way to display an error message and exit. */
static void
stub_error (LPCSTR fmt,...)
{
  va_list args;
  va_start (args, fmt);
  vstub_error (fmt, args);
}

/* Standard "oh well" can't communicate error.  Someday this might attempt
   synchronization. */
static void
attempt_resync (LPCSTR huh, int s)
{
  stub_error ("lost synchronization with target attempting %s", huh);
}

/* Read arbitrary stuff from a socket. */
static int
sockread (LPCSTR huh, int s, void *str, size_t n)
{
  for (;;)
    {
      if (recv (s, str, n, 0) == n)
	return n;
      attempt_resync (huh, s);
    }
}

/* Write arbitrary stuff to a socket. */
static int
sockwrite (LPCSTR huh, const void *str, size_t n)
{
  for (;;)
    {
      if (send (s, str, n, 0) == n)
	return n;
      attempt_resync (huh, s);
    }
}

/* Output an id/dword to the host */
static void
putdword (LPCSTR huh, gdb_wince_id what, DWORD n)
{
  if (sockwrite (huh, &what, sizeof (what)) != sizeof (what))
    stub_error ("error writing record id to host for %s", huh);
  if (sockwrite (huh, &n, sizeof (n)) != sizeof (n))
    stub_error ("error writing %s to host.", huh);
}

/* Output an id/word to the host */
static void
putword (LPCSTR huh, gdb_wince_id what, WORD n)
{
  if (sockwrite (huh, &what, sizeof (what)) != sizeof (what))
    stub_error ("error writing record id to host for %s", huh);
  if (sockwrite (huh, &n, sizeof (n)) != sizeof (n))
    stub_error ("error writing %s host.", huh);
}

/* Convenience define for outputting a "gdb_wince_len" type. */
#define putlen(huh, what, n) putword((huh), (what), (gdb_wince_len) (n))

/* Put an arbitrary block of memory to the gdb host.  This comes in
   two chunks an id/dword representing the length and the stream of memory
   itself. */
static void
putmemory (LPCSTR huh, gdb_wince_id what, const void *mem, gdb_wince_len len)
{
  putlen (huh, what, len);
  if (((short) len > 0) && sockwrite (huh, mem, len) != len)
    stub_error ("error writing %s to host.", huh);
}

/* Output the result of an operation to the host.  If res != 0, sends a block of
   memory starting at mem of len bytes.  If res == 0, sends -GetLastError () and
   avoids sending the mem. */
static DWORD
getdword (LPCSTR huh, gdb_wince_id what_this)
{
  DWORD n;
  gdb_wince_id what;
  do
    if (sockread (huh, s, &what, sizeof (what)) != sizeof (what))
      stub_error ("error getting record type from host - %s.", huh);
  while (what_this != what);

  if (sockread (huh, s, &n, sizeof (n)) != sizeof (n))
    stub_error ("error getting %s from host.", huh);

  return n;
}

/* Get a an ID (possibly) and a WORD from the host gdb.
   Don't bother with the id if the main loop has already
   read it. */
static WORD
getword (LPCSTR huh, gdb_wince_id what_this)
{
  WORD n;
  gdb_wince_id what;
  do
    if (sockread (huh, s, &what, sizeof (what)) != sizeof (what))
      stub_error ("error getting record type from host - %s.", huh);
  while (what_this != what);

  if (sockread (huh, s, &n, sizeof (n)) != sizeof (n))
    stub_error ("error getting %s from host.", huh);

  return n;
}

/* Handy defines for getting/putting various types of values. */
#define gethandle(huh, what) (HANDLE) getdword ((huh), (what))
#define getpvoid(huh, what) (LPVOID) getdword ((huh), (what))
#define getlen(huh, what) (gdb_wince_len) getword ((huh), (what))
#define puthandle(huh, what, h) putdword ((huh), (what), (DWORD) (h))
#define putpvoid(huh, what, p) putdword ((huh), (what), (DWORD) (p))

/* Retrieve the result of an operation from the stub.  If nbytes < 0) then nbytes
   is actually an error and nothing else follows.  Use SetLastError to remember this.
   if nbytes > 0, retrieve a block of *nbytes into buf.
 */
int
getresult (LPCSTR huh, gdb_wince_id what, LPVOID buf, gdb_wince_len * nbytes)
{
  gdb_wince_len dummy;
  if (nbytes == NULL)
    nbytes = &dummy;

  *nbytes = getlen (huh, what);

  if ((short) *nbytes < 0)
    {
      SetLastError (-(short) *nbytes);
      return 0;
    }

  if ((gdb_wince_len) sockread (huh, s, buf, *nbytes) != *nbytes)
    stub_error ("couldn't read information from wince stub - %s", huh);

  return 1;
}

/* Convert "narrow" string to "wide".  Manipulates a buffer ring of 8
   buffers which hold the translated string.  This is an arbitrary limit
   but it is approximately double the current needs of this module.
 */
LPWSTR
towide (const char *s, gdb_wince_len * out_len)
{
  static int n = -1;
  static LPWSTR outs[8] =
  {NULL /*, NULL, etc. */ };
  gdb_wince_len dummy;

  if (!out_len)
    out_len = &dummy;

  /* First determine the length required to hold the converted string. */
  *out_len = sizeof (WCHAR) * MultiByteToWideChar (CP_ACP, 0, s, -1, NULL, 0);
  if (!*out_len)
    return NULL;		/* The conversion failed */

  if (++n >= (sizeof (outs) / sizeof (outs[0])))
    n = 0;			/* wrap */

  /* Allocate space for the converted string, reusing any previously allocated
     space, if applicable. Note that if outs[n] is NULL, xrealloc will act as
     a malloc (under cygwin, at least).
   */
  outs[n] = (LPWSTR) xrealloc (outs[n], *out_len);
  memset (outs[n], 0, *out_len);
  (void) MultiByteToWideChar (CP_ACP, 0, s, -1, outs[n], *out_len);
  return outs[n];
}

/******************** Emulation routines start here. ********************

  The functions below are modelled after their Win32 counterparts.  They are named
  similarly to Win32 and take exactly the same arguments except where otherwise noted.
  They communicate with the stub on the hand-held device by sending their arguments
  over the socket and waiting for results from the socket.

  There is one universal change.  In cases where a length is expected to be returned
  in a DWORD, we use a gdb_wince_len type instead.  Currently this is an unsigned short
  which is smaller than the standard Win32 DWORD.  This is done to minimize unnecessary
  traffic since the connection to Windows CE can be slow.  To change this, modify the
  typedef in wince-stub.h and change the putlen/getlen macros in this file and in
  the stub.
*/

static int
create_process (LPSTR exec_file, LPSTR args, DWORD flags, PROCESS_INFORMATION * pi)
{
  gdb_wince_len len;
  LPWSTR buf;

  buf = towide (exec_file, &len);
  putmemory ("CreateProcess exec_file", GDB_CREATEPROCESS, buf, len);
  buf = towide (args, &len);
  putmemory ("CreateProcess args", GDB_CREATEPROCESS, buf, len);
  putdword ("CreateProcess flags", GDB_CREATEPROCESS, flags);
  return getresult ("CreateProcess result", GDB_CREATEPROCESS, pi, NULL);
}

/* Emulate TerminateProcess.  Don't bother with the second argument since CE
   ignores it.
 */
static int
terminate_process (HANDLE h)
{
  gdb_wince_result res;
  if (s < 0)
    return 1;
  puthandle ("TerminateProcess handle", GDB_TERMINATEPROCESS, h);
  return getresult ("TerminateProcess result", GDB_TERMINATEPROCESS, &res, NULL);
}

static int
wait_for_debug_event (DEBUG_EVENT * ev, DWORD ms)
{
  if (s < 0)
    return 1;
  putdword ("WaitForDebugEvent ms", GDB_WAITFORDEBUGEVENT, ms);
  return getresult ("WaitForDebugEvent event", GDB_WAITFORDEBUGEVENT, ev, NULL);
}

static int
get_thread_context (HANDLE h, CONTEXT * c)
{
  if (s < 0)
    return 1;
  puthandle ("GetThreadContext handle", GDB_GETTHREADCONTEXT, h);
  putdword ("GetThreadContext flags", GDB_GETTHREADCONTEXT, c->ContextFlags);
  return getresult ("GetThreadContext context", GDB_GETTHREADCONTEXT, c, NULL);
}

static int
set_thread_context (HANDLE h, CONTEXT * c)
{
  gdb_wince_result res;
  if (s < 0)
    return 1;
  puthandle ("SetThreadContext handle", GDB_SETTHREADCONTEXT, h);
  putmemory ("SetThreadContext context", GDB_SETTHREADCONTEXT, c, sizeof (*c));
  return getresult ("SetThreadContext context", GDB_SETTHREADCONTEXT, &res, NULL);
}

static int
read_process_memory (HANDLE h, LPCVOID where, LPVOID buf, gdb_wince_len len, gdb_wince_len * nbytes)
{
  if (s < 0)
    return 1;
  puthandle ("ReadProcessMemory handle", GDB_READPROCESSMEMORY, h);
  putpvoid ("ReadProcessMemory location", GDB_READPROCESSMEMORY, where);
  putlen ("ReadProcessMemory size", GDB_READPROCESSMEMORY, len);

  return getresult ("ReadProcessMemory buf", GDB_READPROCESSMEMORY, buf, nbytes);
}

static int
write_process_memory (HANDLE h, LPCVOID where, LPCVOID buf, gdb_wince_len len, gdb_wince_len * nbytes)
{
  if (s < 0)
    return 1;
  puthandle ("WriteProcessMemory handle", GDB_WRITEPROCESSMEMORY, h);
  putpvoid ("WriteProcessMemory location", GDB_WRITEPROCESSMEMORY, where);
  putmemory ("WriteProcProcessMemory buf", GDB_WRITEPROCESSMEMORY, buf, len);

  return getresult ("WriteProcessMemory result", GDB_WRITEPROCESSMEMORY, nbytes, NULL);
}

static int
remote_read_bytes (CORE_ADDR memaddr, char *myaddr, int len)
{
  gdb_wince_len nbytes;
  if (!read_process_memory (current_process_handle, (LPCVOID) memaddr,
			    (LPVOID) myaddr, len, &nbytes))
    return -1;
  return nbytes;
}

static int
remote_write_bytes (CORE_ADDR memaddr, char *myaddr, int len)
{
  gdb_wince_len nbytes;
  if (!write_process_memory (current_process_handle, (LPCVOID) memaddr,
			     (LPCVOID) myaddr, len, &nbytes))
    return -1;
  return nbytes;
}

/* This is not a standard Win32 function.  It instructs the stub to return TRUE
   if the thread referenced by HANDLE h is alive.
 */
static int
thread_alive (HANDLE h)
{
  gdb_wince_result res;
  if (s < 0)
    return 1;
  puthandle ("ThreadAlive handle", GDB_THREADALIVE, h);
  return getresult ("ThreadAlive result", GDB_THREADALIVE, &res, NULL);
}

static int
suspend_thread (HANDLE h)
{
  if (s < 0)
    return 1;
  puthandle ("SuspendThread handle", GDB_SUSPENDTHREAD, h);
  return (int) getdword ("SuspendThread result", GDB_SUSPENDTHREAD);
}

static int
resume_thread (HANDLE h)
{
  if (s < 0)
    return 1;
  puthandle ("ResumeThread handle", GDB_RESUMETHREAD, h);
  return (int) getdword ("SuspendThread result", GDB_RESUMETHREAD);
}

static int
continue_debug_event (DWORD pid, DWORD tid, DWORD status)
{
  gdb_wince_result res;
  if (s < 0)
    return 0;
  putdword ("ContinueDebugEvent pid", GDB_CONTINUEDEBUGEVENT, pid);
  putdword ("ContinueDebugEvent tid", GDB_CONTINUEDEBUGEVENT, tid);
  putdword ("ContinueDebugEvent status", GDB_CONTINUEDEBUGEVENT, status);
  return getresult ("ContinueDebugEvent result", GDB_CONTINUEDEBUGEVENT, &res, NULL);
}

static int
close_handle (HANDLE h)
{
  gdb_wince_result res;
  if (s < 0)
    return 1;
  puthandle ("CloseHandle handle", GDB_CLOSEHANDLE, h);
  return (int) getresult ("CloseHandle result", GDB_CLOSEHANDLE, &res, NULL);
}

/* This is not a standard Win32 interface.  This function tells the stub
   to terminate.
 */
static void
stop_stub (void)
{
  if (s < 0)
    return;
  (void) putdword ("Stopping gdb stub", GDB_STOPSTUB, 0);
  s = -1;
}

/******************** End of emulation routines. ********************/
/******************** End of stub interface ********************/

#define check_for_step(a, x) (x)

#ifdef MIPS
static void
undoSStep (thread_info * th)
{
  if (th->stepped)
    {
      memory_remove_breakpoint (th->step_pc, (void *) &th->step_prev);
      th->stepped = 0;
    }
}

void
wince_software_single_step (enum target_signal ignore,
			    int insert_breakpoints_p)
{
  unsigned long pc;
  thread_info *th = current_thread;	/* Info on currently selected thread */
  CORE_ADDR mips_next_pc (CORE_ADDR pc);

  if (!insert_breakpoints_p)
    {
      undoSStep (th);
      return;
    }

  th->stepped = 1;
  pc = read_register (PC_REGNUM);
  th->step_pc = mips_next_pc (pc);
  th->step_prev = 0;
  memory_insert_breakpoint (th->step_pc, (void *) &th->step_prev);
  return;
}
#elif SHx
/* Renesas SH architecture instruction encoding masks */

#define COND_BR_MASK   0xff00
#define UCOND_DBR_MASK 0xe000
#define UCOND_RBR_MASK 0xf0df
#define TRAPA_MASK     0xff00

#define COND_DISP      0x00ff
#define UCOND_DISP     0x0fff
#define UCOND_REG      0x0f00

/* Renesas SH instruction opcodes */

#define BF_INSTR       0x8b00
#define BT_INSTR       0x8900
#define BRA_INSTR      0xa000
#define BSR_INSTR      0xb000
#define JMP_INSTR      0x402b
#define JSR_INSTR      0x400b
#define RTS_INSTR      0x000b
#define RTE_INSTR      0x002b
#define TRAPA_INSTR    0xc300
#define SSTEP_INSTR    0xc3ff


#define T_BIT_MASK     0x0001

static CORE_ADDR
sh_get_next_pc (CONTEXT *c)
{
  short *instrMem;
  int displacement;
  int reg;
  unsigned short opcode;

  instrMem = (short *) c->Fir;

  opcode = read_memory_integer ((CORE_ADDR) c->Fir, sizeof (opcode));

  if ((opcode & COND_BR_MASK) == BT_INSTR)
    {
      if (c->Psr & T_BIT_MASK)
	{
	  displacement = (opcode & COND_DISP) << 1;
	  if (displacement & 0x80)
	    displacement |= 0xffffff00;
	  /*
	     * Remember PC points to second instr.
	     * after PC of branch ... so add 4
	   */
	  instrMem = (short *) (c->Fir + displacement + 4);
	}
      else
	instrMem += 1;
    }
  else if ((opcode & COND_BR_MASK) == BF_INSTR)
    {
      if (c->Psr & T_BIT_MASK)
	instrMem += 1;
      else
	{
	  displacement = (opcode & COND_DISP) << 1;
	  if (displacement & 0x80)
	    displacement |= 0xffffff00;
	  /*
	     * Remember PC points to second instr.
	     * after PC of branch ... so add 4
	   */
	  instrMem = (short *) (c->Fir + displacement + 4);
	}
    }
  else if ((opcode & UCOND_DBR_MASK) == BRA_INSTR)
    {
      displacement = (opcode & UCOND_DISP) << 1;
      if (displacement & 0x0800)
	displacement |= 0xfffff000;

      /*
	 * Remember PC points to second instr.
	 * after PC of branch ... so add 4
       */
      instrMem = (short *) (c->Fir + displacement + 4);
    }
  else if ((opcode & UCOND_RBR_MASK) == JSR_INSTR)
    {
      reg = (char) ((opcode & UCOND_REG) >> 8);

      instrMem = (short *) *regptr (c, reg);
    }
  else if (opcode == RTS_INSTR)
    instrMem = (short *) c->PR;
  else if (opcode == RTE_INSTR)
    instrMem = (short *) *regptr (c, 15);
  else if ((opcode & TRAPA_MASK) == TRAPA_INSTR)
    instrMem = (short *) ((opcode & ~TRAPA_MASK) << 2);
  else
    instrMem += 1;

  return (CORE_ADDR) instrMem;
}
/* Single step (in a painstaking fashion) by inspecting the current
   instruction and setting a breakpoint on the "next" instruction
   which would be executed.  This code hails from sh-stub.c.
 */
static void
undoSStep (thread_info * th)
{
  if (th->stepped)
    {
      memory_remove_breakpoint (th->step_pc, (void *) &th->step_prev);
      th->stepped = 0;
    }
  return;
}

/* Single step (in a painstaking fashion) by inspecting the current
   instruction and setting a breakpoint on the "next" instruction
   which would be executed.  This code hails from sh-stub.c.
 */
void
wince_software_single_step (enum target_signal ignore,
			    int insert_breakpoints_p)
{
  thread_info *th = current_thread;	/* Info on currently selected thread */

  if (!insert_breakpoints_p)
    {
      undoSStep (th);
      return;
    }

  th->stepped = 1;
  th->step_pc = sh_get_next_pc (&th->context);
  th->step_prev = 0;
  memory_insert_breakpoint (th->step_pc, (void *) &th->step_prev);
  return;
}
#elif defined (ARM)
#undef check_for_step

static enum target_signal
check_for_step (DEBUG_EVENT *ev, enum target_signal x)
{
  thread_info *th = thread_rec (ev->dwThreadId, 1);

  if (th->stepped &&
      th->step_pc == (CORE_ADDR) ev->u.Exception.ExceptionRecord.ExceptionAddress)
    return TARGET_SIGNAL_TRAP;
  else
    return x;
}

/* Single step (in a painstaking fashion) by inspecting the current
   instruction and setting a breakpoint on the "next" instruction
   which would be executed.  This code hails from sh-stub.c.
 */
static void
undoSStep (thread_info * th)
{
  if (th->stepped)
    {
      memory_remove_breakpoint (th->step_pc, (void *) &th->step_prev);
      th->stepped = 0;
    }
}

void
wince_software_single_step (enum target_signal ignore,
			    int insert_breakpoints_p)
{
  unsigned long pc;
  thread_info *th = current_thread;	/* Info on currently selected thread */
  CORE_ADDR mips_next_pc (CORE_ADDR pc);

  if (!insert_breakpoints_p)
    {
      undoSStep (th);
      return;
    }

  th->stepped = 1;
  pc = read_register (PC_REGNUM);
  th->step_pc = arm_get_next_pc (pc);
  th->step_prev = 0;
  memory_insert_breakpoint (th->step_pc, (void *) &th->step_prev);
  return;
}
#endif

/* Find a thread record given a thread id.
   If get_context then also retrieve the context for this
   thread. */
static thread_info *
thread_rec (DWORD id, int get_context)
{
  thread_info *th;

  for (th = &thread_head; (th = th->next) != NULL;)
    if (th->id == id)
      {
	if (!th->suspend_count && get_context)
	  {
	    if (get_context > 0 && th != this_thread)
	      th->suspend_count = suspend_thread (th->h) + 1;
	    else if (get_context < 0)
	      th->suspend_count = -1;

	    th->context.ContextFlags = CONTEXT_DEBUGGER;
	    get_thread_context (th->h, &th->context);
	  }
	return th;
      }

  return NULL;
}

/* Add a thread to the thread list */
static thread_info *
child_add_thread (DWORD id, HANDLE h)
{
  thread_info *th;

  if ((th = thread_rec (id, FALSE)))
    return th;

  th = (thread_info *) xmalloc (sizeof (*th));
  memset (th, 0, sizeof (*th));
  th->id = id;
  th->h = h;
  th->next = thread_head.next;
  thread_head.next = th;
  add_thread (id);
  return th;
}

/* Clear out any old thread list and reintialize it to a
   pristine state. */
static void
child_init_thread_list (void)
{
  thread_info *th = &thread_head;

  DEBUG_EVENTS (("gdb: child_init_thread_list\n"));
  init_thread_list ();
  while (th->next != NULL)
    {
      thread_info *here = th->next;
      th->next = here->next;
      (void) close_handle (here->h);
      xfree (here);
    }
}

/* Delete a thread from the list of threads */
static void
child_delete_thread (DWORD id)
{
  thread_info *th;

  if (info_verbose)
    printf_unfiltered ("[Deleting %s]\n", target_pid_to_str (id));
  delete_thread (id);

  for (th = &thread_head;
       th->next != NULL && th->next->id != id;
       th = th->next)
    continue;

  if (th->next != NULL)
    {
      thread_info *here = th->next;
      th->next = here->next;
      close_handle (here->h);
      xfree (here);
    }
}

static void
check (BOOL ok, const char *file, int line)
{
  if (!ok)
    printf_filtered ("error return %s:%d was %d\n", file, line, GetLastError ());
}

static void
do_child_fetch_inferior_registers (int r)
{
  if (r >= 0)
    {
      supply_register (r, (char *) regptr (&current_thread->context, r));
    }
  else
    {
      for (r = 0; r < NUM_REGS; r++)
	do_child_fetch_inferior_registers (r);
    }
}

static void
child_fetch_inferior_registers (int r)
{
  current_thread = thread_rec (PIDGET (inferior_ptid), TRUE);
  do_child_fetch_inferior_registers (r);
}

static void
do_child_store_inferior_registers (int r)
{
  if (r >= 0)
    deprecated_read_register_gen (r, ((char *) &current_thread->context) + mappings[r]);
  else
    {
      for (r = 0; r < NUM_REGS; r++)
	do_child_store_inferior_registers (r);
    }
}

/* Store a new register value into the current thread context */
static void
child_store_inferior_registers (int r)
{
  current_thread = thread_rec (PIDGET (inferior_ptid), TRUE);
  do_child_store_inferior_registers (r);
}

/* Wait for child to do something.  Return pid of child, or -1 in case
   of error; store status through argument pointer OURSTATUS.  */

static int
handle_load_dll (void *dummy)
{
  LOAD_DLL_DEBUG_INFO *event = &current_event.u.LoadDll;
  char dll_buf[MAX_PATH + 1];
  char *p, *bufp, *imgp, *dll_name, *dll_basename;
  int len;

  dll_buf[0] = dll_buf[sizeof (dll_buf) - 1] = '\0';
  if (!event->lpImageName)
    return 1;

  len = 0;
  for (bufp = dll_buf, imgp = event->lpImageName;
       bufp < dll_buf + sizeof (dll_buf);
       bufp += 16, imgp += 16)
    {
      gdb_wince_len nbytes = 0;
      (void) read_process_memory (current_process_handle,
				  imgp, bufp, 16, &nbytes);

      if (!nbytes && bufp == dll_buf)
	return 1;		/* couldn't read it */
      for (p = bufp; p < bufp + nbytes; p++)
	{
	  len++;
	  if (*p == '\0')
	    goto out;
	  if (event->fUnicode)
	    p++;
	}
      if (!nbytes)
	break;
    }

out:
  if (!len)
    return 1;
#if 0
  dll_buf[len] = '\0';
#endif
  dll_name = alloca (len);

  if (!dll_name)
    return 1;

  if (!event->fUnicode)
    memcpy (dll_name, dll_buf, len);
  else
    WideCharToMultiByte (CP_ACP, 0, (LPCWSTR) dll_buf, len,
			 dll_name, len, 0, 0);

  while ((p = strchr (dll_name, '\\')))
    *p = '/';

  /* FIXME!! It would be nice to define one symbol which pointed to the
     front of the dll if we can't find any symbols. */

  if (!(dll_basename = strrchr (dll_name, '/')))
    dll_basename = dll_name;
  else
    dll_basename++;

  /* The symbols in a dll are offset by 0x1000, which is the
     the offset from 0 of the first byte in an image - because
     of the file header and the section alignment.

     FIXME: Is this the real reason that we need the 0x1000 ? */

  printf_unfiltered ("%x:%s", event->lpBaseOfDll, dll_name);
  printf_unfiltered ("\n");

  return 1;
}

/* Handle DEBUG_STRING output from child process. */
static void
handle_output_debug_string (struct target_waitstatus *ourstatus)
{
  char p[256];
  char s[255];
  char *q;
  gdb_wince_len nbytes_read;
  gdb_wince_len nbytes = current_event.u.DebugString.nDebugStringLength;

  if (nbytes > 255)
    nbytes = 255;

  memset (p, 0, sizeof (p));
  if (!read_process_memory (current_process_handle,
			    current_event.u.DebugString.lpDebugStringData,
			    &p, nbytes, &nbytes_read)
      || !*p)
    return;

  memset (s, 0, sizeof (s));
  WideCharToMultiByte (CP_ACP, 0, (LPCWSTR) p, (int) nbytes_read, s,
		       sizeof (s) - 1, NULL, NULL);
  q = strchr (s, '\n');
  if (q != NULL)
    {
      *q = '\0';
      if (*--q = '\r')
	*q = '\0';
    }

  warning (s);

  return;
}

/* Handle target exceptions. */
static int
handle_exception (struct target_waitstatus *ourstatus)
{
#if 0
  if (current_event.u.Exception.dwFirstChance)
    return 0;
#endif

  ourstatus->kind = TARGET_WAITKIND_STOPPED;

  switch (current_event.u.Exception.ExceptionRecord.ExceptionCode)
    {
    case EXCEPTION_ACCESS_VIOLATION:
      DEBUG_EXCEPT (("gdb: Target exception ACCESS_VIOLATION at 0x%08x\n",
		     (unsigned) current_event.u.Exception.ExceptionRecord.ExceptionAddress));
      ourstatus->value.sig = TARGET_SIGNAL_SEGV;
      break;
    case STATUS_STACK_OVERFLOW:
      DEBUG_EXCEPT (("gdb: Target exception STACK_OVERFLOW at 0x%08x\n",
		     (unsigned) current_event.u.Exception.ExceptionRecord.ExceptionAddress));
      ourstatus->value.sig = TARGET_SIGNAL_SEGV;
      break;
    case EXCEPTION_BREAKPOINT:
      DEBUG_EXCEPT (("gdb: Target exception BREAKPOINT at 0x%08x\n",
		     (unsigned) current_event.u.Exception.ExceptionRecord.ExceptionAddress));
      ourstatus->value.sig = TARGET_SIGNAL_TRAP;
      break;
    case DBG_CONTROL_C:
      DEBUG_EXCEPT (("gdb: Target exception CONTROL_C at 0x%08x\n",
		     (unsigned) current_event.u.Exception.ExceptionRecord.ExceptionAddress));
      ourstatus->value.sig = TARGET_SIGNAL_INT;
      /* User typed CTRL-C.  Continue with this status */
      last_sig = SIGINT;	/* FIXME - should check pass state */
      break;
    case EXCEPTION_SINGLE_STEP:
      DEBUG_EXCEPT (("gdb: Target exception SINGLE_STEP at 0x%08x\n",
		     (unsigned) current_event.u.Exception.ExceptionRecord.ExceptionAddress));
      ourstatus->value.sig = TARGET_SIGNAL_TRAP;
      break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      DEBUG_EXCEPT (("gdb: Target exception SINGLE_ILL at 0x%08x\n",
	       current_event.u.Exception.ExceptionRecord.ExceptionAddress));
      ourstatus->value.sig = check_for_step (&current_event, TARGET_SIGNAL_ILL);
      break;
    default:
      /* This may be a structured exception handling exception.  In
	 that case, we want to let the program try to handle it, and
	 only break if we see the exception a second time.  */

      printf_unfiltered ("gdb: unknown target exception 0x%08x at 0x%08x\n",
		    current_event.u.Exception.ExceptionRecord.ExceptionCode,
		current_event.u.Exception.ExceptionRecord.ExceptionAddress);
      ourstatus->value.sig = TARGET_SIGNAL_UNKNOWN;
      break;
    }
  exception_count++;
  return 1;
}

/* Resume all artificially suspended threads if we are continuing
   execution */
static BOOL
child_continue (DWORD continue_status, int id)
{
  int i;
  thread_info *th;
  BOOL res;

  DEBUG_EVENTS (("ContinueDebugEvent (cpid=%d, ctid=%d, DBG_CONTINUE);\n",
		 (unsigned) current_event.dwProcessId, (unsigned) current_event.dwThreadId));
  res = continue_debug_event (current_event.dwProcessId,
			      current_event.dwThreadId,
			      continue_status);
  if (res)
    for (th = &thread_head; (th = th->next) != NULL;)
      if (((id == -1) || (id == th->id)) && th->suspend_count)
	{
	  for (i = 0; i < th->suspend_count; i++)
	    (void) resume_thread (th->h);
	  th->suspend_count = 0;
	}

  return res;
}

/* Get the next event from the child.  Return 1 if the event requires
   handling by WFI (or whatever).
 */
static int
get_child_debug_event (int pid, struct target_waitstatus *ourstatus,
		       DWORD target_event_code, int *retval)
{
  int breakout = 0;
  BOOL debug_event;
  DWORD continue_status, event_code;
  thread_info *th = NULL;
  static thread_info dummy_thread_info;

  if (!(debug_event = wait_for_debug_event (&current_event, 1000)))
    {
      *retval = 0;
      goto out;
    }

  event_count++;
  continue_status = DBG_CONTINUE;
  *retval = 0;

  event_code = current_event.dwDebugEventCode;
  breakout = event_code == target_event_code;

  switch (event_code)
    {
    case CREATE_THREAD_DEBUG_EVENT:
      DEBUG_EVENTS (("gdb: kernel event for pid=%d tid=%x code=%s)\n",
		     (unsigned) current_event.dwProcessId,
		     (unsigned) current_event.dwThreadId,
		     "CREATE_THREAD_DEBUG_EVENT"));
      /* Record the existence of this thread */
      th = child_add_thread (current_event.dwThreadId,
			     current_event.u.CreateThread.hThread);
      if (info_verbose)
	printf_unfiltered ("[New %s]\n",
			   target_pid_to_str (current_event.dwThreadId));
      break;

    case EXIT_THREAD_DEBUG_EVENT:
      DEBUG_EVENTS (("gdb: kernel event for pid=%d tid=%d code=%s)\n",
		     (unsigned) current_event.dwProcessId,
		     (unsigned) current_event.dwThreadId,
		     "EXIT_THREAD_DEBUG_EVENT"));
      child_delete_thread (current_event.dwThreadId);
      th = &dummy_thread_info;
      break;

    case CREATE_PROCESS_DEBUG_EVENT:
      DEBUG_EVENTS (("gdb: kernel event for pid=%d tid=%d code=%s)\n",
		     (unsigned) current_event.dwProcessId,
		     (unsigned) current_event.dwThreadId,
		     "CREATE_PROCESS_DEBUG_EVENT"));
      current_process_handle = current_event.u.CreateProcessInfo.hProcess;

      main_thread_id = current_event.dwThreadId;
      inferior_ptid = pid_to_ptid (main_thread_id);
      /* Add the main thread */
      th = child_add_thread (PIDGET (inferior_ptid),
			     current_event.u.CreateProcessInfo.hThread);
      break;

    case EXIT_PROCESS_DEBUG_EVENT:
      DEBUG_EVENTS (("gdb: kernel event for pid=%d tid=%d code=%s)\n",
		     (unsigned) current_event.dwProcessId,
		     (unsigned) current_event.dwThreadId,
		     "EXIT_PROCESS_DEBUG_EVENT"));
      ourstatus->kind = TARGET_WAITKIND_EXITED;
      ourstatus->value.integer = current_event.u.ExitProcess.dwExitCode;
      close_handle (current_process_handle);
      *retval = current_event.dwProcessId;
      breakout = 1;
      break;

    case LOAD_DLL_DEBUG_EVENT:
      DEBUG_EVENTS (("gdb: kernel event for pid=%d tid=%d code=%s)\n",
		     (unsigned) current_event.dwProcessId,
		     (unsigned) current_event.dwThreadId,
		     "LOAD_DLL_DEBUG_EVENT"));
      catch_errors (handle_load_dll, NULL, (char *) "", RETURN_MASK_ALL);
      registers_changed ();	/* mark all regs invalid */
      break;

    case UNLOAD_DLL_DEBUG_EVENT:
      DEBUG_EVENTS (("gdb: kernel event for pid=%d tid=%d code=%s)\n",
		     (unsigned) current_event.dwProcessId,
		     (unsigned) current_event.dwThreadId,
		     "UNLOAD_DLL_DEBUG_EVENT"));
      break;			/* FIXME: don't know what to do here */

    case EXCEPTION_DEBUG_EVENT:
      DEBUG_EVENTS (("gdb: kernel event for pid=%d tid=%d code=%s)\n",
		     (unsigned) current_event.dwProcessId,
		     (unsigned) current_event.dwThreadId,
		     "EXCEPTION_DEBUG_EVENT"));
      if (handle_exception (ourstatus))
	*retval = current_event.dwThreadId;
      else
	{
	  continue_status = DBG_EXCEPTION_NOT_HANDLED;
	  breakout = 0;
	}
      break;

    case OUTPUT_DEBUG_STRING_EVENT:	/* message from the kernel */
      DEBUG_EVENTS (("gdb: kernel event for pid=%d tid=%d code=%s)\n",
		     (unsigned) current_event.dwProcessId,
		     (unsigned) current_event.dwThreadId,
		     "OUTPUT_DEBUG_STRING_EVENT"));
      handle_output_debug_string ( ourstatus);
      break;
    default:
      printf_unfiltered ("gdb: kernel event for pid=%d tid=%d\n",
			 current_event.dwProcessId,
			 current_event.dwThreadId);
      printf_unfiltered ("                 unknown event code %d\n",
			 current_event.dwDebugEventCode);
      break;
    }

  if (breakout)
    this_thread = current_thread = th ?: thread_rec (current_event.dwThreadId, TRUE);
  else
    CHECK (child_continue (continue_status, -1));

out:
  return breakout;
}

/* Wait for interesting events to occur in the target process. */
static ptid_t
child_wait (ptid_t ptid, struct target_waitstatus *ourstatus)
{
  DWORD event_code;
  int retval;
  int pid = PIDGET (ptid);

  /* We loop when we get a non-standard exception rather than return
     with a SPURIOUS because resume can try and step or modify things,
     which needs a current_thread->h.  But some of these exceptions mark
     the birth or death of threads, which mean that the current thread
     isn't necessarily what you think it is. */

  while (1)
    if (get_child_debug_event (pid, ourstatus, EXCEPTION_DEBUG_EVENT, &retval))
      return pid_to_ptid (retval);
    else
      {
	int detach = 0;

	if (ui_loop_hook != NULL)
	  detach = ui_loop_hook (0);

	if (detach)
	  child_kill_inferior ();
      }
}

/* Print status information about what we're accessing.  */

static void
child_files_info (struct target_ops *ignore)
{
  printf_unfiltered ("\tUsing the running image of child %s.\n",
		     target_pid_to_str (inferior_ptid));
}

static void
child_open (char *arg, int from_tty)
{
  error ("Use the \"run\" command to start a child process.");
}

#define FACTOR (0x19db1ded53ea710LL)
#define NSPERSEC 10000000

/* Convert a Win32 time to "UNIX" format. */
long
to_time_t (FILETIME * ptr)
{
  /* A file time is the number of 100ns since jan 1 1601
     stuffed into two long words.
     A time_t is the number of seconds since jan 1 1970.  */

  long rem;
  long long x = ((long long) ptr->dwHighDateTime << 32) + ((unsigned) ptr->dwLowDateTime);
  x -= FACTOR;			/* number of 100ns between 1601 and 1970 */
  rem = x % ((long long) NSPERSEC);
  rem += (NSPERSEC / 2);
  x /= (long long) NSPERSEC;	/* number of 100ns in a second */
  x += (long long) (rem / NSPERSEC);
  return x;
}

/* Upload a file to the remote device depending on the user's
   'set remoteupload' specification. */
char *
upload_to_device (const char *to, const char *from)
{
  HANDLE h;
  const char *dir = remote_directory ?: "\\gdb";
  int len;
  static char *remotefile = NULL;
  LPWSTR wstr;
  char *p;
  DWORD err;
  const char *in_to = to;
  FILETIME crtime, actime, wrtime;
  time_t utime;
  struct stat st;
  int fd;

  /* Look for a path separator and only use trailing part. */
  while ((p = strpbrk (to, "/\\")) != NULL)
    to = p + 1;

  if (!*to)
    error ("no filename found to upload - %s.", in_to);

  len = strlen (dir) + strlen (to) + 2;
  remotefile = (char *) xrealloc (remotefile, len);
  strcpy (remotefile, dir);
  strcat (remotefile, "\\");
  strcat (remotefile, to);

  if (upload_when == UPLOAD_NEVER)
    return remotefile;		/* Don't bother uploading. */

  /* Open the source. */
  if ((fd = openp (getenv ("PATH"), TRUE, (char *) from, O_RDONLY, 0, NULL)) < 0)
    error ("couldn't open %s", from);

  /* Get the time for later comparison. */
  if (fstat (fd, &st))
    st.st_mtime = (time_t) - 1;

  /* Always attempt to create the directory on the remote system. */
  wstr = towide (dir, NULL);
  (void) CeCreateDirectory (wstr, NULL);

  /* Attempt to open the remote file, creating it if it doesn't exist. */
  wstr = towide (remotefile, NULL);
  h = CeCreateFile (wstr, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

  /* Some kind of problem? */
  err = CeGetLastError ();
  if (h == NULL || h == INVALID_HANDLE_VALUE)
    error ("error opening file \"%s\".  Windows error %d.",
	   remotefile, err);

  CeGetFileTime (h, &crtime, &actime, &wrtime);
  utime = to_time_t (&wrtime);
#if 0
  if (utime < st.st_mtime)
    {
      char buf[80];
      strcpy (buf, ctime(&utime));
      printf ("%s < %s\n", buf, ctime(&st.st_mtime));
    }
#endif
  /* See if we need to upload the file. */
  if (upload_when == UPLOAD_ALWAYS ||
      err != ERROR_ALREADY_EXISTS ||
      !CeGetFileTime (h, &crtime, &actime, &wrtime) ||
      to_time_t (&wrtime) < st.st_mtime)
    {
      DWORD nbytes;
      char buf[4096];
      int n;

      /* Upload the file. */
      while ((n = read (fd, buf, sizeof (buf))) > 0)
	if (!CeWriteFile (h, buf, (DWORD) n, &nbytes, NULL))
	  error ("error writing to remote device - %d.",
		 CeGetLastError ());
    }

  close (fd);
  if (!CeCloseHandle (h))
    error ("error closing remote file - %d.", CeGetLastError ());

  return remotefile;
}

/* Initialize the connection to the remote device. */
static void
wince_initialize (void)
{
  int tmp;
  char args[256];
  char *hostname;
  struct sockaddr_in sin;
  char *stub_file_name;
  int s0;
  PROCESS_INFORMATION pi;

  if (!connection_initialized)
    switch (CeRapiInit ())
      {
      case 0:
	connection_initialized = 1;
	break;
      default:
	CeRapiUninit ();
	error ("Can't initialize connection to remote device.\n");
	break;
      }

  /* Upload the stub to the handheld device. */
  stub_file_name = upload_to_device ("wince-stub.exe", WINCE_STUB);
  strcpy (args, stub_file_name);

  if (remote_add_host)
    {
      strcat (args, " ");
      hostname = strchr (args, '\0');
      if (gethostname (hostname, sizeof (args) - strlen (args)))
	error ("couldn't get hostname of this system.");
    }

  /* Get a socket. */
  if ((s0 = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    stub_error ("Couldn't connect to host system.");

  /* Allow rapid reuse of the port. */
  tmp = 1;
  (void) setsockopt (s0, SOL_SOCKET, SO_REUSEADDR, (char *) &tmp, sizeof (tmp));


  /* Set up the information for connecting to the host gdb process. */
  memset (&sin, 0, sizeof (sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons (7000);	/* FIXME: This should be configurable */

  if (bind (s0, (struct sockaddr *) &sin, sizeof (sin)))
    error ("couldn't bind socket");

  if (listen (s0, 1))
    error ("Couldn't open socket for listening.\n");

  /* Start up the stub on the remote device. */
  if (!CeCreateProcess (towide (stub_file_name, NULL), towide (args, NULL),
			NULL, NULL, 0, 0, NULL, NULL, NULL, &pi))
    error ("Unable to start remote stub '%s'.  Windows CE error %d.",
	   stub_file_name, CeGetLastError ());

  /* Wait for a connection */

  if ((s = accept (s0, NULL, NULL)) < 0)
    error ("couldn't set up server for connection.");

  close (s0);
}

/* Start an inferior win32 child process and sets inferior_ptid to its pid.
   EXEC_FILE is the file to run.
   ALLARGS is a string containing the arguments to the program.
   ENV is the environment vector to pass.  Errors reported with error().  */
static void
child_create_inferior (char *exec_file, char *args, char **env)
{
  PROCESS_INFORMATION pi;
  struct target_waitstatus dummy;
  int ret;
  DWORD flags, event_code;
  char *exec_and_args;

  if (!exec_file)
    error ("No executable specified, use `target exec'.\n");

  flags = DEBUG_PROCESS;

  wince_initialize ();		/* Make sure we've got a connection. */

  exec_file = upload_to_device (exec_file, exec_file);

  while (*args == ' ')
    args++;

  /* Allocate space for "command<sp>args" */
  if (*args == '\0')
    {
      exec_and_args = alloca (strlen (exec_file) + 1);
      strcpy (exec_and_args, exec_file);
    }
  else
    {
      exec_and_args = alloca (strlen (exec_file + strlen (args) + 2));
      sprintf (exec_and_args, "%s %s", exec_file, args);
    }

  memset (&pi, 0, sizeof (pi));
  /* Execute the process */
  if (!create_process (exec_file, exec_and_args, flags, &pi))
    error ("Error creating process %s, (error %d)\n", exec_file, GetLastError ());

  exception_count = 0;
  event_count = 0;

  current_process_handle = pi.hProcess;
  current_event.dwProcessId = pi.dwProcessId;
  memset (&current_event, 0, sizeof (current_event));
  current_event.dwThreadId = pi.dwThreadId;
  inferior_ptid = pid_to_ptid (current_event.dwThreadId);
  push_target (&child_ops);
  child_init_thread_list ();
  child_add_thread (pi.dwThreadId, pi.hThread);
  init_wait_for_inferior ();
  clear_proceed_status ();
  target_terminal_init ();
  target_terminal_inferior ();

  /* Run until process and threads are loaded */
  while (!get_child_debug_event (PIDGET (inferior_ptid), &dummy,
				 CREATE_PROCESS_DEBUG_EVENT, &ret))
    continue;

  proceed ((CORE_ADDR) -1, TARGET_SIGNAL_0, 0);
}

/* Chile has gone bye-bye. */
static void
child_mourn_inferior (void)
{
  (void) child_continue (DBG_CONTINUE, -1);
  unpush_target (&child_ops);
  stop_stub ();
  CeRapiUninit ();
  connection_initialized = 0;
  generic_mourn_inferior ();
}

/* Move memory from child to/from gdb. */
int
child_xfer_memory (CORE_ADDR memaddr, char *our, int len, int write,
		   struct mem_attrib *attrib,
		   struct target_ops *target)
{
  if (len <= 0)
    return 0;

  if (write)
    res = remote_write_bytes (memaddr, our, len);
  else
    res = remote_read_bytes (memaddr, our, len);

  return res;
}

/* Terminate the process and wait for child to tell us it has completed. */
void
child_kill_inferior (void)
{
  CHECK (terminate_process (current_process_handle));

  for (;;)
    {
      if (!child_continue (DBG_CONTINUE, -1))
	break;
      if (!wait_for_debug_event (&current_event, INFINITE))
	break;
      if (current_event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT)
	break;
    }

  CHECK (close_handle (current_process_handle));
  close_handle (current_thread->h);
  target_mourn_inferior ();	/* or just child_mourn_inferior? */
}

/* Resume the child after an exception. */
void
child_resume (ptid_t ptid, int step, enum target_signal sig)
{
  thread_info *th;
  DWORD continue_status = last_sig > 0 && last_sig < NSIG ?
  DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE;
  int pid = PIDGET (ptid);

  DEBUG_EXEC (("gdb: child_resume (pid=%d, step=%d, sig=%d);\n",
	       pid, step, sig));

  /* Get context for currently selected thread */
  th = thread_rec (current_event.dwThreadId, FALSE);

  if (th->context.ContextFlags)
    {
      CHECK (set_thread_context (th->h, &th->context));
      th->context.ContextFlags = 0;
    }

  /* Allow continuing with the same signal that interrupted us.
     Otherwise complain. */
  if (sig && sig != last_sig)
    fprintf_unfiltered (gdb_stderr, "Can't send signals to the child.  signal %d\n", sig);

  last_sig = 0;
  child_continue (continue_status, pid);
}

static void
child_prepare_to_store (void)
{
  /* Do nothing, since we can store individual regs */
}

static int
child_can_run (void)
{
  return 1;
}

static void
child_close (void)
{
  DEBUG_EVENTS (("gdb: child_close, inferior_ptid=%d\n",
                PIDGET (inferior_ptid)));
}

/* Explicitly upload file to remotedir */

static void
child_load (char *file, int from_tty)
{
  upload_to_device (file, file);
}

struct target_ops child_ops;

static void
init_child_ops (void)
{
  memset (&child_ops, 0, sizeof (child_ops));
  child_ops.to_shortname = (char *) "child";
  child_ops.to_longname = (char *) "Windows CE process";
  child_ops.to_doc = (char *) "Windows CE process (started by the \"run\" command).";
  child_ops.to_open = child_open;
  child_ops.to_close = child_close;
  child_ops.to_resume = child_resume;
  child_ops.to_wait = child_wait;
  child_ops.to_fetch_registers = child_fetch_inferior_registers;
  child_ops.to_store_registers = child_store_inferior_registers;
  child_ops.to_prepare_to_store = child_prepare_to_store;
  child_ops.to_xfer_memory = child_xfer_memory;
  child_ops.to_files_info = child_files_info;
  child_ops.to_insert_breakpoint = memory_insert_breakpoint;
  child_ops.to_remove_breakpoint = memory_remove_breakpoint;
  child_ops.to_terminal_init = terminal_init_inferior;
  child_ops.to_terminal_inferior = terminal_inferior;
  child_ops.to_terminal_ours_for_output = terminal_ours_for_output;
  child_ops.to_terminal_ours = terminal_ours;
  child_ops.to_terminal_save_ours = terminal_save_ours;
  child_ops.to_terminal_info = child_terminal_info;
  child_ops.to_kill = child_kill_inferior;
  child_ops.to_load = child_load;
  child_ops.to_create_inferior = child_create_inferior;
  child_ops.to_mourn_inferior = child_mourn_inferior;
  child_ops.to_can_run = child_can_run;
  child_ops.to_thread_alive = win32_child_thread_alive;
  child_ops.to_stratum = process_stratum;
  child_ops.to_has_all_memory = 1;
  child_ops.to_has_memory = 1;
  child_ops.to_has_stack = 1;
  child_ops.to_has_registers = 1;
  child_ops.to_has_execution = 1;
  child_ops.to_magic = OPS_MAGIC;
}


/* Handle 'set remoteupload' parameter. */

#define replace_upload(what) \
      upload_when = what; \
      remote_upload = xrealloc (remote_upload, strlen (upload_options[upload_when].name) + 1); \
      strcpy (remote_upload, upload_options[upload_when].name);

static void
set_upload_type (char *ignore, int from_tty)
{
  int i, len;
  char *bad_option;

  if (!remote_upload || !remote_upload[0])
    {
      replace_upload (UPLOAD_NEWER);
      if (from_tty)
	printf_unfiltered ("Upload upload_options are: always, newer, never.\n");
      return;
    }

  len = strlen (remote_upload);
  for (i = 0; i < (sizeof (upload_options) / sizeof (upload_options[0])); i++)
    if (len >= upload_options[i].abbrev &&
	strncasecmp (remote_upload, upload_options[i].name, len) == 0)
      {
	replace_upload (i);
	return;
      }

  bad_option = remote_upload;
  replace_upload (UPLOAD_NEWER);
  error ("Unknown upload type: %s.", bad_option);
}

void
_initialize_wince (void)
{
  struct cmd_list_element *set;
  init_child_ops ();

  add_show_from_set
    (add_set_cmd ((char *) "remotedirectory", no_class,
		  var_string_noescape, (char *) &remote_directory,
		  (char *) "Set directory for remote upload.\n",
		  &setlist),
     &showlist);
  remote_directory = xstrdup (remote_directory);

  set = add_set_cmd ((char *) "remoteupload", no_class,
		     var_string_noescape, (char *) &remote_upload,
	       (char *) "Set how to upload executables to remote device.\n",
		     &setlist);
  add_show_from_set (set, &showlist);
  set_cmd_cfunc (set, set_upload_type);
  set_upload_type (NULL, 0);

  add_show_from_set
    (add_set_cmd ((char *) "debugexec", class_support, var_boolean,
		  (char *) &debug_exec,
	      (char *) "Set whether to display execution in child process.",
		  &setlist),
     &showlist);

  add_show_from_set
    (add_set_cmd ((char *) "remoteaddhost", class_support, var_boolean,
		  (char *) &remote_add_host,
		  (char *) "\
Set whether to add this host to remote stub arguments for\n\
debugging over a network.", &setlist),
     &showlist);

  add_show_from_set
    (add_set_cmd ((char *) "debugevents", class_support, var_boolean,
		  (char *) &debug_events,
	  (char *) "Set whether to display kernel events in child process.",
		  &setlist),
     &showlist);

  add_show_from_set
    (add_set_cmd ((char *) "debugmemory", class_support, var_boolean,
		  (char *) &debug_memory,
	(char *) "Set whether to display memory accesses in child process.",
		  &setlist),
     &showlist);

  add_show_from_set
    (add_set_cmd ((char *) "debugexceptions", class_support, var_boolean,
		  (char *) &debug_exceptions,
      (char *) "Set whether to display kernel exceptions in child process.",
		  &setlist),
     &showlist);

  add_target (&child_ops);
}

/* Determine if the thread referenced by "pid" is alive
   by "polling" it.  If WaitForSingleObject returns WAIT_OBJECT_0
   it means that the pid has died.  Otherwise it is assumed to be alive. */
static int
win32_child_thread_alive (ptid_t ptid)
{
  int pid = PIDGET (ptid);
  return thread_alive (thread_rec (pid, FALSE)->h);
}

/* Convert pid to printable format. */
char *
cygwin_pid_to_str (int pid)
{
  static char buf[80];
  if (pid == current_event.dwProcessId)
    sprintf (buf, "process %d", pid);
  else
    sprintf (buf, "thread %d.0x%x", (unsigned) current_event.dwProcessId, pid);
  return buf;
}
