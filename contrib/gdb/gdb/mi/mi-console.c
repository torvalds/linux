/* MI Console code.

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
#include "mi-console.h"
#include "gdb_string.h"

/* MI-console: send output to std-out but correcty encapsulated */

static ui_file_fputs_ftype mi_console_file_fputs;
static ui_file_flush_ftype mi_console_file_flush;
static ui_file_delete_ftype mi_console_file_delete;

struct mi_console_file
  {
    int *magic;
    struct ui_file *raw;
    struct ui_file *buffer;
    const char *prefix;
    char quote;
  };

int mi_console_file_magic;

struct ui_file *
mi_console_file_new (struct ui_file *raw,
		     const char *prefix, char quote)
{
  struct ui_file *ui_file = ui_file_new ();
  struct mi_console_file *mi_console = XMALLOC (struct mi_console_file);
  mi_console->magic = &mi_console_file_magic;
  mi_console->raw = raw;
  mi_console->buffer = mem_fileopen ();
  mi_console->prefix = prefix;
  mi_console->quote = quote;
  set_ui_file_fputs (ui_file, mi_console_file_fputs);
  set_ui_file_flush (ui_file, mi_console_file_flush);
  set_ui_file_data (ui_file, mi_console, mi_console_file_delete);
  return ui_file;
}

static void
mi_console_file_delete (struct ui_file *file)
{
  struct mi_console_file *mi_console = ui_file_data (file);
  if (mi_console->magic != &mi_console_file_magic)
    internal_error (__FILE__, __LINE__,
		    "mi_console_file_delete: bad magic number");
  xfree (mi_console);
}

static void
mi_console_file_fputs (const char *buf,
		       struct ui_file *file)
{
  struct mi_console_file *mi_console = ui_file_data (file);
  if (mi_console->magic != &mi_console_file_magic)
    internal_error (__FILE__, __LINE__,
		    "mi_console_file_fputs: bad magic number");
  /* Append the text to our internal buffer */
  fputs_unfiltered (buf, mi_console->buffer);
  /* Flush when an embedded \n */
  if (strchr (buf, '\n') != NULL)
    gdb_flush (file);
}

/* Transform a byte sequence into a console output packet. */
static void
mi_console_raw_packet (void *data,
		       const char *buf,
		       long length_buf)
{
  struct mi_console_file *mi_console = data;
  if (mi_console->magic != &mi_console_file_magic)
    internal_error (__FILE__, __LINE__,
		    "mi_console_file_transform: bad magic number");

  if (length_buf > 0)
    {
      fputs_unfiltered (mi_console->prefix, mi_console->raw);
      if (mi_console->quote)
	{
	  fputs_unfiltered ("\"", mi_console->raw);
	  fputstrn_unfiltered (buf, length_buf, mi_console->quote, mi_console->raw);
	  fputs_unfiltered ("\"\n", mi_console->raw);
	}
      else
	{
	  fputstrn_unfiltered (buf, length_buf, 0, mi_console->raw);
	  fputs_unfiltered ("\n", mi_console->raw);
	}
      gdb_flush (mi_console->raw);
    }
}

static void
mi_console_file_flush (struct ui_file *file)
{
  struct mi_console_file *mi_console = ui_file_data (file);
  if (mi_console->magic != &mi_console_file_magic)
    internal_error (__FILE__, __LINE__,
		    "mi_console_file_flush: bad magic number");
  ui_file_put (mi_console->buffer, mi_console_raw_packet, mi_console);
  ui_file_rewind (mi_console->buffer);
}
