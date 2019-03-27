/* MeP-specific support for 32-bit ELF.
   Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "elf-bfd.h"
#include "elf/mep.h"
#include "libiberty.h"

/* Forward declarations.  */

/* Private relocation functions.  */

#define MEPREL(type, size, bits, right, left, pcrel, overflow, mask) \
  {(unsigned)type, right, size, bits, pcrel, left, overflow, mep_reloc, #type, FALSE, 0, mask, 0 }

#define N complain_overflow_dont
#define S complain_overflow_signed
#define U complain_overflow_unsigned

static bfd_reloc_status_type mep_reloc (bfd *, arelent *, struct bfd_symbol *,
					void *, asection *, bfd *, char **);

static reloc_howto_type mep_elf_howto_table [] =
{
  /* type, size, bits, leftshift, rightshift, pcrel, OD/OS/OU, mask.  */
  MEPREL (R_MEP_NONE,     0,  0, 0, 0, 0, N, 0),
  MEPREL (R_RELC,         0,  0, 0, 0, 0, N, 0),
  /* MEPRELOC:HOWTO */
    /* This section generated from bfd/mep-relocs.pl from include/elf/mep.h.  */
  MEPREL (R_MEP_8,        0,  8, 0, 0, 0, U, 0xff),
  MEPREL (R_MEP_16,       1, 16, 0, 0, 0, U, 0xffff),
  MEPREL (R_MEP_32,       2, 32, 0, 0, 0, U, 0xffffffff),
  MEPREL (R_MEP_PCREL8A2, 1,  8, 1, 1, 1, S, 0x00fe),
  MEPREL (R_MEP_PCREL12A2,1, 12, 1, 1, 1, S, 0x0ffe),
  MEPREL (R_MEP_PCREL17A2,2, 17, 0, 1, 1, S, 0x0000ffff),
  MEPREL (R_MEP_PCREL24A2,2, 24, 0, 1, 1, S, 0x07f0ffff),
  MEPREL (R_MEP_PCABS24A2,2, 24, 0, 1, 0, U, 0x07f0ffff),
  MEPREL (R_MEP_LOW16,    2, 16, 0, 0, 0, N, 0x0000ffff),
  MEPREL (R_MEP_HI16U,    2, 32, 0,16, 0, N, 0x0000ffff),
  MEPREL (R_MEP_HI16S,    2, 32, 0,16, 0, N, 0x0000ffff),
  MEPREL (R_MEP_GPREL,    2, 16, 0, 0, 0, S, 0x0000ffff),
  MEPREL (R_MEP_TPREL,    2, 16, 0, 0, 0, S, 0x0000ffff),
  MEPREL (R_MEP_TPREL7,   1,  7, 0, 0, 0, U, 0x007f),
  MEPREL (R_MEP_TPREL7A2, 1,  7, 1, 1, 0, U, 0x007e),
  MEPREL (R_MEP_TPREL7A4, 1,  7, 2, 2, 0, U, 0x007c),
  MEPREL (R_MEP_UIMM24,   2, 24, 0, 0, 0, U, 0x00ffffff),
  MEPREL (R_MEP_ADDR24A4, 2, 24, 0, 2, 0, U, 0x00fcffff),
  MEPREL (R_MEP_GNU_VTINHERIT,1,  0,16,32, 0, N, 0x0000),
  MEPREL (R_MEP_GNU_VTENTRY,1,  0,16,32, 0, N, 0x0000),
  /* MEPRELOC:END */
};

#define VALID_MEP_RELOC(N) ((N) >= 0 \
  && (N) < ARRAY_SIZE (mep_elf_howto_table)

#undef N
#undef S
#undef U

static bfd_reloc_status_type
mep_reloc
    (bfd *               abfd ATTRIBUTE_UNUSED,
     arelent *           reloc_entry ATTRIBUTE_UNUSED,
     struct bfd_symbol * symbol ATTRIBUTE_UNUSED,
     void *              data ATTRIBUTE_UNUSED,
     asection *          input_section ATTRIBUTE_UNUSED,
     bfd *               output_bfd ATTRIBUTE_UNUSED,
     char **             error_message ATTRIBUTE_UNUSED)
{
  return bfd_reloc_ok;
}



#define BFD_RELOC_MEP_NONE BFD_RELOC_NONE
#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#define MAP(n) case BFD_RELOC_MEP_##n: type = R_MEP_##n; break
#else
#define MAP(n) case BFD_RELOC_MEP_/**/n: type = R_MEP_/**/n; break
#endif

static reloc_howto_type *
mep_reloc_type_lookup
    (bfd * abfd ATTRIBUTE_UNUSED,
     bfd_reloc_code_real_type code)
{
  unsigned int type = 0;

  switch (code)
    {
    MAP(NONE);
    case BFD_RELOC_8:
      type = R_MEP_8;
      break;
    case BFD_RELOC_16:
      type = R_MEP_16;
      break;
    case BFD_RELOC_32:
      type = R_MEP_32;
      break;
    case BFD_RELOC_VTABLE_ENTRY:
      type = R_MEP_GNU_VTENTRY;
      break;
    case BFD_RELOC_VTABLE_INHERIT:
      type = R_MEP_GNU_VTINHERIT;
      break;
    case BFD_RELOC_RELC:
      type = R_RELC;
      break;

    /* MEPRELOC:MAP */
    /* This section generated from bfd/mep-relocs.pl from include/elf/mep.h.  */
    MAP(8);
    MAP(16);
    MAP(32);
    MAP(PCREL8A2);
    MAP(PCREL12A2);
    MAP(PCREL17A2);
    MAP(PCREL24A2);
    MAP(PCABS24A2);
    MAP(LOW16);
    MAP(HI16U);
    MAP(HI16S);
    MAP(GPREL);
    MAP(TPREL);
    MAP(TPREL7);
    MAP(TPREL7A2);
    MAP(TPREL7A4);
    MAP(UIMM24);
    MAP(ADDR24A4);
    MAP(GNU_VTINHERIT);
    MAP(GNU_VTENTRY);
    /* MEPRELOC:END */

    default:
      /* Pacify gcc -Wall.  */
      fprintf (stderr, "mep: no reloc for code %d\n", code);
      return NULL;
    }

  if (mep_elf_howto_table[type].type != type)
    {
      fprintf (stderr, "MeP: howto %d has type %d\n", type, mep_elf_howto_table[type].type);
      abort ();
    }

  return mep_elf_howto_table + type;
}

#undef MAP

static reloc_howto_type *
mep_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED, const char *r_name)
{
  unsigned int i;

  for (i = 0;
       i < sizeof (mep_elf_howto_table) / sizeof (mep_elf_howto_table[0]);
       i++)
    if (mep_elf_howto_table[i].name != NULL
	&& strcasecmp (mep_elf_howto_table[i].name, r_name) == 0)
      return &mep_elf_howto_table[i];

  return NULL;
}

/* Perform a single relocation.  */

static struct bfd_link_info *mep_info;
static int warn_tp = 0, warn_sda = 0;

static bfd_vma
mep_lookup_global
    (char *    name,
     bfd_vma   ofs,
     bfd_vma * cache,
     int *     warn)
{
  struct bfd_link_hash_entry *h;

  if (*cache || *warn)
    return *cache;

  h = bfd_link_hash_lookup (mep_info->hash, name, FALSE, FALSE, TRUE);
  if (h == 0 || h->type != bfd_link_hash_defined)
    {
      *warn = ofs + 1;
      return 0;
    }
  *cache = (h->u.def.value
	  + h->u.def.section->output_section->vma
	  + h->u.def.section->output_offset);
  return *cache;
}

static bfd_vma
mep_tpoff_base (bfd_vma ofs)
{
  static bfd_vma cache = 0;
  return mep_lookup_global ("__tpbase", ofs, &cache, &warn_tp);
}

static bfd_vma
mep_sdaoff_base (bfd_vma ofs)
{
  static bfd_vma cache = 0;
  return mep_lookup_global ("__sdabase", ofs, &cache, &warn_sda);
}

static bfd_reloc_status_type
mep_final_link_relocate
    (reloc_howto_type *  howto,
     bfd *               input_bfd,
     asection *          input_section,
     bfd_byte *          contents,
     Elf_Internal_Rela * rel,
     bfd_vma             relocation)
{
  unsigned long u;
  long s;
  unsigned char *byte;
  bfd_vma pc;
  bfd_reloc_status_type r = bfd_reloc_ok;
  int e2, e4;

  if (bfd_big_endian (input_bfd))
    {
      e2 = 0;
      e4 = 0;
    }
  else
    {
      e2 = 1;
      e4 = 3;
    }

  pc = (input_section->output_section->vma
	+ input_section->output_offset
	+ rel->r_offset);

  s = relocation + rel->r_addend;

  byte = (unsigned char *)contents + rel->r_offset;

  if (howto->type == R_MEP_PCREL24A2
      && s == 0
      && pc >= 0x800000)
    {
      /* This is an unreachable branch to an undefined weak function.
	 Silently ignore it, since the opcode can't do that but should
	 never be executed anyway.  */
      return bfd_reloc_ok;
    }

  if (howto->pc_relative)
    s -= pc;

  u = (unsigned long) s;

  switch (howto->type)
    {
    /* MEPRELOC:APPLY */
    /* This section generated from bfd/mep-relocs.pl from include/elf/mep.h.  */
    case R_MEP_8: /* 76543210 */
      if (u > 255) r = bfd_reloc_overflow;
      byte[0] = (u & 0xff);
      break;
    case R_MEP_16: /* fedcba9876543210 */
      if (u > 65535) r = bfd_reloc_overflow;
      byte[0^e2] = ((u >> 8) & 0xff);
      byte[1^e2] = (u & 0xff);
      break;
    case R_MEP_32: /* vutsrqponmlkjihgfedcba9876543210 */
      byte[0^e4] = ((u >> 24) & 0xff);
      byte[1^e4] = ((u >> 16) & 0xff);
      byte[2^e4] = ((u >> 8) & 0xff);
      byte[3^e4] = (u & 0xff);
      break;
    case R_MEP_PCREL8A2: /* --------7654321- */
      if (-128 > s || s > 127) r = bfd_reloc_overflow;
      byte[1^e2] = (byte[1^e2] & 0x01) | (s & 0xfe);
      break;
    case R_MEP_PCREL12A2: /* ----ba987654321- */
      if (-2048 > s || s > 2047) r = bfd_reloc_overflow;
      byte[0^e2] = (byte[0^e2] & 0xf0) | ((s >> 8) & 0x0f);
      byte[1^e2] = (byte[1^e2] & 0x01) | (s & 0xfe);
      break;
    case R_MEP_PCREL17A2: /* ----------------gfedcba987654321 */
      if (-65536 > s || s > 65535) r = bfd_reloc_overflow;
      byte[2^e2] = ((s >> 9) & 0xff);
      byte[3^e2] = ((s >> 1) & 0xff);
      break;
    case R_MEP_PCREL24A2: /* -----7654321----nmlkjihgfedcba98 */
      if (-8388608 > s || s > 8388607) r = bfd_reloc_overflow;
      byte[0^e2] = (byte[0^e2] & 0xf8) | ((s >> 5) & 0x07);
      byte[1^e2] = (byte[1^e2] & 0x0f) | ((s << 3) & 0xf0);
      byte[2^e2] = ((s >> 16) & 0xff);
      byte[3^e2] = ((s >> 8) & 0xff);
      break;
    case R_MEP_PCABS24A2: /* -----7654321----nmlkjihgfedcba98 */
      if (u > 16777215) r = bfd_reloc_overflow;
      byte[0^e2] = (byte[0^e2] & 0xf8) | ((u >> 5) & 0x07);
      byte[1^e2] = (byte[1^e2] & 0x0f) | ((u << 3) & 0xf0);
      byte[2^e2] = ((u >> 16) & 0xff);
      byte[3^e2] = ((u >> 8) & 0xff);
      break;
    case R_MEP_LOW16: /* ----------------fedcba9876543210 */
      byte[2^e2] = ((u >> 8) & 0xff);
      byte[3^e2] = (u & 0xff);
      break;
    case R_MEP_HI16U: /* ----------------vutsrqponmlkjihg */
      byte[2^e2] = ((u >> 24) & 0xff);
      byte[3^e2] = ((u >> 16) & 0xff);
      break;
    case R_MEP_HI16S: /* ----------------vutsrqponmlkjihg */
      byte[2^e2] = ((s >> 24) & 0xff);
      byte[3^e2] = ((s >> 16) & 0xff);
      break;
    case R_MEP_GPREL: /* ----------------fedcba9876543210 */
      s -= mep_sdaoff_base(rel->r_offset);
      if (-32768 > s || s > 32767) r = bfd_reloc_overflow;
      byte[2^e2] = ((s >> 8) & 0xff);
      byte[3^e2] = (s & 0xff);
      break;
    case R_MEP_TPREL: /* ----------------fedcba9876543210 */
      s -= mep_tpoff_base(rel->r_offset);
      if (-32768 > s || s > 32767) r = bfd_reloc_overflow;
      byte[2^e2] = ((s >> 8) & 0xff);
      byte[3^e2] = (s & 0xff);
      break;
    case R_MEP_TPREL7: /* ---------6543210 */
      u -= mep_tpoff_base(rel->r_offset);
      if (u > 127) r = bfd_reloc_overflow;
      byte[1^e2] = (byte[1^e2] & 0x80) | (u & 0x7f);
      break;
    case R_MEP_TPREL7A2: /* ---------654321- */
      u -= mep_tpoff_base(rel->r_offset);
      if (u > 127) r = bfd_reloc_overflow;
      byte[1^e2] = (byte[1^e2] & 0x81) | (u & 0x7e);
      break;
    case R_MEP_TPREL7A4: /* ---------65432-- */
      u -= mep_tpoff_base(rel->r_offset);
      if (u > 127) r = bfd_reloc_overflow;
      byte[1^e2] = (byte[1^e2] & 0x83) | (u & 0x7c);
      break;
    case R_MEP_UIMM24: /* --------76543210nmlkjihgfedcba98 */
      if (u > 16777215) r = bfd_reloc_overflow;
      byte[1^e2] = (u & 0xff);
      byte[2^e2] = ((u >> 16) & 0xff);
      byte[3^e2] = ((u >> 8) & 0xff);
      break;
    case R_MEP_ADDR24A4: /* --------765432--nmlkjihgfedcba98 */
      if (u > 16777215) r = bfd_reloc_overflow;
      byte[1^e2] = (byte[1^e2] & 0x03) | (u & 0xfc);
      byte[2^e2] = ((u >> 16) & 0xff);
      byte[3^e2] = ((u >> 8) & 0xff);
      break;
    case R_MEP_GNU_VTINHERIT: /* ---------------- */
      break;
    case R_MEP_GNU_VTENTRY: /* ---------------- */
      break;
    /* MEPRELOC:END */
    default:
      abort ();
    }

  return r;
}

/* Set the howto pointer for a MEP ELF reloc.  */

static void
mep_info_to_howto_rela
    (bfd *               abfd ATTRIBUTE_UNUSED,
     arelent *           cache_ptr,
     Elf_Internal_Rela * dst)
{
  unsigned int r_type;

  r_type = ELF32_R_TYPE (dst->r_info);
  cache_ptr->howto = & mep_elf_howto_table [r_type];
}

/* Look through the relocs for a section during the first phase.
   Since we don't do .gots or .plts, we just need to consider the
   virtual table relocs for gc.  */

static bfd_boolean
mep_elf_check_relocs
    (bfd *                     abfd,
     struct bfd_link_info *    info,
     asection *                sec,
     const Elf_Internal_Rela * relocs)
{
  Elf_Internal_Shdr *           symtab_hdr;
  struct elf_link_hash_entry ** sym_hashes;
  struct elf_link_hash_entry ** sym_hashes_end;
  const Elf_Internal_Rela *     rel;
  const Elf_Internal_Rela *     rel_end;

  if (info->relocatable)
    return TRUE;

  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (abfd);
  sym_hashes_end = sym_hashes + symtab_hdr->sh_size / sizeof (Elf32_External_Sym);
  if (!elf_bad_symtab (abfd))
    sym_hashes_end -= symtab_hdr->sh_info;

  rel_end = relocs + sec->reloc_count;
  for (rel = relocs; rel < rel_end; rel++)
    {
      struct elf_link_hash_entry *h;
      unsigned long r_symndx;

      r_symndx = ELF32_R_SYM (rel->r_info);
      if (r_symndx < symtab_hdr->sh_info)
        h = NULL;
      else
        h = sym_hashes[r_symndx - symtab_hdr->sh_info];
    }
  return TRUE;
}


/* Relocate a MEP ELF section.
   There is some attempt to make this function usable for many architectures,
   both USE_REL and USE_RELA ['twould be nice if such a critter existed],
   if only to serve as a learning tool.

   The RELOCATE_SECTION function is called by the new ELF backend linker
   to handle the relocations for a section.

   The relocs are always passed as Rela structures; if the section
   actually uses Rel structures, the r_addend field will always be
   zero.

   This function is responsible for adjusting the section contents as
   necessary, and (if using Rela relocs and generating a relocatable
   output file) adjusting the reloc addend as necessary.

   This function does not have to worry about setting the reloc
   address or the reloc symbol index.

   LOCAL_SYMS is a pointer to the swapped in local symbols.

   LOCAL_SECTIONS is an array giving the section in the input file
   corresponding to the st_shndx field of each local symbol.

   The global hash table entry for the global symbols can be found
   via elf_sym_hashes (input_bfd).

   When generating relocatable output, this function must handle
   STB_LOCAL/STT_SECTION symbols specially.  The output symbol is
   going to be the section symbol corresponding to the output
   section, which means that the addend must be adjusted
   accordingly.  */

static bfd_boolean
mep_elf_relocate_section
    (bfd *                   output_bfd ATTRIBUTE_UNUSED,
     struct bfd_link_info *  info,
     bfd *                   input_bfd,
     asection *              input_section,
     bfd_byte *              contents,
     Elf_Internal_Rela *     relocs,
     Elf_Internal_Sym *      local_syms,
     asection **             local_sections)
{
  Elf_Internal_Shdr *           symtab_hdr;
  struct elf_link_hash_entry ** sym_hashes;
  Elf_Internal_Rela *           rel;
  Elf_Internal_Rela *           relend;

  symtab_hdr = & elf_tdata (input_bfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (input_bfd);
  relend     = relocs + input_section->reloc_count;

  mep_info = info;

  for (rel = relocs; rel < relend; rel ++)
    {
      reloc_howto_type *           howto;
      unsigned long                r_symndx;
      Elf_Internal_Sym *           sym;
      asection *                   sec;
      struct elf_link_hash_entry * h;
      bfd_vma                      relocation;
      bfd_reloc_status_type        r;
      const char *                 name = NULL;
      int                          r_type;

      r_type = ELF32_R_TYPE (rel->r_info);

      r_symndx = ELF32_R_SYM (rel->r_info);

      /* Is this a complex relocation?  */
      if (!info->relocatable && ELF32_R_TYPE (rel->r_info) == R_RELC)
	{
	  bfd_elf_perform_complex_relocation (output_bfd, info,
					      input_bfd, input_section, contents,
					      rel, local_syms, local_sections);
	  continue;
	}

      howto  = mep_elf_howto_table + ELF32_R_TYPE (rel->r_info);
      h      = NULL;
      sym    = NULL;
      sec    = NULL;

      if (r_symndx < symtab_hdr->sh_info)
	{
	  sym = local_syms + r_symndx;
	  sec = local_sections [r_symndx];
	  relocation = _bfd_elf_rela_local_sym (output_bfd, sym, &sec, rel);

	  name = bfd_elf_string_from_elf_section
	    (input_bfd, symtab_hdr->sh_link, sym->st_name);
	  name = (name == NULL) ? bfd_section_name (input_bfd, sec) : name;
#if 0
	  fprintf (stderr, "local: sec: %s, sym: %s (%d), value: %x + %x + %x addend %x\n",
		   sec->name, name, sym->st_name,
		   sec->output_section->vma, sec->output_offset,
		   sym->st_value, rel->r_addend);
#endif
	}
      else
	{
	  relocation = 0;
	  h = sym_hashes [r_symndx];

	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;

	  name = h->root.root.string;

	  if (h->root.type == bfd_link_hash_defined
	      || h->root.type == bfd_link_hash_defweak)
	    {
	      sec = h->root.u.def.section;
	      relocation = (h->root.u.def.value
			    + sec->output_section->vma
			    + sec->output_offset);
#if 0
	      fprintf (stderr,
		       "defined: sec: %s, name: %s, value: %x + %x + %x gives: %x\n",
		       sec->name, name, h->root.u.def.value,
		       sec->output_section->vma, sec->output_offset, relocation);
#endif
	    }
	  else if (h->root.type == bfd_link_hash_undefweak)
	    {
#if 0
	      fprintf (stderr, "undefined: sec: %s, name: %s\n",
		       sec->name, name);
#endif
	    }
	  else if (!info->relocatable)
	    {
	      if (! ((*info->callbacks->undefined_symbol)
		     (info, h->root.root.string, input_bfd,
		      input_section, rel->r_offset,
		      (!info->shared && info->unresolved_syms_in_objects == RM_GENERATE_ERROR))))
		return FALSE;
#if 0
	      fprintf (stderr, "unknown: name: %s\n", name);
#endif
	    }
	}

      if (sec != NULL && elf_discarded_section (sec))
	{
	  /* For relocs against symbols from removed linkonce sections,
	     or sections discarded by a linker script, we just want the
	     section contents zeroed.  Avoid any special processing.  */
	  _bfd_clear_contents (howto, input_bfd, contents + rel->r_offset);
	  rel->r_info = 0;
	  rel->r_addend = 0;
	  continue;
	}

      if (info->relocatable)
	{
	  /* This is a relocatable link.  We don't have to change
             anything, unless the reloc is against a section symbol,
             in which case we have to adjust according to where the
             section symbol winds up in the output section.  */
	  if (sym != NULL && ELF_ST_TYPE (sym->st_info) == STT_SECTION)
	    rel->r_addend += sec->output_offset;
	  continue;
	}

      switch (r_type)
	{
	default:
	  r = mep_final_link_relocate (howto, input_bfd, input_section,
					 contents, rel, relocation);
	  break;
	}

      if (r != bfd_reloc_ok)
	{
	  const char * msg = (const char *) NULL;

	  switch (r)
	    {
	    case bfd_reloc_overflow:
	      r = info->callbacks->reloc_overflow
		(info, (h ? &h->root : NULL), name, howto->name, (bfd_vma) 0,
		 input_bfd, input_section, rel->r_offset);
	      break;

	    case bfd_reloc_undefined:
	      r = info->callbacks->undefined_symbol
		(info, name, input_bfd, input_section, rel->r_offset, TRUE);
	      break;

	    case bfd_reloc_outofrange:
	      msg = _("internal error: out of range error");
	      break;

	    case bfd_reloc_notsupported:
	      msg = _("internal error: unsupported relocation error");
	      break;

	    case bfd_reloc_dangerous:
	      msg = _("internal error: dangerous relocation");
	      break;

	    default:
	      msg = _("internal error: unknown error");
	      break;
	    }

	  if (msg)
	    r = info->callbacks->warning
	      (info, msg, name, input_bfd, input_section, rel->r_offset);

	  if (! r)
	    return FALSE;
	}
    }

  if (warn_tp)
    info->callbacks->undefined_symbol
      (info, "__tpbase", input_bfd, input_section, warn_tp-1, TRUE);
  if (warn_sda)
    info->callbacks->undefined_symbol
      (info, "__sdabase", input_bfd, input_section, warn_sda-1, TRUE);
  if (warn_sda || warn_tp)
    return FALSE;

  return TRUE;
}


/* Update the got entry reference counts for the section being
   removed.  */

static bfd_boolean
mep_elf_gc_sweep_hook
    (bfd *                     abfd ATTRIBUTE_UNUSED,
     struct bfd_link_info *    info ATTRIBUTE_UNUSED,
     asection *                sec ATTRIBUTE_UNUSED,
     const Elf_Internal_Rela * relocs ATTRIBUTE_UNUSED)
{
  return TRUE;
}

/* Return the section that should be marked against GC for a given
   relocation.  */

static asection *
mep_elf_gc_mark_hook
    (asection *                   sec,
     struct bfd_link_info *       info ATTRIBUTE_UNUSED,
     Elf_Internal_Rela *          rel,
     struct elf_link_hash_entry * h,
     Elf_Internal_Sym *           sym)
{
  if (h != NULL)
    {
      switch (ELF32_R_TYPE (rel->r_info))
	{
	default:
	  switch (h->root.type)
	    {
	    case bfd_link_hash_defined:
	    case bfd_link_hash_defweak:
	      return h->root.u.def.section;

	    case bfd_link_hash_common:
	      return h->root.u.c.p->section;

	    default:
	      break;
	    }
	}
    }
  else
    {
      if (!(elf_bad_symtab (sec->owner)
	    && ELF_ST_BIND (sym->st_info) != STB_LOCAL)
	  && ! ((sym->st_shndx <= 0 || sym->st_shndx >= SHN_LORESERVE)
		&& sym->st_shndx != SHN_COMMON))
	return bfd_section_from_elf_index (sec->owner, sym->st_shndx);
    }

  return NULL;
}


/* Function to set the ELF flag bits.  */

static bfd_boolean
mep_elf_set_private_flags (bfd *    abfd,
			   flagword flags)
{
  elf_elfheader (abfd)->e_flags = flags;
  elf_flags_init (abfd) = TRUE;
  return TRUE;
}

static bfd_boolean
mep_elf_copy_private_bfd_data (bfd * ibfd, bfd * obfd)
{
  if (bfd_get_flavour (ibfd) != bfd_target_elf_flavour
      || bfd_get_flavour (obfd) != bfd_target_elf_flavour)
    return TRUE;

  elf_elfheader (obfd)->e_flags = elf_elfheader (ibfd)->e_flags;
  elf_flags_init (obfd) = TRUE;

  /* Copy object attributes.  */
  _bfd_elf_copy_obj_attributes (ibfd, obfd);

  return TRUE;
}

/* Merge backend specific data from an object file to the output
   object file when linking.  */

static bfd_boolean
mep_elf_merge_private_bfd_data (bfd * ibfd, bfd * obfd)
{
  static bfd *last_ibfd = 0;
  flagword old_flags, new_flags;
  flagword old_partial, new_partial;

  /* Check if we have the same endianess.  */
  if (_bfd_generic_verify_endian_match (ibfd, obfd) == FALSE)
    return FALSE;

  new_flags = elf_elfheader (ibfd)->e_flags;
  old_flags = elf_elfheader (obfd)->e_flags;

#ifdef DEBUG
  _bfd_error_handler ("%B: old_flags = 0x%.8lx, new_flags = 0x%.8lx, init = %s",
		      ibfd, old_flags, new_flags, elf_flags_init (obfd) ? "yes" : "no");
#endif

    /* First call, no flags set.  */
    if (!elf_flags_init (obfd))
    {
      elf_flags_init (obfd) = TRUE;
      old_flags = new_flags;
    }
  else if ((new_flags | old_flags) & EF_MEP_LIBRARY)
    {
      /* Non-library flags trump library flags.  The choice doesn't really
	 matter if both OLD_FLAGS and NEW_FLAGS have EF_MEP_LIBRARY set.  */
      if (old_flags & EF_MEP_LIBRARY)
	old_flags = new_flags;
    }
  else
    {
      /* Make sure they're for the same mach.  Allow upgrade from the "mep"
	 mach.  */
      new_partial = (new_flags & EF_MEP_CPU_MASK);
      old_partial = (old_flags & EF_MEP_CPU_MASK);
      if (new_partial == old_partial)
	;
      else if (new_partial == EF_MEP_CPU_MEP)
	;
      else if (old_partial == EF_MEP_CPU_MEP)
	old_flags = (old_flags & ~EF_MEP_CPU_MASK) | new_partial;
      else
	{
	  _bfd_error_handler (_("%B and %B are for different cores"), last_ibfd, ibfd);
	  bfd_set_error (bfd_error_invalid_target);
	  return FALSE;
	}

      /* Make sure they're for the same me_module.  Allow basic config to
	 mix with any other.  */
      new_partial = (new_flags & EF_MEP_INDEX_MASK);
      old_partial = (old_flags & EF_MEP_INDEX_MASK);
      if (new_partial == old_partial)
	;
      else if (new_partial == 0)
	;
      else if (old_partial == 0)
	old_flags = (old_flags & ~EF_MEP_INDEX_MASK) | new_partial;
      else
	{
	  _bfd_error_handler (_("%B and %B are for different configurations"), last_ibfd, ibfd);
	  bfd_set_error (bfd_error_invalid_target);
	  return FALSE;
	}
    }

  elf_elfheader (obfd)->e_flags = old_flags;
  last_ibfd = ibfd;
  return TRUE;
}

/* This will be edited by the MeP configration tool.  */
static const char * config_names[] =
{
  "basic"
  /* start-mepcfgtool */
  ,"simple"
  ,"fmax"
  /* end-mepcfgtool */
};

static const char * core_names[] =
{
  "MeP", "MeP-c2", "MeP-c3", "MeP-h1"
};

static bfd_boolean
mep_elf_print_private_bfd_data (bfd * abfd, void * ptr)
{
  FILE *   file = (FILE *) ptr;
  flagword flags, partial_flags;

  BFD_ASSERT (abfd != NULL && ptr != NULL);

  /* Print normal ELF private data.  */
  _bfd_elf_print_private_bfd_data (abfd, ptr);

  flags = elf_elfheader (abfd)->e_flags;
  fprintf (file, _("private flags = 0x%lx"), (long)flags);

  partial_flags = (flags & EF_MEP_CPU_MASK) >> 24;
  if (partial_flags < ARRAY_SIZE (core_names))
    fprintf (file, "  core: %s", core_names[(long)partial_flags]);

  partial_flags = flags & EF_MEP_INDEX_MASK;
  if (partial_flags < ARRAY_SIZE (config_names))
    fprintf (file, "  me_module: %s", config_names[(long)partial_flags]);

  fputc ('\n', file);

  return TRUE;
}

/* Return the machine subcode from the ELF e_flags header.  */

static int
elf32_mep_machine (bfd * abfd)
{
  switch (elf_elfheader (abfd)->e_flags & EF_MEP_CPU_MASK)
    {
    default: break;
    case EF_MEP_CPU_C2: return bfd_mach_mep;
    case EF_MEP_CPU_C3: return bfd_mach_mep;
    case EF_MEP_CPU_C4: return bfd_mach_mep;
    case EF_MEP_CPU_H1: return bfd_mach_mep_h1;
    }

  return bfd_mach_mep;
}

static bfd_boolean
mep_elf_object_p (bfd * abfd)
{
  /* Irix 5 and 6 is broken.  Object file symbol tables are not always
     sorted correctly such that local symbols preceed global symbols,
     and the sh_info field in the symbol table is not always right.  */
  /* This is needed for the RELC support code.  */
  elf_bad_symtab (abfd) = TRUE;
  bfd_default_set_arch_mach (abfd, bfd_arch_mep, elf32_mep_machine (abfd));
  return TRUE;
}

static bfd_boolean
mep_elf_section_flags (flagword * flags, const Elf_Internal_Shdr * hdr)
{
  if (hdr->sh_flags & SHF_MEP_VLIW)
    * flags |= SEC_MEP_VLIW;
  return TRUE;
}

static bfd_boolean
mep_elf_fake_sections (bfd *               abfd ATTRIBUTE_UNUSED,
		       Elf_Internal_Shdr * hdr,
		       asection *          sec)
{
  if (sec->flags & SEC_MEP_VLIW)
    hdr->sh_flags |= SHF_MEP_VLIW;
  return TRUE;
}


#define ELF_ARCH		bfd_arch_mep
#define ELF_MACHINE_CODE	EM_CYGNUS_MEP
#define ELF_MAXPAGESIZE		0x1000

#define TARGET_BIG_SYM		bfd_elf32_mep_vec
#define TARGET_BIG_NAME		"elf32-mep"

#define TARGET_LITTLE_SYM	bfd_elf32_mep_little_vec
#define TARGET_LITTLE_NAME	"elf32-mep-little"

#define elf_info_to_howto_rel			NULL
#define elf_info_to_howto			mep_info_to_howto_rela
#define elf_backend_relocate_section		mep_elf_relocate_section
#define elf_backend_gc_mark_hook		mep_elf_gc_mark_hook
#define elf_backend_gc_sweep_hook		mep_elf_gc_sweep_hook
#define elf_backend_check_relocs                mep_elf_check_relocs
#define elf_backend_object_p		        mep_elf_object_p
#define elf_backend_section_flags		mep_elf_section_flags
#define elf_backend_fake_sections		mep_elf_fake_sections

#define elf_backend_can_gc_sections		1

#define bfd_elf32_bfd_reloc_type_lookup		mep_reloc_type_lookup
#define bfd_elf32_bfd_reloc_name_lookup	mep_reloc_name_lookup
#define bfd_elf32_bfd_set_private_flags		mep_elf_set_private_flags
#define bfd_elf32_bfd_copy_private_bfd_data	mep_elf_copy_private_bfd_data
#define bfd_elf32_bfd_merge_private_bfd_data	mep_elf_merge_private_bfd_data
#define bfd_elf32_bfd_print_private_bfd_data	mep_elf_print_private_bfd_data

/* We use only the RELA entries.  */
#define USE_RELA

#include "elf32-target.h"
