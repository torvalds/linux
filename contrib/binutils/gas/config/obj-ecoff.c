/* ECOFF object file format.
   Copyright 1993, 1994, 1995, 1996, 1998, 1999, 2000, 2001, 2002,
   2005  Free Software Foundation, Inc.
   Contributed by Cygnus Support.
   This file was put together by Ian Lance Taylor <ian@cygnus.com>.

   This file is part of GAS.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#define OBJ_HEADER "obj-ecoff.h"
#include "as.h"
#include "coff/internal.h"
#include "bfd/libcoff.h"
#include "bfd/libecoff.h"

/* Almost all of the ECOFF support is actually in ecoff.c in the main
   gas directory.  This file mostly just arranges to call that one at
   the right times.  */

/* Set section VMAs and GP values before reloc processing.  */

void
ecoff_frob_file_before_fix (void)
{
  bfd_vma addr;
  asection *sec;

  /* Set the section VMA values.  We force the .sdata and .sbss
     sections to the end to ensure that their VMA addresses are close
     together so that the GP register can address both of them.  We
     put the .bss section after the .sbss section.

     Also, for the Alpha, we must sort the sections, to make sure they
     appear in the output file in the correct order.  (Actually, maybe
     this is a job for BFD.  But the VMAs computed would be out of
     whack if we computed them given our initial, random ordering.
     It's possible that that wouldn't break things; I could do some
     experimenting sometime and find out.

     This output ordering of sections is magic, on the Alpha, at
     least.  The .lita section must come before .lit8 and .lit4,
     otherwise the OSF/1 linker may silently trash the .lit{4,8}
     section contents.  Also, .text must preceed .rdata.  These differ
     from the order described in some parts of the DEC OSF/1 Assembly
     Language Programmer's Guide, but that order doesn't seem to work
     with their linker.

     I don't know if section ordering on the MIPS is important.  */

  static const char *const names[] =
  {
    /* text segment */
    ".text", ".rdata", ".init", ".fini",
    /* data segment */
    ".data", ".lita", ".lit8", ".lit4", ".sdata", ".got",
    /* bss segment */
    ".sbss", ".bss",
  };
#define n_names ((int) (sizeof (names) / sizeof (names[0])))

  /* Sections that match names, order to be straightened out later.  */
  asection *secs[n_names];
  int i;

  addr = 0;
  for (i = 0; i < n_names; i++)
    secs[i] = NULL;

  for (sec = stdoutput->sections; sec != NULL; sec = sec->next)
    {
      for (i = 0; i < n_names; i++)
	if (!strcmp (sec->name, names[i]))
	  {
	    secs[i] = sec;
	    bfd_section_list_remove (stdoutput, sec);
	    break;
	  }
      if (i == n_names)
	{
	  bfd_set_section_vma (stdoutput, sec, addr);
	  addr += bfd_section_size (stdoutput, sec);
	}
    }
  for (i = 0; i < n_names; i++)
    if (secs[i])
      {
	bfd_set_section_vma (stdoutput, secs[i], addr);
	addr += bfd_section_size (stdoutput, secs[i]);
      }
  for (i = n_names - 1; i >= 0; i--)
    if (secs[i])
      bfd_section_list_prepend (stdoutput, secs[i]);

  /* Fill in the register masks.  */
  {
    unsigned long gprmask = 0;
    unsigned long fprmask = 0;
    unsigned long *cprmask = NULL;

#ifdef TC_MIPS
    /* Fill in the MIPS register masks.  It's probably not worth
       setting up a generic interface for this.  */
    gprmask = mips_gprmask;
    cprmask = mips_cprmask;
#endif

#ifdef TC_ALPHA
    alpha_frob_ecoff_data ();

    if (! bfd_ecoff_set_gp_value (stdoutput, alpha_gp_value))
      as_fatal (_("Can't set GP value"));

    gprmask = alpha_gprmask;
    fprmask = alpha_fprmask;
#endif

    if (! bfd_ecoff_set_regmasks (stdoutput, gprmask, fprmask, cprmask))
      as_fatal (_("Can't set register masks"));
  }
}

/* Swap out the symbols and debugging information for BFD.  */

void
ecoff_frob_file (void)
{
  const struct ecoff_debug_swap * const debug_swap
    = &ecoff_backend (stdoutput)->debug_swap;
  bfd_vma addr ATTRIBUTE_UNUSED;
  HDRR *hdr;
  char *buf;
  char *set;

  /* Build the ECOFF debugging information.  */
  assert (ecoff_data (stdoutput) != 0);
  hdr = &ecoff_data (stdoutput)->debug_info.symbolic_header;
  ecoff_build_debug (hdr, &buf, debug_swap);

  /* Finish up the ecoff_tdata structure.  */
  set = buf;
#define SET(ptr, count, type, size) \
  if (hdr->count == 0) \
    ecoff_data (stdoutput)->debug_info.ptr = NULL; \
  else \
    { \
      ecoff_data (stdoutput)->debug_info.ptr = (type) set; \
      set += hdr->count * size; \
    }

  SET (line, cbLine, unsigned char *, sizeof (unsigned char));
  SET (external_dnr, idnMax, void *, debug_swap->external_dnr_size);
  SET (external_pdr, ipdMax, void *, debug_swap->external_pdr_size);
  SET (external_sym, isymMax, void *, debug_swap->external_sym_size);
  SET (external_opt, ioptMax, void *, debug_swap->external_opt_size);
  SET (external_aux, iauxMax, union aux_ext *, sizeof (union aux_ext));
  SET (ss, issMax, char *, sizeof (char));
  SET (ssext, issExtMax, char *, sizeof (char));
  SET (external_rfd, crfd, void *, debug_swap->external_rfd_size);
  SET (external_fdr, ifdMax, void *, debug_swap->external_fdr_size);
  SET (external_ext, iextMax, void *, debug_swap->external_ext_size);
#undef SET
}

/* This is called by the ECOFF code to set the external information
   for a symbol.  We just pass it on to BFD, which expects the swapped
   information to be stored in the native field of the symbol.  */

void
obj_ecoff_set_ext (symbolS *sym, EXTR *ext)
{
  const struct ecoff_debug_swap * const debug_swap
    = &ecoff_backend (stdoutput)->debug_swap;
  ecoff_symbol_type *esym;

  know (bfd_asymbol_flavour (symbol_get_bfdsym (sym))
	== bfd_target_ecoff_flavour);
  esym = ecoffsymbol (symbol_get_bfdsym (sym));
  esym->local = FALSE;
  esym->native = xmalloc (debug_swap->external_ext_size);
  (*debug_swap->swap_ext_out) (stdoutput, ext, esym->native);
}

static int
ecoff_sec_sym_ok_for_reloc (asection *sec ATTRIBUTE_UNUSED)
{
  return 1;
}

static void
obj_ecoff_frob_symbol (symbolS *sym, int *puntp ATTRIBUTE_UNUSED)
{
  ecoff_frob_symbol (sym);
}

static void
ecoff_pop_insert (void)
{
  pop_insert (obj_pseudo_table);
}

static int
ecoff_separate_stab_sections (void)
{
  return 0;
}

/* These are the pseudo-ops we support in this file.  Only those
   relating to debugging information are supported here.

   The following pseudo-ops from the Kane and Heinrich MIPS book
   should be defined here, but are currently unsupported: .aent,
   .bgnb, .endb, .verstamp, .vreg.

   The following pseudo-ops from the Kane and Heinrich MIPS book are
   MIPS CPU specific, and should be defined by tc-mips.c: .alias,
   .extern, .galive, .gjaldef, .gjrlive, .livereg, .noalias, .option,
   .rdata, .sdata, .set.

   The following pseudo-ops from the Kane and Heinrich MIPS book are
   not MIPS CPU specific, but are also not ECOFF specific.  I have
   only listed the ones which are not already in read.c.  It's not
   completely clear where these should be defined, but tc-mips.c is
   probably the most reasonable place: .asciiz, .asm0, .endr, .err,
   .half, .lab, .repeat, .struct, .weakext.  */

const pseudo_typeS obj_pseudo_table[] =
{
  /* COFF style debugging information. .ln is not used; .loc is used
     instead.  */
  { "def",	ecoff_directive_def,	0 },
  { "dim",	ecoff_directive_dim,	0 },
  { "endef",	ecoff_directive_endef,	0 },
  { "file",	ecoff_directive_file,	0 },
  { "scl",	ecoff_directive_scl,	0 },
  { "size",	ecoff_directive_size,	0 },
  { "esize",	ecoff_directive_size,	0 },
  { "tag",	ecoff_directive_tag,	0 },
  { "type",	ecoff_directive_type,	0 },
  { "etype",	ecoff_directive_type,	0 },
  { "val",	ecoff_directive_val,	0 },

  /* ECOFF specific debugging information.  */
  { "begin",	ecoff_directive_begin,	0 },
  { "bend",	ecoff_directive_bend,	0 },
  { "end",	ecoff_directive_end,	0 },
  { "ent",	ecoff_directive_ent,	0 },
  { "fmask",	ecoff_directive_fmask,	0 },
  { "frame",	ecoff_directive_frame,	0 },
  { "loc",	ecoff_directive_loc,	0 },
  { "mask",	ecoff_directive_mask,	0 },

  /* Other ECOFF directives.  */
  { "extern",	ecoff_directive_extern,	0 },

#ifndef TC_MIPS
  /* For TC_MIPS, tc-mips.c adds this.  */
  { "weakext",	ecoff_directive_weakext, 0 },
#endif

  /* These are used on Irix.  I don't know how to implement them.  */
  { "bgnb",	s_ignore,		0 },
  { "endb",	s_ignore,		0 },
  { "verstamp",	s_ignore,		0 },

  /* Sentinel.  */
  { NULL,	s_ignore,		0 }
};

const struct format_ops ecoff_format_ops =
{
  bfd_target_ecoff_flavour,
  0,	/* dfl_leading_underscore.  */

  /* FIXME: A comment why emit_section_symbols is different here (1) from
     the single-format definition (0) would be in order.  */
  1,	/* emit_section_symbols.  */
  0,	/* begin.  */
  ecoff_new_file,
  obj_ecoff_frob_symbol,
  ecoff_frob_file,
  0,	/* frob_file_before_adjust.  */
  ecoff_frob_file_before_fix,
  0,	/* frob_file_after_relocs.  */
  0,	/* s_get_size.  */
  0,	/* s_set_size.  */
  0,	/* s_get_align.  */
  0,	/* s_set_align.  */
  0,	/* s_get_other.  */
  0,	/* s_set_other.  */
  0,	/* s_get_desc.  */
  0,	/* s_set_desc.  */
  0,	/* s_get_type.  */
  0,	/* s_set_type.  */
  0,	/* copy_symbol_attributes.  */
  ecoff_generate_asm_lineno,
  ecoff_stab,
  ecoff_separate_stab_sections,
  0,	/* init_stab_section.  */
  ecoff_sec_sym_ok_for_reloc,
  ecoff_pop_insert,
  ecoff_set_ext,
  ecoff_read_begin_hook,
  ecoff_symbol_new_hook
};
