/* Precompiled header implementation for the C languages.
   Copyright (C) 2000, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "version.h"
#include "cpplib.h"
#include "tree.h"
#include "flags.h"
#include "c-common.h"
#include "output.h"
#include "toplev.h"
#include "debug.h"
#include "c-pragma.h"
#include "ggc.h"
#include "langhooks.h"
#include "hosthooks.h"
#include "target.h"

/* This is a list of flag variables that must match exactly, and their
   names for the error message.  The possible values for *flag_var must
   fit in a 'signed char'.  */

static const struct c_pch_matching
{
  int *flag_var;
  const char *flag_name;
} pch_matching[] = {
  { &flag_exceptions, "-fexceptions" },
  { &flag_unit_at_a_time, "-funit-at-a-time" }
};

enum {
  MATCH_SIZE = ARRAY_SIZE (pch_matching)
};

/* The value of the checksum in the dummy compiler that is actually
   checksummed.  That compiler should never be run.  */
static const char no_checksum[16] = { 0 };

/* Information about flags and suchlike that affect PCH validity.

   Before this structure is read, both an initial 8-character identification
   string, and a 16-byte checksum, have been read and validated.  */

struct c_pch_validity
{
  unsigned char debug_info_type;
  signed char match[MATCH_SIZE];
  void (*pch_init) (void);
  size_t target_data_length;
};

struct c_pch_header
{
  unsigned long asm_size;
};

#define IDENT_LENGTH 8

/* The file we'll be writing the PCH to.  */
static FILE *pch_outfile;

/* The position in the assembler output file when pch_init was called.  */
static long asm_file_startpos;

static const char *get_ident (void);

/* Compute an appropriate 8-byte magic number for the PCH file, so that
   utilities like file(1) can identify it, and so that GCC can quickly
   ignore non-PCH files and PCH files that are of a completely different
   format.  */

static const char *
get_ident (void)
{
  static char result[IDENT_LENGTH];
  static const char template[IDENT_LENGTH] = "gpch.013";
  static const char c_language_chars[] = "Co+O";

  memcpy (result, template, IDENT_LENGTH);
  result[4] = c_language_chars[c_language];

  return result;
}

/* Prepare to write a PCH file, if one is being written.  This is
   called at the start of compilation.

   Also, print out the executable checksum if -fverbose-asm is in effect.  */

void
pch_init (void)
{
  FILE *f;
  struct c_pch_validity v;
  void *target_validity;
  static const char partial_pch[IDENT_LENGTH] = "gpcWrite";

#ifdef ASM_COMMENT_START
  if (flag_verbose_asm)
    {
      fprintf (asm_out_file, "%s ", ASM_COMMENT_START);
      c_common_print_pch_checksum (asm_out_file);
      fputc ('\n', asm_out_file);
    }
#endif

  if (!pch_file)
    return;

  f = fopen (pch_file, "w+b");
  if (f == NULL)
    fatal_error ("can%'t create precompiled header %s: %m", pch_file);
  pch_outfile = f;

  gcc_assert (memcmp (executable_checksum, no_checksum, 16) != 0);

  v.debug_info_type = write_symbols;
  {
    size_t i;
    for (i = 0; i < MATCH_SIZE; i++)
      {
	v.match[i] = *pch_matching[i].flag_var;
	gcc_assert (v.match[i] == *pch_matching[i].flag_var);
      }
  }
  v.pch_init = &pch_init;
  target_validity = targetm.get_pch_validity (&v.target_data_length);

  if (fwrite (partial_pch, IDENT_LENGTH, 1, f) != 1
      || fwrite (executable_checksum, 16, 1, f) != 1
      || fwrite (&v, sizeof (v), 1, f) != 1
      || fwrite (target_validity, v.target_data_length, 1, f) != 1)
    fatal_error ("can%'t write to %s: %m", pch_file);

  /* We need to be able to re-read the output.  */
  /* The driver always provides a valid -o option.  */
  if (asm_file_name == NULL
      || strcmp (asm_file_name, "-") == 0)
    fatal_error ("%qs is not a valid output file", asm_file_name);

  asm_file_startpos = ftell (asm_out_file);

  /* Let the debugging format deal with the PCHness.  */
  (*debug_hooks->handle_pch) (0);

  cpp_save_state (parse_in, f);
}

/* Write the PCH file.  This is called at the end of a compilation which
   will produce a PCH file.  */

void
c_common_write_pch (void)
{
  char *buf;
  long asm_file_end;
  long written;
  struct c_pch_header h;

  (*debug_hooks->handle_pch) (1);

  cpp_write_pch_deps (parse_in, pch_outfile);

  asm_file_end = ftell (asm_out_file);
  h.asm_size = asm_file_end - asm_file_startpos;

  if (fwrite (&h, sizeof (h), 1, pch_outfile) != 1)
    fatal_error ("can%'t write %s: %m", pch_file);

  buf = XNEWVEC (char, 16384);

  if (fseek (asm_out_file, asm_file_startpos, SEEK_SET) != 0)
    fatal_error ("can%'t seek in %s: %m", asm_file_name);

  for (written = asm_file_startpos; written < asm_file_end; )
    {
      long size = asm_file_end - written;
      if (size > 16384)
	size = 16384;
      if (fread (buf, size, 1, asm_out_file) != 1)
	fatal_error ("can%'t read %s: %m", asm_file_name);
      if (fwrite (buf, size, 1, pch_outfile) != 1)
	fatal_error ("can%'t write %s: %m", pch_file);
      written += size;
    }
  free (buf);
  /* asm_out_file can be written afterwards, so fseek to clear
     _IOREAD flag.  */
  if (fseek (asm_out_file, 0, SEEK_END) != 0)
    fatal_error ("can%'t seek in %s: %m", asm_file_name);

  gt_pch_save (pch_outfile);
  cpp_write_pch_state (parse_in, pch_outfile);

  if (fseek (pch_outfile, 0, SEEK_SET) != 0
      || fwrite (get_ident (), IDENT_LENGTH, 1, pch_outfile) != 1)
    fatal_error ("can%'t write %s: %m", pch_file);

  fclose (pch_outfile);
}

/* Check the PCH file called NAME, open on FD, to see if it can be
   used in this compilation.  Return 1 if valid, 0 if the file can't
   be used now but might be if it's seen later in the compilation, and
   2 if this file could never be used in the compilation.  */

int
c_common_valid_pch (cpp_reader *pfile, const char *name, int fd)
{
  int sizeread;
  int result;
  char ident[IDENT_LENGTH + 16];
  const char *pch_ident;
  struct c_pch_validity v;

  /* Perform a quick test of whether this is a valid
     precompiled header for the current language.  */

  gcc_assert (memcmp (executable_checksum, no_checksum, 16) != 0);

  sizeread = read (fd, ident, IDENT_LENGTH + 16);
  if (sizeread == -1)
    fatal_error ("can%'t read %s: %m", name);
  else if (sizeread != IDENT_LENGTH + 16)
    {
      cpp_error (pfile, CPP_DL_WARNING, "%s: too short to be a PCH file",
		 name);
      return 2;
    }

  pch_ident = get_ident();
  if (memcmp (ident, pch_ident, IDENT_LENGTH) != 0)
    {
      if (cpp_get_options (pfile)->warn_invalid_pch)
	{
	  if (memcmp (ident, pch_ident, 5) == 0)
	    /* It's a PCH, for the right language, but has the wrong version.
	     */
	    cpp_error (pfile, CPP_DL_WARNING,
		       "%s: not compatible with this GCC version", name);
	  else if (memcmp (ident, pch_ident, 4) == 0)
	    /* It's a PCH for the wrong language.  */
	    cpp_error (pfile, CPP_DL_WARNING, "%s: not for %s", name,
		       lang_hooks.name);
	  else
	    /* Not any kind of PCH.  */
	    cpp_error (pfile, CPP_DL_WARNING, "%s: not a PCH file", name);
	}
      return 2;
    }
  if (memcmp (ident + IDENT_LENGTH, executable_checksum, 16) != 0)
    {
      if (cpp_get_options (pfile)->warn_invalid_pch)
	cpp_error (pfile, CPP_DL_WARNING,
		   "%s: created by a different GCC executable", name);
      return 2;
    }

  /* At this point, we know it's a PCH file created by this
     executable, so it ought to be long enough that we can read a
     c_pch_validity structure.  */
  if (read (fd, &v, sizeof (v)) != sizeof (v))
    fatal_error ("can%'t read %s: %m", name);

  /* The allowable debug info combinations are that either the PCH file
     was built with the same as is being used now, or the PCH file was
     built for some kind of debug info but now none is in use.  */
  if (v.debug_info_type != write_symbols
      && write_symbols != NO_DEBUG)
    {
      if (cpp_get_options (pfile)->warn_invalid_pch)
	cpp_error (pfile, CPP_DL_WARNING,
		   "%s: created with -g%s, but used with -g%s", name,
		   debug_type_names[v.debug_info_type],
		   debug_type_names[write_symbols]);
      return 2;
    }

  /* Check flags that must match exactly.  */
  {
    size_t i;
    for (i = 0; i < MATCH_SIZE; i++)
      if (*pch_matching[i].flag_var != v.match[i])
	{
	  if (cpp_get_options (pfile)->warn_invalid_pch)
	    cpp_error (pfile, CPP_DL_WARNING,
		       "%s: settings for %s do not match", name,
		       pch_matching[i].flag_name);
	  return 2;
	}
  }

  /* If the text segment was not loaded at the same address as it was
     when the PCH file was created, function pointers loaded from the
     PCH will not be valid.  We could in theory remap all the function
     pointers, but no support for that exists at present.
     Since we have the same executable, it should only be necessary to
     check one function.  */
  if (v.pch_init != &pch_init)
    {
      if (cpp_get_options (pfile)->warn_invalid_pch)
	cpp_error (pfile, CPP_DL_WARNING,
		   "%s: had text segment at different address", name);
      return 2;
    }

  /* Check the target-specific validity data.  */
  {
    void *this_file_data = xmalloc (v.target_data_length);
    const char *msg;

    if ((size_t) read (fd, this_file_data, v.target_data_length)
	!= v.target_data_length)
      fatal_error ("can%'t read %s: %m", name);
    msg = targetm.pch_valid_p (this_file_data, v.target_data_length);
    free (this_file_data);
    if (msg != NULL)
      {
	if (cpp_get_options (pfile)->warn_invalid_pch)
	  cpp_error (pfile, CPP_DL_WARNING, "%s: %s", name, msg);
	return 2;
      }
  }

  /* Check the preprocessor macros are the same as when the PCH was
     generated.  */

  result = cpp_valid_state (pfile, name, fd);
  if (result == -1)
    return 2;
  else
    return result == 0;
}

/* If non-NULL, this function is called after a precompile header file
   is loaded.  */
void (*lang_post_pch_load) (void);

/* Load in the PCH file NAME, open on FD.  It was originally searched for
   by ORIG_NAME.  */

void
c_common_read_pch (cpp_reader *pfile, const char *name,
		   int fd, const char *orig_name ATTRIBUTE_UNUSED)
{
  FILE *f;
  struct c_pch_header h;
  struct save_macro_data *smd;

  f = fdopen (fd, "rb");
  if (f == NULL)
    {
      cpp_errno (pfile, CPP_DL_ERROR, "calling fdopen");
      return;
    }

  cpp_get_callbacks (parse_in)->valid_pch = NULL;

  if (fread (&h, sizeof (h), 1, f) != 1)
    {
      cpp_errno (pfile, CPP_DL_ERROR, "reading");
      return;
    }

  if (!flag_preprocess_only)
    {
      unsigned long written;
      char * buf = XNEWVEC (char, 16384);

      for (written = 0; written < h.asm_size; )
	{
	  long size = h.asm_size - written;
	  if (size > 16384)
	    size = 16384;
	  if (fread (buf, size, 1, f) != 1
	      || fwrite (buf, size, 1, asm_out_file) != 1)
	    cpp_errno (pfile, CPP_DL_ERROR, "reading");
	  written += size;
	}
      free (buf);
    }
  else
    {
      /* If we're preprocessing, don't write to a NULL
	 asm_out_file.  */
      if (fseek (f, h.asm_size, SEEK_CUR) != 0)
	cpp_errno (pfile, CPP_DL_ERROR, "seeking");
    }

  cpp_prepare_state (pfile, &smd);

  gt_pch_restore (f);

  if (cpp_read_state (pfile, name, f, smd) != 0)
    return;

  fclose (f);

  /* Give the front end a chance to take action after a PCH file has
     been loaded.  */
  if (lang_post_pch_load)
    (*lang_post_pch_load) ();
}

/* Indicate that no more PCH files should be read.  */

void
c_common_no_more_pch (void)
{
  if (cpp_get_callbacks (parse_in)->valid_pch)
    {
      cpp_get_callbacks (parse_in)->valid_pch = NULL;
      host_hooks.gt_pch_use_address (NULL, 0, -1, 0);
    }
}

/* Handle #pragma GCC pch_preprocess, to load in the PCH file.  */

#ifndef O_BINARY
# define O_BINARY 0
#endif

void
c_common_pch_pragma (cpp_reader *pfile, const char *name)
{
  int fd;

  if (!cpp_get_options (pfile)->preprocessed)
    {
      error ("pch_preprocess pragma should only be used with -fpreprocessed");
      inform ("use #include instead");
      return;
    }

  fd = open (name, O_RDONLY | O_BINARY, 0666);
  if (fd == -1)
    fatal_error ("%s: couldn%'t open PCH file: %m", name);

  if (c_common_valid_pch (pfile, name, fd) != 1)
    {
      if (!cpp_get_options (pfile)->warn_invalid_pch)
	inform ("use -Winvalid-pch for more information");
      fatal_error ("%s: PCH file was invalid", name);
    }

  c_common_read_pch (pfile, name, fd, name);

  close (fd);
}

/* Print out executable_checksum[].  */

void
c_common_print_pch_checksum (FILE *f)
{
  int i;
  fputs ("Compiler executable checksum: ", f);
  for (i = 0; i < 16; i++)
    fprintf (f, "%02x", executable_checksum[i]);
  putc ('\n', f);
}
