/* MI Command Set - MI parser.

   Copyright 2000, 2001, 2002 Free Software Foundation, Inc.

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
#include "mi-parse.h"

#include <ctype.h>
#include "gdb_string.h"

static void
mi_parse_argv (char *args, struct mi_parse *parse)
{
  char *chp = args;
  int argc = 0;
  char **argv = xmalloc ((argc + 1) * sizeof (char *));
  argv[argc] = NULL;
  while (1)
    {
      char *arg;
      /* skip leading white space */
      while (isspace (*chp))
	chp++;
      /* Three possibilities: EOF, quoted string, or other text. */
      switch (*chp)
	{
	case '\0':
	  parse->argv = argv;
	  parse->argc = argc;
	  return;
	case '"':
	  {
	    /* A quoted string. */
	    int len;
	    char *start = chp + 1;
	    /* Determine the buffer size. */
	    chp = start;
	    len = 0;
	    while (*chp != '\0' && *chp != '"')
	      {
		if (*chp == '\\')
		  {
		    chp++;
		    if (parse_escape (&chp) <= 0)
		      {
			/* Do not allow split lines or "\000" */
			freeargv (argv);
			return;
		      }
		  }
		else
		  chp++;
		len++;
	      }
	    /* Insist on a closing quote. */
	    if (*chp != '"')
	      {
		freeargv (argv);
		return;
	      }
	    /* Insist on trailing white space. */
	    if (chp[1] != '\0' && !isspace (chp[1]))
	      {
		freeargv (argv);
		return;
	      }
	    /* create the buffer. */
	    arg = xmalloc ((len + 1) * sizeof (char));
	    /* And copy the characters in. */
	    chp = start;
	    len = 0;
	    while (*chp != '\0' && *chp != '"')
	      {
		if (*chp == '\\')
		  {
		    chp++;
		    arg[len] = parse_escape (&chp);
		  }
		else
		  arg[len] = *chp++;
		len++;
	      }
	    arg[len] = '\0';
	    chp++;		/* that closing quote. */
	    break;
	  }
	default:
	  {
	    /* An unquoted string.  Accumulate all non blank
	       characters into a buffer. */
	    int len;
	    char *start = chp;
	    while (*chp != '\0' && !isspace (*chp))
	      {
		chp++;
	      }
	    len = chp - start;
	    arg = xmalloc ((len + 1) * sizeof (char));
	    strncpy (arg, start, len);
	    arg[len] = '\0';
	    break;
	  }
	}
      /* Append arg to argv. */
      argv = xrealloc (argv, (argc + 2) * sizeof (char *));
      argv[argc++] = arg;
      argv[argc] = NULL;
    }
}


void
mi_parse_free (struct mi_parse *parse)
{
  if (parse == NULL)
    return;
  if (parse->command != NULL)
    xfree (parse->command);
  if (parse->token != NULL)
    xfree (parse->token);
  if (parse->args != NULL)
    xfree (parse->args);
  if (parse->argv != NULL)
    freeargv (parse->argv);
  xfree (parse);
}


struct mi_parse *
mi_parse (char *cmd)
{
  char *chp;
  struct mi_parse *parse = XMALLOC (struct mi_parse);
  memset (parse, 0, sizeof (*parse));

  /* Before starting, skip leading white space. */
  while (isspace (*cmd))
    cmd++;

  /* Find/skip any token and then extract it. */
  for (chp = cmd; *chp >= '0' && *chp <= '9'; chp++)
    ;
  parse->token = xmalloc ((chp - cmd + 1) * sizeof (char *));
  memcpy (parse->token, cmd, (chp - cmd));
  parse->token[chp - cmd] = '\0';

  /* This wasn't a real MI command.  Return it as a CLI_COMMAND. */
  if (*chp != '-')
    {
      while (isspace (*chp))
	chp++;
      parse->command = xstrdup (chp);
      parse->op = CLI_COMMAND;
      return parse;
    }

  /* Extract the command. */
  {
    char *tmp = chp + 1;	/* discard ``-'' */
    for (; *chp && !isspace (*chp); chp++)
      ;
    parse->command = xmalloc ((chp - tmp + 1) * sizeof (char *));
    memcpy (parse->command, tmp, chp - tmp);
    parse->command[chp - tmp] = '\0';
  }

  /* Find the command in the MI table. */
  parse->cmd = mi_lookup (parse->command);
  if (parse->cmd == NULL)
    {
      /* FIXME: This should be a function call. */
      fprintf_unfiltered
	(raw_stdout,
	 "%s^error,msg=\"Undefined MI command: %s\"\n",
	 parse->token, parse->command);
      mi_parse_free (parse);
      return NULL;
    }

  /* Skip white space following the command. */
  while (isspace (*chp))
    chp++;

  /* For new argv commands, attempt to return the parsed argument
     list. */
  if (parse->cmd->argv_func != NULL)
    {
      mi_parse_argv (chp, parse);
      if (parse->argv == NULL)
	{
	  /* FIXME: This should be a function call. */
	  fprintf_unfiltered
	    (raw_stdout,
	     "%s^error,msg=\"Problem parsing arguments: %s %s\"\n",
	     parse->token, parse->command, chp);
	  mi_parse_free (parse);
	  return NULL;
	}
    }

  /* FIXME: DELETE THIS */
  /* For CLI and old ARGS commands, also return the remainder of the
     command line as a single string. */
  if (parse->cmd->args_func != NULL
      || parse->cmd->cli.cmd != NULL)
    {
      parse->args = xstrdup (chp);
    }

  /* Fully parsed. */
  parse->op = MI_COMMAND;
  return parse;
}
