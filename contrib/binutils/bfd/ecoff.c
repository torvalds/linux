/* Generic ECOFF (Extended-COFF) routines.
   Copyright 1990, 1991, 1993, 1994, 1995, 1996, 1998, 1999, 2000, 2001,
   2002, 2003, 2004, 2005, 2006, 2007 Free Software Foundation, Inc.
   Original version by Per Bothner.
   Full support added by Ian Lance Taylor, ian@cygnus.com.

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
#include "bfdlink.h"
#include "libbfd.h"
#include "aout/ar.h"
#include "aout/ranlib.h"
#include "aout/stab_gnu.h"

/* FIXME: We need the definitions of N_SET[ADTB], but aout64.h defines
   some other stuff which we don't want and which conflicts with stuff
   we do want.  */
#include "libaout.h"
#include "aout/aout64.h"
#undef N_ABS
#undef exec_hdr
#undef obj_sym_filepos

#include "coff/internal.h"
#include "coff/sym.h"
#include "coff/symconst.h"
#include "coff/ecoff.h"
#include "libcoff.h"
#include "libecoff.h"
#include "libiberty.h"

#define streq(a, b)	(strcmp ((a), (b)) == 0)
#define strneq(a, b, n)	(strncmp ((a), (b), (n)) == 0)


/* This stuff is somewhat copied from coffcode.h.  */
static asection bfd_debug_section =
{
  /* name,      id,  index, next, prev, flags, user_set_vma,       */
     "*DEBUG*", 0,   0,     NULL, NULL, 0,     0,
  /* linker_mark, linker_has_input, gc_mark, gc_mark_from_eh,      */
     0,           0,                1,       0,
  /* segment_mark, sec_info_type, use_rela_p, has_tls_reloc,       */
     0,            0,             0,          0,
  /* has_tls_get_addr_call, has_gp_reloc, need_finalize_relax,     */
     0,	                    0,            0,
  /* reloc_done, vma, lma, size, rawsize,                          */
     0,          0,   0,   0,    0,
  /* output_offset, output_section, alignment_power,               */
     0,             NULL,           0,
  /* relocation, orelocation, reloc_count, filepos, rel_filepos,   */
     NULL,       NULL,        0,           0,       0,
  /* line_filepos, userdata, contents, lineno, lineno_count,       */
     0,            NULL,     NULL,     NULL,   0,
  /* entsize, kept_section, moving_line_filepos,                   */
     0,       NULL,         0,
  /* target_index, used_by_bfd, constructor_chain, owner,          */
     0,            NULL,        NULL,              NULL,
  /* symbol,                                                       */
     NULL,
  /* symbol_ptr_ptr,                                               */
     NULL,
  /* map_head, map_tail                                            */
     { NULL }, { NULL }
};

/* Create an ECOFF object.  */

bfd_boolean
_bfd_ecoff_mkobject (bfd *abfd)
{
  bfd_size_type amt = sizeof (ecoff_data_type);

  abfd->tdata.ecoff_obj_data = bfd_zalloc (abfd, amt);
  if (abfd->tdata.ecoff_obj_data == NULL)
    return FALSE;

  return TRUE;
}

/* This is a hook called by coff_real_object_p to create any backend
   specific information.  */

void *
_bfd_ecoff_mkobject_hook (bfd *abfd, void * filehdr, void * aouthdr)
{
  struct internal_filehdr *internal_f = (struct internal_filehdr *) filehdr;
  struct internal_aouthdr *internal_a = (struct internal_aouthdr *) aouthdr;
  ecoff_data_type *ecoff;

  if (! _bfd_ecoff_mkobject (abfd))
    return NULL;

  ecoff = ecoff_data (abfd);
  ecoff->gp_size = 8;
  ecoff->sym_filepos = internal_f->f_symptr;

  if (internal_a != NULL)
    {
      int i;

      ecoff->text_start = internal_a->text_start;
      ecoff->text_end = internal_a->text_start + internal_a->tsize;
      ecoff->gp = internal_a->gp_value;
      ecoff->gprmask = internal_a->gprmask;
      for (i = 0; i < 4; i++)
	ecoff->cprmask[i] = internal_a->cprmask[i];
      ecoff->fprmask = internal_a->fprmask;
      if (internal_a->magic == ECOFF_AOUT_ZMAGIC)
	abfd->flags |= D_PAGED;
      else
	abfd->flags &=~ D_PAGED;
    }

  /* It turns out that no special action is required by the MIPS or
     Alpha ECOFF backends.  They have different information in the
     a.out header, but we just copy it all (e.g., gprmask, cprmask and
     fprmask) and let the swapping routines ensure that only relevant
     information is written out.  */

  return (void *) ecoff;
}

/* Initialize a new section.  */

bfd_boolean
_bfd_ecoff_new_section_hook (bfd *abfd, asection *section)
{
  unsigned int i;
  static struct
  {
    const char * name;
    flagword flags;
  }
  section_flags [] =
  {
    { _TEXT,   SEC_ALLOC | SEC_CODE | SEC_LOAD },
    { _INIT,   SEC_ALLOC | SEC_CODE | SEC_LOAD },
    { _FINI,   SEC_ALLOC | SEC_CODE | SEC_LOAD },
    { _DATA,   SEC_ALLOC | SEC_DATA | SEC_LOAD },
    { _SDATA,  SEC_ALLOC | SEC_DATA | SEC_LOAD },
    { _RDATA,  SEC_ALLOC | SEC_DATA | SEC_LOAD | SEC_READONLY},
    { _LIT8,   SEC_ALLOC | SEC_DATA | SEC_LOAD | SEC_READONLY},
    { _LIT4,   SEC_ALLOC | SEC_DATA | SEC_LOAD | SEC_READONLY},
    { _RCONST, SEC_ALLOC | SEC_DATA | SEC_LOAD | SEC_READONLY},
    { _PDATA,  SEC_ALLOC | SEC_DATA | SEC_LOAD | SEC_READONLY},
    { _BSS,    SEC_ALLOC},
    { _SBSS,   SEC_ALLOC},
    /* An Irix 4 shared libary.  */
    { _LIB,    SEC_COFF_SHARED_LIBRARY}
  };

  section->alignment_power = 4;

  for (i = 0; i < ARRAY_SIZE (section_flags); i++)
    if (streq (section->name, section_flags[i].name))
      {
	section->flags |= section_flags[i].flags;
	break;
      }


  /* Probably any other section name is SEC_NEVER_LOAD, but I'm
     uncertain about .init on some systems and I don't know how shared
     libraries work.  */

  return _bfd_generic_new_section_hook (abfd, section);
}

/* Determine the machine architecture and type.  This is called from
   the generic COFF routines.  It is the inverse of ecoff_get_magic,
   below.  This could be an ECOFF backend routine, with one version
   for each target, but there aren't all that many ECOFF targets.  */

bfd_boolean
_bfd_ecoff_set_arch_mach_hook (bfd *abfd, void * filehdr)
{
  struct internal_filehdr *internal_f = filehdr;
  enum bfd_architecture arch;
  unsigned long mach;

  switch (internal_f->f_magic)
    {
    case MIPS_MAGIC_1:
    case MIPS_MAGIC_LITTLE:
    case MIPS_MAGIC_BIG:
      arch = bfd_arch_mips;
      mach = bfd_mach_mips3000;
      break;

    case MIPS_MAGIC_LITTLE2:
    case MIPS_MAGIC_BIG2:
      /* MIPS ISA level 2: the r6000.  */
      arch = bfd_arch_mips;
      mach = bfd_mach_mips6000;
      break;

    case MIPS_MAGIC_LITTLE3:
    case MIPS_MAGIC_BIG3:
      /* MIPS ISA level 3: the r4000.  */
      arch = bfd_arch_mips;
      mach = bfd_mach_mips4000;
      break;

    case ALPHA_MAGIC:
      arch = bfd_arch_alpha;
      mach = 0;
      break;

    default:
      arch = bfd_arch_obscure;
      mach = 0;
      break;
    }

  return bfd_default_set_arch_mach (abfd, arch, mach);
}

/* Get the magic number to use based on the architecture and machine.
   This is the inverse of _bfd_ecoff_set_arch_mach_hook, above.  */

static int
ecoff_get_magic (bfd *abfd)
{
  int big, little;

  switch (bfd_get_arch (abfd))
    {
    case bfd_arch_mips:
      switch (bfd_get_mach (abfd))
	{
	default:
	case 0:
	case bfd_mach_mips3000:
	  big = MIPS_MAGIC_BIG;
	  little = MIPS_MAGIC_LITTLE;
	  break;

	case bfd_mach_mips6000:
	  big = MIPS_MAGIC_BIG2;
	  little = MIPS_MAGIC_LITTLE2;
	  break;

	case bfd_mach_mips4000:
	  big = MIPS_MAGIC_BIG3;
	  little = MIPS_MAGIC_LITTLE3;
	  break;
	}

      return bfd_big_endian (abfd) ? big : little;

    case bfd_arch_alpha:
      return ALPHA_MAGIC;

    default:
      abort ();
      return 0;
    }
}

/* Get the section s_flags to use for a section.  */

static long
ecoff_sec_to_styp_flags (const char *name, flagword flags)
{
  unsigned int i;
  static struct
  {
    const char * name;
    long flags;
  }
  styp_flags [] =
  {
    { _TEXT,    STYP_TEXT       },
    { _DATA,    STYP_DATA       },
    { _SDATA,   STYP_SDATA      },
    { _RDATA,   STYP_RDATA      },
    { _LITA,    STYP_LITA       },
    { _LIT8,    STYP_LIT8       },
    { _LIT4,    STYP_LIT4       },
    { _BSS,     STYP_BSS        },
    { _SBSS,    STYP_SBSS       },
    { _INIT,    STYP_ECOFF_INIT },
    { _FINI,    STYP_ECOFF_FINI },
    { _PDATA,   STYP_PDATA      },
    { _XDATA,   STYP_XDATA      },
    { _LIB,     STYP_ECOFF_LIB  },
    { _GOT,     STYP_GOT        },
    { _HASH,    STYP_HASH       },
    { _DYNAMIC, STYP_DYNAMIC    },
    { _LIBLIST, STYP_LIBLIST    },
    { _RELDYN,  STYP_RELDYN     },
    { _CONFLIC, STYP_CONFLIC    },
    { _DYNSTR,  STYP_DYNSTR     },
    { _DYNSYM,  STYP_DYNSYM     },
    { _RCONST,  STYP_RCONST     }
  };
  long styp = 0;

  for (i = 0; i < ARRAY_SIZE (styp_flags); i++)
    if (streq (name, styp_flags[i].name))
      {
	styp = styp_flags[i].flags;
	break;
      }

  if (styp == 0)
    {
      if (streq (name, _COMMENT))
	{
	  styp = STYP_COMMENT;
	  flags &=~ SEC_NEVER_LOAD;
	}
      else if (flags & SEC_CODE)
	styp = STYP_TEXT;
      else if (flags & SEC_DATA)
	styp = STYP_DATA;
      else if (flags & SEC_READONLY)
	styp = STYP_RDATA;
      else if (flags & SEC_LOAD)
	styp = STYP_REG;
      else
	styp = STYP_BSS;
    }

  if (flags & SEC_NEVER_LOAD)
    styp |= STYP_NOLOAD;

  return styp;
}

/* Get the BFD flags to use for a section.  */

bfd_boolean
_bfd_ecoff_styp_to_sec_flags (bfd *abfd ATTRIBUTE_UNUSED,
			      void * hdr,
			      const char *name ATTRIBUTE_UNUSED,
			      asection *section ATTRIBUTE_UNUSED,
			      flagword * flags_ptr)
{
  struct internal_scnhdr *internal_s = hdr;
  long styp_flags = internal_s->s_flags;
  flagword sec_flags = 0;

  if (styp_flags & STYP_NOLOAD)
    sec_flags |= SEC_NEVER_LOAD;

  /* For 386 COFF, at least, an unloadable text or data section is
     actually a shared library section.  */
  if ((styp_flags & STYP_TEXT)
      || (styp_flags & STYP_ECOFF_INIT)
      || (styp_flags & STYP_ECOFF_FINI)
      || (styp_flags & STYP_DYNAMIC)
      || (styp_flags & STYP_LIBLIST)
      || (styp_flags & STYP_RELDYN)
      || styp_flags == STYP_CONFLIC
      || (styp_flags & STYP_DYNSTR)
      || (styp_flags & STYP_DYNSYM)
      || (styp_flags & STYP_HASH))
    {
      if (sec_flags & SEC_NEVER_LOAD)
	sec_flags |= SEC_CODE | SEC_COFF_SHARED_LIBRARY;
      else
	sec_flags |= SEC_CODE | SEC_LOAD | SEC_ALLOC;
    }
  else if ((styp_flags & STYP_DATA)
	   || (styp_flags & STYP_RDATA)
	   || (styp_flags & STYP_SDATA)
	   || styp_flags == STYP_PDATA
	   || styp_flags == STYP_XDATA
	   || (styp_flags & STYP_GOT)
	   || styp_flags == STYP_RCONST)
    {
      if (sec_flags & SEC_NEVER_LOAD)
	sec_flags |= SEC_DATA | SEC_COFF_SHARED_LIBRARY;
      else
	sec_flags |= SEC_DATA | SEC_LOAD | SEC_ALLOC;
      if ((styp_flags & STYP_RDATA)
	  || styp_flags == STYP_PDATA
	  || styp_flags == STYP_RCONST)
	sec_flags |= SEC_READONLY;
    }
  else if ((styp_flags & STYP_BSS)
	   || (styp_flags & STYP_SBSS))
    sec_flags |= SEC_ALLOC;
  else if ((styp_flags & STYP_INFO) || styp_flags == STYP_COMMENT)
    sec_flags |= SEC_NEVER_LOAD;
  else if ((styp_flags & STYP_LITA)
	   || (styp_flags & STYP_LIT8)
	   || (styp_flags & STYP_LIT4))
    sec_flags |= SEC_DATA | SEC_LOAD | SEC_ALLOC | SEC_READONLY;
  else if (styp_flags & STYP_ECOFF_LIB)
    sec_flags |= SEC_COFF_SHARED_LIBRARY;
  else
    sec_flags |= SEC_ALLOC | SEC_LOAD;

  * flags_ptr = sec_flags;
  return TRUE;
}

/* Read in the symbolic header for an ECOFF object file.  */

static bfd_boolean
ecoff_slurp_symbolic_header (bfd *abfd)
{
  const struct ecoff_backend_data * const backend = ecoff_backend (abfd);
  bfd_size_type external_hdr_size;
  void * raw = NULL;
  HDRR *internal_symhdr;

  /* See if we've already read it in.  */
  if (ecoff_data (abfd)->debug_info.symbolic_header.magic ==
      backend->debug_swap.sym_magic)
    return TRUE;

  /* See whether there is a symbolic header.  */
  if (ecoff_data (abfd)->sym_filepos == 0)
    {
      bfd_get_symcount (abfd) = 0;
      return TRUE;
    }

  /* At this point bfd_get_symcount (abfd) holds the number of symbols
     as read from the file header, but on ECOFF this is always the
     size of the symbolic information header.  It would be cleaner to
     handle this when we first read the file in coffgen.c.  */
  external_hdr_size = backend->debug_swap.external_hdr_size;
  if (bfd_get_symcount (abfd) != external_hdr_size)
    {
      bfd_set_error (bfd_error_bad_value);
      return FALSE;
    }

  /* Read the symbolic information header.  */
  raw = bfd_malloc (external_hdr_size);
  if (raw == NULL)
    goto error_return;

  if (bfd_seek (abfd, ecoff_data (abfd)->sym_filepos, SEEK_SET) != 0
      || bfd_bread (raw, external_hdr_size, abfd) != external_hdr_size)
    goto error_return;
  internal_symhdr = &ecoff_data (abfd)->debug_info.symbolic_header;
  (*backend->debug_swap.swap_hdr_in) (abfd, raw, internal_symhdr);

  if (internal_symhdr->magic != backend->debug_swap.sym_magic)
    {
      bfd_set_error (bfd_error_bad_value);
      goto error_return;
    }

  /* Now we can get the correct number of symbols.  */
  bfd_get_symcount (abfd) = (internal_symhdr->isymMax
			     + internal_symhdr->iextMax);

  if (raw != NULL)
    free (raw);
  return TRUE;
 error_return:
  if (raw != NULL)
    free (raw);
  return FALSE;
}

/* Read in and swap the important symbolic information for an ECOFF
   object file.  This is called by gdb via the read_debug_info entry
   point in the backend structure.  */

bfd_boolean
_bfd_ecoff_slurp_symbolic_info (bfd *abfd,
				asection *ignore ATTRIBUTE_UNUSED,
				struct ecoff_debug_info *debug)
{
  const struct ecoff_backend_data * const backend = ecoff_backend (abfd);
  HDRR *internal_symhdr;
  bfd_size_type raw_base;
  bfd_size_type raw_size;
  void * raw;
  bfd_size_type external_fdr_size;
  char *fraw_src;
  char *fraw_end;
  struct fdr *fdr_ptr;
  bfd_size_type raw_end;
  bfd_size_type cb_end;
  bfd_size_type amt;
  file_ptr pos;

  BFD_ASSERT (debug == &ecoff_data (abfd)->debug_info);

  /* Check whether we've already gotten it, and whether there's any to
     get.  */
  if (ecoff_data (abfd)->raw_syments != NULL)
    return TRUE;
  if (ecoff_data (abfd)->sym_filepos == 0)
    {
      bfd_get_symcount (abfd) = 0;
      return TRUE;
    }

  if (! ecoff_slurp_symbolic_header (abfd))
    return FALSE;

  internal_symhdr = &debug->symbolic_header;

  /* Read all the symbolic information at once.  */
  raw_base = (ecoff_data (abfd)->sym_filepos
	      + backend->debug_swap.external_hdr_size);

  /* Alpha ecoff makes the determination of raw_size difficult. It has
     an undocumented debug data section between the symhdr and the first
     documented section. And the ordering of the sections varies between
     statically and dynamically linked executables.
     If bfd supports SEEK_END someday, this code could be simplified.  */
  raw_end = 0;

#define UPDATE_RAW_END(start, count, size) \
  cb_end = internal_symhdr->start + internal_symhdr->count * (size); \
  if (cb_end > raw_end) \
    raw_end = cb_end

  UPDATE_RAW_END (cbLineOffset, cbLine, sizeof (unsigned char));
  UPDATE_RAW_END (cbDnOffset, idnMax, backend->debug_swap.external_dnr_size);
  UPDATE_RAW_END (cbPdOffset, ipdMax, backend->debug_swap.external_pdr_size);
  UPDATE_RAW_END (cbSymOffset, isymMax, backend->debug_swap.external_sym_size);
  /* eraxxon@alumni.rice.edu: ioptMax refers to the size of the
     optimization symtab, not the number of entries.  */
  UPDATE_RAW_END (cbOptOffset, ioptMax, sizeof (char));
  UPDATE_RAW_END (cbAuxOffset, iauxMax, sizeof (union aux_ext));
  UPDATE_RAW_END (cbSsOffset, issMax, sizeof (char));
  UPDATE_RAW_END (cbSsExtOffset, issExtMax, sizeof (char));
  UPDATE_RAW_END (cbFdOffset, ifdMax, backend->debug_swap.external_fdr_size);
  UPDATE_RAW_END (cbRfdOffset, crfd, backend->debug_swap.external_rfd_size);
  UPDATE_RAW_END (cbExtOffset, iextMax, backend->debug_swap.external_ext_size);

#undef UPDATE_RAW_END

  raw_size = raw_end - raw_base;
  if (raw_size == 0)
    {
      ecoff_data (abfd)->sym_filepos = 0;
      return TRUE;
    }
  raw = bfd_alloc (abfd, raw_size);
  if (raw == NULL)
    return FALSE;

  pos = ecoff_data (abfd)->sym_filepos;
  pos += backend->debug_swap.external_hdr_size;
  if (bfd_seek (abfd, pos, SEEK_SET) != 0
      || bfd_bread (raw, raw_size, abfd) != raw_size)
    {
      bfd_release (abfd, raw);
      return FALSE;
    }

  ecoff_data (abfd)->raw_syments = raw;

  /* Get pointers for the numeric offsets in the HDRR structure.  */
#define FIX(off1, off2, type)				\
  if (internal_symhdr->off1 == 0)			\
    debug->off2 = NULL;					\
  else							\
    debug->off2 = (type) ((char *) raw			\
			  + (internal_symhdr->off1	\
			     - raw_base))

  FIX (cbLineOffset, line, unsigned char *);
  FIX (cbDnOffset, external_dnr, void *);
  FIX (cbPdOffset, external_pdr, void *);
  FIX (cbSymOffset, external_sym, void *);
  FIX (cbOptOffset, external_opt, void *);
  FIX (cbAuxOffset, external_aux, union aux_ext *);
  FIX (cbSsOffset, ss, char *);
  FIX (cbSsExtOffset, ssext, char *);
  FIX (cbFdOffset, external_fdr, void *);
  FIX (cbRfdOffset, external_rfd, void *);
  FIX (cbExtOffset, external_ext, void *);
#undef FIX

  /* I don't want to always swap all the data, because it will just
     waste time and most programs will never look at it.  The only
     time the linker needs most of the debugging information swapped
     is when linking big-endian and little-endian MIPS object files
     together, which is not a common occurrence.

     We need to look at the fdr to deal with a lot of information in
     the symbols, so we swap them here.  */
  amt = internal_symhdr->ifdMax;
  amt *= sizeof (struct fdr);
  debug->fdr = bfd_alloc (abfd, amt);
  if (debug->fdr == NULL)
    return FALSE;
  external_fdr_size = backend->debug_swap.external_fdr_size;
  fdr_ptr = debug->fdr;
  fraw_src = (char *) debug->external_fdr;
  fraw_end = fraw_src + internal_symhdr->ifdMax * external_fdr_size;
  for (; fraw_src < fraw_end; fraw_src += external_fdr_size, fdr_ptr++)
    (*backend->debug_swap.swap_fdr_in) (abfd, (void *) fraw_src, fdr_ptr);

  return TRUE;
}

/* ECOFF symbol table routines.  The ECOFF symbol table is described
   in gcc/mips-tfile.c.  */

/* ECOFF uses two common sections.  One is the usual one, and the
   other is for small objects.  All the small objects are kept
   together, and then referenced via the gp pointer, which yields
   faster assembler code.  This is what we use for the small common
   section.  */
static asection ecoff_scom_section;
static asymbol ecoff_scom_symbol;
static asymbol *ecoff_scom_symbol_ptr;

/* Create an empty symbol.  */

asymbol *
_bfd_ecoff_make_empty_symbol (bfd *abfd)
{
  ecoff_symbol_type *new;
  bfd_size_type amt = sizeof (ecoff_symbol_type);

  new = bfd_zalloc (abfd, amt);
  if (new == NULL)
    return NULL;
  new->symbol.section = NULL;
  new->fdr = NULL;
  new->local = FALSE;
  new->native = NULL;
  new->symbol.the_bfd = abfd;
  return &new->symbol;
}

/* Set the BFD flags and section for an ECOFF symbol.  */

static bfd_boolean
ecoff_set_symbol_info (bfd *abfd,
		       SYMR *ecoff_sym,
		       asymbol *asym,
		       int ext,
		       int weak)
{
  asym->the_bfd = abfd;
  asym->value = ecoff_sym->value;
  asym->section = &bfd_debug_section;
  asym->udata.i = 0;

  /* Most symbol types are just for debugging.  */
  switch (ecoff_sym->st)
    {
    case stGlobal:
    case stStatic:
    case stLabel:
    case stProc:
    case stStaticProc:
      break;
    case stNil:
      if (ECOFF_IS_STAB (ecoff_sym))
	{
	  asym->flags = BSF_DEBUGGING;
	  return TRUE;
	}
      break;
    default:
      asym->flags = BSF_DEBUGGING;
      return TRUE;
    }

  if (weak)
    asym->flags = BSF_EXPORT | BSF_WEAK;
  else if (ext)
    asym->flags = BSF_EXPORT | BSF_GLOBAL;
  else
    {
      asym->flags = BSF_LOCAL;
      /* Normally, a local stProc symbol will have a corresponding
         external symbol.  We mark the local symbol as a debugging
         symbol, in order to prevent nm from printing both out.
         Similarly, we mark stLabel and stabs symbols as debugging
         symbols.  In both cases, we do want to set the value
         correctly based on the symbol class.  */
      if (ecoff_sym->st == stProc
	  || ecoff_sym->st == stLabel
	  || ECOFF_IS_STAB (ecoff_sym))
	asym->flags |= BSF_DEBUGGING;
    }

  if (ecoff_sym->st == stProc || ecoff_sym->st == stStaticProc)
    asym->flags |= BSF_FUNCTION;

  switch (ecoff_sym->sc)
    {
    case scNil:
      /* Used for compiler generated labels.  Leave them in the
	 debugging section, and mark them as local.  If BSF_DEBUGGING
	 is set, then nm does not display them for some reason.  If no
	 flags are set then the linker whines about them.  */
      asym->flags = BSF_LOCAL;
      break;
    case scText:
      asym->section = bfd_make_section_old_way (abfd, _TEXT);
      asym->value -= asym->section->vma;
      break;
    case scData:
      asym->section = bfd_make_section_old_way (abfd, _DATA);
      asym->value -= asym->section->vma;
      break;
    case scBss:
      asym->section = bfd_make_section_old_way (abfd, _BSS);
      asym->value -= asym->section->vma;
      break;
    case scRegister:
      asym->flags = BSF_DEBUGGING;
      break;
    case scAbs:
      asym->section = bfd_abs_section_ptr;
      break;
    case scUndefined:
      asym->section = bfd_und_section_ptr;
      asym->flags = 0;
      asym->value = 0;
      break;
    case scCdbLocal:
    case scBits:
    case scCdbSystem:
    case scRegImage:
    case scInfo:
    case scUserStruct:
      asym->flags = BSF_DEBUGGING;
      break;
    case scSData:
      asym->section = bfd_make_section_old_way (abfd, ".sdata");
      asym->value -= asym->section->vma;
      break;
    case scSBss:
      asym->section = bfd_make_section_old_way (abfd, ".sbss");
      asym->value -= asym->section->vma;
      break;
    case scRData:
      asym->section = bfd_make_section_old_way (abfd, ".rdata");
      asym->value -= asym->section->vma;
      break;
    case scVar:
      asym->flags = BSF_DEBUGGING;
      break;
    case scCommon:
      if (asym->value > ecoff_data (abfd)->gp_size)
	{
	  asym->section = bfd_com_section_ptr;
	  asym->flags = 0;
	  break;
	}
      /* Fall through.  */
    case scSCommon:
      if (ecoff_scom_section.name == NULL)
	{
	  /* Initialize the small common section.  */
	  ecoff_scom_section.name = SCOMMON;
	  ecoff_scom_section.flags = SEC_IS_COMMON;
	  ecoff_scom_section.output_section = &ecoff_scom_section;
	  ecoff_scom_section.symbol = &ecoff_scom_symbol;
	  ecoff_scom_section.symbol_ptr_ptr = &ecoff_scom_symbol_ptr;
	  ecoff_scom_symbol.name = SCOMMON;
	  ecoff_scom_symbol.flags = BSF_SECTION_SYM;
	  ecoff_scom_symbol.section = &ecoff_scom_section;
	  ecoff_scom_symbol_ptr = &ecoff_scom_symbol;
	}
      asym->section = &ecoff_scom_section;
      asym->flags = 0;
      break;
    case scVarRegister:
    case scVariant:
      asym->flags = BSF_DEBUGGING;
      break;
    case scSUndefined:
      asym->section = bfd_und_section_ptr;
      asym->flags = 0;
      asym->value = 0;
      break;
    case scInit:
      asym->section = bfd_make_section_old_way (abfd, ".init");
      asym->value -= asym->section->vma;
      break;
    case scBasedVar:
    case scXData:
    case scPData:
      asym->flags = BSF_DEBUGGING;
      break;
    case scFini:
      asym->section = bfd_make_section_old_way (abfd, ".fini");
      asym->value -= asym->section->vma;
      break;
    case scRConst:
      asym->section = bfd_make_section_old_way (abfd, ".rconst");
      asym->value -= asym->section->vma;
      break;
    default:
      break;
    }

  /* Look for special constructors symbols and make relocation entries
     in a special construction section.  These are produced by the
     -fgnu-linker argument to g++.  */
  if (ECOFF_IS_STAB (ecoff_sym))
    {
      switch (ECOFF_UNMARK_STAB (ecoff_sym->index))
	{
	default:
	  break;

	case N_SETA:
	case N_SETT:
	case N_SETD:
	case N_SETB:
	  /* Mark the symbol as a constructor.  */
	  asym->flags |= BSF_CONSTRUCTOR;
	  break;
	}
    }
  return TRUE;
}

/* Read an ECOFF symbol table.  */

bfd_boolean
_bfd_ecoff_slurp_symbol_table (bfd *abfd)
{
  const struct ecoff_backend_data * const backend = ecoff_backend (abfd);
  const bfd_size_type external_ext_size
    = backend->debug_swap.external_ext_size;
  const bfd_size_type external_sym_size
    = backend->debug_swap.external_sym_size;
  void (* const swap_ext_in) (bfd *, void *, EXTR *)
    = backend->debug_swap.swap_ext_in;
  void (* const swap_sym_in) (bfd *, void *, SYMR *)
    = backend->debug_swap.swap_sym_in;
  bfd_size_type internal_size;
  ecoff_symbol_type *internal;
  ecoff_symbol_type *internal_ptr;
  char *eraw_src;
  char *eraw_end;
  FDR *fdr_ptr;
  FDR *fdr_end;

  /* If we've already read in the symbol table, do nothing.  */
  if (ecoff_data (abfd)->canonical_symbols != NULL)
    return TRUE;

  /* Get the symbolic information.  */
  if (! _bfd_ecoff_slurp_symbolic_info (abfd, NULL,
					&ecoff_data (abfd)->debug_info))
    return FALSE;
  if (bfd_get_symcount (abfd) == 0)
    return TRUE;

  internal_size = bfd_get_symcount (abfd);
  internal_size *= sizeof (ecoff_symbol_type);
  internal = bfd_alloc (abfd, internal_size);
  if (internal == NULL)
    return FALSE;

  internal_ptr = internal;
  eraw_src = (char *) ecoff_data (abfd)->debug_info.external_ext;
  eraw_end = (eraw_src
	      + (ecoff_data (abfd)->debug_info.symbolic_header.iextMax
		 * external_ext_size));
  for (; eraw_src < eraw_end; eraw_src += external_ext_size, internal_ptr++)
    {
      EXTR internal_esym;

      (*swap_ext_in) (abfd, (void *) eraw_src, &internal_esym);
      internal_ptr->symbol.name = (ecoff_data (abfd)->debug_info.ssext
				   + internal_esym.asym.iss);
      if (!ecoff_set_symbol_info (abfd, &internal_esym.asym,
				  &internal_ptr->symbol, 1,
				  internal_esym.weakext))
	return FALSE;
      /* The alpha uses a negative ifd field for section symbols.  */
      if (internal_esym.ifd >= 0)
	internal_ptr->fdr = (ecoff_data (abfd)->debug_info.fdr
			     + internal_esym.ifd);
      else
	internal_ptr->fdr = NULL;
      internal_ptr->local = FALSE;
      internal_ptr->native = (void *) eraw_src;
    }

  /* The local symbols must be accessed via the fdr's, because the
     string and aux indices are relative to the fdr information.  */
  fdr_ptr = ecoff_data (abfd)->debug_info.fdr;
  fdr_end = fdr_ptr + ecoff_data (abfd)->debug_info.symbolic_header.ifdMax;
  for (; fdr_ptr < fdr_end; fdr_ptr++)
    {
      char *lraw_src;
      char *lraw_end;

      lraw_src = ((char *) ecoff_data (abfd)->debug_info.external_sym
		  + fdr_ptr->isymBase * external_sym_size);
      lraw_end = lraw_src + fdr_ptr->csym * external_sym_size;
      for (;
	   lraw_src < lraw_end;
	   lraw_src += external_sym_size, internal_ptr++)
	{
	  SYMR internal_sym;

	  (*swap_sym_in) (abfd, (void *) lraw_src, &internal_sym);
	  internal_ptr->symbol.name = (ecoff_data (abfd)->debug_info.ss
				       + fdr_ptr->issBase
				       + internal_sym.iss);
	  if (!ecoff_set_symbol_info (abfd, &internal_sym,
				      &internal_ptr->symbol, 0, 0))
	    return FALSE;
	  internal_ptr->fdr = fdr_ptr;
	  internal_ptr->local = TRUE;
	  internal_ptr->native = (void *) lraw_src;
	}
    }

  ecoff_data (abfd)->canonical_symbols = internal;

  return TRUE;
}

/* Return the amount of space needed for the canonical symbols.  */

long
_bfd_ecoff_get_symtab_upper_bound (bfd *abfd)
{
  if (! _bfd_ecoff_slurp_symbolic_info (abfd, NULL,
					&ecoff_data (abfd)->debug_info))
    return -1;

  if (bfd_get_symcount (abfd) == 0)
    return 0;

  return (bfd_get_symcount (abfd) + 1) * (sizeof (ecoff_symbol_type *));
}

/* Get the canonical symbols.  */

long
_bfd_ecoff_canonicalize_symtab (bfd *abfd, asymbol **alocation)
{
  unsigned int counter = 0;
  ecoff_symbol_type *symbase;
  ecoff_symbol_type **location = (ecoff_symbol_type **) alocation;

  if (! _bfd_ecoff_slurp_symbol_table (abfd))
    return -1;
  if (bfd_get_symcount (abfd) == 0)
    return 0;

  symbase = ecoff_data (abfd)->canonical_symbols;
  while (counter < bfd_get_symcount (abfd))
    {
      *(location++) = symbase++;
      counter++;
    }
  *location++ = NULL;
  return bfd_get_symcount (abfd);
}

/* Turn ECOFF type information into a printable string.
   ecoff_emit_aggregate and ecoff_type_to_string are from
   gcc/mips-tdump.c, with swapping added and used_ptr removed.  */

/* Write aggregate information to a string.  */

static void
ecoff_emit_aggregate (bfd *abfd,
		      FDR *fdr,
		      char *string,
		      RNDXR *rndx,
		      long isym,
		      const char *which)
{
  const struct ecoff_debug_swap * const debug_swap =
    &ecoff_backend (abfd)->debug_swap;
  struct ecoff_debug_info * const debug_info = &ecoff_data (abfd)->debug_info;
  unsigned int ifd = rndx->rfd;
  unsigned int indx = rndx->index;
  const char *name;

  if (ifd == 0xfff)
    ifd = isym;

  /* An ifd of -1 is an opaque type.  An escaped index of 0 is a
     struct return type of a procedure compiled without -g.  */
  if (ifd == 0xffffffff
      || (rndx->rfd == 0xfff && indx == 0))
    name = "<undefined>";
  else if (indx == indexNil)
    name = "<no name>";
  else
    {
      SYMR sym;

      if (debug_info->external_rfd == NULL)
	fdr = debug_info->fdr + ifd;
      else
	{
	  RFDT rfd;

	  (*debug_swap->swap_rfd_in) (abfd,
				      ((char *) debug_info->external_rfd
				       + ((fdr->rfdBase + ifd)
					  * debug_swap->external_rfd_size)),
				      &rfd);
	  fdr = debug_info->fdr + rfd;
	}

      indx += fdr->isymBase;

      (*debug_swap->swap_sym_in) (abfd,
				  ((char *) debug_info->external_sym
				   + indx * debug_swap->external_sym_size),
				  &sym);

      name = debug_info->ss + fdr->issBase + sym.iss;
    }

  sprintf (string,
	   "%s %s { ifd = %u, index = %lu }",
	   which, name, ifd,
	   ((long) indx
	    + debug_info->symbolic_header.iextMax));
}

/* Convert the type information to string format.  */

static char *
ecoff_type_to_string (bfd *abfd, FDR *fdr, unsigned int indx)
{
  union aux_ext *aux_ptr;
  int bigendian;
  AUXU u;
  struct qual
  {
    unsigned int  type;
    int  low_bound;
    int  high_bound;
    int  stride;
  } qualifiers[7];
  unsigned int basic_type;
  int i;
  char buffer1[1024];
  static char buffer2[1024];
  char *p1 = buffer1;
  char *p2 = buffer2;
  RNDXR rndx;

  aux_ptr = ecoff_data (abfd)->debug_info.external_aux + fdr->iauxBase;
  bigendian = fdr->fBigendian;

  for (i = 0; i < 7; i++)
    {
      qualifiers[i].low_bound = 0;
      qualifiers[i].high_bound = 0;
      qualifiers[i].stride = 0;
    }

  if (AUX_GET_ISYM (bigendian, &aux_ptr[indx]) == (bfd_vma) -1)
    return "-1 (no type)";
  _bfd_ecoff_swap_tir_in (bigendian, &aux_ptr[indx++].a_ti, &u.ti);

  basic_type = u.ti.bt;
  qualifiers[0].type = u.ti.tq0;
  qualifiers[1].type = u.ti.tq1;
  qualifiers[2].type = u.ti.tq2;
  qualifiers[3].type = u.ti.tq3;
  qualifiers[4].type = u.ti.tq4;
  qualifiers[5].type = u.ti.tq5;
  qualifiers[6].type = tqNil;

  /* Go get the basic type.  */
  switch (basic_type)
    {
    case btNil:			/* Undefined.  */
      strcpy (p1, "nil");
      break;

    case btAdr:			/* Address - integer same size as pointer.  */
      strcpy (p1, "address");
      break;

    case btChar:		/* Character.  */
      strcpy (p1, "char");
      break;

    case btUChar:		/* Unsigned character.  */
      strcpy (p1, "unsigned char");
      break;

    case btShort:		/* Short.  */
      strcpy (p1, "short");
      break;

    case btUShort:		/* Unsigned short.  */
      strcpy (p1, "unsigned short");
      break;

    case btInt:			/* Int.  */
      strcpy (p1, "int");
      break;

    case btUInt:		/* Unsigned int.  */
      strcpy (p1, "unsigned int");
      break;

    case btLong:		/* Long.  */
      strcpy (p1, "long");
      break;

    case btULong:		/* Unsigned long.  */
      strcpy (p1, "unsigned long");
      break;

    case btFloat:		/* Float (real).  */
      strcpy (p1, "float");
      break;

    case btDouble:		/* Double (real).  */
      strcpy (p1, "double");
      break;

      /* Structures add 1-2 aux words:
	 1st word is [ST_RFDESCAPE, offset] pointer to struct def;
	 2nd word is file index if 1st word rfd is ST_RFDESCAPE.  */

    case btStruct:		/* Structure (Record).  */
      _bfd_ecoff_swap_rndx_in (bigendian, &aux_ptr[indx].a_rndx, &rndx);
      ecoff_emit_aggregate (abfd, fdr, p1, &rndx,
			    (long) AUX_GET_ISYM (bigendian, &aux_ptr[indx+1]),
			    "struct");
      indx++;			/* Skip aux words.  */
      break;

      /* Unions add 1-2 aux words:
	 1st word is [ST_RFDESCAPE, offset] pointer to union def;
	 2nd word is file index if 1st word rfd is ST_RFDESCAPE.  */

    case btUnion:		/* Union.  */
      _bfd_ecoff_swap_rndx_in (bigendian, &aux_ptr[indx].a_rndx, &rndx);
      ecoff_emit_aggregate (abfd, fdr, p1, &rndx,
			    (long) AUX_GET_ISYM (bigendian, &aux_ptr[indx+1]),
			    "union");
      indx++;			/* Skip aux words.  */
      break;

      /* Enumerations add 1-2 aux words:
	 1st word is [ST_RFDESCAPE, offset] pointer to enum def;
	 2nd word is file index if 1st word rfd is ST_RFDESCAPE.  */

    case btEnum:		/* Enumeration.  */
      _bfd_ecoff_swap_rndx_in (bigendian, &aux_ptr[indx].a_rndx, &rndx);
      ecoff_emit_aggregate (abfd, fdr, p1, &rndx,
			    (long) AUX_GET_ISYM (bigendian, &aux_ptr[indx+1]),
			    "enum");
      indx++;			/* Skip aux words.  */
      break;

    case btTypedef:		/* Defined via a typedef, isymRef points.  */
      strcpy (p1, "typedef");
      break;

    case btRange:		/* Subrange of int.  */
      strcpy (p1, "subrange");
      break;

    case btSet:			/* Pascal sets.  */
      strcpy (p1, "set");
      break;

    case btComplex:		/* Fortran complex.  */
      strcpy (p1, "complex");
      break;

    case btDComplex:		/* Fortran double complex.  */
      strcpy (p1, "double complex");
      break;

    case btIndirect:		/* Forward or unnamed typedef.  */
      strcpy (p1, "forward/unamed typedef");
      break;

    case btFixedDec:		/* Fixed Decimal.  */
      strcpy (p1, "fixed decimal");
      break;

    case btFloatDec:		/* Float Decimal.  */
      strcpy (p1, "float decimal");
      break;

    case btString:		/* Varying Length Character String.  */
      strcpy (p1, "string");
      break;

    case btBit:			/* Aligned Bit String.  */
      strcpy (p1, "bit");
      break;

    case btPicture:		/* Picture.  */
      strcpy (p1, "picture");
      break;

    case btVoid:		/* Void.  */
      strcpy (p1, "void");
      break;

    default:
      sprintf (p1, _("Unknown basic type %d"), (int) basic_type);
      break;
    }

  p1 += strlen (buffer1);

  /* If this is a bitfield, get the bitsize.  */
  if (u.ti.fBitfield)
    {
      int bitsize;

      bitsize = AUX_GET_WIDTH (bigendian, &aux_ptr[indx++]);
      sprintf (p1, " : %d", bitsize);
      p1 += strlen (buffer1);
    }

  /* Deal with any qualifiers.  */
  if (qualifiers[0].type != tqNil)
    {
      /* Snarf up any array bounds in the correct order.  Arrays
         store 5 successive words in the aux. table:
        	word 0	RNDXR to type of the bounds (ie, int)
        	word 1	Current file descriptor index
        	word 2	low bound
        	word 3	high bound (or -1 if [])
        	word 4	stride size in bits.  */
      for (i = 0; i < 7; i++)
	{
	  if (qualifiers[i].type == tqArray)
	    {
	      qualifiers[i].low_bound =
		AUX_GET_DNLOW (bigendian, &aux_ptr[indx+2]);
	      qualifiers[i].high_bound =
		AUX_GET_DNHIGH (bigendian, &aux_ptr[indx+3]);
	      qualifiers[i].stride =
		AUX_GET_WIDTH (bigendian, &aux_ptr[indx+4]);
	      indx += 5;
	    }
	}

      /* Now print out the qualifiers.  */
      for (i = 0; i < 6; i++)
	{
	  switch (qualifiers[i].type)
	    {
	    case tqNil:
	    case tqMax:
	      break;

	    case tqPtr:
	      strcpy (p2, "ptr to ");
	      p2 += sizeof ("ptr to ")-1;
	      break;

	    case tqVol:
	      strcpy (p2, "volatile ");
	      p2 += sizeof ("volatile ")-1;
	      break;

	    case tqFar:
	      strcpy (p2, "far ");
	      p2 += sizeof ("far ")-1;
	      break;

	    case tqProc:
	      strcpy (p2, "func. ret. ");
	      p2 += sizeof ("func. ret. ");
	      break;

	    case tqArray:
	      {
		int first_array = i;
		int j;

		/* Print array bounds reversed (ie, in the order the C
		   programmer writes them).  C is such a fun language....  */
		while (i < 5 && qualifiers[i+1].type == tqArray)
		  i++;

		for (j = i; j >= first_array; j--)
		  {
		    strcpy (p2, "array [");
		    p2 += sizeof ("array [")-1;
		    if (qualifiers[j].low_bound != 0)
		      sprintf (p2,
			       "%ld:%ld {%ld bits}",
			       (long) qualifiers[j].low_bound,
			       (long) qualifiers[j].high_bound,
			       (long) qualifiers[j].stride);

		    else if (qualifiers[j].high_bound != -1)
		      sprintf (p2,
			       "%ld {%ld bits}",
			       (long) (qualifiers[j].high_bound + 1),
			       (long) (qualifiers[j].stride));

		    else
		      sprintf (p2, " {%ld bits}", (long) (qualifiers[j].stride));

		    p2 += strlen (p2);
		    strcpy (p2, "] of ");
		    p2 += sizeof ("] of ")-1;
		  }
	      }
	      break;
	    }
	}
    }

  strcpy (p2, buffer1);
  return buffer2;
}

/* Return information about ECOFF symbol SYMBOL in RET.  */

void
_bfd_ecoff_get_symbol_info (bfd *abfd ATTRIBUTE_UNUSED,
			    asymbol *symbol,
			    symbol_info *ret)
{
  bfd_symbol_info (symbol, ret);
}

/* Return whether this is a local label.  */

bfd_boolean
_bfd_ecoff_bfd_is_local_label_name (bfd *abfd ATTRIBUTE_UNUSED,
				    const char *name)
{
  return name[0] == '$';
}

/* Print information about an ECOFF symbol.  */

void
_bfd_ecoff_print_symbol (bfd *abfd,
			 void * filep,
			 asymbol *symbol,
			 bfd_print_symbol_type how)
{
  const struct ecoff_debug_swap * const debug_swap
    = &ecoff_backend (abfd)->debug_swap;
  FILE *file = (FILE *)filep;

  switch (how)
    {
    case bfd_print_symbol_name:
      fprintf (file, "%s", symbol->name);
      break;
    case bfd_print_symbol_more:
      if (ecoffsymbol (symbol)->local)
	{
	  SYMR ecoff_sym;

	  (*debug_swap->swap_sym_in) (abfd, ecoffsymbol (symbol)->native,
				      &ecoff_sym);
	  fprintf (file, "ecoff local ");
	  fprintf_vma (file, (bfd_vma) ecoff_sym.value);
	  fprintf (file, " %x %x", (unsigned) ecoff_sym.st,
		   (unsigned) ecoff_sym.sc);
	}
      else
	{
	  EXTR ecoff_ext;

	  (*debug_swap->swap_ext_in) (abfd, ecoffsymbol (symbol)->native,
				      &ecoff_ext);
	  fprintf (file, "ecoff extern ");
	  fprintf_vma (file, (bfd_vma) ecoff_ext.asym.value);
	  fprintf (file, " %x %x", (unsigned) ecoff_ext.asym.st,
		   (unsigned) ecoff_ext.asym.sc);
	}
      break;
    case bfd_print_symbol_all:
      /* Print out the symbols in a reasonable way.  */
      {
	char type;
	int pos;
	EXTR ecoff_ext;
	char jmptbl;
	char cobol_main;
	char weakext;

	if (ecoffsymbol (symbol)->local)
	  {
	    (*debug_swap->swap_sym_in) (abfd, ecoffsymbol (symbol)->native,
					&ecoff_ext.asym);
	    type = 'l';
	    pos = ((((char *) ecoffsymbol (symbol)->native
		     - (char *) ecoff_data (abfd)->debug_info.external_sym)
		    / debug_swap->external_sym_size)
		   + ecoff_data (abfd)->debug_info.symbolic_header.iextMax);
	    jmptbl = ' ';
	    cobol_main = ' ';
	    weakext = ' ';
	  }
	else
	  {
	    (*debug_swap->swap_ext_in) (abfd, ecoffsymbol (symbol)->native,
					&ecoff_ext);
	    type = 'e';
	    pos = (((char *) ecoffsymbol (symbol)->native
		    - (char *) ecoff_data (abfd)->debug_info.external_ext)
		   / debug_swap->external_ext_size);
	    jmptbl = ecoff_ext.jmptbl ? 'j' : ' ';
	    cobol_main = ecoff_ext.cobol_main ? 'c' : ' ';
	    weakext = ecoff_ext.weakext ? 'w' : ' ';
	  }

	fprintf (file, "[%3d] %c ",
		 pos, type);
	fprintf_vma (file, (bfd_vma) ecoff_ext.asym.value);
	fprintf (file, " st %x sc %x indx %x %c%c%c %s",
		 (unsigned) ecoff_ext.asym.st,
		 (unsigned) ecoff_ext.asym.sc,
		 (unsigned) ecoff_ext.asym.index,
		 jmptbl, cobol_main, weakext,
		 symbol->name);

	if (ecoffsymbol (symbol)->fdr != NULL
	    && ecoff_ext.asym.index != indexNil)
	  {
	    FDR *fdr;
	    unsigned int indx;
	    int bigendian;
	    bfd_size_type sym_base;
	    union aux_ext *aux_base;

	    fdr = ecoffsymbol (symbol)->fdr;
	    indx = ecoff_ext.asym.index;

	    /* sym_base is used to map the fdr relative indices which
	       appear in the file to the position number which we are
	       using.  */
	    sym_base = fdr->isymBase;
	    if (ecoffsymbol (symbol)->local)
	      sym_base +=
		ecoff_data (abfd)->debug_info.symbolic_header.iextMax;

	    /* aux_base is the start of the aux entries for this file;
	       asym.index is an offset from this.  */
	    aux_base = (ecoff_data (abfd)->debug_info.external_aux
			+ fdr->iauxBase);

	    /* The aux entries are stored in host byte order; the
	       order is indicated by a bit in the fdr.  */
	    bigendian = fdr->fBigendian;

	    /* This switch is basically from gcc/mips-tdump.c.  */
	    switch (ecoff_ext.asym.st)
	      {
	      case stNil:
	      case stLabel:
		break;

	      case stFile:
	      case stBlock:
		fprintf (file, _("\n      End+1 symbol: %ld"),
			 (long) (indx + sym_base));
		break;

	      case stEnd:
		if (ecoff_ext.asym.sc == scText
		    || ecoff_ext.asym.sc == scInfo)
		  fprintf (file, _("\n      First symbol: %ld"),
			   (long) (indx + sym_base));
		else
		  fprintf (file, _("\n      First symbol: %ld"),
			   ((long)
			    (AUX_GET_ISYM (bigendian,
					   &aux_base[ecoff_ext.asym.index])
			     + sym_base)));
		break;

	      case stProc:
	      case stStaticProc:
		if (ECOFF_IS_STAB (&ecoff_ext.asym))
		  ;
		else if (ecoffsymbol (symbol)->local)
		  fprintf (file, _("\n      End+1 symbol: %-7ld   Type:  %s"),
			   ((long)
			    (AUX_GET_ISYM (bigendian,
					   &aux_base[ecoff_ext.asym.index])
			     + sym_base)),
			   ecoff_type_to_string (abfd, fdr, indx + 1));
		else
		  fprintf (file, _("\n      Local symbol: %ld"),
			   ((long) indx
			    + (long) sym_base
			    + (ecoff_data (abfd)
			       ->debug_info.symbolic_header.iextMax)));
		break;

	      case stStruct:
		fprintf (file, _("\n      struct; End+1 symbol: %ld"),
			 (long) (indx + sym_base));
		break;

	      case stUnion:
		fprintf (file, _("\n      union; End+1 symbol: %ld"),
			 (long) (indx + sym_base));
		break;

	      case stEnum:
		fprintf (file, _("\n      enum; End+1 symbol: %ld"),
			 (long) (indx + sym_base));
		break;

	      default:
		if (! ECOFF_IS_STAB (&ecoff_ext.asym))
		  fprintf (file, _("\n      Type: %s"),
			   ecoff_type_to_string (abfd, fdr, indx));
		break;
	      }
	  }
      }
      break;
    }
}

/* Read in the relocs for a section.  */

static bfd_boolean
ecoff_slurp_reloc_table (bfd *abfd,
			 asection *section,
			 asymbol **symbols)
{
  const struct ecoff_backend_data * const backend = ecoff_backend (abfd);
  arelent *internal_relocs;
  bfd_size_type external_reloc_size;
  bfd_size_type amt;
  char *external_relocs;
  arelent *rptr;
  unsigned int i;

  if (section->relocation != NULL
      || section->reloc_count == 0
      || (section->flags & SEC_CONSTRUCTOR) != 0)
    return TRUE;

  if (! _bfd_ecoff_slurp_symbol_table (abfd))
    return FALSE;

  amt = section->reloc_count;
  amt *= sizeof (arelent);
  internal_relocs = bfd_alloc (abfd, amt);

  external_reloc_size = backend->external_reloc_size;
  amt = external_reloc_size * section->reloc_count;
  external_relocs = bfd_alloc (abfd, amt);
  if (internal_relocs == NULL || external_relocs == NULL)
    return FALSE;
  if (bfd_seek (abfd, section->rel_filepos, SEEK_SET) != 0)
    return FALSE;
  if (bfd_bread (external_relocs, amt, abfd) != amt)
    return FALSE;

  for (i = 0, rptr = internal_relocs; i < section->reloc_count; i++, rptr++)
    {
      struct internal_reloc intern;

      (*backend->swap_reloc_in) (abfd,
				 external_relocs + i * external_reloc_size,
				 &intern);

      if (intern.r_extern)
	{
	  /* r_symndx is an index into the external symbols.  */
	  BFD_ASSERT (intern.r_symndx >= 0
		      && (intern.r_symndx
			  < (ecoff_data (abfd)
			     ->debug_info.symbolic_header.iextMax)));
	  rptr->sym_ptr_ptr = symbols + intern.r_symndx;
	  rptr->addend = 0;
	}
      else if (intern.r_symndx == RELOC_SECTION_NONE
	       || intern.r_symndx == RELOC_SECTION_ABS)
	{
	  rptr->sym_ptr_ptr = bfd_abs_section_ptr->symbol_ptr_ptr;
	  rptr->addend = 0;
	}
      else
	{
	  const char *sec_name;
	  asection *sec;

	  /* r_symndx is a section key.  */
	  switch (intern.r_symndx)
	    {
	    case RELOC_SECTION_TEXT:  sec_name = _TEXT;  break;
	    case RELOC_SECTION_RDATA: sec_name = _RDATA; break;
	    case RELOC_SECTION_DATA:  sec_name = _DATA;  break;
	    case RELOC_SECTION_SDATA: sec_name = _SDATA; break;
	    case RELOC_SECTION_SBSS:  sec_name = _SBSS;  break;
	    case RELOC_SECTION_BSS:   sec_name = _BSS;   break;
	    case RELOC_SECTION_INIT:  sec_name = _INIT;  break;
	    case RELOC_SECTION_LIT8:  sec_name = _LIT8;  break;
	    case RELOC_SECTION_LIT4:  sec_name = _LIT4;  break;
	    case RELOC_SECTION_XDATA: sec_name = _XDATA; break;
	    case RELOC_SECTION_PDATA: sec_name = _PDATA; break;
	    case RELOC_SECTION_FINI:  sec_name = _FINI;  break;
	    case RELOC_SECTION_LITA:  sec_name = _LITA;  break;
	    case RELOC_SECTION_RCONST: sec_name = _RCONST; break;
	    default: abort ();
	    }

	  sec = bfd_get_section_by_name (abfd, sec_name);
	  if (sec == NULL)
	    abort ();
	  rptr->sym_ptr_ptr = sec->symbol_ptr_ptr;

	  rptr->addend = - bfd_get_section_vma (abfd, sec);
	}

      rptr->address = intern.r_vaddr - bfd_get_section_vma (abfd, section);

      /* Let the backend select the howto field and do any other
	 required processing.  */
      (*backend->adjust_reloc_in) (abfd, &intern, rptr);
    }

  bfd_release (abfd, external_relocs);

  section->relocation = internal_relocs;

  return TRUE;
}

/* Get a canonical list of relocs.  */

long
_bfd_ecoff_canonicalize_reloc (bfd *abfd,
			       asection *section,
			       arelent **relptr,
			       asymbol **symbols)
{
  unsigned int count;

  if (section->flags & SEC_CONSTRUCTOR)
    {
      arelent_chain *chain;

      /* This section has relocs made up by us, not the file, so take
	 them out of their chain and place them into the data area
	 provided.  */
      for (count = 0, chain = section->constructor_chain;
	   count < section->reloc_count;
	   count++, chain = chain->next)
	*relptr++ = &chain->relent;
    }
  else
    {
      arelent *tblptr;

      if (! ecoff_slurp_reloc_table (abfd, section, symbols))
	return -1;

      tblptr = section->relocation;

      for (count = 0; count < section->reloc_count; count++)
	*relptr++ = tblptr++;
    }

  *relptr = NULL;

  return section->reloc_count;
}

/* Provided a BFD, a section and an offset into the section, calculate
   and return the name of the source file and the line nearest to the
   wanted location.  */

bfd_boolean
_bfd_ecoff_find_nearest_line (bfd *abfd,
			      asection *section,
			      asymbol **ignore_symbols ATTRIBUTE_UNUSED,
			      bfd_vma offset,
			      const char **filename_ptr,
			      const char **functionname_ptr,
			      unsigned int *retline_ptr)
{
  const struct ecoff_debug_swap * const debug_swap
    = &ecoff_backend (abfd)->debug_swap;
  struct ecoff_debug_info * const debug_info = &ecoff_data (abfd)->debug_info;
  struct ecoff_find_line *line_info;

  /* Make sure we have the FDR's.  */
  if (! _bfd_ecoff_slurp_symbolic_info (abfd, NULL, debug_info)
      || bfd_get_symcount (abfd) == 0)
    return FALSE;

  if (ecoff_data (abfd)->find_line_info == NULL)
    {
      bfd_size_type amt = sizeof (struct ecoff_find_line);

      ecoff_data (abfd)->find_line_info = bfd_zalloc (abfd, amt);
      if (ecoff_data (abfd)->find_line_info == NULL)
	return FALSE;
    }
  line_info = ecoff_data (abfd)->find_line_info;

  return _bfd_ecoff_locate_line (abfd, section, offset, debug_info,
				 debug_swap, line_info, filename_ptr,
				 functionname_ptr, retline_ptr);
}

/* Copy private BFD data.  This is called by objcopy and strip.  We
   use it to copy the ECOFF debugging information from one BFD to the
   other.  It would be theoretically possible to represent the ECOFF
   debugging information in the symbol table.  However, it would be a
   lot of work, and there would be little gain (gas, gdb, and ld
   already access the ECOFF debugging information via the
   ecoff_debug_info structure, and that structure would have to be
   retained in order to support ECOFF debugging in MIPS ELF).

   The debugging information for the ECOFF external symbols comes from
   the symbol table, so this function only handles the other debugging
   information.  */

bfd_boolean
_bfd_ecoff_bfd_copy_private_bfd_data (bfd *ibfd, bfd *obfd)
{
  struct ecoff_debug_info *iinfo = &ecoff_data (ibfd)->debug_info;
  struct ecoff_debug_info *oinfo = &ecoff_data (obfd)->debug_info;
  int i;
  asymbol **sym_ptr_ptr;
  size_t c;
  bfd_boolean local;

  /* We only want to copy information over if both BFD's use ECOFF
     format.  */
  if (bfd_get_flavour (ibfd) != bfd_target_ecoff_flavour
      || bfd_get_flavour (obfd) != bfd_target_ecoff_flavour)
    return TRUE;

  /* Copy the GP value and the register masks.  */
  ecoff_data (obfd)->gp = ecoff_data (ibfd)->gp;
  ecoff_data (obfd)->gprmask = ecoff_data (ibfd)->gprmask;
  ecoff_data (obfd)->fprmask = ecoff_data (ibfd)->fprmask;
  for (i = 0; i < 3; i++)
    ecoff_data (obfd)->cprmask[i] = ecoff_data (ibfd)->cprmask[i];

  /* Copy the version stamp.  */
  oinfo->symbolic_header.vstamp = iinfo->symbolic_header.vstamp;

  /* If there are no symbols, don't copy any debugging information.  */
  c = bfd_get_symcount (obfd);
  sym_ptr_ptr = bfd_get_outsymbols (obfd);
  if (c == 0 || sym_ptr_ptr == NULL)
    return TRUE;

  /* See if there are any local symbols.  */
  local = FALSE;
  for (; c > 0; c--, sym_ptr_ptr++)
    {
      if (ecoffsymbol (*sym_ptr_ptr)->local)
	{
	  local = TRUE;
	  break;
	}
    }

  if (local)
    {
      /* There are some local symbols.  We just bring over all the
	 debugging information.  FIXME: This is not quite the right
	 thing to do.  If the user has asked us to discard all
	 debugging information, then we are probably going to wind up
	 keeping it because there will probably be some local symbol
	 which objcopy did not discard.  We should actually break
	 apart the debugging information and only keep that which
	 applies to the symbols we want to keep.  */
      oinfo->symbolic_header.ilineMax = iinfo->symbolic_header.ilineMax;
      oinfo->symbolic_header.cbLine = iinfo->symbolic_header.cbLine;
      oinfo->line = iinfo->line;

      oinfo->symbolic_header.idnMax = iinfo->symbolic_header.idnMax;
      oinfo->external_dnr = iinfo->external_dnr;

      oinfo->symbolic_header.ipdMax = iinfo->symbolic_header.ipdMax;
      oinfo->external_pdr = iinfo->external_pdr;

      oinfo->symbolic_header.isymMax = iinfo->symbolic_header.isymMax;
      oinfo->external_sym = iinfo->external_sym;

      oinfo->symbolic_header.ioptMax = iinfo->symbolic_header.ioptMax;
      oinfo->external_opt = iinfo->external_opt;

      oinfo->symbolic_header.iauxMax = iinfo->symbolic_header.iauxMax;
      oinfo->external_aux = iinfo->external_aux;

      oinfo->symbolic_header.issMax = iinfo->symbolic_header.issMax;
      oinfo->ss = iinfo->ss;

      oinfo->symbolic_header.ifdMax = iinfo->symbolic_header.ifdMax;
      oinfo->external_fdr = iinfo->external_fdr;

      oinfo->symbolic_header.crfd = iinfo->symbolic_header.crfd;
      oinfo->external_rfd = iinfo->external_rfd;
    }
  else
    {
      /* We are discarding all the local symbol information.  Look
	 through the external symbols and remove all references to FDR
	 or aux information.  */
      c = bfd_get_symcount (obfd);
      sym_ptr_ptr = bfd_get_outsymbols (obfd);
      for (; c > 0; c--, sym_ptr_ptr++)
	{
	  EXTR esym;

	  (*(ecoff_backend (obfd)->debug_swap.swap_ext_in))
	    (obfd, ecoffsymbol (*sym_ptr_ptr)->native, &esym);
	  esym.ifd = ifdNil;
	  esym.asym.index = indexNil;
	  (*(ecoff_backend (obfd)->debug_swap.swap_ext_out))
	    (obfd, &esym, ecoffsymbol (*sym_ptr_ptr)->native);
	}
    }

  return TRUE;
}

/* Set the architecture.  The supported architecture is stored in the
   backend pointer.  We always set the architecture anyhow, since many
   callers ignore the return value.  */

bfd_boolean
_bfd_ecoff_set_arch_mach (bfd *abfd,
			  enum bfd_architecture arch,
			  unsigned long machine)
{
  bfd_default_set_arch_mach (abfd, arch, machine);
  return arch == ecoff_backend (abfd)->arch;
}

/* Get the size of the section headers.  */

int
_bfd_ecoff_sizeof_headers (bfd *abfd,
			   struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  asection *current;
  int c;
  int ret;

  c = 0;
  for (current = abfd->sections;
       current != NULL;
       current = current->next)
    ++c;

  ret = (bfd_coff_filhsz (abfd)
	 + bfd_coff_aoutsz (abfd)
	 + c * bfd_coff_scnhsz (abfd));
  return (int) BFD_ALIGN (ret, 16);
}

/* Get the contents of a section.  */

bfd_boolean
_bfd_ecoff_get_section_contents (bfd *abfd,
				 asection *section,
				 void * location,
				 file_ptr offset,
				 bfd_size_type count)
{
  return _bfd_generic_get_section_contents (abfd, section, location,
					    offset, count);
}

/* Sort sections by VMA, but put SEC_ALLOC sections first.  This is
   called via qsort.  */

static int
ecoff_sort_hdrs (const void * arg1, const void * arg2)
{
  const asection *hdr1 = *(const asection **) arg1;
  const asection *hdr2 = *(const asection **) arg2;

  if ((hdr1->flags & SEC_ALLOC) != 0)
    {
      if ((hdr2->flags & SEC_ALLOC) == 0)
	return -1;
    }
  else
    {
      if ((hdr2->flags & SEC_ALLOC) != 0)
	return 1;
    }
  if (hdr1->vma < hdr2->vma)
    return -1;
  else if (hdr1->vma > hdr2->vma)
    return 1;
  else
    return 0;
}

/* Calculate the file position for each section, and set
   reloc_filepos.  */

static bfd_boolean
ecoff_compute_section_file_positions (bfd *abfd)
{
  file_ptr sofar, file_sofar;
  asection **sorted_hdrs;
  asection *current;
  unsigned int i;
  file_ptr old_sofar;
  bfd_boolean rdata_in_text;
  bfd_boolean first_data, first_nonalloc;
  const bfd_vma round = ecoff_backend (abfd)->round;
  bfd_size_type amt;

  sofar = _bfd_ecoff_sizeof_headers (abfd, NULL);
  file_sofar = sofar;

  /* Sort the sections by VMA.  */
  amt = abfd->section_count;
  amt *= sizeof (asection *);
  sorted_hdrs = bfd_malloc (amt);
  if (sorted_hdrs == NULL)
    return FALSE;
  for (current = abfd->sections, i = 0;
       current != NULL;
       current = current->next, i++)
    sorted_hdrs[i] = current;
  BFD_ASSERT (i == abfd->section_count);

  qsort (sorted_hdrs, abfd->section_count, sizeof (asection *),
	 ecoff_sort_hdrs);

  /* Some versions of the OSF linker put the .rdata section in the
     text segment, and some do not.  */
  rdata_in_text = ecoff_backend (abfd)->rdata_in_text;
  if (rdata_in_text)
    {
      for (i = 0; i < abfd->section_count; i++)
	{
	  current = sorted_hdrs[i];
	  if (streq (current->name, _RDATA))
	    break;
	  if ((current->flags & SEC_CODE) == 0
	      && ! streq (current->name, _PDATA)
	      && ! streq (current->name, _RCONST))
	    {
	      rdata_in_text = FALSE;
	      break;
	    }
	}
    }
  ecoff_data (abfd)->rdata_in_text = rdata_in_text;

  first_data = TRUE;
  first_nonalloc = TRUE;
  for (i = 0; i < abfd->section_count; i++)
    {
      unsigned int alignment_power;

      current = sorted_hdrs[i];

      /* For the Alpha ECOFF .pdata section the lnnoptr field is
	 supposed to indicate the number of .pdata entries that are
	 really in the section.  Each entry is 8 bytes.  We store this
	 away in line_filepos before increasing the section size.  */
      if (streq (current->name, _PDATA))
	current->line_filepos = current->size / 8;

      alignment_power = current->alignment_power;

      /* On Ultrix, the data sections in an executable file must be
	 aligned to a page boundary within the file.  This does not
	 affect the section size, though.  FIXME: Does this work for
	 other platforms?  It requires some modification for the
	 Alpha, because .rdata on the Alpha goes with the text, not
	 the data.  */
      if ((abfd->flags & EXEC_P) != 0
	  && (abfd->flags & D_PAGED) != 0
	  && ! first_data
	  && (current->flags & SEC_CODE) == 0
	  && (! rdata_in_text
	      || ! streq (current->name, _RDATA))
	  && ! streq (current->name, _PDATA)
	  && ! streq (current->name, _RCONST))
	{
	  sofar = (sofar + round - 1) &~ (round - 1);
	  file_sofar = (file_sofar + round - 1) &~ (round - 1);
	  first_data = FALSE;
	}
      else if (streq (current->name, _LIB))
	{
	  /* On Irix 4, the location of contents of the .lib section
	     from a shared library section is also rounded up to a
	     page boundary.  */

	  sofar = (sofar + round - 1) &~ (round - 1);
	  file_sofar = (file_sofar + round - 1) &~ (round - 1);
	}
      else if (first_nonalloc
	       && (current->flags & SEC_ALLOC) == 0
	       && (abfd->flags & D_PAGED) != 0)
	{
	  /* Skip up to the next page for an unallocated section, such
             as the .comment section on the Alpha.  This leaves room
             for the .bss section.  */
	  first_nonalloc = FALSE;
	  sofar = (sofar + round - 1) &~ (round - 1);
	  file_sofar = (file_sofar + round - 1) &~ (round - 1);
	}

      /* Align the sections in the file to the same boundary on
	 which they are aligned in virtual memory.  */
      sofar = BFD_ALIGN (sofar, 1 << alignment_power);
      if ((current->flags & SEC_HAS_CONTENTS) != 0)
	file_sofar = BFD_ALIGN (file_sofar, 1 << alignment_power);

      if ((abfd->flags & D_PAGED) != 0
	  && (current->flags & SEC_ALLOC) != 0)
	{
	  sofar += (current->vma - sofar) % round;
	  if ((current->flags & SEC_HAS_CONTENTS) != 0)
	    file_sofar += (current->vma - file_sofar) % round;
	}

      if ((current->flags & (SEC_HAS_CONTENTS | SEC_LOAD)) != 0)
	current->filepos = file_sofar;

      sofar += current->size;
      if ((current->flags & SEC_HAS_CONTENTS) != 0)
	file_sofar += current->size;

      /* Make sure that this section is of the right size too.  */
      old_sofar = sofar;
      sofar = BFD_ALIGN (sofar, 1 << alignment_power);
      if ((current->flags & SEC_HAS_CONTENTS) != 0)
	file_sofar = BFD_ALIGN (file_sofar, 1 << alignment_power);
      current->size += sofar - old_sofar;
    }

  free (sorted_hdrs);
  sorted_hdrs = NULL;

  ecoff_data (abfd)->reloc_filepos = file_sofar;

  return TRUE;
}

/* Determine the location of the relocs for all the sections in the
   output file, as well as the location of the symbolic debugging
   information.  */

static bfd_size_type
ecoff_compute_reloc_file_positions (bfd *abfd)
{
  const bfd_size_type external_reloc_size =
    ecoff_backend (abfd)->external_reloc_size;
  file_ptr reloc_base;
  bfd_size_type reloc_size;
  asection *current;
  file_ptr sym_base;

  if (! abfd->output_has_begun)
    {
      if (! ecoff_compute_section_file_positions (abfd))
	abort ();
      abfd->output_has_begun = TRUE;
    }

  reloc_base = ecoff_data (abfd)->reloc_filepos;

  reloc_size = 0;
  for (current = abfd->sections;
       current != NULL;
       current = current->next)
    {
      if (current->reloc_count == 0)
	current->rel_filepos = 0;
      else
	{
	  bfd_size_type relsize;

	  current->rel_filepos = reloc_base;
	  relsize = current->reloc_count * external_reloc_size;
	  reloc_size += relsize;
	  reloc_base += relsize;
	}
    }

  sym_base = ecoff_data (abfd)->reloc_filepos + reloc_size;

  /* At least on Ultrix, the symbol table of an executable file must
     be aligned to a page boundary.  FIXME: Is this true on other
     platforms?  */
  if ((abfd->flags & EXEC_P) != 0
      && (abfd->flags & D_PAGED) != 0)
    sym_base = ((sym_base + ecoff_backend (abfd)->round - 1)
		&~ (ecoff_backend (abfd)->round - 1));

  ecoff_data (abfd)->sym_filepos = sym_base;

  return reloc_size;
}

/* Set the contents of a section.  */

bfd_boolean
_bfd_ecoff_set_section_contents (bfd *abfd,
				 asection *section,
				 const void * location,
				 file_ptr offset,
				 bfd_size_type count)
{
  file_ptr pos;

  /* This must be done first, because bfd_set_section_contents is
     going to set output_has_begun to TRUE.  */
  if (! abfd->output_has_begun
      && ! ecoff_compute_section_file_positions (abfd))
    return FALSE;

  /* Handle the .lib section specially so that Irix 4 shared libraries
     work out.  See coff_set_section_contents in coffcode.h.  */
  if (streq (section->name, _LIB))
    {
      bfd_byte *rec, *recend;

      rec = (bfd_byte *) location;
      recend = rec + count;
      while (rec < recend)
	{
	  ++section->lma;
	  rec += bfd_get_32 (abfd, rec) * 4;
	}

      BFD_ASSERT (rec == recend);
    }

  if (count == 0)
    return TRUE;

  pos = section->filepos + offset;
  if (bfd_seek (abfd, pos, SEEK_SET) != 0
      || bfd_bwrite (location, count, abfd) != count)
    return FALSE;

  return TRUE;
}

/* Get the GP value for an ECOFF file.  This is a hook used by
   nlmconv.  */

bfd_vma
bfd_ecoff_get_gp_value (bfd *abfd)
{
  if (bfd_get_flavour (abfd) != bfd_target_ecoff_flavour
      || bfd_get_format (abfd) != bfd_object)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return 0;
    }

  return ecoff_data (abfd)->gp;
}

/* Set the GP value for an ECOFF file.  This is a hook used by the
   assembler.  */

bfd_boolean
bfd_ecoff_set_gp_value (bfd *abfd, bfd_vma gp_value)
{
  if (bfd_get_flavour (abfd) != bfd_target_ecoff_flavour
      || bfd_get_format (abfd) != bfd_object)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }

  ecoff_data (abfd)->gp = gp_value;

  return TRUE;
}

/* Set the register masks for an ECOFF file.  This is a hook used by
   the assembler.  */

bfd_boolean
bfd_ecoff_set_regmasks (bfd *abfd,
			unsigned long gprmask,
			unsigned long fprmask,
			unsigned long *cprmask)
{
  ecoff_data_type *tdata;

  if (bfd_get_flavour (abfd) != bfd_target_ecoff_flavour
      || bfd_get_format (abfd) != bfd_object)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }

  tdata = ecoff_data (abfd);
  tdata->gprmask = gprmask;
  tdata->fprmask = fprmask;
  if (cprmask != NULL)
    {
      int i;

      for (i = 0; i < 3; i++)
	tdata->cprmask[i] = cprmask[i];
    }

  return TRUE;
}

/* Get ECOFF EXTR information for an external symbol.  This function
   is passed to bfd_ecoff_debug_externals.  */

static bfd_boolean
ecoff_get_extr (asymbol *sym, EXTR *esym)
{
  ecoff_symbol_type *ecoff_sym_ptr;
  bfd *input_bfd;

  if (bfd_asymbol_flavour (sym) != bfd_target_ecoff_flavour
      || ecoffsymbol (sym)->native == NULL)
    {
      /* Don't include debugging, local, or section symbols.  */
      if ((sym->flags & BSF_DEBUGGING) != 0
	  || (sym->flags & BSF_LOCAL) != 0
	  || (sym->flags & BSF_SECTION_SYM) != 0)
	return FALSE;

      esym->jmptbl = 0;
      esym->cobol_main = 0;
      esym->weakext = (sym->flags & BSF_WEAK) != 0;
      esym->reserved = 0;
      esym->ifd = ifdNil;
      /* FIXME: we can do better than this for st and sc.  */
      esym->asym.st = stGlobal;
      esym->asym.sc = scAbs;
      esym->asym.reserved = 0;
      esym->asym.index = indexNil;
      return TRUE;
    }

  ecoff_sym_ptr = ecoffsymbol (sym);

  if (ecoff_sym_ptr->local)
    return FALSE;

  input_bfd = bfd_asymbol_bfd (sym);
  (*(ecoff_backend (input_bfd)->debug_swap.swap_ext_in))
    (input_bfd, ecoff_sym_ptr->native, esym);

  /* If the symbol was defined by the linker, then esym will be
     undefined but sym will not be.  Get a better class for such a
     symbol.  */
  if ((esym->asym.sc == scUndefined
       || esym->asym.sc == scSUndefined)
      && ! bfd_is_und_section (bfd_get_section (sym)))
    esym->asym.sc = scAbs;

  /* Adjust the FDR index for the symbol by that used for the input
     BFD.  */
  if (esym->ifd != -1)
    {
      struct ecoff_debug_info *input_debug;

      input_debug = &ecoff_data (input_bfd)->debug_info;
      BFD_ASSERT (esym->ifd < input_debug->symbolic_header.ifdMax);
      if (input_debug->ifdmap != NULL)
	esym->ifd = input_debug->ifdmap[esym->ifd];
    }

  return TRUE;
}

/* Set the external symbol index.  This routine is passed to
   bfd_ecoff_debug_externals.  */

static void
ecoff_set_index (asymbol *sym, bfd_size_type indx)
{
  ecoff_set_sym_index (sym, indx);
}

/* Write out an ECOFF file.  */

bfd_boolean
_bfd_ecoff_write_object_contents (bfd *abfd)
{
  const struct ecoff_backend_data * const backend = ecoff_backend (abfd);
  const bfd_vma round = backend->round;
  const bfd_size_type filhsz = bfd_coff_filhsz (abfd);
  const bfd_size_type aoutsz = bfd_coff_aoutsz (abfd);
  const bfd_size_type scnhsz = bfd_coff_scnhsz (abfd);
  const bfd_size_type external_hdr_size
    = backend->debug_swap.external_hdr_size;
  const bfd_size_type external_reloc_size = backend->external_reloc_size;
  void (* const adjust_reloc_out) (bfd *, const arelent *, struct internal_reloc *)
    = backend->adjust_reloc_out;
  void (* const swap_reloc_out) (bfd *, const struct internal_reloc *, void *)
    = backend->swap_reloc_out;
  struct ecoff_debug_info * const debug = &ecoff_data (abfd)->debug_info;
  HDRR * const symhdr = &debug->symbolic_header;
  asection *current;
  unsigned int count;
  bfd_size_type reloc_size;
  bfd_size_type text_size;
  bfd_vma text_start;
  bfd_boolean set_text_start;
  bfd_size_type data_size;
  bfd_vma data_start;
  bfd_boolean set_data_start;
  bfd_size_type bss_size;
  void * buff = NULL;
  void * reloc_buff = NULL;
  struct internal_filehdr internal_f;
  struct internal_aouthdr internal_a;
  int i;

  /* Determine where the sections and relocs will go in the output
     file.  */
  reloc_size = ecoff_compute_reloc_file_positions (abfd);

  count = 1;
  for (current = abfd->sections;
       current != NULL;
       current = current->next)
    {
      current->target_index = count;
      ++count;
    }

  if ((abfd->flags & D_PAGED) != 0)
    text_size = _bfd_ecoff_sizeof_headers (abfd, NULL);
  else
    text_size = 0;
  text_start = 0;
  set_text_start = FALSE;
  data_size = 0;
  data_start = 0;
  set_data_start = FALSE;
  bss_size = 0;

  /* Write section headers to the file.  */

  /* Allocate buff big enough to hold a section header,
     file header, or a.out header.  */
  {
    bfd_size_type siz;

    siz = scnhsz;
    if (siz < filhsz)
      siz = filhsz;
    if (siz < aoutsz)
      siz = aoutsz;
    buff = bfd_malloc (siz);
    if (buff == NULL)
      goto error_return;
  }

  internal_f.f_nscns = 0;
  if (bfd_seek (abfd, (file_ptr) (filhsz + aoutsz), SEEK_SET) != 0)
    goto error_return;

  for (current = abfd->sections;
       current != NULL;
       current = current->next)
    {
      struct internal_scnhdr section;
      bfd_vma vma;

      ++internal_f.f_nscns;

      strncpy (section.s_name, current->name, sizeof section.s_name);

      /* This seems to be correct for Irix 4 shared libraries.  */
      vma = bfd_get_section_vma (abfd, current);
      if (streq (current->name, _LIB))
	section.s_vaddr = 0;
      else
	section.s_vaddr = vma;

      section.s_paddr = current->lma;
      section.s_size = current->size;

      /* If this section is unloadable then the scnptr will be 0.  */
      if ((current->flags & (SEC_LOAD | SEC_HAS_CONTENTS)) == 0)
	section.s_scnptr = 0;
      else
	section.s_scnptr = current->filepos;
      section.s_relptr = current->rel_filepos;

      /* FIXME: the lnnoptr of the .sbss or .sdata section of an
	 object file produced by the assembler is supposed to point to
	 information about how much room is required by objects of
	 various different sizes.  I think this only matters if we
	 want the linker to compute the best size to use, or
	 something.  I don't know what happens if the information is
	 not present.  */
      if (! streq (current->name, _PDATA))
	section.s_lnnoptr = 0;
      else
	{
	  /* The Alpha ECOFF .pdata section uses the lnnoptr field to
	     hold the number of entries in the section (each entry is
	     8 bytes).  We stored this in the line_filepos field in
	     ecoff_compute_section_file_positions.  */
	  section.s_lnnoptr = current->line_filepos;
	}

      section.s_nreloc = current->reloc_count;
      section.s_nlnno = 0;
      section.s_flags = ecoff_sec_to_styp_flags (current->name,
						 current->flags);

      if (bfd_coff_swap_scnhdr_out (abfd, (void *) &section, buff) == 0
	  || bfd_bwrite (buff, scnhsz, abfd) != scnhsz)
	goto error_return;

      if ((section.s_flags & STYP_TEXT) != 0
	  || ((section.s_flags & STYP_RDATA) != 0
	      && ecoff_data (abfd)->rdata_in_text)
	  || section.s_flags == STYP_PDATA
	  || (section.s_flags & STYP_DYNAMIC) != 0
	  || (section.s_flags & STYP_LIBLIST) != 0
	  || (section.s_flags & STYP_RELDYN) != 0
	  || section.s_flags == STYP_CONFLIC
	  || (section.s_flags & STYP_DYNSTR) != 0
	  || (section.s_flags & STYP_DYNSYM) != 0
	  || (section.s_flags & STYP_HASH) != 0
	  || (section.s_flags & STYP_ECOFF_INIT) != 0
	  || (section.s_flags & STYP_ECOFF_FINI) != 0
	  || section.s_flags == STYP_RCONST)
	{
	  text_size += current->size;
	  if (! set_text_start || text_start > vma)
	    {
	      text_start = vma;
	      set_text_start = TRUE;
	    }
	}
      else if ((section.s_flags & STYP_RDATA) != 0
	       || (section.s_flags & STYP_DATA) != 0
	       || (section.s_flags & STYP_LITA) != 0
	       || (section.s_flags & STYP_LIT8) != 0
	       || (section.s_flags & STYP_LIT4) != 0
	       || (section.s_flags & STYP_SDATA) != 0
	       || section.s_flags == STYP_XDATA
	       || (section.s_flags & STYP_GOT) != 0)
	{
	  data_size += current->size;
	  if (! set_data_start || data_start > vma)
	    {
	      data_start = vma;
	      set_data_start = TRUE;
	    }
	}
      else if ((section.s_flags & STYP_BSS) != 0
	       || (section.s_flags & STYP_SBSS) != 0)
	bss_size += current->size;
      else if (section.s_flags == 0
	       || (section.s_flags & STYP_ECOFF_LIB) != 0
	       || section.s_flags == STYP_COMMENT)
	/* Do nothing.  */ ;
      else
	abort ();
    }

  /* Set up the file header.  */
  internal_f.f_magic = ecoff_get_magic (abfd);

  /* We will NOT put a fucking timestamp in the header here. Every
     time you put it back, I will come in and take it out again.  I'm
     sorry.  This field does not belong here.  We fill it with a 0 so
     it compares the same but is not a reasonable time. --
     gnu@cygnus.com.  */
  internal_f.f_timdat = 0;

  if (bfd_get_symcount (abfd) != 0)
    {
      /* The ECOFF f_nsyms field is not actually the number of
	 symbols, it's the size of symbolic information header.  */
      internal_f.f_nsyms = external_hdr_size;
      internal_f.f_symptr = ecoff_data (abfd)->sym_filepos;
    }
  else
    {
      internal_f.f_nsyms = 0;
      internal_f.f_symptr = 0;
    }

  internal_f.f_opthdr = aoutsz;

  internal_f.f_flags = F_LNNO;
  if (reloc_size == 0)
    internal_f.f_flags |= F_RELFLG;
  if (bfd_get_symcount (abfd) == 0)
    internal_f.f_flags |= F_LSYMS;
  if (abfd->flags & EXEC_P)
    internal_f.f_flags |= F_EXEC;

  if (bfd_little_endian (abfd))
    internal_f.f_flags |= F_AR32WR;
  else
    internal_f.f_flags |= F_AR32W;

  /* Set up the ``optional'' header.  */
  if ((abfd->flags & D_PAGED) != 0)
    internal_a.magic = ECOFF_AOUT_ZMAGIC;
  else
    internal_a.magic = ECOFF_AOUT_OMAGIC;

  /* FIXME: Is this really correct?  */
  internal_a.vstamp = symhdr->vstamp;

  /* At least on Ultrix, these have to be rounded to page boundaries.
     FIXME: Is this true on other platforms?  */
  if ((abfd->flags & D_PAGED) != 0)
    {
      internal_a.tsize = (text_size + round - 1) &~ (round - 1);
      internal_a.text_start = text_start &~ (round - 1);
      internal_a.dsize = (data_size + round - 1) &~ (round - 1);
      internal_a.data_start = data_start &~ (round - 1);
    }
  else
    {
      internal_a.tsize = text_size;
      internal_a.text_start = text_start;
      internal_a.dsize = data_size;
      internal_a.data_start = data_start;
    }

  /* On Ultrix, the initial portions of the .sbss and .bss segments
     are at the end of the data section.  The bsize field in the
     optional header records how many bss bytes are required beyond
     those in the data section.  The value is not rounded to a page
     boundary.  */
  if (bss_size < internal_a.dsize - data_size)
    bss_size = 0;
  else
    bss_size -= internal_a.dsize - data_size;
  internal_a.bsize = bss_size;
  internal_a.bss_start = internal_a.data_start + internal_a.dsize;

  internal_a.entry = bfd_get_start_address (abfd);

  internal_a.gp_value = ecoff_data (abfd)->gp;

  internal_a.gprmask = ecoff_data (abfd)->gprmask;
  internal_a.fprmask = ecoff_data (abfd)->fprmask;
  for (i = 0; i < 4; i++)
    internal_a.cprmask[i] = ecoff_data (abfd)->cprmask[i];

  /* Let the backend adjust the headers if necessary.  */
  if (backend->adjust_headers)
    {
      if (! (*backend->adjust_headers) (abfd, &internal_f, &internal_a))
	goto error_return;
    }

  /* Write out the file header and the optional header.  */
  if (bfd_seek (abfd, (file_ptr) 0, SEEK_SET) != 0)
    goto error_return;

  bfd_coff_swap_filehdr_out (abfd, (void *) &internal_f, buff);
  if (bfd_bwrite (buff, filhsz, abfd) != filhsz)
    goto error_return;

  bfd_coff_swap_aouthdr_out (abfd, (void *) &internal_a, buff);
  if (bfd_bwrite (buff, aoutsz, abfd) != aoutsz)
    goto error_return;

  /* Build the external symbol information.  This must be done before
     writing out the relocs so that we know the symbol indices.  We
     don't do this if this BFD was created by the backend linker,
     since it will have already handled the symbols and relocs.  */
  if (! ecoff_data (abfd)->linker)
    {
      symhdr->iextMax = 0;
      symhdr->issExtMax = 0;
      debug->external_ext = debug->external_ext_end = NULL;
      debug->ssext = debug->ssext_end = NULL;
      if (! bfd_ecoff_debug_externals (abfd, debug, &backend->debug_swap,
				       (abfd->flags & EXEC_P) == 0,
				       ecoff_get_extr, ecoff_set_index))
	goto error_return;

      /* Write out the relocs.  */
      for (current = abfd->sections;
	   current != NULL;
	   current = current->next)
	{
	  arelent **reloc_ptr_ptr;
	  arelent **reloc_end;
	  char *out_ptr;
	  bfd_size_type amt;

	  if (current->reloc_count == 0)
	    continue;

	  amt = current->reloc_count * external_reloc_size;
	  reloc_buff = bfd_alloc (abfd, amt);
	  if (reloc_buff == NULL)
	    goto error_return;

	  reloc_ptr_ptr = current->orelocation;
	  reloc_end = reloc_ptr_ptr + current->reloc_count;
	  out_ptr = (char *) reloc_buff;

	  for (;
	       reloc_ptr_ptr < reloc_end;
	       reloc_ptr_ptr++, out_ptr += external_reloc_size)
	    {
	      arelent *reloc;
	      asymbol *sym;
	      struct internal_reloc in;

	      memset ((void *) &in, 0, sizeof in);

	      reloc = *reloc_ptr_ptr;
	      sym = *reloc->sym_ptr_ptr;

	      /* If the howto field has not been initialised then skip this reloc.
		 This assumes that an error message has been issued elsewhere.  */
	      if (reloc->howto == NULL)
		continue;

	      in.r_vaddr = (reloc->address
			    + bfd_get_section_vma (abfd, current));
	      in.r_type = reloc->howto->type;

	      if ((sym->flags & BSF_SECTION_SYM) == 0)
		{
		  in.r_symndx = ecoff_get_sym_index (*reloc->sym_ptr_ptr);
		  in.r_extern = 1;
		}
	      else
		{
		  const char *name;
		  unsigned int i;
		  static struct
		  {
		    const char * name;
		    long r_symndx;
		  }
		  section_symndx [] =
		  {
		    { _TEXT,   RELOC_SECTION_TEXT   },
		    { _RDATA,  RELOC_SECTION_RDATA  },
		    { _DATA,   RELOC_SECTION_DATA   },
		    { _SDATA,  RELOC_SECTION_SDATA  },
		    { _SBSS,   RELOC_SECTION_SBSS   },
		    { _BSS,    RELOC_SECTION_BSS    },
		    { _INIT,   RELOC_SECTION_INIT   },
		    { _LIT8,   RELOC_SECTION_LIT8   },
		    { _LIT4,   RELOC_SECTION_LIT4   },
		    { _XDATA,  RELOC_SECTION_XDATA  },
		    { _PDATA,  RELOC_SECTION_PDATA  },
		    { _FINI,   RELOC_SECTION_FINI   },
		    { _LITA,   RELOC_SECTION_LITA   },
		    { "*ABS*", RELOC_SECTION_ABS    },
		    { _RCONST, RELOC_SECTION_RCONST }
		  };

		  name = bfd_get_section_name (abfd, bfd_get_section (sym));

		  for (i = 0; i < ARRAY_SIZE (section_symndx); i++)
		    if (streq (name, section_symndx[i].name))
		      {
			in.r_symndx = section_symndx[i].r_symndx;
			break;
		      }

		  if (i == ARRAY_SIZE (section_symndx))
		    abort ();
		  in.r_extern = 0;
		}

	      (*adjust_reloc_out) (abfd, reloc, &in);

	      (*swap_reloc_out) (abfd, &in, (void *) out_ptr);
	    }

	  if (bfd_seek (abfd, current->rel_filepos, SEEK_SET) != 0)
	    goto error_return;
	  amt = current->reloc_count * external_reloc_size;
	  if (bfd_bwrite (reloc_buff, amt, abfd) != amt)
	    goto error_return;
	  bfd_release (abfd, reloc_buff);
	  reloc_buff = NULL;
	}

      /* Write out the symbolic debugging information.  */
      if (bfd_get_symcount (abfd) > 0)
	{
	  /* Write out the debugging information.  */
	  if (! bfd_ecoff_write_debug (abfd, debug, &backend->debug_swap,
				       ecoff_data (abfd)->sym_filepos))
	    goto error_return;
	}
    }

  /* The .bss section of a demand paged executable must receive an
     entire page.  If there are symbols, the symbols will start on the
     next page.  If there are no symbols, we must fill out the page by
     hand.  */
  if (bfd_get_symcount (abfd) == 0
      && (abfd->flags & EXEC_P) != 0
      && (abfd->flags & D_PAGED) != 0)
    {
      char c;

      if (bfd_seek (abfd, (file_ptr) ecoff_data (abfd)->sym_filepos - 1,
		    SEEK_SET) != 0)
	goto error_return;
      if (bfd_bread (&c, (bfd_size_type) 1, abfd) == 0)
	c = 0;
      if (bfd_seek (abfd, (file_ptr) ecoff_data (abfd)->sym_filepos - 1,
		    SEEK_SET) != 0)
	goto error_return;
      if (bfd_bwrite (&c, (bfd_size_type) 1, abfd) != 1)
	goto error_return;
    }

  if (reloc_buff != NULL)
    bfd_release (abfd, reloc_buff);
  if (buff != NULL)
    free (buff);
  return TRUE;
 error_return:
  if (reloc_buff != NULL)
    bfd_release (abfd, reloc_buff);
  if (buff != NULL)
    free (buff);
  return FALSE;
}

/* Archive handling.  ECOFF uses what appears to be a unique type of
   archive header (armap).  The byte ordering of the armap and the
   contents are encoded in the name of the armap itself.  At least for
   now, we only support archives with the same byte ordering in the
   armap and the contents.

   The first four bytes in the armap are the number of symbol
   definitions.  This is always a power of two.

   This is followed by the symbol definitions.  Each symbol definition
   occupies 8 bytes.  The first four bytes are the offset from the
   start of the armap strings to the null-terminated string naming
   this symbol.  The second four bytes are the file offset to the
   archive member which defines this symbol.  If the second four bytes
   are 0, then this is not actually a symbol definition, and it should
   be ignored.

   The symbols are hashed into the armap with a closed hashing scheme.
   See the functions below for the details of the algorithm.

   After the symbol definitions comes four bytes holding the size of
   the string table, followed by the string table itself.  */

/* The name of an archive headers looks like this:
   __________E[BL]E[BL]_ (with a trailing space).
   The trailing space is changed to an X if the archive is changed to
   indicate that the armap is out of date.

   The Alpha seems to use ________64E[BL]E[BL]_.  */

#define ARMAP_BIG_ENDIAN 		'B'
#define ARMAP_LITTLE_ENDIAN 		'L'
#define ARMAP_MARKER 			'E'
#define ARMAP_START_LENGTH 		10
#define ARMAP_HEADER_MARKER_INDEX	10
#define ARMAP_HEADER_ENDIAN_INDEX 	11
#define ARMAP_OBJECT_MARKER_INDEX 	12
#define ARMAP_OBJECT_ENDIAN_INDEX 	13
#define ARMAP_END_INDEX 		14
#define ARMAP_END 			"_ "

/* This is a magic number used in the hashing algorithm.  */
#define ARMAP_HASH_MAGIC 		0x9dd68ab5

/* This returns the hash value to use for a string.  It also sets
   *REHASH to the rehash adjustment if the first slot is taken.  SIZE
   is the number of entries in the hash table, and HLOG is the log
   base 2 of SIZE.  */

static unsigned int
ecoff_armap_hash (const char *s,
		  unsigned int *rehash,
		  unsigned int size,
		  unsigned int hlog)
{
  unsigned int hash;

  if (hlog == 0)
    return 0;
  hash = *s++;
  while (*s != '\0')
    hash = ((hash >> 27) | (hash << 5)) + *s++;
  hash *= ARMAP_HASH_MAGIC;
  *rehash = (hash & (size - 1)) | 1;
  return hash >> (32 - hlog);
}

/* Read in the armap.  */

bfd_boolean
_bfd_ecoff_slurp_armap (bfd *abfd)
{
  char nextname[17];
  unsigned int i;
  struct areltdata *mapdata;
  bfd_size_type parsed_size;
  char *raw_armap;
  struct artdata *ardata;
  unsigned int count;
  char *raw_ptr;
  struct symdef *symdef_ptr;
  char *stringbase;
  bfd_size_type amt;

  /* Get the name of the first element.  */
  i = bfd_bread ((void *) nextname, (bfd_size_type) 16, abfd);
  if (i == 0)
      return TRUE;
  if (i != 16)
      return FALSE;

  if (bfd_seek (abfd, (file_ptr) -16, SEEK_CUR) != 0)
    return FALSE;

  /* Irix 4.0.5F apparently can use either an ECOFF armap or a
     standard COFF armap.  We could move the ECOFF armap stuff into
     bfd_slurp_armap, but that seems inappropriate since no other
     target uses this format.  Instead, we check directly for a COFF
     armap.  */
  if (CONST_STRNEQ (nextname, "/               "))
    return bfd_slurp_armap (abfd);

  /* See if the first element is an armap.  */
  if (! strneq (nextname, ecoff_backend (abfd)->armap_start, ARMAP_START_LENGTH)
      || nextname[ARMAP_HEADER_MARKER_INDEX] != ARMAP_MARKER
      || (nextname[ARMAP_HEADER_ENDIAN_INDEX] != ARMAP_BIG_ENDIAN
	  && nextname[ARMAP_HEADER_ENDIAN_INDEX] != ARMAP_LITTLE_ENDIAN)
      || nextname[ARMAP_OBJECT_MARKER_INDEX] != ARMAP_MARKER
      || (nextname[ARMAP_OBJECT_ENDIAN_INDEX] != ARMAP_BIG_ENDIAN
	  && nextname[ARMAP_OBJECT_ENDIAN_INDEX] != ARMAP_LITTLE_ENDIAN)
      || ! strneq (nextname + ARMAP_END_INDEX, ARMAP_END, sizeof ARMAP_END - 1))
    {
      bfd_has_map (abfd) = FALSE;
      return TRUE;
    }

  /* Make sure we have the right byte ordering.  */
  if (((nextname[ARMAP_HEADER_ENDIAN_INDEX] == ARMAP_BIG_ENDIAN)
       ^ (bfd_header_big_endian (abfd)))
      || ((nextname[ARMAP_OBJECT_ENDIAN_INDEX] == ARMAP_BIG_ENDIAN)
	  ^ (bfd_big_endian (abfd))))
    {
      bfd_set_error (bfd_error_wrong_format);
      return FALSE;
    }

  /* Read in the armap.  */
  ardata = bfd_ardata (abfd);
  mapdata = (struct areltdata *) _bfd_read_ar_hdr (abfd);
  if (mapdata == NULL)
    return FALSE;
  parsed_size = mapdata->parsed_size;
  bfd_release (abfd, (void *) mapdata);

  raw_armap = bfd_alloc (abfd, parsed_size);
  if (raw_armap == NULL)
    return FALSE;

  if (bfd_bread ((void *) raw_armap, parsed_size, abfd) != parsed_size)
    {
      if (bfd_get_error () != bfd_error_system_call)
	bfd_set_error (bfd_error_malformed_archive);
      bfd_release (abfd, (void *) raw_armap);
      return FALSE;
    }

  ardata->tdata = (void *) raw_armap;

  count = H_GET_32 (abfd, raw_armap);

  ardata->symdef_count = 0;
  ardata->cache = NULL;

  /* This code used to overlay the symdefs over the raw archive data,
     but that doesn't work on a 64 bit host.  */
  stringbase = raw_armap + count * 8 + 8;

#ifdef CHECK_ARMAP_HASH
  {
    unsigned int hlog;

    /* Double check that I have the hashing algorithm right by making
       sure that every symbol can be looked up successfully.  */
    hlog = 0;
    for (i = 1; i < count; i <<= 1)
      hlog++;
    BFD_ASSERT (i == count);

    raw_ptr = raw_armap + 4;
    for (i = 0; i < count; i++, raw_ptr += 8)
      {
	unsigned int name_offset, file_offset;
	unsigned int hash, rehash, srch;

	name_offset = H_GET_32 (abfd, raw_ptr);
	file_offset = H_GET_32 (abfd, (raw_ptr + 4));
	if (file_offset == 0)
	  continue;
	hash = ecoff_armap_hash (stringbase + name_offset, &rehash, count,
				 hlog);
	if (hash == i)
	  continue;

	/* See if we can rehash to this location.  */
	for (srch = (hash + rehash) & (count - 1);
	     srch != hash && srch != i;
	     srch = (srch + rehash) & (count - 1))
	  BFD_ASSERT (H_GET_32 (abfd, (raw_armap + 8 + srch * 8)) != 0);
	BFD_ASSERT (srch == i);
      }
  }

#endif /* CHECK_ARMAP_HASH */

  raw_ptr = raw_armap + 4;
  for (i = 0; i < count; i++, raw_ptr += 8)
    if (H_GET_32 (abfd, (raw_ptr + 4)) != 0)
      ++ardata->symdef_count;

  amt = ardata->symdef_count;
  amt *= sizeof (struct symdef);
  symdef_ptr = bfd_alloc (abfd, amt);
  if (!symdef_ptr)
    return FALSE;

  ardata->symdefs = (carsym *) symdef_ptr;

  raw_ptr = raw_armap + 4;
  for (i = 0; i < count; i++, raw_ptr += 8)
    {
      unsigned int name_offset, file_offset;

      file_offset = H_GET_32 (abfd, (raw_ptr + 4));
      if (file_offset == 0)
	continue;
      name_offset = H_GET_32 (abfd, raw_ptr);
      symdef_ptr->s.name = stringbase + name_offset;
      symdef_ptr->file_offset = file_offset;
      ++symdef_ptr;
    }

  ardata->first_file_filepos = bfd_tell (abfd);
  /* Pad to an even boundary.  */
  ardata->first_file_filepos += ardata->first_file_filepos % 2;

  bfd_has_map (abfd) = TRUE;

  return TRUE;
}

/* Write out an armap.  */

bfd_boolean
_bfd_ecoff_write_armap (bfd *abfd,
			unsigned int elength,
			struct orl *map,
			unsigned int orl_count,
			int stridx)
{
  unsigned int hashsize, hashlog;
  bfd_size_type symdefsize;
  int padit;
  unsigned int stringsize;
  unsigned int mapsize;
  file_ptr firstreal;
  struct ar_hdr hdr;
  struct stat statbuf;
  unsigned int i;
  bfd_byte temp[4];
  bfd_byte *hashtable;
  bfd *current;
  bfd *last_elt;

  /* Ultrix appears to use as a hash table size the least power of two
     greater than twice the number of entries.  */
  for (hashlog = 0; ((unsigned int) 1 << hashlog) <= 2 * orl_count; hashlog++)
    ;
  hashsize = 1 << hashlog;

  symdefsize = hashsize * 8;
  padit = stridx % 2;
  stringsize = stridx + padit;

  /* Include 8 bytes to store symdefsize and stringsize in output.  */
  mapsize = symdefsize + stringsize + 8;

  firstreal = SARMAG + sizeof (struct ar_hdr) + mapsize + elength;

  memset ((void *) &hdr, 0, sizeof hdr);

  /* Work out the ECOFF armap name.  */
  strcpy (hdr.ar_name, ecoff_backend (abfd)->armap_start);
  hdr.ar_name[ARMAP_HEADER_MARKER_INDEX] = ARMAP_MARKER;
  hdr.ar_name[ARMAP_HEADER_ENDIAN_INDEX] =
    (bfd_header_big_endian (abfd)
     ? ARMAP_BIG_ENDIAN
     : ARMAP_LITTLE_ENDIAN);
  hdr.ar_name[ARMAP_OBJECT_MARKER_INDEX] = ARMAP_MARKER;
  hdr.ar_name[ARMAP_OBJECT_ENDIAN_INDEX] =
    bfd_big_endian (abfd) ? ARMAP_BIG_ENDIAN : ARMAP_LITTLE_ENDIAN;
  memcpy (hdr.ar_name + ARMAP_END_INDEX, ARMAP_END, sizeof ARMAP_END - 1);

  /* Write the timestamp of the archive header to be just a little bit
     later than the timestamp of the file, otherwise the linker will
     complain that the index is out of date.  Actually, the Ultrix
     linker just checks the archive name; the GNU linker may check the
     date.  */
  stat (abfd->filename, &statbuf);
  sprintf (hdr.ar_date, "%ld", (long) (statbuf.st_mtime + 60));

  /* The DECstation uses zeroes for the uid, gid and mode of the
     armap.  */
  hdr.ar_uid[0] = '0';
  hdr.ar_gid[0] = '0';
  /* Building gcc ends up extracting the armap as a file - twice.  */
  hdr.ar_mode[0] = '6';
  hdr.ar_mode[1] = '4';
  hdr.ar_mode[2] = '4';

  sprintf (hdr.ar_size, "%-10d", (int) mapsize);

  hdr.ar_fmag[0] = '`';
  hdr.ar_fmag[1] = '\012';

  /* Turn all null bytes in the header into spaces.  */
  for (i = 0; i < sizeof (struct ar_hdr); i++)
   if (((char *) (&hdr))[i] == '\0')
     (((char *) (&hdr))[i]) = ' ';

  if (bfd_bwrite ((void *) &hdr, (bfd_size_type) sizeof (struct ar_hdr), abfd)
      != sizeof (struct ar_hdr))
    return FALSE;

  H_PUT_32 (abfd, hashsize, temp);
  if (bfd_bwrite ((void *) temp, (bfd_size_type) 4, abfd) != 4)
    return FALSE;

  hashtable = bfd_zalloc (abfd, symdefsize);
  if (!hashtable)
    return FALSE;

  current = abfd->archive_head;
  last_elt = current;
  for (i = 0; i < orl_count; i++)
    {
      unsigned int hash, rehash = 0;

      /* Advance firstreal to the file position of this archive
	 element.  */
      if (map[i].u.abfd != last_elt)
	{
	  do
	    {
	      firstreal += arelt_size (current) + sizeof (struct ar_hdr);
	      firstreal += firstreal % 2;
	      current = current->archive_next;
	    }
	  while (current != map[i].u.abfd);
	}

      last_elt = current;

      hash = ecoff_armap_hash (*map[i].name, &rehash, hashsize, hashlog);
      if (H_GET_32 (abfd, (hashtable + (hash * 8) + 4)) != 0)
	{
	  unsigned int srch;

	  /* The desired slot is already taken.  */
	  for (srch = (hash + rehash) & (hashsize - 1);
	       srch != hash;
	       srch = (srch + rehash) & (hashsize - 1))
	    if (H_GET_32 (abfd, (hashtable + (srch * 8) + 4)) == 0)
	      break;

	  BFD_ASSERT (srch != hash);

	  hash = srch;
	}

      H_PUT_32 (abfd, map[i].namidx, (hashtable + hash * 8));
      H_PUT_32 (abfd, firstreal, (hashtable + hash * 8 + 4));
    }

  if (bfd_bwrite ((void *) hashtable, symdefsize, abfd) != symdefsize)
    return FALSE;

  bfd_release (abfd, hashtable);

  /* Now write the strings.  */
  H_PUT_32 (abfd, stringsize, temp);
  if (bfd_bwrite ((void *) temp, (bfd_size_type) 4, abfd) != 4)
    return FALSE;
  for (i = 0; i < orl_count; i++)
    {
      bfd_size_type len;

      len = strlen (*map[i].name) + 1;
      if (bfd_bwrite ((void *) (*map[i].name), len, abfd) != len)
	return FALSE;
    }

  /* The spec sez this should be a newline.  But in order to be
     bug-compatible for DECstation ar we use a null.  */
  if (padit)
    {
      if (bfd_bwrite ("", (bfd_size_type) 1, abfd) != 1)
	return FALSE;
    }

  return TRUE;
}

/* See whether this BFD is an archive.  If it is, read in the armap
   and the extended name table.  */

const bfd_target *
_bfd_ecoff_archive_p (bfd *abfd)
{
  struct artdata *tdata_hold;
  char armag[SARMAG + 1];
  bfd_size_type amt;

  if (bfd_bread ((void *) armag, (bfd_size_type) SARMAG, abfd) != SARMAG)
    {
      if (bfd_get_error () != bfd_error_system_call)
	bfd_set_error (bfd_error_wrong_format);
      return NULL;
    }

  if (! strneq (armag, ARMAG, SARMAG))
    {
      bfd_set_error (bfd_error_wrong_format);
      return NULL;
    }

  tdata_hold = bfd_ardata (abfd);

  amt = sizeof (struct artdata);
  bfd_ardata (abfd) = bfd_zalloc (abfd, amt);
  if (bfd_ardata (abfd) == NULL)
    {
      bfd_ardata (abfd) = tdata_hold;
      return NULL;
    }

  bfd_ardata (abfd)->first_file_filepos = SARMAG;
  /* Already cleared by bfd_zalloc above.
     bfd_ardata (abfd)->cache = NULL;
     bfd_ardata (abfd)->archive_head = NULL;
     bfd_ardata (abfd)->symdefs = NULL;
     bfd_ardata (abfd)->extended_names = NULL;
     bfd_ardata (abfd)->extended_names_size = 0;
     bfd_ardata (abfd)->tdata = NULL;  */

  if (! _bfd_ecoff_slurp_armap (abfd)
      || ! _bfd_ecoff_slurp_extended_name_table (abfd))
    {
      bfd_release (abfd, bfd_ardata (abfd));
      bfd_ardata (abfd) = tdata_hold;
      return NULL;
    }

  if (bfd_has_map (abfd))
    {
      bfd *first;

      /* This archive has a map, so we may presume that the contents
	 are object files.  Make sure that if the first file in the
	 archive can be recognized as an object file, it is for this
	 target.  If not, assume that this is the wrong format.  If
	 the first file is not an object file, somebody is doing
	 something weird, and we permit it so that ar -t will work.  */

      first = bfd_openr_next_archived_file (abfd, NULL);
      if (first != NULL)
	{
	  first->target_defaulted = FALSE;
	  if (bfd_check_format (first, bfd_object)
	      && first->xvec != abfd->xvec)
	    {
	      /* We ought to close `first' here, but we can't, because
		 we have no way to remove it from the archive cache.
		 It's almost impossible to figure out when we can
		 release bfd_ardata.  FIXME.  */
	      bfd_set_error (bfd_error_wrong_object_format);
	      bfd_ardata (abfd) = tdata_hold;
	      return NULL;
	    }
	  /* And we ought to close `first' here too.  */
	}
    }

  return abfd->xvec;
}

/* ECOFF linker code.  */

/* Routine to create an entry in an ECOFF link hash table.  */

static struct bfd_hash_entry *
ecoff_link_hash_newfunc (struct bfd_hash_entry *entry,
			 struct bfd_hash_table *table,
			 const char *string)
{
  struct ecoff_link_hash_entry *ret = (struct ecoff_link_hash_entry *) entry;

  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (ret == NULL)
    ret = ((struct ecoff_link_hash_entry *)
	   bfd_hash_allocate (table, sizeof (struct ecoff_link_hash_entry)));
  if (ret == NULL)
    return NULL;

  /* Call the allocation method of the superclass.  */
  ret = ((struct ecoff_link_hash_entry *)
	 _bfd_link_hash_newfunc ((struct bfd_hash_entry *) ret,
				 table, string));

  if (ret)
    {
      /* Set local fields.  */
      ret->indx = -1;
      ret->abfd = NULL;
      ret->written = 0;
      ret->small = 0;
    }
  memset ((void *) &ret->esym, 0, sizeof ret->esym);

  return (struct bfd_hash_entry *) ret;
}

/* Create an ECOFF link hash table.  */

struct bfd_link_hash_table *
_bfd_ecoff_bfd_link_hash_table_create (bfd *abfd)
{
  struct ecoff_link_hash_table *ret;
  bfd_size_type amt = sizeof (struct ecoff_link_hash_table);

  ret = bfd_malloc (amt);
  if (ret == NULL)
    return NULL;
  if (!_bfd_link_hash_table_init (&ret->root, abfd,
				  ecoff_link_hash_newfunc,
				  sizeof (struct ecoff_link_hash_entry)))
    {
      free (ret);
      return NULL;
    }
  return &ret->root;
}

/* Look up an entry in an ECOFF link hash table.  */

#define ecoff_link_hash_lookup(table, string, create, copy, follow) \
  ((struct ecoff_link_hash_entry *) \
   bfd_link_hash_lookup (&(table)->root, (string), (create), (copy), (follow)))

/* Traverse an ECOFF link hash table.  */

#define ecoff_link_hash_traverse(table, func, info)			\
  (bfd_link_hash_traverse						\
   (&(table)->root,							\
    (bfd_boolean (*) (struct bfd_link_hash_entry *, void *)) (func),	\
    (info)))

/* Get the ECOFF link hash table from the info structure.  This is
   just a cast.  */

#define ecoff_hash_table(p) ((struct ecoff_link_hash_table *) ((p)->hash))

/* Add the external symbols of an object file to the global linker
   hash table.  The external symbols and strings we are passed are
   just allocated on the stack, and will be discarded.  We must
   explicitly save any information we may need later on in the link.
   We do not want to read the external symbol information again.  */

static bfd_boolean
ecoff_link_add_externals (bfd *abfd,
			  struct bfd_link_info *info,
			  void * external_ext,
			  char *ssext)
{
  const struct ecoff_backend_data * const backend = ecoff_backend (abfd);
  void (* const swap_ext_in) (bfd *, void *, EXTR *)
    = backend->debug_swap.swap_ext_in;
  bfd_size_type external_ext_size = backend->debug_swap.external_ext_size;
  unsigned long ext_count;
  struct bfd_link_hash_entry **sym_hash;
  char *ext_ptr;
  char *ext_end;
  bfd_size_type amt;

  ext_count = ecoff_data (abfd)->debug_info.symbolic_header.iextMax;

  amt = ext_count;
  amt *= sizeof (struct bfd_link_hash_entry *);
  sym_hash = bfd_alloc (abfd, amt);
  if (!sym_hash)
    return FALSE;
  ecoff_data (abfd)->sym_hashes = (struct ecoff_link_hash_entry **) sym_hash;

  ext_ptr = (char *) external_ext;
  ext_end = ext_ptr + ext_count * external_ext_size;
  for (; ext_ptr < ext_end; ext_ptr += external_ext_size, sym_hash++)
    {
      EXTR esym;
      bfd_boolean skip;
      bfd_vma value;
      asection *section;
      const char *name;
      struct ecoff_link_hash_entry *h;

      *sym_hash = NULL;

      (*swap_ext_in) (abfd, (void *) ext_ptr, &esym);

      /* Skip debugging symbols.  */
      skip = FALSE;
      switch (esym.asym.st)
	{
	case stGlobal:
	case stStatic:
	case stLabel:
	case stProc:
	case stStaticProc:
	  break;
	default:
	  skip = TRUE;
	  break;
	}

      if (skip)
	continue;

      /* Get the information for this symbol.  */
      value = esym.asym.value;
      switch (esym.asym.sc)
	{
	default:
	case scNil:
	case scRegister:
	case scCdbLocal:
	case scBits:
	case scCdbSystem:
	case scRegImage:
	case scInfo:
	case scUserStruct:
	case scVar:
	case scVarRegister:
	case scVariant:
	case scBasedVar:
	case scXData:
	case scPData:
	  section = NULL;
	  break;
	case scText:
	  section = bfd_make_section_old_way (abfd, _TEXT);
	  value -= section->vma;
	  break;
	case scData:
	  section = bfd_make_section_old_way (abfd, _DATA);
	  value -= section->vma;
	  break;
	case scBss:
	  section = bfd_make_section_old_way (abfd, _BSS);
	  value -= section->vma;
	  break;
	case scAbs:
	  section = bfd_abs_section_ptr;
	  break;
	case scUndefined:
	  section = bfd_und_section_ptr;
	  break;
	case scSData:
	  section = bfd_make_section_old_way (abfd, _SDATA);
	  value -= section->vma;
	  break;
	case scSBss:
	  section = bfd_make_section_old_way (abfd, _SBSS);
	  value -= section->vma;
	  break;
	case scRData:
	  section = bfd_make_section_old_way (abfd, _RDATA);
	  value -= section->vma;
	  break;
	case scCommon:
	  if (value > ecoff_data (abfd)->gp_size)
	    {
	      section = bfd_com_section_ptr;
	      break;
	    }
	  /* Fall through.  */
	case scSCommon:
	  if (ecoff_scom_section.name == NULL)
	    {
	      /* Initialize the small common section.  */
	      ecoff_scom_section.name = SCOMMON;
	      ecoff_scom_section.flags = SEC_IS_COMMON;
	      ecoff_scom_section.output_section = &ecoff_scom_section;
	      ecoff_scom_section.symbol = &ecoff_scom_symbol;
	      ecoff_scom_section.symbol_ptr_ptr = &ecoff_scom_symbol_ptr;
	      ecoff_scom_symbol.name = SCOMMON;
	      ecoff_scom_symbol.flags = BSF_SECTION_SYM;
	      ecoff_scom_symbol.section = &ecoff_scom_section;
	      ecoff_scom_symbol_ptr = &ecoff_scom_symbol;
	    }
	  section = &ecoff_scom_section;
	  break;
	case scSUndefined:
	  section = bfd_und_section_ptr;
	  break;
	case scInit:
	  section = bfd_make_section_old_way (abfd, _INIT);
	  value -= section->vma;
	  break;
	case scFini:
	  section = bfd_make_section_old_way (abfd, _FINI);
	  value -= section->vma;
	  break;
	case scRConst:
	  section = bfd_make_section_old_way (abfd, _RCONST);
	  value -= section->vma;
	  break;
	}

      if (section == NULL)
	continue;

      name = ssext + esym.asym.iss;

      if (! (_bfd_generic_link_add_one_symbol
	     (info, abfd, name,
	      (flagword) (esym.weakext ? BSF_WEAK : BSF_GLOBAL),
	      section, value, NULL, TRUE, TRUE, sym_hash)))
	return FALSE;

      h = (struct ecoff_link_hash_entry *) *sym_hash;

      /* If we are building an ECOFF hash table, save the external
	 symbol information.  */
      if (info->hash->creator->flavour == bfd_get_flavour (abfd))
	{
	  if (h->abfd == NULL
	      || (! bfd_is_und_section (section)
		  && (! bfd_is_com_section (section)
		      || (h->root.type != bfd_link_hash_defined
			  && h->root.type != bfd_link_hash_defweak))))
	    {
	      h->abfd = abfd;
	      h->esym = esym;
	    }

	  /* Remember whether this symbol was small undefined.  */
	  if (esym.asym.sc == scSUndefined)
	    h->small = 1;

	  /* If this symbol was ever small undefined, it needs to wind
	     up in a GP relative section.  We can't control the
	     section of a defined symbol, but we can control the
	     section of a common symbol.  This case is actually needed
	     on Ultrix 4.2 to handle the symbol cred in -lckrb.  */
	  if (h->small
	      && h->root.type == bfd_link_hash_common
	      && streq (h->root.u.c.p->section->name, SCOMMON))
	    {
	      h->root.u.c.p->section = bfd_make_section_old_way (abfd,
								 SCOMMON);
	      h->root.u.c.p->section->flags = SEC_ALLOC;
	      if (h->esym.asym.sc == scCommon)
		h->esym.asym.sc = scSCommon;
	    }
	}
    }

  return TRUE;
}

/* Add symbols from an ECOFF object file to the global linker hash
   table.  */

static bfd_boolean
ecoff_link_add_object_symbols (bfd *abfd, struct bfd_link_info *info)
{
  HDRR *symhdr;
  bfd_size_type external_ext_size;
  void * external_ext = NULL;
  bfd_size_type esize;
  char *ssext = NULL;
  bfd_boolean result;

  if (! ecoff_slurp_symbolic_header (abfd))
    return FALSE;

  /* If there are no symbols, we don't want it.  */
  if (bfd_get_symcount (abfd) == 0)
    return TRUE;

  symhdr = &ecoff_data (abfd)->debug_info.symbolic_header;

  /* Read in the external symbols and external strings.  */
  external_ext_size = ecoff_backend (abfd)->debug_swap.external_ext_size;
  esize = symhdr->iextMax * external_ext_size;
  external_ext = bfd_malloc (esize);
  if (external_ext == NULL && esize != 0)
    goto error_return;

  if (bfd_seek (abfd, (file_ptr) symhdr->cbExtOffset, SEEK_SET) != 0
      || bfd_bread (external_ext, esize, abfd) != esize)
    goto error_return;

  ssext = bfd_malloc ((bfd_size_type) symhdr->issExtMax);
  if (ssext == NULL && symhdr->issExtMax != 0)
    goto error_return;

  if (bfd_seek (abfd, (file_ptr) symhdr->cbSsExtOffset, SEEK_SET) != 0
      || (bfd_bread (ssext, (bfd_size_type) symhdr->issExtMax, abfd)
	  != (bfd_size_type) symhdr->issExtMax))
    goto error_return;

  result = ecoff_link_add_externals (abfd, info, external_ext, ssext);

  if (ssext != NULL)
    free (ssext);
  if (external_ext != NULL)
    free (external_ext);
  return result;

 error_return:
  if (ssext != NULL)
    free (ssext);
  if (external_ext != NULL)
    free (external_ext);
  return FALSE;
}

/* This is called if we used _bfd_generic_link_add_archive_symbols
   because we were not dealing with an ECOFF archive.  */

static bfd_boolean
ecoff_link_check_archive_element (bfd *abfd,
				  struct bfd_link_info *info,
				  bfd_boolean *pneeded)
{
  const struct ecoff_backend_data * const backend = ecoff_backend (abfd);
  void (* const swap_ext_in) (bfd *, void *, EXTR *)
    = backend->debug_swap.swap_ext_in;
  HDRR *symhdr;
  bfd_size_type external_ext_size;
  void * external_ext = NULL;
  bfd_size_type esize;
  char *ssext = NULL;
  char *ext_ptr;
  char *ext_end;

  *pneeded = FALSE;

  if (! ecoff_slurp_symbolic_header (abfd))
    goto error_return;

  /* If there are no symbols, we don't want it.  */
  if (bfd_get_symcount (abfd) == 0)
    goto successful_return;

  symhdr = &ecoff_data (abfd)->debug_info.symbolic_header;

  /* Read in the external symbols and external strings.  */
  external_ext_size = backend->debug_swap.external_ext_size;
  esize = symhdr->iextMax * external_ext_size;
  external_ext = bfd_malloc (esize);
  if (external_ext == NULL && esize != 0)
    goto error_return;

  if (bfd_seek (abfd, (file_ptr) symhdr->cbExtOffset, SEEK_SET) != 0
      || bfd_bread (external_ext, esize, abfd) != esize)
    goto error_return;

  ssext = bfd_malloc ((bfd_size_type) symhdr->issExtMax);
  if (ssext == NULL && symhdr->issExtMax != 0)
    goto error_return;

  if (bfd_seek (abfd, (file_ptr) symhdr->cbSsExtOffset, SEEK_SET) != 0
      || (bfd_bread (ssext, (bfd_size_type) symhdr->issExtMax, abfd)
	  != (bfd_size_type) symhdr->issExtMax))
    goto error_return;

  /* Look through the external symbols to see if they define some
     symbol that is currently undefined.  */
  ext_ptr = (char *) external_ext;
  ext_end = ext_ptr + esize;
  for (; ext_ptr < ext_end; ext_ptr += external_ext_size)
    {
      EXTR esym;
      bfd_boolean def;
      const char *name;
      struct bfd_link_hash_entry *h;

      (*swap_ext_in) (abfd, (void *) ext_ptr, &esym);

      /* See if this symbol defines something.  */
      if (esym.asym.st != stGlobal
	  && esym.asym.st != stLabel
	  && esym.asym.st != stProc)
	continue;

      switch (esym.asym.sc)
	{
	case scText:
	case scData:
	case scBss:
	case scAbs:
	case scSData:
	case scSBss:
	case scRData:
	case scCommon:
	case scSCommon:
	case scInit:
	case scFini:
	case scRConst:
	  def = TRUE;
	  break;
	default:
	  def = FALSE;
	  break;
	}

      if (! def)
	continue;

      name = ssext + esym.asym.iss;
      h = bfd_link_hash_lookup (info->hash, name, FALSE, FALSE, TRUE);

      /* Unlike the generic linker, we do not pull in elements because
	 of common symbols.  */
      if (h == NULL
	  || h->type != bfd_link_hash_undefined)
	continue;

      /* Include this element.  */
      if (! (*info->callbacks->add_archive_element) (info, abfd, name))
	goto error_return;
      if (! ecoff_link_add_externals (abfd, info, external_ext, ssext))
	goto error_return;

      *pneeded = TRUE;
      goto successful_return;
    }

 successful_return:
  if (external_ext != NULL)
    free (external_ext);
  if (ssext != NULL)
    free (ssext);
  return TRUE;
 error_return:
  if (external_ext != NULL)
    free (external_ext);
  if (ssext != NULL)
    free (ssext);
  return FALSE;
}

/* Add the symbols from an archive file to the global hash table.
   This looks through the undefined symbols, looks each one up in the
   archive hash table, and adds any associated object file.  We do not
   use _bfd_generic_link_add_archive_symbols because ECOFF archives
   already have a hash table, so there is no reason to construct
   another one.  */

static bfd_boolean
ecoff_link_add_archive_symbols (bfd *abfd, struct bfd_link_info *info)
{
  const struct ecoff_backend_data * const backend = ecoff_backend (abfd);
  const bfd_byte *raw_armap;
  struct bfd_link_hash_entry **pundef;
  unsigned int armap_count;
  unsigned int armap_log;
  unsigned int i;
  const bfd_byte *hashtable;
  const char *stringbase;

  if (! bfd_has_map (abfd))
    {
      /* An empty archive is a special case.  */
      if (bfd_openr_next_archived_file (abfd, NULL) == NULL)
	return TRUE;
      bfd_set_error (bfd_error_no_armap);
      return FALSE;
    }

  /* If we don't have any raw data for this archive, as can happen on
     Irix 4.0.5F, we call the generic routine.
     FIXME: We should be more clever about this, since someday tdata
     may get to something for a generic archive.  */
  raw_armap = (const bfd_byte *) bfd_ardata (abfd)->tdata;
  if (raw_armap == NULL)
    return (_bfd_generic_link_add_archive_symbols
	    (abfd, info, ecoff_link_check_archive_element));

  armap_count = H_GET_32 (abfd, raw_armap);

  armap_log = 0;
  for (i = 1; i < armap_count; i <<= 1)
    armap_log++;
  BFD_ASSERT (i == armap_count);

  hashtable = raw_armap + 4;
  stringbase = (const char *) raw_armap + armap_count * 8 + 8;

  /* Look through the list of undefined symbols.  */
  pundef = &info->hash->undefs;
  while (*pundef != NULL)
    {
      struct bfd_link_hash_entry *h;
      unsigned int hash, rehash = 0;
      unsigned int file_offset;
      const char *name;
      bfd *element;

      h = *pundef;

      /* When a symbol is defined, it is not necessarily removed from
	 the list.  */
      if (h->type != bfd_link_hash_undefined
	  && h->type != bfd_link_hash_common)
	{
	  /* Remove this entry from the list, for general cleanliness
	     and because we are going to look through the list again
	     if we search any more libraries.  We can't remove the
	     entry if it is the tail, because that would lose any
	     entries we add to the list later on.  */
	  if (*pundef != info->hash->undefs_tail)
	    *pundef = (*pundef)->u.undef.next;
	  else
	    pundef = &(*pundef)->u.undef.next;
	  continue;
	}

      /* Native ECOFF linkers do not pull in archive elements merely
	 to satisfy common definitions, so neither do we.  We leave
	 them on the list, though, in case we are linking against some
	 other object format.  */
      if (h->type != bfd_link_hash_undefined)
	{
	  pundef = &(*pundef)->u.undef.next;
	  continue;
	}

      /* Look for this symbol in the archive hash table.  */
      hash = ecoff_armap_hash (h->root.string, &rehash, armap_count,
			       armap_log);

      file_offset = H_GET_32 (abfd, hashtable + (hash * 8) + 4);
      if (file_offset == 0)
	{
	  /* Nothing in this slot.  */
	  pundef = &(*pundef)->u.undef.next;
	  continue;
	}

      name = stringbase + H_GET_32 (abfd, hashtable + (hash * 8));
      if (name[0] != h->root.string[0]
	  || ! streq (name, h->root.string))
	{
	  unsigned int srch;
	  bfd_boolean found;

	  /* That was the wrong symbol.  Try rehashing.  */
	  found = FALSE;
	  for (srch = (hash + rehash) & (armap_count - 1);
	       srch != hash;
	       srch = (srch + rehash) & (armap_count - 1))
	    {
	      file_offset = H_GET_32 (abfd, hashtable + (srch * 8) + 4);
	      if (file_offset == 0)
		break;
	      name = stringbase + H_GET_32 (abfd, hashtable + (srch * 8));
	      if (name[0] == h->root.string[0]
		  && streq (name, h->root.string))
		{
		  found = TRUE;
		  break;
		}
	    }

	  if (! found)
	    {
	      pundef = &(*pundef)->u.undef.next;
	      continue;
	    }

	  hash = srch;
	}

      element = (*backend->get_elt_at_filepos) (abfd, (file_ptr) file_offset);
      if (element == NULL)
	return FALSE;

      if (! bfd_check_format (element, bfd_object))
	return FALSE;

      /* Unlike the generic linker, we know that this element provides
	 a definition for an undefined symbol and we know that we want
	 to include it.  We don't need to check anything.  */
      if (! (*info->callbacks->add_archive_element) (info, element, name))
	return FALSE;
      if (! ecoff_link_add_object_symbols (element, info))
	return FALSE;

      pundef = &(*pundef)->u.undef.next;
    }

  return TRUE;
}

/* Given an ECOFF BFD, add symbols to the global hash table as
   appropriate.  */

bfd_boolean
_bfd_ecoff_bfd_link_add_symbols (bfd *abfd, struct bfd_link_info *info)
{
  switch (bfd_get_format (abfd))
    {
    case bfd_object:
      return ecoff_link_add_object_symbols (abfd, info);
    case bfd_archive:
      return ecoff_link_add_archive_symbols (abfd, info);
    default:
      bfd_set_error (bfd_error_wrong_format);
      return FALSE;
    }
}


/* ECOFF final link routines.  */

/* Structure used to pass information to ecoff_link_write_external.  */

struct extsym_info
{
  bfd *abfd;
  struct bfd_link_info *info;
};

/* Accumulate the debugging information for an input BFD into the
   output BFD.  This must read in the symbolic information of the
   input BFD.  */

static bfd_boolean
ecoff_final_link_debug_accumulate (bfd *output_bfd,
				   bfd *input_bfd,
				   struct bfd_link_info *info,
				   void * handle)
{
  struct ecoff_debug_info * const debug = &ecoff_data (input_bfd)->debug_info;
  const struct ecoff_debug_swap * const swap =
    &ecoff_backend (input_bfd)->debug_swap;
  HDRR *symhdr = &debug->symbolic_header;
  bfd_boolean ret;

#define READ(ptr, offset, count, size, type)				 \
  if (symhdr->count == 0)						 \
    debug->ptr = NULL;							 \
  else									 \
    {									 \
      bfd_size_type amt = (bfd_size_type) size * symhdr->count;		 \
      debug->ptr = bfd_malloc (amt);					 \
      if (debug->ptr == NULL)						 \
	{								 \
          ret = FALSE;							 \
          goto return_something;					 \
	}								 \
      if (bfd_seek (input_bfd, (file_ptr) symhdr->offset, SEEK_SET) != 0 \
	  || bfd_bread (debug->ptr, amt, input_bfd) != amt)		 \
	{								 \
          ret = FALSE;							 \
          goto return_something;					 \
	}								 \
    }

  /* If raw_syments is not NULL, then the data was already by read by
     _bfd_ecoff_slurp_symbolic_info.  */
  if (ecoff_data (input_bfd)->raw_syments == NULL)
    {
      READ (line, cbLineOffset, cbLine, sizeof (unsigned char),
	    unsigned char *);
      READ (external_dnr, cbDnOffset, idnMax, swap->external_dnr_size, void *);
      READ (external_pdr, cbPdOffset, ipdMax, swap->external_pdr_size, void *);
      READ (external_sym, cbSymOffset, isymMax, swap->external_sym_size, void *);
      READ (external_opt, cbOptOffset, ioptMax, swap->external_opt_size, void *);
      READ (external_aux, cbAuxOffset, iauxMax, sizeof (union aux_ext),
	    union aux_ext *);
      READ (ss, cbSsOffset, issMax, sizeof (char), char *);
      READ (external_fdr, cbFdOffset, ifdMax, swap->external_fdr_size, void *);
      READ (external_rfd, cbRfdOffset, crfd, swap->external_rfd_size, void *);
    }
#undef READ

  /* We do not read the external strings or the external symbols.  */

  ret = (bfd_ecoff_debug_accumulate
	 (handle, output_bfd, &ecoff_data (output_bfd)->debug_info,
	  &ecoff_backend (output_bfd)->debug_swap,
	  input_bfd, debug, swap, info));

 return_something:
  if (ecoff_data (input_bfd)->raw_syments == NULL)
    {
      if (debug->line != NULL)
	free (debug->line);
      if (debug->external_dnr != NULL)
	free (debug->external_dnr);
      if (debug->external_pdr != NULL)
	free (debug->external_pdr);
      if (debug->external_sym != NULL)
	free (debug->external_sym);
      if (debug->external_opt != NULL)
	free (debug->external_opt);
      if (debug->external_aux != NULL)
	free (debug->external_aux);
      if (debug->ss != NULL)
	free (debug->ss);
      if (debug->external_fdr != NULL)
	free (debug->external_fdr);
      if (debug->external_rfd != NULL)
	free (debug->external_rfd);

      /* Make sure we don't accidentally follow one of these pointers
	 into freed memory.  */
      debug->line = NULL;
      debug->external_dnr = NULL;
      debug->external_pdr = NULL;
      debug->external_sym = NULL;
      debug->external_opt = NULL;
      debug->external_aux = NULL;
      debug->ss = NULL;
      debug->external_fdr = NULL;
      debug->external_rfd = NULL;
    }

  return ret;
}

/* Relocate and write an ECOFF section into an ECOFF output file.  */

static bfd_boolean
ecoff_indirect_link_order (bfd *output_bfd,
			   struct bfd_link_info *info,
			   asection *output_section,
			   struct bfd_link_order *link_order)
{
  asection *input_section;
  bfd *input_bfd;
  bfd_byte *contents = NULL;
  bfd_size_type external_reloc_size;
  bfd_size_type external_relocs_size;
  void * external_relocs = NULL;

  BFD_ASSERT ((output_section->flags & SEC_HAS_CONTENTS) != 0);

  input_section = link_order->u.indirect.section;
  input_bfd = input_section->owner;
  if (input_section->size == 0)
    return TRUE;

  BFD_ASSERT (input_section->output_section == output_section);
  BFD_ASSERT (input_section->output_offset == link_order->offset);
  BFD_ASSERT (input_section->size == link_order->size);

  /* Get the section contents.  */
  if (!bfd_malloc_and_get_section (input_bfd, input_section, &contents))
    goto error_return;

  /* Get the relocs.  If we are relaxing MIPS code, they will already
     have been read in.  Otherwise, we read them in now.  */
  external_reloc_size = ecoff_backend (input_bfd)->external_reloc_size;
  external_relocs_size = external_reloc_size * input_section->reloc_count;

  external_relocs = bfd_malloc (external_relocs_size);
  if (external_relocs == NULL && external_relocs_size != 0)
    goto error_return;

  if (bfd_seek (input_bfd, input_section->rel_filepos, SEEK_SET) != 0
      || (bfd_bread (external_relocs, external_relocs_size, input_bfd)
	  != external_relocs_size))
    goto error_return;

  /* Relocate the section contents.  */
  if (! ((*ecoff_backend (input_bfd)->relocate_section)
	 (output_bfd, info, input_bfd, input_section, contents,
	  external_relocs)))
    goto error_return;

  /* Write out the relocated section.  */
  if (! bfd_set_section_contents (output_bfd,
				  output_section,
				  contents,
				  input_section->output_offset,
				  input_section->size))
    goto error_return;

  /* If we are producing relocatable output, the relocs were
     modified, and we write them out now.  We use the reloc_count
     field of output_section to keep track of the number of relocs we
     have output so far.  */
  if (info->relocatable)
    {
      file_ptr pos = (output_section->rel_filepos
		      + output_section->reloc_count * external_reloc_size);
      if (bfd_seek (output_bfd, pos, SEEK_SET) != 0
	  || (bfd_bwrite (external_relocs, external_relocs_size, output_bfd)
	      != external_relocs_size))
	goto error_return;
      output_section->reloc_count += input_section->reloc_count;
    }

  if (contents != NULL)
    free (contents);
  if (external_relocs != NULL)
    free (external_relocs);
  return TRUE;

 error_return:
  if (contents != NULL)
    free (contents);
  if (external_relocs != NULL)
    free (external_relocs);
  return FALSE;
}

/* Generate a reloc when linking an ECOFF file.  This is a reloc
   requested by the linker, and does come from any input file.  This
   is used to build constructor and destructor tables when linking
   with -Ur.  */

static bfd_boolean
ecoff_reloc_link_order (bfd *output_bfd,
			struct bfd_link_info *info,
			asection *output_section,
			struct bfd_link_order *link_order)
{
  enum bfd_link_order_type type;
  asection *section;
  bfd_vma addend;
  arelent rel;
  struct internal_reloc in;
  bfd_size_type external_reloc_size;
  bfd_byte *rbuf;
  bfd_boolean ok;
  file_ptr pos;

  type = link_order->type;
  section = NULL;
  addend = link_order->u.reloc.p->addend;

  /* We set up an arelent to pass to the backend adjust_reloc_out
     routine.  */
  rel.address = link_order->offset;

  rel.howto = bfd_reloc_type_lookup (output_bfd, link_order->u.reloc.p->reloc);
  if (rel.howto == 0)
    {
      bfd_set_error (bfd_error_bad_value);
      return FALSE;
    }

  if (type == bfd_section_reloc_link_order)
    {
      section = link_order->u.reloc.p->u.section;
      rel.sym_ptr_ptr = section->symbol_ptr_ptr;
    }
  else
    {
      struct bfd_link_hash_entry *h;

      /* Treat a reloc against a defined symbol as though it were
         actually against the section.  */
      h = bfd_wrapped_link_hash_lookup (output_bfd, info,
					link_order->u.reloc.p->u.name,
					FALSE, FALSE, FALSE);
      if (h != NULL
	  && (h->type == bfd_link_hash_defined
	      || h->type == bfd_link_hash_defweak))
	{
	  type = bfd_section_reloc_link_order;
	  section = h->u.def.section->output_section;
	  /* It seems that we ought to add the symbol value to the
             addend here, but in practice it has already been added
             because it was passed to constructor_callback.  */
	  addend += section->vma + h->u.def.section->output_offset;
	}
      else
	{
	  /* We can't set up a reloc against a symbol correctly,
	     because we have no asymbol structure.  Currently no
	     adjust_reloc_out routine cares.  */
	  rel.sym_ptr_ptr = NULL;
	}
    }

  /* All ECOFF relocs are in-place.  Put the addend into the object
     file.  */

  BFD_ASSERT (rel.howto->partial_inplace);
  if (addend != 0)
    {
      bfd_size_type size;
      bfd_reloc_status_type rstat;
      bfd_byte *buf;

      size = bfd_get_reloc_size (rel.howto);
      buf = bfd_zmalloc (size);
      if (buf == NULL)
	return FALSE;
      rstat = _bfd_relocate_contents (rel.howto, output_bfd,
				      (bfd_vma) addend, buf);
      switch (rstat)
	{
	case bfd_reloc_ok:
	  break;
	default:
	case bfd_reloc_outofrange:
	  abort ();
	case bfd_reloc_overflow:
	  if (! ((*info->callbacks->reloc_overflow)
		 (info, NULL,
		  (link_order->type == bfd_section_reloc_link_order
		   ? bfd_section_name (output_bfd, section)
		   : link_order->u.reloc.p->u.name),
		  rel.howto->name, addend, NULL,
		  NULL, (bfd_vma) 0)))
	    {
	      free (buf);
	      return FALSE;
	    }
	  break;
	}
      ok = bfd_set_section_contents (output_bfd, output_section, (void *) buf,
				     (file_ptr) link_order->offset, size);
      free (buf);
      if (! ok)
	return FALSE;
    }

  rel.addend = 0;

  /* Move the information into an internal_reloc structure.  */
  in.r_vaddr = (rel.address
		+ bfd_get_section_vma (output_bfd, output_section));
  in.r_type = rel.howto->type;

  if (type == bfd_symbol_reloc_link_order)
    {
      struct ecoff_link_hash_entry *h;

      h = ((struct ecoff_link_hash_entry *)
	   bfd_wrapped_link_hash_lookup (output_bfd, info,
					 link_order->u.reloc.p->u.name,
					 FALSE, FALSE, TRUE));
      if (h != NULL
	  && h->indx != -1)
	in.r_symndx = h->indx;
      else
	{
	  if (! ((*info->callbacks->unattached_reloc)
		 (info, link_order->u.reloc.p->u.name, NULL,
		  NULL, (bfd_vma) 0)))
	    return FALSE;
	  in.r_symndx = 0;
	}
      in.r_extern = 1;
    }
  else
    {
      const char *name;
      unsigned int i;
      static struct
      {
	const char * name;
	long r_symndx;
      }
      section_symndx [] =
      {
	{ _TEXT,   RELOC_SECTION_TEXT   },
	{ _RDATA,  RELOC_SECTION_RDATA  },
	{ _DATA,   RELOC_SECTION_DATA   },
	{ _SDATA,  RELOC_SECTION_SDATA  },
	{ _SBSS,   RELOC_SECTION_SBSS   },
	{ _BSS,    RELOC_SECTION_BSS    },
	{ _INIT,   RELOC_SECTION_INIT   },
	{ _LIT8,   RELOC_SECTION_LIT8   },
	{ _LIT4,   RELOC_SECTION_LIT4   },
	{ _XDATA,  RELOC_SECTION_XDATA  },
	{ _PDATA,  RELOC_SECTION_PDATA  },
	{ _FINI,   RELOC_SECTION_FINI   },
	{ _LITA,   RELOC_SECTION_LITA   },
	{ "*ABS*", RELOC_SECTION_ABS    },
	{ _RCONST, RELOC_SECTION_RCONST }
      };

      name = bfd_get_section_name (output_bfd, section);

      for (i = 0; i < ARRAY_SIZE (section_symndx); i++)
	if (streq (name, section_symndx[i].name))
	  {
	    in.r_symndx = section_symndx[i].r_symndx;
	    break;
	  }

      if (i == ARRAY_SIZE (section_symndx))
	abort ();

      in.r_extern = 0;
    }

  /* Let the BFD backend adjust the reloc.  */
  (*ecoff_backend (output_bfd)->adjust_reloc_out) (output_bfd, &rel, &in);

  /* Get some memory and swap out the reloc.  */
  external_reloc_size = ecoff_backend (output_bfd)->external_reloc_size;
  rbuf = bfd_malloc (external_reloc_size);
  if (rbuf == NULL)
    return FALSE;

  (*ecoff_backend (output_bfd)->swap_reloc_out) (output_bfd, &in, (void *) rbuf);

  pos = (output_section->rel_filepos
	 + output_section->reloc_count * external_reloc_size);
  ok = (bfd_seek (output_bfd, pos, SEEK_SET) == 0
	&& (bfd_bwrite ((void *) rbuf, external_reloc_size, output_bfd)
	    == external_reloc_size));

  if (ok)
    ++output_section->reloc_count;

  free (rbuf);

  return ok;
}

/* Put out information for an external symbol.  These come only from
   the hash table.  */

static bfd_boolean
ecoff_link_write_external (struct ecoff_link_hash_entry *h, void * data)
{
  struct extsym_info *einfo = (struct extsym_info *) data;
  bfd *output_bfd = einfo->abfd;
  bfd_boolean strip;

  if (h->root.type == bfd_link_hash_warning)
    {
      h = (struct ecoff_link_hash_entry *) h->root.u.i.link;
      if (h->root.type == bfd_link_hash_new)
	return TRUE;
    }

  /* We need to check if this symbol is being stripped.  */
  if (h->root.type == bfd_link_hash_undefined
      || h->root.type == bfd_link_hash_undefweak)
    strip = FALSE;
  else if (einfo->info->strip == strip_all
	   || (einfo->info->strip == strip_some
	       && bfd_hash_lookup (einfo->info->keep_hash,
				   h->root.root.string,
				   FALSE, FALSE) == NULL))
    strip = TRUE;
  else
    strip = FALSE;

  if (strip || h->written)
    return TRUE;

  if (h->abfd == NULL)
    {
      h->esym.jmptbl = 0;
      h->esym.cobol_main = 0;
      h->esym.weakext = 0;
      h->esym.reserved = 0;
      h->esym.ifd = ifdNil;
      h->esym.asym.value = 0;
      h->esym.asym.st = stGlobal;

      if (h->root.type != bfd_link_hash_defined
	  && h->root.type != bfd_link_hash_defweak)
	h->esym.asym.sc = scAbs;
      else
	{
	  asection *output_section;
	  const char *name;
	  unsigned int i;
	  static struct
	  {
	    const char * name;
	    int sc;
	  }
	  section_storage_classes [] =
	  {
	    { _TEXT,   scText   },
	    { _DATA,   scData   },
	    { _SDATA,  scSData  },
	    { _RDATA,  scRData  },
	    { _BSS,    scBss    },
	    { _SBSS,   scSBss   },
	    { _INIT,   scInit   },
	    { _FINI,   scFini   },
	    { _PDATA,  scPData  },
	    { _XDATA,  scXData  },
	    { _RCONST, scRConst }
	  };

	  output_section = h->root.u.def.section->output_section;
	  name = bfd_section_name (output_section->owner, output_section);

	  for (i = 0; i < ARRAY_SIZE (section_storage_classes); i++)
	    if (streq (name, section_storage_classes[i].name))
	      {
		h->esym.asym.sc = section_storage_classes[i].sc;
		break;
	      }

	  if (i == ARRAY_SIZE (section_storage_classes))
	    h->esym.asym.sc = scAbs;
	}

      h->esym.asym.reserved = 0;
      h->esym.asym.index = indexNil;
    }
  else if (h->esym.ifd != -1)
    {
      struct ecoff_debug_info *debug;

      /* Adjust the FDR index for the symbol by that used for the
	 input BFD.  */
      debug = &ecoff_data (h->abfd)->debug_info;
      BFD_ASSERT (h->esym.ifd >= 0
		  && h->esym.ifd < debug->symbolic_header.ifdMax);
      h->esym.ifd = debug->ifdmap[h->esym.ifd];
    }

  switch (h->root.type)
    {
    default:
    case bfd_link_hash_warning:
    case bfd_link_hash_new:
      abort ();
    case bfd_link_hash_undefined:
    case bfd_link_hash_undefweak:
      if (h->esym.asym.sc != scUndefined
	  && h->esym.asym.sc != scSUndefined)
	h->esym.asym.sc = scUndefined;
      break;
    case bfd_link_hash_defined:
    case bfd_link_hash_defweak:
      if (h->esym.asym.sc == scUndefined
	  || h->esym.asym.sc == scSUndefined)
	h->esym.asym.sc = scAbs;
      else if (h->esym.asym.sc == scCommon)
	h->esym.asym.sc = scBss;
      else if (h->esym.asym.sc == scSCommon)
	h->esym.asym.sc = scSBss;
      h->esym.asym.value = (h->root.u.def.value
			    + h->root.u.def.section->output_section->vma
			    + h->root.u.def.section->output_offset);
      break;
    case bfd_link_hash_common:
      if (h->esym.asym.sc != scCommon
	  && h->esym.asym.sc != scSCommon)
	h->esym.asym.sc = scCommon;
      h->esym.asym.value = h->root.u.c.size;
      break;
    case bfd_link_hash_indirect:
      /* We ignore these symbols, since the indirected symbol is
	 already in the hash table.  */
      return TRUE;
    }

  /* bfd_ecoff_debug_one_external uses iextMax to keep track of the
     symbol number.  */
  h->indx = ecoff_data (output_bfd)->debug_info.symbolic_header.iextMax;
  h->written = 1;

  return (bfd_ecoff_debug_one_external
	  (output_bfd, &ecoff_data (output_bfd)->debug_info,
	   &ecoff_backend (output_bfd)->debug_swap, h->root.root.string,
	   &h->esym));
}

/* ECOFF final link routine.  This looks through all the input BFDs
   and gathers together all the debugging information, and then
   processes all the link order information.  This may cause it to
   close and reopen some input BFDs; I'll see how bad this is.  */

bfd_boolean
_bfd_ecoff_bfd_final_link (bfd *abfd, struct bfd_link_info *info)
{
  const struct ecoff_backend_data * const backend = ecoff_backend (abfd);
  struct ecoff_debug_info * const debug = &ecoff_data (abfd)->debug_info;
  HDRR *symhdr;
  void * handle;
  bfd *input_bfd;
  asection *o;
  struct bfd_link_order *p;
  struct extsym_info einfo;

  /* We accumulate the debugging information counts in the symbolic
     header.  */
  symhdr = &debug->symbolic_header;
  symhdr->vstamp = 0;
  symhdr->ilineMax = 0;
  symhdr->cbLine = 0;
  symhdr->idnMax = 0;
  symhdr->ipdMax = 0;
  symhdr->isymMax = 0;
  symhdr->ioptMax = 0;
  symhdr->iauxMax = 0;
  symhdr->issMax = 0;
  symhdr->issExtMax = 0;
  symhdr->ifdMax = 0;
  symhdr->crfd = 0;
  symhdr->iextMax = 0;

  /* We accumulate the debugging information itself in the debug_info
     structure.  */
  debug->line = NULL;
  debug->external_dnr = NULL;
  debug->external_pdr = NULL;
  debug->external_sym = NULL;
  debug->external_opt = NULL;
  debug->external_aux = NULL;
  debug->ss = NULL;
  debug->ssext = debug->ssext_end = NULL;
  debug->external_fdr = NULL;
  debug->external_rfd = NULL;
  debug->external_ext = debug->external_ext_end = NULL;

  handle = bfd_ecoff_debug_init (abfd, debug, &backend->debug_swap, info);
  if (handle == NULL)
    return FALSE;

  /* Accumulate the debugging symbols from each input BFD.  */
  for (input_bfd = info->input_bfds;
       input_bfd != NULL;
       input_bfd = input_bfd->link_next)
    {
      bfd_boolean ret;

      if (bfd_get_flavour (input_bfd) == bfd_target_ecoff_flavour)
	{
	  /* Arbitrarily set the symbolic header vstamp to the vstamp
	     of the first object file in the link.  */
	  if (symhdr->vstamp == 0)
	    symhdr->vstamp
	      = ecoff_data (input_bfd)->debug_info.symbolic_header.vstamp;
	  ret = ecoff_final_link_debug_accumulate (abfd, input_bfd, info,
						   handle);
	}
      else
	ret = bfd_ecoff_debug_accumulate_other (handle, abfd,
						debug, &backend->debug_swap,
						input_bfd, info);
      if (! ret)
	return FALSE;

      /* Combine the register masks.  */
      ecoff_data (abfd)->gprmask |= ecoff_data (input_bfd)->gprmask;
      ecoff_data (abfd)->fprmask |= ecoff_data (input_bfd)->fprmask;
      ecoff_data (abfd)->cprmask[0] |= ecoff_data (input_bfd)->cprmask[0];
      ecoff_data (abfd)->cprmask[1] |= ecoff_data (input_bfd)->cprmask[1];
      ecoff_data (abfd)->cprmask[2] |= ecoff_data (input_bfd)->cprmask[2];
      ecoff_data (abfd)->cprmask[3] |= ecoff_data (input_bfd)->cprmask[3];
    }

  /* Write out the external symbols.  */
  einfo.abfd = abfd;
  einfo.info = info;
  ecoff_link_hash_traverse (ecoff_hash_table (info),
			    ecoff_link_write_external,
			    (void *) &einfo);

  if (info->relocatable)
    {
      /* We need to make a pass over the link_orders to count up the
	 number of relocations we will need to output, so that we know
	 how much space they will take up.  */
      for (o = abfd->sections; o != NULL; o = o->next)
	{
	  o->reloc_count = 0;
	  for (p = o->map_head.link_order;
	       p != NULL;
	       p = p->next)
	    if (p->type == bfd_indirect_link_order)
	      o->reloc_count += p->u.indirect.section->reloc_count;
	    else if (p->type == bfd_section_reloc_link_order
		     || p->type == bfd_symbol_reloc_link_order)
	      ++o->reloc_count;
	}
    }

  /* Compute the reloc and symbol file positions.  */
  ecoff_compute_reloc_file_positions (abfd);

  /* Write out the debugging information.  */
  if (! bfd_ecoff_write_accumulated_debug (handle, abfd, debug,
					   &backend->debug_swap, info,
					   ecoff_data (abfd)->sym_filepos))
    return FALSE;

  bfd_ecoff_debug_free (handle, abfd, debug, &backend->debug_swap, info);

  if (info->relocatable)
    {
      /* Now reset the reloc_count field of the sections in the output
	 BFD to 0, so that we can use them to keep track of how many
	 relocs we have output thus far.  */
      for (o = abfd->sections; o != NULL; o = o->next)
	o->reloc_count = 0;
    }

  /* Get a value for the GP register.  */
  if (ecoff_data (abfd)->gp == 0)
    {
      struct bfd_link_hash_entry *h;

      h = bfd_link_hash_lookup (info->hash, "_gp", FALSE, FALSE, TRUE);
      if (h != NULL
	  && h->type == bfd_link_hash_defined)
	ecoff_data (abfd)->gp = (h->u.def.value
				 + h->u.def.section->output_section->vma
				 + h->u.def.section->output_offset);
      else if (info->relocatable)
	{
	  bfd_vma lo;

	  /* Make up a value.  */
	  lo = (bfd_vma) -1;
	  for (o = abfd->sections; o != NULL; o = o->next)
	    {
	      if (o->vma < lo
		  && (streq (o->name, _SBSS)
		      || streq (o->name, _SDATA)
		      || streq (o->name, _LIT4)
		      || streq (o->name, _LIT8)
		      || streq (o->name, _LITA)))
		lo = o->vma;
	    }
	  ecoff_data (abfd)->gp = lo + 0x8000;
	}
      else
	{
	  /* If the relocate_section function needs to do a reloc
	     involving the GP value, it should make a reloc_dangerous
	     callback to warn that GP is not defined.  */
	}
    }

  for (o = abfd->sections; o != NULL; o = o->next)
    {
      for (p = o->map_head.link_order;
	   p != NULL;
	   p = p->next)
	{
	  if (p->type == bfd_indirect_link_order
	      && (bfd_get_flavour (p->u.indirect.section->owner)
		  == bfd_target_ecoff_flavour))
	    {
	      if (! ecoff_indirect_link_order (abfd, info, o, p))
		return FALSE;
	    }
	  else if (p->type == bfd_section_reloc_link_order
		   || p->type == bfd_symbol_reloc_link_order)
	    {
	      if (! ecoff_reloc_link_order (abfd, info, o, p))
		return FALSE;
	    }
	  else
	    {
	      if (! _bfd_default_link_order (abfd, info, o, p))
		return FALSE;
	    }
	}
    }

  bfd_get_symcount (abfd) = symhdr->iextMax + symhdr->isymMax;

  ecoff_data (abfd)->linker = TRUE;

  return TRUE;
}
