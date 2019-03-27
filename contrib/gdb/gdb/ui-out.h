/* Output generating routines for GDB.
   Copyright 1999, 2000, 2001 Free Software Foundation, Inc.
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

#ifndef UI_OUT_H
#define UI_OUT_H 1

/* The ui_out structure */

struct ui_out;
struct ui_out_data;
struct ui_file;

/* the current ui_out */

/* FIXME: This should not be a global but something passed down from main.c
   or top.c */
extern struct ui_out *uiout;

/* alignment enum */
enum ui_align
  {
    ui_left = -1,
    ui_center,
    ui_right,
    ui_noalign
  };

/* flags enum */
enum ui_flags
  {
    ui_from_tty = 1,
    ui_source_list = 2
  };


/* The ui_out stream structure. */
/* NOTE: cagney/2000-02-01: The ui_stream object can be subsumed by
   the more generic ui_file object.  */

struct ui_stream
  {
    struct ui_out *uiout;
    struct ui_file *stream;
  };


/* Prototypes for ui-out API. */

/* A result is a recursive data structure consisting of lists and
   tuples. */

enum ui_out_type
  {
    ui_out_type_tuple,
    ui_out_type_list
  };

extern void ui_out_begin (struct ui_out *uiout,
			  enum ui_out_type level_type,
			  const char *id);

extern void ui_out_end (struct ui_out *uiout, enum ui_out_type type);

extern struct cleanup *ui_out_begin_cleanup_end (struct ui_out *uiout,
						 enum ui_out_type level_type,
						 const char *id);

/* A table can be considered a special tuple/list combination with the
   implied structure: ``table = { hdr = { header, ... } , body = [ {
   field, ... }, ... ] }''. If NR_ROWS is negative then there is at
   least one row. */
extern void ui_out_table_header (struct ui_out *uiout, int width,
				 enum ui_align align, const char *col_name,
				 const char *colhdr);

extern void ui_out_table_body (struct ui_out *uiout);

extern struct cleanup *make_cleanup_ui_out_table_begin_end (struct ui_out *ui_out,
                                                            int nr_cols,
                                                           int nr_rows,
                                                           const char *tblid);
/* Compatibility wrappers.  */

extern struct cleanup *make_cleanup_ui_out_list_begin_end (struct ui_out *uiout,
							   const char *id);

extern struct cleanup *make_cleanup_ui_out_tuple_begin_end (struct ui_out *uiout,
							    const char *id);

extern void ui_out_field_int (struct ui_out *uiout, const char *fldname,
			      int value);

extern void ui_out_field_fmt_int (struct ui_out *uiout, int width,
				  enum ui_align align, const char *fldname, 
		 		  int value);

extern void ui_out_field_core_addr (struct ui_out *uiout, const char *fldname,
				    CORE_ADDR address);

extern void ui_out_field_string (struct ui_out * uiout, const char *fldname,
				 const char *string);

extern void ui_out_field_stream (struct ui_out *uiout, const char *fldname,
				 struct ui_stream *buf);

extern void ui_out_field_fmt (struct ui_out *uiout, const char *fldname,
			      const char *format, ...);

extern void ui_out_field_skip (struct ui_out *uiout, const char *fldname);

extern void ui_out_spaces (struct ui_out *uiout, int numspaces);

extern void ui_out_text (struct ui_out *uiout, const char *string);

extern void ui_out_message (struct ui_out *uiout, int verbosity,
			    const char *format, ...);

extern struct ui_stream *ui_out_stream_new (struct ui_out *uiout);

extern void ui_out_stream_delete (struct ui_stream *buf);

struct cleanup *make_cleanup_ui_out_stream_delete (struct ui_stream *buf);

extern void ui_out_wrap_hint (struct ui_out *uiout, char *identstring);

extern void ui_out_flush (struct ui_out *uiout);

extern void ui_out_get_field_separator (struct ui_out *uiout);

extern int ui_out_set_flags (struct ui_out *uiout, int mask);

extern int ui_out_clear_flags (struct ui_out *uiout, int mask);

extern int ui_out_get_verblvl (struct ui_out *uiout);

extern int ui_out_test_flags (struct ui_out *uiout, int mask);

#if 0
extern void ui_out_result_begin (struct ui_out *uiout, char *class);

extern void ui_out_result_end (struct ui_out *uiout);

extern void ui_out_info_begin (struct ui_out *uiout, char *class);

extern void ui_out_info_end (struct ui_out *uiout);

extern void ui_out_notify_begin (struct ui_out *uiout, char *class);

extern void ui_out_notify_end (struct ui_out *uiout);

extern void ui_out_error_begin (struct ui_out *uiout, char *class);

extern void ui_out_error_end (struct ui_out *uiout);
#endif

#if 0
extern void gdb_error (struct ui_out *uiout, int severity, char *format, ...);

extern void gdb_query (struct ui_out *uiout, int qflags, char *qprompt);
#endif

/* HACK: Code in GDB is currently checking to see the type of ui_out
   builder when determining which output to produce.  This function is
   a hack to encapsulate that test.  Once GDB manages to separate the
   CLI/MI from the core of GDB the problem should just go away ....  */

extern int ui_out_is_mi_like_p (struct ui_out *uiout);

/* From here on we have things that are only needed by implementation
   routines and main.c.   We should pehaps have a separate file for that,
   like a  ui-out-impl.h  file */

/* User Interface Output Implementation Function Table */

/* Type definition of all implementation functions. */

typedef void (table_begin_ftype) (struct ui_out * uiout,
				  int nbrofcols, int nr_rows,
				  const char *tblid);
typedef void (table_body_ftype) (struct ui_out * uiout);
typedef void (table_end_ftype) (struct ui_out * uiout);
typedef void (table_header_ftype) (struct ui_out * uiout, int width,
				   enum ui_align align, const char *col_name,
				   const char *colhdr);
/* Note: level 0 is the top-level so LEVEL is always greater than
   zero. */
typedef void (ui_out_begin_ftype) (struct ui_out *uiout,
				   enum ui_out_type type,
				   int level, const char *id);
typedef void (ui_out_end_ftype) (struct ui_out *uiout,
				 enum ui_out_type type,
				 int level);
typedef void (field_int_ftype) (struct ui_out * uiout, int fldno, int width,
				enum ui_align align,
				const char *fldname, int value);
typedef void (field_skip_ftype) (struct ui_out * uiout, int fldno, int width,
				 enum ui_align align,
				 const char *fldname);
typedef void (field_string_ftype) (struct ui_out * uiout, int fldno, int width,
				   enum ui_align align,
				   const char *fldname,
				   const char *string);
typedef void (field_fmt_ftype) (struct ui_out * uiout, int fldno, int width,
				enum ui_align align,
				const char *fldname,
				const char *format,
				va_list args);
typedef void (spaces_ftype) (struct ui_out * uiout, int numspaces);
typedef void (text_ftype) (struct ui_out * uiout,
			   const char *string);
typedef void (message_ftype) (struct ui_out * uiout, int verbosity,
			      const char *format, va_list args);
typedef void (wrap_hint_ftype) (struct ui_out * uiout, char *identstring);
typedef void (flush_ftype) (struct ui_out * uiout);
typedef int (redirect_ftype) (struct ui_out * uiout,
			      struct ui_file * outstream);

/* ui-out-impl */

/* IMPORTANT: If you change this structure, make sure to change the default
   initialization in ui-out.c */

struct ui_out_impl
  {
    table_begin_ftype *table_begin;
    table_body_ftype *table_body;
    table_end_ftype *table_end;
    table_header_ftype *table_header;
    ui_out_begin_ftype *begin;
    ui_out_end_ftype *end;
    field_int_ftype *field_int;
    field_skip_ftype *field_skip;
    field_string_ftype *field_string;
    field_fmt_ftype *field_fmt;
    spaces_ftype *spaces;
    text_ftype *text;
    message_ftype *message;
    wrap_hint_ftype *wrap_hint;
    flush_ftype *flush;
    redirect_ftype *redirect;
    int is_mi_like_p;
  };

extern struct ui_out_data *ui_out_data (struct ui_out *uiout);


/* Create a ui_out object */

extern struct ui_out *ui_out_new (struct ui_out_impl *impl,
				  struct ui_out_data *data,
				  int flags);

/* Redirect the ouptut of a ui_out object temporarily.  */

extern int ui_out_redirect (struct ui_out *uiout, struct ui_file *outstream);

#endif /* UI_OUT_H */
