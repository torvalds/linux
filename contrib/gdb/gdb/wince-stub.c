/* wince-stub.c -- debugging stub for a Windows CE device

   Copyright 1999, 2000 Free Software Foundation, Inc.
   Contributed by Cygnus Solutions, A Red Hat Company.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without eve nthe implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
 */

/* by Christopher Faylor (cgf@cygnus.com) */

#include <stdarg.h>
#include <windows.h>
#include <winsock.h>
#include "wince-stub.h"

#define MALLOC(n) (void *) LocalAlloc (LMEM_MOVEABLE | LMEM_ZEROINIT, (UINT)(n))
#define REALLOC(s, n) (void *) LocalReAlloc ((HLOCAL)(s), (UINT)(n), LMEM_MOVEABLE)
#define FREE(s) LocalFree ((HLOCAL)(s))

static int skip_next_id = 0;	/* Don't read next API code from socket */

/* v-style interface for handling varying argument list error messages.
   Displays the error message in a dialog box and exits when user clicks
   on OK. */
static void
vstub_error (LPCWSTR fmt, va_list args)
{
  WCHAR buf[4096];
  wvsprintfW (buf, fmt, args);

  MessageBoxW (NULL, buf, L"GDB", MB_ICONERROR);
  WSACleanup ();
  ExitThread (1);
}

/* The standard way to display an error message and exit. */
static void
stub_error (LPCWSTR fmt, ...)
{
  va_list args;
  va_start (args, fmt);
  vstub_error (fmt, args);
}

/* Allocate a limited pool of memory, reallocating over unused
   buffers.  This assumes that there will never be more than four
   "buffers" required which, so far, is a safe assumption. */
static LPVOID
mempool (unsigned int len)
{
  static int outn = -1;
  static LPWSTR outs[4] = {NULL, NULL, NULL, NULL};

  if (++outn >= (sizeof (outs) / sizeof (outs[0])))
    outn = 0;

  /* Allocate space for the converted string, reusing any previously allocated
     space, if applicable. */
  if (outs[outn])
    FREE (outs[outn]);
  outs[outn] = (LPWSTR) MALLOC (len);

  return outs[outn];
}

/* Standard "oh well" can't communicate error.  Someday this might attempt
   synchronization. */
static void
attempt_resync (LPCWSTR huh, int s)
{
  stub_error (L"lost synchronization with host attempting %s.  Error %d", huh, WSAGetLastError ());
}

/* Read arbitrary stuff from a socket. */
static int
sockread (LPCWSTR huh, int s, void *str, size_t n)
{
  for (;;)
    {
      if (recv (s, str, n, 0) == (int) n)
	return n;
      attempt_resync (huh, s);
    }
}

/* Write arbitrary stuff to a socket. */
static int
sockwrite (LPCWSTR huh, int s, const void *str, size_t n)
{
  for (;;)
    {
      if (send (s, str, n, 0) == (int) n)
	return n;
      attempt_resync (huh, s);
    }
}

/* Get a an ID (possibly) and a DWORD from the host gdb.
   Don't bother with the id if the main loop has already
   read it. */
static DWORD
getdword (LPCWSTR huh, int s, gdb_wince_id what_this)
{
  DWORD n;
  gdb_wince_id what;

  if (skip_next_id)
    skip_next_id = 0;
  else
    do
      if (sockread (huh, s, &what, sizeof (what)) != sizeof (what))
	stub_error (L"error getting record type from host - %s.", huh);
    while (what_this != what);

  if (sockread (huh, s, &n, sizeof (n)) != sizeof (n))
    stub_error (L"error getting %s from host.", huh);

  return n;
}

/* Get a an ID (possibly) and a WORD from the host gdb.
   Don't bother with the id if the main loop has already
   read it. */
static WORD
getword (LPCWSTR huh, int s, gdb_wince_id what_this)
{
  WORD n;
  gdb_wince_id what;

  if (skip_next_id)
    skip_next_id = 0;
  else
    do
      if (sockread (huh, s, &what, sizeof (what)) != sizeof (what))
	stub_error (L"error getting record type from host - %s.", huh);
    while (what_this != what);

  if (sockread (huh, s, &n, sizeof (n)) != sizeof (n))
    stub_error (L"error getting %s from host.", huh);

  return n;
}

/* Handy defines for getting various types of values. */
#define gethandle(huh, s, what) (HANDLE) getdword ((huh), (s), (what))
#define getpvoid(huh, s, what) (LPVOID) getdword ((huh), (s), (what))
#define getlen(huh, s, what) (gdb_wince_len) getword ((huh), (s), (what))

/* Get an arbitrary block of memory from the gdb host.  This comes in
   two chunks an id/dword representing the length and the stream of memory
   itself. Returns a pointer, allocated via mempool, to a memory buffer. */
static LPWSTR
getmemory (LPCWSTR huh, int s, gdb_wince_id what, gdb_wince_len *inlen)
{
  LPVOID p;
  gdb_wince_len dummy;

  if (!inlen)
    inlen = &dummy;

  *inlen = getlen (huh, s, what);

  p = mempool ((unsigned int) *inlen);	/* FIXME: check for error */

  if ((gdb_wince_len) sockread (huh, s, p, *inlen) != *inlen)
    stub_error (L"error getting string from host.");

  return p;
}

/* Output an id/dword to the host */
static void
putdword (LPCWSTR huh, int s, gdb_wince_id what, DWORD n)
{
  if (sockwrite (huh, s, &what, sizeof (what)) != sizeof (what))
    stub_error (L"error writing record id for %s to host.", huh);
  if (sockwrite (huh, s, &n, sizeof (n)) != sizeof (n))
    stub_error (L"error writing %s to host.", huh);
}

/* Output an id/word to the host */
static void
putword (LPCWSTR huh, int s, gdb_wince_id what, WORD n)
{
  if (sockwrite (huh, s, &what, sizeof (what)) != sizeof (what))
    stub_error (L"error writing record id for %s to host.", huh);
  if (sockwrite (huh, s, &n, sizeof (n)) != sizeof (n))
    stub_error (L"error writing %s to host.", huh);
}

/* Convenience define for outputting a "gdb_wince_len" type. */
#define putlen(huh, s, what, n) putword ((huh), (s), (what), (gdb_wince_len) (n))

/* Put an arbitrary block of memory to the gdb host.  This comes in
   two chunks an id/dword representing the length and the stream of memory
   itself. */
static void
putmemory (LPCWSTR huh, int s, gdb_wince_id what, const void *mem, gdb_wince_len len)
{
  putlen (huh, s, what, len);
  if (((short) len > 0) && (gdb_wince_len) sockwrite (huh, s, mem, len) != len)
    stub_error (L"error writing memory to host.");
}

/* Output the result of an operation to the host.  If res != 0, sends a block of
   memory starting at mem of len bytes.  If res == 0, sends -GetLastError () and
   avoids sending the mem. */
static void
putresult (LPCWSTR huh, gdb_wince_result res, int s, gdb_wince_id what, const void *mem, gdb_wince_len len)
{
  if (!res)
    len = -(int) GetLastError ();
  putmemory (huh, s, what, mem, len);
}

static HANDLE curproc;		/* Currently unused, but nice for debugging */

/* Emulate CreateProcess.  Returns &pi if no error. */
static void
create_process (int s)
{
  LPWSTR exec_file = getmemory (L"CreateProcess exec_file", s, GDB_CREATEPROCESS, NULL);
  LPWSTR args = getmemory (L"CreateProcess args", s, GDB_CREATEPROCESS, NULL);
  DWORD flags = getdword (L"CreateProcess flags", s, GDB_CREATEPROCESS);
  PROCESS_INFORMATION pi;
  gdb_wince_result res;

  res = CreateProcessW (exec_file,
			args,	/* command line */
			NULL,	/* Security */
			NULL,	/* thread */
			FALSE,	/* inherit handles */
			flags,	/* start flags */
			NULL,
			NULL,	/* current directory */
			NULL,
			&pi);
  putresult (L"CreateProcess", res, s, GDB_CREATEPROCESS, &pi, sizeof (pi));
  curproc = pi.hProcess;
}

/* Emulate TerminateProcess.  Returns return value of TerminateProcess if
   no error.
   *** NOTE:  For some unknown reason, TerminateProcess seems to always return
   an ACCESS_DENIED (on Windows CE???) error.  So, force a TRUE value for now. */
static void
terminate_process (int s)
{
  gdb_wince_result res;
  HANDLE h = gethandle (L"TerminateProcess handle", s, GDB_TERMINATEPROCESS);

  res = TerminateProcess (h, 0) || 1;	/* Doesn't seem to work on SH so default to TRUE */
  putresult (L"Terminate process result", res, s, GDB_TERMINATEPROCESS,
	     &res, sizeof (res));
}

static int stepped = 0;
/* Handle single step instruction.  FIXME: unneded? */
static void
flag_single_step (int s)
{
  stepped = 1;
  skip_next_id = 0;
}

struct skipper
{
  wchar_t *s;
  int nskip;
} skippy[] =
{
  {L"Undefined Instruction:", 1},
  {L"Data Abort:", 2},
  {NULL, 0}
};

static int
skip_message (DEBUG_EVENT *ev)
{
  char s[80];
  DWORD nread;
  struct skipper *skp;
  int nbytes = ev->u.DebugString.nDebugStringLength;

  if (nbytes > sizeof(s))
    nbytes = sizeof(s);

  memset (s, 0, sizeof (s));
  if (!ReadProcessMemory (curproc, ev->u.DebugString.lpDebugStringData,
			  s, nbytes, &nread))
    return 0;

  for (skp = skippy; skp->s != NULL; skp++)
    if (wcsncmp ((wchar_t *) s, skp->s, wcslen (skp->s)) == 0)
      return skp->nskip;

  return 0;
}

/* Emulate WaitForDebugEvent.  Returns the debug event on success. */
static void
wait_for_debug_event (int s)
{
  DWORD ms = getdword (L"WaitForDebugEvent ms", s, GDB_WAITFORDEBUGEVENT);
  gdb_wince_result res;
  DEBUG_EVENT ev;
  static int skip_next = 0;

  for (;;)
    {
      res = WaitForDebugEvent (&ev, ms);

      if (ev.dwDebugEventCode == OUTPUT_DEBUG_STRING_EVENT)
	{
	  if (skip_next)
	    {
	      skip_next--;
	      goto ignore;
	    }
	  if (skip_next = skip_message (&ev))
	    goto ignore;
	}

      putresult (L"WaitForDebugEvent event", res, s, GDB_WAITFORDEBUGEVENT,
		 &ev, sizeof (ev));
      break;

    ignore:
      ContinueDebugEvent (ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
    }

  return;
}

/* Emulate GetThreadContext.  Returns CONTEXT structure on success. */
static void
get_thread_context (int s)
{
  CONTEXT c;
  HANDLE h = gethandle (L"GetThreadContext handle", s, GDB_GETTHREADCONTEXT);
  gdb_wince_result res;

  memset (&c, 0, sizeof (c));
  c.ContextFlags = getdword (L"GetThreadContext flags", s, GDB_GETTHREADCONTEXT);

  res = (gdb_wince_result) GetThreadContext (h, &c);
  putresult (L"GetThreadContext data", res, s, GDB_GETTHREADCONTEXT,
	     &c, sizeof (c));
}

/* Emulate GetThreadContext.  Returns success of SetThreadContext. */
static void
set_thread_context (int s)
{
  gdb_wince_result res;
  HANDLE h = gethandle (L"SetThreadContext handle", s, GDB_SETTHREADCONTEXT);
  LPCONTEXT pc = (LPCONTEXT) getmemory (L"SetThreadContext context", s,
					GDB_SETTHREADCONTEXT, NULL);

  res = SetThreadContext (h, pc);
  putresult (L"SetThreadContext result", res, s, GDB_SETTHREADCONTEXT,
	     &res, sizeof (res));
}

/* Emulate ReadProcessMemory.  Returns memory read on success. */
static void
read_process_memory (int s)
{
  HANDLE h = gethandle (L"ReadProcessMemory handle", s, GDB_READPROCESSMEMORY);
  LPVOID p = getpvoid (L"ReadProcessMemory base", s, GDB_READPROCESSMEMORY);
  gdb_wince_len len = getlen (L"ReadProcessMemory size", s, GDB_READPROCESSMEMORY);
  LPVOID buf = mempool ((unsigned int) len);
  DWORD outlen;
  gdb_wince_result res;

  outlen = 0;
  res = (gdb_wince_result) ReadProcessMemory (h, p, buf, len, &outlen);
  putresult (L"ReadProcessMemory data", res, s, GDB_READPROCESSMEMORY,
	     buf, (gdb_wince_len) outlen);
}

/* Emulate WriteProcessMemory.  Returns WriteProcessMemory success. */
static void
write_process_memory (int s)
{
  HANDLE h = gethandle (L"WriteProcessMemory handle", s, GDB_WRITEPROCESSMEMORY);
  LPVOID p = getpvoid (L"WriteProcessMemory base", s, GDB_WRITEPROCESSMEMORY);
  gdb_wince_len len;
  LPVOID buf = getmemory (L"WriteProcessMemory buf", s, GDB_WRITEPROCESSMEMORY, &len);
  DWORD outlen;
  gdb_wince_result res;

  outlen = 0;
  res = WriteProcessMemory (h, p, buf, (DWORD) len, &outlen);
  putresult (L"WriteProcessMemory data", res, s, GDB_WRITEPROCESSMEMORY,
	     (gdb_wince_len *) & outlen, sizeof (gdb_wince_len));
}

/* Return non-zero to gdb host if given thread is alive. */
static void
thread_alive (int s)
{
  HANDLE h = gethandle (L"ThreadAlive handle", s, GDB_THREADALIVE);
  gdb_wince_result res;

  res = WaitForSingleObject (h, 0) == WAIT_OBJECT_0 ? 1 : 0;
  putresult (L"WriteProcessMemory data", res, s, GDB_THREADALIVE,
	     &res, sizeof (res));
}

/* Emulate SuspendThread.  Returns value returned from SuspendThread. */
static void
suspend_thread (int s)
{
  DWORD res;
  HANDLE h = gethandle (L"SuspendThread handle", s, GDB_SUSPENDTHREAD);
  res = SuspendThread (h);
  putdword (L"SuspendThread result", s, GDB_SUSPENDTHREAD, res);
}

/* Emulate ResumeThread.  Returns value returned from ResumeThread. */
static void
resume_thread (int s)
{
  DWORD res;
  HANDLE h = gethandle (L"ResumeThread handle", s, GDB_RESUMETHREAD);
  res = ResumeThread (h);
  putdword (L"ResumeThread result", s, GDB_RESUMETHREAD, res);
}

/* Emulate ContinueDebugEvent.  Returns ContinueDebugEvent success. */
static void
continue_debug_event (int s)
{
  gdb_wince_result res;
  DWORD pid = getdword (L"ContinueDebugEvent pid", s, GDB_CONTINUEDEBUGEVENT);
  DWORD tid = getdword (L"ContinueDebugEvent tid", s, GDB_CONTINUEDEBUGEVENT);
  DWORD status = getdword (L"ContinueDebugEvent status", s, GDB_CONTINUEDEBUGEVENT);
  res = (gdb_wince_result) ContinueDebugEvent (pid, tid, status);
  putresult (L"ContinueDebugEvent result", res, s, GDB_CONTINUEDEBUGEVENT, &res, sizeof (res));
}

/* Emulate CloseHandle.  Returns CloseHandle success. */
static void
close_handle (int s)
{
  gdb_wince_result res;
  HANDLE h = gethandle (L"CloseHandle handle", s, GDB_CLOSEHANDLE);
  res = (gdb_wince_result) CloseHandle (h);
  putresult (L"CloseHandle result", res, s, GDB_CLOSEHANDLE, &res, sizeof (res));
}

/* Main loop for reading requests from gdb host on the socket. */
static void
dispatch (int s)
{
  gdb_wince_id id;

  /* Continue reading from socket until receive a GDB_STOPSUB. */
  while (sockread (L"Dispatch", s, &id, sizeof (id)) > 0)
    {
      skip_next_id = 1;
      switch (id)
	{
	case GDB_CREATEPROCESS:
	  create_process (s);
	  break;
	case GDB_TERMINATEPROCESS:
	  terminate_process (s);
	  break;
	case GDB_WAITFORDEBUGEVENT:
	  wait_for_debug_event (s);
	  break;
	case GDB_GETTHREADCONTEXT:
	  get_thread_context (s);
	  break;
	case GDB_SETTHREADCONTEXT:
	  set_thread_context (s);
	  break;
	case GDB_READPROCESSMEMORY:
	  read_process_memory (s);
	  break;
	case GDB_WRITEPROCESSMEMORY:
	  write_process_memory (s);
	  break;
	case GDB_THREADALIVE:
	  thread_alive (s);
	  break;
	case GDB_SUSPENDTHREAD:
	  suspend_thread (s);
	  break;
	case GDB_RESUMETHREAD:
	  resume_thread (s);
	  break;
	case GDB_CONTINUEDEBUGEVENT:
	  continue_debug_event (s);
	  break;
	case GDB_CLOSEHANDLE:
	  close_handle (s);
	  break;
	case GDB_STOPSTUB:
	  terminate_process (s);
	  return;
	case GDB_SINGLESTEP:
	  flag_single_step (s);
	  break;
	default:
	  {
	    WCHAR buf[80];
	    wsprintfW (buf, L"Invalid command id received: %d", id);
	    MessageBoxW (NULL, buf, L"GDB", MB_ICONERROR);
	    skip_next_id = 0;
	  }
	}
    }
}

/* The Windows Main entry point */
int WINAPI
WinMain (HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show)
{
  struct hostent *h;
  int s;
  struct WSAData wd;
  struct sockaddr_in sin;
  int tmp;
  LPWSTR whost;
  char host[80];

  whost = wcschr (cmd, L' ');	/* Look for argument. */

  /* If no host is specified, just use default */
  if (whost)
    {
      /* Eat any spaces. */
      while (*whost == L' ' || *whost == L'\t')
	whost++;

      wcstombs (host, whost, 80);	/* Convert from UNICODE to ascii */
    }

  /* Winsock initialization. */
  if (WSAStartup (MAKEWORD (1, 1), &wd))
    stub_error (L"Couldn't initialize WINSOCK.");

  /* If whost was specified, first try it.  If it was not specified or the
     host lookup failed, try the Windows CE magic ppp_peer lookup.  ppp_peer
     is supposed to be the Windows host sitting on the other end of the
     serial cable. */
  if (whost && *whost && (h = gethostbyname (host)) != NULL)
    /* nothing to do */ ;
  else if ((h = gethostbyname ("ppp_peer")) == NULL)
    stub_error (L"Couldn't get IP address of host system.  Error %d", WSAGetLastError ());

  /* Get a socket. */
  if ((s = socket (AF_INET, SOCK_STREAM, 0)) < 0)
    stub_error (L"Couldn't connect to host system. Error %d", WSAGetLastError ());

  /* Allow rapid reuse of the port. */
  tmp = 1;
  setsockopt (s, SOL_SOCKET, SO_REUSEADDR, (char *) &tmp, sizeof (tmp));

  /* Set up the information for connecting to the host gdb process. */
  memset (&sin, 0, sizeof (sin));
  sin.sin_family = h->h_addrtype;
  memcpy (&sin.sin_addr, h->h_addr, h->h_length);
  sin.sin_port = htons (7000);	/* FIXME: This should be configurable */

  /* Connect to host */
  if (connect (s, (struct sockaddr *) &sin, sizeof (sin)) < 0)
    stub_error (L"Couldn't connect to host gdb.");

  /* Read from socket until told to exit. */
  dispatch (s);
  WSACleanup ();
  return 0;
}
