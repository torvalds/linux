/* opncls.c -- open and close a BFD.
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 2000,
   2001, 2002, 2003, 2004, 2005, 2006, 2007
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
#include "objalloc.h"
#include "libbfd.h"
#include "libiberty.h"

#ifndef S_IXUSR
#define S_IXUSR 0100	/* Execute by owner.  */
#endif
#ifndef S_IXGRP
#define S_IXGRP 0010	/* Execute by group.  */
#endif
#ifndef S_IXOTH
#define S_IXOTH 0001	/* Execute by others.  */
#endif

/* Counter used to initialize the bfd identifier.  */

static unsigned int _bfd_id_counter = 0;

/* fdopen is a loser -- we should use stdio exclusively.  Unfortunately
   if we do that we can't use fcntl.  */

/* Return a new BFD.  All BFD's are allocated through this routine.  */

bfd *
_bfd_new_bfd (void)
{
  bfd *nbfd;

  nbfd = bfd_zmalloc (sizeof (bfd));
  if (nbfd == NULL)
    return NULL;

  nbfd->id = _bfd_id_counter++;

  nbfd->memory = objalloc_create ();
  if (nbfd->memory == NULL)
    {
      bfd_set_error (bfd_error_no_memory);
      free (nbfd);
      return NULL;
    }

  nbfd->arch_info = &bfd_default_arch_struct;

  nbfd->direction = no_direction;
  nbfd->iostream = NULL;
  nbfd->where = 0;
  if (!bfd_hash_table_init_n (& nbfd->section_htab, bfd_section_hash_newfunc,
			      sizeof (struct section_hash_entry), 251))
    {
      free (nbfd);
      return NULL;
    }
  nbfd->sections = NULL;
  nbfd->section_last = NULL;
  nbfd->format = bfd_unknown;
  nbfd->my_archive = NULL;
  nbfd->origin = 0;
  nbfd->opened_once = FALSE;
  nbfd->output_has_begun = FALSE;
  nbfd->section_count = 0;
  nbfd->usrdata = NULL;
  nbfd->cacheable = FALSE;
  nbfd->flags = BFD_NO_FLAGS;
  nbfd->mtime_set = FALSE;

  return nbfd;
}

/* Allocate a new BFD as a member of archive OBFD.  */

bfd *
_bfd_new_bfd_contained_in (bfd *obfd)
{
  bfd *nbfd;

  nbfd = _bfd_new_bfd ();
  if (nbfd == NULL)
    return NULL;
  nbfd->xvec = obfd->xvec;
  nbfd->iovec = obfd->iovec;
  nbfd->my_archive = obfd;
  nbfd->direction = read_direction;
  nbfd->target_defaulted = obfd->target_defaulted;
  return nbfd;
}

/* Delete a BFD.  */

void
_bfd_delete_bfd (bfd *abfd)
{
  if (abfd->memory)
    {
      bfd_hash_table_free (&abfd->section_htab);
      objalloc_free ((struct objalloc *) abfd->memory);
    }
  free (abfd);
}

/* Free objalloc memory.  */

bfd_boolean
_bfd_free_cached_info (bfd *abfd)
{
  if (abfd->memory)
    {
      bfd_hash_table_free (&abfd->section_htab);
      objalloc_free ((struct objalloc *) abfd->memory);

      abfd->sections = NULL;
      abfd->section_last = NULL;
      abfd->outsymbols = NULL;
      abfd->tdata.any = NULL;
      abfd->usrdata = NULL;
      abfd->memory = NULL;
    }

  return TRUE;
}

/*
SECTION
	Opening and closing BFDs

SUBSECTION
	Functions for opening and closing
*/

/*
FUNCTION
	bfd_fopen

SYNOPSIS
	bfd *bfd_fopen (const char *filename, const char *target,
                        const char *mode, int fd);

DESCRIPTION
	Open the file @var{filename} with the target @var{target}.
	Return a pointer to the created BFD.  If @var{fd} is not -1,
	then <<fdopen>> is used to open the file; otherwise, <<fopen>>
	is used.  @var{mode} is passed directly to <<fopen>> or
	<<fdopen>>. 

	Calls <<bfd_find_target>>, so @var{target} is interpreted as by
	that function.

	The new BFD is marked as cacheable iff @var{fd} is -1.

	If <<NULL>> is returned then an error has occured.   Possible errors
	are <<bfd_error_no_memory>>, <<bfd_error_invalid_target>> or
	<<system_call>> error.
*/

bfd *
bfd_fopen (const char *filename, const char *target, const char *mode, int fd)
{
  bfd *nbfd;
  const bfd_target *target_vec;

  nbfd = _bfd_new_bfd ();
  if (nbfd == NULL)
    return NULL;

  target_vec = bfd_find_target (target, nbfd);
  if (target_vec == NULL)
    {
      _bfd_delete_bfd (nbfd);
      return NULL;
    }
  
#ifdef HAVE_FDOPEN
  if (fd != -1)
    nbfd->iostream = fdopen (fd, mode);
  else
#endif
    nbfd->iostream = real_fopen (filename, mode);
  if (nbfd->iostream == NULL)
    {
      bfd_set_error (bfd_error_system_call);
      _bfd_delete_bfd (nbfd);
      return NULL;
    }

  /* OK, put everything where it belongs.  */
  nbfd->filename = filename;

  /* Figure out whether the user is opening the file for reading,
     writing, or both, by looking at the MODE argument.  */
  if ((mode[0] == 'r' || mode[0] == 'w' || mode[0] == 'a') 
      && mode[1] == '+')
    nbfd->direction = both_direction;
  else if (mode[0] == 'r')
    nbfd->direction = read_direction;
  else
    nbfd->direction = write_direction;

  if (! bfd_cache_init (nbfd))
    {
      _bfd_delete_bfd (nbfd);
      return NULL;
    }
  nbfd->opened_once = TRUE;
  /* If we opened the file by name, mark it cacheable; we can close it
     and reopen it later.  However, if a file descriptor was provided,
     then it may have been opened with special flags that make it
     unsafe to close and reopen the file.  */
  if (fd == -1)
    (void) bfd_set_cacheable (nbfd, TRUE);

  return nbfd;
}

/*
FUNCTION
	bfd_openr

SYNOPSIS
	bfd *bfd_openr (const char *filename, const char *target);

DESCRIPTION
	Open the file @var{filename} (using <<fopen>>) with the target
	@var{target}.  Return a pointer to the created BFD.

	Calls <<bfd_find_target>>, so @var{target} is interpreted as by
	that function.

	If <<NULL>> is returned then an error has occured.   Possible errors
	are <<bfd_error_no_memory>>, <<bfd_error_invalid_target>> or
	<<system_call>> error.
*/

bfd *
bfd_openr (const char *filename, const char *target)
{
  return bfd_fopen (filename, target, FOPEN_RB, -1);
}

/* Don't try to `optimize' this function:

   o - We lock using stack space so that interrupting the locking
       won't cause a storage leak.
   o - We open the file stream last, since we don't want to have to
       close it if anything goes wrong.  Closing the stream means closing
       the file descriptor too, even though we didn't open it.  */
/*
FUNCTION
	bfd_fdopenr

SYNOPSIS
	bfd *bfd_fdopenr (const char *filename, const char *target, int fd);

DESCRIPTION
	<<bfd_fdopenr>> is to <<bfd_fopenr>> much like <<fdopen>> is to
	<<fopen>>.  It opens a BFD on a file already described by the
	@var{fd} supplied.

	When the file is later <<bfd_close>>d, the file descriptor will
	be closed.  If the caller desires that this file descriptor be
	cached by BFD (opened as needed, closed as needed to free
	descriptors for other opens), with the supplied @var{fd} used as
	an initial file descriptor (but subject to closure at any time),
	call bfd_set_cacheable(bfd, 1) on the returned BFD.  The default
	is to assume no caching; the file descriptor will remain open
	until <<bfd_close>>, and will not be affected by BFD operations
	on other files.

	Possible errors are <<bfd_error_no_memory>>,
	<<bfd_error_invalid_target>> and <<bfd_error_system_call>>.
*/

bfd *
bfd_fdopenr (const char *filename, const char *target, int fd)
{
  const char *mode;
#if defined(HAVE_FCNTL) && defined(F_GETFL)
  int fdflags;
#endif

#if ! defined(HAVE_FCNTL) || ! defined(F_GETFL)
  mode = FOPEN_RUB; /* Assume full access.  */
#else
  fdflags = fcntl (fd, F_GETFL, NULL);
  if (fdflags == -1)
    {
      bfd_set_error (bfd_error_system_call);
      return NULL;
    }

  /* (O_ACCMODE) parens are to avoid Ultrix header file bug.  */
  switch (fdflags & (O_ACCMODE))
    {
    case O_RDONLY: mode = FOPEN_RB; break;
    case O_WRONLY: mode = FOPEN_RUB; break;
    case O_RDWR:   mode = FOPEN_RUB; break;
    default: abort ();
    }
#endif

  return bfd_fopen (filename, target, mode, fd);
}

/*
FUNCTION
	bfd_openstreamr

SYNOPSIS
	bfd *bfd_openstreamr (const char *, const char *, void *);

DESCRIPTION

	Open a BFD for read access on an existing stdio stream.  When
	the BFD is passed to <<bfd_close>>, the stream will be closed.
*/

bfd *
bfd_openstreamr (const char *filename, const char *target, void *streamarg)
{
  FILE *stream = streamarg;
  bfd *nbfd;
  const bfd_target *target_vec;

  nbfd = _bfd_new_bfd ();
  if (nbfd == NULL)
    return NULL;

  target_vec = bfd_find_target (target, nbfd);
  if (target_vec == NULL)
    {
      _bfd_delete_bfd (nbfd);
      return NULL;
    }

  nbfd->iostream = stream;
  nbfd->filename = filename;
  nbfd->direction = read_direction;

  if (! bfd_cache_init (nbfd))
    {
      _bfd_delete_bfd (nbfd);
      return NULL;
    }

  return nbfd;
}

/*
FUNCTION
	bfd_openr_iovec

SYNOPSIS
        bfd *bfd_openr_iovec (const char *filename, const char *target,
                              void *(*open) (struct bfd *nbfd,
                                             void *open_closure),
                              void *open_closure,
                              file_ptr (*pread) (struct bfd *nbfd,
                                                 void *stream,
                                                 void *buf,
                                                 file_ptr nbytes,
                                                 file_ptr offset),
                              int (*close) (struct bfd *nbfd,
                                            void *stream),
			      int (*stat) (struct bfd *abfd,
					   void *stream,
					   struct stat *sb));

DESCRIPTION

        Create and return a BFD backed by a read-only @var{stream}.
        The @var{stream} is created using @var{open}, accessed using
        @var{pread} and destroyed using @var{close}.

	Calls <<bfd_find_target>>, so @var{target} is interpreted as by
	that function.

	Calls @var{open} (which can call <<bfd_zalloc>> and
	<<bfd_get_filename>>) to obtain the read-only stream backing
	the BFD.  @var{open} either succeeds returning the
	non-<<NULL>> @var{stream}, or fails returning <<NULL>>
	(setting <<bfd_error>>).

	Calls @var{pread} to request @var{nbytes} of data from
	@var{stream} starting at @var{offset} (e.g., via a call to
	<<bfd_read>>).  @var{pread} either succeeds returning the
	number of bytes read (which can be less than @var{nbytes} when
	end-of-file), or fails returning -1 (setting <<bfd_error>>).

	Calls @var{close} when the BFD is later closed using
	<<bfd_close>>.  @var{close} either succeeds returning 0, or
	fails returning -1 (setting <<bfd_error>>).

	Calls @var{stat} to fill in a stat structure for bfd_stat,
	bfd_get_size, and bfd_get_mtime calls.  @var{stat} returns 0
	on success, or returns -1 on failure (setting <<bfd_error>>).

	If <<bfd_openr_iovec>> returns <<NULL>> then an error has
	occurred.  Possible errors are <<bfd_error_no_memory>>,
	<<bfd_error_invalid_target>> and <<bfd_error_system_call>>.

*/

struct opncls
{
  void *stream;
  file_ptr (*pread) (struct bfd *abfd, void *stream, void *buf,
		     file_ptr nbytes, file_ptr offset);
  int (*close) (struct bfd *abfd, void *stream);
  int (*stat) (struct bfd *abfd, void *stream, struct stat *sb);
  file_ptr where;
};

static file_ptr
opncls_btell (struct bfd *abfd)
{
  struct opncls *vec = abfd->iostream;
  return vec->where;
}

static int
opncls_bseek (struct bfd *abfd, file_ptr offset, int whence)
{
  struct opncls *vec = abfd->iostream;
  switch (whence)
    {
    case SEEK_SET: vec->where = offset; break;
    case SEEK_CUR: vec->where += offset; break;
    case SEEK_END: return -1;
    }
  return 0;
}

static file_ptr
opncls_bread (struct bfd *abfd, void *buf, file_ptr nbytes)
{
  struct opncls *vec = abfd->iostream;
  file_ptr nread = (vec->pread) (abfd, vec->stream, buf, nbytes, vec->where);
  if (nread < 0)
    return nread;
  vec->where += nread;
  return nread;
}

static file_ptr
opncls_bwrite (struct bfd *abfd ATTRIBUTE_UNUSED,
	      const void *where ATTRIBUTE_UNUSED,
	      file_ptr nbytes ATTRIBUTE_UNUSED)
{
  return -1;
}

static int
opncls_bclose (struct bfd *abfd)
{
  struct opncls *vec = abfd->iostream;
  /* Since the VEC's memory is bound to the bfd deleting the bfd will
     free it.  */
  int status = 0;
  if (vec->close != NULL)
    status = (vec->close) (abfd, vec->stream);
  abfd->iostream = NULL;
  return status;
}

static int
opncls_bflush (struct bfd *abfd ATTRIBUTE_UNUSED)
{
  return 0;
}

static int
opncls_bstat (struct bfd *abfd, struct stat *sb)
{
  struct opncls *vec = abfd->iostream;

  memset (sb, 0, sizeof (*sb));
  if (vec->stat == NULL)
    return 0;

  return (vec->stat) (abfd, vec->stream, sb);
}

static const struct bfd_iovec opncls_iovec = {
  &opncls_bread, &opncls_bwrite, &opncls_btell, &opncls_bseek,
  &opncls_bclose, &opncls_bflush, &opncls_bstat
};

bfd *
bfd_openr_iovec (const char *filename, const char *target,
		 void *(*open) (struct bfd *nbfd,
				void *open_closure),
		 void *open_closure,
		 file_ptr (*pread) (struct bfd *abfd,
				    void *stream,
				    void *buf,
				    file_ptr nbytes,
				    file_ptr offset),
		 int (*close) (struct bfd *nbfd,
			       void *stream),
		 int (*stat) (struct bfd *abfd,
			      void *stream,
			      struct stat *sb))
{
  bfd *nbfd;
  const bfd_target *target_vec;
  struct opncls *vec;
  void *stream;

  nbfd = _bfd_new_bfd ();
  if (nbfd == NULL)
    return NULL;

  target_vec = bfd_find_target (target, nbfd);
  if (target_vec == NULL)
    {
      _bfd_delete_bfd (nbfd);
      return NULL;
    }

  nbfd->filename = filename;
  nbfd->direction = read_direction;

  stream = open (nbfd, open_closure);
  if (stream == NULL)
    {
      _bfd_delete_bfd (nbfd);
      return NULL;
    }

  vec = bfd_zalloc (nbfd, sizeof (struct opncls));
  vec->stream = stream;
  vec->pread = pread;
  vec->close = close;
  vec->stat = stat;

  nbfd->iovec = &opncls_iovec;
  nbfd->iostream = vec;

  return nbfd;
}

/* bfd_openw -- open for writing.
   Returns a pointer to a freshly-allocated BFD on success, or NULL.

   See comment by bfd_fdopenr before you try to modify this function.  */

/*
FUNCTION
	bfd_openw

SYNOPSIS
	bfd *bfd_openw (const char *filename, const char *target);

DESCRIPTION
	Create a BFD, associated with file @var{filename}, using the
	file format @var{target}, and return a pointer to it.

	Possible errors are <<bfd_error_system_call>>, <<bfd_error_no_memory>>,
	<<bfd_error_invalid_target>>.
*/

bfd *
bfd_openw (const char *filename, const char *target)
{
  bfd *nbfd;
  const bfd_target *target_vec;

  /* nbfd has to point to head of malloc'ed block so that bfd_close may
     reclaim it correctly.  */
  nbfd = _bfd_new_bfd ();
  if (nbfd == NULL)
    return NULL;

  target_vec = bfd_find_target (target, nbfd);
  if (target_vec == NULL)
    {
      _bfd_delete_bfd (nbfd);
      return NULL;
    }

  nbfd->filename = filename;
  nbfd->direction = write_direction;

  if (bfd_open_file (nbfd) == NULL)
    {
      /* File not writeable, etc.  */
      bfd_set_error (bfd_error_system_call);
      _bfd_delete_bfd (nbfd);
      return NULL;
  }

  return nbfd;
}

/*

FUNCTION
	bfd_close

SYNOPSIS
	bfd_boolean bfd_close (bfd *abfd);

DESCRIPTION

	Close a BFD. If the BFD was open for writing, then pending
	operations are completed and the file written out and closed.
	If the created file is executable, then <<chmod>> is called
	to mark it as such.

	All memory attached to the BFD is released.

	The file descriptor associated with the BFD is closed (even
	if it was passed in to BFD by <<bfd_fdopenr>>).

RETURNS
	<<TRUE>> is returned if all is ok, otherwise <<FALSE>>.
*/


bfd_boolean
bfd_close (bfd *abfd)
{
  bfd_boolean ret;

  if (bfd_write_p (abfd))
    {
      if (! BFD_SEND_FMT (abfd, _bfd_write_contents, (abfd)))
	return FALSE;
    }

  if (! BFD_SEND (abfd, _close_and_cleanup, (abfd)))
    return FALSE;

  /* FIXME: cagney/2004-02-15: Need to implement a BFD_IN_MEMORY io
     vector.  */
  if (!(abfd->flags & BFD_IN_MEMORY))
    ret = abfd->iovec->bclose (abfd);
  else
    ret = TRUE;

  /* If the file was open for writing and is now executable,
     make it so.  */
  if (ret
      && abfd->direction == write_direction
      && abfd->flags & EXEC_P)
    {
      struct stat buf;

      if (stat (abfd->filename, &buf) == 0)
	{
	  unsigned int mask = umask (0);

	  umask (mask);
	  chmod (abfd->filename,
		 (0777
		  & (buf.st_mode | ((S_IXUSR | S_IXGRP | S_IXOTH) &~ mask))));
	}
    }

  _bfd_delete_bfd (abfd);

  return ret;
}

/*
FUNCTION
	bfd_close_all_done

SYNOPSIS
	bfd_boolean bfd_close_all_done (bfd *);

DESCRIPTION
	Close a BFD.  Differs from <<bfd_close>> since it does not
	complete any pending operations.  This routine would be used
	if the application had just used BFD for swapping and didn't
	want to use any of the writing code.

	If the created file is executable, then <<chmod>> is called
	to mark it as such.

	All memory attached to the BFD is released.

RETURNS
	<<TRUE>> is returned if all is ok, otherwise <<FALSE>>.
*/

bfd_boolean
bfd_close_all_done (bfd *abfd)
{
  bfd_boolean ret;

  ret = bfd_cache_close (abfd);

  /* If the file was open for writing and is now executable,
     make it so.  */
  if (ret
      && abfd->direction == write_direction
      && abfd->flags & EXEC_P)
    {
      struct stat buf;

      if (stat (abfd->filename, &buf) == 0)
	{
	  unsigned int mask = umask (0);

	  umask (mask);
	  chmod (abfd->filename,
		 (0777
		  & (buf.st_mode | ((S_IXUSR | S_IXGRP | S_IXOTH) &~ mask))));
	}
    }

  _bfd_delete_bfd (abfd);

  return ret;
}

/*
FUNCTION
	bfd_create

SYNOPSIS
	bfd *bfd_create (const char *filename, bfd *templ);

DESCRIPTION
	Create a new BFD in the manner of <<bfd_openw>>, but without
	opening a file. The new BFD takes the target from the target
	used by @var{template}. The format is always set to <<bfd_object>>.
*/

bfd *
bfd_create (const char *filename, bfd *templ)
{
  bfd *nbfd;

  nbfd = _bfd_new_bfd ();
  if (nbfd == NULL)
    return NULL;
  nbfd->filename = filename;
  if (templ)
    nbfd->xvec = templ->xvec;
  nbfd->direction = no_direction;
  bfd_set_format (nbfd, bfd_object);

  return nbfd;
}

/*
FUNCTION
	bfd_make_writable

SYNOPSIS
	bfd_boolean bfd_make_writable (bfd *abfd);

DESCRIPTION
	Takes a BFD as created by <<bfd_create>> and converts it
	into one like as returned by <<bfd_openw>>.  It does this
	by converting the BFD to BFD_IN_MEMORY.  It's assumed that
	you will call <<bfd_make_readable>> on this bfd later.

RETURNS
	<<TRUE>> is returned if all is ok, otherwise <<FALSE>>.
*/

bfd_boolean
bfd_make_writable (bfd *abfd)
{
  struct bfd_in_memory *bim;

  if (abfd->direction != no_direction)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }

  bim = bfd_malloc (sizeof (struct bfd_in_memory));
  abfd->iostream = bim;
  /* bfd_bwrite will grow these as needed.  */
  bim->size = 0;
  bim->buffer = 0;

  abfd->flags |= BFD_IN_MEMORY;
  abfd->direction = write_direction;
  abfd->where = 0;

  return TRUE;
}

/*
FUNCTION
	bfd_make_readable

SYNOPSIS
	bfd_boolean bfd_make_readable (bfd *abfd);

DESCRIPTION
	Takes a BFD as created by <<bfd_create>> and
	<<bfd_make_writable>> and converts it into one like as
	returned by <<bfd_openr>>.  It does this by writing the
	contents out to the memory buffer, then reversing the
	direction.

RETURNS
	<<TRUE>> is returned if all is ok, otherwise <<FALSE>>.  */

bfd_boolean
bfd_make_readable (bfd *abfd)
{
  if (abfd->direction != write_direction || !(abfd->flags & BFD_IN_MEMORY))
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }

  if (! BFD_SEND_FMT (abfd, _bfd_write_contents, (abfd)))
    return FALSE;

  if (! BFD_SEND (abfd, _close_and_cleanup, (abfd)))
    return FALSE;


  abfd->arch_info = &bfd_default_arch_struct;

  abfd->where = 0;
  abfd->format = bfd_unknown;
  abfd->my_archive = NULL;
  abfd->origin = 0;
  abfd->opened_once = FALSE;
  abfd->output_has_begun = FALSE;
  abfd->section_count = 0;
  abfd->usrdata = NULL;
  abfd->cacheable = FALSE;
  abfd->flags = BFD_IN_MEMORY;
  abfd->mtime_set = FALSE;

  abfd->target_defaulted = TRUE;
  abfd->direction = read_direction;
  abfd->sections = 0;
  abfd->symcount = 0;
  abfd->outsymbols = 0;
  abfd->tdata.any = 0;

  bfd_section_list_clear (abfd);
  bfd_check_format (abfd, bfd_object);

  return TRUE;
}

/*
INTERNAL_FUNCTION
	bfd_alloc

SYNOPSIS
	void *bfd_alloc (bfd *abfd, bfd_size_type wanted);

DESCRIPTION
	Allocate a block of @var{wanted} bytes of memory attached to
	<<abfd>> and return a pointer to it.
*/

void *
bfd_alloc (bfd *abfd, bfd_size_type size)
{
  void *ret;

  if (size != (unsigned long) size)
    {
      bfd_set_error (bfd_error_no_memory);
      return NULL;
    }

  ret = objalloc_alloc (abfd->memory, (unsigned long) size);
  if (ret == NULL)
    bfd_set_error (bfd_error_no_memory);
  return ret;
}

/*
INTERNAL_FUNCTION
	bfd_alloc2

SYNOPSIS
	void *bfd_alloc2 (bfd *abfd, bfd_size_type nmemb, bfd_size_type size);

DESCRIPTION
	Allocate a block of @var{nmemb} elements of @var{size} bytes each
	of memory attached to <<abfd>> and return a pointer to it.
*/

void *
bfd_alloc2 (bfd *abfd, bfd_size_type nmemb, bfd_size_type size)
{
  void *ret;

  if ((nmemb | size) >= HALF_BFD_SIZE_TYPE
      && size != 0
      && nmemb > ~(bfd_size_type) 0 / size)
    {
      bfd_set_error (bfd_error_no_memory);
      return NULL;
    }

  size *= nmemb;

  if (size != (unsigned long) size)
    {
      bfd_set_error (bfd_error_no_memory);
      return NULL;
    }

  ret = objalloc_alloc (abfd->memory, (unsigned long) size);
  if (ret == NULL)
    bfd_set_error (bfd_error_no_memory);
  return ret;
}

/*
INTERNAL_FUNCTION
	bfd_zalloc

SYNOPSIS
	void *bfd_zalloc (bfd *abfd, bfd_size_type wanted);

DESCRIPTION
	Allocate a block of @var{wanted} bytes of zeroed memory
	attached to <<abfd>> and return a pointer to it.
*/

void *
bfd_zalloc (bfd *abfd, bfd_size_type size)
{
  void *res;

  res = bfd_alloc (abfd, size);
  if (res)
    memset (res, 0, (size_t) size);
  return res;
}

/*
INTERNAL_FUNCTION
	bfd_zalloc2

SYNOPSIS
	void *bfd_zalloc2 (bfd *abfd, bfd_size_type nmemb, bfd_size_type size);

DESCRIPTION
	Allocate a block of @var{nmemb} elements of @var{size} bytes each
	of zeroed memory attached to <<abfd>> and return a pointer to it.
*/

void *
bfd_zalloc2 (bfd *abfd, bfd_size_type nmemb, bfd_size_type size)
{
  void *res;

  if ((nmemb | size) >= HALF_BFD_SIZE_TYPE
      && size != 0
      && nmemb > ~(bfd_size_type) 0 / size)
    {
      bfd_set_error (bfd_error_no_memory);
      return NULL;
    }

  size *= nmemb;

  res = bfd_alloc (abfd, size);
  if (res)
    memset (res, 0, (size_t) size);
  return res;
}

/* Free a block allocated for a BFD.
   Note:  Also frees all more recently allocated blocks!  */

void
bfd_release (bfd *abfd, void *block)
{
  objalloc_free_block ((struct objalloc *) abfd->memory, block);
}


/*
   GNU Extension: separate debug-info files

   The idea here is that a special section called .gnu_debuglink might be
   embedded in a binary file, which indicates that some *other* file
   contains the real debugging information. This special section contains a
   filename and CRC32 checksum, which we read and resolve to another file,
   if it exists.

   This facilitates "optional" provision of debugging information, without
   having to provide two complete copies of every binary object (with and
   without debug symbols).
*/

#define GNU_DEBUGLINK	".gnu_debuglink"
/*
FUNCTION
	bfd_calc_gnu_debuglink_crc32

SYNOPSIS
	unsigned long bfd_calc_gnu_debuglink_crc32
	  (unsigned long crc, const unsigned char *buf, bfd_size_type len);

DESCRIPTION
	Computes a CRC value as used in the .gnu_debuglink section.
	Advances the previously computed @var{crc} value by computing
	and adding in the crc32 for @var{len} bytes of @var{buf}.

RETURNS
	Return the updated CRC32 value.
*/

unsigned long
bfd_calc_gnu_debuglink_crc32 (unsigned long crc,
			      const unsigned char *buf,
			      bfd_size_type len)
{
  static const unsigned long crc32_table[256] =
    {
      0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419,
      0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4,
      0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07,
      0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
      0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856,
      0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
      0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4,
      0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
      0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3,
      0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a,
      0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599,
      0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
      0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190,
      0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f,
      0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0x0f00f934, 0x9609a88e,
      0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
      0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed,
      0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
      0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3,
      0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
      0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a,
      0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5,
      0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010,
      0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
      0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17,
      0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6,
      0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615,
      0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
      0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 0xf00f9344,
      0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
      0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a,
      0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
      0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1,
      0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c,
      0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef,
      0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
      0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe,
      0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31,
      0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c,
      0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
      0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b,
      0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
      0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1,
      0x18b74777, 0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
      0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278,
      0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7,
      0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66,
      0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
      0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605,
      0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8,
      0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b,
      0x2d02ef8d
    };
  const unsigned char *end;

  crc = ~crc & 0xffffffff;
  for (end = buf + len; buf < end; ++ buf)
    crc = crc32_table[(crc ^ *buf) & 0xff] ^ (crc >> 8);
  return ~crc & 0xffffffff;;
}


/*
INTERNAL_FUNCTION
	get_debug_link_info

SYNOPSIS
	char *get_debug_link_info (bfd *abfd, unsigned long *crc32_out);

DESCRIPTION
	fetch the filename and CRC32 value for any separate debuginfo
	associated with @var{abfd}. Return NULL if no such info found,
	otherwise return filename and update @var{crc32_out}.
*/

static char *
get_debug_link_info (bfd *abfd, unsigned long *crc32_out)
{
  asection *sect;
  unsigned long crc32;
  bfd_byte *contents;
  int crc_offset;
  char *name;

  BFD_ASSERT (abfd);
  BFD_ASSERT (crc32_out);

  sect = bfd_get_section_by_name (abfd, GNU_DEBUGLINK);

  if (sect == NULL)
    return NULL;

  if (!bfd_malloc_and_get_section (abfd, sect, &contents))
    {
      if (contents != NULL)
	free (contents);
      return NULL;
    }

  /* Crc value is stored after the filename, aligned up to 4 bytes.  */
  name = (char *) contents;
  crc_offset = strlen (name) + 1;
  crc_offset = (crc_offset + 3) & ~3;

  crc32 = bfd_get_32 (abfd, contents + crc_offset);

  *crc32_out = crc32;
  return name;
}

/*
INTERNAL_FUNCTION
	separate_debug_file_exists

SYNOPSIS
	bfd_boolean separate_debug_file_exists
	  (char *name, unsigned long crc32);

DESCRIPTION
	Checks to see if @var{name} is a file and if its contents
	match @var{crc32}.
*/

static bfd_boolean
separate_debug_file_exists (const char *name, const unsigned long crc)
{
  static unsigned char buffer [8 * 1024];
  unsigned long file_crc = 0;
  int fd;
  bfd_size_type count;

  BFD_ASSERT (name);

  fd = open (name, O_RDONLY);
  if (fd < 0)
    return FALSE;

  while ((count = read (fd, buffer, sizeof (buffer))) > 0)
    file_crc = bfd_calc_gnu_debuglink_crc32 (file_crc, buffer, count);

  close (fd);

  return crc == file_crc;
}


/*
INTERNAL_FUNCTION
	find_separate_debug_file

SYNOPSIS
	char *find_separate_debug_file (bfd *abfd);

DESCRIPTION
	Searches @var{abfd} for a reference to separate debugging
	information, scans various locations in the filesystem, including
	the file tree rooted at @var{debug_file_directory}, and returns a
	filename of such debugging information if the file is found and has
	matching CRC32.  Returns NULL if no reference to debugging file
	exists, or file cannot be found.
*/

static char *
find_separate_debug_file (bfd *abfd, const char *debug_file_directory)
{
  char *basename;
  char *dir;
  char *debugfile;
  unsigned long crc32;
  int i;

  BFD_ASSERT (abfd);
  if (debug_file_directory == NULL)
    debug_file_directory = ".";

  /* BFD may have been opened from a stream.  */
  if (! abfd->filename)
    return NULL;

  basename = get_debug_link_info (abfd, & crc32);
  if (basename == NULL)
    return NULL;

  if (strlen (basename) < 1)
    {
      free (basename);
      return NULL;
    }

  dir = strdup (abfd->filename);
  if (dir == NULL)
    {
      free (basename);
      return NULL;
    }
  BFD_ASSERT (strlen (dir) != 0);

  /* Strip off filename part.  */
  for (i = strlen (dir) - 1; i >= 0; i--)
    if (IS_DIR_SEPARATOR (dir[i]))
      break;

  dir[i + 1] = '\0';
  BFD_ASSERT (dir[i] == '/' || dir[0] == '\0');

  debugfile = malloc (strlen (debug_file_directory) + 1
		      + strlen (dir)
		      + strlen (".debug/")
		      + strlen (basename)
		      + 1);
  if (debugfile == NULL)
    {
      free (basename);
      free (dir);
      return NULL;
    }

  /* First try in the same directory as the original file:  */
  strcpy (debugfile, dir);
  strcat (debugfile, basename);

  if (separate_debug_file_exists (debugfile, crc32))
    {
      free (basename);
      free (dir);
      return debugfile;
    }

  /* Then try in a subdirectory called .debug.  */
  strcpy (debugfile, dir);
  strcat (debugfile, ".debug/");
  strcat (debugfile, basename);

  if (separate_debug_file_exists (debugfile, crc32))
    {
      free (basename);
      free (dir);
      return debugfile;
    }

  /* Then try in the global debugfile directory.  */
  strcpy (debugfile, debug_file_directory);
  i = strlen (debug_file_directory) - 1;
  if (i > 0
      && debug_file_directory[i] != '/'
      && dir[0] != '/')
    strcat (debugfile, "/");
  strcat (debugfile, dir);
  strcat (debugfile, basename);

  if (separate_debug_file_exists (debugfile, crc32))
    {
      free (basename);
      free (dir);
      return debugfile;
    }

  free (debugfile);
  free (basename);
  free (dir);
  return NULL;
}


/*
FUNCTION
	bfd_follow_gnu_debuglink

SYNOPSIS
	char *bfd_follow_gnu_debuglink (bfd *abfd, const char *dir);

DESCRIPTION

	Takes a BFD and searches it for a .gnu_debuglink section.  If this
	section is found, it examines the section for the name and checksum
	of a '.debug' file containing auxiliary debugging information.  It
	then searches the filesystem for this .debug file in some standard
	locations, including the directory tree rooted at @var{dir}, and if
	found returns the full filename.

	If @var{dir} is NULL, it will search a default path configured into
	libbfd at build time.  [XXX this feature is not currently
	implemented].

RETURNS
	<<NULL>> on any errors or failure to locate the .debug file,
	otherwise a pointer to a heap-allocated string containing the
	filename.  The caller is responsible for freeing this string.
*/

char *
bfd_follow_gnu_debuglink (bfd *abfd, const char *dir)
{
  return find_separate_debug_file (abfd, dir);
}

/*
FUNCTION
	bfd_create_gnu_debuglink_section

SYNOPSIS
	struct bfd_section *bfd_create_gnu_debuglink_section
	  (bfd *abfd, const char *filename);

DESCRIPTION

	Takes a @var{BFD} and adds a .gnu_debuglink section to it.  The section is sized
	to be big enough to contain a link to the specified @var{filename}.

RETURNS
	A pointer to the new section is returned if all is ok.  Otherwise <<NULL>> is
	returned and bfd_error is set.
*/

asection *
bfd_create_gnu_debuglink_section (bfd *abfd, const char *filename)
{
  asection *sect;
  bfd_size_type debuglink_size;
  flagword flags;

  if (abfd == NULL || filename == NULL)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return NULL;
    }

  /* Strip off any path components in filename.  */
  filename = lbasename (filename);

  sect = bfd_get_section_by_name (abfd, GNU_DEBUGLINK);
  if (sect)
    {
      /* Section already exists.  */
      bfd_set_error (bfd_error_invalid_operation);
      return NULL;
    }

  flags = SEC_HAS_CONTENTS | SEC_READONLY | SEC_DEBUGGING;
  sect = bfd_make_section_with_flags (abfd, GNU_DEBUGLINK, flags);
  if (sect == NULL)
    return NULL;

  debuglink_size = strlen (filename) + 1;
  debuglink_size += 3;
  debuglink_size &= ~3;
  debuglink_size += 4;

  if (! bfd_set_section_size (abfd, sect, debuglink_size))
    /* XXX Should we delete the section from the bfd ?  */
    return NULL;

  return sect;
}


/*
FUNCTION
	bfd_fill_in_gnu_debuglink_section

SYNOPSIS
	bfd_boolean bfd_fill_in_gnu_debuglink_section
	  (bfd *abfd, struct bfd_section *sect, const char *filename);

DESCRIPTION

	Takes a @var{BFD} and containing a .gnu_debuglink section @var{SECT}
	and fills in the contents of the section to contain a link to the
	specified @var{filename}.  The filename should be relative to the
	current directory.

RETURNS
	<<TRUE>> is returned if all is ok.  Otherwise <<FALSE>> is returned
	and bfd_error is set.
*/

bfd_boolean
bfd_fill_in_gnu_debuglink_section (bfd *abfd,
				   struct bfd_section *sect,
				   const char *filename)
{
  bfd_size_type debuglink_size;
  unsigned long crc32;
  char * contents;
  bfd_size_type crc_offset;
  FILE * handle;
  static unsigned char buffer[8 * 1024];
  size_t count;

  if (abfd == NULL || sect == NULL || filename == NULL)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }

  /* Make sure that we can read the file.
     XXX - Should we attempt to locate the debug info file using the same
     algorithm as gdb ?  At the moment, since we are creating the
     .gnu_debuglink section, we insist upon the user providing us with a
     correct-for-section-creation-time path, but this need not conform to
     the gdb location algorithm.  */
  handle = real_fopen (filename, FOPEN_RB);
  if (handle == NULL)
    {
      bfd_set_error (bfd_error_system_call);
      return FALSE;
    }

  crc32 = 0;
  while ((count = fread (buffer, 1, sizeof buffer, handle)) > 0)
    crc32 = bfd_calc_gnu_debuglink_crc32 (crc32, buffer, count);
  fclose (handle);

  /* Strip off any path components in filename,
     now that we no longer need them.  */
  filename = lbasename (filename);

  debuglink_size = strlen (filename) + 1;
  debuglink_size += 3;
  debuglink_size &= ~3;
  debuglink_size += 4;

  contents = bfd_zmalloc (debuglink_size);
  if (contents == NULL)
    {
      /* XXX Should we delete the section from the bfd ?  */
      bfd_set_error (bfd_error_no_memory);
      return FALSE;
    }

  strcpy (contents, filename);
  crc_offset = debuglink_size - 4;

  bfd_put_32 (abfd, crc32, contents + crc_offset);

  if (! bfd_set_section_contents (abfd, sect, contents, 0, debuglink_size))
    {
      /* XXX Should we delete the section from the bfd ?  */
      free (contents);
      return FALSE;
    }

  return TRUE;
}
