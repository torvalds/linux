/* input_file.c - Deal with Input Files -
   Copyright 1987, 1990, 1991, 1992, 1993, 1994, 1995, 1999, 2000, 2001,
   2002, 2003, 2005, 2006
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* Confines all details of reading source bytes to this module.
   All O/S specific crocks should live here.
   What we lose in "efficiency" we gain in modularity.
   Note we don't need to #include the "as.h" file. No common coupling!  */

#include "as.h"
#include "input-file.h"
#include "safe-ctype.h"

static int input_file_get (char *, int);

/* This variable is non-zero if the file currently being read should be
   preprocessed by app.  It is zero if the file can be read straight in.  */
int preprocess = 0;

/* This code opens a file, then delivers BUFFER_SIZE character
   chunks of the file on demand.
   BUFFER_SIZE is supposed to be a number chosen for speed.
   The caller only asks once what BUFFER_SIZE is, and asks before
   the nature of the input files (if any) is known.  */

#define BUFFER_SIZE (32 * 1024)

/* We use static data: the data area is not sharable.  */

static FILE *f_in;
static char *file_name;

/* Struct for saving the state of this module for file includes.  */
struct saved_file
  {
    FILE * f_in;
    char * file_name;
    int    preprocess;
    char * app_save;
  };

/* These hooks accommodate most operating systems.  */

void
input_file_begin (void)
{
  f_in = (FILE *) 0;
}

void
input_file_end (void)
{
}

/* Return BUFFER_SIZE.  */
unsigned int
input_file_buffer_size (void)
{
  return (BUFFER_SIZE);
}

/* Push the state of our input, returning a pointer to saved info that
   can be restored with input_file_pop ().  */

char *
input_file_push (void)
{
  register struct saved_file *saved;

  saved = (struct saved_file *) xmalloc (sizeof *saved);

  saved->f_in = f_in;
  saved->file_name = file_name;
  saved->preprocess = preprocess;
  if (preprocess)
    saved->app_save = app_push ();

  /* Initialize for new file.  */
  input_file_begin ();

  return (char *) saved;
}

void
input_file_pop (char *arg)
{
  register struct saved_file *saved = (struct saved_file *) arg;

  input_file_end ();		/* Close out old file.  */

  f_in = saved->f_in;
  file_name = saved->file_name;
  preprocess = saved->preprocess;
  if (preprocess)
    app_pop (saved->app_save);

  free (arg);
}

void
input_file_open (char *filename, /* "" means use stdin. Must not be 0.  */
		 int pre)
{
  int c;
  char buf[80];

  preprocess = pre;

  assert (filename != 0);	/* Filename may not be NULL.  */
  if (filename[0])
    {
      f_in = fopen (filename, FOPEN_RT);
      file_name = filename;
    }
  else
    {
      /* Use stdin for the input file.  */
      f_in = stdin;
      /* For error messages.  */
      file_name = _("{standard input}");
    }

  if (f_in == NULL)
    {
      as_bad (_("can't open %s for reading: %s"),
	      file_name, xstrerror (errno));
      return;
    }

  c = getc (f_in);

  if (ferror (f_in))
    {
      as_bad (_("can't read from %s: %s"),
	      file_name, xstrerror (errno));

      fclose (f_in);
      f_in = NULL;
      return;
    }

  if (c == '#')
    {
      /* Begins with comment, may not want to preprocess.  */
      c = getc (f_in);
      if (c == 'N')
	{
	  if (fgets (buf, sizeof (buf), f_in)
	      && !strncmp (buf, "O_APP", 5) && ISSPACE (buf[5]))
	    preprocess = 0;
	  if (!strchr (buf, '\n'))
	    ungetc ('#', f_in);	/* It was longer.  */
	  else
	    ungetc ('\n', f_in);
	}
      else if (c == 'A')
	{
	  if (fgets (buf, sizeof (buf), f_in)
	      && !strncmp (buf, "PP", 2) && ISSPACE (buf[2]))
	    preprocess = 1;
	  if (!strchr (buf, '\n'))
	    ungetc ('#', f_in);
	  else
	    ungetc ('\n', f_in);
	}
      else if (c == '\n')
	ungetc ('\n', f_in);
      else
	ungetc ('#', f_in);
    }
  else
    ungetc (c, f_in);
}

/* Close input file.  */

void
input_file_close (void)
{
  /* Don't close a null file pointer.  */
  if (f_in != NULL)
    fclose (f_in);

  f_in = 0;
}

/* This function is passed to do_scrub_chars.  */

static int
input_file_get (char *buf, int buflen)
{
  int size;

  size = fread (buf, sizeof (char), buflen, f_in);
  if (size < 0)
    {
      as_bad (_("can't read from %s: %s"), file_name, xstrerror (errno));
      size = 0;
    }
  return size;
}

/* Read a buffer from the input file.  */

char *
input_file_give_next_buffer (char *where /* Where to place 1st character of new buffer.  */)
{
  char *return_value;		/* -> Last char of what we read, + 1.  */
  register int size;

  if (f_in == (FILE *) 0)
    return 0;
  /* fflush (stdin); could be done here if you want to synchronise
     stdin and stdout, for the case where our input file is stdin.
     Since the assembler shouldn't do any output to stdout, we
     don't bother to synch output and input.  */
  if (preprocess)
    size = do_scrub_chars (input_file_get, where, BUFFER_SIZE);
  else
    size = fread (where, sizeof (char), BUFFER_SIZE, f_in);
  if (size < 0)
    {
      as_bad (_("can't read from %s: %s"), file_name, xstrerror (errno));
      size = 0;
    }
  if (size)
    return_value = where + size;
  else
    {
      if (fclose (f_in))
	as_warn (_("can't close %s: %s"), file_name, xstrerror (errno));

      f_in = (FILE *) 0;
      return_value = 0;
    }

  return return_value;
}
