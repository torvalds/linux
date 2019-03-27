/* UI_FILE - a generic STDIO like output stream.
   Copyright 1999, 2000, 2001 Free Software Foundation, Inc.

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
#include "ui-file.h"
#include "tui/tui-file.h"
#include "tui/tui-io.h"

#include "tui.h"

#include "gdb_string.h"

/* A ``struct ui_file'' that is compatible with all the legacy
   code. */

/* new */
enum streamtype
{
  afile,
  astring
};

/* new */
struct tui_stream
{
  int *ts_magic;
  enum streamtype ts_streamtype;
  FILE *ts_filestream;
  char *ts_strbuf;
  int ts_buflen;
};

static ui_file_flush_ftype tui_file_flush;
extern ui_file_fputs_ftype tui_file_fputs;
static ui_file_isatty_ftype tui_file_isatty;
static ui_file_rewind_ftype tui_file_rewind;
static ui_file_put_ftype tui_file_put;
static ui_file_delete_ftype tui_file_delete;
static struct ui_file *tui_file_new (void);
static int tui_file_magic;

static struct ui_file *
tui_file_new (void)
{
  struct tui_stream *tui = xmalloc (sizeof (struct tui_stream));
  struct ui_file *file = ui_file_new ();
  set_ui_file_data (file, tui, tui_file_delete);
  set_ui_file_flush (file, tui_file_flush);
  set_ui_file_fputs (file, tui_file_fputs);
  set_ui_file_isatty (file, tui_file_isatty);
  set_ui_file_rewind (file, tui_file_rewind);
  set_ui_file_put (file, tui_file_put);
  tui->ts_magic = &tui_file_magic;
  return file;
}

static void
tui_file_delete (struct ui_file *file)
{
  struct tui_stream *tmpstream = ui_file_data (file);
  if (tmpstream->ts_magic != &tui_file_magic)
    internal_error (__FILE__, __LINE__,
		    "tui_file_delete: bad magic number");
  if ((tmpstream->ts_streamtype == astring) &&
      (tmpstream->ts_strbuf != NULL))
    {
      xfree (tmpstream->ts_strbuf);
    }
  xfree (tmpstream);
}

struct ui_file *
tui_fileopen (FILE *stream)
{
  struct ui_file *file = tui_file_new ();
  struct tui_stream *tmpstream = ui_file_data (file);
  tmpstream->ts_streamtype = afile;
  tmpstream->ts_filestream = stream;
  tmpstream->ts_strbuf = NULL;
  tmpstream->ts_buflen = 0;
  return file;
}

struct ui_file *
tui_sfileopen (int n)
{
  struct ui_file *file = tui_file_new ();
  struct tui_stream *tmpstream = ui_file_data (file);
  tmpstream->ts_streamtype = astring;
  tmpstream->ts_filestream = NULL;
  if (n > 0)
    {
      tmpstream->ts_strbuf = xmalloc ((n + 1) * sizeof (char));
      tmpstream->ts_strbuf[0] = '\0';
    }
  else
    /* Do not allocate the buffer now.  The first time something is printed
       one will be allocated by tui_file_adjust_strbuf()  */
    tmpstream->ts_strbuf = NULL;
  tmpstream->ts_buflen = n;
  return file;
}

static int
tui_file_isatty (struct ui_file *file)
{
  struct tui_stream *stream = ui_file_data (file);
  if (stream->ts_magic != &tui_file_magic)
    internal_error (__FILE__, __LINE__,
		    "tui_file_isatty: bad magic number");
  if (stream->ts_streamtype == afile)
    return (isatty (fileno (stream->ts_filestream)));
  else
    return 0;
}

static void
tui_file_rewind (struct ui_file *file)
{
  struct tui_stream *stream = ui_file_data (file);
  if (stream->ts_magic != &tui_file_magic)
    internal_error (__FILE__, __LINE__,
		    "tui_file_rewind: bad magic number");
  stream->ts_strbuf[0] = '\0';
}

static void
tui_file_put (struct ui_file *file,
	      ui_file_put_method_ftype *write,
	      void *dest)
{
  struct tui_stream *stream = ui_file_data (file);
  if (stream->ts_magic != &tui_file_magic)
    internal_error (__FILE__, __LINE__,
		    "tui_file_put: bad magic number");
  if (stream->ts_streamtype == astring)
    write (dest, stream->ts_strbuf, strlen (stream->ts_strbuf));
}

/* All TUI I/O sent to the *_filtered and *_unfiltered functions
   eventually ends up here.  The fputs_unfiltered_hook is primarily
   used by GUIs to collect all output and send it to the GUI, instead
   of the controlling terminal.  Only output to gdb_stdout and
   gdb_stderr are sent to the hook.  Everything else is sent on to
   fputs to allow file I/O to be handled appropriately.  */

/* FIXME: Should be broken up and moved to a TUI specific file. */

void
tui_file_fputs (const char *linebuffer, struct ui_file *file)
{
  struct tui_stream *stream = ui_file_data (file);

  if (stream->ts_streamtype == astring)
    {
      tui_file_adjust_strbuf (strlen (linebuffer), file);
      strcat (stream->ts_strbuf, linebuffer);
    }
  else
    {
      tui_puts (linebuffer);
    }
}

char *
tui_file_get_strbuf (struct ui_file *file)
{
  struct tui_stream *stream = ui_file_data (file);
  if (stream->ts_magic != &tui_file_magic)
    internal_error (__FILE__, __LINE__,
		    "tui_file_get_strbuf: bad magic number");
  return (stream->ts_strbuf);
}

/* adjust the length of the buffer by the amount necessary
   to accomodate appending a string of length N to the buffer contents */
void
tui_file_adjust_strbuf (int n, struct ui_file *file)
{
  struct tui_stream *stream = ui_file_data (file);
  int non_null_chars;
  if (stream->ts_magic != &tui_file_magic)
    internal_error (__FILE__, __LINE__,
		    "tui_file_adjust_strbuf: bad magic number");

  if (stream->ts_streamtype != astring)
    return;

  if (stream->ts_strbuf)
    {
      /* There is already a buffer allocated */
      non_null_chars = strlen (stream->ts_strbuf);

      if (n > (stream->ts_buflen - non_null_chars - 1))
	{
	  stream->ts_buflen = n + non_null_chars + 1;
	  stream->ts_strbuf = xrealloc (stream->ts_strbuf, stream->ts_buflen);
	}
    }
  else
    /* No buffer yet, so allocate one of the desired size */
    stream->ts_strbuf = xmalloc ((n + 1) * sizeof (char));
}

static void
tui_file_flush (struct ui_file *file)
{
  struct tui_stream *stream = ui_file_data (file);
  if (stream->ts_magic != &tui_file_magic)
    internal_error (__FILE__, __LINE__,
		    "tui_file_flush: bad magic number");

  switch (stream->ts_streamtype)
    {
    case astring:
      break;
    case afile:
      fflush (stream->ts_filestream);
      break;
    }
}
