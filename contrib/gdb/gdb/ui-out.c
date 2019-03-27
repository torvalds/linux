/* Output generating routines for GDB.

   Copyright 1999, 2000, 2001, 2002, 2004 Free Software Foundation, Inc.

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
#include "gdb_string.h"
#include "expression.h"		/* For language.h */
#include "language.h"
#include "ui-out.h"
#include "gdb_assert.h"

/* table header structures */

struct ui_out_hdr
  {
    int colno;
    int width;
    int alignment;
    char *col_name;
    char *colhdr;
    struct ui_out_hdr *next;
  };

/* Maintain a stack so that the info applicable to the inner most list
   is always available.  Stack/nested level 0 is reserved for the
   top-level result. */

enum { MAX_UI_OUT_LEVELS = 6 };

struct ui_out_level
  {
    /* Count each field; the first element is for non-list fields */
    int field_count;
    /* The type of this level. */
    enum ui_out_type type;
  };

/* Tables are special.  Maintain a separate structure that tracks
   their state.  At present an output can only contain a single table
   but that restriction might eventually be lifted.  */

struct ui_out_table
{
  /* If on, a table is being generated.  */
  int flag;

  /* If on, the body of a table is being generated.  If off, the table
     header is being generated.  */
  int body_flag;

  /* The level at which each entry of the table is to be found.  A row
     (a tuple) is made up of entries.  Consequently ENTRY_LEVEL is one
     above that of the table.  */
  int entry_level;

  /* Number of table columns (as specified in the table_begin call).  */
  int columns;

  /* String identifying the table (as specified in the table_begin
     call).  */
  char *id;

  /* Points to the first table header (if any).  */
  struct ui_out_hdr *header_first;

  /* Points to the last table header (if any).  */
  struct ui_out_hdr *header_last;

  /* Points to header of NEXT column to format.  */
  struct ui_out_hdr *header_next;

};


/* The ui_out structure */
/* Any change here requires a corresponding one in the initialization
   of the default uiout, which is statically initialized */

struct ui_out
  {
    int flags;
    /* specific implementation of ui-out */
    struct ui_out_impl *impl;
    struct ui_out_data *data;

    /* Sub structure tracking the ui-out depth.  */
    int level;
    struct ui_out_level levels[MAX_UI_OUT_LEVELS];

    /* A table, if any.  At present only a single table is supported.  */
    struct ui_out_table table;
  };

/* The current (inner most) level. */
static struct ui_out_level *
current_level (struct ui_out *uiout)
{
  return &uiout->levels[uiout->level];
}

/* Create a new level, of TYPE.  Return the new level's index. */
static int
push_level (struct ui_out *uiout,
	    enum ui_out_type type,
	    const char *id)
{
  struct ui_out_level *current;
  /* We had better not overflow the buffer. */
  uiout->level++;
  gdb_assert (uiout->level >= 0 && uiout->level < MAX_UI_OUT_LEVELS);
  current = current_level (uiout);
  current->field_count = 0;
  current->type = type;
  return uiout->level;
}

/* Discard the current level, return the discarded level's index.
   TYPE is the type of the level being discarded. */
static int
pop_level (struct ui_out *uiout,
	   enum ui_out_type type)
{
  /* We had better not underflow the buffer. */
  gdb_assert (uiout->level > 0 && uiout->level < MAX_UI_OUT_LEVELS);
  gdb_assert (current_level (uiout)->type == type);
  uiout->level--;
  return uiout->level + 1;
}


/* These are the default implementation functions */

static void default_table_begin (struct ui_out *uiout, int nbrofcols,
				 int nr_rows, const char *tblid);
static void default_table_body (struct ui_out *uiout);
static void default_table_end (struct ui_out *uiout);
static void default_table_header (struct ui_out *uiout, int width,
				  enum ui_align alig, const char *col_name,
				  const char *colhdr);
static void default_begin (struct ui_out *uiout,
			   enum ui_out_type type,
			   int level, const char *id);
static void default_end (struct ui_out *uiout,
			 enum ui_out_type type,
			 int level);
static void default_field_int (struct ui_out *uiout, int fldno, int width,
			       enum ui_align alig,
			       const char *fldname,
			       int value);
static void default_field_skip (struct ui_out *uiout, int fldno, int width,
				enum ui_align alig,
				const char *fldname);
static void default_field_string (struct ui_out *uiout, int fldno, int width,
				  enum ui_align align,
				  const char *fldname,
				  const char *string);
static void default_field_fmt (struct ui_out *uiout, int fldno,
			       int width, enum ui_align align,
			       const char *fldname,
			       const char *format,
			       va_list args);
static void default_spaces (struct ui_out *uiout, int numspaces);
static void default_text (struct ui_out *uiout, const char *string);
static void default_message (struct ui_out *uiout, int verbosity,
			     const char *format,
			     va_list args);
static void default_wrap_hint (struct ui_out *uiout, char *identstring);
static void default_flush (struct ui_out *uiout);

/* This is the default ui-out implementation functions vector */

struct ui_out_impl default_ui_out_impl =
{
  default_table_begin,
  default_table_body,
  default_table_end,
  default_table_header,
  default_begin,
  default_end,
  default_field_int,
  default_field_skip,
  default_field_string,
  default_field_fmt,
  default_spaces,
  default_text,
  default_message,
  default_wrap_hint,
  default_flush,
  NULL,
  0, /* Does not need MI hacks.  */
};

/* The default ui_out */

struct ui_out def_uiout =
{
  0,				/* flags */
  &default_ui_out_impl,		/* impl */
};

/* Pointer to current ui_out */
/* FIXME: This should not be a global, but something passed down from main.c
   or top.c */

struct ui_out *uiout = &def_uiout;

/* These are the interfaces to implementation functions */

static void uo_table_begin (struct ui_out *uiout, int nbrofcols,
			    int nr_rows, const char *tblid);
static void uo_table_body (struct ui_out *uiout);
static void uo_table_end (struct ui_out *uiout);
static void uo_table_header (struct ui_out *uiout, int width,
			     enum ui_align align, const char *col_name,
			     const char *colhdr);
static void uo_begin (struct ui_out *uiout,
		      enum ui_out_type type,
		      int level, const char *id);
static void uo_end (struct ui_out *uiout,
		    enum ui_out_type type,
		    int level);
static void uo_field_int (struct ui_out *uiout, int fldno, int width,
			  enum ui_align align, const char *fldname, int value);
static void uo_field_skip (struct ui_out *uiout, int fldno, int width,
			   enum ui_align align, const char *fldname);
static void uo_field_string (struct ui_out *uiout, int fldno, int width,
			     enum ui_align align, const char *fldname,
			     const char *string);
static void uo_field_fmt (struct ui_out *uiout, int fldno, int width,
			  enum ui_align align, const char *fldname,
			  const char *format, va_list args);
static void uo_spaces (struct ui_out *uiout, int numspaces);
static void uo_text (struct ui_out *uiout, const char *string);
static void uo_message (struct ui_out *uiout, int verbosity,
			const char *format, va_list args);
static void uo_wrap_hint (struct ui_out *uiout, char *identstring);
static void uo_flush (struct ui_out *uiout);
static int uo_redirect (struct ui_out *uiout, struct ui_file *outstream);

/* Prototypes for local functions */

extern void _initialize_ui_out (void);
static void append_header_to_list (struct ui_out *uiout, int width,
				   int alignment, const char *col_name,
				   const char *colhdr);
static int get_next_header (struct ui_out *uiout, int *colno, int *width,
			    int *alignment, char **colhdr);
static void clear_header_list (struct ui_out *uiout);
static void verify_field (struct ui_out *uiout, int *fldno, int *width,
			  int *align);

/* exported functions (ui_out API) */

/* Mark beginning of a table */

static void
ui_out_table_begin (struct ui_out *uiout, int nbrofcols,
		    int nr_rows,
		    const char *tblid)
{
  if (uiout->table.flag)
    internal_error (__FILE__, __LINE__,
		    "tables cannot be nested; table_begin found before \
previous table_end.");

  uiout->table.flag = 1;
  uiout->table.body_flag = 0;
  uiout->table.entry_level = uiout->level + 1;
  uiout->table.columns = nbrofcols;
  if (tblid != NULL)
    uiout->table.id = xstrdup (tblid);
  else
    uiout->table.id = NULL;
  clear_header_list (uiout);

  uo_table_begin (uiout, nbrofcols, nr_rows, uiout->table.id);
}

void
ui_out_table_body (struct ui_out *uiout)
{
  if (!uiout->table.flag)
    internal_error (__FILE__, __LINE__,
		    "table_body outside a table is not valid; it must be \
after a table_begin and before a table_end.");
  if (uiout->table.body_flag)
    internal_error (__FILE__, __LINE__,
		    "extra table_body call not allowed; there must be \
only one table_body after a table_begin and before a table_end.");
  if (uiout->table.header_next->colno != uiout->table.columns)
    internal_error (__FILE__, __LINE__,
		    "number of headers differ from number of table \
columns.");

  uiout->table.body_flag = 1;
  uiout->table.header_next = uiout->table.header_first;

  uo_table_body (uiout);
}

static void
ui_out_table_end (struct ui_out *uiout)
{
  if (!uiout->table.flag)
    internal_error (__FILE__, __LINE__,
		    "misplaced table_end or missing table_begin.");

  uiout->table.entry_level = 0;
  uiout->table.body_flag = 0;
  uiout->table.flag = 0;

  uo_table_end (uiout);

  if (uiout->table.id)
    xfree (uiout->table.id);
  clear_header_list (uiout);
}

void
ui_out_table_header (struct ui_out *uiout, int width, enum ui_align alignment,
		     const char *col_name,
		     const char *colhdr)
{
  if (!uiout->table.flag || uiout->table.body_flag)
    internal_error (__FILE__, __LINE__,
		    "table header must be specified after table_begin \
and before table_body.");

  append_header_to_list (uiout, width, alignment, col_name, colhdr);

  uo_table_header (uiout, width, alignment, col_name, colhdr);
}

static void
do_cleanup_table_end (void *data)
{
  struct ui_out *ui_out = data;

  ui_out_table_end (ui_out);
}

struct cleanup *
make_cleanup_ui_out_table_begin_end (struct ui_out *ui_out, int nr_cols,
                                     int nr_rows, const char *tblid)
{
  ui_out_table_begin (ui_out, nr_cols, nr_rows, tblid);
  return make_cleanup (do_cleanup_table_end, ui_out);
}

void
ui_out_begin (struct ui_out *uiout,
	      enum ui_out_type type,
	      const char *id)
{
  int new_level;
  if (uiout->table.flag && !uiout->table.body_flag)
    internal_error (__FILE__, __LINE__,
		    "table header or table_body expected; lists must be \
specified after table_body.");

  /* Be careful to verify the ``field'' before the new tuple/list is
     pushed onto the stack.  That way the containing list/table/row is
     verified and not the newly created tuple/list.  This verification
     is needed (at least) for the case where a table row entry
     contains either a tuple/list.  For that case bookkeeping such as
     updating the column count or advancing to the next heading still
     needs to be performed.  */
  {
    int fldno;
    int width;
    int align;
    verify_field (uiout, &fldno, &width, &align);
  }

  new_level = push_level (uiout, type, id);

  /* If the push puts us at the same level as a table row entry, we've
     got a new table row.  Put the header pointer back to the start.  */
  if (uiout->table.body_flag
      && uiout->table.entry_level == new_level)
    uiout->table.header_next = uiout->table.header_first;

  uo_begin (uiout, type, new_level, id);
}

void
ui_out_end (struct ui_out *uiout,
	    enum ui_out_type type)
{
  int old_level = pop_level (uiout, type);
  uo_end (uiout, type, old_level);
}

struct ui_out_end_cleanup_data
{
  struct ui_out *uiout;
  enum ui_out_type type;
};

static void
do_cleanup_end (void *data)
{
  struct ui_out_end_cleanup_data *end_cleanup_data = data;
  ui_out_end (end_cleanup_data->uiout, end_cleanup_data->type);
  xfree (end_cleanup_data);
}

static struct cleanup *
make_cleanup_ui_out_end (struct ui_out *uiout,
			 enum ui_out_type type)
{
  struct ui_out_end_cleanup_data *end_cleanup_data;
  end_cleanup_data = XMALLOC (struct ui_out_end_cleanup_data);
  end_cleanup_data->uiout = uiout;
  end_cleanup_data->type = type;
  return make_cleanup (do_cleanup_end, end_cleanup_data);
}

struct cleanup *
make_cleanup_ui_out_tuple_begin_end (struct ui_out *uiout,
				     const char *id)
{
  ui_out_begin (uiout, ui_out_type_tuple, id);
  return make_cleanup_ui_out_end (uiout, ui_out_type_tuple);
}

struct cleanup *
make_cleanup_ui_out_list_begin_end (struct ui_out *uiout,
				    const char *id)
{
  ui_out_begin (uiout, ui_out_type_list, id);
  return make_cleanup_ui_out_end (uiout, ui_out_type_list);
}

void
ui_out_field_int (struct ui_out *uiout,
		  const char *fldname,
		  int value)
{
  int fldno;
  int width;
  int align;
  struct ui_out_level *current = current_level (uiout);

  verify_field (uiout, &fldno, &width, &align);

  uo_field_int (uiout, fldno, width, align, fldname, value);
}

void
ui_out_field_fmt_int (struct ui_out *uiout,
                      int input_width,
                      enum ui_align input_align,
		      const char *fldname,
		      int value)
{
  int fldno;
  int width;
  int align;
  struct ui_out_level *current = current_level (uiout);

  verify_field (uiout, &fldno, &width, &align);

  uo_field_int (uiout, fldno, input_width, input_align, fldname, value);
}

void
ui_out_field_core_addr (struct ui_out *uiout,
			const char *fldname,
			CORE_ADDR address)
{
  char addstr[20];

  /* FIXME: cagney/2002-05-03: Need local_address_string() function
     that returns the language localized string formatted to a width
     based on TARGET_ADDR_BIT.  */
  /* print_address_numeric (address, 1, local_stream); */
  if (TARGET_ADDR_BIT <= 32)
    strcpy (addstr, local_hex_string_custom (address, "08l"));
  else
    strcpy (addstr, local_hex_string_custom (address, "016l"));

  ui_out_field_string (uiout, fldname, addstr);
}

void
ui_out_field_stream (struct ui_out *uiout,
		     const char *fldname,
		     struct ui_stream *buf)
{
  long length;
  char *buffer = ui_file_xstrdup (buf->stream, &length);
  struct cleanup *old_cleanup = make_cleanup (xfree, buffer);
  if (length > 0)
    ui_out_field_string (uiout, fldname, buffer);
  else
    ui_out_field_skip (uiout, fldname);
  ui_file_rewind (buf->stream);
  do_cleanups (old_cleanup);
}

/* used to ommit a field */

void
ui_out_field_skip (struct ui_out *uiout,
		   const char *fldname)
{
  int fldno;
  int width;
  int align;

  verify_field (uiout, &fldno, &width, &align);

  uo_field_skip (uiout, fldno, width, align, fldname);
}

void
ui_out_field_string (struct ui_out *uiout,
		     const char *fldname,
		     const char *string)
{
  int fldno;
  int width;
  int align;

  verify_field (uiout, &fldno, &width, &align);

  uo_field_string (uiout, fldno, width, align, fldname, string);
}

/* VARARGS */
void
ui_out_field_fmt (struct ui_out *uiout,
		  const char *fldname,
		  const char *format, ...)
{
  va_list args;
  int fldno;
  int width;
  int align;

  /* will not align, but has to call anyway */
  verify_field (uiout, &fldno, &width, &align);

  va_start (args, format);

  uo_field_fmt (uiout, fldno, width, align, fldname, format, args);

  va_end (args);
}

void
ui_out_spaces (struct ui_out *uiout, int numspaces)
{
  uo_spaces (uiout, numspaces);
}

void
ui_out_text (struct ui_out *uiout,
	     const char *string)
{
  uo_text (uiout, string);
}

void
ui_out_message (struct ui_out *uiout, int verbosity,
		const char *format,...)
{
  va_list args;

  va_start (args, format);

  uo_message (uiout, verbosity, format, args);

  va_end (args);
}

struct ui_stream *
ui_out_stream_new (struct ui_out *uiout)
{
  struct ui_stream *tempbuf;

  tempbuf = XMALLOC (struct ui_stream);
  tempbuf->uiout = uiout;
  tempbuf->stream = mem_fileopen ();
  return tempbuf;
}

void
ui_out_stream_delete (struct ui_stream *buf)
{
  ui_file_delete (buf->stream);
  xfree (buf);
}

static void
do_stream_delete (void *buf)
{
  ui_out_stream_delete (buf);
}

struct cleanup *
make_cleanup_ui_out_stream_delete (struct ui_stream *buf)
{
  return make_cleanup (do_stream_delete, buf);
}


void
ui_out_wrap_hint (struct ui_out *uiout, char *identstring)
{
  uo_wrap_hint (uiout, identstring);
}

void
ui_out_flush (struct ui_out *uiout)
{
  uo_flush (uiout);
}

int
ui_out_redirect (struct ui_out *uiout, struct ui_file *outstream)
{
  return uo_redirect (uiout, outstream);
}

/* set the flags specified by the mask given */
int
ui_out_set_flags (struct ui_out *uiout, int mask)
{
  int oldflags = uiout->flags;

  uiout->flags |= mask;

  return oldflags;
}

/* clear the flags specified by the mask given */
int
ui_out_clear_flags (struct ui_out *uiout, int mask)
{
  int oldflags = uiout->flags;

  uiout->flags &= ~mask;

  return oldflags;
}

/* test the flags against the mask given */
int
ui_out_test_flags (struct ui_out *uiout, int mask)
{
  return (uiout->flags & mask);
}

/* obtain the current verbosity level (as stablished by the
   'set verbositylevel' command */

int
ui_out_get_verblvl (struct ui_out *uiout)
{
  /* FIXME: not implemented yet */
  return 0;
}

#if 0
void
ui_out_result_begin (struct ui_out *uiout, char *class)
{
}

void
ui_out_result_end (struct ui_out *uiout)
{
}

void
ui_out_info_begin (struct ui_out *uiout, char *class)
{
}

void
ui_out_info_end (struct ui_out *uiout)
{
}

void
ui_out_notify_begin (struct ui_out *uiout, char *class)
{
}

void
ui_out_notify_end (struct ui_out *uiout)
{
}

void
ui_out_error_begin (struct ui_out *uiout, char *class)
{
}

void
ui_out_error_end (struct ui_out *uiout)
{
}
#endif

#if 0
void
gdb_error (ui_out * uiout, int severity, char *format,...)
{
  va_list args;
}

void
gdb_query (struct ui_out *uiout, int qflags, char *qprompt)
{
}
#endif

int
ui_out_is_mi_like_p (struct ui_out *uiout)
{
  return uiout->impl->is_mi_like_p;
}

/* default gdb-out hook functions */

static void
default_table_begin (struct ui_out *uiout, int nbrofcols,
		     int nr_rows,
		     const char *tblid)
{
}

static void
default_table_body (struct ui_out *uiout)
{
}

static void
default_table_end (struct ui_out *uiout)
{
}

static void
default_table_header (struct ui_out *uiout, int width, enum ui_align alignment,
		      const char *col_name,
		      const char *colhdr)
{
}

static void
default_begin (struct ui_out *uiout,
	       enum ui_out_type type,
	       int level,
	       const char *id)
{
}

static void
default_end (struct ui_out *uiout,
	     enum ui_out_type type,
	     int level)
{
}

static void
default_field_int (struct ui_out *uiout, int fldno, int width,
		   enum ui_align align,
		   const char *fldname, int value)
{
}

static void
default_field_skip (struct ui_out *uiout, int fldno, int width,
		    enum ui_align align, const char *fldname)
{
}

static void
default_field_string (struct ui_out *uiout,
		      int fldno,
		      int width,
		      enum ui_align align,
		      const char *fldname,
		      const char *string)
{
}

static void
default_field_fmt (struct ui_out *uiout, int fldno, int width,
		   enum ui_align align,
		   const char *fldname,
		   const char *format,
		   va_list args)
{
}

static void
default_spaces (struct ui_out *uiout, int numspaces)
{
}

static void
default_text (struct ui_out *uiout, const char *string)
{
}

static void
default_message (struct ui_out *uiout, int verbosity,
		 const char *format,
		 va_list args)
{
}

static void
default_wrap_hint (struct ui_out *uiout, char *identstring)
{
}

static void
default_flush (struct ui_out *uiout)
{
}

/* Interface to the implementation functions */

void
uo_table_begin (struct ui_out *uiout, int nbrofcols,
		int nr_rows,
		const char *tblid)
{
  if (!uiout->impl->table_begin)
    return;
  uiout->impl->table_begin (uiout, nbrofcols, nr_rows, tblid);
}

void
uo_table_body (struct ui_out *uiout)
{
  if (!uiout->impl->table_body)
    return;
  uiout->impl->table_body (uiout);
}

void
uo_table_end (struct ui_out *uiout)
{
  if (!uiout->impl->table_end)
    return;
  uiout->impl->table_end (uiout);
}

void
uo_table_header (struct ui_out *uiout, int width, enum ui_align align,
		 const char *col_name,
		 const char *colhdr)
{
  if (!uiout->impl->table_header)
    return;
  uiout->impl->table_header (uiout, width, align, col_name, colhdr);
}

void
uo_begin (struct ui_out *uiout,
	  enum ui_out_type type,
	  int level,
	  const char *id)
{
  if (uiout->impl->begin == NULL)
    return;
  uiout->impl->begin (uiout, type, level, id);
}

void
uo_end (struct ui_out *uiout,
	enum ui_out_type type,
	int level)
{
  if (uiout->impl->end == NULL)
    return;
  uiout->impl->end (uiout, type, level);
}

void
uo_field_int (struct ui_out *uiout, int fldno, int width, enum ui_align align,
	      const char *fldname,
	      int value)
{
  if (!uiout->impl->field_int)
    return;
  uiout->impl->field_int (uiout, fldno, width, align, fldname, value);
}

void
uo_field_skip (struct ui_out *uiout, int fldno, int width, enum ui_align align,
	       const char *fldname)
{
  if (!uiout->impl->field_skip)
    return;
  uiout->impl->field_skip (uiout, fldno, width, align, fldname);
}

void
uo_field_string (struct ui_out *uiout, int fldno, int width,
		 enum ui_align align,
		 const char *fldname,
		 const char *string)
{
  if (!uiout->impl->field_string)
    return;
  uiout->impl->field_string (uiout, fldno, width, align, fldname, string);
}

void
uo_field_fmt (struct ui_out *uiout, int fldno, int width, enum ui_align align,
	      const char *fldname,
	      const char *format,
	      va_list args)
{
  if (!uiout->impl->field_fmt)
    return;
  uiout->impl->field_fmt (uiout, fldno, width, align, fldname, format, args);
}

void
uo_spaces (struct ui_out *uiout, int numspaces)
{
  if (!uiout->impl->spaces)
    return;
  uiout->impl->spaces (uiout, numspaces);
}

void
uo_text (struct ui_out *uiout,
	 const char *string)
{
  if (!uiout->impl->text)
    return;
  uiout->impl->text (uiout, string);
}

void
uo_message (struct ui_out *uiout, int verbosity,
	    const char *format,
	    va_list args)
{
  if (!uiout->impl->message)
    return;
  uiout->impl->message (uiout, verbosity, format, args);
}

void
uo_wrap_hint (struct ui_out *uiout, char *identstring)
{
  if (!uiout->impl->wrap_hint)
    return;
  uiout->impl->wrap_hint (uiout, identstring);
}

void
uo_flush (struct ui_out *uiout)
{
  if (!uiout->impl->flush)
    return;
  uiout->impl->flush (uiout);
}

int
uo_redirect (struct ui_out *uiout, struct ui_file *outstream)
{
  if (!uiout->impl->redirect)
    return -1;
  uiout->impl->redirect (uiout, outstream);
  return 0;
}

/* local functions */

/* list of column headers manipulation routines */

static void
clear_header_list (struct ui_out *uiout)
{
  while (uiout->table.header_first != NULL)
    {
      uiout->table.header_next = uiout->table.header_first;
      uiout->table.header_first = uiout->table.header_first->next;
      if (uiout->table.header_next->colhdr != NULL)
	xfree (uiout->table.header_next->colhdr);
      xfree (uiout->table.header_next);
    }
  gdb_assert (uiout->table.header_first == NULL);
  uiout->table.header_last = NULL;
  uiout->table.header_next = NULL;
}

static void
append_header_to_list (struct ui_out *uiout,
		       int width,
		       int alignment,
		       const char *col_name,
		       const char *colhdr)
{
  struct ui_out_hdr *temphdr;

  temphdr = XMALLOC (struct ui_out_hdr);
  temphdr->width = width;
  temphdr->alignment = alignment;
  /* we have to copy the column title as the original may be an automatic */
  if (colhdr != NULL)
    temphdr->colhdr = xstrdup (colhdr);
  else
    temphdr->colhdr = NULL;
  if (col_name != NULL)
    temphdr->col_name = xstrdup (colhdr);
  else
    temphdr->col_name = xstrdup (colhdr);
  temphdr->next = NULL;
  if (uiout->table.header_first == NULL)
    {
      temphdr->colno = 1;
      uiout->table.header_first = temphdr;
      uiout->table.header_last = temphdr;
    }
  else
    {
      temphdr->colno = uiout->table.header_last->colno + 1;
      uiout->table.header_last->next = temphdr;
      uiout->table.header_last = temphdr;
    }
  uiout->table.header_next = uiout->table.header_last;
}

/* Extract the format information for the NEXT header and and advance
   the header pointer.  Return 0 if there was no next header.  */

static int
get_next_header (struct ui_out *uiout,
		 int *colno,
		 int *width,
		 int *alignment,
		 char **colhdr)
{
  /* There may be no headers at all or we may have used all columns.  */
  if (uiout->table.header_next == NULL)
    return 0;
  *colno = uiout->table.header_next->colno;
  *width = uiout->table.header_next->width;
  *alignment = uiout->table.header_next->alignment;
  *colhdr = uiout->table.header_next->colhdr;
  /* Advance the header pointer to the next entry.  */
  uiout->table.header_next = uiout->table.header_next->next;
  return 1;
}


/* Verify that the field/tuple/list is correctly positioned.  Return
   the field number and corresponding alignment (if
   available/applicable).  */

static void
verify_field (struct ui_out *uiout, int *fldno, int *width, int *align)
{
  struct ui_out_level *current = current_level (uiout);
  char *text;

  if (uiout->table.flag)
    {
      if (!uiout->table.body_flag)
	internal_error (__FILE__, __LINE__,
			"table_body missing; table fields must be \
specified after table_body and inside a list.");
      /* NOTE: cagney/2001-12-08: There was a check here to ensure
	 that this code was only executed when uiout->level was
	 greater than zero.  That no longer applies - this code is run
	 before each table row tuple is started and at that point the
	 level is zero.  */
    }

  current->field_count += 1;

  if (uiout->table.body_flag
      && uiout->table.entry_level == uiout->level
      && get_next_header (uiout, fldno, width, align, &text))
    {
      if (*fldno != current->field_count)
	internal_error (__FILE__, __LINE__,
			"ui-out internal error in handling headers.");
    }
  else
    {
      *width = 0;
      *align = ui_noalign;
      *fldno = current->field_count;
    }
}


/* access to ui_out format private members */

void
ui_out_get_field_separator (struct ui_out *uiout)
{
}

/* Access to ui-out members data */

struct ui_out_data *
ui_out_data (struct ui_out *uiout)
{
  return uiout->data;
}

/* initalize private members at startup */

struct ui_out *
ui_out_new (struct ui_out_impl *impl,
	    struct ui_out_data *data,
	    int flags)
{
  struct ui_out *uiout = XMALLOC (struct ui_out);
  uiout->data = data;
  uiout->impl = impl;
  uiout->flags = flags;
  uiout->table.flag = 0;
  uiout->table.body_flag = 0;
  uiout->level = 0;
  memset (uiout->levels, 0, sizeof (uiout->levels));
  uiout->table.header_first = NULL;
  uiout->table.header_last = NULL;
  uiout->table.header_next = NULL;
  return uiout;
}

/* standard gdb initialization hook */

void
_initialize_ui_out (void)
{
  /* nothing needs to be done */
}
