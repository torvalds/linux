/* MI Command Set - environment commands.

   Copyright 2002, 2003, 2004 Free Software Foundation, Inc.

   Contributed by Red Hat Inc.

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
#include "inferior.h"
#include "value.h"
#include "mi-out.h"
#include "mi-cmds.h"
#include "mi-getopt.h"
#include "symtab.h"
#include "target.h"
#include "environ.h"
#include "command.h"
#include "ui-out.h"
#include "top.h"

#include "gdb_string.h"
#include "gdb_stat.h"

static void env_mod_path (char *dirname, char **which_path);
extern void _initialize_mi_cmd_env (void);

static const char path_var_name[] = "PATH";
static char *orig_path = NULL;

/* The following is copied from mi-main.c so for m1 and below we can
   perform old behavior and use cli commands.  If ARGS is non-null,
   append it to the CMD.  */
static void
env_execute_cli_command (const char *cmd, const char *args)
{
  if (cmd != 0)
    {
      struct cleanup *old_cleanups;
      char *run;
      if (args != NULL)
	xasprintf (&run, "%s %s", cmd, args);
      else
	run = xstrdup (cmd);
      old_cleanups = make_cleanup (xfree, run);
      execute_command ( /*ui */ run, 0 /*from_tty */ );
      do_cleanups (old_cleanups);
      return;
    }
}


/* Print working directory.  */
enum mi_cmd_result
mi_cmd_env_pwd (char *command, char **argv, int argc)
{
  if (argc > 0)
    error ("mi_cmd_env_pwd: No arguments required");
          
  if (mi_version (uiout) < 2)
    {
      env_execute_cli_command ("pwd", NULL);
      return MI_CMD_DONE;
    }
     
  /* Otherwise the mi level is 2 or higher.  */

  getcwd (gdb_dirbuf, sizeof (gdb_dirbuf));
  ui_out_field_string (uiout, "cwd", gdb_dirbuf);

  return MI_CMD_DONE;
}

/* Change working directory.  */
enum mi_cmd_result
mi_cmd_env_cd (char *command, char **argv, int argc)
{
  if (argc == 0 || argc > 1)
    error ("mi_cmd_env_cd: Usage DIRECTORY");
          
  env_execute_cli_command ("cd", argv[0]);

  return MI_CMD_DONE;
}

static void
env_mod_path (char *dirname, char **which_path)
{
  if (dirname == 0 || dirname[0] == '\0')
    return;

  /* Call add_path with last arg 0 to indicate not to parse for 
     separator characters.  */
  add_path (dirname, which_path, 0);
}

/* Add one or more directories to start of executable search path.  */
enum mi_cmd_result
mi_cmd_env_path (char *command, char **argv, int argc)
{
  char *exec_path;
  char *env;
  int reset = 0;
  int optind = 0;
  int i;
  char *optarg;
  enum opt
    {
      RESET_OPT
    };
  static struct mi_opt opts[] =
  {
    {"r", RESET_OPT, 0},
    0
  };

  dont_repeat ();

  if (mi_version (uiout) < 2)
    {
      for (i = argc - 1; i >= 0; --i)
	env_execute_cli_command ("path", argv[i]);
      return MI_CMD_DONE;
    }

  /* Otherwise the mi level is 2 or higher.  */
  while (1)
    {
      int opt = mi_getopt ("mi_cmd_env_path", argc, argv, opts,
                           &optind, &optarg);
      if (opt < 0)
        break;
      switch ((enum opt) opt)
        {
        case RESET_OPT:
          reset = 1;
          break;
        }
    }
  argv += optind;
  argc -= optind;


  if (reset)
    {
      /* Reset implies resetting to original path first.  */
      exec_path = xstrdup (orig_path);
    }
  else
    {
      /* Otherwise, get current path to modify.  */
      env = get_in_environ (inferior_environ, path_var_name);

      /* Can be null if path is not set.  */
      if (!env)
        env = "";
      exec_path = xstrdup (env);
    }

  for (i = argc - 1; i >= 0; --i)
    env_mod_path (argv[i], &exec_path);

  set_in_environ (inferior_environ, path_var_name, exec_path);
  xfree (exec_path);
  env = get_in_environ (inferior_environ, path_var_name);
  ui_out_field_string (uiout, "path", env);

  return MI_CMD_DONE;
}

/* Add zero or more directories to the front of the source path.  */
enum mi_cmd_result
mi_cmd_env_dir (char *command, char **argv, int argc)
{
  int i;
  int optind = 0;
  int reset = 0;
  char *optarg;
  enum opt
    {
      RESET_OPT
    };
  static struct mi_opt opts[] =
  {
    {"r", RESET_OPT, 0},
    0
  };

  dont_repeat ();

  if (mi_version (uiout) < 2)
    {
      for (i = argc - 1; i >= 0; --i)
	env_execute_cli_command ("dir", argv[i]);
      return MI_CMD_DONE;
    }

  /* Otherwise mi level is 2 or higher.  */
  while (1)
    {
      int opt = mi_getopt ("mi_cmd_env_dir", argc, argv, opts,
                           &optind, &optarg);
      if (opt < 0)
        break;
      switch ((enum opt) opt)
        {
        case RESET_OPT:
          reset = 1;
          break;
        }
    }
  argv += optind;
  argc -= optind;

  if (reset)
    {
      /* Reset means setting to default path first.  */
      xfree (source_path);
      init_source_path ();
    }

  for (i = argc - 1; i >= 0; --i)
    env_mod_path (argv[i], &source_path);
  init_last_source_visited ();

  ui_out_field_string (uiout, "source-path", source_path);
  forget_cached_source_info ();

  return MI_CMD_DONE;
}

void 
_initialize_mi_cmd_env (void)
{
  char *env;

  /* We want original execution path to reset to, if desired later.  */
  env = get_in_environ (inferior_environ, path_var_name);

  /* Can be null if path is not set.  */
  if (!env)
    env = "";
  orig_path = xstrdup (env);
}
