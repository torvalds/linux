/* Data structures associated with tracepoints in GDB.
   Copyright 1997, 1998, 1999, 2000 Free Software Foundation, Inc.

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

#if !defined (TRACEPOINT_H)
#define TRACEPOINT_H 1

/* The data structure for an action: */
struct action_line
  {
    struct action_line *next;
    char *action;
  };

/* The data structure for a tracepoint: */

struct tracepoint
  {
    struct tracepoint *next;

    int enabled_p;

#if 0
    /* Type of tracepoint (MVS FIXME: needed?). */
    enum tptype type;

    /* What to do with this tracepoint after we hit it MVS FIXME: needed?). */
    enum tpdisp disposition;
#endif
    /* Number assigned to distinguish tracepoints.  */
    int number;

    /* Address to trace at, or NULL if not an instruction tracepoint (MVS ?). */
    CORE_ADDR address;

    /* Line number of this address.  Only matters if address is non-NULL.  */
    int line_number;

    /* Source file name of this address.  Only matters if address is non-NULL. */
    char *source_file;

    /* Number of times this tracepoint should single-step 
       and collect additional data */
    long step_count;

    /* Number of times this tracepoint should be hit before disabling/ending. */
    int pass_count;

    /* Chain of action lines to execute when this tracepoint is hit. */
    struct action_line *actions;

    /* Conditional (MVS ?).  */
    struct expression *cond;

    /* String we used to set the tracepoint (malloc'd).  Only matters if
       address is non-NULL.  */
    char *addr_string;

    /* Language we used to set the tracepoint.  */
    enum language language;

    /* Input radix we used to set the tracepoint.  */
    int input_radix;

    /* Count of the number of times this tracepoint was taken, dumped
       with the info, but not used for anything else.  Useful for
       seeing how many times you hit a tracepoint prior to the program
       aborting, so you can back up to just before the abort.  */
    int hit_count;

    /* Thread number for thread-specific tracepoint, or -1 if don't care */
    int thread;

    /* BFD section, in case of overlays: 
       no, I don't know if tracepoints are really gonna work with overlays.  */
    asection *section;
  };

enum actionline_type
  {
    BADLINE = -1,
    GENERIC = 0,
    END = 1,
    STEPPING = 2
  };


/* The tracepoint chain of all tracepoints */

extern struct tracepoint *tracepoint_chain;

extern unsigned long trace_running_p;

/* A hook used to notify the UI of tracepoint operations */

void (*create_tracepoint_hook) (struct tracepoint *);
void (*delete_tracepoint_hook) (struct tracepoint *);
void (*modify_tracepoint_hook) (struct tracepoint *);
void (*trace_find_hook) (char *arg, int from_tty);
void (*trace_start_stop_hook) (int start, int from_tty);

struct tracepoint *get_tracepoint_by_number (char **, int, int);
int get_traceframe_number (void);
void free_actions (struct tracepoint *);
enum actionline_type validate_actionline (char **, struct tracepoint *);


/* Walk the following statement or block through all tracepoints.
   ALL_TRACEPOINTS_SAFE does so even if the statment deletes the current
   breakpoint.  */

#define ALL_TRACEPOINTS(t)  for (t = tracepoint_chain; t; t = t->next)

#define ALL_TRACEPOINTS_SAFE(t,tmp)	\
	for (t = tracepoint_chain;	\
	     t ? (tmp = t->next, 1) : 0;\
	     t = tmp)
#endif /* TRACEPOINT_H */
