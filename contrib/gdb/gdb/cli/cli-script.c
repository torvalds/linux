/* GDB CLI command scripting.

   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994,
   1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002 Free Software
   Foundation, Inc.

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
#include "value.h"
#include "language.h"		/* For value_true */
#include <ctype.h>

#include "ui-out.h"
#include "gdb_string.h"

#include "top.h"
#include "cli/cli-cmds.h"
#include "cli/cli-decode.h"
#include "cli/cli-script.h"

/* Prototypes for local functions */

static enum command_control_type
	recurse_read_control_structure (struct command_line *current_cmd);

static char *insert_args (char *line);

static struct cleanup * setup_user_args (char *p);

static void validate_comname (char *);

/* Level of control structure.  */
static int control_level;

/* Source command state variable. */
static int source_error_allocated;

/* Structure for arguments to user defined functions.  */
#define MAXUSERARGS 10
struct user_args
  {
    struct user_args *next;
    struct
      {
	char *arg;
	int len;
      }
    a[MAXUSERARGS];
    int count;
  }
 *user_args;


/* Allocate, initialize a new command line structure for one of the
   control commands (if/while).  */

static struct command_line *
build_command_line (enum command_control_type type, char *args)
{
  struct command_line *cmd;

  if (args == NULL)
    error ("if/while commands require arguments.\n");

  cmd = (struct command_line *) xmalloc (sizeof (struct command_line));
  cmd->next = NULL;
  cmd->control_type = type;

  cmd->body_count = 1;
  cmd->body_list
    = (struct command_line **) xmalloc (sizeof (struct command_line *)
					* cmd->body_count);
  memset (cmd->body_list, 0, sizeof (struct command_line *) * cmd->body_count);
  cmd->line = savestring (args, strlen (args));
  return cmd;
}

/* Build and return a new command structure for the control commands
   such as "if" and "while".  */

static struct command_line *
get_command_line (enum command_control_type type, char *arg)
{
  struct command_line *cmd;
  struct cleanup *old_chain = NULL;

  /* Allocate and build a new command line structure.  */
  cmd = build_command_line (type, arg);

  old_chain = make_cleanup_free_command_lines (&cmd);

  /* Read in the body of this command.  */
  if (recurse_read_control_structure (cmd) == invalid_control)
    {
      warning ("error reading in control structure\n");
      do_cleanups (old_chain);
      return NULL;
    }

  discard_cleanups (old_chain);
  return cmd;
}

/* Recursively print a command (including full control structures).  */

void
print_command_lines (struct ui_out *uiout, struct command_line *cmd,
		     unsigned int depth)
{
  struct command_line *list;

  list = cmd;
  while (list)
    {

      if (depth)
	ui_out_spaces (uiout, 2 * depth);

      /* A simple command, print it and continue.  */
      if (list->control_type == simple_control)
	{
	  ui_out_field_string (uiout, NULL, list->line);
	  ui_out_text (uiout, "\n");
	  list = list->next;
	  continue;
	}

      /* loop_continue to jump to the start of a while loop, print it
         and continue. */
      if (list->control_type == continue_control)
	{
	  ui_out_field_string (uiout, NULL, "loop_continue");
	  ui_out_text (uiout, "\n");
	  list = list->next;
	  continue;
	}

      /* loop_break to break out of a while loop, print it and continue.  */
      if (list->control_type == break_control)
	{
	  ui_out_field_string (uiout, NULL, "loop_break");
	  ui_out_text (uiout, "\n");
	  list = list->next;
	  continue;
	}

      /* A while command.  Recursively print its subcommands and continue.  */
      if (list->control_type == while_control)
	{
	  ui_out_field_fmt (uiout, NULL, "while %s", list->line);
	  ui_out_text (uiout, "\n");
	  print_command_lines (uiout, *list->body_list, depth + 1);
	  if (depth)
	    ui_out_spaces (uiout, 2 * depth);
	  ui_out_field_string (uiout, NULL, "end");
	  ui_out_text (uiout, "\n");
	  list = list->next;
	  continue;
	}

      /* An if command.  Recursively print both arms before continueing.  */
      if (list->control_type == if_control)
	{
	  ui_out_field_fmt (uiout, NULL, "if %s", list->line);
	  ui_out_text (uiout, "\n");
	  /* The true arm. */
	  print_command_lines (uiout, list->body_list[0], depth + 1);

	  /* Show the false arm if it exists.  */
	  if (list->body_count == 2)
	    {
	      if (depth)
		ui_out_spaces (uiout, 2 * depth);
	      ui_out_field_string (uiout, NULL, "else");
	      ui_out_text (uiout, "\n");
	      print_command_lines (uiout, list->body_list[1], depth + 1);
	    }

	  if (depth)
	    ui_out_spaces (uiout, 2 * depth);
	  ui_out_field_string (uiout, NULL, "end");
	  ui_out_text (uiout, "\n");
	  list = list->next;
	  continue;
	}

      /* ignore illegal command type and try next */
      list = list->next;
    }				/* while (list) */
}

/* Handle pre-post hooks.  */

static void
clear_hook_in_cleanup (void *data)
{
  struct cmd_list_element *c = data;
  c->hook_in = 0; /* Allow hook to work again once it is complete */
}

void
execute_cmd_pre_hook (struct cmd_list_element *c)
{
  if ((c->hook_pre) && (!c->hook_in))
    {
      struct cleanup *cleanups = make_cleanup (clear_hook_in_cleanup, c);
      c->hook_in = 1; /* Prevent recursive hooking */
      execute_user_command (c->hook_pre, (char *) 0);
      do_cleanups (cleanups);
    }
}

void
execute_cmd_post_hook (struct cmd_list_element *c)
{
  if ((c->hook_post) && (!c->hook_in))
    {
      struct cleanup *cleanups = make_cleanup (clear_hook_in_cleanup, c);
      c->hook_in = 1; /* Prevent recursive hooking */
      execute_user_command (c->hook_post, (char *) 0);
      do_cleanups (cleanups);
    }
}

/* Execute the command in CMD.  */
static void
do_restore_user_call_depth (void * call_depth)
{	
  int * depth = call_depth;
  /* We will be returning_to_top_level() at this point, so we want to
     reset our depth. */
  (*depth) = 0;
}


void
execute_user_command (struct cmd_list_element *c, char *args)
{
  struct command_line *cmdlines;
  struct cleanup *old_chain;
  enum command_control_type ret;
  static int user_call_depth = 0;
  extern int max_user_call_depth;

  old_chain = setup_user_args (args);

  cmdlines = c->user_commands;
  if (cmdlines == 0)
    /* Null command */
    return;

  if (++user_call_depth > max_user_call_depth)
    error ("Max user call depth exceeded -- command aborted\n");

  old_chain = make_cleanup (do_restore_user_call_depth, &user_call_depth);

  /* Set the instream to 0, indicating execution of a
     user-defined function.  */
  old_chain = make_cleanup (do_restore_instream_cleanup, instream);
  instream = (FILE *) 0;
  while (cmdlines)
    {
      ret = execute_control_command (cmdlines);
      if (ret != simple_control && ret != break_control)
	{
	  warning ("Error in control structure.\n");
	  break;
	}
      cmdlines = cmdlines->next;
    }
  do_cleanups (old_chain);

  user_call_depth--;
}

enum command_control_type
execute_control_command (struct command_line *cmd)
{
  struct expression *expr;
  struct command_line *current;
  struct cleanup *old_chain = make_cleanup (null_cleanup, 0);
  struct value *val;
  struct value *val_mark;
  int loop;
  enum command_control_type ret;
  char *new_line;

  /* Start by assuming failure, if a problem is detected, the code
     below will simply "break" out of the switch.  */
  ret = invalid_control;

  switch (cmd->control_type)
    {
    case simple_control:
      /* A simple command, execute it and return.  */
      new_line = insert_args (cmd->line);
      if (!new_line)
	break;
      make_cleanup (free_current_contents, &new_line);
      execute_command (new_line, 0);
      ret = cmd->control_type;
      break;

    case continue_control:
    case break_control:
      /* Return for "continue", and "break" so we can either
         continue the loop at the top, or break out.  */
      ret = cmd->control_type;
      break;

    case while_control:
      {
	/* Parse the loop control expression for the while statement.  */
	new_line = insert_args (cmd->line);
	if (!new_line)
	  break;
	make_cleanup (free_current_contents, &new_line);
	expr = parse_expression (new_line);
	make_cleanup (free_current_contents, &expr);

	ret = simple_control;
	loop = 1;

	/* Keep iterating so long as the expression is true.  */
	while (loop == 1)
	  {
	    int cond_result;

	    QUIT;

	    /* Evaluate the expression.  */
	    val_mark = value_mark ();
	    val = evaluate_expression (expr);
	    cond_result = value_true (val);
	    value_free_to_mark (val_mark);

	    /* If the value is false, then break out of the loop.  */
	    if (!cond_result)
	      break;

	    /* Execute the body of the while statement.  */
	    current = *cmd->body_list;
	    while (current)
	      {
		ret = execute_control_command (current);

		/* If we got an error, or a "break" command, then stop
		   looping.  */
		if (ret == invalid_control || ret == break_control)
		  {
		    loop = 0;
		    break;
		  }

		/* If we got a "continue" command, then restart the loop
		   at this point.  */
		if (ret == continue_control)
		  break;

		/* Get the next statement.  */
		current = current->next;
	      }
	  }

	/* Reset RET so that we don't recurse the break all the way down.  */
	if (ret == break_control)
	  ret = simple_control;

	break;
      }

    case if_control:
      {
	new_line = insert_args (cmd->line);
	if (!new_line)
	  break;
	make_cleanup (free_current_contents, &new_line);
	/* Parse the conditional for the if statement.  */
	expr = parse_expression (new_line);
	make_cleanup (free_current_contents, &expr);

	current = NULL;
	ret = simple_control;

	/* Evaluate the conditional.  */
	val_mark = value_mark ();
	val = evaluate_expression (expr);

	/* Choose which arm to take commands from based on the value of the
	   conditional expression.  */
	if (value_true (val))
	  current = *cmd->body_list;
	else if (cmd->body_count == 2)
	  current = *(cmd->body_list + 1);
	value_free_to_mark (val_mark);

	/* Execute commands in the given arm.  */
	while (current)
	  {
	    ret = execute_control_command (current);

	    /* If we got an error, get out.  */
	    if (ret != simple_control)
	      break;

	    /* Get the next statement in the body.  */
	    current = current->next;
	  }

	break;
      }

    default:
      warning ("Invalid control type in command structure.");
      break;
    }

  do_cleanups (old_chain);

  return ret;
}

/* "while" command support.  Executes a body of statements while the
   loop condition is nonzero.  */

void
while_command (char *arg, int from_tty)
{
  struct command_line *command = NULL;

  control_level = 1;
  command = get_command_line (while_control, arg);

  if (command == NULL)
    return;

  execute_control_command (command);
  free_command_lines (&command);
}

/* "if" command support.  Execute either the true or false arm depending
   on the value of the if conditional.  */

void
if_command (char *arg, int from_tty)
{
  struct command_line *command = NULL;

  control_level = 1;
  command = get_command_line (if_control, arg);

  if (command == NULL)
    return;

  execute_control_command (command);
  free_command_lines (&command);
}

/* Cleanup */
static void
arg_cleanup (void *ignore)
{
  struct user_args *oargs = user_args;
  if (!user_args)
    internal_error (__FILE__, __LINE__,
		    "arg_cleanup called with no user args.\n");

  user_args = user_args->next;
  xfree (oargs);
}

/* Bind the incomming arguments for a user defined command to
   $arg0, $arg1 ... $argMAXUSERARGS.  */

static struct cleanup *
setup_user_args (char *p)
{
  struct user_args *args;
  struct cleanup *old_chain;
  unsigned int arg_count = 0;

  args = (struct user_args *) xmalloc (sizeof (struct user_args));
  memset (args, 0, sizeof (struct user_args));

  args->next = user_args;
  user_args = args;

  old_chain = make_cleanup (arg_cleanup, 0/*ignored*/);

  if (p == NULL)
    return old_chain;

  while (*p)
    {
      char *start_arg;
      int squote = 0;
      int dquote = 0;
      int bsquote = 0;

      if (arg_count >= MAXUSERARGS)
	{
	  error ("user defined function may only have %d arguments.\n",
		 MAXUSERARGS);
	  return old_chain;
	}

      /* Strip whitespace.  */
      while (*p == ' ' || *p == '\t')
	p++;

      /* P now points to an argument.  */
      start_arg = p;
      user_args->a[arg_count].arg = p;

      /* Get to the end of this argument.  */
      while (*p)
	{
	  if (((*p == ' ' || *p == '\t')) && !squote && !dquote && !bsquote)
	    break;
	  else
	    {
	      if (bsquote)
		bsquote = 0;
	      else if (*p == '\\')
		bsquote = 1;
	      else if (squote)
		{
		  if (*p == '\'')
		    squote = 0;
		}
	      else if (dquote)
		{
		  if (*p == '"')
		    dquote = 0;
		}
	      else
		{
		  if (*p == '\'')
		    squote = 1;
		  else if (*p == '"')
		    dquote = 1;
		}
	      p++;
	    }
	}

      user_args->a[arg_count].len = p - start_arg;
      arg_count++;
      user_args->count++;
    }
  return old_chain;
}

/* Given character string P, return a point to the first argument ($arg),
   or NULL if P contains no arguments.  */

static char *
locate_arg (char *p)
{
  while ((p = strchr (p, '$')))
    {
      if (strncmp (p, "$arg", 4) == 0 && isdigit (p[4]))
	return p;
      p++;
    }
  return NULL;
}

/* Insert the user defined arguments stored in user_arg into the $arg
   arguments found in line, with the updated copy being placed into nline.  */

static char *
insert_args (char *line)
{
  char *p, *save_line, *new_line;
  unsigned len, i;

  /* First we need to know how much memory to allocate for the new line.  */
  save_line = line;
  len = 0;
  while ((p = locate_arg (line)))
    {
      len += p - line;
      i = p[4] - '0';

      if (i >= user_args->count)
	{
	  error ("Missing argument %d in user function.\n", i);
	  return NULL;
	}
      len += user_args->a[i].len;
      line = p + 5;
    }

  /* Don't forget the tail.  */
  len += strlen (line);

  /* Allocate space for the new line and fill it in.  */
  new_line = (char *) xmalloc (len + 1);
  if (new_line == NULL)
    return NULL;

  /* Restore pointer to beginning of old line.  */
  line = save_line;

  /* Save pointer to beginning of new line.  */
  save_line = new_line;

  while ((p = locate_arg (line)))
    {
      int i, len;

      memcpy (new_line, line, p - line);
      new_line += p - line;
      i = p[4] - '0';

      len = user_args->a[i].len;
      if (len)
	{
	  memcpy (new_line, user_args->a[i].arg, len);
	  new_line += len;
	}
      line = p + 5;
    }
  /* Don't forget the tail.  */
  strcpy (new_line, line);

  /* Return a pointer to the beginning of the new line.  */
  return save_line;
}


/* Expand the body_list of COMMAND so that it can hold NEW_LENGTH
   code bodies.  This is typically used when we encounter an "else"
   clause for an "if" command.  */

static void
realloc_body_list (struct command_line *command, int new_length)
{
  int n;
  struct command_line **body_list;

  n = command->body_count;

  /* Nothing to do?  */
  if (new_length <= n)
    return;

  body_list = (struct command_line **)
    xmalloc (sizeof (struct command_line *) * new_length);

  memcpy (body_list, command->body_list, sizeof (struct command_line *) * n);

  xfree (command->body_list);
  command->body_list = body_list;
  command->body_count = new_length;
}

/* Read one line from the input stream.  If the command is an "else" or
   "end", return such an indication to the caller.  */

static enum misc_command_type
read_next_line (struct command_line **command)
{
  char *p, *p1, *prompt_ptr, control_prompt[256];
  int i = 0;

  if (control_level >= 254)
    error ("Control nesting too deep!\n");

  /* Set a prompt based on the nesting of the control commands.  */
  if (instream == stdin || (instream == 0 && readline_hook != NULL))
    {
      for (i = 0; i < control_level; i++)
	control_prompt[i] = ' ';
      control_prompt[i] = '>';
      control_prompt[i + 1] = '\0';
      prompt_ptr = (char *) &control_prompt[0];
    }
  else
    prompt_ptr = NULL;

  p = command_line_input (prompt_ptr, instream == stdin, "commands");

  /* Not sure what to do here.  */
  if (p == NULL)
    return end_command;

  /* Strip leading and trailing whitespace.  */
  while (*p == ' ' || *p == '\t')
    p++;

  p1 = p + strlen (p);
  while (p1 != p && (p1[-1] == ' ' || p1[-1] == '\t'))
    p1--;

  /* Blanks and comments don't really do anything, but we need to
     distinguish them from else, end and other commands which can be
     executed.  */
  if (p1 == p || p[0] == '#')
    return nop_command;

  /* Is this the end of a simple, while, or if control structure?  */
  if (p1 - p == 3 && !strncmp (p, "end", 3))
    return end_command;

  /* Is the else clause of an if control structure?  */
  if (p1 - p == 4 && !strncmp (p, "else", 4))
    return else_command;

  /* Check for while, if, break, continue, etc and build a new command
     line structure for them.  */
  if (p1 - p > 5 && !strncmp (p, "while", 5))
    *command = build_command_line (while_control, p + 6);
  else if (p1 - p > 2 && !strncmp (p, "if", 2))
    *command = build_command_line (if_control, p + 3);
  else if (p1 - p == 10 && !strncmp (p, "loop_break", 10))
    {
      *command = (struct command_line *)
	xmalloc (sizeof (struct command_line));
      (*command)->next = NULL;
      (*command)->line = NULL;
      (*command)->control_type = break_control;
      (*command)->body_count = 0;
      (*command)->body_list = NULL;
    }
  else if (p1 - p == 13 && !strncmp (p, "loop_continue", 13))
    {
      *command = (struct command_line *)
	xmalloc (sizeof (struct command_line));
      (*command)->next = NULL;
      (*command)->line = NULL;
      (*command)->control_type = continue_control;
      (*command)->body_count = 0;
      (*command)->body_list = NULL;
    }
  else
    {
      /* A normal command.  */
      *command = (struct command_line *)
	xmalloc (sizeof (struct command_line));
      (*command)->next = NULL;
      (*command)->line = savestring (p, p1 - p);
      (*command)->control_type = simple_control;
      (*command)->body_count = 0;
      (*command)->body_list = NULL;
    }

  /* Nothing special.  */
  return ok_command;
}

/* Recursively read in the control structures and create a command_line 
   structure from them.

   The parent_control parameter is the control structure in which the
   following commands are nested.  */

static enum command_control_type
recurse_read_control_structure (struct command_line *current_cmd)
{
  int current_body, i;
  enum misc_command_type val;
  enum command_control_type ret;
  struct command_line **body_ptr, *child_tail, *next;

  child_tail = NULL;
  current_body = 1;

  /* Sanity checks.  */
  if (current_cmd->control_type == simple_control)
    {
      error ("Recursed on a simple control type\n");
      return invalid_control;
    }

  if (current_body > current_cmd->body_count)
    {
      error ("Allocated body is smaller than this command type needs\n");
      return invalid_control;
    }

  /* Read lines from the input stream and build control structures.  */
  while (1)
    {
      dont_repeat ();

      next = NULL;
      val = read_next_line (&next);

      /* Just skip blanks and comments.  */
      if (val == nop_command)
	continue;

      if (val == end_command)
	{
	  if (current_cmd->control_type == while_control
	      || current_cmd->control_type == if_control)
	    {
	      /* Success reading an entire control structure.  */
	      ret = simple_control;
	      break;
	    }
	  else
	    {
	      ret = invalid_control;
	      break;
	    }
	}

      /* Not the end of a control structure.  */
      if (val == else_command)
	{
	  if (current_cmd->control_type == if_control
	      && current_body == 1)
	    {
	      realloc_body_list (current_cmd, 2);
	      current_body = 2;
	      child_tail = NULL;
	      continue;
	    }
	  else
	    {
	      ret = invalid_control;
	      break;
	    }
	}

      if (child_tail)
	{
	  child_tail->next = next;
	}
      else
	{
	  body_ptr = current_cmd->body_list;
	  for (i = 1; i < current_body; i++)
	    body_ptr++;

	  *body_ptr = next;

	}

      child_tail = next;

      /* If the latest line is another control structure, then recurse
         on it.  */
      if (next->control_type == while_control
	  || next->control_type == if_control)
	{
	  control_level++;
	  ret = recurse_read_control_structure (next);
	  control_level--;

	  if (ret != simple_control)
	    break;
	}
    }

  dont_repeat ();

  return ret;
}

/* Read lines from the input stream and accumulate them in a chain of
   struct command_line's, which is then returned.  For input from a
   terminal, the special command "end" is used to mark the end of the
   input, and is not included in the returned chain of commands. */

#define END_MESSAGE "End with a line saying just \"end\"."

struct command_line *
read_command_lines (char *prompt_arg, int from_tty)
{
  struct command_line *head, *tail, *next;
  struct cleanup *old_chain;
  enum command_control_type ret;
  enum misc_command_type val;

  control_level = 0;
  if (readline_begin_hook)
    {
      /* Note - intentional to merge messages with no newline */
      (*readline_begin_hook) ("%s  %s\n", prompt_arg, END_MESSAGE);
    }
  else if (from_tty && input_from_terminal_p ())
    {
      printf_unfiltered ("%s\n%s\n", prompt_arg, END_MESSAGE);
      gdb_flush (gdb_stdout);
    }

  head = tail = NULL;
  old_chain = NULL;

  while (1)
    {
      val = read_next_line (&next);

      /* Ignore blank lines or comments.  */
      if (val == nop_command)
	continue;

      if (val == end_command)
	{
	  ret = simple_control;
	  break;
	}

      if (val != ok_command)
	{
	  ret = invalid_control;
	  break;
	}

      if (next->control_type == while_control
	  || next->control_type == if_control)
	{
	  control_level++;
	  ret = recurse_read_control_structure (next);
	  control_level--;

	  if (ret == invalid_control)
	    break;
	}

      if (tail)
	{
	  tail->next = next;
	}
      else
	{
	  head = next;
	  old_chain = make_cleanup_free_command_lines (&head);
	}
      tail = next;
    }

  dont_repeat ();

  if (head)
    {
      if (ret != invalid_control)
	{
	  discard_cleanups (old_chain);
	}
      else
	do_cleanups (old_chain);
    }

  if (readline_end_hook)
    {
      (*readline_end_hook) ();
    }
  return (head);
}

/* Free a chain of struct command_line's.  */

void
free_command_lines (struct command_line **lptr)
{
  struct command_line *l = *lptr;
  struct command_line *next;
  struct command_line **blist;
  int i;

  while (l)
    {
      if (l->body_count > 0)
	{
	  blist = l->body_list;
	  for (i = 0; i < l->body_count; i++, blist++)
	    free_command_lines (blist);
	}
      next = l->next;
      xfree (l->line);
      xfree (l);
      l = next;
    }
  *lptr = NULL;
}

static void
do_free_command_lines_cleanup (void *arg)
{
  free_command_lines (arg);
}

struct cleanup *
make_cleanup_free_command_lines (struct command_line **arg)
{
  return make_cleanup (do_free_command_lines_cleanup, arg);
}

struct command_line *
copy_command_lines (struct command_line *cmds)
{
  struct command_line *result = NULL;

  if (cmds)
    {
      result = (struct command_line *) xmalloc (sizeof (struct command_line));

      result->next = copy_command_lines (cmds->next);
      result->line = xstrdup (cmds->line);
      result->control_type = cmds->control_type;
      result->body_count = cmds->body_count;
      if (cmds->body_count > 0)
        {
          int i;

          result->body_list = (struct command_line **)
            xmalloc (sizeof (struct command_line *) * cmds->body_count);

          for (i = 0; i < cmds->body_count; i++)
            result->body_list[i] = copy_command_lines (cmds->body_list[i]);
        }
      else
        result->body_list = NULL;
    }

  return result;
}

static void
validate_comname (char *comname)
{
  char *p;

  if (comname == 0)
    error_no_arg ("name of command to define");

  p = comname;
  while (*p)
    {
      if (!isalnum (*p) && *p != '-' && *p != '_')
	error ("Junk in argument list: \"%s\"", p);
      p++;
    }
}

/* This is just a placeholder in the command data structures.  */
static void
user_defined_command (char *ignore, int from_tty)
{
}

void
define_command (char *comname, int from_tty)
{
#define MAX_TMPBUF 128   
  enum cmd_hook_type
    {
      CMD_NO_HOOK = 0,
      CMD_PRE_HOOK,
      CMD_POST_HOOK
    };
  struct command_line *cmds;
  struct cmd_list_element *c, *newc, *oldc, *hookc = 0;
  char *tem = comname;
  char *tem2; 
  char tmpbuf[MAX_TMPBUF];
  int  hook_type      = CMD_NO_HOOK;
  int  hook_name_size = 0;
   
#define	HOOK_STRING	"hook-"
#define	HOOK_LEN 5
#define HOOK_POST_STRING "hookpost-"
#define HOOK_POST_LEN    9

  validate_comname (comname);

  /* Look it up, and verify that we got an exact match.  */
  c = lookup_cmd (&tem, cmdlist, "", -1, 1);
  if (c && strcmp (comname, c->name) != 0)
    c = 0;

  if (c)
    {
      int q;
      if (c->class == class_user || c->class == class_alias)
	q = query ("Redefine command \"%s\"? ", c->name);
      else
	q = query ("Really redefine built-in command \"%s\"? ", c->name);
      if (!q)
	error ("Command \"%s\" not redefined.", c->name);
    }

  /* If this new command is a hook, then mark the command which it
     is hooking.  Note that we allow hooking `help' commands, so that
     we can hook the `stop' pseudo-command.  */

  if (!strncmp (comname, HOOK_STRING, HOOK_LEN))
    {
       hook_type      = CMD_PRE_HOOK;
       hook_name_size = HOOK_LEN;
    }
  else if (!strncmp (comname, HOOK_POST_STRING, HOOK_POST_LEN))
    {
      hook_type      = CMD_POST_HOOK;
      hook_name_size = HOOK_POST_LEN;
    }
   
  if (hook_type != CMD_NO_HOOK)
    {
      /* Look up cmd it hooks, and verify that we got an exact match.  */
      tem = comname + hook_name_size;
      hookc = lookup_cmd (&tem, cmdlist, "", -1, 0);
      if (hookc && strcmp (comname + hook_name_size, hookc->name) != 0)
	hookc = 0;
      if (!hookc)
	{
	  warning ("Your new `%s' command does not hook any existing command.",
		   comname);
	  if (!query ("Proceed? "))
	    error ("Not confirmed.");
	}
    }

  comname = savestring (comname, strlen (comname));

  /* If the rest of the commands will be case insensitive, this one
     should behave in the same manner. */
  for (tem = comname; *tem; tem++)
    if (isupper (*tem))
      *tem = tolower (*tem);

  sprintf (tmpbuf, "Type commands for definition of \"%s\".", comname);
  cmds = read_command_lines (tmpbuf, from_tty);

  if (c && c->class == class_user)
    free_command_lines (&c->user_commands);

  newc = add_cmd (comname, class_user, user_defined_command,
		  (c && c->class == class_user)
		  ? c->doc : savestring ("User-defined.", 13), &cmdlist);
  newc->user_commands = cmds;

  /* If this new command is a hook, then mark both commands as being
     tied.  */
  if (hookc)
    {
      switch (hook_type)
        {
        case CMD_PRE_HOOK:
          hookc->hook_pre  = newc;  /* Target gets hooked.  */
          newc->hookee_pre = hookc; /* We are marked as hooking target cmd. */
          break;
        case CMD_POST_HOOK:
          hookc->hook_post  = newc;  /* Target gets hooked.  */
          newc->hookee_post = hookc; /* We are marked as hooking target cmd. */
          break;
        default:
          /* Should never come here as hookc would be 0. */
	  internal_error (__FILE__, __LINE__, "bad switch");
        }
    }
}

void
document_command (char *comname, int from_tty)
{
  struct command_line *doclines;
  struct cmd_list_element *c;
  char *tem = comname;
  char tmpbuf[128];

  validate_comname (comname);

  c = lookup_cmd (&tem, cmdlist, "", 0, 1);

  if (c->class != class_user)
    error ("Command \"%s\" is built-in.", comname);

  sprintf (tmpbuf, "Type documentation for \"%s\".", comname);
  doclines = read_command_lines (tmpbuf, from_tty);

  if (c->doc)
    xfree (c->doc);

  {
    struct command_line *cl1;
    int len = 0;

    for (cl1 = doclines; cl1; cl1 = cl1->next)
      len += strlen (cl1->line) + 1;

    c->doc = (char *) xmalloc (len + 1);
    *c->doc = 0;

    for (cl1 = doclines; cl1; cl1 = cl1->next)
      {
	strcat (c->doc, cl1->line);
	if (cl1->next)
	  strcat (c->doc, "\n");
      }
  }

  free_command_lines (&doclines);
}

struct source_cleanup_lines_args
{
  int old_line;
  char *old_file;
  char *old_pre_error;
  char *old_error_pre_print;
};

static void
source_cleanup_lines (void *args)
{
  struct source_cleanup_lines_args *p =
  (struct source_cleanup_lines_args *) args;
  source_line_number = p->old_line;
  source_file_name = p->old_file;
  source_pre_error = p->old_pre_error;
  error_pre_print = p->old_error_pre_print;
}

static void
do_fclose_cleanup (void *stream)
{
  fclose (stream);
}

/* Used to implement source_command */

void
script_from_file (FILE *stream, char *file)
{
  struct cleanup *old_cleanups;
  struct source_cleanup_lines_args old_lines;
  int needed_length;

  if (stream == NULL)
    {
      internal_error (__FILE__, __LINE__, "called with NULL file pointer!");
    }

  old_cleanups = make_cleanup (do_fclose_cleanup, stream);

  old_lines.old_line = source_line_number;
  old_lines.old_file = source_file_name;
  old_lines.old_pre_error = source_pre_error;
  old_lines.old_error_pre_print = error_pre_print;
  make_cleanup (source_cleanup_lines, &old_lines);
  source_line_number = 0;
  source_file_name = file;
  source_pre_error = error_pre_print == NULL ? "" : error_pre_print;
  source_pre_error = savestring (source_pre_error, strlen (source_pre_error));
  make_cleanup (xfree, source_pre_error);
  /* This will get set every time we read a line.  So it won't stay "" for
     long.  */
  error_pre_print = "";

  needed_length = strlen (source_file_name) + strlen (source_pre_error) + 80;
  if (source_error_allocated < needed_length)
    {
      source_error_allocated *= 2;
      if (source_error_allocated < needed_length)
	source_error_allocated = needed_length;
      if (source_error == NULL)
	source_error = xmalloc (source_error_allocated);
      else
	source_error = xrealloc (source_error, source_error_allocated);
    }

  read_command_file (stream);

  do_cleanups (old_cleanups);
}

void
show_user_1 (struct cmd_list_element *c, struct ui_file *stream)
{
  struct command_line *cmdlines;

  cmdlines = c->user_commands;
  if (!cmdlines)
    return;
  fputs_filtered ("User command ", stream);
  fputs_filtered (c->name, stream);
  fputs_filtered (":\n", stream);

  print_command_lines (uiout, cmdlines, 1);
  fputs_filtered ("\n", stream);
}

