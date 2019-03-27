/* Low level interface to ptrace, for GDB when running under Unix.
   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1998, 1999, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.

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

#include "defs.h"
#include "frame.h"
#include "inferior.h"
#include "command.h"
#include "serial.h"
#include "terminal.h"
#include "target.h"
#include "gdbthread.h"

#include "gdb_string.h"
#include <signal.h>
#include <fcntl.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include "inflow.h"

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#if defined (SIGIO) && defined (FASYNC) && defined (FD_SET) && defined (F_SETOWN)
static void handle_sigio (int);
#endif

extern void _initialize_inflow (void);

static void pass_signal (int);

static void kill_command (char *, int);

static void terminal_ours_1 (int);

/* Record terminal status separately for debugger and inferior.  */

static struct serial *stdin_serial;

/* TTY state for the inferior.  We save it whenever the inferior stops, and
   restore it when it resumes.  */
static serial_ttystate inferior_ttystate;

/* Our own tty state, which we restore every time we need to deal with the
   terminal.  We only set it once, when GDB first starts.  The settings of
   flags which readline saves and restores and unimportant.  */
static serial_ttystate our_ttystate;

/* fcntl flags for us and the inferior.  Saved and restored just like
   {our,inferior}_ttystate.  */
static int tflags_inferior;
static int tflags_ours;

#ifdef PROCESS_GROUP_TYPE
/* Process group for us and the inferior.  Saved and restored just like
   {our,inferior}_ttystate.  */
PROCESS_GROUP_TYPE our_process_group;
PROCESS_GROUP_TYPE inferior_process_group;
#endif

/* While the inferior is running, we want SIGINT and SIGQUIT to go to the
   inferior only.  If we have job control, that takes care of it.  If not,
   we save our handlers in these two variables and set SIGINT and SIGQUIT
   to SIG_IGN.  */

static void (*sigint_ours) ();
static void (*sigquit_ours) ();

/* The name of the tty (from the `tty' command) that we gave to the inferior
   when it was last started.  */

static char *inferior_thisrun_terminal;

/* Nonzero if our terminal settings are in effect.  Zero if the
   inferior's settings are in effect.  Ignored if !gdb_has_a_terminal
   ().  */

int terminal_is_ours;

enum
  {
    yes, no, have_not_checked
  }
gdb_has_a_terminal_flag = have_not_checked;

/* Does GDB have a terminal (on stdin)?  */
int
gdb_has_a_terminal (void)
{
  switch (gdb_has_a_terminal_flag)
    {
    case yes:
      return 1;
    case no:
      return 0;
    case have_not_checked:
      /* Get all the current tty settings (including whether we have a
         tty at all!).  Can't do this in _initialize_inflow because
         serial_fdopen() won't work until the serial_ops_list is
         initialized.  */

#ifdef F_GETFL
      tflags_ours = fcntl (0, F_GETFL, 0);
#endif

      gdb_has_a_terminal_flag = no;
      stdin_serial = serial_fdopen (0);
      if (stdin_serial != NULL)
	{
	  our_ttystate = serial_get_tty_state (stdin_serial);

	  if (our_ttystate != NULL)
	    {
	      gdb_has_a_terminal_flag = yes;
#ifdef HAVE_TERMIOS
	      our_process_group = tcgetpgrp (0);
#endif
#ifdef HAVE_TERMIO
	      our_process_group = getpgrp ();
#endif
#ifdef HAVE_SGTTY
	      ioctl (0, TIOCGPGRP, &our_process_group);
#endif
	    }
	}

      return gdb_has_a_terminal_flag == yes;
    default:
      /* "Can't happen".  */
      return 0;
    }
}

/* Macro for printing errors from ioctl operations */

#define	OOPSY(what)	\
  if (result == -1)	\
    fprintf_unfiltered(gdb_stderr, "[%s failed in terminal_inferior: %s]\n", \
	    what, safe_strerror (errno))

static void terminal_ours_1 (int);

/* Initialize the terminal settings we record for the inferior,
   before we actually run the inferior.  */

void
terminal_init_inferior_with_pgrp (int pgrp)
{
  if (gdb_has_a_terminal ())
    {
      /* We could just as well copy our_ttystate (if we felt like
         adding a new function serial_copy_tty_state()).  */
      if (inferior_ttystate)
	xfree (inferior_ttystate);
      inferior_ttystate = serial_get_tty_state (stdin_serial);

#ifdef PROCESS_GROUP_TYPE
      inferior_process_group = pgrp;
#endif

      /* Make sure that next time we call terminal_inferior (which will be
         before the program runs, as it needs to be), we install the new
         process group.  */
      terminal_is_ours = 1;
    }
}

/* Save the terminal settings again.  This is necessary for the TUI
   when it switches to TUI or non-TUI mode;  curses changes the terminal
   and gdb must be able to restore it correctly.  */

void
terminal_save_ours (void)
{
  if (gdb_has_a_terminal ())
    {
      /* We could just as well copy our_ttystate (if we felt like adding
         a new function serial_copy_tty_state).  */
      if (our_ttystate)
        xfree (our_ttystate);
      our_ttystate = serial_get_tty_state (stdin_serial);
    }
}

void
terminal_init_inferior (void)
{
#ifdef PROCESS_GROUP_TYPE
  /* This is for Lynx, and should be cleaned up by having Lynx be a separate
     debugging target with a version of target_terminal_init_inferior which
     passes in the process group to a generic routine which does all the work
     (and the non-threaded child_terminal_init_inferior can just pass in
     inferior_ptid to the same routine).  */
  /* We assume INFERIOR_PID is also the child's process group.  */
  terminal_init_inferior_with_pgrp (PIDGET (inferior_ptid));
#endif /* PROCESS_GROUP_TYPE */
}

/* Put the inferior's terminal settings into effect.
   This is preparation for starting or resuming the inferior.  */

void
terminal_inferior (void)
{
  if (gdb_has_a_terminal () && terminal_is_ours
      && inferior_ttystate != NULL
      && inferior_thisrun_terminal == 0)
    {
      int result;

#ifdef F_GETFL
      /* Is there a reason this is being done twice?  It happens both
         places we use F_SETFL, so I'm inclined to think perhaps there
         is some reason, however perverse.  Perhaps not though...  */
      result = fcntl (0, F_SETFL, tflags_inferior);
      result = fcntl (0, F_SETFL, tflags_inferior);
      OOPSY ("fcntl F_SETFL");
#endif

      /* Because we were careful to not change in or out of raw mode in
         terminal_ours, we will not change in our out of raw mode with
         this call, so we don't flush any input.  */
      result = serial_set_tty_state (stdin_serial, inferior_ttystate);
      OOPSY ("setting tty state");

      if (!job_control)
	{
	  sigint_ours = (void (*)()) signal (SIGINT, SIG_IGN);
#ifdef SIGQUIT
	  sigquit_ours = (void (*)()) signal (SIGQUIT, SIG_IGN);
#endif
	}

      /* If attach_flag is set, we don't know whether we are sharing a
         terminal with the inferior or not.  (attaching a process
         without a terminal is one case where we do not; attaching a
         process which we ran from the same shell as GDB via `&' is
         one case where we do, I think (but perhaps this is not
         `sharing' in the sense that we need to save and restore tty
         state)).  I don't know if there is any way to tell whether we
         are sharing a terminal.  So what we do is to go through all
         the saving and restoring of the tty state, but ignore errors
         setting the process group, which will happen if we are not
         sharing a terminal).  */

      if (job_control)
	{
#ifdef HAVE_TERMIOS
	  result = tcsetpgrp (0, inferior_process_group);
	  if (!attach_flag)
	    OOPSY ("tcsetpgrp");
#endif

#ifdef HAVE_SGTTY
	  result = ioctl (0, TIOCSPGRP, &inferior_process_group);
	  if (!attach_flag)
	    OOPSY ("TIOCSPGRP");
#endif
	}

    }
  terminal_is_ours = 0;
}

/* Put some of our terminal settings into effect,
   enough to get proper results from our output,
   but do not change into or out of RAW mode
   so that no input is discarded.

   After doing this, either terminal_ours or terminal_inferior
   should be called to get back to a normal state of affairs.  */

void
terminal_ours_for_output (void)
{
  terminal_ours_1 (1);
}

/* Put our terminal settings into effect.
   First record the inferior's terminal settings
   so they can be restored properly later.  */

void
terminal_ours (void)
{
  terminal_ours_1 (0);
}

/* output_only is not used, and should not be used unless we introduce
   separate terminal_is_ours and terminal_is_ours_for_output
   flags.  */

static void
terminal_ours_1 (int output_only)
{
  /* Checking inferior_thisrun_terminal is necessary so that
     if GDB is running in the background, it won't block trying
     to do the ioctl()'s below.  Checking gdb_has_a_terminal
     avoids attempting all the ioctl's when running in batch.  */
  if (inferior_thisrun_terminal != 0 || gdb_has_a_terminal () == 0)
    return;

  if (!terminal_is_ours)
    {
#ifdef SIGTTOU
      /* Ignore this signal since it will happen when we try to set the
         pgrp.  */
      void (*osigttou) () = NULL;
#endif
      int result;

      terminal_is_ours = 1;

#ifdef SIGTTOU
      if (job_control)
	osigttou = (void (*)()) signal (SIGTTOU, SIG_IGN);
#endif

      if (inferior_ttystate)
	xfree (inferior_ttystate);
      inferior_ttystate = serial_get_tty_state (stdin_serial);
#ifdef HAVE_TERMIOS
      inferior_process_group = tcgetpgrp (0);
#endif
#ifdef HAVE_TERMIO
      inferior_process_group = getpgrp ();
#endif
#ifdef HAVE_SGTTY
      ioctl (0, TIOCGPGRP, &inferior_process_group);
#endif

      /* Here we used to set ICANON in our ttystate, but I believe this
         was an artifact from before when we used readline.  Readline sets
         the tty state when it needs to.
         FIXME-maybe: However, query() expects non-raw mode and doesn't
         use readline.  Maybe query should use readline (on the other hand,
         this only matters for HAVE_SGTTY, not termio or termios, I think).  */

      /* Set tty state to our_ttystate.  We don't change in our out of raw
         mode, to avoid flushing input.  We need to do the same thing
         regardless of output_only, because we don't have separate
         terminal_is_ours and terminal_is_ours_for_output flags.  It's OK,
         though, since readline will deal with raw mode when/if it needs to.
       */

      serial_noflush_set_tty_state (stdin_serial, our_ttystate,
				    inferior_ttystate);

      if (job_control)
	{
#ifdef HAVE_TERMIOS
	  result = tcsetpgrp (0, our_process_group);
#if 0
	  /* This fails on Ultrix with EINVAL if you run the testsuite
	     in the background with nohup, and then log out.  GDB never
	     used to check for an error here, so perhaps there are other
	     such situations as well.  */
	  if (result == -1)
	    fprintf_unfiltered (gdb_stderr, "[tcsetpgrp failed in terminal_ours: %s]\n",
				safe_strerror (errno));
#endif
#endif /* termios */

#ifdef HAVE_SGTTY
	  result = ioctl (0, TIOCSPGRP, &our_process_group);
#endif
	}

#ifdef SIGTTOU
      if (job_control)
	signal (SIGTTOU, osigttou);
#endif

      if (!job_control)
	{
	  signal (SIGINT, sigint_ours);
#ifdef SIGQUIT
	  signal (SIGQUIT, sigquit_ours);
#endif
	}

#ifdef F_GETFL
      tflags_inferior = fcntl (0, F_GETFL, 0);

      /* Is there a reason this is being done twice?  It happens both
         places we use F_SETFL, so I'm inclined to think perhaps there
         is some reason, however perverse.  Perhaps not though...  */
      result = fcntl (0, F_SETFL, tflags_ours);
      result = fcntl (0, F_SETFL, tflags_ours);
#endif

      result = result;		/* lint */
    }
}

void
term_info (char *arg, int from_tty)
{
  target_terminal_info (arg, from_tty);
}

void
child_terminal_info (char *args, int from_tty)
{
  if (!gdb_has_a_terminal ())
    {
      printf_filtered ("This GDB does not control a terminal.\n");
      return;
    }

  printf_filtered ("Inferior's terminal status (currently saved by GDB):\n");

  /* First the fcntl flags.  */
  {
    int flags;

    flags = tflags_inferior;

    printf_filtered ("File descriptor flags = ");

#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif
    /* (O_ACCMODE) parens are to avoid Ultrix header file bug */
    switch (flags & (O_ACCMODE))
      {
      case O_RDONLY:
	printf_filtered ("O_RDONLY");
	break;
      case O_WRONLY:
	printf_filtered ("O_WRONLY");
	break;
      case O_RDWR:
	printf_filtered ("O_RDWR");
	break;
      }
    flags &= ~(O_ACCMODE);

#ifdef O_NONBLOCK
    if (flags & O_NONBLOCK)
      printf_filtered (" | O_NONBLOCK");
    flags &= ~O_NONBLOCK;
#endif

#if defined (O_NDELAY)
    /* If O_NDELAY and O_NONBLOCK are defined to the same thing, we will
       print it as O_NONBLOCK, which is good cause that is what POSIX
       has, and the flag will already be cleared by the time we get here.  */
    if (flags & O_NDELAY)
      printf_filtered (" | O_NDELAY");
    flags &= ~O_NDELAY;
#endif

    if (flags & O_APPEND)
      printf_filtered (" | O_APPEND");
    flags &= ~O_APPEND;

#if defined (O_BINARY)
    if (flags & O_BINARY)
      printf_filtered (" | O_BINARY");
    flags &= ~O_BINARY;
#endif

    if (flags)
      printf_filtered (" | 0x%x", flags);
    printf_filtered ("\n");
  }

#ifdef PROCESS_GROUP_TYPE
  printf_filtered ("Process group = %d\n",
		   (int) inferior_process_group);
#endif

  serial_print_tty_state (stdin_serial, inferior_ttystate, gdb_stdout);
}

/* NEW_TTY_PREFORK is called before forking a new child process,
   so we can record the state of ttys in the child to be formed.
   TTYNAME is null if we are to share the terminal with gdb;
   or points to a string containing the name of the desired tty.

   NEW_TTY is called in new child processes under Unix, which will
   become debugger target processes.  This actually switches to
   the terminal specified in the NEW_TTY_PREFORK call.  */

void
new_tty_prefork (char *ttyname)
{
  /* Save the name for later, for determining whether we and the child
     are sharing a tty.  */
  inferior_thisrun_terminal = ttyname;
}

void
new_tty (void)
{
  int tty;

  if (inferior_thisrun_terminal == 0)
    return;
#if !defined(__GO32__) && !defined(_WIN32)
#ifdef TIOCNOTTY
  /* Disconnect the child process from our controlling terminal.  On some
     systems (SVR4 for example), this may cause a SIGTTOU, so temporarily
     ignore SIGTTOU. */
  tty = open ("/dev/tty", O_RDWR);
  if (tty > 0)
    {
      void (*osigttou) ();

      osigttou = (void (*)()) signal (SIGTTOU, SIG_IGN);
      ioctl (tty, TIOCNOTTY, 0);
      close (tty);
      signal (SIGTTOU, osigttou);
    }
#endif

  /* Now open the specified new terminal.  */

#ifdef USE_O_NOCTTY
  tty = open (inferior_thisrun_terminal, O_RDWR | O_NOCTTY);
#else
  tty = open (inferior_thisrun_terminal, O_RDWR);
#endif
  if (tty == -1)
    {
      print_sys_errmsg (inferior_thisrun_terminal, errno);
      _exit (1);
    }

  /* Avoid use of dup2; doesn't exist on all systems.  */
  if (tty != 0)
    {
      close (0);
      dup (tty);
    }
  if (tty != 1)
    {
      close (1);
      dup (tty);
    }
  if (tty != 2)
    {
      close (2);
      dup (tty);
    }
  if (tty > 2)
    close (tty);
#endif /* !go32 && !win32 */
}

/* Kill the inferior process.  Make us have no inferior.  */

static void
kill_command (char *arg, int from_tty)
{
  /* FIXME:  This should not really be inferior_ptid (or target_has_execution).
     It should be a distinct flag that indicates that a target is active, cuz
     some targets don't have processes! */

  if (ptid_equal (inferior_ptid, null_ptid))
    error ("The program is not being run.");
  if (!query ("Kill the program being debugged? "))
    error ("Not confirmed.");
  target_kill ();

  init_thread_list ();		/* Destroy thread info */

  /* Killing off the inferior can leave us with a core file.  If so,
     print the state we are left in.  */
  if (target_has_stack)
    {
      printf_filtered ("In %s,\n", target_longname);
      if (deprecated_selected_frame == NULL)
	fputs_filtered ("No selected stack frame.\n", gdb_stdout);
      else
	print_stack_frame (deprecated_selected_frame,
			   frame_relative_level (deprecated_selected_frame), 1);
    }
}

/* Call set_sigint_trap when you need to pass a signal on to an attached
   process when handling SIGINT */

static void
pass_signal (int signo)
{
#ifndef _WIN32
  kill (PIDGET (inferior_ptid), SIGINT);
#endif
}

static void (*osig) ();

void
set_sigint_trap (void)
{
  if (attach_flag || inferior_thisrun_terminal)
    {
      osig = (void (*)()) signal (SIGINT, pass_signal);
    }
}

void
clear_sigint_trap (void)
{
  if (attach_flag || inferior_thisrun_terminal)
    {
      signal (SIGINT, osig);
    }
}

#if defined (SIGIO) && defined (FASYNC) && defined (FD_SET) && defined (F_SETOWN)
static void (*old_sigio) ();

static void
handle_sigio (int signo)
{
  int numfds;
  fd_set readfds;

  signal (SIGIO, handle_sigio);

  FD_ZERO (&readfds);
  FD_SET (target_activity_fd, &readfds);
  numfds = select (target_activity_fd + 1, &readfds, NULL, NULL, NULL);
  if (numfds >= 0 && FD_ISSET (target_activity_fd, &readfds))
    {
#ifndef _WIN32
      if ((*target_activity_function) ())
	kill (PIDGET (inferior_ptid), SIGINT);
#endif
    }
}

static int old_fcntl_flags;

void
set_sigio_trap (void)
{
  if (target_activity_function)
    {
      old_sigio = (void (*)()) signal (SIGIO, handle_sigio);
      fcntl (target_activity_fd, F_SETOWN, getpid ());
      old_fcntl_flags = fcntl (target_activity_fd, F_GETFL, 0);
      fcntl (target_activity_fd, F_SETFL, old_fcntl_flags | FASYNC);
    }
}

void
clear_sigio_trap (void)
{
  if (target_activity_function)
    {
      signal (SIGIO, old_sigio);
      fcntl (target_activity_fd, F_SETFL, old_fcntl_flags);
    }
}
#else /* No SIGIO.  */
void
set_sigio_trap (void)
{
  if (target_activity_function)
    internal_error (__FILE__, __LINE__, "failed internal consistency check");
}

void
clear_sigio_trap (void)
{
  if (target_activity_function)
    internal_error (__FILE__, __LINE__, "failed internal consistency check");
}
#endif /* No SIGIO.  */


/* This is here because this is where we figure out whether we (probably)
   have job control.  Just using job_control only does part of it because
   setpgid or setpgrp might not exist on a system without job control.
   It might be considered misplaced (on the other hand, process groups and
   job control are closely related to ttys).

   For a more clean implementation, in libiberty, put a setpgid which merely
   calls setpgrp and a setpgrp which does nothing (any system with job control
   will have one or the other).  */
int
gdb_setpgid (void)
{
  int retval = 0;

  if (job_control)
    {
#if defined (HAVE_TERMIOS) || defined (TIOCGPGRP)
#ifdef HAVE_SETPGID
      /* The call setpgid (0, 0) is supposed to work and mean the same
         thing as this, but on Ultrix 4.2A it fails with EPERM (and
         setpgid (getpid (), getpid ()) succeeds).  */
      retval = setpgid (getpid (), getpid ());
#else
#ifdef HAVE_SETPGRP
#ifdef SETPGRP_VOID 
      retval = setpgrp ();
#else
      retval = setpgrp (getpid (), getpid ());
#endif
#endif /* HAVE_SETPGRP */
#endif /* HAVE_SETPGID */
#endif /* defined (HAVE_TERMIOS) || defined (TIOCGPGRP) */
    }

  return retval;
}

void
_initialize_inflow (void)
{
  add_info ("terminal", term_info,
	    "Print inferior's saved terminal status.");

  add_com ("kill", class_run, kill_command,
	   "Kill execution of program being debugged.");

  inferior_ptid = null_ptid;

  terminal_is_ours = 1;

  /* OK, figure out whether we have job control.  If neither termios nor
     sgtty (i.e. termio or go32), leave job_control 0.  */

#if defined (HAVE_TERMIOS)
  /* Do all systems with termios have the POSIX way of identifying job
     control?  I hope so.  */
#ifdef _POSIX_JOB_CONTROL
  job_control = 1;
#else
#ifdef _SC_JOB_CONTROL
  job_control = sysconf (_SC_JOB_CONTROL);
#else
  job_control = 0;		/* have to assume the worst */
#endif /* _SC_JOB_CONTROL */
#endif /* _POSIX_JOB_CONTROL */
#endif /* HAVE_TERMIOS */

#ifdef HAVE_SGTTY
#ifdef TIOCGPGRP
  job_control = 1;
#else
  job_control = 0;
#endif /* TIOCGPGRP */
#endif /* sgtty */
}
