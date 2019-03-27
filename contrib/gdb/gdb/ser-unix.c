/* Serial interface for local (hardwired) serial ports on Un*x like systems

   Copyright 1992, 1993, 1994, 1995, 1996, 1998, 1999, 2000, 2001,
   2003, 2004 Free Software Foundation, Inc.

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

#include <fcntl.h>
#include <sys/types.h>
#include "terminal.h"
#include <sys/socket.h>
#include <sys/time.h>

#include "gdb_string.h"
#include "event-loop.h"

#ifdef HAVE_TERMIOS

struct hardwire_ttystate
  {
    struct termios termios;
  };
#endif /* termios */

#ifdef HAVE_TERMIO

/* It is believed that all systems which have added job control to SVR3
   (e.g. sco) have also added termios.  Even if not, trying to figure out
   all the variations (TIOCGPGRP vs. TCGETPGRP, etc.) would be pretty
   bewildering.  So we don't attempt it.  */

struct hardwire_ttystate
  {
    struct termio termio;
  };
#endif /* termio */

#ifdef HAVE_SGTTY
struct hardwire_ttystate
  {
    struct sgttyb sgttyb;
    struct tchars tc;
    struct ltchars ltc;
    /* Line discipline flags.  */
    int lmode;
  };
#endif /* sgtty */

static int hardwire_open (struct serial *scb, const char *name);
static void hardwire_raw (struct serial *scb);
static int wait_for (struct serial *scb, int timeout);
static int hardwire_readchar (struct serial *scb, int timeout);
static int do_hardwire_readchar (struct serial *scb, int timeout);
static int generic_readchar (struct serial *scb, int timeout,
			     int (*do_readchar) (struct serial *scb,
						 int timeout));
static int rate_to_code (int rate);
static int hardwire_setbaudrate (struct serial *scb, int rate);
static void hardwire_close (struct serial *scb);
static int get_tty_state (struct serial *scb,
			  struct hardwire_ttystate * state);
static int set_tty_state (struct serial *scb,
			  struct hardwire_ttystate * state);
static serial_ttystate hardwire_get_tty_state (struct serial *scb);
static int hardwire_set_tty_state (struct serial *scb, serial_ttystate state);
static int hardwire_noflush_set_tty_state (struct serial *, serial_ttystate,
					   serial_ttystate);
static void hardwire_print_tty_state (struct serial *, serial_ttystate,
				      struct ui_file *);
static int hardwire_drain_output (struct serial *);
static int hardwire_flush_output (struct serial *);
static int hardwire_flush_input (struct serial *);
static int hardwire_send_break (struct serial *);
static int hardwire_setstopbits (struct serial *, int);

static int do_unix_readchar (struct serial *scb, int timeout);
static timer_handler_func push_event;
static handler_func fd_event;
static void reschedule (struct serial *scb);

void _initialize_ser_hardwire (void);

extern int (*ui_loop_hook) (int);

/* Open up a real live device for serial I/O */

static int
hardwire_open (struct serial *scb, const char *name)
{
  scb->fd = open (name, O_RDWR);
  if (scb->fd < 0)
    return -1;

  return 0;
}

static int
get_tty_state (struct serial *scb, struct hardwire_ttystate *state)
{
#ifdef HAVE_TERMIOS
  if (tcgetattr (scb->fd, &state->termios) < 0)
    return -1;

  return 0;
#endif

#ifdef HAVE_TERMIO
  if (ioctl (scb->fd, TCGETA, &state->termio) < 0)
    return -1;
  return 0;
#endif

#ifdef HAVE_SGTTY
  if (ioctl (scb->fd, TIOCGETP, &state->sgttyb) < 0)
    return -1;
  if (ioctl (scb->fd, TIOCGETC, &state->tc) < 0)
    return -1;
  if (ioctl (scb->fd, TIOCGLTC, &state->ltc) < 0)
    return -1;
  if (ioctl (scb->fd, TIOCLGET, &state->lmode) < 0)
    return -1;

  return 0;
#endif
}

static int
set_tty_state (struct serial *scb, struct hardwire_ttystate *state)
{
#ifdef HAVE_TERMIOS
  if (tcsetattr (scb->fd, TCSANOW, &state->termios) < 0)
    return -1;

  return 0;
#endif

#ifdef HAVE_TERMIO
  if (ioctl (scb->fd, TCSETA, &state->termio) < 0)
    return -1;
  return 0;
#endif

#ifdef HAVE_SGTTY
  if (ioctl (scb->fd, TIOCSETN, &state->sgttyb) < 0)
    return -1;
  if (ioctl (scb->fd, TIOCSETC, &state->tc) < 0)
    return -1;
  if (ioctl (scb->fd, TIOCSLTC, &state->ltc) < 0)
    return -1;
  if (ioctl (scb->fd, TIOCLSET, &state->lmode) < 0)
    return -1;

  return 0;
#endif
}

static serial_ttystate
hardwire_get_tty_state (struct serial *scb)
{
  struct hardwire_ttystate *state;

  state = (struct hardwire_ttystate *) xmalloc (sizeof *state);

  if (get_tty_state (scb, state))
    return NULL;

  return (serial_ttystate) state;
}

static int
hardwire_set_tty_state (struct serial *scb, serial_ttystate ttystate)
{
  struct hardwire_ttystate *state;

  state = (struct hardwire_ttystate *) ttystate;

  return set_tty_state (scb, state);
}

static int
hardwire_noflush_set_tty_state (struct serial *scb,
				serial_ttystate new_ttystate,
				serial_ttystate old_ttystate)
{
  struct hardwire_ttystate new_state;
#ifdef HAVE_SGTTY
  struct hardwire_ttystate *state = (struct hardwire_ttystate *) old_ttystate;
#endif

  new_state = *(struct hardwire_ttystate *) new_ttystate;

  /* Don't change in or out of raw mode; we don't want to flush input.
     termio and termios have no such restriction; for them flushing input
     is separate from setting the attributes.  */

#ifdef HAVE_SGTTY
  if (state->sgttyb.sg_flags & RAW)
    new_state.sgttyb.sg_flags |= RAW;
  else
    new_state.sgttyb.sg_flags &= ~RAW;

  /* I'm not sure whether this is necessary; the manpage just mentions
     RAW not CBREAK.  */
  if (state->sgttyb.sg_flags & CBREAK)
    new_state.sgttyb.sg_flags |= CBREAK;
  else
    new_state.sgttyb.sg_flags &= ~CBREAK;
#endif

  return set_tty_state (scb, &new_state);
}

static void
hardwire_print_tty_state (struct serial *scb,
			  serial_ttystate ttystate,
			  struct ui_file *stream)
{
  struct hardwire_ttystate *state = (struct hardwire_ttystate *) ttystate;
  int i;

#ifdef HAVE_TERMIOS
  fprintf_filtered (stream, "c_iflag = 0x%x, c_oflag = 0x%x,\n",
		    (int) state->termios.c_iflag,
		    (int) state->termios.c_oflag);
  fprintf_filtered (stream, "c_cflag = 0x%x, c_lflag = 0x%x\n",
		    (int) state->termios.c_cflag,
		    (int) state->termios.c_lflag);
#if 0
  /* This not in POSIX, and is not really documented by those systems
     which have it (at least not Sun).  */
  fprintf_filtered (stream, "c_line = 0x%x.\n", state->termios.c_line);
#endif
  fprintf_filtered (stream, "c_cc: ");
  for (i = 0; i < NCCS; i += 1)
    fprintf_filtered (stream, "0x%x ", state->termios.c_cc[i]);
  fprintf_filtered (stream, "\n");
#endif

#ifdef HAVE_TERMIO
  fprintf_filtered (stream, "c_iflag = 0x%x, c_oflag = 0x%x,\n",
		    state->termio.c_iflag, state->termio.c_oflag);
  fprintf_filtered (stream, "c_cflag = 0x%x, c_lflag = 0x%x, c_line = 0x%x.\n",
		    state->termio.c_cflag, state->termio.c_lflag,
		    state->termio.c_line);
  fprintf_filtered (stream, "c_cc: ");
  for (i = 0; i < NCC; i += 1)
    fprintf_filtered (stream, "0x%x ", state->termio.c_cc[i]);
  fprintf_filtered (stream, "\n");
#endif

#ifdef HAVE_SGTTY
  fprintf_filtered (stream, "sgttyb.sg_flags = 0x%x.\n",
		    state->sgttyb.sg_flags);

  fprintf_filtered (stream, "tchars: ");
  for (i = 0; i < (int) sizeof (struct tchars); i++)
    fprintf_filtered (stream, "0x%x ", ((unsigned char *) &state->tc)[i]);
  fprintf_filtered (stream, "\n");

  fprintf_filtered (stream, "ltchars: ");
  for (i = 0; i < (int) sizeof (struct ltchars); i++)
    fprintf_filtered (stream, "0x%x ", ((unsigned char *) &state->ltc)[i]);
  fprintf_filtered (stream, "\n");

  fprintf_filtered (stream, "lmode:  0x%x\n", state->lmode);
#endif
}

/* Wait for the output to drain away, as opposed to flushing (discarding) it */

static int
hardwire_drain_output (struct serial *scb)
{
#ifdef HAVE_TERMIOS
  return tcdrain (scb->fd);
#endif

#ifdef HAVE_TERMIO
  return ioctl (scb->fd, TCSBRK, 1);
#endif

#ifdef HAVE_SGTTY
  /* Get the current state and then restore it using TIOCSETP,
     which should cause the output to drain and pending input
     to be discarded. */
  {
    struct hardwire_ttystate state;
    if (get_tty_state (scb, &state))
      {
	return (-1);
      }
    else
      {
	return (ioctl (scb->fd, TIOCSETP, &state.sgttyb));
      }
  }
#endif
}

static int
hardwire_flush_output (struct serial *scb)
{
#ifdef HAVE_TERMIOS
  return tcflush (scb->fd, TCOFLUSH);
#endif

#ifdef HAVE_TERMIO
  return ioctl (scb->fd, TCFLSH, 1);
#endif

#ifdef HAVE_SGTTY
  /* This flushes both input and output, but we can't do better.  */
  return ioctl (scb->fd, TIOCFLUSH, 0);
#endif
}

static int
hardwire_flush_input (struct serial *scb)
{
  ser_unix_flush_input (scb);

#ifdef HAVE_TERMIOS
  return tcflush (scb->fd, TCIFLUSH);
#endif

#ifdef HAVE_TERMIO
  return ioctl (scb->fd, TCFLSH, 0);
#endif

#ifdef HAVE_SGTTY
  /* This flushes both input and output, but we can't do better.  */
  return ioctl (scb->fd, TIOCFLUSH, 0);
#endif
}

static int
hardwire_send_break (struct serial *scb)
{
#ifdef HAVE_TERMIOS
  return tcsendbreak (scb->fd, 0);
#endif

#ifdef HAVE_TERMIO
  return ioctl (scb->fd, TCSBRK, 0);
#endif

#ifdef HAVE_SGTTY
  {
    int status;
    struct timeval timeout;

    status = ioctl (scb->fd, TIOCSBRK, 0);

    /* Can't use usleep; it doesn't exist in BSD 4.2.  */
    /* Note that if this select() is interrupted by a signal it will not wait
       the full length of time.  I think that is OK.  */
    timeout.tv_sec = 0;
    timeout.tv_usec = 250000;
    select (0, 0, 0, 0, &timeout);
    status = ioctl (scb->fd, TIOCCBRK, 0);
    return status;
  }
#endif
}

static void
hardwire_raw (struct serial *scb)
{
  struct hardwire_ttystate state;

  if (get_tty_state (scb, &state))
    fprintf_unfiltered (gdb_stderr, "get_tty_state failed: %s\n", safe_strerror (errno));

#ifdef HAVE_TERMIOS
  state.termios.c_iflag = 0;
  state.termios.c_oflag = 0;
  state.termios.c_lflag = 0;
  state.termios.c_cflag &= ~(CSIZE | PARENB);
  state.termios.c_cflag |= CLOCAL | CS8;
  state.termios.c_cc[VMIN] = 0;
  state.termios.c_cc[VTIME] = 0;
#endif

#ifdef HAVE_TERMIO
  state.termio.c_iflag = 0;
  state.termio.c_oflag = 0;
  state.termio.c_lflag = 0;
  state.termio.c_cflag &= ~(CSIZE | PARENB);
  state.termio.c_cflag |= CLOCAL | CS8;
  state.termio.c_cc[VMIN] = 0;
  state.termio.c_cc[VTIME] = 0;
#endif

#ifdef HAVE_SGTTY
  state.sgttyb.sg_flags |= RAW | ANYP;
  state.sgttyb.sg_flags &= ~(CBREAK | ECHO);
#endif

  scb->current_timeout = 0;

  if (set_tty_state (scb, &state))
    fprintf_unfiltered (gdb_stderr, "set_tty_state failed: %s\n", safe_strerror (errno));
}

/* Wait for input on scb, with timeout seconds.  Returns 0 on success,
   otherwise SERIAL_TIMEOUT or SERIAL_ERROR.

   For termio{s}, we actually just setup VTIME if necessary, and let the
   timeout occur in the read() in hardwire_read().
 */

/* FIXME: cagney/1999-09-16: Don't replace this with the equivalent
   ser_unix*() until the old TERMIOS/SGTTY/... timer code has been
   flushed. . */

/* NOTE: cagney/1999-09-30: Much of the code below is dead.  The only
   possible values of the TIMEOUT parameter are ONE and ZERO.
   Consequently all the code that tries to handle the possability of
   an overflowed timer is unnecessary. */

static int
wait_for (struct serial *scb, int timeout)
{
#ifdef HAVE_SGTTY
  while (1)
    {
      struct timeval tv;
      fd_set readfds;
      int numfds;

      /* NOTE: Some OS's can scramble the READFDS when the select()
         call fails (ex the kernel with Red Hat 5.2).  Initialize all
         arguments before each call. */

      tv.tv_sec = timeout;
      tv.tv_usec = 0;

      FD_ZERO (&readfds);
      FD_SET (scb->fd, &readfds);

      if (timeout >= 0)
	numfds = select (scb->fd + 1, &readfds, 0, 0, &tv);
      else
	numfds = select (scb->fd + 1, &readfds, 0, 0, 0);

      if (numfds <= 0)
	if (numfds == 0)
	  return SERIAL_TIMEOUT;
	else if (errno == EINTR)
	  continue;
	else
	  return SERIAL_ERROR;	/* Got an error from select or poll */

      return 0;
    }
#endif /* HAVE_SGTTY */

#if defined HAVE_TERMIO || defined HAVE_TERMIOS
  if (timeout == scb->current_timeout)
    return 0;

  scb->current_timeout = timeout;

  {
    struct hardwire_ttystate state;

    if (get_tty_state (scb, &state))
      fprintf_unfiltered (gdb_stderr, "get_tty_state failed: %s\n", safe_strerror (errno));

#ifdef HAVE_TERMIOS
    if (timeout < 0)
      {
	/* No timeout.  */
	state.termios.c_cc[VTIME] = 0;
	state.termios.c_cc[VMIN] = 1;
      }
    else
      {
	state.termios.c_cc[VMIN] = 0;
	state.termios.c_cc[VTIME] = timeout * 10;
	if (state.termios.c_cc[VTIME] != timeout * 10)
	  {

	    /* If c_cc is an 8-bit signed character, we can't go 
	       bigger than this.  If it is always unsigned, we could use
	       25.  */

	    scb->current_timeout = 12;
	    state.termios.c_cc[VTIME] = scb->current_timeout * 10;
	    scb->timeout_remaining = timeout - scb->current_timeout;
	  }
      }
#endif

#ifdef HAVE_TERMIO
    if (timeout < 0)
      {
	/* No timeout.  */
	state.termio.c_cc[VTIME] = 0;
	state.termio.c_cc[VMIN] = 1;
      }
    else
      {
	state.termio.c_cc[VMIN] = 0;
	state.termio.c_cc[VTIME] = timeout * 10;
	if (state.termio.c_cc[VTIME] != timeout * 10)
	  {
	    /* If c_cc is an 8-bit signed character, we can't go 
	       bigger than this.  If it is always unsigned, we could use
	       25.  */

	    scb->current_timeout = 12;
	    state.termio.c_cc[VTIME] = scb->current_timeout * 10;
	    scb->timeout_remaining = timeout - scb->current_timeout;
	  }
      }
#endif

    if (set_tty_state (scb, &state))
      fprintf_unfiltered (gdb_stderr, "set_tty_state failed: %s\n", safe_strerror (errno));

    return 0;
  }
#endif /* HAVE_TERMIO || HAVE_TERMIOS */
}

/* Read a character with user-specified timeout.  TIMEOUT is number of seconds
   to wait, or -1 to wait forever.  Use timeout of 0 to effect a poll.  Returns
   char if successful.  Returns SERIAL_TIMEOUT if timeout expired, EOF if line
   dropped dead, or SERIAL_ERROR for any other error (see errno in that case).  */

/* FIXME: cagney/1999-09-16: Don't replace this with the equivalent
   ser_unix*() until the old TERMIOS/SGTTY/... timer code has been
   flushed. */

/* NOTE: cagney/1999-09-16: This function is not identical to
   ser_unix_readchar() as part of replacing it with ser_unix*()
   merging will be required - this code handles the case where read()
   times out due to no data while ser_unix_readchar() doesn't expect
   that. */

static int
do_hardwire_readchar (struct serial *scb, int timeout)
{
  int status, delta;
  int detach = 0;

  if (timeout > 0)
    timeout++;

  /* We have to be able to keep the GUI alive here, so we break the original
     timeout into steps of 1 second, running the "keep the GUI alive" hook 
     each time through the loop.
     Also, timeout = 0 means to poll, so we just set the delta to 0, so we
     will only go through the loop once. */

  delta = (timeout == 0 ? 0 : 1);
  while (1)
    {

      /* N.B. The UI may destroy our world (for instance by calling
         remote_stop,) in which case we want to get out of here as
         quickly as possible.  It is not safe to touch scb, since
         someone else might have freed it.  The ui_loop_hook signals that 
         we should exit by returning 1. */

      if (ui_loop_hook)
	detach = ui_loop_hook (0);

      if (detach)
	return SERIAL_TIMEOUT;

      scb->timeout_remaining = (timeout < 0 ? timeout : timeout - delta);
      status = wait_for (scb, delta);

      if (status < 0)
	return status;

      status = read (scb->fd, scb->buf, BUFSIZ);

      if (status <= 0)
	{
	  if (status == 0)
	    {
	      /* Zero characters means timeout (it could also be EOF, but
	         we don't (yet at least) distinguish).  */
	      if (scb->timeout_remaining > 0)
		{
		  timeout = scb->timeout_remaining;
		  continue;
		}
	      else if (scb->timeout_remaining < 0)
		continue;
	      else
		return SERIAL_TIMEOUT;
	    }
	  else if (errno == EINTR)
	    continue;
	  else
	    return SERIAL_ERROR;	/* Got an error from read */
	}

      scb->bufcnt = status;
      scb->bufcnt--;
      scb->bufp = scb->buf;
      return *scb->bufp++;
    }
}

static int
hardwire_readchar (struct serial *scb, int timeout)
{
  return generic_readchar (scb, timeout, do_hardwire_readchar);
}


#ifndef B19200
#define B19200 EXTA
#endif

#ifndef B38400
#define B38400 EXTB
#endif

/* Translate baud rates from integers to damn B_codes.  Unix should
   have outgrown this crap years ago, but even POSIX wouldn't buck it.  */

static struct
{
  int rate;
  int code;
}
baudtab[] =
{
  {
    50, B50
  }
  ,
  {
    75, B75
  }
  ,
  {
    110, B110
  }
  ,
  {
    134, B134
  }
  ,
  {
    150, B150
  }
  ,
  {
    200, B200
  }
  ,
  {
    300, B300
  }
  ,
  {
    600, B600
  }
  ,
  {
    1200, B1200
  }
  ,
  {
    1800, B1800
  }
  ,
  {
    2400, B2400
  }
  ,
  {
    4800, B4800
  }
  ,
  {
    9600, B9600
  }
  ,
  {
    19200, B19200
  }
  ,
  {
    38400, B38400
  }
  ,
#ifdef B57600
  {
    57600, B57600
  }
  ,
#endif
#ifdef B115200
  {
    115200, B115200
  }
  ,
#endif
#ifdef B230400
  {
    230400, B230400
  }
  ,
#endif
#ifdef B460800
  {
    460800, B460800
  }
  ,
#endif
  {
    -1, -1
  }
  ,
};

static int
rate_to_code (int rate)
{
  int i;

  for (i = 0; baudtab[i].rate != -1; i++)
    {
      /* test for perfect macth. */
      if (rate == baudtab[i].rate)
        return baudtab[i].code;
      else
        {
	  /* check if it is in between valid values. */
          if (rate < baudtab[i].rate)
	    {
	      if (i)
	        {
	          warning ("Invalid baud rate %d.  Closest values are %d and %d.",
	                    rate, baudtab[i - 1].rate, baudtab[i].rate);
		}
	      else
	        {
	          warning ("Invalid baud rate %d.  Minimum value is %d.",
	                    rate, baudtab[0].rate);
		}
	      return -1;
	    }
        }
    }
 
  /* The requested speed was too large. */
  warning ("Invalid baud rate %d.  Maximum value is %d.",
            rate, baudtab[i - 1].rate);
  return -1;
}

static int
hardwire_setbaudrate (struct serial *scb, int rate)
{
  struct hardwire_ttystate state;
  int baud_code = rate_to_code (rate);
  
  if (baud_code < 0)
    {
      /* The baud rate was not valid.
         A warning has already been issued. */
      errno = EINVAL;
      return -1;
    }

  if (get_tty_state (scb, &state))
    return -1;

#ifdef HAVE_TERMIOS
  cfsetospeed (&state.termios, baud_code);
  cfsetispeed (&state.termios, baud_code);
#endif

#ifdef HAVE_TERMIO
#ifndef CIBAUD
#define CIBAUD CBAUD
#endif

  state.termio.c_cflag &= ~(CBAUD | CIBAUD);
  state.termio.c_cflag |= baud_code;
#endif

#ifdef HAVE_SGTTY
  state.sgttyb.sg_ispeed = baud_code;
  state.sgttyb.sg_ospeed = baud_code;
#endif

  return set_tty_state (scb, &state);
}

static int
hardwire_setstopbits (struct serial *scb, int num)
{
  struct hardwire_ttystate state;
  int newbit;

  if (get_tty_state (scb, &state))
    return -1;

  switch (num)
    {
    case SERIAL_1_STOPBITS:
      newbit = 0;
      break;
    case SERIAL_1_AND_A_HALF_STOPBITS:
    case SERIAL_2_STOPBITS:
      newbit = 1;
      break;
    default:
      return 1;
    }

#ifdef HAVE_TERMIOS
  if (!newbit)
    state.termios.c_cflag &= ~CSTOPB;
  else
    state.termios.c_cflag |= CSTOPB;	/* two bits */
#endif

#ifdef HAVE_TERMIO
  if (!newbit)
    state.termio.c_cflag &= ~CSTOPB;
  else
    state.termio.c_cflag |= CSTOPB;	/* two bits */
#endif

#ifdef HAVE_SGTTY
  return 0;			/* sgtty doesn't support this */
#endif

  return set_tty_state (scb, &state);
}

static void
hardwire_close (struct serial *scb)
{
  if (scb->fd < 0)
    return;

  close (scb->fd);
  scb->fd = -1;
}


/* Generic operations used by all UNIX/FD based serial interfaces. */

serial_ttystate
ser_unix_nop_get_tty_state (struct serial *scb)
{
  /* allocate a dummy */
  return (serial_ttystate) XMALLOC (int);
}

int
ser_unix_nop_set_tty_state (struct serial *scb, serial_ttystate ttystate)
{
  return 0;
}

void
ser_unix_nop_raw (struct serial *scb)
{
  return;			/* Always in raw mode */
}

/* Wait for input on scb, with timeout seconds.  Returns 0 on success,
   otherwise SERIAL_TIMEOUT or SERIAL_ERROR. */

int
ser_unix_wait_for (struct serial *scb, int timeout)
{
  while (1)
    {
      int numfds;
      struct timeval tv;
      fd_set readfds, exceptfds;

      /* NOTE: Some OS's can scramble the READFDS when the select()
         call fails (ex the kernel with Red Hat 5.2).  Initialize all
         arguments before each call. */

      tv.tv_sec = timeout;
      tv.tv_usec = 0;

      FD_ZERO (&readfds);
      FD_ZERO (&exceptfds);
      FD_SET (scb->fd, &readfds);
      FD_SET (scb->fd, &exceptfds);

      if (timeout >= 0)
	numfds = select (scb->fd + 1, &readfds, 0, &exceptfds, &tv);
      else
	numfds = select (scb->fd + 1, &readfds, 0, &exceptfds, 0);

      if (numfds <= 0)
	{
	  if (numfds == 0)
	    return SERIAL_TIMEOUT;
	  else if (errno == EINTR)
	    continue;
	  else
	    return SERIAL_ERROR;	/* Got an error from select or poll */
	}

      return 0;
    }
}

/* Read a character with user-specified timeout.  TIMEOUT is number of seconds
   to wait, or -1 to wait forever.  Use timeout of 0 to effect a poll.  Returns
   char if successful.  Returns -2 if timeout expired, EOF if line dropped
   dead, or -3 for any other error (see errno in that case). */

static int
do_unix_readchar (struct serial *scb, int timeout)
{
  int status;
  int delta;

  /* We have to be able to keep the GUI alive here, so we break the original
     timeout into steps of 1 second, running the "keep the GUI alive" hook 
     each time through the loop.

     Also, timeout = 0 means to poll, so we just set the delta to 0, so we
     will only go through the loop once. */

  delta = (timeout == 0 ? 0 : 1);
  while (1)
    {

      /* N.B. The UI may destroy our world (for instance by calling
         remote_stop,) in which case we want to get out of here as
         quickly as possible.  It is not safe to touch scb, since
         someone else might have freed it.  The ui_loop_hook signals that 
         we should exit by returning 1. */

      if (ui_loop_hook)
	{
	  if (ui_loop_hook (0))
	    return SERIAL_TIMEOUT;
	}

      status = ser_unix_wait_for (scb, delta);
      if (timeout > 0)
        timeout -= delta;

      /* If we got a character or an error back from wait_for, then we can 
         break from the loop before the timeout is completed. */

      if (status != SERIAL_TIMEOUT)
	{
	  break;
	}

      /* If we have exhausted the original timeout, then generate
         a SERIAL_TIMEOUT, and pass it out of the loop. */

      else if (timeout == 0)
	{
	  status = SERIAL_TIMEOUT;
	  break;
	}
    }

  if (status < 0)
    return status;

  while (1)
    {
      status = read (scb->fd, scb->buf, BUFSIZ);
      if (status != -1 || errno != EINTR)
	break;
    }

  if (status <= 0)
    {
      if (status == 0)
	return SERIAL_TIMEOUT;	/* 0 chars means timeout [may need to
				   distinguish between EOF & timeouts
				   someday] */
      else
	return SERIAL_ERROR;	/* Got an error from read */
    }

  scb->bufcnt = status;
  scb->bufcnt--;
  scb->bufp = scb->buf;
  return *scb->bufp++;
}

/* Perform operations common to both old and new readchar. */

/* Return the next character from the input FIFO.  If the FIFO is
   empty, call the SERIAL specific routine to try and read in more
   characters.

   Initially data from the input FIFO is returned (fd_event()
   pre-reads the input into that FIFO.  Once that has been emptied,
   further data is obtained by polling the input FD using the device
   specific readchar() function.  Note: reschedule() is called after
   every read.  This is because there is no guarentee that the lower
   level fd_event() poll_event() code (which also calls reschedule())
   will be called. */

static int
generic_readchar (struct serial *scb, int timeout,
		  int (do_readchar) (struct serial *scb, int timeout))
{
  int ch;
  if (scb->bufcnt > 0)
    {
      ch = *scb->bufp;
      scb->bufcnt--;
      scb->bufp++;
    }
  else if (scb->bufcnt < 0)
    {
      /* Some errors/eof are are sticky. */
      ch = scb->bufcnt;
    }
  else
    {
      ch = do_readchar (scb, timeout);
      if (ch < 0)
	{
	  switch ((enum serial_rc) ch)
	    {
	    case SERIAL_EOF:
	    case SERIAL_ERROR:
	      /* Make the error/eof stick. */
	      scb->bufcnt = ch;
	      break;
	    case SERIAL_TIMEOUT:
	      scb->bufcnt = 0;
	      break;
	    }
	}
    }
  reschedule (scb);
  return ch;
}

int
ser_unix_readchar (struct serial *scb, int timeout)
{
  return generic_readchar (scb, timeout, do_unix_readchar);
}

int
ser_unix_nop_noflush_set_tty_state (struct serial *scb,
				    serial_ttystate new_ttystate,
				    serial_ttystate old_ttystate)
{
  return 0;
}

void
ser_unix_nop_print_tty_state (struct serial *scb, 
			      serial_ttystate ttystate,
			      struct ui_file *stream)
{
  /* Nothing to print.  */
  return;
}

int
ser_unix_nop_setbaudrate (struct serial *scb, int rate)
{
  return 0;			/* Never fails! */
}

int
ser_unix_nop_setstopbits (struct serial *scb, int num)
{
  return 0;			/* Never fails! */
}

int
ser_unix_write (struct serial *scb, const char *str, int len)
{
  int cc;

  while (len > 0)
    {
      cc = write (scb->fd, str, len);

      if (cc < 0)
	return 1;
      len -= cc;
      str += cc;
    }
  return 0;
}

int
ser_unix_nop_flush_output (struct serial *scb)
{
  return 0;
}

int
ser_unix_flush_input (struct serial *scb)
{
  if (scb->bufcnt >= 0)
    {
      scb->bufcnt = 0;
      scb->bufp = scb->buf;
      return 0;
    }
  else
    return SERIAL_ERROR;
}

int
ser_unix_nop_send_break (struct serial *scb)
{
  return 0;
}

int
ser_unix_nop_drain_output (struct serial *scb)
{
  return 0;
}



/* Event handling for ASYNC serial code.

   At any time the SERIAL device either: has an empty FIFO and is
   waiting on a FD event; or has a non-empty FIFO/error condition and
   is constantly scheduling timer events.

   ASYNC only stops pestering its client when it is de-async'ed or it
   is told to go away. */

/* Value of scb->async_state: */
enum {
  /* >= 0 (TIMER_SCHEDULED) */
  /* The ID of the currently scheduled timer event. This state is
     rarely encountered.  Timer events are one-off so as soon as the
     event is delivered the state is shanged to NOTHING_SCHEDULED. */
  FD_SCHEDULED = -1,
  /* The fd_event() handler is scheduled.  It is called when ever the
     file descriptor becomes ready. */
  NOTHING_SCHEDULED = -2
  /* Either no task is scheduled (just going into ASYNC mode) or a
     timer event has just gone off and the current state has been
     forced into nothing scheduled. */
};

/* Identify and schedule the next ASYNC task based on scb->async_state
   and scb->buf* (the input FIFO).  A state machine is used to avoid
   the need to make redundant calls into the event-loop - the next
   scheduled task is only changed when needed. */

static void
reschedule (struct serial *scb)
{
  if (serial_is_async_p (scb))
    {
      int next_state;
      switch (scb->async_state)
	{
	case FD_SCHEDULED:
	  if (scb->bufcnt == 0)
	    next_state = FD_SCHEDULED;
	  else
	    {
	      delete_file_handler (scb->fd);
	      next_state = create_timer (0, push_event, scb);
	    }
	  break;
	case NOTHING_SCHEDULED:
	  if (scb->bufcnt == 0)
	    {
	      add_file_handler (scb->fd, fd_event, scb);
	      next_state = FD_SCHEDULED;
	    }
	  else
	    {
	      next_state = create_timer (0, push_event, scb);
	    }
	  break;
	default: /* TIMER SCHEDULED */
	  if (scb->bufcnt == 0)
	    {
	      delete_timer (scb->async_state);
	      add_file_handler (scb->fd, fd_event, scb);
	      next_state = FD_SCHEDULED;
	    }
	  else
	    next_state = scb->async_state;
	  break;
	}
      if (serial_debug_p (scb))
	{
	  switch (next_state)
	    {
	    case FD_SCHEDULED:
	      if (scb->async_state != FD_SCHEDULED)
		fprintf_unfiltered (gdb_stdlog, "[fd%d->fd-scheduled]\n",
				    scb->fd);
	      break;
	    default: /* TIMER SCHEDULED */
	      if (scb->async_state == FD_SCHEDULED)
		fprintf_unfiltered (gdb_stdlog, "[fd%d->timer-scheduled]\n",
				    scb->fd);
	      break;
	    }
	}
      scb->async_state = next_state;
    }
}

/* FD_EVENT: This is scheduled when the input FIFO is empty (and there
   is no pending error).  As soon as data arrives, it is read into the
   input FIFO and the client notified.  The client should then drain
   the FIFO using readchar().  If the FIFO isn't immediatly emptied,
   push_event() is used to nag the client until it is. */

static void
fd_event (int error, void *context)
{
  struct serial *scb = context;
  if (error != 0)
    {
      scb->bufcnt = SERIAL_ERROR;
    }
  else if (scb->bufcnt == 0)
    {
      /* Prime the input FIFO.  The readchar() function is used to
         pull characters out of the buffer.  See also
         generic_readchar(). */
      int nr;
      do
	{
	  nr = read (scb->fd, scb->buf, BUFSIZ);
	}
      while (nr == -1 && errno == EINTR);
      if (nr == 0)
	{
	  scb->bufcnt = SERIAL_EOF;
	}
      else if (nr > 0)
	{
	  scb->bufcnt = nr;
	  scb->bufp = scb->buf;
	}
      else
	{
	  scb->bufcnt = SERIAL_ERROR;
	}
    }
  scb->async_handler (scb, scb->async_context);
  reschedule (scb);
}

/* PUSH_EVENT: The input FIFO is non-empty (or there is a pending
   error).  Nag the client until all the data has been read.  In the
   case of errors, the client will need to close or de-async the
   device before naging stops. */

static void
push_event (void *context)
{
  struct serial *scb = context;
  scb->async_state = NOTHING_SCHEDULED; /* Timers are one-off */
  scb->async_handler (scb, scb->async_context);
  /* re-schedule */
  reschedule (scb);
}

/* Put the SERIAL device into/out-of ASYNC mode.  */

void
ser_unix_async (struct serial *scb,
		int async_p)
{
  if (async_p)
    {
      /* Force a re-schedule. */
      scb->async_state = NOTHING_SCHEDULED;
      if (serial_debug_p (scb))
	fprintf_unfiltered (gdb_stdlog, "[fd%d->asynchronous]\n",
			    scb->fd);
      reschedule (scb);
    }
  else
    {
      if (serial_debug_p (scb))
	fprintf_unfiltered (gdb_stdlog, "[fd%d->synchronous]\n",
			    scb->fd);
      /* De-schedule whatever tasks are currently scheduled. */
      switch (scb->async_state)
	{
	case FD_SCHEDULED:
	  delete_file_handler (scb->fd);
	  break;
	case NOTHING_SCHEDULED:
	  break;
	default: /* TIMER SCHEDULED */
	  delete_timer (scb->async_state);
	  break;
	}
    }
}

void
_initialize_ser_hardwire (void)
{
  struct serial_ops *ops = XMALLOC (struct serial_ops);
  memset (ops, 0, sizeof (struct serial_ops));
  ops->name = "hardwire";
  ops->next = 0;
  ops->open = hardwire_open;
  ops->close = hardwire_close;
  /* FIXME: Don't replace this with the equivalent ser_unix*() until
     the old TERMIOS/SGTTY/... timer code has been flushed. cagney
     1999-09-16. */
  ops->readchar = hardwire_readchar;
  ops->write = ser_unix_write;
  ops->flush_output = hardwire_flush_output;
  ops->flush_input = hardwire_flush_input;
  ops->send_break = hardwire_send_break;
  ops->go_raw = hardwire_raw;
  ops->get_tty_state = hardwire_get_tty_state;
  ops->set_tty_state = hardwire_set_tty_state;
  ops->print_tty_state = hardwire_print_tty_state;
  ops->noflush_set_tty_state = hardwire_noflush_set_tty_state;
  ops->setbaudrate = hardwire_setbaudrate;
  ops->setstopbits = hardwire_setstopbits;
  ops->drain_output = hardwire_drain_output;
  ops->async = ser_unix_async;
  serial_add_interface (ops);
}
