/* Target signal translation functions for GDB.
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002 Free Software Foundation, Inc.
   Contributed by Cygnus Support.

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
   Boston, MA 02111-1307, USA.  */

#ifdef GDBSERVER
#include "server.h"
#else
#include "defs.h"
#include "target.h"
#include "gdb_string.h"
#endif

#include <signal.h>

/* Always use __SIGRTMIN if it's available.  SIGRTMIN is the lowest
   _available_ realtime signal, not the lowest supported; glibc takes
   several for its own use.  */

#ifndef REALTIME_LO
# if defined(__SIGRTMIN)
#  define REALTIME_LO __SIGRTMIN
#  define REALTIME_HI __SIGRTMAX
# elif defined(SIGRTMIN)
#  define REALTIME_LO SIGRTMIN
#  define REALTIME_HI SIGRTMAX
# endif
#endif

/* This table must match in order and size the signals in enum target_signal
   in target.h.  */
/* *INDENT-OFF* */
static struct {
  char *name;
  char *string;
  } signals [] =
{
  {"0", "Signal 0"},
  {"SIGHUP", "Hangup"},
  {"SIGINT", "Interrupt"},
  {"SIGQUIT", "Quit"},
  {"SIGILL", "Illegal instruction"},
  {"SIGTRAP", "Trace/breakpoint trap"},
  {"SIGABRT", "Aborted"},
  {"SIGEMT", "Emulation trap"},
  {"SIGFPE", "Arithmetic exception"},
  {"SIGKILL", "Killed"},
  {"SIGBUS", "Bus error"},
  {"SIGSEGV", "Segmentation fault"},
  {"SIGSYS", "Bad system call"},
  {"SIGPIPE", "Broken pipe"},
  {"SIGALRM", "Alarm clock"},
  {"SIGTERM", "Terminated"},
  {"SIGURG", "Urgent I/O condition"},
  {"SIGSTOP", "Stopped (signal)"},
  {"SIGTSTP", "Stopped (user)"},
  {"SIGCONT", "Continued"},
  {"SIGCHLD", "Child status changed"},
  {"SIGTTIN", "Stopped (tty input)"},
  {"SIGTTOU", "Stopped (tty output)"},
  {"SIGIO", "I/O possible"},
  {"SIGXCPU", "CPU time limit exceeded"},
  {"SIGXFSZ", "File size limit exceeded"},
  {"SIGVTALRM", "Virtual timer expired"},
  {"SIGPROF", "Profiling timer expired"},
  {"SIGWINCH", "Window size changed"},
  {"SIGLOST", "Resource lost"},
  {"SIGUSR1", "User defined signal 1"},
  {"SIGUSR2", "User defined signal 2"},
  {"SIGPWR", "Power fail/restart"},
  {"SIGPOLL", "Pollable event occurred"},
  {"SIGWIND", "SIGWIND"},
  {"SIGPHONE", "SIGPHONE"},
  {"SIGWAITING", "Process's LWPs are blocked"},
  {"SIGLWP", "Signal LWP"},
  {"SIGDANGER", "Swap space dangerously low"},
  {"SIGGRANT", "Monitor mode granted"},
  {"SIGRETRACT", "Need to relinquish monitor mode"},
  {"SIGMSG", "Monitor mode data available"},
  {"SIGSOUND", "Sound completed"},
  {"SIGSAK", "Secure attention"},
  {"SIGPRIO", "SIGPRIO"},
  {"SIG33", "Real-time event 33"},
  {"SIG34", "Real-time event 34"},
  {"SIG35", "Real-time event 35"},
  {"SIG36", "Real-time event 36"},
  {"SIG37", "Real-time event 37"},
  {"SIG38", "Real-time event 38"},
  {"SIG39", "Real-time event 39"},
  {"SIG40", "Real-time event 40"},
  {"SIG41", "Real-time event 41"},
  {"SIG42", "Real-time event 42"},
  {"SIG43", "Real-time event 43"},
  {"SIG44", "Real-time event 44"},
  {"SIG45", "Real-time event 45"},
  {"SIG46", "Real-time event 46"},
  {"SIG47", "Real-time event 47"},
  {"SIG48", "Real-time event 48"},
  {"SIG49", "Real-time event 49"},
  {"SIG50", "Real-time event 50"},
  {"SIG51", "Real-time event 51"},
  {"SIG52", "Real-time event 52"},
  {"SIG53", "Real-time event 53"},
  {"SIG54", "Real-time event 54"},
  {"SIG55", "Real-time event 55"},
  {"SIG56", "Real-time event 56"},
  {"SIG57", "Real-time event 57"},
  {"SIG58", "Real-time event 58"},
  {"SIG59", "Real-time event 59"},
  {"SIG60", "Real-time event 60"},
  {"SIG61", "Real-time event 61"},
  {"SIG62", "Real-time event 62"},
  {"SIG63", "Real-time event 63"},
  {"SIGCANCEL", "LWP internal signal"},
  {"SIG32", "Real-time event 32"},
  {"SIG64", "Real-time event 64"},
  {"SIG65", "Real-time event 65"},
  {"SIG66", "Real-time event 66"},
  {"SIG67", "Real-time event 67"},
  {"SIG68", "Real-time event 68"},
  {"SIG69", "Real-time event 69"},
  {"SIG70", "Real-time event 70"},
  {"SIG71", "Real-time event 71"},
  {"SIG72", "Real-time event 72"},
  {"SIG73", "Real-time event 73"},
  {"SIG74", "Real-time event 74"},
  {"SIG75", "Real-time event 75"},
  {"SIG76", "Real-time event 76"},
  {"SIG77", "Real-time event 77"},
  {"SIG78", "Real-time event 78"},
  {"SIG79", "Real-time event 79"},
  {"SIG80", "Real-time event 80"},
  {"SIG81", "Real-time event 81"},
  {"SIG82", "Real-time event 82"},
  {"SIG83", "Real-time event 83"},
  {"SIG84", "Real-time event 84"},
  {"SIG85", "Real-time event 85"},
  {"SIG86", "Real-time event 86"},
  {"SIG87", "Real-time event 87"},
  {"SIG88", "Real-time event 88"},
  {"SIG89", "Real-time event 89"},
  {"SIG90", "Real-time event 90"},
  {"SIG91", "Real-time event 91"},
  {"SIG92", "Real-time event 92"},
  {"SIG93", "Real-time event 93"},
  {"SIG94", "Real-time event 94"},
  {"SIG95", "Real-time event 95"},
  {"SIG96", "Real-time event 96"},
  {"SIG97", "Real-time event 97"},
  {"SIG98", "Real-time event 98"},
  {"SIG99", "Real-time event 99"},
  {"SIG100", "Real-time event 100"},
  {"SIG101", "Real-time event 101"},
  {"SIG102", "Real-time event 102"},
  {"SIG103", "Real-time event 103"},
  {"SIG104", "Real-time event 104"},
  {"SIG105", "Real-time event 105"},
  {"SIG106", "Real-time event 106"},
  {"SIG107", "Real-time event 107"},
  {"SIG108", "Real-time event 108"},
  {"SIG109", "Real-time event 109"},
  {"SIG110", "Real-time event 110"},
  {"SIG111", "Real-time event 111"},
  {"SIG112", "Real-time event 112"},
  {"SIG113", "Real-time event 113"},
  {"SIG114", "Real-time event 114"},
  {"SIG115", "Real-time event 115"},
  {"SIG116", "Real-time event 116"},
  {"SIG117", "Real-time event 117"},
  {"SIG118", "Real-time event 118"},
  {"SIG119", "Real-time event 119"},
  {"SIG120", "Real-time event 120"},
  {"SIG121", "Real-time event 121"},
  {"SIG122", "Real-time event 122"},
  {"SIG123", "Real-time event 123"},
  {"SIG124", "Real-time event 124"},
  {"SIG125", "Real-time event 125"},
  {"SIG126", "Real-time event 126"},
  {"SIG127", "Real-time event 127"},

  {"SIGINFO", "Information request"},

  {NULL, "Unknown signal"},
  {NULL, "Internal error: printing TARGET_SIGNAL_DEFAULT"},

  /* Mach exceptions */
  {"EXC_BAD_ACCESS", "Could not access memory"},
  {"EXC_BAD_INSTRUCTION", "Illegal instruction/operand"},
  {"EXC_ARITHMETIC", "Arithmetic exception"},
  {"EXC_EMULATION", "Emulation instruction"},
  {"EXC_SOFTWARE", "Software generated exception"},
  {"EXC_BREAKPOINT", "Breakpoint"},

  /* Last entry, used to check whether the table is the right size.  */
  {NULL, "TARGET_SIGNAL_MAGIC"}
};
/* *INDENT-ON* */



/* Return the string for a signal.  */
char *
target_signal_to_string (enum target_signal sig)
{
  if ((sig >= TARGET_SIGNAL_FIRST) && (sig <= TARGET_SIGNAL_LAST))
    return signals[sig].string;
  else
    return signals[TARGET_SIGNAL_UNKNOWN].string;
}

/* Return the name for a signal.  */
char *
target_signal_to_name (enum target_signal sig)
{
  if ((sig >= TARGET_SIGNAL_FIRST) && (sig <= TARGET_SIGNAL_LAST)
      && signals[sig].name != NULL)
    return signals[sig].name;
  else
    /* I think the code which prints this will always print it along
       with the string, so no need to be verbose (very old comment).  */
    return "?";
}

/* Given a name, return its signal.  */
enum target_signal
target_signal_from_name (char *name)
{
  enum target_signal sig;

  /* It's possible we also should allow "SIGCLD" as well as "SIGCHLD"
     for TARGET_SIGNAL_SIGCHLD.  SIGIOT, on the other hand, is more
     questionable; seems like by now people should call it SIGABRT
     instead.  */

  /* This ugly cast brought to you by the native VAX compiler.  */
  for (sig = TARGET_SIGNAL_HUP;
       sig < TARGET_SIGNAL_LAST;
       sig = (enum target_signal) ((int) sig + 1))
    if (signals[sig].name != NULL
	&& strcmp (name, signals[sig].name) == 0)
      return sig;
  return TARGET_SIGNAL_UNKNOWN;
}

/* The following functions are to help certain targets deal
   with the signal/waitstatus stuff.  They could just as well be in
   a file called native-utils.c or unixwaitstatus-utils.c or whatever.  */

/* Convert host signal to our signals.  */
enum target_signal
target_signal_from_host (int hostsig)
{
  /* A switch statement would make sense but would require special kludges
     to deal with the cases where more than one signal has the same number.  */

  if (hostsig == 0)
    return TARGET_SIGNAL_0;

#if defined (SIGHUP)
  if (hostsig == SIGHUP)
    return TARGET_SIGNAL_HUP;
#endif
#if defined (SIGINT)
  if (hostsig == SIGINT)
    return TARGET_SIGNAL_INT;
#endif
#if defined (SIGQUIT)
  if (hostsig == SIGQUIT)
    return TARGET_SIGNAL_QUIT;
#endif
#if defined (SIGILL)
  if (hostsig == SIGILL)
    return TARGET_SIGNAL_ILL;
#endif
#if defined (SIGTRAP)
  if (hostsig == SIGTRAP)
    return TARGET_SIGNAL_TRAP;
#endif
#if defined (SIGABRT)
  if (hostsig == SIGABRT)
    return TARGET_SIGNAL_ABRT;
#endif
#if defined (SIGEMT)
  if (hostsig == SIGEMT)
    return TARGET_SIGNAL_EMT;
#endif
#if defined (SIGFPE)
  if (hostsig == SIGFPE)
    return TARGET_SIGNAL_FPE;
#endif
#if defined (SIGKILL)
  if (hostsig == SIGKILL)
    return TARGET_SIGNAL_KILL;
#endif
#if defined (SIGBUS)
  if (hostsig == SIGBUS)
    return TARGET_SIGNAL_BUS;
#endif
#if defined (SIGSEGV)
  if (hostsig == SIGSEGV)
    return TARGET_SIGNAL_SEGV;
#endif
#if defined (SIGSYS)
  if (hostsig == SIGSYS)
    return TARGET_SIGNAL_SYS;
#endif
#if defined (SIGPIPE)
  if (hostsig == SIGPIPE)
    return TARGET_SIGNAL_PIPE;
#endif
#if defined (SIGALRM)
  if (hostsig == SIGALRM)
    return TARGET_SIGNAL_ALRM;
#endif
#if defined (SIGTERM)
  if (hostsig == SIGTERM)
    return TARGET_SIGNAL_TERM;
#endif
#if defined (SIGUSR1)
  if (hostsig == SIGUSR1)
    return TARGET_SIGNAL_USR1;
#endif
#if defined (SIGUSR2)
  if (hostsig == SIGUSR2)
    return TARGET_SIGNAL_USR2;
#endif
#if defined (SIGCLD)
  if (hostsig == SIGCLD)
    return TARGET_SIGNAL_CHLD;
#endif
#if defined (SIGCHLD)
  if (hostsig == SIGCHLD)
    return TARGET_SIGNAL_CHLD;
#endif
#if defined (SIGPWR)
  if (hostsig == SIGPWR)
    return TARGET_SIGNAL_PWR;
#endif
#if defined (SIGWINCH)
  if (hostsig == SIGWINCH)
    return TARGET_SIGNAL_WINCH;
#endif
#if defined (SIGURG)
  if (hostsig == SIGURG)
    return TARGET_SIGNAL_URG;
#endif
#if defined (SIGIO)
  if (hostsig == SIGIO)
    return TARGET_SIGNAL_IO;
#endif
#if defined (SIGPOLL)
  if (hostsig == SIGPOLL)
    return TARGET_SIGNAL_POLL;
#endif
#if defined (SIGSTOP)
  if (hostsig == SIGSTOP)
    return TARGET_SIGNAL_STOP;
#endif
#if defined (SIGTSTP)
  if (hostsig == SIGTSTP)
    return TARGET_SIGNAL_TSTP;
#endif
#if defined (SIGCONT)
  if (hostsig == SIGCONT)
    return TARGET_SIGNAL_CONT;
#endif
#if defined (SIGTTIN)
  if (hostsig == SIGTTIN)
    return TARGET_SIGNAL_TTIN;
#endif
#if defined (SIGTTOU)
  if (hostsig == SIGTTOU)
    return TARGET_SIGNAL_TTOU;
#endif
#if defined (SIGVTALRM)
  if (hostsig == SIGVTALRM)
    return TARGET_SIGNAL_VTALRM;
#endif
#if defined (SIGPROF)
  if (hostsig == SIGPROF)
    return TARGET_SIGNAL_PROF;
#endif
#if defined (SIGXCPU)
  if (hostsig == SIGXCPU)
    return TARGET_SIGNAL_XCPU;
#endif
#if defined (SIGXFSZ)
  if (hostsig == SIGXFSZ)
    return TARGET_SIGNAL_XFSZ;
#endif
#if defined (SIGWIND)
  if (hostsig == SIGWIND)
    return TARGET_SIGNAL_WIND;
#endif
#if defined (SIGPHONE)
  if (hostsig == SIGPHONE)
    return TARGET_SIGNAL_PHONE;
#endif
#if defined (SIGLOST)
  if (hostsig == SIGLOST)
    return TARGET_SIGNAL_LOST;
#endif
#if defined (SIGWAITING)
  if (hostsig == SIGWAITING)
    return TARGET_SIGNAL_WAITING;
#endif
#if defined (SIGCANCEL)
  if (hostsig == SIGCANCEL)
    return TARGET_SIGNAL_CANCEL;
#endif
#if defined (SIGLWP)
  if (hostsig == SIGLWP)
    return TARGET_SIGNAL_LWP;
#endif
#if defined (SIGDANGER)
  if (hostsig == SIGDANGER)
    return TARGET_SIGNAL_DANGER;
#endif
#if defined (SIGGRANT)
  if (hostsig == SIGGRANT)
    return TARGET_SIGNAL_GRANT;
#endif
#if defined (SIGRETRACT)
  if (hostsig == SIGRETRACT)
    return TARGET_SIGNAL_RETRACT;
#endif
#if defined (SIGMSG)
  if (hostsig == SIGMSG)
    return TARGET_SIGNAL_MSG;
#endif
#if defined (SIGSOUND)
  if (hostsig == SIGSOUND)
    return TARGET_SIGNAL_SOUND;
#endif
#if defined (SIGSAK)
  if (hostsig == SIGSAK)
    return TARGET_SIGNAL_SAK;
#endif
#if defined (SIGPRIO)
  if (hostsig == SIGPRIO)
    return TARGET_SIGNAL_PRIO;
#endif

  /* Mach exceptions.  Assumes that the values for EXC_ are positive! */
#if defined (EXC_BAD_ACCESS) && defined (_NSIG)
  if (hostsig == _NSIG + EXC_BAD_ACCESS)
    return TARGET_EXC_BAD_ACCESS;
#endif
#if defined (EXC_BAD_INSTRUCTION) && defined (_NSIG)
  if (hostsig == _NSIG + EXC_BAD_INSTRUCTION)
    return TARGET_EXC_BAD_INSTRUCTION;
#endif
#if defined (EXC_ARITHMETIC) && defined (_NSIG)
  if (hostsig == _NSIG + EXC_ARITHMETIC)
    return TARGET_EXC_ARITHMETIC;
#endif
#if defined (EXC_EMULATION) && defined (_NSIG)
  if (hostsig == _NSIG + EXC_EMULATION)
    return TARGET_EXC_EMULATION;
#endif
#if defined (EXC_SOFTWARE) && defined (_NSIG)
  if (hostsig == _NSIG + EXC_SOFTWARE)
    return TARGET_EXC_SOFTWARE;
#endif
#if defined (EXC_BREAKPOINT) && defined (_NSIG)
  if (hostsig == _NSIG + EXC_BREAKPOINT)
    return TARGET_EXC_BREAKPOINT;
#endif

#if defined (SIGINFO)
  if (hostsig == SIGINFO)
    return TARGET_SIGNAL_INFO;
#endif

#if defined (REALTIME_LO)
  if (hostsig >= REALTIME_LO && hostsig < REALTIME_HI)
    {
      /* This block of TARGET_SIGNAL_REALTIME value is in order.  */
      if (33 <= hostsig && hostsig <= 63)
	return (enum target_signal)
	  (hostsig - 33 + (int) TARGET_SIGNAL_REALTIME_33);
      else if (hostsig == 32)
	return TARGET_SIGNAL_REALTIME_32;
      else if (64 <= hostsig && hostsig <= 127)
	return (enum target_signal)
	  (hostsig - 64 + (int) TARGET_SIGNAL_REALTIME_64);
      else
	error ("GDB bug: target.c (target_signal_from_host): unrecognized real-time signal");
    }
#endif

  return TARGET_SIGNAL_UNKNOWN;
}

/* Convert a OURSIG (an enum target_signal) to the form used by the
   target operating system (refered to as the ``host'') or zero if the
   equivalent host signal is not available.  Set/clear OURSIG_OK
   accordingly. */

static int
do_target_signal_to_host (enum target_signal oursig,
			  int *oursig_ok)
{
  int retsig;

  *oursig_ok = 1;
  switch (oursig)
    {
    case TARGET_SIGNAL_0:
      return 0;

#if defined (SIGHUP)
    case TARGET_SIGNAL_HUP:
      return SIGHUP;
#endif
#if defined (SIGINT)
    case TARGET_SIGNAL_INT:
      return SIGINT;
#endif
#if defined (SIGQUIT)
    case TARGET_SIGNAL_QUIT:
      return SIGQUIT;
#endif
#if defined (SIGILL)
    case TARGET_SIGNAL_ILL:
      return SIGILL;
#endif
#if defined (SIGTRAP)
    case TARGET_SIGNAL_TRAP:
      return SIGTRAP;
#endif
#if defined (SIGABRT)
    case TARGET_SIGNAL_ABRT:
      return SIGABRT;
#endif
#if defined (SIGEMT)
    case TARGET_SIGNAL_EMT:
      return SIGEMT;
#endif
#if defined (SIGFPE)
    case TARGET_SIGNAL_FPE:
      return SIGFPE;
#endif
#if defined (SIGKILL)
    case TARGET_SIGNAL_KILL:
      return SIGKILL;
#endif
#if defined (SIGBUS)
    case TARGET_SIGNAL_BUS:
      return SIGBUS;
#endif
#if defined (SIGSEGV)
    case TARGET_SIGNAL_SEGV:
      return SIGSEGV;
#endif
#if defined (SIGSYS)
    case TARGET_SIGNAL_SYS:
      return SIGSYS;
#endif
#if defined (SIGPIPE)
    case TARGET_SIGNAL_PIPE:
      return SIGPIPE;
#endif
#if defined (SIGALRM)
    case TARGET_SIGNAL_ALRM:
      return SIGALRM;
#endif
#if defined (SIGTERM)
    case TARGET_SIGNAL_TERM:
      return SIGTERM;
#endif
#if defined (SIGUSR1)
    case TARGET_SIGNAL_USR1:
      return SIGUSR1;
#endif
#if defined (SIGUSR2)
    case TARGET_SIGNAL_USR2:
      return SIGUSR2;
#endif
#if defined (SIGCHLD) || defined (SIGCLD)
    case TARGET_SIGNAL_CHLD:
#if defined (SIGCHLD)
      return SIGCHLD;
#else
      return SIGCLD;
#endif
#endif /* SIGCLD or SIGCHLD */
#if defined (SIGPWR)
    case TARGET_SIGNAL_PWR:
      return SIGPWR;
#endif
#if defined (SIGWINCH)
    case TARGET_SIGNAL_WINCH:
      return SIGWINCH;
#endif
#if defined (SIGURG)
    case TARGET_SIGNAL_URG:
      return SIGURG;
#endif
#if defined (SIGIO)
    case TARGET_SIGNAL_IO:
      return SIGIO;
#endif
#if defined (SIGPOLL)
    case TARGET_SIGNAL_POLL:
      return SIGPOLL;
#endif
#if defined (SIGSTOP)
    case TARGET_SIGNAL_STOP:
      return SIGSTOP;
#endif
#if defined (SIGTSTP)
    case TARGET_SIGNAL_TSTP:
      return SIGTSTP;
#endif
#if defined (SIGCONT)
    case TARGET_SIGNAL_CONT:
      return SIGCONT;
#endif
#if defined (SIGTTIN)
    case TARGET_SIGNAL_TTIN:
      return SIGTTIN;
#endif
#if defined (SIGTTOU)
    case TARGET_SIGNAL_TTOU:
      return SIGTTOU;
#endif
#if defined (SIGVTALRM)
    case TARGET_SIGNAL_VTALRM:
      return SIGVTALRM;
#endif
#if defined (SIGPROF)
    case TARGET_SIGNAL_PROF:
      return SIGPROF;
#endif
#if defined (SIGXCPU)
    case TARGET_SIGNAL_XCPU:
      return SIGXCPU;
#endif
#if defined (SIGXFSZ)
    case TARGET_SIGNAL_XFSZ:
      return SIGXFSZ;
#endif
#if defined (SIGWIND)
    case TARGET_SIGNAL_WIND:
      return SIGWIND;
#endif
#if defined (SIGPHONE)
    case TARGET_SIGNAL_PHONE:
      return SIGPHONE;
#endif
#if defined (SIGLOST)
    case TARGET_SIGNAL_LOST:
      return SIGLOST;
#endif
#if defined (SIGWAITING)
    case TARGET_SIGNAL_WAITING:
      return SIGWAITING;
#endif
#if defined (SIGCANCEL)
    case TARGET_SIGNAL_CANCEL:
      return SIGCANCEL;
#endif
#if defined (SIGLWP)
    case TARGET_SIGNAL_LWP:
      return SIGLWP;
#endif
#if defined (SIGDANGER)
    case TARGET_SIGNAL_DANGER:
      return SIGDANGER;
#endif
#if defined (SIGGRANT)
    case TARGET_SIGNAL_GRANT:
      return SIGGRANT;
#endif
#if defined (SIGRETRACT)
    case TARGET_SIGNAL_RETRACT:
      return SIGRETRACT;
#endif
#if defined (SIGMSG)
    case TARGET_SIGNAL_MSG:
      return SIGMSG;
#endif
#if defined (SIGSOUND)
    case TARGET_SIGNAL_SOUND:
      return SIGSOUND;
#endif
#if defined (SIGSAK)
    case TARGET_SIGNAL_SAK:
      return SIGSAK;
#endif
#if defined (SIGPRIO)
    case TARGET_SIGNAL_PRIO:
      return SIGPRIO;
#endif

      /* Mach exceptions.  Assumes that the values for EXC_ are positive! */
#if defined (EXC_BAD_ACCESS) && defined (_NSIG)
    case TARGET_EXC_BAD_ACCESS:
      return _NSIG + EXC_BAD_ACCESS;
#endif
#if defined (EXC_BAD_INSTRUCTION) && defined (_NSIG)
    case TARGET_EXC_BAD_INSTRUCTION:
      return _NSIG + EXC_BAD_INSTRUCTION;
#endif
#if defined (EXC_ARITHMETIC) && defined (_NSIG)
    case TARGET_EXC_ARITHMETIC:
      return _NSIG + EXC_ARITHMETIC;
#endif
#if defined (EXC_EMULATION) && defined (_NSIG)
    case TARGET_EXC_EMULATION:
      return _NSIG + EXC_EMULATION;
#endif
#if defined (EXC_SOFTWARE) && defined (_NSIG)
    case TARGET_EXC_SOFTWARE:
      return _NSIG + EXC_SOFTWARE;
#endif
#if defined (EXC_BREAKPOINT) && defined (_NSIG)
    case TARGET_EXC_BREAKPOINT:
      return _NSIG + EXC_BREAKPOINT;
#endif

#if defined (SIGINFO)
    case TARGET_SIGNAL_INFO:
      return SIGINFO;
#endif

    default:
#if defined (REALTIME_LO)
      retsig = 0;

      if (oursig >= TARGET_SIGNAL_REALTIME_33
	  && oursig <= TARGET_SIGNAL_REALTIME_63)
	{
	  /* This block of signals is continuous, and
             TARGET_SIGNAL_REALTIME_33 is 33 by definition.  */
	  retsig = (int) oursig - (int) TARGET_SIGNAL_REALTIME_33 + 33;
	}
      else if (oursig == TARGET_SIGNAL_REALTIME_32)
	{
	  /* TARGET_SIGNAL_REALTIME_32 isn't contiguous with
             TARGET_SIGNAL_REALTIME_33.  It is 32 by definition.  */
	  retsig = 32;
	}
      else if (oursig >= TARGET_SIGNAL_REALTIME_64
	  && oursig <= TARGET_SIGNAL_REALTIME_127)
	{
	  /* This block of signals is continuous, and
             TARGET_SIGNAL_REALTIME_64 is 64 by definition.  */
	  retsig = (int) oursig - (int) TARGET_SIGNAL_REALTIME_64 + 64;
	}

      if (retsig >= REALTIME_LO && retsig < REALTIME_HI)
	return retsig;
#endif

      *oursig_ok = 0;
      return 0;
    }
}

int
target_signal_to_host_p (enum target_signal oursig)
{
  int oursig_ok;
  do_target_signal_to_host (oursig, &oursig_ok);
  return oursig_ok;
}

int
target_signal_to_host (enum target_signal oursig)
{
  int oursig_ok;
  int targ_signo = do_target_signal_to_host (oursig, &oursig_ok);
  if (!oursig_ok)
    {
      /* The user might be trying to do "signal SIGSAK" where this system
         doesn't have SIGSAK.  */
      warning ("Signal %s does not exist on this system.\n",
	       target_signal_to_name (oursig));
      return 0;
    }
  else
    return targ_signo;
}

/* In some circumstances we allow a command to specify a numeric
   signal.  The idea is to keep these circumstances limited so that
   users (and scripts) develop portable habits.  For comparison,
   POSIX.2 `kill' requires that 1,2,3,6,9,14, and 15 work (and using a
   numeric signal at all is obsolescent.  We are slightly more
   lenient and allow 1-15 which should match host signal numbers on
   most systems.  Use of symbolic signal names is strongly encouraged.  */

enum target_signal
target_signal_from_command (int num)
{
  if (num >= 1 && num <= 15)
    return (enum target_signal) num;
  error ("Only signals 1-15 are valid as numeric signals.\n\
Use \"info signals\" for a list of symbolic signals.");
}

#ifndef GDBSERVER
extern initialize_file_ftype _initialize_signals; /* -Wmissing-prototype */

void
_initialize_signals (void)
{
  if (strcmp (signals[TARGET_SIGNAL_LAST].string, "TARGET_SIGNAL_MAGIC") != 0)
    internal_error (__FILE__, __LINE__, "failed internal consistency check");
}
#endif
