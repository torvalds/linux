/* Definitions used by event-top.c, for GDB, the GNU debugger.

   Copyright 1999, 2001, 2003 Free Software Foundation, Inc.

   Written by Elena Zannoni <ezannoni@cygnus.com> of Cygnus Solutions.

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

#ifndef EVENT_TOP_H
#define EVENT_TOP_H

struct cmd_list_element;

/* Stack for prompts.  Each prompt is composed as a prefix, a prompt
   and a suffix.  The prompt to be displayed at any given time is the
   one on top of the stack.  A stack is necessary because of cases in
   which the execution of a gdb command requires further input from
   the user, like for instance 'commands' for breakpoints and
   'actions' for tracepoints.  In these cases, the prompt is '>' and
   gdb should process input using the asynchronous readline interface
   and the event loop.  In order to achieve this, we need to save
   somewhere the state of GDB, i.e. that it is processing user input
   as part of a command and not as part of the top level command loop.
   The prompt stack represents part of the saved state.  Another part
   would be the function that readline would invoke after a whole line
   of input has ben entered.  This second piece would be something
   like, for instance, where to return within the code for the actions
   commands after a line has been read.  This latter portion has not
   beeen implemented yet.  The need for a 3-part prompt arises from
   the annotation level.  When this is set to 2, the prompt is
   actually composed of a prefix, the prompt itself and a suffix.  */

/* At any particular time there will be always at least one prompt on
   the stack, the one being currently displayed by gdb.  If gdb is
   using annotation level equal 2, there will be 2 prompts on the
   stack: the usual one, w/o prefix and suffix (at top - 1), and the
   'composite' one with prefix and suffix added (at top).  At this
   time, this is the only use of the prompt stack.  Resetting annotate
   to 0 or 1, pops the top of the stack, resetting its size to one
   element.  The MAXPROMPTS limit is safe, for now.  Once other cases
   are dealt with (like the different prompts used for 'commands' or
   'actions') this array implementation of the prompt stack may have
   to change.  */

#define MAXPROMPTS 10
struct prompts
  {
    struct
      {
	char *prefix;
	char *prompt;
	char *suffix;
      }
    prompt_stack[MAXPROMPTS];
    int top;
  };

#define PROMPT(X) the_prompts.prompt_stack[the_prompts.top + X].prompt
#define PREFIX(X) the_prompts.prompt_stack[the_prompts.top + X].prefix
#define SUFFIX(X) the_prompts.prompt_stack[the_prompts.top + X].suffix

/* Exported functions from event-top.c. 
   FIXME: these should really go into top.h.  */

extern void display_gdb_prompt (char *new_prompt);
void gdb_setup_readline (void);
void gdb_disable_readline (void);
extern void async_init_signals (void);
extern void set_async_editing_command (char *args, int from_tty,
				       struct cmd_list_element *c);
extern void set_async_annotation_level (char *args, int from_tty,
					struct cmd_list_element *c);
extern void set_async_prompt (char *args, int from_tty,
			      struct cmd_list_element *c);

/* Signal to catch ^Z typed while reading a command: SIGTSTP or SIGCONT.  */
#ifndef STOP_SIGNAL
#include <signal.h>
#ifdef SIGTSTP
#define STOP_SIGNAL SIGTSTP
extern void handle_stop_sig (int sig);
#endif
#endif
extern void handle_sigint (int sig);
extern void pop_prompt (void);
extern void push_prompt (char *prefix, char *prompt, char *suffix);
extern void gdb_readline2 (void *client_data);
extern void mark_async_signal_handler_wrapper (void *token);
extern void async_request_quit (void *arg);
extern void stdin_event_handler (int error, void *client_data);
extern void async_disable_stdin (void);
extern void async_enable_stdin (void *dummy);

/* Exported variables from event-top.c.
   FIXME: these should really go into top.h.  */

extern int async_command_editing_p;
extern int exec_done_display_p;
extern char *async_annotation_suffix;
extern char *new_async_prompt;
extern struct prompts the_prompts;
extern void (*call_readline) (void *);
extern void (*input_handler) (char *);
extern int input_fd;
extern void (*after_char_processing_hook) (void);

extern void cli_command_loop (void);

#endif
