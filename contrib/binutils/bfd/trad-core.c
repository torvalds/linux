/* BFD back end for traditional Unix core files (U-area and raw sections)
   Copyright 1988, 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Written by John Gilmore of Cygnus Support.

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
#include "libaout.h"           /* BFD a.out internal data structures */

#include <sys/param.h>
#ifdef HAVE_DIRENT_H
# include <dirent.h>
#else
# ifdef HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# ifdef HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# ifdef HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif
#include <signal.h>

#include <sys/user.h>		/* After a.out.h  */

#ifdef TRAD_HEADER
#include TRAD_HEADER
#endif

struct trad_core_struct
{
  asection *data_section;
  asection *stack_section;
  asection *reg_section;
  struct user u;
};

#define core_upage(bfd) (&((bfd)->tdata.trad_core_data->u))
#define core_datasec(bfd) ((bfd)->tdata.trad_core_data->data_section)
#define core_stacksec(bfd) ((bfd)->tdata.trad_core_data->stack_section)
#define core_regsec(bfd) ((bfd)->tdata.trad_core_data->reg_section)

/* forward declarations */

const bfd_target *trad_unix_core_file_p PARAMS ((bfd *abfd));
char * trad_unix_core_file_failing_command PARAMS ((bfd *abfd));
int trad_unix_core_file_failing_signal PARAMS ((bfd *abfd));
#define trad_unix_core_file_matches_executable_p generic_core_file_matches_executable_p
static void swap_abort PARAMS ((void));

/* Handle 4.2-style (and perhaps also sysV-style) core dump file.  */

const bfd_target *
trad_unix_core_file_p (abfd)
     bfd *abfd;

{
  int val;
  struct user u;
  struct trad_core_struct *rawptr;
  bfd_size_type amt;
  flagword flags;

#ifdef TRAD_CORE_USER_OFFSET
  /* If defined, this macro is the file position of the user struct.  */
  if (bfd_seek (abfd, (file_ptr) TRAD_CORE_USER_OFFSET, SEEK_SET) != 0)
    return 0;
#endif

  val = bfd_bread ((void *) &u, (bfd_size_type) sizeof u, abfd);
  if (val != sizeof u)
    {
      /* Too small to be a core file */
      bfd_set_error (bfd_error_wrong_format);
      return 0;
    }

  /* Sanity check perhaps??? */
  if (u.u_dsize > 0x1000000)	/* Remember, it's in pages...  */
    {
      bfd_set_error (bfd_error_wrong_format);
      return 0;
    }
  if (u.u_ssize > 0x1000000)
    {
      bfd_set_error (bfd_error_wrong_format);
      return 0;
    }

  /* Check that the size claimed is no greater than the file size.  */
  {
    struct stat statbuf;

    if (bfd_stat (abfd, &statbuf) < 0)
      return 0;

    if ((ufile_ptr) NBPG * (UPAGES + u.u_dsize
#ifdef TRAD_CORE_DSIZE_INCLUDES_TSIZE
			    - u.u_tsize
#endif
			    + u.u_ssize)
	> (ufile_ptr) statbuf.st_size)
      {
	bfd_set_error (bfd_error_wrong_format);
	return 0;
      }
#ifndef TRAD_CORE_ALLOW_ANY_EXTRA_SIZE
    if (((ufile_ptr) NBPG * (UPAGES + u.u_dsize + u.u_ssize)
#ifdef TRAD_CORE_EXTRA_SIZE_ALLOWED
	/* Some systems write the file too big.  */
	 + TRAD_CORE_EXTRA_SIZE_ALLOWED
#endif
	 )
	< (ufile_ptr) statbuf.st_size)
      {
	/* The file is too big.  Maybe it's not a core file
	   or we otherwise have bad values for u_dsize and u_ssize).  */
	bfd_set_error (bfd_error_wrong_format);
	return 0;
      }
#endif
  }

  /* OK, we believe you.  You're a core file (sure, sure).  */

  /* Allocate both the upage and the struct core_data at once, so
     a single free() will free them both.  */
  amt = sizeof (struct trad_core_struct);
  rawptr = (struct trad_core_struct *) bfd_zmalloc (amt);
  if (rawptr == NULL)
    return 0;

  abfd->tdata.trad_core_data = rawptr;

  rawptr->u = u; /*Copy the uarea into the tdata part of the bfd */

  /* Create the sections.  */

  flags = SEC_ALLOC + SEC_LOAD + SEC_HAS_CONTENTS;
  core_stacksec(abfd) = bfd_make_section_anyway_with_flags (abfd, ".stack",
							    flags);
  if (core_stacksec (abfd) == NULL)
    goto fail;
  core_datasec (abfd) = bfd_make_section_anyway_with_flags (abfd, ".data",
							    flags);
  if (core_datasec (abfd) == NULL)
    goto fail;
  core_regsec (abfd) = bfd_make_section_anyway_with_flags (abfd, ".reg",
							   SEC_HAS_CONTENTS);
  if (core_regsec (abfd) == NULL)
    goto fail;

  core_datasec (abfd)->size =  NBPG * u.u_dsize
#ifdef TRAD_CORE_DSIZE_INCLUDES_TSIZE
    - NBPG * u.u_tsize
#endif
      ;
  core_stacksec (abfd)->size = NBPG * u.u_ssize;
  core_regsec (abfd)->size = NBPG * UPAGES; /* Larger than sizeof struct u */

  /* What a hack... we'd like to steal it from the exec file,
     since the upage does not seem to provide it.  FIXME.  */
#ifdef HOST_DATA_START_ADDR
  core_datasec (abfd)->vma = HOST_DATA_START_ADDR;
#else
  core_datasec (abfd)->vma = HOST_TEXT_START_ADDR + (NBPG * u.u_tsize);
#endif

#ifdef HOST_STACK_START_ADDR
  core_stacksec (abfd)->vma = HOST_STACK_START_ADDR;
#else
  core_stacksec (abfd)->vma = HOST_STACK_END_ADDR - (NBPG * u.u_ssize);
#endif

  /* This is tricky.  As the "register section", we give them the entire
     upage and stack.  u.u_ar0 points to where "register 0" is stored.
     There are two tricks with this, though.  One is that the rest of the
     registers might be at positive or negative (or both) displacements
     from *u_ar0.  The other is that u_ar0 is sometimes an absolute address
     in kernel memory, and on other systems it is an offset from the beginning
     of the `struct user'.

     As a practical matter, we don't know where the registers actually are,
     so we have to pass the whole area to GDB.  We encode the value of u_ar0
     by setting the .regs section up so that its virtual memory address
     0 is at the place pointed to by u_ar0 (by setting the vma of the start
     of the section to -u_ar0).  GDB uses this info to locate the regs,
     using minor trickery to get around the offset-or-absolute-addr problem.  */
  core_regsec (abfd)->vma = - (bfd_vma) (unsigned long) u.u_ar0;

  core_datasec (abfd)->filepos = NBPG * UPAGES;
  core_stacksec (abfd)->filepos = (NBPG * UPAGES) + NBPG * u.u_dsize
#ifdef TRAD_CORE_DSIZE_INCLUDES_TSIZE
    - NBPG * u.u_tsize
#endif
      ;
  core_regsec (abfd)->filepos = 0; /* Register segment is the upage */

  /* Align to word at least */
  core_stacksec (abfd)->alignment_power = 2;
  core_datasec (abfd)->alignment_power = 2;
  core_regsec (abfd)->alignment_power = 2;

  return abfd->xvec;

 fail:
  bfd_release (abfd, abfd->tdata.any);
  abfd->tdata.any = NULL;
  bfd_section_list_clear (abfd);
  return NULL;
}

char *
trad_unix_core_file_failing_command (abfd)
     bfd *abfd;
{
#ifndef NO_CORE_COMMAND
  char *com = abfd->tdata.trad_core_data->u.u_comm;
  if (*com)
    return com;
  else
#endif
    return 0;
}

int
trad_unix_core_file_failing_signal (ignore_abfd)
     bfd *ignore_abfd ATTRIBUTE_UNUSED;
{
#ifdef TRAD_UNIX_CORE_FILE_FAILING_SIGNAL
  return TRAD_UNIX_CORE_FILE_FAILING_SIGNAL(ignore_abfd);
#else
  return -1;		/* FIXME, where is it? */
#endif
}

/* If somebody calls any byte-swapping routines, shoot them.  */
static void
swap_abort ()
{
  abort (); /* This way doesn't require any declaration for ANSI to fuck up */
}

#define	NO_GET ((bfd_vma (*) (const void *)) swap_abort)
#define	NO_PUT ((void (*) (bfd_vma, void *)) swap_abort)
#define	NO_GETS ((bfd_signed_vma (*) (const void *)) swap_abort)
#define	NO_GET64 ((bfd_uint64_t (*) (const void *)) swap_abort)
#define	NO_PUT64 ((void (*) (bfd_uint64_t, void *)) swap_abort)
#define	NO_GETS64 ((bfd_int64_t (*) (const void *)) swap_abort)

const bfd_target trad_core_vec =
  {
    "trad-core",
    bfd_target_unknown_flavour,
    BFD_ENDIAN_UNKNOWN,		/* target byte order */
    BFD_ENDIAN_UNKNOWN,		/* target headers byte order */
    (HAS_RELOC | EXEC_P |	/* object flags */
     HAS_LINENO | HAS_DEBUG |
     HAS_SYMS | HAS_LOCALS | WP_TEXT | D_PAGED),
    (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_RELOC), /* section flags */
    0,			                                   /* symbol prefix */
    ' ',						   /* ar_pad_char */
    16,							   /* ar_max_namelen */
    NO_GET64, NO_GETS64, NO_PUT64,	/* 64 bit data */
    NO_GET, NO_GETS, NO_PUT,		/* 32 bit data */
    NO_GET, NO_GETS, NO_PUT,		/* 16 bit data */
    NO_GET64, NO_GETS64, NO_PUT64,	/* 64 bit hdrs */
    NO_GET, NO_GETS, NO_PUT,		/* 32 bit hdrs */
    NO_GET, NO_GETS, NO_PUT,		/* 16 bit hdrs */

    {				/* bfd_check_format */
      _bfd_dummy_target,		/* unknown format */
      _bfd_dummy_target,		/* object file */
      _bfd_dummy_target,		/* archive */
      trad_unix_core_file_p		/* a core file */
    },
    {				/* bfd_set_format */
      bfd_false, bfd_false,
      bfd_false, bfd_false
    },
    {				/* bfd_write_contents */
      bfd_false, bfd_false,
      bfd_false, bfd_false
    },

    BFD_JUMP_TABLE_GENERIC (_bfd_generic),
    BFD_JUMP_TABLE_COPY (_bfd_generic),
    BFD_JUMP_TABLE_CORE (trad_unix),
    BFD_JUMP_TABLE_ARCHIVE (_bfd_noarchive),
    BFD_JUMP_TABLE_SYMBOLS (_bfd_nosymbols),
    BFD_JUMP_TABLE_RELOCS (_bfd_norelocs),
    BFD_JUMP_TABLE_WRITE (_bfd_generic),
    BFD_JUMP_TABLE_LINK (_bfd_nolink),
    BFD_JUMP_TABLE_DYNAMIC (_bfd_nodynamic),

    NULL,

    (PTR) 0			/* backend_data */
  };
