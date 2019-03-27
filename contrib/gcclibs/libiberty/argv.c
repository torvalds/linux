/* Create and destroy argument vectors (argv's)
   Copyright (C) 1992, 2001 Free Software Foundation, Inc.
   Written by Fred Fish @ Cygnus Support

This file is part of the libiberty library.
Libiberty is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

Libiberty is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with libiberty; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */


/*  Create and destroy argument vectors.  An argument vector is simply an
    array of string pointers, terminated by a NULL pointer. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ansidecl.h"
#include "libiberty.h"
#include "safe-ctype.h"

/*  Routines imported from standard C runtime libraries. */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef NULL
#define NULL 0
#endif

#ifndef EOS
#define EOS '\0'
#endif

#define INITIAL_MAXARGC 8	/* Number of args + NULL in initial argv */


/*

@deftypefn Extension char** dupargv (char **@var{vector})

Duplicate an argument vector.  Simply scans through @var{vector},
duplicating each argument until the terminating @code{NULL} is found.
Returns a pointer to the argument vector if successful.  Returns
@code{NULL} if there is insufficient memory to complete building the
argument vector.

@end deftypefn

*/

char **
dupargv (char **argv)
{
  int argc;
  char **copy;
  
  if (argv == NULL)
    return NULL;
  
  /* the vector */
  for (argc = 0; argv[argc] != NULL; argc++);
  copy = (char **) malloc ((argc + 1) * sizeof (char *));
  if (copy == NULL)
    return NULL;
  
  /* the strings */
  for (argc = 0; argv[argc] != NULL; argc++)
    {
      int len = strlen (argv[argc]);
      copy[argc] = (char *) malloc (len + 1);
      if (copy[argc] == NULL)
	{
	  freeargv (copy);
	  return NULL;
	}
      strcpy (copy[argc], argv[argc]);
    }
  copy[argc] = NULL;
  return copy;
}

/*

@deftypefn Extension void freeargv (char **@var{vector})

Free an argument vector that was built using @code{buildargv}.  Simply
scans through @var{vector}, freeing the memory for each argument until
the terminating @code{NULL} is found, and then frees @var{vector}
itself.

@end deftypefn

*/

void freeargv (char **vector)
{
  register char **scan;

  if (vector != NULL)
    {
      for (scan = vector; *scan != NULL; scan++)
	{
	  free (*scan);
	}
      free (vector);
    }
}

/*

@deftypefn Extension char** buildargv (char *@var{sp})

Given a pointer to a string, parse the string extracting fields
separated by whitespace and optionally enclosed within either single
or double quotes (which are stripped off), and build a vector of
pointers to copies of the string for each field.  The input string
remains unchanged.  The last element of the vector is followed by a
@code{NULL} element.

All of the memory for the pointer array and copies of the string
is obtained from @code{malloc}.  All of the memory can be returned to the
system with the single function call @code{freeargv}, which takes the
returned result of @code{buildargv}, as it's argument.

Returns a pointer to the argument vector if successful.  Returns
@code{NULL} if @var{sp} is @code{NULL} or if there is insufficient
memory to complete building the argument vector.

If the input is a null string (as opposed to a @code{NULL} pointer),
then buildarg returns an argument vector that has one arg, a null
string.

@end deftypefn

The memory for the argv array is dynamically expanded as necessary.

In order to provide a working buffer for extracting arguments into,
with appropriate stripping of quotes and translation of backslash
sequences, we allocate a working buffer at least as long as the input
string.  This ensures that we always have enough space in which to
work, since the extracted arg is never larger than the input string.

The argument vector is always kept terminated with a @code{NULL} arg
pointer, so it can be passed to @code{freeargv} at any time, or
returned, as appropriate.

*/

char **buildargv (const char *input)
{
  char *arg;
  char *copybuf;
  int squote = 0;
  int dquote = 0;
  int bsquote = 0;
  int argc = 0;
  int maxargc = 0;
  char **argv = NULL;
  char **nargv;

  if (input != NULL)
    {
      copybuf = (char *) alloca (strlen (input) + 1);
      /* Is a do{}while to always execute the loop once.  Always return an
	 argv, even for null strings.  See NOTES above, test case below. */
      do
	{
	  /* Pick off argv[argc] */
	  while (ISBLANK (*input))
	    {
	      input++;
	    }
	  if ((maxargc == 0) || (argc >= (maxargc - 1)))
	    {
	      /* argv needs initialization, or expansion */
	      if (argv == NULL)
		{
		  maxargc = INITIAL_MAXARGC;
		  nargv = (char **) malloc (maxargc * sizeof (char *));
		}
	      else
		{
		  maxargc *= 2;
		  nargv = (char **) realloc (argv, maxargc * sizeof (char *));
		}
	      if (nargv == NULL)
		{
		  if (argv != NULL)
		    {
		      freeargv (argv);
		      argv = NULL;
		    }
		  break;
		}
	      argv = nargv;
	      argv[argc] = NULL;
	    }
	  /* Begin scanning arg */
	  arg = copybuf;
	  while (*input != EOS)
	    {
	      if (ISSPACE (*input) && !squote && !dquote && !bsquote)
		{
		  break;
		}
	      else
		{
		  if (bsquote)
		    {
		      bsquote = 0;
		      *arg++ = *input;
		    }
		  else if (*input == '\\')
		    {
		      bsquote = 1;
		    }
		  else if (squote)
		    {
		      if (*input == '\'')
			{
			  squote = 0;
			}
		      else
			{
			  *arg++ = *input;
			}
		    }
		  else if (dquote)
		    {
		      if (*input == '"')
			{
			  dquote = 0;
			}
		      else
			{
			  *arg++ = *input;
			}
		    }
		  else
		    {
		      if (*input == '\'')
			{
			  squote = 1;
			}
		      else if (*input == '"')
			{
			  dquote = 1;
			}
		      else
			{
			  *arg++ = *input;
			}
		    }
		  input++;
		}
	    }
	  *arg = EOS;
	  argv[argc] = strdup (copybuf);
	  if (argv[argc] == NULL)
	    {
	      freeargv (argv);
	      argv = NULL;
	      break;
	    }
	  argc++;
	  argv[argc] = NULL;

	  while (ISSPACE (*input))
	    {
	      input++;
	    }
	}
      while (*input != EOS);
    }
  return (argv);
}

/*

@deftypefn Extension void expandargv (int *@var{argcp}, char ***@var{argvp})

The @var{argcp} and @code{argvp} arguments are pointers to the usual
@code{argc} and @code{argv} arguments to @code{main}.  This function
looks for arguments that begin with the character @samp{@@}.  Any such
arguments are interpreted as ``response files''.  The contents of the
response file are interpreted as additional command line options.  In
particular, the file is separated into whitespace-separated strings;
each such string is taken as a command-line option.  The new options
are inserted in place of the option naming the response file, and
@code{*argcp} and @code{*argvp} will be updated.  If the value of
@code{*argvp} is modified by this function, then the new value has
been dynamically allocated and can be deallocated by the caller with
@code{freeargv}.  However, most callers will simply call
@code{expandargv} near the beginning of @code{main} and allow the
operating system to free the memory when the program exits.

@end deftypefn

*/

void
expandargv (argcp, argvp)
     int *argcp;
     char ***argvp;
{
  /* The argument we are currently processing.  */
  int i = 0;
  /* Non-zero if ***argvp has been dynamically allocated.  */
  int argv_dynamic = 0;
  /* Loop over the arguments, handling response files.  We always skip
     ARGVP[0], as that is the name of the program being run.  */
  while (++i < *argcp)
    {
      /* The name of the response file.  */
      const char *filename;
      /* The response file.  */
      FILE *f;
      /* An upper bound on the number of characters in the response
	 file.  */
      long pos;
      /* The number of characters in the response file, when actually
	 read.  */
      size_t len;
      /* A dynamically allocated buffer used to hold options read from a
	 response file.  */
      char *buffer;
      /* Dynamically allocated storage for the options read from the
	 response file.  */
      char **file_argv;
      /* The number of options read from the response file, if any.  */
      size_t file_argc;
      /* We are only interested in options of the form "@file".  */
      filename = (*argvp)[i];
      if (filename[0] != '@')
	continue;
      /* Read the contents of the file.  */
      f = fopen (++filename, "r");
      if (!f)
	continue;
      if (fseek (f, 0L, SEEK_END) == -1)
	goto error;
      pos = ftell (f);
      if (pos == -1)
	goto error;
      if (fseek (f, 0L, SEEK_SET) == -1)
	goto error;
      buffer = (char *) xmalloc (pos * sizeof (char) + 1);
      len = fread (buffer, sizeof (char), pos, f);
      if (len != (size_t) pos
	  /* On Windows, fread may return a value smaller than POS,
	     due to CR/LF->CR translation when reading text files.
	     That does not in-and-of itself indicate failure.  */
	  && ferror (f))
	goto error;
      /* Add a NUL terminator.  */
      buffer[len] = '\0';
      /* Parse the string.  */
      file_argv = buildargv (buffer);
      /* If *ARGVP is not already dynamically allocated, copy it.  */
      if (!argv_dynamic)
	{
	  *argvp = dupargv (*argvp);
	  if (!*argvp)
	    {
	      fputs ("\nout of memory\n", stderr);
	      xexit (1);
	    }
	}
      /* Count the number of arguments.  */
      file_argc = 0;
      while (file_argv[file_argc] && *file_argv[file_argc])
	++file_argc;
      /* Now, insert FILE_ARGV into ARGV.  The "+1" below handles the
	 NULL terminator at the end of ARGV.  */ 
      *argvp = ((char **) 
		xrealloc (*argvp, 
			  (*argcp + file_argc + 1) * sizeof (char *)));
      memmove (*argvp + i + file_argc, *argvp + i + 1, 
	       (*argcp - i) * sizeof (char *));
      memcpy (*argvp + i, file_argv, file_argc * sizeof (char *));
      /* The original option has been replaced by all the new
	 options.  */
      *argcp += file_argc - 1;
      /* Free up memory allocated to process the response file.  We do
	 not use freeargv because the individual options in FILE_ARGV
	 are now in the main ARGV.  */
      free (file_argv);
      free (buffer);
      /* Rescan all of the arguments just read to support response
	 files that include other response files.  */
      --i;
    error:
      /* We're all done with the file now.  */
      fclose (f);
    }
}

#ifdef MAIN

/* Simple little test driver. */

static const char *const tests[] =
{
  "a simple command line",
  "arg 'foo' is single quoted",
  "arg \"bar\" is double quoted",
  "arg \"foo bar\" has embedded whitespace",
  "arg 'Jack said \\'hi\\'' has single quotes",
  "arg 'Jack said \\\"hi\\\"' has double quotes",
  "a b c d e f g h i j k l m n o p q r s t u v w x y z 1 2 3 4 5 6 7 8 9",
  
  /* This should be expanded into only one argument.  */
  "trailing-whitespace ",

  "",
  NULL
};

int
main (void)
{
  char **argv;
  const char *const *test;
  char **targs;

  for (test = tests; *test != NULL; test++)
    {
      printf ("buildargv(\"%s\")\n", *test);
      if ((argv = buildargv (*test)) == NULL)
	{
	  printf ("failed!\n\n");
	}
      else
	{
	  for (targs = argv; *targs != NULL; targs++)
	    {
	      printf ("\t\"%s\"\n", *targs);
	    }
	  printf ("\n");
	}
      freeargv (argv);
    }

  return 0;
}

#endif	/* MAIN */
