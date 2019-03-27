/* Support for memory-mapped windows into a BFD.
   Copyright 1995, 1996, 2001, 2002, 2003, 2005
   Free Software Foundation, Inc.
   Written by Cygnus Support.

This file is part of BFD, the Binary File Descriptor library.

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

#include "sysdep.h"

#include "bfd.h"
#include "libbfd.h"

/* Currently, if USE_MMAP is undefined, none if the window stuff is
   used.  Okay, so it's mis-named.  At least the command-line option
   "--without-mmap" is more obvious than "--without-windows" or some
   such.  */

#ifdef USE_MMAP

#undef HAVE_MPROTECT /* code's not tested yet */

#if HAVE_MMAP || HAVE_MPROTECT || HAVE_MADVISE
#include <sys/mman.h>
#endif

#ifndef MAP_FILE
#define MAP_FILE 0
#endif

static int debug_windows;

/* The idea behind the next and refcount fields is that one mapped
   region can suffice for multiple read-only windows or multiple
   non-overlapping read-write windows.  It's not implemented yet
   though.  */

/*
INTERNAL_DEFINITION

.struct _bfd_window_internal {
.  struct _bfd_window_internal *next;
.  void *data;
.  bfd_size_type size;
.  int refcount : 31;		{* should be enough...  *}
.  unsigned mapped : 1;		{* 1 = mmap, 0 = malloc *}
.};
*/

void
bfd_init_window (bfd_window *windowp)
{
  windowp->data = 0;
  windowp->i = 0;
  windowp->size = 0;
}

void
bfd_free_window (bfd_window *windowp)
{
  bfd_window_internal *i = windowp->i;
  windowp->i = 0;
  windowp->data = 0;
  if (i == 0)
    return;
  i->refcount--;
  if (debug_windows)
    fprintf (stderr, "freeing window @%p<%p,%lx,%p>\n",
	     windowp, windowp->data, windowp->size, windowp->i);
  if (i->refcount != 0)
    return;

  if (i->mapped)
    {
#ifdef HAVE_MMAP
      munmap (i->data, i->size);
      goto no_free;
#else
      abort ();
#endif
    }
#ifdef HAVE_MPROTECT
  mprotect (i->data, i->size, PROT_READ | PROT_WRITE);
#endif
  free (i->data);
#ifdef HAVE_MMAP
 no_free:
#endif
  i->data = 0;
  /* There should be no more references to i at this point.  */
  free (i);
}

static int ok_to_map = 1;

bfd_boolean
bfd_get_file_window (bfd *abfd,
		     file_ptr offset,
		     bfd_size_type size,
		     bfd_window *windowp,
		     bfd_boolean writable)
{
  static size_t pagesize;
  bfd_window_internal *i = windowp->i;
  bfd_size_type size_to_alloc = size;

  if (debug_windows)
    fprintf (stderr, "bfd_get_file_window (%p, %6ld, %6ld, %p<%p,%lx,%p>, %d)",
	     abfd, (long) offset, (long) size,
	     windowp, windowp->data, (unsigned long) windowp->size,
	     windowp->i, writable);

  /* Make sure we know the page size, so we can be friendly to mmap.  */
  if (pagesize == 0)
    pagesize = getpagesize ();
  if (pagesize == 0)
    abort ();

  if (i == 0)
    {
      i = bfd_zmalloc (sizeof (bfd_window_internal));
      windowp->i = i;
      if (i == 0)
	return FALSE;
      i->data = 0;
    }
#ifdef HAVE_MMAP
  if (ok_to_map
      && (i->data == 0 || i->mapped == 1)
      && (abfd->flags & BFD_IN_MEMORY) == 0)
    {
      file_ptr file_offset, offset2;
      size_t real_size;
      int fd;

      /* Find the real file and the real offset into it.  */
      while (abfd->my_archive != NULL)
	{
	  offset += abfd->origin;
	  abfd = abfd->my_archive;
	}

      /* Seek into the file, to ensure it is open if cacheable.  */
      if (abfd->iostream == NULL
	  && (abfd->iovec == NULL
	      || abfd->iovec->bseek (abfd, offset, SEEK_SET) != 0))
	return FALSE;
      fd = fileno ((FILE *) abfd->iostream);

      /* Compute offsets and size for mmap and for the user's data.  */
      offset2 = offset % pagesize;
      if (offset2 < 0)
	abort ();
      file_offset = offset - offset2;
      real_size = offset + size - file_offset;
      real_size = real_size + pagesize - 1;
      real_size -= real_size % pagesize;

      /* If we're re-using a memory region, make sure it's big enough.  */
      if (i->data && i->size < size)
	{
	  munmap (i->data, i->size);
	  i->data = 0;
	}
      i->data = mmap (i->data, real_size,
		      writable ? PROT_WRITE | PROT_READ : PROT_READ,
		      (writable
		       ? MAP_FILE | MAP_PRIVATE
		       : MAP_FILE | MAP_SHARED),
		      fd, file_offset);
      if (i->data == (void *) -1)
	{
	  /* An error happened.  Report it, or try using malloc, or
	     something.  */
	  bfd_set_error (bfd_error_system_call);
	  i->data = 0;
	  windowp->data = 0;
	  if (debug_windows)
	    fprintf (stderr, "\t\tmmap failed!\n");
	  return FALSE;
	}
      if (debug_windows)
	fprintf (stderr, "\n\tmapped %ld at %p, offset is %ld\n",
		 (long) real_size, i->data, (long) offset2);
      i->size = real_size;
      windowp->data = (bfd_byte *) i->data + offset2;
      windowp->size = size;
      i->mapped = 1;
      return TRUE;
    }
  else if (debug_windows)
    {
      if (ok_to_map)
	fprintf (stderr, _("not mapping: data=%lx mapped=%d\n"),
		 (unsigned long) i->data, (int) i->mapped);
      else
	fprintf (stderr, _("not mapping: env var not set\n"));
    }
#else
  ok_to_map = 0;
#endif

#ifdef HAVE_MPROTECT
  if (!writable)
    {
      size_to_alloc += pagesize - 1;
      size_to_alloc -= size_to_alloc % pagesize;
    }
#endif
  if (debug_windows)
    fprintf (stderr, "\n\t%s(%6ld)",
	     i->data ? "realloc" : " malloc", (long) size_to_alloc);
  i->data = bfd_realloc (i->data, size_to_alloc);
  if (debug_windows)
    fprintf (stderr, "\t-> %p\n", i->data);
  i->refcount = 1;
  if (i->data == NULL)
    {
      if (size_to_alloc == 0)
	return TRUE;
      return FALSE;
    }
  if (bfd_seek (abfd, offset, SEEK_SET) != 0)
    return FALSE;
  i->size = bfd_bread (i->data, size, abfd);
  if (i->size != size)
    return FALSE;
  i->mapped = 0;
#ifdef HAVE_MPROTECT
  if (!writable)
    {
      if (debug_windows)
	fprintf (stderr, "\tmprotect (%p, %ld, PROT_READ)\n", i->data,
		 (long) i->size);
      mprotect (i->data, i->size, PROT_READ);
    }
#endif
  windowp->data = i->data;
  windowp->size = i->size;
  return TRUE;
}

#endif /* USE_MMAP */
