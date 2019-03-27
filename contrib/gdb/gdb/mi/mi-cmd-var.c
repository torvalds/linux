/* MI Command Set - varobj commands.

   Copyright 2000, 2002, 2004 Free Software Foundation, Inc.

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
#include "mi-cmds.h"
#include "ui-out.h"
#include "mi-out.h"
#include "varobj.h"
#include "value.h"
#include <ctype.h>
#include "gdb_string.h"

extern int varobjdebug;		/* defined in varobj.c */

static int varobj_update_one (struct varobj *var);

/* VAROBJ operations */

enum mi_cmd_result
mi_cmd_var_create (char *command, char **argv, int argc)
{
  CORE_ADDR frameaddr = 0;
  struct varobj *var;
  char *name;
  char *frame;
  char *expr;
  char *type;
  struct cleanup *old_cleanups;
  enum varobj_type var_type;

  if (argc != 3)
    {
      /*      xasprintf (&mi_error_message,
         "mi_cmd_var_create: Usage: .");
         return MI_CMD_ERROR; */
      error ("mi_cmd_var_create: Usage: NAME FRAME EXPRESSION.");
    }

  name = xstrdup (argv[0]);
  /* Add cleanup for name. Must be free_current_contents as
     name can be reallocated */
  old_cleanups = make_cleanup (free_current_contents, &name);

  frame = xstrdup (argv[1]);
  old_cleanups = make_cleanup (xfree, frame);

  expr = xstrdup (argv[2]);

  if (strcmp (name, "-") == 0)
    {
      xfree (name);
      name = varobj_gen_name ();
    }
  else if (!isalpha (*name))
    error ("mi_cmd_var_create: name of object must begin with a letter");

  if (strcmp (frame, "*") == 0)
    var_type = USE_CURRENT_FRAME;
  else if (strcmp (frame, "@") == 0)
    var_type = USE_SELECTED_FRAME;  
  else
    {
      var_type = USE_SPECIFIED_FRAME;
      frameaddr = string_to_core_addr (frame);
    }

  if (varobjdebug)
    fprintf_unfiltered (gdb_stdlog,
		    "Name=\"%s\", Frame=\"%s\" (0x%s), Expression=\"%s\"\n",
			name, frame, paddr (frameaddr), expr);

  var = varobj_create (name, expr, frameaddr, var_type);

  if (var == NULL)
    error ("mi_cmd_var_create: unable to create variable object");

  ui_out_field_string (uiout, "name", name);
  ui_out_field_int (uiout, "numchild", varobj_get_num_children (var));
  type = varobj_get_type (var);
  if (type == NULL)
    ui_out_field_string (uiout, "type", "");
  else
    {
      ui_out_field_string (uiout, "type", type);
      xfree (type);
    }

  do_cleanups (old_cleanups);
  return MI_CMD_DONE;
}

enum mi_cmd_result
mi_cmd_var_delete (char *command, char **argv, int argc)
{
  char *name;
  char *expr;
  struct varobj *var;
  int numdel;
  int children_only_p = 0;
  struct cleanup *old_cleanups;

  if (argc < 1 || argc > 2)
    error ("mi_cmd_var_delete: Usage: [-c] EXPRESSION.");

  name = xstrdup (argv[0]);
  /* Add cleanup for name. Must be free_current_contents as
     name can be reallocated */
  old_cleanups = make_cleanup (free_current_contents, &name);

  /* If we have one single argument it cannot be '-c' or any string
     starting with '-'. */
  if (argc == 1)
    {
      if (strcmp (name, "-c") == 0)
	error ("mi_cmd_var_delete: Missing required argument after '-c': variable object name");
      if (*name == '-')
	error ("mi_cmd_var_delete: Illegal variable object name");
    }

  /* If we have 2 arguments they must be '-c' followed by a string
     which would be the variable name. */
  if (argc == 2)
    {
      expr = xstrdup (argv[1]);
      if (strcmp (name, "-c") != 0)
	error ("mi_cmd_var_delete: Invalid option.");
      children_only_p = 1;
      xfree (name);
      name = xstrdup (expr);
      xfree (expr);
    }

  /* If we didn't error out, now NAME contains the name of the
     variable. */

  var = varobj_get_handle (name);

  if (var == NULL)
    error ("mi_cmd_var_delete: Variable object not found.");

  numdel = varobj_delete (var, NULL, children_only_p);

  ui_out_field_int (uiout, "ndeleted", numdel);

  do_cleanups (old_cleanups);
  return MI_CMD_DONE;
}

enum mi_cmd_result
mi_cmd_var_set_format (char *command, char **argv, int argc)
{
  enum varobj_display_formats format;
  int len;
  struct varobj *var;
  char *formspec;

  if (argc != 2)
    error ("mi_cmd_var_set_format: Usage: NAME FORMAT.");

  /* Get varobj handle, if a valid var obj name was specified */
  var = varobj_get_handle (argv[0]);

  if (var == NULL)
    error ("mi_cmd_var_set_format: Variable object not found");

  formspec = xstrdup (argv[1]);
  if (formspec == NULL)
    error ("mi_cmd_var_set_format: Must specify the format as: \"natural\", \"binary\", \"decimal\", \"hexadecimal\", or \"octal\"");

  len = strlen (formspec);

  if (strncmp (formspec, "natural", len) == 0)
    format = FORMAT_NATURAL;
  else if (strncmp (formspec, "binary", len) == 0)
    format = FORMAT_BINARY;
  else if (strncmp (formspec, "decimal", len) == 0)
    format = FORMAT_DECIMAL;
  else if (strncmp (formspec, "hexadecimal", len) == 0)
    format = FORMAT_HEXADECIMAL;
  else if (strncmp (formspec, "octal", len) == 0)
    format = FORMAT_OCTAL;
  else
    error ("mi_cmd_var_set_format: Unknown display format: must be: \"natural\", \"binary\", \"decimal\", \"hexadecimal\", or \"octal\"");

  /* Set the format of VAR to given format */
  varobj_set_display_format (var, format);

  /* Report the new current format */
  ui_out_field_string (uiout, "format", varobj_format_string[(int) format]);
  return MI_CMD_DONE;
}

enum mi_cmd_result
mi_cmd_var_show_format (char *command, char **argv, int argc)
{
  enum varobj_display_formats format;
  struct varobj *var;

  if (argc != 1)
    error ("mi_cmd_var_show_format: Usage: NAME.");

  /* Get varobj handle, if a valid var obj name was specified */
  var = varobj_get_handle (argv[0]);
  if (var == NULL)
    error ("mi_cmd_var_show_format: Variable object not found");

  format = varobj_get_display_format (var);

  /* Report the current format */
  ui_out_field_string (uiout, "format", varobj_format_string[(int) format]);
  return MI_CMD_DONE;
}

enum mi_cmd_result
mi_cmd_var_info_num_children (char *command, char **argv, int argc)
{
  struct varobj *var;

  if (argc != 1)
    error ("mi_cmd_var_info_num_children: Usage: NAME.");

  /* Get varobj handle, if a valid var obj name was specified */
  var = varobj_get_handle (argv[0]);
  if (var == NULL)
    error ("mi_cmd_var_info_num_children: Variable object not found");

  ui_out_field_int (uiout, "numchild", varobj_get_num_children (var));
  return MI_CMD_DONE;
}

enum mi_cmd_result
mi_cmd_var_list_children (char *command, char **argv, int argc)
{
  struct varobj *var;
  struct varobj **childlist;
  struct varobj **cc;
  struct cleanup *cleanup_children;
  int numchild;
  char *type;
  enum print_values print_values;

  if (argc != 1 && argc != 2)
    error ("mi_cmd_var_list_children: Usage: [PRINT_VALUES] NAME");

  /* Get varobj handle, if a valid var obj name was specified */
  if (argc == 1) var = varobj_get_handle (argv[0]);
  else var = varobj_get_handle (argv[1]);
  if (var == NULL)
    error ("Variable object not found");

  numchild = varobj_list_children (var, &childlist);
  ui_out_field_int (uiout, "numchild", numchild);
  if (argc == 2)
    if (strcmp (argv[0], "0") == 0
	|| strcmp (argv[0], "--no-values") == 0)
      print_values = PRINT_NO_VALUES;
    else if (strcmp (argv[0], "1") == 0
	     || strcmp (argv[0], "--all-values") == 0)
      print_values = PRINT_ALL_VALUES;
    else
     error ("Unknown value for PRINT_VALUES: must be: 0 or \"--no-values\", 1 or \"--all-values\"");
  else print_values = PRINT_NO_VALUES;

  if (numchild <= 0)
    return MI_CMD_DONE;

  if (mi_version (uiout) == 1)
    cleanup_children = make_cleanup_ui_out_tuple_begin_end (uiout, "children");
  else
    cleanup_children = make_cleanup_ui_out_list_begin_end (uiout, "children");
  cc = childlist;
  while (*cc != NULL)
    {
      struct cleanup *cleanup_child;
      cleanup_child = make_cleanup_ui_out_tuple_begin_end (uiout, "child");
      ui_out_field_string (uiout, "name", varobj_get_objname (*cc));
      ui_out_field_string (uiout, "exp", varobj_get_expression (*cc));
      ui_out_field_int (uiout, "numchild", varobj_get_num_children (*cc));
      if (print_values)
	ui_out_field_string (uiout, "value", varobj_get_value (*cc));
      type = varobj_get_type (*cc);
      /* C++ pseudo-variables (public, private, protected) do not have a type */
      if (type)
	ui_out_field_string (uiout, "type", varobj_get_type (*cc));
      do_cleanups (cleanup_child);
      cc++;
    }
  do_cleanups (cleanup_children);
  xfree (childlist);
  return MI_CMD_DONE;
}

enum mi_cmd_result
mi_cmd_var_info_type (char *command, char **argv, int argc)
{
  struct varobj *var;

  if (argc != 1)
    error ("mi_cmd_var_info_type: Usage: NAME.");

  /* Get varobj handle, if a valid var obj name was specified */
  var = varobj_get_handle (argv[0]);
  if (var == NULL)
    error ("mi_cmd_var_info_type: Variable object not found");

  ui_out_field_string (uiout, "type", varobj_get_type (var));
  return MI_CMD_DONE;
}

enum mi_cmd_result
mi_cmd_var_info_expression (char *command, char **argv, int argc)
{
  enum varobj_languages lang;
  struct varobj *var;

  if (argc != 1)
    error ("mi_cmd_var_info_expression: Usage: NAME.");

  /* Get varobj handle, if a valid var obj name was specified */
  var = varobj_get_handle (argv[0]);
  if (var == NULL)
    error ("mi_cmd_var_info_expression: Variable object not found");

  lang = varobj_get_language (var);

  ui_out_field_string (uiout, "lang", varobj_language_string[(int) lang]);
  ui_out_field_string (uiout, "exp", varobj_get_expression (var));
  return MI_CMD_DONE;
}

enum mi_cmd_result
mi_cmd_var_show_attributes (char *command, char **argv, int argc)
{
  int attr;
  char *attstr;
  struct varobj *var;

  if (argc != 1)
    error ("mi_cmd_var_show_attributes: Usage: NAME.");

  /* Get varobj handle, if a valid var obj name was specified */
  var = varobj_get_handle (argv[0]);
  if (var == NULL)
    error ("mi_cmd_var_show_attributes: Variable object not found");

  attr = varobj_get_attributes (var);
  /* FIXME: define masks for attributes */
  if (attr & 0x00000001)
    attstr = "editable";
  else
    attstr = "noneditable";

  ui_out_field_string (uiout, "attr", attstr);
  return MI_CMD_DONE;
}

enum mi_cmd_result
mi_cmd_var_evaluate_expression (char *command, char **argv, int argc)
{
  struct varobj *var;

  if (argc != 1)
    error ("mi_cmd_var_evaluate_expression: Usage: NAME.");

  /* Get varobj handle, if a valid var obj name was specified */
  var = varobj_get_handle (argv[0]);
  if (var == NULL)
    error ("mi_cmd_var_evaluate_expression: Variable object not found");

  ui_out_field_string (uiout, "value", varobj_get_value (var));
  return MI_CMD_DONE;
}

enum mi_cmd_result
mi_cmd_var_assign (char *command, char **argv, int argc)
{
  struct varobj *var;
  char *expression;

  if (argc != 2)
    error ("mi_cmd_var_assign: Usage: NAME EXPRESSION.");

  /* Get varobj handle, if a valid var obj name was specified */
  var = varobj_get_handle (argv[0]);
  if (var == NULL)
    error ("mi_cmd_var_assign: Variable object not found");

  /* FIXME: define masks for attributes */
  if (!(varobj_get_attributes (var) & 0x00000001))
    error ("mi_cmd_var_assign: Variable object is not editable");

  expression = xstrdup (argv[1]);

  if (!varobj_set_value (var, expression))
    error ("mi_cmd_var_assign: Could not assign expression to varible object");

  ui_out_field_string (uiout, "value", varobj_get_value (var));
  return MI_CMD_DONE;
}

enum mi_cmd_result
mi_cmd_var_update (char *command, char **argv, int argc)
{
  struct varobj *var;
  struct varobj **rootlist;
  struct varobj **cr;
  struct cleanup *cleanup;
  char *name;
  int nv;

  if (argc != 1)
    error ("mi_cmd_var_update: Usage: NAME.");

  name = argv[0];

  /* Check if the parameter is a "*" which means that we want
     to update all variables */

  if ((*name == '*') && (*(name + 1) == '\0'))
    {
      nv = varobj_list (&rootlist);
      if (mi_version (uiout) <= 1)
        cleanup = make_cleanup_ui_out_tuple_begin_end (uiout, "changelist");
      else
        cleanup = make_cleanup_ui_out_list_begin_end (uiout, "changelist");
      if (nv <= 0)
	{
	  do_cleanups (cleanup);
	  return MI_CMD_DONE;
	}
      cr = rootlist;
      while (*cr != NULL)
	{
	  varobj_update_one (*cr);
	  cr++;
	}
      xfree (rootlist);
      do_cleanups (cleanup);
    }
  else
    {
      /* Get varobj handle, if a valid var obj name was specified */
      var = varobj_get_handle (name);
      if (var == NULL)
	error ("mi_cmd_var_update: Variable object not found");

      if (mi_version (uiout) <= 1)
        cleanup = make_cleanup_ui_out_tuple_begin_end (uiout, "changelist");
      else
        cleanup = make_cleanup_ui_out_list_begin_end (uiout, "changelist");
      varobj_update_one (var);
      do_cleanups (cleanup);
    }
    return MI_CMD_DONE;
}

/* Helper for mi_cmd_var_update() Returns 0 if the update for
   the variable fails (usually because the variable is out of
   scope), and 1 if it succeeds. */

static int
varobj_update_one (struct varobj *var)
{
  struct varobj **changelist;
  struct varobj **cc;
  struct cleanup *cleanup = NULL;
  int nc;

  nc = varobj_update (&var, &changelist);

  /* nc == 0 means that nothing has changed.
     nc == -1 means that an error occured in updating the variable.
     nc == -2 means the variable has changed type. */
  
  if (nc == 0)
    return 1;
  else if (nc == -1)
    {
      if (mi_version (uiout) > 1)
        cleanup = make_cleanup_ui_out_tuple_begin_end (uiout, NULL);
      ui_out_field_string (uiout, "name", varobj_get_objname(var));
      ui_out_field_string (uiout, "in_scope", "false");
      if (mi_version (uiout) > 1)
        do_cleanups (cleanup);
      return -1;
    }
  else if (nc == -2)
    {
      if (mi_version (uiout) > 1)
        cleanup = make_cleanup_ui_out_tuple_begin_end (uiout, NULL);
      ui_out_field_string (uiout, "name", varobj_get_objname (var));
      ui_out_field_string (uiout, "in_scope", "true");
      ui_out_field_string (uiout, "new_type", varobj_get_type(var));
      ui_out_field_int (uiout, "new_num_children", 
			   varobj_get_num_children(var));
      if (mi_version (uiout) > 1)
        do_cleanups (cleanup);
    }
  else
    {
      
      cc = changelist;
      while (*cc != NULL)
	{
	  if (mi_version (uiout) > 1)
	    cleanup = make_cleanup_ui_out_tuple_begin_end (uiout, NULL);
	  ui_out_field_string (uiout, "name", varobj_get_objname (*cc));
	  ui_out_field_string (uiout, "in_scope", "true");
	  ui_out_field_string (uiout, "type_changed", "false");
	  if (mi_version (uiout) > 1)
	    do_cleanups (cleanup);
	  cc++;
	}
      xfree (changelist);
      return 1;
    }
  return 1;
}
