/* MI Command Set for GDB, the GNU debugger.

   Copyright 2000, 2003, 2004 Free Software Foundation, Inc.

   Contributed by Cygnus Solutions (a Red Hat company).

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

#ifndef MI_CMDS_H
#define MI_CMDS_H

/* An MI command can return any of the following. */

enum mi_cmd_result
  {
    /* Report the command as ``done''.  Display both the ``NNN^done''
       message and the completion prompt.  */
    MI_CMD_DONE = 0,
    /* The command is still running in the forground.  Main loop should
       display the completion prompt. */
    MI_CMD_FORGROUND,
    /* An error condition was detected and an error message was
       asprintf'd into the mi_error_message buffer.  The main loop will
       display the error message and the completion prompt. */
    MI_CMD_ERROR,
    /* An error condition was detected and caught.  The error message is
       in the global error message buffer. The main loop will display
       the error message and the completion prompt. */
    MI_CMD_CAUGHT_ERROR,
    /* The MI command has already displayed its completion message.
       Main loop will not display a completion message but will display
       the completion prompt. */
    MI_CMD_QUIET
  };

enum print_values {
   PRINT_NO_VALUES,
   PRINT_ALL_VALUES,
   PRINT_SIMPLE_VALUES
};

typedef enum mi_cmd_result (mi_cmd_argv_ftype) (char *command, char **argv, int argc);

/* Older MI commands have this interface. Retained until all old
   commands are flushed. */

typedef enum mi_cmd_result (mi_cmd_args_ftype) ( /*ui */ char *args, int from_tty);

/* Function implementing each command */
extern mi_cmd_argv_ftype mi_cmd_break_insert;
extern mi_cmd_argv_ftype mi_cmd_break_watch;
extern mi_cmd_argv_ftype mi_cmd_disassemble;
extern mi_cmd_argv_ftype mi_cmd_data_evaluate_expression;
extern mi_cmd_argv_ftype mi_cmd_data_list_register_names;
extern mi_cmd_argv_ftype mi_cmd_data_list_register_values;
extern mi_cmd_argv_ftype mi_cmd_data_list_changed_registers;
extern mi_cmd_argv_ftype mi_cmd_data_read_memory;
extern mi_cmd_argv_ftype mi_cmd_data_write_memory;
extern mi_cmd_argv_ftype mi_cmd_data_write_register_values;
extern mi_cmd_argv_ftype mi_cmd_env_cd;
extern mi_cmd_argv_ftype mi_cmd_env_dir;
extern mi_cmd_argv_ftype mi_cmd_env_path;
extern mi_cmd_argv_ftype mi_cmd_env_pwd;
extern mi_cmd_args_ftype mi_cmd_exec_continue;
extern mi_cmd_args_ftype mi_cmd_exec_finish;
extern mi_cmd_args_ftype mi_cmd_exec_next;
extern mi_cmd_args_ftype mi_cmd_exec_next_instruction;
extern mi_cmd_args_ftype mi_cmd_exec_return;
extern mi_cmd_args_ftype mi_cmd_exec_run;
extern mi_cmd_args_ftype mi_cmd_exec_step;
extern mi_cmd_args_ftype mi_cmd_exec_step_instruction;
extern mi_cmd_args_ftype mi_cmd_exec_until;
extern mi_cmd_args_ftype mi_cmd_exec_interrupt;
extern mi_cmd_argv_ftype mi_cmd_file_list_exec_source_file;
extern mi_cmd_argv_ftype mi_cmd_gdb_exit;
extern mi_cmd_argv_ftype mi_cmd_interpreter_exec;
extern mi_cmd_argv_ftype mi_cmd_stack_info_depth;
extern mi_cmd_argv_ftype mi_cmd_stack_list_args;
extern mi_cmd_argv_ftype mi_cmd_stack_list_frames;
extern mi_cmd_argv_ftype mi_cmd_stack_list_locals;
extern mi_cmd_argv_ftype mi_cmd_stack_select_frame;
extern mi_cmd_argv_ftype mi_cmd_symbol_list_lines;
extern mi_cmd_args_ftype mi_cmd_target_download;
extern mi_cmd_args_ftype mi_cmd_target_select;
extern mi_cmd_argv_ftype mi_cmd_thread_list_ids;
extern mi_cmd_argv_ftype mi_cmd_thread_select;
extern mi_cmd_argv_ftype mi_cmd_var_assign;
extern mi_cmd_argv_ftype mi_cmd_var_create;
extern mi_cmd_argv_ftype mi_cmd_var_delete;
extern mi_cmd_argv_ftype mi_cmd_var_evaluate_expression;
extern mi_cmd_argv_ftype mi_cmd_var_info_expression;
extern mi_cmd_argv_ftype mi_cmd_var_info_num_children;
extern mi_cmd_argv_ftype mi_cmd_var_info_type;
extern mi_cmd_argv_ftype mi_cmd_var_list_children;
extern mi_cmd_argv_ftype mi_cmd_var_set_format;
extern mi_cmd_argv_ftype mi_cmd_var_show_attributes;
extern mi_cmd_argv_ftype mi_cmd_var_show_format;
extern mi_cmd_argv_ftype mi_cmd_var_update;

/* Description of a single command. */

struct mi_cli
{
  /* Corresponding CLI command.  If ARGS_P is non-zero, the MI
     command's argument list is appended to the CLI command.  */
  const char *cmd;
  int args_p;
};

struct mi_cmd
{
  /* official name of the command.  */
  const char *name;
  /* The corresponding CLI command that can be used to implement this
     MI command (if cli.lhs is non NULL).  */
  struct mi_cli cli;
  /* If non-null, the function implementing the MI command.  */
  mi_cmd_args_ftype *args_func;
  /* If non-null, the function implementing the MI command.  */
  mi_cmd_argv_ftype *argv_func;
};

/* Lookup a command in the mi comand table */

extern struct mi_cmd *mi_lookup (const char *command);

/* Debug flag */
extern int mi_debug_p;

/* Raw console output - FIXME: should this be a parameter? */
extern struct ui_file *raw_stdout;

extern char *mi_error_message;
extern void mi_error_last_message (void);
extern void mi_execute_command (char *cmd, int from_tty);

#endif
