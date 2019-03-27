/* Error handler for noninteractive utilities
   Copyright (C) 1990-1998, 2000-2002, 2003 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Written by David MacKenzie <djm@gnu.ai.mit.edu>.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "error.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _LIBC
# include <libintl.h>
#else
# include "gettext.h"
#endif

#ifdef _LIBC
# include <wchar.h>
# define mbsrtowcs __mbsrtowcs
#endif

#if !_LIBC
# include "unlocked-io.h"
#endif

#ifndef _
# define _(String) String
#endif

/* If NULL, error will flush stdout, then print on stderr the program
   name, a colon and a space.  Otherwise, error will call this
   function without parameters instead.  */
void (*error_print_progname) (void);

/* This variable is incremented each time `error' is called.  */
unsigned int error_message_count;

#ifdef _LIBC
/* In the GNU C library, there is a predefined variable for this.  */

# define program_name program_invocation_name
# include <errno.h>
# include <libio/libioP.h>

/* In GNU libc we want do not want to use the common name `error' directly.
   Instead make it a weak alias.  */
extern void __error (int status, int errnum, const char *message, ...)
     __attribute__ ((__format__ (__printf__, 3, 4)));
extern void __error_at_line (int status, int errnum, const char *file_name,
			     unsigned int line_number, const char *message,
			     ...)
     __attribute__ ((__format__ (__printf__, 5, 6)));;
# define error __error
# define error_at_line __error_at_line

# include <libio/iolibio.h>
# define fflush(s) INTUSE(_IO_fflush) (s)
# undef putc
# define putc(c, fp) INTUSE(_IO_putc) (c, fp)

# include <bits/libc-lock.h>

#else /* not _LIBC */

# if !HAVE_DECL_STRERROR_R && STRERROR_R_CHAR_P
#  ifndef HAVE_DECL_STRERROR_R
"this configure-time declaration test was not run"
#  endif
char *strerror_r ();
# endif

# ifndef SIZE_MAX
#  define SIZE_MAX ((size_t) -1)
# endif

/* The calling program should define program_name and set it to the
   name of the executing program.  */
extern char *program_name;

# if HAVE_STRERROR_R || defined strerror_r
#  define __strerror_r strerror_r
# endif
#endif	/* not _LIBC */

static void
print_errno_message (int errnum)
{
  char const *s;

#if defined HAVE_STRERROR_R || _LIBC
  char errbuf[1024];
# if STRERROR_R_CHAR_P || _LIBC
  s = __strerror_r (errnum, errbuf, sizeof errbuf);
# else
  if (__strerror_r (errnum, errbuf, sizeof errbuf) == 0)
    s = errbuf;
  else
    s = 0;
# endif
#else
  s = strerror (errnum);
#endif

#if !_LIBC
  if (! s)
    s = _("Unknown system error");
#endif

#if _LIBC
  if (_IO_fwide (stderr, 0) > 0)
    {
      __fwprintf (stderr, L": %s", s);
      return;
    }
#endif

  fprintf (stderr, ": %s", s);
}

static void
error_tail (int status, int errnum, const char *message, va_list args)
{
#if _LIBC
  if (_IO_fwide (stderr, 0) > 0)
    {
# define ALLOCA_LIMIT 2000
      size_t len = strlen (message) + 1;
      const wchar_t *wmessage = L"out of memory";
      wchar_t *wbuf = (len < ALLOCA_LIMIT
		       ? alloca (len * sizeof *wbuf)
		       : len <= SIZE_MAX / sizeof *wbuf
		       ? malloc (len * sizeof *wbuf)
		       : NULL);

      if (wbuf)
	{
	  size_t res;
	  mbstate_t st;
	  const char *tmp = message;
	  memset (&st, '\0', sizeof (st));
	  res = mbsrtowcs (wbuf, &tmp, len, &st);
	  wmessage = res == (size_t) -1 ? L"???" : wbuf;
	}

      __vfwprintf (stderr, wmessage, args);
      if (! (len < ALLOCA_LIMIT))
	free (wbuf);
    }
  else
#endif
    vfprintf (stderr, message, args);
  va_end (args);

  ++error_message_count;
  if (errnum)
    print_errno_message (errnum);
#if _LIBC
  if (_IO_fwide (stderr, 0) > 0)
    putwc (L'\n', stderr);
  else
#endif
    putc ('\n', stderr);
  fflush (stderr);
  if (status)
    exit (status);
}


/* Print the program name and error message MESSAGE, which is a printf-style
   format string with optional args.
   If ERRNUM is nonzero, print its corresponding system error message.
   Exit with status STATUS if it is nonzero.  */
void
error (int status, int errnum, const char *message, ...)
{
  va_list args;

#if defined _LIBC && defined __libc_ptf_call
  /* We do not want this call to be cut short by a thread
     cancellation.  Therefore disable cancellation for now.  */
  int state = PTHREAD_CANCEL_ENABLE;
  __libc_ptf_call (pthread_setcancelstate, (PTHREAD_CANCEL_DISABLE, &state),
		   0);
#endif

  fflush (stdout);
#ifdef _LIBC
  _IO_flockfile (stderr);
#endif
  if (error_print_progname)
    (*error_print_progname) ();
  else
    {
#if _LIBC
      if (_IO_fwide (stderr, 0) > 0)
	__fwprintf (stderr, L"%s: ", program_name);
      else
#endif
	fprintf (stderr, "%s: ", program_name);
    }

  va_start (args, message);
  error_tail (status, errnum, message, args);

#ifdef _LIBC
  _IO_funlockfile (stderr);
# ifdef __libc_ptf_call
  __libc_ptf_call (pthread_setcancelstate, (state, NULL), 0);
# endif
#endif
}

/* Sometimes we want to have at most one error per line.  This
   variable controls whether this mode is selected or not.  */
int error_one_per_line;

void
error_at_line (int status, int errnum, const char *file_name,
	       unsigned int line_number, const char *message, ...)
{
  va_list args;

  if (error_one_per_line)
    {
      static const char *old_file_name;
      static unsigned int old_line_number;

      if (old_line_number == line_number
	  && (file_name == old_file_name
	      || strcmp (old_file_name, file_name) == 0))
	/* Simply return and print nothing.  */
	return;

      old_file_name = file_name;
      old_line_number = line_number;
    }

#if defined _LIBC && defined __libc_ptf_call
  /* We do not want this call to be cut short by a thread
     cancellation.  Therefore disable cancellation for now.  */
  int state = PTHREAD_CANCEL_ENABLE;
  __libc_ptf_call (pthread_setcancelstate, (PTHREAD_CANCEL_DISABLE, &state),
		   0);
#endif

  fflush (stdout);
#ifdef _LIBC
  _IO_flockfile (stderr);
#endif
  if (error_print_progname)
    (*error_print_progname) ();
  else
    {
#if _LIBC
      if (_IO_fwide (stderr, 0) > 0)
	__fwprintf (stderr, L"%s: ", program_name);
      else
#endif
	fprintf (stderr, "%s:", program_name);
    }

  if (file_name != NULL)
    {
#if _LIBC
      if (_IO_fwide (stderr, 0) > 0)
	__fwprintf (stderr, L"%s:%d: ", file_name, line_number);
      else
#endif
	fprintf (stderr, "%s:%d: ", file_name, line_number);
    }

  va_start (args, message);
  error_tail (status, errnum, message, args);

#ifdef _LIBC
  _IO_funlockfile (stderr);
# ifdef __libc_ptf_call
  __libc_ptf_call (pthread_setcancelstate, (state, NULL), 0);
# endif
#endif
}

#ifdef _LIBC
/* Make the weak alias.  */
# undef error
# undef error_at_line
weak_alias (__error, error)
weak_alias (__error_at_line, error_at_line)
#endif
