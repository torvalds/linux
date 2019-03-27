/* Handle set and show GDB commands.

   Copyright 2000, 2001, 2002, 2003 Free Software Foundation, Inc.

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
#include "readline/tilde.h"
#include "value.h"
#include <ctype.h>
#include "gdb_string.h"

#include "ui-out.h"

#include "cli/cli-decode.h"
#include "cli/cli-cmds.h"
#include "cli/cli-setshow.h"

/* Prototypes for local functions */

static int parse_binary_operation (char *);


static enum auto_boolean
parse_auto_binary_operation (const char *arg)
{
  if (arg != NULL && *arg != '\0')
    {
      int length = strlen (arg);
      while (isspace (arg[length - 1]) && length > 0)
	length--;
      if (strncmp (arg, "on", length) == 0
	  || strncmp (arg, "1", length) == 0
	  || strncmp (arg, "yes", length) == 0
	  || strncmp (arg, "enable", length) == 0)
	return AUTO_BOOLEAN_TRUE;
      else if (strncmp (arg, "off", length) == 0
	       || strncmp (arg, "0", length) == 0
	       || strncmp (arg, "no", length) == 0
	       || strncmp (arg, "disable", length) == 0)
	return AUTO_BOOLEAN_FALSE;
      else if (strncmp (arg, "auto", length) == 0
	       || (strncmp (arg, "-1", length) == 0 && length > 1))
	return AUTO_BOOLEAN_AUTO;
    }
  error ("\"on\", \"off\" or \"auto\" expected.");
  return AUTO_BOOLEAN_AUTO; /* pacify GCC */
}

static int
parse_binary_operation (char *arg)
{
  int length;

  if (!arg || !*arg)
    return 1;

  length = strlen (arg);

  while (arg[length - 1] == ' ' || arg[length - 1] == '\t')
    length--;

  if (strncmp (arg, "on", length) == 0
      || strncmp (arg, "1", length) == 0
      || strncmp (arg, "yes", length) == 0
      || strncmp (arg, "enable", length) == 0)
    return 1;
  else if (strncmp (arg, "off", length) == 0
	   || strncmp (arg, "0", length) == 0
	   || strncmp (arg, "no", length) == 0
	   || strncmp (arg, "disable", length) == 0)
    return 0;
  else
    {
      error ("\"on\" or \"off\" expected.");
      return 0;
    }
}

/* Do a "set" or "show" command.  ARG is NULL if no argument, or the text
   of the argument, and FROM_TTY is nonzero if this command is being entered
   directly by the user (i.e. these are just like any other
   command).  C is the command list element for the command.  */

void
do_setshow_command (char *arg, int from_tty, struct cmd_list_element *c)
{
  if (c->type == set_cmd)
    {
      switch (c->var_type)
	{
	case var_string:
	  {
	    char *new;
	    char *p;
	    char *q;
	    int ch;

	    if (arg == NULL)
	      arg = "";
	    new = (char *) xmalloc (strlen (arg) + 2);
	    p = arg;
	    q = new;
	    while ((ch = *p++) != '\000')
	      {
		if (ch == '\\')
		  {
		    /* \ at end of argument is used after spaces
		       so they won't be lost.  */
		    /* This is obsolete now that we no longer strip
		       trailing whitespace and actually, the backslash
		       didn't get here in my test, readline or
		       something did something funky with a backslash
		       right before a newline.  */
		    if (*p == 0)
		      break;
		    ch = parse_escape (&p);
		    if (ch == 0)
		      break;	/* C loses */
		    else if (ch > 0)
		      *q++ = ch;
		  }
		else
		  *q++ = ch;
	      }
#if 0
	    if (*(p - 1) != '\\')
	      *q++ = ' ';
#endif
	    *q++ = '\0';
	    new = (char *) xrealloc (new, q - new);
	    if (*(char **) c->var != NULL)
	      xfree (*(char **) c->var);
	    *(char **) c->var = new;
	  }
	  break;
	case var_string_noescape:
	  if (arg == NULL)
	    arg = "";
	  if (*(char **) c->var != NULL)
	    xfree (*(char **) c->var);
	  *(char **) c->var = savestring (arg, strlen (arg));
	  break;
	case var_filename:
	  if (arg == NULL)
	    error_no_arg ("filename to set it to.");
	  if (*(char **) c->var != NULL)
	    xfree (*(char **) c->var);
	  *(char **) c->var = tilde_expand (arg);
	  break;
	case var_boolean:
	  *(int *) c->var = parse_binary_operation (arg);
	  break;
	case var_auto_boolean:
	  *(enum auto_boolean *) c->var = parse_auto_binary_operation (arg);
	  break;
	case var_uinteger:
	  if (arg == NULL)
	    error_no_arg ("integer to set it to.");
	  *(unsigned int *) c->var = parse_and_eval_long (arg);
	  if (*(unsigned int *) c->var == 0)
	    *(unsigned int *) c->var = UINT_MAX;
	  break;
	case var_integer:
	  {
	    unsigned int val;
	    if (arg == NULL)
	      error_no_arg ("integer to set it to.");
	    val = parse_and_eval_long (arg);
	    if (val == 0)
	      *(int *) c->var = INT_MAX;
	    else if (val >= INT_MAX)
	      error ("integer %u out of range", val);
	    else
	      *(int *) c->var = val;
	    break;
	  }
	case var_zinteger:
	  if (arg == NULL)
	    error_no_arg ("integer to set it to.");
	  *(int *) c->var = parse_and_eval_long (arg);
	  break;
	case var_enum:
	  {
	    int i;
	    int len;
	    int nmatches;
	    const char *match = NULL;
	    char *p;

	    /* if no argument was supplied, print an informative error message */
	    if (arg == NULL)
	      {
		char msg[1024];
		strcpy (msg, "Requires an argument. Valid arguments are ");
		for (i = 0; c->enums[i]; i++)
		  {
		    if (i != 0)
		      strcat (msg, ", ");
		    strcat (msg, c->enums[i]);
		  }
		strcat (msg, ".");
		error ("%s", msg);
	      }

	    p = strchr (arg, ' ');

	    if (p)
	      len = p - arg;
	    else
	      len = strlen (arg);

	    nmatches = 0;
	    for (i = 0; c->enums[i]; i++)
	      if (strncmp (arg, c->enums[i], len) == 0)
		{
		  if (c->enums[i][len] == '\0')
		    {
		      match = c->enums[i];
		      nmatches = 1;
		      break; /* exact match. */
		    }
		  else
		    {
		      match = c->enums[i];
		      nmatches++;
		    }
		}

	    if (nmatches <= 0)
	      error ("Undefined item: \"%s\".", arg);

	    if (nmatches > 1)
	      error ("Ambiguous item \"%s\".", arg);

	    *(const char **) c->var = match;
	  }
	  break;
	default:
	  error ("gdb internal error: bad var_type in do_setshow_command");
	}
    }
  else if (c->type == show_cmd)
    {
      struct cleanup *old_chain;
      struct ui_stream *stb;
      int quote;

      stb = ui_out_stream_new (uiout);
      old_chain = make_cleanup_ui_out_stream_delete (stb);

      /* Possibly call the pre hook.  */
      if (c->pre_show_hook)
	(c->pre_show_hook) (c);

      /* Print doc minus "show" at start.  */
      print_doc_line (gdb_stdout, c->doc + 5);

      ui_out_text (uiout, " is ");
      ui_out_wrap_hint (uiout, "    ");
      quote = 0;
      switch (c->var_type)
	{
	case var_string:
	  {
	    unsigned char *p;

	    if (*(unsigned char **) c->var)
	      fputstr_filtered (*(unsigned char **) c->var, '"', stb->stream);
	    quote = 1;
	  }
	  break;
	case var_string_noescape:
	case var_filename:
	case var_enum:
	  if (*(char **) c->var)
	    fputs_filtered (*(char **) c->var, stb->stream);
	  quote = 1;
	  break;
	case var_boolean:
	  fputs_filtered (*(int *) c->var ? "on" : "off", stb->stream);
	  break;
	case var_auto_boolean:
	  switch (*(enum auto_boolean*) c->var)
	    {
	    case AUTO_BOOLEAN_TRUE:
	      fputs_filtered ("on", stb->stream);
	      break;
	    case AUTO_BOOLEAN_FALSE:
	      fputs_filtered ("off", stb->stream);
	      break;
	    case AUTO_BOOLEAN_AUTO:
	      fputs_filtered ("auto", stb->stream);
	      break;
	    default:
	      internal_error (__FILE__, __LINE__,
			      "do_setshow_command: invalid var_auto_boolean");
	      break;
	    }
	  break;
	case var_uinteger:
	  if (*(unsigned int *) c->var == UINT_MAX)
	    {
	      fputs_filtered ("unlimited", stb->stream);
	      break;
	    }
	  /* else fall through */
	case var_zinteger:
	  fprintf_filtered (stb->stream, "%u", *(unsigned int *) c->var);
	  break;
	case var_integer:
	  if (*(int *) c->var == INT_MAX)
	    {
	      fputs_filtered ("unlimited", stb->stream);
	    }
	  else
	    fprintf_filtered (stb->stream, "%d", *(int *) c->var);
	  break;

	default:
	  error ("gdb internal error: bad var_type in do_setshow_command");
	}
      if (quote)
	ui_out_text (uiout, "\"");
      ui_out_field_stream (uiout, "value", stb);
      if (quote)
	ui_out_text (uiout, "\"");
      ui_out_text (uiout, ".\n");
      do_cleanups (old_chain);
    }
  else
    error ("gdb internal error: bad cmd_type in do_setshow_command");
  c->func (c, NULL, from_tty);
  if (c->type == set_cmd && set_hook)
    set_hook (c);
}

/* Show all the settings in a list of show commands.  */

void
cmd_show_list (struct cmd_list_element *list, int from_tty, char *prefix)
{
  struct cleanup *showlist_chain;

  showlist_chain = make_cleanup_ui_out_tuple_begin_end (uiout, "showlist");
  for (; list != NULL; list = list->next)
    {
      /* If we find a prefix, run its list, prefixing our output by its
         prefix (with "show " skipped).  */
      if (list->prefixlist && !list->abbrev_flag)
	{
	  struct cleanup *optionlist_chain
	    = make_cleanup_ui_out_tuple_begin_end (uiout, "optionlist");
	  ui_out_field_string (uiout, "prefix", list->prefixname + 5);
	  cmd_show_list (*list->prefixlist, from_tty, list->prefixname + 5);
	  /* Close the tuple.  */
	  do_cleanups (optionlist_chain);
	}
      if (list->type == show_cmd)
	{
	  struct cleanup *option_chain
	    = make_cleanup_ui_out_tuple_begin_end (uiout, "option");
	  ui_out_text (uiout, prefix);
	  ui_out_field_string (uiout, "name", list->name);
	  ui_out_text (uiout, ":  ");
	  do_setshow_command ((char *) NULL, from_tty, list);
          /* Close the tuple.  */
	  do_cleanups (option_chain);
	}
    }
  /* Close the tuple.  */
  do_cleanups (showlist_chain);
}

