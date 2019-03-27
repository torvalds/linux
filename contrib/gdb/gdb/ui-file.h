/* UI_FILE - a generic STDIO like output stream.
   Copyright 1999, 2000 Free Software Foundation, Inc.

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

#ifndef UI_FILE_H
#define UI_FILE_H

struct ui_file;

/* Create a generic ui_file object with null methods. */

extern struct ui_file *ui_file_new (void);

/* Override methods used by specific implementations of a UI_FILE
   object. */

typedef void (ui_file_flush_ftype) (struct ui_file * stream);
extern void set_ui_file_flush (struct ui_file *stream, ui_file_flush_ftype * flush);

/* NOTE: Both fputs and write methods are available. Default
   implementations that mapping one onto the other are included. */
typedef void (ui_file_write_ftype) (struct ui_file * stream, const char *buf, long length_buf);
extern void set_ui_file_write (struct ui_file *stream, ui_file_write_ftype *fputs);

typedef void (ui_file_fputs_ftype) (const char *, struct ui_file * stream);
extern void set_ui_file_fputs (struct ui_file *stream, ui_file_fputs_ftype * fputs);

typedef long (ui_file_read_ftype) (struct ui_file * stream, char *buf, long length_buf);
extern void set_ui_file_read (struct ui_file *stream, ui_file_read_ftype *fread);

typedef int (ui_file_isatty_ftype) (struct ui_file * stream);
extern void set_ui_file_isatty (struct ui_file *stream, ui_file_isatty_ftype * isatty);

typedef void (ui_file_rewind_ftype) (struct ui_file * stream);
extern void set_ui_file_rewind (struct ui_file *stream, ui_file_rewind_ftype * rewind);

typedef void (ui_file_put_method_ftype) (void *object, const char *buffer, long length_buffer);
typedef void (ui_file_put_ftype) (struct ui_file *stream, ui_file_put_method_ftype * method, void *context);
extern void set_ui_file_put (struct ui_file *stream, ui_file_put_ftype * put);

typedef void (ui_file_delete_ftype) (struct ui_file * stream);
extern void set_ui_file_data (struct ui_file *stream, void *data, ui_file_delete_ftype * delete);

extern void *ui_file_data (struct ui_file *file);


extern void gdb_flush (struct ui_file *);

extern void ui_file_delete (struct ui_file *stream);

extern void ui_file_rewind (struct ui_file *stream);

extern int ui_file_isatty (struct ui_file *);

extern void ui_file_write (struct ui_file *file, const char *buf, long length_buf);

/* NOTE: copies left to right */
extern void ui_file_put (struct ui_file *src, ui_file_put_method_ftype *write, void *dest);

/* Returns a freshly allocated buffer containing the entire contents
   of FILE (as determined by ui_file_put()) with a NUL character
   appended.  LENGTH is set to the size of the buffer minus that
   appended NUL. */
extern char *ui_file_xstrdup (struct ui_file *file, long *length);



extern long ui_file_read (struct ui_file *file, char *buf, long length_buf);

/* Create/open a memory based file. Can be used as a scratch buffer
   for collecting output. */
extern struct ui_file *mem_fileopen (void);



/* Open/create a an STDIO based UI_FILE using the already open FILE. */
extern struct ui_file *stdio_fileopen (FILE *file);

/* Open NAME returning an STDIO based UI_FILE. */
extern struct ui_file *gdb_fopen (char *name, char *mode);

/* Create a file which writes to both ONE and TWO.  CLOSE_ONE
   and CLOSE_TWO indicate whether the original files should be
   closed when the new file is closed.  */
struct ui_file *tee_file_new (struct ui_file *one,
			      int close_one,
			      struct ui_file *two,
			      int close_two);
#endif
