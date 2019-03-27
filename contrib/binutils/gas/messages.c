/* messages.c - error reporter -
   Copyright 1987, 1991, 1992, 1993, 1994, 1995, 1996, 1998, 2000, 2001,
   2003, 2004, 2005, 2006, 2007
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

static void identify (char *);
static void as_show_where (void);
static void as_warn_internal (char *, unsigned int, char *);
static void as_bad_internal (char *, unsigned int, char *);

/* Despite the rest of the comments in this file, (FIXME-SOON),
   here is the current scheme for error messages etc:

   as_fatal() is used when gas is quite confused and
   continuing the assembly is pointless.  In this case we
   exit immediately with error status.

   as_bad() is used to mark errors that result in what we
   presume to be a useless object file.  Say, we ignored
   something that might have been vital.  If we see any of
   these, assembly will continue to the end of the source,
   no object file will be produced, and we will terminate
   with error status.  The new option, -Z, tells us to
   produce an object file anyway but we still exit with
   error status.  The assumption here is that you don't want
   this object file but we could be wrong.

   as_warn() is used when we have an error from which we
   have a plausible error recovery.  eg, masking the top
   bits of a constant that is longer than will fit in the
   destination.  In this case we will continue to assemble
   the source, although we may have made a bad assumption,
   and we will produce an object file and return normal exit
   status (ie, no error).  The new option -X tells us to
   treat all as_warn() errors as as_bad() errors.  That is,
   no object file will be produced and we will exit with
   error status.  The idea here is that we don't kill an
   entire make because of an error that we knew how to
   correct.  On the other hand, sometimes you might want to
   stop the make at these points.

   as_tsktsk() is used when we see a minor error for which
   our error recovery action is almost certainly correct.
   In this case, we print a message and then assembly
   continues as though no error occurred.  */

static void
identify (char *file)
{
  static int identified;

  if (identified)
    return;
  identified++;

  if (!file)
    {
      unsigned int x;
      as_where (&file, &x);
    }

  if (file)
    fprintf (stderr, "%s: ", file);
  fprintf (stderr, _("Assembler messages:\n"));
}

/* The number of warnings issued.  */
static int warning_count;

int
had_warnings (void)
{
  return warning_count;
}

/* Nonzero if we've hit a 'bad error', and should not write an obj file,
   and exit with a nonzero error code.  */

static int error_count;

int
had_errors (void)
{
  return error_count;
}

/* Print the current location to stderr.  */

static void
as_show_where (void)
{
  char *file;
  unsigned int line;

  as_where (&file, &line);
  identify (file);
  if (file)
    fprintf (stderr, "%s:%u: ", file, line);
}

/* Send to stderr a string as a warning, and locate warning
   in input file(s).
   Please only use this for when we have some recovery action.
   Please explain in string (which may have '\n's) what recovery was
   done.  */

#ifdef USE_STDARG
void
as_tsktsk (const char *format, ...)
{
  va_list args;

  as_show_where ();
  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);
  (void) putc ('\n', stderr);
}
#else
void
as_tsktsk (format, va_alist)
     const char *format;
     va_dcl
{
  va_list args;

  as_show_where ();
  va_start (args);
  vfprintf (stderr, format, args);
  va_end (args);
  (void) putc ('\n', stderr);
}
#endif /* not NO_STDARG */

/* The common portion of as_warn and as_warn_where.  */

static void
as_warn_internal (char *file, unsigned int line, char *buffer)
{
  ++warning_count;

  if (file == NULL)
    as_where (&file, &line);

  identify (file);
  if (file)
    fprintf (stderr, "%s:%u: ", file, line);
  fprintf (stderr, _("Warning: "));
  fputs (buffer, stderr);
  (void) putc ('\n', stderr);
#ifndef NO_LISTING
  listing_warning (buffer);
#endif
}

/* Send to stderr a string as a warning, and locate warning
   in input file(s).
   Please only use this for when we have some recovery action.
   Please explain in string (which may have '\n's) what recovery was
   done.  */

#ifdef USE_STDARG
void
as_warn (const char *format, ...)
{
  va_list args;
  char buffer[2000];

  if (!flag_no_warnings)
    {
      va_start (args, format);
      vsnprintf (buffer, sizeof (buffer), format, args);
      va_end (args);
      as_warn_internal ((char *) NULL, 0, buffer);
    }
}
#else
void
as_warn (format, va_alist)
     const char *format;
     va_dcl
{
  va_list args;
  char buffer[2000];

  if (!flag_no_warnings)
    {
      va_start (args);
      vsnprintf (buffer, sizeof (buffer), format, args);
      va_end (args);
      as_warn_internal ((char *) NULL, 0, buffer);
    }
}
#endif /* not NO_STDARG */

/* Like as_bad but the file name and line number are passed in.
   Unfortunately, we have to repeat the function in order to handle
   the varargs correctly and portably.  */

#ifdef USE_STDARG
void
as_warn_where (char *file, unsigned int line, const char *format, ...)
{
  va_list args;
  char buffer[2000];

  if (!flag_no_warnings)
    {
      va_start (args, format);
      vsnprintf (buffer, sizeof (buffer), format, args);
      va_end (args);
      as_warn_internal (file, line, buffer);
    }
}
#else
void
as_warn_where (file, line, format, va_alist)
     char *file;
     unsigned int line;
     const char *format;
     va_dcl
{
  va_list args;
  char buffer[2000];

  if (!flag_no_warnings)
    {
      va_start (args);
      vsnprintf (buffer, sizeof (buffer), format, args);
      va_end (args);
      as_warn_internal (file, line, buffer);
    }
}
#endif /* not NO_STDARG */

/* The common portion of as_bad and as_bad_where.  */

static void
as_bad_internal (char *file, unsigned int line, char *buffer)
{
  ++error_count;

  if (file == NULL)
    as_where (&file, &line);

  identify (file);
  if (file)
    fprintf (stderr, "%s:%u: ", file, line);
  fprintf (stderr, _("Error: "));
  fputs (buffer, stderr);
  (void) putc ('\n', stderr);
#ifndef NO_LISTING
  listing_error (buffer);
#endif
}

/* Send to stderr a string as a warning, and locate warning in input
   file(s).  Please us when there is no recovery, but we want to
   continue processing but not produce an object file.
   Please explain in string (which may have '\n's) what recovery was
   done.  */

#ifdef USE_STDARG
void
as_bad (const char *format, ...)
{
  va_list args;
  char buffer[2000];

  va_start (args, format);
  vsnprintf (buffer, sizeof (buffer), format, args);
  va_end (args);

  as_bad_internal ((char *) NULL, 0, buffer);
}

#else
void
as_bad (format, va_alist)
     const char *format;
     va_dcl
{
  va_list args;
  char buffer[2000];

  va_start (args);
  vsnprintf (buffer, sizeof (buffer), format, args);
  va_end (args);

  as_bad_internal ((char *) NULL, 0, buffer);
}
#endif /* not NO_STDARG */

/* Like as_bad but the file name and line number are passed in.
   Unfortunately, we have to repeat the function in order to handle
   the varargs correctly and portably.  */

#ifdef USE_STDARG
void
as_bad_where (char *file, unsigned int line, const char *format, ...)
{
  va_list args;
  char buffer[2000];

  va_start (args, format);
  vsnprintf (buffer, sizeof (buffer), format, args);
  va_end (args);

  as_bad_internal (file, line, buffer);
}

#else
void
as_bad_where (file, line, format, va_alist)
     char *file;
     unsigned int line;
     const char *format;
     va_dcl
{
  va_list args;
  char buffer[2000];

  va_start (args);
  vsnprintf (buffer, sizeof (buffer), format, args);
  va_end (args);

  as_bad_internal (file, line, buffer);
}
#endif /* not NO_STDARG */

/* Send to stderr a string as a fatal message, and print location of
   error in input file(s).
   Please only use this for when we DON'T have some recovery action.
   It xexit()s with a warning status.  */

#ifdef USE_STDARG
void
as_fatal (const char *format, ...)
{
  va_list args;

  as_show_where ();
  va_start (args, format);
  fprintf (stderr, _("Fatal error: "));
  vfprintf (stderr, format, args);
  (void) putc ('\n', stderr);
  va_end (args);
  /* Delete the output file, if it exists.  This will prevent make from
     thinking that a file was created and hence does not need rebuilding.  */
  if (out_file_name != NULL)
    unlink_if_ordinary (out_file_name);
  xexit (EXIT_FAILURE);
}
#else
void
as_fatal (format, va_alist)
     char *format;
     va_dcl
{
  va_list args;

  as_show_where ();
  va_start (args);
  fprintf (stderr, _("Fatal error: "));
  vfprintf (stderr, format, args);
  (void) putc ('\n', stderr);
  va_end (args);
  xexit (EXIT_FAILURE);
}
#endif /* not NO_STDARG */

/* Indicate assertion failure.
   Arguments: Filename, line number, optional function name.  */

void
as_assert (const char *file, int line, const char *fn)
{
  as_show_where ();
  fprintf (stderr, _("Internal error!\n"));
  if (fn)
    fprintf (stderr, _("Assertion failure in %s at %s line %d.\n"),
	     fn, file, line);
  else
    fprintf (stderr, _("Assertion failure at %s line %d.\n"), file, line);
  fprintf (stderr, _("Please report this bug.\n"));
  xexit (EXIT_FAILURE);
}

/* as_abort: Print a friendly message saying how totally hosed we are,
   and exit without producing a core file.  */

void
as_abort (const char *file, int line, const char *fn)
{
  as_show_where ();
  if (fn)
    fprintf (stderr, _("Internal error, aborting at %s line %d in %s\n"),
	     file, line, fn);
  else
    fprintf (stderr, _("Internal error, aborting at %s line %d\n"),
	     file, line);
  fprintf (stderr, _("Please report this bug.\n"));
  xexit (EXIT_FAILURE);
}

/* Support routines.  */

void
sprint_value (char *buf, valueT val)
{
  if (sizeof (val) <= sizeof (long))
    {
      sprintf (buf, "%ld", (long) val);
      return;
    }
  if (sizeof (val) <= sizeof (bfd_vma))
    {
      sprintf_vma (buf, val);
      return;
    }
  abort ();
}

#define HEX_MAX_THRESHOLD	1024
#define HEX_MIN_THRESHOLD	-(HEX_MAX_THRESHOLD)

static void
as_internal_value_out_of_range (char *    prefix,
				offsetT   val,
				offsetT   min,
				offsetT   max,
				char *    file,
				unsigned  line,
				int       bad)
{
  const char * err;

  if (prefix == NULL)
    prefix = "";

  if (val >= min && val <= max)
    {
      addressT right = max & -max;

      if (max <= 1)
	abort ();

      /* xgettext:c-format  */
      err = _("%s out of domain (%d is not a multiple of %d)");
      if (bad)
	as_bad_where (file, line, err,
		      prefix, (int) val, (int) right);
      else
	as_warn_where (file, line, err,
		       prefix, (int) val, (int) right);
      return;
    }

  if (   val < HEX_MAX_THRESHOLD
      && min < HEX_MAX_THRESHOLD
      && max < HEX_MAX_THRESHOLD
      && val > HEX_MIN_THRESHOLD
      && min > HEX_MIN_THRESHOLD
      && max > HEX_MIN_THRESHOLD)
    {
      /* xgettext:c-format  */
      err = _("%s out of range (%d is not between %d and %d)");

      if (bad)
	as_bad_where (file, line, err,
		      prefix, (int) val, (int) min, (int) max);
      else
	as_warn_where (file, line, err,
		       prefix, (int) val, (int) min, (int) max);
    }
  else
    {
      char val_buf [sizeof (val) * 3 + 2];
      char min_buf [sizeof (val) * 3 + 2];
      char max_buf [sizeof (val) * 3 + 2];

      if (sizeof (val) > sizeof (bfd_vma))
	abort ();

      sprintf_vma (val_buf, val);
      sprintf_vma (min_buf, min);
      sprintf_vma (max_buf, max);

      /* xgettext:c-format.  */
      err = _("%s out of range (0x%s is not between 0x%s and 0x%s)");

      if (bad)
	as_bad_where (file, line, err, prefix, val_buf, min_buf, max_buf);
      else
	as_warn_where (file, line, err, prefix, val_buf, min_buf, max_buf);
    }
}

void
as_warn_value_out_of_range (char *   prefix,
			   offsetT  value,
			   offsetT  min,
			   offsetT  max,
			   char *   file,
			   unsigned line)
{
  as_internal_value_out_of_range (prefix, value, min, max, file, line, 0);
}

void
as_bad_value_out_of_range (char *   prefix,
			   offsetT  value,
			   offsetT  min,
			   offsetT  max,
			   char *   file,
			   unsigned line)
{
  as_internal_value_out_of_range (prefix, value, min, max, file, line, 1);
}
