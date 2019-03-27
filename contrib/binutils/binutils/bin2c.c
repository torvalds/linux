/* bin2c.c -- dump binary file in hex format
   Copyright 2007 Free Software Foundation, Inc.

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

#include "sysdep.h"
#include "bfd.h"
#include "bucomm.h"

#if !defined O_BINARY && defined _O_BINARY
  /* For MSC-compatible compilers.  */
# define O_BINARY _O_BINARY
# define O_TEXT _O_TEXT
#endif

#ifdef __BEOS__
  /* BeOS 5 has O_BINARY and O_TEXT, but they have no effect.  */
# undef O_BINARY
# undef O_TEXT
#endif

#if O_BINARY
# ifndef __DJGPP__
#  define setmode _setmode
#  define fileno(_fp) _fileno (_fp)
# endif /* not DJGPP */
# define SET_BINARY(_f) \
  do { if (!isatty (_f)) setmode (_f, O_BINARY); } while (0)
#else
# define SET_BINARY(f) (void) 0
# define O_BINARY 0
# define O_TEXT 0
#endif /* O_BINARY */

int
main (int argc, char *argv[])
{
  int c;
  int i;

#if defined (HAVE_SETLOCALE) && defined (HAVE_LC_MESSAGES)
  setlocale (LC_MESSAGES, "");
#endif
#if defined (HAVE_SETLOCALE)
  setlocale (LC_CTYPE, "");
#endif
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  if (argc != 1)
    {
      int ishelp = 0;
      int isvers = 0;
      FILE *stream;

      if (argc == 2 && argv[1][0] == '-')
	{
	  const char *opt = &argv[1][1];
	  if (*opt == '-')
	    ++opt;
	  ishelp = *opt == 'h' || *opt == 'H';
	  isvers = *opt == 'v' || *opt == 'V';
	}

      if (isvers)
	print_version ("bin2c");

      stream = ishelp ? stdout : stderr;
      fprintf (stream, _("Usage: %s < input_file > output_file\n"), argv[0]);
      fprintf (stream, _("Prints bytes from stdin in hex format.\n"));
      exit (!ishelp);
    }

  SET_BINARY (fileno (stdin));

  i = 0;
  while ((c = getc (stdin)) != EOF)
    {
      printf ("0x%02x,", c);
      if (++i == 16)
	{
	  printf ("\n");
	  i = 0;
	}
    }
  if (i != 0)
    printf ("\n");

  exit (0);
}
