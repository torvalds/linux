/* arsup.c - Archive support for MRI compatibility
   Copyright 1992, 1994, 1995, 1996, 1997, 1999, 2000, 2001, 2002, 2003,
   2004, 2007 Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */


/* Contributed by Steve Chamberlain
   sac@cygnus.com

   This file looks after requests from arparse.y, to provide the MRI
   style librarian command syntax + 1 word LIST.  */

#include "sysdep.h"
#include "bfd.h"
#include "libiberty.h"
#include "filenames.h"
#include "bucomm.h"
#include "arsup.h"

static void map_over_list
  (bfd *, void (*function) (bfd *, bfd *), struct list *);
static void ar_directory_doer (bfd *, bfd *);
static void ar_addlib_doer (bfd *, bfd *);

extern int verbose;

static bfd *obfd;
static char *real_name;
static FILE *outfile;

static void
map_over_list (bfd *arch, void (*function) (bfd *, bfd *), struct list *list)
{
  bfd *head;

  if (list == NULL)
    {
      bfd *next;

      head = arch->archive_next;
      while (head != NULL)
	{
	  next = head->archive_next;
	  function (head, (bfd *) NULL);
	  head = next;
	}
    }
  else
    {
      struct list *ptr;

      /* This may appear to be a baroque way of accomplishing what we
	 want.  however we have to iterate over the filenames in order
	 to notice where a filename is requested but does not exist in
	 the archive.  Ditto mapping over each file each time -- we
	 want to hack multiple references.  */
      for (ptr = list; ptr; ptr = ptr->next)
	{
	  bfd_boolean found = FALSE;
	  bfd *prev = arch;

	  for (head = arch->archive_next; head; head = head->archive_next)
	    {
	      if (head->filename != NULL
		  && FILENAME_CMP (ptr->name, head->filename) == 0)
		{
		  found = TRUE;
		  function (head, prev);
		}
	      prev = head;
	    }
	  if (! found)
	    fprintf (stderr, _("No entry %s in archive.\n"), ptr->name);
	}
    }
}



static void
ar_directory_doer (bfd *abfd, bfd *ignore ATTRIBUTE_UNUSED)
{
  print_arelt_descr(outfile, abfd, verbose);
}

void
ar_directory (char *ar_name, struct list *list, char *output)
{
  bfd *arch;

  arch = open_inarch (ar_name, (char *) NULL);
  if (output)
    {
      outfile = fopen(output,"w");
      if (outfile == 0)
	{
	  outfile = stdout;
	  fprintf (stderr,_("Can't open file %s\n"), output);
	  output = 0;
	}
    }
  else
    outfile = stdout;

  map_over_list (arch, ar_directory_doer, list);

  bfd_close (arch);

  if (output)
   fclose (outfile);
}

void
prompt (void)
{
  extern int interactive;

  if (interactive)
    {
      printf ("AR >");
      fflush (stdout);
    }
}

void
maybequit (void)
{
  if (! interactive)
    xexit (9);
}


void
ar_open (char *name, int t)
{
  char *tname = (char *) xmalloc (strlen (name) + 10);
  const char *bname = lbasename (name);
  real_name = name;

  /* Prepend tmp- to the beginning, to avoid file-name clashes after
     truncation on filesystems with limited namespaces (DOS).  */
  sprintf (tname, "%.*stmp-%s", (int) (bname - name), name, bname);
  obfd = bfd_openw (tname, NULL);

  if (!obfd)
    {
      fprintf (stderr,
	       _("%s: Can't open output archive %s\n"),
	       program_name,  tname);

      maybequit ();
    }
  else
    {
      if (!t)
	{
	  bfd **ptr;
	  bfd *element;
	  bfd *ibfd;

	  ibfd = bfd_openr (name, NULL);

	  if (!ibfd)
	    {
	      fprintf (stderr,_("%s: Can't open input archive %s\n"),
		       program_name, name);
	      maybequit ();
	      return;
	    }

	  if (!bfd_check_format(ibfd, bfd_archive))
	    {
	      fprintf (stderr,
		       _("%s: file %s is not an archive\n"),
		       program_name, name);
	      maybequit ();
	      return;
	    }

	  ptr = &(obfd->archive_head);
	  element = bfd_openr_next_archived_file (ibfd, NULL);

	  while (element)
	    {
	      *ptr = element;
	      ptr = &element->archive_next;
	      element = bfd_openr_next_archived_file (ibfd, element);
	    }
	}

      bfd_set_format (obfd, bfd_archive);

      obfd->has_armap = 1;
    }
}

static void
ar_addlib_doer (bfd *abfd, bfd *prev)
{
  /* Add this module to the output bfd.  */
  if (prev != NULL)
    prev->archive_next = abfd->archive_next;

  abfd->archive_next = obfd->archive_head;
  obfd->archive_head = abfd;
}

void
ar_addlib (char *name, struct list *list)
{
  if (obfd == NULL)
    {
      fprintf (stderr, _("%s: no output archive specified yet\n"), program_name);
      maybequit ();
    }
  else
    {
      bfd *arch;

      arch = open_inarch (name, (char *) NULL);
      if (arch != NULL)
	map_over_list (arch, ar_addlib_doer, list);

      /* Don't close the bfd, since it will make the elements disappear.  */
    }
}

void
ar_addmod (struct list *list)
{
  if (!obfd)
    {
      fprintf (stderr, _("%s: no open output archive\n"), program_name);
      maybequit ();
    }
  else
    {
      while (list)
	{
	  bfd *abfd = bfd_openr (list->name, NULL);

	  if (!abfd)
	    {
	      fprintf (stderr, _("%s: can't open file %s\n"),
		       program_name, list->name);
	      maybequit ();
	    }
	  else
	    {
	      abfd->archive_next = obfd->archive_head;
	      obfd->archive_head = abfd;
	    }
	  list = list->next;
	}
    }
}


void
ar_clear (void)
{
  if (obfd)
    obfd->archive_head = 0;
}

void
ar_delete (struct list *list)
{
  if (!obfd)
    {
      fprintf (stderr, _("%s: no open output archive\n"), program_name);
      maybequit ();
    }
  else
    {
      while (list)
	{
	  /* Find this name in the archive.  */
	  bfd *member = obfd->archive_head;
	  bfd **prev = &(obfd->archive_head);
	  int found = 0;

	  while (member)
	    {
	      if (FILENAME_CMP(member->filename, list->name) == 0)
		{
		  *prev = member->archive_next;
		  found = 1;
		}
	      else
		prev = &(member->archive_next);

	      member = member->archive_next;
	    }

	  if (!found)
	    {
	      fprintf (stderr, _("%s: can't find module file %s\n"),
		       program_name, list->name);
	      maybequit ();
	    }

	  list = list->next;
	}
    }
}

void
ar_save (void)
{
  if (!obfd)
    {
      fprintf (stderr, _("%s: no open output archive\n"), program_name);
      maybequit ();
    }
  else
    {
      char *ofilename = xstrdup (bfd_get_filename (obfd));

      bfd_close (obfd);

      smart_rename (ofilename, real_name, 0);
      obfd = 0;
      free (ofilename);
    }
}

void
ar_replace (struct list *list)
{
  if (!obfd)
    {
      fprintf (stderr, _("%s: no open output archive\n"), program_name);
      maybequit ();
    }
  else
    {
      while (list)
	{
	  /* Find this name in the archive.  */
	  bfd *member = obfd->archive_head;
	  bfd **prev = &(obfd->archive_head);
	  int found = 0;

	  while (member)
	    {
	      if (FILENAME_CMP (member->filename, list->name) == 0)
		{
		  /* Found the one to replace.  */
		  bfd *abfd = bfd_openr (list->name, 0);

		  if (!abfd)
		    {
		      fprintf (stderr, _("%s: can't open file %s\n"),
			       program_name, list->name);
		      maybequit ();
		    }
		  else
		    {
		      *prev = abfd;
		      abfd->archive_next = member->archive_next;
		      found = 1;
		    }
		}
	      else
		{
		  prev = &(member->archive_next);
		}
	      member = member->archive_next;
	    }

	  if (!found)
	    {
	      bfd *abfd = bfd_openr (list->name, 0);

	      fprintf (stderr,_("%s: can't find module file %s\n"),
		       program_name, list->name);
	      if (!abfd)
		{
		  fprintf (stderr, _("%s: can't open file %s\n"),
			   program_name, list->name);
		  maybequit ();
		}
	      else
		*prev = abfd;
	    }

	  list = list->next;
	}
    }
}

/* And I added this one.  */
void
ar_list (void)
{
  if (!obfd)
    {
      fprintf (stderr, _("%s: no open output archive\n"), program_name);
      maybequit ();
    }
  else
    {
      bfd *abfd;

      outfile = stdout;
      verbose =1 ;
      printf (_("Current open archive is %s\n"), bfd_get_filename (obfd));

      for (abfd = obfd->archive_head;
	   abfd != (bfd *)NULL;
	   abfd = abfd->archive_next)
	ar_directory_doer (abfd, (bfd *) NULL);
    }
}

void
ar_end (void)
{
  if (obfd)
    {
      bfd_cache_close (obfd);
      unlink (bfd_get_filename (obfd));
    }
}

void
ar_extract (struct list *list)
{
  if (!obfd)
    {
      fprintf (stderr, _("%s: no open archive\n"), program_name);
      maybequit ();
    }
  else
    {
      while (list)
	{
	  /* Find this name in the archive.  */
	  bfd *member = obfd->archive_head;
	  int found = 0;

	  while (member && !found)
	    {
	      if (FILENAME_CMP (member->filename, list->name) == 0)
		{
		  extract_file (member);
		  found = 1;
		}

	      member = member->archive_next;
	    }

	  if (!found)
	    {
	      bfd_openr (list->name, 0);
	      fprintf (stderr, _("%s: can't find module file %s\n"),
		       program_name, list->name);
	    }

	  list = list->next;
	}
    }
}
