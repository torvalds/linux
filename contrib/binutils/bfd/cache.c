/* BFD library -- caching of file descriptors.

   Copyright 1990, 1991, 1992, 1993, 1994, 1996, 2000, 2001, 2002,
   2003, 2004, 2005, 2007 Free Software Foundation, Inc.

   Hacked by Steve Chamberlain of Cygnus Support (steve@cygnus.com).

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

/*
SECTION
	File caching

	The file caching mechanism is embedded within BFD and allows
	the application to open as many BFDs as it wants without
	regard to the underlying operating system's file descriptor
	limit (often as low as 20 open files).  The module in
	<<cache.c>> maintains a least recently used list of
	<<BFD_CACHE_MAX_OPEN>> files, and exports the name
	<<bfd_cache_lookup>>, which runs around and makes sure that
	the required BFD is open. If not, then it chooses a file to
	close, closes it and opens the one wanted, returning its file
	handle.

SUBSECTION
	Caching functions
*/

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "libiberty.h"

/* In some cases we can optimize cache operation when reopening files.
   For instance, a flush is entirely unnecessary if the file is already
   closed, so a flush would use CACHE_NO_OPEN.  Similarly, a seek using
   SEEK_SET or SEEK_END need not first seek to the current position.
   For stat we ignore seek errors, just in case the file has changed
   while we weren't looking.  If it has, then it's possible that the
   file is shorter and we don't want a seek error to prevent us doing
   the stat.  */
enum cache_flag {
  CACHE_NORMAL = 0,
  CACHE_NO_OPEN = 1,
  CACHE_NO_SEEK = 2,
  CACHE_NO_SEEK_ERROR = 4
};

/* The maximum number of files which the cache will keep open at
   one time.  */

#define BFD_CACHE_MAX_OPEN 10

/* The number of BFD files we have open.  */

static int open_files;

/* Zero, or a pointer to the topmost BFD on the chain.  This is
   used by the <<bfd_cache_lookup>> macro in @file{libbfd.h} to
   determine when it can avoid a function call.  */

static bfd *bfd_last_cache = NULL;

/* Insert a BFD into the cache.  */

static void
insert (bfd *abfd)
{
  if (bfd_last_cache == NULL)
    {
      abfd->lru_next = abfd;
      abfd->lru_prev = abfd;
    }
  else
    {
      abfd->lru_next = bfd_last_cache;
      abfd->lru_prev = bfd_last_cache->lru_prev;
      abfd->lru_prev->lru_next = abfd;
      abfd->lru_next->lru_prev = abfd;
    }
  bfd_last_cache = abfd;
}

/* Remove a BFD from the cache.  */

static void
snip (bfd *abfd)
{
  abfd->lru_prev->lru_next = abfd->lru_next;
  abfd->lru_next->lru_prev = abfd->lru_prev;
  if (abfd == bfd_last_cache)
    {
      bfd_last_cache = abfd->lru_next;
      if (abfd == bfd_last_cache)
	bfd_last_cache = NULL;
    }
}

/* Close a BFD and remove it from the cache.  */

static bfd_boolean
bfd_cache_delete (bfd *abfd)
{
  bfd_boolean ret;

  if (fclose ((FILE *) abfd->iostream) == 0)
    ret = TRUE;
  else
    {
      ret = FALSE;
      bfd_set_error (bfd_error_system_call);
    }

  snip (abfd);

  abfd->iostream = NULL;
  --open_files;

  return ret;
}

/* We need to open a new file, and the cache is full.  Find the least
   recently used cacheable BFD and close it.  */

static bfd_boolean
close_one (void)
{
  register bfd *kill;

  if (bfd_last_cache == NULL)
    kill = NULL;
  else
    {
      for (kill = bfd_last_cache->lru_prev;
	   ! kill->cacheable;
	   kill = kill->lru_prev)
	{
	  if (kill == bfd_last_cache)
	    {
	      kill = NULL;
	      break;
	    }
	}
    }

  if (kill == NULL)
    {
      /* There are no open cacheable BFD's.  */
      return TRUE;
    }

  kill->where = real_ftell ((FILE *) kill->iostream);

  /* Save the file st_mtime.  This is a hack so that gdb can detect when
     an executable has been deleted and recreated.  The only thing that
     makes this reasonable is that st_mtime doesn't change when a file
     is unlinked, so saving st_mtime makes BFD's file cache operation
     a little more transparent for this particular usage pattern.  If we
     hadn't closed the file then we would not have lost the original
     contents, st_mtime etc.  Of course, if something is writing to an
     existing file, then this is the wrong thing to do.
     FIXME: gdb should save these times itself on first opening a file,
     and this hack be removed.  */
  if (kill->direction == no_direction || kill->direction == read_direction)
    {
      bfd_get_mtime (kill);
      kill->mtime_set = TRUE;
    }

  return bfd_cache_delete (kill);
}

/* Check to see if the required BFD is the same as the last one
   looked up. If so, then it can use the stream in the BFD with
   impunity, since it can't have changed since the last lookup;
   otherwise, it has to perform the complicated lookup function.  */

#define bfd_cache_lookup(x, flag) \
  ((x) == bfd_last_cache			\
   ? (FILE *) (bfd_last_cache->iostream)	\
   : bfd_cache_lookup_worker (x, flag))

/* Called when the macro <<bfd_cache_lookup>> fails to find a
   quick answer.  Find a file descriptor for @var{abfd}.  If
   necessary, it open it.  If there are already more than
   <<BFD_CACHE_MAX_OPEN>> files open, it tries to close one first, to
   avoid running out of file descriptors.  It will return NULL
   if it is unable to (re)open the @var{abfd}.  */

static FILE *
bfd_cache_lookup_worker (bfd *abfd, enum cache_flag flag)
{
  bfd *orig_bfd = abfd;
  if ((abfd->flags & BFD_IN_MEMORY) != 0)
    abort ();

  if (abfd->my_archive)
    abfd = abfd->my_archive;

  if (abfd->iostream != NULL)
    {
      /* Move the file to the start of the cache.  */
      if (abfd != bfd_last_cache)
	{
	  snip (abfd);
	  insert (abfd);
	}
      return (FILE *) abfd->iostream;
    }

  if (flag & CACHE_NO_OPEN)
    return NULL;

  if (bfd_open_file (abfd) == NULL)
    ;
  else if (!(flag & CACHE_NO_SEEK)
	   && real_fseek ((FILE *) abfd->iostream, abfd->where, SEEK_SET) != 0
	   && !(flag & CACHE_NO_SEEK_ERROR))
    bfd_set_error (bfd_error_system_call);
  else
    return (FILE *) abfd->iostream;

  (*_bfd_error_handler) (_("reopening %B: %s\n"),
			 orig_bfd, bfd_errmsg (bfd_get_error ()));
  return NULL;
}

static file_ptr
cache_btell (struct bfd *abfd)
{
  FILE *f = bfd_cache_lookup (abfd, CACHE_NO_OPEN);
  if (f == NULL)
    return abfd->where;
  return real_ftell (f);
}

static int
cache_bseek (struct bfd *abfd, file_ptr offset, int whence)
{
  FILE *f = bfd_cache_lookup (abfd, whence != SEEK_CUR ? CACHE_NO_SEEK : 0);
  if (f == NULL)
    return -1;
  return real_fseek (f, offset, whence);
}

/* Note that archive entries don't have streams; they share their parent's.
   This allows someone to play with the iostream behind BFD's back.

   Also, note that the origin pointer points to the beginning of a file's
   contents (0 for non-archive elements).  For archive entries this is the
   first octet in the file, NOT the beginning of the archive header.  */

static file_ptr
cache_bread (struct bfd *abfd, void *buf, file_ptr nbytes)
{
  FILE *f;
  file_ptr nread;
  /* FIXME - this looks like an optimization, but it's really to cover
     up for a feature of some OSs (not solaris - sigh) that
     ld/pe-dll.c takes advantage of (apparently) when it creates BFDs
     internally and tries to link against them.  BFD seems to be smart
     enough to realize there are no symbol records in the "file" that
     doesn't exist but attempts to read them anyway.  On Solaris,
     attempting to read zero bytes from a NULL file results in a core
     dump, but on other platforms it just returns zero bytes read.
     This makes it to something reasonable. - DJ */
  if (nbytes == 0)
    return 0;

  f = bfd_cache_lookup (abfd, 0);
  if (f == NULL)
    return 0;

#if defined (__VAX) && defined (VMS)
  /* Apparently fread on Vax VMS does not keep the record length
     information.  */
  nread = read (fileno (f), buf, nbytes);
  /* Set bfd_error if we did not read as much data as we expected.  If
     the read failed due to an error set the bfd_error_system_call,
     else set bfd_error_file_truncated.  */
  if (nread == (file_ptr)-1)
    {
      bfd_set_error (bfd_error_system_call);
      return -1;
    }
#else
  nread = fread (buf, 1, nbytes, f);
  /* Set bfd_error if we did not read as much data as we expected.  If
     the read failed due to an error set the bfd_error_system_call,
     else set bfd_error_file_truncated.  */
  if (nread < nbytes && ferror (f))
    {
      bfd_set_error (bfd_error_system_call);
      return -1;
    }
#endif
  return nread;
}

static file_ptr
cache_bwrite (struct bfd *abfd, const void *where, file_ptr nbytes)
{
  file_ptr nwrite;
  FILE *f = bfd_cache_lookup (abfd, 0);
  if (f == NULL)
    return 0;
  nwrite = fwrite (where, 1, nbytes, f);
  if (nwrite < nbytes && ferror (f))
    {
      bfd_set_error (bfd_error_system_call);
      return -1;
    }
  return nwrite;
}

static int
cache_bclose (struct bfd *abfd)
{
  return bfd_cache_close (abfd);
}

static int
cache_bflush (struct bfd *abfd)
{
  int sts;
  FILE *f = bfd_cache_lookup (abfd, CACHE_NO_OPEN);
  if (f == NULL)
    return 0;
  sts = fflush (f);
  if (sts < 0)
    bfd_set_error (bfd_error_system_call);
  return sts;
}

static int
cache_bstat (struct bfd *abfd, struct stat *sb)
{
  int sts;
  FILE *f = bfd_cache_lookup (abfd, CACHE_NO_SEEK_ERROR);
  if (f == NULL)
    return -1;
  sts = fstat (fileno (f), sb);
  if (sts < 0)
    bfd_set_error (bfd_error_system_call);
  return sts;
}

static const struct bfd_iovec cache_iovec = {
  &cache_bread, &cache_bwrite, &cache_btell, &cache_bseek,
  &cache_bclose, &cache_bflush, &cache_bstat
};

/*
INTERNAL_FUNCTION
	bfd_cache_init

SYNOPSIS
	bfd_boolean bfd_cache_init (bfd *abfd);

DESCRIPTION
	Add a newly opened BFD to the cache.
*/

bfd_boolean
bfd_cache_init (bfd *abfd)
{
  BFD_ASSERT (abfd->iostream != NULL);
  if (open_files >= BFD_CACHE_MAX_OPEN)
    {
      if (! close_one ())
	return FALSE;
    }
  abfd->iovec = &cache_iovec;
  insert (abfd);
  ++open_files;
  return TRUE;
}

/*
INTERNAL_FUNCTION
	bfd_cache_close

SYNOPSIS
	bfd_boolean bfd_cache_close (bfd *abfd);

DESCRIPTION
	Remove the BFD @var{abfd} from the cache. If the attached file is open,
	then close it too.

RETURNS
	<<FALSE>> is returned if closing the file fails, <<TRUE>> is
	returned if all is well.
*/

bfd_boolean
bfd_cache_close (bfd *abfd)
{
  if (abfd->iovec != &cache_iovec)
    return TRUE;

  if (abfd->iostream == NULL)
    /* Previously closed.  */
    return TRUE;

  return bfd_cache_delete (abfd);
}

/*
FUNCTION
	bfd_cache_close_all

SYNOPSIS
	bfd_boolean bfd_cache_close_all (void);

DESCRIPTION
	Remove all BFDs from the cache. If the attached file is open,
	then close it too.

RETURNS
	<<FALSE>> is returned if closing one of the file fails, <<TRUE>> is
	returned if all is well.
*/

bfd_boolean
bfd_cache_close_all ()
{
  bfd_boolean ret = TRUE;

  while (bfd_last_cache != NULL)
    ret &= bfd_cache_close (bfd_last_cache);

  return ret;
}

/*
INTERNAL_FUNCTION
	bfd_open_file

SYNOPSIS
	FILE* bfd_open_file (bfd *abfd);

DESCRIPTION
	Call the OS to open a file for @var{abfd}.  Return the <<FILE *>>
	(possibly <<NULL>>) that results from this operation.  Set up the
	BFD so that future accesses know the file is open. If the <<FILE *>>
	returned is <<NULL>>, then it won't have been put in the
	cache, so it won't have to be removed from it.
*/

FILE *
bfd_open_file (bfd *abfd)
{
  abfd->cacheable = TRUE;	/* Allow it to be closed later.  */

  if (open_files >= BFD_CACHE_MAX_OPEN)
    {
      if (! close_one ())
	return NULL;
    }

  switch (abfd->direction)
    {
    case read_direction:
    case no_direction:
      abfd->iostream = (PTR) real_fopen (abfd->filename, FOPEN_RB);
      break;
    case both_direction:
    case write_direction:
      if (abfd->opened_once)
	{
	  abfd->iostream = (PTR) real_fopen (abfd->filename, FOPEN_RUB);
	  if (abfd->iostream == NULL)
	    abfd->iostream = (PTR) real_fopen (abfd->filename, FOPEN_WUB);
	}
      else
	{
	  /* Create the file.

	     Some operating systems won't let us overwrite a running
	     binary.  For them, we want to unlink the file first.

	     However, gcc 2.95 will create temporary files using
	     O_EXCL and tight permissions to prevent other users from
	     substituting other .o files during the compilation.  gcc
	     will then tell the assembler to use the newly created
	     file as an output file.  If we unlink the file here, we
	     open a brief window when another user could still
	     substitute a file.

	     So we unlink the output file if and only if it has
	     non-zero size.  */
#ifndef __MSDOS__
	  /* Don't do this for MSDOS: it doesn't care about overwriting
	     a running binary, but if this file is already open by
	     another BFD, we will be in deep trouble if we delete an
	     open file.  In fact, objdump does just that if invoked with
	     the --info option.  */
	  struct stat s;

	  if (stat (abfd->filename, &s) == 0 && s.st_size != 0)
	    unlink_if_ordinary (abfd->filename);
#endif
	  abfd->iostream = (PTR) real_fopen (abfd->filename, FOPEN_WUB);
	  abfd->opened_once = TRUE;
	}
      break;
    }

  if (abfd->iostream == NULL)
    bfd_set_error (bfd_error_system_call);
  else
    {
      if (! bfd_cache_init (abfd))
	return NULL;
    }

  return (FILE *) abfd->iostream;
}
