/* MI Command Set for GDB, the GNU debugger.

   Copyright 2000, 2001, 2003 Free Software Foundation, Inc.

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

#include "defs.h"
#include "top.h"
#include "mi-cmds.h"
#include "gdb_string.h"

extern void _initialize_mi_cmds (void);
struct mi_cmd;
static struct mi_cmd **lookup_table (const char *command);
static void build_table (struct mi_cmd *commands);


struct mi_cmd mi_cmds[] =
{
  { "break-after", { "ignore", 1 }, NULL, NULL },
  { "break-catch", { NULL, 0 }, NULL, NULL },
  { "break-commands", { NULL, 0 }, NULL, NULL },
  { "break-condition", { "cond", 1 }, NULL, NULL },
  { "break-delete", { "delete breakpoint", 1 }, NULL, NULL },
  { "break-disable", { "disable breakpoint", 1 }, NULL, NULL },
  { "break-enable", { "enable breakpoint", 1 }, NULL, NULL },
  { "break-info", { "info break", 1 }, NULL, NULL },
  { "break-insert", { NULL, 0 }, 0, mi_cmd_break_insert},
  { "break-list", { "info break", }, NULL, NULL },
  { "break-watch", { NULL, 0 }, 0, mi_cmd_break_watch},
  { "data-disassemble", { NULL, 0 }, 0, mi_cmd_disassemble},
  { "data-evaluate-expression", { NULL, 0 }, 0, mi_cmd_data_evaluate_expression},
  { "data-list-changed-registers", { NULL, 0 }, 0, mi_cmd_data_list_changed_registers},
  { "data-list-register-names", { NULL, 0 }, 0, mi_cmd_data_list_register_names},
  { "data-list-register-values", { NULL, 0 }, 0, mi_cmd_data_list_register_values},
  { "data-read-memory", { NULL, 0 }, 0, mi_cmd_data_read_memory},
  { "data-write-memory", { NULL, 0 }, 0, mi_cmd_data_write_memory},
  { "data-write-register-values", { NULL, 0 }, 0, mi_cmd_data_write_register_values},
  { "display-delete", { NULL, 0 }, NULL, NULL },
  { "display-disable", { NULL, 0 }, NULL, NULL },
  { "display-enable", { NULL, 0 }, NULL, NULL },
  { "display-insert", { NULL, 0 }, NULL, NULL },
  { "display-list", { NULL, 0 }, NULL, NULL },
  { "environment-cd", { NULL, 0 }, 0, mi_cmd_env_cd},
  { "environment-directory", { NULL, 0 }, 0, mi_cmd_env_dir},
  { "environment-path", { NULL, 0 }, 0, mi_cmd_env_path},
  { "environment-pwd", { NULL, 0 }, 0, mi_cmd_env_pwd},
  { "exec-abort", { NULL, 0 }, NULL, NULL },
  { "exec-arguments", { "set args", 1 }, NULL, NULL },
  { "exec-continue", { NULL, 0 }, mi_cmd_exec_continue},
  { "exec-finish", { NULL, 0 }, mi_cmd_exec_finish},
  { "exec-interrupt", { NULL, 0 }, mi_cmd_exec_interrupt},
  { "exec-next", { NULL, 0 }, mi_cmd_exec_next},
  { "exec-next-instruction", { NULL, 0 }, mi_cmd_exec_next_instruction},
  { "exec-return", { NULL, 0 }, mi_cmd_exec_return},
  { "exec-run", { NULL, 0 }, mi_cmd_exec_run},
  { "exec-show-arguments", { NULL, 0 }, NULL, NULL },
  { "exec-signal", { NULL, 0 }, NULL, NULL },
  { "exec-step", { NULL, 0 }, mi_cmd_exec_step},
  { "exec-step-instruction", { NULL, 0 }, mi_cmd_exec_step_instruction},
  { "exec-until", { NULL, 0 }, mi_cmd_exec_until},
  { "file-clear", { NULL, 0 }, NULL, NULL },
  { "file-exec-and-symbols", { "file", 1 }, NULL, NULL },
  { "file-exec-file", { "exec-file", 1 }, NULL, NULL },
  { "file-list-exec-sections", { NULL, 0 }, NULL, NULL },
  { "file-list-exec-source-file", { NULL, 0 }, 0, mi_cmd_file_list_exec_source_file},
  { "file-list-exec-source-files", { NULL, 0 }, NULL, NULL },
  { "file-list-shared-libraries", { NULL, 0 }, NULL, NULL },
  { "file-list-symbol-files", { NULL, 0 }, NULL, NULL },
  { "file-symbol-file", { "symbol-file", 1 }, NULL, NULL },
  { "gdb-complete", { NULL, 0 }, NULL, NULL },
  { "gdb-exit", { NULL, 0 }, 0, mi_cmd_gdb_exit},
  { "gdb-set", { "set", 1 }, NULL, NULL },
  { "gdb-show", { "show", 1 }, NULL, NULL },
  { "gdb-source", { NULL, 0 }, NULL, NULL },
  { "gdb-version", { "show version", 0 }, 0 },
  { "interpreter-exec", { NULL, 0 }, 0, mi_cmd_interpreter_exec},
  { "kod-info", { NULL, 0 }, NULL, NULL },
  { "kod-list", { NULL, 0 }, NULL, NULL },
  { "kod-list-object-types", { NULL, 0 }, NULL, NULL },
  { "kod-show", { NULL, 0 }, NULL, NULL },
  { "overlay-auto", { NULL, 0 }, NULL, NULL },
  { "overlay-list-mapping-state", { NULL, 0 }, NULL, NULL },
  { "overlay-list-overlays", { NULL, 0 }, NULL, NULL },
  { "overlay-map", { NULL, 0 }, NULL, NULL },
  { "overlay-off", { NULL, 0 }, NULL, NULL },
  { "overlay-on", { NULL, 0 }, NULL, NULL },
  { "overlay-unmap", { NULL, 0 }, NULL, NULL },
  { "signal-handle", { NULL, 0 }, NULL, NULL },
  { "signal-list-handle-actions", { NULL, 0 }, NULL, NULL },
  { "signal-list-signal-types", { NULL, 0 }, NULL, NULL },
  { "stack-info-depth", { NULL, 0 }, 0, mi_cmd_stack_info_depth},
  { "stack-info-frame", { NULL, 0 }, NULL, NULL },
  { "stack-list-arguments", { NULL, 0 }, 0, mi_cmd_stack_list_args},
  { "stack-list-exception-handlers", { NULL, 0 }, NULL, NULL },
  { "stack-list-frames", { NULL, 0 }, 0, mi_cmd_stack_list_frames},
  { "stack-list-locals", { NULL, 0 }, 0, mi_cmd_stack_list_locals},
  { "stack-select-frame", { NULL, 0 }, 0, mi_cmd_stack_select_frame},
  { "symbol-info-address", { NULL, 0 }, NULL, NULL },
  { "symbol-info-file", { NULL, 0 }, NULL, NULL },
  { "symbol-info-function", { NULL, 0 }, NULL, NULL },
  { "symbol-info-line", { NULL, 0 }, NULL, NULL },
  { "symbol-info-symbol", { NULL, 0 }, NULL, NULL },
  { "symbol-list-functions", { NULL, 0 }, NULL, NULL },
  { "symbol-list-lines", { NULL, 0 }, 0, mi_cmd_symbol_list_lines},
  { "symbol-list-types", { NULL, 0 }, NULL, NULL },
  { "symbol-list-variables", { NULL, 0 }, NULL, NULL },
  { "symbol-locate", { NULL, 0 }, NULL, NULL },
  { "symbol-type", { NULL, 0 }, NULL, NULL },
  { "target-attach", { NULL, 0 }, NULL, NULL },
  { "target-compare-sections", { NULL, 0 }, NULL, NULL },
  { "target-detach", { "detach", 0 }, 0 },
  { "target-disconnect", { "disconnect", 0 }, 0 },
  { "target-download", { NULL, 0 }, mi_cmd_target_download},
  { "target-exec-status", { NULL, 0 }, NULL, NULL },
  { "target-list-available-targets", { NULL, 0 }, NULL, NULL },
  { "target-list-current-targets", { NULL, 0 }, NULL, NULL },
  { "target-list-parameters", { NULL, 0 }, NULL, NULL },
  { "target-select", { NULL, 0 }, mi_cmd_target_select},
  { "thread-info", { NULL, 0 }, NULL, NULL },
  { "thread-list-all-threads", { NULL, 0 }, NULL, NULL },
  { "thread-list-ids", { NULL, 0 }, 0, mi_cmd_thread_list_ids},
  { "thread-select", { NULL, 0 }, 0, mi_cmd_thread_select},
  { "trace-actions", { NULL, 0 }, NULL, NULL },
  { "trace-delete", { NULL, 0 }, NULL, NULL },
  { "trace-disable", { NULL, 0 }, NULL, NULL },
  { "trace-dump", { NULL, 0 }, NULL, NULL },
  { "trace-enable", { NULL, 0 }, NULL, NULL },
  { "trace-exists", { NULL, 0 }, NULL, NULL },
  { "trace-find", { NULL, 0 }, NULL, NULL },
  { "trace-frame-number", { NULL, 0 }, NULL, NULL },
  { "trace-info", { NULL, 0 }, NULL, NULL },
  { "trace-insert", { NULL, 0 }, NULL, NULL },
  { "trace-list", { NULL, 0 }, NULL, NULL },
  { "trace-pass-count", { NULL, 0 }, NULL, NULL },
  { "trace-save", { NULL, 0 }, NULL, NULL },
  { "trace-start", { NULL, 0 }, NULL, NULL },
  { "trace-stop", { NULL, 0 }, NULL, NULL },
  { "var-assign", { NULL, 0 }, 0, mi_cmd_var_assign},
  { "var-create", { NULL, 0 }, 0, mi_cmd_var_create},
  { "var-delete", { NULL, 0 }, 0, mi_cmd_var_delete},
  { "var-evaluate-expression", { NULL, 0 }, 0, mi_cmd_var_evaluate_expression},
  { "var-info-expression", { NULL, 0 }, 0, mi_cmd_var_info_expression},
  { "var-info-num-children", { NULL, 0 }, 0, mi_cmd_var_info_num_children},
  { "var-info-type", { NULL, 0 }, 0, mi_cmd_var_info_type},
  { "var-list-children", { NULL, 0 }, 0, mi_cmd_var_list_children},
  { "var-set-format", { NULL, 0 }, 0, mi_cmd_var_set_format},
  { "var-show-attributes", { NULL, 0 }, 0, mi_cmd_var_show_attributes},
  { "var-show-format", { NULL, 0 }, 0, mi_cmd_var_show_format},
  { "var-update", { NULL, 0 }, 0, mi_cmd_var_update},
  { NULL, }
};

/* Pointer to the mi command table (built at run time) */

static struct mi_cmd **mi_table;

/* A prime large enough to accomodate the entire command table */
enum
  {
    MI_TABLE_SIZE = 227
  };

/* Exported function used to obtain info from the table */
struct mi_cmd *
mi_lookup (const char *command)
{
  return *lookup_table (command);
}

/* stat collecting */
struct mi_cmd_stats
{
  int hit;
  int miss;
  int rehash;
};
struct mi_cmd_stats stats;

/* our lookup function */
static struct mi_cmd **
lookup_table (const char *command)
{
  const char *chp;
  unsigned int index = 0;
  /* compute our hash */
  for (chp = command; *chp; chp++)
    {
      /* some what arbitrary */
      index = ((index << 6) + (unsigned int) *chp) % MI_TABLE_SIZE;
    }
  /* look it up */
  while (1)
    {
      struct mi_cmd **entry = &mi_table[index];
      if ((*entry) == 0)
	{
	  /* not found, return pointer to next free. */
	  stats.miss++;
	  return entry;
	}
      if (strcmp (command, (*entry)->name) == 0)
	{
	  stats.hit++;
	  return entry;		/* found */
	}
      index = (index + 1) % MI_TABLE_SIZE;
      stats.rehash++;
    }
}

static void
build_table (struct mi_cmd *commands)
{
  int nr_rehash = 0;
  int nr_entries = 0;
  struct mi_cmd *command;
  int sizeof_table = sizeof (struct mi_cmd **) * MI_TABLE_SIZE;

  mi_table = xmalloc (sizeof_table);
  memset (mi_table, 0, sizeof_table);
  for (command = commands; command->name != 0; command++)
    {
      struct mi_cmd **entry = lookup_table (command->name);
      if (*entry)
	internal_error (__FILE__, __LINE__,
			"command `%s' appears to be duplicated",
			command->name);
      *entry = command;
      if (0)
	{
	  fprintf_unfiltered (gdb_stdlog, "%-30s %2d\n",
			      command->name, stats.rehash - nr_rehash);
	}
      nr_entries++;
      nr_rehash = stats.rehash;
    }
  if (0)
    {
      fprintf_filtered (gdb_stdlog, "Average %3.1f\n",
			(double) nr_rehash / (double) nr_entries);
    }
}

void
_initialize_mi_cmds (void)
{
  build_table (mi_cmds);
  memset (&stats, 0, sizeof (stats));
}
