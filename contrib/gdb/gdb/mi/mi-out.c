/* MI Command Set - output generating routines.

   Copyright 2000, 2002, 2003, 2004 Free Software Foundation, Inc.

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
#include "ui-out.h"
#include "mi-out.h"

struct ui_out_data
  {
    int suppress_field_separator;
    int suppress_output;
    int mi_version;
    struct ui_file *buffer;
  };
typedef struct ui_out_data mi_out_data;

/* These are the MI output functions */

static void mi_table_begin (struct ui_out *uiout, int nbrofcols,
			    int nr_rows, const char *tblid);
static void mi_table_body (struct ui_out *uiout);
static void mi_table_end (struct ui_out *uiout);
static void mi_table_header (struct ui_out *uiout, int width,
			     enum ui_align alig, const char *col_name,
			     const char *colhdr);
static void mi_begin (struct ui_out *uiout, enum ui_out_type type,
		      int level, const char *id);
static void mi_end (struct ui_out *uiout, enum ui_out_type type, int level);
static void mi_field_int (struct ui_out *uiout, int fldno, int width,
			  enum ui_align alig, const char *fldname, int value);
static void mi_field_skip (struct ui_out *uiout, int fldno, int width,
			   enum ui_align alig, const char *fldname);
static void mi_field_string (struct ui_out *uiout, int fldno, int width,
			     enum ui_align alig, const char *fldname,
			     const char *string);
static void mi_field_fmt (struct ui_out *uiout, int fldno,
			  int width, enum ui_align align,
			  const char *fldname, const char *format,
			  va_list args);
static void mi_spaces (struct ui_out *uiout, int numspaces);
static void mi_text (struct ui_out *uiout, const char *string);
static void mi_message (struct ui_out *uiout, int verbosity,
			const char *format, va_list args);
static void mi_wrap_hint (struct ui_out *uiout, char *identstring);
static void mi_flush (struct ui_out *uiout);

/* This is the MI ui-out implementation functions vector */

/* FIXME: This can be initialized dynamically after default is set to
   handle initial output in main.c */

struct ui_out_impl mi_ui_out_impl =
{
  mi_table_begin,
  mi_table_body,
  mi_table_end,
  mi_table_header,
  mi_begin,
  mi_end,
  mi_field_int,
  mi_field_skip,
  mi_field_string,
  mi_field_fmt,
  mi_spaces,
  mi_text,
  mi_message,
  mi_wrap_hint,
  mi_flush,
  NULL,
  1, /* Needs MI hacks.  */
};

/* Prototypes for local functions */

extern void _initialize_mi_out (void);
static void field_separator (struct ui_out *uiout);
static void mi_open (struct ui_out *uiout, const char *name,
		     enum ui_out_type type);
static void mi_close (struct ui_out *uiout, enum ui_out_type type);

/* Mark beginning of a table */

void
mi_table_begin (struct ui_out *uiout,
		int nr_cols,
		int nr_rows,
		const char *tblid)
{
  mi_out_data *data = ui_out_data (uiout);
  mi_open (uiout, tblid, ui_out_type_tuple);
  mi_field_int (uiout, -1/*fldno*/, -1/*width*/, -1/*alin*/,
		"nr_rows", nr_rows);
  mi_field_int (uiout, -1/*fldno*/, -1/*width*/, -1/*alin*/,
		"nr_cols", nr_cols);
  mi_open (uiout, "hdr", ui_out_type_list);
}

/* Mark beginning of a table body */

void
mi_table_body (struct ui_out *uiout)
{
  mi_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  /* close the table header line if there were any headers */
  mi_close (uiout, ui_out_type_list);
  mi_open (uiout, "body", ui_out_type_list);
}

/* Mark end of a table */

void
mi_table_end (struct ui_out *uiout)
{
  mi_out_data *data = ui_out_data (uiout);
  data->suppress_output = 0;
  mi_close (uiout, ui_out_type_list); /* body */
  mi_close (uiout, ui_out_type_tuple);
}

/* Specify table header */

void
mi_table_header (struct ui_out *uiout, int width, enum ui_align alignment,
		 const char *col_name,
		 const char *colhdr)
{
  mi_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  mi_open (uiout, NULL, ui_out_type_tuple);
  mi_field_int (uiout, 0, 0, 0, "width", width);
  mi_field_int (uiout, 0, 0, 0, "alignment", alignment);
  mi_field_string (uiout, 0, 0, 0, "col_name", col_name);
  mi_field_string (uiout, 0, width, alignment, "colhdr", colhdr);
  mi_close (uiout, ui_out_type_tuple);
}

/* Mark beginning of a list */

void
mi_begin (struct ui_out *uiout,
	  enum ui_out_type type,
	  int level,
	  const char *id)
{
  mi_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  mi_open (uiout, id, type);
}

/* Mark end of a list */

void
mi_end (struct ui_out *uiout,
	enum ui_out_type type,
	int level)
{
  mi_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  mi_close (uiout, type);
}

/* output an int field */

void
mi_field_int (struct ui_out *uiout, int fldno, int width,
              enum ui_align alignment, const char *fldname, int value)
{
  char buffer[20];		/* FIXME: how many chars long a %d can become? */
  mi_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;

  sprintf (buffer, "%d", value);
  mi_field_string (uiout, fldno, width, alignment, fldname, buffer);
}

/* used to ommit a field */

void
mi_field_skip (struct ui_out *uiout, int fldno, int width,
               enum ui_align alignment, const char *fldname)
{
  mi_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  mi_field_string (uiout, fldno, width, alignment, fldname, "");
}

/* other specific mi_field_* end up here so alignment and field
   separators are both handled by mi_field_string */

void
mi_field_string (struct ui_out *uiout,
		 int fldno,
		 int width,
		 enum ui_align align,
		 const char *fldname,
		 const char *string)
{
  mi_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  field_separator (uiout);
  if (fldname)
    fprintf_unfiltered (data->buffer, "%s=", fldname);
  fprintf_unfiltered (data->buffer, "\"");
  if (string)
    fputstr_unfiltered (string, '"', data->buffer);
  fprintf_unfiltered (data->buffer, "\"");
}

/* This is the only field function that does not align */

void
mi_field_fmt (struct ui_out *uiout, int fldno,
	      int width, enum ui_align align,
	      const char *fldname,
	      const char *format,
	      va_list args)
{
  mi_out_data *data = ui_out_data (uiout);
  if (data->suppress_output)
    return;
  field_separator (uiout);
  if (fldname)
    fprintf_unfiltered (data->buffer, "%s=\"", fldname);
  else
    fputs_unfiltered ("\"", data->buffer);
  vfprintf_unfiltered (data->buffer, format, args);
  fputs_unfiltered ("\"", data->buffer);
}

void
mi_spaces (struct ui_out *uiout, int numspaces)
{
}

void
mi_text (struct ui_out *uiout, const char *string)
{
}

void
mi_message (struct ui_out *uiout, int verbosity,
	    const char *format,
	    va_list args)
{
}

void
mi_wrap_hint (struct ui_out *uiout, char *identstring)
{
  wrap_here (identstring);
}

void
mi_flush (struct ui_out *uiout)
{
  mi_out_data *data = ui_out_data (uiout);
  gdb_flush (data->buffer);
}

/* local functions */

/* access to ui_out format private members */

static void
field_separator (struct ui_out *uiout)
{
  mi_out_data *data = ui_out_data (uiout);
  if (data->suppress_field_separator)
    data->suppress_field_separator = 0;
  else
    fputc_unfiltered (',', data->buffer);
}

static void
mi_open (struct ui_out *uiout,
	 const char *name,
	 enum ui_out_type type)
{
  mi_out_data *data = ui_out_data (uiout);
  field_separator (uiout);
  data->suppress_field_separator = 1;
  if (name)
    fprintf_unfiltered (data->buffer, "%s=", name);
  switch (type)
    {
    case ui_out_type_tuple:
      fputc_unfiltered ('{', data->buffer);
      break;
    case ui_out_type_list:
      fputc_unfiltered ('[', data->buffer);
      break;
    default:
      internal_error (__FILE__, __LINE__, "bad switch");
    }
}

static void
mi_close (struct ui_out *uiout,
	  enum ui_out_type type)
{
  mi_out_data *data = ui_out_data (uiout);
  switch (type)
    {
    case ui_out_type_tuple:
      fputc_unfiltered ('}', data->buffer);
      break;
    case ui_out_type_list:
      fputc_unfiltered (']', data->buffer);
      break;
    default:
      internal_error (__FILE__, __LINE__, "bad switch");
    }
  data->suppress_field_separator = 0;
}

/* add a string to the buffer */

void
mi_out_buffered (struct ui_out *uiout, char *string)
{
  mi_out_data *data = ui_out_data (uiout);
  fprintf_unfiltered (data->buffer, "%s", string);
}

/* clear the buffer */

void
mi_out_rewind (struct ui_out *uiout)
{
  mi_out_data *data = ui_out_data (uiout);
  ui_file_rewind (data->buffer);
}

/* dump the buffer onto the specified stream */

static void
do_write (void *data, const char *buffer, long length_buffer)
{
  ui_file_write (data, buffer, length_buffer);
}

void
mi_out_put (struct ui_out *uiout,
	    struct ui_file *stream)
{
  mi_out_data *data = ui_out_data (uiout);
  ui_file_put (data->buffer, do_write, stream);
  ui_file_rewind (data->buffer);
}

/* Current MI version.  */

int
mi_version (struct ui_out *uiout)
{
  mi_out_data *data = ui_out_data (uiout);
  return data->mi_version;
}

/* initalize private members at startup */

struct ui_out *
mi_out_new (int mi_version)
{
  int flags = 0;
  mi_out_data *data = XMALLOC (mi_out_data);
  data->suppress_field_separator = 0;
  data->suppress_output = 0;
  data->mi_version = mi_version;
  /* FIXME: This code should be using a ``string_file'' and not the
     TUI buffer hack. */
  data->buffer = mem_fileopen ();
  return ui_out_new (&mi_ui_out_impl, data, flags);
}

/* standard gdb initialization hook */
void
_initialize_mi_out (void)
{
  /* nothing happens here */
}
