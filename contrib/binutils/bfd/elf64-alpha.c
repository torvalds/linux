/* Alpha specific support for 64-bit ELF
   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005,
   2006, 2007 Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@tamu.edu>.

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

/* We need a published ABI spec for this.  Until one comes out, don't
   assume this'll remain unchanged forever.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "elf-bfd.h"

#include "elf/alpha.h"

#define ALPHAECOFF

#define NO_COFF_RELOCS
#define NO_COFF_SYMBOLS
#define NO_COFF_LINENOS

/* Get the ECOFF swapping routines.  Needed for the debug information.  */
#include "coff/internal.h"
#include "coff/sym.h"
#include "coff/symconst.h"
#include "coff/ecoff.h"
#include "coff/alpha.h"
#include "aout/ar.h"
#include "libcoff.h"
#include "libecoff.h"
#define ECOFF_64
#include "ecoffswap.h"


/* Instruction data for plt generation and relaxation.  */

#define OP_LDA		0x08
#define OP_LDAH		0x09
#define OP_LDQ		0x29
#define OP_BR		0x30
#define OP_BSR		0x34

#define INSN_LDA	(OP_LDA << 26)
#define INSN_LDAH	(OP_LDAH << 26)
#define INSN_LDQ	(OP_LDQ << 26)
#define INSN_BR		(OP_BR << 26)

#define INSN_ADDQ	0x40000400
#define INSN_RDUNIQ	0x0000009e
#define INSN_SUBQ	0x40000520
#define INSN_S4SUBQ	0x40000560
#define INSN_UNOP	0x2ffe0000

#define INSN_JSR	0x68004000
#define INSN_JMP	0x68000000
#define INSN_JSR_MASK	0xfc00c000

#define INSN_A(I,A)		(I | (A << 21))
#define INSN_AB(I,A,B)		(I | (A << 21) | (B << 16))
#define INSN_ABC(I,A,B,C)	(I | (A << 21) | (B << 16) | C)
#define INSN_ABO(I,A,B,O)	(I | (A << 21) | (B << 16) | ((O) & 0xffff))
#define INSN_AD(I,A,D)		(I | (A << 21) | (((D) >> 2) & 0x1fffff))

/* PLT/GOT Stuff */

/* Set by ld emulation.  Putting this into the link_info or hash structure
   is simply working too hard.  */
#ifdef USE_SECUREPLT
bfd_boolean elf64_alpha_use_secureplt = TRUE;
#else
bfd_boolean elf64_alpha_use_secureplt = FALSE;
#endif

#define OLD_PLT_HEADER_SIZE	32
#define OLD_PLT_ENTRY_SIZE	12
#define NEW_PLT_HEADER_SIZE	36
#define NEW_PLT_ENTRY_SIZE	4

#define PLT_HEADER_SIZE \
  (elf64_alpha_use_secureplt ? NEW_PLT_HEADER_SIZE : OLD_PLT_HEADER_SIZE)
#define PLT_ENTRY_SIZE \
  (elf64_alpha_use_secureplt ? NEW_PLT_ENTRY_SIZE : OLD_PLT_ENTRY_SIZE)

#define MAX_GOT_SIZE		(64*1024)

#define ELF_DYNAMIC_INTERPRETER "/usr/lib/ld.so"

struct alpha_elf_link_hash_entry
{
  struct elf_link_hash_entry root;

  /* External symbol information.  */
  EXTR esym;

  /* Cumulative flags for all the .got entries.  */
  int flags;

  /* Contexts in which a literal was referenced.  */
#define ALPHA_ELF_LINK_HASH_LU_ADDR	 0x01
#define ALPHA_ELF_LINK_HASH_LU_MEM	 0x02
#define ALPHA_ELF_LINK_HASH_LU_BYTE	 0x04
#define ALPHA_ELF_LINK_HASH_LU_JSR	 0x08
#define ALPHA_ELF_LINK_HASH_LU_TLSGD	 0x10
#define ALPHA_ELF_LINK_HASH_LU_TLSLDM	 0x20
#define ALPHA_ELF_LINK_HASH_LU_JSRDIRECT 0x40
#define ALPHA_ELF_LINK_HASH_LU_PLT	 0x38
#define ALPHA_ELF_LINK_HASH_TLS_IE	 0x80

  /* Used to implement multiple .got subsections.  */
  struct alpha_elf_got_entry
  {
    struct alpha_elf_got_entry *next;

    /* Which .got subsection?  */
    bfd *gotobj;

    /* The addend in effect for this entry.  */
    bfd_vma addend;

    /* The .got offset for this entry.  */
    int got_offset;

    /* The .plt offset for this entry.  */
    int plt_offset;

    /* How many references to this entry?  */
    int use_count;

    /* The relocation type of this entry.  */
    unsigned char reloc_type;

    /* How a LITERAL is used.  */
    unsigned char flags;

    /* Have we initialized the dynamic relocation for this entry?  */
    unsigned char reloc_done;

    /* Have we adjusted this entry for SEC_MERGE?  */
    unsigned char reloc_xlated;
  } *got_entries;

  /* Used to count non-got, non-plt relocations for delayed sizing
     of relocation sections.  */
  struct alpha_elf_reloc_entry
  {
    struct alpha_elf_reloc_entry *next;

    /* Which .reloc section? */
    asection *srel;

    /* What kind of relocation? */
    unsigned int rtype;

    /* Is this against read-only section? */
    unsigned int reltext : 1;

    /* How many did we find?  */
    unsigned long count;
  } *reloc_entries;
};

/* Alpha ELF linker hash table.  */

struct alpha_elf_link_hash_table
{
  struct elf_link_hash_table root;

  /* The head of a list of .got subsections linked through
     alpha_elf_tdata(abfd)->got_link_next.  */
  bfd *got_list;

  /* The most recent relax pass that we've seen.  The GOTs
     should be regenerated if this doesn't match.  */
  int relax_trip;
};

/* Look up an entry in a Alpha ELF linker hash table.  */

#define alpha_elf_link_hash_lookup(table, string, create, copy, follow)	\
  ((struct alpha_elf_link_hash_entry *)					\
   elf_link_hash_lookup (&(table)->root, (string), (create),		\
			 (copy), (follow)))

/* Traverse a Alpha ELF linker hash table.  */

#define alpha_elf_link_hash_traverse(table, func, info)			\
  (elf_link_hash_traverse						\
   (&(table)->root,							\
    (bfd_boolean (*) (struct elf_link_hash_entry *, PTR)) (func),	\
    (info)))

/* Get the Alpha ELF linker hash table from a link_info structure.  */

#define alpha_elf_hash_table(p) \
  ((struct alpha_elf_link_hash_table *) ((p)->hash))

/* Get the object's symbols as our own entry type.  */

#define alpha_elf_sym_hashes(abfd) \
  ((struct alpha_elf_link_hash_entry **)elf_sym_hashes(abfd))

/* Should we do dynamic things to this symbol?  This differs from the 
   generic version in that we never need to consider function pointer
   equality wrt PLT entries -- we don't create a PLT entry if a symbol's
   address is ever taken.  */

static inline bfd_boolean
alpha_elf_dynamic_symbol_p (struct elf_link_hash_entry *h,
			    struct bfd_link_info *info)
{
  return _bfd_elf_dynamic_symbol_p (h, info, 0);
}

/* Create an entry in a Alpha ELF linker hash table.  */

static struct bfd_hash_entry *
elf64_alpha_link_hash_newfunc (struct bfd_hash_entry *entry,
			       struct bfd_hash_table *table,
			       const char *string)
{
  struct alpha_elf_link_hash_entry *ret =
    (struct alpha_elf_link_hash_entry *) entry;

  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (ret == (struct alpha_elf_link_hash_entry *) NULL)
    ret = ((struct alpha_elf_link_hash_entry *)
	   bfd_hash_allocate (table,
			      sizeof (struct alpha_elf_link_hash_entry)));
  if (ret == (struct alpha_elf_link_hash_entry *) NULL)
    return (struct bfd_hash_entry *) ret;

  /* Call the allocation method of the superclass.  */
  ret = ((struct alpha_elf_link_hash_entry *)
	 _bfd_elf_link_hash_newfunc ((struct bfd_hash_entry *) ret,
				     table, string));
  if (ret != (struct alpha_elf_link_hash_entry *) NULL)
    {
      /* Set local fields.  */
      memset (&ret->esym, 0, sizeof (EXTR));
      /* We use -2 as a marker to indicate that the information has
	 not been set.  -1 means there is no associated ifd.  */
      ret->esym.ifd = -2;
      ret->flags = 0;
      ret->got_entries = NULL;
      ret->reloc_entries = NULL;
    }

  return (struct bfd_hash_entry *) ret;
}

/* Create a Alpha ELF linker hash table.  */

static struct bfd_link_hash_table *
elf64_alpha_bfd_link_hash_table_create (bfd *abfd)
{
  struct alpha_elf_link_hash_table *ret;
  bfd_size_type amt = sizeof (struct alpha_elf_link_hash_table);

  ret = (struct alpha_elf_link_hash_table *) bfd_zmalloc (amt);
  if (ret == (struct alpha_elf_link_hash_table *) NULL)
    return NULL;

  if (!_bfd_elf_link_hash_table_init (&ret->root, abfd,
				      elf64_alpha_link_hash_newfunc,
				      sizeof (struct alpha_elf_link_hash_entry)))
    {
      free (ret);
      return NULL;
    }

  return &ret->root.root;
}

/* We have some private fields hanging off of the elf_tdata structure.  */

struct alpha_elf_obj_tdata
{
  struct elf_obj_tdata root;

  /* For every input file, these are the got entries for that object's
     local symbols.  */
  struct alpha_elf_got_entry ** local_got_entries;

  /* For every input file, this is the object that owns the got that
     this input file uses.  */
  bfd *gotobj;

  /* For every got, this is a linked list through the objects using this got */
  bfd *in_got_link_next;

  /* For every got, this is a link to the next got subsegment.  */
  bfd *got_link_next;

  /* For every got, this is the section.  */
  asection *got;

  /* For every got, this is it's total number of words.  */
  int total_got_size;

  /* For every got, this is the sum of the number of words required
     to hold all of the member object's local got.  */
  int local_got_size;
};

#define alpha_elf_tdata(abfd) \
  ((struct alpha_elf_obj_tdata *) (abfd)->tdata.any)

static bfd_boolean
elf64_alpha_mkobject (bfd *abfd)
{
  if (abfd->tdata.any == NULL)
    {
      bfd_size_type amt = sizeof (struct alpha_elf_obj_tdata);
      abfd->tdata.any = bfd_zalloc (abfd, amt);
      if (abfd->tdata.any == NULL)
	return FALSE;
    }
  return bfd_elf_mkobject (abfd);
}

static bfd_boolean
elf64_alpha_object_p (bfd *abfd)
{
  /* Set the right machine number for an Alpha ELF file.  */
  return bfd_default_set_arch_mach (abfd, bfd_arch_alpha, 0);
}

/* A relocation function which doesn't do anything.  */

static bfd_reloc_status_type
elf64_alpha_reloc_nil (bfd *abfd ATTRIBUTE_UNUSED, arelent *reloc,
		       asymbol *sym ATTRIBUTE_UNUSED,
		       PTR data ATTRIBUTE_UNUSED, asection *sec,
		       bfd *output_bfd, char **error_message ATTRIBUTE_UNUSED)
{
  if (output_bfd)
    reloc->address += sec->output_offset;
  return bfd_reloc_ok;
}

/* A relocation function used for an unsupported reloc.  */

static bfd_reloc_status_type
elf64_alpha_reloc_bad (bfd *abfd ATTRIBUTE_UNUSED, arelent *reloc,
		       asymbol *sym ATTRIBUTE_UNUSED,
		       PTR data ATTRIBUTE_UNUSED, asection *sec,
		       bfd *output_bfd, char **error_message ATTRIBUTE_UNUSED)
{
  if (output_bfd)
    reloc->address += sec->output_offset;
  return bfd_reloc_notsupported;
}

/* Do the work of the GPDISP relocation.  */

static bfd_reloc_status_type
elf64_alpha_do_reloc_gpdisp (bfd *abfd, bfd_vma gpdisp, bfd_byte *p_ldah,
			     bfd_byte *p_lda)
{
  bfd_reloc_status_type ret = bfd_reloc_ok;
  bfd_vma addend;
  unsigned long i_ldah, i_lda;

  i_ldah = bfd_get_32 (abfd, p_ldah);
  i_lda = bfd_get_32 (abfd, p_lda);

  /* Complain if the instructions are not correct.  */
  if (((i_ldah >> 26) & 0x3f) != 0x09
      || ((i_lda >> 26) & 0x3f) != 0x08)
    ret = bfd_reloc_dangerous;

  /* Extract the user-supplied offset, mirroring the sign extensions
     that the instructions perform.  */
  addend = ((i_ldah & 0xffff) << 16) | (i_lda & 0xffff);
  addend = (addend ^ 0x80008000) - 0x80008000;

  gpdisp += addend;

  if ((bfd_signed_vma) gpdisp < -(bfd_signed_vma) 0x80000000
      || (bfd_signed_vma) gpdisp >= (bfd_signed_vma) 0x7fff8000)
    ret = bfd_reloc_overflow;

  /* compensate for the sign extension again.  */
  i_ldah = ((i_ldah & 0xffff0000)
	    | (((gpdisp >> 16) + ((gpdisp >> 15) & 1)) & 0xffff));
  i_lda = (i_lda & 0xffff0000) | (gpdisp & 0xffff);

  bfd_put_32 (abfd, (bfd_vma) i_ldah, p_ldah);
  bfd_put_32 (abfd, (bfd_vma) i_lda, p_lda);

  return ret;
}

/* The special function for the GPDISP reloc.  */

static bfd_reloc_status_type
elf64_alpha_reloc_gpdisp (bfd *abfd, arelent *reloc_entry,
			  asymbol *sym ATTRIBUTE_UNUSED, PTR data,
			  asection *input_section, bfd *output_bfd,
			  char **err_msg)
{
  bfd_reloc_status_type ret;
  bfd_vma gp, relocation;
  bfd_vma high_address;
  bfd_byte *p_ldah, *p_lda;

  /* Don't do anything if we're not doing a final link.  */
  if (output_bfd)
    {
      reloc_entry->address += input_section->output_offset;
      return bfd_reloc_ok;
    }

  high_address = bfd_get_section_limit (abfd, input_section);
  if (reloc_entry->address > high_address
      || reloc_entry->address + reloc_entry->addend > high_address)
    return bfd_reloc_outofrange;

  /* The gp used in the portion of the output object to which this
     input object belongs is cached on the input bfd.  */
  gp = _bfd_get_gp_value (abfd);

  relocation = (input_section->output_section->vma
		+ input_section->output_offset
		+ reloc_entry->address);

  p_ldah = (bfd_byte *) data + reloc_entry->address;
  p_lda = p_ldah + reloc_entry->addend;

  ret = elf64_alpha_do_reloc_gpdisp (abfd, gp - relocation, p_ldah, p_lda);

  /* Complain if the instructions are not correct.  */
  if (ret == bfd_reloc_dangerous)
    *err_msg = _("GPDISP relocation did not find ldah and lda instructions");

  return ret;
}

/* In case we're on a 32-bit machine, construct a 64-bit "-1" value
   from smaller values.  Start with zero, widen, *then* decrement.  */
#define MINUS_ONE	(((bfd_vma)0) - 1)

#define SKIP_HOWTO(N) \
  HOWTO(N, 0, 0, 0, 0, 0, 0, elf64_alpha_reloc_bad, 0, 0, 0, 0, 0)

static reloc_howto_type elf64_alpha_howto_table[] =
{
  HOWTO (R_ALPHA_NONE,		/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 elf64_alpha_reloc_nil,	/* special_function */
	 "NONE",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* A 32 bit reference to a symbol.  */
  HOWTO (R_ALPHA_REFLONG,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,			/* special_function */
	 "REFLONG",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffffffff,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A 64 bit reference to a symbol.  */
  HOWTO (R_ALPHA_REFQUAD,	/* type */
	 0,			/* rightshift */
	 4,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,			/* special_function */
	 "REFQUAD",		/* name */
	 FALSE,			/* partial_inplace */
	 MINUS_ONE,		/* src_mask */
	 MINUS_ONE,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A 32 bit GP relative offset.  This is just like REFLONG except
     that when the value is used the value of the gp register will be
     added in.  */
  HOWTO (R_ALPHA_GPREL32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,			/* special_function */
	 "GPREL32",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffffffff,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used for an instruction that refers to memory off the GP register.  */
  HOWTO (R_ALPHA_LITERAL,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "ELF_LITERAL",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* This reloc only appears immediately following an ELF_LITERAL reloc.
     It identifies a use of the literal.  The symbol index is special:
     1 means the literal address is in the base register of a memory
     format instruction; 2 means the literal address is in the byte
     offset register of a byte-manipulation instruction; 3 means the
     literal address is in the target register of a jsr instruction.
     This does not actually do any relocation.  */
  HOWTO (R_ALPHA_LITUSE,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 elf64_alpha_reloc_nil,	/* special_function */
	 "LITUSE",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Load the gp register.  This is always used for a ldah instruction
     which loads the upper 16 bits of the gp register.  The symbol
     index of the GPDISP instruction is an offset in bytes to the lda
     instruction that loads the lower 16 bits.  The value to use for
     the relocation is the difference between the GP value and the
     current location; the load will always be done against a register
     holding the current address.

     NOTE: Unlike ECOFF, partial in-place relocation is not done.  If
     any offset is present in the instructions, it is an offset from
     the register to the ldah instruction.  This lets us avoid any
     stupid hackery like inventing a gp value to do partial relocation
     against.  Also unlike ECOFF, we do the whole relocation off of
     the GPDISP rather than a GPDISP_HI16/GPDISP_LO16 pair.  An odd,
     space consuming bit, that, since all the information was present
     in the GPDISP_HI16 reloc.  */
  HOWTO (R_ALPHA_GPDISP,	/* type */
	 16,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 elf64_alpha_reloc_gpdisp, /* special_function */
	 "GPDISP",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* A 21 bit branch.  */
  HOWTO (R_ALPHA_BRADDR,	/* type */
	 2,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 21,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "BRADDR",		/* name */
	 FALSE,			/* partial_inplace */
	 0x1fffff,		/* src_mask */
	 0x1fffff,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* A hint for a jump to a register.  */
  HOWTO (R_ALPHA_HINT,		/* type */
	 2,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 14,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 0,			/* special_function */
	 "HINT",		/* name */
	 FALSE,			/* partial_inplace */
	 0x3fff,		/* src_mask */
	 0x3fff,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 16 bit PC relative offset.  */
  HOWTO (R_ALPHA_SREL16,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "SREL16",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 32 bit PC relative offset.  */
  HOWTO (R_ALPHA_SREL32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "SREL32",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffffffff,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* A 64 bit PC relative offset.  */
  HOWTO (R_ALPHA_SREL64,	/* type */
	 0,			/* rightshift */
	 4,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "SREL64",		/* name */
	 FALSE,			/* partial_inplace */
	 MINUS_ONE,		/* src_mask */
	 MINUS_ONE,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Skip 12 - 16; deprecated ECOFF relocs.  */
  SKIP_HOWTO (12),
  SKIP_HOWTO (13),
  SKIP_HOWTO (14),
  SKIP_HOWTO (15),
  SKIP_HOWTO (16),

  /* The high 16 bits of the displacement from GP to the target.  */
  HOWTO (R_ALPHA_GPRELHIGH,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "GPRELHIGH",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* The low 16 bits of the displacement from GP to the target.  */
  HOWTO (R_ALPHA_GPRELLOW,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 0,			/* special_function */
	 "GPRELLOW",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A 16-bit displacement from the GP to the target.  */
  HOWTO (R_ALPHA_GPREL16,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "GPREL16",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Skip 20 - 23; deprecated ECOFF relocs.  */
  SKIP_HOWTO (20),
  SKIP_HOWTO (21),
  SKIP_HOWTO (22),
  SKIP_HOWTO (23),

  /* Misc ELF relocations.  */

  /* A dynamic relocation to copy the target into our .dynbss section.  */
  /* Not generated, as all Alpha objects use PIC, so it is not needed.  It
     is present because every other ELF has one, but should not be used
     because .dynbss is an ugly thing.  */
  HOWTO (R_ALPHA_COPY,
	 0,
	 0,
	 0,
	 FALSE,
	 0,
	 complain_overflow_dont,
	 bfd_elf_generic_reloc,
	 "COPY",
	 FALSE,
	 0,
	 0,
	 TRUE),

  /* A dynamic relocation for a .got entry.  */
  HOWTO (R_ALPHA_GLOB_DAT,
	 0,
	 0,
	 0,
	 FALSE,
	 0,
	 complain_overflow_dont,
	 bfd_elf_generic_reloc,
	 "GLOB_DAT",
	 FALSE,
	 0,
	 0,
	 TRUE),

  /* A dynamic relocation for a .plt entry.  */
  HOWTO (R_ALPHA_JMP_SLOT,
	 0,
	 0,
	 0,
	 FALSE,
	 0,
	 complain_overflow_dont,
	 bfd_elf_generic_reloc,
	 "JMP_SLOT",
	 FALSE,
	 0,
	 0,
	 TRUE),

  /* A dynamic relocation to add the base of the DSO to a 64-bit field.  */
  HOWTO (R_ALPHA_RELATIVE,
	 0,
	 0,
	 0,
	 FALSE,
	 0,
	 complain_overflow_dont,
	 bfd_elf_generic_reloc,
	 "RELATIVE",
	 FALSE,
	 0,
	 0,
	 TRUE),

  /* A 21 bit branch that adjusts for gp loads.  */
  HOWTO (R_ALPHA_BRSGP,		/* type */
	 2,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 21,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "BRSGP",		/* name */
	 FALSE,			/* partial_inplace */
	 0x1fffff,		/* src_mask */
	 0x1fffff,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Creates a tls_index for the symbol in the got.  */
  HOWTO (R_ALPHA_TLSGD,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "TLSGD",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Creates a tls_index for the (current) module in the got.  */
  HOWTO (R_ALPHA_TLSLDM,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "TLSLDM",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A dynamic relocation for a DTP module entry.  */
  HOWTO (R_ALPHA_DTPMOD64,	/* type */
	 0,			/* rightshift */
	 4,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,			/* special_function */
	 "DTPMOD64",		/* name */
	 FALSE,			/* partial_inplace */
	 MINUS_ONE,		/* src_mask */
	 MINUS_ONE,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Creates a 64-bit offset in the got for the displacement
     from DTP to the target.  */
  HOWTO (R_ALPHA_GOTDTPREL,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "GOTDTPREL",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A dynamic relocation for a displacement from DTP to the target.  */
  HOWTO (R_ALPHA_DTPREL64,	/* type */
	 0,			/* rightshift */
	 4,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,			/* special_function */
	 "DTPREL64",		/* name */
	 FALSE,			/* partial_inplace */
	 MINUS_ONE,		/* src_mask */
	 MINUS_ONE,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* The high 16 bits of the displacement from DTP to the target.  */
  HOWTO (R_ALPHA_DTPRELHI,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "DTPRELHI",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* The low 16 bits of the displacement from DTP to the target.  */
  HOWTO (R_ALPHA_DTPRELLO,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 0,			/* special_function */
	 "DTPRELLO",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A 16-bit displacement from DTP to the target.  */
  HOWTO (R_ALPHA_DTPREL16,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "DTPREL16",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Creates a 64-bit offset in the got for the displacement
     from TP to the target.  */
  HOWTO (R_ALPHA_GOTTPREL,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "GOTTPREL",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A dynamic relocation for a displacement from TP to the target.  */
  HOWTO (R_ALPHA_TPREL64,	/* type */
	 0,			/* rightshift */
	 4,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 0,			/* special_function */
	 "TPREL64",		/* name */
	 FALSE,			/* partial_inplace */
	 MINUS_ONE,		/* src_mask */
	 MINUS_ONE,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* The high 16 bits of the displacement from TP to the target.  */
  HOWTO (R_ALPHA_TPRELHI,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "TPRELHI",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* The low 16 bits of the displacement from TP to the target.  */
  HOWTO (R_ALPHA_TPRELLO,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 0,			/* special_function */
	 "TPRELLO",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A 16-bit displacement from TP to the target.  */
  HOWTO (R_ALPHA_TPREL16,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 0,			/* special_function */
	 "TPREL16",		/* name */
	 FALSE,			/* partial_inplace */
	 0xffff,		/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */
};

/* A mapping from BFD reloc types to Alpha ELF reloc types.  */

struct elf_reloc_map
{
  bfd_reloc_code_real_type bfd_reloc_val;
  int elf_reloc_val;
};

static const struct elf_reloc_map elf64_alpha_reloc_map[] =
{
  {BFD_RELOC_NONE,			R_ALPHA_NONE},
  {BFD_RELOC_32,			R_ALPHA_REFLONG},
  {BFD_RELOC_64,			R_ALPHA_REFQUAD},
  {BFD_RELOC_CTOR,			R_ALPHA_REFQUAD},
  {BFD_RELOC_GPREL32,			R_ALPHA_GPREL32},
  {BFD_RELOC_ALPHA_ELF_LITERAL,		R_ALPHA_LITERAL},
  {BFD_RELOC_ALPHA_LITUSE,		R_ALPHA_LITUSE},
  {BFD_RELOC_ALPHA_GPDISP,		R_ALPHA_GPDISP},
  {BFD_RELOC_23_PCREL_S2,		R_ALPHA_BRADDR},
  {BFD_RELOC_ALPHA_HINT,		R_ALPHA_HINT},
  {BFD_RELOC_16_PCREL,			R_ALPHA_SREL16},
  {BFD_RELOC_32_PCREL,			R_ALPHA_SREL32},
  {BFD_RELOC_64_PCREL,			R_ALPHA_SREL64},
  {BFD_RELOC_ALPHA_GPREL_HI16,		R_ALPHA_GPRELHIGH},
  {BFD_RELOC_ALPHA_GPREL_LO16,		R_ALPHA_GPRELLOW},
  {BFD_RELOC_GPREL16,			R_ALPHA_GPREL16},
  {BFD_RELOC_ALPHA_BRSGP,		R_ALPHA_BRSGP},
  {BFD_RELOC_ALPHA_TLSGD,		R_ALPHA_TLSGD},
  {BFD_RELOC_ALPHA_TLSLDM,		R_ALPHA_TLSLDM},
  {BFD_RELOC_ALPHA_DTPMOD64,		R_ALPHA_DTPMOD64},
  {BFD_RELOC_ALPHA_GOTDTPREL16,		R_ALPHA_GOTDTPREL},
  {BFD_RELOC_ALPHA_DTPREL64,		R_ALPHA_DTPREL64},
  {BFD_RELOC_ALPHA_DTPREL_HI16,		R_ALPHA_DTPRELHI},
  {BFD_RELOC_ALPHA_DTPREL_LO16,		R_ALPHA_DTPRELLO},
  {BFD_RELOC_ALPHA_DTPREL16,		R_ALPHA_DTPREL16},
  {BFD_RELOC_ALPHA_GOTTPREL16,		R_ALPHA_GOTTPREL},
  {BFD_RELOC_ALPHA_TPREL64,		R_ALPHA_TPREL64},
  {BFD_RELOC_ALPHA_TPREL_HI16,		R_ALPHA_TPRELHI},
  {BFD_RELOC_ALPHA_TPREL_LO16,		R_ALPHA_TPRELLO},
  {BFD_RELOC_ALPHA_TPREL16,		R_ALPHA_TPREL16},
};

/* Given a BFD reloc type, return a HOWTO structure.  */

static reloc_howto_type *
elf64_alpha_bfd_reloc_type_lookup (bfd *abfd ATTRIBUTE_UNUSED,
				   bfd_reloc_code_real_type code)
{
  const struct elf_reloc_map *i, *e;
  i = e = elf64_alpha_reloc_map;
  e += sizeof (elf64_alpha_reloc_map) / sizeof (struct elf_reloc_map);
  for (; i != e; ++i)
    {
      if (i->bfd_reloc_val == code)
	return &elf64_alpha_howto_table[i->elf_reloc_val];
    }
  return 0;
}

static reloc_howto_type *
elf64_alpha_bfd_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED,
				   const char *r_name)
{
  unsigned int i;

  for (i = 0;
       i < (sizeof (elf64_alpha_howto_table)
	    / sizeof (elf64_alpha_howto_table[0]));
       i++)
    if (elf64_alpha_howto_table[i].name != NULL
	&& strcasecmp (elf64_alpha_howto_table[i].name, r_name) == 0)
      return &elf64_alpha_howto_table[i];

  return NULL;
}

/* Given an Alpha ELF reloc type, fill in an arelent structure.  */

static void
elf64_alpha_info_to_howto (bfd *abfd ATTRIBUTE_UNUSED, arelent *cache_ptr,
			   Elf_Internal_Rela *dst)
{
  unsigned r_type = ELF64_R_TYPE(dst->r_info);
  BFD_ASSERT (r_type < (unsigned int) R_ALPHA_max);
  cache_ptr->howto = &elf64_alpha_howto_table[r_type];
}

/* These two relocations create a two-word entry in the got.  */
#define alpha_got_entry_size(r_type) \
  (r_type == R_ALPHA_TLSGD || r_type == R_ALPHA_TLSLDM ? 16 : 8)

/* This is PT_TLS segment p_vaddr.  */
#define alpha_get_dtprel_base(info) \
  (elf_hash_table (info)->tls_sec->vma)

/* Main program TLS (whose template starts at PT_TLS p_vaddr)
   is assigned offset round(16, PT_TLS p_align).  */
#define alpha_get_tprel_base(info) \
  (elf_hash_table (info)->tls_sec->vma					\
   - align_power ((bfd_vma) 16,						\
		  elf_hash_table (info)->tls_sec->alignment_power))

/* Handle an Alpha specific section when reading an object file.  This
   is called when bfd_section_from_shdr finds a section with an unknown
   type.
   FIXME: We need to handle the SHF_ALPHA_GPREL flag, but I'm not sure
   how to.  */

static bfd_boolean
elf64_alpha_section_from_shdr (bfd *abfd,
			       Elf_Internal_Shdr *hdr,
			       const char *name,
			       int shindex)
{
  asection *newsect;

  /* There ought to be a place to keep ELF backend specific flags, but
     at the moment there isn't one.  We just keep track of the
     sections by their name, instead.  Fortunately, the ABI gives
     suggested names for all the MIPS specific sections, so we will
     probably get away with this.  */
  switch (hdr->sh_type)
    {
    case SHT_ALPHA_DEBUG:
      if (strcmp (name, ".mdebug") != 0)
	return FALSE;
      break;
    default:
      return FALSE;
    }

  if (! _bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex))
    return FALSE;
  newsect = hdr->bfd_section;

  if (hdr->sh_type == SHT_ALPHA_DEBUG)
    {
      if (! bfd_set_section_flags (abfd, newsect,
				   (bfd_get_section_flags (abfd, newsect)
				    | SEC_DEBUGGING)))
	return FALSE;
    }

  return TRUE;
}

/* Convert Alpha specific section flags to bfd internal section flags.  */

static bfd_boolean
elf64_alpha_section_flags (flagword *flags, const Elf_Internal_Shdr *hdr)
{
  if (hdr->sh_flags & SHF_ALPHA_GPREL)
    *flags |= SEC_SMALL_DATA;

  return TRUE;
}

/* Set the correct type for an Alpha ELF section.  We do this by the
   section name, which is a hack, but ought to work.  */

static bfd_boolean
elf64_alpha_fake_sections (bfd *abfd, Elf_Internal_Shdr *hdr, asection *sec)
{
  register const char *name;

  name = bfd_get_section_name (abfd, sec);

  if (strcmp (name, ".mdebug") == 0)
    {
      hdr->sh_type = SHT_ALPHA_DEBUG;
      /* In a shared object on Irix 5.3, the .mdebug section has an
         entsize of 0.  FIXME: Does this matter?  */
      if ((abfd->flags & DYNAMIC) != 0 )
	hdr->sh_entsize = 0;
      else
	hdr->sh_entsize = 1;
    }
  else if ((sec->flags & SEC_SMALL_DATA)
	   || strcmp (name, ".sdata") == 0
	   || strcmp (name, ".sbss") == 0
	   || strcmp (name, ".lit4") == 0
	   || strcmp (name, ".lit8") == 0)
    hdr->sh_flags |= SHF_ALPHA_GPREL;

  return TRUE;
}

/* Hook called by the linker routine which adds symbols from an object
   file.  We use it to put .comm items in .sbss, and not .bss.  */

static bfd_boolean
elf64_alpha_add_symbol_hook (bfd *abfd, struct bfd_link_info *info,
			     Elf_Internal_Sym *sym,
			     const char **namep ATTRIBUTE_UNUSED,
			     flagword *flagsp ATTRIBUTE_UNUSED,
			     asection **secp, bfd_vma *valp)
{
  if (sym->st_shndx == SHN_COMMON
      && !info->relocatable
      && sym->st_size <= elf_gp_size (abfd))
    {
      /* Common symbols less than or equal to -G nn bytes are
	 automatically put into .sbss.  */

      asection *scomm = bfd_get_section_by_name (abfd, ".scommon");

      if (scomm == NULL)
	{
	  scomm = bfd_make_section_with_flags (abfd, ".scommon",
					       (SEC_ALLOC
						| SEC_IS_COMMON
						| SEC_LINKER_CREATED));
	  if (scomm == NULL)
	    return FALSE;
	}

      *secp = scomm;
      *valp = sym->st_size;
    }

  return TRUE;
}

/* Create the .got section.  */

static bfd_boolean
elf64_alpha_create_got_section (bfd *abfd,
				struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  flagword flags;
  asection *s;

  flags = (SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS | SEC_IN_MEMORY
	   | SEC_LINKER_CREATED);
  s = bfd_make_section_anyway_with_flags (abfd, ".got", flags);
  if (s == NULL
      || !bfd_set_section_alignment (abfd, s, 3))
    return FALSE;

  alpha_elf_tdata (abfd)->got = s;

  /* Make sure the object's gotobj is set to itself so that we default
     to every object with its own .got.  We'll merge .gots later once
     we've collected each object's info.  */
  alpha_elf_tdata (abfd)->gotobj = abfd;

  return TRUE;
}

/* Create all the dynamic sections.  */

static bfd_boolean
elf64_alpha_create_dynamic_sections (bfd *abfd, struct bfd_link_info *info)
{
  asection *s;
  flagword flags;
  struct elf_link_hash_entry *h;

  /* We need to create .plt, .rela.plt, .got, and .rela.got sections.  */

  flags = (SEC_ALLOC | SEC_LOAD | SEC_CODE | SEC_HAS_CONTENTS | SEC_IN_MEMORY
	   | SEC_LINKER_CREATED
	   | (elf64_alpha_use_secureplt ? SEC_READONLY : 0));
  s = bfd_make_section_anyway_with_flags (abfd, ".plt", flags);
  if (s == NULL || ! bfd_set_section_alignment (abfd, s, 4))
    return FALSE;

  /* Define the symbol _PROCEDURE_LINKAGE_TABLE_ at the start of the
     .plt section.  */
  h = _bfd_elf_define_linkage_sym (abfd, info, s,
				   "_PROCEDURE_LINKAGE_TABLE_");
  elf_hash_table (info)->hplt = h;
  if (h == NULL)
    return FALSE;

  flags = (SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS | SEC_IN_MEMORY
	   | SEC_LINKER_CREATED | SEC_READONLY);
  s = bfd_make_section_anyway_with_flags (abfd, ".rela.plt", flags);
  if (s == NULL || ! bfd_set_section_alignment (abfd, s, 3))
    return FALSE;

  if (elf64_alpha_use_secureplt)
    {
      flags = SEC_ALLOC | SEC_LINKER_CREATED;
      s = bfd_make_section_anyway_with_flags (abfd, ".got.plt", flags);
      if (s == NULL || ! bfd_set_section_alignment (abfd, s, 3))
	return FALSE;
    }

  /* We may or may not have created a .got section for this object, but
     we definitely havn't done the rest of the work.  */

  if (alpha_elf_tdata(abfd)->gotobj == NULL)
    {
      if (!elf64_alpha_create_got_section (abfd, info))
	return FALSE;
    }

  flags = (SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS | SEC_IN_MEMORY
	   | SEC_LINKER_CREATED | SEC_READONLY);
  s = bfd_make_section_anyway_with_flags (abfd, ".rela.got", flags);
  if (s == NULL
      || !bfd_set_section_alignment (abfd, s, 3))
    return FALSE;

  /* Define the symbol _GLOBAL_OFFSET_TABLE_ at the start of the
     dynobj's .got section.  We don't do this in the linker script
     because we don't want to define the symbol if we are not creating
     a global offset table.  */
  h = _bfd_elf_define_linkage_sym (abfd, info, alpha_elf_tdata(abfd)->got,
				   "_GLOBAL_OFFSET_TABLE_");
  elf_hash_table (info)->hgot = h;
  if (h == NULL)
    return FALSE;

  return TRUE;
}

/* Read ECOFF debugging information from a .mdebug section into a
   ecoff_debug_info structure.  */

static bfd_boolean
elf64_alpha_read_ecoff_info (bfd *abfd, asection *section,
			     struct ecoff_debug_info *debug)
{
  HDRR *symhdr;
  const struct ecoff_debug_swap *swap;
  char *ext_hdr = NULL;

  swap = get_elf_backend_data (abfd)->elf_backend_ecoff_debug_swap;
  memset (debug, 0, sizeof (*debug));

  ext_hdr = (char *) bfd_malloc (swap->external_hdr_size);
  if (ext_hdr == NULL && swap->external_hdr_size != 0)
    goto error_return;

  if (! bfd_get_section_contents (abfd, section, ext_hdr, (file_ptr) 0,
				  swap->external_hdr_size))
    goto error_return;

  symhdr = &debug->symbolic_header;
  (*swap->swap_hdr_in) (abfd, ext_hdr, symhdr);

  /* The symbolic header contains absolute file offsets and sizes to
     read.  */
#define READ(ptr, offset, count, size, type)				\
  if (symhdr->count == 0)						\
    debug->ptr = NULL;							\
  else									\
    {									\
      bfd_size_type amt = (bfd_size_type) size * symhdr->count;		\
      debug->ptr = (type) bfd_malloc (amt);				\
      if (debug->ptr == NULL)						\
	goto error_return;						\
      if (bfd_seek (abfd, (file_ptr) symhdr->offset, SEEK_SET) != 0	\
	  || bfd_bread (debug->ptr, amt, abfd) != amt)			\
	goto error_return;						\
    }

  READ (line, cbLineOffset, cbLine, sizeof (unsigned char), unsigned char *);
  READ (external_dnr, cbDnOffset, idnMax, swap->external_dnr_size, PTR);
  READ (external_pdr, cbPdOffset, ipdMax, swap->external_pdr_size, PTR);
  READ (external_sym, cbSymOffset, isymMax, swap->external_sym_size, PTR);
  READ (external_opt, cbOptOffset, ioptMax, swap->external_opt_size, PTR);
  READ (external_aux, cbAuxOffset, iauxMax, sizeof (union aux_ext),
	union aux_ext *);
  READ (ss, cbSsOffset, issMax, sizeof (char), char *);
  READ (ssext, cbSsExtOffset, issExtMax, sizeof (char), char *);
  READ (external_fdr, cbFdOffset, ifdMax, swap->external_fdr_size, PTR);
  READ (external_rfd, cbRfdOffset, crfd, swap->external_rfd_size, PTR);
  READ (external_ext, cbExtOffset, iextMax, swap->external_ext_size, PTR);
#undef READ

  debug->fdr = NULL;

  return TRUE;

 error_return:
  if (ext_hdr != NULL)
    free (ext_hdr);
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
  if (debug->ssext != NULL)
    free (debug->ssext);
  if (debug->external_fdr != NULL)
    free (debug->external_fdr);
  if (debug->external_rfd != NULL)
    free (debug->external_rfd);
  if (debug->external_ext != NULL)
    free (debug->external_ext);
  return FALSE;
}

/* Alpha ELF local labels start with '$'.  */

static bfd_boolean
elf64_alpha_is_local_label_name (bfd *abfd ATTRIBUTE_UNUSED, const char *name)
{
  return name[0] == '$';
}

/* Alpha ELF follows MIPS ELF in using a special find_nearest_line
   routine in order to handle the ECOFF debugging information.  We
   still call this mips_elf_find_line because of the slot
   find_line_info in elf_obj_tdata is declared that way.  */

struct mips_elf_find_line
{
  struct ecoff_debug_info d;
  struct ecoff_find_line i;
};

static bfd_boolean
elf64_alpha_find_nearest_line (bfd *abfd, asection *section, asymbol **symbols,
			       bfd_vma offset, const char **filename_ptr,
			       const char **functionname_ptr,
			       unsigned int *line_ptr)
{
  asection *msec;

  if (_bfd_dwarf2_find_nearest_line (abfd, section, symbols, offset,
				     filename_ptr, functionname_ptr,
				     line_ptr, 0,
				     &elf_tdata (abfd)->dwarf2_find_line_info))
    return TRUE;

  msec = bfd_get_section_by_name (abfd, ".mdebug");
  if (msec != NULL)
    {
      flagword origflags;
      struct mips_elf_find_line *fi;
      const struct ecoff_debug_swap * const swap =
	get_elf_backend_data (abfd)->elf_backend_ecoff_debug_swap;

      /* If we are called during a link, alpha_elf_final_link may have
	 cleared the SEC_HAS_CONTENTS field.  We force it back on here
	 if appropriate (which it normally will be).  */
      origflags = msec->flags;
      if (elf_section_data (msec)->this_hdr.sh_type != SHT_NOBITS)
	msec->flags |= SEC_HAS_CONTENTS;

      fi = elf_tdata (abfd)->find_line_info;
      if (fi == NULL)
	{
	  bfd_size_type external_fdr_size;
	  char *fraw_src;
	  char *fraw_end;
	  struct fdr *fdr_ptr;
	  bfd_size_type amt = sizeof (struct mips_elf_find_line);

	  fi = (struct mips_elf_find_line *) bfd_zalloc (abfd, amt);
	  if (fi == NULL)
	    {
	      msec->flags = origflags;
	      return FALSE;
	    }

	  if (!elf64_alpha_read_ecoff_info (abfd, msec, &fi->d))
	    {
	      msec->flags = origflags;
	      return FALSE;
	    }

	  /* Swap in the FDR information.  */
	  amt = fi->d.symbolic_header.ifdMax * sizeof (struct fdr);
	  fi->d.fdr = (struct fdr *) bfd_alloc (abfd, amt);
	  if (fi->d.fdr == NULL)
	    {
	      msec->flags = origflags;
	      return FALSE;
	    }
	  external_fdr_size = swap->external_fdr_size;
	  fdr_ptr = fi->d.fdr;
	  fraw_src = (char *) fi->d.external_fdr;
	  fraw_end = (fraw_src
		      + fi->d.symbolic_header.ifdMax * external_fdr_size);
	  for (; fraw_src < fraw_end; fraw_src += external_fdr_size, fdr_ptr++)
	    (*swap->swap_fdr_in) (abfd, (PTR) fraw_src, fdr_ptr);

	  elf_tdata (abfd)->find_line_info = fi;

	  /* Note that we don't bother to ever free this information.
             find_nearest_line is either called all the time, as in
             objdump -l, so the information should be saved, or it is
             rarely called, as in ld error messages, so the memory
             wasted is unimportant.  Still, it would probably be a
             good idea for free_cached_info to throw it away.  */
	}

      if (_bfd_ecoff_locate_line (abfd, section, offset, &fi->d, swap,
				  &fi->i, filename_ptr, functionname_ptr,
				  line_ptr))
	{
	  msec->flags = origflags;
	  return TRUE;
	}

      msec->flags = origflags;
    }

  /* Fall back on the generic ELF find_nearest_line routine.  */

  return _bfd_elf_find_nearest_line (abfd, section, symbols, offset,
				     filename_ptr, functionname_ptr,
				     line_ptr);
}

/* Structure used to pass information to alpha_elf_output_extsym.  */

struct extsym_info
{
  bfd *abfd;
  struct bfd_link_info *info;
  struct ecoff_debug_info *debug;
  const struct ecoff_debug_swap *swap;
  bfd_boolean failed;
};

static bfd_boolean
elf64_alpha_output_extsym (struct alpha_elf_link_hash_entry *h, PTR data)
{
  struct extsym_info *einfo = (struct extsym_info *) data;
  bfd_boolean strip;
  asection *sec, *output_section;

  if (h->root.root.type == bfd_link_hash_warning)
    h = (struct alpha_elf_link_hash_entry *) h->root.root.u.i.link;

  if (h->root.indx == -2)
    strip = FALSE;
  else if ((h->root.def_dynamic
	    || h->root.ref_dynamic
	    || h->root.root.type == bfd_link_hash_new)
	   && !h->root.def_regular
	   && !h->root.ref_regular)
    strip = TRUE;
  else if (einfo->info->strip == strip_all
	   || (einfo->info->strip == strip_some
	       && bfd_hash_lookup (einfo->info->keep_hash,
				   h->root.root.root.string,
				   FALSE, FALSE) == NULL))
    strip = TRUE;
  else
    strip = FALSE;

  if (strip)
    return TRUE;

  if (h->esym.ifd == -2)
    {
      h->esym.jmptbl = 0;
      h->esym.cobol_main = 0;
      h->esym.weakext = 0;
      h->esym.reserved = 0;
      h->esym.ifd = ifdNil;
      h->esym.asym.value = 0;
      h->esym.asym.st = stGlobal;

      if (h->root.root.type != bfd_link_hash_defined
	  && h->root.root.type != bfd_link_hash_defweak)
	h->esym.asym.sc = scAbs;
      else
	{
	  const char *name;

	  sec = h->root.root.u.def.section;
	  output_section = sec->output_section;

	  /* When making a shared library and symbol h is the one from
	     the another shared library, OUTPUT_SECTION may be null.  */
	  if (output_section == NULL)
	    h->esym.asym.sc = scUndefined;
	  else
	    {
	      name = bfd_section_name (output_section->owner, output_section);

	      if (strcmp (name, ".text") == 0)
		h->esym.asym.sc = scText;
	      else if (strcmp (name, ".data") == 0)
		h->esym.asym.sc = scData;
	      else if (strcmp (name, ".sdata") == 0)
		h->esym.asym.sc = scSData;
	      else if (strcmp (name, ".rodata") == 0
		       || strcmp (name, ".rdata") == 0)
		h->esym.asym.sc = scRData;
	      else if (strcmp (name, ".bss") == 0)
		h->esym.asym.sc = scBss;
	      else if (strcmp (name, ".sbss") == 0)
		h->esym.asym.sc = scSBss;
	      else if (strcmp (name, ".init") == 0)
		h->esym.asym.sc = scInit;
	      else if (strcmp (name, ".fini") == 0)
		h->esym.asym.sc = scFini;
	      else
		h->esym.asym.sc = scAbs;
	    }
	}

      h->esym.asym.reserved = 0;
      h->esym.asym.index = indexNil;
    }

  if (h->root.root.type == bfd_link_hash_common)
    h->esym.asym.value = h->root.root.u.c.size;
  else if (h->root.root.type == bfd_link_hash_defined
	   || h->root.root.type == bfd_link_hash_defweak)
    {
      if (h->esym.asym.sc == scCommon)
	h->esym.asym.sc = scBss;
      else if (h->esym.asym.sc == scSCommon)
	h->esym.asym.sc = scSBss;

      sec = h->root.root.u.def.section;
      output_section = sec->output_section;
      if (output_section != NULL)
	h->esym.asym.value = (h->root.root.u.def.value
			      + sec->output_offset
			      + output_section->vma);
      else
	h->esym.asym.value = 0;
    }

  if (! bfd_ecoff_debug_one_external (einfo->abfd, einfo->debug, einfo->swap,
				      h->root.root.root.string,
				      &h->esym))
    {
      einfo->failed = TRUE;
      return FALSE;
    }

  return TRUE;
}

/* Search for and possibly create a got entry.  */

static struct alpha_elf_got_entry *
get_got_entry (bfd *abfd, struct alpha_elf_link_hash_entry *h,
	       unsigned long r_type, unsigned long r_symndx,
	       bfd_vma r_addend)
{
  struct alpha_elf_got_entry *gotent;
  struct alpha_elf_got_entry **slot;

  if (h)
    slot = &h->got_entries;
  else
    {
      /* This is a local .got entry -- record for merge.  */

      struct alpha_elf_got_entry **local_got_entries;

      local_got_entries = alpha_elf_tdata(abfd)->local_got_entries;
      if (!local_got_entries)
	{
	  bfd_size_type size;
	  Elf_Internal_Shdr *symtab_hdr;

	  symtab_hdr = &elf_tdata(abfd)->symtab_hdr;
	  size = symtab_hdr->sh_info;
	  size *= sizeof (struct alpha_elf_got_entry *);

	  local_got_entries
	    = (struct alpha_elf_got_entry **) bfd_zalloc (abfd, size);
	  if (!local_got_entries)
	    return NULL;

	  alpha_elf_tdata (abfd)->local_got_entries = local_got_entries;
	}

      slot = &local_got_entries[r_symndx];
    }

  for (gotent = *slot; gotent ; gotent = gotent->next)
    if (gotent->gotobj == abfd
	&& gotent->reloc_type == r_type
	&& gotent->addend == r_addend)
      break;

  if (!gotent)
    {
      int entry_size;
      bfd_size_type amt;

      amt = sizeof (struct alpha_elf_got_entry);
      gotent = (struct alpha_elf_got_entry *) bfd_alloc (abfd, amt);
      if (!gotent)
	return NULL;

      gotent->gotobj = abfd;
      gotent->addend = r_addend;
      gotent->got_offset = -1;
      gotent->plt_offset = -1;
      gotent->use_count = 1;
      gotent->reloc_type = r_type;
      gotent->reloc_done = 0;
      gotent->reloc_xlated = 0;

      gotent->next = *slot;
      *slot = gotent;

      entry_size = alpha_got_entry_size (r_type);
      alpha_elf_tdata (abfd)->total_got_size += entry_size;
      if (!h)
	alpha_elf_tdata(abfd)->local_got_size += entry_size;
    }
  else
    gotent->use_count += 1;

  return gotent;
}

static bfd_boolean
elf64_alpha_want_plt (struct alpha_elf_link_hash_entry *ah)
{
  return ((ah->root.type == STT_FUNC
	  || ah->root.root.type == bfd_link_hash_undefweak
	  || ah->root.root.type == bfd_link_hash_undefined)
	  && (ah->flags & ALPHA_ELF_LINK_HASH_LU_PLT) != 0
	  && (ah->flags & ~ALPHA_ELF_LINK_HASH_LU_PLT) == 0);
}

/* Handle dynamic relocations when doing an Alpha ELF link.  */

static bfd_boolean
elf64_alpha_check_relocs (bfd *abfd, struct bfd_link_info *info,
			  asection *sec, const Elf_Internal_Rela *relocs)
{
  bfd *dynobj;
  asection *sreloc;
  const char *rel_sec_name;
  Elf_Internal_Shdr *symtab_hdr;
  struct alpha_elf_link_hash_entry **sym_hashes;
  const Elf_Internal_Rela *rel, *relend;
  bfd_size_type amt;

  if (info->relocatable)
    return TRUE;

  /* Don't do anything special with non-loaded, non-alloced sections.
     In particular, any relocs in such sections should not affect GOT
     and PLT reference counting (ie. we don't allow them to create GOT
     or PLT entries), there's no possibility or desire to optimize TLS
     relocs, and there's not much point in propagating relocs to shared
     libs that the dynamic linker won't relocate.  */
  if ((sec->flags & SEC_ALLOC) == 0)
    return TRUE;

  dynobj = elf_hash_table(info)->dynobj;
  if (dynobj == NULL)
    elf_hash_table(info)->dynobj = dynobj = abfd;

  sreloc = NULL;
  rel_sec_name = NULL;
  symtab_hdr = &elf_tdata(abfd)->symtab_hdr;
  sym_hashes = alpha_elf_sym_hashes(abfd);

  relend = relocs + sec->reloc_count;
  for (rel = relocs; rel < relend; ++rel)
    {
      enum {
	NEED_GOT = 1,
	NEED_GOT_ENTRY = 2,
	NEED_DYNREL = 4
      };

      unsigned long r_symndx, r_type;
      struct alpha_elf_link_hash_entry *h;
      unsigned int gotent_flags;
      bfd_boolean maybe_dynamic;
      unsigned int need;
      bfd_vma addend;

      r_symndx = ELF64_R_SYM (rel->r_info);
      if (r_symndx < symtab_hdr->sh_info)
	h = NULL;
      else
	{
	  h = sym_hashes[r_symndx - symtab_hdr->sh_info];

	  while (h->root.root.type == bfd_link_hash_indirect
		 || h->root.root.type == bfd_link_hash_warning)
	    h = (struct alpha_elf_link_hash_entry *)h->root.root.u.i.link;

	  h->root.ref_regular = 1;
	}

      /* We can only get preliminary data on whether a symbol is
         locally or externally defined, as not all of the input files
         have yet been processed.  Do something with what we know, as
         this may help reduce memory usage and processing time later.  */
      maybe_dynamic = FALSE;
      if (h && ((info->shared
		 && (!info->symbolic
		     || info->unresolved_syms_in_shared_libs == RM_IGNORE))
		|| !h->root.def_regular
		|| h->root.root.type == bfd_link_hash_defweak))
        maybe_dynamic = TRUE;

      need = 0;
      gotent_flags = 0;
      r_type = ELF64_R_TYPE (rel->r_info);
      addend = rel->r_addend;

      switch (r_type)
	{
	case R_ALPHA_LITERAL:
	  need = NEED_GOT | NEED_GOT_ENTRY;

	  /* Remember how this literal is used from its LITUSEs.
	     This will be important when it comes to decide if we can
	     create a .plt entry for a function symbol.  */
	  while (++rel < relend && ELF64_R_TYPE (rel->r_info) == R_ALPHA_LITUSE)
	    if (rel->r_addend >= 1 && rel->r_addend <= 6)
	      gotent_flags |= 1 << rel->r_addend;
	  --rel;

	  /* No LITUSEs -- presumably the address is used somehow.  */
	  if (gotent_flags == 0)
	    gotent_flags = ALPHA_ELF_LINK_HASH_LU_ADDR;
	  break;

	case R_ALPHA_GPDISP:
	case R_ALPHA_GPREL16:
	case R_ALPHA_GPREL32:
	case R_ALPHA_GPRELHIGH:
	case R_ALPHA_GPRELLOW:
	case R_ALPHA_BRSGP:
	  need = NEED_GOT;
	  break;

	case R_ALPHA_REFLONG:
	case R_ALPHA_REFQUAD:
	  if (info->shared || maybe_dynamic)
	    need = NEED_DYNREL;
	  break;

	case R_ALPHA_TLSLDM:
	  /* The symbol for a TLSLDM reloc is ignored.  Collapse the
	     reloc to the 0 symbol so that they all match.  */
	  r_symndx = 0;
	  h = 0;
	  maybe_dynamic = FALSE;
	  /* FALLTHRU */

	case R_ALPHA_TLSGD:
	case R_ALPHA_GOTDTPREL:
	  need = NEED_GOT | NEED_GOT_ENTRY;
	  break;

	case R_ALPHA_GOTTPREL:
	  need = NEED_GOT | NEED_GOT_ENTRY;
	  gotent_flags = ALPHA_ELF_LINK_HASH_TLS_IE;
	  if (info->shared)
	    info->flags |= DF_STATIC_TLS;
	  break;

	case R_ALPHA_TPREL64:
	  if (info->shared || maybe_dynamic)
	    need = NEED_DYNREL;
	  if (info->shared)
	    info->flags |= DF_STATIC_TLS;
	  break;
	}

      if (need & NEED_GOT)
	{
	  if (alpha_elf_tdata(abfd)->gotobj == NULL)
	    {
	      if (!elf64_alpha_create_got_section (abfd, info))
		return FALSE;
	    }
	}

      if (need & NEED_GOT_ENTRY)
	{
	  struct alpha_elf_got_entry *gotent;

	  gotent = get_got_entry (abfd, h, r_type, r_symndx, addend);
	  if (!gotent)
	    return FALSE;

	  if (gotent_flags)
	    {
	      gotent->flags |= gotent_flags;
	      if (h)
		{
		  gotent_flags |= h->flags;
		  h->flags = gotent_flags;

		  /* Make a guess as to whether a .plt entry is needed.  */
		  /* ??? It appears that we won't make it into
		     adjust_dynamic_symbol for symbols that remain
		     totally undefined.  Copying this check here means
		     we can create a plt entry for them too.  */
		  h->root.needs_plt
		    = (maybe_dynamic && elf64_alpha_want_plt (h));
		}
	    }
	}

      if (need & NEED_DYNREL)
	{
	  if (rel_sec_name == NULL)
	    {
	      rel_sec_name = (bfd_elf_string_from_elf_section
			      (abfd, elf_elfheader(abfd)->e_shstrndx,
			       elf_section_data(sec)->rel_hdr.sh_name));
	      if (rel_sec_name == NULL)
		return FALSE;

	      BFD_ASSERT (CONST_STRNEQ (rel_sec_name, ".rela")
			  && strcmp (bfd_get_section_name (abfd, sec),
				     rel_sec_name+5) == 0);
	    }

	  /* We need to create the section here now whether we eventually
	     use it or not so that it gets mapped to an output section by
	     the linker.  If not used, we'll kill it in
	     size_dynamic_sections.  */
	  if (sreloc == NULL)
	    {
	      sreloc = bfd_get_section_by_name (dynobj, rel_sec_name);
	      if (sreloc == NULL)
		{
		  flagword flags;

		  flags = (SEC_HAS_CONTENTS | SEC_IN_MEMORY
			   | SEC_LINKER_CREATED | SEC_READONLY);
		  if (sec->flags & SEC_ALLOC)
		    flags |= SEC_ALLOC | SEC_LOAD;
		  sreloc = bfd_make_section_with_flags (dynobj,
							rel_sec_name,
							flags);
		  if (sreloc == NULL
		      || !bfd_set_section_alignment (dynobj, sreloc, 3))
		    return FALSE;
		}
	    }

	  if (h)
	    {
	      /* Since we havn't seen all of the input symbols yet, we
		 don't know whether we'll actually need a dynamic relocation
		 entry for this reloc.  So make a record of it.  Once we
		 find out if this thing needs dynamic relocation we'll
		 expand the relocation sections by the appropriate amount.  */

	      struct alpha_elf_reloc_entry *rent;

	      for (rent = h->reloc_entries; rent; rent = rent->next)
		if (rent->rtype == r_type && rent->srel == sreloc)
		  break;

	      if (!rent)
		{
		  amt = sizeof (struct alpha_elf_reloc_entry);
		  rent = (struct alpha_elf_reloc_entry *) bfd_alloc (abfd, amt);
		  if (!rent)
		    return FALSE;

		  rent->srel = sreloc;
		  rent->rtype = r_type;
		  rent->count = 1;
		  rent->reltext = (sec->flags & SEC_READONLY) != 0;

		  rent->next = h->reloc_entries;
		  h->reloc_entries = rent;
		}
	      else
		rent->count++;
	    }
	  else if (info->shared)
	    {
	      /* If this is a shared library, and the section is to be
		 loaded into memory, we need a RELATIVE reloc.  */
	      sreloc->size += sizeof (Elf64_External_Rela);
	      if (sec->flags & SEC_READONLY)
		info->flags |= DF_TEXTREL;
	    }
	}
    }

  return TRUE;
}

/* Adjust a symbol defined by a dynamic object and referenced by a
   regular object.  The current definition is in some section of the
   dynamic object, but we're not including those sections.  We have to
   change the definition to something the rest of the link can
   understand.  */

static bfd_boolean
elf64_alpha_adjust_dynamic_symbol (struct bfd_link_info *info,
				   struct elf_link_hash_entry *h)
{
  bfd *dynobj;
  asection *s;
  struct alpha_elf_link_hash_entry *ah;

  dynobj = elf_hash_table(info)->dynobj;
  ah = (struct alpha_elf_link_hash_entry *)h;

  /* Now that we've seen all of the input symbols, finalize our decision
     about whether this symbol should get a .plt entry.  Irritatingly, it
     is common for folk to leave undefined symbols in shared libraries,
     and they still expect lazy binding; accept undefined symbols in lieu
     of STT_FUNC.  */
  if (alpha_elf_dynamic_symbol_p (h, info) && elf64_alpha_want_plt (ah))
    {
      h->needs_plt = TRUE;

      s = bfd_get_section_by_name(dynobj, ".plt");
      if (!s && !elf64_alpha_create_dynamic_sections (dynobj, info))
	return FALSE;

      /* We need one plt entry per got subsection.  Delay allocation of
	 the actual plt entries until size_plt_section, called from
	 size_dynamic_sections or during relaxation.  */

      return TRUE;
    }
  else
    h->needs_plt = FALSE;

  /* If this is a weak symbol, and there is a real definition, the
     processor independent code will have arranged for us to see the
     real definition first, and we can just use the same value.  */
  if (h->u.weakdef != NULL)
    {
      BFD_ASSERT (h->u.weakdef->root.type == bfd_link_hash_defined
		  || h->u.weakdef->root.type == bfd_link_hash_defweak);
      h->root.u.def.section = h->u.weakdef->root.u.def.section;
      h->root.u.def.value = h->u.weakdef->root.u.def.value;
      return TRUE;
    }

  /* This is a reference to a symbol defined by a dynamic object which
     is not a function.  The Alpha, since it uses .got entries for all
     symbols even in regular objects, does not need the hackery of a
     .dynbss section and COPY dynamic relocations.  */

  return TRUE;
}

/* Record STO_ALPHA_NOPV and STO_ALPHA_STD_GPLOAD.  */

static void
elf64_alpha_merge_symbol_attribute (struct elf_link_hash_entry *h,
				    const Elf_Internal_Sym *isym,
				    bfd_boolean definition,
				    bfd_boolean dynamic)
{
  if (!dynamic && definition)
    h->other = ((h->other & ELF_ST_VISIBILITY (-1))
		| (isym->st_other & ~ELF_ST_VISIBILITY (-1)));
}

/* Symbol versioning can create new symbols, and make our old symbols
   indirect to the new ones.  Consolidate the got and reloc information
   in these situations.  */

static bfd_boolean
elf64_alpha_merge_ind_symbols (struct alpha_elf_link_hash_entry *hi,
			       PTR dummy ATTRIBUTE_UNUSED)
{
  struct alpha_elf_link_hash_entry *hs;

  if (hi->root.root.type != bfd_link_hash_indirect)
    return TRUE;
  hs = hi;
  do {
    hs = (struct alpha_elf_link_hash_entry *)hs->root.root.u.i.link;
  } while (hs->root.root.type == bfd_link_hash_indirect);

  /* Merge the flags.  Whee.  */

  hs->flags |= hi->flags;

  /* Merge the .got entries.  Cannibalize the old symbol's list in
     doing so, since we don't need it anymore.  */

  if (hs->got_entries == NULL)
    hs->got_entries = hi->got_entries;
  else
    {
      struct alpha_elf_got_entry *gi, *gs, *gin, *gsh;

      gsh = hs->got_entries;
      for (gi = hi->got_entries; gi ; gi = gin)
	{
	  gin = gi->next;
	  for (gs = gsh; gs ; gs = gs->next)
	    if (gi->gotobj == gs->gotobj
		&& gi->reloc_type == gs->reloc_type
		&& gi->addend == gs->addend)
	      {
		gi->use_count += gs->use_count;
	        goto got_found;
	      }
	  gi->next = hs->got_entries;
	  hs->got_entries = gi;
	got_found:;
	}
    }
  hi->got_entries = NULL;

  /* And similar for the reloc entries.  */

  if (hs->reloc_entries == NULL)
    hs->reloc_entries = hi->reloc_entries;
  else
    {
      struct alpha_elf_reloc_entry *ri, *rs, *rin, *rsh;

      rsh = hs->reloc_entries;
      for (ri = hi->reloc_entries; ri ; ri = rin)
	{
	  rin = ri->next;
	  for (rs = rsh; rs ; rs = rs->next)
	    if (ri->rtype == rs->rtype && ri->srel == rs->srel)
	      {
		rs->count += ri->count;
		goto found_reloc;
	      }
	  ri->next = hs->reloc_entries;
	  hs->reloc_entries = ri;
	found_reloc:;
	}
    }
  hi->reloc_entries = NULL;

  return TRUE;
}

/* Is it possible to merge two object file's .got tables?  */

static bfd_boolean
elf64_alpha_can_merge_gots (bfd *a, bfd *b)
{
  int total = alpha_elf_tdata (a)->total_got_size;
  bfd *bsub;

  /* Trivial quick fallout test.  */
  if (total + alpha_elf_tdata (b)->total_got_size <= MAX_GOT_SIZE)
    return TRUE;

  /* By their nature, local .got entries cannot be merged.  */
  if ((total += alpha_elf_tdata (b)->local_got_size) > MAX_GOT_SIZE)
    return FALSE;

  /* Failing the common trivial comparison, we must effectively
     perform the merge.  Not actually performing the merge means that
     we don't have to store undo information in case we fail.  */
  for (bsub = b; bsub ; bsub = alpha_elf_tdata (bsub)->in_got_link_next)
    {
      struct alpha_elf_link_hash_entry **hashes = alpha_elf_sym_hashes (bsub);
      Elf_Internal_Shdr *symtab_hdr = &elf_tdata (bsub)->symtab_hdr;
      int i, n;

      n = NUM_SHDR_ENTRIES (symtab_hdr) - symtab_hdr->sh_info;
      for (i = 0; i < n; ++i)
	{
	  struct alpha_elf_got_entry *ae, *be;
	  struct alpha_elf_link_hash_entry *h;

	  h = hashes[i];
	  while (h->root.root.type == bfd_link_hash_indirect
	         || h->root.root.type == bfd_link_hash_warning)
	    h = (struct alpha_elf_link_hash_entry *)h->root.root.u.i.link;

	  for (be = h->got_entries; be ; be = be->next)
	    {
	      if (be->use_count == 0)
	        continue;
	      if (be->gotobj != b)
	        continue;

	      for (ae = h->got_entries; ae ; ae = ae->next)
	        if (ae->gotobj == a
		    && ae->reloc_type == be->reloc_type
		    && ae->addend == be->addend)
		  goto global_found;

	      total += alpha_got_entry_size (be->reloc_type);
	      if (total > MAX_GOT_SIZE)
	        return FALSE;
	    global_found:;
	    }
	}
    }

  return TRUE;
}

/* Actually merge two .got tables.  */

static void
elf64_alpha_merge_gots (bfd *a, bfd *b)
{
  int total = alpha_elf_tdata (a)->total_got_size;
  bfd *bsub;

  /* Remember local expansion.  */
  {
    int e = alpha_elf_tdata (b)->local_got_size;
    total += e;
    alpha_elf_tdata (a)->local_got_size += e;
  }

  for (bsub = b; bsub ; bsub = alpha_elf_tdata (bsub)->in_got_link_next)
    {
      struct alpha_elf_got_entry **local_got_entries;
      struct alpha_elf_link_hash_entry **hashes;
      Elf_Internal_Shdr *symtab_hdr;
      int i, n;

      /* Let the local .got entries know they are part of a new subsegment.  */
      local_got_entries = alpha_elf_tdata (bsub)->local_got_entries;
      if (local_got_entries)
        {
	  n = elf_tdata (bsub)->symtab_hdr.sh_info;
	  for (i = 0; i < n; ++i)
	    {
	      struct alpha_elf_got_entry *ent;
	      for (ent = local_got_entries[i]; ent; ent = ent->next)
	        ent->gotobj = a;
	    }
        }

      /* Merge the global .got entries.  */
      hashes = alpha_elf_sym_hashes (bsub);
      symtab_hdr = &elf_tdata (bsub)->symtab_hdr;

      n = NUM_SHDR_ENTRIES (symtab_hdr) - symtab_hdr->sh_info;
      for (i = 0; i < n; ++i)
        {
	  struct alpha_elf_got_entry *ae, *be, **pbe, **start;
	  struct alpha_elf_link_hash_entry *h;

	  h = hashes[i];
	  while (h->root.root.type == bfd_link_hash_indirect
	         || h->root.root.type == bfd_link_hash_warning)
	    h = (struct alpha_elf_link_hash_entry *)h->root.root.u.i.link;

	  pbe = start = &h->got_entries;
	  while ((be = *pbe) != NULL)
	    {
	      if (be->use_count == 0)
	        {
		  *pbe = be->next;
		  memset (be, 0xa5, sizeof (*be));
		  goto kill;
	        }
	      if (be->gotobj != b)
	        goto next;

	      for (ae = *start; ae ; ae = ae->next)
	        if (ae->gotobj == a
		    && ae->reloc_type == be->reloc_type
		    && ae->addend == be->addend)
		  {
		    ae->flags |= be->flags;
		    ae->use_count += be->use_count;
		    *pbe = be->next;
		    memset (be, 0xa5, sizeof (*be));
		    goto kill;
		  }
	      be->gotobj = a;
	      total += alpha_got_entry_size (be->reloc_type);

	    next:;
	      pbe = &be->next;
	    kill:;
	    }
        }

      alpha_elf_tdata (bsub)->gotobj = a;
    }
  alpha_elf_tdata (a)->total_got_size = total;

  /* Merge the two in_got chains.  */
  {
    bfd *next;

    bsub = a;
    while ((next = alpha_elf_tdata (bsub)->in_got_link_next) != NULL)
      bsub = next;

    alpha_elf_tdata (bsub)->in_got_link_next = b;
  }
}

/* Calculate the offsets for the got entries.  */

static bfd_boolean
elf64_alpha_calc_got_offsets_for_symbol (struct alpha_elf_link_hash_entry *h,
					 PTR arg ATTRIBUTE_UNUSED)
{
  struct alpha_elf_got_entry *gotent;

  if (h->root.root.type == bfd_link_hash_warning)
    h = (struct alpha_elf_link_hash_entry *) h->root.root.u.i.link;

  for (gotent = h->got_entries; gotent; gotent = gotent->next)
    if (gotent->use_count > 0)
      {
	struct alpha_elf_obj_tdata *td;
	bfd_size_type *plge;

	td = alpha_elf_tdata (gotent->gotobj);
	plge = &td->got->size;
	gotent->got_offset = *plge;
	*plge += alpha_got_entry_size (gotent->reloc_type);
      }

  return TRUE;
}

static void
elf64_alpha_calc_got_offsets (struct bfd_link_info *info)
{
  bfd *i, *got_list = alpha_elf_hash_table(info)->got_list;

  /* First, zero out the .got sizes, as we may be recalculating the
     .got after optimizing it.  */
  for (i = got_list; i ; i = alpha_elf_tdata(i)->got_link_next)
    alpha_elf_tdata(i)->got->size = 0;

  /* Next, fill in the offsets for all the global entries.  */
  alpha_elf_link_hash_traverse (alpha_elf_hash_table (info),
				elf64_alpha_calc_got_offsets_for_symbol,
				NULL);

  /* Finally, fill in the offsets for the local entries.  */
  for (i = got_list; i ; i = alpha_elf_tdata(i)->got_link_next)
    {
      bfd_size_type got_offset = alpha_elf_tdata(i)->got->size;
      bfd *j;

      for (j = i; j ; j = alpha_elf_tdata(j)->in_got_link_next)
	{
	  struct alpha_elf_got_entry **local_got_entries, *gotent;
	  int k, n;

	  local_got_entries = alpha_elf_tdata(j)->local_got_entries;
	  if (!local_got_entries)
	    continue;

	  for (k = 0, n = elf_tdata(j)->symtab_hdr.sh_info; k < n; ++k)
	    for (gotent = local_got_entries[k]; gotent; gotent = gotent->next)
	      if (gotent->use_count > 0)
	        {
		  gotent->got_offset = got_offset;
		  got_offset += alpha_got_entry_size (gotent->reloc_type);
	        }
	}

      alpha_elf_tdata(i)->got->size = got_offset;
    }
}

/* Constructs the gots.  */

static bfd_boolean
elf64_alpha_size_got_sections (struct bfd_link_info *info)
{
  bfd *i, *got_list, *cur_got_obj = NULL;

  got_list = alpha_elf_hash_table (info)->got_list;

  /* On the first time through, pretend we have an existing got list
     consisting of all of the input files.  */
  if (got_list == NULL)
    {
      for (i = info->input_bfds; i ; i = i->link_next)
	{
	  bfd *this_got = alpha_elf_tdata (i)->gotobj;
	  if (this_got == NULL)
	    continue;

	  /* We are assuming no merging has yet occurred.  */
	  BFD_ASSERT (this_got == i);

          if (alpha_elf_tdata (this_got)->total_got_size > MAX_GOT_SIZE)
	    {
	      /* Yikes! A single object file has too many entries.  */
	      (*_bfd_error_handler)
	        (_("%B: .got subsegment exceeds 64K (size %d)"),
	         i, alpha_elf_tdata (this_got)->total_got_size);
	      return FALSE;
	    }

	  if (got_list == NULL)
	    got_list = this_got;
	  else
	    alpha_elf_tdata(cur_got_obj)->got_link_next = this_got;
	  cur_got_obj = this_got;
	}

      /* Strange degenerate case of no got references.  */
      if (got_list == NULL)
	return TRUE;

      alpha_elf_hash_table (info)->got_list = got_list;
    }

  cur_got_obj = got_list;
  i = alpha_elf_tdata(cur_got_obj)->got_link_next;
  while (i != NULL)
    {
      if (elf64_alpha_can_merge_gots (cur_got_obj, i))
	{
	  elf64_alpha_merge_gots (cur_got_obj, i);

	  alpha_elf_tdata(i)->got->size = 0;
	  i = alpha_elf_tdata(i)->got_link_next;
	  alpha_elf_tdata(cur_got_obj)->got_link_next = i;
	}
      else
	{
	  cur_got_obj = i;
	  i = alpha_elf_tdata(i)->got_link_next;
	}
    }

  /* Once the gots have been merged, fill in the got offsets for
     everything therein.  */
  elf64_alpha_calc_got_offsets (info);

  return TRUE;
}

static bfd_boolean
elf64_alpha_size_plt_section_1 (struct alpha_elf_link_hash_entry *h, PTR data)
{
  asection *splt = (asection *) data;
  struct alpha_elf_got_entry *gotent;
  bfd_boolean saw_one = FALSE;

  /* If we didn't need an entry before, we still don't.  */
  if (!h->root.needs_plt)
    return TRUE;

  /* For each LITERAL got entry still in use, allocate a plt entry.  */
  for (gotent = h->got_entries; gotent ; gotent = gotent->next)
    if (gotent->reloc_type == R_ALPHA_LITERAL
	&& gotent->use_count > 0)
      {
	if (splt->size == 0)
	  splt->size = PLT_HEADER_SIZE;
	gotent->plt_offset = splt->size;
	splt->size += PLT_ENTRY_SIZE;
	saw_one = TRUE;
      }

  /* If there weren't any, there's no longer a need for the PLT entry.  */
  if (!saw_one)
    h->root.needs_plt = FALSE;

  return TRUE;
}

/* Called from relax_section to rebuild the PLT in light of potential changes
   in the function's status.  */

static void
elf64_alpha_size_plt_section (struct bfd_link_info *info)
{
  asection *splt, *spltrel, *sgotplt;
  unsigned long entries;
  bfd *dynobj;

  dynobj = elf_hash_table(info)->dynobj;
  splt = bfd_get_section_by_name (dynobj, ".plt");
  if (splt == NULL)
    return;

  splt->size = 0;

  alpha_elf_link_hash_traverse (alpha_elf_hash_table (info),
				elf64_alpha_size_plt_section_1, splt);

  /* Every plt entry requires a JMP_SLOT relocation.  */
  spltrel = bfd_get_section_by_name (dynobj, ".rela.plt");
  entries = 0;
  if (splt->size)
    {
      if (elf64_alpha_use_secureplt)
	entries = (splt->size - NEW_PLT_HEADER_SIZE) / NEW_PLT_ENTRY_SIZE;
      else
	entries = (splt->size - OLD_PLT_HEADER_SIZE) / OLD_PLT_ENTRY_SIZE;
    }
  spltrel->size = entries * sizeof (Elf64_External_Rela);

  /* When using the secureplt, we need two words somewhere in the data
     segment for the dynamic linker to tell us where to go.  This is the
     entire contents of the .got.plt section.  */
  if (elf64_alpha_use_secureplt)
    {
      sgotplt = bfd_get_section_by_name (dynobj, ".got.plt");
      sgotplt->size = entries ? 16 : 0;
    }
}

static bfd_boolean
elf64_alpha_always_size_sections (bfd *output_bfd ATTRIBUTE_UNUSED,
				  struct bfd_link_info *info)
{
  bfd *i;

  if (info->relocatable)
    return TRUE;

  /* First, take care of the indirect symbols created by versioning.  */
  alpha_elf_link_hash_traverse (alpha_elf_hash_table (info),
				elf64_alpha_merge_ind_symbols,
				NULL);

  if (!elf64_alpha_size_got_sections (info))
    return FALSE;

  /* Allocate space for all of the .got subsections.  */
  i = alpha_elf_hash_table (info)->got_list;
  for ( ; i ; i = alpha_elf_tdata(i)->got_link_next)
    {
      asection *s = alpha_elf_tdata(i)->got;
      if (s->size > 0)
	{
	  s->contents = (bfd_byte *) bfd_zalloc (i, s->size);
	  if (s->contents == NULL)
	    return FALSE;
	}
    }

  return TRUE;
}

/* The number of dynamic relocations required by a static relocation.  */

static int
alpha_dynamic_entries_for_reloc (int r_type, int dynamic, int shared)
{
  switch (r_type)
    {
    /* May appear in GOT entries.  */
    case R_ALPHA_TLSGD:
      return (dynamic ? 2 : shared ? 1 : 0);
    case R_ALPHA_TLSLDM:
      return shared;
    case R_ALPHA_LITERAL:
    case R_ALPHA_GOTTPREL:
      return dynamic || shared;
    case R_ALPHA_GOTDTPREL:
      return dynamic;

    /* May appear in data sections.  */
    case R_ALPHA_REFLONG:
    case R_ALPHA_REFQUAD:
    case R_ALPHA_TPREL64:
      return dynamic || shared;

    /* Everything else is illegal.  We'll issue an error during
       relocate_section.  */
    default:
      return 0;
    }
}

/* Work out the sizes of the dynamic relocation entries.  */

static bfd_boolean
elf64_alpha_calc_dynrel_sizes (struct alpha_elf_link_hash_entry *h,
			       struct bfd_link_info *info)
{
  bfd_boolean dynamic;
  struct alpha_elf_reloc_entry *relent;
  unsigned long entries;

  if (h->root.root.type == bfd_link_hash_warning)
    h = (struct alpha_elf_link_hash_entry *) h->root.root.u.i.link;

  /* If the symbol was defined as a common symbol in a regular object
     file, and there was no definition in any dynamic object, then the
     linker will have allocated space for the symbol in a common
     section but the ELF_LINK_HASH_DEF_REGULAR flag will not have been
     set.  This is done for dynamic symbols in
     elf_adjust_dynamic_symbol but this is not done for non-dynamic
     symbols, somehow.  */
  if (!h->root.def_regular
      && h->root.ref_regular
      && !h->root.def_dynamic
      && (h->root.root.type == bfd_link_hash_defined
	  || h->root.root.type == bfd_link_hash_defweak)
      && !(h->root.root.u.def.section->owner->flags & DYNAMIC))
    h->root.def_regular = 1;

  /* If the symbol is dynamic, we'll need all the relocations in their
     natural form.  If this is a shared object, and it has been forced
     local, we'll need the same number of RELATIVE relocations.  */
  dynamic = alpha_elf_dynamic_symbol_p (&h->root, info);

  /* If the symbol is a hidden undefined weak, then we never have any
     relocations.  Avoid the loop which may want to add RELATIVE relocs
     based on info->shared.  */
  if (h->root.root.type == bfd_link_hash_undefweak && !dynamic)
    return TRUE;

  for (relent = h->reloc_entries; relent; relent = relent->next)
    {
      entries = alpha_dynamic_entries_for_reloc (relent->rtype, dynamic,
						 info->shared);
      if (entries)
	{
	  relent->srel->size +=
	    entries * sizeof (Elf64_External_Rela) * relent->count;
	  if (relent->reltext)
	    info->flags |= DT_TEXTREL;
	}
    }

  return TRUE;
}

/* Subroutine of elf64_alpha_size_rela_got_section for doing the
   global symbols.  */

static bfd_boolean
elf64_alpha_size_rela_got_1 (struct alpha_elf_link_hash_entry *h,
			     struct bfd_link_info *info)
{
  bfd_boolean dynamic;
  struct alpha_elf_got_entry *gotent;
  unsigned long entries;

  if (h->root.root.type == bfd_link_hash_warning)
    h = (struct alpha_elf_link_hash_entry *) h->root.root.u.i.link;

  /* If we're using a plt for this symbol, then all of its relocations
     for its got entries go into .rela.plt.  */
  if (h->root.needs_plt)
    return TRUE;

  /* If the symbol is dynamic, we'll need all the relocations in their
     natural form.  If this is a shared object, and it has been forced
     local, we'll need the same number of RELATIVE relocations.  */
  dynamic = alpha_elf_dynamic_symbol_p (&h->root, info);

  /* If the symbol is a hidden undefined weak, then we never have any
     relocations.  Avoid the loop which may want to add RELATIVE relocs
     based on info->shared.  */
  if (h->root.root.type == bfd_link_hash_undefweak && !dynamic)
    return TRUE;

  entries = 0;
  for (gotent = h->got_entries; gotent ; gotent = gotent->next)
    if (gotent->use_count > 0)
      entries += alpha_dynamic_entries_for_reloc (gotent->reloc_type,
						  dynamic, info->shared);

  if (entries > 0)
    {
      bfd *dynobj = elf_hash_table(info)->dynobj;
      asection *srel = bfd_get_section_by_name (dynobj, ".rela.got");
      BFD_ASSERT (srel != NULL);
      srel->size += sizeof (Elf64_External_Rela) * entries;
    }

  return TRUE;
}

/* Set the sizes of the dynamic relocation sections.  */

static void
elf64_alpha_size_rela_got_section (struct bfd_link_info *info)
{
  unsigned long entries;
  bfd *i, *dynobj;
  asection *srel;

  /* Shared libraries often require RELATIVE relocs, and some relocs
     require attention for the main application as well.  */

  entries = 0;
  for (i = alpha_elf_hash_table(info)->got_list;
       i ; i = alpha_elf_tdata(i)->got_link_next)
    {
      bfd *j;

      for (j = i; j ; j = alpha_elf_tdata(j)->in_got_link_next)
	{
	  struct alpha_elf_got_entry **local_got_entries, *gotent;
	  int k, n;

	  local_got_entries = alpha_elf_tdata(j)->local_got_entries;
	  if (!local_got_entries)
	    continue;

	  for (k = 0, n = elf_tdata(j)->symtab_hdr.sh_info; k < n; ++k)
	    for (gotent = local_got_entries[k];
		 gotent ; gotent = gotent->next)
	      if (gotent->use_count > 0)
		entries += (alpha_dynamic_entries_for_reloc
			    (gotent->reloc_type, 0, info->shared));
	}
    }

  dynobj = elf_hash_table(info)->dynobj;
  srel = bfd_get_section_by_name (dynobj, ".rela.got");
  if (!srel)
    {
      BFD_ASSERT (entries == 0);
      return;
    }
  srel->size = sizeof (Elf64_External_Rela) * entries;

  /* Now do the non-local symbols.  */
  alpha_elf_link_hash_traverse (alpha_elf_hash_table (info),
				elf64_alpha_size_rela_got_1, info);
}

/* Set the sizes of the dynamic sections.  */

static bfd_boolean
elf64_alpha_size_dynamic_sections (bfd *output_bfd ATTRIBUTE_UNUSED,
				   struct bfd_link_info *info)
{
  bfd *dynobj;
  asection *s;
  bfd_boolean relplt;

  dynobj = elf_hash_table(info)->dynobj;
  BFD_ASSERT(dynobj != NULL);

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      /* Set the contents of the .interp section to the interpreter.  */
      if (info->executable)
	{
	  s = bfd_get_section_by_name (dynobj, ".interp");
	  BFD_ASSERT (s != NULL);
	  s->size = sizeof ELF_DYNAMIC_INTERPRETER;
	  s->contents = (unsigned char *) ELF_DYNAMIC_INTERPRETER;
	}

      /* Now that we've seen all of the input files, we can decide which
	 symbols need dynamic relocation entries and which don't.  We've
	 collected information in check_relocs that we can now apply to
	 size the dynamic relocation sections.  */
      alpha_elf_link_hash_traverse (alpha_elf_hash_table (info),
				    elf64_alpha_calc_dynrel_sizes, info);

      elf64_alpha_size_rela_got_section (info);
      elf64_alpha_size_plt_section (info);
    }
  /* else we're not dynamic and by definition we don't need such things.  */

  /* The check_relocs and adjust_dynamic_symbol entry points have
     determined the sizes of the various dynamic sections.  Allocate
     memory for them.  */
  relplt = FALSE;
  for (s = dynobj->sections; s != NULL; s = s->next)
    {
      const char *name;

      if (!(s->flags & SEC_LINKER_CREATED))
	continue;

      /* It's OK to base decisions on the section name, because none
	 of the dynobj section names depend upon the input files.  */
      name = bfd_get_section_name (dynobj, s);

      if (CONST_STRNEQ (name, ".rela"))
	{
	  if (s->size != 0)
	    {
	      if (strcmp (name, ".rela.plt") == 0)
		relplt = TRUE;

	      /* We use the reloc_count field as a counter if we need
		 to copy relocs into the output file.  */
	      s->reloc_count = 0;
	    }
	}
      else if (! CONST_STRNEQ (name, ".got")
	       && strcmp (name, ".plt") != 0
	       && strcmp (name, ".dynbss") != 0)
	{
	  /* It's not one of our dynamic sections, so don't allocate space.  */
	  continue;
	}

      if (s->size == 0)
	{
	  /* If we don't need this section, strip it from the output file.
	     This is to handle .rela.bss and .rela.plt.  We must create it
	     in create_dynamic_sections, because it must be created before
	     the linker maps input sections to output sections.  The
	     linker does that before adjust_dynamic_symbol is called, and
	     it is that function which decides whether anything needs to
	     go into these sections.  */
	  s->flags |= SEC_EXCLUDE;
	}
      else if ((s->flags & SEC_HAS_CONTENTS) != 0)
	{
	  /* Allocate memory for the section contents.  */
	  s->contents = (bfd_byte *) bfd_zalloc (dynobj, s->size);
	  if (s->contents == NULL)
	    return FALSE;
	}
    }

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      /* Add some entries to the .dynamic section.  We fill in the
	 values later, in elf64_alpha_finish_dynamic_sections, but we
	 must add the entries now so that we get the correct size for
	 the .dynamic section.  The DT_DEBUG entry is filled in by the
	 dynamic linker and used by the debugger.  */
#define add_dynamic_entry(TAG, VAL) \
  _bfd_elf_add_dynamic_entry (info, TAG, VAL)

      if (info->executable)
	{
	  if (!add_dynamic_entry (DT_DEBUG, 0))
	    return FALSE;
	}

      if (relplt)
	{
	  if (!add_dynamic_entry (DT_PLTGOT, 0)
	      || !add_dynamic_entry (DT_PLTRELSZ, 0)
	      || !add_dynamic_entry (DT_PLTREL, DT_RELA)
	      || !add_dynamic_entry (DT_JMPREL, 0))
	    return FALSE;

	  if (elf64_alpha_use_secureplt
	      && !add_dynamic_entry (DT_ALPHA_PLTRO, 1))
	    return FALSE;
	}

      if (!add_dynamic_entry (DT_RELA, 0)
	  || !add_dynamic_entry (DT_RELASZ, 0)
	  || !add_dynamic_entry (DT_RELAENT, sizeof (Elf64_External_Rela)))
	return FALSE;

      if (info->flags & DF_TEXTREL)
	{
	  if (!add_dynamic_entry (DT_TEXTREL, 0))
	    return FALSE;
	}
    }
#undef add_dynamic_entry

  return TRUE;
}

/* These functions do relaxation for Alpha ELF.

   Currently I'm only handling what I can do with existing compiler
   and assembler support, which means no instructions are removed,
   though some may be nopped.  At this time GCC does not emit enough
   information to do all of the relaxing that is possible.  It will
   take some not small amount of work for that to happen.

   There are a couple of interesting papers that I once read on this
   subject, that I cannot find references to at the moment, that
   related to Alpha in particular.  They are by David Wall, then of
   DEC WRL.  */

struct alpha_relax_info
{
  bfd *abfd;
  asection *sec;
  bfd_byte *contents;
  Elf_Internal_Shdr *symtab_hdr;
  Elf_Internal_Rela *relocs, *relend;
  struct bfd_link_info *link_info;
  bfd_vma gp;
  bfd *gotobj;
  asection *tsec;
  struct alpha_elf_link_hash_entry *h;
  struct alpha_elf_got_entry **first_gotent;
  struct alpha_elf_got_entry *gotent;
  bfd_boolean changed_contents;
  bfd_boolean changed_relocs;
  unsigned char other;
};

static Elf_Internal_Rela *
elf64_alpha_find_reloc_at_ofs (Elf_Internal_Rela *rel,
			       Elf_Internal_Rela *relend,
			       bfd_vma offset, int type)
{
  while (rel < relend)
    {
      if (rel->r_offset == offset
	  && ELF64_R_TYPE (rel->r_info) == (unsigned int) type)
	return rel;
      ++rel;
    }
  return NULL;
}

static bfd_boolean
elf64_alpha_relax_got_load (struct alpha_relax_info *info, bfd_vma symval,
			    Elf_Internal_Rela *irel, unsigned long r_type)
{
  unsigned int insn;
  bfd_signed_vma disp;

  /* Get the instruction.  */
  insn = bfd_get_32 (info->abfd, info->contents + irel->r_offset);

  if (insn >> 26 != OP_LDQ)
    {
      reloc_howto_type *howto = elf64_alpha_howto_table + r_type;
      ((*_bfd_error_handler)
       ("%B: %A+0x%lx: warning: %s relocation against unexpected insn",
	info->abfd, info->sec,
	(unsigned long) irel->r_offset, howto->name));
      return TRUE;
    }

  /* Can't relax dynamic symbols.  */
  if (alpha_elf_dynamic_symbol_p (&info->h->root, info->link_info))
    return TRUE;

  /* Can't use local-exec relocations in shared libraries.  */
  if (r_type == R_ALPHA_GOTTPREL && info->link_info->shared)
    return TRUE;

  if (r_type == R_ALPHA_LITERAL)
    {
      /* Look for nice constant addresses.  This includes the not-uncommon
	 special case of 0 for undefweak symbols.  */
      if ((info->h && info->h->root.root.type == bfd_link_hash_undefweak)
	  || (!info->link_info->shared
	      && (symval >= (bfd_vma)-0x8000 || symval < 0x8000)))
	{
	  disp = 0;
	  insn = (OP_LDA << 26) | (insn & (31 << 21)) | (31 << 16);
	  insn |= (symval & 0xffff);
	  r_type = R_ALPHA_NONE;
	}
      else
	{
	  disp = symval - info->gp;
	  insn = (OP_LDA << 26) | (insn & 0x03ff0000);
	  r_type = R_ALPHA_GPREL16;
	}
    }
  else
    {
      bfd_vma dtp_base, tp_base;

      BFD_ASSERT (elf_hash_table (info->link_info)->tls_sec != NULL);
      dtp_base = alpha_get_dtprel_base (info->link_info);
      tp_base = alpha_get_tprel_base (info->link_info);
      disp = symval - (r_type == R_ALPHA_GOTDTPREL ? dtp_base : tp_base);

      insn = (OP_LDA << 26) | (insn & (31 << 21)) | (31 << 16);

      switch (r_type)
	{
	case R_ALPHA_GOTDTPREL:
	  r_type = R_ALPHA_DTPREL16;
	  break;
	case R_ALPHA_GOTTPREL:
	  r_type = R_ALPHA_TPREL16;
	  break;
	default:
	  BFD_ASSERT (0);
	  return FALSE;
	}
    }

  if (disp < -0x8000 || disp >= 0x8000)
    return TRUE;

  bfd_put_32 (info->abfd, (bfd_vma) insn, info->contents + irel->r_offset);
  info->changed_contents = TRUE;

  /* Reduce the use count on this got entry by one, possibly
     eliminating it.  */
  if (--info->gotent->use_count == 0)
    {
      int sz = alpha_got_entry_size (r_type);
      alpha_elf_tdata (info->gotobj)->total_got_size -= sz;
      if (!info->h)
	alpha_elf_tdata (info->gotobj)->local_got_size -= sz;
    }

  /* Smash the existing GOT relocation for its 16-bit immediate pair.  */
  irel->r_info = ELF64_R_INFO (ELF64_R_SYM (irel->r_info), r_type);
  info->changed_relocs = TRUE;

  /* ??? Search forward through this basic block looking for insns
     that use the target register.  Stop after an insn modifying the
     register is seen, or after a branch or call.

     Any such memory load insn may be substituted by a load directly
     off the GP.  This allows the memory load insn to be issued before
     the calculated GP register would otherwise be ready.

     Any such jsr insn can be replaced by a bsr if it is in range.

     This would mean that we'd have to _add_ relocations, the pain of
     which gives one pause.  */

  return TRUE;
}

static bfd_vma
elf64_alpha_relax_opt_call (struct alpha_relax_info *info, bfd_vma symval)
{
  /* If the function has the same gp, and we can identify that the
     function does not use its function pointer, we can eliminate the
     address load.  */

  /* If the symbol is marked NOPV, we are being told the function never
     needs its procedure value.  */
  if ((info->other & STO_ALPHA_STD_GPLOAD) == STO_ALPHA_NOPV)
    return symval;

  /* If the symbol is marked STD_GP, we are being told the function does
     a normal ldgp in the first two words.  */
  else if ((info->other & STO_ALPHA_STD_GPLOAD) == STO_ALPHA_STD_GPLOAD)
    ;

  /* Otherwise, we may be able to identify a GP load in the first two
     words, which we can then skip.  */
  else
    {
      Elf_Internal_Rela *tsec_relocs, *tsec_relend, *tsec_free, *gpdisp;
      bfd_vma ofs;

      /* Load the relocations from the section that the target symbol is in.  */
      if (info->sec == info->tsec)
	{
	  tsec_relocs = info->relocs;
	  tsec_relend = info->relend;
	  tsec_free = NULL;
	}
      else
	{
	  tsec_relocs = (_bfd_elf_link_read_relocs
		         (info->abfd, info->tsec, (PTR) NULL,
			 (Elf_Internal_Rela *) NULL,
			 info->link_info->keep_memory));
	  if (tsec_relocs == NULL)
	    return 0;
	  tsec_relend = tsec_relocs + info->tsec->reloc_count;
	  tsec_free = (info->link_info->keep_memory ? NULL : tsec_relocs);
	}

      /* Recover the symbol's offset within the section.  */
      ofs = (symval - info->tsec->output_section->vma
	     - info->tsec->output_offset);

      /* Look for a GPDISP reloc.  */
      gpdisp = (elf64_alpha_find_reloc_at_ofs
		(tsec_relocs, tsec_relend, ofs, R_ALPHA_GPDISP));

      if (!gpdisp || gpdisp->r_addend != 4)
	{
	  if (tsec_free)
	    free (tsec_free);
	  return 0;
	}
      if (tsec_free)
        free (tsec_free);
    }

  /* We've now determined that we can skip an initial gp load.  Verify
     that the call and the target use the same gp.   */
  if (info->link_info->hash->creator != info->tsec->owner->xvec
      || info->gotobj != alpha_elf_tdata (info->tsec->owner)->gotobj)
    return 0;

  return symval + 8;
}

static bfd_boolean
elf64_alpha_relax_with_lituse (struct alpha_relax_info *info,
			       bfd_vma symval, Elf_Internal_Rela *irel)
{
  Elf_Internal_Rela *urel, *irelend = info->relend;
  int flags, count, i;
  bfd_signed_vma disp;
  bfd_boolean fits16;
  bfd_boolean fits32;
  bfd_boolean lit_reused = FALSE;
  bfd_boolean all_optimized = TRUE;
  unsigned int lit_insn;

  lit_insn = bfd_get_32 (info->abfd, info->contents + irel->r_offset);
  if (lit_insn >> 26 != OP_LDQ)
    {
      ((*_bfd_error_handler)
       ("%B: %A+0x%lx: warning: LITERAL relocation against unexpected insn",
	info->abfd, info->sec,
	(unsigned long) irel->r_offset));
      return TRUE;
    }

  /* Can't relax dynamic symbols.  */
  if (alpha_elf_dynamic_symbol_p (&info->h->root, info->link_info))
    return TRUE;

  /* Summarize how this particular LITERAL is used.  */
  for (urel = irel+1, flags = count = 0; urel < irelend; ++urel, ++count)
    {
      if (ELF64_R_TYPE (urel->r_info) != R_ALPHA_LITUSE)
	break;
      if (urel->r_addend <= 6)
	flags |= 1 << urel->r_addend;
    }

  /* A little preparation for the loop...  */
  disp = symval - info->gp;

  for (urel = irel+1, i = 0; i < count; ++i, ++urel)
    {
      unsigned int insn;
      int insn_disp;
      bfd_signed_vma xdisp;

      insn = bfd_get_32 (info->abfd, info->contents + urel->r_offset);

      switch (urel->r_addend)
	{
	case LITUSE_ALPHA_ADDR:
	default:
	  /* This type is really just a placeholder to note that all
	     uses cannot be optimized, but to still allow some.  */
	  all_optimized = FALSE;
	  break;

	case LITUSE_ALPHA_BASE:
	  /* We can always optimize 16-bit displacements.  */

	  /* Extract the displacement from the instruction, sign-extending
	     it if necessary, then test whether it is within 16 or 32 bits
	     displacement from GP.  */
	  insn_disp = ((insn & 0xffff) ^ 0x8000) - 0x8000;

	  xdisp = disp + insn_disp;
	  fits16 = (xdisp >= - (bfd_signed_vma) 0x8000 && xdisp < 0x8000);
	  fits32 = (xdisp >= - (bfd_signed_vma) 0x80000000
		    && xdisp < 0x7fff8000);

	  if (fits16)
	    {
	      /* Take the op code and dest from this insn, take the base
		 register from the literal insn.  Leave the offset alone.  */
	      insn = (insn & 0xffe0ffff) | (lit_insn & 0x001f0000);
	      urel->r_info = ELF64_R_INFO (ELF64_R_SYM (irel->r_info),
					   R_ALPHA_GPREL16);
	      urel->r_addend = irel->r_addend;
	      info->changed_relocs = TRUE;

	      bfd_put_32 (info->abfd, (bfd_vma) insn,
			  info->contents + urel->r_offset);
	      info->changed_contents = TRUE;
	    }

	  /* If all mem+byte, we can optimize 32-bit mem displacements.  */
	  else if (fits32 && !(flags & ~6))
	    {
	      /* FIXME: sanity check that lit insn Ra is mem insn Rb.  */

	      irel->r_info = ELF64_R_INFO (ELF64_R_SYM (irel->r_info),
					   R_ALPHA_GPRELHIGH);
	      lit_insn = (OP_LDAH << 26) | (lit_insn & 0x03ff0000);
	      bfd_put_32 (info->abfd, (bfd_vma) lit_insn,
			  info->contents + irel->r_offset);
	      lit_reused = TRUE;
	      info->changed_contents = TRUE;

	      urel->r_info = ELF64_R_INFO (ELF64_R_SYM (irel->r_info),
					   R_ALPHA_GPRELLOW);
	      urel->r_addend = irel->r_addend;
	      info->changed_relocs = TRUE;
	    }
	  else
	    all_optimized = FALSE;
	  break;

	case LITUSE_ALPHA_BYTOFF:
	  /* We can always optimize byte instructions.  */

	  /* FIXME: sanity check the insn for byte op.  Check that the
	     literal dest reg is indeed Rb in the byte insn.  */

	  insn &= ~ (unsigned) 0x001ff000;
	  insn |= ((symval & 7) << 13) | 0x1000;

	  urel->r_info = ELF64_R_INFO (0, R_ALPHA_NONE);
	  urel->r_addend = 0;
	  info->changed_relocs = TRUE;

	  bfd_put_32 (info->abfd, (bfd_vma) insn,
		      info->contents + urel->r_offset);
	  info->changed_contents = TRUE;
	  break;

	case LITUSE_ALPHA_JSR:
	case LITUSE_ALPHA_TLSGD:
	case LITUSE_ALPHA_TLSLDM:
	case LITUSE_ALPHA_JSRDIRECT:
	  {
	    bfd_vma optdest, org;
	    bfd_signed_vma odisp;

	    /* For undefined weak symbols, we're mostly interested in getting
	       rid of the got entry whenever possible, so optimize this to a
	       use of the zero register.  */
	    if (info->h && info->h->root.root.type == bfd_link_hash_undefweak)
	      {
		insn |= 31 << 16;
		bfd_put_32 (info->abfd, (bfd_vma) insn,
			    info->contents + urel->r_offset);

		info->changed_contents = TRUE;
		break;
	      }

	    /* If not zero, place to jump without needing pv.  */
	    optdest = elf64_alpha_relax_opt_call (info, symval);
	    org = (info->sec->output_section->vma
		   + info->sec->output_offset
		   + urel->r_offset + 4);
	    odisp = (optdest ? optdest : symval) - org;

	    if (odisp >= -0x400000 && odisp < 0x400000)
	      {
		Elf_Internal_Rela *xrel;

		/* Preserve branch prediction call stack when possible.  */
		if ((insn & INSN_JSR_MASK) == INSN_JSR)
		  insn = (OP_BSR << 26) | (insn & 0x03e00000);
		else
		  insn = (OP_BR << 26) | (insn & 0x03e00000);

		urel->r_info = ELF64_R_INFO (ELF64_R_SYM (irel->r_info),
					     R_ALPHA_BRADDR);
		urel->r_addend = irel->r_addend;

		if (optdest)
		  urel->r_addend += optdest - symval;
		else
		  all_optimized = FALSE;

		bfd_put_32 (info->abfd, (bfd_vma) insn,
			    info->contents + urel->r_offset);

		/* Kill any HINT reloc that might exist for this insn.  */
		xrel = (elf64_alpha_find_reloc_at_ofs
			(info->relocs, info->relend, urel->r_offset,
			 R_ALPHA_HINT));
		if (xrel)
		  xrel->r_info = ELF64_R_INFO (0, R_ALPHA_NONE);

		info->changed_contents = TRUE;
		info->changed_relocs = TRUE;
	      }
	    else
	      all_optimized = FALSE;

	    /* Even if the target is not in range for a direct branch,
	       if we share a GP, we can eliminate the gp reload.  */
	    if (optdest)
	      {
		Elf_Internal_Rela *gpdisp
		  = (elf64_alpha_find_reloc_at_ofs
		     (info->relocs, irelend, urel->r_offset + 4,
		      R_ALPHA_GPDISP));
		if (gpdisp)
		  {
		    bfd_byte *p_ldah = info->contents + gpdisp->r_offset;
		    bfd_byte *p_lda = p_ldah + gpdisp->r_addend;
		    unsigned int ldah = bfd_get_32 (info->abfd, p_ldah);
		    unsigned int lda = bfd_get_32 (info->abfd, p_lda);

		    /* Verify that the instruction is "ldah $29,0($26)".
		       Consider a function that ends in a noreturn call,
		       and that the next function begins with an ldgp,
		       and that by accident there is no padding between.
		       In that case the insn would use $27 as the base.  */
		    if (ldah == 0x27ba0000 && lda == 0x23bd0000)
		      {
			bfd_put_32 (info->abfd, (bfd_vma) INSN_UNOP, p_ldah);
			bfd_put_32 (info->abfd, (bfd_vma) INSN_UNOP, p_lda);

			gpdisp->r_info = ELF64_R_INFO (0, R_ALPHA_NONE);
			info->changed_contents = TRUE;
			info->changed_relocs = TRUE;
		      }
		  }
	      }
	  }
	  break;
	}
    }

  /* If all cases were optimized, we can reduce the use count on this
     got entry by one, possibly eliminating it.  */
  if (all_optimized)
    {
      if (--info->gotent->use_count == 0)
	{
	  int sz = alpha_got_entry_size (R_ALPHA_LITERAL);
	  alpha_elf_tdata (info->gotobj)->total_got_size -= sz;
	  if (!info->h)
	    alpha_elf_tdata (info->gotobj)->local_got_size -= sz;
	}

      /* If the literal instruction is no longer needed (it may have been
	 reused.  We can eliminate it.  */
      /* ??? For now, I don't want to deal with compacting the section,
	 so just nop it out.  */
      if (!lit_reused)
	{
	  irel->r_info = ELF64_R_INFO (0, R_ALPHA_NONE);
	  info->changed_relocs = TRUE;

	  bfd_put_32 (info->abfd, (bfd_vma) INSN_UNOP,
		      info->contents + irel->r_offset);
	  info->changed_contents = TRUE;
	}

      return TRUE;
    }
  else
    return elf64_alpha_relax_got_load (info, symval, irel, R_ALPHA_LITERAL);
}

static bfd_boolean
elf64_alpha_relax_tls_get_addr (struct alpha_relax_info *info, bfd_vma symval,
				Elf_Internal_Rela *irel, bfd_boolean is_gd)
{
  bfd_byte *pos[5];
  unsigned int insn;
  Elf_Internal_Rela *gpdisp, *hint;
  bfd_boolean dynamic, use_gottprel, pos1_unusable;
  unsigned long new_symndx;

  dynamic = alpha_elf_dynamic_symbol_p (&info->h->root, info->link_info);

  /* If a TLS symbol is accessed using IE at least once, there is no point
     to use dynamic model for it.  */
  if (is_gd && info->h && (info->h->flags & ALPHA_ELF_LINK_HASH_TLS_IE))
    ;

  /* If the symbol is local, and we've already committed to DF_STATIC_TLS,
     then we might as well relax to IE.  */
  else if (info->link_info->shared && !dynamic
	   && (info->link_info->flags & DF_STATIC_TLS))
    ;

  /* Otherwise we must be building an executable to do anything.  */
  else if (info->link_info->shared)
    return TRUE;

  /* The TLSGD/TLSLDM relocation must be followed by a LITERAL and
     the matching LITUSE_TLS relocations.  */
  if (irel + 2 >= info->relend)
    return TRUE;
  if (ELF64_R_TYPE (irel[1].r_info) != R_ALPHA_LITERAL
      || ELF64_R_TYPE (irel[2].r_info) != R_ALPHA_LITUSE
      || irel[2].r_addend != (is_gd ? LITUSE_ALPHA_TLSGD : LITUSE_ALPHA_TLSLDM))
    return TRUE;

  /* There must be a GPDISP relocation positioned immediately after the
     LITUSE relocation.  */
  gpdisp = elf64_alpha_find_reloc_at_ofs (info->relocs, info->relend,
					  irel[2].r_offset + 4, R_ALPHA_GPDISP);
  if (!gpdisp)
    return TRUE;

  pos[0] = info->contents + irel[0].r_offset;
  pos[1] = info->contents + irel[1].r_offset;
  pos[2] = info->contents + irel[2].r_offset;
  pos[3] = info->contents + gpdisp->r_offset;
  pos[4] = pos[3] + gpdisp->r_addend;
  pos1_unusable = FALSE;

  /* Generally, the positions are not allowed to be out of order, lest the
     modified insn sequence have different register lifetimes.  We can make
     an exception when pos 1 is adjacent to pos 0.  */
  if (pos[1] + 4 == pos[0])
    {
      bfd_byte *tmp = pos[0];
      pos[0] = pos[1];
      pos[1] = tmp;
    }
  else if (pos[1] < pos[0])
    pos1_unusable = TRUE;
  if (pos[1] >= pos[2] || pos[2] >= pos[3])
    return TRUE;

  /* Reduce the use count on the LITERAL relocation.  Do this before we
     smash the symndx when we adjust the relocations below.  */
  {
    struct alpha_elf_got_entry *lit_gotent;
    struct alpha_elf_link_hash_entry *lit_h;
    unsigned long indx;

    BFD_ASSERT (ELF64_R_SYM (irel[1].r_info) >= info->symtab_hdr->sh_info);
    indx = ELF64_R_SYM (irel[1].r_info) - info->symtab_hdr->sh_info;
    lit_h = alpha_elf_sym_hashes (info->abfd)[indx];

    while (lit_h->root.root.type == bfd_link_hash_indirect
	   || lit_h->root.root.type == bfd_link_hash_warning)
      lit_h = (struct alpha_elf_link_hash_entry *) lit_h->root.root.u.i.link;

    for (lit_gotent = lit_h->got_entries; lit_gotent ;
	 lit_gotent = lit_gotent->next)
      if (lit_gotent->gotobj == info->gotobj
	  && lit_gotent->reloc_type == R_ALPHA_LITERAL
	  && lit_gotent->addend == irel[1].r_addend)
	break;
    BFD_ASSERT (lit_gotent);

    if (--lit_gotent->use_count == 0)
      {
	int sz = alpha_got_entry_size (R_ALPHA_LITERAL);
	alpha_elf_tdata (info->gotobj)->total_got_size -= sz;
      }
  }

  /* Change

	lda	$16,x($gp)			!tlsgd!1
	ldq	$27,__tls_get_addr($gp)		!literal!1
	jsr	$26,($27),__tls_get_addr	!lituse_tlsgd!1
	ldah	$29,0($26)			!gpdisp!2
	lda	$29,0($29)			!gpdisp!2
     to
	ldq	$16,x($gp)			!gottprel
	unop
	call_pal rduniq
	addq	$16,$0,$0
	unop
     or the first pair to
	lda	$16,x($gp)			!tprel
	unop
     or
	ldah	$16,x($gp)			!tprelhi
	lda	$16,x($16)			!tprello

     as appropriate.  */

  use_gottprel = FALSE;
  new_symndx = is_gd ? ELF64_R_SYM (irel->r_info) : 0;
  switch (!dynamic && !info->link_info->shared)
    {
    case 1:
      {
	bfd_vma tp_base;
	bfd_signed_vma disp;

	BFD_ASSERT (elf_hash_table (info->link_info)->tls_sec != NULL);
	tp_base = alpha_get_tprel_base (info->link_info);
	disp = symval - tp_base;

	if (disp >= -0x8000 && disp < 0x8000)
	  {
	    insn = (OP_LDA << 26) | (16 << 21) | (31 << 16);
	    bfd_put_32 (info->abfd, (bfd_vma) insn, pos[0]);
	    bfd_put_32 (info->abfd, (bfd_vma) INSN_UNOP, pos[1]);

	    irel[0].r_offset = pos[0] - info->contents;
	    irel[0].r_info = ELF64_R_INFO (new_symndx, R_ALPHA_TPREL16);
	    irel[1].r_info = ELF64_R_INFO (0, R_ALPHA_NONE);
	    break;
	  }
	else if (disp >= -(bfd_signed_vma) 0x80000000
		 && disp < (bfd_signed_vma) 0x7fff8000
		 && !pos1_unusable)
	  {
	    insn = (OP_LDAH << 26) | (16 << 21) | (31 << 16);
	    bfd_put_32 (info->abfd, (bfd_vma) insn, pos[0]);
	    insn = (OP_LDA << 26) | (16 << 21) | (16 << 16);
	    bfd_put_32 (info->abfd, (bfd_vma) insn, pos[1]);

	    irel[0].r_offset = pos[0] - info->contents;
	    irel[0].r_info = ELF64_R_INFO (new_symndx, R_ALPHA_TPRELHI);
	    irel[1].r_offset = pos[1] - info->contents;
	    irel[1].r_info = ELF64_R_INFO (new_symndx, R_ALPHA_TPRELLO);
	    break;
	  }
      }
      /* FALLTHRU */

    default:
      use_gottprel = TRUE;

      insn = (OP_LDQ << 26) | (16 << 21) | (29 << 16);
      bfd_put_32 (info->abfd, (bfd_vma) insn, pos[0]);
      bfd_put_32 (info->abfd, (bfd_vma) INSN_UNOP, pos[1]);

      irel[0].r_offset = pos[0] - info->contents;
      irel[0].r_info = ELF64_R_INFO (new_symndx, R_ALPHA_GOTTPREL);
      irel[1].r_info = ELF64_R_INFO (0, R_ALPHA_NONE);
      break;
    }

  bfd_put_32 (info->abfd, (bfd_vma) INSN_RDUNIQ, pos[2]);

  insn = INSN_ADDQ | (16 << 21) | (0 << 16) | (0 << 0);
  bfd_put_32 (info->abfd, (bfd_vma) insn, pos[3]);

  bfd_put_32 (info->abfd, (bfd_vma) INSN_UNOP, pos[4]);

  irel[2].r_info = ELF64_R_INFO (0, R_ALPHA_NONE);
  gpdisp->r_info = ELF64_R_INFO (0, R_ALPHA_NONE);

  hint = elf64_alpha_find_reloc_at_ofs (info->relocs, info->relend,
					irel[2].r_offset, R_ALPHA_HINT);
  if (hint)
    hint->r_info = ELF64_R_INFO (0, R_ALPHA_NONE);

  info->changed_contents = TRUE;
  info->changed_relocs = TRUE;

  /* Reduce the use count on the TLSGD/TLSLDM relocation.  */
  if (--info->gotent->use_count == 0)
    {
      int sz = alpha_got_entry_size (info->gotent->reloc_type);
      alpha_elf_tdata (info->gotobj)->total_got_size -= sz;
      if (!info->h)
	alpha_elf_tdata (info->gotobj)->local_got_size -= sz;
    }

  /* If we've switched to a GOTTPREL relocation, increment the reference
     count on that got entry.  */
  if (use_gottprel)
    {
      struct alpha_elf_got_entry *tprel_gotent;

      for (tprel_gotent = *info->first_gotent; tprel_gotent ;
	   tprel_gotent = tprel_gotent->next)
	if (tprel_gotent->gotobj == info->gotobj
	    && tprel_gotent->reloc_type == R_ALPHA_GOTTPREL
	    && tprel_gotent->addend == irel->r_addend)
	  break;
      if (tprel_gotent)
	tprel_gotent->use_count++;
      else
	{
	  if (info->gotent->use_count == 0)
	    tprel_gotent = info->gotent;
	  else
	    {
	      tprel_gotent = (struct alpha_elf_got_entry *)
		bfd_alloc (info->abfd, sizeof (struct alpha_elf_got_entry));
	      if (!tprel_gotent)
		return FALSE;

	      tprel_gotent->next = *info->first_gotent;
	      *info->first_gotent = tprel_gotent;

	      tprel_gotent->gotobj = info->gotobj;
	      tprel_gotent->addend = irel->r_addend;
	      tprel_gotent->got_offset = -1;
	      tprel_gotent->reloc_done = 0;
	      tprel_gotent->reloc_xlated = 0;
	    }

	  tprel_gotent->use_count = 1;
	  tprel_gotent->reloc_type = R_ALPHA_GOTTPREL;
	}
    }

  return TRUE;
}

static bfd_boolean
elf64_alpha_relax_section (bfd *abfd, asection *sec,
			   struct bfd_link_info *link_info, bfd_boolean *again)
{
  Elf_Internal_Shdr *symtab_hdr;
  Elf_Internal_Rela *internal_relocs;
  Elf_Internal_Rela *irel, *irelend;
  Elf_Internal_Sym *isymbuf = NULL;
  struct alpha_elf_got_entry **local_got_entries;
  struct alpha_relax_info info;

  /* There's nothing to change, yet.  */
  *again = FALSE;

  if (link_info->relocatable
      || ((sec->flags & (SEC_CODE | SEC_RELOC | SEC_ALLOC))
	  != (SEC_CODE | SEC_RELOC | SEC_ALLOC))
      || sec->reloc_count == 0)
    return TRUE;

  /* Make sure our GOT and PLT tables are up-to-date.  */
  if (alpha_elf_hash_table(link_info)->relax_trip != link_info->relax_trip)
    {
      alpha_elf_hash_table(link_info)->relax_trip = link_info->relax_trip;

      /* This should never fail after the initial round, since the only
	 error is GOT overflow, and relaxation only shrinks the table.  */
      if (!elf64_alpha_size_got_sections (link_info))
	abort ();
      if (elf_hash_table (link_info)->dynamic_sections_created)
	{
	  elf64_alpha_size_plt_section (link_info);
	  elf64_alpha_size_rela_got_section (link_info);
	}
    }

  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  local_got_entries = alpha_elf_tdata(abfd)->local_got_entries;

  /* Load the relocations for this section.  */
  internal_relocs = (_bfd_elf_link_read_relocs
		     (abfd, sec, (PTR) NULL, (Elf_Internal_Rela *) NULL,
		      link_info->keep_memory));
  if (internal_relocs == NULL)
    return FALSE;

  memset(&info, 0, sizeof (info));
  info.abfd = abfd;
  info.sec = sec;
  info.link_info = link_info;
  info.symtab_hdr = symtab_hdr;
  info.relocs = internal_relocs;
  info.relend = irelend = internal_relocs + sec->reloc_count;

  /* Find the GP for this object.  Do not store the result back via
     _bfd_set_gp_value, since this could change again before final.  */
  info.gotobj = alpha_elf_tdata (abfd)->gotobj;
  if (info.gotobj)
    {
      asection *sgot = alpha_elf_tdata (info.gotobj)->got;
      info.gp = (sgot->output_section->vma
		 + sgot->output_offset
		 + 0x8000);
    }

  /* Get the section contents.  */
  if (elf_section_data (sec)->this_hdr.contents != NULL)
    info.contents = elf_section_data (sec)->this_hdr.contents;
  else
    {
      if (!bfd_malloc_and_get_section (abfd, sec, &info.contents))
	goto error_return;
    }

  for (irel = internal_relocs; irel < irelend; irel++)
    {
      bfd_vma symval;
      struct alpha_elf_got_entry *gotent;
      unsigned long r_type = ELF64_R_TYPE (irel->r_info);
      unsigned long r_symndx = ELF64_R_SYM (irel->r_info);

      /* Early exit for unhandled or unrelaxable relocations.  */
      switch (r_type)
	{
	case R_ALPHA_LITERAL:
	case R_ALPHA_GPRELHIGH:
	case R_ALPHA_GPRELLOW:
	case R_ALPHA_GOTDTPREL:
	case R_ALPHA_GOTTPREL:
	case R_ALPHA_TLSGD:
	  break;

	case R_ALPHA_TLSLDM:
	  /* The symbol for a TLSLDM reloc is ignored.  Collapse the
             reloc to the 0 symbol so that they all match.  */
	  r_symndx = 0;
	  break;

	default:
	  continue;
	}

      /* Get the value of the symbol referred to by the reloc.  */
      if (r_symndx < symtab_hdr->sh_info)
	{
	  /* A local symbol.  */
	  Elf_Internal_Sym *isym;

	  /* Read this BFD's local symbols.  */
	  if (isymbuf == NULL)
	    {
	      isymbuf = (Elf_Internal_Sym *) symtab_hdr->contents;
	      if (isymbuf == NULL)
		isymbuf = bfd_elf_get_elf_syms (abfd, symtab_hdr,
						symtab_hdr->sh_info, 0,
						NULL, NULL, NULL);
	      if (isymbuf == NULL)
		goto error_return;
	    }

	  isym = isymbuf + r_symndx;

	  /* Given the symbol for a TLSLDM reloc is ignored, this also
	     means forcing the symbol value to the tp base.  */
	  if (r_type == R_ALPHA_TLSLDM)
	    {
	      info.tsec = bfd_abs_section_ptr;
	      symval = alpha_get_tprel_base (info.link_info);
	    }
	  else
	    {
	      symval = isym->st_value;
	      if (isym->st_shndx == SHN_UNDEF)
	        continue;
	      else if (isym->st_shndx == SHN_ABS)
	        info.tsec = bfd_abs_section_ptr;
	      else if (isym->st_shndx == SHN_COMMON)
	        info.tsec = bfd_com_section_ptr;
	      else
	        info.tsec = bfd_section_from_elf_index (abfd, isym->st_shndx);
	    }

	  info.h = NULL;
	  info.other = isym->st_other;
	  if (local_got_entries)
	    info.first_gotent = &local_got_entries[r_symndx];
	  else
	    {
	      info.first_gotent = &info.gotent;
	      info.gotent = NULL;
	    }
	}
      else
	{
	  unsigned long indx;
	  struct alpha_elf_link_hash_entry *h;

	  indx = r_symndx - symtab_hdr->sh_info;
	  h = alpha_elf_sym_hashes (abfd)[indx];
	  BFD_ASSERT (h != NULL);

	  while (h->root.root.type == bfd_link_hash_indirect
		 || h->root.root.type == bfd_link_hash_warning)
	    h = (struct alpha_elf_link_hash_entry *)h->root.root.u.i.link;

	  /* If the symbol is undefined, we can't do anything with it.  */
	  if (h->root.root.type == bfd_link_hash_undefined)
	    continue;

	  /* If the symbol isn't defined in the current module,
	     again we can't do anything.  */
	  if (h->root.root.type == bfd_link_hash_undefweak)
	    {
	      info.tsec = bfd_abs_section_ptr;
	      symval = 0;
	    }
	  else if (!h->root.def_regular)
	    {
	      /* Except for TLSGD relocs, which can sometimes be
		 relaxed to GOTTPREL relocs.  */
	      if (r_type != R_ALPHA_TLSGD)
		continue;
	      info.tsec = bfd_abs_section_ptr;
	      symval = 0;
	    }
	  else
	    {
	      info.tsec = h->root.root.u.def.section;
	      symval = h->root.root.u.def.value;
	    }

	  info.h = h;
	  info.other = h->root.other;
	  info.first_gotent = &h->got_entries;
	}

      /* Search for the got entry to be used by this relocation.  */
      for (gotent = *info.first_gotent; gotent ; gotent = gotent->next)
	if (gotent->gotobj == info.gotobj
	    && gotent->reloc_type == r_type
	    && gotent->addend == irel->r_addend)
	  break;
      info.gotent = gotent;

      symval += info.tsec->output_section->vma + info.tsec->output_offset;
      symval += irel->r_addend;

      switch (r_type)
	{
	case R_ALPHA_LITERAL:
	  BFD_ASSERT(info.gotent != NULL);

	  /* If there exist LITUSE relocations immediately following, this
	     opens up all sorts of interesting optimizations, because we
	     now know every location that this address load is used.  */
	  if (irel+1 < irelend
	      && ELF64_R_TYPE (irel[1].r_info) == R_ALPHA_LITUSE)
	    {
	      if (!elf64_alpha_relax_with_lituse (&info, symval, irel))
		goto error_return;
	    }
	  else
	    {
	      if (!elf64_alpha_relax_got_load (&info, symval, irel, r_type))
		goto error_return;
	    }
	  break;

	case R_ALPHA_GOTDTPREL:
	case R_ALPHA_GOTTPREL:
	  BFD_ASSERT(info.gotent != NULL);
	  if (!elf64_alpha_relax_got_load (&info, symval, irel, r_type))
	    goto error_return;
	  break;

	case R_ALPHA_TLSGD:
	case R_ALPHA_TLSLDM:
	  BFD_ASSERT(info.gotent != NULL);
	  if (!elf64_alpha_relax_tls_get_addr (&info, symval, irel,
					       r_type == R_ALPHA_TLSGD))
	    goto error_return;
	  break;
	}
    }

  if (isymbuf != NULL
      && symtab_hdr->contents != (unsigned char *) isymbuf)
    {
      if (!link_info->keep_memory)
	free (isymbuf);
      else
	{
	  /* Cache the symbols for elf_link_input_bfd.  */
	  symtab_hdr->contents = (unsigned char *) isymbuf;
	}
    }

  if (info.contents != NULL
      && elf_section_data (sec)->this_hdr.contents != info.contents)
    {
      if (!info.changed_contents && !link_info->keep_memory)
	free (info.contents);
      else
	{
	  /* Cache the section contents for elf_link_input_bfd.  */
	  elf_section_data (sec)->this_hdr.contents = info.contents;
	}
    }

  if (elf_section_data (sec)->relocs != internal_relocs)
    {
      if (!info.changed_relocs)
	free (internal_relocs);
      else
	elf_section_data (sec)->relocs = internal_relocs;
    }

  *again = info.changed_contents || info.changed_relocs;

  return TRUE;

 error_return:
  if (isymbuf != NULL
      && symtab_hdr->contents != (unsigned char *) isymbuf)
    free (isymbuf);
  if (info.contents != NULL
      && elf_section_data (sec)->this_hdr.contents != info.contents)
    free (info.contents);
  if (internal_relocs != NULL
      && elf_section_data (sec)->relocs != internal_relocs)
    free (internal_relocs);
  return FALSE;
}

/* Emit a dynamic relocation for (DYNINDX, RTYPE, ADDEND) at (SEC, OFFSET)
   into the next available slot in SREL.  */

static void
elf64_alpha_emit_dynrel (bfd *abfd, struct bfd_link_info *info,
			 asection *sec, asection *srel, bfd_vma offset,
			 long dynindx, long rtype, bfd_vma addend)
{
  Elf_Internal_Rela outrel;
  bfd_byte *loc;

  BFD_ASSERT (srel != NULL);

  outrel.r_info = ELF64_R_INFO (dynindx, rtype);
  outrel.r_addend = addend;

  offset = _bfd_elf_section_offset (abfd, info, sec, offset);
  if ((offset | 1) != (bfd_vma) -1)
    outrel.r_offset = sec->output_section->vma + sec->output_offset + offset;
  else
    memset (&outrel, 0, sizeof (outrel));

  loc = srel->contents;
  loc += srel->reloc_count++ * sizeof (Elf64_External_Rela);
  bfd_elf64_swap_reloca_out (abfd, &outrel, loc);
  BFD_ASSERT (sizeof (Elf64_External_Rela) * srel->reloc_count <= srel->size);
}

/* Relocate an Alpha ELF section for a relocatable link.

   We don't have to change anything unless the reloc is against a section
   symbol, in which case we have to adjust according to where the section
   symbol winds up in the output section.  */

static bfd_boolean
elf64_alpha_relocate_section_r (bfd *output_bfd ATTRIBUTE_UNUSED,
				struct bfd_link_info *info ATTRIBUTE_UNUSED,
				bfd *input_bfd, asection *input_section,
				bfd_byte *contents ATTRIBUTE_UNUSED,
				Elf_Internal_Rela *relocs,
				Elf_Internal_Sym *local_syms,
				asection **local_sections)
{
  unsigned long symtab_hdr_sh_info;
  Elf_Internal_Rela *rel;
  Elf_Internal_Rela *relend;
  struct elf_link_hash_entry **sym_hashes;
  bfd_boolean ret_val = TRUE;

  symtab_hdr_sh_info = elf_tdata (input_bfd)->symtab_hdr.sh_info;
  sym_hashes = elf_sym_hashes (input_bfd);

  relend = relocs + input_section->reloc_count;
  for (rel = relocs; rel < relend; rel++)
    {
      unsigned long r_symndx;
      Elf_Internal_Sym *sym;
      asection *sec;
      unsigned long r_type;

      r_type = ELF64_R_TYPE (rel->r_info);
      if (r_type >= R_ALPHA_max)
	{
	  (*_bfd_error_handler)
	    (_("%B: unknown relocation type %d"),
	     input_bfd, (int) r_type);
	  bfd_set_error (bfd_error_bad_value);
	  ret_val = FALSE;
	  continue;
	}

      /* The symbol associated with GPDISP and LITUSE is
	 immaterial.  Only the addend is significant.  */
      if (r_type == R_ALPHA_GPDISP || r_type == R_ALPHA_LITUSE)
	continue;

      r_symndx = ELF64_R_SYM (rel->r_info);
      if (r_symndx < symtab_hdr_sh_info)
	{
	  sym = local_syms + r_symndx;
	  sec = local_sections[r_symndx];
	}
      else
	{
	  struct elf_link_hash_entry *h;

	  h = sym_hashes[r_symndx - symtab_hdr_sh_info];

	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;

	  if (h->root.type != bfd_link_hash_defined
	      && h->root.type != bfd_link_hash_defweak)
	    continue;

	  sym = NULL;
	  sec = h->root.u.def.section;
	}

      if (sec != NULL && elf_discarded_section (sec))
	{
	  /* For relocs against symbols from removed linkonce sections,
	     or sections discarded by a linker script, we just want the
	     section contents zeroed.  */
	  _bfd_clear_contents (elf64_alpha_howto_table + r_type,
			       input_bfd, contents + rel->r_offset);
	  rel->r_info = 0;
	  rel->r_addend = 0;
	  continue;
	}

      if (sym != NULL && ELF_ST_TYPE (sym->st_info) == STT_SECTION)
	rel->r_addend += sec->output_offset;
    }

  return ret_val;
}

/* Relocate an Alpha ELF section.  */

static bfd_boolean
elf64_alpha_relocate_section (bfd *output_bfd, struct bfd_link_info *info,
			      bfd *input_bfd, asection *input_section,
			      bfd_byte *contents, Elf_Internal_Rela *relocs,
			      Elf_Internal_Sym *local_syms,
			      asection **local_sections)
{
  Elf_Internal_Shdr *symtab_hdr;
  Elf_Internal_Rela *rel;
  Elf_Internal_Rela *relend;
  asection *sgot, *srel, *srelgot;
  bfd *dynobj, *gotobj;
  bfd_vma gp, tp_base, dtp_base;
  struct alpha_elf_got_entry **local_got_entries;
  bfd_boolean ret_val;

  /* Handle relocatable links with a smaller loop.  */
  if (info->relocatable)
    return elf64_alpha_relocate_section_r (output_bfd, info, input_bfd,
					   input_section, contents, relocs,
					   local_syms, local_sections);

  /* This is a final link.  */

  ret_val = TRUE;

  symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;

  dynobj = elf_hash_table (info)->dynobj;
  if (dynobj)
    srelgot = bfd_get_section_by_name (dynobj, ".rela.got");
  else
    srelgot = NULL;

  if (input_section->flags & SEC_ALLOC)
    {
      const char *section_name;
      section_name = (bfd_elf_string_from_elf_section
		      (input_bfd, elf_elfheader(input_bfd)->e_shstrndx,
		       elf_section_data(input_section)->rel_hdr.sh_name));
      BFD_ASSERT(section_name != NULL);
      srel = bfd_get_section_by_name (dynobj, section_name);
    }
  else
    srel = NULL;

  /* Find the gp value for this input bfd.  */
  gotobj = alpha_elf_tdata (input_bfd)->gotobj;
  if (gotobj)
    {
      sgot = alpha_elf_tdata (gotobj)->got;
      gp = _bfd_get_gp_value (gotobj);
      if (gp == 0)
	{
	  gp = (sgot->output_section->vma
		+ sgot->output_offset
		+ 0x8000);
	  _bfd_set_gp_value (gotobj, gp);
	}
    }
  else
    {
      sgot = NULL;
      gp = 0;
    }

  local_got_entries = alpha_elf_tdata(input_bfd)->local_got_entries;

  if (elf_hash_table (info)->tls_sec != NULL)
    {
      dtp_base = alpha_get_dtprel_base (info);
      tp_base = alpha_get_tprel_base (info);
    }
  else
    dtp_base = tp_base = 0;

  relend = relocs + input_section->reloc_count;
  for (rel = relocs; rel < relend; rel++)
    {
      struct alpha_elf_link_hash_entry *h = NULL;
      struct alpha_elf_got_entry *gotent;
      bfd_reloc_status_type r;
      reloc_howto_type *howto;
      unsigned long r_symndx;
      Elf_Internal_Sym *sym = NULL;
      asection *sec = NULL;
      bfd_vma value;
      bfd_vma addend;
      bfd_boolean dynamic_symbol_p;
      bfd_boolean undef_weak_ref = FALSE;
      unsigned long r_type;

      r_type = ELF64_R_TYPE(rel->r_info);
      if (r_type >= R_ALPHA_max)
	{
	  (*_bfd_error_handler)
	    (_("%B: unknown relocation type %d"),
	     input_bfd, (int) r_type);
	  bfd_set_error (bfd_error_bad_value);
	  ret_val = FALSE;
	  continue;
	}

      howto = elf64_alpha_howto_table + r_type;
      r_symndx = ELF64_R_SYM(rel->r_info);

      /* The symbol for a TLSLDM reloc is ignored.  Collapse the
	 reloc to the 0 symbol so that they all match.  */
      if (r_type == R_ALPHA_TLSLDM)
	r_symndx = 0;

      if (r_symndx < symtab_hdr->sh_info)
	{
	  asection *msec;
	  sym = local_syms + r_symndx;
	  sec = local_sections[r_symndx];
	  msec = sec;
	  value = _bfd_elf_rela_local_sym (output_bfd, sym, &msec, rel);

	  /* If this is a tp-relative relocation against sym 0,
	     this is hackery from relax_section.  Force the value to
	     be the tls module base.  */
	  if (r_symndx == 0
	      && (r_type == R_ALPHA_TLSLDM
		  || r_type == R_ALPHA_GOTTPREL
		  || r_type == R_ALPHA_TPREL64
		  || r_type == R_ALPHA_TPRELHI
		  || r_type == R_ALPHA_TPRELLO
		  || r_type == R_ALPHA_TPREL16))
	    value = dtp_base;

	  if (local_got_entries)
	    gotent = local_got_entries[r_symndx];
	  else
	    gotent = NULL;

	  /* Need to adjust local GOT entries' addends for SEC_MERGE
	     unless it has been done already.  */
	  if ((sec->flags & SEC_MERGE)
	      && ELF_ST_TYPE (sym->st_info) == STT_SECTION
	      && sec->sec_info_type == ELF_INFO_TYPE_MERGE
	      && gotent
	      && !gotent->reloc_xlated)
	    {
	      struct alpha_elf_got_entry *ent;

	      for (ent = gotent; ent; ent = ent->next)
		{
		  ent->reloc_xlated = 1;
		  if (ent->use_count == 0)
		    continue;
		  msec = sec;
		  ent->addend =
		    _bfd_merged_section_offset (output_bfd, &msec,
						elf_section_data (sec)->
						  sec_info,
						sym->st_value + ent->addend);
		  ent->addend -= sym->st_value;
		  ent->addend += msec->output_section->vma
				 + msec->output_offset
				 - sec->output_section->vma
				 - sec->output_offset;
		}
	    }

	  dynamic_symbol_p = FALSE;
	}
      else
	{
	  bfd_boolean warned;
	  bfd_boolean unresolved_reloc;
	  struct elf_link_hash_entry *hh;
	  struct elf_link_hash_entry **sym_hashes = elf_sym_hashes (input_bfd);

	  RELOC_FOR_GLOBAL_SYMBOL (info, input_bfd, input_section, rel,
				   r_symndx, symtab_hdr, sym_hashes,
				   hh, sec, value,
				   unresolved_reloc, warned);

	  if (warned)
	    continue;

	  if (value == 0
	      && ! unresolved_reloc
	      && hh->root.type == bfd_link_hash_undefweak)
	    undef_weak_ref = TRUE;

	  h = (struct alpha_elf_link_hash_entry *) hh;
          dynamic_symbol_p = alpha_elf_dynamic_symbol_p (&h->root, info);
	  gotent = h->got_entries;
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

      addend = rel->r_addend;
      value += addend;

      /* Search for the proper got entry.  */
      for (; gotent ; gotent = gotent->next)
	if (gotent->gotobj == gotobj
	    && gotent->reloc_type == r_type
	    && gotent->addend == addend)
	  break;

      switch (r_type)
	{
	case R_ALPHA_GPDISP:
	  {
	    bfd_byte *p_ldah, *p_lda;

	    BFD_ASSERT(gp != 0);

	    value = (input_section->output_section->vma
		     + input_section->output_offset
		     + rel->r_offset);

	    p_ldah = contents + rel->r_offset;
	    p_lda = p_ldah + rel->r_addend;

	    r = elf64_alpha_do_reloc_gpdisp (input_bfd, gp - value,
					     p_ldah, p_lda);
	  }
	  break;

	case R_ALPHA_LITERAL:
	  BFD_ASSERT(sgot != NULL);
	  BFD_ASSERT(gp != 0);
	  BFD_ASSERT(gotent != NULL);
	  BFD_ASSERT(gotent->use_count >= 1);

	  if (!gotent->reloc_done)
	    {
	      gotent->reloc_done = 1;

	      bfd_put_64 (output_bfd, value,
			  sgot->contents + gotent->got_offset);

	      /* If the symbol has been forced local, output a
		 RELATIVE reloc, otherwise it will be handled in
		 finish_dynamic_symbol.  */
	      if (info->shared && !dynamic_symbol_p && !undef_weak_ref)
		elf64_alpha_emit_dynrel (output_bfd, info, sgot, srelgot,
					 gotent->got_offset, 0,
					 R_ALPHA_RELATIVE, value);
	    }

	  value = (sgot->output_section->vma
		   + sgot->output_offset
		   + gotent->got_offset);
	  value -= gp;
	  goto default_reloc;

	case R_ALPHA_GPREL32:
	case R_ALPHA_GPREL16:
	case R_ALPHA_GPRELLOW:
	  if (dynamic_symbol_p)
            {
              (*_bfd_error_handler)
                (_("%B: gp-relative relocation against dynamic symbol %s"),
                 input_bfd, h->root.root.root.string);
              ret_val = FALSE;
            }
	  BFD_ASSERT(gp != 0);
	  value -= gp;
	  goto default_reloc;

	case R_ALPHA_GPRELHIGH:
	  if (dynamic_symbol_p)
            {
              (*_bfd_error_handler)
                (_("%B: gp-relative relocation against dynamic symbol %s"),
                 input_bfd, h->root.root.root.string);
              ret_val = FALSE;
            }
	  BFD_ASSERT(gp != 0);
	  value -= gp;
	  value = ((bfd_signed_vma) value >> 16) + ((value >> 15) & 1);
	  goto default_reloc;

	case R_ALPHA_HINT:
	  /* A call to a dynamic symbol is definitely out of range of
	     the 16-bit displacement.  Don't bother writing anything.  */
	  if (dynamic_symbol_p)
	    {
	      r = bfd_reloc_ok;
	      break;
	    }
	  /* The regular PC-relative stuff measures from the start of
	     the instruction rather than the end.  */
	  value -= 4;
	  goto default_reloc;

	case R_ALPHA_BRADDR:
	  if (dynamic_symbol_p)
            {
              (*_bfd_error_handler)
                (_("%B: pc-relative relocation against dynamic symbol %s"),
                 input_bfd, h->root.root.root.string);
              ret_val = FALSE;
            }
	  /* The regular PC-relative stuff measures from the start of
	     the instruction rather than the end.  */
	  value -= 4;
	  goto default_reloc;

	case R_ALPHA_BRSGP:
	  {
	    int other;
	    const char *name;

	    /* The regular PC-relative stuff measures from the start of
	       the instruction rather than the end.  */
	    value -= 4;

	    /* The source and destination gp must be the same.  Note that
	       the source will always have an assigned gp, since we forced
	       one in check_relocs, but that the destination may not, as
	       it might not have had any relocations at all.  Also take
	       care not to crash if H is an undefined symbol.  */
	    if (h != NULL && sec != NULL
		&& alpha_elf_tdata (sec->owner)->gotobj
		&& gotobj != alpha_elf_tdata (sec->owner)->gotobj)
	      {
		(*_bfd_error_handler)
		  (_("%B: change in gp: BRSGP %s"),
		   input_bfd, h->root.root.root.string);
		ret_val = FALSE;
	      }

	    /* The symbol should be marked either NOPV or STD_GPLOAD.  */
	    if (h != NULL)
	      other = h->root.other;
	    else
	      other = sym->st_other;
	    switch (other & STO_ALPHA_STD_GPLOAD)
	      {
	      case STO_ALPHA_NOPV:
	        break;
	      case STO_ALPHA_STD_GPLOAD:
		value += 8;
		break;
	      default:
		if (h != NULL)
		  name = h->root.root.root.string;
		else
		  {
		    name = (bfd_elf_string_from_elf_section
			    (input_bfd, symtab_hdr->sh_link, sym->st_name));
		    if (name == NULL)
		      name = _("<unknown>");
		    else if (name[0] == 0)
		      name = bfd_section_name (input_bfd, sec);
		  }
		(*_bfd_error_handler)
		  (_("%B: !samegp reloc against symbol without .prologue: %s"),
		   input_bfd, name);
		ret_val = FALSE;
		break;
	      }

	    goto default_reloc;
	  }

	case R_ALPHA_REFLONG:
	case R_ALPHA_REFQUAD:
	case R_ALPHA_DTPREL64:
	case R_ALPHA_TPREL64:
	  {
	    long dynindx, dyntype = r_type;
	    bfd_vma dynaddend;

	    /* Careful here to remember RELATIVE relocations for global
	       variables for symbolic shared objects.  */

	    if (dynamic_symbol_p)
	      {
		BFD_ASSERT(h->root.dynindx != -1);
		dynindx = h->root.dynindx;
		dynaddend = addend;
		addend = 0, value = 0;
	      }
	    else if (r_type == R_ALPHA_DTPREL64)
	      {
		BFD_ASSERT (elf_hash_table (info)->tls_sec != NULL);
		value -= dtp_base;
		goto default_reloc;
	      }
	    else if (r_type == R_ALPHA_TPREL64)
	      {
		BFD_ASSERT (elf_hash_table (info)->tls_sec != NULL);
		if (!info->shared)
		  {
		    value -= tp_base;
		    goto default_reloc;
		  }
		dynindx = 0;
		dynaddend = value - dtp_base;
	      }
	    else if (info->shared
		     && r_symndx != 0
		     && (input_section->flags & SEC_ALLOC)
		     && !undef_weak_ref)
	      {
		if (r_type == R_ALPHA_REFLONG)
		  {
		    (*_bfd_error_handler)
		      (_("%B: unhandled dynamic relocation against %s"),
		       input_bfd,
		       h->root.root.root.string);
		    ret_val = FALSE;
		  }
		dynindx = 0;
		dyntype = R_ALPHA_RELATIVE;
		dynaddend = value;
	      }
	    else
	      goto default_reloc;

	    if (input_section->flags & SEC_ALLOC)
	      elf64_alpha_emit_dynrel (output_bfd, info, input_section,
				       srel, rel->r_offset, dynindx,
				       dyntype, dynaddend);
	  }
	  goto default_reloc;

	case R_ALPHA_SREL16:
	case R_ALPHA_SREL32:
	case R_ALPHA_SREL64:
	  if (dynamic_symbol_p)
            {
              (*_bfd_error_handler)
                (_("%B: pc-relative relocation against dynamic symbol %s"),
                 input_bfd, h->root.root.root.string);
              ret_val = FALSE;
            }
	  else if ((info->shared || info->pie) && undef_weak_ref)
            {
              (*_bfd_error_handler)
                (_("%B: pc-relative relocation against undefined weak symbol %s"),
                 input_bfd, h->root.root.root.string);
              ret_val = FALSE;
            }


	  /* ??? .eh_frame references to discarded sections will be smashed
	     to relocations against SHN_UNDEF.  The .eh_frame format allows
	     NULL to be encoded as 0 in any format, so this works here.  */
	  if (r_symndx == 0)
	    howto = (elf64_alpha_howto_table
		     + (r_type - R_ALPHA_SREL32 + R_ALPHA_REFLONG));
	  goto default_reloc;

	case R_ALPHA_TLSLDM:
	  /* Ignore the symbol for the relocation.  The result is always
	     the current module.  */
	  dynamic_symbol_p = 0;
	  /* FALLTHRU */

	case R_ALPHA_TLSGD:
	  if (!gotent->reloc_done)
	    {
	      gotent->reloc_done = 1;

	      /* Note that the module index for the main program is 1.  */
	      bfd_put_64 (output_bfd, !info->shared && !dynamic_symbol_p,
			  sgot->contents + gotent->got_offset);

	      /* If the symbol has been forced local, output a
		 DTPMOD64 reloc, otherwise it will be handled in
		 finish_dynamic_symbol.  */
	      if (info->shared && !dynamic_symbol_p)
		elf64_alpha_emit_dynrel (output_bfd, info, sgot, srelgot,
					 gotent->got_offset, 0,
					 R_ALPHA_DTPMOD64, 0);

	      if (dynamic_symbol_p || r_type == R_ALPHA_TLSLDM)
		value = 0;
	      else
		{
		  BFD_ASSERT (elf_hash_table (info)->tls_sec != NULL);
	          value -= dtp_base;
		}
	      bfd_put_64 (output_bfd, value,
			  sgot->contents + gotent->got_offset + 8);
	    }

	  value = (sgot->output_section->vma
		   + sgot->output_offset
		   + gotent->got_offset);
	  value -= gp;
	  goto default_reloc;

	case R_ALPHA_DTPRELHI:
	case R_ALPHA_DTPRELLO:
	case R_ALPHA_DTPREL16:
	  if (dynamic_symbol_p)
            {
              (*_bfd_error_handler)
                (_("%B: dtp-relative relocation against dynamic symbol %s"),
                 input_bfd, h->root.root.root.string);
              ret_val = FALSE;
            }
	  BFD_ASSERT (elf_hash_table (info)->tls_sec != NULL);
	  value -= dtp_base;
	  if (r_type == R_ALPHA_DTPRELHI)
	    value = ((bfd_signed_vma) value >> 16) + ((value >> 15) & 1);
	  goto default_reloc;

	case R_ALPHA_TPRELHI:
	case R_ALPHA_TPRELLO:
	case R_ALPHA_TPREL16:
	  if (info->shared)
	    {
	      (*_bfd_error_handler)
		(_("%B: TLS local exec code cannot be linked into shared objects"),
		input_bfd);
              ret_val = FALSE;
	    }
	  else if (dynamic_symbol_p)
            {
              (*_bfd_error_handler)
                (_("%B: tp-relative relocation against dynamic symbol %s"),
                 input_bfd, h->root.root.root.string);
              ret_val = FALSE;
            }
	  BFD_ASSERT (elf_hash_table (info)->tls_sec != NULL);
	  value -= tp_base;
	  if (r_type == R_ALPHA_TPRELHI)
	    value = ((bfd_signed_vma) value >> 16) + ((value >> 15) & 1);
	  goto default_reloc;

	case R_ALPHA_GOTDTPREL:
	case R_ALPHA_GOTTPREL:
	  BFD_ASSERT(sgot != NULL);
	  BFD_ASSERT(gp != 0);
	  BFD_ASSERT(gotent != NULL);
	  BFD_ASSERT(gotent->use_count >= 1);

	  if (!gotent->reloc_done)
	    {
	      gotent->reloc_done = 1;

	      if (dynamic_symbol_p)
		value = 0;
	      else
		{
		  BFD_ASSERT (elf_hash_table (info)->tls_sec != NULL);
		  if (r_type == R_ALPHA_GOTDTPREL)
		    value -= dtp_base;
		  else if (!info->shared)
		    value -= tp_base;
		  else
		    {
		      elf64_alpha_emit_dynrel (output_bfd, info, sgot, srelgot,
					       gotent->got_offset, 0,
					       R_ALPHA_TPREL64,
					       value - dtp_base);
		      value = 0;
		    }
		}
	      bfd_put_64 (output_bfd, value,
			  sgot->contents + gotent->got_offset);
	    }

	  value = (sgot->output_section->vma
		   + sgot->output_offset
		   + gotent->got_offset);
	  value -= gp;
	  goto default_reloc;

	default:
	default_reloc:
	  r = _bfd_final_link_relocate (howto, input_bfd, input_section,
					contents, rel->r_offset, value, 0);
	  break;
	}

      switch (r)
	{
	case bfd_reloc_ok:
	  break;

	case bfd_reloc_overflow:
	  {
	    const char *name;

	    /* Don't warn if the overflow is due to pc relative reloc
	       against discarded section.  Section optimization code should
	       handle it.  */

	    if (r_symndx < symtab_hdr->sh_info
		&& sec != NULL && howto->pc_relative
		&& elf_discarded_section (sec))
	      break;

	    if (h != NULL)
	      name = NULL;
	    else
	      {
		name = (bfd_elf_string_from_elf_section
			(input_bfd, symtab_hdr->sh_link, sym->st_name));
		if (name == NULL)
		  return FALSE;
		if (*name == '\0')
		  name = bfd_section_name (input_bfd, sec);
	      }
	    if (! ((*info->callbacks->reloc_overflow)
		   (info, (h ? &h->root.root : NULL), name, howto->name,
		    (bfd_vma) 0, input_bfd, input_section,
		    rel->r_offset)))
	      ret_val = FALSE;
	  }
	  break;

	default:
	case bfd_reloc_outofrange:
	  abort ();
	}
    }

  return ret_val;
}

/* Finish up dynamic symbol handling.  We set the contents of various
   dynamic sections here.  */

static bfd_boolean
elf64_alpha_finish_dynamic_symbol (bfd *output_bfd, struct bfd_link_info *info,
				   struct elf_link_hash_entry *h,
				   Elf_Internal_Sym *sym)
{
  struct alpha_elf_link_hash_entry *ah = (struct alpha_elf_link_hash_entry *)h;
  bfd *dynobj = elf_hash_table(info)->dynobj;

  if (h->needs_plt)
    {
      /* Fill in the .plt entry for this symbol.  */
      asection *splt, *sgot, *srel;
      Elf_Internal_Rela outrel;
      bfd_byte *loc;
      bfd_vma got_addr, plt_addr;
      bfd_vma plt_index;
      struct alpha_elf_got_entry *gotent;

      BFD_ASSERT (h->dynindx != -1);

      splt = bfd_get_section_by_name (dynobj, ".plt");
      BFD_ASSERT (splt != NULL);
      srel = bfd_get_section_by_name (dynobj, ".rela.plt");
      BFD_ASSERT (srel != NULL);

      for (gotent = ah->got_entries; gotent ; gotent = gotent->next)
	if (gotent->reloc_type == R_ALPHA_LITERAL
	    && gotent->use_count > 0)
	  {
	    unsigned int insn;
	    int disp;

	    sgot = alpha_elf_tdata (gotent->gotobj)->got;
	    BFD_ASSERT (sgot != NULL);

	    BFD_ASSERT (gotent->got_offset != -1);
	    BFD_ASSERT (gotent->plt_offset != -1);

	    got_addr = (sgot->output_section->vma
			+ sgot->output_offset
			+ gotent->got_offset);
	    plt_addr = (splt->output_section->vma
			+ splt->output_offset
			+ gotent->plt_offset);

	    plt_index = (gotent->plt_offset-PLT_HEADER_SIZE) / PLT_ENTRY_SIZE;

	    /* Fill in the entry in the procedure linkage table.  */
	    if (elf64_alpha_use_secureplt)
	      {
		disp = (PLT_HEADER_SIZE - 4) - (gotent->plt_offset + 4);
		insn = INSN_AD (INSN_BR, 31, disp);
		bfd_put_32 (output_bfd, insn,
			    splt->contents + gotent->plt_offset);

		plt_index = ((gotent->plt_offset - NEW_PLT_HEADER_SIZE)
			     / NEW_PLT_ENTRY_SIZE);
	      }
	    else
	      {
		disp = -(gotent->plt_offset + 4);
		insn = INSN_AD (INSN_BR, 28, disp);
		bfd_put_32 (output_bfd, insn,
			    splt->contents + gotent->plt_offset);
		bfd_put_32 (output_bfd, INSN_UNOP,
			    splt->contents + gotent->plt_offset + 4);
		bfd_put_32 (output_bfd, INSN_UNOP,
			    splt->contents + gotent->plt_offset + 8);

		plt_index = ((gotent->plt_offset - OLD_PLT_HEADER_SIZE)
			     / OLD_PLT_ENTRY_SIZE);
	      }

	    /* Fill in the entry in the .rela.plt section.  */
	    outrel.r_offset = got_addr;
	    outrel.r_info = ELF64_R_INFO(h->dynindx, R_ALPHA_JMP_SLOT);
	    outrel.r_addend = 0;

	    loc = srel->contents + plt_index * sizeof (Elf64_External_Rela);
	    bfd_elf64_swap_reloca_out (output_bfd, &outrel, loc);

	    /* Fill in the entry in the .got.  */
	    bfd_put_64 (output_bfd, plt_addr,
			sgot->contents + gotent->got_offset);
	  }
    }
  else if (alpha_elf_dynamic_symbol_p (h, info))
    {
      /* Fill in the dynamic relocations for this symbol's .got entries.  */
      asection *srel;
      struct alpha_elf_got_entry *gotent;

      srel = bfd_get_section_by_name (dynobj, ".rela.got");
      BFD_ASSERT (srel != NULL);

      for (gotent = ((struct alpha_elf_link_hash_entry *) h)->got_entries;
	   gotent != NULL;
	   gotent = gotent->next)
	{
	  asection *sgot;
	  long r_type;

	  if (gotent->use_count == 0)
	    continue;

	  sgot = alpha_elf_tdata (gotent->gotobj)->got;

	  r_type = gotent->reloc_type;
	  switch (r_type)
	    {
	    case R_ALPHA_LITERAL:
	      r_type = R_ALPHA_GLOB_DAT;
	      break;
	    case R_ALPHA_TLSGD:
	      r_type = R_ALPHA_DTPMOD64;
	      break;
	    case R_ALPHA_GOTDTPREL:
	      r_type = R_ALPHA_DTPREL64;
	      break;
	    case R_ALPHA_GOTTPREL:
	      r_type = R_ALPHA_TPREL64;
	      break;
	    case R_ALPHA_TLSLDM:
	    default:
	      abort ();
	    }

	  elf64_alpha_emit_dynrel (output_bfd, info, sgot, srel, 
				   gotent->got_offset, h->dynindx,
				   r_type, gotent->addend);

	  if (gotent->reloc_type == R_ALPHA_TLSGD)
	    elf64_alpha_emit_dynrel (output_bfd, info, sgot, srel, 
				     gotent->got_offset + 8, h->dynindx,
				     R_ALPHA_DTPREL64, gotent->addend);
	}
    }

  /* Mark some specially defined symbols as absolute.  */
  if (strcmp (h->root.root.string, "_DYNAMIC") == 0
      || h == elf_hash_table (info)->hgot
      || h == elf_hash_table (info)->hplt)
    sym->st_shndx = SHN_ABS;

  return TRUE;
}

/* Finish up the dynamic sections.  */

static bfd_boolean
elf64_alpha_finish_dynamic_sections (bfd *output_bfd,
				     struct bfd_link_info *info)
{
  bfd *dynobj;
  asection *sdyn;

  dynobj = elf_hash_table (info)->dynobj;
  sdyn = bfd_get_section_by_name (dynobj, ".dynamic");

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      asection *splt, *sgotplt, *srelaplt;
      Elf64_External_Dyn *dyncon, *dynconend;
      bfd_vma plt_vma, gotplt_vma;

      splt = bfd_get_section_by_name (dynobj, ".plt");
      srelaplt = bfd_get_section_by_name (output_bfd, ".rela.plt");
      BFD_ASSERT (splt != NULL && sdyn != NULL);

      plt_vma = splt->output_section->vma + splt->output_offset;

      gotplt_vma = 0;
      if (elf64_alpha_use_secureplt)
	{
	  sgotplt = bfd_get_section_by_name (dynobj, ".got.plt");
	  BFD_ASSERT (sgotplt != NULL);
	  if (sgotplt->size > 0)
	    gotplt_vma = sgotplt->output_section->vma + sgotplt->output_offset;
	}

      dyncon = (Elf64_External_Dyn *) sdyn->contents;
      dynconend = (Elf64_External_Dyn *) (sdyn->contents + sdyn->size);
      for (; dyncon < dynconend; dyncon++)
	{
	  Elf_Internal_Dyn dyn;

	  bfd_elf64_swap_dyn_in (dynobj, dyncon, &dyn);

	  switch (dyn.d_tag)
	    {
	    case DT_PLTGOT:
	      dyn.d_un.d_ptr
		= elf64_alpha_use_secureplt ? gotplt_vma : plt_vma;
	      break;
	    case DT_PLTRELSZ:
	      dyn.d_un.d_val = srelaplt ? srelaplt->size : 0;
	      break;
	    case DT_JMPREL:
	      dyn.d_un.d_ptr = srelaplt ? srelaplt->vma : 0;
	      break;

	    case DT_RELASZ:
	      /* My interpretation of the TIS v1.1 ELF document indicates
		 that RELASZ should not include JMPREL.  This is not what
		 the rest of the BFD does.  It is, however, what the
		 glibc ld.so wants.  Do this fixup here until we found
		 out who is right.  */
	      if (srelaplt)
		dyn.d_un.d_val -= srelaplt->size;
	      break;
	    }

	  bfd_elf64_swap_dyn_out (output_bfd, &dyn, dyncon);
	}

      /* Initialize the plt header.  */
      if (splt->size > 0)
	{
	  unsigned int insn;
	  int ofs;

	  if (elf64_alpha_use_secureplt)
	    {
	      ofs = gotplt_vma - (plt_vma + PLT_HEADER_SIZE);

	      insn = INSN_ABC (INSN_SUBQ, 27, 28, 25);
	      bfd_put_32 (output_bfd, insn, splt->contents);

	      insn = INSN_ABO (INSN_LDAH, 28, 28, (ofs + 0x8000) >> 16);
	      bfd_put_32 (output_bfd, insn, splt->contents + 4);

	      insn = INSN_ABC (INSN_S4SUBQ, 25, 25, 25);
	      bfd_put_32 (output_bfd, insn, splt->contents + 8);

	      insn = INSN_ABO (INSN_LDA, 28, 28, ofs);
	      bfd_put_32 (output_bfd, insn, splt->contents + 12);

	      insn = INSN_ABO (INSN_LDQ, 27, 28, 0);
	      bfd_put_32 (output_bfd, insn, splt->contents + 16);

	      insn = INSN_ABC (INSN_ADDQ, 25, 25, 25);
	      bfd_put_32 (output_bfd, insn, splt->contents + 20);

	      insn = INSN_ABO (INSN_LDQ, 28, 28, 8);
	      bfd_put_32 (output_bfd, insn, splt->contents + 24);

	      insn = INSN_AB (INSN_JMP, 31, 27);
	      bfd_put_32 (output_bfd, insn, splt->contents + 28);

	      insn = INSN_AD (INSN_BR, 28, -PLT_HEADER_SIZE);
	      bfd_put_32 (output_bfd, insn, splt->contents + 32);
	    }
	  else
	    {
	      insn = INSN_AD (INSN_BR, 27, 0);	/* br $27, .+4 */
	      bfd_put_32 (output_bfd, insn, splt->contents);

	      insn = INSN_ABO (INSN_LDQ, 27, 27, 12);
	      bfd_put_32 (output_bfd, insn, splt->contents + 4);

	      insn = INSN_UNOP;
	      bfd_put_32 (output_bfd, insn, splt->contents + 8);

	      insn = INSN_AB (INSN_JMP, 27, 27);
	      bfd_put_32 (output_bfd, insn, splt->contents + 12);

	      /* The next two words will be filled in by ld.so.  */
	      bfd_put_64 (output_bfd, 0, splt->contents + 16);
	      bfd_put_64 (output_bfd, 0, splt->contents + 24);
	    }

	  elf_section_data (splt->output_section)->this_hdr.sh_entsize = 0;
	}
    }

  return TRUE;
}

/* We need to use a special link routine to handle the .mdebug section.
   We need to merge all instances of these sections together, not write
   them all out sequentially.  */

static bfd_boolean
elf64_alpha_final_link (bfd *abfd, struct bfd_link_info *info)
{
  asection *o;
  struct bfd_link_order *p;
  asection *mdebug_sec;
  struct ecoff_debug_info debug;
  const struct ecoff_debug_swap *swap
    = get_elf_backend_data (abfd)->elf_backend_ecoff_debug_swap;
  HDRR *symhdr = &debug.symbolic_header;
  PTR mdebug_handle = NULL;

  /* Go through the sections and collect the mdebug information.  */
  mdebug_sec = NULL;
  for (o = abfd->sections; o != (asection *) NULL; o = o->next)
    {
      if (strcmp (o->name, ".mdebug") == 0)
	{
	  struct extsym_info einfo;

	  /* We have found the .mdebug section in the output file.
	     Look through all the link_orders comprising it and merge
	     the information together.  */
	  symhdr->magic = swap->sym_magic;
	  /* FIXME: What should the version stamp be?  */
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

	  /* We accumulate the debugging information itself in the
	     debug_info structure.  */
	  debug.line = NULL;
	  debug.external_dnr = NULL;
	  debug.external_pdr = NULL;
	  debug.external_sym = NULL;
	  debug.external_opt = NULL;
	  debug.external_aux = NULL;
	  debug.ss = NULL;
	  debug.ssext = debug.ssext_end = NULL;
	  debug.external_fdr = NULL;
	  debug.external_rfd = NULL;
	  debug.external_ext = debug.external_ext_end = NULL;

	  mdebug_handle = bfd_ecoff_debug_init (abfd, &debug, swap, info);
	  if (mdebug_handle == (PTR) NULL)
	    return FALSE;

	  if (1)
	    {
	      asection *s;
	      EXTR esym;
	      bfd_vma last = 0;
	      unsigned int i;
	      static const char * const name[] =
		{
		  ".text", ".init", ".fini", ".data",
		  ".rodata", ".sdata", ".sbss", ".bss"
		};
	      static const int sc[] = { scText, scInit, scFini, scData,
					  scRData, scSData, scSBss, scBss };

	      esym.jmptbl = 0;
	      esym.cobol_main = 0;
	      esym.weakext = 0;
	      esym.reserved = 0;
	      esym.ifd = ifdNil;
	      esym.asym.iss = issNil;
	      esym.asym.st = stLocal;
	      esym.asym.reserved = 0;
	      esym.asym.index = indexNil;
	      for (i = 0; i < 8; i++)
		{
		  esym.asym.sc = sc[i];
		  s = bfd_get_section_by_name (abfd, name[i]);
		  if (s != NULL)
		    {
		      esym.asym.value = s->vma;
		      last = s->vma + s->size;
		    }
		  else
		    esym.asym.value = last;

		  if (! bfd_ecoff_debug_one_external (abfd, &debug, swap,
						      name[i], &esym))
		    return FALSE;
		}
	    }

	  for (p = o->map_head.link_order;
	       p != (struct bfd_link_order *) NULL;
	       p = p->next)
	    {
	      asection *input_section;
	      bfd *input_bfd;
	      const struct ecoff_debug_swap *input_swap;
	      struct ecoff_debug_info input_debug;
	      char *eraw_src;
	      char *eraw_end;

	      if (p->type != bfd_indirect_link_order)
		{
		  if (p->type == bfd_data_link_order)
		    continue;
		  abort ();
		}

	      input_section = p->u.indirect.section;
	      input_bfd = input_section->owner;

	      if (bfd_get_flavour (input_bfd) != bfd_target_elf_flavour
		  || (get_elf_backend_data (input_bfd)
		      ->elf_backend_ecoff_debug_swap) == NULL)
		{
		  /* I don't know what a non ALPHA ELF bfd would be
		     doing with a .mdebug section, but I don't really
		     want to deal with it.  */
		  continue;
		}

	      input_swap = (get_elf_backend_data (input_bfd)
			    ->elf_backend_ecoff_debug_swap);

	      BFD_ASSERT (p->size == input_section->size);

	      /* The ECOFF linking code expects that we have already
		 read in the debugging information and set up an
		 ecoff_debug_info structure, so we do that now.  */
	      if (!elf64_alpha_read_ecoff_info (input_bfd, input_section,
						&input_debug))
		return FALSE;

	      if (! (bfd_ecoff_debug_accumulate
		     (mdebug_handle, abfd, &debug, swap, input_bfd,
		      &input_debug, input_swap, info)))
		return FALSE;

	      /* Loop through the external symbols.  For each one with
		 interesting information, try to find the symbol in
		 the linker global hash table and save the information
		 for the output external symbols.  */
	      eraw_src = input_debug.external_ext;
	      eraw_end = (eraw_src
			  + (input_debug.symbolic_header.iextMax
			     * input_swap->external_ext_size));
	      for (;
		   eraw_src < eraw_end;
		   eraw_src += input_swap->external_ext_size)
		{
		  EXTR ext;
		  const char *name;
		  struct alpha_elf_link_hash_entry *h;

		  (*input_swap->swap_ext_in) (input_bfd, (PTR) eraw_src, &ext);
		  if (ext.asym.sc == scNil
		      || ext.asym.sc == scUndefined
		      || ext.asym.sc == scSUndefined)
		    continue;

		  name = input_debug.ssext + ext.asym.iss;
		  h = alpha_elf_link_hash_lookup (alpha_elf_hash_table (info),
						  name, FALSE, FALSE, TRUE);
		  if (h == NULL || h->esym.ifd != -2)
		    continue;

		  if (ext.ifd != -1)
		    {
		      BFD_ASSERT (ext.ifd
				  < input_debug.symbolic_header.ifdMax);
		      ext.ifd = input_debug.ifdmap[ext.ifd];
		    }

		  h->esym = ext;
		}

	      /* Free up the information we just read.  */
	      free (input_debug.line);
	      free (input_debug.external_dnr);
	      free (input_debug.external_pdr);
	      free (input_debug.external_sym);
	      free (input_debug.external_opt);
	      free (input_debug.external_aux);
	      free (input_debug.ss);
	      free (input_debug.ssext);
	      free (input_debug.external_fdr);
	      free (input_debug.external_rfd);
	      free (input_debug.external_ext);

	      /* Hack: reset the SEC_HAS_CONTENTS flag so that
		 elf_link_input_bfd ignores this section.  */
	      input_section->flags &=~ SEC_HAS_CONTENTS;
	    }

	  /* Build the external symbol information.  */
	  einfo.abfd = abfd;
	  einfo.info = info;
	  einfo.debug = &debug;
	  einfo.swap = swap;
	  einfo.failed = FALSE;
	  elf_link_hash_traverse (elf_hash_table (info),
				  elf64_alpha_output_extsym,
				  (PTR) &einfo);
	  if (einfo.failed)
	    return FALSE;

	  /* Set the size of the .mdebug section.  */
	  o->size = bfd_ecoff_debug_size (abfd, &debug, swap);

	  /* Skip this section later on (I don't think this currently
	     matters, but someday it might).  */
	  o->map_head.link_order = (struct bfd_link_order *) NULL;

	  mdebug_sec = o;
	}
    }

  /* Invoke the regular ELF backend linker to do all the work.  */
  if (! bfd_elf_final_link (abfd, info))
    return FALSE;

  /* Now write out the computed sections.  */

  /* The .got subsections...  */
  {
    bfd *i, *dynobj = elf_hash_table(info)->dynobj;
    for (i = alpha_elf_hash_table(info)->got_list;
	 i != NULL;
	 i = alpha_elf_tdata(i)->got_link_next)
      {
	asection *sgot;

	/* elf_bfd_final_link already did everything in dynobj.  */
	if (i == dynobj)
	  continue;

	sgot = alpha_elf_tdata(i)->got;
	if (! bfd_set_section_contents (abfd, sgot->output_section,
					sgot->contents,
					(file_ptr) sgot->output_offset,
					sgot->size))
	  return FALSE;
      }
  }

  if (mdebug_sec != (asection *) NULL)
    {
      BFD_ASSERT (abfd->output_has_begun);
      if (! bfd_ecoff_write_accumulated_debug (mdebug_handle, abfd, &debug,
					       swap, info,
					       mdebug_sec->filepos))
	return FALSE;

      bfd_ecoff_debug_free (mdebug_handle, abfd, &debug, swap, info);
    }

  return TRUE;
}

static enum elf_reloc_type_class
elf64_alpha_reloc_type_class (const Elf_Internal_Rela *rela)
{
  switch ((int) ELF64_R_TYPE (rela->r_info))
    {
    case R_ALPHA_RELATIVE:
      return reloc_class_relative;
    case R_ALPHA_JMP_SLOT:
      return reloc_class_plt;
    case R_ALPHA_COPY:
      return reloc_class_copy;
    default:
      return reloc_class_normal;
    }
}

static const struct bfd_elf_special_section elf64_alpha_special_sections[] =
{
  { STRING_COMMA_LEN (".sbss"),  -2, SHT_NOBITS,   SHF_ALLOC + SHF_WRITE + SHF_ALPHA_GPREL },
  { STRING_COMMA_LEN (".sdata"), -2, SHT_PROGBITS, SHF_ALLOC + SHF_WRITE + SHF_ALPHA_GPREL },
  { NULL,                     0,  0, 0,            0 }
};

/* ECOFF swapping routines.  These are used when dealing with the
   .mdebug section, which is in the ECOFF debugging format.  Copied
   from elf32-mips.c.  */
static const struct ecoff_debug_swap
elf64_alpha_ecoff_debug_swap =
{
  /* Symbol table magic number.  */
  magicSym2,
  /* Alignment of debugging information.  E.g., 4.  */
  8,
  /* Sizes of external symbolic information.  */
  sizeof (struct hdr_ext),
  sizeof (struct dnr_ext),
  sizeof (struct pdr_ext),
  sizeof (struct sym_ext),
  sizeof (struct opt_ext),
  sizeof (struct fdr_ext),
  sizeof (struct rfd_ext),
  sizeof (struct ext_ext),
  /* Functions to swap in external symbolic data.  */
  ecoff_swap_hdr_in,
  ecoff_swap_dnr_in,
  ecoff_swap_pdr_in,
  ecoff_swap_sym_in,
  ecoff_swap_opt_in,
  ecoff_swap_fdr_in,
  ecoff_swap_rfd_in,
  ecoff_swap_ext_in,
  _bfd_ecoff_swap_tir_in,
  _bfd_ecoff_swap_rndx_in,
  /* Functions to swap out external symbolic data.  */
  ecoff_swap_hdr_out,
  ecoff_swap_dnr_out,
  ecoff_swap_pdr_out,
  ecoff_swap_sym_out,
  ecoff_swap_opt_out,
  ecoff_swap_fdr_out,
  ecoff_swap_rfd_out,
  ecoff_swap_ext_out,
  _bfd_ecoff_swap_tir_out,
  _bfd_ecoff_swap_rndx_out,
  /* Function to read in symbolic data.  */
  elf64_alpha_read_ecoff_info
};

/* Use a non-standard hash bucket size of 8.  */

static const struct elf_size_info alpha_elf_size_info =
{
  sizeof (Elf64_External_Ehdr),
  sizeof (Elf64_External_Phdr),
  sizeof (Elf64_External_Shdr),
  sizeof (Elf64_External_Rel),
  sizeof (Elf64_External_Rela),
  sizeof (Elf64_External_Sym),
  sizeof (Elf64_External_Dyn),
  sizeof (Elf_External_Note),
  8,
  1,
  64, 3,
  ELFCLASS64, EV_CURRENT,
  bfd_elf64_write_out_phdrs,
  bfd_elf64_write_shdrs_and_ehdr,
  bfd_elf64_write_relocs,
  bfd_elf64_swap_symbol_in,
  bfd_elf64_swap_symbol_out,
  bfd_elf64_slurp_reloc_table,
  bfd_elf64_slurp_symbol_table,
  bfd_elf64_swap_dyn_in,
  bfd_elf64_swap_dyn_out,
  bfd_elf64_swap_reloc_in,
  bfd_elf64_swap_reloc_out,
  bfd_elf64_swap_reloca_in,
  bfd_elf64_swap_reloca_out
};

#define TARGET_LITTLE_SYM	bfd_elf64_alpha_vec
#define TARGET_LITTLE_NAME	"elf64-alpha"
#define ELF_ARCH		bfd_arch_alpha
#define ELF_MACHINE_CODE	EM_ALPHA
#define ELF_MAXPAGESIZE	0x10000
#define ELF_COMMONPAGESIZE	0x2000

#define bfd_elf64_bfd_link_hash_table_create \
  elf64_alpha_bfd_link_hash_table_create

#define bfd_elf64_bfd_reloc_type_lookup \
  elf64_alpha_bfd_reloc_type_lookup
#define bfd_elf64_bfd_reloc_name_lookup \
  elf64_alpha_bfd_reloc_name_lookup
#define elf_info_to_howto \
  elf64_alpha_info_to_howto

#define bfd_elf64_mkobject \
  elf64_alpha_mkobject
#define elf_backend_object_p \
  elf64_alpha_object_p

#define elf_backend_section_from_shdr \
  elf64_alpha_section_from_shdr
#define elf_backend_section_flags \
  elf64_alpha_section_flags
#define elf_backend_fake_sections \
  elf64_alpha_fake_sections

#define bfd_elf64_bfd_is_local_label_name \
  elf64_alpha_is_local_label_name
#define bfd_elf64_find_nearest_line \
  elf64_alpha_find_nearest_line
#define bfd_elf64_bfd_relax_section \
  elf64_alpha_relax_section

#define elf_backend_add_symbol_hook \
  elf64_alpha_add_symbol_hook
#define elf_backend_relocs_compatible \
  _bfd_elf_relocs_compatible
#define elf_backend_check_relocs \
  elf64_alpha_check_relocs
#define elf_backend_create_dynamic_sections \
  elf64_alpha_create_dynamic_sections
#define elf_backend_adjust_dynamic_symbol \
  elf64_alpha_adjust_dynamic_symbol
#define elf_backend_merge_symbol_attribute \
  elf64_alpha_merge_symbol_attribute
#define elf_backend_always_size_sections \
  elf64_alpha_always_size_sections
#define elf_backend_size_dynamic_sections \
  elf64_alpha_size_dynamic_sections
#define elf_backend_omit_section_dynsym \
  ((bfd_boolean (*) (bfd *, struct bfd_link_info *, asection *)) bfd_true)
#define elf_backend_relocate_section \
  elf64_alpha_relocate_section
#define elf_backend_finish_dynamic_symbol \
  elf64_alpha_finish_dynamic_symbol
#define elf_backend_finish_dynamic_sections \
  elf64_alpha_finish_dynamic_sections
#define bfd_elf64_bfd_final_link \
  elf64_alpha_final_link
#define elf_backend_reloc_type_class \
  elf64_alpha_reloc_type_class

#define elf_backend_ecoff_debug_swap \
  &elf64_alpha_ecoff_debug_swap

#define elf_backend_size_info \
  alpha_elf_size_info

#define elf_backend_special_sections \
  elf64_alpha_special_sections

/* A few constants that determine how the .plt section is set up.  */
#define elf_backend_want_got_plt 0
#define elf_backend_plt_readonly 0
#define elf_backend_want_plt_sym 1
#define elf_backend_got_header_size 0

#include "elf64-target.h"

/* FreeBSD support.  */

#undef TARGET_LITTLE_SYM
#define TARGET_LITTLE_SYM	bfd_elf64_alpha_freebsd_vec
#undef TARGET_LITTLE_NAME
#define TARGET_LITTLE_NAME	"elf64-alpha-freebsd"
#undef	ELF_OSABI
#define	ELF_OSABI		ELFOSABI_FREEBSD

/* The kernel recognizes executables as valid only if they carry a
   "FreeBSD" label in the ELF header.  So we put this label on all
   executables and (for simplicity) also all other object files.  */

static void
elf64_alpha_fbsd_post_process_headers (bfd * abfd,
	struct bfd_link_info * link_info ATTRIBUTE_UNUSED)
{
  Elf_Internal_Ehdr * i_ehdrp;	/* ELF file header, internal form.  */

  i_ehdrp = elf_elfheader (abfd);

  /* Put an ABI label supported by FreeBSD >= 4.1.  */
  i_ehdrp->e_ident[EI_OSABI] = get_elf_backend_data (abfd)->elf_osabi;
#ifdef OLD_FREEBSD_ABI_LABEL
  /* The ABI label supported by FreeBSD <= 4.0 is quite nonstandard.  */
  memcpy (&i_ehdrp->e_ident[EI_ABIVERSION], "FreeBSD", 8);
#endif
}

#undef elf_backend_post_process_headers
#define elf_backend_post_process_headers \
  elf64_alpha_fbsd_post_process_headers

#undef  elf64_bed
#define elf64_bed elf64_alpha_fbsd_bed

#include "elf64-target.h"
