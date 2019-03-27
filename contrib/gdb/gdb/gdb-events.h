/* User Interface Events.

   Copyright 1999, 2001, 2002, 2004 Free Software Foundation, Inc.

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

/* Work in progress */

/* This file was created with the aid of ``gdb-events.sh''.

   The bourn shell script ``gdb-events.sh'' creates the files
   ``new-gdb-events.c'' and ``new-gdb-events.h and then compares
   them against the existing ``gdb-events.[hc]''.  Any differences
   found being reported.

   If editing this file, please also run gdb-events.sh and merge any
   changes into that script. Conversely, when making sweeping changes
   to this file, modifying gdb-events.sh and using its output may
   prove easier. */


#ifndef GDB_EVENTS_H
#define GDB_EVENTS_H

#ifndef WITH_GDB_EVENTS
#define WITH_GDB_EVENTS 1
#endif


/* COMPAT: pointer variables for old, unconverted events.
   A call to set_gdb_events() will automatically update these. */



/* Type definition of all hook functions.
   Recommended pratice is to first declare each hook function using
   the below ftype and then define it. */

typedef void (gdb_events_breakpoint_create_ftype) (int b);
typedef void (gdb_events_breakpoint_delete_ftype) (int b);
typedef void (gdb_events_breakpoint_modify_ftype) (int b);
typedef void (gdb_events_tracepoint_create_ftype) (int number);
typedef void (gdb_events_tracepoint_delete_ftype) (int number);
typedef void (gdb_events_tracepoint_modify_ftype) (int number);
typedef void (gdb_events_architecture_changed_ftype) (void);
typedef void (gdb_events_target_changed_ftype) (void);
typedef void (gdb_events_selected_frame_level_changed_ftype) (int level);
typedef void (gdb_events_selected_thread_changed_ftype) (int thread_num);


/* gdb-events: object. */

struct gdb_events
  {
    gdb_events_breakpoint_create_ftype *breakpoint_create;
    gdb_events_breakpoint_delete_ftype *breakpoint_delete;
    gdb_events_breakpoint_modify_ftype *breakpoint_modify;
    gdb_events_tracepoint_create_ftype *tracepoint_create;
    gdb_events_tracepoint_delete_ftype *tracepoint_delete;
    gdb_events_tracepoint_modify_ftype *tracepoint_modify;
    gdb_events_architecture_changed_ftype *architecture_changed;
    gdb_events_target_changed_ftype *target_changed;
    gdb_events_selected_frame_level_changed_ftype *selected_frame_level_changed;
    gdb_events_selected_thread_changed_ftype *selected_thread_changed;
  };


/* Interface into events functions.
   Where a *_p() predicate is present, it must be called before
   calling the hook proper. */
extern void breakpoint_create_event (int b);
extern void breakpoint_delete_event (int b);
extern void breakpoint_modify_event (int b);
extern void tracepoint_create_event (int number);
extern void tracepoint_delete_event (int number);
extern void tracepoint_modify_event (int number);
extern void architecture_changed_event (void);
extern void target_changed_event (void);
extern void selected_frame_level_changed_event (int level);
extern void selected_thread_changed_event (int thread_num);


/* When GDB_EVENTS are not being used, completely disable them. */

#if !WITH_GDB_EVENTS
#define breakpoint_create_event(b) 0
#define breakpoint_delete_event(b) 0
#define breakpoint_modify_event(b) 0
#define tracepoint_create_event(number) 0
#define tracepoint_delete_event(number) 0
#define tracepoint_modify_event(number) 0
#define architecture_changed_event() 0
#define target_changed_event() 0
#define selected_frame_level_changed_event(level) 0
#define selected_thread_changed_event(thread_num) 0
#endif

/* Install custom gdb-events hooks. */
extern struct gdb_events *set_gdb_event_hooks (struct gdb_events *vector);

/* Deliver any pending events. */
extern void gdb_events_deliver (struct gdb_events *vector);

/* Clear event handlers */
extern void clear_gdb_event_hooks (void);

#if !WITH_GDB_EVENTS
#define set_gdb_events(x) 0
#define set_gdb_event_hooks(x) 0
#define gdb_events_deliver(x) 0
#endif

#endif
