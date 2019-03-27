/* Generic support for remote debugging interfaces.

   Copyright 1993, 1994, 2000, 2001 Free Software Foundation, Inc.

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

#ifndef REMOTE_UTILS_H
#define REMOTE_UTILS_H

struct target_ops;

#include "target.h"
struct serial;

/* Stuff that should be shared (and handled consistently) among the various
   remote targets.  */

struct _sr_settings
  {
    unsigned int timeout;

    int retries;

    char *device;
    struct serial *desc;

  };

extern struct _sr_settings sr_settings;

/* get and set debug value. */
#define sr_get_debug()			(remote_debug)
#define sr_set_debug(newval)		(remote_debug = (newval))

/* get and set timeout. */
#define sr_get_timeout()		(sr_settings.timeout)
#define sr_set_timeout(newval)		(sr_settings.timeout = (newval))

/* get and set device. */
#define sr_get_device()			(sr_settings.device)
#define sr_set_device(newval) \
{ \
    if (sr_settings.device) xfree (sr_settings.device); \
    sr_settings.device = (newval); \
}

/* get and set descriptor value. */
#define sr_get_desc()			(sr_settings.desc)
#define sr_set_desc(newval)		(sr_settings.desc = (newval))

/* get and set retries. */
#define sr_get_retries()		(sr_settings.retries)
#define sr_set_retries(newval)		(sr_settings.retries = (newval))

#define sr_is_open()			(sr_settings.desc != NULL)

#define sr_check_open() 	{ if (!sr_is_open()) \
				    error ("Remote device not open"); }

struct gr_settings
  {
    char *prompt;
    struct target_ops *ops;
    int (*clear_all_breakpoints) (void);
    void (*checkin) (void);
  };

extern struct gr_settings *gr_settings;

/* get and set prompt. */
#define gr_get_prompt()			(gr_settings->prompt)
#define gr_set_prompt(newval)		(gr_settings->prompt = (newval))

/* get and set ops. */
#define gr_get_ops()			(gr_settings->ops)
#define gr_set_ops(newval)		(gr_settings->ops = (newval))

#define gr_clear_all_breakpoints()	((gr_settings->clear_all_breakpoints)())
#define gr_checkin()			((gr_settings->checkin)())

/* Keep discarding input until we see the prompt.

   The convention for dealing with the prompt is that you
   o give your command
   o *then* wait for the prompt.

   Thus the last thing that a procedure does with the serial line
   will be an gr_expect_prompt().  Exception:  resume does not
   wait for the prompt, because the terminal is being handed over
   to the inferior.  However, the next thing which happens after that
   is a bug_wait which does wait for the prompt.
   Note that this includes abnormal exit, e.g. error().  This is
   necessary to prevent getting into states from which we can't
   recover.  */

#define gr_expect_prompt()	sr_expect(gr_get_prompt())

int gr_multi_scan (char *list[], int passthrough);
int sr_get_hex_digit (int ignore_space);
int sr_pollchar (void);
int sr_readchar (void);
int sr_timed_read (char *buf, int n);
long sr_get_hex_word (void);
void gr_close (int quitting);
void gr_create_inferior (char *execfile, char *args, char **env);
void gr_detach (char *args, int from_tty);
void gr_files_info (struct target_ops *ops);
void gr_generic_checkin (void);
void gr_kill (void);
void gr_mourn (void);
void gr_prepare_to_store (void);
void sr_expect (char *string);
void sr_get_hex_byte (char *byt);
void sr_scan_args (char *proto, char *args);
void sr_write (char *a, int l);
void sr_write_cr (char *s);

void gr_open (char *args, int from_tty, struct gr_settings *gr_settings);
void gr_load_image (char *, int from_tty);
#endif /* REMOTE_UTILS_H */
