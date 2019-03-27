/* PowerPC64-specific support for 64-bit ELF.
   Copyright 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Written by Linus Nordberg, Swox AB <info@swox.com>,
   based on elf32-ppc.c by Ian Lance Taylor.
   Largely rewritten by Alan Modra <amodra@bigpond.net.au>

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* The 64-bit PowerPC ELF ABI may be found at
   http://www.linuxbase.org/spec/ELF/ppc64/PPC-elf64abi.txt, and
   http://www.linuxbase.org/spec/ELF/ppc64/spec/book1.html  */

#include "sysdep.h"
#include <stdarg.h>
#include "bfd.h"
#include "bfdlink.h"
#include "libbfd.h"
#include "elf-bfd.h"
#include "elf/ppc64.h"
#include "elf64-ppc.h"

static bfd_reloc_status_type ppc64_elf_ha_reloc
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_reloc_status_type ppc64_elf_branch_reloc
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_reloc_status_type ppc64_elf_brtaken_reloc
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_reloc_status_type ppc64_elf_sectoff_reloc
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_reloc_status_type ppc64_elf_sectoff_ha_reloc
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_reloc_status_type ppc64_elf_toc_reloc
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_reloc_status_type ppc64_elf_toc_ha_reloc
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_reloc_status_type ppc64_elf_toc64_reloc
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_reloc_status_type ppc64_elf_unhandled_reloc
  (bfd *, arelent *, asymbol *, void *, asection *, bfd *, char **);
static bfd_vma opd_entry_value
  (asection *, bfd_vma, asection **, bfd_vma *);

#define TARGET_LITTLE_SYM	bfd_elf64_powerpcle_vec
#define TARGET_LITTLE_NAME	"elf64-powerpcle"
#define TARGET_BIG_SYM		bfd_elf64_powerpc_vec
#define TARGET_BIG_NAME		"elf64-powerpc-freebsd"
#define ELF_ARCH		bfd_arch_powerpc
#define ELF_MACHINE_CODE	EM_PPC64
#define ELF_MAXPAGESIZE		0x10000
#define ELF_COMMONPAGESIZE	0x1000
#define elf_info_to_howto	ppc64_elf_info_to_howto

#define elf_backend_want_got_sym 0
#define elf_backend_want_plt_sym 0
#define elf_backend_plt_alignment 3
#define elf_backend_plt_not_loaded 1
#define elf_backend_got_header_size 8
#define elf_backend_can_gc_sections 1
#define elf_backend_can_refcount 1
#define elf_backend_rela_normal 1
#define elf_backend_default_execstack 0

#define bfd_elf64_mkobject		      ppc64_elf_mkobject
#define bfd_elf64_bfd_reloc_type_lookup	      ppc64_elf_reloc_type_lookup
#define bfd_elf64_bfd_reloc_name_lookup ppc64_elf_reloc_name_lookup
#define bfd_elf64_bfd_merge_private_bfd_data  ppc64_elf_merge_private_bfd_data
#define bfd_elf64_new_section_hook	      ppc64_elf_new_section_hook
#define bfd_elf64_bfd_link_hash_table_create  ppc64_elf_link_hash_table_create
#define bfd_elf64_bfd_link_hash_table_free    ppc64_elf_link_hash_table_free
#define bfd_elf64_get_synthetic_symtab	      ppc64_elf_get_synthetic_symtab

#define elf_backend_object_p		      ppc64_elf_object_p
#define elf_backend_grok_prstatus	      ppc64_elf_grok_prstatus
#define elf_backend_grok_psinfo		      ppc64_elf_grok_psinfo
#define elf_backend_write_core_note	      ppc64_elf_write_core_note
#define elf_backend_create_dynamic_sections   ppc64_elf_create_dynamic_sections
#define elf_backend_copy_indirect_symbol      ppc64_elf_copy_indirect_symbol
#define elf_backend_add_symbol_hook	      ppc64_elf_add_symbol_hook
#define elf_backend_check_directives	      ppc64_elf_check_directives
#define elf_backend_as_needed_cleanup	      ppc64_elf_as_needed_cleanup
#define elf_backend_archive_symbol_lookup     ppc64_elf_archive_symbol_lookup
#define elf_backend_check_relocs	      ppc64_elf_check_relocs
#define elf_backend_gc_mark_dynamic_ref       ppc64_elf_gc_mark_dynamic_ref
#define elf_backend_gc_mark_hook	      ppc64_elf_gc_mark_hook
#define elf_backend_gc_sweep_hook	      ppc64_elf_gc_sweep_hook
#define elf_backend_adjust_dynamic_symbol     ppc64_elf_adjust_dynamic_symbol
#define elf_backend_hide_symbol		      ppc64_elf_hide_symbol
#define elf_backend_always_size_sections      ppc64_elf_func_desc_adjust
#define elf_backend_size_dynamic_sections     ppc64_elf_size_dynamic_sections
#define elf_backend_init_index_section	      _bfd_elf_init_2_index_sections
#define elf_backend_action_discarded	      ppc64_elf_action_discarded
#define elf_backend_relocate_section	      ppc64_elf_relocate_section
#define elf_backend_finish_dynamic_symbol     ppc64_elf_finish_dynamic_symbol
#define elf_backend_reloc_type_class	      ppc64_elf_reloc_type_class
#define elf_backend_finish_dynamic_sections   ppc64_elf_finish_dynamic_sections
#define elf_backend_link_output_symbol_hook   ppc64_elf_output_symbol_hook
#define elf_backend_special_sections	      ppc64_elf_special_sections

/* The name of the dynamic interpreter.  This is put in the .interp
   section.  */
#define ELF_DYNAMIC_INTERPRETER "/usr/lib/ld.so.1"

/* The size in bytes of an entry in the procedure linkage table.  */
#define PLT_ENTRY_SIZE 24

/* The initial size of the plt reserved for the dynamic linker.  */
#define PLT_INITIAL_ENTRY_SIZE PLT_ENTRY_SIZE

/* TOC base pointers offset from start of TOC.  */
#define TOC_BASE_OFF	0x8000

/* Offset of tp and dtp pointers from start of TLS block.  */
#define TP_OFFSET	0x7000
#define DTP_OFFSET	0x8000

/* .plt call stub instructions.  The normal stub is like this, but
   sometimes the .plt entry crosses a 64k boundary and we need to
   insert an addi to adjust r12.  */
#define PLT_CALL_STUB_SIZE (7*4)
#define ADDIS_R12_R2	0x3d820000	/* addis %r12,%r2,xxx@ha     */
#define STD_R2_40R1	0xf8410028	/* std	 %r2,40(%r1)	     */
#define LD_R11_0R12	0xe96c0000	/* ld	 %r11,xxx+0@l(%r12)  */
#define MTCTR_R11	0x7d6903a6	/* mtctr %r11		     */
#define LD_R2_0R12	0xe84c0000	/* ld	 %r2,xxx+8@l(%r12)   */
					/* ld	 %r11,xxx+16@l(%r12) */
#define BCTR		0x4e800420	/* bctr			     */


#define ADDIS_R12_R12	0x3d8c0000	/* addis %r12,%r12,off@ha  */
#define ADDI_R12_R12	0x398c0000	/* addi %r12,%r12,off@l  */
#define ADDIS_R2_R2	0x3c420000	/* addis %r2,%r2,off@ha  */
#define ADDI_R2_R2	0x38420000	/* addi  %r2,%r2,off@l   */

#define LD_R11_0R2	0xe9620000	/* ld	 %r11,xxx+0(%r2) */
#define LD_R2_0R2	0xe8420000	/* ld	 %r2,xxx+0(%r2)  */

#define LD_R2_40R1	0xe8410028	/* ld    %r2,40(%r1)     */

/* glink call stub instructions.  We enter with the index in R0.  */
#define GLINK_CALL_STUB_SIZE (16*4)
					/* 0:				*/
					/*  .quad plt0-1f		*/
					/* __glink:			*/
#define MFLR_R12	0x7d8802a6	/*  mflr %12			*/
#define BCL_20_31	0x429f0005	/*  bcl 20,31,1f		*/
					/* 1:				*/
#define MFLR_R11	0x7d6802a6	/*  mflr %11			*/
#define LD_R2_M16R11	0xe84bfff0	/*  ld %2,(0b-1b)(%11)		*/
#define MTLR_R12	0x7d8803a6	/*  mtlr %12			*/
#define ADD_R12_R2_R11	0x7d825a14	/*  add %12,%2,%11		*/
					/*  ld %11,0(%12)		*/
					/*  ld %2,8(%12)		*/
					/*  mtctr %11			*/
					/*  ld %11,16(%12)		*/
					/*  bctr			*/

/* Pad with this.  */
#define NOP		0x60000000

/* Some other nops.  */
#define CROR_151515	0x4def7b82
#define CROR_313131	0x4ffffb82

/* .glink entries for the first 32k functions are two instructions.  */
#define LI_R0_0		0x38000000	/* li    %r0,0		*/
#define B_DOT		0x48000000	/* b     .		*/

/* After that, we need two instructions to load the index, followed by
   a branch.  */
#define LIS_R0_0	0x3c000000	/* lis   %r0,0		*/
#define ORI_R0_R0_0	0x60000000	/* ori	 %r0,%r0,0	*/

/* Instructions used by the save and restore reg functions.  */
#define STD_R0_0R1	0xf8010000	/* std   %r0,0(%r1)	*/
#define STD_R0_0R12	0xf80c0000	/* std   %r0,0(%r12)	*/
#define LD_R0_0R1	0xe8010000	/* ld    %r0,0(%r1)	*/
#define LD_R0_0R12	0xe80c0000	/* ld    %r0,0(%r12)	*/
#define STFD_FR0_0R1	0xd8010000	/* stfd  %fr0,0(%r1)	*/
#define LFD_FR0_0R1	0xc8010000	/* lfd   %fr0,0(%r1)	*/
#define LI_R12_0	0x39800000	/* li    %r12,0		*/
#define STVX_VR0_R12_R0	0x7c0c01ce	/* stvx  %v0,%r12,%r0	*/
#define LVX_VR0_R12_R0	0x7c0c00ce	/* lvx   %v0,%r12,%r0	*/
#define MTLR_R0		0x7c0803a6	/* mtlr  %r0		*/
#define BLR		0x4e800020	/* blr			*/

/* Since .opd is an array of descriptors and each entry will end up
   with identical R_PPC64_RELATIVE relocs, there is really no need to
   propagate .opd relocs;  The dynamic linker should be taught to
   relocate .opd without reloc entries.  */
#ifndef NO_OPD_RELOCS
#define NO_OPD_RELOCS 0
#endif

#define ONES(n) (((bfd_vma) 1 << ((n) - 1) << 1) - 1)

/* Relocation HOWTO's.  */
static reloc_howto_type *ppc64_elf_howto_table[(int) R_PPC64_max];

static reloc_howto_type ppc64_elf_howto_raw[] = {
  /* This reloc does nothing.  */
  HOWTO (R_PPC64_NONE,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_NONE",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A standard 32 bit relocation.  */
  HOWTO (R_PPC64_ADDR32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_ADDR32",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* An absolute 26 bit branch; the lower two bits must be zero.
     FIXME: we don't check that, we just clear them.  */
  HOWTO (R_PPC64_ADDR24,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 26,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_ADDR24",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x03fffffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A standard 16 bit relocation.  */
  HOWTO (R_PPC64_ADDR16,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_ADDR16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A 16 bit relocation without overflow.  */
  HOWTO (R_PPC64_ADDR16_LO,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_ADDR16_LO",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Bits 16-31 of an address.  */
  HOWTO (R_PPC64_ADDR16_HI,	/* type */
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_ADDR16_HI",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Bits 16-31 of an address, plus 1 if the contents of the low 16
     bits, treated as a signed number, is negative.  */
  HOWTO (R_PPC64_ADDR16_HA,	/* type */
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_ha_reloc,	/* special_function */
	 "R_PPC64_ADDR16_HA",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* An absolute 16 bit branch; the lower two bits must be zero.
     FIXME: we don't check that, we just clear them.  */
  HOWTO (R_PPC64_ADDR14,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 ppc64_elf_branch_reloc, /* special_function */
	 "R_PPC64_ADDR14",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0000fffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* An absolute 16 bit branch, for which bit 10 should be set to
     indicate that the branch is expected to be taken.  The lower two
     bits must be zero.  */
  HOWTO (R_PPC64_ADDR14_BRTAKEN, /* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 ppc64_elf_brtaken_reloc, /* special_function */
	 "R_PPC64_ADDR14_BRTAKEN",/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0000fffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* An absolute 16 bit branch, for which bit 10 should be set to
     indicate that the branch is not expected to be taken.  The lower
     two bits must be zero.  */
  HOWTO (R_PPC64_ADDR14_BRNTAKEN, /* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 ppc64_elf_brtaken_reloc, /* special_function */
	 "R_PPC64_ADDR14_BRNTAKEN",/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0000fffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A relative 26 bit branch; the lower two bits must be zero.  */
  HOWTO (R_PPC64_REL24,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 26,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_branch_reloc, /* special_function */
	 "R_PPC64_REL24",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x03fffffc,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* A relative 16 bit branch; the lower two bits must be zero.  */
  HOWTO (R_PPC64_REL14,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_branch_reloc, /* special_function */
	 "R_PPC64_REL14",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0000fffc,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* A relative 16 bit branch.  Bit 10 should be set to indicate that
     the branch is expected to be taken.  The lower two bits must be
     zero.  */
  HOWTO (R_PPC64_REL14_BRTAKEN,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_brtaken_reloc, /* special_function */
	 "R_PPC64_REL14_BRTAKEN", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0000fffc,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* A relative 16 bit branch.  Bit 10 should be set to indicate that
     the branch is not expected to be taken.  The lower two bits must
     be zero.  */
  HOWTO (R_PPC64_REL14_BRNTAKEN, /* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_brtaken_reloc, /* special_function */
	 "R_PPC64_REL14_BRNTAKEN",/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0000fffc,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Like R_PPC64_ADDR16, but referring to the GOT table entry for the
     symbol.  */
  HOWTO (R_PPC64_GOT16,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_ADDR16_LO, but referring to the GOT table entry for
     the symbol.  */
  HOWTO (R_PPC64_GOT16_LO,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT16_LO",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_ADDR16_HI, but referring to the GOT table entry for
     the symbol.  */
  HOWTO (R_PPC64_GOT16_HI,	/* type */
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT16_HI",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_ADDR16_HA, but referring to the GOT table entry for
     the symbol.  */
  HOWTO (R_PPC64_GOT16_HA,	/* type */
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT16_HA",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* This is used only by the dynamic linker.  The symbol should exist
     both in the object being run and in some shared library.  The
     dynamic linker copies the data addressed by the symbol from the
     shared library into the object, because the object being
     run has to have the data at some particular address.  */
  HOWTO (R_PPC64_COPY,		/* type */
	 0,			/* rightshift */
	 0,			/* this one is variable size */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_COPY",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_ADDR64, but used when setting global offset table
     entries.  */
  HOWTO (R_PPC64_GLOB_DAT,	/* type */
	 0,			/* rightshift */
	 4,			/* size (0=byte, 1=short, 2=long, 4=64 bits) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc,  /* special_function */
	 "R_PPC64_GLOB_DAT",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ONES (64),		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Created by the link editor.  Marks a procedure linkage table
     entry for a symbol.  */
  HOWTO (R_PPC64_JMP_SLOT,	/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_JMP_SLOT",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used only by the dynamic linker.  When the object is run, this
     doubleword64 is set to the load address of the object, plus the
     addend.  */
  HOWTO (R_PPC64_RELATIVE,	/* type */
	 0,			/* rightshift */
	 4,			/* size (0=byte, 1=short, 2=long, 4=64 bits) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_RELATIVE",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ONES (64),		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_ADDR32, but may be unaligned.  */
  HOWTO (R_PPC64_UADDR32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_UADDR32",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_ADDR16, but may be unaligned.  */
  HOWTO (R_PPC64_UADDR16,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_UADDR16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* 32-bit PC relative.  */
  HOWTO (R_PPC64_REL32,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 /* FIXME: Verify.  Was complain_overflow_bitfield.  */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_REL32",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffffffff,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 32-bit relocation to the symbol's procedure linkage table.  */
  HOWTO (R_PPC64_PLT32,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_PLT32",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* 32-bit PC relative relocation to the symbol's procedure linkage table.
     FIXME: R_PPC64_PLTREL32 not supported.  */
  HOWTO (R_PPC64_PLTREL32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_PLTREL32",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffffffff,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Like R_PPC64_ADDR16_LO, but referring to the PLT table entry for
     the symbol.  */
  HOWTO (R_PPC64_PLT16_LO,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_PLT16_LO",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_ADDR16_HI, but referring to the PLT table entry for
     the symbol.  */
  HOWTO (R_PPC64_PLT16_HI,	/* type */
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_PLT16_HI",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_ADDR16_HA, but referring to the PLT table entry for
     the symbol.  */
  HOWTO (R_PPC64_PLT16_HA,	/* type */
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_PLT16_HA",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* 16-bit section relative relocation.  */
  HOWTO (R_PPC64_SECTOFF,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 ppc64_elf_sectoff_reloc, /* special_function */
	 "R_PPC64_SECTOFF",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_SECTOFF, but no overflow warning.  */
  HOWTO (R_PPC64_SECTOFF_LO,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_sectoff_reloc, /* special_function */
	 "R_PPC64_SECTOFF_LO",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* 16-bit upper half section relative relocation.  */
  HOWTO (R_PPC64_SECTOFF_HI,	/* type */
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_sectoff_reloc, /* special_function */
	 "R_PPC64_SECTOFF_HI",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* 16-bit upper half adjusted section relative relocation.  */
  HOWTO (R_PPC64_SECTOFF_HA,	/* type */
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_sectoff_ha_reloc, /* special_function */
	 "R_PPC64_SECTOFF_HA",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_REL24 without touching the two least significant bits.  */
  HOWTO (R_PPC64_REL30,		/* type */
	 2,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 30,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc, /* special_function */
	 "R_PPC64_REL30",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffffffc,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Relocs in the 64-bit PowerPC ELF ABI, not in the 32-bit ABI.  */

  /* A standard 64-bit relocation.  */
  HOWTO (R_PPC64_ADDR64,	/* type */
	 0,			/* rightshift */
	 4,			/* size (0=byte, 1=short, 2=long, 4=64 bits) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_ADDR64",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ONES (64),		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* The bits 32-47 of an address.  */
  HOWTO (R_PPC64_ADDR16_HIGHER,	/* type */
	 32,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_ADDR16_HIGHER", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* The bits 32-47 of an address, plus 1 if the contents of the low
     16 bits, treated as a signed number, is negative.  */
  HOWTO (R_PPC64_ADDR16_HIGHERA, /* type */
	 32,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_ha_reloc,	/* special_function */
	 "R_PPC64_ADDR16_HIGHERA", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* The bits 48-63 of an address.  */
  HOWTO (R_PPC64_ADDR16_HIGHEST,/* type */
	 48,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_ADDR16_HIGHEST", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* The bits 48-63 of an address, plus 1 if the contents of the low
     16 bits, treated as a signed number, is negative.  */
  HOWTO (R_PPC64_ADDR16_HIGHESTA,/* type */
	 48,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_ha_reloc,	/* special_function */
	 "R_PPC64_ADDR16_HIGHESTA", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like ADDR64, but may be unaligned.  */
  HOWTO (R_PPC64_UADDR64,	/* type */
	 0,			/* rightshift */
	 4,			/* size (0=byte, 1=short, 2=long, 4=64 bits) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_UADDR64",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ONES (64),		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* 64-bit relative relocation.  */
  HOWTO (R_PPC64_REL64,		/* type */
	 0,			/* rightshift */
	 4,			/* size (0=byte, 1=short, 2=long, 4=64 bits) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_REL64",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ONES (64),		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 64-bit relocation to the symbol's procedure linkage table.  */
  HOWTO (R_PPC64_PLT64,		/* type */
	 0,			/* rightshift */
	 4,			/* size (0=byte, 1=short, 2=long, 4=64 bits) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_PLT64",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ONES (64),		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* 64-bit PC relative relocation to the symbol's procedure linkage
     table.  */
  /* FIXME: R_PPC64_PLTREL64 not supported.  */
  HOWTO (R_PPC64_PLTREL64,	/* type */
	 0,			/* rightshift */
	 4,			/* size (0=byte, 1=short, 2=long, 4=64 bits) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_PLTREL64",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ONES (64),		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 16 bit TOC-relative relocation.  */

  /* R_PPC64_TOC16	  47	   half16*	S + A - .TOC.  */
  HOWTO (R_PPC64_TOC16,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_toc_reloc,	/* special_function */
	 "R_PPC64_TOC16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* 16 bit TOC-relative relocation without overflow.  */

  /* R_PPC64_TOC16_LO	  48	   half16	 #lo (S + A - .TOC.)  */
  HOWTO (R_PPC64_TOC16_LO,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_toc_reloc,	/* special_function */
	 "R_PPC64_TOC16_LO",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* 16 bit TOC-relative relocation, high 16 bits.  */

  /* R_PPC64_TOC16_HI	  49	   half16	 #hi (S + A - .TOC.)  */
  HOWTO (R_PPC64_TOC16_HI,	/* type */
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_toc_reloc,	/* special_function */
	 "R_PPC64_TOC16_HI",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* 16 bit TOC-relative relocation, high 16 bits, plus 1 if the
     contents of the low 16 bits, treated as a signed number, is
     negative.  */

  /* R_PPC64_TOC16_HA	  50	   half16	 #ha (S + A - .TOC.)  */
  HOWTO (R_PPC64_TOC16_HA,	/* type */
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_toc_ha_reloc, /* special_function */
	 "R_PPC64_TOC16_HA",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* 64-bit relocation; insert value of TOC base (.TOC.).  */

  /* R_PPC64_TOC		  51	   doubleword64	 .TOC.  */
  HOWTO (R_PPC64_TOC,		/* type */
	 0,			/* rightshift */
	 4,			/* size (0=byte, 1=short, 2=long, 4=64 bits) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 ppc64_elf_toc64_reloc,	/* special_function */
	 "R_PPC64_TOC",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ONES (64),		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_GOT16, but also informs the link editor that the
     value to relocate may (!) refer to a PLT entry which the link
     editor (a) may replace with the symbol value.  If the link editor
     is unable to fully resolve the symbol, it may (b) create a PLT
     entry and store the address to the new PLT entry in the GOT.
     This permits lazy resolution of function symbols at run time.
     The link editor may also skip all of this and just (c) emit a
     R_PPC64_GLOB_DAT to tie the symbol to the GOT entry.  */
  /* FIXME: R_PPC64_PLTGOT16 not implemented.  */
    HOWTO (R_PPC64_PLTGOT16,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_PLTGOT16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_PLTGOT16, but without overflow.  */
  /* FIXME: R_PPC64_PLTGOT16_LO not implemented.  */
  HOWTO (R_PPC64_PLTGOT16_LO,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_PLTGOT16_LO",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_PLT_GOT16, but using bits 16-31 of the address.  */
  /* FIXME: R_PPC64_PLTGOT16_HI not implemented.  */
  HOWTO (R_PPC64_PLTGOT16_HI,	/* type */
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_PLTGOT16_HI",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_PLT_GOT16, but using bits 16-31 of the address, plus
     1 if the contents of the low 16 bits, treated as a signed number,
     is negative.  */
  /* FIXME: R_PPC64_PLTGOT16_HA not implemented.  */
  HOWTO (R_PPC64_PLTGOT16_HA,	/* type */
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_PLTGOT16_HA",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_ADDR16, but for instructions with a DS field.  */
  HOWTO (R_PPC64_ADDR16_DS,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_ADDR16_DS",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_ADDR16_LO, but for instructions with a DS field.  */
  HOWTO (R_PPC64_ADDR16_LO_DS,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont,/* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_ADDR16_LO_DS",/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_GOT16, but for instructions with a DS field.  */
  HOWTO (R_PPC64_GOT16_DS,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT16_DS",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_GOT16_LO, but for instructions with a DS field.  */
  HOWTO (R_PPC64_GOT16_LO_DS,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT16_LO_DS",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_PLT16_LO, but for instructions with a DS field.  */
  HOWTO (R_PPC64_PLT16_LO_DS,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_PLT16_LO_DS",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_SECTOFF, but for instructions with a DS field.  */
  HOWTO (R_PPC64_SECTOFF_DS,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 ppc64_elf_sectoff_reloc, /* special_function */
	 "R_PPC64_SECTOFF_DS",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_SECTOFF_LO, but for instructions with a DS field.  */
  HOWTO (R_PPC64_SECTOFF_LO_DS, /* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_sectoff_reloc, /* special_function */
	 "R_PPC64_SECTOFF_LO_DS",/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_TOC16, but for instructions with a DS field.  */
  HOWTO (R_PPC64_TOC16_DS,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_toc_reloc,	/* special_function */
	 "R_PPC64_TOC16_DS",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_TOC16_LO, but for instructions with a DS field.  */
  HOWTO (R_PPC64_TOC16_LO_DS,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_toc_reloc,	/* special_function */
	 "R_PPC64_TOC16_LO_DS",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_PLTGOT16, but for instructions with a DS field.  */
  /* FIXME: R_PPC64_PLTGOT16_DS not implemented.  */
  HOWTO (R_PPC64_PLTGOT16_DS,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_PLTGOT16_DS",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like R_PPC64_PLTGOT16_LO, but for instructions with a DS field.  */
  /* FIXME: R_PPC64_PLTGOT16_LO not implemented.  */
  HOWTO (R_PPC64_PLTGOT16_LO_DS,/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_PLTGOT16_LO_DS",/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Marker relocs for TLS.  */
  HOWTO (R_PPC64_TLS,
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_TLS",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_PPC64_TLSGD,
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_TLSGD",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_PPC64_TLSLD,
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_PPC64_TLSLD",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Computes the load module index of the load module that contains the
     definition of its TLS sym.  */
  HOWTO (R_PPC64_DTPMOD64,
	 0,			/* rightshift */
	 4,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_DTPMOD64",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ONES (64),		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Computes a dtv-relative displacement, the difference between the value
     of sym+add and the base address of the thread-local storage block that
     contains the definition of sym, minus 0x8000.  */
  HOWTO (R_PPC64_DTPREL64,
	 0,			/* rightshift */
	 4,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_DTPREL64",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ONES (64),		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A 16 bit dtprel reloc.  */
  HOWTO (R_PPC64_DTPREL16,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_DTPREL16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like DTPREL16, but no overflow.  */
  HOWTO (R_PPC64_DTPREL16_LO,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_DTPREL16_LO",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like DTPREL16_LO, but next higher group of 16 bits.  */
  HOWTO (R_PPC64_DTPREL16_HI,
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_DTPREL16_HI",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like DTPREL16_HI, but adjust for low 16 bits.  */
  HOWTO (R_PPC64_DTPREL16_HA,
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_DTPREL16_HA",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like DTPREL16_HI, but next higher group of 16 bits.  */
  HOWTO (R_PPC64_DTPREL16_HIGHER,
	 32,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_DTPREL16_HIGHER", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like DTPREL16_HIGHER, but adjust for low 16 bits.  */
  HOWTO (R_PPC64_DTPREL16_HIGHERA,
	 32,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_DTPREL16_HIGHERA", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like DTPREL16_HIGHER, but next higher group of 16 bits.  */
  HOWTO (R_PPC64_DTPREL16_HIGHEST,
	 48,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_DTPREL16_HIGHEST", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like DTPREL16_HIGHEST, but adjust for low 16 bits.  */
  HOWTO (R_PPC64_DTPREL16_HIGHESTA,
	 48,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_DTPREL16_HIGHESTA", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like DTPREL16, but for insns with a DS field.  */
  HOWTO (R_PPC64_DTPREL16_DS,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_DTPREL16_DS",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like DTPREL16_DS, but no overflow.  */
  HOWTO (R_PPC64_DTPREL16_LO_DS,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_DTPREL16_LO_DS", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Computes a tp-relative displacement, the difference between the value of
     sym+add and the value of the thread pointer (r13).  */
  HOWTO (R_PPC64_TPREL64,
	 0,			/* rightshift */
	 4,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_TPREL64",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ONES (64),		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* A 16 bit tprel reloc.  */
  HOWTO (R_PPC64_TPREL16,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_TPREL16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like TPREL16, but no overflow.  */
  HOWTO (R_PPC64_TPREL16_LO,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_TPREL16_LO",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like TPREL16_LO, but next higher group of 16 bits.  */
  HOWTO (R_PPC64_TPREL16_HI,
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_TPREL16_HI",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like TPREL16_HI, but adjust for low 16 bits.  */
  HOWTO (R_PPC64_TPREL16_HA,
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_TPREL16_HA",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like TPREL16_HI, but next higher group of 16 bits.  */
  HOWTO (R_PPC64_TPREL16_HIGHER,
	 32,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_TPREL16_HIGHER",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like TPREL16_HIGHER, but adjust for low 16 bits.  */
  HOWTO (R_PPC64_TPREL16_HIGHERA,
	 32,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_TPREL16_HIGHERA", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like TPREL16_HIGHER, but next higher group of 16 bits.  */
  HOWTO (R_PPC64_TPREL16_HIGHEST,
	 48,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_TPREL16_HIGHEST", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like TPREL16_HIGHEST, but adjust for low 16 bits.  */
  HOWTO (R_PPC64_TPREL16_HIGHESTA,
	 48,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_TPREL16_HIGHESTA", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like TPREL16, but for insns with a DS field.  */
  HOWTO (R_PPC64_TPREL16_DS,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_TPREL16_DS",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like TPREL16_DS, but no overflow.  */
  HOWTO (R_PPC64_TPREL16_LO_DS,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_TPREL16_LO_DS", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Allocates two contiguous entries in the GOT to hold a tls_index structure,
     with values (sym+add)@dtpmod and (sym+add)@dtprel, and computes the offset
     to the first entry relative to the TOC base (r2).  */
  HOWTO (R_PPC64_GOT_TLSGD16,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_TLSGD16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like GOT_TLSGD16, but no overflow.  */
  HOWTO (R_PPC64_GOT_TLSGD16_LO,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_TLSGD16_LO", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like GOT_TLSGD16_LO, but next higher group of 16 bits.  */
  HOWTO (R_PPC64_GOT_TLSGD16_HI,
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_TLSGD16_HI", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like GOT_TLSGD16_HI, but adjust for low 16 bits.  */
  HOWTO (R_PPC64_GOT_TLSGD16_HA,
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_TLSGD16_HA", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Allocates two contiguous entries in the GOT to hold a tls_index structure,
     with values (sym+add)@dtpmod and zero, and computes the offset to the
     first entry relative to the TOC base (r2).  */
  HOWTO (R_PPC64_GOT_TLSLD16,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_TLSLD16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like GOT_TLSLD16, but no overflow.  */
  HOWTO (R_PPC64_GOT_TLSLD16_LO,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_TLSLD16_LO", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like GOT_TLSLD16_LO, but next higher group of 16 bits.  */
  HOWTO (R_PPC64_GOT_TLSLD16_HI,
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_TLSLD16_HI", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like GOT_TLSLD16_HI, but adjust for low 16 bits.  */
  HOWTO (R_PPC64_GOT_TLSLD16_HA,
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_TLSLD16_HA", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Allocates an entry in the GOT with value (sym+add)@dtprel, and computes
     the offset to the entry relative to the TOC base (r2).  */
  HOWTO (R_PPC64_GOT_DTPREL16_DS,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_DTPREL16_DS", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like GOT_DTPREL16_DS, but no overflow.  */
  HOWTO (R_PPC64_GOT_DTPREL16_LO_DS,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_DTPREL16_LO_DS", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like GOT_DTPREL16_LO_DS, but next higher group of 16 bits.  */
  HOWTO (R_PPC64_GOT_DTPREL16_HI,
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_DTPREL16_HI", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like GOT_DTPREL16_HI, but adjust for low 16 bits.  */
  HOWTO (R_PPC64_GOT_DTPREL16_HA,
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_DTPREL16_HA", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Allocates an entry in the GOT with value (sym+add)@tprel, and computes the
     offset to the entry relative to the TOC base (r2).  */
  HOWTO (R_PPC64_GOT_TPREL16_DS,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_TPREL16_DS", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like GOT_TPREL16_DS, but no overflow.  */
  HOWTO (R_PPC64_GOT_TPREL16_LO_DS,
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_TPREL16_LO_DS", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfffc,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like GOT_TPREL16_LO_DS, but next higher group of 16 bits.  */
  HOWTO (R_PPC64_GOT_TPREL16_HI,
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_TPREL16_HI", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Like GOT_TPREL16_HI, but adjust for low 16 bits.  */
  HOWTO (R_PPC64_GOT_TPREL16_HA,
	 16,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 ppc64_elf_unhandled_reloc, /* special_function */
	 "R_PPC64_GOT_TPREL16_HA", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* GNU extension to record C++ vtable hierarchy.  */
  HOWTO (R_PPC64_GNU_VTINHERIT,	/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 NULL,			/* special_function */
	 "R_PPC64_GNU_VTINHERIT", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* GNU extension to record C++ vtable member usage.  */
  HOWTO (R_PPC64_GNU_VTENTRY,	/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 NULL,			/* special_function */
	 "R_PPC64_GNU_VTENTRY",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */
};


/* Initialize the ppc64_elf_howto_table, so that linear accesses can
   be done.  */

static void
ppc_howto_init (void)
{
  unsigned int i, type;

  for (i = 0;
       i < sizeof (ppc64_elf_howto_raw) / sizeof (ppc64_elf_howto_raw[0]);
       i++)
    {
      type = ppc64_elf_howto_raw[i].type;
      BFD_ASSERT (type < (sizeof (ppc64_elf_howto_table)
			  / sizeof (ppc64_elf_howto_table[0])));
      ppc64_elf_howto_table[type] = &ppc64_elf_howto_raw[i];
    }
}

static reloc_howto_type *
ppc64_elf_reloc_type_lookup (bfd *abfd ATTRIBUTE_UNUSED,
			     bfd_reloc_code_real_type code)
{
  enum elf_ppc64_reloc_type r = R_PPC64_NONE;

  if (!ppc64_elf_howto_table[R_PPC64_ADDR32])
    /* Initialize howto table if needed.  */
    ppc_howto_init ();

  switch (code)
    {
    default:
      return NULL;

    case BFD_RELOC_NONE:			r = R_PPC64_NONE;
      break;
    case BFD_RELOC_32:				r = R_PPC64_ADDR32;
      break;
    case BFD_RELOC_PPC_BA26:			r = R_PPC64_ADDR24;
      break;
    case BFD_RELOC_16:				r = R_PPC64_ADDR16;
      break;
    case BFD_RELOC_LO16:			r = R_PPC64_ADDR16_LO;
      break;
    case BFD_RELOC_HI16:			r = R_PPC64_ADDR16_HI;
      break;
    case BFD_RELOC_HI16_S:			r = R_PPC64_ADDR16_HA;
      break;
    case BFD_RELOC_PPC_BA16:			r = R_PPC64_ADDR14;
      break;
    case BFD_RELOC_PPC_BA16_BRTAKEN:		r = R_PPC64_ADDR14_BRTAKEN;
      break;
    case BFD_RELOC_PPC_BA16_BRNTAKEN:		r = R_PPC64_ADDR14_BRNTAKEN;
      break;
    case BFD_RELOC_PPC_B26:			r = R_PPC64_REL24;
      break;
    case BFD_RELOC_PPC_B16:			r = R_PPC64_REL14;
      break;
    case BFD_RELOC_PPC_B16_BRTAKEN:		r = R_PPC64_REL14_BRTAKEN;
      break;
    case BFD_RELOC_PPC_B16_BRNTAKEN:		r = R_PPC64_REL14_BRNTAKEN;
      break;
    case BFD_RELOC_16_GOTOFF:			r = R_PPC64_GOT16;
      break;
    case BFD_RELOC_LO16_GOTOFF:			r = R_PPC64_GOT16_LO;
      break;
    case BFD_RELOC_HI16_GOTOFF:			r = R_PPC64_GOT16_HI;
      break;
    case BFD_RELOC_HI16_S_GOTOFF:		r = R_PPC64_GOT16_HA;
      break;
    case BFD_RELOC_PPC_COPY:			r = R_PPC64_COPY;
      break;
    case BFD_RELOC_PPC_GLOB_DAT:		r = R_PPC64_GLOB_DAT;
      break;
    case BFD_RELOC_32_PCREL:			r = R_PPC64_REL32;
      break;
    case BFD_RELOC_32_PLTOFF:			r = R_PPC64_PLT32;
      break;
    case BFD_RELOC_32_PLT_PCREL:		r = R_PPC64_PLTREL32;
      break;
    case BFD_RELOC_LO16_PLTOFF:			r = R_PPC64_PLT16_LO;
      break;
    case BFD_RELOC_HI16_PLTOFF:			r = R_PPC64_PLT16_HI;
      break;
    case BFD_RELOC_HI16_S_PLTOFF:		r = R_PPC64_PLT16_HA;
      break;
    case BFD_RELOC_16_BASEREL:			r = R_PPC64_SECTOFF;
      break;
    case BFD_RELOC_LO16_BASEREL:		r = R_PPC64_SECTOFF_LO;
      break;
    case BFD_RELOC_HI16_BASEREL:		r = R_PPC64_SECTOFF_HI;
      break;
    case BFD_RELOC_HI16_S_BASEREL:		r = R_PPC64_SECTOFF_HA;
      break;
    case BFD_RELOC_CTOR:			r = R_PPC64_ADDR64;
      break;
    case BFD_RELOC_64:				r = R_PPC64_ADDR64;
      break;
    case BFD_RELOC_PPC64_HIGHER:		r = R_PPC64_ADDR16_HIGHER;
      break;
    case BFD_RELOC_PPC64_HIGHER_S:		r = R_PPC64_ADDR16_HIGHERA;
      break;
    case BFD_RELOC_PPC64_HIGHEST:		r = R_PPC64_ADDR16_HIGHEST;
      break;
    case BFD_RELOC_PPC64_HIGHEST_S:		r = R_PPC64_ADDR16_HIGHESTA;
      break;
    case BFD_RELOC_64_PCREL:			r = R_PPC64_REL64;
      break;
    case BFD_RELOC_64_PLTOFF:			r = R_PPC64_PLT64;
      break;
    case BFD_RELOC_64_PLT_PCREL:		r = R_PPC64_PLTREL64;
      break;
    case BFD_RELOC_PPC_TOC16:			r = R_PPC64_TOC16;
      break;
    case BFD_RELOC_PPC64_TOC16_LO:		r = R_PPC64_TOC16_LO;
      break;
    case BFD_RELOC_PPC64_TOC16_HI:		r = R_PPC64_TOC16_HI;
      break;
    case BFD_RELOC_PPC64_TOC16_HA:		r = R_PPC64_TOC16_HA;
      break;
    case BFD_RELOC_PPC64_TOC:			r = R_PPC64_TOC;
      break;
    case BFD_RELOC_PPC64_PLTGOT16:		r = R_PPC64_PLTGOT16;
      break;
    case BFD_RELOC_PPC64_PLTGOT16_LO:		r = R_PPC64_PLTGOT16_LO;
      break;
    case BFD_RELOC_PPC64_PLTGOT16_HI:		r = R_PPC64_PLTGOT16_HI;
      break;
    case BFD_RELOC_PPC64_PLTGOT16_HA:		r = R_PPC64_PLTGOT16_HA;
      break;
    case BFD_RELOC_PPC64_ADDR16_DS:		r = R_PPC64_ADDR16_DS;
      break;
    case BFD_RELOC_PPC64_ADDR16_LO_DS:		r = R_PPC64_ADDR16_LO_DS;
      break;
    case BFD_RELOC_PPC64_GOT16_DS:		r = R_PPC64_GOT16_DS;
      break;
    case BFD_RELOC_PPC64_GOT16_LO_DS:		r = R_PPC64_GOT16_LO_DS;
      break;
    case BFD_RELOC_PPC64_PLT16_LO_DS:		r = R_PPC64_PLT16_LO_DS;
      break;
    case BFD_RELOC_PPC64_SECTOFF_DS:		r = R_PPC64_SECTOFF_DS;
      break;
    case BFD_RELOC_PPC64_SECTOFF_LO_DS:		r = R_PPC64_SECTOFF_LO_DS;
      break;
    case BFD_RELOC_PPC64_TOC16_DS:		r = R_PPC64_TOC16_DS;
      break;
    case BFD_RELOC_PPC64_TOC16_LO_DS:		r = R_PPC64_TOC16_LO_DS;
      break;
    case BFD_RELOC_PPC64_PLTGOT16_DS:		r = R_PPC64_PLTGOT16_DS;
      break;
    case BFD_RELOC_PPC64_PLTGOT16_LO_DS:	r = R_PPC64_PLTGOT16_LO_DS;
      break;
    case BFD_RELOC_PPC_TLS:			r = R_PPC64_TLS;
      break;
    case BFD_RELOC_PPC_TLSGD:			r = R_PPC64_TLSGD;
      break;
    case BFD_RELOC_PPC_TLSLD:			r = R_PPC64_TLSLD;
      break;
    case BFD_RELOC_PPC_DTPMOD:			r = R_PPC64_DTPMOD64;
      break;
    case BFD_RELOC_PPC_TPREL16:			r = R_PPC64_TPREL16;
      break;
    case BFD_RELOC_PPC_TPREL16_LO:		r = R_PPC64_TPREL16_LO;
      break;
    case BFD_RELOC_PPC_TPREL16_HI:		r = R_PPC64_TPREL16_HI;
      break;
    case BFD_RELOC_PPC_TPREL16_HA:		r = R_PPC64_TPREL16_HA;
      break;
    case BFD_RELOC_PPC_TPREL:			r = R_PPC64_TPREL64;
      break;
    case BFD_RELOC_PPC_DTPREL16:		r = R_PPC64_DTPREL16;
      break;
    case BFD_RELOC_PPC_DTPREL16_LO:		r = R_PPC64_DTPREL16_LO;
      break;
    case BFD_RELOC_PPC_DTPREL16_HI:		r = R_PPC64_DTPREL16_HI;
      break;
    case BFD_RELOC_PPC_DTPREL16_HA:		r = R_PPC64_DTPREL16_HA;
      break;
    case BFD_RELOC_PPC_DTPREL:			r = R_PPC64_DTPREL64;
      break;
    case BFD_RELOC_PPC_GOT_TLSGD16:		r = R_PPC64_GOT_TLSGD16;
      break;
    case BFD_RELOC_PPC_GOT_TLSGD16_LO:		r = R_PPC64_GOT_TLSGD16_LO;
      break;
    case BFD_RELOC_PPC_GOT_TLSGD16_HI:		r = R_PPC64_GOT_TLSGD16_HI;
      break;
    case BFD_RELOC_PPC_GOT_TLSGD16_HA:		r = R_PPC64_GOT_TLSGD16_HA;
      break;
    case BFD_RELOC_PPC_GOT_TLSLD16:		r = R_PPC64_GOT_TLSLD16;
      break;
    case BFD_RELOC_PPC_GOT_TLSLD16_LO:		r = R_PPC64_GOT_TLSLD16_LO;
      break;
    case BFD_RELOC_PPC_GOT_TLSLD16_HI:		r = R_PPC64_GOT_TLSLD16_HI;
      break;
    case BFD_RELOC_PPC_GOT_TLSLD16_HA:		r = R_PPC64_GOT_TLSLD16_HA;
      break;
    case BFD_RELOC_PPC_GOT_TPREL16:		r = R_PPC64_GOT_TPREL16_DS;
      break;
    case BFD_RELOC_PPC_GOT_TPREL16_LO:		r = R_PPC64_GOT_TPREL16_LO_DS;
      break;
    case BFD_RELOC_PPC_GOT_TPREL16_HI:		r = R_PPC64_GOT_TPREL16_HI;
      break;
    case BFD_RELOC_PPC_GOT_TPREL16_HA:		r = R_PPC64_GOT_TPREL16_HA;
      break;
    case BFD_RELOC_PPC_GOT_DTPREL16:		r = R_PPC64_GOT_DTPREL16_DS;
      break;
    case BFD_RELOC_PPC_GOT_DTPREL16_LO:		r = R_PPC64_GOT_DTPREL16_LO_DS;
      break;
    case BFD_RELOC_PPC_GOT_DTPREL16_HI:		r = R_PPC64_GOT_DTPREL16_HI;
      break;
    case BFD_RELOC_PPC_GOT_DTPREL16_HA:		r = R_PPC64_GOT_DTPREL16_HA;
      break;
    case BFD_RELOC_PPC64_TPREL16_DS:		r = R_PPC64_TPREL16_DS;
      break;
    case BFD_RELOC_PPC64_TPREL16_LO_DS:		r = R_PPC64_TPREL16_LO_DS;
      break;
    case BFD_RELOC_PPC64_TPREL16_HIGHER:	r = R_PPC64_TPREL16_HIGHER;
      break;
    case BFD_RELOC_PPC64_TPREL16_HIGHERA:	r = R_PPC64_TPREL16_HIGHERA;
      break;
    case BFD_RELOC_PPC64_TPREL16_HIGHEST:	r = R_PPC64_TPREL16_HIGHEST;
      break;
    case BFD_RELOC_PPC64_TPREL16_HIGHESTA:	r = R_PPC64_TPREL16_HIGHESTA;
      break;
    case BFD_RELOC_PPC64_DTPREL16_DS:		r = R_PPC64_DTPREL16_DS;
      break;
    case BFD_RELOC_PPC64_DTPREL16_LO_DS:	r = R_PPC64_DTPREL16_LO_DS;
      break;
    case BFD_RELOC_PPC64_DTPREL16_HIGHER:	r = R_PPC64_DTPREL16_HIGHER;
      break;
    case BFD_RELOC_PPC64_DTPREL16_HIGHERA:	r = R_PPC64_DTPREL16_HIGHERA;
      break;
    case BFD_RELOC_PPC64_DTPREL16_HIGHEST:	r = R_PPC64_DTPREL16_HIGHEST;
      break;
    case BFD_RELOC_PPC64_DTPREL16_HIGHESTA:	r = R_PPC64_DTPREL16_HIGHESTA;
      break;
    case BFD_RELOC_VTABLE_INHERIT:		r = R_PPC64_GNU_VTINHERIT;
      break;
    case BFD_RELOC_VTABLE_ENTRY:		r = R_PPC64_GNU_VTENTRY;
      break;
    }

  return ppc64_elf_howto_table[r];
};

static reloc_howto_type *
ppc64_elf_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED,
			     const char *r_name)
{
  unsigned int i;

  for (i = 0;
       i < sizeof (ppc64_elf_howto_raw) / sizeof (ppc64_elf_howto_raw[0]);
       i++)
    if (ppc64_elf_howto_raw[i].name != NULL
	&& strcasecmp (ppc64_elf_howto_raw[i].name, r_name) == 0)
      return &ppc64_elf_howto_raw[i];

  return NULL;
}

/* Set the howto pointer for a PowerPC ELF reloc.  */

static void
ppc64_elf_info_to_howto (bfd *abfd ATTRIBUTE_UNUSED, arelent *cache_ptr,
			 Elf_Internal_Rela *dst)
{
  unsigned int type;

  /* Initialize howto table if needed.  */
  if (!ppc64_elf_howto_table[R_PPC64_ADDR32])
    ppc_howto_init ();

  type = ELF64_R_TYPE (dst->r_info);
  if (type >= (sizeof (ppc64_elf_howto_table)
	       / sizeof (ppc64_elf_howto_table[0])))
    {
      (*_bfd_error_handler) (_("%B: invalid relocation type %d"),
			     abfd, (int) type);
      type = R_PPC64_NONE;
    }
  cache_ptr->howto = ppc64_elf_howto_table[type];
}

/* Handle the R_PPC64_ADDR16_HA and similar relocs.  */

static bfd_reloc_status_type
ppc64_elf_ha_reloc (bfd *abfd, arelent *reloc_entry, asymbol *symbol,
		    void *data, asection *input_section,
		    bfd *output_bfd, char **error_message)
{
  /* If this is a relocatable link (output_bfd test tells us), just
     call the generic function.  Any adjustment will be done at final
     link time.  */
  if (output_bfd != NULL)
    return bfd_elf_generic_reloc (abfd, reloc_entry, symbol, data,
				  input_section, output_bfd, error_message);

  /* Adjust the addend for sign extension of the low 16 bits.
     We won't actually be using the low 16 bits, so trashing them
     doesn't matter.  */
  reloc_entry->addend += 0x8000;
  return bfd_reloc_continue;
}

static bfd_reloc_status_type
ppc64_elf_branch_reloc (bfd *abfd, arelent *reloc_entry, asymbol *symbol,
			void *data, asection *input_section,
			bfd *output_bfd, char **error_message)
{
  if (output_bfd != NULL)
    return bfd_elf_generic_reloc (abfd, reloc_entry, symbol, data,
				  input_section, output_bfd, error_message);

  if (strcmp (symbol->section->name, ".opd") == 0
      && (symbol->section->owner->flags & DYNAMIC) == 0)
    {
      bfd_vma dest = opd_entry_value (symbol->section,
				      symbol->value + reloc_entry->addend,
				      NULL, NULL);
      if (dest != (bfd_vma) -1)
	reloc_entry->addend = dest - (symbol->value
				      + symbol->section->output_section->vma
				      + symbol->section->output_offset);
    }
  return bfd_reloc_continue;
}

static bfd_reloc_status_type
ppc64_elf_brtaken_reloc (bfd *abfd, arelent *reloc_entry, asymbol *symbol,
			 void *data, asection *input_section,
			 bfd *output_bfd, char **error_message)
{
  long insn;
  enum elf_ppc64_reloc_type r_type;
  bfd_size_type octets;
  /* Disabled until we sort out how ld should choose 'y' vs 'at'.  */
  bfd_boolean is_power4 = FALSE;

  /* If this is a relocatable link (output_bfd test tells us), just
     call the generic function.  Any adjustment will be done at final
     link time.  */
  if (output_bfd != NULL)
    return bfd_elf_generic_reloc (abfd, reloc_entry, symbol, data,
				  input_section, output_bfd, error_message);

  octets = reloc_entry->address * bfd_octets_per_byte (abfd);
  insn = bfd_get_32 (abfd, (bfd_byte *) data + octets);
  insn &= ~(0x01 << 21);
  r_type = reloc_entry->howto->type;
  if (r_type == R_PPC64_ADDR14_BRTAKEN
      || r_type == R_PPC64_REL14_BRTAKEN)
    insn |= 0x01 << 21; /* 'y' or 't' bit, lowest bit of BO field.  */

  if (is_power4)
    {
      /* Set 'a' bit.  This is 0b00010 in BO field for branch
	 on CR(BI) insns (BO == 001at or 011at), and 0b01000
	 for branch on CTR insns (BO == 1a00t or 1a01t).  */
      if ((insn & (0x14 << 21)) == (0x04 << 21))
	insn |= 0x02 << 21;
      else if ((insn & (0x14 << 21)) == (0x10 << 21))
	insn |= 0x08 << 21;
      else
	goto out;
    }
  else
    {
      bfd_vma target = 0;
      bfd_vma from;

      if (!bfd_is_com_section (symbol->section))
	target = symbol->value;
      target += symbol->section->output_section->vma;
      target += symbol->section->output_offset;
      target += reloc_entry->addend;

      from = (reloc_entry->address
	      + input_section->output_offset
	      + input_section->output_section->vma);

      /* Invert 'y' bit if not the default.  */
      if ((bfd_signed_vma) (target - from) < 0)
	insn ^= 0x01 << 21;
    }
  bfd_put_32 (abfd, insn, (bfd_byte *) data + octets);
 out:
  return ppc64_elf_branch_reloc (abfd, reloc_entry, symbol, data,
				 input_section, output_bfd, error_message);
}

static bfd_reloc_status_type
ppc64_elf_sectoff_reloc (bfd *abfd, arelent *reloc_entry, asymbol *symbol,
			 void *data, asection *input_section,
			 bfd *output_bfd, char **error_message)
{
  /* If this is a relocatable link (output_bfd test tells us), just
     call the generic function.  Any adjustment will be done at final
     link time.  */
  if (output_bfd != NULL)
    return bfd_elf_generic_reloc (abfd, reloc_entry, symbol, data,
				  input_section, output_bfd, error_message);

  /* Subtract the symbol section base address.  */
  reloc_entry->addend -= symbol->section->output_section->vma;
  return bfd_reloc_continue;
}

static bfd_reloc_status_type
ppc64_elf_sectoff_ha_reloc (bfd *abfd, arelent *reloc_entry, asymbol *symbol,
			    void *data, asection *input_section,
			    bfd *output_bfd, char **error_message)
{
  /* If this is a relocatable link (output_bfd test tells us), just
     call the generic function.  Any adjustment will be done at final
     link time.  */
  if (output_bfd != NULL)
    return bfd_elf_generic_reloc (abfd, reloc_entry, symbol, data,
				  input_section, output_bfd, error_message);

  /* Subtract the symbol section base address.  */
  reloc_entry->addend -= symbol->section->output_section->vma;

  /* Adjust the addend for sign extension of the low 16 bits.  */
  reloc_entry->addend += 0x8000;
  return bfd_reloc_continue;
}

static bfd_reloc_status_type
ppc64_elf_toc_reloc (bfd *abfd, arelent *reloc_entry, asymbol *symbol,
		     void *data, asection *input_section,
		     bfd *output_bfd, char **error_message)
{
  bfd_vma TOCstart;

  /* If this is a relocatable link (output_bfd test tells us), just
     call the generic function.  Any adjustment will be done at final
     link time.  */
  if (output_bfd != NULL)
    return bfd_elf_generic_reloc (abfd, reloc_entry, symbol, data,
				  input_section, output_bfd, error_message);

  TOCstart = _bfd_get_gp_value (input_section->output_section->owner);
  if (TOCstart == 0)
    TOCstart = ppc64_elf_toc (input_section->output_section->owner);

  /* Subtract the TOC base address.  */
  reloc_entry->addend -= TOCstart + TOC_BASE_OFF;
  return bfd_reloc_continue;
}

static bfd_reloc_status_type
ppc64_elf_toc_ha_reloc (bfd *abfd, arelent *reloc_entry, asymbol *symbol,
			void *data, asection *input_section,
			bfd *output_bfd, char **error_message)
{
  bfd_vma TOCstart;

  /* If this is a relocatable link (output_bfd test tells us), just
     call the generic function.  Any adjustment will be done at final
     link time.  */
  if (output_bfd != NULL)
    return bfd_elf_generic_reloc (abfd, reloc_entry, symbol, data,
				  input_section, output_bfd, error_message);

  TOCstart = _bfd_get_gp_value (input_section->output_section->owner);
  if (TOCstart == 0)
    TOCstart = ppc64_elf_toc (input_section->output_section->owner);

  /* Subtract the TOC base address.  */
  reloc_entry->addend -= TOCstart + TOC_BASE_OFF;

  /* Adjust the addend for sign extension of the low 16 bits.  */
  reloc_entry->addend += 0x8000;
  return bfd_reloc_continue;
}

static bfd_reloc_status_type
ppc64_elf_toc64_reloc (bfd *abfd, arelent *reloc_entry, asymbol *symbol,
		       void *data, asection *input_section,
		       bfd *output_bfd, char **error_message)
{
  bfd_vma TOCstart;
  bfd_size_type octets;

  /* If this is a relocatable link (output_bfd test tells us), just
     call the generic function.  Any adjustment will be done at final
     link time.  */
  if (output_bfd != NULL)
    return bfd_elf_generic_reloc (abfd, reloc_entry, symbol, data,
				  input_section, output_bfd, error_message);

  TOCstart = _bfd_get_gp_value (input_section->output_section->owner);
  if (TOCstart == 0)
    TOCstart = ppc64_elf_toc (input_section->output_section->owner);

  octets = reloc_entry->address * bfd_octets_per_byte (abfd);
  bfd_put_64 (abfd, TOCstart + TOC_BASE_OFF, (bfd_byte *) data + octets);
  return bfd_reloc_ok;
}

static bfd_reloc_status_type
ppc64_elf_unhandled_reloc (bfd *abfd, arelent *reloc_entry, asymbol *symbol,
			   void *data, asection *input_section,
			   bfd *output_bfd, char **error_message)
{
  /* If this is a relocatable link (output_bfd test tells us), just
     call the generic function.  Any adjustment will be done at final
     link time.  */
  if (output_bfd != NULL)
    return bfd_elf_generic_reloc (abfd, reloc_entry, symbol, data,
				  input_section, output_bfd, error_message);

  if (error_message != NULL)
    {
      static char buf[60];
      sprintf (buf, "generic linker can't handle %s",
	       reloc_entry->howto->name);
      *error_message = buf;
    }
  return bfd_reloc_dangerous;
}

struct ppc64_elf_obj_tdata
{
  struct elf_obj_tdata elf;

  /* Shortcuts to dynamic linker sections.  */
  asection *got;
  asection *relgot;

  /* Used during garbage collection.  We attach global symbols defined
     on removed .opd entries to this section so that the sym is removed.  */
  asection *deleted_section;

  /* TLS local dynamic got entry handling.  Suppose for multiple GOT
     sections means we potentially need one of these for each input bfd.  */
  union {
    bfd_signed_vma refcount;
    bfd_vma offset;
  } tlsld_got;

  /* A copy of relocs before they are modified for --emit-relocs.  */
  Elf_Internal_Rela *opd_relocs;
};

#define ppc64_elf_tdata(bfd) \
  ((struct ppc64_elf_obj_tdata *) (bfd)->tdata.any)

#define ppc64_tlsld_got(bfd) \
  (&ppc64_elf_tdata (bfd)->tlsld_got)

/* Override the generic function because we store some extras.  */

static bfd_boolean
ppc64_elf_mkobject (bfd *abfd)
{
  if (abfd->tdata.any == NULL)
    {
      bfd_size_type amt = sizeof (struct ppc64_elf_obj_tdata);
      abfd->tdata.any = bfd_zalloc (abfd, amt);
      if (abfd->tdata.any == NULL)
	return FALSE;
    }
  return bfd_elf_mkobject (abfd);
}

/* Return 1 if target is one of ours.  */

static bfd_boolean
is_ppc64_elf_target (const struct bfd_target *targ)
{
  extern const bfd_target bfd_elf64_powerpc_vec;
  extern const bfd_target bfd_elf64_powerpcle_vec;

  return targ == &bfd_elf64_powerpc_vec || targ == &bfd_elf64_powerpcle_vec;
}

/* Fix bad default arch selected for a 64 bit input bfd when the
   default is 32 bit.  */

static bfd_boolean
ppc64_elf_object_p (bfd *abfd)
{
  if (abfd->arch_info->the_default && abfd->arch_info->bits_per_word == 32)
    {
      Elf_Internal_Ehdr *i_ehdr = elf_elfheader (abfd);

      if (i_ehdr->e_ident[EI_CLASS] == ELFCLASS64)
	{
	  /* Relies on arch after 32 bit default being 64 bit default.  */
	  abfd->arch_info = abfd->arch_info->next;
	  BFD_ASSERT (abfd->arch_info->bits_per_word == 64);
	}
    }
  return TRUE;
}

/* Support for core dump NOTE sections.  */

static bfd_boolean
ppc64_elf_grok_prstatus (bfd *abfd, Elf_Internal_Note *note)
{
  size_t offset, size;

  if (note->descsz != 504)
    return FALSE;

  /* pr_cursig */
  elf_tdata (abfd)->core_signal = bfd_get_16 (abfd, note->descdata + 12);

  /* pr_pid */
  elf_tdata (abfd)->core_pid = bfd_get_32 (abfd, note->descdata + 32);

  /* pr_reg */
  offset = 112;
  size = 384;

  /* Make a ".reg/999" section.  */
  return _bfd_elfcore_make_pseudosection (abfd, ".reg",
					  size, note->descpos + offset);
}

static bfd_boolean
ppc64_elf_grok_psinfo (bfd *abfd, Elf_Internal_Note *note)
{
  if (note->descsz != 136)
    return FALSE;

  elf_tdata (abfd)->core_program
    = _bfd_elfcore_strndup (abfd, note->descdata + 40, 16);
  elf_tdata (abfd)->core_command
    = _bfd_elfcore_strndup (abfd, note->descdata + 56, 80);

  return TRUE;
}

static char *
ppc64_elf_write_core_note (bfd *abfd, char *buf, int *bufsiz, int note_type,
			   ...)
{
  switch (note_type)
    {
    default:
      return NULL;

    case NT_PRPSINFO:
      {
	char data[136];
	va_list ap;

	va_start (ap, note_type);
	memset (data, 0, 40);
	strncpy (data + 40, va_arg (ap, const char *), 16);
	strncpy (data + 56, va_arg (ap, const char *), 80);
	va_end (ap);
	return elfcore_write_note (abfd, buf, bufsiz,
				   "CORE", note_type, data, sizeof (data));
      }

    case NT_PRSTATUS:
      {
	char data[504];
	va_list ap;
	long pid;
	int cursig;
	const void *greg;

	va_start (ap, note_type);
	memset (data, 0, 112);
	pid = va_arg (ap, long);
	bfd_put_32 (abfd, pid, data + 32);
	cursig = va_arg (ap, int);
	bfd_put_16 (abfd, cursig, data + 12);
	greg = va_arg (ap, const void *);
	memcpy (data + 112, greg, 384);
	memset (data + 496, 0, 8);
	va_end (ap);
	return elfcore_write_note (abfd, buf, bufsiz,
				   "CORE", note_type, data, sizeof (data));
      }
    }
}

/* Merge backend specific data from an object file to the output
   object file when linking.  */

static bfd_boolean
ppc64_elf_merge_private_bfd_data (bfd *ibfd, bfd *obfd)
{
  /* Check if we have the same endianess.  */
  if (ibfd->xvec->byteorder != obfd->xvec->byteorder
      && ibfd->xvec->byteorder != BFD_ENDIAN_UNKNOWN
      && obfd->xvec->byteorder != BFD_ENDIAN_UNKNOWN)
    {
      const char *msg;

      if (bfd_big_endian (ibfd))
	msg = _("%B: compiled for a big endian system "
		"and target is little endian");
      else
	msg = _("%B: compiled for a little endian system "
		"and target is big endian");

      (*_bfd_error_handler) (msg, ibfd);

      bfd_set_error (bfd_error_wrong_format);
      return FALSE;
    }

  return TRUE;
}

/* Add extra PPC sections.  */

static const struct bfd_elf_special_section ppc64_elf_special_sections[]=
{
  { STRING_COMMA_LEN (".plt"),    0, SHT_NOBITS,   0 },
  { STRING_COMMA_LEN (".sbss"),  -2, SHT_NOBITS,   SHF_ALLOC + SHF_WRITE },
  { STRING_COMMA_LEN (".sdata"), -2, SHT_PROGBITS, SHF_ALLOC + SHF_WRITE },
  { STRING_COMMA_LEN (".toc"),    0, SHT_PROGBITS, SHF_ALLOC + SHF_WRITE },
  { STRING_COMMA_LEN (".toc1"),   0, SHT_PROGBITS, SHF_ALLOC + SHF_WRITE },
  { STRING_COMMA_LEN (".tocbss"), 0, SHT_NOBITS,   SHF_ALLOC + SHF_WRITE },
  { NULL,                     0,  0, 0,            0 }
};

enum _ppc64_sec_type {
  sec_normal = 0,
  sec_opd = 1,
  sec_toc = 2
};

struct _ppc64_elf_section_data
{
  struct bfd_elf_section_data elf;

  /* An array with one entry for each opd function descriptor.  */
  union
  {
    /* Points to the function code section for local opd entries.  */
    asection **opd_func_sec;
    /* After editing .opd, adjust references to opd local syms.  */
    long *opd_adjust;

    /* An array for toc sections, indexed by offset/8.  */
    struct _toc_sec_data
    {
      /* Specifies the relocation symbol index used at a given toc offset.  */
      unsigned *symndx;

      /* And the relocation addend.  */
      bfd_vma *add;
    } toc;
  } u;

  enum _ppc64_sec_type sec_type:2;

  /* Flag set when small branches are detected.  Used to
     select suitable defaults for the stub group size.  */
  unsigned int has_14bit_branch:1;
};

#define ppc64_elf_section_data(sec) \
  ((struct _ppc64_elf_section_data *) elf_section_data (sec))

static bfd_boolean
ppc64_elf_new_section_hook (bfd *abfd, asection *sec)
{
  if (!sec->used_by_bfd)
    {
      struct _ppc64_elf_section_data *sdata;
      bfd_size_type amt = sizeof (*sdata);

      sdata = bfd_zalloc (abfd, amt);
      if (sdata == NULL)
	return FALSE;
      sec->used_by_bfd = sdata;
    }

  return _bfd_elf_new_section_hook (abfd, sec);
}

static void *
get_opd_info (asection * sec)
{
  if (sec != NULL
      && ppc64_elf_section_data (sec) != NULL
      && ppc64_elf_section_data (sec)->sec_type == sec_opd)
    return ppc64_elf_section_data (sec)->u.opd_adjust;
  return NULL;
}

/* Parameters for the qsort hook.  */
static asection *synthetic_opd;
static bfd_boolean synthetic_relocatable;

/* qsort comparison function for ppc64_elf_get_synthetic_symtab.  */

static int
compare_symbols (const void *ap, const void *bp)
{
  const asymbol *a = * (const asymbol **) ap;
  const asymbol *b = * (const asymbol **) bp;

  /* Section symbols first.  */
  if ((a->flags & BSF_SECTION_SYM) && !(b->flags & BSF_SECTION_SYM))
    return -1;
  if (!(a->flags & BSF_SECTION_SYM) && (b->flags & BSF_SECTION_SYM))
    return 1;

  /* then .opd symbols.  */
  if (a->section == synthetic_opd && b->section != synthetic_opd)
    return -1;
  if (a->section != synthetic_opd && b->section == synthetic_opd)
    return 1;

  /* then other code symbols.  */
  if ((a->section->flags & (SEC_CODE | SEC_ALLOC | SEC_THREAD_LOCAL))
      == (SEC_CODE | SEC_ALLOC)
      && (b->section->flags & (SEC_CODE | SEC_ALLOC | SEC_THREAD_LOCAL))
	 != (SEC_CODE | SEC_ALLOC))
    return -1;

  if ((a->section->flags & (SEC_CODE | SEC_ALLOC | SEC_THREAD_LOCAL))
      != (SEC_CODE | SEC_ALLOC)
      && (b->section->flags & (SEC_CODE | SEC_ALLOC | SEC_THREAD_LOCAL))
	 == (SEC_CODE | SEC_ALLOC))
    return 1;

  if (synthetic_relocatable)
    {
      if (a->section->id < b->section->id)
	return -1;

      if (a->section->id > b->section->id)
	return 1;
    }

  if (a->value + a->section->vma < b->value + b->section->vma)
    return -1;

  if (a->value + a->section->vma > b->value + b->section->vma)
    return 1;

  /* For syms with the same value, prefer strong dynamic global function
     syms over other syms.  */
  if ((a->flags & BSF_GLOBAL) != 0 && (b->flags & BSF_GLOBAL) == 0)
    return -1;

  if ((a->flags & BSF_GLOBAL) == 0 && (b->flags & BSF_GLOBAL) != 0)
    return 1;

  if ((a->flags & BSF_FUNCTION) != 0 && (b->flags & BSF_FUNCTION) == 0)
    return -1;

  if ((a->flags & BSF_FUNCTION) == 0 && (b->flags & BSF_FUNCTION) != 0)
    return 1;

  if ((a->flags & BSF_WEAK) == 0 && (b->flags & BSF_WEAK) != 0)
    return -1;

  if ((a->flags & BSF_WEAK) != 0 && (b->flags & BSF_WEAK) == 0)
    return 1;

  if ((a->flags & BSF_DYNAMIC) != 0 && (b->flags & BSF_DYNAMIC) == 0)
    return -1;

  if ((a->flags & BSF_DYNAMIC) == 0 && (b->flags & BSF_DYNAMIC) != 0)
    return 1;

  return 0;
}

/* Search SYMS for a symbol of the given VALUE.  */

static asymbol *
sym_exists_at (asymbol **syms, long lo, long hi, int id, bfd_vma value)
{
  long mid;

  if (id == -1)
    {
      while (lo < hi)
	{
	  mid = (lo + hi) >> 1;
	  if (syms[mid]->value + syms[mid]->section->vma < value)
	    lo = mid + 1;
	  else if (syms[mid]->value + syms[mid]->section->vma > value)
	    hi = mid;
	  else
	    return syms[mid];
	}
    }
  else
    {
      while (lo < hi)
	{
	  mid = (lo + hi) >> 1;
	  if (syms[mid]->section->id < id)
	    lo = mid + 1;
	  else if (syms[mid]->section->id > id)
	    hi = mid;
	  else if (syms[mid]->value < value)
	    lo = mid + 1;
	  else if (syms[mid]->value > value)
	    hi = mid;
	  else
	    return syms[mid];
	}
    }
  return NULL;
}

/* Create synthetic symbols, effectively restoring "dot-symbol" function
   entry syms.  */

static long
ppc64_elf_get_synthetic_symtab (bfd *abfd,
				long static_count, asymbol **static_syms,
				long dyn_count, asymbol **dyn_syms,
				asymbol **ret)
{
  asymbol *s;
  long i;
  long count;
  char *names;
  long symcount, codesecsym, codesecsymend, secsymend, opdsymend;
  asection *opd;
  bfd_boolean relocatable = (abfd->flags & (EXEC_P | DYNAMIC)) == 0;
  asymbol **syms;

  *ret = NULL;

  opd = bfd_get_section_by_name (abfd, ".opd");
  if (opd == NULL)
    return 0;

  symcount = static_count;
  if (!relocatable)
    symcount += dyn_count;
  if (symcount == 0)
    return 0;

  syms = bfd_malloc ((symcount + 1) * sizeof (*syms));
  if (syms == NULL)
    return -1;

  if (!relocatable && static_count != 0 && dyn_count != 0)
    {
      /* Use both symbol tables.  */
      memcpy (syms, static_syms, static_count * sizeof (*syms));
      memcpy (syms + static_count, dyn_syms, (dyn_count + 1) * sizeof (*syms));
    }
  else if (!relocatable && static_count == 0)
    memcpy (syms, dyn_syms, (symcount + 1) * sizeof (*syms));
  else
    memcpy (syms, static_syms, (symcount + 1) * sizeof (*syms));

  synthetic_opd = opd;
  synthetic_relocatable = relocatable;
  qsort (syms, symcount, sizeof (*syms), compare_symbols);

  if (!relocatable && symcount > 1)
    {
      long j;
      /* Trim duplicate syms, since we may have merged the normal and
	 dynamic symbols.  Actually, we only care about syms that have
	 different values, so trim any with the same value.  */
      for (i = 1, j = 1; i < symcount; ++i)
	if (syms[i - 1]->value + syms[i - 1]->section->vma
	    != syms[i]->value + syms[i]->section->vma)
	  syms[j++] = syms[i];
      symcount = j;
    }

  i = 0;
  if (syms[i]->section == opd)
    ++i;
  codesecsym = i;

  for (; i < symcount; ++i)
    if (((syms[i]->section->flags & (SEC_CODE | SEC_ALLOC | SEC_THREAD_LOCAL))
	 != (SEC_CODE | SEC_ALLOC))
	|| (syms[i]->flags & BSF_SECTION_SYM) == 0)
      break;
  codesecsymend = i;

  for (; i < symcount; ++i)
    if ((syms[i]->flags & BSF_SECTION_SYM) == 0)
      break;
  secsymend = i;

  for (; i < symcount; ++i)
    if (syms[i]->section != opd)
      break;
  opdsymend = i;

  for (; i < symcount; ++i)
    if ((syms[i]->section->flags & (SEC_CODE | SEC_ALLOC | SEC_THREAD_LOCAL))
	!= (SEC_CODE | SEC_ALLOC))
      break;
  symcount = i;

  count = 0;
  if (opdsymend == secsymend)
    goto done;

  if (relocatable)
    {
      bfd_boolean (*slurp_relocs) (bfd *, asection *, asymbol **, bfd_boolean);
      arelent *r;
      size_t size;
      long relcount;

      slurp_relocs = get_elf_backend_data (abfd)->s->slurp_reloc_table;
      relcount = (opd->flags & SEC_RELOC) ? opd->reloc_count : 0;
      if (relcount == 0)
	goto done;

      if (!(*slurp_relocs) (abfd, opd, static_syms, FALSE))
	{
	  count = -1;
	  goto done;
	}

      size = 0;
      for (i = secsymend, r = opd->relocation; i < opdsymend; ++i)
	{
	  asymbol *sym;

	  while (r < opd->relocation + relcount
		 && r->address < syms[i]->value + opd->vma)
	    ++r;

	  if (r == opd->relocation + relcount)
	    break;

	  if (r->address != syms[i]->value + opd->vma)
	    continue;

	  if (r->howto->type != R_PPC64_ADDR64)
	    continue;

	  sym = *r->sym_ptr_ptr;
	  if (!sym_exists_at (syms, opdsymend, symcount,
			      sym->section->id, sym->value + r->addend))
	    {
	      ++count;
	      size += sizeof (asymbol);
	      size += strlen (syms[i]->name) + 2;
	    }
	}

      s = *ret = bfd_malloc (size);
      if (s == NULL)
	{
	  count = -1;
	  goto done;
	}

      names = (char *) (s + count);

      for (i = secsymend, r = opd->relocation; i < opdsymend; ++i)
	{
	  asymbol *sym;

	  while (r < opd->relocation + relcount
		 && r->address < syms[i]->value + opd->vma)
	    ++r;

	  if (r == opd->relocation + relcount)
	    break;

	  if (r->address != syms[i]->value + opd->vma)
	    continue;

	  if (r->howto->type != R_PPC64_ADDR64)
	    continue;

	  sym = *r->sym_ptr_ptr;
	  if (!sym_exists_at (syms, opdsymend, symcount,
			      sym->section->id, sym->value + r->addend))
	    {
	      size_t len;

	      *s = *syms[i];
	      s->section = sym->section;
	      s->value = sym->value + r->addend;
	      s->name = names;
	      *names++ = '.';
	      len = strlen (syms[i]->name);
	      memcpy (names, syms[i]->name, len + 1);
	      names += len + 1;
	      s++;
	    }
	}
    }
  else
    {
      bfd_byte *contents;
      size_t size;

      if (!bfd_malloc_and_get_section (abfd, opd, &contents))
	{
	  if (contents)
	    {
	    free_contents_and_exit:
	      free (contents);
	    }
	  count = -1;
	  goto done;
	}

      size = 0;
      for (i = secsymend; i < opdsymend; ++i)
	{
	  bfd_vma ent;

	  ent = bfd_get_64 (abfd, contents + syms[i]->value);
	  if (!sym_exists_at (syms, opdsymend, symcount, -1, ent))
	    {
	      ++count;
	      size += sizeof (asymbol);
	      size += strlen (syms[i]->name) + 2;
	    }
	}

      s = *ret = bfd_malloc (size);
      if (s == NULL)
	goto free_contents_and_exit;

      names = (char *) (s + count);

      for (i = secsymend; i < opdsymend; ++i)
	{
	  bfd_vma ent;

	  ent = bfd_get_64 (abfd, contents + syms[i]->value);
	  if (!sym_exists_at (syms, opdsymend, symcount, -1, ent))
	    {
	      long lo, hi;
	      size_t len;
	      asection *sec = abfd->sections;

	      *s = *syms[i];
	      lo = codesecsym;
	      hi = codesecsymend;
	      while (lo < hi)
		{
		  long mid = (lo + hi) >> 1;
		  if (syms[mid]->section->vma < ent)
		    lo = mid + 1;
		  else if (syms[mid]->section->vma > ent)
		    hi = mid;
		  else
		    {
		      sec = syms[mid]->section;
		      break;
		    }
		}

	      if (lo >= hi && lo > codesecsym)
		sec = syms[lo - 1]->section;

	      for (; sec != NULL; sec = sec->next)
		{
		  if (sec->vma > ent)
		    break;
		  if ((sec->flags & SEC_ALLOC) == 0
		      || (sec->flags & SEC_LOAD) == 0)
		    break;
		  if ((sec->flags & SEC_CODE) != 0)
		    s->section = sec;
		}
	      s->value = ent - s->section->vma;
	      s->name = names;
	      *names++ = '.';
	      len = strlen (syms[i]->name);
	      memcpy (names, syms[i]->name, len + 1);
	      names += len + 1;
	      s++;
	    }
	}
      free (contents);
    }

 done:
  free (syms);
  return count;
}

/* The following functions are specific to the ELF linker, while
   functions above are used generally.  Those named ppc64_elf_* are
   called by the main ELF linker code.  They appear in this file more
   or less in the order in which they are called.  eg.
   ppc64_elf_check_relocs is called early in the link process,
   ppc64_elf_finish_dynamic_sections is one of the last functions
   called.

   PowerPC64-ELF uses a similar scheme to PowerPC64-XCOFF in that
   functions have both a function code symbol and a function descriptor
   symbol.  A call to foo in a relocatable object file looks like:

   .		.text
   .	x:
   .		bl	.foo
   .		nop

   The function definition in another object file might be:

   .		.section .opd
   .	foo:	.quad	.foo
   .		.quad	.TOC.@tocbase
   .		.quad	0
   .
   .		.text
   .	.foo:	blr

   When the linker resolves the call during a static link, the branch
   unsurprisingly just goes to .foo and the .opd information is unused.
   If the function definition is in a shared library, things are a little
   different:  The call goes via a plt call stub, the opd information gets
   copied to the plt, and the linker patches the nop.

   .	x:
   .		bl	.foo_stub
   .		ld	2,40(1)
   .
   .
   .	.foo_stub:
   .		addis	12,2,Lfoo@toc@ha	# in practice, the call stub
   .		addi	12,12,Lfoo@toc@l	# is slightly optimized, but
   .		std	2,40(1)			# this is the general idea
   .		ld	11,0(12)
   .		ld	2,8(12)
   .		mtctr	11
   .		ld	11,16(12)
   .		bctr
   .
   .		.section .plt
   .	Lfoo:	reloc (R_PPC64_JMP_SLOT, foo)

   The "reloc ()" notation is supposed to indicate that the linker emits
   an R_PPC64_JMP_SLOT reloc against foo.  The dynamic linker does the opd
   copying.

   What are the difficulties here?  Well, firstly, the relocations
   examined by the linker in check_relocs are against the function code
   sym .foo, while the dynamic relocation in the plt is emitted against
   the function descriptor symbol, foo.  Somewhere along the line, we need
   to carefully copy dynamic link information from one symbol to the other.
   Secondly, the generic part of the elf linker will make .foo a dynamic
   symbol as is normal for most other backends.  We need foo dynamic
   instead, at least for an application final link.  However, when
   creating a shared library containing foo, we need to have both symbols
   dynamic so that references to .foo are satisfied during the early
   stages of linking.  Otherwise the linker might decide to pull in a
   definition from some other object, eg. a static library.

   Update: As of August 2004, we support a new convention.  Function
   calls may use the function descriptor symbol, ie. "bl foo".  This
   behaves exactly as "bl .foo".  */

/* The linker needs to keep track of the number of relocs that it
   decides to copy as dynamic relocs in check_relocs for each symbol.
   This is so that it can later discard them if they are found to be
   unnecessary.  We store the information in a field extending the
   regular ELF linker hash table.  */

struct ppc_dyn_relocs
{
  struct ppc_dyn_relocs *next;

  /* The input section of the reloc.  */
  asection *sec;

  /* Total number of relocs copied for the input section.  */
  bfd_size_type count;

  /* Number of pc-relative relocs copied for the input section.  */
  bfd_size_type pc_count;
};

/* Track GOT entries needed for a given symbol.  We might need more
   than one got entry per symbol.  */
struct got_entry
{
  struct got_entry *next;

  /* The symbol addend that we'll be placing in the GOT.  */
  bfd_vma addend;

  /* Unlike other ELF targets, we use separate GOT entries for the same
     symbol referenced from different input files.  This is to support
     automatic multiple TOC/GOT sections, where the TOC base can vary
     from one input file to another.  FIXME: After group_sections we
     ought to merge entries within the group.

     Point to the BFD owning this GOT entry.  */
  bfd *owner;

  /* Zero for non-tls entries, or TLS_TLS and one of TLS_GD, TLS_LD,
     TLS_TPREL or TLS_DTPREL for tls entries.  */
  char tls_type;

  /* Reference count until size_dynamic_sections, GOT offset thereafter.  */
  union
    {
      bfd_signed_vma refcount;
      bfd_vma offset;
    } got;
};

/* The same for PLT.  */
struct plt_entry
{
  struct plt_entry *next;

  bfd_vma addend;

  union
    {
      bfd_signed_vma refcount;
      bfd_vma offset;
    } plt;
};

/* Of those relocs that might be copied as dynamic relocs, this function
   selects those that must be copied when linking a shared library,
   even when the symbol is local.  */

static int
must_be_dyn_reloc (struct bfd_link_info *info,
		   enum elf_ppc64_reloc_type r_type)
{
  switch (r_type)
    {
    default:
      return 1;

    case R_PPC64_REL32:
    case R_PPC64_REL64:
    case R_PPC64_REL30:
      return 0;

    case R_PPC64_TPREL16:
    case R_PPC64_TPREL16_LO:
    case R_PPC64_TPREL16_HI:
    case R_PPC64_TPREL16_HA:
    case R_PPC64_TPREL16_DS:
    case R_PPC64_TPREL16_LO_DS:
    case R_PPC64_TPREL16_HIGHER:
    case R_PPC64_TPREL16_HIGHERA:
    case R_PPC64_TPREL16_HIGHEST:
    case R_PPC64_TPREL16_HIGHESTA:
    case R_PPC64_TPREL64:
      return !info->executable;
    }
}

/* If ELIMINATE_COPY_RELOCS is non-zero, the linker will try to avoid
   copying dynamic variables from a shared lib into an app's dynbss
   section, and instead use a dynamic relocation to point into the
   shared lib.  With code that gcc generates, it's vital that this be
   enabled;  In the PowerPC64 ABI, the address of a function is actually
   the address of a function descriptor, which resides in the .opd
   section.  gcc uses the descriptor directly rather than going via the
   GOT as some other ABI's do, which means that initialized function
   pointers must reference the descriptor.  Thus, a function pointer
   initialized to the address of a function in a shared library will
   either require a copy reloc, or a dynamic reloc.  Using a copy reloc
   redefines the function descriptor symbol to point to the copy.  This
   presents a problem as a plt entry for that function is also
   initialized from the function descriptor symbol and the copy reloc
   may not be initialized first.  */
#define ELIMINATE_COPY_RELOCS 1

/* Section name for stubs is the associated section name plus this
   string.  */
#define STUB_SUFFIX ".stub"

/* Linker stubs.
   ppc_stub_long_branch:
   Used when a 14 bit branch (or even a 24 bit branch) can't reach its
   destination, but a 24 bit branch in a stub section will reach.
   .	b	dest

   ppc_stub_plt_branch:
   Similar to the above, but a 24 bit branch in the stub section won't
   reach its destination.
   .	addis	%r12,%r2,xxx@toc@ha
   .	ld	%r11,xxx@toc@l(%r12)
   .	mtctr	%r11
   .	bctr

   ppc_stub_plt_call:
   Used to call a function in a shared library.  If it so happens that
   the plt entry referenced crosses a 64k boundary, then an extra
   "addi %r12,%r12,xxx@toc@l" will be inserted before the "mtctr".
   .	addis	%r12,%r2,xxx@toc@ha
   .	std	%r2,40(%r1)
   .	ld	%r11,xxx+0@toc@l(%r12)
   .	mtctr	%r11
   .	ld	%r2,xxx+8@toc@l(%r12)
   .	ld	%r11,xxx+16@toc@l(%r12)
   .	bctr

   ppc_stub_long_branch and ppc_stub_plt_branch may also have additional
   code to adjust the value and save r2 to support multiple toc sections.
   A ppc_stub_long_branch with an r2 offset looks like:
   .	std	%r2,40(%r1)
   .	addis	%r2,%r2,off@ha
   .	addi	%r2,%r2,off@l
   .	b	dest

   A ppc_stub_plt_branch with an r2 offset looks like:
   .	std	%r2,40(%r1)
   .	addis	%r12,%r2,xxx@toc@ha
   .	ld	%r11,xxx@toc@l(%r12)
   .	addis	%r2,%r2,off@ha
   .	addi	%r2,%r2,off@l
   .	mtctr	%r11
   .	bctr

   In cases where the "addis" instruction would add zero, the "addis" is
   omitted and following instructions modified slightly in some cases.
*/

enum ppc_stub_type {
  ppc_stub_none,
  ppc_stub_long_branch,
  ppc_stub_long_branch_r2off,
  ppc_stub_plt_branch,
  ppc_stub_plt_branch_r2off,
  ppc_stub_plt_call
};

struct ppc_stub_hash_entry {

  /* Base hash table entry structure.  */
  struct bfd_hash_entry root;

  enum ppc_stub_type stub_type;

  /* The stub section.  */
  asection *stub_sec;

  /* Offset within stub_sec of the beginning of this stub.  */
  bfd_vma stub_offset;

  /* Given the symbol's value and its section we can determine its final
     value when building the stubs (so the stub knows where to jump.  */
  bfd_vma target_value;
  asection *target_section;

  /* The symbol table entry, if any, that this was derived from.  */
  struct ppc_link_hash_entry *h;

  /* And the reloc addend that this was derived from.  */
  bfd_vma addend;

  /* Where this stub is being called from, or, in the case of combined
     stub sections, the first input section in the group.  */
  asection *id_sec;
};

struct ppc_branch_hash_entry {

  /* Base hash table entry structure.  */
  struct bfd_hash_entry root;

  /* Offset within branch lookup table.  */
  unsigned int offset;

  /* Generation marker.  */
  unsigned int iter;
};

struct ppc_link_hash_entry
{
  struct elf_link_hash_entry elf;

  union {
    /* A pointer to the most recently used stub hash entry against this
       symbol.  */
    struct ppc_stub_hash_entry *stub_cache;

    /* A pointer to the next symbol starting with a '.'  */
    struct ppc_link_hash_entry *next_dot_sym;
  } u;

  /* Track dynamic relocs copied for this symbol.  */
  struct ppc_dyn_relocs *dyn_relocs;

  /* Link between function code and descriptor symbols.  */
  struct ppc_link_hash_entry *oh;

  /* Flag function code and descriptor symbols.  */
  unsigned int is_func:1;
  unsigned int is_func_descriptor:1;
  unsigned int fake:1;

  /* Whether global opd/toc sym has been adjusted or not.
     After ppc64_elf_edit_opd/ppc64_elf_edit_toc has run, this flag
     should be set for all globals defined in any opd/toc section.  */
  unsigned int adjust_done:1;

  /* Set if we twiddled this symbol to weak at some stage.  */
  unsigned int was_undefined:1;

  /* Contexts in which symbol is used in the GOT (or TOC).
     TLS_GD .. TLS_EXPLICIT bits are or'd into the mask as the
     corresponding relocs are encountered during check_relocs.
     tls_optimize clears TLS_GD .. TLS_TPREL when optimizing to
     indicate the corresponding GOT entry type is not needed.
     tls_optimize may also set TLS_TPRELGD when a GD reloc turns into
     a TPREL one.  We use a separate flag rather than setting TPREL
     just for convenience in distinguishing the two cases.  */
#define TLS_GD		 1	/* GD reloc. */
#define TLS_LD		 2	/* LD reloc. */
#define TLS_TPREL	 4	/* TPREL reloc, => IE. */
#define TLS_DTPREL	 8	/* DTPREL reloc, => LD. */
#define TLS_TLS		16	/* Any TLS reloc.  */
#define TLS_EXPLICIT	32	/* Marks TOC section TLS relocs. */
#define TLS_TPRELGD	64	/* TPREL reloc resulting from GD->IE. */
  char tls_mask;
};

/* ppc64 ELF linker hash table.  */

struct ppc_link_hash_table
{
  struct elf_link_hash_table elf;

  /* The stub hash table.  */
  struct bfd_hash_table stub_hash_table;

  /* Another hash table for plt_branch stubs.  */
  struct bfd_hash_table branch_hash_table;

  /* Linker stub bfd.  */
  bfd *stub_bfd;

  /* Linker call-backs.  */
  asection * (*add_stub_section) (const char *, asection *);
  void (*layout_sections_again) (void);

  /* Array to keep track of which stub sections have been created, and
     information on stub grouping.  */
  struct map_stub {
    /* This is the section to which stubs in the group will be attached.  */
    asection *link_sec;
    /* The stub section.  */
    asection *stub_sec;
    /* Along with elf_gp, specifies the TOC pointer used in this group.  */
    bfd_vma toc_off;
  } *stub_group;

  /* Temp used when calculating TOC pointers.  */
  bfd_vma toc_curr;

  /* Highest input section id.  */
  int top_id;

  /* Highest output section index.  */
  int top_index;

  /* Used when adding symbols.  */
  struct ppc_link_hash_entry *dot_syms;

  /* List of input sections for each output section.  */
  asection **input_list;

  /* Short-cuts to get to dynamic linker sections.  */
  asection *got;
  asection *plt;
  asection *relplt;
  asection *dynbss;
  asection *relbss;
  asection *glink;
  asection *sfpr;
  asection *brlt;
  asection *relbrlt;

  /* Shortcut to .__tls_get_addr and __tls_get_addr.  */
  struct ppc_link_hash_entry *tls_get_addr;
  struct ppc_link_hash_entry *tls_get_addr_fd;

  /* Statistics.  */
  unsigned long stub_count[ppc_stub_plt_call];

  /* Number of stubs against global syms.  */
  unsigned long stub_globals;

  /* Set if we should emit symbols for stubs.  */
  unsigned int emit_stub_syms:1;

  /* Support for multiple toc sections.  */
  unsigned int no_multi_toc:1;
  unsigned int multi_toc_needed:1;

  /* Set on error.  */
  unsigned int stub_error:1;

  /* Temp used by ppc64_elf_check_directives.  */
  unsigned int twiddled_syms:1;

  /* Incremented every time we size stubs.  */
  unsigned int stub_iteration;

  /* Small local sym to section mapping cache.  */
  struct sym_sec_cache sym_sec;
};

/* Rename some of the generic section flags to better document how they
   are used here.  */
#define has_toc_reloc has_gp_reloc
#define makes_toc_func_call need_finalize_relax
#define call_check_in_progress reloc_done

/* Get the ppc64 ELF linker hash table from a link_info structure.  */

#define ppc_hash_table(p) \
  ((struct ppc_link_hash_table *) ((p)->hash))

#define ppc_stub_hash_lookup(table, string, create, copy) \
  ((struct ppc_stub_hash_entry *) \
   bfd_hash_lookup ((table), (string), (create), (copy)))

#define ppc_branch_hash_lookup(table, string, create, copy) \
  ((struct ppc_branch_hash_entry *) \
   bfd_hash_lookup ((table), (string), (create), (copy)))

/* Create an entry in the stub hash table.  */

static struct bfd_hash_entry *
stub_hash_newfunc (struct bfd_hash_entry *entry,
		   struct bfd_hash_table *table,
		   const char *string)
{
  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (entry == NULL)
    {
      entry = bfd_hash_allocate (table, sizeof (struct ppc_stub_hash_entry));
      if (entry == NULL)
	return entry;
    }

  /* Call the allocation method of the superclass.  */
  entry = bfd_hash_newfunc (entry, table, string);
  if (entry != NULL)
    {
      struct ppc_stub_hash_entry *eh;

      /* Initialize the local fields.  */
      eh = (struct ppc_stub_hash_entry *) entry;
      eh->stub_type = ppc_stub_none;
      eh->stub_sec = NULL;
      eh->stub_offset = 0;
      eh->target_value = 0;
      eh->target_section = NULL;
      eh->h = NULL;
      eh->id_sec = NULL;
    }

  return entry;
}

/* Create an entry in the branch hash table.  */

static struct bfd_hash_entry *
branch_hash_newfunc (struct bfd_hash_entry *entry,
		     struct bfd_hash_table *table,
		     const char *string)
{
  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (entry == NULL)
    {
      entry = bfd_hash_allocate (table, sizeof (struct ppc_branch_hash_entry));
      if (entry == NULL)
	return entry;
    }

  /* Call the allocation method of the superclass.  */
  entry = bfd_hash_newfunc (entry, table, string);
  if (entry != NULL)
    {
      struct ppc_branch_hash_entry *eh;

      /* Initialize the local fields.  */
      eh = (struct ppc_branch_hash_entry *) entry;
      eh->offset = 0;
      eh->iter = 0;
    }

  return entry;
}

/* Create an entry in a ppc64 ELF linker hash table.  */

static struct bfd_hash_entry *
link_hash_newfunc (struct bfd_hash_entry *entry,
		   struct bfd_hash_table *table,
		   const char *string)
{
  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (entry == NULL)
    {
      entry = bfd_hash_allocate (table, sizeof (struct ppc_link_hash_entry));
      if (entry == NULL)
	return entry;
    }

  /* Call the allocation method of the superclass.  */
  entry = _bfd_elf_link_hash_newfunc (entry, table, string);
  if (entry != NULL)
    {
      struct ppc_link_hash_entry *eh = (struct ppc_link_hash_entry *) entry;

      memset (&eh->u.stub_cache, 0,
	      (sizeof (struct ppc_link_hash_entry)
	       - offsetof (struct ppc_link_hash_entry, u.stub_cache)));

      /* When making function calls, old ABI code references function entry
	 points (dot symbols), while new ABI code references the function
	 descriptor symbol.  We need to make any combination of reference and
	 definition work together, without breaking archive linking.

	 For a defined function "foo" and an undefined call to "bar":
	 An old object defines "foo" and ".foo", references ".bar" (possibly
	 "bar" too).
	 A new object defines "foo" and references "bar".

	 A new object thus has no problem with its undefined symbols being
	 satisfied by definitions in an old object.  On the other hand, the
	 old object won't have ".bar" satisfied by a new object.

	 Keep a list of newly added dot-symbols.  */

      if (string[0] == '.')
	{
	  struct ppc_link_hash_table *htab;

	  htab = (struct ppc_link_hash_table *) table;
	  eh->u.next_dot_sym = htab->dot_syms;
	  htab->dot_syms = eh;
	}
    }

  return entry;
}

/* Create a ppc64 ELF linker hash table.  */

static struct bfd_link_hash_table *
ppc64_elf_link_hash_table_create (bfd *abfd)
{
  struct ppc_link_hash_table *htab;
  bfd_size_type amt = sizeof (struct ppc_link_hash_table);

  htab = bfd_zmalloc (amt);
  if (htab == NULL)
    return NULL;

  if (!_bfd_elf_link_hash_table_init (&htab->elf, abfd, link_hash_newfunc,
				      sizeof (struct ppc_link_hash_entry)))
    {
      free (htab);
      return NULL;
    }

  /* Init the stub hash table too.  */
  if (!bfd_hash_table_init (&htab->stub_hash_table, stub_hash_newfunc,
			    sizeof (struct ppc_stub_hash_entry)))
    return NULL;

  /* And the branch hash table.  */
  if (!bfd_hash_table_init (&htab->branch_hash_table, branch_hash_newfunc,
			    sizeof (struct ppc_branch_hash_entry)))
    return NULL;

  /* Initializing two fields of the union is just cosmetic.  We really
     only care about glist, but when compiled on a 32-bit host the
     bfd_vma fields are larger.  Setting the bfd_vma to zero makes
     debugger inspection of these fields look nicer.  */
  htab->elf.init_got_refcount.refcount = 0;
  htab->elf.init_got_refcount.glist = NULL;
  htab->elf.init_plt_refcount.refcount = 0;
  htab->elf.init_plt_refcount.glist = NULL;
  htab->elf.init_got_offset.offset = 0;
  htab->elf.init_got_offset.glist = NULL;
  htab->elf.init_plt_offset.offset = 0;
  htab->elf.init_plt_offset.glist = NULL;

  return &htab->elf.root;
}

/* Free the derived linker hash table.  */

static void
ppc64_elf_link_hash_table_free (struct bfd_link_hash_table *hash)
{
  struct ppc_link_hash_table *ret = (struct ppc_link_hash_table *) hash;

  bfd_hash_table_free (&ret->stub_hash_table);
  bfd_hash_table_free (&ret->branch_hash_table);
  _bfd_generic_link_hash_table_free (hash);
}

/* Satisfy the ELF linker by filling in some fields in our fake bfd.  */

void
ppc64_elf_init_stub_bfd (bfd *abfd, struct bfd_link_info *info)
{
  struct ppc_link_hash_table *htab;

  elf_elfheader (abfd)->e_ident[EI_CLASS] = ELFCLASS64;

/* Always hook our dynamic sections into the first bfd, which is the
   linker created stub bfd.  This ensures that the GOT header is at
   the start of the output TOC section.  */
  htab = ppc_hash_table (info);
  htab->stub_bfd = abfd;
  htab->elf.dynobj = abfd;
}

/* Build a name for an entry in the stub hash table.  */

static char *
ppc_stub_name (const asection *input_section,
	       const asection *sym_sec,
	       const struct ppc_link_hash_entry *h,
	       const Elf_Internal_Rela *rel)
{
  char *stub_name;
  bfd_size_type len;

  /* rel->r_addend is actually 64 bit, but who uses more than +/- 2^31
     offsets from a sym as a branch target?  In fact, we could
     probably assume the addend is always zero.  */
  BFD_ASSERT (((int) rel->r_addend & 0xffffffff) == rel->r_addend);

  if (h)
    {
      len = 8 + 1 + strlen (h->elf.root.root.string) + 1 + 8 + 1;
      stub_name = bfd_malloc (len);
      if (stub_name == NULL)
	return stub_name;

      sprintf (stub_name, "%08x.%s+%x",
	       input_section->id & 0xffffffff,
	       h->elf.root.root.string,
	       (int) rel->r_addend & 0xffffffff);
    }
  else
    {
      len = 8 + 1 + 8 + 1 + 8 + 1 + 8 + 1;
      stub_name = bfd_malloc (len);
      if (stub_name == NULL)
	return stub_name;

      sprintf (stub_name, "%08x.%x:%x+%x",
	       input_section->id & 0xffffffff,
	       sym_sec->id & 0xffffffff,
	       (int) ELF64_R_SYM (rel->r_info) & 0xffffffff,
	       (int) rel->r_addend & 0xffffffff);
    }
  if (stub_name[len - 2] == '+' && stub_name[len - 1] == '0')
    stub_name[len - 2] = 0;
  return stub_name;
}

/* Look up an entry in the stub hash.  Stub entries are cached because
   creating the stub name takes a bit of time.  */

static struct ppc_stub_hash_entry *
ppc_get_stub_entry (const asection *input_section,
		    const asection *sym_sec,
		    struct ppc_link_hash_entry *h,
		    const Elf_Internal_Rela *rel,
		    struct ppc_link_hash_table *htab)
{
  struct ppc_stub_hash_entry *stub_entry;
  const asection *id_sec;

  /* If this input section is part of a group of sections sharing one
     stub section, then use the id of the first section in the group.
     Stub names need to include a section id, as there may well be
     more than one stub used to reach say, printf, and we need to
     distinguish between them.  */
  id_sec = htab->stub_group[input_section->id].link_sec;

  if (h != NULL && h->u.stub_cache != NULL
      && h->u.stub_cache->h == h
      && h->u.stub_cache->id_sec == id_sec)
    {
      stub_entry = h->u.stub_cache;
    }
  else
    {
      char *stub_name;

      stub_name = ppc_stub_name (id_sec, sym_sec, h, rel);
      if (stub_name == NULL)
	return NULL;

      stub_entry = ppc_stub_hash_lookup (&htab->stub_hash_table,
					 stub_name, FALSE, FALSE);
      if (h != NULL)
	h->u.stub_cache = stub_entry;

      free (stub_name);
    }

  return stub_entry;
}

/* Add a new stub entry to the stub hash.  Not all fields of the new
   stub entry are initialised.  */

static struct ppc_stub_hash_entry *
ppc_add_stub (const char *stub_name,
	      asection *section,
	      struct ppc_link_hash_table *htab)
{
  asection *link_sec;
  asection *stub_sec;
  struct ppc_stub_hash_entry *stub_entry;

  link_sec = htab->stub_group[section->id].link_sec;
  stub_sec = htab->stub_group[section->id].stub_sec;
  if (stub_sec == NULL)
    {
      stub_sec = htab->stub_group[link_sec->id].stub_sec;
      if (stub_sec == NULL)
	{
	  size_t namelen;
	  bfd_size_type len;
	  char *s_name;

	  namelen = strlen (link_sec->name);
	  len = namelen + sizeof (STUB_SUFFIX);
	  s_name = bfd_alloc (htab->stub_bfd, len);
	  if (s_name == NULL)
	    return NULL;

	  memcpy (s_name, link_sec->name, namelen);
	  memcpy (s_name + namelen, STUB_SUFFIX, sizeof (STUB_SUFFIX));
	  stub_sec = (*htab->add_stub_section) (s_name, link_sec);
	  if (stub_sec == NULL)
	    return NULL;
	  htab->stub_group[link_sec->id].stub_sec = stub_sec;
	}
      htab->stub_group[section->id].stub_sec = stub_sec;
    }

  /* Enter this entry into the linker stub hash table.  */
  stub_entry = ppc_stub_hash_lookup (&htab->stub_hash_table, stub_name,
				     TRUE, FALSE);
  if (stub_entry == NULL)
    {
      (*_bfd_error_handler) (_("%B: cannot create stub entry %s"),
			     section->owner, stub_name);
      return NULL;
    }

  stub_entry->stub_sec = stub_sec;
  stub_entry->stub_offset = 0;
  stub_entry->id_sec = link_sec;
  return stub_entry;
}

/* Create sections for linker generated code.  */

static bfd_boolean
create_linkage_sections (bfd *dynobj, struct bfd_link_info *info)
{
  struct ppc_link_hash_table *htab;
  flagword flags;

  htab = ppc_hash_table (info);

  /* Create .sfpr for code to save and restore fp regs.  */
  flags = (SEC_ALLOC | SEC_LOAD | SEC_CODE | SEC_READONLY
	   | SEC_HAS_CONTENTS | SEC_IN_MEMORY | SEC_LINKER_CREATED);
  htab->sfpr = bfd_make_section_anyway_with_flags (dynobj, ".sfpr",
						   flags);
  if (htab->sfpr == NULL
      || ! bfd_set_section_alignment (dynobj, htab->sfpr, 2))
    return FALSE;

  /* Create .glink for lazy dynamic linking support.  */
  htab->glink = bfd_make_section_anyway_with_flags (dynobj, ".glink",
						    flags);
  if (htab->glink == NULL
      || ! bfd_set_section_alignment (dynobj, htab->glink, 3))
    return FALSE;

  /* Create branch lookup table for plt_branch stubs.  */
  flags = (SEC_ALLOC | SEC_LOAD
	   | SEC_HAS_CONTENTS | SEC_IN_MEMORY | SEC_LINKER_CREATED);
  htab->brlt = bfd_make_section_anyway_with_flags (dynobj, ".branch_lt",
						   flags);
  if (htab->brlt == NULL
      || ! bfd_set_section_alignment (dynobj, htab->brlt, 3))
    return FALSE;

  if (!info->shared)
    return TRUE;

  flags = (SEC_ALLOC | SEC_LOAD | SEC_READONLY
	   | SEC_HAS_CONTENTS | SEC_IN_MEMORY | SEC_LINKER_CREATED);
  htab->relbrlt = bfd_make_section_anyway_with_flags (dynobj,
						      ".rela.branch_lt",
						      flags);
  if (!htab->relbrlt
      || ! bfd_set_section_alignment (dynobj, htab->relbrlt, 3))
    return FALSE;

  return TRUE;
}

/* Create .got and .rela.got sections in ABFD, and .got in dynobj if
   not already done.  */

static bfd_boolean
create_got_section (bfd *abfd, struct bfd_link_info *info)
{
  asection *got, *relgot;
  flagword flags;
  struct ppc_link_hash_table *htab = ppc_hash_table (info);

  if (!htab->got)
    {
      if (! _bfd_elf_create_got_section (htab->elf.dynobj, info))
	return FALSE;

      htab->got = bfd_get_section_by_name (htab->elf.dynobj, ".got");
      if (!htab->got)
	abort ();
    }

  flags = (SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS | SEC_IN_MEMORY
	   | SEC_LINKER_CREATED);

  got = bfd_make_section_anyway_with_flags (abfd, ".got", flags);
  if (!got
      || !bfd_set_section_alignment (abfd, got, 3))
    return FALSE;

  relgot = bfd_make_section_anyway_with_flags (abfd, ".rela.got",
					       flags | SEC_READONLY);
  if (!relgot
      || ! bfd_set_section_alignment (abfd, relgot, 3))
    return FALSE;

  ppc64_elf_tdata (abfd)->got = got;
  ppc64_elf_tdata (abfd)->relgot = relgot;
  return TRUE;
}

/* Create the dynamic sections, and set up shortcuts.  */

static bfd_boolean
ppc64_elf_create_dynamic_sections (bfd *dynobj, struct bfd_link_info *info)
{
  struct ppc_link_hash_table *htab;

  if (!_bfd_elf_create_dynamic_sections (dynobj, info))
    return FALSE;

  htab = ppc_hash_table (info);
  if (!htab->got)
    htab->got = bfd_get_section_by_name (dynobj, ".got");
  htab->plt = bfd_get_section_by_name (dynobj, ".plt");
  htab->relplt = bfd_get_section_by_name (dynobj, ".rela.plt");
  htab->dynbss = bfd_get_section_by_name (dynobj, ".dynbss");
  if (!info->shared)
    htab->relbss = bfd_get_section_by_name (dynobj, ".rela.bss");

  if (!htab->got || !htab->plt || !htab->relplt || !htab->dynbss
      || (!info->shared && !htab->relbss))
    abort ();

  return TRUE;
}

/* Merge PLT info on FROM with that on TO.  */

static void
move_plt_plist (struct ppc_link_hash_entry *from,
		struct ppc_link_hash_entry *to)
{
  if (from->elf.plt.plist != NULL)
    {
      if (to->elf.plt.plist != NULL)
	{
	  struct plt_entry **entp;
	  struct plt_entry *ent;

	  for (entp = &from->elf.plt.plist; (ent = *entp) != NULL; )
	    {
	      struct plt_entry *dent;

	      for (dent = to->elf.plt.plist; dent != NULL; dent = dent->next)
		if (dent->addend == ent->addend)
		  {
		    dent->plt.refcount += ent->plt.refcount;
		    *entp = ent->next;
		    break;
		  }
	      if (dent == NULL)
		entp = &ent->next;
	    }
	  *entp = to->elf.plt.plist;
	}

      to->elf.plt.plist = from->elf.plt.plist;
      from->elf.plt.plist = NULL;
    }
}

/* Copy the extra info we tack onto an elf_link_hash_entry.  */

static void
ppc64_elf_copy_indirect_symbol (struct bfd_link_info *info,
				struct elf_link_hash_entry *dir,
				struct elf_link_hash_entry *ind)
{
  struct ppc_link_hash_entry *edir, *eind;

  edir = (struct ppc_link_hash_entry *) dir;
  eind = (struct ppc_link_hash_entry *) ind;

  /* Copy over any dynamic relocs we may have on the indirect sym.  */
  if (eind->dyn_relocs != NULL)
    {
      if (edir->dyn_relocs != NULL)
	{
	  struct ppc_dyn_relocs **pp;
	  struct ppc_dyn_relocs *p;

	  /* Add reloc counts against the indirect sym to the direct sym
	     list.  Merge any entries against the same section.  */
	  for (pp = &eind->dyn_relocs; (p = *pp) != NULL; )
	    {
	      struct ppc_dyn_relocs *q;

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

  edir->is_func |= eind->is_func;
  edir->is_func_descriptor |= eind->is_func_descriptor;
  edir->tls_mask |= eind->tls_mask;

  /* If called to transfer flags for a weakdef during processing
     of elf_adjust_dynamic_symbol, don't copy NON_GOT_REF.
     We clear it ourselves for ELIMINATE_COPY_RELOCS.  */
  if (!(ELIMINATE_COPY_RELOCS
	&& eind->elf.root.type != bfd_link_hash_indirect
	&& edir->elf.dynamic_adjusted))
    edir->elf.non_got_ref |= eind->elf.non_got_ref;

  edir->elf.ref_dynamic |= eind->elf.ref_dynamic;
  edir->elf.ref_regular |= eind->elf.ref_regular;
  edir->elf.ref_regular_nonweak |= eind->elf.ref_regular_nonweak;
  edir->elf.needs_plt |= eind->elf.needs_plt;

  /* If we were called to copy over info for a weak sym, that's all.  */
  if (eind->elf.root.type != bfd_link_hash_indirect)
    return;

  /* Copy over got entries that we may have already seen to the
     symbol which just became indirect.  */
  if (eind->elf.got.glist != NULL)
    {
      if (edir->elf.got.glist != NULL)
	{
	  struct got_entry **entp;
	  struct got_entry *ent;

	  for (entp = &eind->elf.got.glist; (ent = *entp) != NULL; )
	    {
	      struct got_entry *dent;

	      for (dent = edir->elf.got.glist; dent != NULL; dent = dent->next)
		if (dent->addend == ent->addend
		    && dent->owner == ent->owner
		    && dent->tls_type == ent->tls_type)
		  {
		    dent->got.refcount += ent->got.refcount;
		    *entp = ent->next;
		    break;
		  }
	      if (dent == NULL)
		entp = &ent->next;
	    }
	  *entp = edir->elf.got.glist;
	}

      edir->elf.got.glist = eind->elf.got.glist;
      eind->elf.got.glist = NULL;
    }

  /* And plt entries.  */
  move_plt_plist (eind, edir);

  if (eind->elf.dynindx != -1)
    {
      if (edir->elf.dynindx != -1)
	_bfd_elf_strtab_delref (elf_hash_table (info)->dynstr,
				edir->elf.dynstr_index);
      edir->elf.dynindx = eind->elf.dynindx;
      edir->elf.dynstr_index = eind->elf.dynstr_index;
      eind->elf.dynindx = -1;
      eind->elf.dynstr_index = 0;
    }
}

/* Find the function descriptor hash entry from the given function code
   hash entry FH.  Link the entries via their OH fields.  */

static struct ppc_link_hash_entry *
get_fdh (struct ppc_link_hash_entry *fh, struct ppc_link_hash_table *htab)
{
  struct ppc_link_hash_entry *fdh = fh->oh;

  if (fdh == NULL)
    {
      const char *fd_name = fh->elf.root.root.string + 1;

      fdh = (struct ppc_link_hash_entry *)
	elf_link_hash_lookup (&htab->elf, fd_name, FALSE, FALSE, FALSE);
      if (fdh != NULL)
	{
	  fdh->is_func_descriptor = 1;
	  fdh->oh = fh;
	  fh->is_func = 1;
	  fh->oh = fdh;
	}
    }

  return fdh;
}

/* Make a fake function descriptor sym for the code sym FH.  */

static struct ppc_link_hash_entry *
make_fdh (struct bfd_link_info *info,
	  struct ppc_link_hash_entry *fh)
{
  bfd *abfd;
  asymbol *newsym;
  struct bfd_link_hash_entry *bh;
  struct ppc_link_hash_entry *fdh;

  abfd = fh->elf.root.u.undef.abfd;
  newsym = bfd_make_empty_symbol (abfd);
  newsym->name = fh->elf.root.root.string + 1;
  newsym->section = bfd_und_section_ptr;
  newsym->value = 0;
  newsym->flags = BSF_WEAK;

  bh = NULL;
  if (!_bfd_generic_link_add_one_symbol (info, abfd, newsym->name,
					 newsym->flags, newsym->section,
					 newsym->value, NULL, FALSE, FALSE,
					 &bh))
    return NULL;

  fdh = (struct ppc_link_hash_entry *) bh;
  fdh->elf.non_elf = 0;
  fdh->fake = 1;
  fdh->is_func_descriptor = 1;
  fdh->oh = fh;
  fh->is_func = 1;
  fh->oh = fdh;
  return fdh;
}

/* Fix function descriptor symbols defined in .opd sections to be
   function type.  */

static bfd_boolean
ppc64_elf_add_symbol_hook (bfd *ibfd ATTRIBUTE_UNUSED,
			   struct bfd_link_info *info ATTRIBUTE_UNUSED,
			   Elf_Internal_Sym *isym,
			   const char **name ATTRIBUTE_UNUSED,
			   flagword *flags ATTRIBUTE_UNUSED,
			   asection **sec,
			   bfd_vma *value ATTRIBUTE_UNUSED)
{
  if (*sec != NULL
      && strcmp (bfd_get_section_name (ibfd, *sec), ".opd") == 0)
    isym->st_info = ELF_ST_INFO (ELF_ST_BIND (isym->st_info), STT_FUNC);

  return TRUE;
}

/* This function makes an old ABI object reference to ".bar" cause the
   inclusion of a new ABI object archive that defines "bar".
   NAME is a symbol defined in an archive.  Return a symbol in the hash
   table that might be satisfied by the archive symbols.  */

static struct elf_link_hash_entry *
ppc64_elf_archive_symbol_lookup (bfd *abfd,
				 struct bfd_link_info *info,
				 const char *name)
{
  struct elf_link_hash_entry *h;
  char *dot_name;
  size_t len;

  h = _bfd_elf_archive_symbol_lookup (abfd, info, name);
  if (h != NULL
      /* Don't return this sym if it is a fake function descriptor
	 created by add_symbol_adjust.  */
      && !(h->root.type == bfd_link_hash_undefweak
	   && ((struct ppc_link_hash_entry *) h)->fake))
    return h;

  if (name[0] == '.')
    return h;

  len = strlen (name);
  dot_name = bfd_alloc (abfd, len + 2);
  if (dot_name == NULL)
    return (struct elf_link_hash_entry *) 0 - 1;
  dot_name[0] = '.';
  memcpy (dot_name + 1, name, len + 1);
  h = _bfd_elf_archive_symbol_lookup (abfd, info, dot_name);
  bfd_release (abfd, dot_name);
  return h;
}

/* This function satisfies all old ABI object references to ".bar" if a
   new ABI object defines "bar".  Well, at least, undefined dot symbols
   are made weak.  This stops later archive searches from including an
   object if we already have a function descriptor definition.  It also
   prevents the linker complaining about undefined symbols.
   We also check and correct mismatched symbol visibility here.  The
   most restrictive visibility of the function descriptor and the
   function entry symbol is used.  */

static bfd_boolean
add_symbol_adjust (struct ppc_link_hash_entry *eh, struct bfd_link_info *info)
{
  struct ppc_link_hash_table *htab;
  struct ppc_link_hash_entry *fdh;

  if (eh->elf.root.type == bfd_link_hash_indirect)
    return TRUE;

  if (eh->elf.root.type == bfd_link_hash_warning)
    eh = (struct ppc_link_hash_entry *) eh->elf.root.u.i.link;

  if (eh->elf.root.root.string[0] != '.')
    abort ();

  htab = ppc_hash_table (info);
  fdh = get_fdh (eh, htab);
  if (fdh == NULL
      && !info->relocatable
      && (eh->elf.root.type == bfd_link_hash_undefined
	  || eh->elf.root.type == bfd_link_hash_undefweak)
      && eh->elf.ref_regular)
    {
      /* Make an undefweak function descriptor sym, which is enough to
	 pull in an --as-needed shared lib, but won't cause link
	 errors.  Archives are handled elsewhere.  */
      fdh = make_fdh (info, eh);
      if (fdh == NULL)
	return FALSE;
      else
	fdh->elf.ref_regular = 1;
    }
  else if (fdh != NULL)
    {
      unsigned entry_vis = ELF_ST_VISIBILITY (eh->elf.other) - 1;
      unsigned descr_vis = ELF_ST_VISIBILITY (fdh->elf.other) - 1;
      if (entry_vis < descr_vis)
	fdh->elf.other += entry_vis - descr_vis;
      else if (entry_vis > descr_vis)
	eh->elf.other += descr_vis - entry_vis;

      if ((fdh->elf.root.type == bfd_link_hash_defined
	   || fdh->elf.root.type == bfd_link_hash_defweak)
	  && eh->elf.root.type == bfd_link_hash_undefined)
	{
	  eh->elf.root.type = bfd_link_hash_undefweak;
	  eh->was_undefined = 1;
	  htab->twiddled_syms = 1;
	}
    }

  return TRUE;
}

/* Process list of dot-symbols we made in link_hash_newfunc.  */

static bfd_boolean
ppc64_elf_check_directives (bfd *ibfd, struct bfd_link_info *info)
{
  struct ppc_link_hash_table *htab;
  struct ppc_link_hash_entry **p, *eh;

  htab = ppc_hash_table (info);
  if (!is_ppc64_elf_target (htab->elf.root.creator))
    return TRUE;

  if (is_ppc64_elf_target (ibfd->xvec))
    {
      p = &htab->dot_syms;
      while ((eh = *p) != NULL)
	{
	  *p = NULL;
	  if (!add_symbol_adjust (eh, info))
	    return FALSE;
	  p = &eh->u.next_dot_sym;
	}
    }

  /* Clear the list for non-ppc64 input files.  */
  p = &htab->dot_syms;
  while ((eh = *p) != NULL)
    {
      *p = NULL;
      p = &eh->u.next_dot_sym;
    }

  /* We need to fix the undefs list for any syms we have twiddled to
     undef_weak.  */
  if (htab->twiddled_syms)
    {
      bfd_link_repair_undef_list (&htab->elf.root);
      htab->twiddled_syms = 0;
    }
  return TRUE;
}

/* Undo hash table changes when an --as-needed input file is determined
   not to be needed.  */

static bfd_boolean
ppc64_elf_as_needed_cleanup (bfd *ibfd ATTRIBUTE_UNUSED,
			     struct bfd_link_info *info)
{
  ppc_hash_table (info)->dot_syms = NULL;
  return TRUE;
}

static bfd_boolean
update_local_sym_info (bfd *abfd, Elf_Internal_Shdr *symtab_hdr,
		       unsigned long r_symndx, bfd_vma r_addend, int tls_type)
{
  struct got_entry **local_got_ents = elf_local_got_ents (abfd);
  char *local_got_tls_masks;

  if (local_got_ents == NULL)
    {
      bfd_size_type size = symtab_hdr->sh_info;

      size *= sizeof (*local_got_ents) + sizeof (*local_got_tls_masks);
      local_got_ents = bfd_zalloc (abfd, size);
      if (local_got_ents == NULL)
	return FALSE;
      elf_local_got_ents (abfd) = local_got_ents;
    }

  if ((tls_type & TLS_EXPLICIT) == 0)
    {
      struct got_entry *ent;

      for (ent = local_got_ents[r_symndx]; ent != NULL; ent = ent->next)
	if (ent->addend == r_addend
	    && ent->owner == abfd
	    && ent->tls_type == tls_type)
	  break;
      if (ent == NULL)
	{
	  bfd_size_type amt = sizeof (*ent);
	  ent = bfd_alloc (abfd, amt);
	  if (ent == NULL)
	    return FALSE;
	  ent->next = local_got_ents[r_symndx];
	  ent->addend = r_addend;
	  ent->owner = abfd;
	  ent->tls_type = tls_type;
	  ent->got.refcount = 0;
	  local_got_ents[r_symndx] = ent;
	}
      ent->got.refcount += 1;
    }

  local_got_tls_masks = (char *) (local_got_ents + symtab_hdr->sh_info);
  local_got_tls_masks[r_symndx] |= tls_type;
  return TRUE;
}

static bfd_boolean
update_plt_info (bfd *abfd, struct ppc_link_hash_entry *eh, bfd_vma addend)
{
  struct plt_entry *ent;

  for (ent = eh->elf.plt.plist; ent != NULL; ent = ent->next)
    if (ent->addend == addend)
      break;
  if (ent == NULL)
    {
      bfd_size_type amt = sizeof (*ent);
      ent = bfd_alloc (abfd, amt);
      if (ent == NULL)
	return FALSE;
      ent->next = eh->elf.plt.plist;
      ent->addend = addend;
      ent->plt.refcount = 0;
      eh->elf.plt.plist = ent;
    }
  ent->plt.refcount += 1;
  eh->elf.needs_plt = 1;
  if (eh->elf.root.root.string[0] == '.'
      && eh->elf.root.root.string[1] != '\0')
    eh->is_func = 1;
  return TRUE;
}

/* Look through the relocs for a section during the first phase, and
   calculate needed space in the global offset table, procedure
   linkage table, and dynamic reloc sections.  */

static bfd_boolean
ppc64_elf_check_relocs (bfd *abfd, struct bfd_link_info *info,
			asection *sec, const Elf_Internal_Rela *relocs)
{
  struct ppc_link_hash_table *htab;
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes, **sym_hashes_end;
  const Elf_Internal_Rela *rel;
  const Elf_Internal_Rela *rel_end;
  asection *sreloc;
  asection **opd_sym_map;
  struct elf_link_hash_entry *tga, *dottga;

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

  htab = ppc_hash_table (info);
  tga = elf_link_hash_lookup (&htab->elf, "__tls_get_addr",
			      FALSE, FALSE, TRUE);
  dottga = elf_link_hash_lookup (&htab->elf, ".__tls_get_addr",
				 FALSE, FALSE, TRUE);
  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;

  sym_hashes = elf_sym_hashes (abfd);
  sym_hashes_end = (sym_hashes
		    + symtab_hdr->sh_size / sizeof (Elf64_External_Sym)
		    - symtab_hdr->sh_info);

  sreloc = NULL;
  opd_sym_map = NULL;
  if (strcmp (bfd_get_section_name (abfd, sec), ".opd") == 0)
    {
      /* Garbage collection needs some extra help with .opd sections.
	 We don't want to necessarily keep everything referenced by
	 relocs in .opd, as that would keep all functions.  Instead,
	 if we reference an .opd symbol (a function descriptor), we
	 want to keep the function code symbol's section.  This is
	 easy for global symbols, but for local syms we need to keep
	 information about the associated function section.  Later, if
	 edit_opd deletes entries, we'll use this array to adjust
	 local syms in .opd.  */
      union opd_info {
	asection *func_section;
	long entry_adjust;
      };
      bfd_size_type amt;

      amt = sec->size * sizeof (union opd_info) / 8;
      opd_sym_map = bfd_zalloc (abfd, amt);
      if (opd_sym_map == NULL)
	return FALSE;
      ppc64_elf_section_data (sec)->u.opd_func_sec = opd_sym_map;
      BFD_ASSERT (ppc64_elf_section_data (sec)->sec_type == sec_normal);
      ppc64_elf_section_data (sec)->sec_type = sec_opd;
    }

  if (htab->sfpr == NULL
      && !create_linkage_sections (htab->elf.dynobj, info))
    return FALSE;

  rel_end = relocs + sec->reloc_count;
  for (rel = relocs; rel < rel_end; rel++)
    {
      unsigned long r_symndx;
      struct elf_link_hash_entry *h;
      enum elf_ppc64_reloc_type r_type;
      int tls_type;
      struct _ppc64_elf_section_data *ppc64_sec;

      r_symndx = ELF64_R_SYM (rel->r_info);
      if (r_symndx < symtab_hdr->sh_info)
	h = NULL;
      else
	{
	  h = sym_hashes[r_symndx - symtab_hdr->sh_info];
	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;
	}

      tls_type = 0;
      r_type = ELF64_R_TYPE (rel->r_info);
      if (h != NULL && (h == tga || h == dottga))
	switch (r_type)
	  {
	  default:
	    break;

	  case R_PPC64_REL24:
	  case R_PPC64_REL14:
	  case R_PPC64_REL14_BRTAKEN:
	  case R_PPC64_REL14_BRNTAKEN:
	  case R_PPC64_ADDR24:
	  case R_PPC64_ADDR14:
	  case R_PPC64_ADDR14_BRTAKEN:
	  case R_PPC64_ADDR14_BRNTAKEN:
	    if (rel != relocs
		&& (ELF64_R_TYPE (rel[-1].r_info) == R_PPC64_TLSGD
		    || ELF64_R_TYPE (rel[-1].r_info) == R_PPC64_TLSLD))
	      /* We have a new-style __tls_get_addr call with a marker
		 reloc.  */
	      ;
	    else
	      /* Mark this section as having an old-style call.  */
	      sec->has_tls_get_addr_call = 1;
	    break;
	  }

      switch (r_type)
	{
	case R_PPC64_TLSGD:
	case R_PPC64_TLSLD:
	  /* These special tls relocs tie a call to __tls_get_addr with
	     its parameter symbol.  */
	  break;

	case R_PPC64_GOT_TLSLD16:
	case R_PPC64_GOT_TLSLD16_LO:
	case R_PPC64_GOT_TLSLD16_HI:
	case R_PPC64_GOT_TLSLD16_HA:
	  tls_type = TLS_TLS | TLS_LD;
	  goto dogottls;

	case R_PPC64_GOT_TLSGD16:
	case R_PPC64_GOT_TLSGD16_LO:
	case R_PPC64_GOT_TLSGD16_HI:
	case R_PPC64_GOT_TLSGD16_HA:
	  tls_type = TLS_TLS | TLS_GD;
	  goto dogottls;

	case R_PPC64_GOT_TPREL16_DS:
	case R_PPC64_GOT_TPREL16_LO_DS:
	case R_PPC64_GOT_TPREL16_HI:
	case R_PPC64_GOT_TPREL16_HA:
	  if (!info->executable)
	    info->flags |= DF_STATIC_TLS;
	  tls_type = TLS_TLS | TLS_TPREL;
	  goto dogottls;

	case R_PPC64_GOT_DTPREL16_DS:
	case R_PPC64_GOT_DTPREL16_LO_DS:
	case R_PPC64_GOT_DTPREL16_HI:
	case R_PPC64_GOT_DTPREL16_HA:
	  tls_type = TLS_TLS | TLS_DTPREL;
	dogottls:
	  sec->has_tls_reloc = 1;
	  /* Fall thru */

	case R_PPC64_GOT16:
	case R_PPC64_GOT16_DS:
	case R_PPC64_GOT16_HA:
	case R_PPC64_GOT16_HI:
	case R_PPC64_GOT16_LO:
	case R_PPC64_GOT16_LO_DS:
	  /* This symbol requires a global offset table entry.  */
	  sec->has_toc_reloc = 1;
	  if (ppc64_elf_tdata (abfd)->got == NULL
	      && !create_got_section (abfd, info))
	    return FALSE;

	  if (h != NULL)
	    {
	      struct ppc_link_hash_entry *eh;
	      struct got_entry *ent;

	      eh = (struct ppc_link_hash_entry *) h;
	      for (ent = eh->elf.got.glist; ent != NULL; ent = ent->next)
		if (ent->addend == rel->r_addend
		    && ent->owner == abfd
		    && ent->tls_type == tls_type)
		  break;
	      if (ent == NULL)
		{
		  bfd_size_type amt = sizeof (*ent);
		  ent = bfd_alloc (abfd, amt);
		  if (ent == NULL)
		    return FALSE;
		  ent->next = eh->elf.got.glist;
		  ent->addend = rel->r_addend;
		  ent->owner = abfd;
		  ent->tls_type = tls_type;
		  ent->got.refcount = 0;
		  eh->elf.got.glist = ent;
		}
	      ent->got.refcount += 1;
	      eh->tls_mask |= tls_type;
	    }
	  else
	    /* This is a global offset table entry for a local symbol.  */
	    if (!update_local_sym_info (abfd, symtab_hdr, r_symndx,
					rel->r_addend, tls_type))
	      return FALSE;
	  break;

	case R_PPC64_PLT16_HA:
	case R_PPC64_PLT16_HI:
	case R_PPC64_PLT16_LO:
	case R_PPC64_PLT32:
	case R_PPC64_PLT64:
	  /* This symbol requires a procedure linkage table entry.  We
	     actually build the entry in adjust_dynamic_symbol,
	     because this might be a case of linking PIC code without
	     linking in any dynamic objects, in which case we don't
	     need to generate a procedure linkage table after all.  */
	  if (h == NULL)
	    {
	      /* It does not make sense to have a procedure linkage
		 table entry for a local symbol.  */
	      bfd_set_error (bfd_error_bad_value);
	      return FALSE;
	    }
	  else
	    if (!update_plt_info (abfd, (struct ppc_link_hash_entry *) h,
				  rel->r_addend))
	      return FALSE;
	  break;

	  /* The following relocations don't need to propagate the
	     relocation if linking a shared object since they are
	     section relative.  */
	case R_PPC64_SECTOFF:
	case R_PPC64_SECTOFF_LO:
	case R_PPC64_SECTOFF_HI:
	case R_PPC64_SECTOFF_HA:
	case R_PPC64_SECTOFF_DS:
	case R_PPC64_SECTOFF_LO_DS:
	case R_PPC64_DTPREL16:
	case R_PPC64_DTPREL16_LO:
	case R_PPC64_DTPREL16_HI:
	case R_PPC64_DTPREL16_HA:
	case R_PPC64_DTPREL16_DS:
	case R_PPC64_DTPREL16_LO_DS:
	case R_PPC64_DTPREL16_HIGHER:
	case R_PPC64_DTPREL16_HIGHERA:
	case R_PPC64_DTPREL16_HIGHEST:
	case R_PPC64_DTPREL16_HIGHESTA:
	  break;

	  /* Nor do these.  */
	case R_PPC64_TOC16:
	case R_PPC64_TOC16_LO:
	case R_PPC64_TOC16_HI:
	case R_PPC64_TOC16_HA:
	case R_PPC64_TOC16_DS:
	case R_PPC64_TOC16_LO_DS:
	  sec->has_toc_reloc = 1;
	  break;

	  /* This relocation describes the C++ object vtable hierarchy.
	     Reconstruct it for later use during GC.  */
	case R_PPC64_GNU_VTINHERIT:
	  if (!bfd_elf_gc_record_vtinherit (abfd, sec, h, rel->r_offset))
	    return FALSE;
	  break;

	  /* This relocation describes which C++ vtable entries are actually
	     used.  Record for later use during GC.  */
	case R_PPC64_GNU_VTENTRY:
	  if (!bfd_elf_gc_record_vtentry (abfd, sec, h, rel->r_addend))
	    return FALSE;
	  break;

	case R_PPC64_REL14:
	case R_PPC64_REL14_BRTAKEN:
	case R_PPC64_REL14_BRNTAKEN:
	  {
	    asection *dest = NULL;

	    /* Heuristic: If jumping outside our section, chances are
	       we are going to need a stub.  */
	    if (h != NULL)
	      {
		/* If the sym is weak it may be overridden later, so
		   don't assume we know where a weak sym lives.  */
		if (h->root.type == bfd_link_hash_defined)
		  dest = h->root.u.def.section;
	      }
	    else
	      dest = bfd_section_from_r_symndx (abfd, &htab->sym_sec,
						sec, r_symndx);
	    if (dest != sec)
	      ppc64_elf_section_data (sec)->has_14bit_branch = 1;
	  }
	  /* Fall through.  */

	case R_PPC64_REL24:
	  if (h != NULL)
	    {
	      /* We may need a .plt entry if the function this reloc
		 refers to is in a shared lib.  */
	      if (!update_plt_info (abfd, (struct ppc_link_hash_entry *) h,
				    rel->r_addend))
		return FALSE;
	      if (h == tga || h == dottga)
		sec->has_tls_reloc = 1;
	    }
	  break;

	case R_PPC64_TPREL64:
	  tls_type = TLS_EXPLICIT | TLS_TLS | TLS_TPREL;
	  if (!info->executable)
	    info->flags |= DF_STATIC_TLS;
	  goto dotlstoc;

	case R_PPC64_DTPMOD64:
	  if (rel + 1 < rel_end
	      && rel[1].r_info == ELF64_R_INFO (r_symndx, R_PPC64_DTPREL64)
	      && rel[1].r_offset == rel->r_offset + 8)
	    tls_type = TLS_EXPLICIT | TLS_TLS | TLS_GD;
	  else
	    tls_type = TLS_EXPLICIT | TLS_TLS | TLS_LD;
	  goto dotlstoc;

	case R_PPC64_DTPREL64:
	  tls_type = TLS_EXPLICIT | TLS_TLS | TLS_DTPREL;
	  if (rel != relocs
	      && rel[-1].r_info == ELF64_R_INFO (r_symndx, R_PPC64_DTPMOD64)
	      && rel[-1].r_offset == rel->r_offset - 8)
	    /* This is the second reloc of a dtpmod, dtprel pair.
	       Don't mark with TLS_DTPREL.  */
	    goto dodyn;

	dotlstoc:
	  sec->has_tls_reloc = 1;
	  if (h != NULL)
	    {
	      struct ppc_link_hash_entry *eh;
	      eh = (struct ppc_link_hash_entry *) h;
	      eh->tls_mask |= tls_type;
	    }
	  else
	    if (!update_local_sym_info (abfd, symtab_hdr, r_symndx,
					rel->r_addend, tls_type))
	      return FALSE;

	  ppc64_sec = ppc64_elf_section_data (sec);
	  if (ppc64_sec->sec_type != sec_toc)
	    {
	      bfd_size_type amt;

	      /* One extra to simplify get_tls_mask.  */
	      amt = sec->size * sizeof (unsigned) / 8 + sizeof (unsigned);
	      ppc64_sec->u.toc.symndx = bfd_zalloc (abfd, amt);
	      if (ppc64_sec->u.toc.symndx == NULL)
		return FALSE;
	      amt = sec->size * sizeof (bfd_vma) / 8;
	      ppc64_sec->u.toc.add = bfd_zalloc (abfd, amt);
	      if (ppc64_sec->u.toc.add == NULL)
		return FALSE;
	      BFD_ASSERT (ppc64_sec->sec_type == sec_normal);
	      ppc64_sec->sec_type = sec_toc;
	    }
	  BFD_ASSERT (rel->r_offset % 8 == 0);
	  ppc64_sec->u.toc.symndx[rel->r_offset / 8] = r_symndx;
	  ppc64_sec->u.toc.add[rel->r_offset / 8] = rel->r_addend;

	  /* Mark the second slot of a GD or LD entry.
	     -1 to indicate GD and -2 to indicate LD.  */
	  if (tls_type == (TLS_EXPLICIT | TLS_TLS | TLS_GD))
	    ppc64_sec->u.toc.symndx[rel->r_offset / 8 + 1] = -1;
	  else if (tls_type == (TLS_EXPLICIT | TLS_TLS | TLS_LD))
	    ppc64_sec->u.toc.symndx[rel->r_offset / 8 + 1] = -2;
	  goto dodyn;

	case R_PPC64_TPREL16:
	case R_PPC64_TPREL16_LO:
	case R_PPC64_TPREL16_HI:
	case R_PPC64_TPREL16_HA:
	case R_PPC64_TPREL16_DS:
	case R_PPC64_TPREL16_LO_DS:
	case R_PPC64_TPREL16_HIGHER:
	case R_PPC64_TPREL16_HIGHERA:
	case R_PPC64_TPREL16_HIGHEST:
	case R_PPC64_TPREL16_HIGHESTA:
	  if (info->shared)
	    {
	      if (!info->executable)
		info->flags |= DF_STATIC_TLS;
	      goto dodyn;
	    }
	  break;

	case R_PPC64_ADDR64:
	  if (opd_sym_map != NULL
	      && rel + 1 < rel_end
	      && ELF64_R_TYPE ((rel + 1)->r_info) == R_PPC64_TOC)
	    {
	      if (h != NULL)
		{
		  if (h->root.root.string[0] == '.'
		      && h->root.root.string[1] != 0
		      && get_fdh ((struct ppc_link_hash_entry *) h, htab))
		    ;
		  else
		    ((struct ppc_link_hash_entry *) h)->is_func = 1;
		}
	      else
		{
		  asection *s;

		  s = bfd_section_from_r_symndx (abfd, &htab->sym_sec, sec,
						 r_symndx);
		  if (s == NULL)
		    return FALSE;
		  else if (s != sec)
		    opd_sym_map[rel->r_offset / 8] = s;
		}
	    }
	  /* Fall through.  */

	case R_PPC64_REL30:
	case R_PPC64_REL32:
	case R_PPC64_REL64:
	case R_PPC64_ADDR14:
	case R_PPC64_ADDR14_BRNTAKEN:
	case R_PPC64_ADDR14_BRTAKEN:
	case R_PPC64_ADDR16:
	case R_PPC64_ADDR16_DS:
	case R_PPC64_ADDR16_HA:
	case R_PPC64_ADDR16_HI:
	case R_PPC64_ADDR16_HIGHER:
	case R_PPC64_ADDR16_HIGHERA:
	case R_PPC64_ADDR16_HIGHEST:
	case R_PPC64_ADDR16_HIGHESTA:
	case R_PPC64_ADDR16_LO:
	case R_PPC64_ADDR16_LO_DS:
	case R_PPC64_ADDR24:
	case R_PPC64_ADDR32:
	case R_PPC64_UADDR16:
	case R_PPC64_UADDR32:
	case R_PPC64_UADDR64:
	case R_PPC64_TOC:
	  if (h != NULL && !info->shared)
	    /* We may need a copy reloc.  */
	    h->non_got_ref = 1;

	  /* Don't propagate .opd relocs.  */
	  if (NO_OPD_RELOCS && opd_sym_map != NULL)
	    break;

	  /* If we are creating a shared library, and this is a reloc
	     against a global symbol, or a non PC relative reloc
	     against a local symbol, then we need to copy the reloc
	     into the shared library.  However, if we are linking with
	     -Bsymbolic, we do not need to copy a reloc against a
	     global symbol which is defined in an object we are
	     including in the link (i.e., DEF_REGULAR is set).  At
	     this point we have not seen all the input files, so it is
	     possible that DEF_REGULAR is not set now but will be set
	     later (it is never cleared).  In case of a weak definition,
	     DEF_REGULAR may be cleared later by a strong definition in
	     a shared library.  We account for that possibility below by
	     storing information in the dyn_relocs field of the hash
	     table entry.  A similar situation occurs when creating
	     shared libraries and symbol visibility changes render the
	     symbol local.

	     If on the other hand, we are creating an executable, we
	     may need to keep relocations for symbols satisfied by a
	     dynamic library if we manage to avoid copy relocs for the
	     symbol.  */
	dodyn:
	  if ((info->shared
	       && (must_be_dyn_reloc (info, r_type)
		   || (h != NULL
		       && (! info->symbolic
			   || h->root.type == bfd_link_hash_defweak
			   || !h->def_regular))))
	      || (ELIMINATE_COPY_RELOCS
		  && !info->shared
		  && h != NULL
		  && (h->root.type == bfd_link_hash_defweak
		      || !h->def_regular)))
	    {
	      struct ppc_dyn_relocs *p;
	      struct ppc_dyn_relocs **head;

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
		      bfd_set_error (bfd_error_bad_value);
		    }

		  dynobj = htab->elf.dynobj;
		  sreloc = bfd_get_section_by_name (dynobj, name);
		  if (sreloc == NULL)
		    {
		      flagword flags;

		      flags = (SEC_HAS_CONTENTS | SEC_READONLY
			       | SEC_IN_MEMORY | SEC_LINKER_CREATED
			       | SEC_ALLOC | SEC_LOAD);
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
		  head = &((struct ppc_link_hash_entry *) h)->dyn_relocs;
		}
	      else
		{
		  /* Track dynamic relocs needed for local syms too.
		     We really need local syms available to do this
		     easily.  Oh well.  */

		  asection *s;
		  void *vpp;

		  s = bfd_section_from_r_symndx (abfd, &htab->sym_sec,
						 sec, r_symndx);
		  if (s == NULL)
		    return FALSE;

		  vpp = &elf_section_data (s)->local_dynrel;
		  head = (struct ppc_dyn_relocs **) vpp;
		}

	      p = *head;
	      if (p == NULL || p->sec != sec)
		{
		  p = bfd_alloc (htab->elf.dynobj, sizeof *p);
		  if (p == NULL)
		    return FALSE;
		  p->next = *head;
		  *head = p;
		  p->sec = sec;
		  p->count = 0;
		  p->pc_count = 0;
		}

	      p->count += 1;
	      if (!must_be_dyn_reloc (info, r_type))
		p->pc_count += 1;
	    }
	  break;

	default:
	  break;
	}
    }

  return TRUE;
}

/* OFFSET in OPD_SEC specifies a function descriptor.  Return the address
   of the code entry point, and its section.  */

static bfd_vma
opd_entry_value (asection *opd_sec,
		 bfd_vma offset,
		 asection **code_sec,
		 bfd_vma *code_off)
{
  bfd *opd_bfd = opd_sec->owner;
  Elf_Internal_Rela *relocs;
  Elf_Internal_Rela *lo, *hi, *look;
  bfd_vma val;

  /* No relocs implies we are linking a --just-symbols object.  */
  if (opd_sec->reloc_count == 0)
    {
      bfd_vma val;

      if (!bfd_get_section_contents (opd_bfd, opd_sec, &val, offset, 8))
	return (bfd_vma) -1;

      if (code_sec != NULL)
	{
	  asection *sec, *likely = NULL;
	  for (sec = opd_bfd->sections; sec != NULL; sec = sec->next)
	    if (sec->vma <= val
		&& (sec->flags & SEC_LOAD) != 0
		&& (sec->flags & SEC_ALLOC) != 0)
	      likely = sec;
	  if (likely != NULL)
	    {
	      *code_sec = likely;
	      if (code_off != NULL)
		*code_off = val - likely->vma;
	    }
	}
      return val;
    }

  relocs = ppc64_elf_tdata (opd_bfd)->opd_relocs;
  if (relocs == NULL)
    relocs = _bfd_elf_link_read_relocs (opd_bfd, opd_sec, NULL, NULL, TRUE);

  /* Go find the opd reloc at the sym address.  */
  lo = relocs;
  BFD_ASSERT (lo != NULL);
  hi = lo + opd_sec->reloc_count - 1; /* ignore last reloc */
  val = (bfd_vma) -1;
  while (lo < hi)
    {
      look = lo + (hi - lo) / 2;
      if (look->r_offset < offset)
	lo = look + 1;
      else if (look->r_offset > offset)
	hi = look;
      else
	{
	  Elf_Internal_Shdr *symtab_hdr = &elf_tdata (opd_bfd)->symtab_hdr;
	  if (ELF64_R_TYPE (look->r_info) == R_PPC64_ADDR64
	      && ELF64_R_TYPE ((look + 1)->r_info) == R_PPC64_TOC)
	    {
	      unsigned long symndx = ELF64_R_SYM (look->r_info);
	      asection *sec;

	      if (symndx < symtab_hdr->sh_info)
		{
		  Elf_Internal_Sym *sym;

		  sym = (Elf_Internal_Sym *) symtab_hdr->contents;
		  if (sym == NULL)
		    {
		      sym = bfd_elf_get_elf_syms (opd_bfd, symtab_hdr,
						  symtab_hdr->sh_info,
						  0, NULL, NULL, NULL);
		      if (sym == NULL)
			break;
		      symtab_hdr->contents = (bfd_byte *) sym;
		    }

		  sym += symndx;
		  val = sym->st_value;
		  sec = NULL;
		  if ((sym->st_shndx != SHN_UNDEF
		       && sym->st_shndx < SHN_LORESERVE)
		      || sym->st_shndx > SHN_HIRESERVE)
		    sec = bfd_section_from_elf_index (opd_bfd, sym->st_shndx);
		  BFD_ASSERT ((sec->flags & SEC_MERGE) == 0);
		}
	      else
		{
		  struct elf_link_hash_entry **sym_hashes;
		  struct elf_link_hash_entry *rh;

		  sym_hashes = elf_sym_hashes (opd_bfd);
		  rh = sym_hashes[symndx - symtab_hdr->sh_info];
		  while (rh->root.type == bfd_link_hash_indirect
			 || rh->root.type == bfd_link_hash_warning)
		    rh = ((struct elf_link_hash_entry *) rh->root.u.i.link);
		  BFD_ASSERT (rh->root.type == bfd_link_hash_defined
			      || rh->root.type == bfd_link_hash_defweak);
		  val = rh->root.u.def.value;
		  sec = rh->root.u.def.section;
		}
	      val += look->r_addend;
	      if (code_off != NULL)
		*code_off = val;
	      if (code_sec != NULL)
		*code_sec = sec;
	      if (sec != NULL && sec->output_section != NULL)
		val += sec->output_section->vma + sec->output_offset;
	    }
	  break;
	}
    }

  return val;
}

/* Mark sections containing dynamically referenced symbols.  When
   building shared libraries, we must assume that any visible symbol is
   referenced.  */

static bfd_boolean
ppc64_elf_gc_mark_dynamic_ref (struct elf_link_hash_entry *h, void *inf)
{
  struct bfd_link_info *info = (struct bfd_link_info *) inf;
  struct ppc_link_hash_entry *eh = (struct ppc_link_hash_entry *) h;

  if (eh->elf.root.type == bfd_link_hash_warning)
    eh = (struct ppc_link_hash_entry *) eh->elf.root.u.i.link;

  /* Dynamic linking info is on the func descriptor sym.  */
  if (eh->oh != NULL
      && eh->oh->is_func_descriptor
      && (eh->oh->elf.root.type == bfd_link_hash_defined
	  || eh->oh->elf.root.type == bfd_link_hash_defweak))
    eh = eh->oh;

  if ((eh->elf.root.type == bfd_link_hash_defined
       || eh->elf.root.type == bfd_link_hash_defweak)
      && (eh->elf.ref_dynamic
	  || (!info->executable
	      && eh->elf.def_regular
	      && ELF_ST_VISIBILITY (eh->elf.other) != STV_INTERNAL
	      && ELF_ST_VISIBILITY (eh->elf.other) != STV_HIDDEN)))
    {
      asection *code_sec;

      eh->elf.root.u.def.section->flags |= SEC_KEEP;

      /* Function descriptor syms cause the associated
	 function code sym section to be marked.  */
      if (eh->is_func_descriptor
	  && (eh->oh->elf.root.type == bfd_link_hash_defined
	      || eh->oh->elf.root.type == bfd_link_hash_defweak))
	eh->oh->elf.root.u.def.section->flags |= SEC_KEEP;
      else if (get_opd_info (eh->elf.root.u.def.section) != NULL
	       && opd_entry_value (eh->elf.root.u.def.section,
				   eh->elf.root.u.def.value,
				   &code_sec, NULL) != (bfd_vma) -1)
	code_sec->flags |= SEC_KEEP;
    }

  return TRUE;
}

/* Return the section that should be marked against GC for a given
   relocation.  */

static asection *
ppc64_elf_gc_mark_hook (asection *sec,
			struct bfd_link_info *info,
			Elf_Internal_Rela *rel,
			struct elf_link_hash_entry *h,
			Elf_Internal_Sym *sym)
{
  asection *rsec;

  /* First mark all our entry sym sections.  */
  if (info->gc_sym_list != NULL)
    {
      struct ppc_link_hash_table *htab = ppc_hash_table (info);
      struct bfd_sym_chain *sym = info->gc_sym_list;

      info->gc_sym_list = NULL;
      for (; sym != NULL; sym = sym->next)
	{
	  struct ppc_link_hash_entry *eh;

	  eh = (struct ppc_link_hash_entry *)
	    elf_link_hash_lookup (&htab->elf, sym->name, FALSE, FALSE, FALSE);
	  if (eh == NULL)
	    continue;
	  if (eh->elf.root.type != bfd_link_hash_defined
	      && eh->elf.root.type != bfd_link_hash_defweak)
	    continue;

	  if (eh->is_func_descriptor
	      && (eh->oh->elf.root.type == bfd_link_hash_defined
		  || eh->oh->elf.root.type == bfd_link_hash_defweak))
	    rsec = eh->oh->elf.root.u.def.section;
	  else if (get_opd_info (eh->elf.root.u.def.section) != NULL
		   && opd_entry_value (eh->elf.root.u.def.section,
				       eh->elf.root.u.def.value,
				       &rsec, NULL) != (bfd_vma) -1)
	    ;
	  else
	    continue;

	  if (!rsec->gc_mark)
	    _bfd_elf_gc_mark (info, rsec, ppc64_elf_gc_mark_hook);

	  rsec = eh->elf.root.u.def.section;
	  if (!rsec->gc_mark)
	    _bfd_elf_gc_mark (info, rsec, ppc64_elf_gc_mark_hook);
	}
    }

  /* Syms return NULL if we're marking .opd, so we avoid marking all
     function sections, as all functions are referenced in .opd.  */
  rsec = NULL;
  if (get_opd_info (sec) != NULL)
    return rsec;

  if (h != NULL)
    {
      enum elf_ppc64_reloc_type r_type;
      struct ppc_link_hash_entry *eh;

      r_type = ELF64_R_TYPE (rel->r_info);
      switch (r_type)
	{
	case R_PPC64_GNU_VTINHERIT:
	case R_PPC64_GNU_VTENTRY:
	  break;

	default:
	  switch (h->root.type)
	    {
	    case bfd_link_hash_defined:
	    case bfd_link_hash_defweak:
	      eh = (struct ppc_link_hash_entry *) h;
	      if (eh->oh != NULL
		  && eh->oh->is_func_descriptor
		  && (eh->oh->elf.root.type == bfd_link_hash_defined
		      || eh->oh->elf.root.type == bfd_link_hash_defweak))
		eh = eh->oh;

	      /* Function descriptor syms cause the associated
		 function code sym section to be marked.  */
	      if (eh->is_func_descriptor
		  && (eh->oh->elf.root.type == bfd_link_hash_defined
		      || eh->oh->elf.root.type == bfd_link_hash_defweak))
		{
		  /* They also mark their opd section.  */
		  if (!eh->elf.root.u.def.section->gc_mark)
		    _bfd_elf_gc_mark (info, eh->elf.root.u.def.section,
				      ppc64_elf_gc_mark_hook);

		  rsec = eh->oh->elf.root.u.def.section;
		}
	      else if (get_opd_info (eh->elf.root.u.def.section) != NULL
		       && opd_entry_value (eh->elf.root.u.def.section,
					   eh->elf.root.u.def.value,
					   &rsec, NULL) != (bfd_vma) -1)
		{
		  if (!eh->elf.root.u.def.section->gc_mark)
		    _bfd_elf_gc_mark (info, eh->elf.root.u.def.section,
				      ppc64_elf_gc_mark_hook);
		}
	      else
		rsec = h->root.u.def.section;
	      break;

	    case bfd_link_hash_common:
	      rsec = h->root.u.c.p->section;
	      break;

	    default:
	      break;
	    }
	}
    }
  else
    {
      asection **opd_sym_section;

      rsec = bfd_section_from_elf_index (sec->owner, sym->st_shndx);
      opd_sym_section = get_opd_info (rsec);
      if (opd_sym_section != NULL)
	{
	  if (!rsec->gc_mark)
	    _bfd_elf_gc_mark (info, rsec, ppc64_elf_gc_mark_hook);

	  rsec = opd_sym_section[(sym->st_value + rel->r_addend) / 8];
	}
    }

  return rsec;
}

/* Update the .got, .plt. and dynamic reloc reference counts for the
   section being removed.  */

static bfd_boolean
ppc64_elf_gc_sweep_hook (bfd *abfd, struct bfd_link_info *info,
			 asection *sec, const Elf_Internal_Rela *relocs)
{
  struct ppc_link_hash_table *htab;
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  struct got_entry **local_got_ents;
  const Elf_Internal_Rela *rel, *relend;

  if ((sec->flags & SEC_ALLOC) == 0)
    return TRUE;

  elf_section_data (sec)->local_dynrel = NULL;

  htab = ppc_hash_table (info);
  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (abfd);
  local_got_ents = elf_local_got_ents (abfd);

  relend = relocs + sec->reloc_count;
  for (rel = relocs; rel < relend; rel++)
    {
      unsigned long r_symndx;
      enum elf_ppc64_reloc_type r_type;
      struct elf_link_hash_entry *h = NULL;
      char tls_type = 0;

      r_symndx = ELF64_R_SYM (rel->r_info);
      r_type = ELF64_R_TYPE (rel->r_info);
      if (r_symndx >= symtab_hdr->sh_info)
	{
	  struct ppc_link_hash_entry *eh;
	  struct ppc_dyn_relocs **pp;
	  struct ppc_dyn_relocs *p;

	  h = sym_hashes[r_symndx - symtab_hdr->sh_info];
	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;
	  eh = (struct ppc_link_hash_entry *) h;

	  for (pp = &eh->dyn_relocs; (p = *pp) != NULL; pp = &p->next)
	    if (p->sec == sec)
	      {
		/* Everything must go for SEC.  */
		*pp = p->next;
		break;
	      }
	}

      switch (r_type)
	{
	case R_PPC64_GOT_TLSLD16:
	case R_PPC64_GOT_TLSLD16_LO:
	case R_PPC64_GOT_TLSLD16_HI:
	case R_PPC64_GOT_TLSLD16_HA:
	  tls_type = TLS_TLS | TLS_LD;
	  goto dogot;

	case R_PPC64_GOT_TLSGD16:
	case R_PPC64_GOT_TLSGD16_LO:
	case R_PPC64_GOT_TLSGD16_HI:
	case R_PPC64_GOT_TLSGD16_HA:
	  tls_type = TLS_TLS | TLS_GD;
	  goto dogot;

	case R_PPC64_GOT_TPREL16_DS:
	case R_PPC64_GOT_TPREL16_LO_DS:
	case R_PPC64_GOT_TPREL16_HI:
	case R_PPC64_GOT_TPREL16_HA:
	  tls_type = TLS_TLS | TLS_TPREL;
	  goto dogot;

	case R_PPC64_GOT_DTPREL16_DS:
	case R_PPC64_GOT_DTPREL16_LO_DS:
	case R_PPC64_GOT_DTPREL16_HI:
	case R_PPC64_GOT_DTPREL16_HA:
	  tls_type = TLS_TLS | TLS_DTPREL;
	  goto dogot;

	case R_PPC64_GOT16:
	case R_PPC64_GOT16_DS:
	case R_PPC64_GOT16_HA:
	case R_PPC64_GOT16_HI:
	case R_PPC64_GOT16_LO:
	case R_PPC64_GOT16_LO_DS:
	dogot:
	  {
	    struct got_entry *ent;

	    if (h != NULL)
	      ent = h->got.glist;
	    else
	      ent = local_got_ents[r_symndx];

	    for (; ent != NULL; ent = ent->next)
	      if (ent->addend == rel->r_addend
		  && ent->owner == abfd
		  && ent->tls_type == tls_type)
		break;
	    if (ent == NULL)
	      abort ();
	    if (ent->got.refcount > 0)
	      ent->got.refcount -= 1;
	  }
	  break;

	case R_PPC64_PLT16_HA:
	case R_PPC64_PLT16_HI:
	case R_PPC64_PLT16_LO:
	case R_PPC64_PLT32:
	case R_PPC64_PLT64:
	case R_PPC64_REL14:
	case R_PPC64_REL14_BRNTAKEN:
	case R_PPC64_REL14_BRTAKEN:
	case R_PPC64_REL24:
	  if (h != NULL)
	    {
	      struct plt_entry *ent;

	      for (ent = h->plt.plist; ent != NULL; ent = ent->next)
		if (ent->addend == rel->r_addend)
		  break;
	      if (ent != NULL && ent->plt.refcount > 0)
		ent->plt.refcount -= 1;
	    }
	  break;

	default:
	  break;
	}
    }
  return TRUE;
}

/* The maximum size of .sfpr.  */
#define SFPR_MAX (218*4)

struct sfpr_def_parms
{
  const char name[12];
  unsigned char lo, hi;
  bfd_byte * (*write_ent) (bfd *, bfd_byte *, int);
  bfd_byte * (*write_tail) (bfd *, bfd_byte *, int);
};

/* Auto-generate _save*, _rest* functions in .sfpr.  */

static unsigned int
sfpr_define (struct bfd_link_info *info, const struct sfpr_def_parms *parm)
{
  struct ppc_link_hash_table *htab = ppc_hash_table (info);
  unsigned int i;
  size_t len = strlen (parm->name);
  bfd_boolean writing = FALSE;
  char sym[16];

  memcpy (sym, parm->name, len);
  sym[len + 2] = 0;

  for (i = parm->lo; i <= parm->hi; i++)
    {
      struct elf_link_hash_entry *h;

      sym[len + 0] = i / 10 + '0';
      sym[len + 1] = i % 10 + '0';
      h = elf_link_hash_lookup (&htab->elf, sym, FALSE, FALSE, TRUE);
      if (h != NULL
	  && !h->def_regular)
	{
	  h->root.type = bfd_link_hash_defined;
	  h->root.u.def.section = htab->sfpr;
	  h->root.u.def.value = htab->sfpr->size;
	  h->type = STT_FUNC;
	  h->def_regular = 1;
	  _bfd_elf_link_hash_hide_symbol (info, h, TRUE);
	  writing = TRUE;
	  if (htab->sfpr->contents == NULL)
	    {
	      htab->sfpr->contents = bfd_alloc (htab->elf.dynobj, SFPR_MAX);
	      if (htab->sfpr->contents == NULL)
		return FALSE;
	    }
	}
      if (writing)
	{
	  bfd_byte *p = htab->sfpr->contents + htab->sfpr->size;
	  if (i != parm->hi)
	    p = (*parm->write_ent) (htab->elf.dynobj, p, i);
	  else
	    p = (*parm->write_tail) (htab->elf.dynobj, p, i);
	  htab->sfpr->size = p - htab->sfpr->contents;
	}
    }

  return TRUE;
}

static bfd_byte *
savegpr0 (bfd *abfd, bfd_byte *p, int r)
{
  bfd_put_32 (abfd, STD_R0_0R1 + (r << 21) + (1 << 16) - (32 - r) * 8, p);
  return p + 4;
}

static bfd_byte *
savegpr0_tail (bfd *abfd, bfd_byte *p, int r)
{
  p = savegpr0 (abfd, p, r);
  bfd_put_32 (abfd, STD_R0_0R1 + 16, p);
  p = p + 4;
  bfd_put_32 (abfd, BLR, p);
  return p + 4;
}

static bfd_byte *
restgpr0 (bfd *abfd, bfd_byte *p, int r)
{
  bfd_put_32 (abfd, LD_R0_0R1 + (r << 21) + (1 << 16) - (32 - r) * 8, p);
  return p + 4;
}

static bfd_byte *
restgpr0_tail (bfd *abfd, bfd_byte *p, int r)
{
  bfd_put_32 (abfd, LD_R0_0R1 + 16, p);
  p = p + 4;
  p = restgpr0 (abfd, p, r);
  bfd_put_32 (abfd, MTLR_R0, p);
  p = p + 4;
  if (r == 29)
    {
      p = restgpr0 (abfd, p, 30);
      p = restgpr0 (abfd, p, 31);
    }
  bfd_put_32 (abfd, BLR, p);
  return p + 4;
}

static bfd_byte *
savegpr1 (bfd *abfd, bfd_byte *p, int r)
{
  bfd_put_32 (abfd, STD_R0_0R12 + (r << 21) + (1 << 16) - (32 - r) * 8, p);
  return p + 4;
}

static bfd_byte *
savegpr1_tail (bfd *abfd, bfd_byte *p, int r)
{
  p = savegpr1 (abfd, p, r);
  bfd_put_32 (abfd, BLR, p);
  return p + 4;
}

static bfd_byte *
restgpr1 (bfd *abfd, bfd_byte *p, int r)
{
  bfd_put_32 (abfd, LD_R0_0R12 + (r << 21) + (1 << 16) - (32 - r) * 8, p);
  return p + 4;
}

static bfd_byte *
restgpr1_tail (bfd *abfd, bfd_byte *p, int r)
{
  p = restgpr1 (abfd, p, r);
  bfd_put_32 (abfd, BLR, p);
  return p + 4;
}

static bfd_byte *
savefpr (bfd *abfd, bfd_byte *p, int r)
{
  bfd_put_32 (abfd, STFD_FR0_0R1 + (r << 21) + (1 << 16) - (32 - r) * 8, p);
  return p + 4;
}

static bfd_byte *
savefpr0_tail (bfd *abfd, bfd_byte *p, int r)
{
  p = savefpr (abfd, p, r);
  bfd_put_32 (abfd, STD_R0_0R1 + 16, p);
  p = p + 4;
  bfd_put_32 (abfd, BLR, p);
  return p + 4;
}

static bfd_byte *
restfpr (bfd *abfd, bfd_byte *p, int r)
{
  bfd_put_32 (abfd, LFD_FR0_0R1 + (r << 21) + (1 << 16) - (32 - r) * 8, p);
  return p + 4;
}

static bfd_byte *
restfpr0_tail (bfd *abfd, bfd_byte *p, int r)
{
  bfd_put_32 (abfd, LD_R0_0R1 + 16, p);
  p = p + 4;
  p = restfpr (abfd, p, r);
  bfd_put_32 (abfd, MTLR_R0, p);
  p = p + 4;
  if (r == 29)
    {
      p = restfpr (abfd, p, 30);
      p = restfpr (abfd, p, 31);
    }
  bfd_put_32 (abfd, BLR, p);
  return p + 4;
}

static bfd_byte *
savefpr1_tail (bfd *abfd, bfd_byte *p, int r)
{
  p = savefpr (abfd, p, r);
  bfd_put_32 (abfd, BLR, p);
  return p + 4;
}

static bfd_byte *
restfpr1_tail (bfd *abfd, bfd_byte *p, int r)
{
  p = restfpr (abfd, p, r);
  bfd_put_32 (abfd, BLR, p);
  return p + 4;
}

static bfd_byte *
savevr (bfd *abfd, bfd_byte *p, int r)
{
  bfd_put_32 (abfd, LI_R12_0 + (1 << 16) - (32 - r) * 16, p);
  p = p + 4;
  bfd_put_32 (abfd, STVX_VR0_R12_R0 + (r << 21), p);
  return p + 4;
}

static bfd_byte *
savevr_tail (bfd *abfd, bfd_byte *p, int r)
{
  p = savevr (abfd, p, r);
  bfd_put_32 (abfd, BLR, p);
  return p + 4;
}

static bfd_byte *
restvr (bfd *abfd, bfd_byte *p, int r)
{
  bfd_put_32 (abfd, LI_R12_0 + (1 << 16) - (32 - r) * 16, p);
  p = p + 4;
  bfd_put_32 (abfd, LVX_VR0_R12_R0 + (r << 21), p);
  return p + 4;
}

static bfd_byte *
restvr_tail (bfd *abfd, bfd_byte *p, int r)
{
  p = restvr (abfd, p, r);
  bfd_put_32 (abfd, BLR, p);
  return p + 4;
}

/* Called via elf_link_hash_traverse to transfer dynamic linking
   information on function code symbol entries to their corresponding
   function descriptor symbol entries.  */

static bfd_boolean
func_desc_adjust (struct elf_link_hash_entry *h, void *inf)
{
  struct bfd_link_info *info;
  struct ppc_link_hash_table *htab;
  struct plt_entry *ent;
  struct ppc_link_hash_entry *fh;
  struct ppc_link_hash_entry *fdh;
  bfd_boolean force_local;

  fh = (struct ppc_link_hash_entry *) h;
  if (fh->elf.root.type == bfd_link_hash_indirect)
    return TRUE;

  if (fh->elf.root.type == bfd_link_hash_warning)
    fh = (struct ppc_link_hash_entry *) fh->elf.root.u.i.link;

  info = inf;
  htab = ppc_hash_table (info);

  /* Resolve undefined references to dot-symbols as the value
     in the function descriptor, if we have one in a regular object.
     This is to satisfy cases like ".quad .foo".  Calls to functions
     in dynamic objects are handled elsewhere.  */
  if (fh->elf.root.type == bfd_link_hash_undefweak
      && fh->was_undefined
      && (fh->oh->elf.root.type == bfd_link_hash_defined
	  || fh->oh->elf.root.type == bfd_link_hash_defweak)
      && get_opd_info (fh->oh->elf.root.u.def.section) != NULL
      && opd_entry_value (fh->oh->elf.root.u.def.section,
			  fh->oh->elf.root.u.def.value,
			  &fh->elf.root.u.def.section,
			  &fh->elf.root.u.def.value) != (bfd_vma) -1)
    {
      fh->elf.root.type = fh->oh->elf.root.type;
      fh->elf.forced_local = 1;
      fh->elf.def_regular = fh->oh->elf.def_regular;
      fh->elf.def_dynamic = fh->oh->elf.def_dynamic;
    }

  /* If this is a function code symbol, transfer dynamic linking
     information to the function descriptor symbol.  */
  if (!fh->is_func)
    return TRUE;

  for (ent = fh->elf.plt.plist; ent != NULL; ent = ent->next)
    if (ent->plt.refcount > 0)
      break;
  if (ent == NULL
      || fh->elf.root.root.string[0] != '.'
      || fh->elf.root.root.string[1] == '\0')
    return TRUE;

  /* Find the corresponding function descriptor symbol.  Create it
     as undefined if necessary.  */

  fdh = get_fdh (fh, htab);
  if (fdh != NULL)
    while (fdh->elf.root.type == bfd_link_hash_indirect
	   || fdh->elf.root.type == bfd_link_hash_warning)
      fdh = (struct ppc_link_hash_entry *) fdh->elf.root.u.i.link;

  if (fdh == NULL
      && info->shared
      && (fh->elf.root.type == bfd_link_hash_undefined
	  || fh->elf.root.type == bfd_link_hash_undefweak))
    {
      fdh = make_fdh (info, fh);
      if (fdh == NULL)
	return FALSE;
    }

  /* Fake function descriptors are made undefweak.  If the function
     code symbol is strong undefined, make the fake sym the same.
     If the function code symbol is defined, then force the fake
     descriptor local;  We can't support overriding of symbols in a
     shared library on a fake descriptor.  */

  if (fdh != NULL
      && fdh->fake
      && fdh->elf.root.type == bfd_link_hash_undefweak)
    {
      if (fh->elf.root.type == bfd_link_hash_undefined)
	{
	  fdh->elf.root.type = bfd_link_hash_undefined;
	  bfd_link_add_undef (&htab->elf.root, &fdh->elf.root);
	}
      else if (fh->elf.root.type == bfd_link_hash_defined
	       || fh->elf.root.type == bfd_link_hash_defweak)
	{
	  _bfd_elf_link_hash_hide_symbol (info, &fdh->elf, TRUE);
	}
    }

  if (fdh != NULL
      && !fdh->elf.forced_local
      && (info->shared
	  || fdh->elf.def_dynamic
	  || fdh->elf.ref_dynamic
	  || (fdh->elf.root.type == bfd_link_hash_undefweak
	      && ELF_ST_VISIBILITY (fdh->elf.other) == STV_DEFAULT)))
    {
      if (fdh->elf.dynindx == -1)
	if (! bfd_elf_link_record_dynamic_symbol (info, &fdh->elf))
	  return FALSE;
      fdh->elf.ref_regular |= fh->elf.ref_regular;
      fdh->elf.ref_dynamic |= fh->elf.ref_dynamic;
      fdh->elf.ref_regular_nonweak |= fh->elf.ref_regular_nonweak;
      fdh->elf.non_got_ref |= fh->elf.non_got_ref;
      if (ELF_ST_VISIBILITY (fh->elf.other) == STV_DEFAULT)
	{
	  move_plt_plist (fh, fdh);
	  fdh->elf.needs_plt = 1;
	}
      fdh->is_func_descriptor = 1;
      fdh->oh = fh;
      fh->oh = fdh;
    }

  /* Now that the info is on the function descriptor, clear the
     function code sym info.  Any function code syms for which we
     don't have a definition in a regular file, we force local.
     This prevents a shared library from exporting syms that have
     been imported from another library.  Function code syms that
     are really in the library we must leave global to prevent the
     linker dragging in a definition from a static library.  */
  force_local = (!fh->elf.def_regular
		 || fdh == NULL
		 || !fdh->elf.def_regular
		 || fdh->elf.forced_local);
  _bfd_elf_link_hash_hide_symbol (info, &fh->elf, force_local);

  return TRUE;
}

/* Called near the start of bfd_elf_size_dynamic_sections.  We use
   this hook to a) provide some gcc support functions, and b) transfer
   dynamic linking information gathered so far on function code symbol
   entries, to their corresponding function descriptor symbol entries.  */

static bfd_boolean
ppc64_elf_func_desc_adjust (bfd *obfd ATTRIBUTE_UNUSED,
			    struct bfd_link_info *info)
{
  struct ppc_link_hash_table *htab;
  unsigned int i;
  const struct sfpr_def_parms funcs[] =
    {
      { "_savegpr0_", 14, 31, savegpr0, savegpr0_tail },
      { "_restgpr0_", 14, 29, restgpr0, restgpr0_tail },
      { "_restgpr0_", 30, 31, restgpr0, restgpr0_tail },
      { "_savegpr1_", 14, 31, savegpr1, savegpr1_tail },
      { "_restgpr1_", 14, 31, restgpr1, restgpr1_tail },
      { "_savefpr_", 14, 31, savefpr, savefpr0_tail },
      { "_restfpr_", 14, 29, restfpr, restfpr0_tail },
      { "_restfpr_", 30, 31, restfpr, restfpr0_tail },
      { "._savef", 14, 31, savefpr, savefpr1_tail },
      { "._restf", 14, 31, restfpr, restfpr1_tail },
      { "_savevr_", 20, 31, savevr, savevr_tail },
      { "_restvr_", 20, 31, restvr, restvr_tail }
    };

  htab = ppc_hash_table (info);
  if (htab->sfpr == NULL)
    /* We don't have any relocs.  */
    return TRUE;

  /* Provide any missing _save* and _rest* functions.  */
  htab->sfpr->size = 0;
  for (i = 0; i < sizeof (funcs) / sizeof (funcs[0]); i++)
    if (!sfpr_define (info, &funcs[i]))
      return FALSE;

  elf_link_hash_traverse (&htab->elf, func_desc_adjust, info);

  if (htab->sfpr->size == 0)
    htab->sfpr->flags |= SEC_EXCLUDE;

  return TRUE;
}

/* Adjust a symbol defined by a dynamic object and referenced by a
   regular object.  The current definition is in some section of the
   dynamic object, but we're not including those sections.  We have to
   change the definition to something the rest of the link can
   understand.  */

static bfd_boolean
ppc64_elf_adjust_dynamic_symbol (struct bfd_link_info *info,
				 struct elf_link_hash_entry *h)
{
  struct ppc_link_hash_table *htab;
  asection *s;

  htab = ppc_hash_table (info);

  /* Deal with function syms.  */
  if (h->type == STT_FUNC
      || h->needs_plt)
    {
      /* Clear procedure linkage table information for any symbol that
	 won't need a .plt entry.  */
      struct plt_entry *ent;
      for (ent = h->plt.plist; ent != NULL; ent = ent->next)
	if (ent->plt.refcount > 0)
	  break;
      if (ent == NULL
	  || SYMBOL_CALLS_LOCAL (info, h)
	  || (ELF_ST_VISIBILITY (h->other) != STV_DEFAULT
	      && h->root.type == bfd_link_hash_undefweak))
	{
	  h->plt.plist = NULL;
	  h->needs_plt = 0;
	}
    }
  else
    h->plt.plist = NULL;

  /* If this is a weak symbol, and there is a real definition, the
     processor independent code will have arranged for us to see the
     real definition first, and we can just use the same value.  */
  if (h->u.weakdef != NULL)
    {
      BFD_ASSERT (h->u.weakdef->root.type == bfd_link_hash_defined
		  || h->u.weakdef->root.type == bfd_link_hash_defweak);
      h->root.u.def.section = h->u.weakdef->root.u.def.section;
      h->root.u.def.value = h->u.weakdef->root.u.def.value;
      if (ELIMINATE_COPY_RELOCS)
	h->non_got_ref = h->u.weakdef->non_got_ref;
      return TRUE;
    }

  /* If we are creating a shared library, we must presume that the
     only references to the symbol are via the global offset table.
     For such cases we need not do anything here; the relocations will
     be handled correctly by relocate_section.  */
  if (info->shared)
    return TRUE;

  /* If there are no references to this symbol that do not use the
     GOT, we don't need to generate a copy reloc.  */
  if (!h->non_got_ref)
    return TRUE;

  /* Don't generate a copy reloc for symbols defined in the executable.  */
  if (!h->def_dynamic || !h->ref_regular || h->def_regular)
    return TRUE;

  if (ELIMINATE_COPY_RELOCS)
    {
      struct ppc_link_hash_entry * eh;
      struct ppc_dyn_relocs *p;

      eh = (struct ppc_link_hash_entry *) h;
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

  if (h->plt.plist != NULL)
    {
      /* We should never get here, but unfortunately there are versions
	 of gcc out there that improperly (for this ABI) put initialized
	 function pointers, vtable refs and suchlike in read-only
	 sections.  Allow them to proceed, but warn that this might
	 break at runtime.  */
      (*_bfd_error_handler)
	(_("copy reloc against `%s' requires lazy plt linking; "
	   "avoid setting LD_BIND_NOW=1 or upgrade gcc"),
	 h->root.root.string);
    }

  /* This is a reference to a symbol defined by a dynamic object which
     is not a function.  */

  if (h->size == 0)
    {
      (*_bfd_error_handler) (_("dynamic variable `%s' is zero size"),
			     h->root.root.string);
      return TRUE;
    }

  /* We must allocate the symbol in our .dynbss section, which will
     become part of the .bss section of the executable.  There will be
     an entry for this symbol in the .dynsym section.  The dynamic
     object will contain position independent code, so all references
     from the dynamic object to this symbol will go through the global
     offset table.  The dynamic linker will use the .dynsym entry to
     determine the address it must put in the global offset table, so
     both the dynamic object and the regular object will refer to the
     same memory location for the variable.  */

  /* We must generate a R_PPC64_COPY reloc to tell the dynamic linker
     to copy the initial value out of the dynamic object and into the
     runtime process image.  We need to remember the offset into the
     .rela.bss section we are going to use.  */
  if ((h->root.u.def.section->flags & SEC_ALLOC) != 0)
    {
      htab->relbss->size += sizeof (Elf64_External_Rela);
      h->needs_copy = 1;
    }

  s = htab->dynbss;

  return _bfd_elf_adjust_dynamic_copy (h, s);
}

/* If given a function descriptor symbol, hide both the function code
   sym and the descriptor.  */
static void
ppc64_elf_hide_symbol (struct bfd_link_info *info,
		       struct elf_link_hash_entry *h,
		       bfd_boolean force_local)
{
  struct ppc_link_hash_entry *eh;
  _bfd_elf_link_hash_hide_symbol (info, h, force_local);

  eh = (struct ppc_link_hash_entry *) h;
  if (eh->is_func_descriptor)
    {
      struct ppc_link_hash_entry *fh = eh->oh;

      if (fh == NULL)
	{
	  const char *p, *q;
	  struct ppc_link_hash_table *htab;
	  char save;

	  /* We aren't supposed to use alloca in BFD because on
	     systems which do not have alloca the version in libiberty
	     calls xmalloc, which might cause the program to crash
	     when it runs out of memory.  This function doesn't have a
	     return status, so there's no way to gracefully return an
	     error.  So cheat.  We know that string[-1] can be safely
	     accessed;  It's either a string in an ELF string table,
	     or allocated in an objalloc structure.  */

	  p = eh->elf.root.root.string - 1;
	  save = *p;
	  *(char *) p = '.';
	  htab = ppc_hash_table (info);
	  fh = (struct ppc_link_hash_entry *)
	    elf_link_hash_lookup (&htab->elf, p, FALSE, FALSE, FALSE);
	  *(char *) p = save;

	  /* Unfortunately, if it so happens that the string we were
	     looking for was allocated immediately before this string,
	     then we overwrote the string terminator.  That's the only
	     reason the lookup should fail.  */
	  if (fh == NULL)
	    {
	      q = eh->elf.root.root.string + strlen (eh->elf.root.root.string);
	      while (q >= eh->elf.root.root.string && *q == *p)
		--q, --p;
	      if (q < eh->elf.root.root.string && *p == '.')
		fh = (struct ppc_link_hash_entry *)
		  elf_link_hash_lookup (&htab->elf, p, FALSE, FALSE, FALSE);
	    }
	  if (fh != NULL)
	    {
	      eh->oh = fh;
	      fh->oh = eh;
	    }
	}
      if (fh != NULL)
	_bfd_elf_link_hash_hide_symbol (info, &fh->elf, force_local);
    }
}

static bfd_boolean
get_sym_h (struct elf_link_hash_entry **hp,
	   Elf_Internal_Sym **symp,
	   asection **symsecp,
	   char **tls_maskp,
	   Elf_Internal_Sym **locsymsp,
	   unsigned long r_symndx,
	   bfd *ibfd)
{
  Elf_Internal_Shdr *symtab_hdr = &elf_tdata (ibfd)->symtab_hdr;

  if (r_symndx >= symtab_hdr->sh_info)
    {
      struct elf_link_hash_entry **sym_hashes = elf_sym_hashes (ibfd);
      struct elf_link_hash_entry *h;

      h = sym_hashes[r_symndx - symtab_hdr->sh_info];
      while (h->root.type == bfd_link_hash_indirect
	     || h->root.type == bfd_link_hash_warning)
	h = (struct elf_link_hash_entry *) h->root.u.i.link;

      if (hp != NULL)
	*hp = h;

      if (symp != NULL)
	*symp = NULL;

      if (symsecp != NULL)
	{
	  asection *symsec = NULL;
	  if (h->root.type == bfd_link_hash_defined
	      || h->root.type == bfd_link_hash_defweak)
	    symsec = h->root.u.def.section;
	  *symsecp = symsec;
	}

      if (tls_maskp != NULL)
	{
	  struct ppc_link_hash_entry *eh;

	  eh = (struct ppc_link_hash_entry *) h;
	  *tls_maskp = &eh->tls_mask;
	}
    }
  else
    {
      Elf_Internal_Sym *sym;
      Elf_Internal_Sym *locsyms = *locsymsp;

      if (locsyms == NULL)
	{
	  locsyms = (Elf_Internal_Sym *) symtab_hdr->contents;
	  if (locsyms == NULL)
	    locsyms = bfd_elf_get_elf_syms (ibfd, symtab_hdr,
					    symtab_hdr->sh_info,
					    0, NULL, NULL, NULL);
	  if (locsyms == NULL)
	    return FALSE;
	  *locsymsp = locsyms;
	}
      sym = locsyms + r_symndx;

      if (hp != NULL)
	*hp = NULL;

      if (symp != NULL)
	*symp = sym;

      if (symsecp != NULL)
	{
	  asection *symsec = NULL;
	  if ((sym->st_shndx != SHN_UNDEF
	       && sym->st_shndx < SHN_LORESERVE)
	      || sym->st_shndx > SHN_HIRESERVE)
	    symsec = bfd_section_from_elf_index (ibfd, sym->st_shndx);
	  *symsecp = symsec;
	}

      if (tls_maskp != NULL)
	{
	  struct got_entry **lgot_ents;
	  char *tls_mask;

	  tls_mask = NULL;
	  lgot_ents = elf_local_got_ents (ibfd);
	  if (lgot_ents != NULL)
	    {
	      char *lgot_masks = (char *) (lgot_ents + symtab_hdr->sh_info);
	      tls_mask = &lgot_masks[r_symndx];
	    }
	  *tls_maskp = tls_mask;
	}
    }
  return TRUE;
}

/* Returns TLS_MASKP for the given REL symbol.  Function return is 0 on
   error, 2 on a toc GD type suitable for optimization, 3 on a toc LD
   type suitable for optimization, and 1 otherwise.  */

static int
get_tls_mask (char **tls_maskp,
	      unsigned long *toc_symndx,
	      bfd_vma *toc_addend,
	      Elf_Internal_Sym **locsymsp,
	      const Elf_Internal_Rela *rel,
	      bfd *ibfd)
{
  unsigned long r_symndx;
  int next_r;
  struct elf_link_hash_entry *h;
  Elf_Internal_Sym *sym;
  asection *sec;
  bfd_vma off;

  r_symndx = ELF64_R_SYM (rel->r_info);
  if (!get_sym_h (&h, &sym, &sec, tls_maskp, locsymsp, r_symndx, ibfd))
    return 0;

  if ((*tls_maskp != NULL && **tls_maskp != 0)
      || sec == NULL
      || ppc64_elf_section_data (sec)->sec_type != sec_toc)
    return 1;

  /* Look inside a TOC section too.  */
  if (h != NULL)
    {
      BFD_ASSERT (h->root.type == bfd_link_hash_defined);
      off = h->root.u.def.value;
    }
  else
    off = sym->st_value;
  off += rel->r_addend;
  BFD_ASSERT (off % 8 == 0);
  r_symndx = ppc64_elf_section_data (sec)->u.toc.symndx[off / 8];
  next_r = ppc64_elf_section_data (sec)->u.toc.symndx[off / 8 + 1];
  if (toc_symndx != NULL)
    *toc_symndx = r_symndx;
  if (toc_addend != NULL)
    *toc_addend = ppc64_elf_section_data (sec)->u.toc.add[off / 8];
  if (!get_sym_h (&h, &sym, &sec, tls_maskp, locsymsp, r_symndx, ibfd))
    return 0;
  if ((h == NULL
       || ((h->root.type == bfd_link_hash_defined
	    || h->root.type == bfd_link_hash_defweak)
	   && !h->def_dynamic))
      && (next_r == -1 || next_r == -2))
    return 1 - next_r;
  return 1;
}

/* Adjust all global syms defined in opd sections.  In gcc generated
   code for the old ABI, these will already have been done.  */

static bfd_boolean
adjust_opd_syms (struct elf_link_hash_entry *h, void *inf ATTRIBUTE_UNUSED)
{
  struct ppc_link_hash_entry *eh;
  asection *sym_sec;
  long *opd_adjust;

  if (h->root.type == bfd_link_hash_indirect)
    return TRUE;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  if (h->root.type != bfd_link_hash_defined
      && h->root.type != bfd_link_hash_defweak)
    return TRUE;

  eh = (struct ppc_link_hash_entry *) h;
  if (eh->adjust_done)
    return TRUE;

  sym_sec = eh->elf.root.u.def.section;
  opd_adjust = get_opd_info (sym_sec);
  if (opd_adjust != NULL)
    {
      long adjust = opd_adjust[eh->elf.root.u.def.value / 8];
      if (adjust == -1)
	{
	  /* This entry has been deleted.  */
	  asection *dsec = ppc64_elf_tdata (sym_sec->owner)->deleted_section;
	  if (dsec == NULL)
	    {
	      for (dsec = sym_sec->owner->sections; dsec; dsec = dsec->next)
		if (elf_discarded_section (dsec))
		  {
		    ppc64_elf_tdata (sym_sec->owner)->deleted_section = dsec;
		    break;
		  }
	    }
	  eh->elf.root.u.def.value = 0;
	  eh->elf.root.u.def.section = dsec;
	}
      else
	eh->elf.root.u.def.value += adjust;
      eh->adjust_done = 1;
    }
  return TRUE;
}

/* Handles decrementing dynamic reloc counts for the reloc specified by
   R_INFO in section SEC.  If LOCAL_SYMS is NULL, then H and SYM_SEC
   have already been determined.  */

static bfd_boolean
dec_dynrel_count (bfd_vma r_info,
		  asection *sec,
		  struct bfd_link_info *info,
		  Elf_Internal_Sym **local_syms,
		  struct elf_link_hash_entry *h,
		  asection *sym_sec)
{
  enum elf_ppc64_reloc_type r_type;
  struct ppc_dyn_relocs *p;
  struct ppc_dyn_relocs **pp;

  /* Can this reloc be dynamic?  This switch, and later tests here
     should be kept in sync with the code in check_relocs.  */
  r_type = ELF64_R_TYPE (r_info);
  switch (r_type)
    {
    default:
      return TRUE;

    case R_PPC64_TPREL16:
    case R_PPC64_TPREL16_LO:
    case R_PPC64_TPREL16_HI:
    case R_PPC64_TPREL16_HA:
    case R_PPC64_TPREL16_DS:
    case R_PPC64_TPREL16_LO_DS:
    case R_PPC64_TPREL16_HIGHER:
    case R_PPC64_TPREL16_HIGHERA:
    case R_PPC64_TPREL16_HIGHEST:
    case R_PPC64_TPREL16_HIGHESTA:
      if (!info->shared)
	return TRUE;

    case R_PPC64_TPREL64:
    case R_PPC64_DTPMOD64:
    case R_PPC64_DTPREL64:
    case R_PPC64_ADDR64:
    case R_PPC64_REL30:
    case R_PPC64_REL32:
    case R_PPC64_REL64:
    case R_PPC64_ADDR14:
    case R_PPC64_ADDR14_BRNTAKEN:
    case R_PPC64_ADDR14_BRTAKEN:
    case R_PPC64_ADDR16:
    case R_PPC64_ADDR16_DS:
    case R_PPC64_ADDR16_HA:
    case R_PPC64_ADDR16_HI:
    case R_PPC64_ADDR16_HIGHER:
    case R_PPC64_ADDR16_HIGHERA:
    case R_PPC64_ADDR16_HIGHEST:
    case R_PPC64_ADDR16_HIGHESTA:
    case R_PPC64_ADDR16_LO:
    case R_PPC64_ADDR16_LO_DS:
    case R_PPC64_ADDR24:
    case R_PPC64_ADDR32:
    case R_PPC64_UADDR16:
    case R_PPC64_UADDR32:
    case R_PPC64_UADDR64:
    case R_PPC64_TOC:
      break;
    }

  if (local_syms != NULL)
    {
      unsigned long r_symndx;
      Elf_Internal_Sym *sym;
      bfd *ibfd = sec->owner;

      r_symndx = ELF64_R_SYM (r_info);
      if (!get_sym_h (&h, &sym, &sym_sec, NULL, local_syms, r_symndx, ibfd))
	return FALSE;
    }

  if ((info->shared
       && (must_be_dyn_reloc (info, r_type)
	   || (h != NULL
	       && (!info->symbolic
		   || h->root.type == bfd_link_hash_defweak
		   || !h->def_regular))))
      || (ELIMINATE_COPY_RELOCS
	  && !info->shared
	  && h != NULL
	  && (h->root.type == bfd_link_hash_defweak
	      || !h->def_regular)))
    ;
  else
    return TRUE;

  if (h != NULL)
    pp = &((struct ppc_link_hash_entry *) h)->dyn_relocs;
  else
    {
      if (sym_sec != NULL)
	{
	  void *vpp = &elf_section_data (sym_sec)->local_dynrel;
	  pp = (struct ppc_dyn_relocs **) vpp;
	}
      else
	{
	  void *vpp = &elf_section_data (sec)->local_dynrel;
	  pp = (struct ppc_dyn_relocs **) vpp;
	}

      /* elf_gc_sweep may have already removed all dyn relocs associated
	 with local syms for a given section.  Don't report a dynreloc
	 miscount.  */
      if (*pp == NULL)
	return TRUE;
    }

  while ((p = *pp) != NULL)
    {
      if (p->sec == sec)
	{
	  if (!must_be_dyn_reloc (info, r_type))
	    p->pc_count -= 1;
	  p->count -= 1;
	  if (p->count == 0)
	    *pp = p->next;
	  return TRUE;
	}
      pp = &p->next;
    }

  (*_bfd_error_handler) (_("dynreloc miscount for %B, section %A"),
			   sec->owner, sec);
  bfd_set_error (bfd_error_bad_value);
  return FALSE;
}

/* Remove unused Official Procedure Descriptor entries.  Currently we
   only remove those associated with functions in discarded link-once
   sections, or weakly defined functions that have been overridden.  It
   would be possible to remove many more entries for statically linked
   applications.  */

bfd_boolean
ppc64_elf_edit_opd (bfd *obfd, struct bfd_link_info *info,
		    bfd_boolean no_opd_opt,
		    bfd_boolean non_overlapping)
{
  bfd *ibfd;
  bfd_boolean some_edited = FALSE;
  asection *need_pad = NULL;

  for (ibfd = info->input_bfds; ibfd != NULL; ibfd = ibfd->link_next)
    {
      asection *sec;
      Elf_Internal_Rela *relstart, *rel, *relend;
      Elf_Internal_Shdr *symtab_hdr;
      Elf_Internal_Sym *local_syms;
      struct elf_link_hash_entry **sym_hashes;
      bfd_vma offset;
      bfd_size_type amt;
      long *opd_adjust;
      bfd_boolean need_edit, add_aux_fields;
      bfd_size_type cnt_16b = 0;

      sec = bfd_get_section_by_name (ibfd, ".opd");
      if (sec == NULL || sec->size == 0)
	continue;

      amt = sec->size * sizeof (long) / 8;
      opd_adjust = get_opd_info (sec);
      if (opd_adjust == NULL)
	{
	  /* check_relocs hasn't been called.  Must be a ld -r link
	     or --just-symbols object.   */
	  opd_adjust = bfd_alloc (obfd, amt);
	  if (opd_adjust == NULL)
	    return FALSE;
	  ppc64_elf_section_data (sec)->u.opd_adjust = opd_adjust;
	  BFD_ASSERT (ppc64_elf_section_data (sec)->sec_type == sec_normal);
	  ppc64_elf_section_data (sec)->sec_type = sec_opd;
	}
      memset (opd_adjust, 0, amt);

      if (no_opd_opt)
	continue;

      if (sec->sec_info_type == ELF_INFO_TYPE_JUST_SYMS)
	continue;

      if (sec->output_section == bfd_abs_section_ptr)
	continue;

      /* Look through the section relocs.  */
      if ((sec->flags & SEC_RELOC) == 0 || sec->reloc_count == 0)
	continue;

      local_syms = NULL;
      symtab_hdr = &elf_tdata (ibfd)->symtab_hdr;
      sym_hashes = elf_sym_hashes (ibfd);

      /* Read the relocations.  */
      relstart = _bfd_elf_link_read_relocs (ibfd, sec, NULL, NULL,
					    info->keep_memory);
      if (relstart == NULL)
	return FALSE;

      /* First run through the relocs to check they are sane, and to
	 determine whether we need to edit this opd section.  */
      need_edit = FALSE;
      need_pad = sec;
      offset = 0;
      relend = relstart + sec->reloc_count;
      for (rel = relstart; rel < relend; )
	{
	  enum elf_ppc64_reloc_type r_type;
	  unsigned long r_symndx;
	  asection *sym_sec;
	  struct elf_link_hash_entry *h;
	  Elf_Internal_Sym *sym;

	  /* .opd contains a regular array of 16 or 24 byte entries.  We're
	     only interested in the reloc pointing to a function entry
	     point.  */
	  if (rel->r_offset != offset
	      || rel + 1 >= relend
	      || (rel + 1)->r_offset != offset + 8)
	    {
	      /* If someone messes with .opd alignment then after a
		 "ld -r" we might have padding in the middle of .opd.
		 Also, there's nothing to prevent someone putting
		 something silly in .opd with the assembler.  No .opd
		 optimization for them!  */
	    broken_opd:
	      (*_bfd_error_handler)
		(_("%B: .opd is not a regular array of opd entries"), ibfd);
	      need_edit = FALSE;
	      break;
	    }

	  if ((r_type = ELF64_R_TYPE (rel->r_info)) != R_PPC64_ADDR64
	      || (r_type = ELF64_R_TYPE ((rel + 1)->r_info)) != R_PPC64_TOC)
	    {
	      (*_bfd_error_handler)
		(_("%B: unexpected reloc type %u in .opd section"),
		 ibfd, r_type);
	      need_edit = FALSE;
	      break;
	    }

	  r_symndx = ELF64_R_SYM (rel->r_info);
	  if (!get_sym_h (&h, &sym, &sym_sec, NULL, &local_syms,
			  r_symndx, ibfd))
	    goto error_ret;

	  if (sym_sec == NULL || sym_sec->owner == NULL)
	    {
	      const char *sym_name;
	      if (h != NULL)
		sym_name = h->root.root.string;
	      else
		sym_name = bfd_elf_sym_name (ibfd, symtab_hdr, sym,
					     sym_sec);

	      (*_bfd_error_handler)
		(_("%B: undefined sym `%s' in .opd section"),
		 ibfd, sym_name);
	      need_edit = FALSE;
	      break;
	    }

	  /* opd entries are always for functions defined in the
	     current input bfd.  If the symbol isn't defined in the
	     input bfd, then we won't be using the function in this
	     bfd;  It must be defined in a linkonce section in another
	     bfd, or is weak.  It's also possible that we are
	     discarding the function due to a linker script /DISCARD/,
	     which we test for via the output_section.  */
	  if (sym_sec->owner != ibfd
	      || sym_sec->output_section == bfd_abs_section_ptr)
	    need_edit = TRUE;

	  rel += 2;
	  if (rel == relend
	      || (rel + 1 == relend && rel->r_offset == offset + 16))
	    {
	      if (sec->size == offset + 24)
		{
		  need_pad = NULL;
		  break;
		}
	      if (rel == relend && sec->size == offset + 16)
		{
		  cnt_16b++;
		  break;
		}
	      goto broken_opd;
	    }

	  if (rel->r_offset == offset + 24)
	    offset += 24;
	  else if (rel->r_offset != offset + 16)
	    goto broken_opd;
	  else if (rel + 1 < relend
		   && ELF64_R_TYPE (rel[0].r_info) == R_PPC64_ADDR64
		   && ELF64_R_TYPE (rel[1].r_info) == R_PPC64_TOC)
	    {
	      offset += 16;
	      cnt_16b++;
	    }
	  else if (rel + 2 < relend
		   && ELF64_R_TYPE (rel[1].r_info) == R_PPC64_ADDR64
		   && ELF64_R_TYPE (rel[2].r_info) == R_PPC64_TOC)
	    {
	      offset += 24;
	      rel += 1;
	    }
	  else
	    goto broken_opd;
	}

      add_aux_fields = non_overlapping && cnt_16b > 0;

      if (need_edit || add_aux_fields)
	{
	  Elf_Internal_Rela *write_rel;
	  bfd_byte *rptr, *wptr;
	  bfd_byte *new_contents = NULL;
	  bfd_boolean skip;
	  long opd_ent_size;

	  /* This seems a waste of time as input .opd sections are all
	     zeros as generated by gcc, but I suppose there's no reason
	     this will always be so.  We might start putting something in
	     the third word of .opd entries.  */
	  if ((sec->flags & SEC_IN_MEMORY) == 0)
	    {
	      bfd_byte *loc;
	      if (!bfd_malloc_and_get_section (ibfd, sec, &loc))
		{
		  if (loc != NULL)
		    free (loc);
		error_ret:
		  if (local_syms != NULL
		      && symtab_hdr->contents != (unsigned char *) local_syms)
		    free (local_syms);
		  if (elf_section_data (sec)->relocs != relstart)
		    free (relstart);
		  return FALSE;
		}
	      sec->contents = loc;
	      sec->flags |= (SEC_IN_MEMORY | SEC_HAS_CONTENTS);
	    }

	  elf_section_data (sec)->relocs = relstart;

	  new_contents = sec->contents;
	  if (add_aux_fields)
	    {
	      new_contents = bfd_malloc (sec->size + cnt_16b * 8);
	      if (new_contents == NULL)
		return FALSE;
	      need_pad = FALSE;
	    }
	  wptr = new_contents;
	  rptr = sec->contents;

	  write_rel = relstart;
	  skip = FALSE;
	  offset = 0;
	  opd_ent_size = 0;
	  for (rel = relstart; rel < relend; rel++)
	    {
	      unsigned long r_symndx;
	      asection *sym_sec;
	      struct elf_link_hash_entry *h;
	      Elf_Internal_Sym *sym;

	      r_symndx = ELF64_R_SYM (rel->r_info);
	      if (!get_sym_h (&h, &sym, &sym_sec, NULL, &local_syms,
			      r_symndx, ibfd))
		goto error_ret;

	      if (rel->r_offset == offset)
		{
		  struct ppc_link_hash_entry *fdh = NULL;

		  /* See if the .opd entry is full 24 byte or
		     16 byte (with fd_aux entry overlapped with next
		     fd_func).  */
		  opd_ent_size = 24;
		  if ((rel + 2 == relend && sec->size == offset + 16)
		      || (rel + 3 < relend
			  && rel[2].r_offset == offset + 16
			  && rel[3].r_offset == offset + 24
			  && ELF64_R_TYPE (rel[2].r_info) == R_PPC64_ADDR64
			  && ELF64_R_TYPE (rel[3].r_info) == R_PPC64_TOC))
		    opd_ent_size = 16;

		  if (h != NULL
		      && h->root.root.string[0] == '.')
		    {
		      fdh = get_fdh ((struct ppc_link_hash_entry *) h,
				     ppc_hash_table (info));
		      if (fdh != NULL
			  && fdh->elf.root.type != bfd_link_hash_defined
			  && fdh->elf.root.type != bfd_link_hash_defweak)
			fdh = NULL;
		    }

		  skip = (sym_sec->owner != ibfd
			  || sym_sec->output_section == bfd_abs_section_ptr);
		  if (skip)
		    {
		      if (fdh != NULL && sym_sec->owner == ibfd)
			{
			  /* Arrange for the function descriptor sym
			     to be dropped.  */
			  fdh->elf.root.u.def.value = 0;
			  fdh->elf.root.u.def.section = sym_sec;
			}
		      opd_adjust[rel->r_offset / 8] = -1;
		    }
		  else
		    {
		      /* We'll be keeping this opd entry.  */

		      if (fdh != NULL)
			{
			  /* Redefine the function descriptor symbol to
			     this location in the opd section.  It is
			     necessary to update the value here rather
			     than using an array of adjustments as we do
			     for local symbols, because various places
			     in the generic ELF code use the value
			     stored in u.def.value.  */
			  fdh->elf.root.u.def.value = wptr - new_contents;
			  fdh->adjust_done = 1;
			}

		      /* Local syms are a bit tricky.  We could
			 tweak them as they can be cached, but
			 we'd need to look through the local syms
			 for the function descriptor sym which we
			 don't have at the moment.  So keep an
			 array of adjustments.  */
		      opd_adjust[rel->r_offset / 8]
			= (wptr - new_contents) - (rptr - sec->contents);

		      if (wptr != rptr)
			memcpy (wptr, rptr, opd_ent_size);
		      wptr += opd_ent_size;
		      if (add_aux_fields && opd_ent_size == 16)
			{
			  memset (wptr, '\0', 8);
			  wptr += 8;
			}
		    }
		  rptr += opd_ent_size;
		  offset += opd_ent_size;
		}

	      if (skip)
		{
		  if (!NO_OPD_RELOCS
		      && !info->relocatable
		      && !dec_dynrel_count (rel->r_info, sec, info,
					    NULL, h, sym_sec))
		    goto error_ret;
		}
	      else
		{
		  /* We need to adjust any reloc offsets to point to the
		     new opd entries.  While we're at it, we may as well
		     remove redundant relocs.  */
		  rel->r_offset += opd_adjust[(offset - opd_ent_size) / 8];
		  if (write_rel != rel)
		    memcpy (write_rel, rel, sizeof (*rel));
		  ++write_rel;
		}
	    }

	  sec->size = wptr - new_contents;
	  sec->reloc_count = write_rel - relstart;
	  if (add_aux_fields)
	    {
	      free (sec->contents);
	      sec->contents = new_contents;
	    }

	  /* Fudge the header size too, as this is used later in
	     elf_bfd_final_link if we are emitting relocs.  */
	  elf_section_data (sec)->rel_hdr.sh_size
	    = sec->reloc_count * elf_section_data (sec)->rel_hdr.sh_entsize;
	  BFD_ASSERT (elf_section_data (sec)->rel_hdr2 == NULL);
	  some_edited = TRUE;
	}
      else if (elf_section_data (sec)->relocs != relstart)
	free (relstart);

      if (local_syms != NULL
	  && symtab_hdr->contents != (unsigned char *) local_syms)
	{
	  if (!info->keep_memory)
	    free (local_syms);
	  else
	    symtab_hdr->contents = (unsigned char *) local_syms;
	}
    }

  if (some_edited)
    elf_link_hash_traverse (elf_hash_table (info), adjust_opd_syms, NULL);

  /* If we are doing a final link and the last .opd entry is just 16 byte
     long, add a 8 byte padding after it.  */
  if (need_pad != NULL && !info->relocatable)
    {
      bfd_byte *p;

      if ((need_pad->flags & SEC_IN_MEMORY) == 0)
	{
	  BFD_ASSERT (need_pad->size > 0);

	  p = bfd_malloc (need_pad->size + 8);
	  if (p == NULL)
	    return FALSE;

	  if (! bfd_get_section_contents (need_pad->owner, need_pad,
					  p, 0, need_pad->size))
	    return FALSE;

	  need_pad->contents = p;
	  need_pad->flags |= (SEC_IN_MEMORY | SEC_HAS_CONTENTS);
	}
      else
	{
	  p = bfd_realloc (need_pad->contents, need_pad->size + 8);
	  if (p == NULL)
	    return FALSE;

	  need_pad->contents = p;
	}

      memset (need_pad->contents + need_pad->size, 0, 8);
      need_pad->size += 8;
    }

  return TRUE;
}

/* Set htab->tls_get_addr and call the generic ELF tls_setup function.  */

asection *
ppc64_elf_tls_setup (bfd *obfd, struct bfd_link_info *info)
{
  struct ppc_link_hash_table *htab;

  htab = ppc_hash_table (info);
  htab->tls_get_addr = ((struct ppc_link_hash_entry *)
			elf_link_hash_lookup (&htab->elf, ".__tls_get_addr",
					      FALSE, FALSE, TRUE));
  htab->tls_get_addr_fd = ((struct ppc_link_hash_entry *)
			   elf_link_hash_lookup (&htab->elf, "__tls_get_addr",
						 FALSE, FALSE, TRUE));
  return _bfd_elf_tls_setup (obfd, info);
}

/* Return TRUE iff REL is a branch reloc with a global symbol matching
   HASH1 or HASH2.  */

static bfd_boolean
branch_reloc_hash_match (const bfd *ibfd,
			 const Elf_Internal_Rela *rel,
			 const struct ppc_link_hash_entry *hash1,
			 const struct ppc_link_hash_entry *hash2)
{
  Elf_Internal_Shdr *symtab_hdr = &elf_tdata (ibfd)->symtab_hdr;
  enum elf_ppc64_reloc_type r_type = ELF64_R_TYPE (rel->r_info);
  unsigned int r_symndx = ELF64_R_SYM (rel->r_info);

  if (r_symndx >= symtab_hdr->sh_info
      && (r_type == R_PPC64_REL24
	  || r_type == R_PPC64_REL14
	  || r_type == R_PPC64_REL14_BRTAKEN
	  || r_type == R_PPC64_REL14_BRNTAKEN
	  || r_type == R_PPC64_ADDR24
	  || r_type == R_PPC64_ADDR14
	  || r_type == R_PPC64_ADDR14_BRTAKEN
	  || r_type == R_PPC64_ADDR14_BRNTAKEN))
    {
      struct elf_link_hash_entry **sym_hashes = elf_sym_hashes (ibfd);
      struct elf_link_hash_entry *h;

      h = sym_hashes[r_symndx - symtab_hdr->sh_info];
      while (h->root.type == bfd_link_hash_indirect
	     || h->root.type == bfd_link_hash_warning)
	h = (struct elf_link_hash_entry *) h->root.u.i.link;
      if (h == &hash1->elf || h == &hash2->elf)
	return TRUE;
    }
  return FALSE;
}

/* Run through all the TLS relocs looking for optimization
   opportunities.  The linker has been hacked (see ppc64elf.em) to do
   a preliminary section layout so that we know the TLS segment
   offsets.  We can't optimize earlier because some optimizations need
   to know the tp offset, and we need to optimize before allocating
   dynamic relocations.  */

bfd_boolean
ppc64_elf_tls_optimize (bfd *obfd ATTRIBUTE_UNUSED, struct bfd_link_info *info)
{
  bfd *ibfd;
  asection *sec;
  struct ppc_link_hash_table *htab;
  int pass;

  if (info->relocatable || !info->executable)
    return TRUE;

  htab = ppc_hash_table (info);
  for (ibfd = info->input_bfds; ibfd != NULL; ibfd = ibfd->link_next)
    {
      Elf_Internal_Sym *locsyms = NULL;
      asection *toc = bfd_get_section_by_name (ibfd, ".toc");
      unsigned char *toc_ref = NULL;

      /* Look at all the sections for this file.  Make two passes over
	 the relocs.  On the first pass, mark toc entries involved
	 with tls relocs, and check that tls relocs involved in
	 setting up a tls_get_addr call are indeed followed by such a
	 call.  If they are not, exclude them from the optimizations
	 done on the second pass.  */
      for (pass = 0; pass < 2; ++pass)
	for (sec = ibfd->sections; sec != NULL; sec = sec->next)
	  if (sec->has_tls_reloc && !bfd_is_abs_section (sec->output_section))
	    {
	      Elf_Internal_Rela *relstart, *rel, *relend;

	      /* Read the relocations.  */
	      relstart = _bfd_elf_link_read_relocs (ibfd, sec, NULL, NULL,
						    info->keep_memory);
	      if (relstart == NULL)
		return FALSE;

	      relend = relstart + sec->reloc_count;
	      for (rel = relstart; rel < relend; rel++)
		{
		  enum elf_ppc64_reloc_type r_type;
		  unsigned long r_symndx;
		  struct elf_link_hash_entry *h;
		  Elf_Internal_Sym *sym;
		  asection *sym_sec;
		  char *tls_mask;
		  char tls_set, tls_clear, tls_type = 0;
		  bfd_vma value;
		  bfd_boolean ok_tprel, is_local;
		  long toc_ref_index = 0;
		  int expecting_tls_get_addr = 0;

		  r_symndx = ELF64_R_SYM (rel->r_info);
		  if (!get_sym_h (&h, &sym, &sym_sec, &tls_mask, &locsyms,
				  r_symndx, ibfd))
		    {
		    err_free_rel:
		      if (elf_section_data (sec)->relocs != relstart)
			free (relstart);
		      if (toc_ref != NULL)
			free (toc_ref);
		      if (locsyms != NULL
			  && (elf_tdata (ibfd)->symtab_hdr.contents
			      != (unsigned char *) locsyms))
			free (locsyms);
		      return FALSE;
		    }

		  if (h != NULL)
		    {
		      if (h->root.type != bfd_link_hash_defined
			  && h->root.type != bfd_link_hash_defweak)
			continue;
		      value = h->root.u.def.value;
		    }
		  else
		    /* Symbols referenced by TLS relocs must be of type
		       STT_TLS.  So no need for .opd local sym adjust.  */
		    value = sym->st_value;

		  ok_tprel = FALSE;
		  is_local = FALSE;
		  if (h == NULL
		      || !h->def_dynamic)
		    {
		      is_local = TRUE;
		      value += sym_sec->output_offset;
		      value += sym_sec->output_section->vma;
		      value -= htab->elf.tls_sec->vma;
		      ok_tprel = (value + TP_OFFSET + ((bfd_vma) 1 << 31)
				  < (bfd_vma) 1 << 32);
		    }

		  r_type = ELF64_R_TYPE (rel->r_info);
		  switch (r_type)
		    {
		    case R_PPC64_GOT_TLSLD16:
		    case R_PPC64_GOT_TLSLD16_LO:
		      expecting_tls_get_addr = 1;
		      /* Fall thru */

		    case R_PPC64_GOT_TLSLD16_HI:
		    case R_PPC64_GOT_TLSLD16_HA:
		      /* These relocs should never be against a symbol
			 defined in a shared lib.  Leave them alone if
			 that turns out to be the case.  */
		      if (!is_local)
			continue;

		      /* LD -> LE */
		      tls_set = 0;
		      tls_clear = TLS_LD;
		      tls_type = TLS_TLS | TLS_LD;
		      break;

		    case R_PPC64_GOT_TLSGD16:
		    case R_PPC64_GOT_TLSGD16_LO:
		      expecting_tls_get_addr = 1;
		      /* Fall thru */

		    case R_PPC64_GOT_TLSGD16_HI:
		    case R_PPC64_GOT_TLSGD16_HA:
		      if (ok_tprel)
			/* GD -> LE */
			tls_set = 0;
		      else
			/* GD -> IE */
			tls_set = TLS_TLS | TLS_TPRELGD;
		      tls_clear = TLS_GD;
		      tls_type = TLS_TLS | TLS_GD;
		      break;

		    case R_PPC64_GOT_TPREL16_DS:
		    case R_PPC64_GOT_TPREL16_LO_DS:
		    case R_PPC64_GOT_TPREL16_HI:
		    case R_PPC64_GOT_TPREL16_HA:
		      if (ok_tprel)
			{
			  /* IE -> LE */
			  tls_set = 0;
			  tls_clear = TLS_TPREL;
			  tls_type = TLS_TLS | TLS_TPREL;
			  break;
			}
		      continue;

		    case R_PPC64_TOC16:
		    case R_PPC64_TOC16_LO:
		    case R_PPC64_TLS:
		    case R_PPC64_TLSGD:
		    case R_PPC64_TLSLD:
		      if (sym_sec == NULL || sym_sec != toc)
			continue;

		      /* Mark this toc entry as referenced by a TLS
			 code sequence.  We can do that now in the
			 case of R_PPC64_TLS, and after checking for
			 tls_get_addr for the TOC16 relocs.  */
		      if (toc_ref == NULL)
			{
			  toc_ref = bfd_zmalloc (toc->size / 8);
			  if (toc_ref == NULL)
			    goto err_free_rel;
			}
		      if (h != NULL)
			value = h->root.u.def.value;
		      else
			value = sym->st_value;
		      value += rel->r_addend;
		      BFD_ASSERT (value < toc->size && value % 8 == 0);
		      toc_ref_index = value / 8;
		      if (r_type == R_PPC64_TLS
			  || r_type == R_PPC64_TLSGD
			  || r_type == R_PPC64_TLSLD)
			{
			  toc_ref[toc_ref_index] = 1;
			  continue;
			}

		      if (pass != 0 && toc_ref[toc_ref_index] == 0)
			continue;

		      tls_set = 0;
		      tls_clear = 0;
		      expecting_tls_get_addr = 2;
		      break;

		    case R_PPC64_TPREL64:
		      if (pass == 0
			  || sec != toc
			  || toc_ref == NULL
			  || !toc_ref[rel->r_offset / 8])
			continue;
		      if (ok_tprel)
			{
			  /* IE -> LE */
			  tls_set = TLS_EXPLICIT;
			  tls_clear = TLS_TPREL;
			  break;
			}
		      continue;

		    case R_PPC64_DTPMOD64:
		      if (pass == 0
			  || sec != toc
			  || toc_ref == NULL
			  || !toc_ref[rel->r_offset / 8])
			continue;
		      if (rel + 1 < relend
			  && (rel[1].r_info
			      == ELF64_R_INFO (r_symndx, R_PPC64_DTPREL64))
			  && rel[1].r_offset == rel->r_offset + 8)
			{
			  if (ok_tprel)
			    /* GD -> LE */
			    tls_set = TLS_EXPLICIT | TLS_GD;
			  else
			    /* GD -> IE */
			    tls_set = TLS_EXPLICIT | TLS_GD | TLS_TPRELGD;
			  tls_clear = TLS_GD;
			}
		      else
			{
			  if (!is_local)
			    continue;

			  /* LD -> LE */
			  tls_set = TLS_EXPLICIT;
			  tls_clear = TLS_LD;
			}
		      break;

		    default:
		      continue;
		    }

		  if (pass == 0)
		    {
		      if (!expecting_tls_get_addr
			  || !sec->has_tls_get_addr_call)
			continue;

		      if (rel + 1 < relend
			  && branch_reloc_hash_match (ibfd, rel + 1,
						      htab->tls_get_addr,
						      htab->tls_get_addr_fd))
			{
			  if (expecting_tls_get_addr == 2)
			    {
			      /* Check for toc tls entries.  */
			      char *toc_tls;
			      int retval;

			      retval = get_tls_mask (&toc_tls, NULL, NULL,
						     &locsyms,
						     rel, ibfd);
			      if (retval == 0)
				goto err_free_rel;
			      if (retval > 1 && toc_tls != NULL)
				toc_ref[toc_ref_index] = 1;
			    }
			  continue;
			}

		      if (expecting_tls_get_addr != 1)
			continue;

		      /* Uh oh, we didn't find the expected call.  We
			 could just mark this symbol to exclude it
			 from tls optimization but it's safer to skip
			 the entire section.  */
		      sec->has_tls_reloc = 0;
		      break;
		    }

		  if (expecting_tls_get_addr)
		    {
		      struct plt_entry *ent;
		      for (ent = htab->tls_get_addr->elf.plt.plist;
			   ent != NULL;
			   ent = ent->next)
			if (ent->addend == 0)
			  {
			    if (ent->plt.refcount > 0)
			      {
				ent->plt.refcount -= 1;
				expecting_tls_get_addr = 0;
			      }
			    break;
			  }
		    }

		  if (expecting_tls_get_addr)
		    {
		      struct plt_entry *ent;
		      for (ent = htab->tls_get_addr_fd->elf.plt.plist;
			   ent != NULL;
			   ent = ent->next)
			if (ent->addend == 0)
			  {
			    if (ent->plt.refcount > 0)
			      ent->plt.refcount -= 1;
			    break;
			  }
		    }

		  if (tls_clear == 0)
		    continue;

		  if ((tls_set & TLS_EXPLICIT) == 0)
		    {
		      struct got_entry *ent;

		      /* Adjust got entry for this reloc.  */
		      if (h != NULL)
			ent = h->got.glist;
		      else
			ent = elf_local_got_ents (ibfd)[r_symndx];

		      for (; ent != NULL; ent = ent->next)
			if (ent->addend == rel->r_addend
			    && ent->owner == ibfd
			    && ent->tls_type == tls_type)
			  break;
		      if (ent == NULL)
			abort ();

		      if (tls_set == 0)
			{
			  /* We managed to get rid of a got entry.  */
			  if (ent->got.refcount > 0)
			    ent->got.refcount -= 1;
			}
		    }
		  else
		    {
		      /* If we got rid of a DTPMOD/DTPREL reloc pair then
			 we'll lose one or two dyn relocs.  */
		      if (!dec_dynrel_count (rel->r_info, sec, info,
					     NULL, h, sym_sec))
			return FALSE;

		      if (tls_set == (TLS_EXPLICIT | TLS_GD))
			{
			  if (!dec_dynrel_count ((rel + 1)->r_info, sec, info,
						 NULL, h, sym_sec))
			    return FALSE;
			}
		    }

		  *tls_mask |= tls_set;
		  *tls_mask &= ~tls_clear;
		}

	      if (elf_section_data (sec)->relocs != relstart)
		free (relstart);
	    }

	if (toc_ref != NULL)
	  free (toc_ref);

	if (locsyms != NULL
	    && (elf_tdata (ibfd)->symtab_hdr.contents
		!= (unsigned char *) locsyms))
	  {
	    if (!info->keep_memory)
	      free (locsyms);
	    else
	      elf_tdata (ibfd)->symtab_hdr.contents = (unsigned char *) locsyms;
	  }
      }
  return TRUE;
}

/* Called via elf_link_hash_traverse from ppc64_elf_edit_toc to adjust
   the values of any global symbols in a toc section that has been
   edited.  Globals in toc sections should be a rarity, so this function
   sets a flag if any are found in toc sections other than the one just
   edited, so that futher hash table traversals can be avoided.  */

struct adjust_toc_info
{
  asection *toc;
  unsigned long *skip;
  bfd_boolean global_toc_syms;
};

static bfd_boolean
adjust_toc_syms (struct elf_link_hash_entry *h, void *inf)
{
  struct ppc_link_hash_entry *eh;
  struct adjust_toc_info *toc_inf = (struct adjust_toc_info *) inf;

  if (h->root.type == bfd_link_hash_indirect)
    return TRUE;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  if (h->root.type != bfd_link_hash_defined
      && h->root.type != bfd_link_hash_defweak)
    return TRUE;

  eh = (struct ppc_link_hash_entry *) h;
  if (eh->adjust_done)
    return TRUE;

  if (eh->elf.root.u.def.section == toc_inf->toc)
    {
      unsigned long skip = toc_inf->skip[eh->elf.root.u.def.value >> 3];
      if (skip != (unsigned long) -1)
	eh->elf.root.u.def.value -= skip;
      else
	{
	  (*_bfd_error_handler)
	    (_("%s defined in removed toc entry"), eh->elf.root.root.string);
	  eh->elf.root.u.def.section = &bfd_abs_section;
	  eh->elf.root.u.def.value = 0;
	}
      eh->adjust_done = 1;
    }
  else if (strcmp (eh->elf.root.u.def.section->name, ".toc") == 0)
    toc_inf->global_toc_syms = TRUE;

  return TRUE;
}

/* Examine all relocs referencing .toc sections in order to remove
   unused .toc entries.  */

bfd_boolean
ppc64_elf_edit_toc (bfd *obfd ATTRIBUTE_UNUSED, struct bfd_link_info *info)
{
  bfd *ibfd;
  struct adjust_toc_info toc_inf;

  toc_inf.global_toc_syms = TRUE;
  for (ibfd = info->input_bfds; ibfd != NULL; ibfd = ibfd->link_next)
    {
      asection *toc, *sec;
      Elf_Internal_Shdr *symtab_hdr;
      Elf_Internal_Sym *local_syms;
      struct elf_link_hash_entry **sym_hashes;
      Elf_Internal_Rela *relstart, *rel;
      unsigned long *skip, *drop;
      unsigned char *used;
      unsigned char *keep, last, some_unused;

      toc = bfd_get_section_by_name (ibfd, ".toc");
      if (toc == NULL
	  || toc->size == 0
	  || toc->sec_info_type == ELF_INFO_TYPE_JUST_SYMS
	  || elf_discarded_section (toc))
	continue;

      local_syms = NULL;
      symtab_hdr = &elf_tdata (ibfd)->symtab_hdr;
      sym_hashes = elf_sym_hashes (ibfd);

      /* Look at sections dropped from the final link.  */
      skip = NULL;
      relstart = NULL;
      for (sec = ibfd->sections; sec != NULL; sec = sec->next)
	{
	  if (sec->reloc_count == 0
	      || !elf_discarded_section (sec)
	      || get_opd_info (sec)
	      || (sec->flags & SEC_ALLOC) == 0
	      || (sec->flags & SEC_DEBUGGING) != 0)
	    continue;

	  relstart = _bfd_elf_link_read_relocs (ibfd, sec, NULL, NULL, FALSE);
	  if (relstart == NULL)
	    goto error_ret;

	  /* Run through the relocs to see which toc entries might be
	     unused.  */
	  for (rel = relstart; rel < relstart + sec->reloc_count; ++rel)
	    {
	      enum elf_ppc64_reloc_type r_type;
	      unsigned long r_symndx;
	      asection *sym_sec;
	      struct elf_link_hash_entry *h;
	      Elf_Internal_Sym *sym;
	      bfd_vma val;

	      r_type = ELF64_R_TYPE (rel->r_info);
	      switch (r_type)
		{
		default:
		  continue;

		case R_PPC64_TOC16:
		case R_PPC64_TOC16_LO:
		case R_PPC64_TOC16_HI:
		case R_PPC64_TOC16_HA:
		case R_PPC64_TOC16_DS:
		case R_PPC64_TOC16_LO_DS:
		  break;
		}

	      r_symndx = ELF64_R_SYM (rel->r_info);
	      if (!get_sym_h (&h, &sym, &sym_sec, NULL, &local_syms,
			      r_symndx, ibfd))
		goto error_ret;

	      if (sym_sec != toc)
		continue;

	      if (h != NULL)
		val = h->root.u.def.value;
	      else
		val = sym->st_value;
	      val += rel->r_addend;

	      if (val >= toc->size)
		continue;

	      /* Anything in the toc ought to be aligned to 8 bytes.
		 If not, don't mark as unused.  */
	      if (val & 7)
		continue;

	      if (skip == NULL)
		{
		  skip = bfd_zmalloc (sizeof (*skip) * (toc->size + 7) / 8);
		  if (skip == NULL)
		    goto error_ret;
		}

	      skip[val >> 3] = 1;
	    }

	  if (elf_section_data (sec)->relocs != relstart)
	    free (relstart);
	}

      if (skip == NULL)
	continue;

      used = bfd_zmalloc (sizeof (*used) * (toc->size + 7) / 8);
      if (used == NULL)
	{
	error_ret:
	  if (local_syms != NULL
	      && symtab_hdr->contents != (unsigned char *) local_syms)
	    free (local_syms);
	  if (sec != NULL
	      && relstart != NULL
	      && elf_section_data (sec)->relocs != relstart)
	    free (relstart);
	  if (skip != NULL)
	    free (skip);
	  return FALSE;
	}

      /* Now check all kept sections that might reference the toc.
	 Check the toc itself last.  */
      for (sec = (ibfd->sections == toc && toc->next ? toc->next
		  : ibfd->sections);
	   sec != NULL;
	   sec = (sec == toc ? NULL
		  : sec->next == NULL ? toc
		  : sec->next == toc && toc->next ? toc->next
		  : sec->next))
	{
	  int repeat;

	  if (sec->reloc_count == 0
	      || elf_discarded_section (sec)
	      || get_opd_info (sec)
	      || (sec->flags & SEC_ALLOC) == 0
	      || (sec->flags & SEC_DEBUGGING) != 0)
	    continue;

	  relstart = _bfd_elf_link_read_relocs (ibfd, sec, NULL, NULL, TRUE);
	  if (relstart == NULL)
	    goto error_ret;

	  /* Mark toc entries referenced as used.  */
	  repeat = 0;
	  do
	    for (rel = relstart; rel < relstart + sec->reloc_count; ++rel)
	      {
		enum elf_ppc64_reloc_type r_type;
		unsigned long r_symndx;
		asection *sym_sec;
		struct elf_link_hash_entry *h;
		Elf_Internal_Sym *sym;
		bfd_vma val;

		r_type = ELF64_R_TYPE (rel->r_info);
		switch (r_type)
		  {
		  case R_PPC64_TOC16:
		  case R_PPC64_TOC16_LO:
		  case R_PPC64_TOC16_HI:
		  case R_PPC64_TOC16_HA:
		  case R_PPC64_TOC16_DS:
		  case R_PPC64_TOC16_LO_DS:
		    /* In case we're taking addresses of toc entries.  */
		  case R_PPC64_ADDR64:
		    break;

		  default:
		    continue;
		  }

		r_symndx = ELF64_R_SYM (rel->r_info);
		if (!get_sym_h (&h, &sym, &sym_sec, NULL, &local_syms,
				r_symndx, ibfd))
		  {
		    free (used);
		    goto error_ret;
		  }

		if (sym_sec != toc)
		  continue;

		if (h != NULL)
		  val = h->root.u.def.value;
		else
		  val = sym->st_value;
		val += rel->r_addend;

		if (val >= toc->size)
		  continue;

		/* For the toc section, we only mark as used if
		   this entry itself isn't unused.  */
		if (sec == toc
		    && !used[val >> 3]
		    && (used[rel->r_offset >> 3]
			|| !skip[rel->r_offset >> 3]))
		  /* Do all the relocs again, to catch reference
		     chains.  */
		  repeat = 1;

		used[val >> 3] = 1;
	      }
	  while (repeat);
	}

      /* Merge the used and skip arrays.  Assume that TOC
	 doublewords not appearing as either used or unused belong
	 to to an entry more than one doubleword in size.  */
      for (drop = skip, keep = used, last = 0, some_unused = 0;
	   drop < skip + (toc->size + 7) / 8;
	   ++drop, ++keep)
	{
	  if (*keep)
	    {
	      *drop = 0;
	      last = 0;
	    }
	  else if (*drop)
	    {
	      some_unused = 1;
	      last = 1;
	    }
	  else
	    *drop = last;
	}

      free (used);

      if (some_unused)
	{
	  bfd_byte *contents, *src;
	  unsigned long off;

	  /* Shuffle the toc contents, and at the same time convert the
	     skip array from booleans into offsets.  */
	  if (!bfd_malloc_and_get_section (ibfd, toc, &contents))
	    goto error_ret;

	  elf_section_data (toc)->this_hdr.contents = contents;

	  for (src = contents, off = 0, drop = skip;
	       src < contents + toc->size;
	       src += 8, ++drop)
	    {
	      if (*drop)
		{
		  *drop = (unsigned long) -1;
		  off += 8;
		}
	      else if (off != 0)
		{
		  *drop = off;
		  memcpy (src - off, src, 8);
		}
	    }
	  toc->rawsize = toc->size;
	  toc->size = src - contents - off;

	  if (toc->reloc_count != 0)
	    {
	      Elf_Internal_Rela *wrel;
	      bfd_size_type sz;

	      /* Read toc relocs.  */
	      relstart = _bfd_elf_link_read_relocs (ibfd, toc, NULL, NULL,
						    TRUE);
	      if (relstart == NULL)
		goto error_ret;

	      /* Remove unused toc relocs, and adjust those we keep.  */
	      wrel = relstart;
	      for (rel = relstart; rel < relstart + toc->reloc_count; ++rel)
		if (skip[rel->r_offset >> 3] != (unsigned long) -1)
		  {
		    wrel->r_offset = rel->r_offset - skip[rel->r_offset >> 3];
		    wrel->r_info = rel->r_info;
		    wrel->r_addend = rel->r_addend;
		    ++wrel;
		  }
		else if (!dec_dynrel_count (rel->r_info, toc, info,
					    &local_syms, NULL, NULL))
		  goto error_ret;

	      toc->reloc_count = wrel - relstart;
	      sz = elf_section_data (toc)->rel_hdr.sh_entsize;
	      elf_section_data (toc)->rel_hdr.sh_size = toc->reloc_count * sz;
	      BFD_ASSERT (elf_section_data (toc)->rel_hdr2 == NULL);
	    }

	  /* Adjust addends for relocs against the toc section sym.  */
	  for (sec = ibfd->sections; sec != NULL; sec = sec->next)
	    {
	      if (sec->reloc_count == 0
		  || elf_discarded_section (sec))
		continue;

	      relstart = _bfd_elf_link_read_relocs (ibfd, sec, NULL, NULL,
						    TRUE);
	      if (relstart == NULL)
		goto error_ret;

	      for (rel = relstart; rel < relstart + sec->reloc_count; ++rel)
		{
		  enum elf_ppc64_reloc_type r_type;
		  unsigned long r_symndx;
		  asection *sym_sec;
		  struct elf_link_hash_entry *h;
		  Elf_Internal_Sym *sym;

		  r_type = ELF64_R_TYPE (rel->r_info);
		  switch (r_type)
		    {
		    default:
		      continue;

		    case R_PPC64_TOC16:
		    case R_PPC64_TOC16_LO:
		    case R_PPC64_TOC16_HI:
		    case R_PPC64_TOC16_HA:
		    case R_PPC64_TOC16_DS:
		    case R_PPC64_TOC16_LO_DS:
		    case R_PPC64_ADDR64:
		      break;
		    }

		  r_symndx = ELF64_R_SYM (rel->r_info);
		  if (!get_sym_h (&h, &sym, &sym_sec, NULL, &local_syms,
				  r_symndx, ibfd))
		    goto error_ret;

		  if (sym_sec != toc || h != NULL || sym->st_value != 0)
		    continue;

		  rel->r_addend -= skip[rel->r_addend >> 3];
		}
	    }

	  /* We shouldn't have local or global symbols defined in the TOC,
	     but handle them anyway.  */
	  if (local_syms != NULL)
	    {
	      Elf_Internal_Sym *sym;

	      for (sym = local_syms;
		   sym < local_syms + symtab_hdr->sh_info;
		   ++sym)
		if (sym->st_shndx != SHN_UNDEF
		    && (sym->st_shndx < SHN_LORESERVE
			|| sym->st_shndx > SHN_HIRESERVE)
		    && sym->st_value != 0
		    && bfd_section_from_elf_index (ibfd, sym->st_shndx) == toc)
		  {
		    if (skip[sym->st_value >> 3] != (unsigned long) -1)
		      sym->st_value -= skip[sym->st_value >> 3];
		    else
		      {
			(*_bfd_error_handler)
			  (_("%s defined in removed toc entry"),
			   bfd_elf_sym_name (ibfd, symtab_hdr, sym,
					     NULL));
			sym->st_value = 0;
			sym->st_shndx = SHN_ABS;
		      }
		    symtab_hdr->contents = (unsigned char *) local_syms;
		  }
	    }

	  /* Finally, adjust any global syms defined in the toc.  */
	  if (toc_inf.global_toc_syms)
	    {
	      toc_inf.toc = toc;
	      toc_inf.skip = skip;
	      toc_inf.global_toc_syms = FALSE;
	      elf_link_hash_traverse (elf_hash_table (info), adjust_toc_syms,
				      &toc_inf);
	    }
	}

      if (local_syms != NULL
	  && symtab_hdr->contents != (unsigned char *) local_syms)
	{
	  if (!info->keep_memory)
	    free (local_syms);
	  else
	    symtab_hdr->contents = (unsigned char *) local_syms;
	}
      free (skip);
    }

  return TRUE;
}

/* Allocate space in .plt, .got and associated reloc sections for
   dynamic relocs.  */

static bfd_boolean
allocate_dynrelocs (struct elf_link_hash_entry *h, void *inf)
{
  struct bfd_link_info *info;
  struct ppc_link_hash_table *htab;
  asection *s;
  struct ppc_link_hash_entry *eh;
  struct ppc_dyn_relocs *p;
  struct got_entry *gent;

  if (h->root.type == bfd_link_hash_indirect)
    return TRUE;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  info = (struct bfd_link_info *) inf;
  htab = ppc_hash_table (info);

  if (htab->elf.dynamic_sections_created
      && h->dynindx != -1
      && WILL_CALL_FINISH_DYNAMIC_SYMBOL (1, info->shared, h))
    {
      struct plt_entry *pent;
      bfd_boolean doneone = FALSE;
      for (pent = h->plt.plist; pent != NULL; pent = pent->next)
	if (pent->plt.refcount > 0)
	  {
	    /* If this is the first .plt entry, make room for the special
	       first entry.  */
	    s = htab->plt;
	    if (s->size == 0)
	      s->size += PLT_INITIAL_ENTRY_SIZE;

	    pent->plt.offset = s->size;

	    /* Make room for this entry.  */
	    s->size += PLT_ENTRY_SIZE;

	    /* Make room for the .glink code.  */
	    s = htab->glink;
	    if (s->size == 0)
	      s->size += GLINK_CALL_STUB_SIZE;
	    /* We need bigger stubs past index 32767.  */
	    if (s->size >= GLINK_CALL_STUB_SIZE + 32768*2*4)
	      s->size += 4;
	    s->size += 2*4;

	    /* We also need to make an entry in the .rela.plt section.  */
	    s = htab->relplt;
	    s->size += sizeof (Elf64_External_Rela);
	    doneone = TRUE;
	  }
	else
	  pent->plt.offset = (bfd_vma) -1;
      if (!doneone)
	{
	  h->plt.plist = NULL;
	  h->needs_plt = 0;
	}
    }
  else
    {
      h->plt.plist = NULL;
      h->needs_plt = 0;
    }

  eh = (struct ppc_link_hash_entry *) h;
  /* Run through the TLS GD got entries first if we're changing them
     to TPREL.  */
  if ((eh->tls_mask & TLS_TPRELGD) != 0)
    for (gent = h->got.glist; gent != NULL; gent = gent->next)
      if (gent->got.refcount > 0
	  && (gent->tls_type & TLS_GD) != 0)
	{
	  /* This was a GD entry that has been converted to TPREL.  If
	     there happens to be a TPREL entry we can use that one.  */
	  struct got_entry *ent;
	  for (ent = h->got.glist; ent != NULL; ent = ent->next)
	    if (ent->got.refcount > 0
		&& (ent->tls_type & TLS_TPREL) != 0
		&& ent->addend == gent->addend
		&& ent->owner == gent->owner)
	      {
		gent->got.refcount = 0;
		break;
	      }

	  /* If not, then we'll be using our own TPREL entry.  */
	  if (gent->got.refcount != 0)
	    gent->tls_type = TLS_TLS | TLS_TPREL;
	}

  for (gent = h->got.glist; gent != NULL; gent = gent->next)
    if (gent->got.refcount > 0)
      {
	bfd_boolean dyn;

	/* Make sure this symbol is output as a dynamic symbol.
	   Undefined weak syms won't yet be marked as dynamic,
	   nor will all TLS symbols.  */
	if (h->dynindx == -1
	    && !h->forced_local)
	  {
	    if (! bfd_elf_link_record_dynamic_symbol (info, h))
	      return FALSE;
	  }

	if ((gent->tls_type & TLS_LD) != 0
	    && !h->def_dynamic)
	  {
	    ppc64_tlsld_got (gent->owner)->refcount += 1;
	    gent->got.offset = (bfd_vma) -1;
	    continue;
	  }

	s = ppc64_elf_tdata (gent->owner)->got;
	gent->got.offset = s->size;
	s->size
	  += (gent->tls_type & eh->tls_mask & (TLS_GD | TLS_LD)) ? 16 : 8;
	dyn = htab->elf.dynamic_sections_created;
	if ((info->shared
	     || WILL_CALL_FINISH_DYNAMIC_SYMBOL (dyn, 0, h))
	    && (ELF_ST_VISIBILITY (h->other) == STV_DEFAULT
		|| h->root.type != bfd_link_hash_undefweak))
	  ppc64_elf_tdata (gent->owner)->relgot->size
	    += (gent->tls_type & eh->tls_mask & TLS_GD
		? 2 * sizeof (Elf64_External_Rela)
		: sizeof (Elf64_External_Rela));
      }
    else
      gent->got.offset = (bfd_vma) -1;

  if (eh->dyn_relocs == NULL)
    return TRUE;

  /* In the shared -Bsymbolic case, discard space allocated for
     dynamic pc-relative relocs against symbols which turn out to be
     defined in regular objects.  For the normal shared case, discard
     space for relocs that have become local due to symbol visibility
     changes.  */

  if (info->shared)
    {
      /* Relocs that use pc_count are those that appear on a call insn,
	 or certain REL relocs (see must_be_dyn_reloc) that can be
	 generated via assembly.  We want calls to protected symbols to
	 resolve directly to the function rather than going via the plt.
	 If people want function pointer comparisons to work as expected
	 then they should avoid writing weird assembly.  */
      if (SYMBOL_CALLS_LOCAL (info, h))
	{
	  struct ppc_dyn_relocs **pp;

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

	  /* Make sure this symbol is output as a dynamic symbol.
	     Undefined weak syms won't yet be marked as dynamic.  */
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
	  && h->def_dynamic
	  && !h->def_regular)
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
readonly_dynrelocs (struct elf_link_hash_entry *h, void *inf)
{
  struct ppc_link_hash_entry *eh;
  struct ppc_dyn_relocs *p;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  eh = (struct ppc_link_hash_entry *) h;
  for (p = eh->dyn_relocs; p != NULL; p = p->next)
    {
      asection *s = p->sec->output_section;

      if (s != NULL && (s->flags & SEC_READONLY) != 0)
	{
	  struct bfd_link_info *info = inf;

	  info->flags |= DF_TEXTREL;

	  /* Not an error, just cut short the traversal.  */
	  return FALSE;
	}
    }
  return TRUE;
}

/* Set the sizes of the dynamic sections.  */

static bfd_boolean
ppc64_elf_size_dynamic_sections (bfd *output_bfd ATTRIBUTE_UNUSED,
				 struct bfd_link_info *info)
{
  struct ppc_link_hash_table *htab;
  bfd *dynobj;
  asection *s;
  bfd_boolean relocs;
  bfd *ibfd;

  htab = ppc_hash_table (info);
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
      struct got_entry **lgot_ents;
      struct got_entry **end_lgot_ents;
      char *lgot_masks;
      bfd_size_type locsymcount;
      Elf_Internal_Shdr *symtab_hdr;
      asection *srel;

      if (!is_ppc64_elf_target (ibfd->xvec))
	continue;

      for (s = ibfd->sections; s != NULL; s = s->next)
	{
	  struct ppc_dyn_relocs *p;

	  for (p = elf_section_data (s)->local_dynrel; p != NULL; p = p->next)
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

      lgot_ents = elf_local_got_ents (ibfd);
      if (!lgot_ents)
	continue;

      symtab_hdr = &elf_tdata (ibfd)->symtab_hdr;
      locsymcount = symtab_hdr->sh_info;
      end_lgot_ents = lgot_ents + locsymcount;
      lgot_masks = (char *) end_lgot_ents;
      s = ppc64_elf_tdata (ibfd)->got;
      srel = ppc64_elf_tdata (ibfd)->relgot;
      for (; lgot_ents < end_lgot_ents; ++lgot_ents, ++lgot_masks)
	{
	  struct got_entry *ent;

	  for (ent = *lgot_ents; ent != NULL; ent = ent->next)
	    if (ent->got.refcount > 0)
	      {
		if ((ent->tls_type & *lgot_masks & TLS_LD) != 0)
		  {
		    ppc64_tlsld_got (ibfd)->refcount += 1;
		    ent->got.offset = (bfd_vma) -1;
		  }
		else
		  {
		    ent->got.offset = s->size;
		    if ((ent->tls_type & *lgot_masks & TLS_GD) != 0)
		      {
			s->size += 16;
			if (info->shared)
			  srel->size += 2 * sizeof (Elf64_External_Rela);
		      }
		    else
		      {
			s->size += 8;
			if (info->shared)
			  srel->size += sizeof (Elf64_External_Rela);
		      }
		  }
	      }
	    else
	      ent->got.offset = (bfd_vma) -1;
	}
    }

  /* Allocate global sym .plt and .got entries, and space for global
     sym dynamic relocs.  */
  elf_link_hash_traverse (&htab->elf, allocate_dynrelocs, info);

  for (ibfd = info->input_bfds; ibfd != NULL; ibfd = ibfd->link_next)
    {
      if (!is_ppc64_elf_target (ibfd->xvec))
	continue;

      if (ppc64_tlsld_got (ibfd)->refcount > 0)
	{
	  s = ppc64_elf_tdata (ibfd)->got;
	  ppc64_tlsld_got (ibfd)->offset = s->size;
	  s->size += 16;
	  if (info->shared)
	    {
	      asection *srel = ppc64_elf_tdata (ibfd)->relgot;
	      srel->size += sizeof (Elf64_External_Rela);
	    }
	}
      else
	ppc64_tlsld_got (ibfd)->offset = (bfd_vma) -1;
    }

  /* We now have determined the sizes of the various dynamic sections.
     Allocate memory for them.  */
  relocs = FALSE;
  for (s = dynobj->sections; s != NULL; s = s->next)
    {
      if ((s->flags & SEC_LINKER_CREATED) == 0)
	continue;

      if (s == htab->brlt || s == htab->relbrlt)
	/* These haven't been allocated yet;  don't strip.  */
	continue;
      else if (s == htab->got
	       || s == htab->plt
	       || s == htab->glink
	       || s == htab->dynbss)
	{
	  /* Strip this section if we don't need it; see the
	     comment below.  */
	}
      else if (CONST_STRNEQ (bfd_get_section_name (dynobj, s), ".rela"))
	{
	  if (s->size != 0)
	    {
	      if (s != htab->relplt)
		relocs = TRUE;

	      /* We use the reloc_count field as a counter if we need
		 to copy relocs into the output file.  */
	      s->reloc_count = 0;
	    }
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
	 but this way if it does we get a R_PPC64_NONE reloc in .rela
	 sections instead of garbage.
	 We also rely on the section contents being zero when writing
	 the GOT.  */
      s->contents = bfd_zalloc (dynobj, s->size);
      if (s->contents == NULL)
	return FALSE;
    }

  for (ibfd = info->input_bfds; ibfd != NULL; ibfd = ibfd->link_next)
    {
      if (!is_ppc64_elf_target (ibfd->xvec))
	continue;

      s = ppc64_elf_tdata (ibfd)->got;
      if (s != NULL && s != htab->got)
	{
	  if (s->size == 0)
	    s->flags |= SEC_EXCLUDE;
	  else
	    {
	      s->contents = bfd_zalloc (ibfd, s->size);
	      if (s->contents == NULL)
		return FALSE;
	    }
	}
      s = ppc64_elf_tdata (ibfd)->relgot;
      if (s != NULL)
	{
	  if (s->size == 0)
	    s->flags |= SEC_EXCLUDE;
	  else
	    {
	      s->contents = bfd_zalloc (ibfd, s->size);
	      if (s->contents == NULL)
		return FALSE;
	      relocs = TRUE;
	      s->reloc_count = 0;
	    }
	}
    }

  if (htab->elf.dynamic_sections_created)
    {
      /* Add some entries to the .dynamic section.  We fill in the
	 values later, in ppc64_elf_finish_dynamic_sections, but we
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

      if (htab->plt != NULL && htab->plt->size != 0)
	{
	  if (!add_dynamic_entry (DT_PLTGOT, 0)
	      || !add_dynamic_entry (DT_PLTRELSZ, 0)
	      || !add_dynamic_entry (DT_PLTREL, DT_RELA)
	      || !add_dynamic_entry (DT_JMPREL, 0)
	      || !add_dynamic_entry (DT_PPC64_GLINK, 0))
	    return FALSE;
	}

      if (NO_OPD_RELOCS)
	{
	  if (!add_dynamic_entry (DT_PPC64_OPD, 0)
	      || !add_dynamic_entry (DT_PPC64_OPDSZ, 0))
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
	    elf_link_hash_traverse (&htab->elf, readonly_dynrelocs, info);

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

/* Determine the type of stub needed, if any, for a call.  */

static inline enum ppc_stub_type
ppc_type_of_stub (asection *input_sec,
		  const Elf_Internal_Rela *rel,
		  struct ppc_link_hash_entry **hash,
		  bfd_vma destination)
{
  struct ppc_link_hash_entry *h = *hash;
  bfd_vma location;
  bfd_vma branch_offset;
  bfd_vma max_branch_offset;
  enum elf_ppc64_reloc_type r_type;

  if (h != NULL)
    {
      struct ppc_link_hash_entry *fdh = h;
      if (fdh->oh != NULL
	  && fdh->oh->is_func_descriptor)
	fdh = fdh->oh;

      if (fdh->elf.dynindx != -1)
	{
	  struct plt_entry *ent;

	  for (ent = fdh->elf.plt.plist; ent != NULL; ent = ent->next)
	    if (ent->addend == rel->r_addend
		&& ent->plt.offset != (bfd_vma) -1)
	      {
		*hash = fdh;
		return ppc_stub_plt_call;
	      }
	}

      /* Here, we know we don't have a plt entry.  If we don't have a
	 either a defined function descriptor or a defined entry symbol
	 in a regular object file, then it is pointless trying to make
	 any other type of stub.  */
      if (!((fdh->elf.root.type == bfd_link_hash_defined
	    || fdh->elf.root.type == bfd_link_hash_defweak)
	    && fdh->elf.root.u.def.section->output_section != NULL)
	  && !((h->elf.root.type == bfd_link_hash_defined
		|| h->elf.root.type == bfd_link_hash_defweak)
	       && h->elf.root.u.def.section->output_section != NULL))
	return ppc_stub_none;
    }

  /* Determine where the call point is.  */
  location = (input_sec->output_offset
	      + input_sec->output_section->vma
	      + rel->r_offset);

  branch_offset = destination - location;
  r_type = ELF64_R_TYPE (rel->r_info);

  /* Determine if a long branch stub is needed.  */
  max_branch_offset = 1 << 25;
  if (r_type != R_PPC64_REL24)
    max_branch_offset = 1 << 15;

  if (branch_offset + max_branch_offset >= 2 * max_branch_offset)
    /* We need a stub.  Figure out whether a long_branch or plt_branch
       is needed later.  */
    return ppc_stub_long_branch;

  return ppc_stub_none;
}

/* Build a .plt call stub.  */

static inline bfd_byte *
build_plt_stub (bfd *obfd, bfd_byte *p, int offset)
{
#define PPC_LO(v) ((v) & 0xffff)
#define PPC_HI(v) (((v) >> 16) & 0xffff)
#define PPC_HA(v) PPC_HI ((v) + 0x8000)

  if (PPC_HA (offset) != 0)
    {
      bfd_put_32 (obfd, ADDIS_R12_R2 | PPC_HA (offset), p),	p += 4;
      bfd_put_32 (obfd, STD_R2_40R1, p),			p += 4;
      bfd_put_32 (obfd, LD_R11_0R12 | PPC_LO (offset), p),	p += 4;
      if (PPC_HA (offset + 16) != PPC_HA (offset))
	{
	  bfd_put_32 (obfd, ADDI_R12_R12 | PPC_LO (offset), p),	p += 4;
	  offset = 0;
	}
      bfd_put_32 (obfd, MTCTR_R11, p),				p += 4;
      bfd_put_32 (obfd, LD_R2_0R12 | PPC_LO (offset + 8), p),	p += 4;
      bfd_put_32 (obfd, LD_R11_0R12 | PPC_LO (offset + 16), p),	p += 4;
      bfd_put_32 (obfd, BCTR, p),				p += 4;
    }
  else
    {
      bfd_put_32 (obfd, STD_R2_40R1, p),			p += 4;
      bfd_put_32 (obfd, LD_R11_0R2 | PPC_LO (offset), p),	p += 4;
      if (PPC_HA (offset + 16) != PPC_HA (offset))
	{
	  bfd_put_32 (obfd, ADDI_R2_R2 | PPC_LO (offset), p),	p += 4;
	  offset = 0;
	}
      bfd_put_32 (obfd, MTCTR_R11, p),				p += 4;
      bfd_put_32 (obfd, LD_R11_0R2 | PPC_LO (offset + 16), p),	p += 4;
      bfd_put_32 (obfd, LD_R2_0R2 | PPC_LO (offset + 8), p),	p += 4;
      bfd_put_32 (obfd, BCTR, p),				p += 4;
    }
  return p;
}

static bfd_boolean
ppc_build_one_stub (struct bfd_hash_entry *gen_entry, void *in_arg)
{
  struct ppc_stub_hash_entry *stub_entry;
  struct ppc_branch_hash_entry *br_entry;
  struct bfd_link_info *info;
  struct ppc_link_hash_table *htab;
  bfd_byte *loc;
  bfd_byte *p;
  unsigned int indx;
  struct plt_entry *ent;
  bfd_vma dest, off;
  int size;

  /* Massage our args to the form they really have.  */
  stub_entry = (struct ppc_stub_hash_entry *) gen_entry;
  info = in_arg;

  htab = ppc_hash_table (info);

  /* Make a note of the offset within the stubs for this entry.  */
  stub_entry->stub_offset = stub_entry->stub_sec->size;
  loc = stub_entry->stub_sec->contents + stub_entry->stub_offset;

  htab->stub_count[stub_entry->stub_type - 1] += 1;
  switch (stub_entry->stub_type)
    {
    case ppc_stub_long_branch:
    case ppc_stub_long_branch_r2off:
      /* Branches are relative.  This is where we are going to.  */
      off = dest = (stub_entry->target_value
		    + stub_entry->target_section->output_offset
		    + stub_entry->target_section->output_section->vma);

      /* And this is where we are coming from.  */
      off -= (stub_entry->stub_offset
	      + stub_entry->stub_sec->output_offset
	      + stub_entry->stub_sec->output_section->vma);

      size = 4;
      if (stub_entry->stub_type == ppc_stub_long_branch_r2off)
	{
	  bfd_vma r2off;

	  r2off = (htab->stub_group[stub_entry->target_section->id].toc_off
		   - htab->stub_group[stub_entry->id_sec->id].toc_off);
	  bfd_put_32 (htab->stub_bfd, STD_R2_40R1, loc);
	  loc += 4;
	  size = 12;
	  if (PPC_HA (r2off) != 0)
	    {
	      size = 16;
	      bfd_put_32 (htab->stub_bfd, ADDIS_R2_R2 | PPC_HA (r2off), loc);
	      loc += 4;
	    }
	  bfd_put_32 (htab->stub_bfd, ADDI_R2_R2 | PPC_LO (r2off), loc);
	  loc += 4;
	  off -= size - 4;
	}
      bfd_put_32 (htab->stub_bfd, B_DOT | (off & 0x3fffffc), loc);

      if (off + (1 << 25) >= (bfd_vma) (1 << 26))
	{
	  (*_bfd_error_handler) (_("long branch stub `%s' offset overflow"),
				 stub_entry->root.string);
	  htab->stub_error = TRUE;
	  return FALSE;
	}

      if (info->emitrelocations)
	{
	  Elf_Internal_Rela *relocs, *r;
	  struct bfd_elf_section_data *elfsec_data;

	  elfsec_data = elf_section_data (stub_entry->stub_sec);
	  relocs = elfsec_data->relocs;
	  if (relocs == NULL)
	    {
	      bfd_size_type relsize;
	      relsize = stub_entry->stub_sec->reloc_count * sizeof (*relocs);
	      relocs = bfd_alloc (htab->stub_bfd, relsize);
	      if (relocs == NULL)
		return FALSE;
	      elfsec_data->relocs = relocs;
	      elfsec_data->rel_hdr.sh_size = relsize;
	      elfsec_data->rel_hdr.sh_entsize = 24;
	      stub_entry->stub_sec->reloc_count = 0;
	    }
	  r = relocs + stub_entry->stub_sec->reloc_count;
	  stub_entry->stub_sec->reloc_count += 1;
	  r->r_offset = loc - stub_entry->stub_sec->contents;
	  r->r_info = ELF64_R_INFO (0, R_PPC64_REL24);
	  r->r_addend = dest;
	  if (stub_entry->h != NULL)
	    {
	      struct elf_link_hash_entry **hashes;
	      unsigned long symndx;
	      struct ppc_link_hash_entry *h;

	      hashes = elf_sym_hashes (htab->stub_bfd);
	      if (hashes == NULL)
		{
		  bfd_size_type hsize;

		  hsize = (htab->stub_globals + 1) * sizeof (*hashes);
		  hashes = bfd_zalloc (htab->stub_bfd, hsize);
		  if (hashes == NULL)
		    return FALSE;
		  elf_sym_hashes (htab->stub_bfd) = hashes;
		  htab->stub_globals = 1;
		}
	      symndx = htab->stub_globals++;
	      h = stub_entry->h;
	      hashes[symndx] = &h->elf;
	      r->r_info = ELF64_R_INFO (symndx, R_PPC64_REL24);
	      if (h->oh != NULL && h->oh->is_func)
		h = h->oh;
	      if (h->elf.root.u.def.section != stub_entry->target_section)
		/* H is an opd symbol.  The addend must be zero.  */
		r->r_addend = 0;
	      else
		{
		  off = (h->elf.root.u.def.value
			 + h->elf.root.u.def.section->output_offset
			 + h->elf.root.u.def.section->output_section->vma);
		  r->r_addend -= off;
		}
	    }
	}
      break;

    case ppc_stub_plt_branch:
    case ppc_stub_plt_branch_r2off:
      br_entry = ppc_branch_hash_lookup (&htab->branch_hash_table,
					 stub_entry->root.string + 9,
					 FALSE, FALSE);
      if (br_entry == NULL)
	{
	  (*_bfd_error_handler) (_("can't find branch stub `%s'"),
				 stub_entry->root.string);
	  htab->stub_error = TRUE;
	  return FALSE;
	}

      off = (stub_entry->target_value
	     + stub_entry->target_section->output_offset
	     + stub_entry->target_section->output_section->vma);

      bfd_put_64 (htab->brlt->owner, off,
		  htab->brlt->contents + br_entry->offset);

      if (htab->relbrlt != NULL)
	{
	  /* Create a reloc for the branch lookup table entry.  */
	  Elf_Internal_Rela rela;
	  bfd_byte *rl;

	  rela.r_offset = (br_entry->offset
			   + htab->brlt->output_offset
			   + htab->brlt->output_section->vma);
	  rela.r_info = ELF64_R_INFO (0, R_PPC64_RELATIVE);
	  rela.r_addend = off;

	  rl = htab->relbrlt->contents;
	  rl += htab->relbrlt->reloc_count++ * sizeof (Elf64_External_Rela);
	  bfd_elf64_swap_reloca_out (htab->relbrlt->owner, &rela, rl);
	}
      else if (info->emitrelocations)
	{
	  Elf_Internal_Rela *relocs, *r;
	  struct bfd_elf_section_data *elfsec_data;

	  elfsec_data = elf_section_data (htab->brlt);
	  relocs = elfsec_data->relocs;
	  if (relocs == NULL)
	    {
	      bfd_size_type relsize;
	      relsize = htab->brlt->reloc_count * sizeof (*relocs);
	      relocs = bfd_alloc (htab->brlt->owner, relsize);
	      if (relocs == NULL)
		return FALSE;
	      elfsec_data->relocs = relocs;
	      elfsec_data->rel_hdr.sh_size = relsize;
	      elfsec_data->rel_hdr.sh_entsize = 24;
	      htab->brlt->reloc_count = 0;
	    }
	  r = relocs + htab->brlt->reloc_count;
	  htab->brlt->reloc_count += 1;
	  r->r_offset = (br_entry->offset
			 + htab->brlt->output_offset
			 + htab->brlt->output_section->vma);
	  r->r_info = ELF64_R_INFO (0, R_PPC64_RELATIVE);
	  r->r_addend = off;
	}

      off = (br_entry->offset
	     + htab->brlt->output_offset
	     + htab->brlt->output_section->vma
	     - elf_gp (htab->brlt->output_section->owner)
	     - htab->stub_group[stub_entry->id_sec->id].toc_off);

      if (off + 0x80008000 > 0xffffffff || (off & 7) != 0)
	{
	  (*_bfd_error_handler)
	    (_("linkage table error against `%s'"),
	     stub_entry->root.string);
	  bfd_set_error (bfd_error_bad_value);
	  htab->stub_error = TRUE;
	  return FALSE;
	}

      indx = off;
      if (stub_entry->stub_type != ppc_stub_plt_branch_r2off)
	{
	  if (PPC_HA (indx) != 0)
	    {
	      size = 16;
	      bfd_put_32 (htab->stub_bfd, ADDIS_R12_R2 | PPC_HA (indx), loc);
	      loc += 4;
	      bfd_put_32 (htab->stub_bfd, LD_R11_0R12 | PPC_LO (indx), loc);
	    }
	  else
	    {
	      size = 12;
	      bfd_put_32 (htab->stub_bfd, LD_R11_0R2 | PPC_LO (indx), loc);
	    }
	}
      else
	{
	  bfd_vma r2off;

	  r2off = (htab->stub_group[stub_entry->target_section->id].toc_off
		   - htab->stub_group[stub_entry->id_sec->id].toc_off);
	  bfd_put_32 (htab->stub_bfd, STD_R2_40R1, loc);
	  loc += 4;
	  size = 20;
	  if (PPC_HA (indx) != 0)
	    {
	      size += 4;
	      bfd_put_32 (htab->stub_bfd, ADDIS_R12_R2 | PPC_HA (indx), loc);
	      loc += 4;
	      bfd_put_32 (htab->stub_bfd, LD_R11_0R12 | PPC_LO (indx), loc);
	      loc += 4;
	    }
	  else
	    {
	      bfd_put_32 (htab->stub_bfd, LD_R11_0R2 | PPC_LO (indx), loc);
	      loc += 4;
	    }

	  if (PPC_HA (r2off) != 0)
	    {
	      size += 4;
	      bfd_put_32 (htab->stub_bfd, ADDIS_R2_R2 | PPC_HA (r2off), loc);
	      loc += 4;
	    }
	  bfd_put_32 (htab->stub_bfd, ADDI_R2_R2 | PPC_LO (r2off), loc);
	}
      loc += 4;
      bfd_put_32 (htab->stub_bfd, MTCTR_R11, loc);
      loc += 4;
      bfd_put_32 (htab->stub_bfd, BCTR, loc);
      break;

    case ppc_stub_plt_call:
      /* Do the best we can for shared libraries built without
	 exporting ".foo" for each "foo".  This can happen when symbol
	 versioning scripts strip all bar a subset of symbols.  */
      if (stub_entry->h->oh != NULL
	  && stub_entry->h->oh->elf.root.type != bfd_link_hash_defined
	  && stub_entry->h->oh->elf.root.type != bfd_link_hash_defweak)
	{
	  /* Point the symbol at the stub.  There may be multiple stubs,
	     we don't really care;  The main thing is to make this sym
	     defined somewhere.  Maybe defining the symbol in the stub
	     section is a silly idea.  If we didn't do this, htab->top_id
	     could disappear.  */
	  stub_entry->h->oh->elf.root.type = bfd_link_hash_defined;
	  stub_entry->h->oh->elf.root.u.def.section = stub_entry->stub_sec;
	  stub_entry->h->oh->elf.root.u.def.value = stub_entry->stub_offset;
	}

      /* Now build the stub.  */
      off = (bfd_vma) -1;
      for (ent = stub_entry->h->elf.plt.plist; ent != NULL; ent = ent->next)
	if (ent->addend == stub_entry->addend)
	  {
	    off = ent->plt.offset;
	    break;
	  }
      if (off >= (bfd_vma) -2)
	abort ();

      off &= ~ (bfd_vma) 1;
      off += (htab->plt->output_offset
	      + htab->plt->output_section->vma
	      - elf_gp (htab->plt->output_section->owner)
	      - htab->stub_group[stub_entry->id_sec->id].toc_off);

      if (off + 0x80008000 > 0xffffffff || (off & 7) != 0)
	{
	  (*_bfd_error_handler)
	    (_("linkage table error against `%s'"),
	     stub_entry->h->elf.root.root.string);
	  bfd_set_error (bfd_error_bad_value);
	  htab->stub_error = TRUE;
	  return FALSE;
	}

      p = build_plt_stub (htab->stub_bfd, loc, off);
      size = p - loc;
      break;

    default:
      BFD_FAIL ();
      return FALSE;
    }

  stub_entry->stub_sec->size += size;

  if (htab->emit_stub_syms)
    {
      struct elf_link_hash_entry *h;
      size_t len1, len2;
      char *name;
      const char *const stub_str[] = { "long_branch",
				       "long_branch_r2off",
				       "plt_branch",
				       "plt_branch_r2off",
				       "plt_call" };

      len1 = strlen (stub_str[stub_entry->stub_type - 1]);
      len2 = strlen (stub_entry->root.string);
      name = bfd_malloc (len1 + len2 + 2);
      if (name == NULL)
	return FALSE;
      memcpy (name, stub_entry->root.string, 9);
      memcpy (name + 9, stub_str[stub_entry->stub_type - 1], len1);
      memcpy (name + len1 + 9, stub_entry->root.string + 8, len2 - 8 + 1);
      h = elf_link_hash_lookup (&htab->elf, name, TRUE, FALSE, FALSE);
      if (h == NULL)
	return FALSE;
      if (h->root.type == bfd_link_hash_new)
	{
	  h->root.type = bfd_link_hash_defined;
	  h->root.u.def.section = stub_entry->stub_sec;
	  h->root.u.def.value = stub_entry->stub_offset;
	  h->ref_regular = 1;
	  h->def_regular = 1;
	  h->ref_regular_nonweak = 1;
	  h->forced_local = 1;
	  h->non_elf = 0;
	}
    }

  return TRUE;
}

/* As above, but don't actually build the stub.  Just bump offset so
   we know stub section sizes, and select plt_branch stubs where
   long_branch stubs won't do.  */

static bfd_boolean
ppc_size_one_stub (struct bfd_hash_entry *gen_entry, void *in_arg)
{
  struct ppc_stub_hash_entry *stub_entry;
  struct bfd_link_info *info;
  struct ppc_link_hash_table *htab;
  bfd_vma off;
  int size;

  /* Massage our args to the form they really have.  */
  stub_entry = (struct ppc_stub_hash_entry *) gen_entry;
  info = in_arg;

  htab = ppc_hash_table (info);

  if (stub_entry->stub_type == ppc_stub_plt_call)
    {
      struct plt_entry *ent;
      off = (bfd_vma) -1;
      for (ent = stub_entry->h->elf.plt.plist; ent != NULL; ent = ent->next)
	if (ent->addend == stub_entry->addend)
	  {
	    off = ent->plt.offset & ~(bfd_vma) 1;
	    break;
	  }
      if (off >= (bfd_vma) -2)
	abort ();
      off += (htab->plt->output_offset
	      + htab->plt->output_section->vma
	      - elf_gp (htab->plt->output_section->owner)
	      - htab->stub_group[stub_entry->id_sec->id].toc_off);

      size = PLT_CALL_STUB_SIZE;
      if (PPC_HA (off) == 0)
	size -= 4;
      if (PPC_HA (off + 16) != PPC_HA (off))
	size += 4;
    }
  else
    {
      /* ppc_stub_long_branch or ppc_stub_plt_branch, or their r2off
	 variants.  */
      bfd_vma r2off = 0;

      off = (stub_entry->target_value
	     + stub_entry->target_section->output_offset
	     + stub_entry->target_section->output_section->vma);
      off -= (stub_entry->stub_sec->size
	      + stub_entry->stub_sec->output_offset
	      + stub_entry->stub_sec->output_section->vma);

      /* Reset the stub type from the plt variant in case we now
	 can reach with a shorter stub.  */
      if (stub_entry->stub_type >= ppc_stub_plt_branch)
	stub_entry->stub_type += ppc_stub_long_branch - ppc_stub_plt_branch;

      size = 4;
      if (stub_entry->stub_type == ppc_stub_long_branch_r2off)
	{
	  r2off = (htab->stub_group[stub_entry->target_section->id].toc_off
		   - htab->stub_group[stub_entry->id_sec->id].toc_off);
	  size = 12;
	  if (PPC_HA (r2off) != 0)
	    size = 16;
	  off -= size - 4;
	}

      /* If the branch offset if too big, use a ppc_stub_plt_branch.  */
      if (off + (1 << 25) >= (bfd_vma) (1 << 26))
	{
	  struct ppc_branch_hash_entry *br_entry;
	  unsigned int indx;

	  br_entry = ppc_branch_hash_lookup (&htab->branch_hash_table,
					     stub_entry->root.string + 9,
					     TRUE, FALSE);
	  if (br_entry == NULL)
	    {
	      (*_bfd_error_handler) (_("can't build branch stub `%s'"),
				     stub_entry->root.string);
	      htab->stub_error = TRUE;
	      return FALSE;
	    }

	  if (br_entry->iter != htab->stub_iteration)
	    {
	      br_entry->iter = htab->stub_iteration;
	      br_entry->offset = htab->brlt->size;
	      htab->brlt->size += 8;

	      if (htab->relbrlt != NULL)
		htab->relbrlt->size += sizeof (Elf64_External_Rela);
	      else if (info->emitrelocations)
		{
		  htab->brlt->reloc_count += 1;
		  htab->brlt->flags |= SEC_RELOC;
		}
	    }

	  stub_entry->stub_type += ppc_stub_plt_branch - ppc_stub_long_branch;
	  off = (br_entry->offset
		 + htab->brlt->output_offset
		 + htab->brlt->output_section->vma
		 - elf_gp (htab->brlt->output_section->owner)
		 - htab->stub_group[stub_entry->id_sec->id].toc_off);

	  indx = off;
	  if (stub_entry->stub_type != ppc_stub_plt_branch_r2off)
	    {
	      size = 12;
	      if (PPC_HA (indx) != 0)
		size = 16;
	    }
	  else
	    {
	      size = 20;
	      if (PPC_HA (indx) != 0)
		size += 4;

	      if (PPC_HA (r2off) != 0)
		size += 4;
	    }
	}
      else if (info->emitrelocations)
	{
	  stub_entry->stub_sec->reloc_count += 1;
	  stub_entry->stub_sec->flags |= SEC_RELOC;
	}
    }

  stub_entry->stub_sec->size += size;
  return TRUE;
}

/* Set up various things so that we can make a list of input sections
   for each output section included in the link.  Returns -1 on error,
   0 when no stubs will be needed, and 1 on success.  */

int
ppc64_elf_setup_section_lists (bfd *output_bfd,
			       struct bfd_link_info *info,
			       int no_multi_toc)
{
  bfd *input_bfd;
  int top_id, top_index, id;
  asection *section;
  asection **input_list;
  bfd_size_type amt;
  struct ppc_link_hash_table *htab = ppc_hash_table (info);

  htab->no_multi_toc = no_multi_toc;

  if (htab->brlt == NULL)
    return 0;

  /* Find the top input section id.  */
  for (input_bfd = info->input_bfds, top_id = 3;
       input_bfd != NULL;
       input_bfd = input_bfd->link_next)
    {
      for (section = input_bfd->sections;
	   section != NULL;
	   section = section->next)
	{
	  if (top_id < section->id)
	    top_id = section->id;
	}
    }

  htab->top_id = top_id;
  amt = sizeof (struct map_stub) * (top_id + 1);
  htab->stub_group = bfd_zmalloc (amt);
  if (htab->stub_group == NULL)
    return -1;

  /* Set toc_off for com, und, abs and ind sections.  */
  for (id = 0; id < 3; id++)
    htab->stub_group[id].toc_off = TOC_BASE_OFF;

  elf_gp (output_bfd) = htab->toc_curr = ppc64_elf_toc (output_bfd);

  /* We can't use output_bfd->section_count here to find the top output
     section index as some sections may have been removed, and
     strip_excluded_output_sections doesn't renumber the indices.  */
  for (section = output_bfd->sections, top_index = 0;
       section != NULL;
       section = section->next)
    {
      if (top_index < section->index)
	top_index = section->index;
    }

  htab->top_index = top_index;
  amt = sizeof (asection *) * (top_index + 1);
  input_list = bfd_zmalloc (amt);
  htab->input_list = input_list;
  if (input_list == NULL)
    return -1;

  return 1;
}

/* The linker repeatedly calls this function for each TOC input section
   and linker generated GOT section.  Group input bfds such that the toc
   within a group is less than 64k in size.  Will break with cute linker
   scripts that play games with dot in the output toc section.  */

void
ppc64_elf_next_toc_section (struct bfd_link_info *info, asection *isec)
{
  struct ppc_link_hash_table *htab = ppc_hash_table (info);

  if (!htab->no_multi_toc)
    {
      bfd_vma addr = isec->output_offset + isec->output_section->vma;
      bfd_vma off = addr - htab->toc_curr;

      if (off + isec->size > 0x10000)
	htab->toc_curr = addr;

      elf_gp (isec->owner) = (htab->toc_curr
			      - elf_gp (isec->output_section->owner)
			      + TOC_BASE_OFF);
    }
}

/* Called after the last call to the above function.  */

void
ppc64_elf_reinit_toc (bfd *output_bfd, struct bfd_link_info *info)
{
  struct ppc_link_hash_table *htab = ppc_hash_table (info);

  htab->multi_toc_needed = htab->toc_curr != elf_gp (output_bfd);

  /* toc_curr tracks the TOC offset used for code sections below in
     ppc64_elf_next_input_section.  Start off at 0x8000.  */
  htab->toc_curr = TOC_BASE_OFF;
}

/* No toc references were found in ISEC.  If the code in ISEC makes no
   calls, then there's no need to use toc adjusting stubs when branching
   into ISEC.  Actually, indirect calls from ISEC are OK as they will
   load r2.  Returns -1 on error, 0 for no stub needed, 1 for stub
   needed, and 2 if a cyclical call-graph was found but no other reason
   for a stub was detected.  If called from the top level, a return of
   2 means the same as a return of 0.  */

static int
toc_adjusting_stub_needed (struct bfd_link_info *info, asection *isec)
{
  Elf_Internal_Rela *relstart, *rel;
  Elf_Internal_Sym *local_syms;
  int ret;
  struct ppc_link_hash_table *htab;

  /* We know none of our code bearing sections will need toc stubs.  */
  if ((isec->flags & SEC_LINKER_CREATED) != 0)
    return 0;

  if (isec->size == 0)
    return 0;

  if (isec->output_section == NULL)
    return 0;

  /* Hack for linux kernel.  .fixup contains branches, but only back to
     the function that hit an exception.  */
  if (strcmp (isec->name, ".fixup") == 0)
    return 0;

  if (isec->reloc_count == 0)
    return 0;

  relstart = _bfd_elf_link_read_relocs (isec->owner, isec, NULL, NULL,
					info->keep_memory);
  if (relstart == NULL)
    return -1;

  /* Look for branches to outside of this section.  */
  local_syms = NULL;
  ret = 0;
  htab = ppc_hash_table (info);
  for (rel = relstart; rel < relstart + isec->reloc_count; ++rel)
    {
      enum elf_ppc64_reloc_type r_type;
      unsigned long r_symndx;
      struct elf_link_hash_entry *h;
      struct ppc_link_hash_entry *eh;
      Elf_Internal_Sym *sym;
      asection *sym_sec;
      long *opd_adjust;
      bfd_vma sym_value;
      bfd_vma dest;

      r_type = ELF64_R_TYPE (rel->r_info);
      if (r_type != R_PPC64_REL24
	  && r_type != R_PPC64_REL14
	  && r_type != R_PPC64_REL14_BRTAKEN
	  && r_type != R_PPC64_REL14_BRNTAKEN)
	continue;

      r_symndx = ELF64_R_SYM (rel->r_info);
      if (!get_sym_h (&h, &sym, &sym_sec, NULL, &local_syms, r_symndx,
		      isec->owner))
	{
	  ret = -1;
	  break;
	}

      /* Calls to dynamic lib functions go through a plt call stub
	 that uses r2.  */
      eh = (struct ppc_link_hash_entry *) h;
      if (eh != NULL
	  && (eh->elf.plt.plist != NULL
	      || (eh->oh != NULL
		  && eh->oh->elf.plt.plist != NULL)))
	{
	  ret = 1;
	  break;
	}

      if (sym_sec == NULL)
	/* Ignore other undefined symbols.  */
	continue;

      /* Assume branches to other sections not included in the link need
	 stubs too, to cover -R and absolute syms.  */
      if (sym_sec->output_section == NULL)
	{
	  ret = 1;
	  break;
	}

      if (h == NULL)
	sym_value = sym->st_value;
      else
	{
	  if (h->root.type != bfd_link_hash_defined
	      && h->root.type != bfd_link_hash_defweak)
	    abort ();
	  sym_value = h->root.u.def.value;
	}
      sym_value += rel->r_addend;

      /* If this branch reloc uses an opd sym, find the code section.  */
      opd_adjust = get_opd_info (sym_sec);
      if (opd_adjust != NULL)
	{
	  if (h == NULL)
	    {
	      long adjust;

	      adjust = opd_adjust[sym->st_value / 8];
	      if (adjust == -1)
		/* Assume deleted functions won't ever be called.  */
		continue;
	      sym_value += adjust;
	    }

	  dest = opd_entry_value (sym_sec, sym_value, &sym_sec, NULL);
	  if (dest == (bfd_vma) -1)
	    continue;
	}
      else
	dest = (sym_value
		+ sym_sec->output_offset
		+ sym_sec->output_section->vma);

      /* Ignore branch to self.  */
      if (sym_sec == isec)
	continue;

      /* If the called function uses the toc, we need a stub.  */
      if (sym_sec->has_toc_reloc
	  || sym_sec->makes_toc_func_call)
	{
	  ret = 1;
	  break;
	}

      /* Assume any branch that needs a long branch stub might in fact
	 need a plt_branch stub.  A plt_branch stub uses r2.  */
      else if (dest - (isec->output_offset
		       + isec->output_section->vma
		       + rel->r_offset) + (1 << 25) >= (2 << 25))
	{
	  ret = 1;
	  break;
	}

      /* If calling back to a section in the process of being tested, we
	 can't say for sure that no toc adjusting stubs are needed, so
	 don't return zero.  */
      else if (sym_sec->call_check_in_progress)
	ret = 2;

      /* Branches to another section that itself doesn't have any TOC
	 references are OK.  Recursively call ourselves to check.  */
      else if (sym_sec->id <= htab->top_id
	       && htab->stub_group[sym_sec->id].toc_off == 0)
	{
	  int recur;

	  /* Mark current section as indeterminate, so that other
	     sections that call back to current won't be marked as
	     known.  */
	  isec->call_check_in_progress = 1;
	  recur = toc_adjusting_stub_needed (info, sym_sec);
	  isec->call_check_in_progress = 0;

	  if (recur < 0)
	    {
	      /* An error.  Exit.  */
	      ret = -1;
	      break;
	    }
	  else if (recur <= 1)
	    {
	      /* Known result.  Mark as checked and set section flag.  */
	      htab->stub_group[sym_sec->id].toc_off = 1;
	      if (recur != 0)
		{
		  sym_sec->makes_toc_func_call = 1;
		  ret = 1;
		  break;
		}
	    }
	  else
	    {
	      /* Unknown result.  Continue checking.  */
	      ret = 2;
	    }
	}
    }

  if (local_syms != NULL
      && (elf_tdata (isec->owner)->symtab_hdr.contents
	  != (unsigned char *) local_syms))
    free (local_syms);
  if (elf_section_data (isec)->relocs != relstart)
    free (relstart);

  return ret;
}

/* The linker repeatedly calls this function for each input section,
   in the order that input sections are linked into output sections.
   Build lists of input sections to determine groupings between which
   we may insert linker stubs.  */

bfd_boolean
ppc64_elf_next_input_section (struct bfd_link_info *info, asection *isec)
{
  struct ppc_link_hash_table *htab = ppc_hash_table (info);

  if ((isec->output_section->flags & SEC_CODE) != 0
      && isec->output_section->index <= htab->top_index)
    {
      asection **list = htab->input_list + isec->output_section->index;
      /* Steal the link_sec pointer for our list.  */
#define PREV_SEC(sec) (htab->stub_group[(sec)->id].link_sec)
      /* This happens to make the list in reverse order,
	 which is what we want.  */
      PREV_SEC (isec) = *list;
      *list = isec;
    }

  if (htab->multi_toc_needed)
    {
      /* If a code section has a function that uses the TOC then we need
	 to use the right TOC (obviously).  Also, make sure that .opd gets
	 the correct TOC value for R_PPC64_TOC relocs that don't have or
	 can't find their function symbol (shouldn't ever happen now).  */
      if (isec->has_toc_reloc || (isec->flags & SEC_CODE) == 0)
	{
	  if (elf_gp (isec->owner) != 0)
	    htab->toc_curr = elf_gp (isec->owner);
	}
      else if (htab->stub_group[isec->id].toc_off == 0)
	{
	  int ret = toc_adjusting_stub_needed (info, isec);
	  if (ret < 0)
	    return FALSE;
	  else
	    isec->makes_toc_func_call = ret & 1;
	}
    }

  /* Functions that don't use the TOC can belong in any TOC group.
     Use the last TOC base.  This happens to make _init and _fini
     pasting work.  */
  htab->stub_group[isec->id].toc_off = htab->toc_curr;
  return TRUE;
}

/* See whether we can group stub sections together.  Grouping stub
   sections may result in fewer stubs.  More importantly, we need to
   put all .init* and .fini* stubs at the beginning of the .init or
   .fini output sections respectively, because glibc splits the
   _init and _fini functions into multiple parts.  Putting a stub in
   the middle of a function is not a good idea.  */

static void
group_sections (struct ppc_link_hash_table *htab,
		bfd_size_type stub_group_size,
		bfd_boolean stubs_always_before_branch)
{
  asection **list;
  bfd_size_type stub14_group_size;
  bfd_boolean suppress_size_errors;

  suppress_size_errors = FALSE;
  stub14_group_size = stub_group_size;
  if (stub_group_size == 1)
    {
      /* Default values.  */
      if (stubs_always_before_branch)
	{
	  stub_group_size = 0x1e00000;
	  stub14_group_size = 0x7800;
	}
      else
	{
	  stub_group_size = 0x1c00000;
	  stub14_group_size = 0x7000;
	}
      suppress_size_errors = TRUE;
    }

  list = htab->input_list + htab->top_index;
  do
    {
      asection *tail = *list;
      while (tail != NULL)
	{
	  asection *curr;
	  asection *prev;
	  bfd_size_type total;
	  bfd_boolean big_sec;
	  bfd_vma curr_toc;

	  curr = tail;
	  total = tail->size;
	  big_sec = total > (ppc64_elf_section_data (tail)->has_14bit_branch
			     ? stub14_group_size : stub_group_size);
	  if (big_sec && !suppress_size_errors)
	    (*_bfd_error_handler) (_("%B section %A exceeds stub group size"),
				     tail->owner, tail);
	  curr_toc = htab->stub_group[tail->id].toc_off;

	  while ((prev = PREV_SEC (curr)) != NULL
		 && ((total += curr->output_offset - prev->output_offset)
		     < (ppc64_elf_section_data (prev)->has_14bit_branch
			? stub14_group_size : stub_group_size))
		 && htab->stub_group[prev->id].toc_off == curr_toc)
	    curr = prev;

	  /* OK, the size from the start of CURR to the end is less
	     than stub_group_size and thus can be handled by one stub
	     section.  (or the tail section is itself larger than
	     stub_group_size, in which case we may be toast.)  We
	     should really be keeping track of the total size of stubs
	     added here, as stubs contribute to the final output
	     section size.  That's a little tricky, and this way will
	     only break if stubs added make the total size more than
	     2^25, ie. for the default stub_group_size, if stubs total
	     more than 2097152 bytes, or nearly 75000 plt call stubs.  */
	  do
	    {
	      prev = PREV_SEC (tail);
	      /* Set up this stub group.  */
	      htab->stub_group[tail->id].link_sec = curr;
	    }
	  while (tail != curr && (tail = prev) != NULL);

	  /* But wait, there's more!  Input sections up to stub_group_size
	     bytes before the stub section can be handled by it too.
	     Don't do this if we have a really large section after the
	     stubs, as adding more stubs increases the chance that
	     branches may not reach into the stub section.  */
	  if (!stubs_always_before_branch && !big_sec)
	    {
	      total = 0;
	      while (prev != NULL
		     && ((total += tail->output_offset - prev->output_offset)
			 < (ppc64_elf_section_data (prev)->has_14bit_branch
			    ? stub14_group_size : stub_group_size))
		     && htab->stub_group[prev->id].toc_off == curr_toc)
		{
		  tail = prev;
		  prev = PREV_SEC (tail);
		  htab->stub_group[tail->id].link_sec = curr;
		}
	    }
	  tail = prev;
	}
    }
  while (list-- != htab->input_list);
  free (htab->input_list);
#undef PREV_SEC
}

/* Determine and set the size of the stub section for a final link.

   The basic idea here is to examine all the relocations looking for
   PC-relative calls to a target that is unreachable with a "bl"
   instruction.  */

bfd_boolean
ppc64_elf_size_stubs (bfd *output_bfd,
		      struct bfd_link_info *info,
		      bfd_signed_vma group_size,
		      asection *(*add_stub_section) (const char *, asection *),
		      void (*layout_sections_again) (void))
{
  bfd_size_type stub_group_size;
  bfd_boolean stubs_always_before_branch;
  struct ppc_link_hash_table *htab = ppc_hash_table (info);

  /* Stash our params away.  */
  htab->add_stub_section = add_stub_section;
  htab->layout_sections_again = layout_sections_again;
  stubs_always_before_branch = group_size < 0;
  if (group_size < 0)
    stub_group_size = -group_size;
  else
    stub_group_size = group_size;

  group_sections (htab, stub_group_size, stubs_always_before_branch);

  while (1)
    {
      bfd *input_bfd;
      unsigned int bfd_indx;
      asection *stub_sec;

      htab->stub_iteration += 1;

      for (input_bfd = info->input_bfds, bfd_indx = 0;
	   input_bfd != NULL;
	   input_bfd = input_bfd->link_next, bfd_indx++)
	{
	  Elf_Internal_Shdr *symtab_hdr;
	  asection *section;
	  Elf_Internal_Sym *local_syms = NULL;

	  if (!is_ppc64_elf_target (input_bfd->xvec))
	    continue;

	  /* We'll need the symbol table in a second.  */
	  symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
	  if (symtab_hdr->sh_info == 0)
	    continue;

	  /* Walk over each section attached to the input bfd.  */
	  for (section = input_bfd->sections;
	       section != NULL;
	       section = section->next)
	    {
	      Elf_Internal_Rela *internal_relocs, *irelaend, *irela;

	      /* If there aren't any relocs, then there's nothing more
		 to do.  */
	      if ((section->flags & SEC_RELOC) == 0
		  || (section->flags & SEC_ALLOC) == 0
		  || (section->flags & SEC_LOAD) == 0
		  || (section->flags & SEC_CODE) == 0
		  || section->reloc_count == 0)
		continue;

	      /* If this section is a link-once section that will be
		 discarded, then don't create any stubs.  */
	      if (section->output_section == NULL
		  || section->output_section->owner != output_bfd)
		continue;

	      /* Get the relocs.  */
	      internal_relocs
		= _bfd_elf_link_read_relocs (input_bfd, section, NULL, NULL,
					     info->keep_memory);
	      if (internal_relocs == NULL)
		goto error_ret_free_local;

	      /* Now examine each relocation.  */
	      irela = internal_relocs;
	      irelaend = irela + section->reloc_count;
	      for (; irela < irelaend; irela++)
		{
		  enum elf_ppc64_reloc_type r_type;
		  unsigned int r_indx;
		  enum ppc_stub_type stub_type;
		  struct ppc_stub_hash_entry *stub_entry;
		  asection *sym_sec, *code_sec;
		  bfd_vma sym_value;
		  bfd_vma destination;
		  bfd_boolean ok_dest;
		  struct ppc_link_hash_entry *hash;
		  struct ppc_link_hash_entry *fdh;
		  struct elf_link_hash_entry *h;
		  Elf_Internal_Sym *sym;
		  char *stub_name;
		  const asection *id_sec;
		  long *opd_adjust;

		  r_type = ELF64_R_TYPE (irela->r_info);
		  r_indx = ELF64_R_SYM (irela->r_info);

		  if (r_type >= R_PPC64_max)
		    {
		      bfd_set_error (bfd_error_bad_value);
		      goto error_ret_free_internal;
		    }

		  /* Only look for stubs on branch instructions.  */
		  if (r_type != R_PPC64_REL24
		      && r_type != R_PPC64_REL14
		      && r_type != R_PPC64_REL14_BRTAKEN
		      && r_type != R_PPC64_REL14_BRNTAKEN)
		    continue;

		  /* Now determine the call target, its name, value,
		     section.  */
		  if (!get_sym_h (&h, &sym, &sym_sec, NULL, &local_syms,
				  r_indx, input_bfd))
		    goto error_ret_free_internal;
		  hash = (struct ppc_link_hash_entry *) h;

		  ok_dest = FALSE;
		  fdh = NULL;
		  sym_value = 0;
		  if (hash == NULL)
		    {
		      sym_value = sym->st_value;
		      ok_dest = TRUE;
		    }
		  else if (hash->elf.root.type == bfd_link_hash_defined
			   || hash->elf.root.type == bfd_link_hash_defweak)
		    {
		      sym_value = hash->elf.root.u.def.value;
		      if (sym_sec->output_section != NULL)
			ok_dest = TRUE;
		    }
		  else if (hash->elf.root.type == bfd_link_hash_undefweak
			   || hash->elf.root.type == bfd_link_hash_undefined)
		    {
		      /* Recognise an old ABI func code entry sym, and
			 use the func descriptor sym instead if it is
			 defined.  */
		      if (hash->elf.root.root.string[0] == '.'
			  && (fdh = get_fdh (hash, htab)) != NULL)
			{
			  if (fdh->elf.root.type == bfd_link_hash_defined
			      || fdh->elf.root.type == bfd_link_hash_defweak)
			    {
			      sym_sec = fdh->elf.root.u.def.section;
			      sym_value = fdh->elf.root.u.def.value;
			      if (sym_sec->output_section != NULL)
				ok_dest = TRUE;
			    }
			  else
			    fdh = NULL;
			}
		    }
		  else
		    {
		      bfd_set_error (bfd_error_bad_value);
		      goto error_ret_free_internal;
		    }

		  destination = 0;
		  if (ok_dest)
		    {
		      sym_value += irela->r_addend;
		      destination = (sym_value
				     + sym_sec->output_offset
				     + sym_sec->output_section->vma);
		    }

		  code_sec = sym_sec;
		  opd_adjust = get_opd_info (sym_sec);
		  if (opd_adjust != NULL)
		    {
		      bfd_vma dest;

		      if (hash == NULL)
			{
			  long adjust = opd_adjust[sym_value / 8];
			  if (adjust == -1)
			    continue;
			  sym_value += adjust;
			}
		      dest = opd_entry_value (sym_sec, sym_value,
					      &code_sec, &sym_value);
		      if (dest != (bfd_vma) -1)
			{
			  destination = dest;
			  if (fdh != NULL)
			    {
			      /* Fixup old ABI sym to point at code
				 entry.  */
			      hash->elf.root.type = bfd_link_hash_defweak;
			      hash->elf.root.u.def.section = code_sec;
			      hash->elf.root.u.def.value = sym_value;
			    }
			}
		    }

		  /* Determine what (if any) linker stub is needed.  */
		  stub_type = ppc_type_of_stub (section, irela, &hash,
						destination);

		  if (stub_type != ppc_stub_plt_call)
		    {
		      /* Check whether we need a TOC adjusting stub.
			 Since the linker pastes together pieces from
			 different object files when creating the
			 _init and _fini functions, it may be that a
			 call to what looks like a local sym is in
			 fact a call needing a TOC adjustment.  */
		      if (code_sec != NULL
			  && code_sec->output_section != NULL
			  && (htab->stub_group[code_sec->id].toc_off
			      != htab->stub_group[section->id].toc_off)
			  && (code_sec->has_toc_reloc
			      || code_sec->makes_toc_func_call))
			stub_type = ppc_stub_long_branch_r2off;
		    }

		  if (stub_type == ppc_stub_none)
		    continue;

		  /* __tls_get_addr calls might be eliminated.  */
		  if (stub_type != ppc_stub_plt_call
		      && hash != NULL
		      && (hash == htab->tls_get_addr
			  || hash == htab->tls_get_addr_fd)
		      && section->has_tls_reloc
		      && irela != internal_relocs)
		    {
		      /* Get tls info.  */
		      char *tls_mask;

		      if (!get_tls_mask (&tls_mask, NULL, NULL, &local_syms,
					 irela - 1, input_bfd))
			goto error_ret_free_internal;
		      if (*tls_mask != 0)
			continue;
		    }

		  /* Support for grouping stub sections.  */
		  id_sec = htab->stub_group[section->id].link_sec;

		  /* Get the name of this stub.  */
		  stub_name = ppc_stub_name (id_sec, sym_sec, hash, irela);
		  if (!stub_name)
		    goto error_ret_free_internal;

		  stub_entry = ppc_stub_hash_lookup (&htab->stub_hash_table,
						     stub_name, FALSE, FALSE);
		  if (stub_entry != NULL)
		    {
		      /* The proper stub has already been created.  */
		      free (stub_name);
		      continue;
		    }

		  stub_entry = ppc_add_stub (stub_name, section, htab);
		  if (stub_entry == NULL)
		    {
		      free (stub_name);
		    error_ret_free_internal:
		      if (elf_section_data (section)->relocs == NULL)
			free (internal_relocs);
		    error_ret_free_local:
		      if (local_syms != NULL
			  && (symtab_hdr->contents
			      != (unsigned char *) local_syms))
			free (local_syms);
		      return FALSE;
		    }

		  stub_entry->stub_type = stub_type;
		  stub_entry->target_value = sym_value;
		  stub_entry->target_section = code_sec;
		  stub_entry->h = hash;
		  stub_entry->addend = irela->r_addend;

		  if (stub_entry->h != NULL)
		    htab->stub_globals += 1;
		}

	      /* We're done with the internal relocs, free them.  */
	      if (elf_section_data (section)->relocs != internal_relocs)
		free (internal_relocs);
	    }

	  if (local_syms != NULL
	      && symtab_hdr->contents != (unsigned char *) local_syms)
	    {
	      if (!info->keep_memory)
		free (local_syms);
	      else
		symtab_hdr->contents = (unsigned char *) local_syms;
	    }
	}

      /* We may have added some stubs.  Find out the new size of the
	 stub sections.  */
      for (stub_sec = htab->stub_bfd->sections;
	   stub_sec != NULL;
	   stub_sec = stub_sec->next)
	if ((stub_sec->flags & SEC_LINKER_CREATED) == 0)
	  {
	    stub_sec->rawsize = stub_sec->size;
	    stub_sec->size = 0;
	    stub_sec->reloc_count = 0;
	    stub_sec->flags &= ~SEC_RELOC;
	  }

      htab->brlt->size = 0;
      htab->brlt->reloc_count = 0;
      htab->brlt->flags &= ~SEC_RELOC;
      if (htab->relbrlt != NULL)
	htab->relbrlt->size = 0;

      bfd_hash_traverse (&htab->stub_hash_table, ppc_size_one_stub, info);

      for (stub_sec = htab->stub_bfd->sections;
	   stub_sec != NULL;
	   stub_sec = stub_sec->next)
	if ((stub_sec->flags & SEC_LINKER_CREATED) == 0
	    && stub_sec->rawsize != stub_sec->size)
	  break;

      /* Exit from this loop when no stubs have been added, and no stubs
	 have changed size.  */
      if (stub_sec == NULL)
	break;

      /* Ask the linker to do its stuff.  */
      (*htab->layout_sections_again) ();
    }

  /* It would be nice to strip htab->brlt from the output if the
     section is empty, but it's too late.  If we strip sections here,
     the dynamic symbol table is corrupted since the section symbol
     for the stripped section isn't written.  */

  return TRUE;
}

/* Called after we have determined section placement.  If sections
   move, we'll be called again.  Provide a value for TOCstart.  */

bfd_vma
ppc64_elf_toc (bfd *obfd)
{
  asection *s;
  bfd_vma TOCstart;

  /* The TOC consists of sections .got, .toc, .tocbss, .plt in that
     order.  The TOC starts where the first of these sections starts.  */
  s = bfd_get_section_by_name (obfd, ".got");
  if (s == NULL)
    s = bfd_get_section_by_name (obfd, ".toc");
  if (s == NULL)
    s = bfd_get_section_by_name (obfd, ".tocbss");
  if (s == NULL)
    s = bfd_get_section_by_name (obfd, ".plt");
  if (s == NULL)
    {
      /* This may happen for
	 o  references to TOC base (SYM@toc / TOC[tc0]) without a
	 .toc directive
	 o  bad linker script
	 o --gc-sections and empty TOC sections

	 FIXME: Warn user?  */

      /* Look for a likely section.  We probably won't even be
	 using TOCstart.  */
      for (s = obfd->sections; s != NULL; s = s->next)
	if ((s->flags & (SEC_ALLOC | SEC_SMALL_DATA | SEC_READONLY))
	    == (SEC_ALLOC | SEC_SMALL_DATA))
	  break;
      if (s == NULL)
	for (s = obfd->sections; s != NULL; s = s->next)
	  if ((s->flags & (SEC_ALLOC | SEC_SMALL_DATA))
	      == (SEC_ALLOC | SEC_SMALL_DATA))
	    break;
      if (s == NULL)
	for (s = obfd->sections; s != NULL; s = s->next)
	  if ((s->flags & (SEC_ALLOC | SEC_READONLY)) == SEC_ALLOC)
	    break;
      if (s == NULL)
	for (s = obfd->sections; s != NULL; s = s->next)
	  if ((s->flags & SEC_ALLOC) == SEC_ALLOC)
	    break;
    }

  TOCstart = 0;
  if (s != NULL)
    TOCstart = s->output_section->vma + s->output_offset;

  return TOCstart;
}

/* Build all the stubs associated with the current output file.
   The stubs are kept in a hash table attached to the main linker
   hash table.  This function is called via gldelf64ppc_finish.  */

bfd_boolean
ppc64_elf_build_stubs (bfd_boolean emit_stub_syms,
		       struct bfd_link_info *info,
		       char **stats)
{
  struct ppc_link_hash_table *htab = ppc_hash_table (info);
  asection *stub_sec;
  bfd_byte *p;
  int stub_sec_count = 0;

  htab->emit_stub_syms = emit_stub_syms;

  /* Allocate memory to hold the linker stubs.  */
  for (stub_sec = htab->stub_bfd->sections;
       stub_sec != NULL;
       stub_sec = stub_sec->next)
    if ((stub_sec->flags & SEC_LINKER_CREATED) == 0
	&& stub_sec->size != 0)
      {
	stub_sec->contents = bfd_zalloc (htab->stub_bfd, stub_sec->size);
	if (stub_sec->contents == NULL)
	  return FALSE;
	/* We want to check that built size is the same as calculated
	   size.  rawsize is a convenient location to use.  */
	stub_sec->rawsize = stub_sec->size;
	stub_sec->size = 0;
      }

  if (htab->glink != NULL && htab->glink->size != 0)
    {
      unsigned int indx;
      bfd_vma plt0;

      /* Build the .glink plt call stub.  */
      if (htab->emit_stub_syms)
	{
	  struct elf_link_hash_entry *h;
	  h = elf_link_hash_lookup (&htab->elf, "__glink", TRUE, FALSE, FALSE);
	  if (h == NULL)
	    return FALSE;
	  if (h->root.type == bfd_link_hash_new)
	    {
	      h->root.type = bfd_link_hash_defined;
	      h->root.u.def.section = htab->glink;
	      h->root.u.def.value = 8;
	      h->ref_regular = 1;
	      h->def_regular = 1;
	      h->ref_regular_nonweak = 1;
	      h->forced_local = 1;
	      h->non_elf = 0;
	    }
	}
      p = htab->glink->contents;
      plt0 = (htab->plt->output_section->vma
	      + htab->plt->output_offset
	      - (htab->glink->output_section->vma
		 + htab->glink->output_offset
		 + 16));
      bfd_put_64 (htab->glink->owner, plt0, p);
      p += 8;
      bfd_put_32 (htab->glink->owner, MFLR_R12, p);
      p += 4;
      bfd_put_32 (htab->glink->owner, BCL_20_31, p);
      p += 4;
      bfd_put_32 (htab->glink->owner, MFLR_R11, p);
      p += 4;
      bfd_put_32 (htab->glink->owner, LD_R2_M16R11, p);
      p += 4;
      bfd_put_32 (htab->glink->owner, MTLR_R12, p);
      p += 4;
      bfd_put_32 (htab->glink->owner, ADD_R12_R2_R11, p);
      p += 4;
      bfd_put_32 (htab->glink->owner, LD_R11_0R12, p);
      p += 4;
      bfd_put_32 (htab->glink->owner, LD_R2_0R12 | 8, p);
      p += 4;
      bfd_put_32 (htab->glink->owner, MTCTR_R11, p);
      p += 4;
      bfd_put_32 (htab->glink->owner, LD_R11_0R12 | 16, p);
      p += 4;
      bfd_put_32 (htab->glink->owner, BCTR, p);
      p += 4;
      while (p - htab->glink->contents < GLINK_CALL_STUB_SIZE)
	{
	  bfd_put_32 (htab->glink->owner, NOP, p);
	  p += 4;
	}

      /* Build the .glink lazy link call stubs.  */
      indx = 0;
      while (p < htab->glink->contents + htab->glink->size)
	{
	  if (indx < 0x8000)
	    {
	      bfd_put_32 (htab->glink->owner, LI_R0_0 | indx, p);
	      p += 4;
	    }
	  else
	    {
	      bfd_put_32 (htab->glink->owner, LIS_R0_0 | PPC_HI (indx), p);
	      p += 4;
	      bfd_put_32 (htab->glink->owner, ORI_R0_R0_0 | PPC_LO (indx), p);
	      p += 4;
	    }
	  bfd_put_32 (htab->glink->owner,
		      B_DOT | ((htab->glink->contents - p + 8) & 0x3fffffc), p);
	  indx++;
	  p += 4;
	}
      htab->glink->rawsize = p - htab->glink->contents;
    }

  if (htab->brlt->size != 0)
    {
      htab->brlt->contents = bfd_zalloc (htab->brlt->owner,
					 htab->brlt->size);
      if (htab->brlt->contents == NULL)
	return FALSE;
    }
  if (htab->relbrlt != NULL && htab->relbrlt->size != 0)
    {
      htab->relbrlt->contents = bfd_zalloc (htab->relbrlt->owner,
					    htab->relbrlt->size);
      if (htab->relbrlt->contents == NULL)
	return FALSE;
    }

  /* Build the stubs as directed by the stub hash table.  */
  bfd_hash_traverse (&htab->stub_hash_table, ppc_build_one_stub, info);

  if (htab->relbrlt != NULL)
    htab->relbrlt->reloc_count = 0;

  for (stub_sec = htab->stub_bfd->sections;
       stub_sec != NULL;
       stub_sec = stub_sec->next)
    if ((stub_sec->flags & SEC_LINKER_CREATED) == 0)
      {
	stub_sec_count += 1;
	if (stub_sec->rawsize != stub_sec->size)
	  break;
      }

  if (stub_sec != NULL
      || htab->glink->rawsize != htab->glink->size)
    {
      htab->stub_error = TRUE;
      (*_bfd_error_handler) (_("stubs don't match calculated size"));
    }

  if (htab->stub_error)
    return FALSE;

  if (stats != NULL)
    {
      *stats = bfd_malloc (500);
      if (*stats == NULL)
	return FALSE;

      sprintf (*stats, _("linker stubs in %u group%s\n"
			 "  branch       %lu\n"
			 "  toc adjust   %lu\n"
			 "  long branch  %lu\n"
			 "  long toc adj %lu\n"
			 "  plt call     %lu"),
	       stub_sec_count,
	       stub_sec_count == 1 ? "" : "s",
	       htab->stub_count[ppc_stub_long_branch - 1],
	       htab->stub_count[ppc_stub_long_branch_r2off - 1],
	       htab->stub_count[ppc_stub_plt_branch - 1],
	       htab->stub_count[ppc_stub_plt_branch_r2off - 1],
	       htab->stub_count[ppc_stub_plt_call - 1]);
    }
  return TRUE;
}

/* This function undoes the changes made by add_symbol_adjust.  */

static bfd_boolean
undo_symbol_twiddle (struct elf_link_hash_entry *h, void *inf ATTRIBUTE_UNUSED)
{
  struct ppc_link_hash_entry *eh;

  if (h->root.type == bfd_link_hash_indirect)
    return TRUE;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  eh = (struct ppc_link_hash_entry *) h;
  if (eh->elf.root.type != bfd_link_hash_undefweak || !eh->was_undefined)
    return TRUE;

  eh->elf.root.type = bfd_link_hash_undefined;
  return TRUE;
}

void
ppc64_elf_restore_symbols (struct bfd_link_info *info)
{
  struct ppc_link_hash_table *htab = ppc_hash_table (info);
  elf_link_hash_traverse (&htab->elf, undo_symbol_twiddle, info);
}

/* What to do when ld finds relocations against symbols defined in
   discarded sections.  */

static unsigned int
ppc64_elf_action_discarded (asection *sec)
{
  if (strcmp (".opd", sec->name) == 0)
    return 0;

  if (strcmp (".toc", sec->name) == 0)
    return 0;

  if (strcmp (".toc1", sec->name) == 0)
    return 0;

  if (sec->flags & SEC_RELOC)
    return 0;

  return _bfd_elf_default_action_discarded (sec);
}

/* The RELOCATE_SECTION function is called by the ELF backend linker
   to handle the relocations for a section.

   The relocs are always passed as Rela structures; if the section
   actually uses Rel structures, the r_addend field will always be
   zero.

   This function is responsible for adjust the section contents as
   necessary, and (if using Rela relocs and generating a
   relocatable output file) adjusting the reloc addend as
   necessary.

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
ppc64_elf_relocate_section (bfd *output_bfd,
			    struct bfd_link_info *info,
			    bfd *input_bfd,
			    asection *input_section,
			    bfd_byte *contents,
			    Elf_Internal_Rela *relocs,
			    Elf_Internal_Sym *local_syms,
			    asection **local_sections)
{
  struct ppc_link_hash_table *htab;
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  Elf_Internal_Rela *rel;
  Elf_Internal_Rela *relend;
  Elf_Internal_Rela outrel;
  bfd_byte *loc;
  struct got_entry **local_got_ents;
  bfd_vma TOCstart;
  bfd_boolean ret = TRUE;
  bfd_boolean is_opd;
  /* Disabled until we sort out how ld should choose 'y' vs 'at'.  */
  bfd_boolean is_power4 = FALSE;
  bfd_vma d_offset = (bfd_big_endian (output_bfd) ? 2 : 0);

  /* Initialize howto table if needed.  */
  if (!ppc64_elf_howto_table[R_PPC64_ADDR32])
    ppc_howto_init ();

  htab = ppc_hash_table (info);

  /* Don't relocate stub sections.  */
  if (input_section->owner == htab->stub_bfd)
    return TRUE;

  local_got_ents = elf_local_got_ents (input_bfd);
  TOCstart = elf_gp (output_bfd);
  symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (input_bfd);
  is_opd = ppc64_elf_section_data (input_section)->sec_type == sec_opd;

  rel = relocs;
  relend = relocs + input_section->reloc_count;
  for (; rel < relend; rel++)
    {
      enum elf_ppc64_reloc_type r_type;
      bfd_vma addend, orig_addend;
      bfd_reloc_status_type r;
      Elf_Internal_Sym *sym;
      asection *sec;
      struct elf_link_hash_entry *h_elf;
      struct ppc_link_hash_entry *h;
      struct ppc_link_hash_entry *fdh;
      const char *sym_name;
      unsigned long r_symndx, toc_symndx;
      bfd_vma toc_addend;
      char tls_mask, tls_gd, tls_type;
      char sym_type;
      bfd_vma relocation;
      bfd_boolean unresolved_reloc;
      bfd_boolean warned;
      unsigned long insn, mask;
      struct ppc_stub_hash_entry *stub_entry;
      bfd_vma max_br_offset;
      bfd_vma from;

      r_type = ELF64_R_TYPE (rel->r_info);
      r_symndx = ELF64_R_SYM (rel->r_info);

      /* For old style R_PPC64_TOC relocs with a zero symbol, use the
	 symbol of the previous ADDR64 reloc.  The symbol gives us the
	 proper TOC base to use.  */
      if (rel->r_info == ELF64_R_INFO (0, R_PPC64_TOC)
	  && rel != relocs
	  && ELF64_R_TYPE (rel[-1].r_info) == R_PPC64_ADDR64
	  && is_opd)
	r_symndx = ELF64_R_SYM (rel[-1].r_info);

      sym = NULL;
      sec = NULL;
      h_elf = NULL;
      sym_name = NULL;
      unresolved_reloc = FALSE;
      warned = FALSE;
      orig_addend = rel->r_addend;

      if (r_symndx < symtab_hdr->sh_info)
	{
	  /* It's a local symbol.  */
	  long *opd_adjust;

	  sym = local_syms + r_symndx;
	  sec = local_sections[r_symndx];
	  sym_name = bfd_elf_sym_name (input_bfd, symtab_hdr, sym, sec);
	  sym_type = ELF64_ST_TYPE (sym->st_info);
	  relocation = _bfd_elf_rela_local_sym (output_bfd, sym, &sec, rel);
	  opd_adjust = get_opd_info (sec);
	  if (opd_adjust != NULL)
	    {
	      long adjust = opd_adjust[(sym->st_value + rel->r_addend) / 8];
	      if (adjust == -1)
		relocation = 0;
	      else
		{
		  /* If this is a relocation against the opd section sym
		     and we have edited .opd, adjust the reloc addend so
		     that ld -r and ld --emit-relocs output is correct.
		     If it is a reloc against some other .opd symbol,
		     then the symbol value will be adjusted later.  */
		  if (ELF_ST_TYPE (sym->st_info) == STT_SECTION)
		    rel->r_addend += adjust;
		  else
		    relocation += adjust;
		}
	    }
	}
      else
	{
	  RELOC_FOR_GLOBAL_SYMBOL (info, input_bfd, input_section, rel,
				   r_symndx, symtab_hdr, sym_hashes,
				   h_elf, sec, relocation,
				   unresolved_reloc, warned);
	  sym_name = h_elf->root.root.string;
	  sym_type = h_elf->type;
	}
      h = (struct ppc_link_hash_entry *) h_elf;

      if (sec != NULL && elf_discarded_section (sec))
	{
	  /* For relocs against symbols from removed linkonce sections,
	     or sections discarded by a linker script, we just want the
	     section contents zeroed.  Avoid any special processing.  */
	  _bfd_clear_contents (ppc64_elf_howto_table[r_type], input_bfd,
			       contents + rel->r_offset);
	  rel->r_info = 0;
	  rel->r_addend = 0;
	  continue;
	}

      if (info->relocatable)
	continue;

      /* TLS optimizations.  Replace instruction sequences and relocs
	 based on information we collected in tls_optimize.  We edit
	 RELOCS so that --emit-relocs will output something sensible
	 for the final instruction stream.  */
      tls_mask = 0;
      tls_gd = 0;
      toc_symndx = 0;
      if (h != NULL)
	tls_mask = h->tls_mask;
      else if (local_got_ents != NULL)
	{
	  char *lgot_masks;
	  lgot_masks = (char *) (local_got_ents + symtab_hdr->sh_info);
	  tls_mask = lgot_masks[r_symndx];
	}
      if (tls_mask == 0
	  && (r_type == R_PPC64_TLS
	      || r_type == R_PPC64_TLSGD
	      || r_type == R_PPC64_TLSLD))
	{
	  /* Check for toc tls entries.  */
	  char *toc_tls;

	  if (!get_tls_mask (&toc_tls, &toc_symndx, &toc_addend,
			     &local_syms, rel, input_bfd))
	    return FALSE;

	  if (toc_tls)
	    tls_mask = *toc_tls;
	}

      /* Check that tls relocs are used with tls syms, and non-tls
	 relocs are used with non-tls syms.  */
      if (r_symndx != 0
	  && r_type != R_PPC64_NONE
	  && (h == NULL
	      || h->elf.root.type == bfd_link_hash_defined
	      || h->elf.root.type == bfd_link_hash_defweak)
	  && (IS_PPC64_TLS_RELOC (r_type)
	      != (sym_type == STT_TLS
		  || (sym_type == STT_SECTION
		      && (sec->flags & SEC_THREAD_LOCAL) != 0))))
	{
	  if (tls_mask != 0
	      && (r_type == R_PPC64_TLS
		  || r_type == R_PPC64_TLSGD
		  || r_type == R_PPC64_TLSLD))
	    /* R_PPC64_TLS is OK against a symbol in the TOC.  */
	    ;
	  else
	    (*_bfd_error_handler)
	      (!IS_PPC64_TLS_RELOC (r_type)
	       ? _("%B(%A+0x%lx): %s used with TLS symbol %s")
	       : _("%B(%A+0x%lx): %s used with non-TLS symbol %s"),
	       input_bfd,
	       input_section,
	       (long) rel->r_offset,
	       ppc64_elf_howto_table[r_type]->name,
	       sym_name);
	}

      /* Ensure reloc mapping code below stays sane.  */
      if (R_PPC64_TOC16_LO_DS != R_PPC64_TOC16_DS + 1
	  || R_PPC64_TOC16_LO != R_PPC64_TOC16 + 1
	  || (R_PPC64_GOT_TLSLD16 & 3)    != (R_PPC64_GOT_TLSGD16 & 3)
	  || (R_PPC64_GOT_TLSLD16_LO & 3) != (R_PPC64_GOT_TLSGD16_LO & 3)
	  || (R_PPC64_GOT_TLSLD16_HI & 3) != (R_PPC64_GOT_TLSGD16_HI & 3)
	  || (R_PPC64_GOT_TLSLD16_HA & 3) != (R_PPC64_GOT_TLSGD16_HA & 3)
	  || (R_PPC64_GOT_TLSLD16 & 3)    != (R_PPC64_GOT_TPREL16_DS & 3)
	  || (R_PPC64_GOT_TLSLD16_LO & 3) != (R_PPC64_GOT_TPREL16_LO_DS & 3)
	  || (R_PPC64_GOT_TLSLD16_HI & 3) != (R_PPC64_GOT_TPREL16_HI & 3)
	  || (R_PPC64_GOT_TLSLD16_HA & 3) != (R_PPC64_GOT_TPREL16_HA & 3))
	abort ();

      switch (r_type)
	{
	default:
	  break;

	case R_PPC64_TOC16:
	case R_PPC64_TOC16_LO:
	case R_PPC64_TOC16_DS:
	case R_PPC64_TOC16_LO_DS:
	  {
	    /* Check for toc tls entries.  */
	    char *toc_tls;
	    int retval;

	    retval = get_tls_mask (&toc_tls, &toc_symndx, &toc_addend,
				   &local_syms, rel, input_bfd);
	    if (retval == 0)
	      return FALSE;

	    if (toc_tls)
	      {
		tls_mask = *toc_tls;
		if (r_type == R_PPC64_TOC16_DS
		    || r_type == R_PPC64_TOC16_LO_DS)
		  {
		    if (tls_mask != 0
			&& (tls_mask & (TLS_DTPREL | TLS_TPREL)) == 0)
		      goto toctprel;
		  }
		else
		  {
		    /* If we found a GD reloc pair, then we might be
		       doing a GD->IE transition.  */
		    if (retval == 2)
		      {
			tls_gd = TLS_TPRELGD;
			if (tls_mask != 0 && (tls_mask & TLS_GD) == 0)
			  goto tls_ldgd_opt;
		      }
		    else if (retval == 3)
		      {
			if (tls_mask != 0 && (tls_mask & TLS_LD) == 0)
			  goto tls_ldgd_opt;
		      }
		  }
	      }
	  }
	  break;

	case R_PPC64_GOT_TPREL16_DS:
	case R_PPC64_GOT_TPREL16_LO_DS:
	  if (tls_mask != 0
	      && (tls_mask & TLS_TPREL) == 0)
	    {
	    toctprel:
	      insn = bfd_get_32 (output_bfd, contents + rel->r_offset - d_offset);
	      insn &= 31 << 21;
	      insn |= 0x3c0d0000;	/* addis 0,13,0 */
	      bfd_put_32 (output_bfd, insn, contents + rel->r_offset - d_offset);
	      r_type = R_PPC64_TPREL16_HA;
	      if (toc_symndx != 0)
		{
		  rel->r_info = ELF64_R_INFO (toc_symndx, r_type);
		  /* We changed the symbol.  Start over in order to
		     get h, sym, sec etc. right.  */
		  rel--;
		  continue;
		}
	      else
		rel->r_info = ELF64_R_INFO (r_symndx, r_type);
	    }
	  break;

	case R_PPC64_TLS:
	  if (tls_mask != 0
	      && (tls_mask & TLS_TPREL) == 0)
	    {
	      bfd_vma rtra;
	      insn = bfd_get_32 (output_bfd, contents + rel->r_offset);
	      if ((insn & ((0x3f << 26) | (31 << 11)))
		  == ((31 << 26) | (13 << 11)))
		rtra = insn & ((1 << 26) - (1 << 16));
	      else if ((insn & ((0x3f << 26) | (31 << 16)))
		       == ((31 << 26) | (13 << 16)))
		rtra = (insn & (31 << 21)) | ((insn & (31 << 11)) << 5);
	      else
		abort ();
	      if ((insn & ((1 << 11) - (1 << 1))) == 266 << 1)
		/* add -> addi.  */
		insn = 14 << 26;
	      else if ((insn & (31 << 1)) == 23 << 1
		       && ((insn & (31 << 6)) < 14 << 6
			   || ((insn & (31 << 6)) >= 16 << 6
			       && (insn & (31 << 6)) < 24 << 6)))
		/* load and store indexed -> dform.  */
		insn = (32 | ((insn >> 6) & 31)) << 26;
	      else if ((insn & (31 << 1)) == 21 << 1
		       && (insn & (0x1a << 6)) == 0)
		/* ldx, ldux, stdx, stdux -> ld, ldu, std, stdu.  */
		insn = (((58 | ((insn >> 6) & 4)) << 26)
			| ((insn >> 6) & 1));
	      else if ((insn & (31 << 1)) == 21 << 1
		       && (insn & ((1 << 11) - (1 << 1))) == 341 << 1)
		/* lwax -> lwa.  */
		insn = (58 << 26) | 2;
	      else
		abort ();
	      insn |= rtra;
	      bfd_put_32 (output_bfd, insn, contents + rel->r_offset);
	      /* Was PPC64_TLS which sits on insn boundary, now
		 PPC64_TPREL16_LO which is at low-order half-word.  */
	      rel->r_offset += d_offset;
	      r_type = R_PPC64_TPREL16_LO;
	      if (toc_symndx != 0)
		{
		  rel->r_info = ELF64_R_INFO (toc_symndx, r_type);
		  rel->r_addend = toc_addend;
		  rel->r_addend = toc_addend;
		  /* We changed the symbol.  Start over in order to
		     get h, sym, sec etc. right.  */
		  rel--;
		  continue;
		}
	      else
		rel->r_info = ELF64_R_INFO (r_symndx, r_type);
	    }
	  break;

	case R_PPC64_GOT_TLSGD16_HI:
	case R_PPC64_GOT_TLSGD16_HA:
	  tls_gd = TLS_TPRELGD;
	  if (tls_mask != 0 && (tls_mask & TLS_GD) == 0)
	    goto tls_gdld_hi;
	  break;

	case R_PPC64_GOT_TLSLD16_HI:
	case R_PPC64_GOT_TLSLD16_HA:
	  if (tls_mask != 0 && (tls_mask & TLS_LD) == 0)
	    {
	    tls_gdld_hi:
	      if ((tls_mask & tls_gd) != 0)
		r_type = (((r_type - (R_PPC64_GOT_TLSGD16 & 3)) & 3)
			  + R_PPC64_GOT_TPREL16_DS);
	      else
		{
		  bfd_put_32 (output_bfd, NOP, contents + rel->r_offset);
		  rel->r_offset -= d_offset;
		  r_type = R_PPC64_NONE;
		}
	      rel->r_info = ELF64_R_INFO (r_symndx, r_type);
	    }
	  break;

	case R_PPC64_GOT_TLSGD16:
	case R_PPC64_GOT_TLSGD16_LO:
	  tls_gd = TLS_TPRELGD;
	  if (tls_mask != 0 && (tls_mask & TLS_GD) == 0)
	    goto tls_ldgd_opt;
	  break;

	case R_PPC64_GOT_TLSLD16:
	case R_PPC64_GOT_TLSLD16_LO:
	  if (tls_mask != 0 && (tls_mask & TLS_LD) == 0)
	    {
	      unsigned int insn1, insn2, insn3;
	      bfd_vma offset;

	    tls_ldgd_opt:
	      offset = (bfd_vma) -1;
	      /* If not using the newer R_PPC64_TLSGD/LD to mark
		 __tls_get_addr calls, we must trust that the call
		 stays with its arg setup insns, ie. that the next
		 reloc is the __tls_get_addr call associated with
		 the current reloc.  Edit both insns.  */
	      if (input_section->has_tls_get_addr_call
		  && rel + 1 < relend
		  && branch_reloc_hash_match (input_bfd, rel + 1,
					      htab->tls_get_addr,
					      htab->tls_get_addr_fd))
		offset = rel[1].r_offset;
	      if ((tls_mask & tls_gd) != 0)
		{
		  /* IE */
		  insn1 = bfd_get_32 (output_bfd,
				      contents + rel->r_offset - d_offset);
		  insn1 &= (1 << 26) - (1 << 2);
		  insn1 |= 58 << 26;	/* ld */
		  insn2 = 0x7c636a14;	/* add 3,3,13 */
		  if (offset != (bfd_vma) -1)
		    rel[1].r_info = ELF64_R_INFO (ELF64_R_SYM (rel[1].r_info),
						  R_PPC64_NONE);
		  if ((tls_mask & TLS_EXPLICIT) == 0)
		    r_type = (((r_type - (R_PPC64_GOT_TLSGD16 & 3)) & 3)
			      + R_PPC64_GOT_TPREL16_DS);
		  else
		    r_type += R_PPC64_TOC16_DS - R_PPC64_TOC16;
		  rel->r_info = ELF64_R_INFO (r_symndx, r_type);
		}
	      else
		{
		  /* LE */
		  insn1 = 0x3c6d0000;	/* addis 3,13,0 */
		  insn2 = 0x38630000;	/* addi 3,3,0 */
		  if (tls_gd == 0)
		    {
		      /* Was an LD reloc.  */
		      if (toc_symndx)
			sec = local_sections[toc_symndx];
		      for (r_symndx = 0;
			   r_symndx < symtab_hdr->sh_info;
			   r_symndx++)
			if (local_sections[r_symndx] == sec)
			  break;
		      if (r_symndx >= symtab_hdr->sh_info)
			r_symndx = 0;
		      rel->r_addend = htab->elf.tls_sec->vma + DTP_OFFSET;
		      if (r_symndx != 0)
			rel->r_addend -= (local_syms[r_symndx].st_value
					  + sec->output_offset
					  + sec->output_section->vma);
		    }
		  else if (toc_symndx != 0)
		    {
		      r_symndx = toc_symndx;
		      rel->r_addend = toc_addend;
		    }
		  r_type = R_PPC64_TPREL16_HA;
		  rel->r_info = ELF64_R_INFO (r_symndx, r_type);
		  if (offset != (bfd_vma) -1)
		    {
		      rel[1].r_info = ELF64_R_INFO (r_symndx,
						    R_PPC64_TPREL16_LO);
		      rel[1].r_offset = offset + d_offset;
		      rel[1].r_addend = rel->r_addend;
		    }
		}
	      bfd_put_32 (output_bfd, insn1,
			  contents + rel->r_offset - d_offset);
	      if (offset != (bfd_vma) -1)
		{
		  insn3 = bfd_get_32 (output_bfd,
				      contents + offset + 4);
		  if (insn3 == NOP
		      || insn3 == CROR_151515 || insn3 == CROR_313131)
		    {
		      rel[1].r_offset += 4;
		      bfd_put_32 (output_bfd, insn2, contents + offset + 4);
		      insn2 = NOP;
		    }
		  bfd_put_32 (output_bfd, insn2, contents + offset);
		}
	      if ((tls_mask & tls_gd) == 0
		  && (tls_gd == 0 || toc_symndx != 0))
		{
		  /* We changed the symbol.  Start over in order
		     to get h, sym, sec etc. right.  */
		  rel--;
		  continue;
		}
	    }
	  break;

	case R_PPC64_TLSGD:
	  if (tls_mask != 0 && (tls_mask & TLS_GD) == 0)
	    {
	      unsigned int insn2, insn3;
	      bfd_vma offset = rel->r_offset;

	      if ((tls_mask & TLS_TPRELGD) != 0)
		{
		  /* IE */
		  r_type = R_PPC64_NONE;
		  insn2 = 0x7c636a14;	/* add 3,3,13 */
		}
	      else
		{
		  /* LE */
		  if (toc_symndx != 0)
		    {
		      r_symndx = toc_symndx;
		      rel->r_addend = toc_addend;
		    }
		  r_type = R_PPC64_TPREL16_LO;
		  rel->r_offset = offset + d_offset;
		  insn2 = 0x38630000;	/* addi 3,3,0 */
		}
	      rel->r_info = ELF64_R_INFO (r_symndx, r_type);
	      /* Zap the reloc on the _tls_get_addr call too.  */
	      BFD_ASSERT (offset == rel[1].r_offset);
	      rel[1].r_info = ELF64_R_INFO (ELF64_R_SYM (rel[1].r_info),
					    R_PPC64_NONE);
	      insn3 = bfd_get_32 (output_bfd,
				  contents + offset + 4);
	      if (insn3 == NOP
		  || insn3 == CROR_151515 || insn3 == CROR_313131)
		{
		  rel->r_offset += 4;
		  bfd_put_32 (output_bfd, insn2, contents + offset + 4);
		  insn2 = NOP;
		}
	      bfd_put_32 (output_bfd, insn2, contents + offset);
	      if ((tls_mask & TLS_TPRELGD) == 0 && toc_symndx != 0)
		{
		  rel--;
		  continue;
		}
	    }
	  break;

	case R_PPC64_TLSLD:
	  if (tls_mask != 0 && (tls_mask & TLS_LD) == 0)
	    {
	      unsigned int insn2, insn3;
	      bfd_vma offset = rel->r_offset;

	      if (toc_symndx)
		sec = local_sections[toc_symndx];
	      for (r_symndx = 0;
		   r_symndx < symtab_hdr->sh_info;
		   r_symndx++)
		if (local_sections[r_symndx] == sec)
		  break;
	      if (r_symndx >= symtab_hdr->sh_info)
		r_symndx = 0;
	      rel->r_addend = htab->elf.tls_sec->vma + DTP_OFFSET;
	      if (r_symndx != 0)
		rel->r_addend -= (local_syms[r_symndx].st_value
				  + sec->output_offset
				  + sec->output_section->vma);

	      r_type = R_PPC64_TPREL16_LO;
	      rel->r_info = ELF64_R_INFO (r_symndx, r_type);
	      rel->r_offset = offset + d_offset;
	      /* Zap the reloc on the _tls_get_addr call too.  */
	      BFD_ASSERT (offset == rel[1].r_offset);
	      rel[1].r_info = ELF64_R_INFO (ELF64_R_SYM (rel[1].r_info),
					    R_PPC64_NONE);
	      insn2 = 0x38630000;	/* addi 3,3,0 */
	      insn3 = bfd_get_32 (output_bfd,
				  contents + offset + 4);
	      if (insn3 == NOP
		  || insn3 == CROR_151515 || insn3 == CROR_313131)
		{
		  rel->r_offset += 4;
		  bfd_put_32 (output_bfd, insn2, contents + offset + 4);
		  insn2 = NOP;
		}
	      bfd_put_32 (output_bfd, insn2, contents + offset);
	      rel--;
	      continue;
	    }
	  break;

	case R_PPC64_DTPMOD64:
	  if (rel + 1 < relend
	      && rel[1].r_info == ELF64_R_INFO (r_symndx, R_PPC64_DTPREL64)
	      && rel[1].r_offset == rel->r_offset + 8)
	    {
	      if ((tls_mask & TLS_GD) == 0)
		{
		  rel[1].r_info = ELF64_R_INFO (r_symndx, R_PPC64_NONE);
		  if ((tls_mask & TLS_TPRELGD) != 0)
		    r_type = R_PPC64_TPREL64;
		  else
		    {
		      bfd_put_64 (output_bfd, 1, contents + rel->r_offset);
		      r_type = R_PPC64_NONE;
		    }
		  rel->r_info = ELF64_R_INFO (r_symndx, r_type);
		}
	    }
	  else
	    {
	      if ((tls_mask & TLS_LD) == 0)
		{
		  bfd_put_64 (output_bfd, 1, contents + rel->r_offset);
		  r_type = R_PPC64_NONE;
		  rel->r_info = ELF64_R_INFO (r_symndx, r_type);
		}
	    }
	  break;

	case R_PPC64_TPREL64:
	  if ((tls_mask & TLS_TPREL) == 0)
	    {
	      r_type = R_PPC64_NONE;
	      rel->r_info = ELF64_R_INFO (r_symndx, r_type);
	    }
	  break;
	}

      /* Handle other relocations that tweak non-addend part of insn.  */
      insn = 0;
      max_br_offset = 1 << 25;
      addend = rel->r_addend;
      switch (r_type)
	{
	default:
	  break;

	  /* Branch taken prediction relocations.  */
	case R_PPC64_ADDR14_BRTAKEN:
	case R_PPC64_REL14_BRTAKEN:
	  insn = 0x01 << 21; /* 'y' or 't' bit, lowest bit of BO field.  */
	  /* Fall thru.  */

	  /* Branch not taken prediction relocations.  */
	case R_PPC64_ADDR14_BRNTAKEN:
	case R_PPC64_REL14_BRNTAKEN:
	  insn |= bfd_get_32 (output_bfd,
			      contents + rel->r_offset) & ~(0x01 << 21);
	  /* Fall thru.  */

	case R_PPC64_REL14:
	  max_br_offset = 1 << 15;
	  /* Fall thru.  */

	case R_PPC64_REL24:
	  /* Calls to functions with a different TOC, such as calls to
	     shared objects, need to alter the TOC pointer.  This is
	     done using a linkage stub.  A REL24 branching to these
	     linkage stubs needs to be followed by a nop, as the nop
	     will be replaced with an instruction to restore the TOC
	     base pointer.  */
	  stub_entry = NULL;
	  fdh = h;
	  if (((h != NULL
		&& (((fdh = h->oh) != NULL
		     && fdh->elf.plt.plist != NULL)
		    || (fdh = h)->elf.plt.plist != NULL))
	       || (sec != NULL
		   && sec->output_section != NULL
		   && sec->id <= htab->top_id
		   && (htab->stub_group[sec->id].toc_off
		       != htab->stub_group[input_section->id].toc_off)))
	      && (stub_entry = ppc_get_stub_entry (input_section, sec, fdh,
						   rel, htab)) != NULL
	      && (stub_entry->stub_type == ppc_stub_plt_call
		  || stub_entry->stub_type == ppc_stub_plt_branch_r2off
		  || stub_entry->stub_type == ppc_stub_long_branch_r2off))
	    {
	      bfd_boolean can_plt_call = FALSE;

	      if (rel->r_offset + 8 <= input_section->size)
		{
		  unsigned long nop;
		  nop = bfd_get_32 (input_bfd, contents + rel->r_offset + 4);
		  if (nop == NOP
		      || nop == CROR_151515 || nop == CROR_313131)
		    {
		      bfd_put_32 (input_bfd, LD_R2_40R1,
				  contents + rel->r_offset + 4);
		      can_plt_call = TRUE;
		    }
		}

	      if (!can_plt_call)
		{
		  if (stub_entry->stub_type == ppc_stub_plt_call)
		    {
		      /* If this is a plain branch rather than a branch
			 and link, don't require a nop.  However, don't
			 allow tail calls in a shared library as they
			 will result in r2 being corrupted.  */
		      unsigned long br;
		      br = bfd_get_32 (input_bfd, contents + rel->r_offset);
		      if (info->executable && (br & 1) == 0)
			can_plt_call = TRUE;
		      else
			stub_entry = NULL;
		    }
		  else if (h != NULL
			   && strcmp (h->elf.root.root.string,
				      ".__libc_start_main") == 0)
		    {
		      /* Allow crt1 branch to go via a toc adjusting stub.  */
		      can_plt_call = TRUE;
		    }
		  else
		    {
		      if (strcmp (input_section->output_section->name,
				  ".init") == 0
			  || strcmp (input_section->output_section->name,
				     ".fini") == 0)
			(*_bfd_error_handler)
			  (_("%B(%A+0x%lx): automatic multiple TOCs "
			     "not supported using your crt files; "
			     "recompile with -mminimal-toc or upgrade gcc"),
			   input_bfd,
			   input_section,
			   (long) rel->r_offset);
		      else
			(*_bfd_error_handler)
			  (_("%B(%A+0x%lx): sibling call optimization to `%s' "
			     "does not allow automatic multiple TOCs; "
			     "recompile with -mminimal-toc or "
			     "-fno-optimize-sibling-calls, "
			     "or make `%s' extern"),
			   input_bfd,
			   input_section,
			   (long) rel->r_offset,
			   sym_name,
			   sym_name);
		      bfd_set_error (bfd_error_bad_value);
		      ret = FALSE;
		    }
		}

	      if (can_plt_call
		  && stub_entry->stub_type == ppc_stub_plt_call)
		unresolved_reloc = FALSE;
	    }

	  if (stub_entry == NULL
	      && get_opd_info (sec) != NULL)
	    {
	      /* The branch destination is the value of the opd entry. */
	      bfd_vma off = (relocation + addend
			     - sec->output_section->vma
			     - sec->output_offset);
	      bfd_vma dest = opd_entry_value (sec, off, NULL, NULL);
	      if (dest != (bfd_vma) -1)
		{
		  relocation = dest;
		  addend = 0;
		}
	    }

	  /* If the branch is out of reach we ought to have a long
	     branch stub.  */
	  from = (rel->r_offset
		  + input_section->output_offset
		  + input_section->output_section->vma);

	  if (stub_entry == NULL
	      && (relocation + addend - from + max_br_offset
		  >= 2 * max_br_offset)
	      && r_type != R_PPC64_ADDR14_BRTAKEN
	      && r_type != R_PPC64_ADDR14_BRNTAKEN)
	    stub_entry = ppc_get_stub_entry (input_section, sec, h, rel,
					     htab);

	  if (stub_entry != NULL)
	    {
	      /* Munge up the value and addend so that we call the stub
		 rather than the procedure directly.  */
	      relocation = (stub_entry->stub_offset
			    + stub_entry->stub_sec->output_offset
			    + stub_entry->stub_sec->output_section->vma);
	      addend = 0;
	    }

	  if (insn != 0)
	    {
	      if (is_power4)
		{
		  /* Set 'a' bit.  This is 0b00010 in BO field for branch
		     on CR(BI) insns (BO == 001at or 011at), and 0b01000
		     for branch on CTR insns (BO == 1a00t or 1a01t).  */
		  if ((insn & (0x14 << 21)) == (0x04 << 21))
		    insn |= 0x02 << 21;
		  else if ((insn & (0x14 << 21)) == (0x10 << 21))
		    insn |= 0x08 << 21;
		  else
		    break;
		}
	      else
		{
		  /* Invert 'y' bit if not the default.  */
		  if ((bfd_signed_vma) (relocation + addend - from) < 0)
		    insn ^= 0x01 << 21;
		}

	      bfd_put_32 (output_bfd, insn, contents + rel->r_offset);
	    }

	  /* NOP out calls to undefined weak functions.
	     We can thus call a weak function without first
	     checking whether the function is defined.  */
	  else if (h != NULL
		   && h->elf.root.type == bfd_link_hash_undefweak
		   && r_type == R_PPC64_REL24
		   && relocation == 0
		   && addend == 0)
	    {
	      bfd_put_32 (output_bfd, NOP, contents + rel->r_offset);
	      continue;
	    }
	  break;
	}

      /* Set `addend'.  */
      tls_type = 0;
      switch (r_type)
	{
	default:
	  (*_bfd_error_handler)
	    (_("%B: unknown relocation type %d for symbol %s"),
	     input_bfd, (int) r_type, sym_name);

	  bfd_set_error (bfd_error_bad_value);
	  ret = FALSE;
	  continue;

	case R_PPC64_NONE:
	case R_PPC64_TLS:
	case R_PPC64_TLSGD:
	case R_PPC64_TLSLD:
	case R_PPC64_GNU_VTINHERIT:
	case R_PPC64_GNU_VTENTRY:
	  continue;

	  /* GOT16 relocations.  Like an ADDR16 using the symbol's
	     address in the GOT as relocation value instead of the
	     symbol's value itself.  Also, create a GOT entry for the
	     symbol and put the symbol value there.  */
	case R_PPC64_GOT_TLSGD16:
	case R_PPC64_GOT_TLSGD16_LO:
	case R_PPC64_GOT_TLSGD16_HI:
	case R_PPC64_GOT_TLSGD16_HA:
	  tls_type = TLS_TLS | TLS_GD;
	  goto dogot;

	case R_PPC64_GOT_TLSLD16:
	case R_PPC64_GOT_TLSLD16_LO:
	case R_PPC64_GOT_TLSLD16_HI:
	case R_PPC64_GOT_TLSLD16_HA:
	  tls_type = TLS_TLS | TLS_LD;
	  goto dogot;

	case R_PPC64_GOT_TPREL16_DS:
	case R_PPC64_GOT_TPREL16_LO_DS:
	case R_PPC64_GOT_TPREL16_HI:
	case R_PPC64_GOT_TPREL16_HA:
	  tls_type = TLS_TLS | TLS_TPREL;
	  goto dogot;

	case R_PPC64_GOT_DTPREL16_DS:
	case R_PPC64_GOT_DTPREL16_LO_DS:
	case R_PPC64_GOT_DTPREL16_HI:
	case R_PPC64_GOT_DTPREL16_HA:
	  tls_type = TLS_TLS | TLS_DTPREL;
	  goto dogot;

	case R_PPC64_GOT16:
	case R_PPC64_GOT16_LO:
	case R_PPC64_GOT16_HI:
	case R_PPC64_GOT16_HA:
	case R_PPC64_GOT16_DS:
	case R_PPC64_GOT16_LO_DS:
	dogot:
	  {
	    /* Relocation is to the entry for this symbol in the global
	       offset table.  */
	    asection *got;
	    bfd_vma *offp;
	    bfd_vma off;
	    unsigned long indx = 0;

	    if (tls_type == (TLS_TLS | TLS_LD)
		&& (h == NULL
		    || !h->elf.def_dynamic))
	      offp = &ppc64_tlsld_got (input_bfd)->offset;
	    else
	      {
		struct got_entry *ent;

		if (h != NULL)
		  {
		    bfd_boolean dyn = htab->elf.dynamic_sections_created;
		    if (!WILL_CALL_FINISH_DYNAMIC_SYMBOL (dyn, info->shared,
							  &h->elf)
			|| (info->shared
			    && SYMBOL_REFERENCES_LOCAL (info, &h->elf)))
		      /* This is actually a static link, or it is a
			 -Bsymbolic link and the symbol is defined
			 locally, or the symbol was forced to be local
			 because of a version file.  */
		      ;
		    else
		      {
			indx = h->elf.dynindx;
			unresolved_reloc = FALSE;
		      }
		    ent = h->elf.got.glist;
		  }
		else
		  {
		    if (local_got_ents == NULL)
		      abort ();
		    ent = local_got_ents[r_symndx];
		  }

		for (; ent != NULL; ent = ent->next)
		  if (ent->addend == orig_addend
		      && ent->owner == input_bfd
		      && ent->tls_type == tls_type)
		    break;
		if (ent == NULL)
		  abort ();
		offp = &ent->got.offset;
	      }

	    got = ppc64_elf_tdata (input_bfd)->got;
	    if (got == NULL)
	      abort ();

	    /* The offset must always be a multiple of 8.  We use the
	       least significant bit to record whether we have already
	       processed this entry.  */
	    off = *offp;
	    if ((off & 1) != 0)
	      off &= ~1;
	    else
	      {
		/* Generate relocs for the dynamic linker, except in
		   the case of TLSLD where we'll use one entry per
		   module.  */
		asection *relgot = ppc64_elf_tdata (input_bfd)->relgot;

		*offp = off | 1;
		if ((info->shared || indx != 0)
		    && (offp == &ppc64_tlsld_got (input_bfd)->offset
			|| h == NULL
			|| ELF_ST_VISIBILITY (h->elf.other) == STV_DEFAULT
			|| h->elf.root.type != bfd_link_hash_undefweak))
		  {
		    outrel.r_offset = (got->output_section->vma
				       + got->output_offset
				       + off);
		    outrel.r_addend = addend;
		    if (tls_type & (TLS_LD | TLS_GD))
		      {
			outrel.r_addend = 0;
			outrel.r_info = ELF64_R_INFO (indx, R_PPC64_DTPMOD64);
			if (tls_type == (TLS_TLS | TLS_GD))
			  {
			    loc = relgot->contents;
			    loc += (relgot->reloc_count++
				    * sizeof (Elf64_External_Rela));
			    bfd_elf64_swap_reloca_out (output_bfd,
						       &outrel, loc);
			    outrel.r_offset += 8;
			    outrel.r_addend = addend;
			    outrel.r_info
			      = ELF64_R_INFO (indx, R_PPC64_DTPREL64);
			  }
		      }
		    else if (tls_type == (TLS_TLS | TLS_DTPREL))
		      outrel.r_info = ELF64_R_INFO (indx, R_PPC64_DTPREL64);
		    else if (tls_type == (TLS_TLS | TLS_TPREL))
		      outrel.r_info = ELF64_R_INFO (indx, R_PPC64_TPREL64);
		    else if (indx == 0)
		      {
			outrel.r_info = ELF64_R_INFO (indx, R_PPC64_RELATIVE);

			/* Write the .got section contents for the sake
			   of prelink.  */
			loc = got->contents + off;
			bfd_put_64 (output_bfd, outrel.r_addend + relocation,
				    loc);
		      }
		    else
		      outrel.r_info = ELF64_R_INFO (indx, R_PPC64_GLOB_DAT);

		    if (indx == 0 && tls_type != (TLS_TLS | TLS_LD))
		      {
			outrel.r_addend += relocation;
			if (tls_type & (TLS_GD | TLS_DTPREL | TLS_TPREL))
			  outrel.r_addend -= htab->elf.tls_sec->vma;
		      }
		    loc = relgot->contents;
		    loc += (relgot->reloc_count++
			    * sizeof (Elf64_External_Rela));
		    bfd_elf64_swap_reloca_out (output_bfd, &outrel, loc);
		  }

		/* Init the .got section contents here if we're not
		   emitting a reloc.  */
		else
		  {
		    relocation += addend;
		    if (tls_type == (TLS_TLS | TLS_LD))
		      relocation = 1;
		    else if (tls_type != 0)
		      {
			relocation -= htab->elf.tls_sec->vma + DTP_OFFSET;
			if (tls_type == (TLS_TLS | TLS_TPREL))
			  relocation += DTP_OFFSET - TP_OFFSET;

			if (tls_type == (TLS_TLS | TLS_GD))
			  {
			    bfd_put_64 (output_bfd, relocation,
					got->contents + off + 8);
			    relocation = 1;
			  }
		      }

		    bfd_put_64 (output_bfd, relocation,
				got->contents + off);
		  }
	      }

	    if (off >= (bfd_vma) -2)
	      abort ();

	    relocation = got->output_offset + off;

	    /* TOC base (r2) is TOC start plus 0x8000.  */
	    addend = -TOC_BASE_OFF;
	  }
	  break;

	case R_PPC64_PLT16_HA:
	case R_PPC64_PLT16_HI:
	case R_PPC64_PLT16_LO:
	case R_PPC64_PLT32:
	case R_PPC64_PLT64:
	  /* Relocation is to the entry for this symbol in the
	     procedure linkage table.  */

	  /* Resolve a PLT reloc against a local symbol directly,
	     without using the procedure linkage table.  */
	  if (h == NULL)
	    break;

	  /* It's possible that we didn't make a PLT entry for this
	     symbol.  This happens when statically linking PIC code,
	     or when using -Bsymbolic.  Go find a match if there is a
	     PLT entry.  */
	  if (htab->plt != NULL)
	    {
	      struct plt_entry *ent;
	      for (ent = h->elf.plt.plist; ent != NULL; ent = ent->next)
		if (ent->addend == orig_addend
		    && ent->plt.offset != (bfd_vma) -1)
		  {
		    relocation = (htab->plt->output_section->vma
				  + htab->plt->output_offset
				  + ent->plt.offset);
		    unresolved_reloc = FALSE;
		  }
	    }
	  break;

	case R_PPC64_TOC:
	  /* Relocation value is TOC base.  */
	  relocation = TOCstart;
	  if (r_symndx == 0)
	    relocation += htab->stub_group[input_section->id].toc_off;
	  else if (unresolved_reloc)
	    ;
	  else if (sec != NULL && sec->id <= htab->top_id)
	    relocation += htab->stub_group[sec->id].toc_off;
	  else
	    unresolved_reloc = TRUE;
	  goto dodyn;

	  /* TOC16 relocs.  We want the offset relative to the TOC base,
	     which is the address of the start of the TOC plus 0x8000.
	     The TOC consists of sections .got, .toc, .tocbss, and .plt,
	     in this order.  */
	case R_PPC64_TOC16:
	case R_PPC64_TOC16_LO:
	case R_PPC64_TOC16_HI:
	case R_PPC64_TOC16_DS:
	case R_PPC64_TOC16_LO_DS:
	case R_PPC64_TOC16_HA:
	  addend -= TOCstart + htab->stub_group[input_section->id].toc_off;
	  break;

	  /* Relocate against the beginning of the section.  */
	case R_PPC64_SECTOFF:
	case R_PPC64_SECTOFF_LO:
	case R_PPC64_SECTOFF_HI:
	case R_PPC64_SECTOFF_DS:
	case R_PPC64_SECTOFF_LO_DS:
	case R_PPC64_SECTOFF_HA:
	  if (sec != NULL)
	    addend -= sec->output_section->vma;
	  break;

	case R_PPC64_REL14:
	case R_PPC64_REL14_BRNTAKEN:
	case R_PPC64_REL14_BRTAKEN:
	case R_PPC64_REL24:
	  break;

	case R_PPC64_TPREL16:
	case R_PPC64_TPREL16_LO:
	case R_PPC64_TPREL16_HI:
	case R_PPC64_TPREL16_HA:
	case R_PPC64_TPREL16_DS:
	case R_PPC64_TPREL16_LO_DS:
	case R_PPC64_TPREL16_HIGHER:
	case R_PPC64_TPREL16_HIGHERA:
	case R_PPC64_TPREL16_HIGHEST:
	case R_PPC64_TPREL16_HIGHESTA:
	  addend -= htab->elf.tls_sec->vma + TP_OFFSET;
	  if (info->shared)
	    /* The TPREL16 relocs shouldn't really be used in shared
	       libs as they will result in DT_TEXTREL being set, but
	       support them anyway.  */
	    goto dodyn;
	  break;

	case R_PPC64_DTPREL16:
	case R_PPC64_DTPREL16_LO:
	case R_PPC64_DTPREL16_HI:
	case R_PPC64_DTPREL16_HA:
	case R_PPC64_DTPREL16_DS:
	case R_PPC64_DTPREL16_LO_DS:
	case R_PPC64_DTPREL16_HIGHER:
	case R_PPC64_DTPREL16_HIGHERA:
	case R_PPC64_DTPREL16_HIGHEST:
	case R_PPC64_DTPREL16_HIGHESTA:
	  addend -= htab->elf.tls_sec->vma + DTP_OFFSET;
	  break;

	case R_PPC64_DTPMOD64:
	  relocation = 1;
	  addend = 0;
	  goto dodyn;

	case R_PPC64_TPREL64:
	  addend -= htab->elf.tls_sec->vma + TP_OFFSET;
	  goto dodyn;

	case R_PPC64_DTPREL64:
	  addend -= htab->elf.tls_sec->vma + DTP_OFFSET;
	  /* Fall thru */

	  /* Relocations that may need to be propagated if this is a
	     dynamic object.  */
	case R_PPC64_REL30:
	case R_PPC64_REL32:
	case R_PPC64_REL64:
	case R_PPC64_ADDR14:
	case R_PPC64_ADDR14_BRNTAKEN:
	case R_PPC64_ADDR14_BRTAKEN:
	case R_PPC64_ADDR16:
	case R_PPC64_ADDR16_DS:
	case R_PPC64_ADDR16_HA:
	case R_PPC64_ADDR16_HI:
	case R_PPC64_ADDR16_HIGHER:
	case R_PPC64_ADDR16_HIGHERA:
	case R_PPC64_ADDR16_HIGHEST:
	case R_PPC64_ADDR16_HIGHESTA:
	case R_PPC64_ADDR16_LO:
	case R_PPC64_ADDR16_LO_DS:
	case R_PPC64_ADDR24:
	case R_PPC64_ADDR32:
	case R_PPC64_ADDR64:
	case R_PPC64_UADDR16:
	case R_PPC64_UADDR32:
	case R_PPC64_UADDR64:
	dodyn:
	  if ((input_section->flags & SEC_ALLOC) == 0)
	    break;

	  if (NO_OPD_RELOCS && is_opd)
	    break;

	  if ((info->shared
	       && (h == NULL
		   || ELF_ST_VISIBILITY (h->elf.other) == STV_DEFAULT
		   || h->elf.root.type != bfd_link_hash_undefweak)
	       && (must_be_dyn_reloc (info, r_type)
		   || !SYMBOL_CALLS_LOCAL (info, &h->elf)))
	      || (ELIMINATE_COPY_RELOCS
		  && !info->shared
		  && h != NULL
		  && h->elf.dynindx != -1
		  && !h->elf.non_got_ref
		  && h->elf.def_dynamic
		  && !h->elf.def_regular))
	    {
	      Elf_Internal_Rela outrel;
	      bfd_boolean skip, relocate;
	      asection *sreloc;
	      bfd_byte *loc;
	      bfd_vma out_off;

	      /* When generating a dynamic object, these relocations
		 are copied into the output file to be resolved at run
		 time.  */

	      skip = FALSE;
	      relocate = FALSE;

	      out_off = _bfd_elf_section_offset (output_bfd, info,
						 input_section, rel->r_offset);
	      if (out_off == (bfd_vma) -1)
		skip = TRUE;
	      else if (out_off == (bfd_vma) -2)
		skip = TRUE, relocate = TRUE;
	      out_off += (input_section->output_section->vma
			  + input_section->output_offset);
	      outrel.r_offset = out_off;
	      outrel.r_addend = rel->r_addend;

	      /* Optimize unaligned reloc use.  */
	      if ((r_type == R_PPC64_ADDR64 && (out_off & 7) != 0)
		  || (r_type == R_PPC64_UADDR64 && (out_off & 7) == 0))
		r_type ^= R_PPC64_ADDR64 ^ R_PPC64_UADDR64;
	      else if ((r_type == R_PPC64_ADDR32 && (out_off & 3) != 0)
		       || (r_type == R_PPC64_UADDR32 && (out_off & 3) == 0))
		r_type ^= R_PPC64_ADDR32 ^ R_PPC64_UADDR32;
	      else if ((r_type == R_PPC64_ADDR16 && (out_off & 1) != 0)
		       || (r_type == R_PPC64_UADDR16 && (out_off & 1) == 0))
		r_type ^= R_PPC64_ADDR16 ^ R_PPC64_UADDR16;

	      if (skip)
		memset (&outrel, 0, sizeof outrel);
	      else if (!SYMBOL_REFERENCES_LOCAL (info, &h->elf)
		       && !is_opd
		       && r_type != R_PPC64_TOC)
		outrel.r_info = ELF64_R_INFO (h->elf.dynindx, r_type);
	      else
		{
		  /* This symbol is local, or marked to become local,
		     or this is an opd section reloc which must point
		     at a local function.  */
		  outrel.r_addend += relocation;
		  if (r_type == R_PPC64_ADDR64 || r_type == R_PPC64_TOC)
		    {
		      if (is_opd && h != NULL)
			{
			  /* Lie about opd entries.  This case occurs
			     when building shared libraries and we
			     reference a function in another shared
			     lib.  The same thing happens for a weak
			     definition in an application that's
			     overridden by a strong definition in a
			     shared lib.  (I believe this is a generic
			     bug in binutils handling of weak syms.)
			     In these cases we won't use the opd
			     entry in this lib.  */
			  unresolved_reloc = FALSE;
			}
		      outrel.r_info = ELF64_R_INFO (0, R_PPC64_RELATIVE);

		      /* We need to relocate .opd contents for ld.so.
			 Prelink also wants simple and consistent rules
			 for relocs.  This make all RELATIVE relocs have
			 *r_offset equal to r_addend.  */
		      relocate = TRUE;
		    }
		  else
		    {
		      long indx = 0;

		      if (r_symndx == 0 || bfd_is_abs_section (sec))
			;
		      else if (sec == NULL || sec->owner == NULL)
			{
			  bfd_set_error (bfd_error_bad_value);
			  return FALSE;
			}
		      else
			{
			  asection *osec;

			  osec = sec->output_section;
			  indx = elf_section_data (osec)->dynindx;

			  if (indx == 0)
			    {
			      if ((osec->flags & SEC_READONLY) == 0
				  && htab->elf.data_index_section != NULL)
				osec = htab->elf.data_index_section;
			      else
				osec = htab->elf.text_index_section;
			      indx = elf_section_data (osec)->dynindx;
			    }
			  BFD_ASSERT (indx != 0);

			  /* We are turning this relocation into one
			     against a section symbol, so subtract out
			     the output section's address but not the
			     offset of the input section in the output
			     section.  */
			  outrel.r_addend -= osec->vma;
			}

		      outrel.r_info = ELF64_R_INFO (indx, r_type);
		    }
		}

	      sreloc = elf_section_data (input_section)->sreloc;
	      if (sreloc == NULL)
		abort ();

	      if (sreloc->reloc_count * sizeof (Elf64_External_Rela)
		  >= sreloc->size)
		abort ();
	      loc = sreloc->contents;
	      loc += sreloc->reloc_count++ * sizeof (Elf64_External_Rela);
	      bfd_elf64_swap_reloca_out (output_bfd, &outrel, loc);

	      /* If this reloc is against an external symbol, it will
		 be computed at runtime, so there's no need to do
		 anything now.  However, for the sake of prelink ensure
		 that the section contents are a known value.  */
	      if (! relocate)
		{
		  unresolved_reloc = FALSE;
		  /* The value chosen here is quite arbitrary as ld.so
		     ignores section contents except for the special
		     case of .opd where the contents might be accessed
		     before relocation.  Choose zero, as that won't
		     cause reloc overflow.  */
		  relocation = 0;
		  addend = 0;
		  /* Use *r_offset == r_addend for R_PPC64_ADDR64 relocs
		     to improve backward compatibility with older
		     versions of ld.  */
		  if (r_type == R_PPC64_ADDR64)
		    addend = outrel.r_addend;
		  /* Adjust pc_relative relocs to have zero in *r_offset.  */
		  else if (ppc64_elf_howto_table[r_type]->pc_relative)
		    addend = (input_section->output_section->vma
			      + input_section->output_offset
			      + rel->r_offset);
		}
	    }
	  break;

	case R_PPC64_COPY:
	case R_PPC64_GLOB_DAT:
	case R_PPC64_JMP_SLOT:
	case R_PPC64_RELATIVE:
	  /* We shouldn't ever see these dynamic relocs in relocatable
	     files.  */
	  /* Fall through.  */

	case R_PPC64_PLTGOT16:
	case R_PPC64_PLTGOT16_DS:
	case R_PPC64_PLTGOT16_HA:
	case R_PPC64_PLTGOT16_HI:
	case R_PPC64_PLTGOT16_LO:
	case R_PPC64_PLTGOT16_LO_DS:
	case R_PPC64_PLTREL32:
	case R_PPC64_PLTREL64:
	  /* These ones haven't been implemented yet.  */

	  (*_bfd_error_handler)
	    (_("%B: relocation %s is not supported for symbol %s."),
	     input_bfd,
	     ppc64_elf_howto_table[r_type]->name, sym_name);

	  bfd_set_error (bfd_error_invalid_operation);
	  ret = FALSE;
	  continue;
	}

      /* Do any further special processing.  */
      switch (r_type)
	{
	default:
	  break;

	case R_PPC64_ADDR16_HA:
	case R_PPC64_ADDR16_HIGHERA:
	case R_PPC64_ADDR16_HIGHESTA:
	case R_PPC64_TOC16_HA:
	case R_PPC64_SECTOFF_HA:
	case R_PPC64_TPREL16_HA:
	case R_PPC64_DTPREL16_HA:
	case R_PPC64_TPREL16_HIGHER:
	case R_PPC64_TPREL16_HIGHERA:
	case R_PPC64_TPREL16_HIGHEST:
	case R_PPC64_TPREL16_HIGHESTA:
	case R_PPC64_DTPREL16_HIGHER:
	case R_PPC64_DTPREL16_HIGHERA:
	case R_PPC64_DTPREL16_HIGHEST:
	case R_PPC64_DTPREL16_HIGHESTA:
	  /* It's just possible that this symbol is a weak symbol
	     that's not actually defined anywhere. In that case,
	     'sec' would be NULL, and we should leave the symbol
	     alone (it will be set to zero elsewhere in the link).  */
	  if (sec == NULL)
	    break;
	  /* Fall thru */

	case R_PPC64_GOT16_HA:
	case R_PPC64_PLTGOT16_HA:
	case R_PPC64_PLT16_HA:
	case R_PPC64_GOT_TLSGD16_HA:
	case R_PPC64_GOT_TLSLD16_HA:
	case R_PPC64_GOT_TPREL16_HA:
	case R_PPC64_GOT_DTPREL16_HA:
	  /* Add 0x10000 if sign bit in 0:15 is set.
	     Bits 0:15 are not used.  */
	  addend += 0x8000;
	  break;

	case R_PPC64_ADDR16_DS:
	case R_PPC64_ADDR16_LO_DS:
	case R_PPC64_GOT16_DS:
	case R_PPC64_GOT16_LO_DS:
	case R_PPC64_PLT16_LO_DS:
	case R_PPC64_SECTOFF_DS:
	case R_PPC64_SECTOFF_LO_DS:
	case R_PPC64_TOC16_DS:
	case R_PPC64_TOC16_LO_DS:
	case R_PPC64_PLTGOT16_DS:
	case R_PPC64_PLTGOT16_LO_DS:
	case R_PPC64_GOT_TPREL16_DS:
	case R_PPC64_GOT_TPREL16_LO_DS:
	case R_PPC64_GOT_DTPREL16_DS:
	case R_PPC64_GOT_DTPREL16_LO_DS:
	case R_PPC64_TPREL16_DS:
	case R_PPC64_TPREL16_LO_DS:
	case R_PPC64_DTPREL16_DS:
	case R_PPC64_DTPREL16_LO_DS:
	  insn = bfd_get_32 (input_bfd, contents + (rel->r_offset & ~3));
	  mask = 3;
	  /* If this reloc is against an lq insn, then the value must be
	     a multiple of 16.  This is somewhat of a hack, but the
	     "correct" way to do this by defining _DQ forms of all the
	     _DS relocs bloats all reloc switches in this file.  It
	     doesn't seem to make much sense to use any of these relocs
	     in data, so testing the insn should be safe.  */
	  if ((insn & (0x3f << 26)) == (56u << 26))
	    mask = 15;
	  if (((relocation + addend) & mask) != 0)
	    {
	      (*_bfd_error_handler)
		(_("%B: error: relocation %s not a multiple of %d"),
		 input_bfd,
		 ppc64_elf_howto_table[r_type]->name,
		 mask + 1);
	      bfd_set_error (bfd_error_bad_value);
	      ret = FALSE;
	      continue;
	    }
	  break;
	}

      /* Dynamic relocs are not propagated for SEC_DEBUGGING sections
	 because such sections are not SEC_ALLOC and thus ld.so will
	 not process them.  */
      if (unresolved_reloc
	  && !((input_section->flags & SEC_DEBUGGING) != 0
	       && h->elf.def_dynamic))
	{
	  (*_bfd_error_handler)
	    (_("%B(%A+0x%lx): unresolvable %s relocation against symbol `%s'"),
	     input_bfd,
	     input_section,
	     (long) rel->r_offset,
	     ppc64_elf_howto_table[(int) r_type]->name,
	     h->elf.root.root.string);
	  ret = FALSE;
	}

      r = _bfd_final_link_relocate (ppc64_elf_howto_table[(int) r_type],
				    input_bfd,
				    input_section,
				    contents,
				    rel->r_offset,
				    relocation,
				    addend);

      if (r != bfd_reloc_ok)
	{
	  if (sym_name == NULL)
	    sym_name = "(null)";
	  if (r == bfd_reloc_overflow)
	    {
	      if (warned)
		continue;
	      if (h != NULL
		  && h->elf.root.type == bfd_link_hash_undefweak
		  && ppc64_elf_howto_table[r_type]->pc_relative)
		{
		  /* Assume this is a call protected by other code that
		     detects the symbol is undefined.  If this is the case,
		     we can safely ignore the overflow.  If not, the
		     program is hosed anyway, and a little warning isn't
		     going to help.  */

		  continue;
		}

	      if (!((*info->callbacks->reloc_overflow)
		    (info, (h ? &h->elf.root : NULL), sym_name,
		     ppc64_elf_howto_table[r_type]->name,
		     orig_addend, input_bfd, input_section, rel->r_offset)))
		return FALSE;
	    }
	  else
	    {
	      (*_bfd_error_handler)
		(_("%B(%A+0x%lx): %s reloc against `%s': error %d"),
		 input_bfd,
		 input_section,
		 (long) rel->r_offset,
		 ppc64_elf_howto_table[r_type]->name,
		 sym_name,
		 (int) r);
	      ret = FALSE;
	    }
	}
    }

  /* If we're emitting relocations, then shortly after this function
     returns, reloc offsets and addends for this section will be
     adjusted.  Worse, reloc symbol indices will be for the output
     file rather than the input.  Save a copy of the relocs for
     opd_entry_value.  */
  if (is_opd && (info->emitrelocations || info->relocatable))
    {
      bfd_size_type amt;
      amt = input_section->reloc_count * sizeof (Elf_Internal_Rela);
      rel = bfd_alloc (input_bfd, amt);
      BFD_ASSERT (ppc64_elf_tdata (input_bfd)->opd_relocs == NULL);
      ppc64_elf_tdata (input_bfd)->opd_relocs = rel;
      if (rel == NULL)
	return FALSE;
      memcpy (rel, relocs, amt);
    }
  return ret;
}

/* Adjust the value of any local symbols in opd sections.  */

static bfd_boolean
ppc64_elf_output_symbol_hook (struct bfd_link_info *info,
			      const char *name ATTRIBUTE_UNUSED,
			      Elf_Internal_Sym *elfsym,
			      asection *input_sec,
			      struct elf_link_hash_entry *h)
{
  long *opd_adjust, adjust;
  bfd_vma value;

  if (h != NULL)
    return TRUE;

  opd_adjust = get_opd_info (input_sec);
  if (opd_adjust == NULL)
    return TRUE;

  value = elfsym->st_value - input_sec->output_offset;
  if (!info->relocatable)
    value -= input_sec->output_section->vma;

  adjust = opd_adjust[value / 8];
  if (adjust == -1)
    elfsym->st_value = 0;
  else
    elfsym->st_value += adjust;
  return TRUE;
}

/* Finish up dynamic symbol handling.  We set the contents of various
   dynamic sections here.  */

static bfd_boolean
ppc64_elf_finish_dynamic_symbol (bfd *output_bfd,
				 struct bfd_link_info *info,
				 struct elf_link_hash_entry *h,
				 Elf_Internal_Sym *sym)
{
  struct ppc_link_hash_table *htab;
  struct plt_entry *ent;
  Elf_Internal_Rela rela;
  bfd_byte *loc;

  htab = ppc_hash_table (info);

  for (ent = h->plt.plist; ent != NULL; ent = ent->next)
    if (ent->plt.offset != (bfd_vma) -1)
      {
	/* This symbol has an entry in the procedure linkage
	   table.  Set it up.  */

	if (htab->plt == NULL
	    || htab->relplt == NULL
	    || htab->glink == NULL)
	  abort ();

	/* Create a JMP_SLOT reloc to inform the dynamic linker to
	   fill in the PLT entry.  */
	rela.r_offset = (htab->plt->output_section->vma
			 + htab->plt->output_offset
			 + ent->plt.offset);
	rela.r_info = ELF64_R_INFO (h->dynindx, R_PPC64_JMP_SLOT);
	rela.r_addend = ent->addend;

	loc = htab->relplt->contents;
	loc += ((ent->plt.offset - PLT_INITIAL_ENTRY_SIZE) / PLT_ENTRY_SIZE
		* sizeof (Elf64_External_Rela));
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
	  || htab->relbss == NULL)
	abort ();

      rela.r_offset = (h->root.u.def.value
		       + h->root.u.def.section->output_section->vma
		       + h->root.u.def.section->output_offset);
      rela.r_info = ELF64_R_INFO (h->dynindx, R_PPC64_COPY);
      rela.r_addend = 0;
      loc = htab->relbss->contents;
      loc += htab->relbss->reloc_count++ * sizeof (Elf64_External_Rela);
      bfd_elf64_swap_reloca_out (output_bfd, &rela, loc);
    }

  /* Mark some specially defined symbols as absolute.  */
  if (strcmp (h->root.root.string, "_DYNAMIC") == 0)
    sym->st_shndx = SHN_ABS;

  return TRUE;
}

/* Used to decide how to sort relocs in an optimal manner for the
   dynamic linker, before writing them out.  */

static enum elf_reloc_type_class
ppc64_elf_reloc_type_class (const Elf_Internal_Rela *rela)
{
  enum elf_ppc64_reloc_type r_type;

  r_type = ELF64_R_TYPE (rela->r_info);
  switch (r_type)
    {
    case R_PPC64_RELATIVE:
      return reloc_class_relative;
    case R_PPC64_JMP_SLOT:
      return reloc_class_plt;
    case R_PPC64_COPY:
      return reloc_class_copy;
    default:
      return reloc_class_normal;
    }
}

/* Finish up the dynamic sections.  */

static bfd_boolean
ppc64_elf_finish_dynamic_sections (bfd *output_bfd,
				   struct bfd_link_info *info)
{
  struct ppc_link_hash_table *htab;
  bfd *dynobj;
  asection *sdyn;

  htab = ppc_hash_table (info);
  dynobj = htab->elf.dynobj;
  sdyn = bfd_get_section_by_name (dynobj, ".dynamic");

  if (htab->elf.dynamic_sections_created)
    {
      Elf64_External_Dyn *dyncon, *dynconend;

      if (sdyn == NULL || htab->got == NULL)
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

	    case DT_PPC64_GLINK:
	      s = htab->glink;
	      dyn.d_un.d_ptr = s->output_section->vma + s->output_offset;
	      /* We stupidly defined DT_PPC64_GLINK to be the start
		 of glink rather than the first entry point, which is
		 what ld.so needs, and now have a bigger stub to
		 support automatic multiple TOCs.  */
	      dyn.d_un.d_ptr += GLINK_CALL_STUB_SIZE - 32;
	      break;

	    case DT_PPC64_OPD:
	      s = bfd_get_section_by_name (output_bfd, ".opd");
	      if (s == NULL)
		continue;
	      dyn.d_un.d_ptr = s->vma;
	      break;

	    case DT_PPC64_OPDSZ:
	      s = bfd_get_section_by_name (output_bfd, ".opd");
	      if (s == NULL)
		continue;
	      dyn.d_un.d_val = s->size;
	      break;

	    case DT_PLTGOT:
	      s = htab->plt;
	      dyn.d_un.d_ptr = s->output_section->vma + s->output_offset;
	      break;

	    case DT_JMPREL:
	      s = htab->relplt;
	      dyn.d_un.d_ptr = s->output_section->vma + s->output_offset;
	      break;

	    case DT_PLTRELSZ:
	      dyn.d_un.d_val = htab->relplt->size;
	      break;

	    case DT_RELASZ:
	      /* Don't count procedure linkage table relocs in the
		 overall reloc count.  */
	      s = htab->relplt;
	      if (s == NULL)
		continue;
	      dyn.d_un.d_val -= s->size;
	      break;

	    case DT_RELA:
	      /* We may not be using the standard ELF linker script.
		 If .rela.plt is the first .rela section, we adjust
		 DT_RELA to not include it.  */
	      s = htab->relplt;
	      if (s == NULL)
		continue;
	      if (dyn.d_un.d_ptr != s->output_section->vma + s->output_offset)
		continue;
	      dyn.d_un.d_ptr += s->size;
	      break;
	    }

	  bfd_elf64_swap_dyn_out (output_bfd, &dyn, dyncon);
	}
    }

  if (htab->got != NULL && htab->got->size != 0)
    {
      /* Fill in the first entry in the global offset table.
	 We use it to hold the link-time TOCbase.  */
      bfd_put_64 (output_bfd,
		  elf_gp (output_bfd) + TOC_BASE_OFF,
		  htab->got->contents);

      /* Set .got entry size.  */
      elf_section_data (htab->got->output_section)->this_hdr.sh_entsize = 8;
    }

  if (htab->plt != NULL && htab->plt->size != 0)
    {
      /* Set .plt entry size.  */
      elf_section_data (htab->plt->output_section)->this_hdr.sh_entsize
	= PLT_ENTRY_SIZE;
    }

  /* brlt is SEC_LINKER_CREATED, so we need to write out relocs for
     brlt ourselves if emitrelocations.  */
  if (htab->brlt != NULL
      && htab->brlt->reloc_count != 0
      && !_bfd_elf_link_output_relocs (output_bfd,
				       htab->brlt,
				       &elf_section_data (htab->brlt)->rel_hdr,
				       elf_section_data (htab->brlt)->relocs,
				       NULL))
    return FALSE;

  /* We need to handle writing out multiple GOT sections ourselves,
     since we didn't add them to DYNOBJ.  We know dynobj is the first
     bfd.  */
  while ((dynobj = dynobj->link_next) != NULL)
    {
      asection *s;

      if (!is_ppc64_elf_target (dynobj->xvec))
	continue;

      s = ppc64_elf_tdata (dynobj)->got;
      if (s != NULL
	  && s->size != 0
	  && s->output_section != bfd_abs_section_ptr
	  && !bfd_set_section_contents (output_bfd, s->output_section,
					s->contents, s->output_offset,
					s->size))
	return FALSE;
      s = ppc64_elf_tdata (dynobj)->relgot;
      if (s != NULL
	  && s->size != 0
	  && s->output_section != bfd_abs_section_ptr
	  && !bfd_set_section_contents (output_bfd, s->output_section,
					s->contents, s->output_offset,
					s->size))
	return FALSE;
    }

  return TRUE;
}

#include "elf64-target.h"
