/* Output generating routines for GDB CLI.

   Copyright 1999, 2000, 2001, 2002, 2003 Free Software Foundation,
   Inc.

   Contributed by Cygnus Solutions.
   Written by Fernando Nasser for Cygnus.

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
#include "ui-out.h"
#include "tui.h"
#include "gdb_string.h"
#include "gdb_assert.h"

struct ui_out_data
  {
    struct ui_file *stream;
    int suppress_output;
    int line;
    int start_of_line;
  };
typedef struct ui_out_data tui_out_data;

/* These are the CLI output functions */

static void tui_table_begin (struct ui_out *uiout, int nbrofcols,
			     int nr_rows, const char *tblid);
static void tui_table_body (struct ui_out *uiout);
static void tui_table_end (struct ui_out *uiout);
static void tui_table_header (struct ui_out *uiout, int width,
			      enum ui_align alig, const char *col_name,
			      const char *colhdr);
static void tui_begin (struct ui_out *uiout, enum ui_out_type type,
		       int level, const char *lstid);
static void tui_end (struct ui_out *uiout, enum ui_out_type type, int level);
static void tui_field_int (struct ui_out *uiout, int fldno, int width,
			   enum ui_align alig, const char *fldname, int value);
static void tui_field_skip (struct ui_out *uiout, int fldno, int width,
			    enum ui_align alig, const char *fldname);
static void tui_field_string (struct ui_out *uiout, int fldno, int width,
			      enum ui_align alig, const char *fldname,
			      const char *string);
static void tui_field_fmt (struct ui_out *uiout, int fldno,
			   int width, enum ui_align align,
			   const char *fldname, const char *format,
			   va_list args);
static void tui_spaces (struct ui_out *uiout, int numspaces);
static void tui_text (struct ui_out *uiout, const char *string);
static void tui_message (struct ui_out *uiout, int verbosity,
			 const char *format, va_list args);
static void tui_wrap_hint (struct ui_out *uiout, char *identstring);
static void tui_flush (struct ui_out *uiout);

/* This is the CLI ui-out implementation functions vector */

/* FIXME: This can be initialized dynamically after default is set to
   handle initial output in main.c */

static struct ui_out_impl tui_ui_out_impl =
{
  tui_table_begin,
  tui_table_body,
  tui_table_end,
  tui_table_header,
  tui_begin,
  tui_end,
  tui_field_int,
  tui_field_skip,
  tui_field_string,
  tui_field_fmt,
  tui_spaces,
  tui_text,
  tui_message,
  tui_wrap_hint,
  tui_flush,
  NULL,
  0, /* Does not need MI hacks (i.e. needs CLI hacks).  */
};

/* Prototypes for local functions */

extern void _initialize_tui_out (void);

static void field_separator (void);

static void out_field_fmt (struct ui_out *uiout, int fldno,
			   const char *fldname,
			   const char *format,...);

/* local variables */

/* (none yet) */

/* Mark beginning of a table */

void
tui_table_begin (struct ui_out *uiout, int nbrofcols,
		 int nr_rows,
		 const char *tblid)
{
  tui_out_data *data = ui_out_data (uiout);
  if (nr_rows == 0)
    data->suppress_output = 1;
  else
    /* Only the table suppresses the output and, fortunately, a table
       is not a recursive data structure. */
    gdb_assert (data->suppress_output == 0);
}

/* Mark beginning of a table body */

void
tui_table_body (struct ui_out *uiout)
{
  tui_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  /* first, close the table header line */
  tui_text (uiout, "\n");
}

/* Mark end of a table */

void
tui_table_end (struct ui_out *uiout)
{
  tui_out_data *data = ui_out_data (uiout);
  data->suppress_output = 0;
}

/* Specify table header */

void
tui_table_header (struct ui_out *uiout, int width, enum ui_align alignment,
		  const char *col_name,
		  const char *colhdr)
{
  tui_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  tui_field_string (uiout, 0, width, alignment, 0, colhdr);
}

/* Mark beginning of a list */

void
tui_begin (struct ui_out *uiout,
	   enum ui_out_type type,
	   int level,
	   const char *id)
{
  tui_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
}

/* Mark end of a list */

void
tui_end (struct ui_out *uiout,
	 enum ui_out_type type,
	 int level)
{
  tui_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
}

/* output an int field */

void
tui_field_int (struct ui_out *uiout, int fldno, int width,
	       enum ui_align alignment,
	       const char *fldname, int value)
{
  char buffer[20];		/* FIXME: how many chars long a %d can become? */

  tui_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;

  /* Don't print line number, keep it for later.  */
  if (data->start_of_line == 0 && strcmp (fldname, "line") == 0)
    {
      data->start_of_line ++;
      data->line = value;
      return;
    }
  data->start_of_line ++;
  sprintf (buffer, "%d", value);
  tui_field_string (uiout, fldno, width, alignment, fldname, buffer);
}

/* used to ommit a field */

void
tui_field_skip (struct ui_out *uiout, int fldno, int width,
		enum ui_align alignment,
		const char *fldname)
{
  tui_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  tui_field_string (uiout, fldno, width, alignment, fldname, "");
}

/* other specific tui_field_* end up here so alignment and field
   separators are both handled by tui_field_string */

void
tui_field_string (struct ui_out *uiout,
		  int fldno,
		  int width,
		  enum ui_align align,
		  const char *fldname,
		  const char *string)
{
  int before = 0;
  int after = 0;

  tui_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;

  if (fldname && data->line > 0 && strcmp (fldname, "file") == 0)
    {
      data->start_of_line ++;
      if (data->line > 0)
        {
          tui_show_source (string, data->line);
        }
      return;
    }
  
  data->start_of_line ++;
  if ((align != ui_noalign) && string)
    {
      before = width - strlen (string);
      if (before <= 0)
	before = 0;
      else
	{
	  if (align == ui_right)
	    after = 0;
	  else if (align == ui_left)
	    {
	      after = before;
	      before = 0;
	    }
	  else
	    /* ui_center */
	    {
	      after = before / 2;
	      before -= after;
	    }
	}
    }

  if (before)
    ui_out_spaces (uiout, before);
  if (string)
    out_field_fmt (uiout, fldno, fldname, "%s", string);
  if (after)
    ui_out_spaces (uiout, after);

  if (align != ui_noalign)
    field_separator ();
}

/* This is the only field function that does not align */

void
tui_field_fmt (struct ui_out *uiout, int fldno,
	       int width, enum ui_align align,
	       const char *fldname,
	       const char *format,
	       va_list args)
{
  tui_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;

  data->start_of_line ++;
  vfprintf_filtered (data->stream, format, args);

  if (align != ui_noalign)
    field_separator ();
}

void
tui_spaces (struct ui_out *uiout, int numspaces)
{
  tui_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  print_spaces_filtered (numspaces, data->stream);
}

void
tui_text (struct ui_out *uiout, const char *string)
{
  tui_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  data->start_of_line ++;
  if (data->line > 0)
    {
      if (strchr (string, '\n') != 0)
        {
          data->line = -1;
          data->start_of_line = 0;
        }
      return;
    }
  if (strchr (string, '\n'))
    data->start_of_line = 0;
  fputs_filtered (string, data->stream);
}

void
tui_message (struct ui_out *uiout, int verbosity,
	     const char *format, va_list args)
{
  tui_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  if (ui_out_get_verblvl (uiout) >= verbosity)
    vfprintf_unfiltered (data->stream, format, args);
}

void
tui_wrap_hint (struct ui_out *uiout, char *identstring)
{
  tui_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  wrap_here (identstring);
}

void
tui_flush (struct ui_out *uiout)
{
  tui_out_data *data = ui_out_data (uiout);
  gdb_flush (data->stream);
}

/* local functions */

/* Like tui_field_fmt, but takes a variable number of args
   and makes a va_list and does not insert a separator */

/* VARARGS */
static void
out_field_fmt (struct ui_out *uiout, int fldno,
	       const char *fldname,
	       const char *format,...)
{
  tui_out_data *data = ui_out_data (uiout);
  va_list args;

  va_start (args, format);
  vfprintf_filtered (data->stream, format, args);

  va_end (args);
}

/* access to ui_out format private members */

static void
field_separator (void)
{
  tui_out_data *data = ui_out_data (uiout);
  fputc_filtered (' ', data->stream);
}

/* initalize private members at startup */

struct ui_out *
tui_out_new (struct ui_file *stream)
{
  int flags = 0;

  tui_out_data *data = XMALLOC (tui_out_data);
  data->stream = stream;
  data->suppress_output = 0;
  data->line = -1;
  data->start_of_line = 0;
  return ui_out_new (&tui_ui_out_impl, data, flags);
}

/* standard gdb initialization hook */
void
_initialize_tui_out (void)
{
  /* nothing needs to be done */
}
