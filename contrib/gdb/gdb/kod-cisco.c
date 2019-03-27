/* Kernel Object Display facility for Cisco
   Copyright 1999, 2000 Free Software Foundation, Inc.
   
   Written by Tom Tromey <tromey@cygnus.com>.
   
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
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "gdb_string.h"
#include "kod.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

/* Define this to turn off communication with target.  */
/* #define FAKE_PACKET */

/* Size of buffer used for remote communication.  */
#define PBUFSIZ 400

/* Pointers to gdb callbacks.  */
static void (*gdb_kod_display) (char *);
static void (*gdb_kod_query) (char *, char *, int *);



/* Initialize and return library name and version.
   The gdb side of KOD, kod.c, passes us two functions: one for
   displaying output (presumably to the user) and the other for
   querying the target.  */
char *
cisco_kod_open (kod_display_callback_ftype *display_func,
		kod_query_callback_ftype *query_func)
{
  char buffer[PBUFSIZ];
  int bufsiz = PBUFSIZ;
  int i, count;

  gdb_kod_display = display_func;
  gdb_kod_query = query_func;

  /* Get the OS info, and check the version field.  This is the stub
     version, which we use to see whether we will understand what
     comes back.  This is lame, but the `qKoL' request doesn't
     actually provide enough configurability.
     
     Right now the only defined version number is `0.0.0'.
     This stub supports qKoI and the `a' (any) object requests qKaL
     and qKaI.  Each `a' object is returned as a 4-byte integer ID.
     An info request on an object returns a pair of 4-byte integers;
     the first is the object pointer and the second is the thread ID.  */

#ifndef FAKE_PACKET
  (*gdb_kod_query) ("oI;", buffer, &bufsiz);
#else
  strcpy (buffer, "Cisco IOS/Classic/13.4 0.0.0");
#endif

  count = 2;
  for (i = 0; count && buffer[i] != '\0'; ++i)
    {
      if (buffer[i] == ' ')
	--count;
    }

  if (buffer[i] == '\0')
    error ("Remote returned malformed packet\n");
  if (strcmp (&buffer[i], "0.0.0"))
    error ("Remote returned unknown stub version: %s\n", &buffer[i]);

  /* Return name, version, and description.  I hope we have enough
     space.  */
  return (xstrdup ("gdbkodcisco v0.0.0 - Cisco Kernel Object Display"));
}

/* Close the connection.  */
void
cisco_kod_close (void)
{
}

/* Print a "bad packet" message.  */
static void
bad_packet (void)
{
  (*gdb_kod_display) ("Remote target returned malformed packet.\n");
}

/* Print information about currently known kernel objects.
   We currently ignore the argument.  There is only one mode of
   querying the Cisco kernel: we ask for a dump of everything, and
   it returns it.  */
void
cisco_kod_request (char *arg, int from_tty)
{
  char buffer[PBUFSIZ], command[PBUFSIZ];
  int done = 0, i;
  int fail = 0;

  char **sync_ids = NULL;
  int sync_len = 0;
  int sync_next = 0;
  char *prev_id = NULL;

  if (! arg || strcmp (arg, "any"))
    {
      /* "Top-level" command.  This is really silly, but it also seems
	 to be how KOD is defined.  */
      /* Even sillier is the fact that this first line must start
	 with the word "List".  See kod.tcl.  */
      (*gdb_kod_display) ("List of Cisco Kernel Objects\n");
      (*gdb_kod_display) ("Object\tDescription\n");
      (*gdb_kod_display) ("any\tAny and all objects\n");
      return;
    }

  while (! done)
    {
      int off = 0;		/* Where we are in the string.  */
      long count;		/* Number of objects in this packet.  */
      int bufsiz = PBUFSIZ;
      char *s_end;

      strcpy (command, "aL");
      if (prev_id)
	{
	  strcat (command, ",");
	  strcat (command, prev_id);
	}
      strcat (command, ";");

#ifndef FAKE_PACKET
      /* We talk to the target by calling through the query function
	 passed to us when we were initialized.  */
      (*gdb_kod_query) (command, buffer, &bufsiz);
#else
      /* Fake up a multi-part packet.  */
      if (! strncmp (&command[3], "a500005a", 8))
	strcpy (buffer, "KAL,01,1,f500005f;f500005f;");
      else
	strcpy (buffer, "KAL,02,0,a500005a;a500005a;de02869f;");
#endif

      /* Empty response is an error.  */
      if (strlen (buffer) == 0)
	{
	  (*gdb_kod_display) ("Remote target did not recognize kernel object query command.\n");
	  fail = 1;
	  break;
	}

      /* If we don't get a `K' response then the buffer holds the
	 target's error message.  */
      if (buffer[0] != 'K')
	{
	  (*gdb_kod_display) (buffer);
	  fail = 1;
	  break;
	}

      /* Make sure we get the response we expect.  */
      if (strncmp (buffer, "KAL,", 4))
	{
	  bad_packet ();
	  fail = 1;
	  break;
	}
      off += 4;

      /* Parse out the count.  We expect to convert exactly two
	 characters followed by a comma.  */
      count = strtol (&buffer[off], &s_end, 16);
      if (s_end - &buffer[off] != 2 || buffer[off + 2] != ',')
	{
	  bad_packet ();
	  fail = 1;
	  break;
	}
      off += 3;

      /* Parse out the `done' flag.  */
      if ((buffer[off] != '0' && buffer[off] != '1')
	  || buffer[off + 1] != ',')
	{
	  bad_packet ();
	  fail = 1;
	  break;
	}
      done = buffer[off] == '1';
      off += 2;

      /* Id of the last item; we might this to construct the next
	 request.  */
      prev_id = &buffer[off];
      if (strlen (prev_id) < 8 || buffer[off + 8] != ';')
	{
	  bad_packet ();
	  fail = 1;
	  break;
	}
      buffer[off + 8] = '\0';
      off += 9;

      sync_len += count;
      sync_ids = (char **) xrealloc (sync_ids, sync_len * sizeof (char *));

      for (i = 0; i < count; ++i)
	{
	  if (strlen (&buffer[off]) < 8 || buffer[off + 8] != ';')
	    {
	      bad_packet ();
	      fail = 1;
	      break;
	    }
	  buffer[off + 8] = '\0';
	  sync_ids[sync_next++] = xstrdup (&buffer[off]);
	  off += 9;
	}

      if (buffer[off] != '\0')
	{
	  bad_packet ();
	  fail = 1;
	  break;
	}
    }

  /* We've collected all the sync object IDs.  Now query to get the
     specific information, and arrange to print this info.  */
  if (! fail)
    {
      (*gdb_kod_display) ("Object ID\tObject Pointer\tThread ID\n");

      for (i = 0; i < sync_next; ++i)
	{
	  int off = 0;
	  int bufsiz = PBUFSIZ;

	  /* For now assume a query can be accomplished in a single
	     transaction.  This is implied in the protocol document.
	     See comments above, and the KOD protocol document, to
	     understand the parsing of the return value.  */
	  strcpy (command, "aI,");
	  strcat (command, sync_ids[i]);
	  strcat (command, ";");

#ifndef FAKE_PACKET
	  (*gdb_kod_query) (command, buffer, &bufsiz);
#else
	  strcpy (buffer, "KAI,");
	  strcat (buffer, sync_ids[i]);
	  strcat (buffer, ",ffef00a0,cd00123d;");
#endif

	  if (strlen (buffer) == 0)
	    {
	      (*gdb_kod_display) ("Remote target did not recognize KOD command.\n");
	      break;
	    }

	  if (strncmp (buffer, "KAI,", 4))
	    {
	      bad_packet ();
	      break;
	    }
	  off += 4;

	  if (strncmp (&buffer[off], sync_ids[i], 8)
	      || buffer[off + 8] != ',')
	    {
	      bad_packet ();
	      break;
	    }
	  off += 9;

	  /* Extract thread id and sync object pointer.  */
	  if (strlen (&buffer[off]) != 2 * 8 + 2
	      || buffer[off + 8] != ','
	      || buffer[off + 17] != ';')
	    {
	      bad_packet ();
	      break;
	    }

	  buffer[off + 8] = '\0';
	  buffer[off + 17] = '\0';

	  /* Display the result.  */
	  (*gdb_kod_display) (sync_ids[i]);
	  (*gdb_kod_display) ("\t");
	  (*gdb_kod_display) (&buffer[off]);
	  (*gdb_kod_display) ("\t");
	  (*gdb_kod_display) (&buffer[off + 9]);
	  (*gdb_kod_display) ("\n");
	}
    }

  /* Free memory.  */
  for (i = 0; i < sync_next; ++i)
    xfree (sync_ids[i]);
  xfree (sync_ids);
}
