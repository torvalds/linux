/* resrc.c -- read and write Windows rc files.
   Copyright 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2005, 2007
   Free Software Foundation, Inc.
   Written by Ian Lance Taylor, Cygnus Support.
   Rewritten by Kai Tietz, Onevision.

   This file is part of GNU Binutils.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* This file contains functions that read and write Windows rc files.
   These are text files that represent resources.  */

#include "sysdep.h"
#include "bfd.h"
#include "bucomm.h"
#include "libiberty.h"
#include "safe-ctype.h"
#include "windres.h"

#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#else /* ! HAVE_SYS_WAIT_H */
#if ! defined (_WIN32) || defined (__CYGWIN__)
#ifndef WIFEXITED
#define WIFEXITED(w)	(((w)&0377) == 0)
#endif
#ifndef WIFSIGNALED
#define WIFSIGNALED(w)	(((w)&0377) != 0177 && ((w)&~0377) == 0)
#endif
#ifndef WTERMSIG
#define WTERMSIG(w)	((w) & 0177)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(w)	(((w) >> 8) & 0377)
#endif
#else /* defined (_WIN32) && ! defined (__CYGWIN__) */
#ifndef WIFEXITED
#define WIFEXITED(w)	(((w) & 0xff) == 0)
#endif
#ifndef WIFSIGNALED
#define WIFSIGNALED(w)	(((w) & 0xff) != 0 && ((w) & 0xff) != 0x7f)
#endif
#ifndef WTERMSIG
#define WTERMSIG(w)	((w) & 0x7f)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(w)	(((w) & 0xff00) >> 8)
#endif
#endif /* defined (_WIN32) && ! defined (__CYGWIN__) */
#endif /* ! HAVE_SYS_WAIT_H */

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#if defined (_WIN32) && ! defined (__CYGWIN__)
#define popen _popen
#define pclose _pclose
#endif

/* The default preprocessor.  */

#define DEFAULT_PREPROCESSOR "gcc -E -xc -DRC_INVOKED"

/* We read the directory entries in a cursor or icon file into
   instances of this structure.  */

struct icondir
{
  /* Width of image.  */
  bfd_byte width;
  /* Height of image.  */
  bfd_byte height;
  /* Number of colors in image.  */
  bfd_byte colorcount;
  union
  {
    struct
    {
      /* Color planes.  */
      unsigned short planes;
      /* Bits per pixel.  */
      unsigned short bits;
    } icon;
    struct
    {
      /* X coordinate of hotspot.  */
      unsigned short xhotspot;
      /* Y coordinate of hotspot.  */
      unsigned short yhotspot;
    } cursor;
  } u;
  /* Bytes in image.  */
  unsigned long bytes;
  /* File offset of image.  */
  unsigned long offset;
};

/* The name of the rc file we are reading.  */

char *rc_filename;

/* The line number in the rc file.  */

int rc_lineno;

/* The pipe we are reading from, so that we can close it if we exit.  */

FILE *cpp_pipe;

/* The temporary file used if we're not using popen, so we can delete it
   if we exit.  */

static char *cpp_temp_file;

/* Input stream is either a file or a pipe.  */

static enum {ISTREAM_PIPE, ISTREAM_FILE} istream_type;

/* As we read the rc file, we attach information to this structure.  */

static rc_res_directory *resources;

/* The number of cursor resources we have written out.  */

static int cursors;

/* The number of font resources we have written out.  */

static int fonts;

/* Font directory information.  */

rc_fontdir *fontdirs;

/* Resource info to use for fontdirs.  */

rc_res_res_info fontdirs_resinfo;

/* The number of icon resources we have written out.  */

static int icons;

/* The windres target bfd .  */

static windres_bfd wrtarget =
{
  (bfd *) NULL, (asection *) NULL, WR_KIND_TARGET
};

/* Local functions for rcdata based resource definitions.  */

static void define_font_rcdata (rc_res_id, const rc_res_res_info *,
				rc_rcdata_item *);
static void define_icon_rcdata (rc_res_id, const rc_res_res_info *,
				rc_rcdata_item *);
static void define_bitmap_rcdata (rc_res_id, const rc_res_res_info *,
				  rc_rcdata_item *);
static void define_cursor_rcdata (rc_res_id, const rc_res_res_info *,
				  rc_rcdata_item *);
static void define_fontdir_rcdata (rc_res_id, const rc_res_res_info *,
				   rc_rcdata_item *);
static void define_messagetable_rcdata (rc_res_id, const rc_res_res_info *,
					rc_rcdata_item *);
static rc_uint_type rcdata_copy (const rc_rcdata_item *, bfd_byte *);
static bfd_byte *rcdata_render_as_buffer (const rc_rcdata_item *, rc_uint_type *);

static int run_cmd (char *, const char *);
static FILE *open_input_stream (char *);
static FILE *look_for_default
  (char *, const char *, int, const char *, const char *);
static void close_input_stream (void);
static void unexpected_eof (const char *);
static int get_word (FILE *, const char *);
static unsigned long get_long (FILE *, const char *);
static void get_data (FILE *, bfd_byte *, rc_uint_type, const char *);
static void define_fontdirs (void);

/* Run `cmd' and redirect the output to `redir'.  */

static int
run_cmd (char *cmd, const char *redir)
{
  char *s;
  int pid, wait_status, retcode;
  int i;
  const char **argv;
  char *errmsg_fmt, *errmsg_arg;
  char *temp_base = choose_temp_base ();
  int in_quote;
  char sep;
  int redir_handle = -1;
  int stdout_save = -1;

  /* Count the args.  */
  i = 0;

  for (s = cmd; *s; s++)
    if (*s == ' ')
      i++;

  i++;
  argv = alloca (sizeof (char *) * (i + 3));
  i = 0;
  s = cmd;

  while (1)
    {
      while (*s == ' ' && *s != 0)
	s++;

      if (*s == 0)
	break;

      in_quote = (*s == '\'' || *s == '"');
      sep = (in_quote) ? *s++ : ' ';
      argv[i++] = s;

      while (*s != sep && *s != 0)
	s++;

      if (*s == 0)
	break;

      *s++ = 0;

      if (in_quote)
	s++;
    }
  argv[i++] = NULL;

  /* Setup the redirection.  We can't use the usual fork/exec and redirect
     since we may be running on non-POSIX Windows host.  */

  fflush (stdout);
  fflush (stderr);

  /* Open temporary output file.  */
  redir_handle = open (redir, O_WRONLY | O_TRUNC | O_CREAT, 0666);
  if (redir_handle == -1)
    fatal (_("can't open temporary file `%s': %s"), redir,
	   strerror (errno));

  /* Duplicate the stdout file handle so it can be restored later.  */
  stdout_save = dup (STDOUT_FILENO);
  if (stdout_save == -1)
    fatal (_("can't redirect stdout: `%s': %s"), redir, strerror (errno));

  /* Redirect stdout to our output file.  */
  dup2 (redir_handle, STDOUT_FILENO);

  pid = pexecute (argv[0], (char * const *) argv, program_name, temp_base,
		  &errmsg_fmt, &errmsg_arg, PEXECUTE_ONE | PEXECUTE_SEARCH);

  /* Restore stdout to its previous setting.  */
  dup2 (stdout_save, STDOUT_FILENO);

  /* Close response file.  */
  close (redir_handle);

  if (pid == -1)
    {
      fatal (_("%s %s: %s"), errmsg_fmt, errmsg_arg, strerror (errno));
      return 1;
    }

  retcode = 0;
  pid = pwait (pid, &wait_status, 0);

  if (pid == -1)
    {
      fatal (_("wait: %s"), strerror (errno));
      retcode = 1;
    }
  else if (WIFSIGNALED (wait_status))
    {
      fatal (_("subprocess got fatal signal %d"), WTERMSIG (wait_status));
      retcode = 1;
    }
  else if (WIFEXITED (wait_status))
    {
      if (WEXITSTATUS (wait_status) != 0)
	{
	  fatal (_("%s exited with status %d"), cmd,
	         WEXITSTATUS (wait_status));
	  retcode = 1;
	}
    }
  else
    retcode = 1;

  return retcode;
}

static FILE *
open_input_stream (char *cmd)
{
  if (istream_type == ISTREAM_FILE)
    {
      char *fileprefix;

      fileprefix = choose_temp_base ();
      cpp_temp_file = (char *) xmalloc (strlen (fileprefix) + 5);
      sprintf (cpp_temp_file, "%s.irc", fileprefix);
      free (fileprefix);

      if (run_cmd (cmd, cpp_temp_file))
	fatal (_("can't execute `%s': %s"), cmd, strerror (errno));

      cpp_pipe = fopen (cpp_temp_file, FOPEN_RT);;
      if (cpp_pipe == NULL)
	fatal (_("can't open temporary file `%s': %s"),
	       cpp_temp_file, strerror (errno));

      if (verbose)
	fprintf (stderr,
	         _("Using temporary file `%s' to read preprocessor output\n"),
		 cpp_temp_file);
    }
  else
    {
      cpp_pipe = popen (cmd, FOPEN_RT);
      if (cpp_pipe == NULL)
	fatal (_("can't popen `%s': %s"), cmd, strerror (errno));
      if (verbose)
	fprintf (stderr, _("Using popen to read preprocessor output\n"));
    }

  xatexit (close_input_stream);
  return cpp_pipe;
}

/* Determine if FILENAME contains special characters that
   can cause problems unless the entire filename is quoted.  */

static int
filename_need_quotes (const char *filename)
{
  if (filename == NULL || (filename[0] == '-' && filename[1] == 0))
    return 0;

  while (*filename != 0)
    {
      switch (*filename)
        {
        case '&':
        case ' ':
        case '<':
        case '>':
        case '|':
        case '%':
          return 1;
        }
      ++filename;
    }
  return 0;
}

/* Look for the preprocessor program.  */

static FILE *
look_for_default (char *cmd, const char *prefix, int end_prefix,
		  const char *preprocargs, const char *filename)
{
  char *space;
  int found;
  struct stat s;
  const char *fnquotes = (filename_need_quotes (filename) ? "\"" : "");

  strcpy (cmd, prefix);

  sprintf (cmd + end_prefix, "%s", DEFAULT_PREPROCESSOR);
  space = strchr (cmd + end_prefix, ' ');
  if (space)
    *space = 0;

  if (
#if defined (__DJGPP__) || defined (__CYGWIN__) || defined (_WIN32)
      strchr (cmd, '\\') ||
#endif
      strchr (cmd, '/'))
    {
      found = (stat (cmd, &s) == 0
#ifdef HAVE_EXECUTABLE_SUFFIX
	       || stat (strcat (cmd, EXECUTABLE_SUFFIX), &s) == 0
#endif
	       );

      if (! found)
	{
	  if (verbose)
	    fprintf (stderr, _("Tried `%s'\n"), cmd);
	  return NULL;
	}
    }

  strcpy (cmd, prefix);

  sprintf (cmd + end_prefix, "%s %s %s%s%s",
	   DEFAULT_PREPROCESSOR, preprocargs, fnquotes, filename, fnquotes);

  if (verbose)
    fprintf (stderr, _("Using `%s'\n"), cmd);

  cpp_pipe = open_input_stream (cmd);
  return cpp_pipe;
}

/* Read an rc file.  */

rc_res_directory *
read_rc_file (const char *filename, const char *preprocessor,
	      const char *preprocargs, int language, int use_temp_file)
{
  char *cmd;
  const char *fnquotes = (filename_need_quotes (filename) ? "\"" : "");

  istream_type = (use_temp_file) ? ISTREAM_FILE : ISTREAM_PIPE;

  if (preprocargs == NULL)
    preprocargs = "";
  if (filename == NULL)
    filename = "-";

  if (preprocessor)
    {
      cmd = xmalloc (strlen (preprocessor)
		     + strlen (preprocargs)
		     + strlen (filename)
		     + strlen (fnquotes) * 2
		     + 10);
      sprintf (cmd, "%s %s %s%s%s", preprocessor, preprocargs,
	       fnquotes, filename, fnquotes);

      cpp_pipe = open_input_stream (cmd);
    }
  else
    {
      char *dash, *slash, *cp;

      preprocessor = DEFAULT_PREPROCESSOR;

      cmd = xmalloc (strlen (program_name)
		     + strlen (preprocessor)
		     + strlen (preprocargs)
		     + strlen (filename)
		     + strlen (fnquotes) * 2
#ifdef HAVE_EXECUTABLE_SUFFIX
		     + strlen (EXECUTABLE_SUFFIX)
#endif
		     + 10);


      dash = slash = 0;
      for (cp = program_name; *cp; cp++)
	{
	  if (*cp == '-')
	    dash = cp;
	  if (
#if defined (__DJGPP__) || defined (__CYGWIN__) || defined(_WIN32)
	      *cp == ':' || *cp == '\\' ||
#endif
	      *cp == '/')
	    {
	      slash = cp;
	      dash = 0;
	    }
	}

      cpp_pipe = 0;

      if (dash)
	{
	  /* First, try looking for a prefixed gcc in the windres
	     directory, with the same prefix as windres */

	  cpp_pipe = look_for_default (cmd, program_name, dash - program_name + 1,
				       preprocargs, filename);
	}

      if (slash && ! cpp_pipe)
	{
	  /* Next, try looking for a gcc in the same directory as
             that windres */

	  cpp_pipe = look_for_default (cmd, program_name, slash - program_name + 1,
				       preprocargs, filename);
	}

      if (! cpp_pipe)
	{
	  /* Sigh, try the default */

	  cpp_pipe = look_for_default (cmd, "", 0, preprocargs, filename);
	}

    }

  free (cmd);

  rc_filename = xstrdup (filename);
  rc_lineno = 1;
  if (language != -1)
    rcparse_set_language (language);
  yyparse ();
  rcparse_discard_strings ();

  close_input_stream ();

  if (fontdirs != NULL)
    define_fontdirs ();

  free (rc_filename);
  rc_filename = NULL;

  return resources;
}

/* Close the input stream if it is open.  */

static void
close_input_stream (void)
{
  if (istream_type == ISTREAM_FILE)
    {
      if (cpp_pipe != NULL)
	fclose (cpp_pipe);

      if (cpp_temp_file != NULL)
	{
	  int errno_save = errno;

	  unlink (cpp_temp_file);
	  errno = errno_save;
	  free (cpp_temp_file);
	}
    }
  else
    {
      if (cpp_pipe != NULL)
	pclose (cpp_pipe);
    }

  /* Since this is also run via xatexit, safeguard.  */
  cpp_pipe = NULL;
  cpp_temp_file = NULL;
}

/* Report an error while reading an rc file.  */

void
yyerror (const char *msg)
{
  fatal ("%s:%d: %s", rc_filename, rc_lineno, msg);
}

/* Issue a warning while reading an rc file.  */

void
rcparse_warning (const char *msg)
{
  fprintf (stderr, _("%s:%d: %s\n"), rc_filename, rc_lineno, msg);
}

/* Die if we get an unexpected end of file.  */

static void
unexpected_eof (const char *msg)
{
  fatal (_("%s: unexpected EOF"), msg);
}

/* Read a 16 bit word from a file.  The data is assumed to be little
   endian.  */

static int
get_word (FILE *e, const char *msg)
{
  int b1, b2;

  b1 = getc (e);
  b2 = getc (e);
  if (feof (e))
    unexpected_eof (msg);
  return ((b2 & 0xff) << 8) | (b1 & 0xff);
}

/* Read a 32 bit word from a file.  The data is assumed to be little
   endian.  */

static unsigned long
get_long (FILE *e, const char *msg)
{
  int b1, b2, b3, b4;

  b1 = getc (e);
  b2 = getc (e);
  b3 = getc (e);
  b4 = getc (e);
  if (feof (e))
    unexpected_eof (msg);
  return (((((((b4 & 0xff) << 8)
	      | (b3 & 0xff)) << 8)
	    | (b2 & 0xff)) << 8)
	  | (b1 & 0xff));
}

/* Read data from a file.  This is a wrapper to do error checking.  */

static void
get_data (FILE *e, bfd_byte *p, rc_uint_type c, const char *msg)
{
  rc_uint_type got; // $$$d

  got = (rc_uint_type) fread (p, 1, c, e);
  if (got == c)
    return;

  fatal (_("%s: read of %lu returned %lu"), msg, (long) c, (long) got);
}

/* Define an accelerator resource.  */

void
define_accelerator (rc_res_id id, const rc_res_res_info *resinfo,
		    rc_accelerator *data)
{
  rc_res_resource *r;

  r = define_standard_resource (&resources, RT_ACCELERATOR, id,
				resinfo->language, 0);
  r->type = RES_TYPE_ACCELERATOR;
  r->u.acc = data;
  r->res_info = *resinfo;
}

/* Define a bitmap resource.  Bitmap data is stored in a file.  The
   first 14 bytes of the file are a standard header, which is not
   included in the resource data.  */

#define BITMAP_SKIP (14)

void
define_bitmap (rc_res_id id, const rc_res_res_info *resinfo,
	       const char *filename)
{
  FILE *e;
  char *real_filename;
  struct stat s;
  bfd_byte *data;
  rc_uint_type i;
  rc_res_resource *r;

  e = open_file_search (filename, FOPEN_RB, "bitmap file", &real_filename);

  if (stat (real_filename, &s) < 0)
    fatal (_("stat failed on bitmap file `%s': %s"), real_filename,
	   strerror (errno));

  data = (bfd_byte *) res_alloc (s.st_size - BITMAP_SKIP);

  for (i = 0; i < BITMAP_SKIP; i++)
    getc (e);

  get_data (e, data, s.st_size - BITMAP_SKIP, real_filename);

  fclose (e);
  free (real_filename);

  r = define_standard_resource (&resources, RT_BITMAP, id,
				resinfo->language, 0);

  r->type = RES_TYPE_BITMAP;
  r->u.data.length = s.st_size - BITMAP_SKIP;
  r->u.data.data = data;
  r->res_info = *resinfo;
}

/* Define a cursor resource.  A cursor file may contain a set of
   bitmaps, each representing the same cursor at various different
   resolutions.  They each get written out with a different ID.  The
   real cursor resource is then a group resource which can be used to
   select one of the actual cursors.  */

void
define_cursor (rc_res_id id, const rc_res_res_info *resinfo,
	       const char *filename)
{
  FILE *e;
  char *real_filename;
  int type, count, i;
  struct icondir *icondirs;
  int first_cursor;
  rc_res_resource *r;
  rc_group_cursor *first, **pp;

  e = open_file_search (filename, FOPEN_RB, "cursor file", &real_filename);

  /* A cursor file is basically an icon file.  The start of the file
     is a three word structure.  The first word is ignored.  The
     second word is the type of data.  The third word is the number of
     entries.  */

  get_word (e, real_filename);
  type = get_word (e, real_filename);
  count = get_word (e, real_filename);
  if (type != 2)
    fatal (_("cursor file `%s' does not contain cursor data"), real_filename);

  /* Read in the icon directory entries.  */

  icondirs = (struct icondir *) xmalloc (count * sizeof *icondirs);

  for (i = 0; i < count; i++)
    {
      icondirs[i].width = getc (e);
      icondirs[i].height = getc (e);
      icondirs[i].colorcount = getc (e);
      getc (e);
      icondirs[i].u.cursor.xhotspot = get_word (e, real_filename);
      icondirs[i].u.cursor.yhotspot = get_word (e, real_filename);
      icondirs[i].bytes = get_long (e, real_filename);
      icondirs[i].offset = get_long (e, real_filename);

      if (feof (e))
	unexpected_eof (real_filename);
    }

  /* Define each cursor as a unique resource.  */

  first_cursor = cursors;

  for (i = 0; i < count; i++)
    {
      bfd_byte *data;
      rc_res_id name;
      rc_cursor *c;

      if (fseek (e, icondirs[i].offset, SEEK_SET) != 0)
	fatal (_("%s: fseek to %lu failed: %s"), real_filename,
	       icondirs[i].offset, strerror (errno));

      data = (bfd_byte *) res_alloc (icondirs[i].bytes);

      get_data (e, data, icondirs[i].bytes, real_filename);

      c = (rc_cursor *) res_alloc (sizeof (rc_cursor));
      c->xhotspot = icondirs[i].u.cursor.xhotspot;
      c->yhotspot = icondirs[i].u.cursor.yhotspot;
      c->length = icondirs[i].bytes;
      c->data = data;

      ++cursors;

      name.named = 0;
      name.u.id = cursors;

      r = define_standard_resource (&resources, RT_CURSOR, name,
				    resinfo->language, 0);
      r->type = RES_TYPE_CURSOR;
      r->u.cursor = c;
      r->res_info = *resinfo;
    }

  fclose (e);
  free (real_filename);

  /* Define a cursor group resource.  */

  first = NULL;
  pp = &first;
  for (i = 0; i < count; i++)
    {
      rc_group_cursor *cg;

      cg = (rc_group_cursor *) res_alloc (sizeof (rc_group_cursor));
      cg->next = NULL;
      cg->width = icondirs[i].width;
      cg->height = 2 * icondirs[i].height;

      /* FIXME: What should these be set to?  */
      cg->planes = 1;
      cg->bits = 1;

      cg->bytes = icondirs[i].bytes + 4;
      cg->index = first_cursor + i + 1;

      *pp = cg;
      pp = &(*pp)->next;
    }

  free (icondirs);

  r = define_standard_resource (&resources, RT_GROUP_CURSOR, id,
				resinfo->language, 0);
  r->type = RES_TYPE_GROUP_CURSOR;
  r->u.group_cursor = first;
  r->res_info = *resinfo;
}

/* Define a dialog resource.  */

void
define_dialog (rc_res_id id, const rc_res_res_info *resinfo,
	       const rc_dialog *dialog)
{
  rc_dialog *copy;
  rc_res_resource *r;

  copy = (rc_dialog *) res_alloc (sizeof *copy);
  *copy = *dialog;

  r = define_standard_resource (&resources, RT_DIALOG, id,
				resinfo->language, 0);
  r->type = RES_TYPE_DIALOG;
  r->u.dialog = copy;
  r->res_info = *resinfo;
}

/* Define a dialog control.  This does not define a resource, but
   merely allocates and fills in a structure.  */

rc_dialog_control *
define_control (const rc_res_id iid, rc_uint_type id, rc_uint_type x,
		rc_uint_type y, rc_uint_type width, rc_uint_type height,
		const rc_res_id class, rc_uint_type style,
		rc_uint_type exstyle)
{
  rc_dialog_control *n;

  n = (rc_dialog_control *) res_alloc (sizeof (rc_dialog_control));
  n->next = NULL;
  n->id = id;
  n->style = style;
  n->exstyle = exstyle;
  n->x = x;
  n->y = y;
  n->width = width;
  n->height = height;
  n->class = class;
  n->text = iid;
  n->data = NULL;
  n->help = 0;

  return n;
}

rc_dialog_control *
define_icon_control (rc_res_id iid, rc_uint_type id, rc_uint_type x,
		     rc_uint_type y, rc_uint_type style,
		     rc_uint_type exstyle, rc_uint_type help,
		     rc_rcdata_item *data, rc_dialog_ex *ex)
{
  rc_dialog_control *n;
  rc_res_id tid;
  rc_res_id cid;

  if (style == 0)
    style = SS_ICON | WS_CHILD | WS_VISIBLE;
  res_string_to_id (&tid, "");
  cid.named = 0;
  cid.u.id = CTL_STATIC;
  n = define_control (tid, id, x, y, 0, 0, cid, style, exstyle);
  n->text = iid;
  if (help && ! ex)
    rcparse_warning (_("help ID requires DIALOGEX"));
  if (data && ! ex)
    rcparse_warning (_("control data requires DIALOGEX"));
  n->help = help;
  n->data = data;

  return n;
}

/* Define a font resource.  */

void
define_font (rc_res_id id, const rc_res_res_info *resinfo,
	     const char *filename)
{
  FILE *e;
  char *real_filename;
  struct stat s;
  bfd_byte *data;
  rc_res_resource *r;
  long offset;
  long fontdatalength;
  bfd_byte *fontdata;
  rc_fontdir *fd;
  const char *device, *face;
  rc_fontdir **pp;

  e = open_file_search (filename, FOPEN_RB, "font file", &real_filename);

  if (stat (real_filename, &s) < 0)
    fatal (_("stat failed on font file `%s': %s"), real_filename,
	   strerror (errno));

  data = (bfd_byte *) res_alloc (s.st_size);

  get_data (e, data, s.st_size, real_filename);

  fclose (e);
  free (real_filename);

  r = define_standard_resource (&resources, RT_FONT, id,
				resinfo->language, 0);

  r->type = RES_TYPE_FONT;
  r->u.data.length = s.st_size;
  r->u.data.data = data;
  r->res_info = *resinfo;

  /* For each font resource, we must add an entry in the FONTDIR
     resource.  The FONTDIR resource includes some strings in the font
     file.  To find them, we have to do some magic on the data we have
     read.  */

  offset = ((((((data[47] << 8)
		| data[46]) << 8)
	      | data[45]) << 8)
	    | data[44]);
  if (offset > 0 && offset < s.st_size)
    device = (char *) data + offset;
  else
    device = "";

  offset = ((((((data[51] << 8)
		| data[50]) << 8)
	      | data[49]) << 8)
	    | data[48]);
  if (offset > 0 && offset < s.st_size)
    face = (char *) data + offset;
  else
    face = "";

  ++fonts;

  fontdatalength = 58 + strlen (device) + strlen (face);
  fontdata = (bfd_byte *) res_alloc (fontdatalength);
  memcpy (fontdata, data, 56);
  strcpy ((char *) fontdata + 56, device);
  strcpy ((char *) fontdata + 57 + strlen (device), face);

  fd = (rc_fontdir *) res_alloc (sizeof (rc_fontdir));
  fd->next = NULL;
  fd->index = fonts;
  fd->length = fontdatalength;
  fd->data = fontdata;

  for (pp = &fontdirs; *pp != NULL; pp = &(*pp)->next)
    ;
  *pp = fd;

  /* For the single fontdirs resource, we always use the resource
     information of the last font.  I don't know what else to do.  */
  fontdirs_resinfo = *resinfo;
}

static void
define_font_rcdata (rc_res_id id,const rc_res_res_info *resinfo,
		    rc_rcdata_item *data)
{
  rc_res_resource *r;
  rc_uint_type len_data;
  bfd_byte *pb_data;

  r = define_standard_resource (&resources, RT_FONT, id,
				resinfo->language, 0);

  pb_data = rcdata_render_as_buffer (data, &len_data);

  r->type = RES_TYPE_FONT;
  r->u.data.length = len_data;
  r->u.data.data = pb_data;
  r->res_info = *resinfo;
}

/* Define the fontdirs resource.  This is called after the entire rc
   file has been parsed, if any font resources were seen.  */

static void
define_fontdirs (void)
{
  rc_res_resource *r;
  rc_res_id id;

  id.named = 0;
  id.u.id = 1;

  r = define_standard_resource (&resources, RT_FONTDIR, id, 0x409, 0);

  r->type = RES_TYPE_FONTDIR;
  r->u.fontdir = fontdirs;
  r->res_info = fontdirs_resinfo;
}

static bfd_byte *
rcdata_render_as_buffer (const rc_rcdata_item *data, rc_uint_type *plen)
{
  const rc_rcdata_item *d;
  bfd_byte *ret = NULL, *pret;
  rc_uint_type len = 0;

  for (d = data; d != NULL; d = d->next)
    len += rcdata_copy (d, NULL);
  if (len != 0)
    {
      ret = pret = (bfd_byte *) res_alloc (len);
      for (d = data; d != NULL; d = d->next)
	pret += rcdata_copy (d, pret);
    }
  if (plen)
    *plen = len;
  return ret;
}

static void
define_fontdir_rcdata (rc_res_id id,const rc_res_res_info *resinfo,
		       rc_rcdata_item *data)
{
  rc_res_resource *r;
  rc_fontdir *fd, *fd_first, *fd_cur;
  rc_uint_type len_data;
  bfd_byte *pb_data;
  rc_uint_type c;

  fd_cur = fd_first = NULL;
  r = define_standard_resource (&resources, RT_FONTDIR, id, 0x409, 0);

  pb_data = rcdata_render_as_buffer (data, &len_data);

  if (pb_data)
    {
      rc_uint_type off = 2;
      c = windres_get_16 (&wrtarget, pb_data, len_data);
      for (; c > 0; c--)
	{
	  size_t len;
	  rc_uint_type safe_pos = off;
	  const struct bin_fontdir_item *bfi;

	  bfi = (const struct bin_fontdir_item *) pb_data + off;
	  fd = (rc_fontdir *) res_alloc (sizeof (rc_fontdir));
	  fd->index = windres_get_16 (&wrtarget, bfi->index, len_data - off);
	  fd->data = pb_data + off;
	  off += 56;
	  len = strlen ((char *) bfi->device_name) + 1;
	  off += (rc_uint_type) len;
	  off += (rc_uint_type) strlen ((char *) bfi->device_name + len) + 1;
	  fd->length = (off - safe_pos);
	  fd->next = NULL;
	  if (fd_first == NULL)
	    fd_first = fd;
	  else
	    fd_cur->next = fd;
	  fd_cur = fd;
	}
    }
  r->type = RES_TYPE_FONTDIR;
  r->u.fontdir = fd_first;
  r->res_info = *resinfo;
}

static void define_messagetable_rcdata (rc_res_id id, const rc_res_res_info *resinfo,
					rc_rcdata_item *data)
{
  rc_res_resource *r;
  rc_uint_type len_data;
  bfd_byte *pb_data;

  r = define_standard_resource (&resources, RT_MESSAGETABLE, id, resinfo->language, 0);

  pb_data = rcdata_render_as_buffer (data, &len_data);
  r->type = RES_TYPE_MESSAGETABLE;
  r->u.data.length = len_data;
  r->u.data.data = pb_data;
  r->res_info = *resinfo;
}

/* Define an icon resource.  An icon file may contain a set of
   bitmaps, each representing the same icon at various different
   resolutions.  They each get written out with a different ID.  The
   real icon resource is then a group resource which can be used to
   select one of the actual icon bitmaps.  */

void
define_icon (rc_res_id id, const rc_res_res_info *resinfo,
	     const char *filename)
{
  FILE *e;
  char *real_filename;
  int type, count, i;
  struct icondir *icondirs;
  int first_icon;
  rc_res_resource *r;
  rc_group_icon *first, **pp;

  e = open_file_search (filename, FOPEN_RB, "icon file", &real_filename);

  /* The start of an icon file is a three word structure.  The first
     word is ignored.  The second word is the type of data.  The third
     word is the number of entries.  */

  get_word (e, real_filename);
  type = get_word (e, real_filename);
  count = get_word (e, real_filename);
  if (type != 1)
    fatal (_("icon file `%s' does not contain icon data"), real_filename);

  /* Read in the icon directory entries.  */

  icondirs = (struct icondir *) xmalloc (count * sizeof *icondirs);

  for (i = 0; i < count; i++)
    {
      icondirs[i].width = getc (e);
      icondirs[i].height = getc (e);
      icondirs[i].colorcount = getc (e);
      getc (e);
      icondirs[i].u.icon.planes = get_word (e, real_filename);
      icondirs[i].u.icon.bits = get_word (e, real_filename);
      icondirs[i].bytes = get_long (e, real_filename);
      icondirs[i].offset = get_long (e, real_filename);

      if (feof (e))
	unexpected_eof (real_filename);
    }

  /* Define each icon as a unique resource.  */

  first_icon = icons;

  for (i = 0; i < count; i++)
    {
      bfd_byte *data;
      rc_res_id name;

      if (fseek (e, icondirs[i].offset, SEEK_SET) != 0)
	fatal (_("%s: fseek to %lu failed: %s"), real_filename,
	       icondirs[i].offset, strerror (errno));

      data = (bfd_byte *) res_alloc (icondirs[i].bytes);

      get_data (e, data, icondirs[i].bytes, real_filename);

      ++icons;

      name.named = 0;
      name.u.id = icons;

      r = define_standard_resource (&resources, RT_ICON, name,
				    resinfo->language, 0);
      r->type = RES_TYPE_ICON;
      r->u.data.length = icondirs[i].bytes;
      r->u.data.data = data;
      r->res_info = *resinfo;
    }

  fclose (e);
  free (real_filename);

  /* Define an icon group resource.  */

  first = NULL;
  pp = &first;
  for (i = 0; i < count; i++)
    {
      rc_group_icon *cg;

      /* For some reason, at least in some files the planes and bits
         are zero.  We instead set them from the color.  This is
         copied from rcl.  */

      cg = (rc_group_icon *) res_alloc (sizeof (rc_group_icon));
      cg->next = NULL;
      cg->width = icondirs[i].width;
      cg->height = icondirs[i].height;
      cg->colors = icondirs[i].colorcount;

      if (icondirs[i].u.icon.planes)
	cg->planes = icondirs[i].u.icon.planes;
      else
	cg->planes = 1;

      if (icondirs[i].u.icon.bits)
	cg->bits = icondirs[i].u.icon.bits;
      else
	{
	  cg->bits = 0;

	  while ((1L << cg->bits) < cg->colors)
	    ++cg->bits;
	}

      cg->bytes = icondirs[i].bytes;
      cg->index = first_icon + i + 1;

      *pp = cg;
      pp = &(*pp)->next;
    }

  free (icondirs);

  r = define_standard_resource (&resources, RT_GROUP_ICON, id,
				resinfo->language, 0);
  r->type = RES_TYPE_GROUP_ICON;
  r->u.group_icon = first;
  r->res_info = *resinfo;
}

static void
define_group_icon_rcdata (rc_res_id id, const rc_res_res_info *resinfo,
			  rc_rcdata_item *data)
{
  rc_res_resource *r;
  rc_group_icon *cg, *first, *cur;
  rc_uint_type len_data;
  bfd_byte *pb_data;

  pb_data = rcdata_render_as_buffer (data, &len_data);

  cur = NULL;
  first = NULL;

  while (len_data >= 6)
    {
      int c, i;
      unsigned short type;
      type = windres_get_16 (&wrtarget, pb_data + 2, len_data - 2);
      if (type != 1)
	fatal (_("unexpected group icon type %d"), type);
      c = windres_get_16 (&wrtarget, pb_data + 4, len_data - 4);
      len_data -= 6;
      pb_data += 6;

      for (i = 0; i < c; i++)
	{
	  if (len_data < 14)
	    fatal ("too small group icon rcdata");
	  cg = (rc_group_icon *) res_alloc (sizeof (rc_group_icon));
	  cg->next = NULL;
	  cg->width = pb_data[0];
	  cg->height = pb_data[1];
	  cg->colors = pb_data[2];
	  cg->planes = windres_get_16 (&wrtarget, pb_data + 4, len_data - 4);
	  cg->bits =  windres_get_16 (&wrtarget, pb_data + 6, len_data - 6);
	  cg->bytes = windres_get_32 (&wrtarget, pb_data + 8, len_data - 8);
	  cg->index = windres_get_16 (&wrtarget, pb_data + 12, len_data - 12);
	  if (! first)
	    first = cg;
	  else
	    cur->next = cg;
	  cur = cg;
	  pb_data += 14;
	  len_data -= 14;
	}
    }
  r = define_standard_resource (&resources, RT_GROUP_ICON, id,
				resinfo->language, 0);
  r->type = RES_TYPE_GROUP_ICON;
  r->u.group_icon = first;
  r->res_info = *resinfo;
}

static void
define_group_cursor_rcdata (rc_res_id id, const rc_res_res_info *resinfo,
			    rc_rcdata_item *data)
{
  rc_res_resource *r;
  rc_group_cursor *cg, *first, *cur;
  rc_uint_type len_data;
  bfd_byte *pb_data;

  pb_data = rcdata_render_as_buffer (data, &len_data);

  first = cur = NULL;

  while (len_data >= 6)
    {
      int c, i;
      unsigned short type;
      type = windres_get_16 (&wrtarget, pb_data + 2, len_data - 2);
      if (type != 2)
	fatal (_("unexpected group cursor type %d"), type);
      c = windres_get_16 (&wrtarget, pb_data + 4, len_data - 4);
      len_data -= 6;
      pb_data += 6;

      for (i = 0; i < c; i++)
	{
	  if (len_data < 14)
	    fatal ("too small group icon rcdata");
	  cg = (rc_group_cursor *) res_alloc (sizeof (rc_group_cursor));
	  cg->next = NULL;
	  cg->width = windres_get_16 (&wrtarget, pb_data, len_data);
	  cg->height = windres_get_16 (&wrtarget, pb_data + 2, len_data - 2);
	  cg->planes = windres_get_16 (&wrtarget, pb_data + 4, len_data - 4);
	  cg->bits =  windres_get_16 (&wrtarget, pb_data + 6, len_data - 6);
	  cg->bytes = windres_get_32 (&wrtarget, pb_data + 8, len_data - 8);
	  cg->index = windres_get_16 (&wrtarget, pb_data + 12, len_data - 12);
	  if (! first)
	    first = cg;
	  else
	    cur->next = cg;
	  cur = cg;
	  pb_data += 14;
	  len_data -= 14;
	}
    }

  r = define_standard_resource (&resources, RT_GROUP_ICON, id,
				resinfo->language, 0);
  r->type = RES_TYPE_GROUP_CURSOR;
  r->u.group_cursor = first;
  r->res_info = *resinfo;
}

static void
define_cursor_rcdata (rc_res_id id, const rc_res_res_info *resinfo,
		      rc_rcdata_item *data)
{
  rc_cursor *c;
  rc_res_resource *r;
  rc_uint_type len_data;
  bfd_byte *pb_data;

  pb_data = rcdata_render_as_buffer (data, &len_data);

  c = (rc_cursor *) res_alloc (sizeof (rc_cursor));
  c->xhotspot = windres_get_16 (&wrtarget, pb_data, len_data);
  c->yhotspot = windres_get_16 (&wrtarget, pb_data + 2, len_data - 2);
  c->length = len_data - BIN_CURSOR_SIZE;
  c->data = (const bfd_byte *) (data + BIN_CURSOR_SIZE);

  r = define_standard_resource (&resources, RT_CURSOR, id, resinfo->language, 0);
  r->type = RES_TYPE_CURSOR;
  r->u.cursor = c;
  r->res_info = *resinfo;
}

static void
define_bitmap_rcdata (rc_res_id id, const rc_res_res_info *resinfo,
		      rc_rcdata_item *data)
{
  rc_res_resource *r;
  rc_uint_type len_data;
  bfd_byte *pb_data;

  pb_data = rcdata_render_as_buffer (data, &len_data);

  r = define_standard_resource (&resources, RT_BITMAP, id, resinfo->language, 0);
  r->type = RES_TYPE_BITMAP;
  r->u.data.length = len_data;
  r->u.data.data = pb_data;
  r->res_info = *resinfo;
}

static void
define_icon_rcdata (rc_res_id id, const rc_res_res_info *resinfo,
		    rc_rcdata_item *data)
{
  rc_res_resource *r;
  rc_uint_type len_data;
  bfd_byte *pb_data;

  pb_data = rcdata_render_as_buffer (data, &len_data);

  r = define_standard_resource (&resources, RT_ICON, id, resinfo->language, 0);
  r->type = RES_TYPE_ICON;
  r->u.data.length = len_data;
  r->u.data.data = pb_data;
  r->res_info = *resinfo;
}

/* Define a menu resource.  */

void
define_menu (rc_res_id id, const rc_res_res_info *resinfo,
	     rc_menuitem *menuitems)
{
  rc_menu *m;
  rc_res_resource *r;

  m = (rc_menu *) res_alloc (sizeof (rc_menu));
  m->items = menuitems;
  m->help = 0;

  r = define_standard_resource (&resources, RT_MENU, id, resinfo->language, 0);
  r->type = RES_TYPE_MENU;
  r->u.menu = m;
  r->res_info = *resinfo;
}

/* Define a menu item.  This does not define a resource, but merely
   allocates and fills in a structure.  */

rc_menuitem *
define_menuitem (const unichar *text, rc_uint_type menuid, rc_uint_type type,
		 rc_uint_type state, rc_uint_type help,
		 rc_menuitem *menuitems)
{
  rc_menuitem *mi;

  mi = (rc_menuitem *) res_alloc (sizeof (rc_menuitem));
  mi->next = NULL;
  mi->type = type;
  mi->state = state;
  mi->id = menuid;
  mi->text = unichar_dup (text);
  mi->help = help;
  mi->popup = menuitems;
  return mi;
}

/* Define a messagetable resource.  */

void
define_messagetable (rc_res_id id, const rc_res_res_info *resinfo,
		     const char *filename)
{
  FILE *e;
  char *real_filename;
  struct stat s;
  bfd_byte *data;
  rc_res_resource *r;

  e = open_file_search (filename, FOPEN_RB, "messagetable file",
			&real_filename);

  if (stat (real_filename, &s) < 0)
    fatal (_("stat failed on bitmap file `%s': %s"), real_filename,
	   strerror (errno));

  data = (bfd_byte *) res_alloc (s.st_size);

  get_data (e, data, s.st_size, real_filename);

  fclose (e);
  free (real_filename);

  r = define_standard_resource (&resources, RT_MESSAGETABLE, id,
				resinfo->language, 0);

  r->type = RES_TYPE_MESSAGETABLE;
  r->u.data.length = s.st_size;
  r->u.data.data = data;
  r->res_info = *resinfo;
}

/* Define an rcdata resource.  */

void
define_rcdata (rc_res_id id, const rc_res_res_info *resinfo,
	       rc_rcdata_item *data)
{
  rc_res_resource *r;

  r = define_standard_resource (&resources, RT_RCDATA, id,
				resinfo->language, 0);
  r->type = RES_TYPE_RCDATA;
  r->u.rcdata = data;
  r->res_info = *resinfo;
}

/* Create an rcdata item holding a string.  */

rc_rcdata_item *
define_rcdata_string (const char *string, rc_uint_type len)
{
  rc_rcdata_item *ri;
  char *s;

  ri = (rc_rcdata_item *) res_alloc (sizeof (rc_rcdata_item));
  ri->next = NULL;
  ri->type = RCDATA_STRING;
  ri->u.string.length = len;
  s = (char *) res_alloc (len);
  memcpy (s, string, len);
  ri->u.string.s = s;

  return ri;
}

/* Create an rcdata item holding a unicode string.  */

rc_rcdata_item *
define_rcdata_unistring (const unichar *string, rc_uint_type len)
{
  rc_rcdata_item *ri;
  unichar *s;

  ri = (rc_rcdata_item *) res_alloc (sizeof (rc_rcdata_item));
  ri->next = NULL;
  ri->type = RCDATA_WSTRING;
  ri->u.wstring.length = len;
  s = (unichar *) res_alloc (len * sizeof (unichar));
  memcpy (s, string, len * sizeof (unichar));
  ri->u.wstring.w = s;

  return ri;
}

/* Create an rcdata item holding a number.  */

rc_rcdata_item *
define_rcdata_number (rc_uint_type val, int dword)
{
  rc_rcdata_item *ri;

  ri = (rc_rcdata_item *) res_alloc (sizeof (rc_rcdata_item));
  ri->next = NULL;
  ri->type = dword ? RCDATA_DWORD : RCDATA_WORD;
  ri->u.word = val;

  return ri;
}

/* Define a stringtable resource.  This is called for each string
   which appears in a STRINGTABLE statement.  */

void
define_stringtable (const rc_res_res_info *resinfo,
		    rc_uint_type stringid, const unichar *string)
{
  rc_res_id id;
  rc_res_resource *r;

  id.named = 0;
  id.u.id = (stringid >> 4) + 1;
  r = define_standard_resource (&resources, RT_STRING, id,
				resinfo->language, 1);

  if (r->type == RES_TYPE_UNINITIALIZED)
    {
      int i;

      r->type = RES_TYPE_STRINGTABLE;
      r->u.stringtable = ((rc_stringtable *)
			  res_alloc (sizeof (rc_stringtable)));
      for (i = 0; i < 16; i++)
	{
	  r->u.stringtable->strings[i].length = 0;
	  r->u.stringtable->strings[i].string = NULL;
	}

      r->res_info = *resinfo;
    }

  r->u.stringtable->strings[stringid & 0xf].length = unichar_len (string);
  r->u.stringtable->strings[stringid & 0xf].string = unichar_dup (string);
}

void
define_toolbar (rc_res_id id, rc_res_res_info *resinfo, rc_uint_type width, rc_uint_type height,
		rc_toolbar_item *items)
{
  rc_toolbar *t;
  rc_res_resource *r;

  t = (rc_toolbar *) res_alloc (sizeof (rc_toolbar));
  t->button_width = width;
  t->button_height = height;
  t->nitems = 0;
  t->items = items;
  while (items != NULL)
  {
    t->nitems+=1;
    items = items->next;
  }
  r = define_standard_resource (&resources, RT_TOOLBAR, id, resinfo->language, 0);
  r->type = RES_TYPE_TOOLBAR;
  r->u.toolbar = t;
  r->res_info = *resinfo;
}

/* Define a user data resource where the data is in the rc file.  */

void
define_user_data (rc_res_id id, rc_res_id type,
		  const rc_res_res_info *resinfo,
		  rc_rcdata_item *data)
{
  rc_res_id ids[3];
  rc_res_resource *r;
  bfd_byte *pb_data;
  rc_uint_type len_data;

  /* We have to check if the binary data is parsed specially.  */
  if (type.named == 0)
    {
      switch (type.u.id)
      {
      case RT_FONTDIR:
	define_fontdir_rcdata (id, resinfo, data);
	return;
      case RT_FONT:
	define_font_rcdata (id, resinfo, data);
	return;
      case RT_ICON:
	define_icon_rcdata (id, resinfo, data);
	return;
      case RT_BITMAP:
	define_bitmap_rcdata (id, resinfo, data);
	return;
      case RT_CURSOR:
	define_cursor_rcdata (id, resinfo, data);
	return;
      case RT_GROUP_ICON:
	define_group_icon_rcdata (id, resinfo, data);
	return;
      case RT_GROUP_CURSOR:
	define_group_cursor_rcdata (id, resinfo, data);
	return;
      case RT_MESSAGETABLE:
	define_messagetable_rcdata (id, resinfo, data);
	return;
      default:
	/* Treat as normal user-data.  */
	break;
      }
    }
  ids[0] = type;
  ids[1] = id;
  ids[2].named = 0;
  ids[2].u.id = resinfo->language;

  r = define_resource (& resources, 3, ids, 0);
  r->type = RES_TYPE_USERDATA;
  r->u.userdata = ((rc_rcdata_item *)
		   res_alloc (sizeof (rc_rcdata_item)));
  r->u.userdata->next = NULL;
  r->u.userdata->type = RCDATA_BUFFER;
  pb_data = rcdata_render_as_buffer (data, &len_data);
  r->u.userdata->u.buffer.length = len_data;
  r->u.userdata->u.buffer.data = pb_data;
  r->res_info = *resinfo;
}

void
define_rcdata_file (rc_res_id id, const rc_res_res_info *resinfo,
		    const char *filename)
{
  rc_rcdata_item *ri;
  FILE *e;
  char *real_filename;
  struct stat s;
  bfd_byte *data;

  e = open_file_search (filename, FOPEN_RB, "file", &real_filename);


  if (stat (real_filename, &s) < 0)
    fatal (_("stat failed on file `%s': %s"), real_filename,
	   strerror (errno));

  data = (bfd_byte *) res_alloc (s.st_size);

  get_data (e, data, s.st_size, real_filename);

  fclose (e);
  free (real_filename);

  ri = (rc_rcdata_item *) res_alloc (sizeof (rc_rcdata_item));
  ri->next = NULL;
  ri->type = RCDATA_BUFFER;
  ri->u.buffer.length = s.st_size;
  ri->u.buffer.data = data;

  define_rcdata (id, resinfo, ri);
}

/* Define a user data resource where the data is in a file.  */

void
define_user_file (rc_res_id id, rc_res_id type,
		  const rc_res_res_info *resinfo, const char *filename)
{
  FILE *e;
  char *real_filename;
  struct stat s;
  bfd_byte *data;
  rc_res_id ids[3];
  rc_res_resource *r;

  e = open_file_search (filename, FOPEN_RB, "file", &real_filename);

  if (stat (real_filename, &s) < 0)
    fatal (_("stat failed on file `%s': %s"), real_filename,
	   strerror (errno));

  data = (bfd_byte *) res_alloc (s.st_size);

  get_data (e, data, s.st_size, real_filename);

  fclose (e);
  free (real_filename);

  ids[0] = type;
  ids[1] = id;
  ids[2].named = 0;
  ids[2].u.id = resinfo->language;

  r = define_resource (&resources, 3, ids, 0);
  r->type = RES_TYPE_USERDATA;
  r->u.userdata = ((rc_rcdata_item *)
		   res_alloc (sizeof (rc_rcdata_item)));
  r->u.userdata->next = NULL;
  r->u.userdata->type = RCDATA_BUFFER;
  r->u.userdata->u.buffer.length = s.st_size;
  r->u.userdata->u.buffer.data = data;
  r->res_info = *resinfo;
}

/* Define a versioninfo resource.  */

void
define_versioninfo (rc_res_id id, rc_uint_type language,
		    rc_fixed_versioninfo *fixedverinfo,
		    rc_ver_info *verinfo)
{
  rc_res_resource *r;

  r = define_standard_resource (&resources, RT_VERSION, id, language, 0);
  r->type = RES_TYPE_VERSIONINFO;
  r->u.versioninfo = ((rc_versioninfo *)
		      res_alloc (sizeof (rc_versioninfo)));
  r->u.versioninfo->fixed = fixedverinfo;
  r->u.versioninfo->var = verinfo;
  r->res_info.language = language;
}

/* Add string version info to a list of version information.  */

rc_ver_info *
append_ver_stringfileinfo (rc_ver_info *verinfo, const char *language,
			   rc_ver_stringinfo *strings)
{
  rc_ver_info *vi, **pp;

  vi = (rc_ver_info *) res_alloc (sizeof (rc_ver_info));
  vi->next = NULL;
  vi->type = VERINFO_STRING;
  unicode_from_ascii ((rc_uint_type *) NULL, &vi->u.string.language, language);
  vi->u.string.strings = strings;

  for (pp = &verinfo; *pp != NULL; pp = &(*pp)->next)
    ;
  *pp = vi;

  return verinfo;
}

/* Add variable version info to a list of version information.  */

rc_ver_info *
append_ver_varfileinfo (rc_ver_info *verinfo, const unichar *key,
			rc_ver_varinfo *var)
{
  rc_ver_info *vi, **pp;

  vi = (rc_ver_info *) res_alloc (sizeof *vi);
  vi->next = NULL;
  vi->type = VERINFO_VAR;
  vi->u.var.key = unichar_dup (key);
  vi->u.var.var = var;

  for (pp = &verinfo; *pp != NULL; pp = &(*pp)->next)
    ;
  *pp = vi;

  return verinfo;
}

/* Append version string information to a list.  */

rc_ver_stringinfo *
append_verval (rc_ver_stringinfo *strings, const unichar *key,
	       const unichar *value)
{
  rc_ver_stringinfo *vs, **pp;

  vs = (rc_ver_stringinfo *) res_alloc (sizeof (rc_ver_stringinfo));
  vs->next = NULL;
  vs->key = unichar_dup (key);
  vs->value = unichar_dup (value);

  for (pp = &strings; *pp != NULL; pp = &(*pp)->next)
    ;
  *pp = vs;

  return strings;
}

/* Append version variable information to a list.  */

rc_ver_varinfo *
append_vertrans (rc_ver_varinfo *var, rc_uint_type language,
		 rc_uint_type charset)
{
  rc_ver_varinfo *vv, **pp;

  vv = (rc_ver_varinfo *) res_alloc (sizeof (rc_ver_varinfo));
  vv->next = NULL;
  vv->language = language;
  vv->charset = charset;

  for (pp = &var; *pp != NULL; pp = &(*pp)->next)
    ;
  *pp = vv;

  return var;
}

/* Local functions used to write out an rc file.  */

static void indent (FILE *, int);
static void write_rc_directory (FILE *, const rc_res_directory *, const rc_res_id *,
				const rc_res_id *, rc_uint_type *, int);
static void write_rc_subdir (FILE *, const rc_res_entry *, const rc_res_id *,
			     const rc_res_id *, rc_uint_type *, int);
static void write_rc_resource (FILE *, const rc_res_id *, const rc_res_id *,
			       const rc_res_resource *, rc_uint_type *);
static void write_rc_accelerators (FILE *, const rc_accelerator *);
static void write_rc_cursor (FILE *, const rc_cursor *);
static void write_rc_group_cursor (FILE *, const rc_group_cursor *);
static void write_rc_dialog (FILE *, const rc_dialog *);
static void write_rc_dialog_control (FILE *, const rc_dialog_control *);
static void write_rc_fontdir (FILE *, const rc_fontdir *);
static void write_rc_group_icon (FILE *, const rc_group_icon *);
static void write_rc_menu (FILE *, const rc_menu *, int);
static void write_rc_toolbar (FILE *, const rc_toolbar *);
static void write_rc_menuitems (FILE *, const rc_menuitem *, int, int);
static void write_rc_messagetable (FILE *, rc_uint_type , const bfd_byte *);

static void write_rc_datablock (FILE *, rc_uint_type , const bfd_byte *, int, int, int);
static void write_rc_rcdata (FILE *, const rc_rcdata_item *, int);
static void write_rc_stringtable (FILE *, const rc_res_id *, const rc_stringtable *);
static void write_rc_versioninfo (FILE *, const rc_versioninfo *);

/* Indent a given number of spaces.  */

static void
indent (FILE *e, int c)
{
  int i;

  for (i = 0; i < c; i++)
    putc (' ', e);
}

/* Dump the resources we have read in the format of an rc file.

   Reasoned by the fact, that some resources need to be stored into file and
   refer to that file, we use the user-data model for that to express it binary
   without the need to store it somewhere externally.  */

void
write_rc_file (const char *filename, const rc_res_directory *resources)
{
  FILE *e;
  rc_uint_type language;

  if (filename == NULL)
    e = stdout;
  else
    {
      e = fopen (filename, FOPEN_WT);
      if (e == NULL)
	fatal (_("can't open `%s' for output: %s"), filename, strerror (errno));
    }

  language = (rc_uint_type) ((bfd_signed_vma) -1);
  write_rc_directory (e, resources, (const rc_res_id *) NULL,
		      (const rc_res_id *) NULL, &language, 1);
}

/* Write out a directory.  E is the file to write to.  RD is the
   directory.  TYPE is a pointer to the level 1 ID which serves as the
   resource type.  NAME is a pointer to the level 2 ID which serves as
   an individual resource name.  LANGUAGE is a pointer to the current
   language.  LEVEL is the level in the tree.  */

static void
write_rc_directory (FILE *e, const rc_res_directory *rd,
		    const rc_res_id *type, const rc_res_id *name,
		    rc_uint_type *language, int level)
{
  const rc_res_entry *re;

  /* Print out some COFF information that rc files can't represent.  */
  if (rd->time != 0 || rd->characteristics != 0 || rd->major != 0 || rd->minor != 0)
    {
      wr_printcomment (e, "COFF information not part of RC");
  if (rd->time != 0)
	wr_printcomment (e, "Time stamp: %u", rd->time);
  if (rd->characteristics != 0)
	wr_printcomment (e, "Characteristics: %u", rd->characteristics);
  if (rd->major != 0 || rd->minor != 0)
	wr_printcomment (e, "Version major:%d minor:%d", rd->major, rd->minor);
    }

  for (re = rd->entries;  re != NULL; re = re->next)
    {
      switch (level)
	{
	case 1:
	  /* If we're at level 1, the key of this resource is the
             type.  This normally duplicates the information we have
             stored with the resource itself, but we need to remember
             the type if this is a user define resource type.  */
	  type = &re->id;
	  break;

	case 2:
	  /* If we're at level 2, the key of this resource is the name
	     we are going to use in the rc printout.  */
	  name = &re->id;
	  break;

	case 3:
	  /* If we're at level 3, then this key represents a language.
	     Use it to update the current language.  */
	  if (! re->id.named
	      && re->id.u.id != (unsigned long) (unsigned int) *language
	      && (re->id.u.id & 0xffff) == re->id.u.id)
	    {
	      wr_print (e, "LANGUAGE %u, %u\n",
		       re->id.u.id & ((1 << SUBLANG_SHIFT) - 1),
		       (re->id.u.id >> SUBLANG_SHIFT) & 0xff);
	      *language = re->id.u.id;
	    }
	  break;

	default:
	  break;
	}

      if (re->subdir)
	write_rc_subdir (e, re, type, name, language, level);
      else
	{
	  if (level == 3)
	    {
	      /* This is the normal case: the three levels are
                 TYPE/NAME/LANGUAGE.  NAME will have been set at level
                 2, and represents the name to use.  We probably just
                 set LANGUAGE, and it will probably match what the
                 resource itself records if anything.  */
	      write_rc_resource (e, type, name, re->u.res, language);
	    }
	  else
	    {
	      wr_printcomment (e, "Resource at unexpected level %d", level);
	      write_rc_resource (e, type, (rc_res_id *) NULL, re->u.res,
				 language);
	    }
	}
    }
  if (rd->entries == NULL)
    {
      wr_print_flush (e);
    }
}

/* Write out a subdirectory entry.  E is the file to write to.  RE is
   the subdirectory entry.  TYPE and NAME are pointers to higher level
   IDs, or NULL.  LANGUAGE is a pointer to the current language.
   LEVEL is the level in the tree.  */

static void
write_rc_subdir (FILE *e, const rc_res_entry *re,
		 const rc_res_id *type, const rc_res_id *name,
		 rc_uint_type *language, int level)
{
  fprintf (e, "\n");
  switch (level)
    {
    case 1:
      wr_printcomment (e, "Type: ");
      if (re->id.named)
	res_id_print (e, re->id, 1);
      else
	{
	  const char *s;

	  switch (re->id.u.id)
	    {
	    case RT_CURSOR: s = "cursor"; break;
	    case RT_BITMAP: s = "bitmap"; break;
	    case RT_ICON: s = "icon"; break;
	    case RT_MENU: s = "menu"; break;
	    case RT_DIALOG: s = "dialog"; break;
	    case RT_STRING: s = "stringtable"; break;
	    case RT_FONTDIR: s = "fontdir"; break;
	    case RT_FONT: s = "font"; break;
	    case RT_ACCELERATOR: s = "accelerators"; break;
	    case RT_RCDATA: s = "rcdata"; break;
	    case RT_MESSAGETABLE: s = "messagetable"; break;
	    case RT_GROUP_CURSOR: s = "group cursor"; break;
	    case RT_GROUP_ICON: s = "group icon"; break;
	    case RT_VERSION: s = "version"; break;
	    case RT_DLGINCLUDE: s = "dlginclude"; break;
	    case RT_PLUGPLAY: s = "plugplay"; break;
	    case RT_VXD: s = "vxd"; break;
	    case RT_ANICURSOR: s = "anicursor"; break;
	    case RT_ANIICON: s = "aniicon"; break;
	    case RT_TOOLBAR: s = "toolbar"; break;
	    case RT_HTML: s = "html"; break;
	    default: s = NULL; break;
	    }

	  if (s != NULL)
	    fprintf (e, "%s", s);
	  else
	    res_id_print (e, re->id, 1);
	}
      break;

    case 2:
      wr_printcomment (e, "Name: ");
      res_id_print (e, re->id, 1);
      break;

    case 3:
      wr_printcomment (e, "Language: ");
      res_id_print (e, re->id, 1);
      break;

    default:
      wr_printcomment (e, "Level %d: ", level);
      res_id_print (e, re->id, 1);
    }

  write_rc_directory (e, re->u.dir, type, name, language, level + 1);
}

/* Write out a single resource.  E is the file to write to.  TYPE is a
   pointer to the type of the resource.  NAME is a pointer to the name
   of the resource; it will be NULL if there is a level mismatch.  RES
   is the resource data.  LANGUAGE is a pointer to the current
   language.  */

static void
write_rc_resource (FILE *e, const rc_res_id *type,
		   const rc_res_id *name, const rc_res_resource *res,
		   rc_uint_type *language)
{
  const char *s;
  int rt;
  int menuex = 0;

  switch (res->type)
    {
    default:
      abort ();

    case RES_TYPE_ACCELERATOR:
      s = "ACCELERATORS";
      rt = RT_ACCELERATOR;
      break;

    case RES_TYPE_BITMAP:
      s = "2 /* RT_BITMAP */";
      rt = RT_BITMAP;
      break;

    case RES_TYPE_CURSOR:
      s = "1 /* RT_CURSOR */";
      rt = RT_CURSOR;
      break;

    case RES_TYPE_GROUP_CURSOR:
      s = "12 /* RT_GROUP_CURSOR */";
      rt = RT_GROUP_CURSOR;
      break;

    case RES_TYPE_DIALOG:
      if (extended_dialog (res->u.dialog))
	s = "DIALOGEX";
      else
	s = "DIALOG";
      rt = RT_DIALOG;
      break;

    case RES_TYPE_FONT:
      s = "8 /* RT_FONT */";
      rt = RT_FONT;
      break;

    case RES_TYPE_FONTDIR:
      s = "7 /* RT_FONTDIR */";
      rt = RT_FONTDIR;
      break;

    case RES_TYPE_ICON:
      s = "3 /* RT_ICON */";
      rt = RT_ICON;
      break;

    case RES_TYPE_GROUP_ICON:
      s = "14 /* RT_GROUP_ICON */";
      rt = RT_GROUP_ICON;
      break;

    case RES_TYPE_MENU:
      if (extended_menu (res->u.menu))
	{
	  s = "MENUEX";
	  menuex = 1;
	}
      else
	{
	  s = "MENU";
	  menuex = 0;
	}
      rt = RT_MENU;
      break;

    case RES_TYPE_MESSAGETABLE:
      s = "11 /* RT_MESSAGETABLE */";
      rt = RT_MESSAGETABLE;
      break;

    case RES_TYPE_RCDATA:
      s = "RCDATA";
      rt = RT_RCDATA;
      break;

    case RES_TYPE_STRINGTABLE:
      s = "STRINGTABLE";
      rt = RT_STRING;
      break;

    case RES_TYPE_USERDATA:
      s = NULL;
      rt = 0;
      break;

    case RES_TYPE_VERSIONINFO:
      s = "VERSIONINFO";
      rt = RT_VERSION;
      break;

    case RES_TYPE_TOOLBAR:
      s = "TOOLBAR";
      rt = RT_TOOLBAR;
      break;
    }

  if (rt != 0
      && type != NULL
      && (type->named || type->u.id != (unsigned long) rt))
    {
      wr_printcomment (e, "Unexpected resource type mismatch: ");
      res_id_print (e, *type, 1);
      fprintf (e, " != %d", rt);
    }

  if (res->coff_info.codepage != 0)
    wr_printcomment (e, "Code page: %u", res->coff_info.codepage);
  if (res->coff_info.reserved != 0)
    wr_printcomment (e, "COFF reserved value: %u", res->coff_info.reserved);

  wr_print (e, "\n");
  if (rt == RT_STRING)
    ;
  else
    {
  if (name != NULL)
	res_id_print (e, *name, 1);
  else
    fprintf (e, "??Unknown-Name??");
  fprintf (e, " ");
    }

  if (s != NULL)
    fprintf (e, "%s", s);
  else if (type != NULL)
    {
      if (type->named == 0)
	{
#define PRINT_RT_NAME(NAME) case NAME: \
	fprintf (e, "%u /* %s */", (unsigned int) NAME, #NAME); \
	break

	  switch (type->u.id)
	    {
	    default:
    res_id_print (e, *type, 0);
	      break;
	
	    PRINT_RT_NAME(RT_MANIFEST);
	    PRINT_RT_NAME(RT_ANICURSOR);
	    PRINT_RT_NAME(RT_ANIICON);
	    PRINT_RT_NAME(RT_RCDATA);
	    PRINT_RT_NAME(RT_ICON);
	    PRINT_RT_NAME(RT_CURSOR);
	    PRINT_RT_NAME(RT_BITMAP);
	    PRINT_RT_NAME(RT_PLUGPLAY);
	    PRINT_RT_NAME(RT_VXD);
	    PRINT_RT_NAME(RT_FONT);
	    PRINT_RT_NAME(RT_FONTDIR);
	    PRINT_RT_NAME(RT_HTML);
	    PRINT_RT_NAME(RT_MESSAGETABLE);
	    PRINT_RT_NAME(RT_DLGINCLUDE);
	    PRINT_RT_NAME(RT_DLGINIT);
	    }
#undef PRINT_RT_NAME
	}
      else
	res_id_print (e, *type, 1);
    }
  else
    fprintf (e, "??Unknown-Type??");

  if (res->res_info.memflags != 0)
    {
      if ((res->res_info.memflags & MEMFLAG_MOVEABLE) != 0)
	fprintf (e, " MOVEABLE");
      if ((res->res_info.memflags & MEMFLAG_PURE) != 0)
	fprintf (e, " PURE");
      if ((res->res_info.memflags & MEMFLAG_PRELOAD) != 0)
	fprintf (e, " PRELOAD");
      if ((res->res_info.memflags & MEMFLAG_DISCARDABLE) != 0)
	fprintf (e, " DISCARDABLE");
    }

  if (res->type == RES_TYPE_DIALOG)
    {
      fprintf (e, " %d, %d, %d, %d",
	       (int) res->u.dialog->x, (int) res->u.dialog->y,
	       (int) res->u.dialog->width, (int) res->u.dialog->height);
      if (res->u.dialog->ex != NULL
	  && res->u.dialog->ex->help != 0)
	fprintf (e, ", %u", (unsigned int) res->u.dialog->ex->help);
    }
  else if (res->type == RES_TYPE_TOOLBAR)
  {
    fprintf (e, " %d, %d", (int) res->u.toolbar->button_width,
	     (int) res->u.toolbar->button_height);
    }

  fprintf (e, "\n");

  if ((res->res_info.language != 0 && res->res_info.language != *language)
      || res->res_info.characteristics != 0
      || res->res_info.version != 0)
    {
      int modifiers;

      switch (res->type)
	{
	case RES_TYPE_ACCELERATOR:
	case RES_TYPE_DIALOG:
	case RES_TYPE_MENU:
	case RES_TYPE_RCDATA:
	case RES_TYPE_STRINGTABLE:
	  modifiers = 1;
	  break;

	default:
	  modifiers = 0;
	  break;
	}

      if (res->res_info.language != 0 && res->res_info.language != *language)
	fprintf (e, "%sLANGUAGE %d, %d\n",
		 modifiers ? "// " : "",
		 (int) res->res_info.language & ((1<<SUBLANG_SHIFT)-1),
		 (int) (res->res_info.language >> SUBLANG_SHIFT) & 0xff);
      if (res->res_info.characteristics != 0)
	fprintf (e, "%sCHARACTERISTICS %u\n",
		 modifiers ? "// " : "",
		 (unsigned int) res->res_info.characteristics);
      if (res->res_info.version != 0)
	fprintf (e, "%sVERSION %u\n",
		 modifiers ? "// " : "",
		 (unsigned int) res->res_info.version);
    }

  switch (res->type)
    {
    default:
      abort ();

    case RES_TYPE_ACCELERATOR:
      write_rc_accelerators (e, res->u.acc);
      break;

    case RES_TYPE_CURSOR:
      write_rc_cursor (e, res->u.cursor);
      break;

    case RES_TYPE_GROUP_CURSOR:
      write_rc_group_cursor (e, res->u.group_cursor);
      break;

    case RES_TYPE_DIALOG:
      write_rc_dialog (e, res->u.dialog);
      break;

    case RES_TYPE_FONTDIR:
      write_rc_fontdir (e, res->u.fontdir);
      break;

    case RES_TYPE_GROUP_ICON:
      write_rc_group_icon (e, res->u.group_icon);
      break;

    case RES_TYPE_MENU:
      write_rc_menu (e, res->u.menu, menuex);
      break;

    case RES_TYPE_RCDATA:
      write_rc_rcdata (e, res->u.rcdata, 0);
      break;

    case RES_TYPE_STRINGTABLE:
      write_rc_stringtable (e, name, res->u.stringtable);
      break;

    case RES_TYPE_USERDATA:
      write_rc_rcdata (e, res->u.userdata, 0);
      break;

    case RES_TYPE_TOOLBAR:
      write_rc_toolbar (e, res->u.toolbar);
      break;

    case RES_TYPE_VERSIONINFO:
      write_rc_versioninfo (e, res->u.versioninfo);
      break;

    case RES_TYPE_BITMAP:
    case RES_TYPE_FONT:
    case RES_TYPE_ICON:
      write_rc_datablock (e, res->u.data.length, res->u.data.data, 0, 1, 0);
      break;
    case RES_TYPE_MESSAGETABLE:
      write_rc_messagetable (e, res->u.data.length, res->u.data.data);
      break;
    }
}

/* Write out accelerator information.  */

static void
write_rc_accelerators (FILE *e, const rc_accelerator *accelerators)
{
  const rc_accelerator *acc;

  fprintf (e, "BEGIN\n");
  for (acc = accelerators; acc != NULL; acc = acc->next)
    {
      int printable;

      fprintf (e, "  ");

      if ((acc->key & 0x7f) == acc->key
	  && ISPRINT (acc->key)
	  && (acc->flags & ACC_VIRTKEY) == 0)
	{
	  fprintf (e, "\"%c\"", (char) acc->key);
	  printable = 1;
	}
      else
	{
	  fprintf (e, "%d", (int) acc->key);
	  printable = 0;
	}

      fprintf (e, ", %d", (int) acc->id);

      if (! printable)
	{
	  if ((acc->flags & ACC_VIRTKEY) != 0)
	    fprintf (e, ", VIRTKEY");
	  else
	    fprintf (e, ", ASCII");
	}

      if ((acc->flags & ACC_SHIFT) != 0)
	fprintf (e, ", SHIFT");
      if ((acc->flags & ACC_CONTROL) != 0)
	fprintf (e, ", CONTROL");
      if ((acc->flags & ACC_ALT) != 0)
	fprintf (e, ", ALT");

      fprintf (e, "\n");
    }

  fprintf (e, "END\n");
}

/* Write out cursor information.  This would normally be in a separate
   file, which the rc file would include.  */

static void
write_rc_cursor (FILE *e, const rc_cursor *cursor)
{
  fprintf (e, "BEGIN\n");
  indent (e, 2);
  fprintf (e, " 0x%x, 0x%x,\t/* Hotspot x: %d, y: %d.  */\n",
	   (unsigned int) cursor->xhotspot, (unsigned int) cursor->yhotspot,
	   (int) cursor->xhotspot, (int) cursor->yhotspot);
  write_rc_datablock (e, (rc_uint_type) cursor->length, (const bfd_byte *) cursor->data,
  		      0, 0, 0);
  fprintf (e, "END\n");
}

/* Write out group cursor data.  This would normally be built from the
   cursor data.  */

static void
write_rc_group_cursor (FILE *e, const rc_group_cursor *group_cursor)
{
  const rc_group_cursor *gc;
  int c;

  for (c = 0, gc = group_cursor; gc != NULL; gc = gc->next, c++)
    ;
  fprintf (e, "BEGIN\n");

  indent (e, 2);
  fprintf (e, "0, 2, %d%s\t /* Having %d items.  */\n", c, (c != 0 ? "," : ""), c);
  indent (e, 4);
  fprintf (e, "/* width, height, planes, bits, bytes, index.  */\n");

  for (c = 1, gc = group_cursor; gc != NULL; gc = gc->next, c++)
    {
      indent (e, 4);
      fprintf (e, "%d, %d, %d, %d, 0x%xL, %d%s /* Element %d. */\n",
	(int) gc->width, (int) gc->height, (int) gc->planes, (int) gc->bits,
	(unsigned int) gc->bytes, (int) gc->index, (gc->next != NULL ? "," : ""), c);
      fprintf (e, "/* width: %d; height %d; planes %d; bits %d.  */\n",
	     (int) gc->width, (int) gc->height, (int) gc->planes,
	     (int) gc->bits);
    }
  fprintf (e, "END\n");
}

/* Write dialog data.  */

static void
write_rc_dialog (FILE *e, const rc_dialog *dialog)
{
  const rc_dialog_control *control;

  fprintf (e, "STYLE 0x%x\n", dialog->style);

  if (dialog->exstyle != 0)
    fprintf (e, "EXSTYLE 0x%x\n", (unsigned int) dialog->exstyle);

  if ((dialog->class.named && dialog->class.u.n.length > 0)
      || dialog->class.u.id != 0)
    {
      fprintf (e, "CLASS ");
      res_id_print (e, dialog->class, 1);
      fprintf (e, "\n");
    }

  if (dialog->caption != NULL)
    {
      fprintf (e, "CAPTION ");
      unicode_print_quoted (e, dialog->caption, -1);
      fprintf (e, "\n");
    }

  if ((dialog->menu.named && dialog->menu.u.n.length > 0)
      || dialog->menu.u.id != 0)
    {
      fprintf (e, "MENU ");
      res_id_print (e, dialog->menu, 0);
      fprintf (e, "\n");
    }

  if (dialog->font != NULL)
    {
      fprintf (e, "FONT %d, ", (int) dialog->pointsize);
      unicode_print_quoted (e, dialog->font, -1);
      if (dialog->ex != NULL
	  && (dialog->ex->weight != 0
	      || dialog->ex->italic != 0
	      || dialog->ex->charset != 1))
	fprintf (e, ", %d, %d, %d",
		 (int) dialog->ex->weight,
		 (int) dialog->ex->italic,
		 (int) dialog->ex->charset);
      fprintf (e, "\n");
    }

  fprintf (e, "BEGIN\n");

  for (control = dialog->controls; control != NULL; control = control->next)
    write_rc_dialog_control (e, control);

  fprintf (e, "END\n");
}

/* For each predefined control keyword, this table provides the class
   and the style.  */

struct control_info
{
  const char *name;
  unsigned short class;
  unsigned long style;
};

static const struct control_info control_info[] =
{
  { "AUTO3STATE", CTL_BUTTON, BS_AUTO3STATE },
  { "AUTOCHECKBOX", CTL_BUTTON, BS_AUTOCHECKBOX },
  { "AUTORADIOBUTTON", CTL_BUTTON, BS_AUTORADIOBUTTON },
  { "CHECKBOX", CTL_BUTTON, BS_CHECKBOX },
  { "COMBOBOX", CTL_COMBOBOX, (unsigned long) -1 },
  { "CTEXT", CTL_STATIC, SS_CENTER },
  { "DEFPUSHBUTTON", CTL_BUTTON, BS_DEFPUSHBUTTON },
  { "EDITTEXT", CTL_EDIT, (unsigned long) -1 },
  { "GROUPBOX", CTL_BUTTON, BS_GROUPBOX },
  { "ICON", CTL_STATIC, SS_ICON },
  { "LISTBOX", CTL_LISTBOX, (unsigned long) -1 },
  { "LTEXT", CTL_STATIC, SS_LEFT },
  { "PUSHBOX", CTL_BUTTON, BS_PUSHBOX },
  { "PUSHBUTTON", CTL_BUTTON, BS_PUSHBUTTON },
  { "RADIOBUTTON", CTL_BUTTON, BS_RADIOBUTTON },
  { "RTEXT", CTL_STATIC, SS_RIGHT },
  { "SCROLLBAR", CTL_SCROLLBAR, (unsigned long) -1 },
  { "STATE3", CTL_BUTTON, BS_3STATE },
  /* It's important that USERBUTTON come after all the other button
     types, so that it won't be matched too early.  */
  { "USERBUTTON", CTL_BUTTON, (unsigned long) -1 },
  { NULL, 0, 0 }
};

/* Write a dialog control.  */

static void
write_rc_dialog_control (FILE *e, const rc_dialog_control *control)
{
  const struct control_info *ci;

  fprintf (e, "  ");

  if (control->class.named)
    ci = NULL;
  else
    {
      for (ci = control_info; ci->name != NULL; ++ci)
	if (ci->class == control->class.u.id
	    && (ci->style == (unsigned long) -1
		|| ci->style == (control->style & 0xff)))
	  break;
    }
  if (ci == NULL)
    fprintf (e, "CONTROL");
  else if (ci->name != NULL)
    fprintf (e, "%s", ci->name);
  else
    {
    fprintf (e, "CONTROL");
      ci = NULL;
    }

  if (control->text.named || control->text.u.id != 0)
    {
      fprintf (e, " ");
      res_id_print (e, control->text, 1);
      fprintf (e, ",");
    }

  fprintf (e, " %d, ", (int) control->id);

  if (ci == NULL)
    {
      if (control->class.named)
	fprintf (e, "\"");
      res_id_print (e, control->class, 0);
      if (control->class.named)
	fprintf (e, "\"");
      fprintf (e, ", 0x%x, ", (unsigned int) control->style);
    }

  fprintf (e, "%d, %d", (int) control->x, (int) control->y);

  if (control->style != SS_ICON
      || control->exstyle != 0
      || control->width != 0
      || control->height != 0
      || control->help != 0)
    {
      fprintf (e, ", %d, %d", (int) control->width, (int) control->height);

      /* FIXME: We don't need to print the style if it is the default.
	 More importantly, in certain cases we actually need to turn
	 off parts of the forced style, by using NOT.  */
      if (ci != NULL)
	fprintf (e, ", 0x%x", (unsigned int) control->style);

      if (control->exstyle != 0 || control->help != 0)
	fprintf (e, ", 0x%x, %u", (unsigned int) control->exstyle,
		 (unsigned int) control->help);
    }

  fprintf (e, "\n");

  if (control->data != NULL)
    write_rc_rcdata (e, control->data, 2);
}

/* Write out font directory data.  This would normally be built from
   the font data.  */

static void
write_rc_fontdir (FILE *e, const rc_fontdir *fontdir)
{
  const rc_fontdir *fc;
  int c;

  for (c = 0, fc = fontdir; fc != NULL; fc = fc->next, c++)
    ;
  fprintf (e, "BEGIN\n");
  indent (e, 2);
  fprintf (e, "%d%s\t /* Has %d elements.  */\n", c, (c != 0 ? "," : ""), c);
  for (c = 1, fc = fontdir; fc != NULL; fc = fc->next, c++)
    {
      indent (e, 4);
      fprintf (e, "%d,\t/* Font no %d with index %d.  */\n",
	(int) fc->index, c, (int) fc->index);
      write_rc_datablock (e, (rc_uint_type) fc->length - 2,
			  (const bfd_byte *) fc->data + 4,fc->next != NULL,
			  0, 0);
    }
  fprintf (e, "END\n");
}

/* Write out group icon data.  This would normally be built from the
   icon data.  */

static void
write_rc_group_icon (FILE *e, const rc_group_icon *group_icon)
{
  const rc_group_icon *gi;
  int c;

  for (c = 0, gi = group_icon; gi != NULL; gi = gi->next, c++)
    ;

  fprintf (e, "BEGIN\n");
  indent (e, 2);
  fprintf (e, " 0, 1, %d%s\t /* Has %d elements.  */\n", c, (c != 0 ? "," : ""), c);

  indent (e, 4);
  fprintf (e, "/* \"width height colors pad\", planes, bits, bytes, index.  */\n");
  for (c = 1, gi = group_icon; gi != NULL; gi = gi->next, c++)
    {
      indent (e, 4);
      fprintf (e, "\"\\%03o\\%03o\\%03o\\%03o\", %d, %d, 0x%xL, %d%s\t/* Element no %d.  */\n",
	gi->width, gi->height, gi->colors, 0, (int) gi->planes, (int) gi->bits,
	(unsigned int) gi->bytes, (int) gi->index, (gi->next != NULL ? "," : ""), c);
    }
  fprintf (e, "END\n");
}

/* Write out a menu resource.  */

static void
write_rc_menu (FILE *e, const rc_menu *menu, int menuex)
{
  if (menu->help != 0)
    fprintf (e, "// Help ID: %u\n", (unsigned int) menu->help);
  write_rc_menuitems (e, menu->items, menuex, 0);
}

static void
write_rc_toolbar (FILE *e, const rc_toolbar *tb)
{
  rc_toolbar_item *it;
  indent (e, 0);
  fprintf (e, "BEGIN\n");
  it = tb->items;
  while(it != NULL)
  {
    indent (e, 2);
    if (it->id.u.id == 0)
      fprintf (e, "SEPARATOR\n");
    else 
      fprintf (e, "BUTTON %d\n", (int) it->id.u.id);
    it = it->next;
  }
  indent (e, 0);
  fprintf (e, "END\n");
}

/* Write out menuitems.  */

static void
write_rc_menuitems (FILE *e, const rc_menuitem *menuitems, int menuex,
		    int ind)
{
  const rc_menuitem *mi;

  indent (e, ind);
  fprintf (e, "BEGIN\n");

  for (mi = menuitems; mi != NULL; mi = mi->next)
    {
      indent (e, ind + 2);

      if (mi->popup == NULL)
	fprintf (e, "MENUITEM");
      else
	fprintf (e, "POPUP");

      if (! menuex
	  && mi->popup == NULL
	  && mi->text == NULL
	  && mi->type == 0
	  && mi->id == 0)
	{
	  fprintf (e, " SEPARATOR\n");
	  continue;
	}

      if (mi->text == NULL)
	fprintf (e, " \"\"");
      else
	{
	  fprintf (e, " ");
	  unicode_print_quoted (e, mi->text, -1);
	}

      if (! menuex)
	{
	  if (mi->popup == NULL)
	    fprintf (e, ", %d", (int) mi->id);

	  if ((mi->type & MENUITEM_CHECKED) != 0)
	    fprintf (e, ", CHECKED");
	  if ((mi->type & MENUITEM_GRAYED) != 0)
	    fprintf (e, ", GRAYED");
	  if ((mi->type & MENUITEM_HELP) != 0)
	    fprintf (e, ", HELP");
	  if ((mi->type & MENUITEM_INACTIVE) != 0)
	    fprintf (e, ", INACTIVE");
	  if ((mi->type & MENUITEM_MENUBARBREAK) != 0)
	    fprintf (e, ", MENUBARBREAK");
	  if ((mi->type & MENUITEM_MENUBREAK) != 0)
	    fprintf (e, ", MENUBREAK");
	}
      else
	{
	  if (mi->id != 0 || mi->type != 0 || mi->state != 0 || mi->help != 0)
	    {
	      fprintf (e, ", %d", (int) mi->id);
	      if (mi->type != 0 || mi->state != 0 || mi->help != 0)
		{
		  fprintf (e, ", %u", (unsigned int) mi->type);
		  if (mi->state != 0 || mi->help != 0)
		    {
		      fprintf (e, ", %u", (unsigned int) mi->state);
		      if (mi->help != 0)
			fprintf (e, ", %u", (unsigned int) mi->help);
		    }
		}
	    }
	}

      fprintf (e, "\n");

      if (mi->popup != NULL)
	write_rc_menuitems (e, mi->popup, menuex, ind + 2);
    }

  indent (e, ind);
  fprintf (e, "END\n");
}

static int
test_rc_datablock_unicode (rc_uint_type length, const bfd_byte *data)
{
  rc_uint_type i;
  if ((length & 1) != 0)
    return 0;

  for (i = 0; i < length; i += 2)
    {
      if (data[i] == 0 && data[i + 1] == 0 && (i + 2) < length)
	return 0;
      if (data[i] == 0xff && data[i + 1] == 0xff)
	return 0;
    }
  return 1;
}

static int
test_rc_datablock_text (rc_uint_type length, const bfd_byte *data)
{
  int has_nl;
  rc_uint_type c;
  rc_uint_type i;
  
  if (length <= 1)
    return 0;

  has_nl = 0;
  for (i = 0, c = 0; i < length; i++)
    {
      if (! ISPRINT (data[i]) && data[i] != '\n'
      	  && ! (data[i] == '\r' && (i + 1) < length && data[i + 1] == '\n')
      	  && data[i] != '\t'
	  && ! (data[i] == 0 && (i + 1) != length))
	{
	  if (data[i] <= 7)
	    return 0;
	  c++;
	}
      else if (data[i] == '\n') has_nl++;
    }
  if (length > 80 && ! has_nl)
    return 0;
  c = (((c * 10000) + (i / 100) - 1)) / i;
  if (c >= 150)
    return 0;
  return 1;
}

static void
write_rc_messagetable (FILE *e, rc_uint_type length, const bfd_byte *data)
{
  int has_error = 0;
  const struct bin_messagetable *mt;
  fprintf (e, "BEGIN\n");

  write_rc_datablock (e, length, data, 0, 0, 0);

  fprintf (e, "\n");
  wr_printcomment (e, "MC syntax dump");
  if (length < BIN_MESSAGETABLE_SIZE)
    has_error = 1;
  else
    do {
      rc_uint_type m, i;
      mt = (const struct bin_messagetable *) data;
      m = windres_get_32 (&wrtarget, mt->cblocks, length);
      if (length < (BIN_MESSAGETABLE_SIZE + m * BIN_MESSAGETABLE_BLOCK_SIZE))
	{
	  has_error = 1;
	  break;
	}
      for (i = 0; i < m; i++)
	{
	  rc_uint_type low, high, offset;
	  const struct bin_messagetable_item *mti;

	  low = windres_get_32 (&wrtarget, mt->items[i].lowid, 4);
	  high = windres_get_32 (&wrtarget, mt->items[i].highid, 4);
	  offset = windres_get_32 (&wrtarget, mt->items[i].offset, 4);
	  while (low <= high)
	    {
	      rc_uint_type elen, flags;
	      if ((offset + BIN_MESSAGETABLE_ITEM_SIZE) > length)
		{
		  has_error = 1;
	  break;
		}
	      mti = (const struct bin_messagetable_item *) &data[offset];
	      elen = windres_get_16 (&wrtarget, mti->length, 2);
	      flags = windres_get_16 (&wrtarget, mti->flags, 2);
	      if ((offset + elen) > length)
		{
		  has_error = 1;
		  break;
		}
	      wr_printcomment (e, "MessageId = 0x%x", low);
	      wr_printcomment (e, "");
	      if ((flags & MESSAGE_RESOURCE_UNICODE) == MESSAGE_RESOURCE_UNICODE)
		unicode_print (e, (const unichar *) mti->data,
			       (elen - BIN_MESSAGETABLE_ITEM_SIZE) / 2);
	      else
		ascii_print (e, (const char *) mti->data,
			     (elen - BIN_MESSAGETABLE_ITEM_SIZE));
	      wr_printcomment (e,"");
	      ++low;
	      offset += elen;
	    }
	}
    } while (0);
  if (has_error)
    wr_printcomment (e, "Illegal data");
  wr_print_flush (e);
  fprintf (e, "END\n");
}

static void
write_rc_datablock (FILE *e, rc_uint_type length, const bfd_byte *data, int has_next,
		    int hasblock, int show_comment)
{
  int plen;

  if (hasblock)
    fprintf (e, "BEGIN\n");

  if (show_comment == -1)
	  {
      if (test_rc_datablock_text(length, data))
	{
	  rc_uint_type i, c;
	  for (i = 0; i < length;)
	    {
	      indent (e, 2);
	      fprintf (e, "\"");

	      for (c = 0; i < length && c < 160 && data[i] != '\n'; c++, i++)
		;
	      if (i < length && data[i] == '\n')
		++i, ++c;
	      ascii_print (e, (const char *) &data[i - c], c);
	    fprintf (e, "\"");
	      if (i < length)
		fprintf (e, "\n");
	    }
          
	  if (i == 0)
	      {
	      indent (e, 2);
	      fprintf (e, "\"\"");
	      }
	  if (has_next)
	    fprintf (e, ",");
	  fprintf (e, "\n");
	  if (hasblock)
	    fprintf (e, "END\n");
	  return;
	  }
      if (test_rc_datablock_unicode (length, data))
	{
	  rc_uint_type i, c;
	  for (i = 0; i < length;)
	    {
	      const unichar *u;

	      u = (const unichar *) &data[i];
	      indent (e, 2);
	  fprintf (e, "L\"");
    	  
	      for (c = 0; i < length && c < 160 && u[c] != '\n'; c++, i += 2)
		;
	      if (i < length && u[c] == '\n')
		i += 2, ++c;
	      unicode_print (e, u, c);
	  fprintf (e, "\"");
	      if (i < length)
		fprintf (e, "\n");
	    }

	  if (i == 0)
	  {
	      indent (e, 2);
	      fprintf (e, "L\"\"");
	    }
	  if (has_next)
	    fprintf (e, ",");
	  fprintf (e, "\n");
	  if (hasblock)
	    fprintf (e, "END\n");
	  return;
	}

      show_comment = 0;
    }

  if (length != 0)
	      {
      rc_uint_type i, max_row;
      int first = 1;

      max_row = (show_comment ? 4 : 8);
      indent (e, 2);
      for (i = 0; i + 3 < length;)
		  {
	  rc_uint_type k;
	  rc_uint_type comment_start;
	  
	  comment_start = i;
	  
	  if (! first)
	    indent (e, 2);

	  for (k = 0; k < max_row && i + 3 < length; k++, i += 4)
		      {
	      if (k == 0)
		plen  = fprintf (e, "0x%lxL",
				 (long) windres_get_32 (&wrtarget, data + i, length - i));
			else
		plen = fprintf (e, " 0x%lxL",
				(long) windres_get_32 (&wrtarget, data + i, length - i)) - 1;
	      if (has_next || (i + 4) < length)
			  {
		  if (plen>0 && plen < 11)
		    indent (e, 11 - plen);
		  fprintf (e, ",");
			  }
		      }
	  if (show_comment)
	    {
	      fprintf (e, "\t/* ");
	      ascii_print (e, (const char *) &data[comment_start], i - comment_start);
	      fprintf (e, ".  */");
		  }
		fprintf (e, "\n");
		first = 0;
	      }

      if (i + 1 < length)
	      {
		if (! first)
	    indent (e, 2);
	  plen = fprintf (e, "0x%x",
	  		  (int) windres_get_16 (&wrtarget, data + i, length - i));
	  if (has_next || i + 2 < length)
		  {
	      if (plen > 0 && plen < 11)
		indent (e, 11 - plen);
	      fprintf (e, ",");
		      }
	  if (show_comment)
	    {
	      fprintf (e, "\t/* ");
	      ascii_print (e, (const char *) &data[i], 2);
	      fprintf (e, ".  */");
		  }
		fprintf (e, "\n");
		i += 2;
		first = 0;
	      }

      if (i < length)
	      {
		if (! first)
	    indent (e, 2);
	  fprintf (e, "\"");
	  ascii_print (e, (const char *) &data[i], 1);
	  fprintf (e, "\"");
	  if (has_next)
		  fprintf (e, ",");
		fprintf (e, "\n");
		first = 0;
	      }
    }
  if (hasblock)
    fprintf (e, "END\n");
}

/* Write out an rcdata resource.  This is also used for other types of
   resources that need to print arbitrary data.  */

static void
write_rc_rcdata (FILE *e, const rc_rcdata_item *rcdata, int ind)
{
  const rc_rcdata_item *ri;

  indent (e, ind);
  fprintf (e, "BEGIN\n");

  for (ri = rcdata; ri != NULL; ri = ri->next)
    {
      if (ri->type == RCDATA_BUFFER && ri->u.buffer.length == 0)
	continue;

      switch (ri->type)
	{
	default:
	  abort ();

	case RCDATA_WORD:
	  indent (e, ind + 2);
	  fprintf (e, "%ld", (long) (ri->u.word & 0xffff));
	  break;

	case RCDATA_DWORD:
	  indent (e, ind + 2);
	  fprintf (e, "%luL", (unsigned long) ri->u.dword);
	  break;

	case RCDATA_STRING:
	  indent (e, ind + 2);
	  fprintf (e, "\"");
	  ascii_print (e, ri->u.string.s, ri->u.string.length);
	  fprintf (e, "\"");
	  break;

	case RCDATA_WSTRING:
	  indent (e, ind + 2);
	  fprintf (e, "L\"");
	  unicode_print (e, ri->u.wstring.w, ri->u.wstring.length);
	  fprintf (e, "\"");
	  break;

	case RCDATA_BUFFER:
	  write_rc_datablock (e, (rc_uint_type) ri->u.buffer.length,
	  		      (const bfd_byte *) ri->u.buffer.data,
	    		      ri->next != NULL, 0, -1);
	    break;
	}

      if (ri->type != RCDATA_BUFFER)
	{
	  if (ri->next != NULL)
	    fprintf (e, ",");
	  fprintf (e, "\n");
	}
    }

  indent (e, ind);
  fprintf (e, "END\n");
}

/* Write out a stringtable resource.  */

static void
write_rc_stringtable (FILE *e, const rc_res_id *name,
		      const rc_stringtable *stringtable)
{
  rc_uint_type offset;
  int i;

  if (name != NULL && ! name->named)
    offset = (name->u.id - 1) << 4;
  else
    {
      fprintf (e, "/* %s string table name.  */\n",
	       name == NULL ? "Missing" : "Invalid");
      offset = 0;
    }

  fprintf (e, "BEGIN\n");

  for (i = 0; i < 16; i++)
    {
      if (stringtable->strings[i].length != 0)
	{
	  fprintf (e, "  %lu, ", (long) offset + i);
	  unicode_print_quoted (e, stringtable->strings[i].string,
			 stringtable->strings[i].length);
	  fprintf (e, "\n");
	}
    }

  fprintf (e, "END\n");
}

/* Write out a versioninfo resource.  */

static void
write_rc_versioninfo (FILE *e, const rc_versioninfo *versioninfo)
{
  const rc_fixed_versioninfo *f;
  const rc_ver_info *vi;

  f = versioninfo->fixed;
  if (f->file_version_ms != 0 || f->file_version_ls != 0)
    fprintf (e, " FILEVERSION %u, %u, %u, %u\n",
	     (unsigned int) ((f->file_version_ms >> 16) & 0xffff),
	     (unsigned int) (f->file_version_ms & 0xffff),
	     (unsigned int) ((f->file_version_ls >> 16) & 0xffff),
	     (unsigned int) (f->file_version_ls & 0xffff));
  if (f->product_version_ms != 0 || f->product_version_ls != 0)
    fprintf (e, " PRODUCTVERSION %u, %u, %u, %u\n",
	     (unsigned int) ((f->product_version_ms >> 16) & 0xffff),
	     (unsigned int) (f->product_version_ms & 0xffff),
	     (unsigned int) ((f->product_version_ls >> 16) & 0xffff),
	     (unsigned int) (f->product_version_ls & 0xffff));
  if (f->file_flags_mask != 0)
    fprintf (e, " FILEFLAGSMASK 0x%x\n", (unsigned int) f->file_flags_mask);
  if (f->file_flags != 0)
    fprintf (e, " FILEFLAGS 0x%x\n", (unsigned int) f->file_flags);
  if (f->file_os != 0)
    fprintf (e, " FILEOS 0x%x\n", (unsigned int) f->file_os);
  if (f->file_type != 0)
    fprintf (e, " FILETYPE 0x%x\n", (unsigned int) f->file_type);
  if (f->file_subtype != 0)
    fprintf (e, " FILESUBTYPE 0x%x\n", (unsigned int) f->file_subtype);
  if (f->file_date_ms != 0 || f->file_date_ls != 0)
    fprintf (e, "/* Date: %u, %u.  */\n",
    	     (unsigned int) f->file_date_ms, (unsigned int) f->file_date_ls);

  fprintf (e, "BEGIN\n");

  for (vi = versioninfo->var; vi != NULL; vi = vi->next)
    {
      switch (vi->type)
	{
	case VERINFO_STRING:
	  {
	    const rc_ver_stringinfo *vs;

	    fprintf (e, "  BLOCK \"StringFileInfo\"\n");
	    fprintf (e, "  BEGIN\n");
	    fprintf (e, "    BLOCK ");
	    unicode_print_quoted (e, vi->u.string.language, -1);
	    fprintf (e, "\n");
	    fprintf (e, "    BEGIN\n");

	    for (vs = vi->u.string.strings; vs != NULL; vs = vs->next)
	      {
		fprintf (e, "      VALUE ");
		unicode_print_quoted (e, vs->key, -1);
		fprintf (e, ", ");
		unicode_print_quoted (e, vs->value, -1);
		fprintf (e, "\n");
	      }

	    fprintf (e, "    END\n");
	    fprintf (e, "  END\n");
	    break;
	  }

	case VERINFO_VAR:
	  {
	    const rc_ver_varinfo *vv;

	    fprintf (e, "  BLOCK \"VarFileInfo\"\n");
	    fprintf (e, "  BEGIN\n");
	    fprintf (e, "    VALUE ");
	    unicode_print_quoted (e, vi->u.var.key, -1);

	    for (vv = vi->u.var.var; vv != NULL; vv = vv->next)
	      fprintf (e, ", 0x%x, %d", (unsigned int) vv->language,
		       (int) vv->charset);

	    fprintf (e, "\n  END\n");

	    break;
	  }
	}
    }

  fprintf (e, "END\n");
}

static rc_uint_type
rcdata_copy (const rc_rcdata_item *src, bfd_byte *dst)
{
  if (! src)
    return 0;
  switch (src->type)
	{
    case RCDATA_WORD:
      if (dst)
	windres_put_16 (&wrtarget, dst, (rc_uint_type) src->u.word);
      return 2;
    case RCDATA_DWORD:
      if (dst)
	windres_put_32 (&wrtarget, dst, (rc_uint_type) src->u.dword);
      return 4;
    case RCDATA_STRING:
      if (dst && src->u.string.length)
	memcpy (dst, src->u.string.s, src->u.string.length);
      return (rc_uint_type) src->u.string.length;
    case RCDATA_WSTRING:
      if (dst && src->u.wstring.length)
	memcpy (dst, src->u.wstring.w, src->u.wstring.length * sizeof (unichar));
      return (rc_uint_type) (src->u.wstring.length  * sizeof (unichar));
    case RCDATA_BUFFER:
      if (dst && src->u.buffer.length)
	memcpy (dst, src->u.buffer.data, src->u.buffer.length);
      return (rc_uint_type) src->u.buffer.length;
    default:
      abort ();
    }
  /* Never reached.  */
  return 0;
}
