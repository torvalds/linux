/* General utility routines for the remote server for GDB.
   Copyright 1986, 1989, 1993, 1995, 1996, 1997, 1999, 2000, 2002, 2003
   Free Software Foundation, Inc.

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

#include "server.h"
#include <stdio.h>
#include <string.h>

/* Generally useful subroutines used throughout the program.  */

/* Print the system error message for errno, and also mention STRING
   as the file name for which the error was encountered.
   Then return to command level.  */

void
perror_with_name (char *string)
{
#ifndef STDC_HEADERS
  extern int errno;
#endif
  const char *err;
  char *combined;

  err = strerror (errno);
  if (err == NULL)
    err = "unknown error";

  combined = (char *) alloca (strlen (err) + strlen (string) + 3);
  strcpy (combined, string);
  strcat (combined, ": ");
  strcat (combined, err);

  error ("%s.", combined);
}

/* Print an error message and return to command level.
   STRING is the error message, used as a fprintf string,
   and ARG is passed as an argument to it.  */

void
error (const char *string,...)
{
  extern jmp_buf toplevel;
  va_list args;
  va_start (args, string);
  fflush (stdout);
  vfprintf (stderr, string, args);
  fprintf (stderr, "\n");
  longjmp (toplevel, 1);
}

/* Print an error message and exit reporting failure.
   This is for a error that we cannot continue from.
   STRING and ARG are passed to fprintf.  */

/* VARARGS */
void
fatal (const char *string,...)
{
  va_list args;
  va_start (args, string);
  fprintf (stderr, "gdb: ");
  vfprintf (stderr, string, args);
  fprintf (stderr, "\n");
  va_end (args);
  exit (1);
}

/* VARARGS */
void
warning (const char *string,...)
{
  va_list args;
  va_start (args, string);
  fprintf (stderr, "gdb: ");
  vfprintf (stderr, string, args);
  fprintf (stderr, "\n");
  va_end (args);
}
