/* X86-64 specific support for 64-bit ELF
   Copyright 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Contributed by Jan Hubicka <jh@suse.cz>.

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
#include "elf-bfd.h"

#include "elf/x86-64.h"

/* In case we're on a 32-bit machine, construct a 64-bit "-1" value.  */
#define MINUS_ONE (~ (bfd_vma) 0)

/* The relocation "howto" table.  Order of fields:
   type, rightshift, size, bitsize, pc_relative, bitpos, complain_on_overflow,
   special_function, name, partial_inplace, src_mask, dst_mask, pcrel_offset.  */
static reloc_howto_type x86_64_elf_howto_table[] =
{
  HOWTO(R_X86_64_NONE, 0, 0, 0, FALSE, 0, complain_overflow_dont,
	bfd_elf_generic_reloc, "R_X86_64_NONE",	FALSE, 0x00000000, 0x00000000,
	FALSE),
  HOWTO(R_X86_64_64, 0, 4, 64, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_X86_64_64", FALSE, MINUS_ONE, MINUS_ONE,
	FALSE),
  HOWTO(R_X86_64_PC32, 0, 2, 32, TRUE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_PC32", FALSE, 0xffffffff, 0xffffffff,
	TRUE),
  HOWTO(R_X86_64_GOT32, 0, 2, 32, FALSE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_GOT32", FALSE, 0xffffffff, 0xffffffff,
	FALSE),
  HOWTO(R_X86_64_PLT32, 0, 2, 32, TRUE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_PLT32", FALSE, 0xffffffff, 0xffffffff,
	TRUE),
  HOWTO(R_X86_64_COPY, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_X86_64_COPY", FALSE, 0xffffffff, 0xffffffff,
	FALSE),
  HOWTO(R_X86_64_GLOB_DAT, 0, 4, 64, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_X86_64_GLOB_DAT", FALSE, MINUS_ONE,
	MINUS_ONE, FALSE),
  HOWTO(R_X86_64_JUMP_SLOT, 0, 4, 64, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_X86_64_JUMP_SLOT", FALSE, MINUS_ONE,
	MINUS_ONE, FALSE),
  HOWTO(R_X86_64_RELATIVE, 0, 4, 64, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_X86_64_RELATIVE", FALSE, MINUS_ONE,
	MINUS_ONE, FALSE),
  HOWTO(R_X86_64_GOTPCREL, 0, 2, 32, TRUE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_GOTPCREL", FALSE, 0xffffffff,
	0xffffffff, TRUE),
  HOWTO(R_X86_64_32, 0, 2, 32, FALSE, 0, complain_overflow_unsigned,
	bfd_elf_generic_reloc, "R_X86_64_32", FALSE, 0xffffffff, 0xffffffff,
	FALSE),
  HOWTO(R_X86_64_32S, 0, 2, 32, FALSE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_32S", FALSE, 0xffffffff, 0xffffffff,
	FALSE),
  HOWTO(R_X86_64_16, 0, 1, 16, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_X86_64_16", FALSE, 0xffff, 0xffff, FALSE),
  HOWTO(R_X86_64_PC16,0, 1, 16, TRUE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_X86_64_PC16", FALSE, 0xffff, 0xffff, TRUE),
  HOWTO(R_X86_64_8, 0, 0, 8, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_X86_64_8", FALSE, 0xff, 0xff, FALSE),
  HOWTO(R_X86_64_PC8, 0, 0, 8, TRUE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_PC8", FALSE, 0xff, 0xff, TRUE),
  HOWTO(R_X86_64_DTPMOD64, 0, 4, 64, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_X86_64_DTPMOD64", FALSE, MINUS_ONE,
	MINUS_ONE, FALSE),
  HOWTO(R_X86_64_DTPOFF64, 0, 4, 64, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_X86_64_DTPOFF64", FALSE, MINUS_ONE,
	MINUS_ONE, FALSE),
  HOWTO(R_X86_64_TPOFF64, 0, 4, 64, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_X86_64_TPOFF64", FALSE, MINUS_ONE,
	MINUS_ONE, FALSE),
  HOWTO(R_X86_64_TLSGD, 0, 2, 32, TRUE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_TLSGD", FALSE, 0xffffffff,
	0xffffffff, TRUE),
  HOWTO(R_X86_64_TLSLD, 0, 2, 32, TRUE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_TLSLD", FALSE, 0xffffffff,
	0xffffffff, TRUE),
  HOWTO(R_X86_64_DTPOFF32, 0, 2, 32, FALSE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_DTPOFF32", FALSE, 0xffffffff,
	0xffffffff, FALSE),
  HOWTO(R_X86_64_GOTTPOFF, 0, 2, 32, TRUE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_GOTTPOFF", FALSE, 0xffffffff,
	0xffffffff, TRUE),
  HOWTO(R_X86_64_TPOFF32, 0, 2, 32, FALSE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_TPOFF32", FALSE, 0xffffffff,
	0xffffffff, FALSE),
  HOWTO(R_X86_64_PC64, 0, 4, 64, TRUE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_X86_64_PC64", FALSE, MINUS_ONE, MINUS_ONE,
	TRUE),
  HOWTO(R_X86_64_GOTOFF64, 0, 4, 64, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_X86_64_GOTOFF64",
	FALSE, MINUS_ONE, MINUS_ONE, FALSE),
  HOWTO(R_X86_64_GOTPC32, 0, 2, 32, TRUE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_GOTPC32",
	FALSE, 0xffffffff, 0xffffffff, TRUE),
  HOWTO(R_X86_64_GOT64, 0, 4, 64, FALSE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_GOT64", FALSE, MINUS_ONE, MINUS_ONE,
	FALSE),
  HOWTO(R_X86_64_GOTPCREL64, 0, 4, 64, TRUE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_GOTPCREL64", FALSE, MINUS_ONE,
	MINUS_ONE, TRUE),
  HOWTO(R_X86_64_GOTPC64, 0, 4, 64, TRUE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_GOTPC64",
	FALSE, MINUS_ONE, MINUS_ONE, TRUE),
  HOWTO(R_X86_64_GOTPLT64, 0, 4, 64, FALSE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_GOTPLT64", FALSE, MINUS_ONE,
	MINUS_ONE, FALSE),
  HOWTO(R_X86_64_PLTOFF64, 0, 4, 64, FALSE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_X86_64_PLTOFF64", FALSE, MINUS_ONE,
	MINUS_ONE, FALSE),
  EMPTY_HOWTO (32),
  EMPTY_HOWTO (33),
  HOWTO(R_X86_64_GOTPC32_TLSDESC, 0, 2, 32, TRUE, 0,
	complain_overflow_bitfield, bfd_elf_generic_reloc,
	"R_X86_64_GOTPC32_TLSDESC",
	FALSE, 0xffffffff, 0xffffffff, TRUE),
  HOWTO(R_X86_64_TLSDESC_CALL, 0, 0, 0, FALSE, 0,
	complain_overflow_dont, bfd_elf_generic_reloc,
	"R_X86_64_TLSDESC_CALL",
	FALSE, 0, 0, FALSE),
  HOWTO(R_X86_64_TLSDESC, 0, 4, 64, FALSE, 0,
	complain_overflow_bitfield, bfd_elf_generic_reloc,
	"R_X86_64_TLSDESC",
	FALSE, MINUS_ONE, MINUS_ONE, FALSE),

  /* We have a gap in the reloc numbers here.
     R_X86_64_standard counts the number up to this point, and
     R_X86_64_vt_offset is the value to subtract from a reloc type of
     R_X86_64_GNU_VT* to form an index into this table.  */
#define R_X86_64_standard (R_X86_64_TLSDESC + 1)
#define R_X86_64_vt_offset (R_X86_64_GNU_VTINHERIT - R_X86_64_standard)

/* GNU extension to record C++ vtable hierarchy.  */
  HOWTO (R_X86_64_GNU_VTINHERIT, 0, 4, 0, FALSE, 0, complain_overflow_dont,
	 NULL, "R_X86_64_GNU_VTINHERIT", FALSE, 0, 0, FALSE),

/* GNU extension to record C++ vtable member usage.  */
  HOWTO (R_X86_64_GNU_VTENTRY, 0, 4, 0, FALSE, 0, complain_overflow_dont,
	 _bfd_elf_rel_vtable_reloc_fn, "R_X86_64_GNU_VTENTRY", FALSE, 0, 0,
	 FALSE)
};

/* Map BFD relocs to the x86_64 elf relocs.  */
struct elf_reloc_map
{
  bfd_reloc_code_real_type bfd_reloc_val;
  unsigned char elf_reloc_val;
};

static const struct elf_reloc_map x86_64_reloc_map[] =
{
  { BFD_RELOC_NONE,		R_X86_64_NONE, },
  { BFD_RELOC_64,		R_X86_64_64,   },
  { BFD_RELOC_32_PCREL,		R_X86_64_PC32, },
  { BFD_RELOC_X86_64_GOT32,	R_X86_64_GOT32,},
  { BFD_RELOC_X86_64_PLT32,	R_X86_64_PLT32,},
  { BFD_RELOC_X86_64_COPY,	R_X86_64_COPY, },
  { BFD_RELOC_X86_64_GLOB_DAT,	R_X86_64_GLOB_DAT, },
  { BFD_RELOC_X86_64_JUMP_SLOT, R_X86_64_JUMP_SLOT, },
  { BFD_RELOC_X86_64_RELATIVE,	R_X86_64_RELATIVE, },
  { BFD_RELOC_X86_64_GOTPCREL,	R_X86_64_GOTPCREL, },
  { BFD_RELOC_32,		R_X86_64_32, },
  { BFD_RELOC_X86_64_32S,	R_X86_64_32S, },
  { BFD_RELOC_16,		R_X86_64_16, },
  { BFD_RELOC_16_PCREL,		R_X86_64_PC16, },
  { BFD_RELOC_8,		R_X86_64_8, },
  { BFD_RELOC_8_PCREL,		R_X86_64_PC8, },
  { BFD_RELOC_X86_64_DTPMOD64,	R_X86_64_DTPMOD64, },
  { BFD_RELOC_X86_64_DTPOFF64,	R_X86_64_DTPOFF64, },
  { BFD_RELOC_X86_64_TPOFF64,	R_X86_64_TPOFF64, },
  { BFD_RELOC_X86_64_TLSGD,	R_X86_64_TLSGD, },
  { BFD_RELOC_X86_64_TLSLD,	R_X86_64_TLSLD, },
  { BFD_RELOC_X86_64_DTPOFF32,	R_X86_64_DTPOFF32, },
  { BFD_RELOC_X86_64_GOTTPOFF,	R_X86_64_GOTTPOFF, },
  { BFD_RELOC_X86_64_TPOFF32,	R_X86_64_TPOFF32, },
  { BFD_RELOC_64_PCREL,		R_X86_64_PC64, },
  { BFD_RELOC_X86_64_GOTOFF64,	R_X86_64_GOTOFF64, },
  { BFD_RELOC_X86_64_GOTPC32,	R_X86_64_GOTPC32, },
  { BFD_RELOC_X86_64_GOT64,	R_X86_64_GOT64, },
  { BFD_RELOC_X86_64_GOTPCREL64,R_X86_64_GOTPCREL64, },
  { BFD_RELOC_X86_64_GOTPC64,	R_X86_64_GOTPC64, },
  { BFD_RELOC_X86_64_GOTPLT64,	R_X86_64_GOTPLT64, },
  { BFD_RELOC_X86_64_PLTOFF64,	R_X86_64_PLTOFF64, },
  { BFD_RELOC_X86_64_GOTPC32_TLSDESC, R_X86_64_GOTPC32_TLSDESC, },
  { BFD_RELOC_X86_64_TLSDESC_CALL, R_X86_64_TLSDESC_CALL, },
  { BFD_RELOC_X86_64_TLSDESC,	R_X86_64_TLSDESC, },
  { BFD_RELOC_VTABLE_INHERIT,	R_X86_64_GNU_VTINHERIT, },
  { BFD_RELOC_VTABLE_ENTRY,	R_X86_64_GNU_VTENTRY, },
};

static reloc_howto_type *
elf64_x86_64_rtype_to_howto (bfd *abfd, unsigned r_type)
{
  unsigned i;

  if (r_type < (unsigned int) R_X86_64_GNU_VTINHERIT
      || r_type >= (unsigned int) R_X86_64_max)
    {
      if (r_type >= (unsigned int) R_X86_64_standard)
	{
	  (*_bfd_error_handler) (_("%B: invalid relocation type %d"),
				 abfd, (int) r_type);
	  r_type = R_X86_64_NONE;
	}
      i = r_type;
    }
  else
    i = r_type - (unsigned int) R_X86_64_vt_offset;
  BFD_ASSERT (x86_64_elf_howto_table[i].type == r_type);
  return &x86_64_elf_howto_table[i];
}

/* Given a BFD reloc type, return a HOWTO structure.  */
static reloc_howto_type *
elf64_x86_64_reloc_type_lookup (bfd *abfd,
				bfd_reloc_code_real_type code)
{
  unsigned int i;

  for (i = 0; i < sizeof (x86_64_reloc_map) / sizeof (struct elf_reloc_map);
       i++)
    {
      if (x86_64_reloc_map[i].bfd_reloc_val == code)
	return elf64_x86_64_rtype_to_howto (abfd,
					    x86_64_reloc_map[i].elf_reloc_val);
    }
  return 0;
}

static reloc_howto_type *
elf64_x86_64_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED,
				const char *r_name)
{
  unsigned int i;

  for (i = 0;
       i < (sizeof (x86_64_elf_howto_table)
	    / sizeof (x86_64_elf_howto_table[0]));
       i++)
    if (x86_64_elf_howto_table[i].name != NULL
	&& strcasecmp (x86_64_elf_howto_table[i].name, r_name) == 0)
      return &x86_64_elf_howto_table[i];

  return NULL;
}

/* Given an x86_64 ELF reloc type, fill in an arelent structure.  */

static void
elf64_x86_64_info_to_howto (bfd *abfd ATTRIBUTE_UNUSED, arelent *cache_ptr,
			    Elf_Internal_Rela *dst)
{
  unsigned r_type;

  r_type = ELF64_R_TYPE (dst->r_info);
  cache_ptr->howto = elf64_x86_64_rtype_to_howto (abfd, r_type);
  BFD_ASSERT (r_type == cache_ptr->howto->type);
}

/* Support for core dump NOTE sections.  */
static bfd_boolean
elf64_x86_64_grok_prstatus (bfd *abfd, Elf_Internal_Note *note)
{
  int offset;
  size_t size;

  switch (note->descsz)
    {
      default:
	return FALSE;

      case 336:		/* sizeof(istruct elf_prstatus) on Linux/x86_64 */
	/* pr_cursig */
	elf_tdata (abfd)->core_signal
	  = bfd_get_16 (abfd, note->descdata + 12);

	/* pr_pid */
	elf_tdata (abfd)->core_pid
	  = bfd_get_32 (abfd, note->descdata + 32);

	/* pr_reg */
	offset = 112;
	size = 216;

	break;
    }

  /* Make a ".reg/999" section.  */
  return _bfd_elfcore_make_pseudosection (abfd, ".reg",
					  size, note->descpos + offset);
}

static bfd_boolean
elf64_x86_64_grok_psinfo (bfd *abfd, Elf_Internal_Note *note)
{
  switch (note->descsz)
    {
      default:
	return FALSE;

      case 136:		/* sizeof(struct elf_prpsinfo) on Linux/x86_64 */
	elf_tdata (abfd)->core_program
	 = _bfd_elfcore_strndup (abfd, note->descdata + 40, 16);
	elf_tdata (abfd)->core_command
	 = _bfd_elfcore_strndup (abfd, note->descdata + 56, 80);
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

/* Functions for the x86-64 ELF linker.	 */

/* The name of the dynamic interpreter.	 This is put in the .interp
   section.  */

#define ELF_DYNAMIC_INTERPRETER "/lib/ld64.so.1"

/* If ELIMINATE_COPY_RELOCS is non-zero, the linker will try to avoid
   copying dynamic variables from a shared lib into an app's dynbss
   section, and instead use a dynamic relocation to point into the
   shared lib.  */
#define ELIMINATE_COPY_RELOCS 1

/* The size in bytes of an entry in the global offset table.  */

#define GOT_ENTRY_SIZE 8

/* The size in bytes of an entry in the procedure linkage table.  */

#define PLT_ENTRY_SIZE 16

/* The first entry in a procedure linkage table looks like this.  See the
   SVR4 ABI i386 supplement and the x86-64 ABI to see how this works.  */

static const bfd_byte elf64_x86_64_plt0_entry[PLT_ENTRY_SIZE] =
{
  0xff, 0x35, 8, 0, 0, 0,	/* pushq GOT+8(%rip)  */
  0xff, 0x25, 16, 0, 0, 0,	/* jmpq *GOT+16(%rip) */
  0x0f, 0x1f, 0x40, 0x00	/* nopl 0(%rax)       */
};

/* Subsequent entries in a procedure linkage table look like this.  */

static const bfd_byte elf64_x86_64_plt_entry[PLT_ENTRY_SIZE] =
{
  0xff, 0x25,	/* jmpq *name@GOTPC(%rip) */
  0, 0, 0, 0,	/* replaced with offset to this symbol in .got.	 */
  0x68,		/* pushq immediate */
  0, 0, 0, 0,	/* replaced with index into relocation table.  */
  0xe9,		/* jmp relative */
  0, 0, 0, 0	/* replaced with offset to start of .plt0.  */
};

/* The x86-64 linker needs to keep track of the number of relocs that
   it decides to copy as dynamic relocs in check_relocs for each symbol.
   This is so that it can later discard them if they are found to be
   unnecessary.  We store the information in a field extending the
   regular ELF linker hash table.  */

struct elf64_x86_64_dyn_relocs
{
  /* Next section.  */
  struct elf64_x86_64_dyn_relocs *next;

  /* The input section of the reloc.  */
  asection *sec;

  /* Total number of relocs copied for the input section.  */
  bfd_size_type count;

  /* Number of pc-relative relocs copied for the input section.  */
  bfd_size_type pc_count;
};

/* x86-64 ELF linker hash entry.  */

struct elf64_x86_64_link_hash_entry
{
  struct elf_link_hash_entry elf;

  /* Track dynamic relocs copied for this symbol.  */
  struct elf64_x86_64_dyn_relocs *dyn_relocs;

#define GOT_UNKNOWN	0
#define GOT_NORMAL	1
#define GOT_TLS_GD	2
#define GOT_TLS_IE	3
#define GOT_TLS_GDESC	4
#define GOT_TLS_GD_BOTH_P(type) \
  ((type) == (GOT_TLS_GD | GOT_TLS_GDESC))
#define GOT_TLS_GD_P(type) \
  ((type) == GOT_TLS_GD || GOT_TLS_GD_BOTH_P (type))
#define GOT_TLS_GDESC_P(type) \
  ((type) == GOT_TLS_GDESC || GOT_TLS_GD_BOTH_P (type))
#define GOT_TLS_GD_ANY_P(type) \
  (GOT_TLS_GD_P (type) || GOT_TLS_GDESC_P (type))
  unsigned char tls_type;

  /* Offset of the GOTPLT entry reserved for the TLS descriptor,
     starting at the end of the jump table.  */
  bfd_vma tlsdesc_got;
};

#define elf64_x86_64_hash_entry(ent) \
  ((struct elf64_x86_64_link_hash_entry *)(ent))

struct elf64_x86_64_obj_tdata
{
  struct elf_obj_tdata root;

  /* tls_type for each local got entry.  */
  char *local_got_tls_type;

  /* GOTPLT entries for TLS descriptors.  */
  bfd_vma *local_tlsdesc_gotent;
};

#define elf64_x86_64_tdata(abfd) \
  ((struct elf64_x86_64_obj_tdata *) (abfd)->tdata.any)

#define elf64_x86_64_local_got_tls_type(abfd) \
  (elf64_x86_64_tdata (abfd)->local_got_tls_type)

#define elf64_x86_64_local_tlsdesc_gotent(abfd) \
  (elf64_x86_64_tdata (abfd)->local_tlsdesc_gotent)

/* x86-64 ELF linker hash table.  */

struct elf64_x86_64_link_hash_table
{
  struct elf_link_hash_table elf;

  /* Short-cuts to get to dynamic linker sections.  */
  asection *sgot;
  asection *sgotplt;
  asection *srelgot;
  asection *splt;
  asection *srelplt;
  asection *sdynbss;
  asection *srelbss;

  /* The offset into splt of the PLT entry for the TLS descriptor
     resolver.  Special values are 0, if not necessary (or not found
     to be necessary yet), and -1 if needed but not determined
     yet.  */
  bfd_vma tlsdesc_plt;
  /* The offset into sgot of the GOT entry used by the PLT entry
     above.  */
  bfd_vma tlsdesc_got;

  union {
    bfd_signed_vma refcount;
    bfd_vma offset;
  } tls_ld_got;

  /* The amount of space used by the jump slots in the GOT.  */
  bfd_vma sgotplt_jump_table_size;

  /* Small local sym to section mapping cache.  */
  struct sym_sec_cache sym_sec;
};

/* Get the x86-64 ELF linker hash table from a link_info structure.  */

#define elf64_x86_64_hash_table(p) \
  ((struct elf64_x86_64_link_hash_table *) ((p)->hash))

#define elf64_x86_64_compute_jump_table_size(htab) \
  ((htab)->srelplt->reloc_count * GOT_ENTRY_SIZE)

/* Create an entry in an x86-64 ELF linker hash table.	*/

static struct bfd_hash_entry *
link_hash_newfunc (struct bfd_hash_entry *entry, struct bfd_hash_table *table,
		   const char *string)
{
  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (entry == NULL)
    {
      entry = bfd_hash_allocate (table,
				 sizeof (struct elf64_x86_64_link_hash_entry));
      if (entry == NULL)
	return entry;
    }

  /* Call the allocation method of the superclass.  */
  entry = _bfd_elf_link_hash_newfunc (entry, table, string);
  if (entry != NULL)
    {
      struct elf64_x86_64_link_hash_entry *eh;

      eh = (struct elf64_x86_64_link_hash_entry *) entry;
      eh->dyn_relocs = NULL;
      eh->tls_type = GOT_UNKNOWN;
      eh->tlsdesc_got = (bfd_vma) -1;
    }

  return entry;
}

/* Create an X86-64 ELF linker hash table.  */

static struct bfd_link_hash_table *
elf64_x86_64_link_hash_table_create (bfd *abfd)
{
  struct elf64_x86_64_link_hash_table *ret;
  bfd_size_type amt = sizeof (struct elf64_x86_64_link_hash_table);

  ret = (struct elf64_x86_64_link_hash_table *) bfd_malloc (amt);
  if (ret == NULL)
    return NULL;

  if (!_bfd_elf_link_hash_table_init (&ret->elf, abfd, link_hash_newfunc,
				      sizeof (struct elf64_x86_64_link_hash_entry)))
    {
      free (ret);
      return NULL;
    }

  ret->sgot = NULL;
  ret->sgotplt = NULL;
  ret->srelgot = NULL;
  ret->splt = NULL;
  ret->srelplt = NULL;
  ret->sdynbss = NULL;
  ret->srelbss = NULL;
  ret->sym_sec.abfd = NULL;
  ret->tlsdesc_plt = 0;
  ret->tlsdesc_got = 0;
  ret->tls_ld_got.refcount = 0;
  ret->sgotplt_jump_table_size = 0;

  return &ret->elf.root;
}

/* Create .got, .gotplt, and .rela.got sections in DYNOBJ, and set up
   shortcuts to them in our hash table.  */

static bfd_boolean
create_got_section (bfd *dynobj, struct bfd_link_info *info)
{
  struct elf64_x86_64_link_hash_table *htab;

  if (! _bfd_elf_create_got_section (dynobj, info))
    return FALSE;

  htab = elf64_x86_64_hash_table (info);
  htab->sgot = bfd_get_section_by_name (dynobj, ".got");
  htab->sgotplt = bfd_get_section_by_name (dynobj, ".got.plt");
  if (!htab->sgot || !htab->sgotplt)
    abort ();

  htab->srelgot = bfd_make_section_with_flags (dynobj, ".rela.got",
					       (SEC_ALLOC | SEC_LOAD
						| SEC_HAS_CONTENTS
						| SEC_IN_MEMORY
						| SEC_LINKER_CREATED
						| SEC_READONLY));
  if (htab->srelgot == NULL
      || ! bfd_set_section_alignment (dynobj, htab->srelgot, 3))
    return FALSE;
  return TRUE;
}

/* Create .plt, .rela.plt, .got, .got.plt, .rela.got, .dynbss, and
   .rela.bss sections in DYNOBJ, and set up shortcuts to them in our
   hash table.  */

static bfd_boolean
elf64_x86_64_create_dynamic_sections (bfd *dynobj, struct bfd_link_info *info)
{
  struct elf64_x86_64_link_hash_table *htab;

  htab = elf64_x86_64_hash_table (info);
  if (!htab->sgot && !create_got_section (dynobj, info))
    return FALSE;

  if (!_bfd_elf_create_dynamic_sections (dynobj, info))
    return FALSE;

  htab->splt = bfd_get_section_by_name (dynobj, ".plt");
  htab->srelplt = bfd_get_section_by_name (dynobj, ".rela.plt");
  htab->sdynbss = bfd_get_section_by_name (dynobj, ".dynbss");
  if (!info->shared)
    htab->srelbss = bfd_get_section_by_name (dynobj, ".rela.bss");

  if (!htab->splt || !htab->srelplt || !htab->sdynbss
      || (!info->shared && !htab->srelbss))
    abort ();

  return TRUE;
}

/* Copy the extra info we tack onto an elf_link_hash_entry.  */

static void
elf64_x86_64_copy_indirect_symbol (struct bfd_link_info *info,
				   struct elf_link_hash_entry *dir,
				   struct elf_link_hash_entry *ind)
{
  struct elf64_x86_64_link_hash_entry *edir, *eind;

  edir = (struct elf64_x86_64_link_hash_entry *) dir;
  eind = (struct elf64_x86_64_link_hash_entry *) ind;

  if (eind->dyn_relocs != NULL)
    {
      if (edir->dyn_relocs != NULL)
	{
	  struct elf64_x86_64_dyn_relocs **pp;
	  struct elf64_x86_64_dyn_relocs *p;

	  /* Add reloc counts against the indirect sym to the direct sym
	     list.  Merge any entries against the same section.  */
	  for (pp = &eind->dyn_relocs; (p = *pp) != NULL; )
	    {
	      struct elf64_x86_64_dyn_relocs *q;

	      for (q = edir->dyn_relocs; q != NULL; q = q->next)
		if (q->sec == p->sec)
		  {
		    q->pc_count += p->pc_count;
		    q->count += p->count;
		    *pp = p->next;
		    break;
		  }
	      if (q == NULL)
		pp = &p->next;
	    }
	  *pp = edir->dyn_relocs;
	}

      edir->dyn_relocs = eind->dyn_relocs;
      eind->dyn_relocs = NULL;
    }

  if (ind->root.type == bfd_link_hash_indirect
      && dir->got.refcount <= 0)
    {
      edir->tls_type = eind->tls_type;
      eind->tls_type = GOT_UNKNOWN;
    }

  if (ELIMINATE_COPY_RELOCS
      && ind->root.type != bfd_link_hash_indirect
      && dir->dynamic_adjusted)
    {
      /* If called to transfer flags for a weakdef during processing
	 of elf_adjust_dynamic_symbol, don't copy non_got_ref.
	 We clear it ourselves for ELIMINATE_COPY_RELOCS.  */
      dir->ref_dynamic |= ind->ref_dynamic;
      dir->ref_regular |= ind->ref_regular;
      dir->ref_regular_nonweak |= ind->ref_regular_nonweak;
      dir->needs_plt |= ind->needs_plt;
      dir->pointer_equality_needed |= ind->pointer_equality_needed;
    }
  else
    _bfd_elf_link_hash_copy_indirect (info, dir, ind);
}

static bfd_boolean
elf64_x86_64_mkobject (bfd *abfd)
{
  if (abfd->tdata.any == NULL)
    {
      bfd_size_type amt = sizeof (struct elf64_x86_64_obj_tdata);
      abfd->tdata.any = bfd_zalloc (abfd, amt);
      if (abfd->tdata.any == NULL)
	return FALSE;
    }
  return bfd_elf_mkobject (abfd);
}

static bfd_boolean
elf64_x86_64_elf_object_p (bfd *abfd)
{
  /* Set the right machine number for an x86-64 elf64 file.  */
  bfd_default_set_arch_mach (abfd, bfd_arch_i386, bfd_mach_x86_64);
  return TRUE;
}

static int
elf64_x86_64_tls_transition (struct bfd_link_info *info, int r_type, int is_local)
{
  if (info->shared)
    return r_type;

  switch (r_type)
    {
    case R_X86_64_TLSGD:
    case R_X86_64_GOTPC32_TLSDESC:
    case R_X86_64_TLSDESC_CALL:
    case R_X86_64_GOTTPOFF:
      if (is_local)
	return R_X86_64_TPOFF32;
      return R_X86_64_GOTTPOFF;
    case R_X86_64_TLSLD:
      return R_X86_64_TPOFF32;
    }

   return r_type;
}

/* Look through the relocs for a section during the first phase, and
   calculate needed space in the global offset table, procedure
   linkage table, and dynamic reloc sections.  */

static bfd_boolean
elf64_x86_64_check_relocs (bfd *abfd, struct bfd_link_info *info, asection *sec,
			   const Elf_Internal_Rela *relocs)
{
  struct elf64_x86_64_link_hash_table *htab;
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  const Elf_Internal_Rela *rel;
  const Elf_Internal_Rela *rel_end;
  asection *sreloc;

  if (info->relocatable)
    return TRUE;

  htab = elf64_x86_64_hash_table (info);
  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (abfd);

  sreloc = NULL;

  rel_end = relocs + sec->reloc_count;
  for (rel = relocs; rel < rel_end; rel++)
    {
      unsigned int r_type;
      unsigned long r_symndx;
      struct elf_link_hash_entry *h;

      r_symndx = ELF64_R_SYM (rel->r_info);
      r_type = ELF64_R_TYPE (rel->r_info);

      if (r_symndx >= NUM_SHDR_ENTRIES (symtab_hdr))
	{
	  (*_bfd_error_handler) (_("%B: bad symbol index: %d"),
				 abfd, r_symndx);
	  return FALSE;
	}

      if (r_symndx < symtab_hdr->sh_info)
	h = NULL;
      else
	{
	  h = sym_hashes[r_symndx - symtab_hdr->sh_info];
	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;
	}

      r_type = elf64_x86_64_tls_transition (info, r_type, h == NULL);
      switch (r_type)
	{
	case R_X86_64_TLSLD:
	  htab->tls_ld_got.refcount += 1;
	  goto create_got;

	case R_X86_64_TPOFF32:
	  if (info->shared)
	    {
	      (*_bfd_error_handler)
		(_("%B: relocation %s against `%s' can not be used when making a shared object; recompile with -fPIC"),
		 abfd,
		 x86_64_elf_howto_table[r_type].name,
		 (h) ? h->root.root.string : "a local symbol");
	      bfd_set_error (bfd_error_bad_value);
	      return FALSE;
	    }
	  break;

	case R_X86_64_GOTTPOFF:
	  if (info->shared)
	    info->flags |= DF_STATIC_TLS;
	  /* Fall through */

	case R_X86_64_GOT32:
	case R_X86_64_GOTPCREL:
	case R_X86_64_TLSGD:
	case R_X86_64_GOT64:
	case R_X86_64_GOTPCREL64:
	case R_X86_64_GOTPLT64:
	case R_X86_64_GOTPC32_TLSDESC:
	case R_X86_64_TLSDESC_CALL:
	  /* This symbol requires a global offset table entry.	*/
	  {
	    int tls_type, old_tls_type;

	    switch (r_type)
	      {
	      default: tls_type = GOT_NORMAL; break;
	      case R_X86_64_TLSGD: tls_type = GOT_TLS_GD; break;
	      case R_X86_64_GOTTPOFF: tls_type = GOT_TLS_IE; break;
	      case R_X86_64_GOTPC32_TLSDESC:
	      case R_X86_64_TLSDESC_CALL:
		tls_type = GOT_TLS_GDESC; break;
	      }

	    if (h != NULL)
	      {
		if (r_type == R_X86_64_GOTPLT64)
		  {
		    /* This relocation indicates that we also need
		       a PLT entry, as this is a function.  We don't need
		       a PLT entry for local symbols.  */
		    h->needs_plt = 1;
		    h->plt.refcount += 1;
		  }
		h->got.refcount += 1;
		old_tls_type = elf64_x86_64_hash_entry (h)->tls_type;
	      }
	    else
	      {
		bfd_signed_vma *local_got_refcounts;

		/* This is a global offset table entry for a local symbol.  */
		local_got_refcounts = elf_local_got_refcounts (abfd);
		if (local_got_refcounts == NULL)
		  {
		    bfd_size_type size;

		    size = symtab_hdr->sh_info;
		    size *= sizeof (bfd_signed_vma)
		      + sizeof (bfd_vma) + sizeof (char);
		    local_got_refcounts = ((bfd_signed_vma *)
					   bfd_zalloc (abfd, size));
		    if (local_got_refcounts == NULL)
		      return FALSE;
		    elf_local_got_refcounts (abfd) = local_got_refcounts;
		    elf64_x86_64_local_tlsdesc_gotent (abfd)
		      = (bfd_vma *) (local_got_refcounts + symtab_hdr->sh_info);
		    elf64_x86_64_local_got_tls_type (abfd)
		      = (char *) (local_got_refcounts + 2 * symtab_hdr->sh_info);
		  }
		local_got_refcounts[r_symndx] += 1;
		old_tls_type
		  = elf64_x86_64_local_got_tls_type (abfd) [r_symndx];
	      }

	    /* If a TLS symbol is accessed using IE at least once,
	       there is no point to use dynamic model for it.  */
	    if (old_tls_type != tls_type && old_tls_type != GOT_UNKNOWN
		&& (! GOT_TLS_GD_ANY_P (old_tls_type)
		    || tls_type != GOT_TLS_IE))
	      {
		if (old_tls_type == GOT_TLS_IE && GOT_TLS_GD_ANY_P (tls_type))
		  tls_type = old_tls_type;
		else if (GOT_TLS_GD_ANY_P (old_tls_type)
			 && GOT_TLS_GD_ANY_P (tls_type))
		  tls_type |= old_tls_type;
		else
		  {
		    (*_bfd_error_handler)
		      (_("%B: %s' accessed both as normal and thread local symbol"),
		       abfd, h ? h->root.root.string : "<local>");
		    return FALSE;
		  }
	      }

	    if (old_tls_type != tls_type)
	      {
		if (h != NULL)
		  elf64_x86_64_hash_entry (h)->tls_type = tls_type;
		else
		  elf64_x86_64_local_got_tls_type (abfd) [r_symndx] = tls_type;
	      }
	  }
	  /* Fall through */

	case R_X86_64_GOTOFF64:
	case R_X86_64_GOTPC32:
	case R_X86_64_GOTPC64:
	create_got:
	  if (htab->sgot == NULL)
	    {
	      if (htab->elf.dynobj == NULL)
		htab->elf.dynobj = abfd;
	      if (!create_got_section (htab->elf.dynobj, info))
		return FALSE;
	    }
	  break;

	case R_X86_64_PLT32:
	  /* This symbol requires a procedure linkage table entry.  We
	     actually build the entry in adjust_dynamic_symbol,
	     because this might be a case of linking PIC code which is
	     never referenced by a dynamic object, in which case we
	     don't need to generate a procedure linkage table entry
	     after all.	 */

	  /* If this is a local symbol, we resolve it directly without
	     creating a procedure linkage table entry.	*/
	  if (h == NULL)
	    continue;

	  h->needs_plt = 1;
	  h->plt.refcount += 1;
	  break;

	case R_X86_64_PLTOFF64:
	  /* This tries to form the 'address' of a function relative
	     to GOT.  For global symbols we need a PLT entry.  */
	  if (h != NULL)
	    {
	      h->needs_plt = 1;
	      h->plt.refcount += 1;
	    }
	  goto create_got;

	case R_X86_64_8:
	case R_X86_64_16:
	case R_X86_64_32:
	case R_X86_64_32S:
	  /* Let's help debug shared library creation.  These relocs
	     cannot be used in shared libs.  Don't error out for
	     sections we don't care about, such as debug sections or
	     non-constant sections.  */
	  if (info->shared
	      && (sec->flags & SEC_ALLOC) != 0
	      && (sec->flags & SEC_READONLY) != 0)
	    {
	      (*_bfd_error_handler)
		(_("%B: relocation %s against `%s' can not be used when making a shared object; recompile with -fPIC"),
		 abfd,
		 x86_64_elf_howto_table[r_type].name,
		 (h) ? h->root.root.string : "a local symbol");
	      bfd_set_error (bfd_error_bad_value);
	      return FALSE;
	    }
	  /* Fall through.  */

	case R_X86_64_PC8:
	case R_X86_64_PC16:
	case R_X86_64_PC32:
	case R_X86_64_PC64:
	case R_X86_64_64:
	  if (h != NULL && !info->shared)
	    {
	      /* If this reloc is in a read-only section, we might
		 need a copy reloc.  We can't check reliably at this
		 stage whether the section is read-only, as input
		 sections have not yet been mapped to output sections.
		 Tentatively set the flag for now, and correct in
		 adjust_dynamic_symbol.  */
	      h->non_got_ref = 1;

	      /* We may need a .plt entry if the function this reloc
		 refers to is in a shared lib.  */
	      h->plt.refcount += 1;
	      if (r_type != R_X86_64_PC32 && r_type != R_X86_64_PC64)
		h->pointer_equality_needed = 1;
	    }

	  /* If we are creating a shared library, and this is a reloc
	     against a global symbol, or a non PC relative reloc
	     against a local symbol, then we need to copy the reloc
	     into the shared library.  However, if we are linking with
	     -Bsymbolic, we do not need to copy a reloc against a
	     global symbol which is defined in an object we are
	     including in the link (i.e., DEF_REGULAR is set).	At
	     this point we have not seen all the input files, so it is
	     possible that DEF_REGULAR is not set now but will be set
	     later (it is never cleared).  In case of a weak definition,
	     DEF_REGULAR may be cleared later by a strong definition in
	     a shared library.  We account for that possibility below by
	     storing information in the relocs_copied field of the hash
	     table entry.  A similar situation occurs when creating
	     shared libraries and symbol visibility changes render the
	     symbol local.

	     If on the other hand, we are creating an executable, we
	     may need to keep relocations for symbols satisfied by a
	     dynamic library if we manage to avoid copy relocs for the
	     symbol.  */
	  if ((info->shared
	       && (sec->flags & SEC_ALLOC) != 0
	       && (((r_type != R_X86_64_PC8)
		    && (r_type != R_X86_64_PC16)
		    && (r_type != R_X86_64_PC32)
		    && (r_type != R_X86_64_PC64))
		   || (h != NULL
		       && (! SYMBOLIC_BIND (info, h)
			   || h->root.type == bfd_link_hash_defweak
			   || !h->def_regular))))
	      || (ELIMINATE_COPY_RELOCS
		  && !info->shared
		  && (sec->flags & SEC_ALLOC) != 0
		  && h != NULL
		  && (h->root.type == bfd_link_hash_defweak
		      || !h->def_regular)))
	    {
	      struct elf64_x86_64_dyn_relocs *p;
	      struct elf64_x86_64_dyn_relocs **head;

	      /* We must copy these reloc types into the output file.
		 Create a reloc section in dynobj and make room for
		 this reloc.  */
	      if (sreloc == NULL)
		{
		  const char *name;
		  bfd *dynobj;

		  name = (bfd_elf_string_from_elf_section
			  (abfd,
			   elf_elfheader (abfd)->e_shstrndx,
			   elf_section_data (sec)->rel_hdr.sh_name));
		  if (name == NULL)
		    return FALSE;

		  if (! CONST_STRNEQ (name, ".rela")
		      || strcmp (bfd_get_section_name (abfd, sec),
				 name + 5) != 0)
		    {
		      (*_bfd_error_handler)
			(_("%B: bad relocation section name `%s\'"),
			 abfd, name);
		    }

		  if (htab->elf.dynobj == NULL)
		    htab->elf.dynobj = abfd;

		  dynobj = htab->elf.dynobj;

		  sreloc = bfd_get_section_by_name (dynobj, name);
		  if (sreloc == NULL)
		    {
		      flagword flags;

		      flags = (SEC_HAS_CONTENTS | SEC_READONLY
			       | SEC_IN_MEMORY | SEC_LINKER_CREATED);
		      if ((sec->flags & SEC_ALLOC) != 0)
			flags |= SEC_ALLOC | SEC_LOAD;
		      sreloc = bfd_make_section_with_flags (dynobj,
							    name,
							    flags);
		      if (sreloc == NULL
			  || ! bfd_set_section_alignment (dynobj, sreloc, 3))
			return FALSE;
		    }
		  elf_section_data (sec)->sreloc = sreloc;
		}

	      /* If this is a global symbol, we count the number of
		 relocations we need for this symbol.  */
	      if (h != NULL)
		{
		  head = &((struct elf64_x86_64_link_hash_entry *) h)->dyn_relocs;
		}
	      else
		{
		  void **vpp;
		  /* Track dynamic relocs needed for local syms too.
		     We really need local syms available to do this
		     easily.  Oh well.  */

		  asection *s;
		  s = bfd_section_from_r_symndx (abfd, &htab->sym_sec,
						 sec, r_symndx);
		  if (s == NULL)
		    return FALSE;

		  /* Beware of type punned pointers vs strict aliasing
		     rules.  */
		  vpp = &(elf_section_data (s)->local_dynrel);
		  head = (struct elf64_x86_64_dyn_relocs **)vpp;
		}

	      p = *head;
	      if (p == NULL || p->sec != sec)
		{
		  bfd_size_type amt = sizeof *p;
		  p = ((struct elf64_x86_64_dyn_relocs *)
		       bfd_alloc (htab->elf.dynobj, amt));
		  if (p == NULL)
		    return FALSE;
		  p->next = *head;
		  *head = p;
		  p->sec = sec;
		  p->count = 0;
		  p->pc_count = 0;
		}

	      p->count += 1;
	      if (r_type == R_X86_64_PC8
		  || r_type == R_X86_64_PC16
		  || r_type == R_X86_64_PC32
		  || r_type == R_X86_64_PC64)
		p->pc_count += 1;
	    }
	  break;

	  /* This relocation describes the C++ object vtable hierarchy.
	     Reconstruct it for later use during GC.  */
	case R_X86_64_GNU_VTINHERIT:
	  if (!bfd_elf_gc_record_vtinherit (abfd, sec, h, rel->r_offset))
	    return FALSE;
	  break;

	  /* This relocation describes which C++ vtable entries are actually
	     used.  Record for later use during GC.  */
	case R_X86_64_GNU_VTENTRY:
	  if (!bfd_elf_gc_record_vtentry (abfd, sec, h, rel->r_addend))
	    return FALSE;
	  break;

	default:
	  break;
	}
    }

  return TRUE;
}

/* Return the section that should be marked against GC for a given
   relocation.	*/

static asection *
elf64_x86_64_gc_mark_hook (asection *sec,
			   struct bfd_link_info *info,
			   Elf_Internal_Rela *rel,
			   struct elf_link_hash_entry *h,
			   Elf_Internal_Sym *sym)
{
  if (h != NULL)
    switch (ELF64_R_TYPE (rel->r_info))
      {
      case R_X86_64_GNU_VTINHERIT:
      case R_X86_64_GNU_VTENTRY:
	return NULL;
      }

  return _bfd_elf_gc_mark_hook (sec, info, rel, h, sym);
}

/* Update the got entry reference counts for the section being removed.	 */

static bfd_boolean
elf64_x86_64_gc_sweep_hook (bfd *abfd, struct bfd_link_info *info,
			    asection *sec, const Elf_Internal_Rela *relocs)
{
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  bfd_signed_vma *local_got_refcounts;
  const Elf_Internal_Rela *rel, *relend;

  elf_section_data (sec)->local_dynrel = NULL;

  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (abfd);
  local_got_refcounts = elf_local_got_refcounts (abfd);

  relend = relocs + sec->reloc_count;
  for (rel = relocs; rel < relend; rel++)
    {
      unsigned long r_symndx;
      unsigned int r_type;
      struct elf_link_hash_entry *h = NULL;

      r_symndx = ELF64_R_SYM (rel->r_info);
      if (r_symndx >= symtab_hdr->sh_info)
	{
	  struct elf64_x86_64_link_hash_entry *eh;
	  struct elf64_x86_64_dyn_relocs **pp;
	  struct elf64_x86_64_dyn_relocs *p;

	  h = sym_hashes[r_symndx - symtab_hdr->sh_info];
	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;
	  eh = (struct elf64_x86_64_link_hash_entry *) h;

	  for (pp = &eh->dyn_relocs; (p = *pp) != NULL; pp = &p->next)
	    if (p->sec == sec)
	      {
		/* Everything must go for SEC.  */
		*pp = p->next;
		break;
	      }
	}

      r_type = ELF64_R_TYPE (rel->r_info);
      r_type = elf64_x86_64_tls_transition (info, r_type, h != NULL);
      switch (r_type)
	{
	case R_X86_64_TLSLD:
	  if (elf64_x86_64_hash_table (info)->tls_ld_got.refcount > 0)
	    elf64_x86_64_hash_table (info)->tls_ld_got.refcount -= 1;
	  break;

	case R_X86_64_TLSGD:
	case R_X86_64_GOTPC32_TLSDESC:
	case R_X86_64_TLSDESC_CALL:
	case R_X86_64_GOTTPOFF:
	case R_X86_64_GOT32:
	case R_X86_64_GOTPCREL:
	case R_X86_64_GOT64:
	case R_X86_64_GOTPCREL64:
	case R_X86_64_GOTPLT64:
	  if (h != NULL)
	    {
	      if (r_type == R_X86_64_GOTPLT64 && h->plt.refcount > 0)
	        h->plt.refcount -= 1;
	      if (h->got.refcount > 0)
		h->got.refcount -= 1;
	    }
	  else if (local_got_refcounts != NULL)
	    {
	      if (local_got_refcounts[r_symndx] > 0)
		local_got_refcounts[r_symndx] -= 1;
	    }
	  break;

	case R_X86_64_8:
	case R_X86_64_16:
	case R_X86_64_32:
	case R_X86_64_64:
	case R_X86_64_32S:
	case R_X86_64_PC8:
	case R_X86_64_PC16:
	case R_X86_64_PC32:
	case R_X86_64_PC64:
	  if (info->shared)
	    break;
	  /* Fall thru */

	case R_X86_64_PLT32:
	case R_X86_64_PLTOFF64:
	  if (h != NULL)
	    {
	      if (h->plt.refcount > 0)
		h->plt.refcount -= 1;
	    }
	  break;

	default:
	  break;
	}
    }

  return TRUE;
}

/* Adjust a symbol defined by a dynamic object and referenced by a
   regular object.  The current definition is in some section of the
   dynamic object, but we're not including those sections.  We have to
   change the definition to something the rest of the link can
   understand.	*/

static bfd_boolean
elf64_x86_64_adjust_dynamic_symbol (struct bfd_link_info *info,
				    struct elf_link_hash_entry *h)
{
  struct elf64_x86_64_link_hash_table *htab;
  asection *s;

  /* If this is a function, put it in the procedure linkage table.  We
     will fill in the contents of the procedure linkage table later,
     when we know the address of the .got section.  */
  if (h->type == STT_FUNC
      || h->needs_plt)
    {
      if (h->plt.refcount <= 0
	  || SYMBOL_CALLS_LOCAL (info, h)
	  || (ELF_ST_VISIBILITY (h->other) != STV_DEFAULT
	      && h->root.type == bfd_link_hash_undefweak))
	{
	  /* This case can occur if we saw a PLT32 reloc in an input
	     file, but the symbol was never referred to by a dynamic
	     object, or if all references were garbage collected.  In
	     such a case, we don't actually need to build a procedure
	     linkage table, and we can just do a PC32 reloc instead.  */
	  h->plt.offset = (bfd_vma) -1;
	  h->needs_plt = 0;
	}

      return TRUE;
    }
  else
    /* It's possible that we incorrectly decided a .plt reloc was
       needed for an R_X86_64_PC32 reloc to a non-function sym in
       check_relocs.  We can't decide accurately between function and
       non-function syms in check-relocs;  Objects loaded later in
       the link may change h->type.  So fix it now.  */
    h->plt.offset = (bfd_vma) -1;

  /* If this is a weak symbol, and there is a real definition, the
     processor independent code will have arranged for us to see the
     real definition first, and we can just use the same value.	 */
  if (h->u.weakdef != NULL)
    {
      BFD_ASSERT (h->u.weakdef->root.type == bfd_link_hash_defined
		  || h->u.weakdef->root.type == bfd_link_hash_defweak);
      h->root.u.def.section = h->u.weakdef->root.u.def.section;
      h->root.u.def.value = h->u.weakdef->root.u.def.value;
      if (ELIMINATE_COPY_RELOCS || info->nocopyreloc)
	h->non_got_ref = h->u.weakdef->non_got_ref;
      return TRUE;
    }

  /* This is a reference to a symbol defined by a dynamic object which
     is not a function.	 */

  /* If we are creating a shared library, we must presume that the
     only references to the symbol are via the global offset table.
     For such cases we need not do anything here; the relocations will
     be handled correctly by relocate_section.	*/
  if (info->shared)
    return TRUE;

  /* If there are no references to this symbol that do not use the
     GOT, we don't need to generate a copy reloc.  */
  if (!h->non_got_ref)
    return TRUE;

  /* If -z nocopyreloc was given, we won't generate them either.  */
  if (info->nocopyreloc)
    {
      h->non_got_ref = 0;
      return TRUE;
    }

  if (ELIMINATE_COPY_RELOCS)
    {
      struct elf64_x86_64_link_hash_entry * eh;
      struct elf64_x86_64_dyn_relocs *p;

      eh = (struct elf64_x86_64_link_hash_entry *) h;
      for (p = eh->dyn_relocs; p != NULL; p = p->next)
	{
	  s = p->sec->output_section;
	  if (s != NULL && (s->flags & SEC_READONLY) != 0)
	    break;
	}

      /* If we didn't find any dynamic relocs in read-only sections, then
	 we'll be keeping the dynamic relocs and avoiding the copy reloc.  */
      if (p == NULL)
	{
	  h->non_got_ref = 0;
	  return TRUE;
	}
    }

  if (h->size == 0)
    {
      (*_bfd_error_handler) (_("dynamic variable `%s' is zero size"),
			     h->root.root.string);
      return TRUE;
    }

  /* We must allocate the symbol in our .dynbss section, which will
     become part of the .bss section of the executable.	 There will be
     an entry for this symbol in the .dynsym section.  The dynamic
     object will contain position independent code, so all references
     from the dynamic object to this symbol will go through the global
     offset table.  The dynamic linker will use the .dynsym entry to
     determine the address it must put in the global offset table, so
     both the dynamic object and the regular object will refer to the
     same memory location for the variable.  */

  htab = elf64_x86_64_hash_table (info);

  /* We must generate a R_X86_64_COPY reloc to tell the dynamic linker
     to copy the initial value out of the dynamic object and into the
     runtime process image.  */
  if ((h->root.u.def.section->flags & SEC_ALLOC) != 0)
    {
      htab->srelbss->size += sizeof (Elf64_External_Rela);
      h->needs_copy = 1;
    }

  s = htab->sdynbss;

  return _bfd_elf_adjust_dynamic_copy (h, s);
}

/* Allocate space in .plt, .got and associated reloc sections for
   dynamic relocs.  */

static bfd_boolean
allocate_dynrelocs (struct elf_link_hash_entry *h, void * inf)
{
  struct bfd_link_info *info;
  struct elf64_x86_64_link_hash_table *htab;
  struct elf64_x86_64_link_hash_entry *eh;
  struct elf64_x86_64_dyn_relocs *p;

  if (h->root.type == bfd_link_hash_indirect)
    return TRUE;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  info = (struct bfd_link_info *) inf;
  htab = elf64_x86_64_hash_table (info);

  if (htab->elf.dynamic_sections_created
      && h->plt.refcount > 0)
    {
      /* Make sure this symbol is output as a dynamic symbol.
	 Undefined weak syms won't yet be marked as dynamic.  */
      if (h->dynindx == -1
	  && !h->forced_local)
	{
	  if (! bfd_elf_link_record_dynamic_symbol (info, h))
	    return FALSE;
	}

      if (info->shared
	  || WILL_CALL_FINISH_DYNAMIC_SYMBOL (1, 0, h))
	{
	  asection *s = htab->splt;

	  /* If this is the first .plt entry, make room for the special
	     first entry.  */
	  if (s->size == 0)
	    s->size += PLT_ENTRY_SIZE;

	  h->plt.offset = s->size;

	  /* If this symbol is not defined in a regular file, and we are
	     not generating a shared library, then set the symbol to this
	     location in the .plt.  This is required to make function
	     pointers compare as equal between the normal executable and
	     the shared library.  */
	  if (! info->shared
	      && !h->def_regular)
	    {
	      h->root.u.def.section = s;
	      h->root.u.def.value = h->plt.offset;
	    }

	  /* Make room for this entry.  */
	  s->size += PLT_ENTRY_SIZE;

	  /* We also need to make an entry in the .got.plt section, which
	     will be placed in the .got section by the linker script.  */
	  htab->sgotplt->size += GOT_ENTRY_SIZE;

	  /* We also need to make an entry in the .rela.plt section.  */
	  htab->srelplt->size += sizeof (Elf64_External_Rela);
	  htab->srelplt->reloc_count++;
	}
      else
	{
	  h->plt.offset = (bfd_vma) -1;
	  h->needs_plt = 0;
	}
    }
  else
    {
      h->plt.offset = (bfd_vma) -1;
      h->needs_plt = 0;
    }

  eh = (struct elf64_x86_64_link_hash_entry *) h;
  eh->tlsdesc_got = (bfd_vma) -1;

  /* If R_X86_64_GOTTPOFF symbol is now local to the binary,
     make it a R_X86_64_TPOFF32 requiring no GOT entry.  */
  if (h->got.refcount > 0
      && !info->shared
      && h->dynindx == -1
      && elf64_x86_64_hash_entry (h)->tls_type == GOT_TLS_IE)
    h->got.offset = (bfd_vma) -1;
  else if (h->got.refcount > 0)
    {
      asection *s;
      bfd_boolean dyn;
      int tls_type = elf64_x86_64_hash_entry (h)->tls_type;

      /* Make sure this symbol is output as a dynamic symbol.
	 Undefined weak syms won't yet be marked as dynamic.  */
      if (h->dynindx == -1
	  && !h->forced_local)
	{
	  if (! bfd_elf_link_record_dynamic_symbol (info, h))
	    return FALSE;
	}

      if (GOT_TLS_GDESC_P (tls_type))
	{
	  eh->tlsdesc_got = htab->sgotplt->size
	    - elf64_x86_64_compute_jump_table_size (htab);
	  htab->sgotplt->size += 2 * GOT_ENTRY_SIZE;
	  h->got.offset = (bfd_vma) -2;
	}
      if (! GOT_TLS_GDESC_P (tls_type)
	  || GOT_TLS_GD_P (tls_type))
	{
	  s = htab->sgot;
	  h->got.offset = s->size;
	  s->size += GOT_ENTRY_SIZE;
	  if (GOT_TLS_GD_P (tls_type))
	    s->size += GOT_ENTRY_SIZE;
	}
      dyn = htab->elf.dynamic_sections_created;
      /* R_X86_64_TLSGD needs one dynamic relocation if local symbol
	 and two if global.
	 R_X86_64_GOTTPOFF needs one dynamic relocation.  */
      if ((GOT_TLS_GD_P (tls_type) && h->dynindx == -1)
	  || tls_type == GOT_TLS_IE)
	htab->srelgot->size += sizeof (Elf64_External_Rela);
      else if (GOT_TLS_GD_P (tls_type))
	htab->srelgot->size += 2 * sizeof (Elf64_External_Rela);
      else if (! GOT_TLS_GDESC_P (tls_type)
	       && (ELF_ST_VISIBILITY (h->other) == STV_DEFAULT
		   || h->root.type != bfd_link_hash_undefweak)
	       && (info->shared
		   || WILL_CALL_FINISH_DYNAMIC_SYMBOL (dyn, 0, h)))
	htab->srelgot->size += sizeof (Elf64_External_Rela);
      if (GOT_TLS_GDESC_P (tls_type))
	{
	  htab->srelplt->size += sizeof (Elf64_External_Rela);
	  htab->tlsdesc_plt = (bfd_vma) -1;
	}
    }
  else
    h->got.offset = (bfd_vma) -1;

  if (eh->dyn_relocs == NULL)
    return TRUE;

  /* In the shared -Bsymbolic case, discard space allocated for
     dynamic pc-relative relocs against symbols which turn out to be
     defined in regular objects.  For the normal shared case, discard
     space for pc-relative relocs that have become local due to symbol
     visibility changes.  */

  if (info->shared)
    {
      /* Relocs that use pc_count are those that appear on a call
	 insn, or certain REL relocs that can generated via assembly.
	 We want calls to protected symbols to resolve directly to the
	 function rather than going via the plt.  If people want
	 function pointer comparisons to work as expected then they
	 should avoid writing weird assembly.  */
      if (SYMBOL_CALLS_LOCAL (info, h))
	{
	  struct elf64_x86_64_dyn_relocs **pp;

	  for (pp = &eh->dyn_relocs; (p = *pp) != NULL; )
	    {
	      p->count -= p->pc_count;
	      p->pc_count = 0;
	      if (p->count == 0)
		*pp = p->next;
	      else
		pp = &p->next;
	    }
	}

      /* Also discard relocs on undefined weak syms with non-default
	 visibility.  */
      if (eh->dyn_relocs != NULL
	  && h->root.type == bfd_link_hash_undefweak)
	{
	  if (ELF_ST_VISIBILITY (h->other) != STV_DEFAULT)
	    eh->dyn_relocs = NULL;

	  /* Make sure undefined weak symbols are output as a dynamic
	     symbol in PIEs.  */
	  else if (h->dynindx == -1
		   && !h->forced_local)
	    {
	      if (! bfd_elf_link_record_dynamic_symbol (info, h))
		return FALSE;
	    }
	}
    }
  else if (ELIMINATE_COPY_RELOCS)
    {
      /* For the non-shared case, discard space for relocs against
	 symbols which turn out to need copy relocs or are not
	 dynamic.  */

      if (!h->non_got_ref
	  && ((h->def_dynamic
	       && !h->def_regular)
	      || (htab->elf.dynamic_sections_created
		  && (h->root.type == bfd_link_hash_undefweak
		      || h->root.type == bfd_link_hash_undefined))))
	{
	  /* Make sure this symbol is output as a dynamic symbol.
	     Undefined weak syms won't yet be marked as dynamic.  */
	  if (h->dynindx == -1
	      && !h->forced_local)
	    {
	      if (! bfd_elf_link_record_dynamic_symbol (info, h))
		return FALSE;
	    }

	  /* If that succeeded, we know we'll be keeping all the
	     relocs.  */
	  if (h->dynindx != -1)
	    goto keep;
	}

      eh->dyn_relocs = NULL;

    keep: ;
    }

  /* Finally, allocate space.  */
  for (p = eh->dyn_relocs; p != NULL; p = p->next)
    {
      asection *sreloc = elf_section_data (p->sec)->sreloc;
      sreloc->size += p->count * sizeof (Elf64_External_Rela);
    }

  return TRUE;
}

/* Find any dynamic relocs that apply to read-only sections.  */

static bfd_boolean
readonly_dynrelocs (struct elf_link_hash_entry *h, void * inf)
{
  struct elf64_x86_64_link_hash_entry *eh;
  struct elf64_x86_64_dyn_relocs *p;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  eh = (struct elf64_x86_64_link_hash_entry *) h;
  for (p = eh->dyn_relocs; p != NULL; p = p->next)
    {
      asection *s = p->sec->output_section;

      if (s != NULL && (s->flags & SEC_READONLY) != 0)
	{
	  struct bfd_link_info *info = (struct bfd_link_info *) inf;

	  info->flags |= DF_TEXTREL;

	  /* Not an error, just cut short the traversal.  */
	  return FALSE;
	}
    }
  return TRUE;
}

/* Set the sizes of the dynamic sections.  */

static bfd_boolean
elf64_x86_64_size_dynamic_sections (bfd *output_bfd ATTRIBUTE_UNUSED,
				    struct bfd_link_info *info)
{
  struct elf64_x86_64_link_hash_table *htab;
  bfd *dynobj;
  asection *s;
  bfd_boolean relocs;
  bfd *ibfd;

  htab = elf64_x86_64_hash_table (info);
  dynobj = htab->elf.dynobj;
  if (dynobj == NULL)
    abort ();

  if (htab->elf.dynamic_sections_created)
    {
      /* Set the contents of the .interp section to the interpreter.  */
      if (info->executable)
	{
	  s = bfd_get_section_by_name (dynobj, ".interp");
	  if (s == NULL)
	    abort ();
	  s->size = sizeof ELF_DYNAMIC_INTERPRETER;
	  s->contents = (unsigned char *) ELF_DYNAMIC_INTERPRETER;
	}
    }

  /* Set up .got offsets for local syms, and space for local dynamic
     relocs.  */
  for (ibfd = info->input_bfds; ibfd != NULL; ibfd = ibfd->link_next)
    {
      bfd_signed_vma *local_got;
      bfd_signed_vma *end_local_got;
      char *local_tls_type;
      bfd_vma *local_tlsdesc_gotent;
      bfd_size_type locsymcount;
      Elf_Internal_Shdr *symtab_hdr;
      asection *srel;

      if (bfd_get_flavour (ibfd) != bfd_target_elf_flavour)
	continue;

      for (s = ibfd->sections; s != NULL; s = s->next)
	{
	  struct elf64_x86_64_dyn_relocs *p;

	  for (p = (struct elf64_x86_64_dyn_relocs *)
		    (elf_section_data (s)->local_dynrel);
	       p != NULL;
	       p = p->next)
	    {
	      if (!bfd_is_abs_section (p->sec)
		  && bfd_is_abs_section (p->sec->output_section))
		{
		  /* Input section has been discarded, either because
		     it is a copy of a linkonce section or due to
		     linker script /DISCARD/, so we'll be discarding
		     the relocs too.  */
		}
	      else if (p->count != 0)
		{
		  srel = elf_section_data (p->sec)->sreloc;
		  srel->size += p->count * sizeof (Elf64_External_Rela);
		  if ((p->sec->output_section->flags & SEC_READONLY) != 0)
		    info->flags |= DF_TEXTREL;

		}
	    }
	}

      local_got = elf_local_got_refcounts (ibfd);
      if (!local_got)
	continue;

      symtab_hdr = &elf_tdata (ibfd)->symtab_hdr;
      locsymcount = symtab_hdr->sh_info;
      end_local_got = local_got + locsymcount;
      local_tls_type = elf64_x86_64_local_got_tls_type (ibfd);
      local_tlsdesc_gotent = elf64_x86_64_local_tlsdesc_gotent (ibfd);
      s = htab->sgot;
      srel = htab->srelgot;
      for (; local_got < end_local_got;
	   ++local_got, ++local_tls_type, ++local_tlsdesc_gotent)
	{
	  *local_tlsdesc_gotent = (bfd_vma) -1;
	  if (*local_got > 0)
	    {
	      if (GOT_TLS_GDESC_P (*local_tls_type))
		{
		  *local_tlsdesc_gotent = htab->sgotplt->size
		    - elf64_x86_64_compute_jump_table_size (htab);
		  htab->sgotplt->size += 2 * GOT_ENTRY_SIZE;
		  *local_got = (bfd_vma) -2;
		}
	      if (! GOT_TLS_GDESC_P (*local_tls_type)
		  || GOT_TLS_GD_P (*local_tls_type))
		{
		  *local_got = s->size;
		  s->size += GOT_ENTRY_SIZE;
		  if (GOT_TLS_GD_P (*local_tls_type))
		    s->size += GOT_ENTRY_SIZE;
		}
	      if (info->shared
		  || GOT_TLS_GD_ANY_P (*local_tls_type)
		  || *local_tls_type == GOT_TLS_IE)
		{
		  if (GOT_TLS_GDESC_P (*local_tls_type))
		    {
		      htab->srelplt->size += sizeof (Elf64_External_Rela);
		      htab->tlsdesc_plt = (bfd_vma) -1;
		    }
		  if (! GOT_TLS_GDESC_P (*local_tls_type)
		      || GOT_TLS_GD_P (*local_tls_type))
		    srel->size += sizeof (Elf64_External_Rela);
		}
	    }
	  else
	    *local_got = (bfd_vma) -1;
	}
    }

  if (htab->tls_ld_got.refcount > 0)
    {
      /* Allocate 2 got entries and 1 dynamic reloc for R_X86_64_TLSLD
	 relocs.  */
      htab->tls_ld_got.offset = htab->sgot->size;
      htab->sgot->size += 2 * GOT_ENTRY_SIZE;
      htab->srelgot->size += sizeof (Elf64_External_Rela);
    }
  else
    htab->tls_ld_got.offset = -1;

  /* Allocate global sym .plt and .got entries, and space for global
     sym dynamic relocs.  */
  elf_link_hash_traverse (&htab->elf, allocate_dynrelocs, (PTR) info);

  /* For every jump slot reserved in the sgotplt, reloc_count is
     incremented.  However, when we reserve space for TLS descriptors,
     it's not incremented, so in order to compute the space reserved
     for them, it suffices to multiply the reloc count by the jump
     slot size.  */
  if (htab->srelplt)
    htab->sgotplt_jump_table_size
      = elf64_x86_64_compute_jump_table_size (htab);

  if (htab->tlsdesc_plt)
    {
      /* If we're not using lazy TLS relocations, don't generate the
	 PLT and GOT entries they require.  */
      if ((info->flags & DF_BIND_NOW))
	htab->tlsdesc_plt = 0;
      else
	{
	  htab->tlsdesc_got = htab->sgot->size;
	  htab->sgot->size += GOT_ENTRY_SIZE;
	  /* Reserve room for the initial entry.
	     FIXME: we could probably do away with it in this case.  */
	  if (htab->splt->size == 0)
	    htab->splt->size += PLT_ENTRY_SIZE;
	  htab->tlsdesc_plt = htab->splt->size;
	  htab->splt->size += PLT_ENTRY_SIZE;
	}
    }

  /* We now have determined the sizes of the various dynamic sections.
     Allocate memory for them.  */
  relocs = FALSE;
  for (s = dynobj->sections; s != NULL; s = s->next)
    {
      if ((s->flags & SEC_LINKER_CREATED) == 0)
	continue;

      if (s == htab->splt
	  || s == htab->sgot
	  || s == htab->sgotplt
	  || s == htab->sdynbss)
	{
	  /* Strip this section if we don't need it; see the
	     comment below.  */
	}
      else if (CONST_STRNEQ (bfd_get_section_name (dynobj, s), ".rela"))
	{
	  if (s->size != 0 && s != htab->srelplt)
	    relocs = TRUE;

	  /* We use the reloc_count field as a counter if we need
	     to copy relocs into the output file.  */
	  if (s != htab->srelplt)
	    s->reloc_count = 0;
	}
      else
	{
	  /* It's not one of our sections, so don't allocate space.  */
	  continue;
	}

      if (s->size == 0)
	{
	  /* If we don't need this section, strip it from the
	     output file.  This is mostly to handle .rela.bss and
	     .rela.plt.  We must create both sections in
	     create_dynamic_sections, because they must be created
	     before the linker maps input sections to output
	     sections.  The linker does that before
	     adjust_dynamic_symbol is called, and it is that
	     function which decides whether anything needs to go
	     into these sections.  */

	  s->flags |= SEC_EXCLUDE;
	  continue;
	}

      if ((s->flags & SEC_HAS_CONTENTS) == 0)
	continue;

      /* Allocate memory for the section contents.  We use bfd_zalloc
	 here in case unused entries are not reclaimed before the
	 section's contents are written out.  This should not happen,
	 but this way if it does, we get a R_X86_64_NONE reloc instead
	 of garbage.  */
      s->contents = (bfd_byte *) bfd_zalloc (dynobj, s->size);
      if (s->contents == NULL)
	return FALSE;
    }

  if (htab->elf.dynamic_sections_created)
    {
      /* Add some entries to the .dynamic section.  We fill in the
	 values later, in elf64_x86_64_finish_dynamic_sections, but we
	 must add the entries now so that we get the correct size for
	 the .dynamic section.	The DT_DEBUG entry is filled in by the
	 dynamic linker and used by the debugger.  */
#define add_dynamic_entry(TAG, VAL) \
  _bfd_elf_add_dynamic_entry (info, TAG, VAL)

      if (info->executable)
	{
	  if (!add_dynamic_entry (DT_DEBUG, 0))
	    return FALSE;
	}

      if (htab->splt->size != 0)
	{
	  if (!add_dynamic_entry (DT_PLTGOT, 0)
	      || !add_dynamic_entry (DT_PLTRELSZ, 0)
	      || !add_dynamic_entry (DT_PLTREL, DT_RELA)
	      || !add_dynamic_entry (DT_JMPREL, 0))
	    return FALSE;

	  if (htab->tlsdesc_plt
	      && (!add_dynamic_entry (DT_TLSDESC_PLT, 0)
		  || !add_dynamic_entry (DT_TLSDESC_GOT, 0)))
	    return FALSE;
	}

      if (relocs)
	{
	  if (!add_dynamic_entry (DT_RELA, 0)
	      || !add_dynamic_entry (DT_RELASZ, 0)
	      || !add_dynamic_entry (DT_RELAENT, sizeof (Elf64_External_Rela)))
	    return FALSE;

	  /* If any dynamic relocs apply to a read-only section,
	     then we need a DT_TEXTREL entry.  */
	  if ((info->flags & DF_TEXTREL) == 0)
	    elf_link_hash_traverse (&htab->elf, readonly_dynrelocs,
				    (PTR) info);

	  if ((info->flags & DF_TEXTREL) != 0)
	    {
	      if (!add_dynamic_entry (DT_TEXTREL, 0))
		return FALSE;
	    }
	}
    }
#undef add_dynamic_entry

  return TRUE;
}

static bfd_boolean
elf64_x86_64_always_size_sections (bfd *output_bfd,
				   struct bfd_link_info *info)
{
  asection *tls_sec = elf_hash_table (info)->tls_sec;

  if (tls_sec)
    {
      struct elf_link_hash_entry *tlsbase;

      tlsbase = elf_link_hash_lookup (elf_hash_table (info),
				      "_TLS_MODULE_BASE_",
				      FALSE, FALSE, FALSE);

      if (tlsbase && tlsbase->type == STT_TLS)
	{
	  struct bfd_link_hash_entry *bh = NULL;
	  const struct elf_backend_data *bed
	    = get_elf_backend_data (output_bfd);

	  if (!(_bfd_generic_link_add_one_symbol
		(info, output_bfd, "_TLS_MODULE_BASE_", BSF_LOCAL,
		 tls_sec, 0, NULL, FALSE,
		 bed->collect, &bh)))
	    return FALSE;
	  tlsbase = (struct elf_link_hash_entry *)bh;
	  tlsbase->def_regular = 1;
	  tlsbase->other = STV_HIDDEN;
	  (*bed->elf_backend_hide_symbol) (info, tlsbase, TRUE);
	}
    }

  return TRUE;
}

/* Return the base VMA address which should be subtracted from real addresses
   when resolving @dtpoff relocation.
   This is PT_TLS segment p_vaddr.  */

static bfd_vma
dtpoff_base (struct bfd_link_info *info)
{
  /* If tls_sec is NULL, we should have signalled an error already.  */
  if (elf_hash_table (info)->tls_sec == NULL)
    return 0;
  return elf_hash_table (info)->tls_sec->vma;
}

/* Return the relocation value for @tpoff relocation
   if STT_TLS virtual address is ADDRESS.  */

static bfd_vma
tpoff (struct bfd_link_info *info, bfd_vma address)
{
  struct elf_link_hash_table *htab = elf_hash_table (info);

  /* If tls_segment is NULL, we should have signalled an error already.  */
  if (htab->tls_sec == NULL)
    return 0;
  return address - htab->tls_size - htab->tls_sec->vma;
}

/* Is the instruction before OFFSET in CONTENTS a 32bit relative
   branch?  */

static bfd_boolean
is_32bit_relative_branch (bfd_byte *contents, bfd_vma offset)
{
  /* Opcode		Instruction
     0xe8		call
     0xe9		jump
     0x0f 0x8x		conditional jump */
  return ((offset > 0
	   && (contents [offset - 1] == 0xe8
	       || contents [offset - 1] == 0xe9))
	  || (offset > 1
	      && contents [offset - 2] == 0x0f
	      && (contents [offset - 1] & 0xf0) == 0x80));
}

/* Relocate an x86_64 ELF section.  */

static bfd_boolean
elf64_x86_64_relocate_section (bfd *output_bfd, struct bfd_link_info *info,
			       bfd *input_bfd, asection *input_section,
			       bfd_byte *contents, Elf_Internal_Rela *relocs,
			       Elf_Internal_Sym *local_syms,
			       asection **local_sections)
{
  struct elf64_x86_64_link_hash_table *htab;
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  bfd_vma *local_got_offsets;
  bfd_vma *local_tlsdesc_gotents;
  Elf_Internal_Rela *rel;
  Elf_Internal_Rela *relend;

  htab = elf64_x86_64_hash_table (info);
  symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (input_bfd);
  local_got_offsets = elf_local_got_offsets (input_bfd);
  local_tlsdesc_gotents = elf64_x86_64_local_tlsdesc_gotent (input_bfd);

  rel = relocs;
  relend = relocs + input_section->reloc_count;
  for (; rel < relend; rel++)
    {
      unsigned int r_type;
      reloc_howto_type *howto;
      unsigned long r_symndx;
      struct elf_link_hash_entry *h;
      Elf_Internal_Sym *sym;
      asection *sec;
      bfd_vma off, offplt;
      bfd_vma relocation;
      bfd_boolean unresolved_reloc;
      bfd_reloc_status_type r;
      int tls_type;

      r_type = ELF64_R_TYPE (rel->r_info);
      if (r_type == (int) R_X86_64_GNU_VTINHERIT
	  || r_type == (int) R_X86_64_GNU_VTENTRY)
	continue;

      if (r_type >= R_X86_64_max)
	{
	  bfd_set_error (bfd_error_bad_value);
	  return FALSE;
	}

      howto = x86_64_elf_howto_table + r_type;
      r_symndx = ELF64_R_SYM (rel->r_info);
      h = NULL;
      sym = NULL;
      sec = NULL;
      unresolved_reloc = FALSE;
      if (r_symndx < symtab_hdr->sh_info)
	{
	  sym = local_syms + r_symndx;
	  sec = local_sections[r_symndx];

	  relocation = _bfd_elf_rela_local_sym (output_bfd, sym, &sec, rel);
	}
      else
	{
	  bfd_boolean warned;

	  RELOC_FOR_GLOBAL_SYMBOL (info, input_bfd, input_section, rel,
				   r_symndx, symtab_hdr, sym_hashes,
				   h, sec, relocation,
				   unresolved_reloc, warned);
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
	continue;

      /* When generating a shared object, the relocations handled here are
	 copied into the output file to be resolved at run time.  */
      switch (r_type)
	{
	asection *base_got;
	case R_X86_64_GOT32:
	case R_X86_64_GOT64:
	  /* Relocation is to the entry for this symbol in the global
	     offset table.  */
	case R_X86_64_GOTPCREL:
	case R_X86_64_GOTPCREL64:
	  /* Use global offset table entry as symbol value.  */
	case R_X86_64_GOTPLT64:
	  /* This is the same as GOT64 for relocation purposes, but
	     indicates the existence of a PLT entry.  The difficulty is,
	     that we must calculate the GOT slot offset from the PLT
	     offset, if this symbol got a PLT entry (it was global).
	     Additionally if it's computed from the PLT entry, then that
	     GOT offset is relative to .got.plt, not to .got.  */
	  base_got = htab->sgot;

	  if (htab->sgot == NULL)
	    abort ();

	  if (h != NULL)
	    {
	      bfd_boolean dyn;

	      off = h->got.offset;
	      if (h->needs_plt
	          && h->plt.offset != (bfd_vma)-1
		  && off == (bfd_vma)-1)
		{
		  /* We can't use h->got.offset here to save
		     state, or even just remember the offset, as
		     finish_dynamic_symbol would use that as offset into
		     .got.  */
		  bfd_vma plt_index = h->plt.offset / PLT_ENTRY_SIZE - 1;
		  off = (plt_index + 3) * GOT_ENTRY_SIZE;
		  base_got = htab->sgotplt;
		}

	      dyn = htab->elf.dynamic_sections_created;

	      if (! WILL_CALL_FINISH_DYNAMIC_SYMBOL (dyn, info->shared, h)
		  || (info->shared
		      && SYMBOL_REFERENCES_LOCAL (info, h))
		  || (ELF_ST_VISIBILITY (h->other)
		      && h->root.type == bfd_link_hash_undefweak))
		{
		  /* This is actually a static link, or it is a -Bsymbolic
		     link and the symbol is defined locally, or the symbol
		     was forced to be local because of a version file.	We
		     must initialize this entry in the global offset table.
		     Since the offset must always be a multiple of 8, we
		     use the least significant bit to record whether we
		     have initialized it already.

		     When doing a dynamic link, we create a .rela.got
		     relocation entry to initialize the value.	This is
		     done in the finish_dynamic_symbol routine.	 */
		  if ((off & 1) != 0)
		    off &= ~1;
		  else
		    {
		      bfd_put_64 (output_bfd, relocation,
				  base_got->contents + off);
		      /* Note that this is harmless for the GOTPLT64 case,
		         as -1 | 1 still is -1.  */
		      h->got.offset |= 1;
		    }
		}
	      else
		unresolved_reloc = FALSE;
	    }
	  else
	    {
	      if (local_got_offsets == NULL)
		abort ();

	      off = local_got_offsets[r_symndx];

	      /* The offset must always be a multiple of 8.  We use
		 the least significant bit to record whether we have
		 already generated the necessary reloc.	 */
	      if ((off & 1) != 0)
		off &= ~1;
	      else
		{
		  bfd_put_64 (output_bfd, relocation,
			      base_got->contents + off);

		  if (info->shared)
		    {
		      asection *s;
		      Elf_Internal_Rela outrel;
		      bfd_byte *loc;

		      /* We need to generate a R_X86_64_RELATIVE reloc
			 for the dynamic linker.  */
		      s = htab->srelgot;
		      if (s == NULL)
			abort ();

		      outrel.r_offset = (base_got->output_section->vma
					 + base_got->output_offset
					 + off);
		      outrel.r_info = ELF64_R_INFO (0, R_X86_64_RELATIVE);
		      outrel.r_addend = relocation;
		      loc = s->contents;
		      loc += s->reloc_count++ * sizeof (Elf64_External_Rela);
		      bfd_elf64_swap_reloca_out (output_bfd, &outrel, loc);
		    }

		  local_got_offsets[r_symndx] |= 1;
		}
	    }

	  if (off >= (bfd_vma) -2)
	    abort ();

	  relocation = base_got->output_section->vma
		       + base_got->output_offset + off;
	  if (r_type != R_X86_64_GOTPCREL && r_type != R_X86_64_GOTPCREL64)
	    relocation -= htab->sgotplt->output_section->vma
			  - htab->sgotplt->output_offset;

	  break;

	case R_X86_64_GOTOFF64:
	  /* Relocation is relative to the start of the global offset
	     table.  */

	  /* Check to make sure it isn't a protected function symbol
	     for shared library since it may not be local when used
	     as function address.  */
	  if (info->shared
	      && h
	      && h->def_regular
	      && h->type == STT_FUNC
	      && ELF_ST_VISIBILITY (h->other) == STV_PROTECTED)
	    {
	      (*_bfd_error_handler)
		(_("%B: relocation R_X86_64_GOTOFF64 against protected function `%s' can not be used when making a shared object"),
		 input_bfd, h->root.root.string);
	      bfd_set_error (bfd_error_bad_value);
	      return FALSE;
	    }

	  /* Note that sgot is not involved in this
	     calculation.  We always want the start of .got.plt.  If we
	     defined _GLOBAL_OFFSET_TABLE_ in a different way, as is
	     permitted by the ABI, we might have to change this
	     calculation.  */
	  relocation -= htab->sgotplt->output_section->vma
			+ htab->sgotplt->output_offset;
	  break;

	case R_X86_64_GOTPC32:
	case R_X86_64_GOTPC64:
	  /* Use global offset table as symbol value.  */
	  relocation = htab->sgotplt->output_section->vma
		       + htab->sgotplt->output_offset;
	  unresolved_reloc = FALSE;
	  break;

	case R_X86_64_PLTOFF64:
	  /* Relocation is PLT entry relative to GOT.  For local
	     symbols it's the symbol itself relative to GOT.  */
          if (h != NULL
	      /* See PLT32 handling.  */
	      && h->plt.offset != (bfd_vma) -1
	      && htab->splt != NULL)
	    {
	      relocation = (htab->splt->output_section->vma
			    + htab->splt->output_offset
			    + h->plt.offset);
	      unresolved_reloc = FALSE;
	    }

	  relocation -= htab->sgotplt->output_section->vma
			+ htab->sgotplt->output_offset;
	  break;

	case R_X86_64_PLT32:
	  /* Relocation is to the entry for this symbol in the
	     procedure linkage table.  */

	  /* Resolve a PLT32 reloc against a local symbol directly,
	     without using the procedure linkage table.	 */
	  if (h == NULL)
	    break;

	  if (h->plt.offset == (bfd_vma) -1
	      || htab->splt == NULL)
	    {
	      /* We didn't make a PLT entry for this symbol.  This
		 happens when statically linking PIC code, or when
		 using -Bsymbolic.  */
	      break;
	    }

	  relocation = (htab->splt->output_section->vma
			+ htab->splt->output_offset
			+ h->plt.offset);
	  unresolved_reloc = FALSE;
	  break;

	case R_X86_64_PC8:
	case R_X86_64_PC16:
	case R_X86_64_PC32:
	  if (info->shared
	      && !SYMBOL_REFERENCES_LOCAL (info, h)
	      && (input_section->flags & SEC_ALLOC) != 0
	      && (input_section->flags & SEC_READONLY) != 0
	      && (!h->def_regular
		  || r_type != R_X86_64_PC32
		  || h->type != STT_FUNC
		  || ELF_ST_VISIBILITY (h->other) != STV_PROTECTED
		  || !is_32bit_relative_branch (contents,
						rel->r_offset)))
	    {
	      if (h->def_regular
		  && r_type == R_X86_64_PC32
		  && h->type == STT_FUNC
		  && ELF_ST_VISIBILITY (h->other) == STV_PROTECTED)
		(*_bfd_error_handler)
		   (_("%B: relocation R_X86_64_PC32 against protected function `%s' can not be used when making a shared object"),
		    input_bfd, h->root.root.string);
	      else
		(*_bfd_error_handler)
		  (_("%B: relocation %s against `%s' can not be used when making a shared object; recompile with -fPIC"),
		   input_bfd, x86_64_elf_howto_table[r_type].name,
		   h->root.root.string);
	      bfd_set_error (bfd_error_bad_value);
	      return FALSE;
	    }
	  /* Fall through.  */

	case R_X86_64_8:
	case R_X86_64_16:
	case R_X86_64_32:
	case R_X86_64_PC64:
	case R_X86_64_64:
	  /* FIXME: The ABI says the linker should make sure the value is
	     the same when it's zeroextended to 64 bit.	 */

	  if ((input_section->flags & SEC_ALLOC) == 0)
	    break;

	  if ((info->shared
	       && (h == NULL
		   || ELF_ST_VISIBILITY (h->other) == STV_DEFAULT
		   || h->root.type != bfd_link_hash_undefweak)
	       && ((r_type != R_X86_64_PC8
		    && r_type != R_X86_64_PC16
		    && r_type != R_X86_64_PC32
		    && r_type != R_X86_64_PC64)
		   || !SYMBOL_CALLS_LOCAL (info, h)))
	      || (ELIMINATE_COPY_RELOCS
		  && !info->shared
		  && h != NULL
		  && h->dynindx != -1
		  && !h->non_got_ref
		  && ((h->def_dynamic
		       && !h->def_regular)
		      || h->root.type == bfd_link_hash_undefweak
		      || h->root.type == bfd_link_hash_undefined)))
	    {
	      Elf_Internal_Rela outrel;
	      bfd_byte *loc;
	      bfd_boolean skip, relocate;
	      asection *sreloc;

	      /* When generating a shared object, these relocations
		 are copied into the output file to be resolved at run
		 time.	*/
	      skip = FALSE;
	      relocate = FALSE;

	      outrel.r_offset =
		_bfd_elf_section_offset (output_bfd, info, input_section,
					 rel->r_offset);
	      if (outrel.r_offset == (bfd_vma) -1)
		skip = TRUE;
	      else if (outrel.r_offset == (bfd_vma) -2)
		skip = TRUE, relocate = TRUE;

	      outrel.r_offset += (input_section->output_section->vma
				  + input_section->output_offset);

	      if (skip)
		memset (&outrel, 0, sizeof outrel);

	      /* h->dynindx may be -1 if this symbol was marked to
		 become local.  */
	      else if (h != NULL
		       && h->dynindx != -1
		       && (r_type == R_X86_64_PC8
			   || r_type == R_X86_64_PC16
			   || r_type == R_X86_64_PC32
			   || r_type == R_X86_64_PC64
			   || !info->shared
			   || !SYMBOLIC_BIND (info, h)
			   || !h->def_regular))
		{
		  outrel.r_info = ELF64_R_INFO (h->dynindx, r_type);
		  outrel.r_addend = rel->r_addend;
		}
	      else
		{
		  /* This symbol is local, or marked to become local.  */
		  if (r_type == R_X86_64_64)
		    {
		      relocate = TRUE;
		      outrel.r_info = ELF64_R_INFO (0, R_X86_64_RELATIVE);
		      outrel.r_addend = relocation + rel->r_addend;
		    }
		  else
		    {
		      long sindx;

		      if (bfd_is_abs_section (sec))
			sindx = 0;
		      else if (sec == NULL || sec->owner == NULL)
			{
			  bfd_set_error (bfd_error_bad_value);
			  return FALSE;
			}
		      else
			{
			  asection *osec;

			  /* We are turning this relocation into one
			     against a section symbol.  It would be
			     proper to subtract the symbol's value,
			     osec->vma, from the emitted reloc addend,
			     but ld.so expects buggy relocs.  */
			  osec = sec->output_section;
			  sindx = elf_section_data (osec)->dynindx;
			  if (sindx == 0)
			    {
			      asection *oi = htab->elf.text_index_section;
			      sindx = elf_section_data (oi)->dynindx;
			    }
			  BFD_ASSERT (sindx != 0);
			}

		      outrel.r_info = ELF64_R_INFO (sindx, r_type);
		      outrel.r_addend = relocation + rel->r_addend;
		    }
		}

	      sreloc = elf_section_data (input_section)->sreloc;
	      if (sreloc == NULL)
		abort ();

	      loc = sreloc->contents;
	      loc += sreloc->reloc_count++ * sizeof (Elf64_External_Rela);
	      bfd_elf64_swap_reloca_out (output_bfd, &outrel, loc);

	      /* If this reloc is against an external symbol, we do
		 not want to fiddle with the addend.  Otherwise, we
		 need to include the symbol value so that it becomes
		 an addend for the dynamic reloc.  */
	      if (! relocate)
		continue;
	    }

	  break;

	case R_X86_64_TLSGD:
	case R_X86_64_GOTPC32_TLSDESC:
	case R_X86_64_TLSDESC_CALL:
	case R_X86_64_GOTTPOFF:
	  r_type = elf64_x86_64_tls_transition (info, r_type, h == NULL);
	  tls_type = GOT_UNKNOWN;
	  if (h == NULL && local_got_offsets)
	    tls_type = elf64_x86_64_local_got_tls_type (input_bfd) [r_symndx];
	  else if (h != NULL)
	    {
	      tls_type = elf64_x86_64_hash_entry (h)->tls_type;
	      if (!info->shared && h->dynindx == -1 && tls_type == GOT_TLS_IE)
		r_type = R_X86_64_TPOFF32;
	    }
	  if (r_type == R_X86_64_TLSGD
	      || r_type == R_X86_64_GOTPC32_TLSDESC
	      || r_type == R_X86_64_TLSDESC_CALL)
	    {
	      if (tls_type == GOT_TLS_IE)
		r_type = R_X86_64_GOTTPOFF;
	    }

	  if (r_type == R_X86_64_TPOFF32)
	    {
	      BFD_ASSERT (! unresolved_reloc);
	      if (ELF64_R_TYPE (rel->r_info) == R_X86_64_TLSGD)
		{
		  unsigned int i;
		  static unsigned char tlsgd[8]
		    = { 0x66, 0x48, 0x8d, 0x3d, 0x66, 0x66, 0x48, 0xe8 };

		  /* GD->LE transition.
		     .byte 0x66; leaq foo@tlsgd(%rip), %rdi
		     .word 0x6666; rex64; call __tls_get_addr@plt
		     Change it into:
		     movq %fs:0, %rax
		     leaq foo@tpoff(%rax), %rax */
		  BFD_ASSERT (rel->r_offset >= 4);
		  for (i = 0; i < 4; i++)
		    BFD_ASSERT (bfd_get_8 (input_bfd,
					   contents + rel->r_offset - 4 + i)
				== tlsgd[i]);
		  BFD_ASSERT (rel->r_offset + 12 <= input_section->size);
		  for (i = 0; i < 4; i++)
		    BFD_ASSERT (bfd_get_8 (input_bfd,
					   contents + rel->r_offset + 4 + i)
				== tlsgd[i+4]);
		  BFD_ASSERT (rel + 1 < relend);
		  BFD_ASSERT (ELF64_R_TYPE (rel[1].r_info) == R_X86_64_PLT32);
		  memcpy (contents + rel->r_offset - 4,
			  "\x64\x48\x8b\x04\x25\0\0\0\0\x48\x8d\x80\0\0\0",
			  16);
		  bfd_put_32 (output_bfd, tpoff (info, relocation),
			      contents + rel->r_offset + 8);
		  /* Skip R_X86_64_PLT32.  */
		  rel++;
		  continue;
		}
	      else if (ELF64_R_TYPE (rel->r_info) == R_X86_64_GOTPC32_TLSDESC)
		{
		  /* GDesc -> LE transition.
		     It's originally something like:
		     leaq x@tlsdesc(%rip), %rax

		     Change it to:
		     movl $x@tpoff, %rax

		     Registers other than %rax may be set up here.  */

		  unsigned int val, type, type2;
		  bfd_vma roff;

		  /* First, make sure it's a leaq adding rip to a
		     32-bit offset into any register, although it's
		     probably almost always going to be rax.  */
		  roff = rel->r_offset;
		  BFD_ASSERT (roff >= 3);
		  type = bfd_get_8 (input_bfd, contents + roff - 3);
		  BFD_ASSERT ((type & 0xfb) == 0x48);
		  type2 = bfd_get_8 (input_bfd, contents + roff - 2);
		  BFD_ASSERT (type2 == 0x8d);
		  val = bfd_get_8 (input_bfd, contents + roff - 1);
		  BFD_ASSERT ((val & 0xc7) == 0x05);
		  BFD_ASSERT (roff + 4 <= input_section->size);

		  /* Now modify the instruction as appropriate.  */
		  bfd_put_8 (output_bfd, 0x48 | ((type >> 2) & 1),
			     contents + roff - 3);
		  bfd_put_8 (output_bfd, 0xc7, contents + roff - 2);
		  bfd_put_8 (output_bfd, 0xc0 | ((val >> 3) & 7),
			     contents + roff - 1);
		  bfd_put_32 (output_bfd, tpoff (info, relocation),
			      contents + roff);
		  continue;
		}
	      else if (ELF64_R_TYPE (rel->r_info) == R_X86_64_TLSDESC_CALL)
		{
		  /* GDesc -> LE transition.
		     It's originally:
		     call *(%rax)
		     Turn it into:
		     nop; nop.  */

		  unsigned int val, type;
		  bfd_vma roff;

		  /* First, make sure it's a call *(%rax).  */
		  roff = rel->r_offset;
		  BFD_ASSERT (roff + 2 <= input_section->size);
		  type = bfd_get_8 (input_bfd, contents + roff);
		  BFD_ASSERT (type == 0xff);
		  val = bfd_get_8 (input_bfd, contents + roff + 1);
		  BFD_ASSERT (val == 0x10);

		  /* Now modify the instruction as appropriate.  Use
		     xchg %ax,%ax instead of 2 nops.  */
		  bfd_put_8 (output_bfd, 0x66, contents + roff);
		  bfd_put_8 (output_bfd, 0x90, contents + roff + 1);
		  continue;
		}
	      else
		{
		  unsigned int val, type, reg;

		  /* IE->LE transition:
		     Originally it can be one of:
		     movq foo@gottpoff(%rip), %reg
		     addq foo@gottpoff(%rip), %reg
		     We change it into:
		     movq $foo, %reg
		     leaq foo(%reg), %reg
		     addq $foo, %reg.  */
		  BFD_ASSERT (rel->r_offset >= 3);
		  val = bfd_get_8 (input_bfd, contents + rel->r_offset - 3);
		  BFD_ASSERT (val == 0x48 || val == 0x4c);
		  type = bfd_get_8 (input_bfd, contents + rel->r_offset - 2);
		  BFD_ASSERT (type == 0x8b || type == 0x03);
		  reg = bfd_get_8 (input_bfd, contents + rel->r_offset - 1);
		  BFD_ASSERT ((reg & 0xc7) == 5);
		  reg >>= 3;
		  BFD_ASSERT (rel->r_offset + 4 <= input_section->size);
		  if (type == 0x8b)
		    {
		      /* movq */
		      if (val == 0x4c)
			bfd_put_8 (output_bfd, 0x49,
				   contents + rel->r_offset - 3);
		      bfd_put_8 (output_bfd, 0xc7,
				 contents + rel->r_offset - 2);
		      bfd_put_8 (output_bfd, 0xc0 | reg,
				 contents + rel->r_offset - 1);
		    }
		  else if (reg == 4)
		    {
		      /* addq -> addq - addressing with %rsp/%r12 is
			 special  */
		      if (val == 0x4c)
			bfd_put_8 (output_bfd, 0x49,
				   contents + rel->r_offset - 3);
		      bfd_put_8 (output_bfd, 0x81,
				 contents + rel->r_offset - 2);
		      bfd_put_8 (output_bfd, 0xc0 | reg,
				 contents + rel->r_offset - 1);
		    }
		  else
		    {
		      /* addq -> leaq */
		      if (val == 0x4c)
			bfd_put_8 (output_bfd, 0x4d,
				   contents + rel->r_offset - 3);
		      bfd_put_8 (output_bfd, 0x8d,
				 contents + rel->r_offset - 2);
		      bfd_put_8 (output_bfd, 0x80 | reg | (reg << 3),
				 contents + rel->r_offset - 1);
		    }
		  bfd_put_32 (output_bfd, tpoff (info, relocation),
			      contents + rel->r_offset);
		  continue;
		}
	    }

	  if (htab->sgot == NULL)
	    abort ();

	  if (h != NULL)
	    {
	      off = h->got.offset;
	      offplt = elf64_x86_64_hash_entry (h)->tlsdesc_got;
	    }
	  else
	    {
	      if (local_got_offsets == NULL)
		abort ();

	      off = local_got_offsets[r_symndx];
	      offplt = local_tlsdesc_gotents[r_symndx];
	    }

	  if ((off & 1) != 0)
	    off &= ~1;
	  else
	    {
	      Elf_Internal_Rela outrel;
	      bfd_byte *loc;
	      int dr_type, indx;
	      asection *sreloc;

	      if (htab->srelgot == NULL)
		abort ();

	      indx = h && h->dynindx != -1 ? h->dynindx : 0;

	      if (GOT_TLS_GDESC_P (tls_type))
		{
		  outrel.r_info = ELF64_R_INFO (indx, R_X86_64_TLSDESC);
		  BFD_ASSERT (htab->sgotplt_jump_table_size + offplt
			      + 2 * GOT_ENTRY_SIZE <= htab->sgotplt->size);
		  outrel.r_offset = (htab->sgotplt->output_section->vma
				     + htab->sgotplt->output_offset
				     + offplt
				     + htab->sgotplt_jump_table_size);
		  sreloc = htab->srelplt;
		  loc = sreloc->contents;
		  loc += sreloc->reloc_count++
		    * sizeof (Elf64_External_Rela);
		  BFD_ASSERT (loc + sizeof (Elf64_External_Rela)
			      <= sreloc->contents + sreloc->size);
		  if (indx == 0)
		    outrel.r_addend = relocation - dtpoff_base (info);
		  else
		    outrel.r_addend = 0;
		  bfd_elf64_swap_reloca_out (output_bfd, &outrel, loc);
		}

	      sreloc = htab->srelgot;

	      outrel.r_offset = (htab->sgot->output_section->vma
				 + htab->sgot->output_offset + off);

	      if (GOT_TLS_GD_P (tls_type))
		dr_type = R_X86_64_DTPMOD64;
	      else if (GOT_TLS_GDESC_P (tls_type))
		goto dr_done;
	      else
		dr_type = R_X86_64_TPOFF64;

	      bfd_put_64 (output_bfd, 0, htab->sgot->contents + off);
	      outrel.r_addend = 0;
	      if ((dr_type == R_X86_64_TPOFF64
		   || dr_type == R_X86_64_TLSDESC) && indx == 0)
		outrel.r_addend = relocation - dtpoff_base (info);
	      outrel.r_info = ELF64_R_INFO (indx, dr_type);

	      loc = sreloc->contents;
	      loc += sreloc->reloc_count++ * sizeof (Elf64_External_Rela);
	      BFD_ASSERT (loc + sizeof (Elf64_External_Rela)
			  <= sreloc->contents + sreloc->size);
	      bfd_elf64_swap_reloca_out (output_bfd, &outrel, loc);

	      if (GOT_TLS_GD_P (tls_type))
		{
		  if (indx == 0)
		    {
		      BFD_ASSERT (! unresolved_reloc);
		      bfd_put_64 (output_bfd,
				  relocation - dtpoff_base (info),
				  htab->sgot->contents + off + GOT_ENTRY_SIZE);
		    }
		  else
		    {
		      bfd_put_64 (output_bfd, 0,
				  htab->sgot->contents + off + GOT_ENTRY_SIZE);
		      outrel.r_info = ELF64_R_INFO (indx,
						    R_X86_64_DTPOFF64);
		      outrel.r_offset += GOT_ENTRY_SIZE;
		      sreloc->reloc_count++;
		      loc += sizeof (Elf64_External_Rela);
		      BFD_ASSERT (loc + sizeof (Elf64_External_Rela)
				  <= sreloc->contents + sreloc->size);
		      bfd_elf64_swap_reloca_out (output_bfd, &outrel, loc);
		    }
		}

	    dr_done:
	      if (h != NULL)
		h->got.offset |= 1;
	      else
		local_got_offsets[r_symndx] |= 1;
	    }

	  if (off >= (bfd_vma) -2
	      && ! GOT_TLS_GDESC_P (tls_type))
	    abort ();
	  if (r_type == ELF64_R_TYPE (rel->r_info))
	    {
	      if (r_type == R_X86_64_GOTPC32_TLSDESC
		  || r_type == R_X86_64_TLSDESC_CALL)
		relocation = htab->sgotplt->output_section->vma
		  + htab->sgotplt->output_offset
		  + offplt + htab->sgotplt_jump_table_size;
	      else
		relocation = htab->sgot->output_section->vma
		  + htab->sgot->output_offset + off;
	      unresolved_reloc = FALSE;
	    }
	  else if (ELF64_R_TYPE (rel->r_info) == R_X86_64_TLSGD)
	    {
	      unsigned int i;
	      static unsigned char tlsgd[8]
		= { 0x66, 0x48, 0x8d, 0x3d, 0x66, 0x66, 0x48, 0xe8 };

	      /* GD->IE transition.
		 .byte 0x66; leaq foo@tlsgd(%rip), %rdi
		 .word 0x6666; rex64; call __tls_get_addr@plt
		 Change it into:
		 movq %fs:0, %rax
		 addq foo@gottpoff(%rip), %rax */
	      BFD_ASSERT (rel->r_offset >= 4);
	      for (i = 0; i < 4; i++)
		BFD_ASSERT (bfd_get_8 (input_bfd,
				       contents + rel->r_offset - 4 + i)
			    == tlsgd[i]);
	      BFD_ASSERT (rel->r_offset + 12 <= input_section->size);
	      for (i = 0; i < 4; i++)
		BFD_ASSERT (bfd_get_8 (input_bfd,
				       contents + rel->r_offset + 4 + i)
			    == tlsgd[i+4]);
	      BFD_ASSERT (rel + 1 < relend);
	      BFD_ASSERT (ELF64_R_TYPE (rel[1].r_info) == R_X86_64_PLT32);
	      memcpy (contents + rel->r_offset - 4,
		      "\x64\x48\x8b\x04\x25\0\0\0\0\x48\x03\x05\0\0\0",
		      16);

	      relocation = (htab->sgot->output_section->vma
			    + htab->sgot->output_offset + off
			    - rel->r_offset
			    - input_section->output_section->vma
			    - input_section->output_offset
			    - 12);
	      bfd_put_32 (output_bfd, relocation,
			  contents + rel->r_offset + 8);
	      /* Skip R_X86_64_PLT32.  */
	      rel++;
	      continue;
	    }
	  else if (ELF64_R_TYPE (rel->r_info) == R_X86_64_GOTPC32_TLSDESC)
	    {
	      /* GDesc -> IE transition.
		 It's originally something like:
		 leaq x@tlsdesc(%rip), %rax

		 Change it to:
		 movq x@gottpoff(%rip), %rax # before nop; nop

		 Registers other than %rax may be set up here.  */

	      unsigned int val, type, type2;
	      bfd_vma roff;

	      /* First, make sure it's a leaq adding rip to a 32-bit
		 offset into any register, although it's probably
		 almost always going to be rax.  */
	      roff = rel->r_offset;
	      BFD_ASSERT (roff >= 3);
	      type = bfd_get_8 (input_bfd, contents + roff - 3);
	      BFD_ASSERT ((type & 0xfb) == 0x48);
	      type2 = bfd_get_8 (input_bfd, contents + roff - 2);
	      BFD_ASSERT (type2 == 0x8d);
	      val = bfd_get_8 (input_bfd, contents + roff - 1);
	      BFD_ASSERT ((val & 0xc7) == 0x05);
	      BFD_ASSERT (roff + 4 <= input_section->size);

	      /* Now modify the instruction as appropriate.  */
	      /* To turn a leaq into a movq in the form we use it, it
		 suffices to change the second byte from 0x8d to
		 0x8b.  */
	      bfd_put_8 (output_bfd, 0x8b, contents + roff - 2);

	      bfd_put_32 (output_bfd,
			  htab->sgot->output_section->vma
			  + htab->sgot->output_offset + off
			  - rel->r_offset
			  - input_section->output_section->vma
			  - input_section->output_offset
			  - 4,
			  contents + roff);
	      continue;
	    }
	  else if (ELF64_R_TYPE (rel->r_info) == R_X86_64_TLSDESC_CALL)
	    {
	      /* GDesc -> IE transition.
		 It's originally:
		 call *(%rax)

		 Change it to:
		 nop; nop.  */

	      unsigned int val, type;
	      bfd_vma roff;

	      /* First, make sure it's a call *(%eax).  */
	      roff = rel->r_offset;
	      BFD_ASSERT (roff + 2 <= input_section->size);
	      type = bfd_get_8 (input_bfd, contents + roff);
	      BFD_ASSERT (type == 0xff);
	      val = bfd_get_8 (input_bfd, contents + roff + 1);
	      BFD_ASSERT (val == 0x10);

	      /* Now modify the instruction as appropriate.  Use
		 xchg %ax,%ax instead of 2 nops.  */
	      bfd_put_8 (output_bfd, 0x66, contents + roff);
	      bfd_put_8 (output_bfd, 0x90, contents + roff + 1);

	      continue;
	    }
	  else
	    BFD_ASSERT (FALSE);
	  break;

	case R_X86_64_TLSLD:
	  if (! info->shared)
	    {
	      /* LD->LE transition:
		 Ensure it is:
		 leaq foo@tlsld(%rip), %rdi; call __tls_get_addr@plt.
		 We change it into:
		 .word 0x6666; .byte 0x66; movl %fs:0, %rax.  */
	      BFD_ASSERT (rel->r_offset >= 3);
	      BFD_ASSERT (bfd_get_8 (input_bfd, contents + rel->r_offset - 3)
			  == 0x48);
	      BFD_ASSERT (bfd_get_8 (input_bfd, contents + rel->r_offset - 2)
			  == 0x8d);
	      BFD_ASSERT (bfd_get_8 (input_bfd, contents + rel->r_offset - 1)
			  == 0x3d);
	      BFD_ASSERT (rel->r_offset + 9 <= input_section->size);
	      BFD_ASSERT (bfd_get_8 (input_bfd, contents + rel->r_offset + 4)
			  == 0xe8);
	      BFD_ASSERT (rel + 1 < relend);
	      BFD_ASSERT (ELF64_R_TYPE (rel[1].r_info) == R_X86_64_PLT32);
	      memcpy (contents + rel->r_offset - 3,
		      "\x66\x66\x66\x64\x48\x8b\x04\x25\0\0\0", 12);
	      /* Skip R_X86_64_PLT32.  */
	      rel++;
	      continue;
	    }

	  if (htab->sgot == NULL)
	    abort ();

	  off = htab->tls_ld_got.offset;
	  if (off & 1)
	    off &= ~1;
	  else
	    {
	      Elf_Internal_Rela outrel;
	      bfd_byte *loc;

	      if (htab->srelgot == NULL)
		abort ();

	      outrel.r_offset = (htab->sgot->output_section->vma
				 + htab->sgot->output_offset + off);

	      bfd_put_64 (output_bfd, 0,
			  htab->sgot->contents + off);
	      bfd_put_64 (output_bfd, 0,
			  htab->sgot->contents + off + GOT_ENTRY_SIZE);
	      outrel.r_info = ELF64_R_INFO (0, R_X86_64_DTPMOD64);
	      outrel.r_addend = 0;
	      loc = htab->srelgot->contents;
	      loc += htab->srelgot->reloc_count++ * sizeof (Elf64_External_Rela);
	      bfd_elf64_swap_reloca_out (output_bfd, &outrel, loc);
	      htab->tls_ld_got.offset |= 1;
	    }
	  relocation = htab->sgot->output_section->vma
		       + htab->sgot->output_offset + off;
	  unresolved_reloc = FALSE;
	  break;

	case R_X86_64_DTPOFF32:
	  if (info->shared || (input_section->flags & SEC_CODE) == 0)
	    relocation -= dtpoff_base (info);
	  else
	    relocation = tpoff (info, relocation);
	  break;

	case R_X86_64_TPOFF32:
	  BFD_ASSERT (! info->shared);
	  relocation = tpoff (info, relocation);
	  break;

	default:
	  break;
	}

      /* Dynamic relocs are not propagated for SEC_DEBUGGING sections
	 because such sections are not SEC_ALLOC and thus ld.so will
	 not process them.  */
      if (unresolved_reloc
	  && !((input_section->flags & SEC_DEBUGGING) != 0
	       && h->def_dynamic))
	(*_bfd_error_handler)
	  (_("%B(%A+0x%lx): unresolvable %s relocation against symbol `%s'"),
	   input_bfd,
	   input_section,
	   (long) rel->r_offset,
	   howto->name,
	   h->root.root.string);

      r = _bfd_final_link_relocate (howto, input_bfd, input_section,
				    contents, rel->r_offset,
				    relocation, rel->r_addend);

      if (r != bfd_reloc_ok)
	{
	  const char *name;

	  if (h != NULL)
	    name = h->root.root.string;
	  else
	    {
	      name = bfd_elf_string_from_elf_section (input_bfd,
						      symtab_hdr->sh_link,
						      sym->st_name);
	      if (name == NULL)
		return FALSE;
	      if (*name == '\0')
		name = bfd_section_name (input_bfd, sec);
	    }

	  if (r == bfd_reloc_overflow)
	    {
	      if (! ((*info->callbacks->reloc_overflow)
		     (info, (h ? &h->root : NULL), name, howto->name,
		      (bfd_vma) 0, input_bfd, input_section,
		      rel->r_offset)))
		return FALSE;
	    }
	  else
	    {
	      (*_bfd_error_handler)
		(_("%B(%A+0x%lx): reloc against `%s': error %d"),
		 input_bfd, input_section,
		 (long) rel->r_offset, name, (int) r);
	      return FALSE;
	    }
	}
    }

  return TRUE;
}

/* Finish up dynamic symbol handling.  We set the contents of various
   dynamic sections here.  */

static bfd_boolean
elf64_x86_64_finish_dynamic_symbol (bfd *output_bfd,
				    struct bfd_link_info *info,
				    struct elf_link_hash_entry *h,
				    Elf_Internal_Sym *sym)
{
  struct elf64_x86_64_link_hash_table *htab;

  htab = elf64_x86_64_hash_table (info);

  if (h->plt.offset != (bfd_vma) -1)
    {
      bfd_vma plt_index;
      bfd_vma got_offset;
      Elf_Internal_Rela rela;
      bfd_byte *loc;

      /* This symbol has an entry in the procedure linkage table.  Set
	 it up.	 */
      if (h->dynindx == -1
	  || htab->splt == NULL
	  || htab->sgotplt == NULL
	  || htab->srelplt == NULL)
	abort ();

      /* Get the index in the procedure linkage table which
	 corresponds to this symbol.  This is the index of this symbol
	 in all the symbols for which we are making plt entries.  The
	 first entry in the procedure linkage table is reserved.  */
      plt_index = h->plt.offset / PLT_ENTRY_SIZE - 1;

      /* Get the offset into the .got table of the entry that
	 corresponds to this function.	Each .got entry is GOT_ENTRY_SIZE
	 bytes. The first three are reserved for the dynamic linker.  */
      got_offset = (plt_index + 3) * GOT_ENTRY_SIZE;

      /* Fill in the entry in the procedure linkage table.  */
      memcpy (htab->splt->contents + h->plt.offset, elf64_x86_64_plt_entry,
	      PLT_ENTRY_SIZE);

      /* Insert the relocation positions of the plt section.  The magic
	 numbers at the end of the statements are the positions of the
	 relocations in the plt section.  */
      /* Put offset for jmp *name@GOTPCREL(%rip), since the
	 instruction uses 6 bytes, subtract this value.  */
      bfd_put_32 (output_bfd,
		      (htab->sgotplt->output_section->vma
		       + htab->sgotplt->output_offset
		       + got_offset
		       - htab->splt->output_section->vma
		       - htab->splt->output_offset
		       - h->plt.offset
		       - 6),
		  htab->splt->contents + h->plt.offset + 2);
      /* Put relocation index.  */
      bfd_put_32 (output_bfd, plt_index,
		  htab->splt->contents + h->plt.offset + 7);
      /* Put offset for jmp .PLT0.  */
      bfd_put_32 (output_bfd, - (h->plt.offset + PLT_ENTRY_SIZE),
		  htab->splt->contents + h->plt.offset + 12);

      /* Fill in the entry in the global offset table, initially this
	 points to the pushq instruction in the PLT which is at offset 6.  */
      bfd_put_64 (output_bfd, (htab->splt->output_section->vma
			       + htab->splt->output_offset
			       + h->plt.offset + 6),
		  htab->sgotplt->contents + got_offset);

      /* Fill in the entry in the .rela.plt section.  */
      rela.r_offset = (htab->sgotplt->output_section->vma
		       + htab->sgotplt->output_offset
		       + got_offset);
      rela.r_info = ELF64_R_INFO (h->dynindx, R_X86_64_JUMP_SLOT);
      rela.r_addend = 0;
      loc = htab->srelplt->contents + plt_index * sizeof (Elf64_External_Rela);
      bfd_elf64_swap_reloca_out (output_bfd, &rela, loc);

      if (!h->def_regular)
	{
	  /* Mark the symbol as undefined, rather than as defined in
	     the .plt section.  Leave the value if there were any
	     relocations where pointer equality matters (this is a clue
	     for the dynamic linker, to make function pointer
	     comparisons work between an application and shared
	     library), otherwise set it to zero.  If a function is only
	     called from a binary, there is no need to slow down
	     shared libraries because of that.  */
	  sym->st_shndx = SHN_UNDEF;
	  if (!h->pointer_equality_needed)
	    sym->st_value = 0;
	}
    }

  if (h->got.offset != (bfd_vma) -1
      && ! GOT_TLS_GD_ANY_P (elf64_x86_64_hash_entry (h)->tls_type)
      && elf64_x86_64_hash_entry (h)->tls_type != GOT_TLS_IE)
    {
      Elf_Internal_Rela rela;
      bfd_byte *loc;

      /* This symbol has an entry in the global offset table.  Set it
	 up.  */
      if (htab->sgot == NULL || htab->srelgot == NULL)
	abort ();

      rela.r_offset = (htab->sgot->output_section->vma
		       + htab->sgot->output_offset
		       + (h->got.offset &~ (bfd_vma) 1));

      /* If this is a static link, or it is a -Bsymbolic link and the
	 symbol is defined locally or was forced to be local because
	 of a version file, we just want to emit a RELATIVE reloc.
	 The entry in the global offset table will already have been
	 initialized in the relocate_section function.  */
      if (info->shared
	  && SYMBOL_REFERENCES_LOCAL (info, h))
	{
	  BFD_ASSERT((h->got.offset & 1) != 0);
	  rela.r_info = ELF64_R_INFO (0, R_X86_64_RELATIVE);
	  rela.r_addend = (h->root.u.def.value
			   + h->root.u.def.section->output_section->vma
			   + h->root.u.def.section->output_offset);
	}
      else
	{
	  BFD_ASSERT((h->got.offset & 1) == 0);
	  bfd_put_64 (output_bfd, (bfd_vma) 0,
		      htab->sgot->contents + h->got.offset);
	  rela.r_info = ELF64_R_INFO (h->dynindx, R_X86_64_GLOB_DAT);
	  rela.r_addend = 0;
	}

      loc = htab->srelgot->contents;
      loc += htab->srelgot->reloc_count++ * sizeof (Elf64_External_Rela);
      bfd_elf64_swap_reloca_out (output_bfd, &rela, loc);
    }

  if (h->needs_copy)
    {
      Elf_Internal_Rela rela;
      bfd_byte *loc;

      /* This symbol needs a copy reloc.  Set it up.  */

      if (h->dynindx == -1
	  || (h->root.type != bfd_link_hash_defined
	      && h->root.type != bfd_link_hash_defweak)
	  || htab->srelbss == NULL)
	abort ();

      rela.r_offset = (h->root.u.def.value
		       + h->root.u.def.section->output_section->vma
		       + h->root.u.def.section->output_offset);
      rela.r_info = ELF64_R_INFO (h->dynindx, R_X86_64_COPY);
      rela.r_addend = 0;
      loc = htab->srelbss->contents;
      loc += htab->srelbss->reloc_count++ * sizeof (Elf64_External_Rela);
      bfd_elf64_swap_reloca_out (output_bfd, &rela, loc);
    }

  /* Mark _DYNAMIC and _GLOBAL_OFFSET_TABLE_ as absolute.  */
  if (strcmp (h->root.root.string, "_DYNAMIC") == 0
      || h == htab->elf.hgot)
    sym->st_shndx = SHN_ABS;

  return TRUE;
}

/* Used to decide how to sort relocs in an optimal manner for the
   dynamic linker, before writing them out.  */

static enum elf_reloc_type_class
elf64_x86_64_reloc_type_class (const Elf_Internal_Rela *rela)
{
  switch ((int) ELF64_R_TYPE (rela->r_info))
    {
    case R_X86_64_RELATIVE:
      return reloc_class_relative;
    case R_X86_64_JUMP_SLOT:
      return reloc_class_plt;
    case R_X86_64_COPY:
      return reloc_class_copy;
    default:
      return reloc_class_normal;
    }
}

/* Finish up the dynamic sections.  */

static bfd_boolean
elf64_x86_64_finish_dynamic_sections (bfd *output_bfd, struct bfd_link_info *info)
{
  struct elf64_x86_64_link_hash_table *htab;
  bfd *dynobj;
  asection *sdyn;

  htab = elf64_x86_64_hash_table (info);
  dynobj = htab->elf.dynobj;
  sdyn = bfd_get_section_by_name (dynobj, ".dynamic");

  if (htab->elf.dynamic_sections_created)
    {
      Elf64_External_Dyn *dyncon, *dynconend;

      if (sdyn == NULL || htab->sgot == NULL)
	abort ();

      dyncon = (Elf64_External_Dyn *) sdyn->contents;
      dynconend = (Elf64_External_Dyn *) (sdyn->contents + sdyn->size);
      for (; dyncon < dynconend; dyncon++)
	{
	  Elf_Internal_Dyn dyn;
	  asection *s;

	  bfd_elf64_swap_dyn_in (dynobj, dyncon, &dyn);

	  switch (dyn.d_tag)
	    {
	    default:
	      continue;

	    case DT_PLTGOT:
	      s = htab->sgotplt;
	      dyn.d_un.d_ptr = s->output_section->vma + s->output_offset;
	      break;

	    case DT_JMPREL:
	      dyn.d_un.d_ptr = htab->srelplt->output_section->vma;
	      break;

	    case DT_PLTRELSZ:
	      s = htab->srelplt->output_section;
	      dyn.d_un.d_val = s->size;
	      break;

	    case DT_RELASZ:
	      /* The procedure linkage table relocs (DT_JMPREL) should
		 not be included in the overall relocs (DT_RELA).
		 Therefore, we override the DT_RELASZ entry here to
		 make it not include the JMPREL relocs.  Since the
		 linker script arranges for .rela.plt to follow all
		 other relocation sections, we don't have to worry
		 about changing the DT_RELA entry.  */
	      if (htab->srelplt != NULL)
		{
		  s = htab->srelplt->output_section;
		  dyn.d_un.d_val -= s->size;
		}
	      break;

	    case DT_TLSDESC_PLT:
	      s = htab->splt;
	      dyn.d_un.d_ptr = s->output_section->vma + s->output_offset
		+ htab->tlsdesc_plt;
	      break;

	    case DT_TLSDESC_GOT:
	      s = htab->sgot;
	      dyn.d_un.d_ptr = s->output_section->vma + s->output_offset
		+ htab->tlsdesc_got;
	      break;
	    }

	  bfd_elf64_swap_dyn_out (output_bfd, &dyn, dyncon);
	}

      /* Fill in the special first entry in the procedure linkage table.  */
      if (htab->splt && htab->splt->size > 0)
	{
	  /* Fill in the first entry in the procedure linkage table.  */
	  memcpy (htab->splt->contents, elf64_x86_64_plt0_entry,
		  PLT_ENTRY_SIZE);
	  /* Add offset for pushq GOT+8(%rip), since the instruction
	     uses 6 bytes subtract this value.  */
	  bfd_put_32 (output_bfd,
		      (htab->sgotplt->output_section->vma
		       + htab->sgotplt->output_offset
		       + 8
		       - htab->splt->output_section->vma
		       - htab->splt->output_offset
		       - 6),
		      htab->splt->contents + 2);
	  /* Add offset for jmp *GOT+16(%rip). The 12 is the offset to
	     the end of the instruction.  */
	  bfd_put_32 (output_bfd,
		      (htab->sgotplt->output_section->vma
		       + htab->sgotplt->output_offset
		       + 16
		       - htab->splt->output_section->vma
		       - htab->splt->output_offset
		       - 12),
		      htab->splt->contents + 8);

	  elf_section_data (htab->splt->output_section)->this_hdr.sh_entsize =
	    PLT_ENTRY_SIZE;

	  if (htab->tlsdesc_plt)
	    {
	      bfd_put_64 (output_bfd, (bfd_vma) 0,
			  htab->sgot->contents + htab->tlsdesc_got);

	      memcpy (htab->splt->contents + htab->tlsdesc_plt,
		      elf64_x86_64_plt0_entry,
		      PLT_ENTRY_SIZE);

	      /* Add offset for pushq GOT+8(%rip), since the
		 instruction uses 6 bytes subtract this value.  */
	      bfd_put_32 (output_bfd,
			  (htab->sgotplt->output_section->vma
			   + htab->sgotplt->output_offset
			   + 8
			   - htab->splt->output_section->vma
			   - htab->splt->output_offset
			   - htab->tlsdesc_plt
			   - 6),
			  htab->splt->contents + htab->tlsdesc_plt + 2);
	      /* Add offset for jmp *GOT+TDG(%rip), where TGD stands for
		 htab->tlsdesc_got. The 12 is the offset to the end of
		 the instruction.  */
	      bfd_put_32 (output_bfd,
			  (htab->sgot->output_section->vma
			   + htab->sgot->output_offset
			   + htab->tlsdesc_got
			   - htab->splt->output_section->vma
			   - htab->splt->output_offset
			   - htab->tlsdesc_plt
			   - 12),
			  htab->splt->contents + htab->tlsdesc_plt + 8);
	    }
	}
    }

  if (htab->sgotplt)
    {
      /* Fill in the first three entries in the global offset table.  */
      if (htab->sgotplt->size > 0)
	{
	  /* Set the first entry in the global offset table to the address of
	     the dynamic section.  */
	  if (sdyn == NULL)
	    bfd_put_64 (output_bfd, (bfd_vma) 0, htab->sgotplt->contents);
	  else
	    bfd_put_64 (output_bfd,
			sdyn->output_section->vma + sdyn->output_offset,
			htab->sgotplt->contents);
	  /* Write GOT[1] and GOT[2], needed for the dynamic linker.  */
	  bfd_put_64 (output_bfd, (bfd_vma) 0, htab->sgotplt->contents + GOT_ENTRY_SIZE);
	  bfd_put_64 (output_bfd, (bfd_vma) 0, htab->sgotplt->contents + GOT_ENTRY_SIZE*2);
	}

      elf_section_data (htab->sgotplt->output_section)->this_hdr.sh_entsize =
	GOT_ENTRY_SIZE;
    }

  if (htab->sgot && htab->sgot->size > 0)
    elf_section_data (htab->sgot->output_section)->this_hdr.sh_entsize
      = GOT_ENTRY_SIZE;

  return TRUE;
}

/* Return address for Ith PLT stub in section PLT, for relocation REL
   or (bfd_vma) -1 if it should not be included.  */

static bfd_vma
elf64_x86_64_plt_sym_val (bfd_vma i, const asection *plt,
			  const arelent *rel ATTRIBUTE_UNUSED)
{
  return plt->vma + (i + 1) * PLT_ENTRY_SIZE;
}

/* Handle an x86-64 specific section when reading an object file.  This
   is called when elfcode.h finds a section with an unknown type.  */

static bfd_boolean
elf64_x86_64_section_from_shdr (bfd *abfd,
				Elf_Internal_Shdr *hdr,
				const char *name,
				int shindex)
{
  if (hdr->sh_type != SHT_X86_64_UNWIND)
    return FALSE;

  if (! _bfd_elf_make_section_from_shdr (abfd, hdr, name, shindex))
    return FALSE;

  return TRUE;
}

/* Hook called by the linker routine which adds symbols from an object
   file.  We use it to put SHN_X86_64_LCOMMON items in .lbss, instead
   of .bss.  */

static bfd_boolean
elf64_x86_64_add_symbol_hook (bfd *abfd,
			      struct bfd_link_info *info ATTRIBUTE_UNUSED,
			      Elf_Internal_Sym *sym,
			      const char **namep ATTRIBUTE_UNUSED,
			      flagword *flagsp ATTRIBUTE_UNUSED,
			      asection **secp, bfd_vma *valp)
{
  asection *lcomm;

  switch (sym->st_shndx)
    {
    case SHN_X86_64_LCOMMON:
      lcomm = bfd_get_section_by_name (abfd, "LARGE_COMMON");
      if (lcomm == NULL)
	{
	  lcomm = bfd_make_section_with_flags (abfd,
					       "LARGE_COMMON",
					       (SEC_ALLOC
						| SEC_IS_COMMON
						| SEC_LINKER_CREATED));
	  if (lcomm == NULL)
	    return FALSE;
	  elf_section_flags (lcomm) |= SHF_X86_64_LARGE;
	}
      *secp = lcomm;
      *valp = sym->st_size;
      break;
    }
  return TRUE;
}


/* Given a BFD section, try to locate the corresponding ELF section
   index.  */

static bfd_boolean
elf64_x86_64_elf_section_from_bfd_section (bfd *abfd ATTRIBUTE_UNUSED,
					   asection *sec, int *index)
{
  if (sec == &_bfd_elf_large_com_section)
    {
      *index = SHN_X86_64_LCOMMON;
      return TRUE;
    }
  return FALSE;
}

/* Process a symbol.  */

static void
elf64_x86_64_symbol_processing (bfd *abfd ATTRIBUTE_UNUSED,
				asymbol *asym)
{
  elf_symbol_type *elfsym = (elf_symbol_type *) asym;

  switch (elfsym->internal_elf_sym.st_shndx)
    {
    case SHN_X86_64_LCOMMON:
      asym->section = &_bfd_elf_large_com_section;
      asym->value = elfsym->internal_elf_sym.st_size;
      /* Common symbol doesn't set BSF_GLOBAL.  */
      asym->flags &= ~BSF_GLOBAL;
      break;
    }
}

static bfd_boolean
elf64_x86_64_common_definition (Elf_Internal_Sym *sym)
{
  return (sym->st_shndx == SHN_COMMON
	  || sym->st_shndx == SHN_X86_64_LCOMMON);
}

static unsigned int
elf64_x86_64_common_section_index (asection *sec)
{
  if ((elf_section_flags (sec) & SHF_X86_64_LARGE) == 0)
    return SHN_COMMON;
  else
    return SHN_X86_64_LCOMMON;
}

static asection *
elf64_x86_64_common_section (asection *sec)
{
  if ((elf_section_flags (sec) & SHF_X86_64_LARGE) == 0)
    return bfd_com_section_ptr;
  else
    return &_bfd_elf_large_com_section;
}

static bfd_boolean
elf64_x86_64_merge_symbol (struct bfd_link_info *info ATTRIBUTE_UNUSED,
			   struct elf_link_hash_entry **sym_hash ATTRIBUTE_UNUSED,
			   struct elf_link_hash_entry *h,
			   Elf_Internal_Sym *sym,
			   asection **psec,
			   bfd_vma *pvalue ATTRIBUTE_UNUSED,
			   unsigned int *pold_alignment ATTRIBUTE_UNUSED,
			   bfd_boolean *skip ATTRIBUTE_UNUSED,
			   bfd_boolean *override ATTRIBUTE_UNUSED,
			   bfd_boolean *type_change_ok ATTRIBUTE_UNUSED,
			   bfd_boolean *size_change_ok ATTRIBUTE_UNUSED,
			   bfd_boolean *newdef ATTRIBUTE_UNUSED,
			   bfd_boolean *newdyn,
			   bfd_boolean *newdyncommon ATTRIBUTE_UNUSED,
			   bfd_boolean *newweak ATTRIBUTE_UNUSED,
			   bfd *abfd ATTRIBUTE_UNUSED,
			   asection **sec,
			   bfd_boolean *olddef ATTRIBUTE_UNUSED,
			   bfd_boolean *olddyn,
			   bfd_boolean *olddyncommon ATTRIBUTE_UNUSED,
			   bfd_boolean *oldweak ATTRIBUTE_UNUSED,
			   bfd *oldbfd,
			   asection **oldsec)
{
  /* A normal common symbol and a large common symbol result in a
     normal common symbol.  We turn the large common symbol into a
     normal one.  */
  if (!*olddyn
      && h->root.type == bfd_link_hash_common
      && !*newdyn
      && bfd_is_com_section (*sec)
      && *oldsec != *sec)
    {
      if (sym->st_shndx == SHN_COMMON
	  && (elf_section_flags (*oldsec) & SHF_X86_64_LARGE) != 0)
	{
	  h->root.u.c.p->section
	    = bfd_make_section_old_way (oldbfd, "COMMON");
	  h->root.u.c.p->section->flags = SEC_ALLOC;
	}
      else if (sym->st_shndx == SHN_X86_64_LCOMMON
	       && (elf_section_flags (*oldsec) & SHF_X86_64_LARGE) == 0)
	*psec = *sec = bfd_com_section_ptr; 
    }

  return TRUE;
}

static int
elf64_x86_64_additional_program_headers (bfd *abfd,
					 struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  asection *s;
  int count = 0; 

  /* Check to see if we need a large readonly segment.  */
  s = bfd_get_section_by_name (abfd, ".lrodata");
  if (s && (s->flags & SEC_LOAD))
    count++;

  /* Check to see if we need a large data segment.  Since .lbss sections
     is placed right after the .bss section, there should be no need for
     a large data segment just because of .lbss.  */
  s = bfd_get_section_by_name (abfd, ".ldata");
  if (s && (s->flags & SEC_LOAD))
    count++;

  return count;
}

/* Return TRUE if symbol should be hashed in the `.gnu.hash' section.  */

static bfd_boolean
elf64_x86_64_hash_symbol (struct elf_link_hash_entry *h)
{
  if (h->plt.offset != (bfd_vma) -1
      && !h->def_regular
      && !h->pointer_equality_needed)
    return FALSE;

  return _bfd_elf_hash_symbol (h);
}

static const struct bfd_elf_special_section 
  elf64_x86_64_special_sections[]=
{
  { STRING_COMMA_LEN (".gnu.linkonce.lb"), -2, SHT_NOBITS,   SHF_ALLOC + SHF_WRITE + SHF_X86_64_LARGE},
  { STRING_COMMA_LEN (".gnu.linkonce.lr"), -2, SHT_PROGBITS, SHF_ALLOC + SHF_X86_64_LARGE},
  { STRING_COMMA_LEN (".gnu.linkonce.lt"), -2, SHT_PROGBITS, SHF_ALLOC + SHF_EXECINSTR + SHF_X86_64_LARGE},
  { STRING_COMMA_LEN (".lbss"),	           -2, SHT_NOBITS,   SHF_ALLOC + SHF_WRITE + SHF_X86_64_LARGE},
  { STRING_COMMA_LEN (".ldata"),	   -2, SHT_PROGBITS, SHF_ALLOC + SHF_WRITE + SHF_X86_64_LARGE},
  { STRING_COMMA_LEN (".lrodata"),	   -2, SHT_PROGBITS, SHF_ALLOC + SHF_X86_64_LARGE},
  { NULL,	                0,          0, 0,            0 }
};

#define TARGET_LITTLE_SYM		    bfd_elf64_x86_64_vec
#define TARGET_LITTLE_NAME		    "elf64-x86-64"
#define ELF_ARCH			    bfd_arch_i386
#define ELF_MACHINE_CODE		    EM_X86_64
#define ELF_MAXPAGESIZE			    0x200000
#define ELF_MINPAGESIZE			    0x1000
#define ELF_COMMONPAGESIZE		    0x1000

#define elf_backend_can_gc_sections	    1
#define elf_backend_can_refcount	    1
#define elf_backend_want_got_plt	    1
#define elf_backend_plt_readonly	    1
#define elf_backend_want_plt_sym	    0
#define elf_backend_got_header_size	    (GOT_ENTRY_SIZE*3)
#define elf_backend_rela_normal		    1

#define elf_info_to_howto		    elf64_x86_64_info_to_howto

#define bfd_elf64_bfd_link_hash_table_create \
  elf64_x86_64_link_hash_table_create
#define bfd_elf64_bfd_reloc_type_lookup	    elf64_x86_64_reloc_type_lookup
#define bfd_elf64_bfd_reloc_name_lookup \
  elf64_x86_64_reloc_name_lookup

#define elf_backend_adjust_dynamic_symbol   elf64_x86_64_adjust_dynamic_symbol
#define elf_backend_relocs_compatible	    _bfd_elf_relocs_compatible
#define elf_backend_check_relocs	    elf64_x86_64_check_relocs
#define elf_backend_copy_indirect_symbol    elf64_x86_64_copy_indirect_symbol
#define elf_backend_create_dynamic_sections elf64_x86_64_create_dynamic_sections
#define elf_backend_finish_dynamic_sections elf64_x86_64_finish_dynamic_sections
#define elf_backend_finish_dynamic_symbol   elf64_x86_64_finish_dynamic_symbol
#define elf_backend_gc_mark_hook	    elf64_x86_64_gc_mark_hook
#define elf_backend_gc_sweep_hook	    elf64_x86_64_gc_sweep_hook
#define elf_backend_grok_prstatus	    elf64_x86_64_grok_prstatus
#define elf_backend_grok_psinfo		    elf64_x86_64_grok_psinfo
#define elf_backend_reloc_type_class	    elf64_x86_64_reloc_type_class
#define elf_backend_relocate_section	    elf64_x86_64_relocate_section
#define elf_backend_size_dynamic_sections   elf64_x86_64_size_dynamic_sections
#define elf_backend_always_size_sections    elf64_x86_64_always_size_sections
#define elf_backend_init_index_section	    _bfd_elf_init_1_index_section
#define elf_backend_plt_sym_val		    elf64_x86_64_plt_sym_val
#define elf_backend_object_p		    elf64_x86_64_elf_object_p
#define bfd_elf64_mkobject		    elf64_x86_64_mkobject

#define elf_backend_section_from_shdr \
	elf64_x86_64_section_from_shdr

#define elf_backend_section_from_bfd_section \
  elf64_x86_64_elf_section_from_bfd_section
#define elf_backend_add_symbol_hook \
  elf64_x86_64_add_symbol_hook
#define elf_backend_symbol_processing \
  elf64_x86_64_symbol_processing
#define elf_backend_common_section_index \
  elf64_x86_64_common_section_index
#define elf_backend_common_section \
  elf64_x86_64_common_section
#define elf_backend_common_definition \
  elf64_x86_64_common_definition
#define elf_backend_merge_symbol \
  elf64_x86_64_merge_symbol
#define elf_backend_special_sections \
  elf64_x86_64_special_sections
#define elf_backend_additional_program_headers \
  elf64_x86_64_additional_program_headers
#define elf_backend_hash_symbol \
  elf64_x86_64_hash_symbol

#include "elf64-target.h"

/* FreeBSD support.  */

#undef  TARGET_LITTLE_SYM
#define TARGET_LITTLE_SYM		    bfd_elf64_x86_64_freebsd_vec
#undef  TARGET_LITTLE_NAME
#define TARGET_LITTLE_NAME		    "elf64-x86-64-freebsd"

#undef	ELF_OSABI
#define	ELF_OSABI			    ELFOSABI_FREEBSD

#undef  elf_backend_post_process_headers
#define elf_backend_post_process_headers  _bfd_elf_set_osabi

#undef  elf64_bed
#define elf64_bed elf64_x86_64_fbsd_bed

#include "elf64-target.h"
