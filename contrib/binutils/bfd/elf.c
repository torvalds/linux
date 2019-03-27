/* ELF executable support for BFD.

   Copyright 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
   2002, 2003, 2004, 2005, 2006, 2007 Free Software Foundation, Inc.

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


/* $FreeBSD$ */


/*
SECTION
	ELF backends

	BFD support for ELF formats is being worked on.
	Currently, the best supported back ends are for sparc and i386
	(running svr4 or Solaris 2).

	Documentation of the internals of the support code still needs
	to be written.  The code is changing quickly enough that we
	haven't bothered yet.  */

/* For sparc64-cross-sparc32.  */
#define _SYSCALL32
#include "sysdep.h"
#include "bfd.h"
#include "bfdlink.h"
#include "libbfd.h"
#define ARCH_SIZE 0
#include "elf-bfd.h"
#include "libiberty.h"

static int elf_sort_sections (const void *, const void *);
static bfd_boolean assign_file_positions_except_relocs (bfd *, struct bfd_link_info *);
static bfd_boolean prep_headers (bfd *);
static bfd_boolean swap_out_syms (bfd *, struct bfd_strtab_hash **, int) ;
static bfd_boolean elfcore_read_notes (bfd *, file_ptr, bfd_size_type) ;

/* Swap version information in and out.  The version information is
   currently size independent.  If that ever changes, this code will
   need to move into elfcode.h.  */

/* Swap in a Verdef structure.  */

void
_bfd_elf_swap_verdef_in (bfd *abfd,
			 const Elf_External_Verdef *src,
			 Elf_Internal_Verdef *dst)
{
  dst->vd_version = H_GET_16 (abfd, src->vd_version);
  dst->vd_flags   = H_GET_16 (abfd, src->vd_flags);
  dst->vd_ndx     = H_GET_16 (abfd, src->vd_ndx);
  dst->vd_cnt     = H_GET_16 (abfd, src->vd_cnt);
  dst->vd_hash    = H_GET_32 (abfd, src->vd_hash);
  dst->vd_aux     = H_GET_32 (abfd, src->vd_aux);
  dst->vd_next    = H_GET_32 (abfd, src->vd_next);
}

/* Swap out a Verdef structure.  */

void
_bfd_elf_swap_verdef_out (bfd *abfd,
			  const Elf_Internal_Verdef *src,
			  Elf_External_Verdef *dst)
{
  H_PUT_16 (abfd, src->vd_version, dst->vd_version);
  H_PUT_16 (abfd, src->vd_flags, dst->vd_flags);
  H_PUT_16 (abfd, src->vd_ndx, dst->vd_ndx);
  H_PUT_16 (abfd, src->vd_cnt, dst->vd_cnt);
  H_PUT_32 (abfd, src->vd_hash, dst->vd_hash);
  H_PUT_32 (abfd, src->vd_aux, dst->vd_aux);
  H_PUT_32 (abfd, src->vd_next, dst->vd_next);
}

/* Swap in a Verdaux structure.  */

void
_bfd_elf_swap_verdaux_in (bfd *abfd,
			  const Elf_External_Verdaux *src,
			  Elf_Internal_Verdaux *dst)
{
  dst->vda_name = H_GET_32 (abfd, src->vda_name);
  dst->vda_next = H_GET_32 (abfd, src->vda_next);
}

/* Swap out a Verdaux structure.  */

void
_bfd_elf_swap_verdaux_out (bfd *abfd,
			   const Elf_Internal_Verdaux *src,
			   Elf_External_Verdaux *dst)
{
  H_PUT_32 (abfd, src->vda_name, dst->vda_name);
  H_PUT_32 (abfd, src->vda_next, dst->vda_next);
}

/* Swap in a Verneed structure.  */

void
_bfd_elf_swap_verneed_in (bfd *abfd,
			  const Elf_External_Verneed *src,
			  Elf_Internal_Verneed *dst)
{
  dst->vn_version = H_GET_16 (abfd, src->vn_version);
  dst->vn_cnt     = H_GET_16 (abfd, src->vn_cnt);
  dst->vn_file    = H_GET_32 (abfd, src->vn_file);
  dst->vn_aux     = H_GET_32 (abfd, src->vn_aux);
  dst->vn_next    = H_GET_32 (abfd, src->vn_next);
}

/* Swap out a Verneed structure.  */

void
_bfd_elf_swap_verneed_out (bfd *abfd,
			   const Elf_Internal_Verneed *src,
			   Elf_External_Verneed *dst)
{
  H_PUT_16 (abfd, src->vn_version, dst->vn_version);
  H_PUT_16 (abfd, src->vn_cnt, dst->vn_cnt);
  H_PUT_32 (abfd, src->vn_file, dst->vn_file);
  H_PUT_32 (abfd, src->vn_aux, dst->vn_aux);
  H_PUT_32 (abfd, src->vn_next, dst->vn_next);
}

/* Swap in a Vernaux structure.  */

void
_bfd_elf_swap_vernaux_in (bfd *abfd,
			  const Elf_External_Vernaux *src,
			  Elf_Internal_Vernaux *dst)
{
  dst->vna_hash  = H_GET_32 (abfd, src->vna_hash);
  dst->vna_flags = H_GET_16 (abfd, src->vna_flags);
  dst->vna_other = H_GET_16 (abfd, src->vna_other);
  dst->vna_name  = H_GET_32 (abfd, src->vna_name);
  dst->vna_next  = H_GET_32 (abfd, src->vna_next);
}

/* Swap out a Vernaux structure.  */

void
_bfd_elf_swap_vernaux_out (bfd *abfd,
			   const Elf_Internal_Vernaux *src,
			   Elf_External_Vernaux *dst)
{
  H_PUT_32 (abfd, src->vna_hash, dst->vna_hash);
  H_PUT_16 (abfd, src->vna_flags, dst->vna_flags);
  H_PUT_16 (abfd, src->vna_other, dst->vna_other);
  H_PUT_32 (abfd, src->vna_name, dst->vna_name);
  H_PUT_32 (abfd, src->vna_next, dst->vna_next);
}

/* Swap in a Versym structure.  */

void
_bfd_elf_swap_versym_in (bfd *abfd,
			 const Elf_External_Versym *src,
			 Elf_Internal_Versym *dst)
{
  dst->vs_vers = H_GET_16 (abfd, src->vs_vers);
}

/* Swap out a Versym structure.  */

void
_bfd_elf_swap_versym_out (bfd *abfd,
			  const Elf_Internal_Versym *src,
			  Elf_External_Versym *dst)
{
  H_PUT_16 (abfd, src->vs_vers, dst->vs_vers);
}

/* Standard ELF hash function.  Do not change this function; you will
   cause invalid hash tables to be generated.  */

unsigned long
bfd_elf_hash (const char *namearg)
{
  const unsigned char *name = (const unsigned char *) namearg;
  unsigned long h = 0;
  unsigned long g;
  int ch;

  while ((ch = *name++) != '\0')
    {
      h = (h << 4) + ch;
      if ((g = (h & 0xf0000000)) != 0)
	{
	  h ^= g >> 24;
	  /* The ELF ABI says `h &= ~g', but this is equivalent in
	     this case and on some machines one insn instead of two.  */
	  h ^= g;
	}
    }
  return h & 0xffffffff;
}

/* DT_GNU_HASH hash function.  Do not change this function; you will
   cause invalid hash tables to be generated.  */

unsigned long
bfd_elf_gnu_hash (const char *namearg)
{
  const unsigned char *name = (const unsigned char *) namearg;
  unsigned long h = 5381;
  unsigned char ch;

  while ((ch = *name++) != '\0')
    h = (h << 5) + h + ch;
  return h & 0xffffffff;
}

bfd_boolean
bfd_elf_mkobject (bfd *abfd)
{
  if (abfd->tdata.any == NULL)
    {
      abfd->tdata.any = bfd_zalloc (abfd, sizeof (struct elf_obj_tdata));
      if (abfd->tdata.any == NULL)
	return FALSE;
    }

  elf_tdata (abfd)->program_header_size = (bfd_size_type) -1;

  return TRUE;
}

bfd_boolean
bfd_elf_mkcorefile (bfd *abfd)
{
  /* I think this can be done just like an object file.  */
  return bfd_elf_mkobject (abfd);
}

char *
bfd_elf_get_str_section (bfd *abfd, unsigned int shindex)
{
  Elf_Internal_Shdr **i_shdrp;
  bfd_byte *shstrtab = NULL;
  file_ptr offset;
  bfd_size_type shstrtabsize;

  i_shdrp = elf_elfsections (abfd);
  if (i_shdrp == 0
      || shindex >= elf_numsections (abfd)
      || i_shdrp[shindex] == 0)
    return NULL;

  shstrtab = i_shdrp[shindex]->contents;
  if (shstrtab == NULL)
    {
      /* No cached one, attempt to read, and cache what we read.  */
      offset = i_shdrp[shindex]->sh_offset;
      shstrtabsize = i_shdrp[shindex]->sh_size;

      /* Allocate and clear an extra byte at the end, to prevent crashes
	 in case the string table is not terminated.  */
      if (shstrtabsize + 1 == 0
	  || (shstrtab = bfd_alloc (abfd, shstrtabsize + 1)) == NULL
	  || bfd_seek (abfd, offset, SEEK_SET) != 0)
	shstrtab = NULL;
      else if (bfd_bread (shstrtab, shstrtabsize, abfd) != shstrtabsize)
	{
	  if (bfd_get_error () != bfd_error_system_call)
	    bfd_set_error (bfd_error_file_truncated);
	  shstrtab = NULL;
	}
      else
	shstrtab[shstrtabsize] = '\0';
      i_shdrp[shindex]->contents = shstrtab;
    }
  return (char *) shstrtab;
}

char *
bfd_elf_string_from_elf_section (bfd *abfd,
				 unsigned int shindex,
				 unsigned int strindex)
{
  Elf_Internal_Shdr *hdr;

  if (strindex == 0)
    return "";

  if (elf_elfsections (abfd) == NULL || shindex >= elf_numsections (abfd))
    return NULL;

  hdr = elf_elfsections (abfd)[shindex];

  if (hdr->contents == NULL
      && bfd_elf_get_str_section (abfd, shindex) == NULL)
    return NULL;

  if (strindex >= hdr->sh_size)
    {
      unsigned int shstrndx = elf_elfheader(abfd)->e_shstrndx;
      (*_bfd_error_handler)
	(_("%B: invalid string offset %u >= %lu for section `%s'"),
	 abfd, strindex, (unsigned long) hdr->sh_size,
	 (shindex == shstrndx && strindex == hdr->sh_name
	  ? ".shstrtab"
	  : bfd_elf_string_from_elf_section (abfd, shstrndx, hdr->sh_name)));
      return "";
    }

  return ((char *) hdr->contents) + strindex;
}

/* Read and convert symbols to internal format.
   SYMCOUNT specifies the number of symbols to read, starting from
   symbol SYMOFFSET.  If any of INTSYM_BUF, EXTSYM_BUF or EXTSHNDX_BUF
   are non-NULL, they are used to store the internal symbols, external
   symbols, and symbol section index extensions, respectively.  */

Elf_Internal_Sym *
bfd_elf_get_elf_syms (bfd *ibfd,
		      Elf_Internal_Shdr *symtab_hdr,
		      size_t symcount,
		      size_t symoffset,
		      Elf_Internal_Sym *intsym_buf,
		      void *extsym_buf,
		      Elf_External_Sym_Shndx *extshndx_buf)
{
  Elf_Internal_Shdr *shndx_hdr;
  void *alloc_ext;
  const bfd_byte *esym;
  Elf_External_Sym_Shndx *alloc_extshndx;
  Elf_External_Sym_Shndx *shndx;
  Elf_Internal_Sym *isym;
  Elf_Internal_Sym *isymend;
  const struct elf_backend_data *bed;
  size_t extsym_size;
  bfd_size_type amt;
  file_ptr pos;

  if (symcount == 0)
    return intsym_buf;

  /* Normal syms might have section extension entries.  */
  shndx_hdr = NULL;
  if (symtab_hdr == &elf_tdata (ibfd)->symtab_hdr)
    shndx_hdr = &elf_tdata (ibfd)->symtab_shndx_hdr;

  /* Read the symbols.  */
  alloc_ext = NULL;
  alloc_extshndx = NULL;
  bed = get_elf_backend_data (ibfd);
  extsym_size = bed->s->sizeof_sym;
  amt = symcount * extsym_size;
  pos = symtab_hdr->sh_offset + symoffset * extsym_size;
  if (extsym_buf == NULL)
    {
      alloc_ext = bfd_malloc2 (symcount, extsym_size);
      extsym_buf = alloc_ext;
    }
  if (extsym_buf == NULL
      || bfd_seek (ibfd, pos, SEEK_SET) != 0
      || bfd_bread (extsym_buf, amt, ibfd) != amt)
    {
      intsym_buf = NULL;
      goto out;
    }

  if (shndx_hdr == NULL || shndx_hdr->sh_size == 0)
    extshndx_buf = NULL;
  else
    {
      amt = symcount * sizeof (Elf_External_Sym_Shndx);
      pos = shndx_hdr->sh_offset + symoffset * sizeof (Elf_External_Sym_Shndx);
      if (extshndx_buf == NULL)
	{
	  alloc_extshndx = bfd_malloc2 (symcount,
					sizeof (Elf_External_Sym_Shndx));
	  extshndx_buf = alloc_extshndx;
	}
      if (extshndx_buf == NULL
	  || bfd_seek (ibfd, pos, SEEK_SET) != 0
	  || bfd_bread (extshndx_buf, amt, ibfd) != amt)
	{
	  intsym_buf = NULL;
	  goto out;
	}
    }

  if (intsym_buf == NULL)
    {
      intsym_buf = bfd_malloc2 (symcount, sizeof (Elf_Internal_Sym));
      if (intsym_buf == NULL)
	goto out;
    }

  /* Convert the symbols to internal form.  */
  isymend = intsym_buf + symcount;
  for (esym = extsym_buf, isym = intsym_buf, shndx = extshndx_buf;
       isym < isymend;
       esym += extsym_size, isym++, shndx = shndx != NULL ? shndx + 1 : NULL)
    if (!(*bed->s->swap_symbol_in) (ibfd, esym, shndx, isym))
      {
	symoffset += (esym - (bfd_byte *) extsym_buf) / extsym_size;
	(*_bfd_error_handler) (_("%B symbol number %lu references "
				 "nonexistent SHT_SYMTAB_SHNDX section"),
			       ibfd, (unsigned long) symoffset);
	intsym_buf = NULL;
	goto out;
      }

 out:
  if (alloc_ext != NULL)
    free (alloc_ext);
  if (alloc_extshndx != NULL)
    free (alloc_extshndx);

  return intsym_buf;
}

/* Look up a symbol name.  */
const char *
bfd_elf_sym_name (bfd *abfd,
		  Elf_Internal_Shdr *symtab_hdr,
		  Elf_Internal_Sym *isym,
		  asection *sym_sec)
{
  const char *name;
  unsigned int iname = isym->st_name;
  unsigned int shindex = symtab_hdr->sh_link;

  if (iname == 0 && ELF_ST_TYPE (isym->st_info) == STT_SECTION
      /* Check for a bogus st_shndx to avoid crashing.  */
      && isym->st_shndx < elf_numsections (abfd)
      && !(isym->st_shndx >= SHN_LORESERVE && isym->st_shndx <= SHN_HIRESERVE))
    {
      iname = elf_elfsections (abfd)[isym->st_shndx]->sh_name;
      shindex = elf_elfheader (abfd)->e_shstrndx;
    }

  name = bfd_elf_string_from_elf_section (abfd, shindex, iname);
  if (name == NULL)
    name = "(null)";
  else if (sym_sec && *name == '\0')
    name = bfd_section_name (abfd, sym_sec);

  return name;
}

/* Elf_Internal_Shdr->contents is an array of these for SHT_GROUP
   sections.  The first element is the flags, the rest are section
   pointers.  */

typedef union elf_internal_group {
  Elf_Internal_Shdr *shdr;
  unsigned int flags;
} Elf_Internal_Group;

/* Return the name of the group signature symbol.  Why isn't the
   signature just a string?  */

static const char *
group_signature (bfd *abfd, Elf_Internal_Shdr *ghdr)
{
  Elf_Internal_Shdr *hdr;
  unsigned char esym[sizeof (Elf64_External_Sym)];
  Elf_External_Sym_Shndx eshndx;
  Elf_Internal_Sym isym;

  /* First we need to ensure the symbol table is available.  Make sure
     that it is a symbol table section.  */
  hdr = elf_elfsections (abfd) [ghdr->sh_link];
  if (hdr->sh_type != SHT_SYMTAB
      || ! bfd_section_from_shdr (abfd, ghdr->sh_link))
    return NULL;

  /* Go read the symbol.  */
  hdr = &elf_tdata (abfd)->symtab_hdr;
  if (bfd_elf_get_elf_syms (abfd, hdr, 1, ghdr->sh_info,
			    &isym, esym, &eshndx) == NULL)
    return NULL;

  return bfd_elf_sym_name (abfd, hdr, &isym, NULL);
}

/* Set next_in_group list pointer, and group name for NEWSECT.  */

static bfd_boolean
setup_group (bfd *abfd, Elf_Internal_Shdr *hdr, asection *newsect)
{
  unsigned int num_group = elf_tdata (abfd)->num_group;

  /* If num_group is zero, read in all SHT_GROUP sections.  The count
     is set to -1 if there are no SHT_GROUP sections.  */
  if (num_group == 0)
    {
      unsigned int i, shnum;

      /* First count the number of groups.  If we have a SHT_GROUP
	 section with just a flag word (ie. sh_size is 4), ignore it.  */
      shnum = elf_numsections (abfd);
      num_group = 0;

#define IS_VALID_GROUP_SECTION_HEADER(shdr)		\
	(   (shdr)->sh_type == SHT_GROUP		\
	 && (shdr)->sh_size >= (2 * GRP_ENTRY_SIZE)	\
	 && (shdr)->sh_entsize == GRP_ENTRY_SIZE	\
	 && ((shdr)->sh_size % GRP_ENTRY_SIZE) == 0)

      for (i = 0; i < shnum; i++)
	{
	  Elf_Internal_Shdr *shdr = elf_elfsections (abfd)[i];

	  if (IS_VALID_GROUP_SECTION_HEADER (shdr))
	    num_group += 1;
	}

      if (num_group == 0)
	{
	  num_group = (unsigned) -1;
	  elf_tdata (abfd)->num_group = num_group;
	}
      else
	{
	  /* We keep a list of elf section headers for group sections,
	     so we can find them quickly.  */
	  bfd_size_type amt;

	  elf_tdata (abfd)->num_group = num_group;
	  elf_tdata (abfd)->group_sect_ptr
	    = bfd_alloc2 (abfd, num_group, sizeof (Elf_Internal_Shdr *));
	  if (elf_tdata (abfd)->group_sect_ptr == NULL)
	    return FALSE;

	  num_group = 0;
	  for (i = 0; i < shnum; i++)
	    {
	      Elf_Internal_Shdr *shdr = elf_elfsections (abfd)[i];

	      if (IS_VALID_GROUP_SECTION_HEADER (shdr))
		{
		  unsigned char *src;
		  Elf_Internal_Group *dest;

		  /* Add to list of sections.  */
		  elf_tdata (abfd)->group_sect_ptr[num_group] = shdr;
		  num_group += 1;

		  /* Read the raw contents.  */
		  BFD_ASSERT (sizeof (*dest) >= 4);
		  amt = shdr->sh_size * sizeof (*dest) / 4;
		  shdr->contents = bfd_alloc2 (abfd, shdr->sh_size,
					       sizeof (*dest) / 4);
		  /* PR binutils/4110: Handle corrupt group headers.  */
		  if (shdr->contents == NULL)
		    {
		      _bfd_error_handler
			(_("%B: Corrupt size field in group section header: 0x%lx"), abfd, shdr->sh_size);
		      bfd_set_error (bfd_error_bad_value);
		      return FALSE;
		    }

		  memset (shdr->contents, 0, amt);

		  if (bfd_seek (abfd, shdr->sh_offset, SEEK_SET) != 0
		      || (bfd_bread (shdr->contents, shdr->sh_size, abfd)
			  != shdr->sh_size))
		    return FALSE;

		  /* Translate raw contents, a flag word followed by an
		     array of elf section indices all in target byte order,
		     to the flag word followed by an array of elf section
		     pointers.  */
		  src = shdr->contents + shdr->sh_size;
		  dest = (Elf_Internal_Group *) (shdr->contents + amt);
		  while (1)
		    {
		      unsigned int idx;

		      src -= 4;
		      --dest;
		      idx = H_GET_32 (abfd, src);
		      if (src == shdr->contents)
			{
			  dest->flags = idx;
			  if (shdr->bfd_section != NULL && (idx & GRP_COMDAT))
			    shdr->bfd_section->flags
			      |= SEC_LINK_ONCE | SEC_LINK_DUPLICATES_DISCARD;
			  break;
			}
		      if (idx >= shnum)
			{
			  ((*_bfd_error_handler)
			   (_("%B: invalid SHT_GROUP entry"), abfd));
			  idx = 0;
			}
		      dest->shdr = elf_elfsections (abfd)[idx];
		    }
		}
	    }
	}
    }

  if (num_group != (unsigned) -1)
    {
      unsigned int i;

      for (i = 0; i < num_group; i++)
	{
	  Elf_Internal_Shdr *shdr = elf_tdata (abfd)->group_sect_ptr[i];
	  Elf_Internal_Group *idx = (Elf_Internal_Group *) shdr->contents;
	  unsigned int n_elt = shdr->sh_size / 4;

	  /* Look through this group's sections to see if current
	     section is a member.  */
	  while (--n_elt != 0)
	    if ((++idx)->shdr == hdr)
	      {
		asection *s = NULL;

		/* We are a member of this group.  Go looking through
		   other members to see if any others are linked via
		   next_in_group.  */
		idx = (Elf_Internal_Group *) shdr->contents;
		n_elt = shdr->sh_size / 4;
		while (--n_elt != 0)
		  if ((s = (++idx)->shdr->bfd_section) != NULL
		      && elf_next_in_group (s) != NULL)
		    break;
		if (n_elt != 0)
		  {
		    /* Snarf the group name from other member, and
		       insert current section in circular list.  */
		    elf_group_name (newsect) = elf_group_name (s);
		    elf_next_in_group (newsect) = elf_next_in_group (s);
		    elf_next_in_group (s) = newsect;
		  }
		else
		  {
		    const char *gname;

		    gname = group_signature (abfd, shdr);
		    if (gname == NULL)
		      return FALSE;
		    elf_group_name (newsect) = gname;

		    /* Start a circular list with one element.  */
		    elf_next_in_group (newsect) = newsect;
		  }

		/* If the group section has been created, point to the
		   new member.  */
		if (shdr->bfd_section != NULL)
		  elf_next_in_group (shdr->bfd_section) = newsect;

		i = num_group - 1;
		break;
	      }
	}
    }

  if (elf_group_name (newsect) == NULL)
    {
      (*_bfd_error_handler) (_("%B: no group info for section %A"),
			     abfd, newsect);
    }
  return TRUE;
}

bfd_boolean
_bfd_elf_setup_sections (bfd *abfd)
{
  unsigned int i;
  unsigned int num_group = elf_tdata (abfd)->num_group;
  bfd_boolean result = TRUE;
  asection *s;

  /* Process SHF_LINK_ORDER.  */
  for (s = abfd->sections; s != NULL; s = s->next)
    {
      Elf_Internal_Shdr *this_hdr = &elf_section_data (s)->this_hdr;
      if ((this_hdr->sh_flags & SHF_LINK_ORDER) != 0)
	{
	  unsigned int elfsec = this_hdr->sh_link;
	  /* FIXME: The old Intel compiler and old strip/objcopy may
	     not set the sh_link or sh_info fields.  Hence we could
	     get the situation where elfsec is 0.  */
	  if (elfsec == 0)
	    {
	      const struct elf_backend_data *bed
		= get_elf_backend_data (abfd);
	      if (bed->link_order_error_handler)
		bed->link_order_error_handler
		  (_("%B: warning: sh_link not set for section `%A'"),
		   abfd, s);
	    }
	  else
	    {
	      asection *link;

	      this_hdr = elf_elfsections (abfd)[elfsec];

	      /* PR 1991, 2008:
		 Some strip/objcopy may leave an incorrect value in
		 sh_link.  We don't want to proceed.  */
	      link = this_hdr->bfd_section;
	      if (link == NULL)
		{
		  (*_bfd_error_handler)
		    (_("%B: sh_link [%d] in section `%A' is incorrect"),
		     s->owner, s, elfsec);
		  result = FALSE;
		}

	      elf_linked_to_section (s) = link;
	    }
	}
    }

  /* Process section groups.  */
  if (num_group == (unsigned) -1)
    return result;

  for (i = 0; i < num_group; i++)
    {
      Elf_Internal_Shdr *shdr = elf_tdata (abfd)->group_sect_ptr[i];
      Elf_Internal_Group *idx = (Elf_Internal_Group *) shdr->contents;
      unsigned int n_elt = shdr->sh_size / 4;

      while (--n_elt != 0)
	if ((++idx)->shdr->bfd_section)
	  elf_sec_group (idx->shdr->bfd_section) = shdr->bfd_section;
	else if (idx->shdr->sh_type == SHT_RELA
		 || idx->shdr->sh_type == SHT_REL)
	  /* We won't include relocation sections in section groups in
	     output object files. We adjust the group section size here
	     so that relocatable link will work correctly when
	     relocation sections are in section group in input object
	     files.  */
	  shdr->bfd_section->size -= 4;
	else
	  {
	    /* There are some unknown sections in the group.  */
	    (*_bfd_error_handler)
	      (_("%B: unknown [%d] section `%s' in group [%s]"),
	       abfd,
	       (unsigned int) idx->shdr->sh_type,
	       bfd_elf_string_from_elf_section (abfd,
						(elf_elfheader (abfd)
						 ->e_shstrndx),
						idx->shdr->sh_name),
	       shdr->bfd_section->name);
	    result = FALSE;
	  }
    }
  return result;
}

bfd_boolean
bfd_elf_is_group_section (bfd *abfd ATTRIBUTE_UNUSED, const asection *sec)
{
  return elf_next_in_group (sec) != NULL;
}

/* Make a BFD section from an ELF section.  We store a pointer to the
   BFD section in the bfd_section field of the header.  */

bfd_boolean
_bfd_elf_make_section_from_shdr (bfd *abfd,
				 Elf_Internal_Shdr *hdr,
				 const char *name,
				 int shindex)
{
  asection *newsect;
  flagword flags;
  const struct elf_backend_data *bed;

  if (hdr->bfd_section != NULL)
    {
      BFD_ASSERT (strcmp (name,
			  bfd_get_section_name (abfd, hdr->bfd_section)) == 0);
      return TRUE;
    }

  newsect = bfd_make_section_anyway (abfd, name);
  if (newsect == NULL)
    return FALSE;

  hdr->bfd_section = newsect;
  elf_section_data (newsect)->this_hdr = *hdr;
  elf_section_data (newsect)->this_idx = shindex;

  /* Always use the real type/flags.  */
  elf_section_type (newsect) = hdr->sh_type;
  elf_section_flags (newsect) = hdr->sh_flags;

  newsect->filepos = hdr->sh_offset;

  if (! bfd_set_section_vma (abfd, newsect, hdr->sh_addr)
      || ! bfd_set_section_size (abfd, newsect, hdr->sh_size)
      || ! bfd_set_section_alignment (abfd, newsect,
				      bfd_log2 ((bfd_vma) hdr->sh_addralign)))
    return FALSE;

  flags = SEC_NO_FLAGS;
  if (hdr->sh_type != SHT_NOBITS)
    flags |= SEC_HAS_CONTENTS;
  if (hdr->sh_type == SHT_GROUP)
    flags |= SEC_GROUP | SEC_EXCLUDE;
  if ((hdr->sh_flags & SHF_ALLOC) != 0)
    {
      flags |= SEC_ALLOC;
      if (hdr->sh_type != SHT_NOBITS)
	flags |= SEC_LOAD;
    }
  if ((hdr->sh_flags & SHF_WRITE) == 0)
    flags |= SEC_READONLY;
  if ((hdr->sh_flags & SHF_EXECINSTR) != 0)
    flags |= SEC_CODE;
  else if ((flags & SEC_LOAD) != 0)
    flags |= SEC_DATA;
  if ((hdr->sh_flags & SHF_MERGE) != 0)
    {
      flags |= SEC_MERGE;
      newsect->entsize = hdr->sh_entsize;
      if ((hdr->sh_flags & SHF_STRINGS) != 0)
	flags |= SEC_STRINGS;
    }
  if (hdr->sh_flags & SHF_GROUP)
    if (!setup_group (abfd, hdr, newsect))
      return FALSE;
  if ((hdr->sh_flags & SHF_TLS) != 0)
    flags |= SEC_THREAD_LOCAL;

  if ((flags & SEC_ALLOC) == 0)
    {
      /* The debugging sections appear to be recognized only by name,
	 not any sort of flag.  Their SEC_ALLOC bits are cleared.  */
      static const struct
	{
	  const char *name;
	  int len;
	} debug_sections [] =
	{
	  { STRING_COMMA_LEN ("debug") },	/* 'd' */
	  { NULL,		 0  },	/* 'e' */
	  { NULL,		 0  },	/* 'f' */
	  { STRING_COMMA_LEN ("gnu.linkonce.wi.") },	/* 'g' */
	  { NULL,		 0  },	/* 'h' */
	  { NULL,		 0  },	/* 'i' */
	  { NULL,		 0  },	/* 'j' */
	  { NULL,		 0  },	/* 'k' */
	  { STRING_COMMA_LEN ("line") },	/* 'l' */
	  { NULL,		 0  },	/* 'm' */
	  { NULL,		 0  },	/* 'n' */
	  { NULL,		 0  },	/* 'o' */
	  { NULL,		 0  },	/* 'p' */
	  { NULL,		 0  },	/* 'q' */
	  { NULL,		 0  },	/* 'r' */
	  { STRING_COMMA_LEN ("stab") }	/* 's' */
	};

      if (name [0] == '.')
	{
	  int i = name [1] - 'd';
	  if (i >= 0
	      && i < (int) ARRAY_SIZE (debug_sections)
	      && debug_sections [i].name != NULL
	      && strncmp (&name [1], debug_sections [i].name,
			  debug_sections [i].len) == 0)
	    flags |= SEC_DEBUGGING;
	}
    }

  /* As a GNU extension, if the name begins with .gnu.linkonce, we
     only link a single copy of the section.  This is used to support
     g++.  g++ will emit each template expansion in its own section.
     The symbols will be defined as weak, so that multiple definitions
     are permitted.  The GNU linker extension is to actually discard
     all but one of the sections.  */
  if (CONST_STRNEQ (name, ".gnu.linkonce")
      && elf_next_in_group (newsect) == NULL)
    flags |= SEC_LINK_ONCE | SEC_LINK_DUPLICATES_DISCARD;

  bed = get_elf_backend_data (abfd);
  if (bed->elf_backend_section_flags)
    if (! bed->elf_backend_section_flags (&flags, hdr))
      return FALSE;

  if (! bfd_set_section_flags (abfd, newsect, flags))
    return FALSE;

  if ((flags & SEC_ALLOC) != 0)
    {
      Elf_Internal_Phdr *phdr;
      unsigned int i;

      /* Look through the phdrs to see if we need to adjust the lma.
	 If all the p_paddr fields are zero, we ignore them, since
	 some ELF linkers produce such output.  */
      phdr = elf_tdata (abfd)->phdr;
      for (i = 0; i < elf_elfheader (abfd)->e_phnum; i++, phdr++)
	{
	  if (phdr->p_paddr != 0)
	    break;
	}
      if (i < elf_elfheader (abfd)->e_phnum)
	{
	  phdr = elf_tdata (abfd)->phdr;
	  for (i = 0; i < elf_elfheader (abfd)->e_phnum; i++, phdr++)
	    {
	      /* This section is part of this segment if its file
		 offset plus size lies within the segment's memory
		 span and, if the section is loaded, the extent of the
		 loaded data lies within the extent of the segment.

		 Note - we used to check the p_paddr field as well, and
		 refuse to set the LMA if it was 0.  This is wrong
		 though, as a perfectly valid initialised segment can
		 have a p_paddr of zero.  Some architectures, eg ARM,
		 place special significance on the address 0 and
		 executables need to be able to have a segment which
		 covers this address.  */
	      if (phdr->p_type == PT_LOAD
		  && (bfd_vma) hdr->sh_offset >= phdr->p_offset
		  && (hdr->sh_offset + hdr->sh_size
		      <= phdr->p_offset + phdr->p_memsz)
		  && ((flags & SEC_LOAD) == 0
		      || (hdr->sh_offset + hdr->sh_size
			  <= phdr->p_offset + phdr->p_filesz)))
		{
		  if ((flags & SEC_LOAD) == 0)
		    newsect->lma = (phdr->p_paddr
				    + hdr->sh_addr - phdr->p_vaddr);
		  else
		    /* We used to use the same adjustment for SEC_LOAD
		       sections, but that doesn't work if the segment
		       is packed with code from multiple VMAs.
		       Instead we calculate the section LMA based on
		       the segment LMA.  It is assumed that the
		       segment will contain sections with contiguous
		       LMAs, even if the VMAs are not.  */
		    newsect->lma = (phdr->p_paddr
				    + hdr->sh_offset - phdr->p_offset);

		  /* With contiguous segments, we can't tell from file
		     offsets whether a section with zero size should
		     be placed at the end of one segment or the
		     beginning of the next.  Decide based on vaddr.  */
		  if (hdr->sh_addr >= phdr->p_vaddr
		      && (hdr->sh_addr + hdr->sh_size
			  <= phdr->p_vaddr + phdr->p_memsz))
		    break;
		}
	    }
	}
    }

  return TRUE;
}

/*
INTERNAL_FUNCTION
	bfd_elf_find_section

SYNOPSIS
	struct elf_internal_shdr *bfd_elf_find_section (bfd *abfd, char *name);

DESCRIPTION
	Helper functions for GDB to locate the string tables.
	Since BFD hides string tables from callers, GDB needs to use an
	internal hook to find them.  Sun's .stabstr, in particular,
	isn't even pointed to by the .stab section, so ordinary
	mechanisms wouldn't work to find it, even if we had some.
*/

struct elf_internal_shdr *
bfd_elf_find_section (bfd *abfd, char *name)
{
  Elf_Internal_Shdr **i_shdrp;
  char *shstrtab;
  unsigned int max;
  unsigned int i;

  i_shdrp = elf_elfsections (abfd);
  if (i_shdrp != NULL)
    {
      shstrtab = bfd_elf_get_str_section (abfd,
					  elf_elfheader (abfd)->e_shstrndx);
      if (shstrtab != NULL)
	{
	  max = elf_numsections (abfd);
	  for (i = 1; i < max; i++)
	    if (!strcmp (&shstrtab[i_shdrp[i]->sh_name], name))
	      return i_shdrp[i];
	}
    }
  return 0;
}

const char *const bfd_elf_section_type_names[] = {
  "SHT_NULL", "SHT_PROGBITS", "SHT_SYMTAB", "SHT_STRTAB",
  "SHT_RELA", "SHT_HASH", "SHT_DYNAMIC", "SHT_NOTE",
  "SHT_NOBITS", "SHT_REL", "SHT_SHLIB", "SHT_DYNSYM",
};

/* ELF relocs are against symbols.  If we are producing relocatable
   output, and the reloc is against an external symbol, and nothing
   has given us any additional addend, the resulting reloc will also
   be against the same symbol.  In such a case, we don't want to
   change anything about the way the reloc is handled, since it will
   all be done at final link time.  Rather than put special case code
   into bfd_perform_relocation, all the reloc types use this howto
   function.  It just short circuits the reloc if producing
   relocatable output against an external symbol.  */

bfd_reloc_status_type
bfd_elf_generic_reloc (bfd *abfd ATTRIBUTE_UNUSED,
		       arelent *reloc_entry,
		       asymbol *symbol,
		       void *data ATTRIBUTE_UNUSED,
		       asection *input_section,
		       bfd *output_bfd,
		       char **error_message ATTRIBUTE_UNUSED)
{
  if (output_bfd != NULL
      && (symbol->flags & BSF_SECTION_SYM) == 0
      && (! reloc_entry->howto->partial_inplace
	  || reloc_entry->addend == 0))
    {
      reloc_entry->address += input_section->output_offset;
      return bfd_reloc_ok;
    }

  return bfd_reloc_continue;
}

/* Make sure sec_info_type is cleared if sec_info is cleared too.  */

static void
merge_sections_remove_hook (bfd *abfd ATTRIBUTE_UNUSED,
			    asection *sec)
{
  BFD_ASSERT (sec->sec_info_type == ELF_INFO_TYPE_MERGE);
  sec->sec_info_type = ELF_INFO_TYPE_NONE;
}

/* Finish SHF_MERGE section merging.  */

bfd_boolean
_bfd_elf_merge_sections (bfd *abfd, struct bfd_link_info *info)
{
  bfd *ibfd;
  asection *sec;

  if (!is_elf_hash_table (info->hash))
    return FALSE;

  for (ibfd = info->input_bfds; ibfd != NULL; ibfd = ibfd->link_next)
    if ((ibfd->flags & DYNAMIC) == 0)
      for (sec = ibfd->sections; sec != NULL; sec = sec->next)
	if ((sec->flags & SEC_MERGE) != 0
	    && !bfd_is_abs_section (sec->output_section))
	  {
	    struct bfd_elf_section_data *secdata;

	    secdata = elf_section_data (sec);
	    if (! _bfd_add_merge_section (abfd,
					  &elf_hash_table (info)->merge_info,
					  sec, &secdata->sec_info))
	      return FALSE;
	    else if (secdata->sec_info)
	      sec->sec_info_type = ELF_INFO_TYPE_MERGE;
	  }

  if (elf_hash_table (info)->merge_info != NULL)
    _bfd_merge_sections (abfd, info, elf_hash_table (info)->merge_info,
			 merge_sections_remove_hook);
  return TRUE;
}

void
_bfd_elf_link_just_syms (asection *sec, struct bfd_link_info *info)
{
  sec->output_section = bfd_abs_section_ptr;
  sec->output_offset = sec->vma;
  if (!is_elf_hash_table (info->hash))
    return;

  sec->sec_info_type = ELF_INFO_TYPE_JUST_SYMS;
}

/* Copy the program header and other data from one object module to
   another.  */

bfd_boolean
_bfd_elf_copy_private_bfd_data (bfd *ibfd, bfd *obfd)
{
  if (bfd_get_flavour (ibfd) != bfd_target_elf_flavour
      || bfd_get_flavour (obfd) != bfd_target_elf_flavour)
    return TRUE;

  BFD_ASSERT (!elf_flags_init (obfd)
	      || (elf_elfheader (obfd)->e_flags
		  == elf_elfheader (ibfd)->e_flags));

  elf_gp (obfd) = elf_gp (ibfd);
  elf_elfheader (obfd)->e_flags = elf_elfheader (ibfd)->e_flags;
  elf_flags_init (obfd) = TRUE;

  /* Copy object attributes.  */
  _bfd_elf_copy_obj_attributes (ibfd, obfd);

  return TRUE;
}

static const char *
get_segment_type (unsigned int p_type)
{
  const char *pt;
  switch (p_type)
    {
    case PT_NULL: pt = "NULL"; break;
    case PT_LOAD: pt = "LOAD"; break;
    case PT_DYNAMIC: pt = "DYNAMIC"; break;
    case PT_INTERP: pt = "INTERP"; break;
    case PT_NOTE: pt = "NOTE"; break;
    case PT_SHLIB: pt = "SHLIB"; break;
    case PT_PHDR: pt = "PHDR"; break;
    case PT_TLS: pt = "TLS"; break;
    case PT_GNU_EH_FRAME: pt = "EH_FRAME"; break;
    case PT_GNU_STACK: pt = "STACK"; break;
    case PT_GNU_RELRO: pt = "RELRO"; break;
    default: pt = NULL; break;
    }
  return pt;
}

/* Print out the program headers.  */

bfd_boolean
_bfd_elf_print_private_bfd_data (bfd *abfd, void *farg)
{
  FILE *f = farg;
  Elf_Internal_Phdr *p;
  asection *s;
  bfd_byte *dynbuf = NULL;

  p = elf_tdata (abfd)->phdr;
  if (p != NULL)
    {
      unsigned int i, c;

      fprintf (f, _("\nProgram Header:\n"));
      c = elf_elfheader (abfd)->e_phnum;
      for (i = 0; i < c; i++, p++)
	{
	  const char *pt = get_segment_type (p->p_type);
	  char buf[20];

	  if (pt == NULL)
	    {
	      sprintf (buf, "0x%lx", p->p_type);
	      pt = buf;
	    }
	  fprintf (f, "%8s off    0x", pt);
	  bfd_fprintf_vma (abfd, f, p->p_offset);
	  fprintf (f, " vaddr 0x");
	  bfd_fprintf_vma (abfd, f, p->p_vaddr);
	  fprintf (f, " paddr 0x");
	  bfd_fprintf_vma (abfd, f, p->p_paddr);
	  fprintf (f, " align 2**%u\n", bfd_log2 (p->p_align));
	  fprintf (f, "         filesz 0x");
	  bfd_fprintf_vma (abfd, f, p->p_filesz);
	  fprintf (f, " memsz 0x");
	  bfd_fprintf_vma (abfd, f, p->p_memsz);
	  fprintf (f, " flags %c%c%c",
		   (p->p_flags & PF_R) != 0 ? 'r' : '-',
		   (p->p_flags & PF_W) != 0 ? 'w' : '-',
		   (p->p_flags & PF_X) != 0 ? 'x' : '-');
	  if ((p->p_flags &~ (unsigned) (PF_R | PF_W | PF_X)) != 0)
	    fprintf (f, " %lx", p->p_flags &~ (unsigned) (PF_R | PF_W | PF_X));
	  fprintf (f, "\n");
	}
    }

  s = bfd_get_section_by_name (abfd, ".dynamic");
  if (s != NULL)
    {
      int elfsec;
      unsigned long shlink;
      bfd_byte *extdyn, *extdynend;
      size_t extdynsize;
      void (*swap_dyn_in) (bfd *, const void *, Elf_Internal_Dyn *);

      fprintf (f, _("\nDynamic Section:\n"));

      if (!bfd_malloc_and_get_section (abfd, s, &dynbuf))
	goto error_return;

      elfsec = _bfd_elf_section_from_bfd_section (abfd, s);
      if (elfsec == -1)
	goto error_return;
      shlink = elf_elfsections (abfd)[elfsec]->sh_link;

      extdynsize = get_elf_backend_data (abfd)->s->sizeof_dyn;
      swap_dyn_in = get_elf_backend_data (abfd)->s->swap_dyn_in;

      extdyn = dynbuf;
      extdynend = extdyn + s->size;
      for (; extdyn < extdynend; extdyn += extdynsize)
	{
	  Elf_Internal_Dyn dyn;
	  const char *name;
	  char ab[20];
	  bfd_boolean stringp;

	  (*swap_dyn_in) (abfd, extdyn, &dyn);

	  if (dyn.d_tag == DT_NULL)
	    break;

	  stringp = FALSE;
	  switch (dyn.d_tag)
	    {
	    default:
	      sprintf (ab, "0x%lx", (unsigned long) dyn.d_tag);
	      name = ab;
	      break;

	    case DT_NEEDED: name = "NEEDED"; stringp = TRUE; break;
	    case DT_PLTRELSZ: name = "PLTRELSZ"; break;
	    case DT_PLTGOT: name = "PLTGOT"; break;
	    case DT_HASH: name = "HASH"; break;
	    case DT_STRTAB: name = "STRTAB"; break;
	    case DT_SYMTAB: name = "SYMTAB"; break;
	    case DT_RELA: name = "RELA"; break;
	    case DT_RELASZ: name = "RELASZ"; break;
	    case DT_RELAENT: name = "RELAENT"; break;
	    case DT_STRSZ: name = "STRSZ"; break;
	    case DT_SYMENT: name = "SYMENT"; break;
	    case DT_INIT: name = "INIT"; break;
	    case DT_FINI: name = "FINI"; break;
	    case DT_SONAME: name = "SONAME"; stringp = TRUE; break;
	    case DT_RPATH: name = "RPATH"; stringp = TRUE; break;
	    case DT_SYMBOLIC: name = "SYMBOLIC"; break;
	    case DT_REL: name = "REL"; break;
	    case DT_RELSZ: name = "RELSZ"; break;
	    case DT_RELENT: name = "RELENT"; break;
	    case DT_PLTREL: name = "PLTREL"; break;
	    case DT_DEBUG: name = "DEBUG"; break;
	    case DT_TEXTREL: name = "TEXTREL"; break;
	    case DT_JMPREL: name = "JMPREL"; break;
	    case DT_BIND_NOW: name = "BIND_NOW"; break;
	    case DT_INIT_ARRAY: name = "INIT_ARRAY"; break;
	    case DT_FINI_ARRAY: name = "FINI_ARRAY"; break;
	    case DT_INIT_ARRAYSZ: name = "INIT_ARRAYSZ"; break;
	    case DT_FINI_ARRAYSZ: name = "FINI_ARRAYSZ"; break;
	    case DT_RUNPATH: name = "RUNPATH"; stringp = TRUE; break;
	    case DT_FLAGS: name = "FLAGS"; break;
	    case DT_PREINIT_ARRAY: name = "PREINIT_ARRAY"; break;
	    case DT_PREINIT_ARRAYSZ: name = "PREINIT_ARRAYSZ"; break;
	    case DT_CHECKSUM: name = "CHECKSUM"; break;
	    case DT_PLTPADSZ: name = "PLTPADSZ"; break;
	    case DT_MOVEENT: name = "MOVEENT"; break;
	    case DT_MOVESZ: name = "MOVESZ"; break;
	    case DT_FEATURE: name = "FEATURE"; break;
	    case DT_POSFLAG_1: name = "POSFLAG_1"; break;
	    case DT_SYMINSZ: name = "SYMINSZ"; break;
	    case DT_SYMINENT: name = "SYMINENT"; break;
	    case DT_CONFIG: name = "CONFIG"; stringp = TRUE; break;
	    case DT_DEPAUDIT: name = "DEPAUDIT"; stringp = TRUE; break;
	    case DT_AUDIT: name = "AUDIT"; stringp = TRUE; break;
	    case DT_PLTPAD: name = "PLTPAD"; break;
	    case DT_MOVETAB: name = "MOVETAB"; break;
	    case DT_SYMINFO: name = "SYMINFO"; break;
	    case DT_RELACOUNT: name = "RELACOUNT"; break;
	    case DT_RELCOUNT: name = "RELCOUNT"; break;
	    case DT_FLAGS_1: name = "FLAGS_1"; break;
	    case DT_VERSYM: name = "VERSYM"; break;
	    case DT_VERDEF: name = "VERDEF"; break;
	    case DT_VERDEFNUM: name = "VERDEFNUM"; break;
	    case DT_VERNEED: name = "VERNEED"; break;
	    case DT_VERNEEDNUM: name = "VERNEEDNUM"; break;
	    case DT_AUXILIARY: name = "AUXILIARY"; stringp = TRUE; break;
	    case DT_USED: name = "USED"; break;
	    case DT_FILTER: name = "FILTER"; stringp = TRUE; break;
	    case DT_GNU_HASH: name = "GNU_HASH"; break;
	    }

	  fprintf (f, "  %-11s ", name);
	  if (! stringp)
	    fprintf (f, "0x%lx", (unsigned long) dyn.d_un.d_val);
	  else
	    {
	      const char *string;
	      unsigned int tagv = dyn.d_un.d_val;

	      string = bfd_elf_string_from_elf_section (abfd, shlink, tagv);
	      if (string == NULL)
		goto error_return;
	      fprintf (f, "%s", string);
	    }
	  fprintf (f, "\n");
	}

      free (dynbuf);
      dynbuf = NULL;
    }

  if ((elf_dynverdef (abfd) != 0 && elf_tdata (abfd)->verdef == NULL)
      || (elf_dynverref (abfd) != 0 && elf_tdata (abfd)->verref == NULL))
    {
      if (! _bfd_elf_slurp_version_tables (abfd, FALSE))
	return FALSE;
    }

  if (elf_dynverdef (abfd) != 0)
    {
      Elf_Internal_Verdef *t;

      fprintf (f, _("\nVersion definitions:\n"));
      for (t = elf_tdata (abfd)->verdef; t != NULL; t = t->vd_nextdef)
	{
	  fprintf (f, "%d 0x%2.2x 0x%8.8lx %s\n", t->vd_ndx,
		   t->vd_flags, t->vd_hash,
		   t->vd_nodename ? t->vd_nodename : "<corrupt>");
	  if (t->vd_auxptr != NULL && t->vd_auxptr->vda_nextptr != NULL)
	    {
	      Elf_Internal_Verdaux *a;

	      fprintf (f, "\t");
	      for (a = t->vd_auxptr->vda_nextptr;
		   a != NULL;
		   a = a->vda_nextptr)
		fprintf (f, "%s ",
			 a->vda_nodename ? a->vda_nodename : "<corrupt>");
	      fprintf (f, "\n");
	    }
	}
    }

  if (elf_dynverref (abfd) != 0)
    {
      Elf_Internal_Verneed *t;

      fprintf (f, _("\nVersion References:\n"));
      for (t = elf_tdata (abfd)->verref; t != NULL; t = t->vn_nextref)
	{
	  Elf_Internal_Vernaux *a;

	  fprintf (f, _("  required from %s:\n"),
		   t->vn_filename ? t->vn_filename : "<corrupt>");
	  for (a = t->vn_auxptr; a != NULL; a = a->vna_nextptr)
	    fprintf (f, "    0x%8.8lx 0x%2.2x %2.2d %s\n", a->vna_hash,
		     a->vna_flags, a->vna_other,
		     a->vna_nodename ? a->vna_nodename : "<corrupt>");
	}
    }

  return TRUE;

 error_return:
  if (dynbuf != NULL)
    free (dynbuf);
  return FALSE;
}

/* Display ELF-specific fields of a symbol.  */

void
bfd_elf_print_symbol (bfd *abfd,
		      void *filep,
		      asymbol *symbol,
		      bfd_print_symbol_type how)
{
  FILE *file = filep;
  switch (how)
    {
    case bfd_print_symbol_name:
      fprintf (file, "%s", symbol->name);
      break;
    case bfd_print_symbol_more:
      fprintf (file, "elf ");
      bfd_fprintf_vma (abfd, file, symbol->value);
      fprintf (file, " %lx", (long) symbol->flags);
      break;
    case bfd_print_symbol_all:
      {
	const char *section_name;
	const char *name = NULL;
	const struct elf_backend_data *bed;
	unsigned char st_other;
	bfd_vma val;

	section_name = symbol->section ? symbol->section->name : "(*none*)";

	bed = get_elf_backend_data (abfd);
	if (bed->elf_backend_print_symbol_all)
	  name = (*bed->elf_backend_print_symbol_all) (abfd, filep, symbol);

	if (name == NULL)
	  {
	    name = symbol->name;
	    bfd_print_symbol_vandf (abfd, file, symbol);
	  }

	fprintf (file, " %s\t", section_name);
	/* Print the "other" value for a symbol.  For common symbols,
	   we've already printed the size; now print the alignment.
	   For other symbols, we have no specified alignment, and
	   we've printed the address; now print the size.  */
	if (bfd_is_com_section (symbol->section))
	  val = ((elf_symbol_type *) symbol)->internal_elf_sym.st_value;
	else
	  val = ((elf_symbol_type *) symbol)->internal_elf_sym.st_size;
	bfd_fprintf_vma (abfd, file, val);

	/* If we have version information, print it.  */
	if (elf_tdata (abfd)->dynversym_section != 0
	    && (elf_tdata (abfd)->dynverdef_section != 0
		|| elf_tdata (abfd)->dynverref_section != 0))
	  {
	    unsigned int vernum;
	    const char *version_string;

	    vernum = ((elf_symbol_type *) symbol)->version & VERSYM_VERSION;

	    if (vernum == 0)
	      version_string = "";
	    else if (vernum == 1)
	      version_string = "Base";
	    else if (vernum <= elf_tdata (abfd)->cverdefs)
	      version_string =
		elf_tdata (abfd)->verdef[vernum - 1].vd_nodename;
	    else
	      {
		Elf_Internal_Verneed *t;

		version_string = "";
		for (t = elf_tdata (abfd)->verref;
		     t != NULL;
		     t = t->vn_nextref)
		  {
		    Elf_Internal_Vernaux *a;

		    for (a = t->vn_auxptr; a != NULL; a = a->vna_nextptr)
		      {
			if (a->vna_other == vernum)
			  {
			    version_string = a->vna_nodename;
			    break;
			  }
		      }
		  }
	      }

	    if ((((elf_symbol_type *) symbol)->version & VERSYM_HIDDEN) == 0)
	      fprintf (file, "  %-11s", version_string);
	    else
	      {
		int i;

		fprintf (file, " (%s)", version_string);
		for (i = 10 - strlen (version_string); i > 0; --i)
		  putc (' ', file);
	      }
	  }

	/* If the st_other field is not zero, print it.  */
	st_other = ((elf_symbol_type *) symbol)->internal_elf_sym.st_other;

	switch (st_other)
	  {
	  case 0: break;
	  case STV_INTERNAL:  fprintf (file, " .internal");  break;
	  case STV_HIDDEN:    fprintf (file, " .hidden");    break;
	  case STV_PROTECTED: fprintf (file, " .protected"); break;
	  default:
	    /* Some other non-defined flags are also present, so print
	       everything hex.  */
	    fprintf (file, " 0x%02x", (unsigned int) st_other);
	  }

	fprintf (file, " %s", name);
      }
      break;
    }
}

/* Create an entry in an ELF linker hash table.  */

struct bfd_hash_entry *
_bfd_elf_link_hash_newfunc (struct bfd_hash_entry *entry,
			    struct bfd_hash_table *table,
			    const char *string)
{
  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (entry == NULL)
    {
      entry = bfd_hash_allocate (table, sizeof (struct elf_link_hash_entry));
      if (entry == NULL)
	return entry;
    }

  /* Call the allocation method of the superclass.  */
  entry = _bfd_link_hash_newfunc (entry, table, string);
  if (entry != NULL)
    {
      struct elf_link_hash_entry *ret = (struct elf_link_hash_entry *) entry;
      struct elf_link_hash_table *htab = (struct elf_link_hash_table *) table;

      /* Set local fields.  */
      ret->indx = -1;
      ret->dynindx = -1;
      ret->got = htab->init_got_refcount;
      ret->plt = htab->init_plt_refcount;
      memset (&ret->size, 0, (sizeof (struct elf_link_hash_entry)
			      - offsetof (struct elf_link_hash_entry, size)));
      /* Assume that we have been called by a non-ELF symbol reader.
	 This flag is then reset by the code which reads an ELF input
	 file.  This ensures that a symbol created by a non-ELF symbol
	 reader will have the flag set correctly.  */
      ret->non_elf = 1;
    }

  return entry;
}

/* Copy data from an indirect symbol to its direct symbol, hiding the
   old indirect symbol.  Also used for copying flags to a weakdef.  */

void
_bfd_elf_link_hash_copy_indirect (struct bfd_link_info *info,
				  struct elf_link_hash_entry *dir,
				  struct elf_link_hash_entry *ind)
{
  struct elf_link_hash_table *htab;

  /* Copy down any references that we may have already seen to the
     symbol which just became indirect.  */

  dir->ref_dynamic |= ind->ref_dynamic;
  dir->ref_regular |= ind->ref_regular;
  dir->ref_regular_nonweak |= ind->ref_regular_nonweak;
  dir->non_got_ref |= ind->non_got_ref;
  dir->needs_plt |= ind->needs_plt;
  dir->pointer_equality_needed |= ind->pointer_equality_needed;

  if (ind->root.type != bfd_link_hash_indirect)
    return;

  /* Copy over the global and procedure linkage table refcount entries.
     These may have been already set up by a check_relocs routine.  */
  htab = elf_hash_table (info);
  if (ind->got.refcount > htab->init_got_refcount.refcount)
    {
      if (dir->got.refcount < 0)
	dir->got.refcount = 0;
      dir->got.refcount += ind->got.refcount;
      ind->got.refcount = htab->init_got_refcount.refcount;
    }

  if (ind->plt.refcount > htab->init_plt_refcount.refcount)
    {
      if (dir->plt.refcount < 0)
	dir->plt.refcount = 0;
      dir->plt.refcount += ind->plt.refcount;
      ind->plt.refcount = htab->init_plt_refcount.refcount;
    }

  if (ind->dynindx != -1)
    {
      if (dir->dynindx != -1)
	_bfd_elf_strtab_delref (htab->dynstr, dir->dynstr_index);
      dir->dynindx = ind->dynindx;
      dir->dynstr_index = ind->dynstr_index;
      ind->dynindx = -1;
      ind->dynstr_index = 0;
    }
}

void
_bfd_elf_link_hash_hide_symbol (struct bfd_link_info *info,
				struct elf_link_hash_entry *h,
				bfd_boolean force_local)
{
  h->plt = elf_hash_table (info)->init_plt_offset;
  h->needs_plt = 0;
  if (force_local)
    {
      h->forced_local = 1;
      if (h->dynindx != -1)
	{
	  h->dynindx = -1;
	  _bfd_elf_strtab_delref (elf_hash_table (info)->dynstr,
				  h->dynstr_index);
	}
    }
}

/* Initialize an ELF linker hash table.  */

bfd_boolean
_bfd_elf_link_hash_table_init
  (struct elf_link_hash_table *table,
   bfd *abfd,
   struct bfd_hash_entry *(*newfunc) (struct bfd_hash_entry *,
				      struct bfd_hash_table *,
				      const char *),
   unsigned int entsize)
{
  bfd_boolean ret;
  int can_refcount = get_elf_backend_data (abfd)->can_refcount;

  memset (table, 0, sizeof * table);
  table->init_got_refcount.refcount = can_refcount - 1;
  table->init_plt_refcount.refcount = can_refcount - 1;
  table->init_got_offset.offset = -(bfd_vma) 1;
  table->init_plt_offset.offset = -(bfd_vma) 1;
  /* The first dynamic symbol is a dummy.  */
  table->dynsymcount = 1;

  ret = _bfd_link_hash_table_init (&table->root, abfd, newfunc, entsize);
  table->root.type = bfd_link_elf_hash_table;

  return ret;
}

/* Create an ELF linker hash table.  */

struct bfd_link_hash_table *
_bfd_elf_link_hash_table_create (bfd *abfd)
{
  struct elf_link_hash_table *ret;
  bfd_size_type amt = sizeof (struct elf_link_hash_table);

  ret = bfd_malloc (amt);
  if (ret == NULL)
    return NULL;

  if (! _bfd_elf_link_hash_table_init (ret, abfd, _bfd_elf_link_hash_newfunc,
				       sizeof (struct elf_link_hash_entry)))
    {
      free (ret);
      return NULL;
    }

  return &ret->root;
}

/* This is a hook for the ELF emulation code in the generic linker to
   tell the backend linker what file name to use for the DT_NEEDED
   entry for a dynamic object.  */

void
bfd_elf_set_dt_needed_name (bfd *abfd, const char *name)
{
  if (bfd_get_flavour (abfd) == bfd_target_elf_flavour
      && bfd_get_format (abfd) == bfd_object)
    elf_dt_name (abfd) = name;
}

int
bfd_elf_get_dyn_lib_class (bfd *abfd)
{
  int lib_class;
  if (bfd_get_flavour (abfd) == bfd_target_elf_flavour
      && bfd_get_format (abfd) == bfd_object)
    lib_class = elf_dyn_lib_class (abfd);
  else
    lib_class = 0;
  return lib_class;
}

void
bfd_elf_set_dyn_lib_class (bfd *abfd, enum dynamic_lib_link_class lib_class)
{
  if (bfd_get_flavour (abfd) == bfd_target_elf_flavour
      && bfd_get_format (abfd) == bfd_object)
    elf_dyn_lib_class (abfd) = lib_class;
}

/* Get the list of DT_NEEDED entries for a link.  This is a hook for
   the linker ELF emulation code.  */

struct bfd_link_needed_list *
bfd_elf_get_needed_list (bfd *abfd ATTRIBUTE_UNUSED,
			 struct bfd_link_info *info)
{
  if (! is_elf_hash_table (info->hash))
    return NULL;
  return elf_hash_table (info)->needed;
}

/* Get the list of DT_RPATH/DT_RUNPATH entries for a link.  This is a
   hook for the linker ELF emulation code.  */

struct bfd_link_needed_list *
bfd_elf_get_runpath_list (bfd *abfd ATTRIBUTE_UNUSED,
			  struct bfd_link_info *info)
{
  if (! is_elf_hash_table (info->hash))
    return NULL;
  return elf_hash_table (info)->runpath;
}

/* Get the name actually used for a dynamic object for a link.  This
   is the SONAME entry if there is one.  Otherwise, it is the string
   passed to bfd_elf_set_dt_needed_name, or it is the filename.  */

const char *
bfd_elf_get_dt_soname (bfd *abfd)
{
  if (bfd_get_flavour (abfd) == bfd_target_elf_flavour
      && bfd_get_format (abfd) == bfd_object)
    return elf_dt_name (abfd);
  return NULL;
}

/* Get the list of DT_NEEDED entries from a BFD.  This is a hook for
   the ELF linker emulation code.  */

bfd_boolean
bfd_elf_get_bfd_needed_list (bfd *abfd,
			     struct bfd_link_needed_list **pneeded)
{
  asection *s;
  bfd_byte *dynbuf = NULL;
  int elfsec;
  unsigned long shlink;
  bfd_byte *extdyn, *extdynend;
  size_t extdynsize;
  void (*swap_dyn_in) (bfd *, const void *, Elf_Internal_Dyn *);

  *pneeded = NULL;

  if (bfd_get_flavour (abfd) != bfd_target_elf_flavour
      || bfd_get_format (abfd) != bfd_object)
    return TRUE;

  s = bfd_get_section_by_name (abfd, ".dynamic");
  if (s == NULL || s->size == 0)
    return TRUE;

  if (!bfd_malloc_and_get_section (abfd, s, &dynbuf))
    goto error_return;

  elfsec = _bfd_elf_section_from_bfd_section (abfd, s);
  if (elfsec == -1)
    goto error_return;

  shlink = elf_elfsections (abfd)[elfsec]->sh_link;

  extdynsize = get_elf_backend_data (abfd)->s->sizeof_dyn;
  swap_dyn_in = get_elf_backend_data (abfd)->s->swap_dyn_in;

  extdyn = dynbuf;
  extdynend = extdyn + s->size;
  for (; extdyn < extdynend; extdyn += extdynsize)
    {
      Elf_Internal_Dyn dyn;

      (*swap_dyn_in) (abfd, extdyn, &dyn);

      if (dyn.d_tag == DT_NULL)
	break;

      if (dyn.d_tag == DT_NEEDED)
	{
	  const char *string;
	  struct bfd_link_needed_list *l;
	  unsigned int tagv = dyn.d_un.d_val;
	  bfd_size_type amt;

	  string = bfd_elf_string_from_elf_section (abfd, shlink, tagv);
	  if (string == NULL)
	    goto error_return;

	  amt = sizeof *l;
	  l = bfd_alloc (abfd, amt);
	  if (l == NULL)
	    goto error_return;

	  l->by = abfd;
	  l->name = string;
	  l->next = *pneeded;
	  *pneeded = l;
	}
    }

  free (dynbuf);

  return TRUE;

 error_return:
  if (dynbuf != NULL)
    free (dynbuf);
  return FALSE;
}

/* Allocate an ELF string table--force the first byte to be zero.  */

struct bfd_strtab_hash *
_bfd_elf_stringtab_init (void)
{
  struct bfd_strtab_hash *ret;

  ret = _bfd_stringtab_init ();
  if (ret != NULL)
    {
      bfd_size_type loc;

      loc = _bfd_stringtab_add (ret, "", TRUE, FALSE);
      BFD_ASSERT (loc == 0 || loc == (bfd_size_type) -1);
      if (loc == (bfd_size_type) -1)
	{
	  _bfd_stringtab_free (ret);
	  ret = NULL;
	}
    }
  return ret;
}

/* ELF .o/exec file reading */

/* Create a new bfd section from an ELF section header.  */

bfd_boolean
bfd_section_from_shdr (bfd *abfd, unsigned int shindex)
{
  Elf_Internal_Shdr *hdr = elf_elfsections (abfd)[shindex];
  Elf_Internal_Ehdr *ehdr = elf_elfheader (abfd);
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  const char *name;

  name = bfd_elf_string_from_elf_section (abfd,
					  elf_elfheader (abfd)->e_shstrndx,
					  hdr->sh_name);
  if (name == NULL)
    return FALSE;

  switch (hdr->sh_type)
    {
    case SHT_NULL:
      /* Inactive section. Throw it away.  */
      return TRUE;

    case SHT_PROGBITS:	/* Normal section with contents.  */
    case SHT_NOBITS:	/* .bss section.  */
    case SHT_HASH:	/* .hash section.  */
    case SHT_NOTE:	/* .note section.  */
    case SHT_INIT_ARRAY:	/* .init_array section.  */
    case SHT_FINI_ARRAY:	/* .fini_array section.  */
    case SHT_PREINIT_ARRAY:	/* .preinit_array section.  */
    case SHT_GNU_LIBLIST:	/* .gnu.liblist section.  */
    case SHT_GNU_HASH:		/* .gnu.hash section.  */
      return _bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex);

    case SHT_DYNAMIC:	/* Dynamic linking information.  */
      if (! _bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex))
	return FALSE;
      if (hdr->sh_link > elf_numsections (abfd)
	  || elf_elfsections (abfd)[hdr->sh_link] == NULL)
	return FALSE;
      if (elf_elfsections (abfd)[hdr->sh_link]->sh_type != SHT_STRTAB)
	{
	  Elf_Internal_Shdr *dynsymhdr;

	  /* The shared libraries distributed with hpux11 have a bogus
	     sh_link field for the ".dynamic" section.  Find the
	     string table for the ".dynsym" section instead.  */
	  if (elf_dynsymtab (abfd) != 0)
	    {
	      dynsymhdr = elf_elfsections (abfd)[elf_dynsymtab (abfd)];
	      hdr->sh_link = dynsymhdr->sh_link;
	    }
	  else
	    {
	      unsigned int i, num_sec;

	      num_sec = elf_numsections (abfd);
	      for (i = 1; i < num_sec; i++)
		{
		  dynsymhdr = elf_elfsections (abfd)[i];
		  if (dynsymhdr->sh_type == SHT_DYNSYM)
		    {
		      hdr->sh_link = dynsymhdr->sh_link;
		      break;
		    }
		}
	    }
	}
      break;

    case SHT_SYMTAB:		/* A symbol table */
      if (elf_onesymtab (abfd) == shindex)
	return TRUE;

      if (hdr->sh_entsize != bed->s->sizeof_sym)
	return FALSE;
      BFD_ASSERT (elf_onesymtab (abfd) == 0);
      elf_onesymtab (abfd) = shindex;
      elf_tdata (abfd)->symtab_hdr = *hdr;
      elf_elfsections (abfd)[shindex] = hdr = &elf_tdata (abfd)->symtab_hdr;
      abfd->flags |= HAS_SYMS;

      /* Sometimes a shared object will map in the symbol table.  If
	 SHF_ALLOC is set, and this is a shared object, then we also
	 treat this section as a BFD section.  We can not base the
	 decision purely on SHF_ALLOC, because that flag is sometimes
	 set in a relocatable object file, which would confuse the
	 linker.  */
      if ((hdr->sh_flags & SHF_ALLOC) != 0
	  && (abfd->flags & DYNAMIC) != 0
	  && ! _bfd_elf_make_section_from_shdr (abfd, hdr, name,
						shindex))
	return FALSE;

      /* Go looking for SHT_SYMTAB_SHNDX too, since if there is one we
	 can't read symbols without that section loaded as well.  It
	 is most likely specified by the next section header.  */
      if (elf_elfsections (abfd)[elf_symtab_shndx (abfd)]->sh_link != shindex)
	{
	  unsigned int i, num_sec;

	  num_sec = elf_numsections (abfd);
	  for (i = shindex + 1; i < num_sec; i++)
	    {
	      Elf_Internal_Shdr *hdr2 = elf_elfsections (abfd)[i];
	      if (hdr2->sh_type == SHT_SYMTAB_SHNDX
		  && hdr2->sh_link == shindex)
		break;
	    }
	  if (i == num_sec)
	    for (i = 1; i < shindex; i++)
	      {
		Elf_Internal_Shdr *hdr2 = elf_elfsections (abfd)[i];
		if (hdr2->sh_type == SHT_SYMTAB_SHNDX
		    && hdr2->sh_link == shindex)
		  break;
	      }
	  if (i != shindex)
	    return bfd_section_from_shdr (abfd, i);
	}
      return TRUE;

    case SHT_DYNSYM:		/* A dynamic symbol table */
      if (elf_dynsymtab (abfd) == shindex)
	return TRUE;

      if (hdr->sh_entsize != bed->s->sizeof_sym)
	return FALSE;
      BFD_ASSERT (elf_dynsymtab (abfd) == 0);
      elf_dynsymtab (abfd) = shindex;
      elf_tdata (abfd)->dynsymtab_hdr = *hdr;
      elf_elfsections (abfd)[shindex] = hdr = &elf_tdata (abfd)->dynsymtab_hdr;
      abfd->flags |= HAS_SYMS;

      /* Besides being a symbol table, we also treat this as a regular
	 section, so that objcopy can handle it.  */
      return _bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex);

    case SHT_SYMTAB_SHNDX:	/* Symbol section indices when >64k sections */
      if (elf_symtab_shndx (abfd) == shindex)
	return TRUE;

      BFD_ASSERT (elf_symtab_shndx (abfd) == 0);
      elf_symtab_shndx (abfd) = shindex;
      elf_tdata (abfd)->symtab_shndx_hdr = *hdr;
      elf_elfsections (abfd)[shindex] = &elf_tdata (abfd)->symtab_shndx_hdr;
      return TRUE;

    case SHT_STRTAB:		/* A string table */
      if (hdr->bfd_section != NULL)
	return TRUE;
      if (ehdr->e_shstrndx == shindex)
	{
	  elf_tdata (abfd)->shstrtab_hdr = *hdr;
	  elf_elfsections (abfd)[shindex] = &elf_tdata (abfd)->shstrtab_hdr;
	  return TRUE;
	}
      if (elf_elfsections (abfd)[elf_onesymtab (abfd)]->sh_link == shindex)
	{
	symtab_strtab:
	  elf_tdata (abfd)->strtab_hdr = *hdr;
	  elf_elfsections (abfd)[shindex] = &elf_tdata (abfd)->strtab_hdr;
	  return TRUE;
	}
      if (elf_elfsections (abfd)[elf_dynsymtab (abfd)]->sh_link == shindex)
	{
	dynsymtab_strtab:
	  elf_tdata (abfd)->dynstrtab_hdr = *hdr;
	  hdr = &elf_tdata (abfd)->dynstrtab_hdr;
	  elf_elfsections (abfd)[shindex] = hdr;
	  /* We also treat this as a regular section, so that objcopy
	     can handle it.  */
	  return _bfd_elf_make_section_from_shdr (abfd, hdr, name,
						  shindex);
	}

      /* If the string table isn't one of the above, then treat it as a
	 regular section.  We need to scan all the headers to be sure,
	 just in case this strtab section appeared before the above.  */
      if (elf_onesymtab (abfd) == 0 || elf_dynsymtab (abfd) == 0)
	{
	  unsigned int i, num_sec;

	  num_sec = elf_numsections (abfd);
	  for (i = 1; i < num_sec; i++)
	    {
	      Elf_Internal_Shdr *hdr2 = elf_elfsections (abfd)[i];
	      if (hdr2->sh_link == shindex)
		{
		  /* Prevent endless recursion on broken objects.  */
		  if (i == shindex)
		    return FALSE;
		  if (! bfd_section_from_shdr (abfd, i))
		    return FALSE;
		  if (elf_onesymtab (abfd) == i)
		    goto symtab_strtab;
		  if (elf_dynsymtab (abfd) == i)
		    goto dynsymtab_strtab;
		}
	    }
	}
      return _bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex);

    case SHT_REL:
    case SHT_RELA:
      /* *These* do a lot of work -- but build no sections!  */
      {
	asection *target_sect;
	Elf_Internal_Shdr *hdr2;
	unsigned int num_sec = elf_numsections (abfd);

	if (hdr->sh_entsize
	    != (bfd_size_type) (hdr->sh_type == SHT_REL
				? bed->s->sizeof_rel : bed->s->sizeof_rela))
	  return FALSE;

	/* Check for a bogus link to avoid crashing.  */
	if ((hdr->sh_link >= SHN_LORESERVE && hdr->sh_link <= SHN_HIRESERVE)
	    || hdr->sh_link >= num_sec)
	  {
	    ((*_bfd_error_handler)
	     (_("%B: invalid link %lu for reloc section %s (index %u)"),
	      abfd, hdr->sh_link, name, shindex));
	    return _bfd_elf_make_section_from_shdr (abfd, hdr, name,
						    shindex);
	  }

	/* For some incomprehensible reason Oracle distributes
	   libraries for Solaris in which some of the objects have
	   bogus sh_link fields.  It would be nice if we could just
	   reject them, but, unfortunately, some people need to use
	   them.  We scan through the section headers; if we find only
	   one suitable symbol table, we clobber the sh_link to point
	   to it.  I hope this doesn't break anything.  */
	if (elf_elfsections (abfd)[hdr->sh_link]->sh_type != SHT_SYMTAB
	    && elf_elfsections (abfd)[hdr->sh_link]->sh_type != SHT_DYNSYM)
	  {
	    unsigned int scan;
	    int found;

	    found = 0;
	    for (scan = 1; scan < num_sec; scan++)
	      {
		if (elf_elfsections (abfd)[scan]->sh_type == SHT_SYMTAB
		    || elf_elfsections (abfd)[scan]->sh_type == SHT_DYNSYM)
		  {
		    if (found != 0)
		      {
			found = 0;
			break;
		      }
		    found = scan;
		  }
	      }
	    if (found != 0)
	      hdr->sh_link = found;
	  }

	/* Get the symbol table.  */
	if ((elf_elfsections (abfd)[hdr->sh_link]->sh_type == SHT_SYMTAB
	     || elf_elfsections (abfd)[hdr->sh_link]->sh_type == SHT_DYNSYM)
	    && ! bfd_section_from_shdr (abfd, hdr->sh_link))
	  return FALSE;

	/* If this reloc section does not use the main symbol table we
	   don't treat it as a reloc section.  BFD can't adequately
	   represent such a section, so at least for now, we don't
	   try.  We just present it as a normal section.  We also
	   can't use it as a reloc section if it points to the null
	   section, an invalid section, or another reloc section.  */
	if (hdr->sh_link != elf_onesymtab (abfd)
	    || hdr->sh_info == SHN_UNDEF
	    || (hdr->sh_info >= SHN_LORESERVE && hdr->sh_info <= SHN_HIRESERVE)
	    || hdr->sh_info >= num_sec
	    || elf_elfsections (abfd)[hdr->sh_info]->sh_type == SHT_REL
	    || elf_elfsections (abfd)[hdr->sh_info]->sh_type == SHT_RELA)
	  return _bfd_elf_make_section_from_shdr (abfd, hdr, name,
						  shindex);

	if (! bfd_section_from_shdr (abfd, hdr->sh_info))
	  return FALSE;
	target_sect = bfd_section_from_elf_index (abfd, hdr->sh_info);
	if (target_sect == NULL)
	  return FALSE;

	if ((target_sect->flags & SEC_RELOC) == 0
	    || target_sect->reloc_count == 0)
	  hdr2 = &elf_section_data (target_sect)->rel_hdr;
	else
	  {
	    bfd_size_type amt;
	    BFD_ASSERT (elf_section_data (target_sect)->rel_hdr2 == NULL);
	    amt = sizeof (*hdr2);
	    hdr2 = bfd_alloc (abfd, amt);
	    elf_section_data (target_sect)->rel_hdr2 = hdr2;
	  }
	*hdr2 = *hdr;
	elf_elfsections (abfd)[shindex] = hdr2;
	target_sect->reloc_count += NUM_SHDR_ENTRIES (hdr);
	target_sect->flags |= SEC_RELOC;
	target_sect->relocation = NULL;
	target_sect->rel_filepos = hdr->sh_offset;
	/* In the section to which the relocations apply, mark whether
	   its relocations are of the REL or RELA variety.  */
	if (hdr->sh_size != 0)
	  target_sect->use_rela_p = hdr->sh_type == SHT_RELA;
	abfd->flags |= HAS_RELOC;
	return TRUE;
      }

    case SHT_GNU_verdef:
      elf_dynverdef (abfd) = shindex;
      elf_tdata (abfd)->dynverdef_hdr = *hdr;
      return _bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex);

    case SHT_GNU_versym:
      if (hdr->sh_entsize != sizeof (Elf_External_Versym))
	return FALSE;
      elf_dynversym (abfd) = shindex;
      elf_tdata (abfd)->dynversym_hdr = *hdr;
      return _bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex);

    case SHT_GNU_verneed:
      elf_dynverref (abfd) = shindex;
      elf_tdata (abfd)->dynverref_hdr = *hdr;
      return _bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex);

    case SHT_SHLIB:
      return TRUE;

    case SHT_GROUP:
      /* We need a BFD section for objcopy and relocatable linking,
	 and it's handy to have the signature available as the section
	 name.  */
      if (! IS_VALID_GROUP_SECTION_HEADER (hdr))
	return FALSE;
      name = group_signature (abfd, hdr);
      if (name == NULL)
	return FALSE;
      if (!_bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex))
	return FALSE;
      if (hdr->contents != NULL)
	{
	  Elf_Internal_Group *idx = (Elf_Internal_Group *) hdr->contents;
	  unsigned int n_elt = hdr->sh_size / GRP_ENTRY_SIZE;
	  asection *s;

	  if (idx->flags & GRP_COMDAT)
	    hdr->bfd_section->flags
	      |= SEC_LINK_ONCE | SEC_LINK_DUPLICATES_DISCARD;

	  /* We try to keep the same section order as it comes in.  */
	  idx += n_elt;
	  while (--n_elt != 0)
	    {
	      --idx;

	      if (idx->shdr != NULL
		  && (s = idx->shdr->bfd_section) != NULL
		  && elf_next_in_group (s) != NULL)
		{
		  elf_next_in_group (hdr->bfd_section) = s;
		  break;
		}
	    }
	}
      break;

    default:
      /* Possibly an attributes section.  */
      if (hdr->sh_type == SHT_GNU_ATTRIBUTES
	  || hdr->sh_type == bed->obj_attrs_section_type)
	{
	  if (! _bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex))
	    return FALSE;
	  _bfd_elf_parse_attributes (abfd, hdr);
	  return TRUE;
	}

      /* Check for any processor-specific section types.  */
      if (bed->elf_backend_section_from_shdr (abfd, hdr, name, shindex))
	return TRUE;

      if (hdr->sh_type >= SHT_LOUSER && hdr->sh_type <= SHT_HIUSER)
	{
	  if ((hdr->sh_flags & SHF_ALLOC) != 0)
	    /* FIXME: How to properly handle allocated section reserved
	       for applications?  */
	    (*_bfd_error_handler)
	      (_("%B: don't know how to handle allocated, application "
		 "specific section `%s' [0x%8x]"),
	       abfd, name, hdr->sh_type);
	  else
	    /* Allow sections reserved for applications.  */
	    return _bfd_elf_make_section_from_shdr (abfd, hdr, name,
						    shindex);
	}
      else if (hdr->sh_type >= SHT_LOPROC
	       && hdr->sh_type <= SHT_HIPROC)
	/* FIXME: We should handle this section.  */
	(*_bfd_error_handler)
	  (_("%B: don't know how to handle processor specific section "
	     "`%s' [0x%8x]"),
	   abfd, name, hdr->sh_type);
      else if (hdr->sh_type >= SHT_LOOS && hdr->sh_type <= SHT_HIOS)
	{
	  /* Unrecognised OS-specific sections.  */
	  if ((hdr->sh_flags & SHF_OS_NONCONFORMING) != 0)
	    /* SHF_OS_NONCONFORMING indicates that special knowledge is
	       required to correctly process the section and the file should
	       be rejected with an error message.  */
	    (*_bfd_error_handler)
	      (_("%B: don't know how to handle OS specific section "
		 "`%s' [0x%8x]"),
	       abfd, name, hdr->sh_type);
	  else
	    /* Otherwise it should be processed.  */
	    return _bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex);
	}
      else
	/* FIXME: We should handle this section.  */
	(*_bfd_error_handler)
	  (_("%B: don't know how to handle section `%s' [0x%8x]"),
	   abfd, name, hdr->sh_type);

      return FALSE;
    }

  return TRUE;
}

/* Return the section for the local symbol specified by ABFD, R_SYMNDX.
   Return SEC for sections that have no elf section, and NULL on error.  */

asection *
bfd_section_from_r_symndx (bfd *abfd,
			   struct sym_sec_cache *cache,
			   asection *sec,
			   unsigned long r_symndx)
{
  Elf_Internal_Shdr *symtab_hdr;
  unsigned char esym[sizeof (Elf64_External_Sym)];
  Elf_External_Sym_Shndx eshndx;
  Elf_Internal_Sym isym;
  unsigned int ent = r_symndx % LOCAL_SYM_CACHE_SIZE;

  if (cache->abfd == abfd && cache->indx[ent] == r_symndx)
    return cache->sec[ent];

  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  if (bfd_elf_get_elf_syms (abfd, symtab_hdr, 1, r_symndx,
			    &isym, esym, &eshndx) == NULL)
    return NULL;

  if (cache->abfd != abfd)
    {
      memset (cache->indx, -1, sizeof (cache->indx));
      cache->abfd = abfd;
    }
  cache->indx[ent] = r_symndx;
  cache->sec[ent] = sec;
  if ((isym.st_shndx != SHN_UNDEF && isym.st_shndx < SHN_LORESERVE)
      || isym.st_shndx > SHN_HIRESERVE)
    {
      asection *s;
      s = bfd_section_from_elf_index (abfd, isym.st_shndx);
      if (s != NULL)
	cache->sec[ent] = s;
    }
  return cache->sec[ent];
}

/* Given an ELF section number, retrieve the corresponding BFD
   section.  */

asection *
bfd_section_from_elf_index (bfd *abfd, unsigned int index)
{
  if (index >= elf_numsections (abfd))
    return NULL;
  return elf_elfsections (abfd)[index]->bfd_section;
}

static const struct bfd_elf_special_section special_sections_b[] =
{
  { STRING_COMMA_LEN (".bss"), -2, SHT_NOBITS,   SHF_ALLOC + SHF_WRITE },
  { NULL,                   0,  0, 0,            0 }
};

static const struct bfd_elf_special_section special_sections_c[] =
{
  { STRING_COMMA_LEN (".comment"), 0, SHT_PROGBITS, 0 },
  { NULL,                       0, 0, 0,            0 }
};

static const struct bfd_elf_special_section special_sections_d[] =
{
  { STRING_COMMA_LEN (".data"),         -2, SHT_PROGBITS, SHF_ALLOC + SHF_WRITE },
  { STRING_COMMA_LEN (".data1"),         0, SHT_PROGBITS, SHF_ALLOC + SHF_WRITE },
  { STRING_COMMA_LEN (".debug"),         0, SHT_PROGBITS, 0 },
  { STRING_COMMA_LEN (".debug_line"),    0, SHT_PROGBITS, 0 },
  { STRING_COMMA_LEN (".debug_info"),    0, SHT_PROGBITS, 0 },
  { STRING_COMMA_LEN (".debug_abbrev"),  0, SHT_PROGBITS, 0 },
  { STRING_COMMA_LEN (".debug_aranges"), 0, SHT_PROGBITS, 0 },
  { STRING_COMMA_LEN (".dynamic"),       0, SHT_DYNAMIC,  SHF_ALLOC },
  { STRING_COMMA_LEN (".dynstr"),        0, SHT_STRTAB,   SHF_ALLOC },
  { STRING_COMMA_LEN (".dynsym"),        0, SHT_DYNSYM,   SHF_ALLOC },
  { NULL,                      0,        0, 0,            0 }
};

static const struct bfd_elf_special_section special_sections_f[] =
{
  { STRING_COMMA_LEN (".fini"),       0, SHT_PROGBITS,   SHF_ALLOC + SHF_EXECINSTR },
  { STRING_COMMA_LEN (".fini_array"), 0, SHT_FINI_ARRAY, SHF_ALLOC + SHF_WRITE },
  { NULL,                          0, 0, 0,              0 }
};

static const struct bfd_elf_special_section special_sections_g[] =
{
  { STRING_COMMA_LEN (".gnu.linkonce.b"), -2, SHT_NOBITS,      SHF_ALLOC + SHF_WRITE },
  { STRING_COMMA_LEN (".got"),             0, SHT_PROGBITS,    SHF_ALLOC + SHF_WRITE },
  { STRING_COMMA_LEN (".gnu.version"),     0, SHT_GNU_versym,  0 },
  { STRING_COMMA_LEN (".gnu.version_d"),   0, SHT_GNU_verdef,  0 },
  { STRING_COMMA_LEN (".gnu.version_r"),   0, SHT_GNU_verneed, 0 },
  { STRING_COMMA_LEN (".gnu.liblist"),     0, SHT_GNU_LIBLIST, SHF_ALLOC },
  { STRING_COMMA_LEN (".gnu.conflict"),    0, SHT_RELA,        SHF_ALLOC },
  { STRING_COMMA_LEN (".gnu.hash"),        0, SHT_GNU_HASH,    SHF_ALLOC },
  { NULL,                        0,        0, 0,               0 }
};

static const struct bfd_elf_special_section special_sections_h[] =
{
  { STRING_COMMA_LEN (".hash"), 0, SHT_HASH,     SHF_ALLOC },
  { NULL,                    0, 0, 0,            0 }
};

static const struct bfd_elf_special_section special_sections_i[] =
{
  { STRING_COMMA_LEN (".init"),       0, SHT_PROGBITS,   SHF_ALLOC + SHF_EXECINSTR },
  { STRING_COMMA_LEN (".init_array"), 0, SHT_INIT_ARRAY, SHF_ALLOC + SHF_WRITE },
  { STRING_COMMA_LEN (".interp"),     0, SHT_PROGBITS,   0 },
  { NULL,                      0,     0, 0,              0 }
};

static const struct bfd_elf_special_section special_sections_l[] =
{
  { STRING_COMMA_LEN (".line"), 0, SHT_PROGBITS, 0 },
  { NULL,                    0, 0, 0,            0 }
};

static const struct bfd_elf_special_section special_sections_n[] =
{
  { STRING_COMMA_LEN (".note.GNU-stack"), 0, SHT_PROGBITS, 0 },
  { STRING_COMMA_LEN (".note"),          -1, SHT_NOTE,     0 },
  { NULL,                    0,           0, 0,            0 }
};

static const struct bfd_elf_special_section special_sections_p[] =
{
  { STRING_COMMA_LEN (".preinit_array"), 0, SHT_PREINIT_ARRAY, SHF_ALLOC + SHF_WRITE },
  { STRING_COMMA_LEN (".plt"),           0, SHT_PROGBITS,      SHF_ALLOC + SHF_EXECINSTR },
  { NULL,                   0,           0, 0,                 0 }
};

static const struct bfd_elf_special_section special_sections_r[] =
{
  { STRING_COMMA_LEN (".rodata"), -2, SHT_PROGBITS, SHF_ALLOC },
  { STRING_COMMA_LEN (".rodata1"), 0, SHT_PROGBITS, SHF_ALLOC },
  { STRING_COMMA_LEN (".rela"),   -1, SHT_RELA,     0 },
  { STRING_COMMA_LEN (".rel"),    -1, SHT_REL,      0 },
  { NULL,                   0,     0, 0,            0 }
};

static const struct bfd_elf_special_section special_sections_s[] =
{
  { STRING_COMMA_LEN (".shstrtab"), 0, SHT_STRTAB, 0 },
  { STRING_COMMA_LEN (".strtab"),   0, SHT_STRTAB, 0 },
  { STRING_COMMA_LEN (".symtab"),   0, SHT_SYMTAB, 0 },
  /* See struct bfd_elf_special_section declaration for the semantics of
     this special case where .prefix_length != strlen (.prefix).  */
  { ".stabstr",			5,  3, SHT_STRTAB, 0 },
  { NULL,                       0,  0, 0,          0 }
};

static const struct bfd_elf_special_section special_sections_t[] =
{
  { STRING_COMMA_LEN (".text"),  -2, SHT_PROGBITS, SHF_ALLOC + SHF_EXECINSTR },
  { STRING_COMMA_LEN (".tbss"),  -2, SHT_NOBITS,   SHF_ALLOC + SHF_WRITE + SHF_TLS },
  { STRING_COMMA_LEN (".tdata"), -2, SHT_PROGBITS, SHF_ALLOC + SHF_WRITE + SHF_TLS },
  { NULL,                     0,  0, 0,            0 }
};

static const struct bfd_elf_special_section *special_sections[] =
{
  special_sections_b,		/* 'b' */
  special_sections_c,		/* 'b' */
  special_sections_d,		/* 'd' */
  NULL,				/* 'e' */
  special_sections_f,		/* 'f' */
  special_sections_g,		/* 'g' */
  special_sections_h,		/* 'h' */
  special_sections_i,		/* 'i' */
  NULL,				/* 'j' */
  NULL,				/* 'k' */
  special_sections_l,		/* 'l' */
  NULL,				/* 'm' */
  special_sections_n,		/* 'n' */
  NULL,				/* 'o' */
  special_sections_p,		/* 'p' */
  NULL,				/* 'q' */
  special_sections_r,		/* 'r' */
  special_sections_s,		/* 's' */
  special_sections_t,		/* 't' */
};

const struct bfd_elf_special_section *
_bfd_elf_get_special_section (const char *name,
			      const struct bfd_elf_special_section *spec,
			      unsigned int rela)
{
  int i;
  int len;

  len = strlen (name);

  for (i = 0; spec[i].prefix != NULL; i++)
    {
      int suffix_len;
      int prefix_len = spec[i].prefix_length;

      if (len < prefix_len)
	continue;
      if (memcmp (name, spec[i].prefix, prefix_len) != 0)
	continue;

      suffix_len = spec[i].suffix_length;
      if (suffix_len <= 0)
	{
	  if (name[prefix_len] != 0)
	    {
	      if (suffix_len == 0)
		continue;
	      if (name[prefix_len] != '.'
		  && (suffix_len == -2
		      || (rela && spec[i].type == SHT_REL)))
		continue;
	    }
	}
      else
	{
	  if (len < prefix_len + suffix_len)
	    continue;
	  if (memcmp (name + len - suffix_len,
		      spec[i].prefix + prefix_len,
		      suffix_len) != 0)
	    continue;
	}
      return &spec[i];
    }

  return NULL;
}

const struct bfd_elf_special_section *
_bfd_elf_get_sec_type_attr (bfd *abfd, asection *sec)
{
  int i;
  const struct bfd_elf_special_section *spec;
  const struct elf_backend_data *bed;

  /* See if this is one of the special sections.  */
  if (sec->name == NULL)
    return NULL;

  bed = get_elf_backend_data (abfd);
  spec = bed->special_sections;
  if (spec)
    {
      spec = _bfd_elf_get_special_section (sec->name,
					   bed->special_sections,
					   sec->use_rela_p);
      if (spec != NULL)
	return spec;
    }

  if (sec->name[0] != '.')
    return NULL;

  i = sec->name[1] - 'b';
  if (i < 0 || i > 't' - 'b')
    return NULL;

  spec = special_sections[i];

  if (spec == NULL)
    return NULL;

  return _bfd_elf_get_special_section (sec->name, spec, sec->use_rela_p);
}

bfd_boolean
_bfd_elf_new_section_hook (bfd *abfd, asection *sec)
{
  struct bfd_elf_section_data *sdata;
  const struct elf_backend_data *bed;
  const struct bfd_elf_special_section *ssect;

  sdata = (struct bfd_elf_section_data *) sec->used_by_bfd;
  if (sdata == NULL)
    {
      sdata = bfd_zalloc (abfd, sizeof (*sdata));
      if (sdata == NULL)
	return FALSE;
      sec->used_by_bfd = sdata;
    }

  /* Indicate whether or not this section should use RELA relocations.  */
  bed = get_elf_backend_data (abfd);
  sec->use_rela_p = bed->default_use_rela_p;

  /* When we read a file, we don't need to set ELF section type and
     flags.  They will be overridden in _bfd_elf_make_section_from_shdr
     anyway.  We will set ELF section type and flags for all linker
     created sections.  If user specifies BFD section flags, we will
     set ELF section type and flags based on BFD section flags in
     elf_fake_sections.  */
  if ((!sec->flags && abfd->direction != read_direction)
      || (sec->flags & SEC_LINKER_CREATED) != 0)
    {
      ssect = (*bed->get_sec_type_attr) (abfd, sec);
      if (ssect != NULL)
	{
	  elf_section_type (sec) = ssect->type;
	  elf_section_flags (sec) = ssect->attr;
	}
    }

  return _bfd_generic_new_section_hook (abfd, sec);
}

/* Create a new bfd section from an ELF program header.

   Since program segments have no names, we generate a synthetic name
   of the form segment<NUM>, where NUM is generally the index in the
   program header table.  For segments that are split (see below) we
   generate the names segment<NUM>a and segment<NUM>b.

   Note that some program segments may have a file size that is different than
   (less than) the memory size.  All this means is that at execution the
   system must allocate the amount of memory specified by the memory size,
   but only initialize it with the first "file size" bytes read from the
   file.  This would occur for example, with program segments consisting
   of combined data+bss.

   To handle the above situation, this routine generates TWO bfd sections
   for the single program segment.  The first has the length specified by
   the file size of the segment, and the second has the length specified
   by the difference between the two sizes.  In effect, the segment is split
   into it's initialized and uninitialized parts.

 */

bfd_boolean
_bfd_elf_make_section_from_phdr (bfd *abfd,
				 Elf_Internal_Phdr *hdr,
				 int index,
				 const char *typename)
{
  asection *newsect;
  char *name;
  char namebuf[64];
  size_t len;
  int split;

  split = ((hdr->p_memsz > 0)
	    && (hdr->p_filesz > 0)
	    && (hdr->p_memsz > hdr->p_filesz));
  sprintf (namebuf, "%s%d%s", typename, index, split ? "a" : "");
  len = strlen (namebuf) + 1;
  name = bfd_alloc (abfd, len);
  if (!name)
    return FALSE;
  memcpy (name, namebuf, len);
  newsect = bfd_make_section (abfd, name);
  if (newsect == NULL)
    return FALSE;
  newsect->vma = hdr->p_vaddr;
  newsect->lma = hdr->p_paddr;
  newsect->size = hdr->p_filesz;
  newsect->filepos = hdr->p_offset;
  newsect->flags |= SEC_HAS_CONTENTS;
  newsect->alignment_power = bfd_log2 (hdr->p_align);
  if (hdr->p_type == PT_LOAD)
    {
      newsect->flags |= SEC_ALLOC;
      newsect->flags |= SEC_LOAD;
      if (hdr->p_flags & PF_X)
	{
	  /* FIXME: all we known is that it has execute PERMISSION,
	     may be data.  */
	  newsect->flags |= SEC_CODE;
	}
    }
  if (!(hdr->p_flags & PF_W))
    {
      newsect->flags |= SEC_READONLY;
    }

  if (split)
    {
      sprintf (namebuf, "%s%db", typename, index);
      len = strlen (namebuf) + 1;
      name = bfd_alloc (abfd, len);
      if (!name)
	return FALSE;
      memcpy (name, namebuf, len);
      newsect = bfd_make_section (abfd, name);
      if (newsect == NULL)
	return FALSE;
      newsect->vma = hdr->p_vaddr + hdr->p_filesz;
      newsect->lma = hdr->p_paddr + hdr->p_filesz;
      newsect->size = hdr->p_memsz - hdr->p_filesz;
      if (hdr->p_type == PT_LOAD)
	{
	  newsect->flags |= SEC_ALLOC;
	  if (hdr->p_flags & PF_X)
	    newsect->flags |= SEC_CODE;
	}
      if (!(hdr->p_flags & PF_W))
	newsect->flags |= SEC_READONLY;
    }

  return TRUE;
}

bfd_boolean
bfd_section_from_phdr (bfd *abfd, Elf_Internal_Phdr *hdr, int index)
{
  const struct elf_backend_data *bed;

  switch (hdr->p_type)
    {
    case PT_NULL:
      return _bfd_elf_make_section_from_phdr (abfd, hdr, index, "null");

    case PT_LOAD:
      return _bfd_elf_make_section_from_phdr (abfd, hdr, index, "load");

    case PT_DYNAMIC:
      return _bfd_elf_make_section_from_phdr (abfd, hdr, index, "dynamic");

    case PT_INTERP:
      return _bfd_elf_make_section_from_phdr (abfd, hdr, index, "interp");

    case PT_NOTE:
      if (! _bfd_elf_make_section_from_phdr (abfd, hdr, index, "note"))
	return FALSE;
      if (! elfcore_read_notes (abfd, hdr->p_offset, hdr->p_filesz))
	return FALSE;
      return TRUE;

    case PT_SHLIB:
      return _bfd_elf_make_section_from_phdr (abfd, hdr, index, "shlib");

    case PT_PHDR:
      return _bfd_elf_make_section_from_phdr (abfd, hdr, index, "phdr");

    case PT_GNU_EH_FRAME:
      return _bfd_elf_make_section_from_phdr (abfd, hdr, index,
					      "eh_frame_hdr");

    case PT_GNU_STACK:
      return _bfd_elf_make_section_from_phdr (abfd, hdr, index, "stack");

    case PT_GNU_RELRO:
      return _bfd_elf_make_section_from_phdr (abfd, hdr, index, "relro");

    default:
      /* Check for any processor-specific program segment types.  */
      bed = get_elf_backend_data (abfd);
      return bed->elf_backend_section_from_phdr (abfd, hdr, index, "proc");
    }
}

/* Initialize REL_HDR, the section-header for new section, containing
   relocations against ASECT.  If USE_RELA_P is TRUE, we use RELA
   relocations; otherwise, we use REL relocations.  */

bfd_boolean
_bfd_elf_init_reloc_shdr (bfd *abfd,
			  Elf_Internal_Shdr *rel_hdr,
			  asection *asect,
			  bfd_boolean use_rela_p)
{
  char *name;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  bfd_size_type amt = sizeof ".rela" + strlen (asect->name);

  name = bfd_alloc (abfd, amt);
  if (name == NULL)
    return FALSE;
  sprintf (name, "%s%s", use_rela_p ? ".rela" : ".rel", asect->name);
  rel_hdr->sh_name =
    (unsigned int) _bfd_elf_strtab_add (elf_shstrtab (abfd), name,
					FALSE);
  if (rel_hdr->sh_name == (unsigned int) -1)
    return FALSE;
  rel_hdr->sh_type = use_rela_p ? SHT_RELA : SHT_REL;
  rel_hdr->sh_entsize = (use_rela_p
			 ? bed->s->sizeof_rela
			 : bed->s->sizeof_rel);
  rel_hdr->sh_addralign = 1 << bed->s->log_file_align;
  rel_hdr->sh_flags = 0;
  rel_hdr->sh_addr = 0;
  rel_hdr->sh_size = 0;
  rel_hdr->sh_offset = 0;

  return TRUE;
}

/* Set up an ELF internal section header for a section.  */

static void
elf_fake_sections (bfd *abfd, asection *asect, void *failedptrarg)
{
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  bfd_boolean *failedptr = failedptrarg;
  Elf_Internal_Shdr *this_hdr;
  unsigned int sh_type;

  if (*failedptr)
    {
      /* We already failed; just get out of the bfd_map_over_sections
	 loop.  */
      return;
    }

  this_hdr = &elf_section_data (asect)->this_hdr;

  this_hdr->sh_name = (unsigned int) _bfd_elf_strtab_add (elf_shstrtab (abfd),
							  asect->name, FALSE);
  if (this_hdr->sh_name == (unsigned int) -1)
    {
      *failedptr = TRUE;
      return;
    }

  /* Don't clear sh_flags. Assembler may set additional bits.  */

  if ((asect->flags & SEC_ALLOC) != 0
      || asect->user_set_vma)
    this_hdr->sh_addr = asect->vma;
  else
    this_hdr->sh_addr = 0;

  this_hdr->sh_offset = 0;
  this_hdr->sh_size = asect->size;
  this_hdr->sh_link = 0;
  this_hdr->sh_addralign = 1 << asect->alignment_power;
  /* The sh_entsize and sh_info fields may have been set already by
     copy_private_section_data.  */

  this_hdr->bfd_section = asect;
  this_hdr->contents = NULL;

  /* If the section type is unspecified, we set it based on
     asect->flags.  */
  if (this_hdr->sh_type == SHT_NULL)
    {
      if ((asect->flags & SEC_GROUP) != 0)
	this_hdr->sh_type = SHT_GROUP;
      else if ((asect->flags & SEC_ALLOC) != 0
	       && (((asect->flags & (SEC_LOAD | SEC_HAS_CONTENTS)) == 0)
		   || (asect->flags & SEC_NEVER_LOAD) != 0))
	this_hdr->sh_type = SHT_NOBITS;
      else
	this_hdr->sh_type = SHT_PROGBITS;
    }

  switch (this_hdr->sh_type)
    {
    default:
      break;

    case SHT_STRTAB:
    case SHT_INIT_ARRAY:
    case SHT_FINI_ARRAY:
    case SHT_PREINIT_ARRAY:
    case SHT_NOTE:
    case SHT_NOBITS:
    case SHT_PROGBITS:
      break;

    case SHT_HASH:
      this_hdr->sh_entsize = bed->s->sizeof_hash_entry;
      break;

    case SHT_DYNSYM:
      this_hdr->sh_entsize = bed->s->sizeof_sym;
      break;

    case SHT_DYNAMIC:
      this_hdr->sh_entsize = bed->s->sizeof_dyn;
      break;

    case SHT_RELA:
      if (get_elf_backend_data (abfd)->may_use_rela_p)
	this_hdr->sh_entsize = bed->s->sizeof_rela;
      break;

     case SHT_REL:
      if (get_elf_backend_data (abfd)->may_use_rel_p)
	this_hdr->sh_entsize = bed->s->sizeof_rel;
      break;

     case SHT_GNU_versym:
      this_hdr->sh_entsize = sizeof (Elf_External_Versym);
      break;

     case SHT_GNU_verdef:
      this_hdr->sh_entsize = 0;
      /* objcopy or strip will copy over sh_info, but may not set
	 cverdefs.  The linker will set cverdefs, but sh_info will be
	 zero.  */
      if (this_hdr->sh_info == 0)
	this_hdr->sh_info = elf_tdata (abfd)->cverdefs;
      else
	BFD_ASSERT (elf_tdata (abfd)->cverdefs == 0
		    || this_hdr->sh_info == elf_tdata (abfd)->cverdefs);
      break;

    case SHT_GNU_verneed:
      this_hdr->sh_entsize = 0;
      /* objcopy or strip will copy over sh_info, but may not set
	 cverrefs.  The linker will set cverrefs, but sh_info will be
	 zero.  */
      if (this_hdr->sh_info == 0)
	this_hdr->sh_info = elf_tdata (abfd)->cverrefs;
      else
	BFD_ASSERT (elf_tdata (abfd)->cverrefs == 0
		    || this_hdr->sh_info == elf_tdata (abfd)->cverrefs);
      break;

    case SHT_GROUP:
      this_hdr->sh_entsize = GRP_ENTRY_SIZE;
      break;

    case SHT_GNU_HASH:
      this_hdr->sh_entsize = bed->s->arch_size == 64 ? 0 : 4;
      break;
    }

  if ((asect->flags & SEC_ALLOC) != 0)
    this_hdr->sh_flags |= SHF_ALLOC;
  if ((asect->flags & SEC_READONLY) == 0)
    this_hdr->sh_flags |= SHF_WRITE;
  if ((asect->flags & SEC_CODE) != 0)
    this_hdr->sh_flags |= SHF_EXECINSTR;
  if ((asect->flags & SEC_MERGE) != 0)
    {
      this_hdr->sh_flags |= SHF_MERGE;
      this_hdr->sh_entsize = asect->entsize;
      if ((asect->flags & SEC_STRINGS) != 0)
	this_hdr->sh_flags |= SHF_STRINGS;
    }
  if ((asect->flags & SEC_GROUP) == 0 && elf_group_name (asect) != NULL)
    this_hdr->sh_flags |= SHF_GROUP;
  if ((asect->flags & SEC_THREAD_LOCAL) != 0)
    {
      this_hdr->sh_flags |= SHF_TLS;
      if (asect->size == 0
	  && (asect->flags & SEC_HAS_CONTENTS) == 0)
	{
	  struct bfd_link_order *o = asect->map_tail.link_order;

	  this_hdr->sh_size = 0;
	  if (o != NULL)
	    {
	      this_hdr->sh_size = o->offset + o->size;
	      if (this_hdr->sh_size != 0)
		this_hdr->sh_type = SHT_NOBITS;
	    }
	}
    }

  /* Check for processor-specific section types.  */
  sh_type = this_hdr->sh_type;
  if (bed->elf_backend_fake_sections
      && !(*bed->elf_backend_fake_sections) (abfd, this_hdr, asect))
    *failedptr = TRUE;

  if (sh_type == SHT_NOBITS && asect->size != 0)
    {
      /* Don't change the header type from NOBITS if we are being
	 called for objcopy --only-keep-debug.  */
      this_hdr->sh_type = sh_type;
    }

  /* If the section has relocs, set up a section header for the
     SHT_REL[A] section.  If two relocation sections are required for
     this section, it is up to the processor-specific back-end to
     create the other.  */
  if ((asect->flags & SEC_RELOC) != 0
      && !_bfd_elf_init_reloc_shdr (abfd,
				    &elf_section_data (asect)->rel_hdr,
				    asect,
				    asect->use_rela_p))
    *failedptr = TRUE;
}

/* Fill in the contents of a SHT_GROUP section.  */

void
bfd_elf_set_group_contents (bfd *abfd, asection *sec, void *failedptrarg)
{
  bfd_boolean *failedptr = failedptrarg;
  unsigned long symindx;
  asection *elt, *first;
  unsigned char *loc;
  bfd_boolean gas;

  /* Ignore linker created group section.  See elfNN_ia64_object_p in
     elfxx-ia64.c.  */
  if (((sec->flags & (SEC_GROUP | SEC_LINKER_CREATED)) != SEC_GROUP)
      || *failedptr)
    return;

  symindx = 0;
  if (elf_group_id (sec) != NULL)
    symindx = elf_group_id (sec)->udata.i;

  if (symindx == 0)
    {
      /* If called from the assembler, swap_out_syms will have set up
	 elf_section_syms;  If called for "ld -r", use target_index.  */
      if (elf_section_syms (abfd) != NULL)
	symindx = elf_section_syms (abfd)[sec->index]->udata.i;
      else
	symindx = sec->target_index;
    }
  elf_section_data (sec)->this_hdr.sh_info = symindx;

  /* The contents won't be allocated for "ld -r" or objcopy.  */
  gas = TRUE;
  if (sec->contents == NULL)
    {
      gas = FALSE;
      sec->contents = bfd_alloc (abfd, sec->size);

      /* Arrange for the section to be written out.  */
      elf_section_data (sec)->this_hdr.contents = sec->contents;
      if (sec->contents == NULL)
	{
	  *failedptr = TRUE;
	  return;
	}
    }

  loc = sec->contents + sec->size;

  /* Get the pointer to the first section in the group that gas
     squirreled away here.  objcopy arranges for this to be set to the
     start of the input section group.  */
  first = elt = elf_next_in_group (sec);

  /* First element is a flag word.  Rest of section is elf section
     indices for all the sections of the group.  Write them backwards
     just to keep the group in the same order as given in .section
     directives, not that it matters.  */
  while (elt != NULL)
    {
      asection *s;
      unsigned int idx;

      loc -= 4;
      s = elt;
      if (!gas)
	s = s->output_section;
      idx = 0;
      if (s != NULL)
	idx = elf_section_data (s)->this_idx;
      H_PUT_32 (abfd, idx, loc);
      elt = elf_next_in_group (elt);
      if (elt == first)
	break;
    }

  if ((loc -= 4) != sec->contents)
    abort ();

  H_PUT_32 (abfd, sec->flags & SEC_LINK_ONCE ? GRP_COMDAT : 0, loc);
}

/* Assign all ELF section numbers.  The dummy first section is handled here
   too.  The link/info pointers for the standard section types are filled
   in here too, while we're at it.  */

static bfd_boolean
assign_section_numbers (bfd *abfd, struct bfd_link_info *link_info)
{
  struct elf_obj_tdata *t = elf_tdata (abfd);
  asection *sec;
  unsigned int section_number, secn;
  Elf_Internal_Shdr **i_shdrp;
  struct bfd_elf_section_data *d;

  section_number = 1;

  _bfd_elf_strtab_clear_all_refs (elf_shstrtab (abfd));

  /* SHT_GROUP sections are in relocatable files only.  */
  if (link_info == NULL || link_info->relocatable)
    {
      /* Put SHT_GROUP sections first.  */
      for (sec = abfd->sections; sec != NULL; sec = sec->next)
	{
	  d = elf_section_data (sec);

	  if (d->this_hdr.sh_type == SHT_GROUP)
	    {
	      if (sec->flags & SEC_LINKER_CREATED)
		{
		  /* Remove the linker created SHT_GROUP sections.  */
		  bfd_section_list_remove (abfd, sec);
		  abfd->section_count--;
		}
	      else
		{
		  if (section_number == SHN_LORESERVE)
		    section_number += SHN_HIRESERVE + 1 - SHN_LORESERVE;
		  d->this_idx = section_number++;
		}
	    }
	}
    }

  for (sec = abfd->sections; sec; sec = sec->next)
    {
      d = elf_section_data (sec);

      if (d->this_hdr.sh_type != SHT_GROUP)
	{
	  if (section_number == SHN_LORESERVE)
	    section_number += SHN_HIRESERVE + 1 - SHN_LORESERVE;
	  d->this_idx = section_number++;
	}
      _bfd_elf_strtab_addref (elf_shstrtab (abfd), d->this_hdr.sh_name);
      if ((sec->flags & SEC_RELOC) == 0)
	d->rel_idx = 0;
      else
	{
	  if (section_number == SHN_LORESERVE)
	    section_number += SHN_HIRESERVE + 1 - SHN_LORESERVE;
	  d->rel_idx = section_number++;
	  _bfd_elf_strtab_addref (elf_shstrtab (abfd), d->rel_hdr.sh_name);
	}

      if (d->rel_hdr2)
	{
	  if (section_number == SHN_LORESERVE)
	    section_number += SHN_HIRESERVE + 1 - SHN_LORESERVE;
	  d->rel_idx2 = section_number++;
	  _bfd_elf_strtab_addref (elf_shstrtab (abfd), d->rel_hdr2->sh_name);
	}
      else
	d->rel_idx2 = 0;
    }

  if (section_number == SHN_LORESERVE)
    section_number += SHN_HIRESERVE + 1 - SHN_LORESERVE;
  t->shstrtab_section = section_number++;
  _bfd_elf_strtab_addref (elf_shstrtab (abfd), t->shstrtab_hdr.sh_name);
  elf_elfheader (abfd)->e_shstrndx = t->shstrtab_section;

  if (bfd_get_symcount (abfd) > 0)
    {
      if (section_number == SHN_LORESERVE)
	section_number += SHN_HIRESERVE + 1 - SHN_LORESERVE;
      t->symtab_section = section_number++;
      _bfd_elf_strtab_addref (elf_shstrtab (abfd), t->symtab_hdr.sh_name);
      if (section_number > SHN_LORESERVE - 2)
	{
	  if (section_number == SHN_LORESERVE)
	    section_number += SHN_HIRESERVE + 1 - SHN_LORESERVE;
	  t->symtab_shndx_section = section_number++;
	  t->symtab_shndx_hdr.sh_name
	    = (unsigned int) _bfd_elf_strtab_add (elf_shstrtab (abfd),
						  ".symtab_shndx", FALSE);
	  if (t->symtab_shndx_hdr.sh_name == (unsigned int) -1)
	    return FALSE;
	}
      if (section_number == SHN_LORESERVE)
	section_number += SHN_HIRESERVE + 1 - SHN_LORESERVE;
      t->strtab_section = section_number++;
      _bfd_elf_strtab_addref (elf_shstrtab (abfd), t->strtab_hdr.sh_name);
    }

  _bfd_elf_strtab_finalize (elf_shstrtab (abfd));
  t->shstrtab_hdr.sh_size = _bfd_elf_strtab_size (elf_shstrtab (abfd));

  elf_numsections (abfd) = section_number;
  elf_elfheader (abfd)->e_shnum = section_number;
  if (section_number > SHN_LORESERVE)
    elf_elfheader (abfd)->e_shnum -= SHN_HIRESERVE + 1 - SHN_LORESERVE;

  /* Set up the list of section header pointers, in agreement with the
     indices.  */
  i_shdrp = bfd_zalloc2 (abfd, section_number, sizeof (Elf_Internal_Shdr *));
  if (i_shdrp == NULL)
    return FALSE;

  i_shdrp[0] = bfd_zalloc (abfd, sizeof (Elf_Internal_Shdr));
  if (i_shdrp[0] == NULL)
    {
      bfd_release (abfd, i_shdrp);
      return FALSE;
    }

  elf_elfsections (abfd) = i_shdrp;

  i_shdrp[t->shstrtab_section] = &t->shstrtab_hdr;
  if (bfd_get_symcount (abfd) > 0)
    {
      i_shdrp[t->symtab_section] = &t->symtab_hdr;
      if (elf_numsections (abfd) > SHN_LORESERVE)
	{
	  i_shdrp[t->symtab_shndx_section] = &t->symtab_shndx_hdr;
	  t->symtab_shndx_hdr.sh_link = t->symtab_section;
	}
      i_shdrp[t->strtab_section] = &t->strtab_hdr;
      t->symtab_hdr.sh_link = t->strtab_section;
    }

  for (sec = abfd->sections; sec; sec = sec->next)
    {
      struct bfd_elf_section_data *d = elf_section_data (sec);
      asection *s;
      const char *name;

      i_shdrp[d->this_idx] = &d->this_hdr;
      if (d->rel_idx != 0)
	i_shdrp[d->rel_idx] = &d->rel_hdr;
      if (d->rel_idx2 != 0)
	i_shdrp[d->rel_idx2] = d->rel_hdr2;

      /* Fill in the sh_link and sh_info fields while we're at it.  */

      /* sh_link of a reloc section is the section index of the symbol
	 table.  sh_info is the section index of the section to which
	 the relocation entries apply.  */
      if (d->rel_idx != 0)
	{
	  d->rel_hdr.sh_link = t->symtab_section;
	  d->rel_hdr.sh_info = d->this_idx;
	}
      if (d->rel_idx2 != 0)
	{
	  d->rel_hdr2->sh_link = t->symtab_section;
	  d->rel_hdr2->sh_info = d->this_idx;
	}

      /* We need to set up sh_link for SHF_LINK_ORDER.  */
      if ((d->this_hdr.sh_flags & SHF_LINK_ORDER) != 0)
	{
	  s = elf_linked_to_section (sec);
	  if (s)
	    {
	      /* elf_linked_to_section points to the input section.  */
	      if (link_info != NULL)
		{
		  /* Check discarded linkonce section.  */
		  if (elf_discarded_section (s))
		    {
		      asection *kept;
		      (*_bfd_error_handler)
			(_("%B: sh_link of section `%A' points to discarded section `%A' of `%B'"),
			 abfd, d->this_hdr.bfd_section,
			 s, s->owner);
		      /* Point to the kept section if it has the same
			 size as the discarded one.  */
		      kept = _bfd_elf_check_kept_section (s, link_info);
		      if (kept == NULL)
			{
			  bfd_set_error (bfd_error_bad_value);
			  return FALSE;
			}
		      s = kept;
		    }

		  s = s->output_section;
		  BFD_ASSERT (s != NULL);
		}
	      else
		{
		  /* Handle objcopy. */
		  if (s->output_section == NULL)
		    {
		      (*_bfd_error_handler)
			(_("%B: sh_link of section `%A' points to removed section `%A' of `%B'"),
			 abfd, d->this_hdr.bfd_section, s, s->owner);
		      bfd_set_error (bfd_error_bad_value);
		      return FALSE;
		    }
		  s = s->output_section;
		}
	      d->this_hdr.sh_link = elf_section_data (s)->this_idx;
	    }
	  else
	    {
	      /* PR 290:
		 The Intel C compiler generates SHT_IA_64_UNWIND with
		 SHF_LINK_ORDER.  But it doesn't set the sh_link or
		 sh_info fields.  Hence we could get the situation
		 where s is NULL.  */
	      const struct elf_backend_data *bed
		= get_elf_backend_data (abfd);
	      if (bed->link_order_error_handler)
		bed->link_order_error_handler
		  (_("%B: warning: sh_link not set for section `%A'"),
		   abfd, sec);
	    }
	}

      switch (d->this_hdr.sh_type)
	{
	case SHT_REL:
	case SHT_RELA:
	  /* A reloc section which we are treating as a normal BFD
	     section.  sh_link is the section index of the symbol
	     table.  sh_info is the section index of the section to
	     which the relocation entries apply.  We assume that an
	     allocated reloc section uses the dynamic symbol table.
	     FIXME: How can we be sure?  */
	  s = bfd_get_section_by_name (abfd, ".dynsym");
	  if (s != NULL)
	    d->this_hdr.sh_link = elf_section_data (s)->this_idx;

	  /* We look up the section the relocs apply to by name.  */
	  name = sec->name;
	  if (d->this_hdr.sh_type == SHT_REL)
	    name += 4;
	  else
	    name += 5;
	  s = bfd_get_section_by_name (abfd, name);
	  if (s != NULL)
	    d->this_hdr.sh_info = elf_section_data (s)->this_idx;
	  break;

	case SHT_STRTAB:
	  /* We assume that a section named .stab*str is a stabs
	     string section.  We look for a section with the same name
	     but without the trailing ``str'', and set its sh_link
	     field to point to this section.  */
	  if (CONST_STRNEQ (sec->name, ".stab")
	      && strcmp (sec->name + strlen (sec->name) - 3, "str") == 0)
	    {
	      size_t len;
	      char *alc;

	      len = strlen (sec->name);
	      alc = bfd_malloc (len - 2);
	      if (alc == NULL)
		return FALSE;
	      memcpy (alc, sec->name, len - 3);
	      alc[len - 3] = '\0';
	      s = bfd_get_section_by_name (abfd, alc);
	      free (alc);
	      if (s != NULL)
		{
		  elf_section_data (s)->this_hdr.sh_link = d->this_idx;

		  /* This is a .stab section.  */
		  if (elf_section_data (s)->this_hdr.sh_entsize == 0)
		    elf_section_data (s)->this_hdr.sh_entsize
		      = 4 + 2 * bfd_get_arch_size (abfd) / 8;
		}
	    }
	  break;

	case SHT_DYNAMIC:
	case SHT_DYNSYM:
	case SHT_GNU_verneed:
	case SHT_GNU_verdef:
	  /* sh_link is the section header index of the string table
	     used for the dynamic entries, or the symbol table, or the
	     version strings.  */
	  s = bfd_get_section_by_name (abfd, ".dynstr");
	  if (s != NULL)
	    d->this_hdr.sh_link = elf_section_data (s)->this_idx;
	  break;

	case SHT_GNU_LIBLIST:
	  /* sh_link is the section header index of the prelink library
	     list used for the dynamic entries, or the symbol table, or
	     the version strings.  */
	  s = bfd_get_section_by_name (abfd, (sec->flags & SEC_ALLOC)
					     ? ".dynstr" : ".gnu.libstr");
	  if (s != NULL)
	    d->this_hdr.sh_link = elf_section_data (s)->this_idx;
	  break;

	case SHT_HASH:
	case SHT_GNU_HASH:
	case SHT_GNU_versym:
	  /* sh_link is the section header index of the symbol table
	     this hash table or version table is for.  */
	  s = bfd_get_section_by_name (abfd, ".dynsym");
	  if (s != NULL)
	    d->this_hdr.sh_link = elf_section_data (s)->this_idx;
	  break;

	case SHT_GROUP:
	  d->this_hdr.sh_link = t->symtab_section;
	}
    }

  for (secn = 1; secn < section_number; ++secn)
    if (i_shdrp[secn] == NULL)
      i_shdrp[secn] = i_shdrp[0];
    else
      i_shdrp[secn]->sh_name = _bfd_elf_strtab_offset (elf_shstrtab (abfd),
						       i_shdrp[secn]->sh_name);
  return TRUE;
}

/* Map symbol from it's internal number to the external number, moving
   all local symbols to be at the head of the list.  */

static bfd_boolean
sym_is_global (bfd *abfd, asymbol *sym)
{
  /* If the backend has a special mapping, use it.  */
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  if (bed->elf_backend_sym_is_global)
    return (*bed->elf_backend_sym_is_global) (abfd, sym);

  return ((sym->flags & (BSF_GLOBAL | BSF_WEAK)) != 0
	  || bfd_is_und_section (bfd_get_section (sym))
	  || bfd_is_com_section (bfd_get_section (sym)));
}

/* Don't output section symbols for sections that are not going to be
   output.  Also, don't output section symbols for reloc and other
   special sections.  */

static bfd_boolean
ignore_section_sym (bfd *abfd, asymbol *sym)
{
  return ((sym->flags & BSF_SECTION_SYM) != 0
	  && (sym->value != 0
	      || (sym->section->owner != abfd
		  && (sym->section->output_section->owner != abfd
		      || sym->section->output_offset != 0))));
}

static bfd_boolean
elf_map_symbols (bfd *abfd)
{
  unsigned int symcount = bfd_get_symcount (abfd);
  asymbol **syms = bfd_get_outsymbols (abfd);
  asymbol **sect_syms;
  unsigned int num_locals = 0;
  unsigned int num_globals = 0;
  unsigned int num_locals2 = 0;
  unsigned int num_globals2 = 0;
  int max_index = 0;
  unsigned int idx;
  asection *asect;
  asymbol **new_syms;

#ifdef DEBUG
  fprintf (stderr, "elf_map_symbols\n");
  fflush (stderr);
#endif

  for (asect = abfd->sections; asect; asect = asect->next)
    {
      if (max_index < asect->index)
	max_index = asect->index;
    }

  max_index++;
  sect_syms = bfd_zalloc2 (abfd, max_index, sizeof (asymbol *));
  if (sect_syms == NULL)
    return FALSE;
  elf_section_syms (abfd) = sect_syms;
  elf_num_section_syms (abfd) = max_index;

  /* Init sect_syms entries for any section symbols we have already
     decided to output.  */
  for (idx = 0; idx < symcount; idx++)
    {
      asymbol *sym = syms[idx];

      if ((sym->flags & BSF_SECTION_SYM) != 0
	  && !ignore_section_sym (abfd, sym))
	{
	  asection *sec = sym->section;

	  if (sec->owner != abfd)
	    sec = sec->output_section;

	  sect_syms[sec->index] = syms[idx];
	}
    }

  /* Classify all of the symbols.  */
  for (idx = 0; idx < symcount; idx++)
    {
      if (ignore_section_sym (abfd, syms[idx]))
	continue;
      if (!sym_is_global (abfd, syms[idx]))
	num_locals++;
      else
	num_globals++;
    }

  /* We will be adding a section symbol for each normal BFD section.  Most
     sections will already have a section symbol in outsymbols, but
     eg. SHT_GROUP sections will not, and we need the section symbol mapped
     at least in that case.  */
  for (asect = abfd->sections; asect; asect = asect->next)
    {
      if (sect_syms[asect->index] == NULL)
	{
	  if (!sym_is_global (abfd, asect->symbol))
	    num_locals++;
	  else
	    num_globals++;
	}
    }

  /* Now sort the symbols so the local symbols are first.  */
  new_syms = bfd_alloc2 (abfd, num_locals + num_globals, sizeof (asymbol *));

  if (new_syms == NULL)
    return FALSE;

  for (idx = 0; idx < symcount; idx++)
    {
      asymbol *sym = syms[idx];
      unsigned int i;

      if (ignore_section_sym (abfd, sym))
	continue;
      if (!sym_is_global (abfd, sym))
	i = num_locals2++;
      else
	i = num_locals + num_globals2++;
      new_syms[i] = sym;
      sym->udata.i = i + 1;
    }
  for (asect = abfd->sections; asect; asect = asect->next)
    {
      if (sect_syms[asect->index] == NULL)
	{
	  asymbol *sym = asect->symbol;
	  unsigned int i;

	  sect_syms[asect->index] = sym;
	  if (!sym_is_global (abfd, sym))
	    i = num_locals2++;
	  else
	    i = num_locals + num_globals2++;
	  new_syms[i] = sym;
	  sym->udata.i = i + 1;
	}
    }

  bfd_set_symtab (abfd, new_syms, num_locals + num_globals);

  elf_num_locals (abfd) = num_locals;
  elf_num_globals (abfd) = num_globals;
  return TRUE;
}

/* Align to the maximum file alignment that could be required for any
   ELF data structure.  */

static inline file_ptr
align_file_position (file_ptr off, int align)
{
  return (off + align - 1) & ~(align - 1);
}

/* Assign a file position to a section, optionally aligning to the
   required section alignment.  */

file_ptr
_bfd_elf_assign_file_position_for_section (Elf_Internal_Shdr *i_shdrp,
					   file_ptr offset,
					   bfd_boolean align)
{
  if (align)
    {
      unsigned int al;

      al = i_shdrp->sh_addralign;
      if (al > 1)
	offset = BFD_ALIGN (offset, al);
    }
  i_shdrp->sh_offset = offset;
  if (i_shdrp->bfd_section != NULL)
    i_shdrp->bfd_section->filepos = offset;
  if (i_shdrp->sh_type != SHT_NOBITS)
    offset += i_shdrp->sh_size;
  return offset;
}

/* Compute the file positions we are going to put the sections at, and
   otherwise prepare to begin writing out the ELF file.  If LINK_INFO
   is not NULL, this is being called by the ELF backend linker.  */

bfd_boolean
_bfd_elf_compute_section_file_positions (bfd *abfd,
					 struct bfd_link_info *link_info)
{
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  bfd_boolean failed;
  struct bfd_strtab_hash *strtab = NULL;
  Elf_Internal_Shdr *shstrtab_hdr;

  if (abfd->output_has_begun)
    return TRUE;

  /* Do any elf backend specific processing first.  */
  if (bed->elf_backend_begin_write_processing)
    (*bed->elf_backend_begin_write_processing) (abfd, link_info);

  if (! prep_headers (abfd))
    return FALSE;

  /* Post process the headers if necessary.  */
  if (bed->elf_backend_post_process_headers)
    (*bed->elf_backend_post_process_headers) (abfd, link_info);

  failed = FALSE;
  bfd_map_over_sections (abfd, elf_fake_sections, &failed);
  if (failed)
    return FALSE;

  if (!assign_section_numbers (abfd, link_info))
    return FALSE;

  /* The backend linker builds symbol table information itself.  */
  if (link_info == NULL && bfd_get_symcount (abfd) > 0)
    {
      /* Non-zero if doing a relocatable link.  */
      int relocatable_p = ! (abfd->flags & (EXEC_P | DYNAMIC));

      if (! swap_out_syms (abfd, &strtab, relocatable_p))
	return FALSE;
    }

  if (link_info == NULL)
    {
      bfd_map_over_sections (abfd, bfd_elf_set_group_contents, &failed);
      if (failed)
	return FALSE;
    }

  shstrtab_hdr = &elf_tdata (abfd)->shstrtab_hdr;
  /* sh_name was set in prep_headers.  */
  shstrtab_hdr->sh_type = SHT_STRTAB;
  shstrtab_hdr->sh_flags = 0;
  shstrtab_hdr->sh_addr = 0;
  shstrtab_hdr->sh_size = _bfd_elf_strtab_size (elf_shstrtab (abfd));
  shstrtab_hdr->sh_entsize = 0;
  shstrtab_hdr->sh_link = 0;
  shstrtab_hdr->sh_info = 0;
  /* sh_offset is set in assign_file_positions_except_relocs.  */
  shstrtab_hdr->sh_addralign = 1;

  if (!assign_file_positions_except_relocs (abfd, link_info))
    return FALSE;

  if (link_info == NULL && bfd_get_symcount (abfd) > 0)
    {
      file_ptr off;
      Elf_Internal_Shdr *hdr;

      off = elf_tdata (abfd)->next_file_pos;

      hdr = &elf_tdata (abfd)->symtab_hdr;
      off = _bfd_elf_assign_file_position_for_section (hdr, off, TRUE);

      hdr = &elf_tdata (abfd)->symtab_shndx_hdr;
      if (hdr->sh_size != 0)
	off = _bfd_elf_assign_file_position_for_section (hdr, off, TRUE);

      hdr = &elf_tdata (abfd)->strtab_hdr;
      off = _bfd_elf_assign_file_position_for_section (hdr, off, TRUE);

      elf_tdata (abfd)->next_file_pos = off;

      /* Now that we know where the .strtab section goes, write it
	 out.  */
      if (bfd_seek (abfd, hdr->sh_offset, SEEK_SET) != 0
	  || ! _bfd_stringtab_emit (abfd, strtab))
	return FALSE;
      _bfd_stringtab_free (strtab);
    }

  abfd->output_has_begun = TRUE;

  return TRUE;
}

/* Make an initial estimate of the size of the program header.  If we
   get the number wrong here, we'll redo section placement.  */

static bfd_size_type
get_program_header_size (bfd *abfd, struct bfd_link_info *info)
{
  size_t segs;
  asection *s;
  const struct elf_backend_data *bed;

  /* Assume we will need exactly two PT_LOAD segments: one for text
     and one for data.  */
  segs = 2;

  s = bfd_get_section_by_name (abfd, ".interp");
  if (s != NULL && (s->flags & SEC_LOAD) != 0)
    {
      /* If we have a loadable interpreter section, we need a
	 PT_INTERP segment.  In this case, assume we also need a
	 PT_PHDR segment, although that may not be true for all
	 targets.  */
      segs += 2;
    }

  if (bfd_get_section_by_name (abfd, ".dynamic") != NULL)
    {
      /* We need a PT_DYNAMIC segment.  */
      ++segs;

      if (elf_tdata (abfd)->relro)
	{
	  /* We need a PT_GNU_RELRO segment only when there is a
	     PT_DYNAMIC segment.  */
	  ++segs;
	}
    }

  if (elf_tdata (abfd)->eh_frame_hdr)
    {
      /* We need a PT_GNU_EH_FRAME segment.  */
      ++segs;
    }

  if (elf_tdata (abfd)->stack_flags)
    {
      /* We need a PT_GNU_STACK segment.  */
      ++segs;
    }

  for (s = abfd->sections; s != NULL; s = s->next)
    {
      if ((s->flags & SEC_LOAD) != 0
	  && CONST_STRNEQ (s->name, ".note"))
	{
	  /* We need a PT_NOTE segment.  */
	  ++segs;
	}
    }

  for (s = abfd->sections; s != NULL; s = s->next)
    {
      if (s->flags & SEC_THREAD_LOCAL)
	{
	  /* We need a PT_TLS segment.  */
	  ++segs;
	  break;
	}
    }

  /* Let the backend count up any program headers it might need.  */
  bed = get_elf_backend_data (abfd);
  if (bed->elf_backend_additional_program_headers)
    {
      int a;

      a = (*bed->elf_backend_additional_program_headers) (abfd, info);
      if (a == -1)
	abort ();
      segs += a;
    }

  return segs * bed->s->sizeof_phdr;
}

/* Create a mapping from a set of sections to a program segment.  */

static struct elf_segment_map *
make_mapping (bfd *abfd,
	      asection **sections,
	      unsigned int from,
	      unsigned int to,
	      bfd_boolean phdr)
{
  struct elf_segment_map *m;
  unsigned int i;
  asection **hdrpp;
  bfd_size_type amt;

  amt = sizeof (struct elf_segment_map);
  amt += (to - from - 1) * sizeof (asection *);
  m = bfd_zalloc (abfd, amt);
  if (m == NULL)
    return NULL;
  m->next = NULL;
  m->p_type = PT_LOAD;
  for (i = from, hdrpp = sections + from; i < to; i++, hdrpp++)
    m->sections[i - from] = *hdrpp;
  m->count = to - from;

  if (from == 0 && phdr)
    {
      /* Include the headers in the first PT_LOAD segment.  */
      m->includes_filehdr = 1;
      m->includes_phdrs = 1;
    }

  return m;
}

/* Create the PT_DYNAMIC segment, which includes DYNSEC.  Returns NULL
   on failure.  */

struct elf_segment_map *
_bfd_elf_make_dynamic_segment (bfd *abfd, asection *dynsec)
{
  struct elf_segment_map *m;

  m = bfd_zalloc (abfd, sizeof (struct elf_segment_map));
  if (m == NULL)
    return NULL;
  m->next = NULL;
  m->p_type = PT_DYNAMIC;
  m->count = 1;
  m->sections[0] = dynsec;

  return m;
}

/* Possibly add or remove segments from the segment map.  */

static bfd_boolean
elf_modify_segment_map (bfd *abfd, struct bfd_link_info *info)
{
  struct elf_segment_map **m;
  const struct elf_backend_data *bed;

  /* The placement algorithm assumes that non allocated sections are
     not in PT_LOAD segments.  We ensure this here by removing such
     sections from the segment map.  We also remove excluded
     sections.  Finally, any PT_LOAD segment without sections is
     removed.  */
  m = &elf_tdata (abfd)->segment_map;
  while (*m)
    {
      unsigned int i, new_count;

      for (new_count = 0, i = 0; i < (*m)->count; i++)
	{
	  if (((*m)->sections[i]->flags & SEC_EXCLUDE) == 0
	      && (((*m)->sections[i]->flags & SEC_ALLOC) != 0
		  || (*m)->p_type != PT_LOAD))
	    {
	      (*m)->sections[new_count] = (*m)->sections[i];
	      new_count++;
	    }
	}
      (*m)->count = new_count;

      if ((*m)->p_type == PT_LOAD && (*m)->count == 0)
	*m = (*m)->next;
      else
	m = &(*m)->next;
    }

  bed = get_elf_backend_data (abfd);
  if (bed->elf_backend_modify_segment_map != NULL)
    {
      if (!(*bed->elf_backend_modify_segment_map) (abfd, info))
	return FALSE;
    }

  return TRUE;
}

/* Set up a mapping from BFD sections to program segments.  */

bfd_boolean
_bfd_elf_map_sections_to_segments (bfd *abfd, struct bfd_link_info *info)
{
  unsigned int count;
  struct elf_segment_map *m;
  asection **sections = NULL;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);

  if (elf_tdata (abfd)->segment_map == NULL
      && bfd_count_sections (abfd) != 0)
    {
      asection *s;
      unsigned int i;
      struct elf_segment_map *mfirst;
      struct elf_segment_map **pm;
      asection *last_hdr;
      bfd_vma last_size;
      unsigned int phdr_index;
      bfd_vma maxpagesize;
      asection **hdrpp;
      bfd_boolean phdr_in_segment = TRUE;
      bfd_boolean writable;
      int tls_count = 0;
      asection *first_tls = NULL;
      asection *dynsec, *eh_frame_hdr;
      bfd_size_type amt;

      /* Select the allocated sections, and sort them.  */

      sections = bfd_malloc2 (bfd_count_sections (abfd), sizeof (asection *));
      if (sections == NULL)
	goto error_return;

      i = 0;
      for (s = abfd->sections; s != NULL; s = s->next)
	{
	  if ((s->flags & SEC_ALLOC) != 0)
	    {
	      sections[i] = s;
	      ++i;
	    }
	}
      BFD_ASSERT (i <= bfd_count_sections (abfd));
      count = i;

      qsort (sections, (size_t) count, sizeof (asection *), elf_sort_sections);

      /* Build the mapping.  */

      mfirst = NULL;
      pm = &mfirst;

      /* If we have a .interp section, then create a PT_PHDR segment for
	 the program headers and a PT_INTERP segment for the .interp
	 section.  */
      s = bfd_get_section_by_name (abfd, ".interp");
      if (s != NULL && (s->flags & SEC_LOAD) != 0)
	{
	  amt = sizeof (struct elf_segment_map);
	  m = bfd_zalloc (abfd, amt);
	  if (m == NULL)
	    goto error_return;
	  m->next = NULL;
	  m->p_type = PT_PHDR;
	  /* FIXME: UnixWare and Solaris set PF_X, Irix 5 does not.  */
	  m->p_flags = PF_R | PF_X;
	  m->p_flags_valid = 1;
	  m->includes_phdrs = 1;

	  *pm = m;
	  pm = &m->next;

	  amt = sizeof (struct elf_segment_map);
	  m = bfd_zalloc (abfd, amt);
	  if (m == NULL)
	    goto error_return;
	  m->next = NULL;
	  m->p_type = PT_INTERP;
	  m->count = 1;
	  m->sections[0] = s;

	  *pm = m;
	  pm = &m->next;
	}

      /* Look through the sections.  We put sections in the same program
	 segment when the start of the second section can be placed within
	 a few bytes of the end of the first section.  */
      last_hdr = NULL;
      last_size = 0;
      phdr_index = 0;
      maxpagesize = bed->maxpagesize;
      writable = FALSE;
      dynsec = bfd_get_section_by_name (abfd, ".dynamic");
      if (dynsec != NULL
	  && (dynsec->flags & SEC_LOAD) == 0)
	dynsec = NULL;

      /* Deal with -Ttext or something similar such that the first section
	 is not adjacent to the program headers.  This is an
	 approximation, since at this point we don't know exactly how many
	 program headers we will need.  */
      if (count > 0)
	{
	  bfd_size_type phdr_size = elf_tdata (abfd)->program_header_size;

	  if (phdr_size == (bfd_size_type) -1)
	    phdr_size = get_program_header_size (abfd, info);
	  if ((abfd->flags & D_PAGED) == 0
	      || sections[0]->lma < phdr_size
	      || sections[0]->lma % maxpagesize < phdr_size % maxpagesize)
	    phdr_in_segment = FALSE;
	}

      for (i = 0, hdrpp = sections; i < count; i++, hdrpp++)
	{
	  asection *hdr;
	  bfd_boolean new_segment;

	  hdr = *hdrpp;

	  /* See if this section and the last one will fit in the same
	     segment.  */

	  if (last_hdr == NULL)
	    {
	      /* If we don't have a segment yet, then we don't need a new
		 one (we build the last one after this loop).  */
	      new_segment = FALSE;
	    }
	  else if (last_hdr->lma - last_hdr->vma != hdr->lma - hdr->vma)
	    {
	      /* If this section has a different relation between the
		 virtual address and the load address, then we need a new
		 segment.  */
	      new_segment = TRUE;
	    }
	  else if (BFD_ALIGN (last_hdr->lma + last_size, maxpagesize)
		   < BFD_ALIGN (hdr->lma, maxpagesize))
	    {
	      /* If putting this section in this segment would force us to
		 skip a page in the segment, then we need a new segment.  */
	      new_segment = TRUE;
	    }
	  else if ((last_hdr->flags & (SEC_LOAD | SEC_THREAD_LOCAL)) == 0
		   && (hdr->flags & (SEC_LOAD | SEC_THREAD_LOCAL)) != 0)
	    {
	      /* We don't want to put a loadable section after a
		 nonloadable section in the same segment.
		 Consider .tbss sections as loadable for this purpose.  */
	      new_segment = TRUE;
	    }
	  else if ((abfd->flags & D_PAGED) == 0)
	    {
	      /* If the file is not demand paged, which means that we
		 don't require the sections to be correctly aligned in the
		 file, then there is no other reason for a new segment.  */
	      new_segment = FALSE;
	    }
	  else if (! writable
		   && (hdr->flags & SEC_READONLY) == 0
		   && (((last_hdr->lma + last_size - 1)
			& ~(maxpagesize - 1))
		       != (hdr->lma & ~(maxpagesize - 1))))
	    {
	      /* We don't want to put a writable section in a read only
		 segment, unless they are on the same page in memory
		 anyhow.  We already know that the last section does not
		 bring us past the current section on the page, so the
		 only case in which the new section is not on the same
		 page as the previous section is when the previous section
		 ends precisely on a page boundary.  */
	      new_segment = TRUE;
	    }
	  else
	    {
	      /* Otherwise, we can use the same segment.  */
	      new_segment = FALSE;
	    }

	  /* Allow interested parties a chance to override our decision.  */
	  if (last_hdr && info->callbacks->override_segment_assignment)
	    new_segment = info->callbacks->override_segment_assignment (info, abfd, hdr, last_hdr, new_segment);

	  if (! new_segment)
	    {
	      if ((hdr->flags & SEC_READONLY) == 0)
		writable = TRUE;
	      last_hdr = hdr;
	      /* .tbss sections effectively have zero size.  */
	      if ((hdr->flags & (SEC_THREAD_LOCAL | SEC_LOAD))
		  != SEC_THREAD_LOCAL)
		last_size = hdr->size;
	      else
		last_size = 0;
	      continue;
	    }

	  /* We need a new program segment.  We must create a new program
	     header holding all the sections from phdr_index until hdr.  */

	  m = make_mapping (abfd, sections, phdr_index, i, phdr_in_segment);
	  if (m == NULL)
	    goto error_return;

	  *pm = m;
	  pm = &m->next;

	  if ((hdr->flags & SEC_READONLY) == 0)
	    writable = TRUE;
	  else
	    writable = FALSE;

	  last_hdr = hdr;
	  /* .tbss sections effectively have zero size.  */
	  if ((hdr->flags & (SEC_THREAD_LOCAL | SEC_LOAD)) != SEC_THREAD_LOCAL)
	    last_size = hdr->size;
	  else
	    last_size = 0;
	  phdr_index = i;
	  phdr_in_segment = FALSE;
	}

      /* Create a final PT_LOAD program segment.  */
      if (last_hdr != NULL)
	{
	  m = make_mapping (abfd, sections, phdr_index, i, phdr_in_segment);
	  if (m == NULL)
	    goto error_return;

	  *pm = m;
	  pm = &m->next;
	}

      /* If there is a .dynamic section, throw in a PT_DYNAMIC segment.  */
      if (dynsec != NULL)
	{
	  m = _bfd_elf_make_dynamic_segment (abfd, dynsec);
	  if (m == NULL)
	    goto error_return;
	  *pm = m;
	  pm = &m->next;
	}

      /* For each loadable .note section, add a PT_NOTE segment.  We don't
	 use bfd_get_section_by_name, because if we link together
	 nonloadable .note sections and loadable .note sections, we will
	 generate two .note sections in the output file.  FIXME: Using
	 names for section types is bogus anyhow.  */
      for (s = abfd->sections; s != NULL; s = s->next)
	{
	  if ((s->flags & SEC_LOAD) != 0
	      && CONST_STRNEQ (s->name, ".note"))
	    {
	      amt = sizeof (struct elf_segment_map);
	      m = bfd_zalloc (abfd, amt);
	      if (m == NULL)
		goto error_return;
	      m->next = NULL;
	      m->p_type = PT_NOTE;
	      m->count = 1;
	      m->sections[0] = s;

	      *pm = m;
	      pm = &m->next;
	    }
	  if (s->flags & SEC_THREAD_LOCAL)
	    {
	      if (! tls_count)
		first_tls = s;
	      tls_count++;
	    }
	}

      /* If there are any SHF_TLS output sections, add PT_TLS segment.  */
      if (tls_count > 0)
	{
	  int i;

	  amt = sizeof (struct elf_segment_map);
	  amt += (tls_count - 1) * sizeof (asection *);
	  m = bfd_zalloc (abfd, amt);
	  if (m == NULL)
	    goto error_return;
	  m->next = NULL;
	  m->p_type = PT_TLS;
	  m->count = tls_count;
	  /* Mandated PF_R.  */
	  m->p_flags = PF_R;
	  m->p_flags_valid = 1;
	  for (i = 0; i < tls_count; ++i)
	    {
	      BFD_ASSERT (first_tls->flags & SEC_THREAD_LOCAL);
	      m->sections[i] = first_tls;
	      first_tls = first_tls->next;
	    }

	  *pm = m;
	  pm = &m->next;
	}

      /* If there is a .eh_frame_hdr section, throw in a PT_GNU_EH_FRAME
	 segment.  */
      eh_frame_hdr = elf_tdata (abfd)->eh_frame_hdr;
      if (eh_frame_hdr != NULL
	  && (eh_frame_hdr->output_section->flags & SEC_LOAD) != 0)
	{
	  amt = sizeof (struct elf_segment_map);
	  m = bfd_zalloc (abfd, amt);
	  if (m == NULL)
	    goto error_return;
	  m->next = NULL;
	  m->p_type = PT_GNU_EH_FRAME;
	  m->count = 1;
	  m->sections[0] = eh_frame_hdr->output_section;

	  *pm = m;
	  pm = &m->next;
	}

      if (elf_tdata (abfd)->stack_flags)
	{
	  amt = sizeof (struct elf_segment_map);
	  m = bfd_zalloc (abfd, amt);
	  if (m == NULL)
	    goto error_return;
	  m->next = NULL;
	  m->p_type = PT_GNU_STACK;
	  m->p_flags = elf_tdata (abfd)->stack_flags;
	  m->p_flags_valid = 1;

	  *pm = m;
	  pm = &m->next;
	}

      if (dynsec != NULL && elf_tdata (abfd)->relro)
	{
	  /* We make a PT_GNU_RELRO segment only when there is a
	     PT_DYNAMIC segment.  */
	  amt = sizeof (struct elf_segment_map);
	  m = bfd_zalloc (abfd, amt);
	  if (m == NULL)
	    goto error_return;
	  m->next = NULL;
	  m->p_type = PT_GNU_RELRO;
	  m->p_flags = PF_R;
	  m->p_flags_valid = 1;

	  *pm = m;
	  pm = &m->next;
	}

      free (sections);
      elf_tdata (abfd)->segment_map = mfirst;
    }

  if (!elf_modify_segment_map (abfd, info))
    return FALSE;

  for (count = 0, m = elf_tdata (abfd)->segment_map; m != NULL; m = m->next)
    ++count;
  elf_tdata (abfd)->program_header_size = count * bed->s->sizeof_phdr;

  return TRUE;

 error_return:
  if (sections != NULL)
    free (sections);
  return FALSE;
}

/* Sort sections by address.  */

static int
elf_sort_sections (const void *arg1, const void *arg2)
{
  const asection *sec1 = *(const asection **) arg1;
  const asection *sec2 = *(const asection **) arg2;
  bfd_size_type size1, size2;

  /* Sort by LMA first, since this is the address used to
     place the section into a segment.  */
  if (sec1->lma < sec2->lma)
    return -1;
  else if (sec1->lma > sec2->lma)
    return 1;

  /* Then sort by VMA.  Normally the LMA and the VMA will be
     the same, and this will do nothing.  */
  if (sec1->vma < sec2->vma)
    return -1;
  else if (sec1->vma > sec2->vma)
    return 1;

  /* Put !SEC_LOAD sections after SEC_LOAD ones.  */

#define TOEND(x) (((x)->flags & (SEC_LOAD | SEC_THREAD_LOCAL)) == 0)

  if (TOEND (sec1))
    {
      if (TOEND (sec2))
	{
	  /* If the indicies are the same, do not return 0
	     here, but continue to try the next comparison.  */
	  if (sec1->target_index - sec2->target_index != 0)
	    return sec1->target_index - sec2->target_index;
	}
      else
	return 1;
    }
  else if (TOEND (sec2))
    return -1;

#undef TOEND

  /* Sort by size, to put zero sized sections
     before others at the same address.  */

  size1 = (sec1->flags & SEC_LOAD) ? sec1->size : 0;
  size2 = (sec2->flags & SEC_LOAD) ? sec2->size : 0;

  if (size1 < size2)
    return -1;
  if (size1 > size2)
    return 1;

  return sec1->target_index - sec2->target_index;
}

/* Ian Lance Taylor writes:

   We shouldn't be using % with a negative signed number.  That's just
   not good.  We have to make sure either that the number is not
   negative, or that the number has an unsigned type.  When the types
   are all the same size they wind up as unsigned.  When file_ptr is a
   larger signed type, the arithmetic winds up as signed long long,
   which is wrong.

   What we're trying to say here is something like ``increase OFF by
   the least amount that will cause it to be equal to the VMA modulo
   the page size.''  */
/* In other words, something like:

   vma_offset = m->sections[0]->vma % bed->maxpagesize;
   off_offset = off % bed->maxpagesize;
   if (vma_offset < off_offset)
     adjustment = vma_offset + bed->maxpagesize - off_offset;
   else
     adjustment = vma_offset - off_offset;

   which can can be collapsed into the expression below.  */

static file_ptr
vma_page_aligned_bias (bfd_vma vma, ufile_ptr off, bfd_vma maxpagesize)
{
  return ((vma - off) % maxpagesize);
}

/* Assign file positions to the sections based on the mapping from
   sections to segments.  This function also sets up some fields in
   the file header.  */

static bfd_boolean
assign_file_positions_for_load_sections (bfd *abfd,
					 struct bfd_link_info *link_info)
{
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  struct elf_segment_map *m;
  Elf_Internal_Phdr *phdrs;
  Elf_Internal_Phdr *p;
  file_ptr off;
  bfd_size_type maxpagesize;
  unsigned int alloc;
  unsigned int i, j;

  if (link_info == NULL
      && !elf_modify_segment_map (abfd, link_info))
    return FALSE;

  alloc = 0;
  for (m = elf_tdata (abfd)->segment_map; m != NULL; m = m->next)
    ++alloc;

  elf_elfheader (abfd)->e_phoff = bed->s->sizeof_ehdr;
  elf_elfheader (abfd)->e_phentsize = bed->s->sizeof_phdr;
  elf_elfheader (abfd)->e_phnum = alloc;

  if (elf_tdata (abfd)->program_header_size == (bfd_size_type) -1)
    elf_tdata (abfd)->program_header_size = alloc * bed->s->sizeof_phdr;
  else
    BFD_ASSERT (elf_tdata (abfd)->program_header_size
		>= alloc * bed->s->sizeof_phdr);

  if (alloc == 0)
    {
      elf_tdata (abfd)->next_file_pos = bed->s->sizeof_ehdr;
      return TRUE;
    }

  phdrs = bfd_alloc2 (abfd, alloc, sizeof (Elf_Internal_Phdr));
  elf_tdata (abfd)->phdr = phdrs;
  if (phdrs == NULL)
    return FALSE;

  maxpagesize = 1;
  if ((abfd->flags & D_PAGED) != 0)
    maxpagesize = bed->maxpagesize;

  off = bed->s->sizeof_ehdr;
  off += alloc * bed->s->sizeof_phdr;

  for (m = elf_tdata (abfd)->segment_map, p = phdrs, j = 0;
       m != NULL;
       m = m->next, p++, j++)
    {
      asection **secpp;
      bfd_vma off_adjust;
      bfd_boolean no_contents;

      /* If elf_segment_map is not from map_sections_to_segments, the
	 sections may not be correctly ordered.  NOTE: sorting should
	 not be done to the PT_NOTE section of a corefile, which may
	 contain several pseudo-sections artificially created by bfd.
	 Sorting these pseudo-sections breaks things badly.  */
      if (m->count > 1
	  && !(elf_elfheader (abfd)->e_type == ET_CORE
	       && m->p_type == PT_NOTE))
	qsort (m->sections, (size_t) m->count, sizeof (asection *),
	       elf_sort_sections);

      /* An ELF segment (described by Elf_Internal_Phdr) may contain a
	 number of sections with contents contributing to both p_filesz
	 and p_memsz, followed by a number of sections with no contents
	 that just contribute to p_memsz.  In this loop, OFF tracks next
	 available file offset for PT_LOAD and PT_NOTE segments.  */
      p->p_type = m->p_type;
      p->p_flags = m->p_flags;

      if (m->count == 0)
	p->p_vaddr = 0;
      else
	p->p_vaddr = m->sections[0]->vma - m->p_vaddr_offset;

      if (m->p_paddr_valid)
	p->p_paddr = m->p_paddr;
      else if (m->count == 0)
	p->p_paddr = 0;
      else
	p->p_paddr = m->sections[0]->lma - m->p_vaddr_offset;

      if (p->p_type == PT_LOAD
	  && (abfd->flags & D_PAGED) != 0)
	{
	  /* p_align in demand paged PT_LOAD segments effectively stores
	     the maximum page size.  When copying an executable with
	     objcopy, we set m->p_align from the input file.  Use this
	     value for maxpagesize rather than bed->maxpagesize, which
	     may be different.  Note that we use maxpagesize for PT_TLS
	     segment alignment later in this function, so we are relying
	     on at least one PT_LOAD segment appearing before a PT_TLS
	     segment.  */
	  if (m->p_align_valid)
	    maxpagesize = m->p_align;

	  p->p_align = maxpagesize;
	}
      else if (m->count == 0)
	p->p_align = 1 << bed->s->log_file_align;
      else if (m->p_align_valid)
	p->p_align = m->p_align;
      else
	p->p_align = 0;

      no_contents = FALSE;
      off_adjust = 0;
      if (p->p_type == PT_NOTE)
	{
	  for (i = 0; i < m->count; i++)
	    elf_section_type (m->sections[i]) = SHT_NOTE;
	}
      else if (p->p_type == PT_LOAD
	  && m->count > 0)
	{
	  bfd_size_type align;
	  unsigned int align_power = 0;

	  if (m->p_align_valid)
	    align = p->p_align;
	  else
	    {
	      for (i = 0, secpp = m->sections; i < m->count; i++, secpp++)
		{
		  unsigned int secalign;

		  secalign = bfd_get_section_alignment (abfd, *secpp);
		  if (secalign > align_power)
		    align_power = secalign;
		}
	      align = (bfd_size_type) 1 << align_power;
	      if (align < maxpagesize)
		align = maxpagesize;
	    }

	  for (i = 0; i < m->count; i++)
	    if ((m->sections[i]->flags & (SEC_LOAD | SEC_HAS_CONTENTS)) == 0)
	      /* If we aren't making room for this section, then
		 it must be SHT_NOBITS regardless of what we've
		 set via struct bfd_elf_special_section.  */
	      elf_section_type (m->sections[i]) = SHT_NOBITS;

	  /* Find out whether this segment contains any loadable
	     sections.  If the first section isn't loadable, the same
	     holds for any other sections.  */
	  i = 0;
	  while (elf_section_type (m->sections[i]) == SHT_NOBITS)
	    {
	      /* If a segment starts with .tbss, we need to look
		 at the next section to decide whether the segment
		 has any loadable sections.  */
	      if ((elf_section_flags (m->sections[i]) & SHF_TLS) == 0
		  || ++i >= m->count)
		{
		  no_contents = TRUE;
		  break;
		}
	    }

	  off_adjust = vma_page_aligned_bias (m->sections[0]->vma, off, align);
	  off += off_adjust;
	  if (no_contents)
	    {
	      /* We shouldn't need to align the segment on disk since
		 the segment doesn't need file space, but the gABI
		 arguably requires the alignment and glibc ld.so
		 checks it.  So to comply with the alignment
		 requirement but not waste file space, we adjust
		 p_offset for just this segment.  (OFF_ADJUST is
		 subtracted from OFF later.)  This may put p_offset
		 past the end of file, but that shouldn't matter.  */
	    }
	  else
	    off_adjust = 0;
	}
      /* Make sure the .dynamic section is the first section in the
	 PT_DYNAMIC segment.  */
      else if (p->p_type == PT_DYNAMIC
	       && m->count > 1
	       && strcmp (m->sections[0]->name, ".dynamic") != 0)
	{
	  _bfd_error_handler
	    (_("%B: The first section in the PT_DYNAMIC segment is not the .dynamic section"),
	     abfd);
	  bfd_set_error (bfd_error_bad_value);
	  return FALSE;
	}

      p->p_offset = 0;
      p->p_filesz = 0;
      p->p_memsz = 0;

      if (m->includes_filehdr)
	{
	  if (!m->p_flags_valid)
	    p->p_flags |= PF_R;
	  p->p_filesz = bed->s->sizeof_ehdr;
	  p->p_memsz = bed->s->sizeof_ehdr;
	  if (m->count > 0)
	    {
	      BFD_ASSERT (p->p_type == PT_LOAD);

	      if (p->p_vaddr < (bfd_vma) off)
		{
		  (*_bfd_error_handler)
		    (_("%B: Not enough room for program headers, try linking with -N"),
		     abfd);
		  bfd_set_error (bfd_error_bad_value);
		  return FALSE;
		}

	      p->p_vaddr -= off;
	      if (!m->p_paddr_valid)
		p->p_paddr -= off;
	    }
	}

      if (m->includes_phdrs)
	{
	  if (!m->p_flags_valid)
	    p->p_flags |= PF_R;

	  if (!m->includes_filehdr)
	    {
	      p->p_offset = bed->s->sizeof_ehdr;

	      if (m->count > 0)
		{
		  BFD_ASSERT (p->p_type == PT_LOAD);
		  p->p_vaddr -= off - p->p_offset;
		  if (!m->p_paddr_valid)
		    p->p_paddr -= off - p->p_offset;
		}
	    }

	  p->p_filesz += alloc * bed->s->sizeof_phdr;
	  p->p_memsz += alloc * bed->s->sizeof_phdr;
	}

      if (p->p_type == PT_LOAD
	  || (p->p_type == PT_NOTE && bfd_get_format (abfd) == bfd_core))
	{
	  if (!m->includes_filehdr && !m->includes_phdrs)
	    p->p_offset = off;
	  else
	    {
	      file_ptr adjust;

	      adjust = off - (p->p_offset + p->p_filesz);
	      if (!no_contents)
		p->p_filesz += adjust;
	      p->p_memsz += adjust;
	    }
	}

      /* Set up p_filesz, p_memsz, p_align and p_flags from the section
	 maps.  Set filepos for sections in PT_LOAD segments, and in
	 core files, for sections in PT_NOTE segments.
	 assign_file_positions_for_non_load_sections will set filepos
	 for other sections and update p_filesz for other segments.  */
      for (i = 0, secpp = m->sections; i < m->count; i++, secpp++)
	{
	  asection *sec;
	  bfd_size_type align;
	  Elf_Internal_Shdr *this_hdr;

	  sec = *secpp;
	  this_hdr = &elf_section_data (sec)->this_hdr;
	  align = (bfd_size_type) 1 << bfd_get_section_alignment (abfd, sec);

	  if (p->p_type == PT_LOAD
	      || p->p_type == PT_TLS)
	    {
	      bfd_signed_vma adjust = sec->lma - (p->p_paddr + p->p_memsz);

	      if (this_hdr->sh_type != SHT_NOBITS
		  || ((this_hdr->sh_flags & SHF_ALLOC) != 0
		      && ((this_hdr->sh_flags & SHF_TLS) == 0
			  || p->p_type == PT_TLS)))
		{
		  if (adjust < 0)
		    {
		      (*_bfd_error_handler)
			(_("%B: section %A lma 0x%lx overlaps previous sections"),
			 abfd, sec, (unsigned long) sec->lma);
		      adjust = 0;
		    }
		  p->p_memsz += adjust;

		  if (this_hdr->sh_type != SHT_NOBITS)
		    {
		      off += adjust;
		      p->p_filesz += adjust;
		    }
		}
	    }

	  if (p->p_type == PT_NOTE && bfd_get_format (abfd) == bfd_core)
	    {
	      /* The section at i == 0 is the one that actually contains
		 everything.  */
	      if (i == 0)
		{
		  this_hdr->sh_offset = sec->filepos = off;
		  off += this_hdr->sh_size;
		  p->p_filesz = this_hdr->sh_size;
		  p->p_memsz = 0;
		  p->p_align = 1;
		}
	      else
		{
		  /* The rest are fake sections that shouldn't be written.  */
		  sec->filepos = 0;
		  sec->size = 0;
		  sec->flags = 0;
		  continue;
		}
	    }
	  else
	    {
	      if (p->p_type == PT_LOAD)
		{
		  this_hdr->sh_offset = sec->filepos = off;
		  if (this_hdr->sh_type != SHT_NOBITS)
		    off += this_hdr->sh_size;
		}

	      if (this_hdr->sh_type != SHT_NOBITS)
		{
		  p->p_filesz += this_hdr->sh_size;
		  /* A load section without SHF_ALLOC is something like
		     a note section in a PT_NOTE segment.  These take
		     file space but are not loaded into memory.  */
		  if ((this_hdr->sh_flags & SHF_ALLOC) != 0)
		    p->p_memsz += this_hdr->sh_size;
		}
	      else if ((this_hdr->sh_flags & SHF_ALLOC) != 0)
		{
		  if (p->p_type == PT_TLS)
		    p->p_memsz += this_hdr->sh_size;

		  /* .tbss is special.  It doesn't contribute to p_memsz of
		     normal segments.  */
		  else if ((this_hdr->sh_flags & SHF_TLS) == 0)
		    p->p_memsz += this_hdr->sh_size;
		}

	      if (p->p_type == PT_GNU_RELRO)
		p->p_align = 1;
	      else if (align > p->p_align
		       && !m->p_align_valid
		       && (p->p_type != PT_LOAD
			   || (abfd->flags & D_PAGED) == 0))
		p->p_align = align;
	    }

	  if (!m->p_flags_valid)
	    {
	      p->p_flags |= PF_R;
	      if ((this_hdr->sh_flags & SHF_EXECINSTR) != 0)
		p->p_flags |= PF_X;
	      if ((this_hdr->sh_flags & SHF_WRITE) != 0)
		p->p_flags |= PF_W;
	    }
	}
      off -= off_adjust;

      /* Check that all sections are in a PT_LOAD segment.
	 Don't check funky gdb generated core files.  */
      if (p->p_type == PT_LOAD && bfd_get_format (abfd) != bfd_core)
	for (i = 0, secpp = m->sections; i < m->count; i++, secpp++)
	  {
	    Elf_Internal_Shdr *this_hdr;
	    asection *sec;

	    sec = *secpp;
	    this_hdr = &(elf_section_data(sec)->this_hdr);
	    if (this_hdr->sh_size != 0
		&& !ELF_IS_SECTION_IN_SEGMENT_FILE (this_hdr, p))
	      {
		(*_bfd_error_handler)
		  (_("%B: section `%A' can't be allocated in segment %d"),
		   abfd, sec, j);
		bfd_set_error (bfd_error_bad_value);
		return FALSE;
	      }
	  }
    }

  elf_tdata (abfd)->next_file_pos = off;
  return TRUE;
}

/* Assign file positions for the other sections.  */

static bfd_boolean
assign_file_positions_for_non_load_sections (bfd *abfd,
					     struct bfd_link_info *link_info)
{
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  Elf_Internal_Shdr **i_shdrpp;
  Elf_Internal_Shdr **hdrpp;
  Elf_Internal_Phdr *phdrs;
  Elf_Internal_Phdr *p;
  struct elf_segment_map *m;
  bfd_vma filehdr_vaddr, filehdr_paddr;
  bfd_vma phdrs_vaddr, phdrs_paddr;
  file_ptr off;
  unsigned int num_sec;
  unsigned int i;
  unsigned int count;

  i_shdrpp = elf_elfsections (abfd);
  num_sec = elf_numsections (abfd);
  off = elf_tdata (abfd)->next_file_pos;
  for (i = 1, hdrpp = i_shdrpp + 1; i < num_sec; i++, hdrpp++)
    {
      struct elf_obj_tdata *tdata = elf_tdata (abfd);
      Elf_Internal_Shdr *hdr;

      hdr = *hdrpp;
      if (hdr->bfd_section != NULL
	  && (hdr->bfd_section->filepos != 0
	      || (hdr->sh_type == SHT_NOBITS
		  && hdr->contents == NULL)))
	BFD_ASSERT (hdr->sh_offset == hdr->bfd_section->filepos);
      else if ((hdr->sh_flags & SHF_ALLOC) != 0)
	{
	  if (hdr->sh_size != 0)
	    ((*_bfd_error_handler)
	     (_("%B: warning: allocated section `%s' not in segment"),
	      abfd,
	      (hdr->bfd_section == NULL
	       ? "*unknown*"
	       : hdr->bfd_section->name)));
	  /* We don't need to page align empty sections.  */
	  if ((abfd->flags & D_PAGED) != 0 && hdr->sh_size != 0)
	    off += vma_page_aligned_bias (hdr->sh_addr, off,
					  bed->maxpagesize);
	  else
	    off += vma_page_aligned_bias (hdr->sh_addr, off,
					  hdr->sh_addralign);
	  off = _bfd_elf_assign_file_position_for_section (hdr, off,
							   FALSE);
	}
      else if (((hdr->sh_type == SHT_REL || hdr->sh_type == SHT_RELA)
		&& hdr->bfd_section == NULL)
	       || hdr == i_shdrpp[tdata->symtab_section]
	       || hdr == i_shdrpp[tdata->symtab_shndx_section]
	       || hdr == i_shdrpp[tdata->strtab_section])
	hdr->sh_offset = -1;
      else
	off = _bfd_elf_assign_file_position_for_section (hdr, off, TRUE);

      if (i == SHN_LORESERVE - 1)
	{
	  i += SHN_HIRESERVE + 1 - SHN_LORESERVE;
	  hdrpp += SHN_HIRESERVE + 1 - SHN_LORESERVE;
	}
    }

  /* Now that we have set the section file positions, we can set up
     the file positions for the non PT_LOAD segments.  */
  count = 0;
  filehdr_vaddr = 0;
  filehdr_paddr = 0;
  phdrs_vaddr = bed->maxpagesize + bed->s->sizeof_ehdr;
  phdrs_paddr = 0;
  phdrs = elf_tdata (abfd)->phdr;
  for (m = elf_tdata (abfd)->segment_map, p = phdrs;
       m != NULL;
       m = m->next, p++)
    {
      ++count;
      if (p->p_type != PT_LOAD)
	continue;

      if (m->includes_filehdr)
	{
	  filehdr_vaddr = p->p_vaddr;
	  filehdr_paddr = p->p_paddr;
	}
      if (m->includes_phdrs)
	{
	  phdrs_vaddr = p->p_vaddr;
	  phdrs_paddr = p->p_paddr;
	  if (m->includes_filehdr)
	    {
	      phdrs_vaddr += bed->s->sizeof_ehdr;
	      phdrs_paddr += bed->s->sizeof_ehdr;
	    }
	}
    }

  for (m = elf_tdata (abfd)->segment_map, p = phdrs;
       m != NULL;
       m = m->next, p++)
    {
      if (m->count != 0)
	{
	  if (p->p_type != PT_LOAD
	      && (p->p_type != PT_NOTE || bfd_get_format (abfd) != bfd_core))
	    {
	      Elf_Internal_Shdr *hdr;
	      BFD_ASSERT (!m->includes_filehdr && !m->includes_phdrs);

	      hdr = &elf_section_data (m->sections[m->count - 1])->this_hdr;
	      p->p_filesz = (m->sections[m->count - 1]->filepos
			     - m->sections[0]->filepos);
	      if (hdr->sh_type != SHT_NOBITS)
		p->p_filesz += hdr->sh_size;

	      p->p_offset = m->sections[0]->filepos;
	    }
	}
      else
	{
	  if (m->includes_filehdr)
	    {
	      p->p_vaddr = filehdr_vaddr;
	      if (! m->p_paddr_valid)
		p->p_paddr = filehdr_paddr;
	    }
	  else if (m->includes_phdrs)
	    {
	      p->p_vaddr = phdrs_vaddr;
	      if (! m->p_paddr_valid)
		p->p_paddr = phdrs_paddr;
	    }
	  else if (p->p_type == PT_GNU_RELRO)
	    {
	      Elf_Internal_Phdr *lp;

	      for (lp = phdrs; lp < phdrs + count; ++lp)
		{
		  if (lp->p_type == PT_LOAD
		      && lp->p_vaddr <= link_info->relro_end
		      && lp->p_vaddr >= link_info->relro_start
		      && (lp->p_vaddr + lp->p_filesz
			  >= link_info->relro_end))
		    break;
		}

	      if (lp < phdrs + count
		  && link_info->relro_end > lp->p_vaddr)
		{
		  p->p_vaddr = lp->p_vaddr;
		  p->p_paddr = lp->p_paddr;
		  p->p_offset = lp->p_offset;
		  p->p_filesz = link_info->relro_end - lp->p_vaddr;
		  p->p_memsz = p->p_filesz;
		  p->p_align = 1;
		  p->p_flags = (lp->p_flags & ~PF_W);
		}
	      else
		{
		  memset (p, 0, sizeof *p);
		  p->p_type = PT_NULL;
		}
	    }
	}
    }

  elf_tdata (abfd)->next_file_pos = off;

  return TRUE;
}

/* Work out the file positions of all the sections.  This is called by
   _bfd_elf_compute_section_file_positions.  All the section sizes and
   VMAs must be known before this is called.

   Reloc sections come in two flavours: Those processed specially as
   "side-channel" data attached to a section to which they apply, and
   those that bfd doesn't process as relocations.  The latter sort are
   stored in a normal bfd section by bfd_section_from_shdr.   We don't
   consider the former sort here, unless they form part of the loadable
   image.  Reloc sections not assigned here will be handled later by
   assign_file_positions_for_relocs.

   We also don't set the positions of the .symtab and .strtab here.  */

static bfd_boolean
assign_file_positions_except_relocs (bfd *abfd,
				     struct bfd_link_info *link_info)
{
  struct elf_obj_tdata *tdata = elf_tdata (abfd);
  Elf_Internal_Ehdr *i_ehdrp = elf_elfheader (abfd);
  file_ptr off;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);

  if ((abfd->flags & (EXEC_P | DYNAMIC)) == 0
      && bfd_get_format (abfd) != bfd_core)
    {
      Elf_Internal_Shdr ** const i_shdrpp = elf_elfsections (abfd);
      unsigned int num_sec = elf_numsections (abfd);
      Elf_Internal_Shdr **hdrpp;
      unsigned int i;

      /* Start after the ELF header.  */
      off = i_ehdrp->e_ehsize;

      /* We are not creating an executable, which means that we are
	 not creating a program header, and that the actual order of
	 the sections in the file is unimportant.  */
      for (i = 1, hdrpp = i_shdrpp + 1; i < num_sec; i++, hdrpp++)
	{
	  Elf_Internal_Shdr *hdr;

	  hdr = *hdrpp;
	  if (((hdr->sh_type == SHT_REL || hdr->sh_type == SHT_RELA)
	       && hdr->bfd_section == NULL)
	      || i == tdata->symtab_section
	      || i == tdata->symtab_shndx_section
	      || i == tdata->strtab_section)
	    {
	      hdr->sh_offset = -1;
	    }
	  else
	    off = _bfd_elf_assign_file_position_for_section (hdr, off, TRUE);

	  if (i == SHN_LORESERVE - 1)
	    {
	      i += SHN_HIRESERVE + 1 - SHN_LORESERVE;
	      hdrpp += SHN_HIRESERVE + 1 - SHN_LORESERVE;
	    }
	}
    }
  else
    {
      unsigned int alloc;

      /* Assign file positions for the loaded sections based on the
	 assignment of sections to segments.  */
      if (!assign_file_positions_for_load_sections (abfd, link_info))
	return FALSE;

      /* And for non-load sections.  */
      if (!assign_file_positions_for_non_load_sections (abfd, link_info))
	return FALSE;

      if (bed->elf_backend_modify_program_headers != NULL)
	{
	  if (!(*bed->elf_backend_modify_program_headers) (abfd, link_info))
	    return FALSE;
	}

      /* Write out the program headers.  */
      alloc = tdata->program_header_size / bed->s->sizeof_phdr;
      if (bfd_seek (abfd, (bfd_signed_vma) bed->s->sizeof_ehdr, SEEK_SET) != 0
	  || bed->s->write_out_phdrs (abfd, tdata->phdr, alloc) != 0)
	return FALSE;

      off = tdata->next_file_pos;
    }

  /* Place the section headers.  */
  off = align_file_position (off, 1 << bed->s->log_file_align);
  i_ehdrp->e_shoff = off;
  off += i_ehdrp->e_shnum * i_ehdrp->e_shentsize;

  tdata->next_file_pos = off;

  return TRUE;
}

static bfd_boolean
prep_headers (bfd *abfd)
{
  Elf_Internal_Ehdr *i_ehdrp;	/* Elf file header, internal form */
  Elf_Internal_Phdr *i_phdrp = 0; /* Program header table, internal form */
  Elf_Internal_Shdr **i_shdrp;	/* Section header table, internal form */
  struct elf_strtab_hash *shstrtab;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);

  i_ehdrp = elf_elfheader (abfd);
  i_shdrp = elf_elfsections (abfd);

  shstrtab = _bfd_elf_strtab_init ();
  if (shstrtab == NULL)
    return FALSE;

  elf_shstrtab (abfd) = shstrtab;

  i_ehdrp->e_ident[EI_MAG0] = ELFMAG0;
  i_ehdrp->e_ident[EI_MAG1] = ELFMAG1;
  i_ehdrp->e_ident[EI_MAG2] = ELFMAG2;
  i_ehdrp->e_ident[EI_MAG3] = ELFMAG3;

  i_ehdrp->e_ident[EI_CLASS] = bed->s->elfclass;
  i_ehdrp->e_ident[EI_DATA] =
    bfd_big_endian (abfd) ? ELFDATA2MSB : ELFDATA2LSB;
  i_ehdrp->e_ident[EI_VERSION] = bed->s->ev_current;

  i_ehdrp->e_ident[EI_OSABI] = ELFOSABI_FREEBSD;

  if ((abfd->flags & DYNAMIC) != 0)
    i_ehdrp->e_type = ET_DYN;
  else if ((abfd->flags & EXEC_P) != 0)
    i_ehdrp->e_type = ET_EXEC;
  else if (bfd_get_format (abfd) == bfd_core)
    i_ehdrp->e_type = ET_CORE;
  else
    i_ehdrp->e_type = ET_REL;

  switch (bfd_get_arch (abfd))
    {
    case bfd_arch_unknown:
      i_ehdrp->e_machine = EM_NONE;
      break;

      /* There used to be a long list of cases here, each one setting
	 e_machine to the same EM_* macro #defined as ELF_MACHINE_CODE
	 in the corresponding bfd definition.  To avoid duplication,
	 the switch was removed.  Machines that need special handling
	 can generally do it in elf_backend_final_write_processing(),
	 unless they need the information earlier than the final write.
	 Such need can generally be supplied by replacing the tests for
	 e_machine with the conditions used to determine it.  */
    default:
      i_ehdrp->e_machine = bed->elf_machine_code;
    }

  i_ehdrp->e_version = bed->s->ev_current;
  i_ehdrp->e_ehsize = bed->s->sizeof_ehdr;

  /* No program header, for now.  */
  i_ehdrp->e_phoff = 0;
  i_ehdrp->e_phentsize = 0;
  i_ehdrp->e_phnum = 0;

  /* Each bfd section is section header entry.  */
  i_ehdrp->e_entry = bfd_get_start_address (abfd);
  i_ehdrp->e_shentsize = bed->s->sizeof_shdr;

  /* If we're building an executable, we'll need a program header table.  */
  if (abfd->flags & EXEC_P)
    /* It all happens later.  */
    ;
  else
    {
      i_ehdrp->e_phentsize = 0;
      i_phdrp = 0;
      i_ehdrp->e_phoff = 0;
    }

  elf_tdata (abfd)->symtab_hdr.sh_name =
    (unsigned int) _bfd_elf_strtab_add (shstrtab, ".symtab", FALSE);
  elf_tdata (abfd)->strtab_hdr.sh_name =
    (unsigned int) _bfd_elf_strtab_add (shstrtab, ".strtab", FALSE);
  elf_tdata (abfd)->shstrtab_hdr.sh_name =
    (unsigned int) _bfd_elf_strtab_add (shstrtab, ".shstrtab", FALSE);
  if (elf_tdata (abfd)->symtab_hdr.sh_name == (unsigned int) -1
      || elf_tdata (abfd)->symtab_hdr.sh_name == (unsigned int) -1
      || elf_tdata (abfd)->shstrtab_hdr.sh_name == (unsigned int) -1)
    return FALSE;

  return TRUE;
}

/* Assign file positions for all the reloc sections which are not part
   of the loadable file image.  */

void
_bfd_elf_assign_file_positions_for_relocs (bfd *abfd)
{
  file_ptr off;
  unsigned int i, num_sec;
  Elf_Internal_Shdr **shdrpp;

  off = elf_tdata (abfd)->next_file_pos;

  num_sec = elf_numsections (abfd);
  for (i = 1, shdrpp = elf_elfsections (abfd) + 1; i < num_sec; i++, shdrpp++)
    {
      Elf_Internal_Shdr *shdrp;

      shdrp = *shdrpp;
      if ((shdrp->sh_type == SHT_REL || shdrp->sh_type == SHT_RELA)
	  && shdrp->sh_offset == -1)
	off = _bfd_elf_assign_file_position_for_section (shdrp, off, TRUE);
    }

  elf_tdata (abfd)->next_file_pos = off;
}

bfd_boolean
_bfd_elf_write_object_contents (bfd *abfd)
{
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  Elf_Internal_Ehdr *i_ehdrp;
  Elf_Internal_Shdr **i_shdrp;
  bfd_boolean failed;
  unsigned int count, num_sec;

  if (! abfd->output_has_begun
      && ! _bfd_elf_compute_section_file_positions (abfd, NULL))
    return FALSE;

  i_shdrp = elf_elfsections (abfd);
  i_ehdrp = elf_elfheader (abfd);

  failed = FALSE;
  bfd_map_over_sections (abfd, bed->s->write_relocs, &failed);
  if (failed)
    return FALSE;

  _bfd_elf_assign_file_positions_for_relocs (abfd);

  /* After writing the headers, we need to write the sections too...  */
  num_sec = elf_numsections (abfd);
  for (count = 1; count < num_sec; count++)
    {
      if (bed->elf_backend_section_processing)
	(*bed->elf_backend_section_processing) (abfd, i_shdrp[count]);
      if (i_shdrp[count]->contents)
	{
	  bfd_size_type amt = i_shdrp[count]->sh_size;

	  if (bfd_seek (abfd, i_shdrp[count]->sh_offset, SEEK_SET) != 0
	      || bfd_bwrite (i_shdrp[count]->contents, amt, abfd) != amt)
	    return FALSE;
	}
      if (count == SHN_LORESERVE - 1)
	count += SHN_HIRESERVE + 1 - SHN_LORESERVE;
    }

  /* Write out the section header names.  */
  if (elf_shstrtab (abfd) != NULL
      && (bfd_seek (abfd, elf_tdata (abfd)->shstrtab_hdr.sh_offset, SEEK_SET) != 0
	  || !_bfd_elf_strtab_emit (abfd, elf_shstrtab (abfd))))
    return FALSE;

  if (bed->elf_backend_final_write_processing)
    (*bed->elf_backend_final_write_processing) (abfd,
						elf_tdata (abfd)->linker);

  return bed->s->write_shdrs_and_ehdr (abfd);
}

bfd_boolean
_bfd_elf_write_corefile_contents (bfd *abfd)
{
  /* Hopefully this can be done just like an object file.  */
  return _bfd_elf_write_object_contents (abfd);
}

/* Given a section, search the header to find them.  */

int
_bfd_elf_section_from_bfd_section (bfd *abfd, struct bfd_section *asect)
{
  const struct elf_backend_data *bed;
  int index;

  if (elf_section_data (asect) != NULL
      && elf_section_data (asect)->this_idx != 0)
    return elf_section_data (asect)->this_idx;

  if (bfd_is_abs_section (asect))
    index = SHN_ABS;
  else if (bfd_is_com_section (asect))
    index = SHN_COMMON;
  else if (bfd_is_und_section (asect))
    index = SHN_UNDEF;
  else
    index = -1;

  bed = get_elf_backend_data (abfd);
  if (bed->elf_backend_section_from_bfd_section)
    {
      int retval = index;

      if ((*bed->elf_backend_section_from_bfd_section) (abfd, asect, &retval))
	return retval;
    }

  if (index == -1)
    bfd_set_error (bfd_error_nonrepresentable_section);

  return index;
}

/* Given a BFD symbol, return the index in the ELF symbol table, or -1
   on error.  */

int
_bfd_elf_symbol_from_bfd_symbol (bfd *abfd, asymbol **asym_ptr_ptr)
{
  asymbol *asym_ptr = *asym_ptr_ptr;
  int idx;
  flagword flags = asym_ptr->flags;

  /* When gas creates relocations against local labels, it creates its
     own symbol for the section, but does put the symbol into the
     symbol chain, so udata is 0.  When the linker is generating
     relocatable output, this section symbol may be for one of the
     input sections rather than the output section.  */
  if (asym_ptr->udata.i == 0
      && (flags & BSF_SECTION_SYM)
      && asym_ptr->section)
    {
      asection *sec;
      int indx;

      sec = asym_ptr->section;
      if (sec->owner != abfd && sec->output_section != NULL)
	sec = sec->output_section;
      if (sec->owner == abfd
	  && (indx = sec->index) < elf_num_section_syms (abfd)
	  && elf_section_syms (abfd)[indx] != NULL)
	asym_ptr->udata.i = elf_section_syms (abfd)[indx]->udata.i;
    }

  idx = asym_ptr->udata.i;

  if (idx == 0)
    {
      /* This case can occur when using --strip-symbol on a symbol
	 which is used in a relocation entry.  */
      (*_bfd_error_handler)
	(_("%B: symbol `%s' required but not present"),
	 abfd, bfd_asymbol_name (asym_ptr));
      bfd_set_error (bfd_error_no_symbols);
      return -1;
    }

#if DEBUG & 4
  {
    fprintf (stderr,
	     "elf_symbol_from_bfd_symbol 0x%.8lx, name = %s, sym num = %d, flags = 0x%.8lx%s\n",
	     (long) asym_ptr, asym_ptr->name, idx, flags,
	     elf_symbol_flags (flags));
    fflush (stderr);
  }
#endif

  return idx;
}

/* Rewrite program header information.  */

static bfd_boolean
rewrite_elf_program_header (bfd *ibfd, bfd *obfd)
{
  Elf_Internal_Ehdr *iehdr;
  struct elf_segment_map *map;
  struct elf_segment_map *map_first;
  struct elf_segment_map **pointer_to_map;
  Elf_Internal_Phdr *segment;
  asection *section;
  unsigned int i;
  unsigned int num_segments;
  bfd_boolean phdr_included = FALSE;
  bfd_vma maxpagesize;
  struct elf_segment_map *phdr_adjust_seg = NULL;
  unsigned int phdr_adjust_num = 0;
  const struct elf_backend_data *bed;

  bed = get_elf_backend_data (ibfd);
  iehdr = elf_elfheader (ibfd);

  map_first = NULL;
  pointer_to_map = &map_first;

  num_segments = elf_elfheader (ibfd)->e_phnum;
  maxpagesize = get_elf_backend_data (obfd)->maxpagesize;

  /* Returns the end address of the segment + 1.  */
#define SEGMENT_END(segment, start)					\
  (start + (segment->p_memsz > segment->p_filesz			\
	    ? segment->p_memsz : segment->p_filesz))

#define SECTION_SIZE(section, segment)					\
  (((section->flags & (SEC_HAS_CONTENTS | SEC_THREAD_LOCAL))		\
    != SEC_THREAD_LOCAL || segment->p_type == PT_TLS)			\
   ? section->size : 0)

  /* Returns TRUE if the given section is contained within
     the given segment.  VMA addresses are compared.  */
#define IS_CONTAINED_BY_VMA(section, segment)				\
  (section->vma >= segment->p_vaddr					\
   && (section->vma + SECTION_SIZE (section, segment)			\
       <= (SEGMENT_END (segment, segment->p_vaddr))))

  /* Returns TRUE if the given section is contained within
     the given segment.  LMA addresses are compared.  */
#define IS_CONTAINED_BY_LMA(section, segment, base)			\
  (section->lma >= base							\
   && (section->lma + SECTION_SIZE (section, segment)			\
       <= SEGMENT_END (segment, base)))

  /* Special case: corefile "NOTE" section containing regs, prpsinfo etc.  */
#define IS_COREFILE_NOTE(p, s)						\
  (p->p_type == PT_NOTE							\
   && bfd_get_format (ibfd) == bfd_core					\
   && s->vma == 0 && s->lma == 0					\
   && (bfd_vma) s->filepos >= p->p_offset				\
   && ((bfd_vma) s->filepos + s->size					\
       <= p->p_offset + p->p_filesz))

  /* The complicated case when p_vaddr is 0 is to handle the Solaris
     linker, which generates a PT_INTERP section with p_vaddr and
     p_memsz set to 0.  */
#define IS_SOLARIS_PT_INTERP(p, s)					\
  (p->p_vaddr == 0							\
   && p->p_paddr == 0							\
   && p->p_memsz == 0							\
   && p->p_filesz > 0							\
   && (s->flags & SEC_HAS_CONTENTS) != 0				\
   && s->size > 0							\
   && (bfd_vma) s->filepos >= p->p_offset				\
   && ((bfd_vma) s->filepos + s->size					\
       <= p->p_offset + p->p_filesz))

  /* Decide if the given section should be included in the given segment.
     A section will be included if:
       1. It is within the address space of the segment -- we use the LMA
	  if that is set for the segment and the VMA otherwise,
       2. It is an allocated segment,
       3. There is an output section associated with it,
       4. The section has not already been allocated to a previous segment.
       5. PT_GNU_STACK segments do not include any sections.
       6. PT_TLS segment includes only SHF_TLS sections.
       7. SHF_TLS sections are only in PT_TLS or PT_LOAD segments.
       8. PT_DYNAMIC should not contain empty sections at the beginning
	  (with the possible exception of .dynamic).  */
#define IS_SECTION_IN_INPUT_SEGMENT(section, segment, bed)		\
  ((((segment->p_paddr							\
      ? IS_CONTAINED_BY_LMA (section, segment, segment->p_paddr)	\
      : IS_CONTAINED_BY_VMA (section, segment))				\
     && (section->flags & SEC_ALLOC) != 0)				\
    || IS_COREFILE_NOTE (segment, section))				\
   && segment->p_type != PT_GNU_STACK					\
   && (segment->p_type != PT_TLS					\
       || (section->flags & SEC_THREAD_LOCAL))				\
   && (segment->p_type == PT_LOAD					\
       || segment->p_type == PT_TLS					\
       || (section->flags & SEC_THREAD_LOCAL) == 0)			\
   && (segment->p_type != PT_DYNAMIC					\
       || SECTION_SIZE (section, segment) > 0				\
       || (segment->p_paddr						\
	   ? segment->p_paddr != section->lma				\
	   : segment->p_vaddr != section->vma)				\
       || (strcmp (bfd_get_section_name (ibfd, section), ".dynamic")	\
	   == 0))							\
   && ! section->segment_mark)

/* If the output section of a section in the input segment is NULL,
   it is removed from the corresponding output segment.   */
#define INCLUDE_SECTION_IN_SEGMENT(section, segment, bed)		\
  (IS_SECTION_IN_INPUT_SEGMENT (section, segment, bed)		\
   && section->output_section != NULL)

  /* Returns TRUE iff seg1 starts after the end of seg2.  */
#define SEGMENT_AFTER_SEGMENT(seg1, seg2, field)			\
  (seg1->field >= SEGMENT_END (seg2, seg2->field))

  /* Returns TRUE iff seg1 and seg2 overlap. Segments overlap iff both
     their VMA address ranges and their LMA address ranges overlap.
     It is possible to have overlapping VMA ranges without overlapping LMA
     ranges.  RedBoot images for example can have both .data and .bss mapped
     to the same VMA range, but with the .data section mapped to a different
     LMA.  */
#define SEGMENT_OVERLAPS(seg1, seg2)					\
  (   !(SEGMENT_AFTER_SEGMENT (seg1, seg2, p_vaddr)			\
	|| SEGMENT_AFTER_SEGMENT (seg2, seg1, p_vaddr))			\
   && !(SEGMENT_AFTER_SEGMENT (seg1, seg2, p_paddr)			\
	|| SEGMENT_AFTER_SEGMENT (seg2, seg1, p_paddr)))

  /* Initialise the segment mark field.  */
  for (section = ibfd->sections; section != NULL; section = section->next)
    section->segment_mark = FALSE;

  /* Scan through the segments specified in the program header
     of the input BFD.  For this first scan we look for overlaps
     in the loadable segments.  These can be created by weird
     parameters to objcopy.  Also, fix some solaris weirdness.  */
  for (i = 0, segment = elf_tdata (ibfd)->phdr;
       i < num_segments;
       i++, segment++)
    {
      unsigned int j;
      Elf_Internal_Phdr *segment2;

      if (segment->p_type == PT_INTERP)
	for (section = ibfd->sections; section; section = section->next)
	  if (IS_SOLARIS_PT_INTERP (segment, section))
	    {
	      /* Mininal change so that the normal section to segment
		 assignment code will work.  */
	      segment->p_vaddr = section->vma;
	      break;
	    }

      if (segment->p_type != PT_LOAD)
	continue;

      /* Determine if this segment overlaps any previous segments.  */
      for (j = 0, segment2 = elf_tdata (ibfd)->phdr; j < i; j++, segment2 ++)
	{
	  bfd_signed_vma extra_length;

	  if (segment2->p_type != PT_LOAD
	      || ! SEGMENT_OVERLAPS (segment, segment2))
	    continue;

	  /* Merge the two segments together.  */
	  if (segment2->p_vaddr < segment->p_vaddr)
	    {
	      /* Extend SEGMENT2 to include SEGMENT and then delete
		 SEGMENT.  */
	      extra_length =
		SEGMENT_END (segment, segment->p_vaddr)
		- SEGMENT_END (segment2, segment2->p_vaddr);

	      if (extra_length > 0)
		{
		  segment2->p_memsz  += extra_length;
		  segment2->p_filesz += extra_length;
		}

	      segment->p_type = PT_NULL;

	      /* Since we have deleted P we must restart the outer loop.  */
	      i = 0;
	      segment = elf_tdata (ibfd)->phdr;
	      break;
	    }
	  else
	    {
	      /* Extend SEGMENT to include SEGMENT2 and then delete
		 SEGMENT2.  */
	      extra_length =
		SEGMENT_END (segment2, segment2->p_vaddr)
		- SEGMENT_END (segment, segment->p_vaddr);

	      if (extra_length > 0)
		{
		  segment->p_memsz  += extra_length;
		  segment->p_filesz += extra_length;
		}

	      segment2->p_type = PT_NULL;
	    }
	}
    }

  /* The second scan attempts to assign sections to segments.  */
  for (i = 0, segment = elf_tdata (ibfd)->phdr;
       i < num_segments;
       i ++, segment ++)
    {
      unsigned int  section_count;
      asection **   sections;
      asection *    output_section;
      unsigned int  isec;
      bfd_vma       matching_lma;
      bfd_vma       suggested_lma;
      unsigned int  j;
      bfd_size_type amt;
      asection *    first_section;

      if (segment->p_type == PT_NULL)
	continue;

      first_section = NULL;
      /* Compute how many sections might be placed into this segment.  */
      for (section = ibfd->sections, section_count = 0;
	   section != NULL;
	   section = section->next)
	{
	  /* Find the first section in the input segment, which may be
	     removed from the corresponding output segment.   */
	  if (IS_SECTION_IN_INPUT_SEGMENT (section, segment, bed))
	    {
	      if (first_section == NULL)
		first_section = section;
	      if (section->output_section != NULL)
		++section_count;
	    }
	}

      /* Allocate a segment map big enough to contain
	 all of the sections we have selected.  */
      amt = sizeof (struct elf_segment_map);
      amt += ((bfd_size_type) section_count - 1) * sizeof (asection *);
      map = bfd_zalloc (obfd, amt);
      if (map == NULL)
	return FALSE;

      /* Initialise the fields of the segment map.  Default to
	 using the physical address of the segment in the input BFD.  */
      map->next          = NULL;
      map->p_type        = segment->p_type;
      map->p_flags       = segment->p_flags;
      map->p_flags_valid = 1;

      /* If the first section in the input segment is removed, there is
	 no need to preserve segment physical address in the corresponding
	 output segment.  */
      if (!first_section || first_section->output_section != NULL)
	{
	  map->p_paddr = segment->p_paddr;
	  map->p_paddr_valid = 1;
	}

      /* Determine if this segment contains the ELF file header
	 and if it contains the program headers themselves.  */
      map->includes_filehdr = (segment->p_offset == 0
			       && segment->p_filesz >= iehdr->e_ehsize);

      map->includes_phdrs = 0;

      if (! phdr_included || segment->p_type != PT_LOAD)
	{
	  map->includes_phdrs =
	    (segment->p_offset <= (bfd_vma) iehdr->e_phoff
	     && (segment->p_offset + segment->p_filesz
		 >= ((bfd_vma) iehdr->e_phoff
		     + iehdr->e_phnum * iehdr->e_phentsize)));

	  if (segment->p_type == PT_LOAD && map->includes_phdrs)
	    phdr_included = TRUE;
	}

      if (section_count == 0)
	{
	  /* Special segments, such as the PT_PHDR segment, may contain
	     no sections, but ordinary, loadable segments should contain
	     something.  They are allowed by the ELF spec however, so only
	     a warning is produced.  */
	  if (segment->p_type == PT_LOAD)
	    (*_bfd_error_handler)
	      (_("%B: warning: Empty loadable segment detected, is this intentional ?\n"),
	       ibfd);

	  map->count = 0;
	  *pointer_to_map = map;
	  pointer_to_map = &map->next;

	  continue;
	}

      /* Now scan the sections in the input BFD again and attempt
	 to add their corresponding output sections to the segment map.
	 The problem here is how to handle an output section which has
	 been moved (ie had its LMA changed).  There are four possibilities:

	 1. None of the sections have been moved.
	    In this case we can continue to use the segment LMA from the
	    input BFD.

	 2. All of the sections have been moved by the same amount.
	    In this case we can change the segment's LMA to match the LMA
	    of the first section.

	 3. Some of the sections have been moved, others have not.
	    In this case those sections which have not been moved can be
	    placed in the current segment which will have to have its size,
	    and possibly its LMA changed, and a new segment or segments will
	    have to be created to contain the other sections.

	 4. The sections have been moved, but not by the same amount.
	    In this case we can change the segment's LMA to match the LMA
	    of the first section and we will have to create a new segment
	    or segments to contain the other sections.

	 In order to save time, we allocate an array to hold the section
	 pointers that we are interested in.  As these sections get assigned
	 to a segment, they are removed from this array.  */

      /* Gcc 2.96 miscompiles this code on mips. Don't do casting here
	 to work around this long long bug.  */
      sections = bfd_malloc2 (section_count, sizeof (asection *));
      if (sections == NULL)
	return FALSE;

      /* Step One: Scan for segment vs section LMA conflicts.
	 Also add the sections to the section array allocated above.
	 Also add the sections to the current segment.  In the common
	 case, where the sections have not been moved, this means that
	 we have completely filled the segment, and there is nothing
	 more to do.  */
      isec = 0;
      matching_lma = 0;
      suggested_lma = 0;

      for (j = 0, section = ibfd->sections;
	   section != NULL;
	   section = section->next)
	{
	  if (INCLUDE_SECTION_IN_SEGMENT (section, segment, bed))
	    {
	      output_section = section->output_section;

	      sections[j ++] = section;

	      /* The Solaris native linker always sets p_paddr to 0.
		 We try to catch that case here, and set it to the
		 correct value.  Note - some backends require that
		 p_paddr be left as zero.  */
	      if (segment->p_paddr == 0
		  && segment->p_vaddr != 0
		  && (! bed->want_p_paddr_set_to_zero)
		  && isec == 0
		  && output_section->lma != 0
		  && (output_section->vma == (segment->p_vaddr
					      + (map->includes_filehdr
						 ? iehdr->e_ehsize
						 : 0)
					      + (map->includes_phdrs
						 ? (iehdr->e_phnum
						    * iehdr->e_phentsize)
						 : 0))))
		map->p_paddr = segment->p_vaddr;

	      /* Match up the physical address of the segment with the
		 LMA address of the output section.  */
	      if (IS_CONTAINED_BY_LMA (output_section, segment, map->p_paddr)
		  || IS_COREFILE_NOTE (segment, section)
		  || (bed->want_p_paddr_set_to_zero &&
		      IS_CONTAINED_BY_VMA (output_section, segment)))
		{
		  if (matching_lma == 0)
		    matching_lma = output_section->lma;

		  /* We assume that if the section fits within the segment
		     then it does not overlap any other section within that
		     segment.  */
		  map->sections[isec ++] = output_section;
		}
	      else if (suggested_lma == 0)
		suggested_lma = output_section->lma;
	    }
	}

      BFD_ASSERT (j == section_count);

      /* Step Two: Adjust the physical address of the current segment,
	 if necessary.  */
      if (isec == section_count)
	{
	  /* All of the sections fitted within the segment as currently
	     specified.  This is the default case.  Add the segment to
	     the list of built segments and carry on to process the next
	     program header in the input BFD.  */
	  map->count = section_count;
	  *pointer_to_map = map;
	  pointer_to_map = &map->next;

	  if (matching_lma != map->p_paddr
	      && !map->includes_filehdr && !map->includes_phdrs)
	    /* There is some padding before the first section in the
	       segment.  So, we must account for that in the output
	       segment's vma.  */
	    map->p_vaddr_offset = matching_lma - map->p_paddr;

	  free (sections);
	  continue;
	}
      else
	{
	  if (matching_lma != 0)
	    {
	      /* At least one section fits inside the current segment.
		 Keep it, but modify its physical address to match the
		 LMA of the first section that fitted.  */
	      map->p_paddr = matching_lma;
	    }
	  else
	    {
	      /* None of the sections fitted inside the current segment.
		 Change the current segment's physical address to match
		 the LMA of the first section.  */
	      map->p_paddr = suggested_lma;
	    }

	  /* Offset the segment physical address from the lma
	     to allow for space taken up by elf headers.  */
	  if (map->includes_filehdr)
	    map->p_paddr -= iehdr->e_ehsize;

	  if (map->includes_phdrs)
	    {
	      map->p_paddr -= iehdr->e_phnum * iehdr->e_phentsize;

	      /* iehdr->e_phnum is just an estimate of the number
		 of program headers that we will need.  Make a note
		 here of the number we used and the segment we chose
		 to hold these headers, so that we can adjust the
		 offset when we know the correct value.  */
	      phdr_adjust_num = iehdr->e_phnum;
	      phdr_adjust_seg = map;
	    }
	}

      /* Step Three: Loop over the sections again, this time assigning
	 those that fit to the current segment and removing them from the
	 sections array; but making sure not to leave large gaps.  Once all
	 possible sections have been assigned to the current segment it is
	 added to the list of built segments and if sections still remain
	 to be assigned, a new segment is constructed before repeating
	 the loop.  */
      isec = 0;
      do
	{
	  map->count = 0;
	  suggested_lma = 0;

	  /* Fill the current segment with sections that fit.  */
	  for (j = 0; j < section_count; j++)
	    {
	      section = sections[j];

	      if (section == NULL)
		continue;

	      output_section = section->output_section;

	      BFD_ASSERT (output_section != NULL);

	      if (IS_CONTAINED_BY_LMA (output_section, segment, map->p_paddr)
		  || IS_COREFILE_NOTE (segment, section))
		{
		  if (map->count == 0)
		    {
		      /* If the first section in a segment does not start at
			 the beginning of the segment, then something is
			 wrong.  */
		      if (output_section->lma !=
			  (map->p_paddr
			   + (map->includes_filehdr ? iehdr->e_ehsize : 0)
			   + (map->includes_phdrs
			      ? iehdr->e_phnum * iehdr->e_phentsize
			      : 0)))
			abort ();
		    }
		  else
		    {
		      asection * prev_sec;

		      prev_sec = map->sections[map->count - 1];

		      /* If the gap between the end of the previous section
			 and the start of this section is more than
			 maxpagesize then we need to start a new segment.  */
		      if ((BFD_ALIGN (prev_sec->lma + prev_sec->size,
				      maxpagesize)
			   < BFD_ALIGN (output_section->lma, maxpagesize))
			  || ((prev_sec->lma + prev_sec->size)
			      > output_section->lma))
			{
			  if (suggested_lma == 0)
			    suggested_lma = output_section->lma;

			  continue;
			}
		    }

		  map->sections[map->count++] = output_section;
		  ++isec;
		  sections[j] = NULL;
		  section->segment_mark = TRUE;
		}
	      else if (suggested_lma == 0)
		suggested_lma = output_section->lma;
	    }

	  BFD_ASSERT (map->count > 0);

	  /* Add the current segment to the list of built segments.  */
	  *pointer_to_map = map;
	  pointer_to_map = &map->next;

	  if (isec < section_count)
	    {
	      /* We still have not allocated all of the sections to
		 segments.  Create a new segment here, initialise it
		 and carry on looping.  */
	      amt = sizeof (struct elf_segment_map);
	      amt += ((bfd_size_type) section_count - 1) * sizeof (asection *);
	      map = bfd_alloc (obfd, amt);
	      if (map == NULL)
		{
		  free (sections);
		  return FALSE;
		}

	      /* Initialise the fields of the segment map.  Set the physical
		 physical address to the LMA of the first section that has
		 not yet been assigned.  */
	      map->next             = NULL;
	      map->p_type           = segment->p_type;
	      map->p_flags          = segment->p_flags;
	      map->p_flags_valid    = 1;
	      map->p_paddr          = suggested_lma;
	      map->p_paddr_valid    = 1;
	      map->includes_filehdr = 0;
	      map->includes_phdrs   = 0;
	    }
	}
      while (isec < section_count);

      free (sections);
    }

  /* The Solaris linker creates program headers in which all the
     p_paddr fields are zero.  When we try to objcopy or strip such a
     file, we get confused.  Check for this case, and if we find it
     reset the p_paddr_valid fields.  */
  for (map = map_first; map != NULL; map = map->next)
    if (map->p_paddr != 0)
      break;
  if (map == NULL)
    for (map = map_first; map != NULL; map = map->next)
      map->p_paddr_valid = 0;

  elf_tdata (obfd)->segment_map = map_first;

  /* If we had to estimate the number of program headers that were
     going to be needed, then check our estimate now and adjust
     the offset if necessary.  */
  if (phdr_adjust_seg != NULL)
    {
      unsigned int count;

      for (count = 0, map = map_first; map != NULL; map = map->next)
	count++;

      if (count > phdr_adjust_num)
	phdr_adjust_seg->p_paddr
	  -= (count - phdr_adjust_num) * iehdr->e_phentsize;
    }

#undef SEGMENT_END
#undef SECTION_SIZE
#undef IS_CONTAINED_BY_VMA
#undef IS_CONTAINED_BY_LMA
#undef IS_COREFILE_NOTE
#undef IS_SOLARIS_PT_INTERP
#undef IS_SECTION_IN_INPUT_SEGMENT
#undef INCLUDE_SECTION_IN_SEGMENT
#undef SEGMENT_AFTER_SEGMENT
#undef SEGMENT_OVERLAPS
  return TRUE;
}

/* Copy ELF program header information.  */

static bfd_boolean
copy_elf_program_header (bfd *ibfd, bfd *obfd)
{
  Elf_Internal_Ehdr *iehdr;
  struct elf_segment_map *map;
  struct elf_segment_map *map_first;
  struct elf_segment_map **pointer_to_map;
  Elf_Internal_Phdr *segment;
  unsigned int i;
  unsigned int num_segments;
  bfd_boolean phdr_included = FALSE;

  iehdr = elf_elfheader (ibfd);

  map_first = NULL;
  pointer_to_map = &map_first;

  num_segments = elf_elfheader (ibfd)->e_phnum;
  for (i = 0, segment = elf_tdata (ibfd)->phdr;
       i < num_segments;
       i++, segment++)
    {
      asection *section;
      unsigned int section_count;
      bfd_size_type amt;
      Elf_Internal_Shdr *this_hdr;
      asection *first_section = NULL;

      /* FIXME: Do we need to copy PT_NULL segment?  */
      if (segment->p_type == PT_NULL)
	continue;

      /* Compute how many sections are in this segment.  */
      for (section = ibfd->sections, section_count = 0;
	   section != NULL;
	   section = section->next)
	{
	  this_hdr = &(elf_section_data(section)->this_hdr);
	  if (ELF_IS_SECTION_IN_SEGMENT_FILE (this_hdr, segment))
	    {
	      if (!first_section)
		first_section = section;
	      section_count++;
	    }
	}

      /* Allocate a segment map big enough to contain
	 all of the sections we have selected.  */
      amt = sizeof (struct elf_segment_map);
      if (section_count != 0)
	amt += ((bfd_size_type) section_count - 1) * sizeof (asection *);
      map = bfd_zalloc (obfd, amt);
      if (map == NULL)
	return FALSE;

      /* Initialize the fields of the output segment map with the
	 input segment.  */
      map->next = NULL;
      map->p_type = segment->p_type;
      map->p_flags = segment->p_flags;
      map->p_flags_valid = 1;
      map->p_paddr = segment->p_paddr;
      map->p_paddr_valid = 1;
      map->p_align = segment->p_align;
      map->p_align_valid = 1;
      map->p_vaddr_offset = 0;

      /* Determine if this segment contains the ELF file header
	 and if it contains the program headers themselves.  */
      map->includes_filehdr = (segment->p_offset == 0
			       && segment->p_filesz >= iehdr->e_ehsize);

      map->includes_phdrs = 0;
      if (! phdr_included || segment->p_type != PT_LOAD)
	{
	  map->includes_phdrs =
	    (segment->p_offset <= (bfd_vma) iehdr->e_phoff
	     && (segment->p_offset + segment->p_filesz
		 >= ((bfd_vma) iehdr->e_phoff
		     + iehdr->e_phnum * iehdr->e_phentsize)));

	  if (segment->p_type == PT_LOAD && map->includes_phdrs)
	    phdr_included = TRUE;
	}

      if (!map->includes_phdrs && !map->includes_filehdr)
	/* There is some other padding before the first section.  */
	map->p_vaddr_offset = ((first_section ? first_section->lma : 0)
			       - segment->p_paddr);

      if (section_count != 0)
	{
	  unsigned int isec = 0;

	  for (section = first_section;
	       section != NULL;
	       section = section->next)
	    {
	      this_hdr = &(elf_section_data(section)->this_hdr);
	      if (ELF_IS_SECTION_IN_SEGMENT_FILE (this_hdr, segment))
		{
		  map->sections[isec++] = section->output_section;
		  if (isec == section_count)
		    break;
		}
	    }
	}

      map->count = section_count;
      *pointer_to_map = map;
      pointer_to_map = &map->next;
    }

  elf_tdata (obfd)->segment_map = map_first;
  return TRUE;
}

/* Copy private BFD data.  This copies or rewrites ELF program header
   information.  */

static bfd_boolean
copy_private_bfd_data (bfd *ibfd, bfd *obfd)
{
  if (bfd_get_flavour (ibfd) != bfd_target_elf_flavour
      || bfd_get_flavour (obfd) != bfd_target_elf_flavour)
    return TRUE;

  if (elf_tdata (ibfd)->phdr == NULL)
    return TRUE;

  if (ibfd->xvec == obfd->xvec)
    {
      /* Check to see if any sections in the input BFD
	 covered by ELF program header have changed.  */
      Elf_Internal_Phdr *segment;
      asection *section, *osec;
      unsigned int i, num_segments;
      Elf_Internal_Shdr *this_hdr;

      /* Initialize the segment mark field.  */
      for (section = obfd->sections; section != NULL;
	   section = section->next)
	section->segment_mark = FALSE;

      num_segments = elf_elfheader (ibfd)->e_phnum;
      for (i = 0, segment = elf_tdata (ibfd)->phdr;
	   i < num_segments;
	   i++, segment++)
	{
	  /* PR binutils/3535.  The Solaris linker always sets the p_paddr
	     and p_memsz fields of special segments (DYNAMIC, INTERP) to 0
	     which severly confuses things, so always regenerate the segment
	     map in this case.  */
	  if (segment->p_paddr == 0
	      && segment->p_memsz == 0
	      && (segment->p_type == PT_INTERP || segment->p_type == PT_DYNAMIC))
	    goto rewrite;

	  for (section = ibfd->sections;
	       section != NULL; section = section->next)
	    {
	      /* We mark the output section so that we know it comes
		 from the input BFD.  */
	      osec = section->output_section;
	      if (osec)
		osec->segment_mark = TRUE;

	      /* Check if this section is covered by the segment.  */
	      this_hdr = &(elf_section_data(section)->this_hdr);
	      if (ELF_IS_SECTION_IN_SEGMENT_FILE (this_hdr, segment))
		{
		  /* FIXME: Check if its output section is changed or
		     removed.  What else do we need to check?  */
		  if (osec == NULL
		      || section->flags != osec->flags
		      || section->lma != osec->lma
		      || section->vma != osec->vma
		      || section->size != osec->size
		      || section->rawsize != osec->rawsize
		      || section->alignment_power != osec->alignment_power)
		    goto rewrite;
		}
	    }
	}

      /* Check to see if any output section do not come from the
	 input BFD.  */
      for (section = obfd->sections; section != NULL;
	   section = section->next)
	{
	  if (section->segment_mark == FALSE)
	    goto rewrite;
	  else
	    section->segment_mark = FALSE;
	}

      return copy_elf_program_header (ibfd, obfd);
    }

rewrite:
  return rewrite_elf_program_header (ibfd, obfd);
}

/* Initialize private output section information from input section.  */

bfd_boolean
_bfd_elf_init_private_section_data (bfd *ibfd,
				    asection *isec,
				    bfd *obfd,
				    asection *osec,
				    struct bfd_link_info *link_info)

{
  Elf_Internal_Shdr *ihdr, *ohdr;
  bfd_boolean need_group = link_info == NULL || link_info->relocatable;

  if (ibfd->xvec->flavour != bfd_target_elf_flavour
      || obfd->xvec->flavour != bfd_target_elf_flavour)
    return TRUE;

  /* Don't copy the output ELF section type from input if the
     output BFD section flags have been set to something different.
     elf_fake_sections will set ELF section type based on BFD
     section flags.  */
  if (elf_section_type (osec) == SHT_NULL
      && (osec->flags == isec->flags || !osec->flags))
    elf_section_type (osec) = elf_section_type (isec);

  /* FIXME: Is this correct for all OS/PROC specific flags?  */
  elf_section_flags (osec) |= (elf_section_flags (isec)
			       & (SHF_MASKOS | SHF_MASKPROC));

  /* Set things up for objcopy and relocatable link.  The output
     SHT_GROUP section will have its elf_next_in_group pointing back
     to the input group members.  Ignore linker created group section.
     See elfNN_ia64_object_p in elfxx-ia64.c.  */
  if (need_group)
    {
      if (elf_sec_group (isec) == NULL
	  || (elf_sec_group (isec)->flags & SEC_LINKER_CREATED) == 0)
	{
	  if (elf_section_flags (isec) & SHF_GROUP)
	    elf_section_flags (osec) |= SHF_GROUP;
	  elf_next_in_group (osec) = elf_next_in_group (isec);
	  elf_group_name (osec) = elf_group_name (isec);
	}
    }

  ihdr = &elf_section_data (isec)->this_hdr;

  /* We need to handle elf_linked_to_section for SHF_LINK_ORDER. We
     don't use the output section of the linked-to section since it
     may be NULL at this point.  */
  if ((ihdr->sh_flags & SHF_LINK_ORDER) != 0)
    {
      ohdr = &elf_section_data (osec)->this_hdr;
      ohdr->sh_flags |= SHF_LINK_ORDER;
      elf_linked_to_section (osec) = elf_linked_to_section (isec);
    }

  osec->use_rela_p = isec->use_rela_p;

  return TRUE;
}

/* Copy private section information.  This copies over the entsize
   field, and sometimes the info field.  */

bfd_boolean
_bfd_elf_copy_private_section_data (bfd *ibfd,
				    asection *isec,
				    bfd *obfd,
				    asection *osec)
{
  Elf_Internal_Shdr *ihdr, *ohdr;

  if (ibfd->xvec->flavour != bfd_target_elf_flavour
      || obfd->xvec->flavour != bfd_target_elf_flavour)
    return TRUE;

  ihdr = &elf_section_data (isec)->this_hdr;
  ohdr = &elf_section_data (osec)->this_hdr;

  ohdr->sh_entsize = ihdr->sh_entsize;

  if (ihdr->sh_type == SHT_SYMTAB
      || ihdr->sh_type == SHT_DYNSYM
      || ihdr->sh_type == SHT_GNU_verneed
      || ihdr->sh_type == SHT_GNU_verdef)
    ohdr->sh_info = ihdr->sh_info;

  return _bfd_elf_init_private_section_data (ibfd, isec, obfd, osec,
					     NULL);
}

/* Copy private header information.  */

bfd_boolean
_bfd_elf_copy_private_header_data (bfd *ibfd, bfd *obfd)
{
  asection *isec;

  if (bfd_get_flavour (ibfd) != bfd_target_elf_flavour
      || bfd_get_flavour (obfd) != bfd_target_elf_flavour)
    return TRUE;

  /* Copy over private BFD data if it has not already been copied.
     This must be done here, rather than in the copy_private_bfd_data
     entry point, because the latter is called after the section
     contents have been set, which means that the program headers have
     already been worked out.  */
  if (elf_tdata (obfd)->segment_map == NULL && elf_tdata (ibfd)->phdr != NULL)
    {
      if (! copy_private_bfd_data (ibfd, obfd))
	return FALSE;
    }

  /* _bfd_elf_copy_private_section_data copied over the SHF_GROUP flag
     but this might be wrong if we deleted the group section.  */
  for (isec = ibfd->sections; isec != NULL; isec = isec->next)
    if (elf_section_type (isec) == SHT_GROUP
	&& isec->output_section == NULL)
      {
	asection *first = elf_next_in_group (isec);
	asection *s = first;
	while (s != NULL)
	  {
	    if (s->output_section != NULL)
	      {
		elf_section_flags (s->output_section) &= ~SHF_GROUP;
		elf_group_name (s->output_section) = NULL;
	      }
	    s = elf_next_in_group (s);
	    if (s == first)
	      break;
	  }
      }

  return TRUE;
}

/* Copy private symbol information.  If this symbol is in a section
   which we did not map into a BFD section, try to map the section
   index correctly.  We use special macro definitions for the mapped
   section indices; these definitions are interpreted by the
   swap_out_syms function.  */

#define MAP_ONESYMTAB (SHN_HIOS + 1)
#define MAP_DYNSYMTAB (SHN_HIOS + 2)
#define MAP_STRTAB    (SHN_HIOS + 3)
#define MAP_SHSTRTAB  (SHN_HIOS + 4)
#define MAP_SYM_SHNDX (SHN_HIOS + 5)

bfd_boolean
_bfd_elf_copy_private_symbol_data (bfd *ibfd,
				   asymbol *isymarg,
				   bfd *obfd,
				   asymbol *osymarg)
{
  elf_symbol_type *isym, *osym;

  if (bfd_get_flavour (ibfd) != bfd_target_elf_flavour
      || bfd_get_flavour (obfd) != bfd_target_elf_flavour)
    return TRUE;

  isym = elf_symbol_from (ibfd, isymarg);
  osym = elf_symbol_from (obfd, osymarg);

  if (isym != NULL
      && osym != NULL
      && bfd_is_abs_section (isym->symbol.section))
    {
      unsigned int shndx;

      shndx = isym->internal_elf_sym.st_shndx;
      if (shndx == elf_onesymtab (ibfd))
	shndx = MAP_ONESYMTAB;
      else if (shndx == elf_dynsymtab (ibfd))
	shndx = MAP_DYNSYMTAB;
      else if (shndx == elf_tdata (ibfd)->strtab_section)
	shndx = MAP_STRTAB;
      else if (shndx == elf_tdata (ibfd)->shstrtab_section)
	shndx = MAP_SHSTRTAB;
      else if (shndx == elf_tdata (ibfd)->symtab_shndx_section)
	shndx = MAP_SYM_SHNDX;
      osym->internal_elf_sym.st_shndx = shndx;
    }

  return TRUE;
}

/* Swap out the symbols.  */

static bfd_boolean
swap_out_syms (bfd *abfd,
	       struct bfd_strtab_hash **sttp,
	       int relocatable_p)
{
  const struct elf_backend_data *bed;
  int symcount;
  asymbol **syms;
  struct bfd_strtab_hash *stt;
  Elf_Internal_Shdr *symtab_hdr;
  Elf_Internal_Shdr *symtab_shndx_hdr;
  Elf_Internal_Shdr *symstrtab_hdr;
  bfd_byte *outbound_syms;
  bfd_byte *outbound_shndx;
  int idx;
  bfd_size_type amt;
  bfd_boolean name_local_sections;

  if (!elf_map_symbols (abfd))
    return FALSE;

  /* Dump out the symtabs.  */
  stt = _bfd_elf_stringtab_init ();
  if (stt == NULL)
    return FALSE;

  bed = get_elf_backend_data (abfd);
  symcount = bfd_get_symcount (abfd);
  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  symtab_hdr->sh_type = SHT_SYMTAB;
  symtab_hdr->sh_entsize = bed->s->sizeof_sym;
  symtab_hdr->sh_size = symtab_hdr->sh_entsize * (symcount + 1);
  symtab_hdr->sh_info = elf_num_locals (abfd) + 1;
  symtab_hdr->sh_addralign = 1 << bed->s->log_file_align;

  symstrtab_hdr = &elf_tdata (abfd)->strtab_hdr;
  symstrtab_hdr->sh_type = SHT_STRTAB;

  outbound_syms = bfd_alloc2 (abfd, 1 + symcount, bed->s->sizeof_sym);
  if (outbound_syms == NULL)
    {
      _bfd_stringtab_free (stt);
      return FALSE;
    }
  symtab_hdr->contents = outbound_syms;

  outbound_shndx = NULL;
  symtab_shndx_hdr = &elf_tdata (abfd)->symtab_shndx_hdr;
  if (symtab_shndx_hdr->sh_name != 0)
    {
      amt = (bfd_size_type) (1 + symcount) * sizeof (Elf_External_Sym_Shndx);
      outbound_shndx = bfd_zalloc2 (abfd, 1 + symcount,
				    sizeof (Elf_External_Sym_Shndx));
      if (outbound_shndx == NULL)
	{
	  _bfd_stringtab_free (stt);
	  return FALSE;
	}

      symtab_shndx_hdr->contents = outbound_shndx;
      symtab_shndx_hdr->sh_type = SHT_SYMTAB_SHNDX;
      symtab_shndx_hdr->sh_size = amt;
      symtab_shndx_hdr->sh_addralign = sizeof (Elf_External_Sym_Shndx);
      symtab_shndx_hdr->sh_entsize = sizeof (Elf_External_Sym_Shndx);
    }

  /* Now generate the data (for "contents").  */
  {
    /* Fill in zeroth symbol and swap it out.  */
    Elf_Internal_Sym sym;
    sym.st_name = 0;
    sym.st_value = 0;
    sym.st_size = 0;
    sym.st_info = 0;
    sym.st_other = 0;
    sym.st_shndx = SHN_UNDEF;
    bed->s->swap_symbol_out (abfd, &sym, outbound_syms, outbound_shndx);
    outbound_syms += bed->s->sizeof_sym;
    if (outbound_shndx != NULL)
      outbound_shndx += sizeof (Elf_External_Sym_Shndx);
  }

  name_local_sections
    = (bed->elf_backend_name_local_section_symbols
       && bed->elf_backend_name_local_section_symbols (abfd));

  syms = bfd_get_outsymbols (abfd);
  for (idx = 0; idx < symcount; idx++)
    {
      Elf_Internal_Sym sym;
      bfd_vma value = syms[idx]->value;
      elf_symbol_type *type_ptr;
      flagword flags = syms[idx]->flags;
      int type;

      if (!name_local_sections
	  && (flags & (BSF_SECTION_SYM | BSF_GLOBAL)) == BSF_SECTION_SYM)
	{
	  /* Local section symbols have no name.  */
	  sym.st_name = 0;
	}
      else
	{
	  sym.st_name = (unsigned long) _bfd_stringtab_add (stt,
							    syms[idx]->name,
							    TRUE, FALSE);
	  if (sym.st_name == (unsigned long) -1)
	    {
	      _bfd_stringtab_free (stt);
	      return FALSE;
	    }
	}

      type_ptr = elf_symbol_from (abfd, syms[idx]);

      if ((flags & BSF_SECTION_SYM) == 0
	  && bfd_is_com_section (syms[idx]->section))
	{
	  /* ELF common symbols put the alignment into the `value' field,
	     and the size into the `size' field.  This is backwards from
	     how BFD handles it, so reverse it here.  */
	  sym.st_size = value;
	  if (type_ptr == NULL
	      || type_ptr->internal_elf_sym.st_value == 0)
	    sym.st_value = value >= 16 ? 16 : (1 << bfd_log2 (value));
	  else
	    sym.st_value = type_ptr->internal_elf_sym.st_value;
	  sym.st_shndx = _bfd_elf_section_from_bfd_section
	    (abfd, syms[idx]->section);
	}
      else
	{
	  asection *sec = syms[idx]->section;
	  int shndx;

	  if (sec->output_section)
	    {
	      value += sec->output_offset;
	      sec = sec->output_section;
	    }

	  /* Don't add in the section vma for relocatable output.  */
	  if (! relocatable_p)
	    value += sec->vma;
	  sym.st_value = value;
	  sym.st_size = type_ptr ? type_ptr->internal_elf_sym.st_size : 0;

	  if (bfd_is_abs_section (sec)
	      && type_ptr != NULL
	      && type_ptr->internal_elf_sym.st_shndx != 0)
	    {
	      /* This symbol is in a real ELF section which we did
		 not create as a BFD section.  Undo the mapping done
		 by copy_private_symbol_data.  */
	      shndx = type_ptr->internal_elf_sym.st_shndx;
	      switch (shndx)
		{
		case MAP_ONESYMTAB:
		  shndx = elf_onesymtab (abfd);
		  break;
		case MAP_DYNSYMTAB:
		  shndx = elf_dynsymtab (abfd);
		  break;
		case MAP_STRTAB:
		  shndx = elf_tdata (abfd)->strtab_section;
		  break;
		case MAP_SHSTRTAB:
		  shndx = elf_tdata (abfd)->shstrtab_section;
		  break;
		case MAP_SYM_SHNDX:
		  shndx = elf_tdata (abfd)->symtab_shndx_section;
		  break;
		default:
		  break;
		}
	    }
	  else
	    {
	      shndx = _bfd_elf_section_from_bfd_section (abfd, sec);

	      if (shndx == -1)
		{
		  asection *sec2;

		  /* Writing this would be a hell of a lot easier if
		     we had some decent documentation on bfd, and
		     knew what to expect of the library, and what to
		     demand of applications.  For example, it
		     appears that `objcopy' might not set the
		     section of a symbol to be a section that is
		     actually in the output file.  */
		  sec2 = bfd_get_section_by_name (abfd, sec->name);
		  if (sec2 == NULL)
		    {
		      _bfd_error_handler (_("\
Unable to find equivalent output section for symbol '%s' from section '%s'"),
					  syms[idx]->name ? syms[idx]->name : "<Local sym>",
					  sec->name);
		      bfd_set_error (bfd_error_invalid_operation);
		      _bfd_stringtab_free (stt);
		      return FALSE;
		    }

		  shndx = _bfd_elf_section_from_bfd_section (abfd, sec2);
		  BFD_ASSERT (shndx != -1);
		}
	    }

	  sym.st_shndx = shndx;
	}

      if ((flags & BSF_THREAD_LOCAL) != 0)
	type = STT_TLS;
      else if ((flags & BSF_FUNCTION) != 0)
	type = STT_FUNC;
      else if ((flags & BSF_OBJECT) != 0)
	type = STT_OBJECT;
      else if ((flags & BSF_RELC) != 0)
	type = STT_RELC;
      else if ((flags & BSF_SRELC) != 0)
	type = STT_SRELC;
      else
	type = STT_NOTYPE;

      if (syms[idx]->section->flags & SEC_THREAD_LOCAL)
	type = STT_TLS;

      /* Processor-specific types.  */
      if (type_ptr != NULL
	  && bed->elf_backend_get_symbol_type)
	type = ((*bed->elf_backend_get_symbol_type)
		(&type_ptr->internal_elf_sym, type));

      if (flags & BSF_SECTION_SYM)
	{
	  if (flags & BSF_GLOBAL)
	    sym.st_info = ELF_ST_INFO (STB_GLOBAL, STT_SECTION);
	  else
	    sym.st_info = ELF_ST_INFO (STB_LOCAL, STT_SECTION);
	}
      else if (bfd_is_com_section (syms[idx]->section))
	sym.st_info = ELF_ST_INFO (STB_GLOBAL, type);
      else if (bfd_is_und_section (syms[idx]->section))
	sym.st_info = ELF_ST_INFO (((flags & BSF_WEAK)
				    ? STB_WEAK
				    : STB_GLOBAL),
				   type);
      else if (flags & BSF_FILE)
	sym.st_info = ELF_ST_INFO (STB_LOCAL, STT_FILE);
      else
	{
	  int bind = STB_LOCAL;

	  if (flags & BSF_LOCAL)
	    bind = STB_LOCAL;
	  else if (flags & BSF_WEAK)
	    bind = STB_WEAK;
	  else if (flags & BSF_GLOBAL)
	    bind = STB_GLOBAL;

	  sym.st_info = ELF_ST_INFO (bind, type);
	}

      if (type_ptr != NULL)
	sym.st_other = type_ptr->internal_elf_sym.st_other;
      else
	sym.st_other = 0;

      bed->s->swap_symbol_out (abfd, &sym, outbound_syms, outbound_shndx);
      outbound_syms += bed->s->sizeof_sym;
      if (outbound_shndx != NULL)
	outbound_shndx += sizeof (Elf_External_Sym_Shndx);
    }

  *sttp = stt;
  symstrtab_hdr->sh_size = _bfd_stringtab_size (stt);
  symstrtab_hdr->sh_type = SHT_STRTAB;

  symstrtab_hdr->sh_flags = 0;
  symstrtab_hdr->sh_addr = 0;
  symstrtab_hdr->sh_entsize = 0;
  symstrtab_hdr->sh_link = 0;
  symstrtab_hdr->sh_info = 0;
  symstrtab_hdr->sh_addralign = 1;

  return TRUE;
}

/* Return the number of bytes required to hold the symtab vector.

   Note that we base it on the count plus 1, since we will null terminate
   the vector allocated based on this size.  However, the ELF symbol table
   always has a dummy entry as symbol #0, so it ends up even.  */

long
_bfd_elf_get_symtab_upper_bound (bfd *abfd)
{
  long symcount;
  long symtab_size;
  Elf_Internal_Shdr *hdr = &elf_tdata (abfd)->symtab_hdr;

  symcount = hdr->sh_size / get_elf_backend_data (abfd)->s->sizeof_sym;
  symtab_size = (symcount + 1) * (sizeof (asymbol *));
  if (symcount > 0)
    symtab_size -= sizeof (asymbol *);

  return symtab_size;
}

long
_bfd_elf_get_dynamic_symtab_upper_bound (bfd *abfd)
{
  long symcount;
  long symtab_size;
  Elf_Internal_Shdr *hdr = &elf_tdata (abfd)->dynsymtab_hdr;

  if (elf_dynsymtab (abfd) == 0)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return -1;
    }

  symcount = hdr->sh_size / get_elf_backend_data (abfd)->s->sizeof_sym;
  symtab_size = (symcount + 1) * (sizeof (asymbol *));
  if (symcount > 0)
    symtab_size -= sizeof (asymbol *);

  return symtab_size;
}

long
_bfd_elf_get_reloc_upper_bound (bfd *abfd ATTRIBUTE_UNUSED,
				sec_ptr asect)
{
  return (asect->reloc_count + 1) * sizeof (arelent *);
}

/* Canonicalize the relocs.  */

long
_bfd_elf_canonicalize_reloc (bfd *abfd,
			     sec_ptr section,
			     arelent **relptr,
			     asymbol **symbols)
{
  arelent *tblptr;
  unsigned int i;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);

  if (! bed->s->slurp_reloc_table (abfd, section, symbols, FALSE))
    return -1;

  tblptr = section->relocation;
  for (i = 0; i < section->reloc_count; i++)
    *relptr++ = tblptr++;

  *relptr = NULL;

  return section->reloc_count;
}

long
_bfd_elf_canonicalize_symtab (bfd *abfd, asymbol **allocation)
{
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  long symcount = bed->s->slurp_symbol_table (abfd, allocation, FALSE);

  if (symcount >= 0)
    bfd_get_symcount (abfd) = symcount;
  return symcount;
}

long
_bfd_elf_canonicalize_dynamic_symtab (bfd *abfd,
				      asymbol **allocation)
{
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  long symcount = bed->s->slurp_symbol_table (abfd, allocation, TRUE);

  if (symcount >= 0)
    bfd_get_dynamic_symcount (abfd) = symcount;
  return symcount;
}

/* Return the size required for the dynamic reloc entries.  Any loadable
   section that was actually installed in the BFD, and has type SHT_REL
   or SHT_RELA, and uses the dynamic symbol table, is considered to be a
   dynamic reloc section.  */

long
_bfd_elf_get_dynamic_reloc_upper_bound (bfd *abfd)
{
  long ret;
  asection *s;

  if (elf_dynsymtab (abfd) == 0)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return -1;
    }

  ret = sizeof (arelent *);
  for (s = abfd->sections; s != NULL; s = s->next)
    if ((s->flags & SEC_LOAD) != 0
	&& elf_section_data (s)->this_hdr.sh_link == elf_dynsymtab (abfd)
	&& (elf_section_data (s)->this_hdr.sh_type == SHT_REL
	    || elf_section_data (s)->this_hdr.sh_type == SHT_RELA))
      ret += ((s->size / elf_section_data (s)->this_hdr.sh_entsize)
	      * sizeof (arelent *));

  return ret;
}

/* Canonicalize the dynamic relocation entries.  Note that we return the
   dynamic relocations as a single block, although they are actually
   associated with particular sections; the interface, which was
   designed for SunOS style shared libraries, expects that there is only
   one set of dynamic relocs.  Any loadable section that was actually
   installed in the BFD, and has type SHT_REL or SHT_RELA, and uses the
   dynamic symbol table, is considered to be a dynamic reloc section.  */

long
_bfd_elf_canonicalize_dynamic_reloc (bfd *abfd,
				     arelent **storage,
				     asymbol **syms)
{
  bfd_boolean (*slurp_relocs) (bfd *, asection *, asymbol **, bfd_boolean);
  asection *s;
  long ret;

  if (elf_dynsymtab (abfd) == 0)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return -1;
    }

  slurp_relocs = get_elf_backend_data (abfd)->s->slurp_reloc_table;
  ret = 0;
  for (s = abfd->sections; s != NULL; s = s->next)
    {
      if ((s->flags & SEC_LOAD) != 0
	  && elf_section_data (s)->this_hdr.sh_link == elf_dynsymtab (abfd)
	  && (elf_section_data (s)->this_hdr.sh_type == SHT_REL
	      || elf_section_data (s)->this_hdr.sh_type == SHT_RELA))
	{
	  arelent *p;
	  long count, i;

	  if (! (*slurp_relocs) (abfd, s, syms, TRUE))
	    return -1;
	  count = s->size / elf_section_data (s)->this_hdr.sh_entsize;
	  p = s->relocation;
	  for (i = 0; i < count; i++)
	    *storage++ = p++;
	  ret += count;
	}
    }

  *storage = NULL;

  return ret;
}

/* Read in the version information.  */

bfd_boolean
_bfd_elf_slurp_version_tables (bfd *abfd, bfd_boolean default_imported_symver)
{
  bfd_byte *contents = NULL;
  unsigned int freeidx = 0;

  if (elf_dynverref (abfd) != 0)
    {
      Elf_Internal_Shdr *hdr;
      Elf_External_Verneed *everneed;
      Elf_Internal_Verneed *iverneed;
      unsigned int i;
      bfd_byte *contents_end;

      hdr = &elf_tdata (abfd)->dynverref_hdr;

      elf_tdata (abfd)->verref = bfd_zalloc2 (abfd, hdr->sh_info,
					      sizeof (Elf_Internal_Verneed));
      if (elf_tdata (abfd)->verref == NULL)
	goto error_return;

      elf_tdata (abfd)->cverrefs = hdr->sh_info;

      contents = bfd_malloc (hdr->sh_size);
      if (contents == NULL)
	{
error_return_verref:
	  elf_tdata (abfd)->verref = NULL;
	  elf_tdata (abfd)->cverrefs = 0;
	  goto error_return;
	}
      if (bfd_seek (abfd, hdr->sh_offset, SEEK_SET) != 0
	  || bfd_bread (contents, hdr->sh_size, abfd) != hdr->sh_size)
	goto error_return_verref;

      if (hdr->sh_info && hdr->sh_size < sizeof (Elf_External_Verneed))
	goto error_return_verref;

      BFD_ASSERT (sizeof (Elf_External_Verneed)
		  == sizeof (Elf_External_Vernaux));
      contents_end = contents + hdr->sh_size - sizeof (Elf_External_Verneed);
      everneed = (Elf_External_Verneed *) contents;
      iverneed = elf_tdata (abfd)->verref;
      for (i = 0; i < hdr->sh_info; i++, iverneed++)
	{
	  Elf_External_Vernaux *evernaux;
	  Elf_Internal_Vernaux *ivernaux;
	  unsigned int j;

	  _bfd_elf_swap_verneed_in (abfd, everneed, iverneed);

	  iverneed->vn_bfd = abfd;

	  iverneed->vn_filename =
	    bfd_elf_string_from_elf_section (abfd, hdr->sh_link,
					     iverneed->vn_file);
	  if (iverneed->vn_filename == NULL)
	    goto error_return_verref;

	  if (iverneed->vn_cnt == 0)
	    iverneed->vn_auxptr = NULL;
	  else
	    {
	      iverneed->vn_auxptr = bfd_alloc2 (abfd, iverneed->vn_cnt,
						sizeof (Elf_Internal_Vernaux));
	      if (iverneed->vn_auxptr == NULL)
		goto error_return_verref;
	    }

	  if (iverneed->vn_aux
	      > (size_t) (contents_end - (bfd_byte *) everneed))
	    goto error_return_verref;

	  evernaux = ((Elf_External_Vernaux *)
		      ((bfd_byte *) everneed + iverneed->vn_aux));
	  ivernaux = iverneed->vn_auxptr;
	  for (j = 0; j < iverneed->vn_cnt; j++, ivernaux++)
	    {
	      _bfd_elf_swap_vernaux_in (abfd, evernaux, ivernaux);

	      ivernaux->vna_nodename =
		bfd_elf_string_from_elf_section (abfd, hdr->sh_link,
						 ivernaux->vna_name);
	      if (ivernaux->vna_nodename == NULL)
		goto error_return_verref;

	      if (j + 1 < iverneed->vn_cnt)
		ivernaux->vna_nextptr = ivernaux + 1;
	      else
		ivernaux->vna_nextptr = NULL;

	      if (ivernaux->vna_next
		  > (size_t) (contents_end - (bfd_byte *) evernaux))
		goto error_return_verref;

	      evernaux = ((Elf_External_Vernaux *)
			  ((bfd_byte *) evernaux + ivernaux->vna_next));

	      if (ivernaux->vna_other > freeidx)
		freeidx = ivernaux->vna_other;
	    }

	  if (i + 1 < hdr->sh_info)
	    iverneed->vn_nextref = iverneed + 1;
	  else
	    iverneed->vn_nextref = NULL;

	  if (iverneed->vn_next
	      > (size_t) (contents_end - (bfd_byte *) everneed))
	    goto error_return_verref;

	  everneed = ((Elf_External_Verneed *)
		      ((bfd_byte *) everneed + iverneed->vn_next));
	}

      free (contents);
      contents = NULL;
    }

  if (elf_dynverdef (abfd) != 0)
    {
      Elf_Internal_Shdr *hdr;
      Elf_External_Verdef *everdef;
      Elf_Internal_Verdef *iverdef;
      Elf_Internal_Verdef *iverdefarr;
      Elf_Internal_Verdef iverdefmem;
      unsigned int i;
      unsigned int maxidx;
      bfd_byte *contents_end_def, *contents_end_aux;

      hdr = &elf_tdata (abfd)->dynverdef_hdr;

      contents = bfd_malloc (hdr->sh_size);
      if (contents == NULL)
	goto error_return;
      if (bfd_seek (abfd, hdr->sh_offset, SEEK_SET) != 0
	  || bfd_bread (contents, hdr->sh_size, abfd) != hdr->sh_size)
	goto error_return;

      if (hdr->sh_info && hdr->sh_size < sizeof (Elf_External_Verdef))
	goto error_return;

      BFD_ASSERT (sizeof (Elf_External_Verdef)
		  >= sizeof (Elf_External_Verdaux));
      contents_end_def = contents + hdr->sh_size
			 - sizeof (Elf_External_Verdef);
      contents_end_aux = contents + hdr->sh_size
			 - sizeof (Elf_External_Verdaux);

      /* We know the number of entries in the section but not the maximum
	 index.  Therefore we have to run through all entries and find
	 the maximum.  */
      everdef = (Elf_External_Verdef *) contents;
      maxidx = 0;
      for (i = 0; i < hdr->sh_info; ++i)
	{
	  _bfd_elf_swap_verdef_in (abfd, everdef, &iverdefmem);

	  if ((iverdefmem.vd_ndx & ((unsigned) VERSYM_VERSION)) > maxidx)
	    maxidx = iverdefmem.vd_ndx & ((unsigned) VERSYM_VERSION);

	  if (iverdefmem.vd_next
	      > (size_t) (contents_end_def - (bfd_byte *) everdef))
	    goto error_return;

	  everdef = ((Elf_External_Verdef *)
		     ((bfd_byte *) everdef + iverdefmem.vd_next));
	}

      if (default_imported_symver)
	{
	  if (freeidx > maxidx)
	    maxidx = ++freeidx;
	  else
	    freeidx = ++maxidx;
	}
      elf_tdata (abfd)->verdef = bfd_zalloc2 (abfd, maxidx,
					      sizeof (Elf_Internal_Verdef));
      if (elf_tdata (abfd)->verdef == NULL)
	goto error_return;

      elf_tdata (abfd)->cverdefs = maxidx;

      everdef = (Elf_External_Verdef *) contents;
      iverdefarr = elf_tdata (abfd)->verdef;
      for (i = 0; i < hdr->sh_info; i++)
	{
	  Elf_External_Verdaux *everdaux;
	  Elf_Internal_Verdaux *iverdaux;
	  unsigned int j;

	  _bfd_elf_swap_verdef_in (abfd, everdef, &iverdefmem);

	  if ((iverdefmem.vd_ndx & VERSYM_VERSION) == 0)
	    {
error_return_verdef:
	      elf_tdata (abfd)->verdef = NULL;
	      elf_tdata (abfd)->cverdefs = 0;
	      goto error_return;
	    }

	  iverdef = &iverdefarr[(iverdefmem.vd_ndx & VERSYM_VERSION) - 1];
	  memcpy (iverdef, &iverdefmem, sizeof (Elf_Internal_Verdef));

	  iverdef->vd_bfd = abfd;

	  if (iverdef->vd_cnt == 0)
	    iverdef->vd_auxptr = NULL;
	  else
	    {
	      iverdef->vd_auxptr = bfd_alloc2 (abfd, iverdef->vd_cnt,
					       sizeof (Elf_Internal_Verdaux));
	      if (iverdef->vd_auxptr == NULL)
		goto error_return_verdef;
	    }

	  if (iverdef->vd_aux
	      > (size_t) (contents_end_aux - (bfd_byte *) everdef))
	    goto error_return_verdef;

	  everdaux = ((Elf_External_Verdaux *)
		      ((bfd_byte *) everdef + iverdef->vd_aux));
	  iverdaux = iverdef->vd_auxptr;
	  for (j = 0; j < iverdef->vd_cnt; j++, iverdaux++)
	    {
	      _bfd_elf_swap_verdaux_in (abfd, everdaux, iverdaux);

	      iverdaux->vda_nodename =
		bfd_elf_string_from_elf_section (abfd, hdr->sh_link,
						 iverdaux->vda_name);
	      if (iverdaux->vda_nodename == NULL)
		goto error_return_verdef;

	      if (j + 1 < iverdef->vd_cnt)
		iverdaux->vda_nextptr = iverdaux + 1;
	      else
		iverdaux->vda_nextptr = NULL;

	      if (iverdaux->vda_next
		  > (size_t) (contents_end_aux - (bfd_byte *) everdaux))
		goto error_return_verdef;

	      everdaux = ((Elf_External_Verdaux *)
			  ((bfd_byte *) everdaux + iverdaux->vda_next));
	    }

	  if (iverdef->vd_cnt)
	    iverdef->vd_nodename = iverdef->vd_auxptr->vda_nodename;

	  if ((size_t) (iverdef - iverdefarr) + 1 < maxidx)
	    iverdef->vd_nextdef = iverdef + 1;
	  else
	    iverdef->vd_nextdef = NULL;

	  everdef = ((Elf_External_Verdef *)
		     ((bfd_byte *) everdef + iverdef->vd_next));
	}

      free (contents);
      contents = NULL;
    }
  else if (default_imported_symver)
    {
      if (freeidx < 3)
	freeidx = 3;
      else
	freeidx++;

      elf_tdata (abfd)->verdef = bfd_zalloc2 (abfd, freeidx,
					      sizeof (Elf_Internal_Verdef));
      if (elf_tdata (abfd)->verdef == NULL)
	goto error_return;

      elf_tdata (abfd)->cverdefs = freeidx;
    }

  /* Create a default version based on the soname.  */
  if (default_imported_symver)
    {
      Elf_Internal_Verdef *iverdef;
      Elf_Internal_Verdaux *iverdaux;

      iverdef = &elf_tdata (abfd)->verdef[freeidx - 1];;

      iverdef->vd_version = VER_DEF_CURRENT;
      iverdef->vd_flags = 0;
      iverdef->vd_ndx = freeidx;
      iverdef->vd_cnt = 1;

      iverdef->vd_bfd = abfd;

      iverdef->vd_nodename = bfd_elf_get_dt_soname (abfd);
      if (iverdef->vd_nodename == NULL)
	goto error_return_verdef;
      iverdef->vd_nextdef = NULL;
      iverdef->vd_auxptr = bfd_alloc (abfd, sizeof (Elf_Internal_Verdaux));
      if (iverdef->vd_auxptr == NULL)
	goto error_return_verdef;

      iverdaux = iverdef->vd_auxptr;
      iverdaux->vda_nodename = iverdef->vd_nodename;
      iverdaux->vda_nextptr = NULL;
    }

  return TRUE;

 error_return:
  if (contents != NULL)
    free (contents);
  return FALSE;
}

asymbol *
_bfd_elf_make_empty_symbol (bfd *abfd)
{
  elf_symbol_type *newsym;
  bfd_size_type amt = sizeof (elf_symbol_type);

  newsym = bfd_zalloc (abfd, amt);
  if (!newsym)
    return NULL;
  else
    {
      newsym->symbol.the_bfd = abfd;
      return &newsym->symbol;
    }
}

void
_bfd_elf_get_symbol_info (bfd *abfd ATTRIBUTE_UNUSED,
			  asymbol *symbol,
			  symbol_info *ret)
{
  bfd_symbol_info (symbol, ret);
}

/* Return whether a symbol name implies a local symbol.  Most targets
   use this function for the is_local_label_name entry point, but some
   override it.  */

bfd_boolean
_bfd_elf_is_local_label_name (bfd *abfd ATTRIBUTE_UNUSED,
			      const char *name)
{
  /* Normal local symbols start with ``.L''.  */
  if (name[0] == '.' && name[1] == 'L')
    return TRUE;

  /* At least some SVR4 compilers (e.g., UnixWare 2.1 cc) generate
     DWARF debugging symbols starting with ``..''.  */
  if (name[0] == '.' && name[1] == '.')
    return TRUE;

  /* gcc will sometimes generate symbols beginning with ``_.L_'' when
     emitting DWARF debugging output.  I suspect this is actually a
     small bug in gcc (it calls ASM_OUTPUT_LABEL when it should call
     ASM_GENERATE_INTERNAL_LABEL, and this causes the leading
     underscore to be emitted on some ELF targets).  For ease of use,
     we treat such symbols as local.  */
  if (name[0] == '_' && name[1] == '.' && name[2] == 'L' && name[3] == '_')
    return TRUE;

  return FALSE;
}

alent *
_bfd_elf_get_lineno (bfd *abfd ATTRIBUTE_UNUSED,
		     asymbol *symbol ATTRIBUTE_UNUSED)
{
  abort ();
  return NULL;
}

bfd_boolean
_bfd_elf_set_arch_mach (bfd *abfd,
			enum bfd_architecture arch,
			unsigned long machine)
{
  /* If this isn't the right architecture for this backend, and this
     isn't the generic backend, fail.  */
  if (arch != get_elf_backend_data (abfd)->arch
      && arch != bfd_arch_unknown
      && get_elf_backend_data (abfd)->arch != bfd_arch_unknown)
    return FALSE;

  return bfd_default_set_arch_mach (abfd, arch, machine);
}

/* Find the function to a particular section and offset,
   for error reporting.  */

static bfd_boolean
elf_find_function (bfd *abfd ATTRIBUTE_UNUSED,
		   asection *section,
		   asymbol **symbols,
		   bfd_vma offset,
		   const char **filename_ptr,
		   const char **functionname_ptr)
{
  const char *filename;
  asymbol *func, *file;
  bfd_vma low_func;
  asymbol **p;
  /* ??? Given multiple file symbols, it is impossible to reliably
     choose the right file name for global symbols.  File symbols are
     local symbols, and thus all file symbols must sort before any
     global symbols.  The ELF spec may be interpreted to say that a
     file symbol must sort before other local symbols, but currently
     ld -r doesn't do this.  So, for ld -r output, it is possible to
     make a better choice of file name for local symbols by ignoring
     file symbols appearing after a given local symbol.  */
  enum { nothing_seen, symbol_seen, file_after_symbol_seen } state;

  filename = NULL;
  func = NULL;
  file = NULL;
  low_func = 0;
  state = nothing_seen;

  for (p = symbols; *p != NULL; p++)
    {
      elf_symbol_type *q;

      q = (elf_symbol_type *) *p;

      switch (ELF_ST_TYPE (q->internal_elf_sym.st_info))
	{
	default:
	  break;
	case STT_FILE:
	  file = &q->symbol;
	  if (state == symbol_seen)
	    state = file_after_symbol_seen;
	  continue;
	case STT_NOTYPE:
	case STT_FUNC:
	  if (bfd_get_section (&q->symbol) == section
	      && q->symbol.value >= low_func
	      && q->symbol.value <= offset)
	    {
	      func = (asymbol *) q;
	      low_func = q->symbol.value;
	      filename = NULL;
	      if (file != NULL
		  && (ELF_ST_BIND (q->internal_elf_sym.st_info) == STB_LOCAL
		      || state != file_after_symbol_seen))
		filename = bfd_asymbol_name (file);
	    }
	  break;
	}
      if (state == nothing_seen)
	state = symbol_seen;
    }

  if (func == NULL)
    return FALSE;

  if (filename_ptr)
    *filename_ptr = filename;
  if (functionname_ptr)
    *functionname_ptr = bfd_asymbol_name (func);

  return TRUE;
}

/* Find the nearest line to a particular section and offset,
   for error reporting.  */

bfd_boolean
_bfd_elf_find_nearest_line (bfd *abfd,
			    asection *section,
			    asymbol **symbols,
			    bfd_vma offset,
			    const char **filename_ptr,
			    const char **functionname_ptr,
			    unsigned int *line_ptr)
{
  bfd_boolean found;

  if (_bfd_dwarf1_find_nearest_line (abfd, section, symbols, offset,
				     filename_ptr, functionname_ptr,
				     line_ptr))
    {
      if (!*functionname_ptr)
	elf_find_function (abfd, section, symbols, offset,
			   *filename_ptr ? NULL : filename_ptr,
			   functionname_ptr);

      return TRUE;
    }

  if (_bfd_dwarf2_find_nearest_line (abfd, section, symbols, offset,
				     filename_ptr, functionname_ptr,
				     line_ptr, 0,
				     &elf_tdata (abfd)->dwarf2_find_line_info))
    {
      if (!*functionname_ptr)
	elf_find_function (abfd, section, symbols, offset,
			   *filename_ptr ? NULL : filename_ptr,
			   functionname_ptr);

      return TRUE;
    }

  if (! _bfd_stab_section_find_nearest_line (abfd, symbols, section, offset,
					     &found, filename_ptr,
					     functionname_ptr, line_ptr,
					     &elf_tdata (abfd)->line_info))
    return FALSE;
  if (found && (*functionname_ptr || *line_ptr))
    return TRUE;

  if (symbols == NULL)
    return FALSE;

  if (! elf_find_function (abfd, section, symbols, offset,
			   filename_ptr, functionname_ptr))
    return FALSE;

  *line_ptr = 0;
  return TRUE;
}

/* Find the line for a symbol.  */

bfd_boolean
_bfd_elf_find_line (bfd *abfd, asymbol **symbols, asymbol *symbol,
		    const char **filename_ptr, unsigned int *line_ptr)
{
  return _bfd_dwarf2_find_line (abfd, symbols, symbol,
				filename_ptr, line_ptr, 0,
				&elf_tdata (abfd)->dwarf2_find_line_info);
}

/* After a call to bfd_find_nearest_line, successive calls to
   bfd_find_inliner_info can be used to get source information about
   each level of function inlining that terminated at the address
   passed to bfd_find_nearest_line.  Currently this is only supported
   for DWARF2 with appropriate DWARF3 extensions. */

bfd_boolean
_bfd_elf_find_inliner_info (bfd *abfd,
			    const char **filename_ptr,
			    const char **functionname_ptr,
			    unsigned int *line_ptr)
{
  bfd_boolean found;
  found = _bfd_dwarf2_find_inliner_info (abfd, filename_ptr,
					 functionname_ptr, line_ptr,
					 & elf_tdata (abfd)->dwarf2_find_line_info);
  return found;
}

int
_bfd_elf_sizeof_headers (bfd *abfd, struct bfd_link_info *info)
{
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  int ret = bed->s->sizeof_ehdr;

  if (!info->relocatable)
    {
      bfd_size_type phdr_size = elf_tdata (abfd)->program_header_size;

      if (phdr_size == (bfd_size_type) -1)
	{
	  struct elf_segment_map *m;

	  phdr_size = 0;
	  for (m = elf_tdata (abfd)->segment_map; m != NULL; m = m->next)
	    phdr_size += bed->s->sizeof_phdr;

	  if (phdr_size == 0)
	    phdr_size = get_program_header_size (abfd, info);
	}

      elf_tdata (abfd)->program_header_size = phdr_size;
      ret += phdr_size;
    }

  return ret;
}

bfd_boolean
_bfd_elf_set_section_contents (bfd *abfd,
			       sec_ptr section,
			       const void *location,
			       file_ptr offset,
			       bfd_size_type count)
{
  Elf_Internal_Shdr *hdr;
  bfd_signed_vma pos;

  if (! abfd->output_has_begun
      && ! _bfd_elf_compute_section_file_positions (abfd, NULL))
    return FALSE;

  hdr = &elf_section_data (section)->this_hdr;
  pos = hdr->sh_offset + offset;
  if (bfd_seek (abfd, pos, SEEK_SET) != 0
      || bfd_bwrite (location, count, abfd) != count)
    return FALSE;

  return TRUE;
}

void
_bfd_elf_no_info_to_howto (bfd *abfd ATTRIBUTE_UNUSED,
			   arelent *cache_ptr ATTRIBUTE_UNUSED,
			   Elf_Internal_Rela *dst ATTRIBUTE_UNUSED)
{
  abort ();
}

/* Try to convert a non-ELF reloc into an ELF one.  */

bfd_boolean
_bfd_elf_validate_reloc (bfd *abfd, arelent *areloc)
{
  /* Check whether we really have an ELF howto.  */

  if ((*areloc->sym_ptr_ptr)->the_bfd->xvec != abfd->xvec)
    {
      bfd_reloc_code_real_type code;
      reloc_howto_type *howto;

      /* Alien reloc: Try to determine its type to replace it with an
	 equivalent ELF reloc.  */

      if (areloc->howto->pc_relative)
	{
	  switch (areloc->howto->bitsize)
	    {
	    case 8:
	      code = BFD_RELOC_8_PCREL;
	      break;
	    case 12:
	      code = BFD_RELOC_12_PCREL;
	      break;
	    case 16:
	      code = BFD_RELOC_16_PCREL;
	      break;
	    case 24:
	      code = BFD_RELOC_24_PCREL;
	      break;
	    case 32:
	      code = BFD_RELOC_32_PCREL;
	      break;
	    case 64:
	      code = BFD_RELOC_64_PCREL;
	      break;
	    default:
	      goto fail;
	    }

	  howto = bfd_reloc_type_lookup (abfd, code);

	  if (areloc->howto->pcrel_offset != howto->pcrel_offset)
	    {
	      if (howto->pcrel_offset)
		areloc->addend += areloc->address;
	      else
		areloc->addend -= areloc->address; /* addend is unsigned!! */
	    }
	}
      else
	{
	  switch (areloc->howto->bitsize)
	    {
	    case 8:
	      code = BFD_RELOC_8;
	      break;
	    case 14:
	      code = BFD_RELOC_14;
	      break;
	    case 16:
	      code = BFD_RELOC_16;
	      break;
	    case 26:
	      code = BFD_RELOC_26;
	      break;
	    case 32:
	      code = BFD_RELOC_32;
	      break;
	    case 64:
	      code = BFD_RELOC_64;
	      break;
	    default:
	      goto fail;
	    }

	  howto = bfd_reloc_type_lookup (abfd, code);
	}

      if (howto)
	areloc->howto = howto;
      else
	goto fail;
    }

  return TRUE;

 fail:
  (*_bfd_error_handler)
    (_("%B: unsupported relocation type %s"),
     abfd, areloc->howto->name);
  bfd_set_error (bfd_error_bad_value);
  return FALSE;
}

bfd_boolean
_bfd_elf_close_and_cleanup (bfd *abfd)
{
  if (bfd_get_format (abfd) == bfd_object)
    {
      if (elf_tdata (abfd) != NULL && elf_shstrtab (abfd) != NULL)
	_bfd_elf_strtab_free (elf_shstrtab (abfd));
      _bfd_dwarf2_cleanup_debug_info (abfd);
    }

  return _bfd_generic_close_and_cleanup (abfd);
}

/* For Rel targets, we encode meaningful data for BFD_RELOC_VTABLE_ENTRY
   in the relocation's offset.  Thus we cannot allow any sort of sanity
   range-checking to interfere.  There is nothing else to do in processing
   this reloc.  */

bfd_reloc_status_type
_bfd_elf_rel_vtable_reloc_fn
  (bfd *abfd ATTRIBUTE_UNUSED, arelent *re ATTRIBUTE_UNUSED,
   struct bfd_symbol *symbol ATTRIBUTE_UNUSED,
   void *data ATTRIBUTE_UNUSED, asection *is ATTRIBUTE_UNUSED,
   bfd *obfd ATTRIBUTE_UNUSED, char **errmsg ATTRIBUTE_UNUSED)
{
  return bfd_reloc_ok;
}

/* Elf core file support.  Much of this only works on native
   toolchains, since we rely on knowing the
   machine-dependent procfs structure in order to pick
   out details about the corefile.  */

#ifdef HAVE_SYS_PROCFS_H
# include <sys/procfs.h>

/* Define HAVE_THRMISC_T for consistency with other similar GNU-type stubs. */
#undef	HAVE_THRMISC_T
#if defined (THRMISC_VERSION)
#define	HAVE_THRMISC_T	1
#endif
#endif

/* FIXME: this is kinda wrong, but it's what gdb wants.  */

static int
elfcore_make_pid (bfd *abfd)
{
  return ((elf_tdata (abfd)->core_lwpid << 16)
	  + (elf_tdata (abfd)->core_pid));
}

/* If there isn't a section called NAME, make one, using
   data from SECT.  Note, this function will generate a
   reference to NAME, so you shouldn't deallocate or
   overwrite it.  */

static bfd_boolean
elfcore_maybe_make_sect (bfd *abfd, char *name, asection *sect)
{
  asection *sect2;

  if (bfd_get_section_by_name (abfd, name) != NULL)
    return TRUE;

  sect2 = bfd_make_section_with_flags (abfd, name, sect->flags);
  if (sect2 == NULL)
    return FALSE;

  sect2->size = sect->size;
  sect2->filepos = sect->filepos;
  sect2->alignment_power = sect->alignment_power;
  return TRUE;
}

/* Create a pseudosection containing SIZE bytes at FILEPOS.  This
   actually creates up to two pseudosections:
   - For the single-threaded case, a section named NAME, unless
     such a section already exists.
   - For the multi-threaded case, a section named "NAME/PID", where
     PID is elfcore_make_pid (abfd).
   Both pseudosections have identical contents. */
bfd_boolean
_bfd_elfcore_make_pseudosection (bfd *abfd,
				 char *name,
				 size_t size,
				 ufile_ptr filepos)
{
  char buf[100];
  char *threaded_name;
  size_t len;
  asection *sect;

  /* Build the section name.  */

  sprintf (buf, "%s/%d", name, elfcore_make_pid (abfd));
  len = strlen (buf) + 1;
  threaded_name = bfd_alloc (abfd, len);
  if (threaded_name == NULL)
    return FALSE;
  memcpy (threaded_name, buf, len);

  sect = bfd_make_section_anyway_with_flags (abfd, threaded_name,
					     SEC_HAS_CONTENTS);
  if (sect == NULL)
    return FALSE;
  sect->size = size;
  sect->filepos = filepos;
  sect->alignment_power = 2;

  return elfcore_maybe_make_sect (abfd, name, sect);
}

/* prstatus_t exists on:
     solaris 2.5+
     linux 2.[01] + glibc
     unixware 4.2
*/

#if defined (HAVE_PRSTATUS_T)

static bfd_boolean
elfcore_grok_prstatus (bfd *abfd, Elf_Internal_Note *note)
{
  size_t size;
  int offset;

  if (note->descsz == sizeof (prstatus_t))
    {
      prstatus_t prstat;

      size = sizeof (prstat.pr_reg);
      offset   = offsetof (prstatus_t, pr_reg);
      memcpy (&prstat, note->descdata, sizeof (prstat));

      /* Do not overwrite the core signal if it
	 has already been set by another thread.  */
      if (elf_tdata (abfd)->core_signal == 0)
	elf_tdata (abfd)->core_signal = prstat.pr_cursig;
      elf_tdata (abfd)->core_pid = prstat.pr_pid;

      /* pr_who exists on:
	 solaris 2.5+
	 unixware 4.2
	 pr_who doesn't exist on:
	 linux 2.[01]
	 */
#if defined (HAVE_PRSTATUS_T_PR_WHO)
      elf_tdata (abfd)->core_lwpid = prstat.pr_who;
#endif
    }
#if defined (HAVE_PRSTATUS32_T)
  else if (note->descsz == sizeof (prstatus32_t))
    {
      /* 64-bit host, 32-bit corefile */
      prstatus32_t prstat;

      size = sizeof (prstat.pr_reg);
      offset   = offsetof (prstatus32_t, pr_reg);
      memcpy (&prstat, note->descdata, sizeof (prstat));

      /* Do not overwrite the core signal if it
	 has already been set by another thread.  */
      if (elf_tdata (abfd)->core_signal == 0)
	elf_tdata (abfd)->core_signal = prstat.pr_cursig;
      elf_tdata (abfd)->core_pid = prstat.pr_pid;

      /* pr_who exists on:
	 solaris 2.5+
	 unixware 4.2
	 pr_who doesn't exist on:
	 linux 2.[01]
	 */
#if defined (HAVE_PRSTATUS32_T_PR_WHO)
      elf_tdata (abfd)->core_lwpid = prstat.pr_who;
#endif
    }
#endif /* HAVE_PRSTATUS32_T */
  else
    {
      /* Fail - we don't know how to handle any other
	 note size (ie. data object type).  */
      return TRUE;
    }

  /* Make a ".reg/999" section and a ".reg" section.  */
  return _bfd_elfcore_make_pseudosection (abfd, ".reg",
					  size, note->descpos + offset);
}
#endif /* defined (HAVE_PRSTATUS_T) */

/* Create a pseudosection containing the exact contents of NOTE.  */
static bfd_boolean
elfcore_make_note_pseudosection (bfd *abfd,
				 char *name,
				 Elf_Internal_Note *note)
{
  return _bfd_elfcore_make_pseudosection (abfd, name,
					  note->descsz, note->descpos);
}

/* There isn't a consistent prfpregset_t across platforms,
   but it doesn't matter, because we don't have to pick this
   data structure apart.  */

static bfd_boolean
elfcore_grok_prfpreg (bfd *abfd, Elf_Internal_Note *note)
{
  return elfcore_make_note_pseudosection (abfd, ".reg2", note);
}

/* Linux dumps the Intel SSE regs in a note named "LINUX" with a note
   type of 5 (NT_PRXFPREG).  Just include the whole note's contents
   literally.  */

static bfd_boolean
elfcore_grok_prxfpreg (bfd *abfd, Elf_Internal_Note *note)
{
  return elfcore_make_note_pseudosection (abfd, ".reg-xfp", note);
}

#if defined (HAVE_THRMISC_T)

static bfd_boolean
elfcore_grok_thrmisc (bfd *abfd, Elf_Internal_Note *note)
{
  return elfcore_make_note_pseudosection (abfd, ".tname", note);
}

#endif /* defined (HAVE_THRMISC_T) */

#if defined (HAVE_PRPSINFO_T)
typedef prpsinfo_t   elfcore_psinfo_t;
#if defined (HAVE_PRPSINFO32_T)		/* Sparc64 cross Sparc32 */
typedef prpsinfo32_t elfcore_psinfo32_t;
#endif
#endif

#if defined (HAVE_PSINFO_T)
typedef psinfo_t   elfcore_psinfo_t;
#if defined (HAVE_PSINFO32_T)		/* Sparc64 cross Sparc32 */
typedef psinfo32_t elfcore_psinfo32_t;
#endif
#endif

/* return a malloc'ed copy of a string at START which is at
   most MAX bytes long, possibly without a terminating '\0'.
   the copy will always have a terminating '\0'.  */

char *
_bfd_elfcore_strndup (bfd *abfd, char *start, size_t max)
{
  char *dups;
  char *end = memchr (start, '\0', max);
  size_t len;

  if (end == NULL)
    len = max;
  else
    len = end - start;

  dups = bfd_alloc (abfd, len + 1);
  if (dups == NULL)
    return NULL;

  memcpy (dups, start, len);
  dups[len] = '\0';

  return dups;
}

#if defined (HAVE_PRPSINFO_T) || defined (HAVE_PSINFO_T)
static bfd_boolean
elfcore_grok_psinfo (bfd *abfd, Elf_Internal_Note *note)
{
  if (note->descsz == sizeof (elfcore_psinfo_t))
    {
      elfcore_psinfo_t psinfo;

      memcpy (&psinfo, note->descdata, sizeof (psinfo));

      elf_tdata (abfd)->core_program
	= _bfd_elfcore_strndup (abfd, psinfo.pr_fname,
				sizeof (psinfo.pr_fname));

      elf_tdata (abfd)->core_command
	= _bfd_elfcore_strndup (abfd, psinfo.pr_psargs,
				sizeof (psinfo.pr_psargs));
    }
#if defined (HAVE_PRPSINFO32_T) || defined (HAVE_PSINFO32_T)
  else if (note->descsz == sizeof (elfcore_psinfo32_t))
    {
      /* 64-bit host, 32-bit corefile */
      elfcore_psinfo32_t psinfo;

      memcpy (&psinfo, note->descdata, sizeof (psinfo));

      elf_tdata (abfd)->core_program
	= _bfd_elfcore_strndup (abfd, psinfo.pr_fname,
				sizeof (psinfo.pr_fname));

      elf_tdata (abfd)->core_command
	= _bfd_elfcore_strndup (abfd, psinfo.pr_psargs,
				sizeof (psinfo.pr_psargs));
    }
#endif

  else
    {
      /* Fail - we don't know how to handle any other
	 note size (ie. data object type).  */
      return TRUE;
    }

  /* Note that for some reason, a spurious space is tacked
     onto the end of the args in some (at least one anyway)
     implementations, so strip it off if it exists.  */

  {
    char *command = elf_tdata (abfd)->core_command;
    int n = strlen (command);

    if (0 < n && command[n - 1] == ' ')
      command[n - 1] = '\0';
  }

  return TRUE;
}
#endif /* defined (HAVE_PRPSINFO_T) || defined (HAVE_PSINFO_T) */

#if defined (HAVE_PSTATUS_T)
static bfd_boolean
elfcore_grok_pstatus (bfd *abfd, Elf_Internal_Note *note)
{
  if (note->descsz == sizeof (pstatus_t)
#if defined (HAVE_PXSTATUS_T)
      || note->descsz == sizeof (pxstatus_t)
#endif
      )
    {
      pstatus_t pstat;

      memcpy (&pstat, note->descdata, sizeof (pstat));

      elf_tdata (abfd)->core_pid = pstat.pr_pid;
    }
#if defined (HAVE_PSTATUS32_T)
  else if (note->descsz == sizeof (pstatus32_t))
    {
      /* 64-bit host, 32-bit corefile */
      pstatus32_t pstat;

      memcpy (&pstat, note->descdata, sizeof (pstat));

      elf_tdata (abfd)->core_pid = pstat.pr_pid;
    }
#endif
  /* Could grab some more details from the "representative"
     lwpstatus_t in pstat.pr_lwp, but we'll catch it all in an
     NT_LWPSTATUS note, presumably.  */

  return TRUE;
}
#endif /* defined (HAVE_PSTATUS_T) */

#if defined (HAVE_LWPSTATUS_T)
static bfd_boolean
elfcore_grok_lwpstatus (bfd *abfd, Elf_Internal_Note *note)
{
  lwpstatus_t lwpstat;
  char buf[100];
  char *name;
  size_t len;
  asection *sect;

  if (note->descsz != sizeof (lwpstat)
#if defined (HAVE_LWPXSTATUS_T)
      && note->descsz != sizeof (lwpxstatus_t)
#endif
      )
    return TRUE;

  memcpy (&lwpstat, note->descdata, sizeof (lwpstat));

  elf_tdata (abfd)->core_lwpid = lwpstat.pr_lwpid;
  elf_tdata (abfd)->core_signal = lwpstat.pr_cursig;

  /* Make a ".reg/999" section.  */

  sprintf (buf, ".reg/%d", elfcore_make_pid (abfd));
  len = strlen (buf) + 1;
  name = bfd_alloc (abfd, len);
  if (name == NULL)
    return FALSE;
  memcpy (name, buf, len);

  sect = bfd_make_section_anyway_with_flags (abfd, name, SEC_HAS_CONTENTS);
  if (sect == NULL)
    return FALSE;

#if defined (HAVE_LWPSTATUS_T_PR_CONTEXT)
  sect->size = sizeof (lwpstat.pr_context.uc_mcontext.gregs);
  sect->filepos = note->descpos
    + offsetof (lwpstatus_t, pr_context.uc_mcontext.gregs);
#endif

#if defined (HAVE_LWPSTATUS_T_PR_REG)
  sect->size = sizeof (lwpstat.pr_reg);
  sect->filepos = note->descpos + offsetof (lwpstatus_t, pr_reg);
#endif

  sect->alignment_power = 2;

  if (!elfcore_maybe_make_sect (abfd, ".reg", sect))
    return FALSE;

  /* Make a ".reg2/999" section */

  sprintf (buf, ".reg2/%d", elfcore_make_pid (abfd));
  len = strlen (buf) + 1;
  name = bfd_alloc (abfd, len);
  if (name == NULL)
    return FALSE;
  memcpy (name, buf, len);

  sect = bfd_make_section_anyway_with_flags (abfd, name, SEC_HAS_CONTENTS);
  if (sect == NULL)
    return FALSE;

#if defined (HAVE_LWPSTATUS_T_PR_CONTEXT)
  sect->size = sizeof (lwpstat.pr_context.uc_mcontext.fpregs);
  sect->filepos = note->descpos
    + offsetof (lwpstatus_t, pr_context.uc_mcontext.fpregs);
#endif

#if defined (HAVE_LWPSTATUS_T_PR_FPREG)
  sect->size = sizeof (lwpstat.pr_fpreg);
  sect->filepos = note->descpos + offsetof (lwpstatus_t, pr_fpreg);
#endif

  sect->alignment_power = 2;

  return elfcore_maybe_make_sect (abfd, ".reg2", sect);
}
#endif /* defined (HAVE_LWPSTATUS_T) */

#if defined (HAVE_WIN32_PSTATUS_T)
static bfd_boolean
elfcore_grok_win32pstatus (bfd *abfd, Elf_Internal_Note *note)
{
  char buf[30];
  char *name;
  size_t len;
  asection *sect;
  win32_pstatus_t pstatus;

  if (note->descsz < sizeof (pstatus))
    return TRUE;

  memcpy (&pstatus, note->descdata, sizeof (pstatus));

  switch (pstatus.data_type)
    {
    case NOTE_INFO_PROCESS:
      /* FIXME: need to add ->core_command.  */
      elf_tdata (abfd)->core_signal = pstatus.data.process_info.signal;
      elf_tdata (abfd)->core_pid = pstatus.data.process_info.pid;
      break;

    case NOTE_INFO_THREAD:
      /* Make a ".reg/999" section.  */
      sprintf (buf, ".reg/%ld", (long) pstatus.data.thread_info.tid);

      len = strlen (buf) + 1;
      name = bfd_alloc (abfd, len);
      if (name == NULL)
	return FALSE;

      memcpy (name, buf, len);

      sect = bfd_make_section_anyway_with_flags (abfd, name, SEC_HAS_CONTENTS);
      if (sect == NULL)
	return FALSE;

      sect->size = sizeof (pstatus.data.thread_info.thread_context);
      sect->filepos = (note->descpos
		       + offsetof (struct win32_pstatus,
				   data.thread_info.thread_context));
      sect->alignment_power = 2;

      if (pstatus.data.thread_info.is_active_thread)
	if (! elfcore_maybe_make_sect (abfd, ".reg", sect))
	  return FALSE;
      break;

    case NOTE_INFO_MODULE:
      /* Make a ".module/xxxxxxxx" section.  */
      sprintf (buf, ".module/%08lx",
	       (long) pstatus.data.module_info.base_address);

      len = strlen (buf) + 1;
      name = bfd_alloc (abfd, len);
      if (name == NULL)
	return FALSE;

      memcpy (name, buf, len);

      sect = bfd_make_section_anyway_with_flags (abfd, name, SEC_HAS_CONTENTS);

      if (sect == NULL)
	return FALSE;

      sect->size = note->descsz;
      sect->filepos = note->descpos;
      sect->alignment_power = 2;
      break;

    default:
      return TRUE;
    }

  return TRUE;
}
#endif /* HAVE_WIN32_PSTATUS_T */

static bfd_boolean
elfcore_grok_note (bfd *abfd, Elf_Internal_Note *note)
{
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);

  switch (note->type)
    {
    default:
      return TRUE;

    case NT_PRSTATUS:
      if (bed->elf_backend_grok_prstatus)
	if ((*bed->elf_backend_grok_prstatus) (abfd, note))
	  return TRUE;
#if defined (HAVE_PRSTATUS_T)
      return elfcore_grok_prstatus (abfd, note);
#else
      return TRUE;
#endif

#if defined (HAVE_PSTATUS_T)
    case NT_PSTATUS:
      return elfcore_grok_pstatus (abfd, note);
#endif

#if defined (HAVE_LWPSTATUS_T)
    case NT_LWPSTATUS:
      return elfcore_grok_lwpstatus (abfd, note);
#endif

    case NT_FPREGSET:		/* FIXME: rename to NT_PRFPREG */
      return elfcore_grok_prfpreg (abfd, note);

#if defined (HAVE_WIN32_PSTATUS_T)
    case NT_WIN32PSTATUS:
      return elfcore_grok_win32pstatus (abfd, note);
#endif

    case NT_PRXFPREG:		/* Linux SSE extension */
      if (note->namesz == 6
	  && strcmp (note->namedata, "LINUX") == 0)
	return elfcore_grok_prxfpreg (abfd, note);
      else
	return TRUE;

    case NT_PRPSINFO:
    case NT_PSINFO:
      if (bed->elf_backend_grok_psinfo)
	if ((*bed->elf_backend_grok_psinfo) (abfd, note))
	  return TRUE;
#if defined (HAVE_PRPSINFO_T) || defined (HAVE_PSINFO_T)
      return elfcore_grok_psinfo (abfd, note);
#else
      return TRUE;
#endif

    case NT_AUXV:
      {
	asection *sect = bfd_make_section_anyway_with_flags (abfd, ".auxv",
							     SEC_HAS_CONTENTS);

	if (sect == NULL)
	  return FALSE;
	sect->size = note->descsz;
	sect->filepos = note->descpos;
	sect->alignment_power = 1 + bfd_get_arch_size (abfd) / 32;

	return TRUE;
      }

#if defined (HAVE_THRMISC_T)
    case NT_THRMISC:
      return elfcore_grok_thrmisc (abfd, note);
#endif

    }
}

static bfd_boolean
elfcore_netbsd_get_lwpid (Elf_Internal_Note *note, int *lwpidp)
{
  char *cp;

  cp = strchr (note->namedata, '@');
  if (cp != NULL)
    {
      *lwpidp = atoi(cp + 1);
      return TRUE;
    }
  return FALSE;
}

static bfd_boolean
elfcore_grok_netbsd_procinfo (bfd *abfd, Elf_Internal_Note *note)
{
  /* Signal number at offset 0x08. */
  elf_tdata (abfd)->core_signal
    = bfd_h_get_32 (abfd, (bfd_byte *) note->descdata + 0x08);

  /* Process ID at offset 0x50. */
  elf_tdata (abfd)->core_pid
    = bfd_h_get_32 (abfd, (bfd_byte *) note->descdata + 0x50);

  /* Command name at 0x7c (max 32 bytes, including nul). */
  elf_tdata (abfd)->core_command
    = _bfd_elfcore_strndup (abfd, note->descdata + 0x7c, 31);

  return elfcore_make_note_pseudosection (abfd, ".note.netbsdcore.procinfo",
					  note);
}

static bfd_boolean
elfcore_grok_netbsd_note (bfd *abfd, Elf_Internal_Note *note)
{
  int lwp;

  if (elfcore_netbsd_get_lwpid (note, &lwp))
    elf_tdata (abfd)->core_lwpid = lwp;

  if (note->type == NT_NETBSDCORE_PROCINFO)
    {
      /* NetBSD-specific core "procinfo".  Note that we expect to
	 find this note before any of the others, which is fine,
	 since the kernel writes this note out first when it
	 creates a core file.  */

      return elfcore_grok_netbsd_procinfo (abfd, note);
    }

  /* As of Jan 2002 there are no other machine-independent notes
     defined for NetBSD core files.  If the note type is less
     than the start of the machine-dependent note types, we don't
     understand it.  */

  if (note->type < NT_NETBSDCORE_FIRSTMACH)
    return TRUE;


  switch (bfd_get_arch (abfd))
    {
      /* On the Alpha, SPARC (32-bit and 64-bit), PT_GETREGS == mach+0 and
	 PT_GETFPREGS == mach+2.  */

    case bfd_arch_alpha:
    case bfd_arch_sparc:
      switch (note->type)
	{
	case NT_NETBSDCORE_FIRSTMACH+0:
	  return elfcore_make_note_pseudosection (abfd, ".reg", note);

	case NT_NETBSDCORE_FIRSTMACH+2:
	  return elfcore_make_note_pseudosection (abfd, ".reg2", note);

	default:
	  return TRUE;
	}

      /* On all other arch's, PT_GETREGS == mach+1 and
	 PT_GETFPREGS == mach+3.  */

    default:
      switch (note->type)
	{
	case NT_NETBSDCORE_FIRSTMACH+1:
	  return elfcore_make_note_pseudosection (abfd, ".reg", note);

	case NT_NETBSDCORE_FIRSTMACH+3:
	  return elfcore_make_note_pseudosection (abfd, ".reg2", note);

	default:
	  return TRUE;
	}
    }
    /* NOTREACHED */
}

static bfd_boolean
elfcore_grok_nto_status (bfd *abfd, Elf_Internal_Note *note, long *tid)
{
  void *ddata = note->descdata;
  char buf[100];
  char *name;
  asection *sect;
  short sig;
  unsigned flags;

  /* nto_procfs_status 'pid' field is at offset 0.  */
  elf_tdata (abfd)->core_pid = bfd_get_32 (abfd, (bfd_byte *) ddata);

  /* nto_procfs_status 'tid' field is at offset 4.  Pass it back.  */
  *tid = bfd_get_32 (abfd, (bfd_byte *) ddata + 4);

  /* nto_procfs_status 'flags' field is at offset 8.  */
  flags = bfd_get_32 (abfd, (bfd_byte *) ddata + 8);

  /* nto_procfs_status 'what' field is at offset 14.  */
  if ((sig = bfd_get_16 (abfd, (bfd_byte *) ddata + 14)) > 0)
    {
      elf_tdata (abfd)->core_signal = sig;
      elf_tdata (abfd)->core_lwpid = *tid;
    }

  /* _DEBUG_FLAG_CURTID (current thread) is 0x80.  Some cores
     do not come from signals so we make sure we set the current
     thread just in case.  */
  if (flags & 0x00000080)
    elf_tdata (abfd)->core_lwpid = *tid;

  /* Make a ".qnx_core_status/%d" section.  */
  sprintf (buf, ".qnx_core_status/%ld", *tid);

  name = bfd_alloc (abfd, strlen (buf) + 1);
  if (name == NULL)
    return FALSE;
  strcpy (name, buf);

  sect = bfd_make_section_anyway_with_flags (abfd, name, SEC_HAS_CONTENTS);
  if (sect == NULL)
    return FALSE;

  sect->size            = note->descsz;
  sect->filepos         = note->descpos;
  sect->alignment_power = 2;

  return (elfcore_maybe_make_sect (abfd, ".qnx_core_status", sect));
}

static bfd_boolean
elfcore_grok_nto_regs (bfd *abfd,
		       Elf_Internal_Note *note,
		       long tid,
		       char *base)
{
  char buf[100];
  char *name;
  asection *sect;

  /* Make a "(base)/%d" section.  */
  sprintf (buf, "%s/%ld", base, tid);

  name = bfd_alloc (abfd, strlen (buf) + 1);
  if (name == NULL)
    return FALSE;
  strcpy (name, buf);

  sect = bfd_make_section_anyway_with_flags (abfd, name, SEC_HAS_CONTENTS);
  if (sect == NULL)
    return FALSE;

  sect->size            = note->descsz;
  sect->filepos         = note->descpos;
  sect->alignment_power = 2;

  /* This is the current thread.  */
  if (elf_tdata (abfd)->core_lwpid == tid)
    return elfcore_maybe_make_sect (abfd, base, sect);

  return TRUE;
}

#define BFD_QNT_CORE_INFO	7
#define BFD_QNT_CORE_STATUS	8
#define BFD_QNT_CORE_GREG	9
#define BFD_QNT_CORE_FPREG	10

static bfd_boolean
elfcore_grok_nto_note (bfd *abfd, Elf_Internal_Note *note)
{
  /* Every GREG section has a STATUS section before it.  Store the
     tid from the previous call to pass down to the next gregs
     function.  */
  static long tid = 1;

  switch (note->type)
    {
    case BFD_QNT_CORE_INFO:
      return elfcore_make_note_pseudosection (abfd, ".qnx_core_info", note);
    case BFD_QNT_CORE_STATUS:
      return elfcore_grok_nto_status (abfd, note, &tid);
    case BFD_QNT_CORE_GREG:
      return elfcore_grok_nto_regs (abfd, note, tid, ".reg");
    case BFD_QNT_CORE_FPREG:
      return elfcore_grok_nto_regs (abfd, note, tid, ".reg2");
    default:
      return TRUE;
    }
}

/* Function: elfcore_write_note

   Inputs:
     buffer to hold note, and current size of buffer
     name of note
     type of note
     data for note
     size of data for note

   Writes note to end of buffer.  ELF64 notes are written exactly as
   for ELF32, despite the current (as of 2006) ELF gabi specifying
   that they ought to have 8-byte namesz and descsz field, and have
   8-byte alignment.  Other writers, eg. Linux kernel, do the same.

   Return:
   Pointer to realloc'd buffer, *BUFSIZ updated.  */

char *
elfcore_write_note (bfd *abfd,
		    char *buf,
		    int *bufsiz,
		    const char *name,
		    int type,
		    const void *input,
		    int size)
{
  Elf_External_Note *xnp;
  size_t namesz;
  size_t newspace;
  char *dest;

  namesz = 0;
  if (name != NULL)
    namesz = strlen (name) + 1;

  newspace = 12 + ((namesz + 3) & -4) + ((size + 3) & -4);

  buf = realloc (buf, *bufsiz + newspace);
  dest = buf + *bufsiz;
  *bufsiz += newspace;
  xnp = (Elf_External_Note *) dest;
  H_PUT_32 (abfd, namesz, xnp->namesz);
  H_PUT_32 (abfd, size, xnp->descsz);
  H_PUT_32 (abfd, type, xnp->type);
  dest = xnp->name;
  if (name != NULL)
    {
      memcpy (dest, name, namesz);
      dest += namesz;
      while (namesz & 3)
	{
	  *dest++ = '\0';
	  ++namesz;
	}
    }
  memcpy (dest, input, size);
  dest += size;
  while (size & 3)
    {
      *dest++ = '\0';
      ++size;
    }
  return buf;
}

#if defined (HAVE_PRPSINFO_T) || defined (HAVE_PSINFO_T)
char *
elfcore_write_prpsinfo (bfd  *abfd,
			char *buf,
			int  *bufsiz,
			const char *fname,
			const char *psargs)
{
  const char *note_name = "CORE";
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);

  if (bed->elf_backend_write_core_note != NULL)
    {
      char *ret;
      ret = (*bed->elf_backend_write_core_note) (abfd, buf, bufsiz,
						 NT_PRPSINFO, fname, psargs);
      if (ret != NULL)
	return ret;
    }

#if defined (HAVE_PRPSINFO32_T) || defined (HAVE_PSINFO32_T)
  if (bed->s->elfclass == ELFCLASS32)
    {
#if defined (HAVE_PSINFO32_T)
      psinfo32_t data;
      int note_type = NT_PSINFO;
#else
      prpsinfo32_t data;
      int note_type = NT_PRPSINFO;
#endif

      memset (&data, 0, sizeof (data));
      strncpy (data.pr_fname, fname, sizeof (data.pr_fname));
      strncpy (data.pr_psargs, psargs, sizeof (data.pr_psargs));
      return elfcore_write_note (abfd, buf, bufsiz,
				 note_name, note_type, &data, sizeof (data));
    }
  else
#endif
    {
#if defined (HAVE_PSINFO_T)
      psinfo_t data;
      int note_type = NT_PSINFO;
#else
      prpsinfo_t data;
      int note_type = NT_PRPSINFO;
#endif

      memset (&data, 0, sizeof (data));
      strncpy (data.pr_fname, fname, sizeof (data.pr_fname));
      strncpy (data.pr_psargs, psargs, sizeof (data.pr_psargs));
      return elfcore_write_note (abfd, buf, bufsiz,
				 note_name, note_type, &data, sizeof (data));
    }
}
#endif	/* PSINFO_T or PRPSINFO_T */

#if defined (HAVE_PRSTATUS_T)
char *
elfcore_write_prstatus (bfd *abfd,
			char *buf,
			int *bufsiz,
			long pid,
			int cursig,
			const void *gregs)
{
  const char *note_name = "CORE";
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);

  if (bed->elf_backend_write_core_note != NULL)
    {
      char *ret;
      ret = (*bed->elf_backend_write_core_note) (abfd, buf, bufsiz,
						 NT_PRSTATUS,
						 pid, cursig, gregs);
      if (ret != NULL)
	return ret;
    }

#if defined (HAVE_PRSTATUS32_T)
  if (bed->s->elfclass == ELFCLASS32)
    {
      prstatus32_t prstat;

      memset (&prstat, 0, sizeof (prstat));
      prstat.pr_pid = pid;
      prstat.pr_cursig = cursig;
      memcpy (&prstat.pr_reg, gregs, sizeof (prstat.pr_reg));
      return elfcore_write_note (abfd, buf, bufsiz, note_name,
				 NT_PRSTATUS, &prstat, sizeof (prstat));
    }
  else
#endif
    {
      prstatus_t prstat;

      memset (&prstat, 0, sizeof (prstat));
      prstat.pr_pid = pid;
      prstat.pr_cursig = cursig;
      memcpy (&prstat.pr_reg, gregs, sizeof (prstat.pr_reg));
      return elfcore_write_note (abfd, buf, bufsiz, note_name,
				 NT_PRSTATUS, &prstat, sizeof (prstat));
    }
}
#endif /* HAVE_PRSTATUS_T */

#if defined (HAVE_LWPSTATUS_T)
char *
elfcore_write_lwpstatus (bfd *abfd,
			 char *buf,
			 int *bufsiz,
			 long pid,
			 int cursig,
			 const void *gregs)
{
  lwpstatus_t lwpstat;
  const char *note_name = "CORE";

  memset (&lwpstat, 0, sizeof (lwpstat));
  lwpstat.pr_lwpid  = pid >> 16;
  lwpstat.pr_cursig = cursig;
#if defined (HAVE_LWPSTATUS_T_PR_REG)
  memcpy (lwpstat.pr_reg, gregs, sizeof (lwpstat.pr_reg));
#elif defined (HAVE_LWPSTATUS_T_PR_CONTEXT)
#if !defined(gregs)
  memcpy (lwpstat.pr_context.uc_mcontext.gregs,
	  gregs, sizeof (lwpstat.pr_context.uc_mcontext.gregs));
#else
  memcpy (lwpstat.pr_context.uc_mcontext.__gregs,
	  gregs, sizeof (lwpstat.pr_context.uc_mcontext.__gregs));
#endif
#endif
  return elfcore_write_note (abfd, buf, bufsiz, note_name,
			     NT_LWPSTATUS, &lwpstat, sizeof (lwpstat));
}
#endif /* HAVE_LWPSTATUS_T */

#if defined (HAVE_PSTATUS_T)
char *
elfcore_write_pstatus (bfd *abfd,
		       char *buf,
		       int *bufsiz,
		       long pid,
		       int cursig ATTRIBUTE_UNUSED,
		       const void *gregs ATTRIBUTE_UNUSED)
{
  const char *note_name = "CORE";
#if defined (HAVE_PSTATUS32_T)
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);

  if (bed->s->elfclass == ELFCLASS32)
    {
      pstatus32_t pstat;

      memset (&pstat, 0, sizeof (pstat));
      pstat.pr_pid = pid & 0xffff;
      buf = elfcore_write_note (abfd, buf, bufsiz, note_name,
				NT_PSTATUS, &pstat, sizeof (pstat));
      return buf;
    }
  else
#endif
    {
      pstatus_t pstat;

      memset (&pstat, 0, sizeof (pstat));
      pstat.pr_pid = pid & 0xffff;
      buf = elfcore_write_note (abfd, buf, bufsiz, note_name,
				NT_PSTATUS, &pstat, sizeof (pstat));
      return buf;
    }
}
#endif /* HAVE_PSTATUS_T */

char *
elfcore_write_prfpreg (bfd *abfd,
		       char *buf,
		       int *bufsiz,
		       const void *fpregs,
		       int size)
{
  const char *note_name = "CORE";
  return elfcore_write_note (abfd, buf, bufsiz,
			     note_name, NT_FPREGSET, fpregs, size);
}

char *
elfcore_write_thrmisc (bfd *abfd,
		       char *buf,
		       int *bufsiz,
		       const char *tname,
		       int size)
{
#if defined (HAVE_THRMISC_T)
  char *note_name = "CORE";
  return elfcore_write_note (abfd, buf, bufsiz,
			     note_name, NT_THRMISC, tname, size);
#else
  return buf;
#endif
}

char *
elfcore_write_prxfpreg (bfd *abfd,
			char *buf,
			int *bufsiz,
			const void *xfpregs,
			int size)
{
  char *note_name = "LINUX";
  return elfcore_write_note (abfd, buf, bufsiz,
			     note_name, NT_PRXFPREG, xfpregs, size);
}

static bfd_boolean
elfcore_read_notes (bfd *abfd, file_ptr offset, bfd_size_type size)
{
  char *buf;
  char *p;

  if (size <= 0)
    return TRUE;

  if (bfd_seek (abfd, offset, SEEK_SET) != 0)
    return FALSE;

  buf = bfd_malloc (size);
  if (buf == NULL)
    return FALSE;

  if (bfd_bread (buf, size, abfd) != size)
    {
    error:
      free (buf);
      return FALSE;
    }

  p = buf;
  while (p < buf + size)
    {
      /* FIXME: bad alignment assumption.  */
      Elf_External_Note *xnp = (Elf_External_Note *) p;
      Elf_Internal_Note in;

      in.type = H_GET_32 (abfd, xnp->type);

      in.namesz = H_GET_32 (abfd, xnp->namesz);
      in.namedata = xnp->name;

      in.descsz = H_GET_32 (abfd, xnp->descsz);
      in.descdata = in.namedata + BFD_ALIGN (in.namesz, 4);
      in.descpos = offset + (in.descdata - buf);

      if (CONST_STRNEQ (in.namedata, "NetBSD-CORE"))
	{
	  if (! elfcore_grok_netbsd_note (abfd, &in))
	    goto error;
	}
      else if (CONST_STRNEQ (in.namedata, "QNX"))
	{
	  if (! elfcore_grok_nto_note (abfd, &in))
	    goto error;
	}
      else
	{
	  if (! elfcore_grok_note (abfd, &in))
	    goto error;
	}

      p = in.descdata + BFD_ALIGN (in.descsz, 4);
    }

  free (buf);
  return TRUE;
}

/* Providing external access to the ELF program header table.  */

/* Return an upper bound on the number of bytes required to store a
   copy of ABFD's program header table entries.  Return -1 if an error
   occurs; bfd_get_error will return an appropriate code.  */

long
bfd_get_elf_phdr_upper_bound (bfd *abfd)
{
  if (abfd->xvec->flavour != bfd_target_elf_flavour)
    {
      bfd_set_error (bfd_error_wrong_format);
      return -1;
    }

  return elf_elfheader (abfd)->e_phnum * sizeof (Elf_Internal_Phdr);
}

/* Copy ABFD's program header table entries to *PHDRS.  The entries
   will be stored as an array of Elf_Internal_Phdr structures, as
   defined in include/elf/internal.h.  To find out how large the
   buffer needs to be, call bfd_get_elf_phdr_upper_bound.

   Return the number of program header table entries read, or -1 if an
   error occurs; bfd_get_error will return an appropriate code.  */

int
bfd_get_elf_phdrs (bfd *abfd, void *phdrs)
{
  int num_phdrs;

  if (abfd->xvec->flavour != bfd_target_elf_flavour)
    {
      bfd_set_error (bfd_error_wrong_format);
      return -1;
    }

  num_phdrs = elf_elfheader (abfd)->e_phnum;
  memcpy (phdrs, elf_tdata (abfd)->phdr,
	  num_phdrs * sizeof (Elf_Internal_Phdr));

  return num_phdrs;
}

void
_bfd_elf_sprintf_vma (bfd *abfd ATTRIBUTE_UNUSED, char *buf, bfd_vma value)
{
#ifdef BFD64
  Elf_Internal_Ehdr *i_ehdrp;	/* Elf file header, internal form */

  i_ehdrp = elf_elfheader (abfd);
  if (i_ehdrp == NULL)
    sprintf_vma (buf, value);
  else
    {
      if (i_ehdrp->e_ident[EI_CLASS] == ELFCLASS64)
	{
#if BFD_HOST_64BIT_LONG
	  sprintf (buf, "%016lx", value);
#else
	  sprintf (buf, "%08lx%08lx", _bfd_int64_high (value),
		   _bfd_int64_low (value));
#endif
	}
      else
	sprintf (buf, "%08lx", (unsigned long) (value & 0xffffffff));
    }
#else
  sprintf_vma (buf, value);
#endif
}

void
_bfd_elf_fprintf_vma (bfd *abfd ATTRIBUTE_UNUSED, void *stream, bfd_vma value)
{
#ifdef BFD64
  Elf_Internal_Ehdr *i_ehdrp;	/* Elf file header, internal form */

  i_ehdrp = elf_elfheader (abfd);
  if (i_ehdrp == NULL)
    fprintf_vma ((FILE *) stream, value);
  else
    {
      if (i_ehdrp->e_ident[EI_CLASS] == ELFCLASS64)
	{
#if BFD_HOST_64BIT_LONG
	  fprintf ((FILE *) stream, "%016lx", value);
#else
	  fprintf ((FILE *) stream, "%08lx%08lx",
		   _bfd_int64_high (value), _bfd_int64_low (value));
#endif
	}
      else
	fprintf ((FILE *) stream, "%08lx",
		 (unsigned long) (value & 0xffffffff));
    }
#else
  fprintf_vma ((FILE *) stream, value);
#endif
}

enum elf_reloc_type_class
_bfd_elf_reloc_type_class (const Elf_Internal_Rela *rela ATTRIBUTE_UNUSED)
{
  return reloc_class_normal;
}

/* For RELA architectures, return the relocation value for a
   relocation against a local symbol.  */

bfd_vma
_bfd_elf_rela_local_sym (bfd *abfd,
			 Elf_Internal_Sym *sym,
			 asection **psec,
			 Elf_Internal_Rela *rel)
{
  asection *sec = *psec;
  bfd_vma relocation;

  relocation = (sec->output_section->vma
		+ sec->output_offset
		+ sym->st_value);
  if ((sec->flags & SEC_MERGE)
      && ELF_ST_TYPE (sym->st_info) == STT_SECTION
      && sec->sec_info_type == ELF_INFO_TYPE_MERGE)
    {
      rel->r_addend =
	_bfd_merged_section_offset (abfd, psec,
				    elf_section_data (sec)->sec_info,
				    sym->st_value + rel->r_addend);
      if (sec != *psec)
	{
	  /* If we have changed the section, and our original section is
	     marked with SEC_EXCLUDE, it means that the original
	     SEC_MERGE section has been completely subsumed in some
	     other SEC_MERGE section.  In this case, we need to leave
	     some info around for --emit-relocs.  */
	  if ((sec->flags & SEC_EXCLUDE) != 0)
	    sec->kept_section = *psec;
	  sec = *psec;
	}
      rel->r_addend -= relocation;
      rel->r_addend += sec->output_section->vma + sec->output_offset;
    }
  return relocation;
}

bfd_vma
_bfd_elf_rel_local_sym (bfd *abfd,
			Elf_Internal_Sym *sym,
			asection **psec,
			bfd_vma addend)
{
  asection *sec = *psec;

  if (sec->sec_info_type != ELF_INFO_TYPE_MERGE)
    return sym->st_value + addend;

  return _bfd_merged_section_offset (abfd, psec,
				     elf_section_data (sec)->sec_info,
				     sym->st_value + addend);
}

bfd_vma
_bfd_elf_section_offset (bfd *abfd,
			 struct bfd_link_info *info,
			 asection *sec,
			 bfd_vma offset)
{
  switch (sec->sec_info_type)
    {
    case ELF_INFO_TYPE_STABS:
      return _bfd_stab_section_offset (sec, elf_section_data (sec)->sec_info,
				       offset);
    case ELF_INFO_TYPE_EH_FRAME:
      return _bfd_elf_eh_frame_section_offset (abfd, info, sec, offset);
    default:
      return offset;
    }
}

/* Create a new BFD as if by bfd_openr.  Rather than opening a file,
   reconstruct an ELF file by reading the segments out of remote memory
   based on the ELF file header at EHDR_VMA and the ELF program headers it
   points to.  If not null, *LOADBASEP is filled in with the difference
   between the VMAs from which the segments were read, and the VMAs the
   file headers (and hence BFD's idea of each section's VMA) put them at.

   The function TARGET_READ_MEMORY is called to copy LEN bytes from the
   remote memory at target address VMA into the local buffer at MYADDR; it
   should return zero on success or an `errno' code on failure.  TEMPL must
   be a BFD for an ELF target with the word size and byte order found in
   the remote memory.  */

bfd *
bfd_elf_bfd_from_remote_memory
  (bfd *templ,
   bfd_vma ehdr_vma,
   bfd_vma *loadbasep,
   int (*target_read_memory) (bfd_vma, bfd_byte *, int))
{
  return (*get_elf_backend_data (templ)->elf_backend_bfd_from_remote_memory)
    (templ, ehdr_vma, loadbasep, target_read_memory);
}

long
_bfd_elf_get_synthetic_symtab (bfd *abfd,
			       long symcount ATTRIBUTE_UNUSED,
			       asymbol **syms ATTRIBUTE_UNUSED,
			       long dynsymcount,
			       asymbol **dynsyms,
			       asymbol **ret)
{
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  asection *relplt;
  asymbol *s;
  const char *relplt_name;
  bfd_boolean (*slurp_relocs) (bfd *, asection *, asymbol **, bfd_boolean);
  arelent *p;
  long count, i, n;
  size_t size;
  Elf_Internal_Shdr *hdr;
  char *names;
  asection *plt;

  *ret = NULL;

  if ((abfd->flags & (DYNAMIC | EXEC_P)) == 0)
    return 0;

  if (dynsymcount <= 0)
    return 0;

  if (!bed->plt_sym_val)
    return 0;

  relplt_name = bed->relplt_name;
  if (relplt_name == NULL)
    relplt_name = bed->default_use_rela_p ? ".rela.plt" : ".rel.plt";
  relplt = bfd_get_section_by_name (abfd, relplt_name);
  if (relplt == NULL)
    return 0;

  hdr = &elf_section_data (relplt)->this_hdr;
  if (hdr->sh_link != elf_dynsymtab (abfd)
      || (hdr->sh_type != SHT_REL && hdr->sh_type != SHT_RELA))
    return 0;

  plt = bfd_get_section_by_name (abfd, ".plt");
  if (plt == NULL)
    return 0;

  slurp_relocs = get_elf_backend_data (abfd)->s->slurp_reloc_table;
  if (! (*slurp_relocs) (abfd, relplt, dynsyms, TRUE))
    return -1;

  count = relplt->size / hdr->sh_entsize;
  size = count * sizeof (asymbol);
  p = relplt->relocation;
  for (i = 0; i < count; i++, p++)
    size += strlen ((*p->sym_ptr_ptr)->name) + sizeof ("@plt");

  s = *ret = bfd_malloc (size);
  if (s == NULL)
    return -1;

  names = (char *) (s + count);
  p = relplt->relocation;
  n = 0;
  for (i = 0; i < count; i++, s++, p++)
    {
      size_t len;
      bfd_vma addr;

      addr = bed->plt_sym_val (i, plt, p);
      if (addr == (bfd_vma) -1)
	continue;

      *s = **p->sym_ptr_ptr;
      /* Undefined syms won't have BSF_LOCAL or BSF_GLOBAL set.  Since
	 we are defining a symbol, ensure one of them is set.  */
      if ((s->flags & BSF_LOCAL) == 0)
	s->flags |= BSF_GLOBAL;
      s->section = plt;
      s->value = addr - plt->vma;
      s->name = names;
      len = strlen ((*p->sym_ptr_ptr)->name);
      memcpy (names, (*p->sym_ptr_ptr)->name, len);
      names += len;
      memcpy (names, "@plt", sizeof ("@plt"));
      names += sizeof ("@plt");
      ++n;
    }

  return n;
}

struct elf_symbuf_symbol
{
  unsigned long st_name;	/* Symbol name, index in string tbl */
  unsigned char st_info;	/* Type and binding attributes */
  unsigned char st_other;	/* Visibilty, and target specific */
};

struct elf_symbuf_head
{
  struct elf_symbuf_symbol *ssym;
  bfd_size_type count;
  unsigned int st_shndx;
};

struct elf_symbol
{
  union
    {
      Elf_Internal_Sym *isym;
      struct elf_symbuf_symbol *ssym;
    } u;
  const char *name;
};

/* Sort references to symbols by ascending section number.  */

static int
elf_sort_elf_symbol (const void *arg1, const void *arg2)
{
  const Elf_Internal_Sym *s1 = *(const Elf_Internal_Sym **) arg1;
  const Elf_Internal_Sym *s2 = *(const Elf_Internal_Sym **) arg2;

  return s1->st_shndx - s2->st_shndx;
}

static int
elf_sym_name_compare (const void *arg1, const void *arg2)
{
  const struct elf_symbol *s1 = (const struct elf_symbol *) arg1;
  const struct elf_symbol *s2 = (const struct elf_symbol *) arg2;
  return strcmp (s1->name, s2->name);
}

static struct elf_symbuf_head *
elf_create_symbuf (bfd_size_type symcount, Elf_Internal_Sym *isymbuf)
{
  Elf_Internal_Sym **ind, **indbufend, **indbuf
    = bfd_malloc2 (symcount, sizeof (*indbuf));
  struct elf_symbuf_symbol *ssym;
  struct elf_symbuf_head *ssymbuf, *ssymhead;
  bfd_size_type i, shndx_count;

  if (indbuf == NULL)
    return NULL;

  for (ind = indbuf, i = 0; i < symcount; i++)
    if (isymbuf[i].st_shndx != SHN_UNDEF)
      *ind++ = &isymbuf[i];
  indbufend = ind;

  qsort (indbuf, indbufend - indbuf, sizeof (Elf_Internal_Sym *),
	 elf_sort_elf_symbol);

  shndx_count = 0;
  if (indbufend > indbuf)
    for (ind = indbuf, shndx_count++; ind < indbufend - 1; ind++)
      if (ind[0]->st_shndx != ind[1]->st_shndx)
	shndx_count++;

  ssymbuf = bfd_malloc ((shndx_count + 1) * sizeof (*ssymbuf)
			+ (indbufend - indbuf) * sizeof (*ssym));
  if (ssymbuf == NULL)
    {
      free (indbuf);
      return NULL;
    }

  ssym = (struct elf_symbuf_symbol *) (ssymbuf + shndx_count + 1);
  ssymbuf->ssym = NULL;
  ssymbuf->count = shndx_count;
  ssymbuf->st_shndx = 0;
  for (ssymhead = ssymbuf, ind = indbuf; ind < indbufend; ssym++, ind++)
    {
      if (ind == indbuf || ssymhead->st_shndx != (*ind)->st_shndx)
	{
	  ssymhead++;
	  ssymhead->ssym = ssym;
	  ssymhead->count = 0;
	  ssymhead->st_shndx = (*ind)->st_shndx;
	}
      ssym->st_name = (*ind)->st_name;
      ssym->st_info = (*ind)->st_info;
      ssym->st_other = (*ind)->st_other;
      ssymhead->count++;
    }
  BFD_ASSERT ((bfd_size_type) (ssymhead - ssymbuf) == shndx_count);

  free (indbuf);
  return ssymbuf;
}

/* Check if 2 sections define the same set of local and global
   symbols.  */

bfd_boolean
bfd_elf_match_symbols_in_sections (asection *sec1, asection *sec2,
				   struct bfd_link_info *info)
{
  bfd *bfd1, *bfd2;
  const struct elf_backend_data *bed1, *bed2;
  Elf_Internal_Shdr *hdr1, *hdr2;
  bfd_size_type symcount1, symcount2;
  Elf_Internal_Sym *isymbuf1, *isymbuf2;
  struct elf_symbuf_head *ssymbuf1, *ssymbuf2;
  Elf_Internal_Sym *isym, *isymend;
  struct elf_symbol *symtable1 = NULL, *symtable2 = NULL;
  bfd_size_type count1, count2, i;
  int shndx1, shndx2;
  bfd_boolean result;

  bfd1 = sec1->owner;
  bfd2 = sec2->owner;

  /* If both are .gnu.linkonce sections, they have to have the same
     section name.  */
  if (CONST_STRNEQ (sec1->name, ".gnu.linkonce")
      && CONST_STRNEQ (sec2->name, ".gnu.linkonce"))
    return strcmp (sec1->name + sizeof ".gnu.linkonce",
		   sec2->name + sizeof ".gnu.linkonce") == 0;

  /* Both sections have to be in ELF.  */
  if (bfd_get_flavour (bfd1) != bfd_target_elf_flavour
      || bfd_get_flavour (bfd2) != bfd_target_elf_flavour)
    return FALSE;

  if (elf_section_type (sec1) != elf_section_type (sec2))
    return FALSE;

  if ((elf_section_flags (sec1) & SHF_GROUP) != 0
      && (elf_section_flags (sec2) & SHF_GROUP) != 0)
    {
      /* If both are members of section groups, they have to have the
	 same group name.  */
      if (strcmp (elf_group_name (sec1), elf_group_name (sec2)) != 0)
	return FALSE;
    }

  shndx1 = _bfd_elf_section_from_bfd_section (bfd1, sec1);
  shndx2 = _bfd_elf_section_from_bfd_section (bfd2, sec2);
  if (shndx1 == -1 || shndx2 == -1)
    return FALSE;

  bed1 = get_elf_backend_data (bfd1);
  bed2 = get_elf_backend_data (bfd2);
  hdr1 = &elf_tdata (bfd1)->symtab_hdr;
  symcount1 = hdr1->sh_size / bed1->s->sizeof_sym;
  hdr2 = &elf_tdata (bfd2)->symtab_hdr;
  symcount2 = hdr2->sh_size / bed2->s->sizeof_sym;

  if (symcount1 == 0 || symcount2 == 0)
    return FALSE;

  result = FALSE;
  isymbuf1 = NULL;
  isymbuf2 = NULL;
  ssymbuf1 = elf_tdata (bfd1)->symbuf;
  ssymbuf2 = elf_tdata (bfd2)->symbuf;

  if (ssymbuf1 == NULL)
    {
      isymbuf1 = bfd_elf_get_elf_syms (bfd1, hdr1, symcount1, 0,
				       NULL, NULL, NULL);
      if (isymbuf1 == NULL)
	goto done;

      if (!info->reduce_memory_overheads)
	elf_tdata (bfd1)->symbuf = ssymbuf1
	  = elf_create_symbuf (symcount1, isymbuf1);
    }

  if (ssymbuf1 == NULL || ssymbuf2 == NULL)
    {
      isymbuf2 = bfd_elf_get_elf_syms (bfd2, hdr2, symcount2, 0,
				       NULL, NULL, NULL);
      if (isymbuf2 == NULL)
	goto done;

      if (ssymbuf1 != NULL && !info->reduce_memory_overheads)
	elf_tdata (bfd2)->symbuf = ssymbuf2
	  = elf_create_symbuf (symcount2, isymbuf2);
    }

  if (ssymbuf1 != NULL && ssymbuf2 != NULL)
    {
      /* Optimized faster version.  */
      bfd_size_type lo, hi, mid;
      struct elf_symbol *symp;
      struct elf_symbuf_symbol *ssym, *ssymend;

      lo = 0;
      hi = ssymbuf1->count;
      ssymbuf1++;
      count1 = 0;
      while (lo < hi)
	{
	  mid = (lo + hi) / 2;
	  if ((unsigned int) shndx1 < ssymbuf1[mid].st_shndx)
	    hi = mid;
	  else if ((unsigned int) shndx1 > ssymbuf1[mid].st_shndx)
	    lo = mid + 1;
	  else
	    {
	      count1 = ssymbuf1[mid].count;
	      ssymbuf1 += mid;
	      break;
	    }
	}

      lo = 0;
      hi = ssymbuf2->count;
      ssymbuf2++;
      count2 = 0;
      while (lo < hi)
	{
	  mid = (lo + hi) / 2;
	  if ((unsigned int) shndx2 < ssymbuf2[mid].st_shndx)
	    hi = mid;
	  else if ((unsigned int) shndx2 > ssymbuf2[mid].st_shndx)
	    lo = mid + 1;
	  else
	    {
	      count2 = ssymbuf2[mid].count;
	      ssymbuf2 += mid;
	      break;
	    }
	}

      if (count1 == 0 || count2 == 0 || count1 != count2)
	goto done;

      symtable1 = bfd_malloc (count1 * sizeof (struct elf_symbol));
      symtable2 = bfd_malloc (count2 * sizeof (struct elf_symbol));
      if (symtable1 == NULL || symtable2 == NULL)
	goto done;

      symp = symtable1;
      for (ssym = ssymbuf1->ssym, ssymend = ssym + count1;
	   ssym < ssymend; ssym++, symp++)
	{
	  symp->u.ssym = ssym;
	  symp->name = bfd_elf_string_from_elf_section (bfd1,
							hdr1->sh_link,
							ssym->st_name);
	}

      symp = symtable2;
      for (ssym = ssymbuf2->ssym, ssymend = ssym + count2;
	   ssym < ssymend; ssym++, symp++)
	{
	  symp->u.ssym = ssym;
	  symp->name = bfd_elf_string_from_elf_section (bfd2,
							hdr2->sh_link,
							ssym->st_name);
	}

      /* Sort symbol by name.  */
      qsort (symtable1, count1, sizeof (struct elf_symbol),
	     elf_sym_name_compare);
      qsort (symtable2, count1, sizeof (struct elf_symbol),
	     elf_sym_name_compare);

      for (i = 0; i < count1; i++)
	/* Two symbols must have the same binding, type and name.  */
	if (symtable1 [i].u.ssym->st_info != symtable2 [i].u.ssym->st_info
	    || symtable1 [i].u.ssym->st_other != symtable2 [i].u.ssym->st_other
	    || strcmp (symtable1 [i].name, symtable2 [i].name) != 0)
	  goto done;

      result = TRUE;
      goto done;
    }

  symtable1 = bfd_malloc (symcount1 * sizeof (struct elf_symbol));
  symtable2 = bfd_malloc (symcount2 * sizeof (struct elf_symbol));
  if (symtable1 == NULL || symtable2 == NULL)
    goto done;

  /* Count definitions in the section.  */
  count1 = 0;
  for (isym = isymbuf1, isymend = isym + symcount1; isym < isymend; isym++)
    if (isym->st_shndx == (unsigned int) shndx1)
      symtable1[count1++].u.isym = isym;

  count2 = 0;
  for (isym = isymbuf2, isymend = isym + symcount2; isym < isymend; isym++)
    if (isym->st_shndx == (unsigned int) shndx2)
      symtable2[count2++].u.isym = isym;

  if (count1 == 0 || count2 == 0 || count1 != count2)
    goto done;

  for (i = 0; i < count1; i++)
    symtable1[i].name
      = bfd_elf_string_from_elf_section (bfd1, hdr1->sh_link,
					 symtable1[i].u.isym->st_name);

  for (i = 0; i < count2; i++)
    symtable2[i].name
      = bfd_elf_string_from_elf_section (bfd2, hdr2->sh_link,
					 symtable2[i].u.isym->st_name);

  /* Sort symbol by name.  */
  qsort (symtable1, count1, sizeof (struct elf_symbol),
	 elf_sym_name_compare);
  qsort (symtable2, count1, sizeof (struct elf_symbol),
	 elf_sym_name_compare);

  for (i = 0; i < count1; i++)
    /* Two symbols must have the same binding, type and name.  */
    if (symtable1 [i].u.isym->st_info != symtable2 [i].u.isym->st_info
	|| symtable1 [i].u.isym->st_other != symtable2 [i].u.isym->st_other
	|| strcmp (symtable1 [i].name, symtable2 [i].name) != 0)
      goto done;

  result = TRUE;

done:
  if (symtable1)
    free (symtable1);
  if (symtable2)
    free (symtable2);
  if (isymbuf1)
    free (isymbuf1);
  if (isymbuf2)
    free (isymbuf2);

  return result;
}

/* It is only used by x86-64 so far.  */
asection _bfd_elf_large_com_section
  = BFD_FAKE_SECTION (_bfd_elf_large_com_section,
		      SEC_IS_COMMON, NULL, "LARGE_COMMON", 0);

/* Return TRUE if 2 section types are compatible.  */

bfd_boolean
_bfd_elf_match_sections_by_type (bfd *abfd, const asection *asec,
				 bfd *bbfd, const asection *bsec)
{
  if (asec == NULL
      || bsec == NULL
      || abfd->xvec->flavour != bfd_target_elf_flavour
      || bbfd->xvec->flavour != bfd_target_elf_flavour)
    return TRUE;

  return elf_section_type (asec) == elf_section_type (bsec);
}

void
_bfd_elf_set_osabi (bfd * abfd,
		    struct bfd_link_info * link_info ATTRIBUTE_UNUSED)
{
  Elf_Internal_Ehdr * i_ehdrp;	/* ELF file header, internal form.  */

  i_ehdrp = elf_elfheader (abfd);

  i_ehdrp->e_ident[EI_OSABI] = get_elf_backend_data (abfd)->elf_osabi;
}


/* Return TRUE for ELF symbol types that represent functions.
   This is the default version of this function, which is sufficient for
   most targets.  It returns true if TYPE is STT_FUNC.  */

bfd_boolean
_bfd_elf_is_function_type (unsigned int type)
{
  return (type == STT_FUNC);
}
