/* input_scrub.c - Break up input buffers into whole numbers of lines.
   Copyright 1987, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   2000, 2001, 2003, 2006, 2007
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

#include "as.h"
#include "input-file.h"
#include "sb.h"
#include "listing.h"

/*
 * O/S independent module to supply buffers of sanitised source code
 * to rest of assembler.  We get sanitised input data of arbitrary length.
 * We break these buffers on line boundaries, recombine pieces that
 * were broken across buffers, and return a buffer of full lines to
 * the caller.
 * The last partial line begins the next buffer we build and return to caller.
 * The buffer returned to caller is preceded by BEFORE_STRING and followed
 * by AFTER_STRING, as sentinels. The last character before AFTER_STRING
 * is a newline.
 * Also looks after line numbers, for e.g. error messages.
 */

/*
 * We don't care how filthy our buffers are, but our callers assume
 * that the following sanitation has already been done.
 *
 * No comments, reduce a comment to a space.
 * Reduce a tab to a space unless it is 1st char of line.
 * All multiple tabs and spaces collapsed into 1 char. Tab only
 *   legal if 1st char of line.
 * # line file statements converted to .line x;.file y; statements.
 * Escaped newlines at end of line: remove them but add as many newlines
 *   to end of statement as you removed in the middle, to synch line numbers.
 */

#define BEFORE_STRING ("\n")
#define AFTER_STRING ("\0")	/* memcpy of 0 chars might choke.  */
#define BEFORE_SIZE (1)
#define AFTER_SIZE  (1)

#ifndef TC_EOL_IN_INSN
#define TC_EOL_IN_INSN(P) 0
#endif

static char *buffer_start;	/*->1st char of full buffer area.  */
static char *partial_where;	/*->after last full line in buffer.  */
static int partial_size;	/* >=0. Number of chars in partial line in buffer.  */

/* Because we need AFTER_STRING just after last full line, it clobbers
   1st part of partial line. So we preserve 1st part of partial line
   here.  */
static char save_source[AFTER_SIZE];

/* What is the largest size buffer that input_file_give_next_buffer()
   could return to us?  */
static unsigned int buffer_length;

/* The index into an sb structure we are reading from.  -1 if none.  */
static int sb_index = -1;

/* If we are reading from an sb structure, this is it.  */
static sb from_sb;

/* Should we do a conditional check on from_sb? */
static int from_sb_is_expansion = 1;

/* The number of nested sb structures we have included.  */
int macro_nest;

/* We can have more than one source file open at once, though the info for all
   but the latest one are saved off in a struct input_save.  These files remain
   open, so we are limited by the number of open files allowed by the
   underlying OS. We may also sequentially read more than one source file in an
   assembly.  */

/* We must track the physical file and line number for error messages. We also
   track a "logical" file and line number corresponding to (C?)  compiler
   source line numbers.  Whenever we open a file we must fill in
   physical_input_file. So if it is NULL we have not opened any files yet.  */

static char *physical_input_file;
static char *logical_input_file;

typedef unsigned int line_numberT;	/* 1-origin line number in a source file.  */
/* A line ends in '\n' or eof.  */

static line_numberT physical_input_line;
static int logical_input_line;

/* Struct used to save the state of the input handler during include files */
struct input_save {
  char *              buffer_start;
  char *              partial_where;
  int                 partial_size;
  char                save_source[AFTER_SIZE];
  unsigned int        buffer_length;
  char *              physical_input_file;
  char *              logical_input_file;
  line_numberT        physical_input_line;
  int                 logical_input_line;
  int                 sb_index;
  sb                  from_sb;
  int                 from_sb_is_expansion; /* Should we do a conditional check?  */
  struct input_save * next_saved_file;	/* Chain of input_saves.  */
  char *              input_file_save;	/* Saved state of input routines.  */
  char *              saved_position;	/* Caller's saved position in buf.  */
};

static struct input_save *input_scrub_push (char *saved_position);
static char *input_scrub_pop (struct input_save *arg);

/* Saved information about the file that .include'd this one.  When we hit EOF,
   we automatically pop to that file.  */

static struct input_save *next_saved_file;

/* Push the state of input reading and scrubbing so that we can #include.
   The return value is a 'void *' (fudged for old compilers) to a save
   area, which can be restored by passing it to input_scrub_pop().  */

static struct input_save *
input_scrub_push (char *saved_position)
{
  register struct input_save *saved;

  saved = (struct input_save *) xmalloc (sizeof *saved);

  saved->saved_position = saved_position;
  saved->buffer_start = buffer_start;
  saved->partial_where = partial_where;
  saved->partial_size = partial_size;
  saved->buffer_length = buffer_length;
  saved->physical_input_file = physical_input_file;
  saved->logical_input_file = logical_input_file;
  saved->physical_input_line = physical_input_line;
  saved->logical_input_line = logical_input_line;
  saved->sb_index = sb_index;
  saved->from_sb = from_sb;
  saved->from_sb_is_expansion = from_sb_is_expansion;
  memcpy (saved->save_source, save_source, sizeof (save_source));
  saved->next_saved_file = next_saved_file;
  saved->input_file_save = input_file_push ();

  input_file_begin ();		/* Reinitialize! */
  logical_input_line = -1;
  logical_input_file = (char *) NULL;
  buffer_length = input_file_buffer_size ();
  sb_index = -1;

  buffer_start = xmalloc ((BEFORE_SIZE + buffer_length + buffer_length + AFTER_SIZE));
  memcpy (buffer_start, BEFORE_STRING, (int) BEFORE_SIZE);

  return saved;
}

static char *
input_scrub_pop (struct input_save *saved)
{
  char *saved_position;

  input_scrub_end ();		/* Finish off old buffer */

  input_file_pop (saved->input_file_save);
  saved_position = saved->saved_position;
  buffer_start = saved->buffer_start;
  buffer_length = saved->buffer_length;
  physical_input_file = saved->physical_input_file;
  logical_input_file = saved->logical_input_file;
  physical_input_line = saved->physical_input_line;
  logical_input_line = saved->logical_input_line;
  sb_index = saved->sb_index;
  from_sb = saved->from_sb;
  from_sb_is_expansion = saved->from_sb_is_expansion;
  partial_where = saved->partial_where;
  partial_size = saved->partial_size;
  next_saved_file = saved->next_saved_file;
  memcpy (save_source, saved->save_source, sizeof (save_source));

  free (saved);
  return saved_position;
}

void
input_scrub_begin (void)
{
  know (strlen (BEFORE_STRING) == BEFORE_SIZE);
  know (strlen (AFTER_STRING) == AFTER_SIZE
	|| (AFTER_STRING[0] == '\0' && AFTER_SIZE == 1));

  input_file_begin ();

  buffer_length = input_file_buffer_size ();

  buffer_start = xmalloc ((BEFORE_SIZE + buffer_length + buffer_length + AFTER_SIZE));
  memcpy (buffer_start, BEFORE_STRING, (int) BEFORE_SIZE);

  /* Line number things.  */
  logical_input_line = -1;
  logical_input_file = (char *) NULL;
  physical_input_file = NULL;	/* No file read yet.  */
  next_saved_file = NULL;	/* At EOF, don't pop to any other file */
  do_scrub_begin (flag_m68k_mri);
}

void
input_scrub_end (void)
{
  if (buffer_start)
    {
      free (buffer_start);
      buffer_start = 0;
      input_file_end ();
    }
}

/* Start reading input from a new file.
   Return start of caller's part of buffer.  */

char *
input_scrub_new_file (char *filename)
{
  input_file_open (filename, !flag_no_comments);
  physical_input_file = filename[0] ? filename : _("{standard input}");
  physical_input_line = 0;

  partial_size = 0;
  return (buffer_start + BEFORE_SIZE);
}

/* Include a file from the current file.  Save our state, cause it to
   be restored on EOF, and begin handling a new file.  Same result as
   input_scrub_new_file.  */

char *
input_scrub_include_file (char *filename, char *position)
{
  next_saved_file = input_scrub_push (position);
  return input_scrub_new_file (filename);
}

/* Start getting input from an sb structure.  This is used when
   expanding a macro.  */

void
input_scrub_include_sb (sb *from, char *position, int is_expansion)
{
  if (macro_nest > max_macro_nest)
    as_fatal (_("macros nested too deeply"));
  ++macro_nest;

#ifdef md_macro_start
  if (is_expansion)
    {
      md_macro_start ();
    }
#endif

  next_saved_file = input_scrub_push (position);

  sb_new (&from_sb);
  from_sb_is_expansion = is_expansion;
  if (from->len >= 1 && from->ptr[0] != '\n')
    {
      /* Add the sentinel required by read.c.  */
      sb_add_char (&from_sb, '\n');
    }
  sb_scrub_and_add_sb (&from_sb, from);
  sb_index = 1;

  /* These variables are reset by input_scrub_push.  Restore them
     since we are, after all, still at the same point in the file.  */
  logical_input_line = next_saved_file->logical_input_line;
  logical_input_file = next_saved_file->logical_input_file;
}

void
input_scrub_close (void)
{
  input_file_close ();
}

char *
input_scrub_next_buffer (char **bufp)
{
  register char *limit;		/*->just after last char of buffer.  */

  if (sb_index >= 0)
    {
      if (sb_index >= from_sb.len)
	{
	  sb_kill (&from_sb);
	  if (from_sb_is_expansion
	      )
	    {
	      cond_finish_check (macro_nest);
#ifdef md_macro_end
	      /* Allow the target to clean up per-macro expansion
	         data.  */
	      md_macro_end ();
#endif
	    }
	  --macro_nest;
	  partial_where = NULL;
	  if (next_saved_file != NULL)
	    *bufp = input_scrub_pop (next_saved_file);
	  return partial_where;
	}

      partial_where = from_sb.ptr + from_sb.len;
      partial_size = 0;
      *bufp = from_sb.ptr + sb_index;
      sb_index = from_sb.len;
      return partial_where;
    }

  *bufp = buffer_start + BEFORE_SIZE;

  if (partial_size)
    {
      memmove (buffer_start + BEFORE_SIZE, partial_where,
	      (unsigned int) partial_size);
      memcpy (buffer_start + BEFORE_SIZE, save_source, AFTER_SIZE);
    }
  limit = input_file_give_next_buffer (buffer_start
				       + BEFORE_SIZE
				       + partial_size);
  if (limit)
    {
      register char *p;		/* Find last newline.  */
      /* Terminate the buffer to avoid confusing TC_EOL_IN_INSN.  */
      *limit = '\0';
      for (p = limit - 1; *p != '\n' || TC_EOL_IN_INSN (p); --p)
	;
      ++p;

      while (p <= buffer_start + BEFORE_SIZE)
	{
	  int limoff;

	  limoff = limit - buffer_start;
	  buffer_length += input_file_buffer_size ();
	  buffer_start = xrealloc (buffer_start,
				   (BEFORE_SIZE
				    + 2 * buffer_length
				    + AFTER_SIZE));
	  *bufp = buffer_start + BEFORE_SIZE;
	  limit = input_file_give_next_buffer (buffer_start + limoff);

	  if (limit == NULL)
	    {
	      as_warn (_("partial line at end of file ignored"));
	      partial_where = NULL;
	      if (next_saved_file)
		*bufp = input_scrub_pop (next_saved_file);
	      return NULL;
	    }

	  /* Terminate the buffer to avoid confusing TC_EOL_IN_INSN.  */
	  *limit = '\0';
	  for (p = limit - 1; *p != '\n' || TC_EOL_IN_INSN (p); --p)
	    ;
	  ++p;
	}

      partial_where = p;
      partial_size = limit - p;
      memcpy (save_source, partial_where, (int) AFTER_SIZE);
      memcpy (partial_where, AFTER_STRING, (int) AFTER_SIZE);
    }
  else
    {
      partial_where = 0;
      if (partial_size > 0)
	{
	  as_warn (_("partial line at end of file ignored"));
	}

      /* Tell the listing we've finished the file.  */
      LISTING_EOF ();

      /* If we should pop to another file at EOF, do it.  */
      if (next_saved_file)
	{
	  *bufp = input_scrub_pop (next_saved_file);	/* Pop state */
	  /* partial_where is now correct to return, since we popped it.  */
	}
    }
  return (partial_where);
}

/* The remaining part of this file deals with line numbers, error
   messages and so on.  Return TRUE if we opened any file.  */

int
seen_at_least_1_file (void)
{
  return (physical_input_file != NULL);
}

void
bump_line_counters (void)
{
  if (sb_index < 0)
    {
      ++physical_input_line;
      if (logical_input_line >= 0)
	++logical_input_line;
    }
}

/* Tells us what the new logical line number and file are.
   If the line_number is -1, we don't change the current logical line
   number.  If it is -2, we decrement the logical line number (this is
   to support the .appfile pseudo-op inserted into the stream by
   do_scrub_chars).
   If the fname is NULL, we don't change the current logical file name.
   Returns nonzero if the filename actually changes.  */

int
new_logical_line_flags (char *fname, /* DON'T destroy it!  We point to it!  */
			int line_number,
			int flags)
{
  switch (flags)
    {
    case 0:
      break;
    case 1:
      if (line_number != -1)
	abort ();
      break;
    case 1 << 1:
    case 1 << 2:
      /* FIXME: we could check that include nesting is correct.  */
      break;
    default:
      abort ();
    }

  if (line_number >= 0)
    logical_input_line = line_number;
  else if (line_number == -1 && fname && !*fname && (flags & (1 << 2)))
    {
      logical_input_file = physical_input_file;
      logical_input_line = physical_input_line;
      fname = NULL;
    }

  if (fname
      && (logical_input_file == NULL
	  || strcmp (logical_input_file, fname)))
    {
      logical_input_file = fname;
      return 1;
    }
  else
    return 0;
}

int
new_logical_line (char *fname, int line_number)
{
  return new_logical_line_flags (fname, line_number, 0);
}


/* Return the current file name and line number.
   namep should be char * const *, but there are compilers which screw
   up declarations like that, and it's easier to avoid it.  */

void
as_where (char **namep, unsigned int *linep)
{
  if (logical_input_file != NULL
      && (linep == NULL || logical_input_line >= 0))
    {
      *namep = logical_input_file;
      if (linep != NULL)
	*linep = logical_input_line;
    }
  else if (physical_input_file != NULL)
    {
      *namep = physical_input_file;
      if (linep != NULL)
	*linep = physical_input_line;
    }
  else
    {
      *namep = 0;
      if (linep != NULL)
	*linep = 0;
    }
}
