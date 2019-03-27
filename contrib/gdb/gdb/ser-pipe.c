/* Serial interface for a pipe to a separate program
   Copyright 1999, 2000, 2001 Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "serial.h"
#include "ser-unix.h"

#include "gdb_vfork.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include "gdb_string.h"

#include <signal.h>

static int pipe_open (struct serial *scb, const char *name);
static void pipe_close (struct serial *scb);

extern void _initialize_ser_pipe (void);

struct pipe_state
  {
    int pid;
  };

/* Open up a raw pipe */

static int
pipe_open (struct serial *scb, const char *name)
{
#if !HAVE_SOCKETPAIR
  return -1;
#else
  struct pipe_state *state;
  /* This chunk: */
  /* Copyright (c) 1988, 1993
   *      The Regents of the University of California.  All rights reserved.
   *
   * This code is derived from software written by Ken Arnold and
   * published in UNIX Review, Vol. 6, No. 8.
   */
  int pdes[2];
  int pid;
  if (socketpair (AF_UNIX, SOCK_STREAM, 0, pdes) < 0)
    return -1;

  /* Create the child process to run the command in.  Note that the
     apparent call to vfork() below *might* actually be a call to
     fork() due to the fact that autoconf will ``#define vfork fork''
     on certain platforms.  */
  pid = vfork ();
  
  /* Error. */
  if (pid == -1)
    {
      close (pdes[0]);
      close (pdes[1]);
      return -1;
    }

  /* Child. */
  if (pid == 0)
    {
      /* re-wire pdes[1] to stdin/stdout */
      close (pdes[0]);
      if (pdes[1] != STDOUT_FILENO)
	{
	  dup2 (pdes[1], STDOUT_FILENO);
	  close (pdes[1]);
	}
      dup2 (STDOUT_FILENO, STDIN_FILENO);
#if 0
      /* close any stray FD's - FIXME - how? */
      /* POSIX.2 B.3.2.2 "popen() shall ensure that any streams
         from previous popen() calls that remain open in the 
         parent process are closed in the new child process. */
      for (old = pidlist; old; old = old->next)
	close (fileno (old->fp));	/* don't allow a flush */
#endif
      execl ("/bin/sh", "sh", "-c", name, (char *) 0);
      _exit (127);
    }

  /* Parent. */
  close (pdes[1]);
  /* :end chunk */
  state = XMALLOC (struct pipe_state);
  state->pid = pid;
  scb->fd = pdes[0];
  scb->state = state;

  /* If we don't do this, GDB simply exits when the remote side dies.  */
  signal (SIGPIPE, SIG_IGN);
  return 0;
#endif
}

static void
pipe_close (struct serial *scb)
{
  struct pipe_state *state = scb->state;
  if (state != NULL)
    {
      int pid = state->pid;
      close (scb->fd);
      scb->fd = -1;
      xfree (state);
      scb->state = NULL;
      kill (pid, SIGTERM);
      /* Might be useful to check that the child does die. */
    }
}

static struct serial_ops pipe_ops;

void
_initialize_ser_pipe (void)
{
  struct serial_ops *ops = XMALLOC (struct serial_ops);
  memset (ops, 0, sizeof (struct serial_ops));
  ops->name = "pipe";
  ops->next = 0;
  ops->open = pipe_open;
  ops->close = pipe_close;
  ops->readchar = ser_unix_readchar;
  ops->write = ser_unix_write;
  ops->flush_output = ser_unix_nop_flush_output;
  ops->flush_input = ser_unix_flush_input;
  ops->send_break = ser_unix_nop_send_break;
  ops->go_raw = ser_unix_nop_raw;
  ops->get_tty_state = ser_unix_nop_get_tty_state;
  ops->set_tty_state = ser_unix_nop_set_tty_state;
  ops->print_tty_state = ser_unix_nop_print_tty_state;
  ops->noflush_set_tty_state = ser_unix_nop_noflush_set_tty_state;
  ops->setbaudrate = ser_unix_nop_setbaudrate;
  ops->setstopbits = ser_unix_nop_setstopbits;
  ops->drain_output = ser_unix_nop_drain_output;
  ops->async = ser_unix_async;
  serial_add_interface (ops);
}
