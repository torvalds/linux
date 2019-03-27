/* IBM RS/6000 "XCOFF" back-end for BFD.
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 2000,
   2001, 2002, 2004, 2006, 2007
   Free Software Foundation, Inc.
   Written by Metin G. Ozisik, Mimi Phuong-Thao Vo, and John Gilmore.
   Archive support from Damon A. Permezel.
   Contributed by IBM Corporation and Cygnus Support.

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

/* This port currently only handles reading object files, except when
   compiled on an RS/6000 host.  -- no archive support, no core files.
   In all cases, it does not support writing.

   This is in a separate file from coff-rs6000.c, because it includes
   system include files that conflict with coff/rs6000.h.  */

/* Internalcoff.h and coffcode.h modify themselves based on this flag.  */
#define RS6000COFF_C 1

/* The AIX 4.1 kernel is obviously compiled with -D_LONG_LONG, so
   we have to define _LONG_LONG for older versions of gcc to get the
   proper alignments in the user structure.  */
#if defined(_AIX41) && !defined(_LONG_LONG)
#define _LONG_LONG
#endif

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"

#ifdef AIX_CORE

/* AOUTHDR is defined by the above.  We need another defn of it, from the
   system include files.  Punt the old one and get us a new name for the
   typedef in the system include files.  */
#ifdef AOUTHDR
#undef AOUTHDR
#endif
#define	AOUTHDR	second_AOUTHDR

#undef	SCNHDR

/* ------------------------------------------------------------------------ */
/*	Support for core file stuff..					    */
/* ------------------------------------------------------------------------ */

#include <sys/user.h>
#define __LDINFO_PTRACE32__	/* for __ld_info32 */
#define __LDINFO_PTRACE64__	/* for __ld_info64 */
#include <sys/ldr.h>
#include <sys/core.h>
#include <sys/systemcfg.h>

/* Borrowed from <sys/inttypes.h> on recent AIX versions.  */
typedef unsigned long ptr_to_uint;

#define	core_hdr(bfd)		((CoreHdr *) bfd->tdata.any)

/* AIX 4.1 changed the names and locations of a few items in the core file.
   AIX 4.3 defined an entirely new structure, core_dumpx, but kept support for
   the previous 4.1 structure, core_dump.

   AIX_CORE_DUMPX_CORE is defined (by configure) on AIX 4.3+, and
   CORE_VERSION_1 is defined (by AIX core.h) as 2 on AIX 4.3+ and as 1 on AIX
   4.1 and 4.2.  AIX pre-4.1 (aka 3.x) either doesn't define CORE_VERSION_1
   or else defines it as 0.  */

#if defined(CORE_VERSION_1) && !CORE_VERSION_1
# undef CORE_VERSION_1
#endif

/* The following union and macros allow this module to compile on all AIX
   versions and to handle both core_dumpx and core_dump on 4.3+.  CNEW_*()
   and COLD_*() macros respectively retrieve core_dumpx and core_dump
   values.  */

/* Union of 32-bit and 64-bit versions of ld_info.  */

typedef union {
#ifdef __ld_info32
  struct __ld_info32 l32;
  struct __ld_info64 l64;
#else
  struct ld_info l32;
  struct ld_info l64;
#endif
} LdInfo;

/* Union of old and new core dump structures.  */

typedef union {
#ifdef AIX_CORE_DUMPX_CORE
  struct core_dumpx new;	/* new AIX 4.3+ core dump */
#else
  struct core_dump new;		/* for simpler coding */
#endif
  struct core_dump old;		/* old AIX 4.2- core dump, still used on
				   4.3+ with appropriate SMIT config */
} CoreHdr;

/* Union of old and new vm_info structures.  */

#ifdef CORE_VERSION_1
typedef union {
#ifdef AIX_CORE_DUMPX_CORE
  struct vm_infox new;
#else
  struct vm_info new;
#endif
  struct vm_info old;
} VmInfo;
#endif

/* Return whether CoreHdr C is in new or old format.  */

#ifdef AIX_CORE_DUMPX_CORE
# define CORE_NEW(c)	(!(c).old.c_entries)
#else
# define CORE_NEW(c)	0
#endif

/* Return the c_stackorg field from struct core_dumpx C.  */

#ifdef AIX_CORE_DUMPX_CORE
# define CNEW_STACKORG(c)	(c).c_stackorg
#else
# define CNEW_STACKORG(c)	0
#endif

/* Return the offset to the loader region from struct core_dump C.  */

#ifdef AIX_CORE_DUMPX_CORE
# define CNEW_LOADER(c)	(c).c_loader
#else
# define CNEW_LOADER(c)	0
#endif

/* Return the offset to the loader region from struct core_dump C.  */

#define COLD_LOADER(c)	(c).c_tab

/* Return the c_lsize field from struct core_dumpx C.  */

#ifdef AIX_CORE_DUMPX_CORE
# define CNEW_LSIZE(c)	(c).c_lsize
#else
# define CNEW_LSIZE(c)	0
#endif

/* Return the c_dataorg field from struct core_dumpx C.  */

#ifdef AIX_CORE_DUMPX_CORE
# define CNEW_DATAORG(c)	(c).c_dataorg
#else
# define CNEW_DATAORG(c)	0
#endif

/* Return the c_datasize field from struct core_dumpx C.  */

#ifdef AIX_CORE_DUMPX_CORE
# define CNEW_DATASIZE(c)	(c).c_datasize
#else
# define CNEW_DATASIZE(c)	0
#endif

/* Return the c_impl field from struct core_dumpx C.  */

#if defined (HAVE_ST_C_IMPL) || defined (AIX_5_CORE)
# define CNEW_IMPL(c)	(c).c_impl
#else
# define CNEW_IMPL(c)	0
#endif

/* Return the command string from struct core_dumpx C.  */

#ifdef AIX_CORE_DUMPX_CORE
# define CNEW_COMM(c)	(c).c_u.U_proc.pi_comm
#else
# define CNEW_COMM(c)	0
#endif

/* Return the command string from struct core_dump C.  */

#ifdef CORE_VERSION_1
# define COLD_COMM(c)	(c).c_u.U_comm
#else
# define COLD_COMM(c)	(c).c_u.u_comm
#endif

/* Return the struct __context64 pointer from struct core_dumpx C.  */

#ifdef AIX_CORE_DUMPX_CORE
# define CNEW_CONTEXT64(c)	(c).c_flt.hctx.r64
#else
# define CNEW_CONTEXT64(c)	c
#endif

/* Return the struct mstsave pointer from struct core_dumpx C.  */

#ifdef AIX_CORE_DUMPX_CORE
# define CNEW_MSTSAVE(c)	(c).c_flt.hctx.r32
#else
# define CNEW_MSTSAVE(c)	c
#endif

/* Return the struct mstsave pointer from struct core_dump C.  */

#ifdef CORE_VERSION_1
# define COLD_MSTSAVE(c)	(c).c_mst
#else
# define COLD_MSTSAVE(c)	(c).c_u.u_save
#endif

/* Return whether struct core_dumpx is from a 64-bit process.  */

#ifdef AIX_CORE_DUMPX_CORE
# define CNEW_PROC64(c)		IS_PROC64(&(c).c_u.U_proc)
#else
# define CNEW_PROC64(c)		0
#endif

/* Magic end-of-stack addresses for old core dumps.  This is _very_ fragile,
   but I don't see any easy way to get that info right now.  */

#ifdef CORE_VERSION_1
# define COLD_STACKEND	0x2ff23000
#else
# define COLD_STACKEND	0x2ff80000
#endif

/* Size of the leading portion that old and new core dump structures have in
   common.  */
#define CORE_COMMONSZ	((int) &((struct core_dump *) 0)->c_entries \
			 + sizeof (((struct core_dump *) 0)->c_entries))

/* Define prototypes for certain functions, to avoid a compiler warning
   saying that they are missing.  */

const bfd_target * rs6000coff_core_p (bfd *abfd);
bfd_boolean rs6000coff_core_file_matches_executable_p (bfd *core_bfd,
                                                       bfd *exec_bfd);
char * rs6000coff_core_file_failing_command (bfd *abfd);
int rs6000coff_core_file_failing_signal (bfd *abfd);

/* Try to read into CORE the header from the core file associated with ABFD.
   Return success.  */

static bfd_boolean
read_hdr (bfd *abfd, CoreHdr *core)
{
  bfd_size_type size;

  if (bfd_seek (abfd, (file_ptr) 0, SEEK_SET) != 0)
    return FALSE;

  /* Read the leading portion that old and new core dump structures have in
     common.  */
  size = CORE_COMMONSZ;
  if (bfd_bread (core, size, abfd) != size)
    return FALSE;

  /* Read the trailing portion of the structure.  */
  if (CORE_NEW (*core))
    size = sizeof (core->new);
  else
    size = sizeof (core->old);
  size -= CORE_COMMONSZ;
  return bfd_bread ((char *) core + CORE_COMMONSZ, size, abfd) == size;
}

static asection *
make_bfd_asection (bfd *abfd, const char *name, flagword flags,
		   bfd_size_type size, bfd_vma vma, file_ptr filepos)
{
  asection *asect;

  asect = bfd_make_section_anyway_with_flags (abfd, name, flags);
  if (!asect)
    return NULL;

  asect->size = size;
  asect->vma = vma;
  asect->filepos = filepos;
  asect->alignment_power = 8;

  return asect;
}

/* Decide if a given bfd represents a `core' file or not. There really is no
   magic number or anything like, in rs6000coff.  */

const bfd_target *
rs6000coff_core_p (bfd *abfd)
{
  CoreHdr core;
  struct stat statbuf;
  bfd_size_type size;
  char *tmpptr;

  /* Values from new and old core structures.  */
  int c_flag;
  file_ptr c_stack, c_regoff, c_loader;
  bfd_size_type c_size, c_regsize, c_lsize;
  bfd_vma c_stackend;
  void *c_regptr;
  int proc64;

  if (!read_hdr (abfd, &core))
    {
      if (bfd_get_error () != bfd_error_system_call)
	bfd_set_error (bfd_error_wrong_format);
      return NULL;
    }

  /* Copy fields from new or old core structure.  */
  if (CORE_NEW (core))
    {
      c_flag = core.new.c_flag;
      c_stack = (file_ptr) core.new.c_stack;
      c_size = core.new.c_size;
      c_stackend = CNEW_STACKORG (core.new) + c_size;
      c_lsize = CNEW_LSIZE (core.new);
      c_loader = CNEW_LOADER (core.new);
      proc64 = CNEW_PROC64 (core.new);
    }
  else
    {
      c_flag = core.old.c_flag;
      c_stack = (file_ptr) (ptr_to_uint) core.old.c_stack;
      c_size = core.old.c_size;
      c_stackend = COLD_STACKEND;
      c_lsize = 0x7ffffff;
      c_loader = (file_ptr) (ptr_to_uint) COLD_LOADER (core.old);
      proc64 = 0;
    }

  if (proc64)
    {
      c_regsize = sizeof (CNEW_CONTEXT64 (core.new));
      c_regptr = &CNEW_CONTEXT64 (core.new);
    }
  else if (CORE_NEW (core))
    {
      c_regsize = sizeof (CNEW_MSTSAVE (core.new));
      c_regptr = &CNEW_MSTSAVE (core.new);
    }
  else
    {
      c_regsize = sizeof (COLD_MSTSAVE (core.old));
      c_regptr = &COLD_MSTSAVE (core.old);
    }
  c_regoff = (char *) c_regptr - (char *) &core;

  if (bfd_stat (abfd, &statbuf) < 0)
    {
      bfd_set_error (bfd_error_system_call);
      return NULL;
    }

  /* If the core file ulimit is too small, the system will first
     omit the data segment, then omit the stack, then decline to
     dump core altogether (as far as I know UBLOCK_VALID and LE_VALID
     are always set) (this is based on experimentation on AIX 3.2).
     Now, the thing is that GDB users will be surprised
     if segments just silently don't appear (well, maybe they would
     think to check "info files", I don't know).

     For the data segment, we have no choice but to keep going if it's
     not there, since the default behavior is not to dump it (regardless
     of the ulimit, it's based on SA_FULLDUMP).  But for the stack segment,
     if it's not there, we refuse to have anything to do with this core
     file.  The usefulness of a core dump without a stack segment is pretty
     limited anyway.  */

  if (!(c_flag & UBLOCK_VALID)
      || !(c_flag & LE_VALID))
    {
      bfd_set_error (bfd_error_wrong_format);
      return NULL;
    }

  if (!(c_flag & USTACK_VALID))
    {
      bfd_set_error (bfd_error_file_truncated);
      return NULL;
    }

  /* Don't check the core file size for a full core, AIX 4.1 includes
     additional shared library sections in a full core.  */
  if (!(c_flag & (FULL_CORE | CORE_TRUNC)))
    {
      /* If the size is wrong, it means we're misinterpreting something.  */
      if (c_stack + (file_ptr) c_size != statbuf.st_size)
	{
	  bfd_set_error (bfd_error_wrong_format);
	  return NULL;
	}
    }

  /* Sanity check on the c_tab field.  */
  if (!CORE_NEW (core) && (c_loader < (file_ptr) sizeof core.old ||
			   c_loader >= statbuf.st_size ||
			   c_loader >= c_stack))
    {
      bfd_set_error (bfd_error_wrong_format);
      return NULL;
    }

  /* Issue warning if the core file was truncated during writing.  */
  if (c_flag & CORE_TRUNC)
    (*_bfd_error_handler) (_("%s: warning core file truncated"),
			   bfd_get_filename (abfd));

  /* Allocate core file header.  */
  size = CORE_NEW (core) ? sizeof (core.new) : sizeof (core.old);
  tmpptr = (char *) bfd_zalloc (abfd, (bfd_size_type) size);
  if (!tmpptr)
    return NULL;

  /* Copy core file header.  */
  memcpy (tmpptr, &core, size);
  set_tdata (abfd, tmpptr);

  /* Set architecture.  */
  if (CORE_NEW (core))
    {
      enum bfd_architecture arch;
      unsigned long mach;

      switch (CNEW_IMPL (core.new))
	{
	case POWER_RS1:
	case POWER_RSC:
	case POWER_RS2:
	  arch = bfd_arch_rs6000;
	  mach = bfd_mach_rs6k;
	  break;
	default:
	  arch = bfd_arch_powerpc;
	  mach = bfd_mach_ppc;
	  break;
	}
      bfd_default_set_arch_mach (abfd, arch, mach);
    }

  /* .stack section.  */
  if (!make_bfd_asection (abfd, ".stack",
			  SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS,
			  c_size, c_stackend - c_size, c_stack))
    goto fail;

  /* .reg section for all registers.  */
  if (!make_bfd_asection (abfd, ".reg",
			  SEC_HAS_CONTENTS,
			  c_regsize, (bfd_vma) 0, c_regoff))
    goto fail;

  /* .ldinfo section.
     To actually find out how long this section is in this particular
     core dump would require going down the whole list of struct ld_info's.
     See if we can just fake it.  */
  if (!make_bfd_asection (abfd, ".ldinfo",
			  SEC_HAS_CONTENTS,
			  c_lsize, (bfd_vma) 0, c_loader))
    goto fail;

#ifndef CORE_VERSION_1
  /* .data section if present.
     AIX 3 dumps the complete data section and sets FULL_CORE if the
     ulimit is large enough, otherwise the data section is omitted.
     AIX 4 sets FULL_CORE even if the core file is truncated, we have
     to examine core.c_datasize below to find out the actual size of
     the .data section.  */
  if (c_flag & FULL_CORE)
    {
      if (!make_bfd_asection (abfd, ".data",
			      SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS,
			      (bfd_size_type) core.old.c_u.u_dsize,
			      (bfd_vma)
				CDATA_ADDR (core.old.c_u.u_dsize),
			      c_stack + c_size))
	goto fail;
    }
#endif

#ifdef CORE_VERSION_1
  /* AIX 4 adds data sections from loaded objects to the core file,
     which can be found by examining ldinfo, and anonymously mmapped
     regions.  */
  {
    LdInfo ldinfo;
    bfd_size_type ldi_datasize;
    file_ptr ldi_core;
    uint ldi_next;
    bfd_vma ldi_dataorg;

    /* Fields from new and old core structures.  */
    bfd_size_type c_datasize, c_vmregions;
    file_ptr c_data, c_vmm;

    if (CORE_NEW (core))
      {
	c_datasize = CNEW_DATASIZE (core.new);
	c_data = (file_ptr) core.new.c_data;
	c_vmregions = core.new.c_vmregions;
	c_vmm = (file_ptr) core.new.c_vmm;
      }
    else
      {
	c_datasize = core.old.c_datasize;
	c_data = (file_ptr) (ptr_to_uint) core.old.c_data;
	c_vmregions = core.old.c_vmregions;
	c_vmm = (file_ptr) (ptr_to_uint) core.old.c_vmm;
      }

    /* .data section from executable.  */
    if (c_datasize)
      {
	if (!make_bfd_asection (abfd, ".data",
				SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS,
				c_datasize,
				(bfd_vma) CDATA_ADDR (c_datasize),
				c_data))
	  goto fail;
      }

    /* .data sections from loaded objects.  */
    if (proc64)
      size = (int) ((LdInfo *) 0)->l64.ldinfo_filename;
    else
      size = (int) ((LdInfo *) 0)->l32.ldinfo_filename;

    while (1)
      {
	if (bfd_seek (abfd, c_loader, SEEK_SET) != 0)
	  goto fail;
	if (bfd_bread (&ldinfo, size, abfd) != size)
	  goto fail;

	if (proc64)
	  {
	    ldi_core = ldinfo.l64.ldinfo_core;
	    ldi_datasize = ldinfo.l64.ldinfo_datasize;
	    ldi_dataorg = (bfd_vma) ldinfo.l64.ldinfo_dataorg;
	    ldi_next = ldinfo.l64.ldinfo_next;
	  }
	else
	  {
	    ldi_core = ldinfo.l32.ldinfo_core;
	    ldi_datasize = ldinfo.l32.ldinfo_datasize;
	    ldi_dataorg = (bfd_vma) (long) ldinfo.l32.ldinfo_dataorg;
	    ldi_next = ldinfo.l32.ldinfo_next;
	  }

	if (ldi_core)
	  if (!make_bfd_asection (abfd, ".data",
				  SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS,
				  ldi_datasize, ldi_dataorg, ldi_core))
	    goto fail;

	if (ldi_next == 0)
	  break;
	c_loader += ldi_next;
      }

    /* .vmdata sections from anonymously mmapped regions.  */
    if (c_vmregions)
      {
	bfd_size_type i;

	if (bfd_seek (abfd, c_vmm, SEEK_SET) != 0)
	  goto fail;

	for (i = 0; i < c_vmregions; i++)
	  {
	    VmInfo vminfo;
	    bfd_size_type vminfo_size;
	    file_ptr vminfo_offset;
	    bfd_vma vminfo_addr;

	    size = CORE_NEW (core) ? sizeof (vminfo.new) : sizeof (vminfo.old);
	    if (bfd_bread (&vminfo, size, abfd) != size)
	      goto fail;

	    if (CORE_NEW (core))
	      {
		vminfo_addr = (bfd_vma) vminfo.new.vminfo_addr;
		vminfo_size = vminfo.new.vminfo_size;
		vminfo_offset = vminfo.new.vminfo_offset;
	      }
	    else
	      {
		vminfo_addr = (bfd_vma) (long) vminfo.old.vminfo_addr;
		vminfo_size = vminfo.old.vminfo_size;
		vminfo_offset = vminfo.old.vminfo_offset;
	      }

	    if (vminfo_offset)
	      if (!make_bfd_asection (abfd, ".vmdata",
				      SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS,
				      vminfo_size, vminfo_addr,
				      vminfo_offset))
		goto fail;
	  }
      }
  }
#endif

  return abfd->xvec;		/* This is garbage for now.  */

 fail:
  bfd_release (abfd, abfd->tdata.any);
  abfd->tdata.any = NULL;
  bfd_section_list_clear (abfd);
  return NULL;
}

/* Return `TRUE' if given core is from the given executable.  */

bfd_boolean
rs6000coff_core_file_matches_executable_p (bfd *core_bfd, bfd *exec_bfd)
{
  CoreHdr core;
  bfd_size_type size;
  char *path, *s;
  size_t alloc;
  const char *str1, *str2;
  bfd_boolean ret;
  file_ptr c_loader;

  if (!read_hdr (core_bfd, &core))
    return FALSE;

  if (CORE_NEW (core))
    c_loader = CNEW_LOADER (core.new);
  else
    c_loader = (file_ptr) (ptr_to_uint) COLD_LOADER (core.old);

  if (CORE_NEW (core) && CNEW_PROC64 (core.new))
    size = (int) ((LdInfo *) 0)->l64.ldinfo_filename;
  else
    size = (int) ((LdInfo *) 0)->l32.ldinfo_filename;

  if (bfd_seek (core_bfd, c_loader + size, SEEK_SET) != 0)
    return FALSE;

  alloc = 100;
  path = bfd_malloc ((bfd_size_type) alloc);
  if (path == NULL)
    return FALSE;
  s = path;

  while (1)
    {
      if (bfd_bread (s, (bfd_size_type) 1, core_bfd) != 1)
	{
	  free (path);
	  return FALSE;
	}
      if (*s == '\0')
	break;
      ++s;
      if (s == path + alloc)
	{
	  char *n;

	  alloc *= 2;
	  n = bfd_realloc (path, (bfd_size_type) alloc);
	  if (n == NULL)
	    {
	      free (path);
	      return FALSE;
	    }
	  s = n + (path - s);
	  path = n;
	}
    }

  str1 = strrchr (path, '/');
  str2 = strrchr (exec_bfd->filename, '/');

  /* step over character '/' */
  str1 = str1 != NULL ? str1 + 1 : path;
  str2 = str2 != NULL ? str2 + 1 : exec_bfd->filename;

  if (strcmp (str1, str2) == 0)
    ret = TRUE;
  else
    ret = FALSE;

  free (path);

  return ret;
}

char *
rs6000coff_core_file_failing_command (bfd *abfd)
{
  CoreHdr *core = core_hdr (abfd);
  char *com = CORE_NEW (*core) ?
    CNEW_COMM (core->new) : COLD_COMM (core->old);

  if (*com)
    return com;
  else
    return 0;
}

int
rs6000coff_core_file_failing_signal (bfd *abfd)
{
  CoreHdr *core = core_hdr (abfd);
  return CORE_NEW (*core) ? core->new.c_signo : core->old.c_signo;
}

#endif /* AIX_CORE */
