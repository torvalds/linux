/* Manages interpreters for GDB, the GNU debugger.

   Copyright 2000, 2002, 2003 Free Software Foundation, Inc.

   Written by Jim Ingham <jingham@apple.com> of Apple Computer, Inc.

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
   Boston, MA 02111-1307, USA. */

#ifndef INTERPS_H
#define INTERPS_H

struct ui_out;
struct interp;

extern int interp_resume (struct interp *interp);
extern int interp_suspend (struct interp *interp);
extern int interp_prompt_p (struct interp *interp);
extern int interp_exec_p (struct interp *interp);
extern int interp_exec (struct interp *interp, const char *command);
extern int interp_quiet_p (struct interp *interp);

typedef void *(interp_init_ftype) (void);
typedef int (interp_resume_ftype) (void *data);
typedef int (interp_suspend_ftype) (void *data);
typedef int (interp_prompt_p_ftype) (void *data);
typedef int (interp_exec_ftype) (void *data, const char *command);
typedef void (interp_command_loop_ftype) (void *data);

struct interp_procs
{
  interp_init_ftype *init_proc;
  interp_resume_ftype *resume_proc;
  interp_suspend_ftype *suspend_proc;
  interp_exec_ftype *exec_proc;
  interp_prompt_p_ftype *prompt_proc_p;
  interp_command_loop_ftype *command_loop_proc;
};

extern struct interp *interp_new (const char *name, void *data,
				  struct ui_out *uiout,
				  const struct interp_procs *procs);
extern void interp_add (struct interp *interp);
extern int interp_set (struct interp *interp);
extern struct interp *interp_lookup (const char *name);
extern struct ui_out *interp_ui_out (struct interp *interp);

extern int current_interp_named_p (const char *name);
extern int current_interp_display_prompt_p (void);
extern void current_interp_command_loop (void);

extern void clear_interpreter_hooks (void);

/* well-known interpreters */
#define INTERP_CONSOLE		"console"
#define INTERP_MI1             "mi1"
#define INTERP_MI2             "mi2"
#define INTERP_MI3             "mi3"
#define INTERP_MI		"mi"
#define INTERP_TUI		"tui"

#endif
