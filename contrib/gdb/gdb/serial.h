/* Remote serial support interface definitions for GDB, the GNU Debugger.
   Copyright 1992, 1993, 1994, 1995, 1996, 1998, 1999, 2000
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

#ifndef SERIAL_H
#define SERIAL_H

struct ui_file;

/* For most routines, if a failure is indicated, then errno should be
   examined.  */

/* Terminal state pointer.  This is specific to each type of
   interface. */

typedef void *serial_ttystate;
struct serial;

/* Try to open NAME.  Returns a new `struct serial *' on success, NULL
   on failure. Note that some open calls can block and, if possible, 
   should be  written to be non-blocking, with calls to ui_look_hook 
   so they can be cancelled. An async interface for open could be
   added to GDB if necessary. */

extern struct serial *serial_open (const char *name);

/* Open a new serial stream using a file handle.  */

extern struct serial *serial_fdopen (const int fd);

/* Push out all buffers, close the device and destroy SCB. */

extern void serial_close (struct serial *scb);

/* Push out all buffers and destroy SCB without closing the device.  */

extern void serial_un_fdopen (struct serial *scb);

/* Read one char from the serial device with TIMEOUT seconds to wait
   or -1 to wait forever.  Use timeout of 0 to effect a poll.
   Infinite waits are not permitted. Returns unsigned char if ok, else
   one of the following codes.  Note that all error return-codes are
   guaranteed to be < 0. */

enum serial_rc {
  SERIAL_ERROR = -1,	/* General error. */
  SERIAL_TIMEOUT = -2,	/* Timeout or data-not-ready during read.
			   Unfortunately, through ui_loop_hook(), this
			   can also be a QUIT indication.  */
  SERIAL_EOF = -3	/* General end-of-file or remote target
			   connection closed, indication.  Includes
			   things like the line dropping dead. */
};

extern int serial_readchar (struct serial *scb, int timeout);

/* Write LEN chars from STRING to the port SCB.  Returns 0 for
   success, non-zero for failure.  */

extern int serial_write (struct serial *scb, const char *str, int len);

/* Write a printf style string onto the serial port. */

extern void serial_printf (struct serial *desc, const char *,...) ATTR_FORMAT (printf, 2, 3);

/* Allow pending output to drain. */

extern int serial_drain_output (struct serial *);

/* Flush (discard) pending output.  Might also flush input (if this
   system can't flush only output).  */

extern int serial_flush_output (struct serial *);

/* Flush pending input.  Might also flush output (if this system can't
   flush only input).  */

extern int serial_flush_input (struct serial *);

/* Send a break between 0.25 and 0.5 seconds long.  */

extern int serial_send_break (struct serial *scb);

/* Turn the port into raw mode. */

extern void serial_raw (struct serial *scb);

/* Return a pointer to a newly malloc'd ttystate containing the state
   of the tty.  */

extern serial_ttystate serial_get_tty_state (struct serial *scb);

/* Set the state of the tty to TTYSTATE.  The change is immediate.
   When changing to or from raw mode, input might be discarded.
   Returns 0 for success, negative value for error (in which case
   errno contains the error).  */

extern int serial_set_tty_state (struct serial *scb, serial_ttystate ttystate);

/* printf_filtered a user-comprehensible description of ttystate on
   the specified STREAM. FIXME: At present this sends output to the
   default stream - GDB_STDOUT. */

extern void serial_print_tty_state (struct serial *scb, serial_ttystate ttystate, struct ui_file *);

/* Set the tty state to NEW_TTYSTATE, where OLD_TTYSTATE is the
   current state (generally obtained from a recent call to
   serial_get_tty_state()), but be careful not to discard any input.
   This means that we never switch in or out of raw mode, even if
   NEW_TTYSTATE specifies a switch.  */

extern int serial_noflush_set_tty_state (struct serial *scb, serial_ttystate new_ttystate, serial_ttystate old_ttystate);

/* Set the baudrate to the decimal value supplied.  Returns 0 for
   success, -1 for failure.  */

extern int serial_setbaudrate (struct serial *scb, int rate);

/* Set the number of stop bits to the value specified.  Returns 0 for
   success, -1 for failure.  */

#define SERIAL_1_STOPBITS 1
#define SERIAL_1_AND_A_HALF_STOPBITS 2	/* 1.5 bits, snicker... */
#define SERIAL_2_STOPBITS 3

extern int serial_setstopbits (struct serial *scb, int num);

/* Asynchronous serial interface: */

/* Can the serial device support asynchronous mode? */

extern int serial_can_async_p (struct serial *scb);

/* Has the serial device been put in asynchronous mode? */

extern int serial_is_async_p (struct serial *scb);

/* For ASYNC enabled devices, register a callback and enable
   asynchronous mode.  To disable asynchronous mode, register a NULL
   callback. */

typedef void (serial_event_ftype) (struct serial *scb, void *context);
extern void serial_async (struct serial *scb, serial_event_ftype *handler, void *context);

/* Provide direct access to the underlying FD (if any) used to
   implement the serial device.  This interface is clearly
   deprecated. Will call internal_error() if the operation isn't
   applicable to the current serial device. */

extern int deprecated_serial_fd (struct serial *scb);

/* Trace/debug mechanism.

   serial_debug() enables/disables internal debugging.
   serial_debug_p() indicates the current debug state. */

extern void serial_debug (struct serial *scb, int debug_p);

extern int serial_debug_p (struct serial *scb);


/* Details of an instance of a serial object */

struct serial
  {
    int fd;			/* File descriptor */
    struct serial_ops *ops;	/* Function vector */
    void *state;       		/* Local context info for open FD */
    serial_ttystate ttystate;	/* Not used (yet) */
    int bufcnt;			/* Amount of data remaining in receive
				   buffer.  -ve for sticky errors. */
    unsigned char *bufp;	/* Current byte */
    unsigned char buf[BUFSIZ];	/* Da buffer itself */
    int current_timeout;	/* (ser-unix.c termio{,s} only), last
				   value of VTIME */
    int timeout_remaining;	/* (ser-unix.c termio{,s} only), we
				   still need to wait for this many
				   more seconds.  */
    char *name;			/* The name of the device or host */
    struct serial *next;	/* Pointer to the next `struct serial *' */
    int refcnt;			/* Number of pointers to this block */
    int debug_p;		/* Trace this serial devices operation. */
    int async_state;		/* Async internal state. */
    void *async_context;	/* Async event thread's context */
    serial_event_ftype *async_handler;/* Async event handler */
  };

struct serial_ops
  {
    char *name;
    struct serial_ops *next;
    int (*open) (struct serial *, const char *name);
    void (*close) (struct serial *);
    int (*readchar) (struct serial *, int timeout);
    int (*write) (struct serial *, const char *str, int len);
    /* Discard pending output */
    int (*flush_output) (struct serial *);
    /* Discard pending input */
    int (*flush_input) (struct serial *);
    int (*send_break) (struct serial *);
    void (*go_raw) (struct serial *);
    serial_ttystate (*get_tty_state) (struct serial *);
    int (*set_tty_state) (struct serial *, serial_ttystate);
    void (*print_tty_state) (struct serial *, serial_ttystate,
			     struct ui_file *);
    int (*noflush_set_tty_state) (struct serial *, serial_ttystate,
				  serial_ttystate);
    int (*setbaudrate) (struct serial *, int rate);
    int (*setstopbits) (struct serial *, int num);
    /* Wait for output to drain */
    int (*drain_output) (struct serial *);
    /* Change the serial device into/out of asynchronous mode, call
       the specified function when ever there is something
       interesting. */
    void (*async) (struct serial *scb, int async_p);
  };

/* Add a new serial interface to the interface list */

extern void serial_add_interface (struct serial_ops * optable);

/* File in which to record the remote debugging session */

extern void serial_log_command (const char *);

#endif /* SERIAL_H */
