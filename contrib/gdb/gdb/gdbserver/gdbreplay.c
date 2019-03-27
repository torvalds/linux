/* Replay a remote debug session logfile for GDB.
   Copyright 1996, 1998, 1999, 2000, 2002, 2003 Free Software Foundation, Inc.
   Written by Fred Fish (fnf@cygnus.com) from pieces of gdbserver.

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

#include "config.h"
#include <stdio.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* Sort of a hack... */
#define EOL (EOF - 1)

static int remote_desc;

/* Print the system error message for errno, and also mention STRING
   as the file name for which the error was encountered.
   Then return to command level.  */

static void
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
  fprintf (stderr, "\n%s.\n", combined);
  fflush (stderr);
  exit (1);
}

static void
sync_error (FILE *fp, char *desc, int expect, int got)
{
  fprintf (stderr, "\n%s\n", desc);
  fprintf (stderr, "At logfile offset %ld, expected '0x%x' got '0x%x'\n",
	   ftell (fp), expect, got);
  fflush (stderr);
  exit (1);
}

static void
remote_close (void)
{
  close (remote_desc);
}

/* Open a connection to a remote debugger.
   NAME is the filename used for communication.  */

static void
remote_open (char *name)
{
  if (!strchr (name, ':'))
    {
      fprintf (stderr, "%s: Must specify tcp connection as host:addr\n", name);
      fflush (stderr);
      exit (1);
    }
  else
    {
      char *port_str;
      int port;
      struct sockaddr_in sockaddr;
      int tmp;
      int tmp_desc;

      port_str = strchr (name, ':');

      port = atoi (port_str + 1);

      tmp_desc = socket (PF_INET, SOCK_STREAM, 0);
      if (tmp_desc < 0)
	perror_with_name ("Can't open socket");

      /* Allow rapid reuse of this port. */
      tmp = 1;
      setsockopt (tmp_desc, SOL_SOCKET, SO_REUSEADDR, (char *) &tmp,
		  sizeof (tmp));

      sockaddr.sin_family = PF_INET;
      sockaddr.sin_port = htons (port);
      sockaddr.sin_addr.s_addr = INADDR_ANY;

      if (bind (tmp_desc, (struct sockaddr *) &sockaddr, sizeof (sockaddr))
	  || listen (tmp_desc, 1))
	perror_with_name ("Can't bind address");

      tmp = sizeof (sockaddr);
      remote_desc = accept (tmp_desc, (struct sockaddr *) &sockaddr, &tmp);
      if (remote_desc == -1)
	perror_with_name ("Accept failed");

      /* Enable TCP keep alive process. */
      tmp = 1;
      setsockopt (tmp_desc, SOL_SOCKET, SO_KEEPALIVE, (char *) &tmp, sizeof (tmp));

      /* Tell TCP not to delay small packets.  This greatly speeds up
         interactive response. */
      tmp = 1;
      setsockopt (remote_desc, IPPROTO_TCP, TCP_NODELAY,
		  (char *) &tmp, sizeof (tmp));

      close (tmp_desc);		/* No longer need this */

      signal (SIGPIPE, SIG_IGN);	/* If we don't do this, then gdbreplay simply
					   exits when the remote side dies.  */
    }

  fcntl (remote_desc, F_SETFL, FASYNC);

  fprintf (stderr, "Replay logfile using %s\n", name);
  fflush (stderr);
}

static int
tohex (int ch)
{
  if (ch >= '0' && ch <= '9')
    {
      return (ch - '0');
    }
  if (ch >= 'A' && ch <= 'F')
    {
      return (ch - 'A' + 10);
    }
  if (ch >= 'a' && ch <= 'f')
    {
      return (ch - 'a' + 10);
    }
  fprintf (stderr, "\nInvalid hex digit '%c'\n", ch);
  fflush (stderr);
  exit (1);
}

static int
logchar (FILE *fp)
{
  int ch;
  int ch2;

  ch = fgetc (fp);
  fputc (ch, stdout);
  fflush (stdout);
  switch (ch)
    {
    case '\n':
      ch = EOL;
      break;
    case '\\':
      ch = fgetc (fp);
      fputc (ch, stdout);
      fflush (stdout);
      switch (ch)
	{
	case '\\':
	  break;
	case 'b':
	  ch = '\b';
	  break;
	case 'f':
	  ch = '\f';
	  break;
	case 'n':
	  ch = '\n';
	  break;
	case 'r':
	  ch = '\r';
	  break;
	case 't':
	  ch = '\t';
	  break;
	case 'v':
	  ch = '\v';
	  break;
	case 'x':
	  ch2 = fgetc (fp);
	  fputc (ch2, stdout);
	  fflush (stdout);
	  ch = tohex (ch2) << 4;
	  ch2 = fgetc (fp);
	  fputc (ch2, stdout);
	  fflush (stdout);
	  ch |= tohex (ch2);
	  break;
	default:
	  /* Treat any other char as just itself */
	  break;
	}
    default:
      break;
    }
  return (ch);
}

/* Accept input from gdb and match with chars from fp (after skipping one
   blank) up until a \n is read from fp (which is not matched) */

static void
expect (FILE *fp)
{
  int fromlog;
  unsigned char fromgdb;

  if ((fromlog = logchar (fp)) != ' ')
    {
      sync_error (fp, "Sync error during gdb read of leading blank", ' ',
		  fromlog);
    }
  do
    {
      fromlog = logchar (fp);
      if (fromlog == EOL)
	{
	  break;
	}
      read (remote_desc, &fromgdb, 1);
    }
  while (fromlog == fromgdb);
  if (fromlog != EOL)
    {
      sync_error (fp, "Sync error during read of gdb packet", fromlog,
		  fromgdb);
    }
}

/* Play data back to gdb from fp (after skipping leading blank) up until a
   \n is read from fp (which is discarded and not sent to gdb). */

static void
play (FILE *fp)
{
  int fromlog;
  char ch;

  if ((fromlog = logchar (fp)) != ' ')
    {
      sync_error (fp, "Sync error skipping blank during write to gdb", ' ',
		  fromlog);
    }
  while ((fromlog = logchar (fp)) != EOL)
    {
      ch = fromlog;
      write (remote_desc, &ch, 1);
    }
}

int
main (int argc, char *argv[])
{
  FILE *fp;
  int ch;

  if (argc < 3)
    {
      fprintf (stderr, "Usage: gdbreplay <logfile> <host:port>\n");
      fflush (stderr);
      exit (1);
    }
  fp = fopen (argv[1], "r");
  if (fp == NULL)
    {
      perror_with_name (argv[1]);
    }
  remote_open (argv[2]);
  while ((ch = logchar (fp)) != EOF)
    {
      switch (ch)
	{
	case 'w':
	  /* data sent from gdb to gdbreplay, accept and match it */
	  expect (fp);
	  break;
	case 'r':
	  /* data sent from gdbreplay to gdb, play it */
	  play (fp);
	  break;
	case 'c':
	  /* Command executed by gdb */
	  while ((ch = logchar (fp)) != EOL);
	  break;
	}
    }
  remote_close ();
  exit (0);
}
