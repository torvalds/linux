/* Intel 80386/80486-specific support for 32-bit ELF
   Copyright 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005, 2006, 2007 Free Software Foundation, Inc.

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
#include "elf-vxworks.h"

/* 386 uses REL relocations instead of RELA.  */
#define USE_REL	1

#include "elf/i386.h"

static reloc_howto_type elf_howto_table[]=
{
  HOWTO(R_386_NONE, 0, 0, 0, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_NONE",
	TRUE, 0x00000000, 0x00000000, FALSE),
  HOWTO(R_386_32, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_32",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_PC32, 0, 2, 32, TRUE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_PC32",
	TRUE, 0xffffffff, 0xffffffff, TRUE),
  HOWTO(R_386_GOT32, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_GOT32",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_PLT32, 0, 2, 32, TRUE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_PLT32",
	TRUE, 0xffffffff, 0xffffffff, TRUE),
  HOWTO(R_386_COPY, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_COPY",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_GLOB_DAT, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_GLOB_DAT",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_JUMP_SLOT, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_JUMP_SLOT",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_RELATIVE, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_RELATIVE",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_GOTOFF, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_GOTOFF",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_GOTPC, 0, 2, 32, TRUE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_GOTPC",
	TRUE, 0xffffffff, 0xffffffff, TRUE),

  /* We have a gap in the reloc numbers here.
     R_386_standard counts the number up to this point, and
     R_386_ext_offset is the value to subtract from a reloc type of
     R_386_16 thru R_386_PC8 to form an index into this table.  */
#define R_386_standard (R_386_GOTPC + 1)
#define R_386_ext_offset (R_386_TLS_TPOFF - R_386_standard)

  /* These relocs are a GNU extension.  */
  HOWTO(R_386_TLS_TPOFF, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_TPOFF",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_TLS_IE, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_IE",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_TLS_GOTIE, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_GOTIE",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_TLS_LE, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_LE",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_TLS_GD, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_GD",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_TLS_LDM, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_LDM",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_16, 0, 1, 16, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_16",
	TRUE, 0xffff, 0xffff, FALSE),
  HOWTO(R_386_PC16, 0, 1, 16, TRUE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_PC16",
	TRUE, 0xffff, 0xffff, TRUE),
  HOWTO(R_386_8, 0, 0, 8, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_8",
	TRUE, 0xff, 0xff, FALSE),
  HOWTO(R_386_PC8, 0, 0, 8, TRUE, 0, complain_overflow_signed,
	bfd_elf_generic_reloc, "R_386_PC8",
	TRUE, 0xff, 0xff, TRUE),

#define R_386_ext (R_386_PC8 + 1 - R_386_ext_offset)
#define R_386_tls_offset (R_386_TLS_LDO_32 - R_386_ext)
  /* These are common with Solaris TLS implementation.  */
  HOWTO(R_386_TLS_LDO_32, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_LDO_32",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_TLS_IE_32, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_IE_32",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_TLS_LE_32, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_LE_32",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_TLS_DTPMOD32, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_DTPMOD32",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_TLS_DTPOFF32, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_DTPOFF32",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_TLS_TPOFF32, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_TPOFF32",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  EMPTY_HOWTO (38),
  HOWTO(R_386_TLS_GOTDESC, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_GOTDESC",
	TRUE, 0xffffffff, 0xffffffff, FALSE),
  HOWTO(R_386_TLS_DESC_CALL, 0, 0, 0, FALSE, 0, complain_overflow_dont,
	bfd_elf_generic_reloc, "R_386_TLS_DESC_CALL",
	FALSE, 0, 0, FALSE),
  HOWTO(R_386_TLS_DESC, 0, 2, 32, FALSE, 0, complain_overflow_bitfield,
	bfd_elf_generic_reloc, "R_386_TLS_DESC",
	TRUE, 0xffffffff, 0xffffffff, FALSE),

  /* Another gap.  */
#define R_386_tls (R_386_TLS_DESC + 1 - R_386_tls_offset)
#define R_386_vt_offset (R_386_GNU_VTINHERIT - R_386_tls)

/* GNU extension to record C++ vtable hierarchy.  */
  HOWTO (R_386_GNU_VTINHERIT,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 NULL,			/* special_function */
	 "R_386_GNU_VTINHERIT",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

/* GNU extension to record C++ vtable member usage.  */
  HOWTO (R_386_GNU_VTENTRY,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 _bfd_elf_rel_vtable_reloc_fn, /* special_function */
	 "R_386_GNU_VTENTRY",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE)			/* pcrel_offset */

#define R_386_vt (R_386_GNU_VTENTRY + 1 - R_386_vt_offset)

};

#ifdef DEBUG_GEN_RELOC
#define TRACE(str) \
  fprintf (stderr, "i386 bfd reloc lookup %d (%s)\n", code, str)
#else
#define TRACE(str)
#endif

static reloc_howto_type *
elf_i386_reloc_type_lookup (bfd *abfd ATTRIBUTE_UNUSED,
			    bfd_reloc_code_real_type code)
{
  switch (code)
    {
    case BFD_RELOC_NONE:
      TRACE ("BFD_RELOC_NONE");
      return &elf_howto_table[R_386_NONE];

    case BFD_RELOC_32:
      TRACE ("BFD_RELOC_32");
      return &elf_howto_table[R_386_32];

    case BFD_RELOC_CTOR:
      TRACE ("BFD_RELOC_CTOR");
      return &elf_howto_table[R_386_32];

    case BFD_RELOC_32_PCREL:
      TRACE ("BFD_RELOC_PC32");
      return &elf_howto_table[R_386_PC32];

    case BFD_RELOC_386_GOT32:
      TRACE ("BFD_RELOC_386_GOT32");
      return &elf_howto_table[R_386_GOT32];

    case BFD_RELOC_386_PLT32:
      TRACE ("BFD_RELOC_386_PLT32");
      return &elf_howto_table[R_386_PLT32];

    case BFD_RELOC_386_COPY:
      TRACE ("BFD_RELOC_386_COPY");
      return &elf_howto_table[R_386_COPY];

    case BFD_RELOC_386_GLOB_DAT:
      TRACE ("BFD_RELOC_386_GLOB_DAT");
      return &elf_howto_table[R_386_GLOB_DAT];

    case BFD_RELOC_386_JUMP_SLOT:
      TRACE ("BFD_RELOC_386_JUMP_SLOT");
      return &elf_howto_table[R_386_JUMP_SLOT];

    case BFD_RELOC_386_RELATIVE:
      TRACE ("BFD_RELOC_386_RELATIVE");
      return &elf_howto_table[R_386_RELATIVE];

    case BFD_RELOC_386_GOTOFF:
      TRACE ("BFD_RELOC_386_GOTOFF");
      return &elf_howto_table[R_386_GOTOFF];

    case BFD_RELOC_386_GOTPC:
      TRACE ("BFD_RELOC_386_GOTPC");
      return &elf_howto_table[R_386_GOTPC];

      /* These relocs are a GNU extension.  */
    case BFD_RELOC_386_TLS_TPOFF:
      TRACE ("BFD_RELOC_386_TLS_TPOFF");
      return &elf_howto_table[R_386_TLS_TPOFF - R_386_ext_offset];

    case BFD_RELOC_386_TLS_IE:
      TRACE ("BFD_RELOC_386_TLS_IE");
      return &elf_howto_table[R_386_TLS_IE - R_386_ext_offset];

    case BFD_RELOC_386_TLS_GOTIE:
      TRACE ("BFD_RELOC_386_TLS_GOTIE");
      return &elf_howto_table[R_386_TLS_GOTIE - R_386_ext_offset];

    case BFD_RELOC_386_TLS_LE:
      TRACE ("BFD_RELOC_386_TLS_LE");
      return &elf_howto_table[R_386_TLS_LE - R_386_ext_offset];

    case BFD_RELOC_386_TLS_GD:
      TRACE ("BFD_RELOC_386_TLS_GD");
      return &elf_howto_table[R_386_TLS_GD - R_386_ext_offset];

    case BFD_RELOC_386_TLS_LDM:
      TRACE ("BFD_RELOC_386_TLS_LDM");
      return &elf_howto_table[R_386_TLS_LDM - R_386_ext_offset];

    case BFD_RELOC_16:
      TRACE ("BFD_RELOC_16");
      return &elf_howto_table[R_386_16 - R_386_ext_offset];

    case BFD_RELOC_16_PCREL:
      TRACE ("BFD_RELOC_16_PCREL");
      return &elf_howto_table[R_386_PC16 - R_386_ext_offset];

    case BFD_RELOC_8:
      TRACE ("BFD_RELOC_8");
      return &elf_howto_table[R_386_8 - R_386_ext_offset];

    case BFD_RELOC_8_PCREL:
      TRACE ("BFD_RELOC_8_PCREL");
      return &elf_howto_table[R_386_PC8 - R_386_ext_offset];

    /* Common with Sun TLS implementation.  */
    case BFD_RELOC_386_TLS_LDO_32:
      TRACE ("BFD_RELOC_386_TLS_LDO_32");
      return &elf_howto_table[R_386_TLS_LDO_32 - R_386_tls_offset];

    case BFD_RELOC_386_TLS_IE_32:
      TRACE ("BFD_RELOC_386_TLS_IE_32");
      return &elf_howto_table[R_386_TLS_IE_32 - R_386_tls_offset];

    case BFD_RELOC_386_TLS_LE_32:
      TRACE ("BFD_RELOC_386_TLS_LE_32");
      return &elf_howto_table[R_386_TLS_LE_32 - R_386_tls_offset];

    case BFD_RELOC_386_TLS_DTPMOD32:
      TRACE ("BFD_RELOC_386_TLS_DTPMOD32");
      return &elf_howto_table[R_386_TLS_DTPMOD32 - R_386_tls_offset];

    case BFD_RELOC_386_TLS_DTPOFF32:
      TRACE ("BFD_RELOC_386_TLS_DTPOFF32");
      return &elf_howto_table[R_386_TLS_DTPOFF32 - R_386_tls_offset];

    case BFD_RELOC_386_TLS_TPOFF32:
      TRACE ("BFD_RELOC_386_TLS_TPOFF32");
      return &elf_howto_table[R_386_TLS_TPOFF32 - R_386_tls_offset];

    case BFD_RELOC_386_TLS_GOTDESC:
      TRACE ("BFD_RELOC_386_TLS_GOTDESC");
      return &elf_howto_table[R_386_TLS_GOTDESC - R_386_tls_offset];

    case BFD_RELOC_386_TLS_DESC_CALL:
      TRACE ("BFD_RELOC_386_TLS_DESC_CALL");
      return &elf_howto_table[R_386_TLS_DESC_CALL - R_386_tls_offset];

    case BFD_RELOC_386_TLS_DESC:
      TRACE ("BFD_RELOC_386_TLS_DESC");
      return &elf_howto_table[R_386_TLS_DESC - R_386_tls_offset];

    case BFD_RELOC_VTABLE_INHERIT:
      TRACE ("BFD_RELOC_VTABLE_INHERIT");
      return &elf_howto_table[R_386_GNU_VTINHERIT - R_386_vt_offset];

    case BFD_RELOC_VTABLE_ENTRY:
      TRACE ("BFD_RELOC_VTABLE_ENTRY");
      return &elf_howto_table[R_386_GNU_VTENTRY - R_386_vt_offset];

    default:
      break;
    }

  TRACE ("Unknown");
  return 0;
}

static reloc_howto_type *
elf_i386_reloc_name_lookup (bfd *abfd ATTRIBUTE_UNUSED,
			    const char *r_name)
{
  unsigned int i;

  for (i = 0; i < sizeof (elf_howto_table) / sizeof (elf_howto_table[0]); i++)
    if (elf_howto_table[i].name != NULL
	&& strcasecmp (elf_howto_table[i].name, r_name) == 0)
      return &elf_howto_table[i];

  return NULL;
}

static void
elf_i386_info_to_howto_rel (bfd *abfd ATTRIBUTE_UNUSED,
			    arelent *cache_ptr,
			    Elf_Internal_Rela *dst)
{
  unsigned int r_type = ELF32_R_TYPE (dst->r_info);
  unsigned int indx;

  if ((indx = r_type) >= R_386_standard
      && ((indx = r_type - R_386_ext_offset) - R_386_standard
	  >= R_386_ext - R_386_standard)
      && ((indx = r_type - R_386_tls_offset) - R_386_ext
	  >= R_386_tls - R_386_ext)
      && ((indx = r_type - R_386_vt_offset) - R_386_tls
	  >= R_386_vt - R_386_tls))
    {
      (*_bfd_error_handler) (_("%B: invalid relocation type %d"),
			     abfd, (int) r_type);
      indx = R_386_NONE;
    }
  cache_ptr->howto = &elf_howto_table[indx];
}

/* Return whether a symbol name implies a local label.  The UnixWare
   2.1 cc generates temporary symbols that start with .X, so we
   recognize them here.  FIXME: do other SVR4 compilers also use .X?.
   If so, we should move the .X recognition into
   _bfd_elf_is_local_label_name.  */

static bfd_boolean
elf_i386_is_local_label_name (bfd *abfd, const char *name)
{
  if (name[0] == '.' && name[1] == 'X')
    return TRUE;

  return _bfd_elf_is_local_label_name (abfd, name);
}

/* Support for core dump NOTE sections.  */

static bfd_boolean
elf_i386_grok_prstatus (bfd *abfd, Elf_Internal_Note *note)
{
  int offset;
  size_t size;

  if (note->namesz == 8 && strcmp (note->namedata, "FreeBSD") == 0)
    {
      int pr_version = bfd_get_32 (abfd, note->descdata);

      if (pr_version != 1)
 	return FALSE;

      /* pr_cursig */
      elf_tdata (abfd)->core_signal = bfd_get_32 (abfd, note->descdata + 20);

      /* pr_pid */
      elf_tdata (abfd)->core_pid = bfd_get_32 (abfd, note->descdata + 24);

      /* pr_reg */
      offset = 28;
      size = bfd_get_32 (abfd, note->descdata + 8);
    }
  else
    {
      switch (note->descsz)
	{
	default:
	  return FALSE;

	case 144:		/* Linux/i386 */
	  /* pr_cursig */
	  elf_tdata (abfd)->core_signal = bfd_get_16 (abfd, note->descdata + 12);

	  /* pr_pid */
	  elf_tdata (abfd)->core_pid = bfd_get_32 (abfd, note->descdata + 24);

	  /* pr_reg */
	  offset = 72;
	  size = 68;

	  break;
	}
    }

  /* Make a ".reg/999" section.  */
  return _bfd_elfcore_make_pseudosection (abfd, ".reg",
					  size, note->descpos + offset);
}

static bfd_boolean
elf_i386_grok_psinfo (bfd *abfd, Elf_Internal_Note *note)
{
  if (note->namesz == 8 && strcmp (note->namedata, "FreeBSD") == 0)
    {
      int pr_version = bfd_get_32 (abfd, note->descdata);

      if (pr_version != 1)
	return FALSE;

      elf_tdata (abfd)->core_program
	= _bfd_elfcore_strndup (abfd, note->descdata + 8, 17);
      elf_tdata (abfd)->core_command
	= _bfd_elfcore_strndup (abfd, note->descdata + 25, 81);
    }
  else
    {
      switch (note->descsz)
	{
	default:
	  return FALSE;

	case 124:		/* Linux/i386 elf_prpsinfo.  */
	  elf_tdata (abfd)->core_program
	    = _bfd_elfcore_strndup (abfd, note->descdata + 28, 16);
	  elf_tdata (abfd)->core_command
	    = _bfd_elfcore_strndup (abfd, note->descdata + 44, 80);
	}
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

/* Functions for the i386 ELF linker.

   In order to gain some understanding of code in this file without
   knowing all the intricate details of the linker, note the
   following:

   Functions named elf_i386_* are called by external routines, other
   functions are only called locally.  elf_i386_* functions appear
   in this file more or less in the order in which they are called
   from external routines.  eg. elf_i386_check_relocs is called
   early in the link process, elf_i386_finish_dynamic_sections is
   one of the last functions.  */


/* The name of the dynamic interpreter.  This is put in the .interp
   section.  */

#define ELF_DYNAMIC_INTERPRETER "/usr/lib/libc.so.1"

/* If ELIMINATE_COPY_RELOCS is non-zero, the linker will try to avoid
   copying dynamic variables from a shared lib into an app's dynbss
   section, and instead use a dynamic relocation to point into the
   shared lib.  */
#define ELIMINATE_COPY_RELOCS 1

/* The size in bytes of an entry in the procedure linkage table.  */

#define PLT_ENTRY_SIZE 16

/* The first entry in an absolute procedure linkage table looks like
   this.  See the SVR4 ABI i386 supplement to see how this works.
   Will be padded to PLT_ENTRY_SIZE with htab->plt0_pad_byte.  */

static const bfd_byte elf_i386_plt0_entry[12] =
{
  0xff, 0x35,	/* pushl contents of address */
  0, 0, 0, 0,	/* replaced with address of .got + 4.  */
  0xff, 0x25,	/* jmp indirect */
  0, 0, 0, 0	/* replaced with address of .got + 8.  */
};

/* Subsequent entries in an absolute procedure linkage table look like
   this.  */

static const bfd_byte elf_i386_plt_entry[PLT_ENTRY_SIZE] =
{
  0xff, 0x25,	/* jmp indirect */
  0, 0, 0, 0,	/* replaced with address of this symbol in .got.  */
  0x68,		/* pushl immediate */
  0, 0, 0, 0,	/* replaced with offset into relocation table.  */
  0xe9,		/* jmp relative */
  0, 0, 0, 0	/* replaced with offset to start of .plt.  */
};

/* The first entry in a PIC procedure linkage table look like this.
   Will be padded to PLT_ENTRY_SIZE with htab->plt0_pad_byte.  */

static const bfd_byte elf_i386_pic_plt0_entry[12] =
{
  0xff, 0xb3, 4, 0, 0, 0,	/* pushl 4(%ebx) */
  0xff, 0xa3, 8, 0, 0, 0	/* jmp *8(%ebx) */
};

/* Subsequent entries in a PIC procedure linkage table look like this.  */

static const bfd_byte elf_i386_pic_plt_entry[PLT_ENTRY_SIZE] =
{
  0xff, 0xa3,	/* jmp *offset(%ebx) */
  0, 0, 0, 0,	/* replaced with offset of this symbol in .got.  */
  0x68,		/* pushl immediate */
  0, 0, 0, 0,	/* replaced with offset into relocation table.  */
  0xe9,		/* jmp relative */
  0, 0, 0, 0	/* replaced with offset to start of .plt.  */
};

/* On VxWorks, the .rel.plt.unloaded section has absolute relocations
   for the PLTResolve stub and then for each PLT entry.  */
#define PLTRESOLVE_RELOCS_SHLIB 0
#define PLTRESOLVE_RELOCS 2
#define PLT_NON_JUMP_SLOT_RELOCS 2

/* The i386 linker needs to keep track of the number of relocs that it
   decides to copy as dynamic relocs in check_relocs for each symbol.
   This is so that it can later discard them if they are found to be
   unnecessary.  We store the information in a field extending the
   regular ELF linker hash table.  */

struct elf_i386_dyn_relocs
{
  struct elf_i386_dyn_relocs *next;

  /* The input section of the reloc.  */
  asection *sec;

  /* Total number of relocs copied for the input section.  */
  bfd_size_type count;

  /* Number of pc-relative relocs copied for the input section.  */
  bfd_size_type pc_count;
};

/* i386 ELF linker hash entry.  */

struct elf_i386_link_hash_entry
{
  struct elf_link_hash_entry elf;

  /* Track dynamic relocs copied for this symbol.  */
  struct elf_i386_dyn_relocs *dyn_relocs;

#define GOT_UNKNOWN	0
#define GOT_NORMAL	1
#define GOT_TLS_GD	2
#define GOT_TLS_IE	4
#define GOT_TLS_IE_POS	5
#define GOT_TLS_IE_NEG	6
#define GOT_TLS_IE_BOTH 7
#define GOT_TLS_GDESC	8
#define GOT_TLS_GD_BOTH_P(type)						\
  ((type) == (GOT_TLS_GD | GOT_TLS_GDESC))
#define GOT_TLS_GD_P(type)						\
  ((type) == GOT_TLS_GD || GOT_TLS_GD_BOTH_P (type))
#define GOT_TLS_GDESC_P(type)						\
  ((type) == GOT_TLS_GDESC || GOT_TLS_GD_BOTH_P (type))
#define GOT_TLS_GD_ANY_P(type)						\
  (GOT_TLS_GD_P (type) || GOT_TLS_GDESC_P (type))
  unsigned char tls_type;

  /* Offset of the GOTPLT entry reserved for the TLS descriptor,
     starting at the end of the jump table.  */
  bfd_vma tlsdesc_got;
};

#define elf_i386_hash_entry(ent) ((struct elf_i386_link_hash_entry *)(ent))

struct elf_i386_obj_tdata
{
  struct elf_obj_tdata root;

  /* tls_type for each local got entry.  */
  char *local_got_tls_type;

  /* GOTPLT entries for TLS descriptors.  */
  bfd_vma *local_tlsdesc_gotent;
};

#define elf_i386_tdata(abfd) \
  ((struct elf_i386_obj_tdata *) (abfd)->tdata.any)

#define elf_i386_local_got_tls_type(abfd) \
  (elf_i386_tdata (abfd)->local_got_tls_type)

#define elf_i386_local_tlsdesc_gotent(abfd) \
  (elf_i386_tdata (abfd)->local_tlsdesc_gotent)

static bfd_boolean
elf_i386_mkobject (bfd *abfd)
{
  if (abfd->tdata.any == NULL)
    {
      bfd_size_type amt = sizeof (struct elf_i386_obj_tdata);
      abfd->tdata.any = bfd_zalloc (abfd, amt);
      if (abfd->tdata.any == NULL)
	return FALSE;
    }
  return bfd_elf_mkobject (abfd);
}

/* i386 ELF linker hash table.  */

struct elf_i386_link_hash_table
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

  /* The (unloaded but important) .rel.plt.unloaded section on VxWorks.  */
  asection *srelplt2;

  /* True if the target system is VxWorks.  */
  int is_vxworks;

  /* Value used to fill the last word of the first plt entry.  */
  bfd_byte plt0_pad_byte;

  /* The index of the next unused R_386_TLS_DESC slot in .rel.plt.  */
  bfd_vma next_tls_desc_index;

  union {
    bfd_signed_vma refcount;
    bfd_vma offset;
  } tls_ldm_got;

  /* The amount of space used by the reserved portion of the sgotplt
     section, plus whatever space is used by the jump slots.  */
  bfd_vma sgotplt_jump_table_size;

  /* Small local sym to section mapping cache.  */
  struct sym_sec_cache sym_sec;
};

/* Get the i386 ELF linker hash table from a link_info structure.  */

#define elf_i386_hash_table(p) \
  ((struct elf_i386_link_hash_table *) ((p)->hash))

#define elf_i386_compute_jump_table_size(htab) \
  ((htab)->next_tls_desc_index * 4)

/* Create an entry in an i386 ELF linker hash table.  */

static struct bfd_hash_entry *
link_hash_newfunc (struct bfd_hash_entry *entry,
		   struct bfd_hash_table *table,
		   const char *string)
{
  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (entry == NULL)
    {
      entry = bfd_hash_allocate (table,
				 sizeof (struct elf_i386_link_hash_entry));
      if (entry == NULL)
	return entry;
    }

  /* Call the allocation method of the superclass.  */
  entry = _bfd_elf_link_hash_newfunc (entry, table, string);
  if (entry != NULL)
    {
      struct elf_i386_link_hash_entry *eh;

      eh = (struct elf_i386_link_hash_entry *) entry;
      eh->dyn_relocs = NULL;
      eh->tls_type = GOT_UNKNOWN;
      eh->tlsdesc_got = (bfd_vma) -1;
    }

  return entry;
}

/* Create an i386 ELF linker hash table.  */

static struct bfd_link_hash_table *
elf_i386_link_hash_table_create (bfd *abfd)
{
  struct elf_i386_link_hash_table *ret;
  bfd_size_type amt = sizeof (struct elf_i386_link_hash_table);

  ret = bfd_malloc (amt);
  if (ret == NULL)
    return NULL;

  if (!_bfd_elf_link_hash_table_init (&ret->elf, abfd, link_hash_newfunc,
				      sizeof (struct elf_i386_link_hash_entry)))
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
  ret->tls_ldm_got.refcount = 0;
  ret->next_tls_desc_index = 0;
  ret->sgotplt_jump_table_size = 0;
  ret->sym_sec.abfd = NULL;
  ret->is_vxworks = 0;
  ret->srelplt2 = NULL;
  ret->plt0_pad_byte = 0;

  return &ret->elf.root;
}

/* Create .got, .gotplt, and .rel.got sections in DYNOBJ, and set up
   shortcuts to them in our hash table.  */

static bfd_boolean
create_got_section (bfd *dynobj, struct bfd_link_info *info)
{
  struct elf_i386_link_hash_table *htab;

  if (! _bfd_elf_create_got_section (dynobj, info))
    return FALSE;

  htab = elf_i386_hash_table (info);
  htab->sgot = bfd_get_section_by_name (dynobj, ".got");
  htab->sgotplt = bfd_get_section_by_name (dynobj, ".got.plt");
  if (!htab->sgot || !htab->sgotplt)
    abort ();

  htab->srelgot = bfd_make_section_with_flags (dynobj, ".rel.got",
					       (SEC_ALLOC | SEC_LOAD
						| SEC_HAS_CONTENTS
						| SEC_IN_MEMORY
						| SEC_LINKER_CREATED
						| SEC_READONLY));
  if (htab->srelgot == NULL
      || ! bfd_set_section_alignment (dynobj, htab->srelgot, 2))
    return FALSE;
  return TRUE;
}

/* Create .plt, .rel.plt, .got, .got.plt, .rel.got, .dynbss, and
   .rel.bss sections in DYNOBJ, and set up shortcuts to them in our
   hash table.  */

static bfd_boolean
elf_i386_create_dynamic_sections (bfd *dynobj, struct bfd_link_info *info)
{
  struct elf_i386_link_hash_table *htab;

  htab = elf_i386_hash_table (info);
  if (!htab->sgot && !create_got_section (dynobj, info))
    return FALSE;

  if (!_bfd_elf_create_dynamic_sections (dynobj, info))
    return FALSE;

  htab->splt = bfd_get_section_by_name (dynobj, ".plt");
  htab->srelplt = bfd_get_section_by_name (dynobj, ".rel.plt");
  htab->sdynbss = bfd_get_section_by_name (dynobj, ".dynbss");
  if (!info->shared)
    htab->srelbss = bfd_get_section_by_name (dynobj, ".rel.bss");

  if (!htab->splt || !htab->srelplt || !htab->sdynbss
      || (!info->shared && !htab->srelbss))
    abort ();

  if (htab->is_vxworks
      && !elf_vxworks_create_dynamic_sections (dynobj, info, &htab->srelplt2))
    return FALSE;

  return TRUE;
}

/* Copy the extra info we tack onto an elf_link_hash_entry.  */

static void
elf_i386_copy_indirect_symbol (struct bfd_link_info *info,
			       struct elf_link_hash_entry *dir,
			       struct elf_link_hash_entry *ind)
{
  struct elf_i386_link_hash_entry *edir, *eind;

  edir = (struct elf_i386_link_hash_entry *) dir;
  eind = (struct elf_i386_link_hash_entry *) ind;

  if (eind->dyn_relocs != NULL)
    {
      if (edir->dyn_relocs != NULL)
	{
	  struct elf_i386_dyn_relocs **pp;
	  struct elf_i386_dyn_relocs *p;

	  /* Add reloc counts against the indirect sym to the direct sym
	     list.  Merge any entries against the same section.  */
	  for (pp = &eind->dyn_relocs; (p = *pp) != NULL; )
	    {
	      struct elf_i386_dyn_relocs *q;

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

static int
elf_i386_tls_transition (struct bfd_link_info *info, int r_type, int is_local)
{
  if (info->shared)
    return r_type;

  switch (r_type)
    {
    case R_386_TLS_GD:
    case R_386_TLS_GOTDESC:
    case R_386_TLS_DESC_CALL:
    case R_386_TLS_IE_32:
      if (is_local)
	return R_386_TLS_LE_32;
      return R_386_TLS_IE_32;
    case R_386_TLS_IE:
    case R_386_TLS_GOTIE:
      if (is_local)
	return R_386_TLS_LE_32;
      return r_type;
    case R_386_TLS_LDM:
      return R_386_TLS_LE_32;
    }

  return r_type;
}

/* Look through the relocs for a section during the first phase, and
   calculate needed space in the global offset table, procedure linkage
   table, and dynamic reloc sections.  */

static bfd_boolean
elf_i386_check_relocs (bfd *abfd,
		       struct bfd_link_info *info,
		       asection *sec,
		       const Elf_Internal_Rela *relocs)
{
  struct elf_i386_link_hash_table *htab;
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  const Elf_Internal_Rela *rel;
  const Elf_Internal_Rela *rel_end;
  asection *sreloc;

  if (info->relocatable)
    return TRUE;

  htab = elf_i386_hash_table (info);
  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (abfd);

  sreloc = NULL;

  rel_end = relocs + sec->reloc_count;
  for (rel = relocs; rel < rel_end; rel++)
    {
      unsigned int r_type;
      unsigned long r_symndx;
      struct elf_link_hash_entry *h;

      r_symndx = ELF32_R_SYM (rel->r_info);
      r_type = ELF32_R_TYPE (rel->r_info);

      if (r_symndx >= NUM_SHDR_ENTRIES (symtab_hdr))
	{
	  (*_bfd_error_handler) (_("%B: bad symbol index: %d"),
				 abfd,
				 r_symndx);
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

      r_type = elf_i386_tls_transition (info, r_type, h == NULL);

      switch (r_type)
	{
	case R_386_TLS_LDM:
	  htab->tls_ldm_got.refcount += 1;
	  goto create_got;

	case R_386_PLT32:
	  /* This symbol requires a procedure linkage table entry.  We
	     actually build the entry in adjust_dynamic_symbol,
	     because this might be a case of linking PIC code which is
	     never referenced by a dynamic object, in which case we
	     don't need to generate a procedure linkage table entry
	     after all.  */

	  /* If this is a local symbol, we resolve it directly without
	     creating a procedure linkage table entry.  */
	  if (h == NULL)
	    continue;

	  h->needs_plt = 1;
	  h->plt.refcount += 1;
	  break;

	case R_386_TLS_IE_32:
	case R_386_TLS_IE:
	case R_386_TLS_GOTIE:
	  if (info->shared)
	    info->flags |= DF_STATIC_TLS;
	  /* Fall through */

	case R_386_GOT32:
	case R_386_TLS_GD:
	case R_386_TLS_GOTDESC:
	case R_386_TLS_DESC_CALL:
	  /* This symbol requires a global offset table entry.  */
	  {
	    int tls_type, old_tls_type;

	    switch (r_type)
	      {
	      default:
	      case R_386_GOT32: tls_type = GOT_NORMAL; break;
	      case R_386_TLS_GD: tls_type = GOT_TLS_GD; break;
	      case R_386_TLS_GOTDESC:
	      case R_386_TLS_DESC_CALL:
		tls_type = GOT_TLS_GDESC; break;
	      case R_386_TLS_IE_32:
		if (ELF32_R_TYPE (rel->r_info) == r_type)
		  tls_type = GOT_TLS_IE_NEG;
		else
		  /* If this is a GD->IE transition, we may use either of
		     R_386_TLS_TPOFF and R_386_TLS_TPOFF32.  */
		  tls_type = GOT_TLS_IE;
		break;
	      case R_386_TLS_IE:
	      case R_386_TLS_GOTIE:
		tls_type = GOT_TLS_IE_POS; break;
	      }

	    if (h != NULL)
	      {
		h->got.refcount += 1;
		old_tls_type = elf_i386_hash_entry(h)->tls_type;
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
		    size *= (sizeof (bfd_signed_vma)
			     + sizeof (bfd_vma) + sizeof(char));
		    local_got_refcounts = bfd_zalloc (abfd, size);
		    if (local_got_refcounts == NULL)
		      return FALSE;
		    elf_local_got_refcounts (abfd) = local_got_refcounts;
		    elf_i386_local_tlsdesc_gotent (abfd)
		      = (bfd_vma *) (local_got_refcounts + symtab_hdr->sh_info);
		    elf_i386_local_got_tls_type (abfd)
		      = (char *) (local_got_refcounts + 2 * symtab_hdr->sh_info);
		  }
		local_got_refcounts[r_symndx] += 1;
		old_tls_type = elf_i386_local_got_tls_type (abfd) [r_symndx];
	      }

	    if ((old_tls_type & GOT_TLS_IE) && (tls_type & GOT_TLS_IE))
	      tls_type |= old_tls_type;
	    /* If a TLS symbol is accessed using IE at least once,
	       there is no point to use dynamic model for it.  */
	    else if (old_tls_type != tls_type && old_tls_type != GOT_UNKNOWN
		     && (! GOT_TLS_GD_ANY_P (old_tls_type)
			 || (tls_type & GOT_TLS_IE) == 0))
	      {
		if ((old_tls_type & GOT_TLS_IE) && GOT_TLS_GD_ANY_P (tls_type))
		  tls_type = old_tls_type;
		else if (GOT_TLS_GD_ANY_P (old_tls_type)
			 && GOT_TLS_GD_ANY_P (tls_type))
		  tls_type |= old_tls_type;
		else
		  {
		    (*_bfd_error_handler)
		      (_("%B: `%s' accessed both as normal and "
			 "thread local symbol"),
		       abfd,
		       h ? h->root.root.string : "<local>");
		    return FALSE;
		  }
	      }

	    if (old_tls_type != tls_type)
	      {
		if (h != NULL)
		  elf_i386_hash_entry (h)->tls_type = tls_type;
		else
		  elf_i386_local_got_tls_type (abfd) [r_symndx] = tls_type;
	      }
	  }
	  /* Fall through */

	case R_386_GOTOFF:
	case R_386_GOTPC:
	create_got:
	  if (htab->sgot == NULL)
	    {
	      if (htab->elf.dynobj == NULL)
		htab->elf.dynobj = abfd;
	      if (!create_got_section (htab->elf.dynobj, info))
		return FALSE;
	    }
	  if (r_type != R_386_TLS_IE)
	    break;
	  /* Fall through */

	case R_386_TLS_LE_32:
	case R_386_TLS_LE:
	  if (!info->shared)
	    break;
	  info->flags |= DF_STATIC_TLS;
	  /* Fall through */

	case R_386_32:
	case R_386_PC32:
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
	      if (r_type != R_386_PC32)
		h->pointer_equality_needed = 1;
	    }

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
	       && (r_type != R_386_PC32
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
	      struct elf_i386_dyn_relocs *p;
	      struct elf_i386_dyn_relocs **head;

	      /* We must copy these reloc types into the output file.
		 Create a reloc section in dynobj and make room for
		 this reloc.  */
	      if (sreloc == NULL)
		{
		  const char *name;
		  bfd *dynobj;
		  unsigned int strndx = elf_elfheader (abfd)->e_shstrndx;
		  unsigned int shnam = elf_section_data (sec)->rel_hdr.sh_name;

		  name = bfd_elf_string_from_elf_section (abfd, strndx, shnam);
		  if (name == NULL)
		    return FALSE;

		  if (! CONST_STRNEQ (name, ".rel")
		      || strcmp (bfd_get_section_name (abfd, sec),
				 name + 4) != 0)
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
			  || ! bfd_set_section_alignment (dynobj, sreloc, 2))
			return FALSE;
		    }
		  elf_section_data (sec)->sreloc = sreloc;
		}

	      /* If this is a global symbol, we count the number of
		 relocations we need for this symbol.  */
	      if (h != NULL)
		{
		  head = &((struct elf_i386_link_hash_entry *) h)->dyn_relocs;
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

		  vpp = &elf_section_data (s)->local_dynrel;
		  head = (struct elf_i386_dyn_relocs **)vpp;
		}

	      p = *head;
	      if (p == NULL || p->sec != sec)
		{
		  bfd_size_type amt = sizeof *p;
		  p = bfd_alloc (htab->elf.dynobj, amt);
		  if (p == NULL)
		    return FALSE;
		  p->next = *head;
		  *head = p;
		  p->sec = sec;
		  p->count = 0;
		  p->pc_count = 0;
		}

	      p->count += 1;
	      if (r_type == R_386_PC32)
		p->pc_count += 1;
	    }
	  break;

	  /* This relocation describes the C++ object vtable hierarchy.
	     Reconstruct it for later use during GC.  */
	case R_386_GNU_VTINHERIT:
	  if (!bfd_elf_gc_record_vtinherit (abfd, sec, h, rel->r_offset))
	    return FALSE;
	  break;

	  /* This relocation describes which C++ vtable entries are actually
	     used.  Record for later use during GC.  */
	case R_386_GNU_VTENTRY:
	  if (!bfd_elf_gc_record_vtentry (abfd, sec, h, rel->r_offset))
	    return FALSE;
	  break;

	default:
	  break;
	}
    }

  return TRUE;
}

/* Return the section that should be marked against GC for a given
   relocation.  */

static asection *
elf_i386_gc_mark_hook (asection *sec,
		       struct bfd_link_info *info,
		       Elf_Internal_Rela *rel,
		       struct elf_link_hash_entry *h,
		       Elf_Internal_Sym *sym)
{
  if (h != NULL)
    switch (ELF32_R_TYPE (rel->r_info))
      {
      case R_386_GNU_VTINHERIT:
      case R_386_GNU_VTENTRY:
	return NULL;
      }

  return _bfd_elf_gc_mark_hook (sec, info, rel, h, sym);
}

/* Update the got entry reference counts for the section being removed.  */

static bfd_boolean
elf_i386_gc_sweep_hook (bfd *abfd,
			struct bfd_link_info *info,
			asection *sec,
			const Elf_Internal_Rela *relocs)
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

      r_symndx = ELF32_R_SYM (rel->r_info);
      if (r_symndx >= symtab_hdr->sh_info)
	{
	  struct elf_i386_link_hash_entry *eh;
	  struct elf_i386_dyn_relocs **pp;
	  struct elf_i386_dyn_relocs *p;

	  h = sym_hashes[r_symndx - symtab_hdr->sh_info];
	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;
	  eh = (struct elf_i386_link_hash_entry *) h;

	  for (pp = &eh->dyn_relocs; (p = *pp) != NULL; pp = &p->next)
	    if (p->sec == sec)
	      {
		/* Everything must go for SEC.  */
		*pp = p->next;
		break;
	      }
	}

      r_type = ELF32_R_TYPE (rel->r_info);
      r_type = elf_i386_tls_transition (info, r_type, h != NULL);
      switch (r_type)
	{
	case R_386_TLS_LDM:
	  if (elf_i386_hash_table (info)->tls_ldm_got.refcount > 0)
	    elf_i386_hash_table (info)->tls_ldm_got.refcount -= 1;
	  break;

	case R_386_TLS_GD:
	case R_386_TLS_GOTDESC:
	case R_386_TLS_DESC_CALL:
	case R_386_TLS_IE_32:
	case R_386_TLS_IE:
	case R_386_TLS_GOTIE:
	case R_386_GOT32:
	  if (h != NULL)
	    {
	      if (h->got.refcount > 0)
		h->got.refcount -= 1;
	    }
	  else if (local_got_refcounts != NULL)
	    {
	      if (local_got_refcounts[r_symndx] > 0)
		local_got_refcounts[r_symndx] -= 1;
	    }
	  break;

	case R_386_32:
	case R_386_PC32:
	  if (info->shared)
	    break;
	  /* Fall through */

	case R_386_PLT32:
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
   understand.  */

static bfd_boolean
elf_i386_adjust_dynamic_symbol (struct bfd_link_info *info,
				struct elf_link_hash_entry *h)
{
  struct elf_i386_link_hash_table *htab;
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
       needed for an R_386_PC32 reloc to a non-function sym in
       check_relocs.  We can't decide accurately between function and
       non-function syms in check-relocs;  Objects loaded later in
       the link may change h->type.  So fix it now.  */
    h->plt.offset = (bfd_vma) -1;

  /* If this is a weak symbol, and there is a real definition, the
     processor independent code will have arranged for us to see the
     real definition first, and we can just use the same value.  */
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
     is not a function.  */

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

  /* If -z nocopyreloc was given, we won't generate them either.  */
  if (info->nocopyreloc)
    {
      h->non_got_ref = 0;
      return TRUE;
    }

  htab = elf_i386_hash_table (info);

  /* If there aren't any dynamic relocs in read-only sections, then
     we can keep the dynamic relocs and avoid the copy reloc.  This
     doesn't work on VxWorks, where we can not have dynamic relocations
     (other than copy and jump slot relocations) in an executable.  */
  if (ELIMINATE_COPY_RELOCS && !htab->is_vxworks)
    {
      struct elf_i386_link_hash_entry * eh;
      struct elf_i386_dyn_relocs *p;

      eh = (struct elf_i386_link_hash_entry *) h;
      for (p = eh->dyn_relocs; p != NULL; p = p->next)
	{
	  s = p->sec->output_section;
	  if (s != NULL && (s->flags & SEC_READONLY) != 0)
	    break;
	}

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
     become part of the .bss section of the executable.  There will be
     an entry for this symbol in the .dynsym section.  The dynamic
     object will contain position independent code, so all references
     from the dynamic object to this symbol will go through the global
     offset table.  The dynamic linker will use the .dynsym entry to
     determine the address it must put in the global offset table, so
     both the dynamic object and the regular object will refer to the
     same memory location for the variable.  */

  /* We must generate a R_386_COPY reloc to tell the dynamic linker to
     copy the initial value out of the dynamic object and into the
     runtime process image.  */
  if ((h->root.u.def.section->flags & SEC_ALLOC) != 0)
    {
      htab->srelbss->size += sizeof (Elf32_External_Rel);
      h->needs_copy = 1;
    }

  s = htab->sdynbss;

  return _bfd_elf_adjust_dynamic_copy (h, s);
}

/* Allocate space in .plt, .got and associated reloc sections for
   dynamic relocs.  */

static bfd_boolean
allocate_dynrelocs (struct elf_link_hash_entry *h, void *inf)
{
  struct bfd_link_info *info;
  struct elf_i386_link_hash_table *htab;
  struct elf_i386_link_hash_entry *eh;
  struct elf_i386_dyn_relocs *p;

  if (h->root.type == bfd_link_hash_indirect)
    return TRUE;

  if (h->root.type == bfd_link_hash_warning)
    /* When warning symbols are created, they **replace** the "real"
       entry in the hash table, thus we never get to see the real
       symbol in a hash traversal.  So look at it now.  */
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  info = (struct bfd_link_info *) inf;
  htab = elf_i386_hash_table (info);

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
	  htab->sgotplt->size += 4;

	  /* We also need to make an entry in the .rel.plt section.  */
	  htab->srelplt->size += sizeof (Elf32_External_Rel);
	  htab->next_tls_desc_index++;

	  if (htab->is_vxworks && !info->shared)
	    {
	      /* VxWorks has a second set of relocations for each PLT entry
		 in executables.  They go in a separate relocation section,
		 which is processed by the kernel loader.  */

	      /* There are two relocations for the initial PLT entry: an
		 R_386_32 relocation for _GLOBAL_OFFSET_TABLE_ + 4 and an
		 R_386_32 relocation for _GLOBAL_OFFSET_TABLE_ + 8.  */

	      if (h->plt.offset == PLT_ENTRY_SIZE)
		htab->srelplt2->size += (sizeof (Elf32_External_Rel) * 2);

	      /* There are two extra relocations for each subsequent PLT entry:
		 an R_386_32 relocation for the GOT entry, and an R_386_32
		 relocation for the PLT entry.  */

	      htab->srelplt2->size += (sizeof (Elf32_External_Rel) * 2);
	    }
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

  eh = (struct elf_i386_link_hash_entry *) h;
  eh->tlsdesc_got = (bfd_vma) -1;

  /* If R_386_TLS_{IE_32,IE,GOTIE} symbol is now local to the binary,
     make it a R_386_TLS_LE_32 requiring no TLS entry.  */
  if (h->got.refcount > 0
      && !info->shared
      && h->dynindx == -1
      && (elf_i386_hash_entry(h)->tls_type & GOT_TLS_IE))
    h->got.offset = (bfd_vma) -1;
  else if (h->got.refcount > 0)
    {
      asection *s;
      bfd_boolean dyn;
      int tls_type = elf_i386_hash_entry(h)->tls_type;

      /* Make sure this symbol is output as a dynamic symbol.
	 Undefined weak syms won't yet be marked as dynamic.  */
      if (h->dynindx == -1
	  && !h->forced_local)
	{
	  if (! bfd_elf_link_record_dynamic_symbol (info, h))
	    return FALSE;
	}

      s = htab->sgot;
      if (GOT_TLS_GDESC_P (tls_type))
	{
	  eh->tlsdesc_got = htab->sgotplt->size
	    - elf_i386_compute_jump_table_size (htab);
	  htab->sgotplt->size += 8;
	  h->got.offset = (bfd_vma) -2;
	}
      if (! GOT_TLS_GDESC_P (tls_type)
	  || GOT_TLS_GD_P (tls_type))
	{
	  h->got.offset = s->size;
	  s->size += 4;
	  /* R_386_TLS_GD needs 2 consecutive GOT slots.  */
	  if (GOT_TLS_GD_P (tls_type) || tls_type == GOT_TLS_IE_BOTH)
	    s->size += 4;
	}
      dyn = htab->elf.dynamic_sections_created;
      /* R_386_TLS_IE_32 needs one dynamic relocation,
	 R_386_TLS_IE resp. R_386_TLS_GOTIE needs one dynamic relocation,
	 (but if both R_386_TLS_IE_32 and R_386_TLS_IE is present, we
	 need two), R_386_TLS_GD needs one if local symbol and two if
	 global.  */
      if (tls_type == GOT_TLS_IE_BOTH)
	htab->srelgot->size += 2 * sizeof (Elf32_External_Rel);
      else if ((GOT_TLS_GD_P (tls_type) && h->dynindx == -1)
	       || (tls_type & GOT_TLS_IE))
	htab->srelgot->size += sizeof (Elf32_External_Rel);
      else if (GOT_TLS_GD_P (tls_type))
	htab->srelgot->size += 2 * sizeof (Elf32_External_Rel);
      else if (! GOT_TLS_GDESC_P (tls_type)
	       && (ELF_ST_VISIBILITY (h->other) == STV_DEFAULT
		   || h->root.type != bfd_link_hash_undefweak)
	       && (info->shared
		   || WILL_CALL_FINISH_DYNAMIC_SYMBOL (dyn, 0, h)))
	htab->srelgot->size += sizeof (Elf32_External_Rel);
      if (GOT_TLS_GDESC_P (tls_type))
	htab->srelplt->size += sizeof (Elf32_External_Rel);
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
      /* The only reloc that uses pc_count is R_386_PC32, which will
	 appear on a call or on something like ".long foo - .".  We
	 want calls to protected symbols to resolve directly to the
	 function rather than going via the plt.  If people want
	 function pointer comparisons to work as expected then they
	 should avoid writing assembly like ".long foo - .".  */
      if (SYMBOL_CALLS_LOCAL (info, h))
	{
	  struct elf_i386_dyn_relocs **pp;

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
      sreloc->size += p->count * sizeof (Elf32_External_Rel);
    }

  return TRUE;
}

/* Find any dynamic relocs that apply to read-only sections.  */

static bfd_boolean
readonly_dynrelocs (struct elf_link_hash_entry *h, void *inf)
{
  struct elf_i386_link_hash_entry *eh;
  struct elf_i386_dyn_relocs *p;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  eh = (struct elf_i386_link_hash_entry *) h;
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
elf_i386_size_dynamic_sections (bfd *output_bfd ATTRIBUTE_UNUSED,
				struct bfd_link_info *info)
{
  struct elf_i386_link_hash_table *htab;
  bfd *dynobj;
  asection *s;
  bfd_boolean relocs;
  bfd *ibfd;

  htab = elf_i386_hash_table (info);
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
	  struct elf_i386_dyn_relocs *p;

	  for (p = ((struct elf_i386_dyn_relocs *)
		     elf_section_data (s)->local_dynrel);
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
		  srel->size += p->count * sizeof (Elf32_External_Rel);
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
      local_tls_type = elf_i386_local_got_tls_type (ibfd);
      local_tlsdesc_gotent = elf_i386_local_tlsdesc_gotent (ibfd);
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
		    - elf_i386_compute_jump_table_size (htab);
		  htab->sgotplt->size += 8;
		  *local_got = (bfd_vma) -2;
		}
	      if (! GOT_TLS_GDESC_P (*local_tls_type)
		  || GOT_TLS_GD_P (*local_tls_type))
		{
		  *local_got = s->size;
		  s->size += 4;
		  if (GOT_TLS_GD_P (*local_tls_type)
		      || *local_tls_type == GOT_TLS_IE_BOTH)
		    s->size += 4;
		}
	      if (info->shared
		  || GOT_TLS_GD_ANY_P (*local_tls_type)
		  || (*local_tls_type & GOT_TLS_IE))
		{
		  if (*local_tls_type == GOT_TLS_IE_BOTH)
		    srel->size += 2 * sizeof (Elf32_External_Rel);
		  else if (GOT_TLS_GD_P (*local_tls_type)
			   || ! GOT_TLS_GDESC_P (*local_tls_type))
		    srel->size += sizeof (Elf32_External_Rel);
		  if (GOT_TLS_GDESC_P (*local_tls_type))
		    htab->srelplt->size += sizeof (Elf32_External_Rel);
		}
	    }
	  else
	    *local_got = (bfd_vma) -1;
	}
    }

  if (htab->tls_ldm_got.refcount > 0)
    {
      /* Allocate 2 got entries and 1 dynamic reloc for R_386_TLS_LDM
	 relocs.  */
      htab->tls_ldm_got.offset = htab->sgot->size;
      htab->sgot->size += 8;
      htab->srelgot->size += sizeof (Elf32_External_Rel);
    }
  else
    htab->tls_ldm_got.offset = -1;

  /* Allocate global sym .plt and .got entries, and space for global
     sym dynamic relocs.  */
  elf_link_hash_traverse (&htab->elf, allocate_dynrelocs, (PTR) info);

  /* For every jump slot reserved in the sgotplt, reloc_count is
     incremented.  However, when we reserve space for TLS descriptors,
     it's not incremented, so in order to compute the space reserved
     for them, it suffices to multiply the reloc count by the jump
     slot size.  */
  if (htab->srelplt)
    htab->sgotplt_jump_table_size = htab->next_tls_desc_index * 4;

  /* We now have determined the sizes of the various dynamic sections.
     Allocate memory for them.  */
  relocs = FALSE;
  for (s = dynobj->sections; s != NULL; s = s->next)
    {
      bfd_boolean strip_section = TRUE;

      if ((s->flags & SEC_LINKER_CREATED) == 0)
	continue;

      if (s == htab->splt
	  || s == htab->sgot
	  || s == htab->sgotplt
	  || s == htab->sdynbss)
	{
	  /* Strip this section if we don't need it; see the
	     comment below.  */
	  /* We'd like to strip these sections if they aren't needed, but if
	     we've exported dynamic symbols from them we must leave them.
	     It's too late to tell BFD to get rid of the symbols.  */

	  if (htab->elf.hplt != NULL)
	    strip_section = FALSE;
	}
      else if (CONST_STRNEQ (bfd_get_section_name (dynobj, s), ".rel"))
	{
	  if (s->size != 0 && s != htab->srelplt && s != htab->srelplt2)
	    relocs = TRUE;

	  /* We use the reloc_count field as a counter if we need
	     to copy relocs into the output file.  */
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
	     output file.  This is mostly to handle .rel.bss and
	     .rel.plt.  We must create both sections in
	     create_dynamic_sections, because they must be created
	     before the linker maps input sections to output
	     sections.  The linker does that before
	     adjust_dynamic_symbol is called, and it is that
	     function which decides whether anything needs to go
	     into these sections.  */
	  if (strip_section)
	    s->flags |= SEC_EXCLUDE;
	  continue;
	}

      if ((s->flags & SEC_HAS_CONTENTS) == 0)
	continue;

      /* Allocate memory for the section contents.  We use bfd_zalloc
	 here in case unused entries are not reclaimed before the
	 section's contents are written out.  This should not happen,
	 but this way if it does, we get a R_386_NONE reloc instead
	 of garbage.  */
      s->contents = bfd_zalloc (dynobj, s->size);
      if (s->contents == NULL)
	return FALSE;
    }

  if (htab->elf.dynamic_sections_created)
    {
      /* Add some entries to the .dynamic section.  We fill in the
	 values later, in elf_i386_finish_dynamic_sections, but we
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

      if (htab->splt->size != 0)
	{
	  if (!add_dynamic_entry (DT_PLTGOT, 0)
	      || !add_dynamic_entry (DT_PLTRELSZ, 0)
	      || !add_dynamic_entry (DT_PLTREL, DT_REL)
	      || !add_dynamic_entry (DT_JMPREL, 0))
	    return FALSE;
	}

      if (relocs)
	{
	  if (!add_dynamic_entry (DT_REL, 0)
	      || !add_dynamic_entry (DT_RELSZ, 0)
	      || !add_dynamic_entry (DT_RELENT, sizeof (Elf32_External_Rel)))
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
elf_i386_always_size_sections (bfd *output_bfd,
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

/* Set the correct type for an x86 ELF section.  We do this by the
   section name, which is a hack, but ought to work.  */

static bfd_boolean
elf_i386_fake_sections (bfd *abfd ATTRIBUTE_UNUSED,
			Elf_Internal_Shdr *hdr,
			asection *sec)
{
  register const char *name;

  name = bfd_get_section_name (abfd, sec);

  /* This is an ugly, but unfortunately necessary hack that is
     needed when producing EFI binaries on x86. It tells
     elf.c:elf_fake_sections() not to consider ".reloc" as a section
     containing ELF relocation info.  We need this hack in order to
     be able to generate ELF binaries that can be translated into
     EFI applications (which are essentially COFF objects).  Those
     files contain a COFF ".reloc" section inside an ELFNN object,
     which would normally cause BFD to segfault because it would
     attempt to interpret this section as containing relocation
     entries for section "oc".  With this hack enabled, ".reloc"
     will be treated as a normal data section, which will avoid the
     segfault.  However, you won't be able to create an ELFNN binary
     with a section named "oc" that needs relocations, but that's
     the kind of ugly side-effects you get when detecting section
     types based on their names...  In practice, this limitation is
     unlikely to bite.  */
  if (strcmp (name, ".reloc") == 0)
    hdr->sh_type = SHT_PROGBITS;

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

  /* If tls_sec is NULL, we should have signalled an error already.  */
  if (htab->tls_sec == NULL)
    return 0;
  return htab->tls_size + htab->tls_sec->vma - address;
}

/* Relocate an i386 ELF section.  */

static bfd_boolean
elf_i386_relocate_section (bfd *output_bfd,
			   struct bfd_link_info *info,
			   bfd *input_bfd,
			   asection *input_section,
			   bfd_byte *contents,
			   Elf_Internal_Rela *relocs,
			   Elf_Internal_Sym *local_syms,
			   asection **local_sections)
{
  struct elf_i386_link_hash_table *htab;
  Elf_Internal_Shdr *symtab_hdr;
  struct elf_link_hash_entry **sym_hashes;
  bfd_vma *local_got_offsets;
  bfd_vma *local_tlsdesc_gotents;
  Elf_Internal_Rela *rel;
  Elf_Internal_Rela *relend;

  htab = elf_i386_hash_table (info);
  symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (input_bfd);
  local_got_offsets = elf_local_got_offsets (input_bfd);
  local_tlsdesc_gotents = elf_i386_local_tlsdesc_gotent (input_bfd);

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
      unsigned int indx;
      int tls_type;

      r_type = ELF32_R_TYPE (rel->r_info);
      if (r_type == R_386_GNU_VTINHERIT
	  || r_type == R_386_GNU_VTENTRY)
	continue;

      if ((indx = r_type) >= R_386_standard
	  && ((indx = r_type - R_386_ext_offset) - R_386_standard
	      >= R_386_ext - R_386_standard)
	  && ((indx = r_type - R_386_tls_offset) - R_386_ext
	      >= R_386_tls - R_386_ext))
	{
	  (*_bfd_error_handler)
	    (_("%B: unrecognized relocation (0x%x) in section `%A'"),
	     input_bfd, input_section, r_type);
	  bfd_set_error (bfd_error_bad_value);
	  return FALSE;
	}
      howto = elf_howto_table + indx;

      r_symndx = ELF32_R_SYM (rel->r_info);
      h = NULL;
      sym = NULL;
      sec = NULL;
      unresolved_reloc = FALSE;
      if (r_symndx < symtab_hdr->sh_info)
	{
	  sym = local_syms + r_symndx;
	  sec = local_sections[r_symndx];
	  relocation = (sec->output_section->vma
			+ sec->output_offset
			+ sym->st_value);

	  if (ELF_ST_TYPE (sym->st_info) == STT_SECTION
	      && ((sec->flags & SEC_MERGE) != 0
		  || (info->relocatable
		      && sec->output_offset != 0)))
	    {
	      bfd_vma addend;
	      bfd_byte *where = contents + rel->r_offset;

	      switch (howto->size)
		{
		case 0:
		  addend = bfd_get_8 (input_bfd, where);
		  if (howto->pc_relative)
		    {
		      addend = (addend ^ 0x80) - 0x80;
		      addend += 1;
		    }
		  break;
		case 1:
		  addend = bfd_get_16 (input_bfd, where);
		  if (howto->pc_relative)
		    {
		      addend = (addend ^ 0x8000) - 0x8000;
		      addend += 2;
		    }
		  break;
		case 2:
		  addend = bfd_get_32 (input_bfd, where);
		  if (howto->pc_relative)
		    {
		      addend = (addend ^ 0x80000000) - 0x80000000;
		      addend += 4;
		    }
		  break;
		default:
		  abort ();
		}

	      if (info->relocatable)
		addend += sec->output_offset;
	      else
		{
		  asection *msec = sec;
		  addend = _bfd_elf_rel_local_sym (output_bfd, sym, &msec,
						   addend);
		  addend -= relocation;
		  addend += msec->output_section->vma + msec->output_offset;
		}

	      switch (howto->size)
		{
		case 0:
		  /* FIXME: overflow checks.  */
		  if (howto->pc_relative)
		    addend -= 1;
		  bfd_put_8 (input_bfd, addend, where);
		  break;
		case 1:
		  if (howto->pc_relative)
		    addend -= 2;
		  bfd_put_16 (input_bfd, addend, where);
		  break;
		case 2:
		  if (howto->pc_relative)
		    addend -= 4;
		  bfd_put_32 (input_bfd, addend, where);
		  break;
		}
	    }
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

      switch (r_type)
	{
	case R_386_GOT32:
	  /* Relocation is to the entry for this symbol in the global
	     offset table.  */
	  if (htab->sgot == NULL)
	    abort ();

	  if (h != NULL)
	    {
	      bfd_boolean dyn;

	      off = h->got.offset;
	      dyn = htab->elf.dynamic_sections_created;
	      if (! WILL_CALL_FINISH_DYNAMIC_SYMBOL (dyn, info->shared, h)
		  || (info->shared
		      && SYMBOL_REFERENCES_LOCAL (info, h))
		  || (ELF_ST_VISIBILITY (h->other)
		      && h->root.type == bfd_link_hash_undefweak))
		{
		  /* This is actually a static link, or it is a
		     -Bsymbolic link and the symbol is defined
		     locally, or the symbol was forced to be local
		     because of a version file.  We must initialize
		     this entry in the global offset table.  Since the
		     offset must always be a multiple of 4, we use the
		     least significant bit to record whether we have
		     initialized it already.

		     When doing a dynamic link, we create a .rel.got
		     relocation entry to initialize the value.  This
		     is done in the finish_dynamic_symbol routine.  */
		  if ((off & 1) != 0)
		    off &= ~1;
		  else
		    {
		      bfd_put_32 (output_bfd, relocation,
				  htab->sgot->contents + off);
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

	      /* The offset must always be a multiple of 4.  We use
		 the least significant bit to record whether we have
		 already generated the necessary reloc.  */
	      if ((off & 1) != 0)
		off &= ~1;
	      else
		{
		  bfd_put_32 (output_bfd, relocation,
			      htab->sgot->contents + off);

		  if (info->shared)
		    {
		      asection *s;
		      Elf_Internal_Rela outrel;
		      bfd_byte *loc;

		      s = htab->srelgot;
		      if (s == NULL)
			abort ();

		      outrel.r_offset = (htab->sgot->output_section->vma
					 + htab->sgot->output_offset
					 + off);
		      outrel.r_info = ELF32_R_INFO (0, R_386_RELATIVE);
		      loc = s->contents;
		      loc += s->reloc_count++ * sizeof (Elf32_External_Rel);
		      bfd_elf32_swap_reloc_out (output_bfd, &outrel, loc);
		    }

		  local_got_offsets[r_symndx] |= 1;
		}
	    }

	  if (off >= (bfd_vma) -2)
	    abort ();

	  relocation = htab->sgot->output_section->vma
		       + htab->sgot->output_offset + off
		       - htab->sgotplt->output_section->vma
		       - htab->sgotplt->output_offset;
	  break;

	case R_386_GOTOFF:
	  /* Relocation is relative to the start of the global offset
	     table.  */

	  /* Check to make sure it isn't a protected function symbol
	     for shared library since it may not be local when used
	     as function address.  */
	  if (info->shared
	      && !info->executable
	      && h
	      && h->def_regular
	      && h->type == STT_FUNC
	      && ELF_ST_VISIBILITY (h->other) == STV_PROTECTED)
	    {
	      (*_bfd_error_handler)
		(_("%B: relocation R_386_GOTOFF against protected function `%s' can not be used when making a shared object"),
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

	case R_386_GOTPC:
	  /* Use global offset table as symbol value.  */
	  relocation = htab->sgotplt->output_section->vma
		       + htab->sgotplt->output_offset;
	  unresolved_reloc = FALSE;
	  break;

	case R_386_PLT32:
	  /* Relocation is to the entry for this symbol in the
	     procedure linkage table.  */

	  /* Resolve a PLT32 reloc against a local symbol directly,
	     without using the procedure linkage table.  */
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

	case R_386_32:
	case R_386_PC32:
	  if ((input_section->flags & SEC_ALLOC) == 0)
	    break;

	  if ((info->shared
	       && (h == NULL
		   || ELF_ST_VISIBILITY (h->other) == STV_DEFAULT
		   || h->root.type != bfd_link_hash_undefweak)
	       && (r_type != R_386_PC32
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
		 time.  */

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
	      else if (h != NULL
		       && h->dynindx != -1
		       && (r_type == R_386_PC32
			   || !info->shared
			   || !SYMBOLIC_BIND (info, h)
			   || !h->def_regular))
		outrel.r_info = ELF32_R_INFO (h->dynindx, r_type);
	      else
		{
		  /* This symbol is local, or marked to become local.  */
		  relocate = TRUE;
		  outrel.r_info = ELF32_R_INFO (0, R_386_RELATIVE);
		}

	      sreloc = elf_section_data (input_section)->sreloc;
	      if (sreloc == NULL)
		abort ();

	      loc = sreloc->contents;
	      loc += sreloc->reloc_count++ * sizeof (Elf32_External_Rel);
	      bfd_elf32_swap_reloc_out (output_bfd, &outrel, loc);

	      /* If this reloc is against an external symbol, we do
		 not want to fiddle with the addend.  Otherwise, we
		 need to include the symbol value so that it becomes
		 an addend for the dynamic reloc.  */
	      if (! relocate)
		continue;
	    }
	  break;

	case R_386_TLS_IE:
	  if (info->shared)
	    {
	      Elf_Internal_Rela outrel;
	      bfd_byte *loc;
	      asection *sreloc;

	      outrel.r_offset = rel->r_offset
				+ input_section->output_section->vma
				+ input_section->output_offset;
	      outrel.r_info = ELF32_R_INFO (0, R_386_RELATIVE);
	      sreloc = elf_section_data (input_section)->sreloc;
	      if (sreloc == NULL)
		abort ();
	      loc = sreloc->contents;
	      loc += sreloc->reloc_count++ * sizeof (Elf32_External_Rel);
	      bfd_elf32_swap_reloc_out (output_bfd, &outrel, loc);
	    }
	  /* Fall through */

	case R_386_TLS_GD:
	case R_386_TLS_GOTDESC:
	case R_386_TLS_DESC_CALL:
	case R_386_TLS_IE_32:
	case R_386_TLS_GOTIE:
	  r_type = elf_i386_tls_transition (info, r_type, h == NULL);
	  tls_type = GOT_UNKNOWN;
	  if (h == NULL && local_got_offsets)
	    tls_type = elf_i386_local_got_tls_type (input_bfd) [r_symndx];
	  else if (h != NULL)
	    {
	      tls_type = elf_i386_hash_entry(h)->tls_type;
	      if (!info->shared && h->dynindx == -1 && (tls_type & GOT_TLS_IE))
		r_type = R_386_TLS_LE_32;
	    }
	  if (tls_type == GOT_TLS_IE)
	    tls_type = GOT_TLS_IE_NEG;
	  if (r_type == R_386_TLS_GD
	      || r_type == R_386_TLS_GOTDESC
	      || r_type == R_386_TLS_DESC_CALL)
	    {
	      if (tls_type == GOT_TLS_IE_POS)
		r_type = R_386_TLS_GOTIE;
	      else if (tls_type & GOT_TLS_IE)
		r_type = R_386_TLS_IE_32;
	    }

	  if (r_type == R_386_TLS_LE_32)
	    {
	      BFD_ASSERT (! unresolved_reloc);
	      if (ELF32_R_TYPE (rel->r_info) == R_386_TLS_GD)
		{
		  unsigned int val, type;
		  bfd_vma roff;

		  /* GD->LE transition.  */
		  BFD_ASSERT (rel->r_offset >= 2);
		  type = bfd_get_8 (input_bfd, contents + rel->r_offset - 2);
		  BFD_ASSERT (type == 0x8d || type == 0x04);
		  BFD_ASSERT (rel->r_offset + 9 <= input_section->size);
		  BFD_ASSERT (bfd_get_8 (input_bfd,
					 contents + rel->r_offset + 4)
			      == 0xe8);
		  BFD_ASSERT (rel + 1 < relend);
		  BFD_ASSERT (ELF32_R_TYPE (rel[1].r_info) == R_386_PLT32);
		  roff = rel->r_offset + 5;
		  val = bfd_get_8 (input_bfd,
				   contents + rel->r_offset - 1);
		  if (type == 0x04)
		    {
		      /* leal foo(,%reg,1), %eax; call ___tls_get_addr
			 Change it into:
			 movl %gs:0, %eax; subl $foo@tpoff, %eax
			 (6 byte form of subl).  */
		      BFD_ASSERT (rel->r_offset >= 3);
		      BFD_ASSERT (bfd_get_8 (input_bfd,
					     contents + rel->r_offset - 3)
				  == 0x8d);
		      BFD_ASSERT ((val & 0xc7) == 0x05 && val != (4 << 3));
		      memcpy (contents + rel->r_offset - 3,
			      "\x65\xa1\0\0\0\0\x81\xe8\0\0\0", 12);
		    }
		  else
		    {
		      BFD_ASSERT ((val & 0xf8) == 0x80 && (val & 7) != 4);
		      if (rel->r_offset + 10 <= input_section->size
			  && bfd_get_8 (input_bfd,
					contents + rel->r_offset + 9) == 0x90)
			{
			  /* leal foo(%reg), %eax; call ___tls_get_addr; nop
			     Change it into:
			     movl %gs:0, %eax; subl $foo@tpoff, %eax
			     (6 byte form of subl).  */
			  memcpy (contents + rel->r_offset - 2,
				  "\x65\xa1\0\0\0\0\x81\xe8\0\0\0", 12);
			  roff = rel->r_offset + 6;
			}
		      else
			{
			  /* leal foo(%reg), %eax; call ___tls_get_addr
			     Change it into:
			     movl %gs:0, %eax; subl $foo@tpoff, %eax
			     (5 byte form of subl).  */
			  memcpy (contents + rel->r_offset - 2,
				  "\x65\xa1\0\0\0\0\x2d\0\0\0", 11);
			}
		    }
		  bfd_put_32 (output_bfd, tpoff (info, relocation),
			      contents + roff);
		  /* Skip R_386_PLT32.  */
		  rel++;
		  continue;
		}
	      else if (ELF32_R_TYPE (rel->r_info) == R_386_TLS_GOTDESC)
		{
		  /* GDesc -> LE transition.
		     It's originally something like:
		     leal x@tlsdesc(%ebx), %eax

		     leal x@ntpoff, %eax

		     Registers other than %eax may be set up here.  */

		  unsigned int val, type;
		  bfd_vma roff;

		  /* First, make sure it's a leal adding ebx to a
		     32-bit offset into any register, although it's
		     probably almost always going to be eax.  */
		  roff = rel->r_offset;
		  BFD_ASSERT (roff >= 2);
		  type = bfd_get_8 (input_bfd, contents + roff - 2);
		  BFD_ASSERT (type == 0x8d);
		  val = bfd_get_8 (input_bfd, contents + roff - 1);
		  BFD_ASSERT ((val & 0xc7) == 0x83);
		  BFD_ASSERT (roff + 4 <= input_section->size);

		  /* Now modify the instruction as appropriate.  */
		  /* aoliva FIXME: remove the above and xor the byte
		     below with 0x86.  */
		  bfd_put_8 (output_bfd, val ^ 0x86,
			     contents + roff - 1);
		  bfd_put_32 (output_bfd, -tpoff (info, relocation),
			      contents + roff);
		  continue;
		}
	      else if (ELF32_R_TYPE (rel->r_info) == R_386_TLS_DESC_CALL)
		{
		  /* GDesc -> LE transition.
		     It's originally:
		     call *(%eax)
		     Turn it into:
		     nop; nop  */

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
	      else if (ELF32_R_TYPE (rel->r_info) == R_386_TLS_IE)
		{
		  unsigned int val, type;

		  /* IE->LE transition:
		     Originally it can be one of:
		     movl foo, %eax
		     movl foo, %reg
		     addl foo, %reg
		     We change it into:
		     movl $foo, %eax
		     movl $foo, %reg
		     addl $foo, %reg.  */
		  BFD_ASSERT (rel->r_offset >= 1);
		  val = bfd_get_8 (input_bfd, contents + rel->r_offset - 1);
		  BFD_ASSERT (rel->r_offset + 4 <= input_section->size);
		  if (val == 0xa1)
		    {
		      /* movl foo, %eax.  */
		      bfd_put_8 (output_bfd, 0xb8,
				 contents + rel->r_offset - 1);
		    }
		  else
		    {
		      BFD_ASSERT (rel->r_offset >= 2);
		      type = bfd_get_8 (input_bfd,
					contents + rel->r_offset - 2);
		      switch (type)
			{
			case 0x8b:
			  /* movl */
			  BFD_ASSERT ((val & 0xc7) == 0x05);
			  bfd_put_8 (output_bfd, 0xc7,
				     contents + rel->r_offset - 2);
			  bfd_put_8 (output_bfd,
				     0xc0 | ((val >> 3) & 7),
				     contents + rel->r_offset - 1);
			  break;
			case 0x03:
			  /* addl */
			  BFD_ASSERT ((val & 0xc7) == 0x05);
			  bfd_put_8 (output_bfd, 0x81,
				     contents + rel->r_offset - 2);
			  bfd_put_8 (output_bfd,
				     0xc0 | ((val >> 3) & 7),
				     contents + rel->r_offset - 1);
			  break;
			default:
			  BFD_FAIL ();
			  break;
			}
		    }
		  bfd_put_32 (output_bfd, -tpoff (info, relocation),
			      contents + rel->r_offset);
		  continue;
		}
	      else
		{
		  unsigned int val, type;

		  /* {IE_32,GOTIE}->LE transition:
		     Originally it can be one of:
		     subl foo(%reg1), %reg2
		     movl foo(%reg1), %reg2
		     addl foo(%reg1), %reg2
		     We change it into:
		     subl $foo, %reg2
		     movl $foo, %reg2 (6 byte form)
		     addl $foo, %reg2.  */
		  BFD_ASSERT (rel->r_offset >= 2);
		  type = bfd_get_8 (input_bfd, contents + rel->r_offset - 2);
		  val = bfd_get_8 (input_bfd, contents + rel->r_offset - 1);
		  BFD_ASSERT (rel->r_offset + 4 <= input_section->size);
		  BFD_ASSERT ((val & 0xc0) == 0x80 && (val & 7) != 4);
		  if (type == 0x8b)
		    {
		      /* movl */
		      bfd_put_8 (output_bfd, 0xc7,
				 contents + rel->r_offset - 2);
		      bfd_put_8 (output_bfd, 0xc0 | ((val >> 3) & 7),
				 contents + rel->r_offset - 1);
		    }
		  else if (type == 0x2b)
		    {
		      /* subl */
		      bfd_put_8 (output_bfd, 0x81,
				 contents + rel->r_offset - 2);
		      bfd_put_8 (output_bfd, 0xe8 | ((val >> 3) & 7),
				 contents + rel->r_offset - 1);
		    }
		  else if (type == 0x03)
		    {
		      /* addl */
		      bfd_put_8 (output_bfd, 0x81,
				 contents + rel->r_offset - 2);
		      bfd_put_8 (output_bfd, 0xc0 | ((val >> 3) & 7),
				 contents + rel->r_offset - 1);
		    }
		  else
		    BFD_FAIL ();
		  if (ELF32_R_TYPE (rel->r_info) == R_386_TLS_GOTIE)
		    bfd_put_32 (output_bfd, -tpoff (info, relocation),
				contents + rel->r_offset);
		  else
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
	      offplt = elf_i386_hash_entry (h)->tlsdesc_got;
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
		  outrel.r_info = ELF32_R_INFO (indx, R_386_TLS_DESC);
		  BFD_ASSERT (htab->sgotplt_jump_table_size + offplt + 8
			      <= htab->sgotplt->size);
		  outrel.r_offset = (htab->sgotplt->output_section->vma
				     + htab->sgotplt->output_offset
				     + offplt
				     + htab->sgotplt_jump_table_size);
		  sreloc = htab->srelplt;
		  loc = sreloc->contents;
		  loc += (htab->next_tls_desc_index++
			  * sizeof (Elf32_External_Rel));
		  BFD_ASSERT (loc + sizeof (Elf32_External_Rel)
			      <= sreloc->contents + sreloc->size);
		  bfd_elf32_swap_reloc_out (output_bfd, &outrel, loc);
		  if (indx == 0)
		    {
		      BFD_ASSERT (! unresolved_reloc);
		      bfd_put_32 (output_bfd,
				  relocation - dtpoff_base (info),
				  htab->sgotplt->contents + offplt
				  + htab->sgotplt_jump_table_size + 4);
		    }
		  else
		    {
		      bfd_put_32 (output_bfd, 0,
				  htab->sgotplt->contents + offplt
				  + htab->sgotplt_jump_table_size + 4);
		    }
		}

	      sreloc = htab->srelgot;

	      outrel.r_offset = (htab->sgot->output_section->vma
				 + htab->sgot->output_offset + off);

	      if (GOT_TLS_GD_P (tls_type))
		dr_type = R_386_TLS_DTPMOD32;
	      else if (GOT_TLS_GDESC_P (tls_type))
		goto dr_done;
	      else if (tls_type == GOT_TLS_IE_POS)
		dr_type = R_386_TLS_TPOFF;
	      else
		dr_type = R_386_TLS_TPOFF32;

	      if (dr_type == R_386_TLS_TPOFF && indx == 0)
		bfd_put_32 (output_bfd, relocation - dtpoff_base (info),
			    htab->sgot->contents + off);
	      else if (dr_type == R_386_TLS_TPOFF32 && indx == 0)
		bfd_put_32 (output_bfd, dtpoff_base (info) - relocation,
			    htab->sgot->contents + off);
	      else if (dr_type != R_386_TLS_DESC)
		bfd_put_32 (output_bfd, 0,
			    htab->sgot->contents + off);
	      outrel.r_info = ELF32_R_INFO (indx, dr_type);

	      loc = sreloc->contents;
	      loc += sreloc->reloc_count++ * sizeof (Elf32_External_Rel);
	      BFD_ASSERT (loc + sizeof (Elf32_External_Rel)
			  <= sreloc->contents + sreloc->size);
	      bfd_elf32_swap_reloc_out (output_bfd, &outrel, loc);

	      if (GOT_TLS_GD_P (tls_type))
		{
		  if (indx == 0)
		    {
	    	      BFD_ASSERT (! unresolved_reloc);
		      bfd_put_32 (output_bfd,
				  relocation - dtpoff_base (info),
				  htab->sgot->contents + off + 4);
		    }
		  else
		    {
		      bfd_put_32 (output_bfd, 0,
				  htab->sgot->contents + off + 4);
		      outrel.r_info = ELF32_R_INFO (indx,
						    R_386_TLS_DTPOFF32);
		      outrel.r_offset += 4;
		      sreloc->reloc_count++;
		      loc += sizeof (Elf32_External_Rel);
		      BFD_ASSERT (loc + sizeof (Elf32_External_Rel)
				  <= sreloc->contents + sreloc->size);
		      bfd_elf32_swap_reloc_out (output_bfd, &outrel, loc);
		    }
		}
	      else if (tls_type == GOT_TLS_IE_BOTH)
		{
		  bfd_put_32 (output_bfd,
			      indx == 0 ? relocation - dtpoff_base (info) : 0,
			      htab->sgot->contents + off + 4);
		  outrel.r_info = ELF32_R_INFO (indx, R_386_TLS_TPOFF);
		  outrel.r_offset += 4;
		  sreloc->reloc_count++;
		  loc += sizeof (Elf32_External_Rel);
		  bfd_elf32_swap_reloc_out (output_bfd, &outrel, loc);
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
	  if (r_type == R_386_TLS_GOTDESC
	      || r_type == R_386_TLS_DESC_CALL)
	    {
	      relocation = htab->sgotplt_jump_table_size + offplt;
	      unresolved_reloc = FALSE;
	    }
	  else if (r_type == ELF32_R_TYPE (rel->r_info))
	    {
	      bfd_vma g_o_t = htab->sgotplt->output_section->vma
			      + htab->sgotplt->output_offset;
	      relocation = htab->sgot->output_section->vma
		+ htab->sgot->output_offset + off - g_o_t;
	      if ((r_type == R_386_TLS_IE || r_type == R_386_TLS_GOTIE)
		  && tls_type == GOT_TLS_IE_BOTH)
		relocation += 4;
	      if (r_type == R_386_TLS_IE)
		relocation += g_o_t;
	      unresolved_reloc = FALSE;
	    }
	  else if (ELF32_R_TYPE (rel->r_info) == R_386_TLS_GD)
	    {
	      unsigned int val, type;
	      bfd_vma roff;

	      /* GD->IE transition.  */
	      BFD_ASSERT (rel->r_offset >= 2);
	      type = bfd_get_8 (input_bfd, contents + rel->r_offset - 2);
	      BFD_ASSERT (type == 0x8d || type == 0x04);
	      BFD_ASSERT (rel->r_offset + 9 <= input_section->size);
	      BFD_ASSERT (bfd_get_8 (input_bfd, contents + rel->r_offset + 4)
			  == 0xe8);
	      BFD_ASSERT (rel + 1 < relend);
	      BFD_ASSERT (ELF32_R_TYPE (rel[1].r_info) == R_386_PLT32);
	      roff = rel->r_offset - 3;
	      val = bfd_get_8 (input_bfd, contents + rel->r_offset - 1);
	      if (type == 0x04)
		{
		  /* leal foo(,%reg,1), %eax; call ___tls_get_addr
		     Change it into:
		     movl %gs:0, %eax; subl $foo@gottpoff(%reg), %eax.  */
		  BFD_ASSERT (rel->r_offset >= 3);
		  BFD_ASSERT (bfd_get_8 (input_bfd,
					 contents + rel->r_offset - 3)
			      == 0x8d);
		  BFD_ASSERT ((val & 0xc7) == 0x05 && val != (4 << 3));
		  val >>= 3;
		}
	      else
		{
		  /* leal foo(%reg), %eax; call ___tls_get_addr; nop
		     Change it into:
		     movl %gs:0, %eax; subl $foo@gottpoff(%reg), %eax.  */
		  BFD_ASSERT (rel->r_offset + 10 <= input_section->size);
		  BFD_ASSERT ((val & 0xf8) == 0x80 && (val & 7) != 4);
		  BFD_ASSERT (bfd_get_8 (input_bfd,
					 contents + rel->r_offset + 9)
			      == 0x90);
		  roff = rel->r_offset - 2;
		}
	      memcpy (contents + roff,
		      "\x65\xa1\0\0\0\0\x2b\x80\0\0\0", 12);
	      contents[roff + 7] = 0x80 | (val & 7);
	      /* If foo is used only with foo@gotntpoff(%reg) and
		 foo@indntpoff, but not with foo@gottpoff(%reg), change
		 subl $foo@gottpoff(%reg), %eax
		 into:
		 addl $foo@gotntpoff(%reg), %eax.  */
	      if (tls_type == GOT_TLS_IE_POS)
		contents[roff + 6] = 0x03;
	      bfd_put_32 (output_bfd,
			  htab->sgot->output_section->vma
			  + htab->sgot->output_offset + off
			  - htab->sgotplt->output_section->vma
			  - htab->sgotplt->output_offset,
			  contents + roff + 8);
	      /* Skip R_386_PLT32.  */
	      rel++;
	      continue;
	    }
	  else if (ELF32_R_TYPE (rel->r_info) == R_386_TLS_GOTDESC)
	    {
	      /* GDesc -> IE transition.
		 It's originally something like:
		 leal x@tlsdesc(%ebx), %eax

		 Change it to:
		 movl x@gotntpoff(%ebx), %eax # before nop; nop
		 or:
		 movl x@gottpoff(%ebx), %eax # before negl %eax

		 Registers other than %eax may be set up here.  */

	      unsigned int val, type;
	      bfd_vma roff;

	      /* First, make sure it's a leal adding ebx to a 32-bit
		 offset into any register, although it's probably
		 almost always going to be eax.  */
	      roff = rel->r_offset;
	      BFD_ASSERT (roff >= 2);
	      type = bfd_get_8 (input_bfd, contents + roff - 2);
	      BFD_ASSERT (type == 0x8d);
	      val = bfd_get_8 (input_bfd, contents + roff - 1);
	      BFD_ASSERT ((val & 0xc7) == 0x83);
	      BFD_ASSERT (roff + 4 <= input_section->size);

	      /* Now modify the instruction as appropriate.  */
	      /* To turn a leal into a movl in the form we use it, it
		 suffices to change the first byte from 0x8d to 0x8b.
		 aoliva FIXME: should we decide to keep the leal, all
		 we have to do is remove the statement below, and
		 adjust the relaxation of R_386_TLS_DESC_CALL.  */
	      bfd_put_8 (output_bfd, 0x8b, contents + roff - 2);

	      if (tls_type == GOT_TLS_IE_BOTH)
		off += 4;

	      bfd_put_32 (output_bfd,
			  htab->sgot->output_section->vma
			  + htab->sgot->output_offset + off
			  - htab->sgotplt->output_section->vma
			  - htab->sgotplt->output_offset,
			  contents + roff);
	      continue;
	    }
	  else if (ELF32_R_TYPE (rel->r_info) == R_386_TLS_DESC_CALL)
	    {
	      /* GDesc -> IE transition.
		 It's originally:
		 call *(%eax)

		 Change it to:
		 nop; nop
		 or
		 negl %eax
		 depending on how we transformed the TLS_GOTDESC above.
	      */

	      unsigned int val, type;
	      bfd_vma roff;

	      /* First, make sure it's a call *(%eax).  */
	      roff = rel->r_offset;
	      BFD_ASSERT (roff + 2 <= input_section->size);
	      type = bfd_get_8 (input_bfd, contents + roff);
	      BFD_ASSERT (type == 0xff);
	      val = bfd_get_8 (input_bfd, contents + roff + 1);
	      BFD_ASSERT (val == 0x10);

	      /* Now modify the instruction as appropriate.  */
	      if (tls_type != GOT_TLS_IE_NEG)
		{
		  /* xchg %ax,%ax */
		  bfd_put_8 (output_bfd, 0x66, contents + roff);
		  bfd_put_8 (output_bfd, 0x90, contents + roff + 1);
		}
	      else
		{
		  /* negl %eax */
		  bfd_put_8 (output_bfd, 0xf7, contents + roff);
		  bfd_put_8 (output_bfd, 0xd8, contents + roff + 1);
		}

	      continue;
	    }
	  else
	    BFD_ASSERT (FALSE);
	  break;

	case R_386_TLS_LDM:
	  if (! info->shared)
	    {
	      unsigned int val;

	      /* LD->LE transition:
		 Ensure it is:
		 leal foo(%reg), %eax; call ___tls_get_addr.
		 We change it into:
		 movl %gs:0, %eax; nop; leal 0(%esi,1), %esi.  */
	      BFD_ASSERT (rel->r_offset >= 2);
	      BFD_ASSERT (bfd_get_8 (input_bfd, contents + rel->r_offset - 2)
			  == 0x8d);
	      val = bfd_get_8 (input_bfd, contents + rel->r_offset - 1);
	      BFD_ASSERT ((val & 0xf8) == 0x80 && (val & 7) != 4);
	      BFD_ASSERT (rel->r_offset + 9 <= input_section->size);
	      BFD_ASSERT (bfd_get_8 (input_bfd, contents + rel->r_offset + 4)
			  == 0xe8);
	      BFD_ASSERT (rel + 1 < relend);
	      BFD_ASSERT (ELF32_R_TYPE (rel[1].r_info) == R_386_PLT32);
	      memcpy (contents + rel->r_offset - 2,
		      "\x65\xa1\0\0\0\0\x90\x8d\x74\x26", 11);
	      /* Skip R_386_PLT32.  */
	      rel++;
	      continue;
	    }

	  if (htab->sgot == NULL)
	    abort ();

	  off = htab->tls_ldm_got.offset;
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

	      bfd_put_32 (output_bfd, 0,
			  htab->sgot->contents + off);
	      bfd_put_32 (output_bfd, 0,
			  htab->sgot->contents + off + 4);
	      outrel.r_info = ELF32_R_INFO (0, R_386_TLS_DTPMOD32);
	      loc = htab->srelgot->contents;
	      loc += htab->srelgot->reloc_count++ * sizeof (Elf32_External_Rel);
	      bfd_elf32_swap_reloc_out (output_bfd, &outrel, loc);
	      htab->tls_ldm_got.offset |= 1;
	    }
	  relocation = htab->sgot->output_section->vma
		       + htab->sgot->output_offset + off
		       - htab->sgotplt->output_section->vma
		       - htab->sgotplt->output_offset;
	  unresolved_reloc = FALSE;
	  break;

	case R_386_TLS_LDO_32:
	  if (info->shared || (input_section->flags & SEC_CODE) == 0)
	    relocation -= dtpoff_base (info);
	  else
	    /* When converting LDO to LE, we must negate.  */
	    relocation = -tpoff (info, relocation);
	  break;

	case R_386_TLS_LE_32:
	case R_386_TLS_LE:
	  if (info->shared)
	    {
	      Elf_Internal_Rela outrel;
	      asection *sreloc;
	      bfd_byte *loc;
	      int indx;

	      outrel.r_offset = rel->r_offset
				+ input_section->output_section->vma
				+ input_section->output_offset;
	      if (h != NULL && h->dynindx != -1)
		indx = h->dynindx;
	      else
		indx = 0;
	      if (r_type == R_386_TLS_LE_32)
		outrel.r_info = ELF32_R_INFO (indx, R_386_TLS_TPOFF32);
	      else
		outrel.r_info = ELF32_R_INFO (indx, R_386_TLS_TPOFF);
	      sreloc = elf_section_data (input_section)->sreloc;
	      if (sreloc == NULL)
		abort ();
	      loc = sreloc->contents;
	      loc += sreloc->reloc_count++ * sizeof (Elf32_External_Rel);
	      bfd_elf32_swap_reloc_out (output_bfd, &outrel, loc);
	      if (indx)
		continue;
	      else if (r_type == R_386_TLS_LE_32)
		relocation = dtpoff_base (info) - relocation;
	      else
		relocation -= dtpoff_base (info);
	    }
	  else if (r_type == R_386_TLS_LE_32)
	    relocation = tpoff (info, relocation);
	  else
	    relocation = -tpoff (info, relocation);
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
	{
	  (*_bfd_error_handler)
	    (_("%B(%A+0x%lx): unresolvable %s relocation against symbol `%s'"),
	     input_bfd,
	     input_section,
	     (long) rel->r_offset,
	     howto->name,
	     h->root.root.string);
	  return FALSE;
	}

      r = _bfd_final_link_relocate (howto, input_bfd, input_section,
				    contents, rel->r_offset,
				    relocation, 0);

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
elf_i386_finish_dynamic_symbol (bfd *output_bfd,
				struct bfd_link_info *info,
				struct elf_link_hash_entry *h,
				Elf_Internal_Sym *sym)
{
  struct elf_i386_link_hash_table *htab;

  htab = elf_i386_hash_table (info);

  if (h->plt.offset != (bfd_vma) -1)
    {
      bfd_vma plt_index;
      bfd_vma got_offset;
      Elf_Internal_Rela rel;
      bfd_byte *loc;

      /* This symbol has an entry in the procedure linkage table.  Set
	 it up.  */

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
	 corresponds to this function.  Each .got entry is 4 bytes.
	 The first three are reserved.  */
      got_offset = (plt_index + 3) * 4;

      /* Fill in the entry in the procedure linkage table.  */
      if (! info->shared)
	{
	  memcpy (htab->splt->contents + h->plt.offset, elf_i386_plt_entry,
		  PLT_ENTRY_SIZE);
	  bfd_put_32 (output_bfd,
		      (htab->sgotplt->output_section->vma
		       + htab->sgotplt->output_offset
		       + got_offset),
		      htab->splt->contents + h->plt.offset + 2);

	  if (htab->is_vxworks)
	    {
	      int s, k, reloc_index;

	      /* Create the R_386_32 relocation referencing the GOT
		 for this PLT entry.  */

	      /* S: Current slot number (zero-based).  */
	      s = (h->plt.offset - PLT_ENTRY_SIZE) / PLT_ENTRY_SIZE;
	      /* K: Number of relocations for PLTResolve. */
	      if (info->shared)
		k = PLTRESOLVE_RELOCS_SHLIB;
	      else
		k = PLTRESOLVE_RELOCS;
	      /* Skip the PLTresolve relocations, and the relocations for
		 the other PLT slots. */
	      reloc_index = k + s * PLT_NON_JUMP_SLOT_RELOCS;
	      loc = (htab->srelplt2->contents + reloc_index
		     * sizeof (Elf32_External_Rel));

	      rel.r_offset = (htab->splt->output_section->vma
			      + htab->splt->output_offset
			      + h->plt.offset + 2),
	      rel.r_info = ELF32_R_INFO (htab->elf.hgot->indx, R_386_32);
	      bfd_elf32_swap_reloc_out (output_bfd, &rel, loc);

	      /* Create the R_386_32 relocation referencing the beginning of
		 the PLT for this GOT entry.  */
	      rel.r_offset = (htab->sgotplt->output_section->vma
			      + htab->sgotplt->output_offset
			      + got_offset);
	      rel.r_info = ELF32_R_INFO (htab->elf.hplt->indx, R_386_32);
	      bfd_elf32_swap_reloc_out (output_bfd, &rel,
	      loc + sizeof (Elf32_External_Rel));
	    }
	}
      else
	{
	  memcpy (htab->splt->contents + h->plt.offset, elf_i386_pic_plt_entry,
		  PLT_ENTRY_SIZE);
	  bfd_put_32 (output_bfd, got_offset,
		      htab->splt->contents + h->plt.offset + 2);
	}

      bfd_put_32 (output_bfd, plt_index * sizeof (Elf32_External_Rel),
		  htab->splt->contents + h->plt.offset + 7);
      bfd_put_32 (output_bfd, - (h->plt.offset + PLT_ENTRY_SIZE),
		  htab->splt->contents + h->plt.offset + 12);

      /* Fill in the entry in the global offset table.  */
      bfd_put_32 (output_bfd,
		  (htab->splt->output_section->vma
		   + htab->splt->output_offset
		   + h->plt.offset
		   + 6),
		  htab->sgotplt->contents + got_offset);

      /* Fill in the entry in the .rel.plt section.  */
      rel.r_offset = (htab->sgotplt->output_section->vma
		      + htab->sgotplt->output_offset
		      + got_offset);
      rel.r_info = ELF32_R_INFO (h->dynindx, R_386_JUMP_SLOT);
      loc = htab->srelplt->contents + plt_index * sizeof (Elf32_External_Rel);
      bfd_elf32_swap_reloc_out (output_bfd, &rel, loc);

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
      && ! GOT_TLS_GD_ANY_P (elf_i386_hash_entry(h)->tls_type)
      && (elf_i386_hash_entry(h)->tls_type & GOT_TLS_IE) == 0)
    {
      Elf_Internal_Rela rel;
      bfd_byte *loc;

      /* This symbol has an entry in the global offset table.  Set it
	 up.  */

      if (htab->sgot == NULL || htab->srelgot == NULL)
	abort ();

      rel.r_offset = (htab->sgot->output_section->vma
		      + htab->sgot->output_offset
		      + (h->got.offset & ~(bfd_vma) 1));

      /* If this is a static link, or it is a -Bsymbolic link and the
	 symbol is defined locally or was forced to be local because
	 of a version file, we just want to emit a RELATIVE reloc.
	 The entry in the global offset table will already have been
	 initialized in the relocate_section function.  */
      if (info->shared
	  && SYMBOL_REFERENCES_LOCAL (info, h))
	{
	  BFD_ASSERT((h->got.offset & 1) != 0);
	  rel.r_info = ELF32_R_INFO (0, R_386_RELATIVE);
	}
      else
	{
	  BFD_ASSERT((h->got.offset & 1) == 0);
	  bfd_put_32 (output_bfd, (bfd_vma) 0,
		      htab->sgot->contents + h->got.offset);
	  rel.r_info = ELF32_R_INFO (h->dynindx, R_386_GLOB_DAT);
	}

      loc = htab->srelgot->contents;
      loc += htab->srelgot->reloc_count++ * sizeof (Elf32_External_Rel);
      bfd_elf32_swap_reloc_out (output_bfd, &rel, loc);
    }

  if (h->needs_copy)
    {
      Elf_Internal_Rela rel;
      bfd_byte *loc;

      /* This symbol needs a copy reloc.  Set it up.  */

      if (h->dynindx == -1
	  || (h->root.type != bfd_link_hash_defined
	      && h->root.type != bfd_link_hash_defweak)
	  || htab->srelbss == NULL)
	abort ();

      rel.r_offset = (h->root.u.def.value
		      + h->root.u.def.section->output_section->vma
		      + h->root.u.def.section->output_offset);
      rel.r_info = ELF32_R_INFO (h->dynindx, R_386_COPY);
      loc = htab->srelbss->contents;
      loc += htab->srelbss->reloc_count++ * sizeof (Elf32_External_Rel);
      bfd_elf32_swap_reloc_out (output_bfd, &rel, loc);
    }

  /* Mark _DYNAMIC and _GLOBAL_OFFSET_TABLE_ as absolute.
     On VxWorks, the _GLOBAL_OFFSET_TABLE_ symbol is not absolute: it
     is relative to the ".got" section.  */
  if (strcmp (h->root.root.string, "_DYNAMIC") == 0
      || (!htab->is_vxworks && h == htab->elf.hgot))
    sym->st_shndx = SHN_ABS;

  return TRUE;
}

/* Used to decide how to sort relocs in an optimal manner for the
   dynamic linker, before writing them out.  */

static enum elf_reloc_type_class
elf_i386_reloc_type_class (const Elf_Internal_Rela *rela)
{
  switch (ELF32_R_TYPE (rela->r_info))
    {
    case R_386_RELATIVE:
      return reloc_class_relative;
    case R_386_JUMP_SLOT:
      return reloc_class_plt;
    case R_386_COPY:
      return reloc_class_copy;
    default:
      return reloc_class_normal;
    }
}

/* Finish up the dynamic sections.  */

static bfd_boolean
elf_i386_finish_dynamic_sections (bfd *output_bfd,
				  struct bfd_link_info *info)
{
  struct elf_i386_link_hash_table *htab;
  bfd *dynobj;
  asection *sdyn;

  htab = elf_i386_hash_table (info);
  dynobj = htab->elf.dynobj;
  sdyn = bfd_get_section_by_name (dynobj, ".dynamic");

  if (htab->elf.dynamic_sections_created)
    {
      Elf32_External_Dyn *dyncon, *dynconend;

      if (sdyn == NULL || htab->sgot == NULL)
	abort ();

      dyncon = (Elf32_External_Dyn *) sdyn->contents;
      dynconend = (Elf32_External_Dyn *) (sdyn->contents + sdyn->size);
      for (; dyncon < dynconend; dyncon++)
	{
	  Elf_Internal_Dyn dyn;
	  asection *s;

	  bfd_elf32_swap_dyn_in (dynobj, dyncon, &dyn);

	  switch (dyn.d_tag)
	    {
	    default:
	      continue;

	    case DT_PLTGOT:
	      s = htab->sgotplt;
	      dyn.d_un.d_ptr = s->output_section->vma + s->output_offset;
	      break;

	    case DT_JMPREL:
	      s = htab->srelplt;
	      dyn.d_un.d_ptr = s->output_section->vma + s->output_offset;
	      break;

	    case DT_PLTRELSZ:
	      s = htab->srelplt;
	      dyn.d_un.d_val = s->size;
	      break;

	    case DT_RELSZ:
	      /* My reading of the SVR4 ABI indicates that the
		 procedure linkage table relocs (DT_JMPREL) should be
		 included in the overall relocs (DT_REL).  This is
		 what Solaris does.  However, UnixWare can not handle
		 that case.  Therefore, we override the DT_RELSZ entry
		 here to make it not include the JMPREL relocs.  */
	      s = htab->srelplt;
	      if (s == NULL)
		continue;
	      dyn.d_un.d_val -= s->size;
	      break;

	    case DT_REL:
	      /* We may not be using the standard ELF linker script.
		 If .rel.plt is the first .rel section, we adjust
		 DT_REL to not include it.  */
	      s = htab->srelplt;
	      if (s == NULL)
		continue;
	      if (dyn.d_un.d_ptr != s->output_section->vma + s->output_offset)
		continue;
	      dyn.d_un.d_ptr += s->size;
	      break;
	    }

	  bfd_elf32_swap_dyn_out (output_bfd, &dyn, dyncon);
	}

      /* Fill in the first entry in the procedure linkage table.  */
      if (htab->splt && htab->splt->size > 0)
	{
	  if (info->shared)
	    {
	      memcpy (htab->splt->contents, elf_i386_pic_plt0_entry,
		      sizeof (elf_i386_pic_plt0_entry));
	      memset (htab->splt->contents + sizeof (elf_i386_pic_plt0_entry),
		      htab->plt0_pad_byte,
		      PLT_ENTRY_SIZE - sizeof (elf_i386_pic_plt0_entry));
	    }
	  else
	    {
	      memcpy (htab->splt->contents, elf_i386_plt0_entry,
		      sizeof(elf_i386_plt0_entry));
	      memset (htab->splt->contents + sizeof (elf_i386_plt0_entry),
		      htab->plt0_pad_byte,
		      PLT_ENTRY_SIZE - sizeof (elf_i386_plt0_entry));
	      bfd_put_32 (output_bfd,
			  (htab->sgotplt->output_section->vma
			   + htab->sgotplt->output_offset
			   + 4),
			  htab->splt->contents + 2);
	      bfd_put_32 (output_bfd,
			  (htab->sgotplt->output_section->vma
			   + htab->sgotplt->output_offset
			   + 8),
			  htab->splt->contents + 8);

	      if (htab->is_vxworks)
		{
		  Elf_Internal_Rela rel;

		  /* Generate a relocation for _GLOBAL_OFFSET_TABLE_ + 4.
		     On IA32 we use REL relocations so the addend goes in
		     the PLT directly.  */
		  rel.r_offset = (htab->splt->output_section->vma
				  + htab->splt->output_offset
				  + 2);
		  rel.r_info = ELF32_R_INFO (htab->elf.hgot->indx, R_386_32);
		  bfd_elf32_swap_reloc_out (output_bfd, &rel,
					    htab->srelplt2->contents);
		  /* Generate a relocation for _GLOBAL_OFFSET_TABLE_ + 8.  */
		  rel.r_offset = (htab->splt->output_section->vma
				  + htab->splt->output_offset
				  + 8);
		  rel.r_info = ELF32_R_INFO (htab->elf.hgot->indx, R_386_32);
		  bfd_elf32_swap_reloc_out (output_bfd, &rel,
					    htab->srelplt2->contents +
					    sizeof (Elf32_External_Rel));
		}
	    }

	  /* UnixWare sets the entsize of .plt to 4, although that doesn't
	     really seem like the right value.  */
	  elf_section_data (htab->splt->output_section)
	    ->this_hdr.sh_entsize = 4;

	  /* Correct the .rel.plt.unloaded relocations.  */
	  if (htab->is_vxworks && !info->shared)
	    {
	      int num_plts = (htab->splt->size / PLT_ENTRY_SIZE) - 1;
	      unsigned char *p;

	      p = htab->srelplt2->contents;
	      if (info->shared)
		p += PLTRESOLVE_RELOCS_SHLIB * sizeof (Elf32_External_Rel);
	      else
		p += PLTRESOLVE_RELOCS * sizeof (Elf32_External_Rel);

	      for (; num_plts; num_plts--)
		{
		  Elf_Internal_Rela rel;
		  bfd_elf32_swap_reloc_in (output_bfd, p, &rel);
		  rel.r_info = ELF32_R_INFO (htab->elf.hgot->indx, R_386_32);
		  bfd_elf32_swap_reloc_out (output_bfd, &rel, p);
		  p += sizeof (Elf32_External_Rel);

		  bfd_elf32_swap_reloc_in (output_bfd, p, &rel);
		  rel.r_info = ELF32_R_INFO (htab->elf.hplt->indx, R_386_32);
		  bfd_elf32_swap_reloc_out (output_bfd, &rel, p);
		  p += sizeof (Elf32_External_Rel);
		}
	    }
	}
    }

  if (htab->sgotplt)
    {
      /* Fill in the first three entries in the global offset table.  */
      if (htab->sgotplt->size > 0)
	{
	  bfd_put_32 (output_bfd,
		      (sdyn == NULL ? 0
		       : sdyn->output_section->vma + sdyn->output_offset),
		      htab->sgotplt->contents);
	  bfd_put_32 (output_bfd, 0, htab->sgotplt->contents + 4);
	  bfd_put_32 (output_bfd, 0, htab->sgotplt->contents + 8);
	}

      elf_section_data (htab->sgotplt->output_section)->this_hdr.sh_entsize = 4;
    }

  if (htab->sgot && htab->sgot->size > 0)
    elf_section_data (htab->sgot->output_section)->this_hdr.sh_entsize = 4;

  return TRUE;
}

/* Return address for Ith PLT stub in section PLT, for relocation REL
   or (bfd_vma) -1 if it should not be included.  */

static bfd_vma
elf_i386_plt_sym_val (bfd_vma i, const asection *plt,
		      const arelent *rel ATTRIBUTE_UNUSED)
{
  return plt->vma + (i + 1) * PLT_ENTRY_SIZE;
}

/* Return TRUE if symbol should be hashed in the `.gnu.hash' section.  */

static bfd_boolean
elf_i386_hash_symbol (struct elf_link_hash_entry *h)
{
  if (h->plt.offset != (bfd_vma) -1
      && !h->def_regular
      && !h->pointer_equality_needed)
    return FALSE;

  return _bfd_elf_hash_symbol (h);
}

#define TARGET_LITTLE_SYM		bfd_elf32_i386_vec
#define TARGET_LITTLE_NAME		"elf32-i386"
#define ELF_ARCH			bfd_arch_i386
#define ELF_MACHINE_CODE		EM_386
#define ELF_MAXPAGESIZE			0x1000

#define elf_backend_can_gc_sections	1
#define elf_backend_can_refcount	1
#define elf_backend_want_got_plt	1
#define elf_backend_plt_readonly	1
#define elf_backend_want_plt_sym	0
#define elf_backend_got_header_size	12

/* Support RELA for objdump of prelink objects.  */
#define elf_info_to_howto		      elf_i386_info_to_howto_rel
#define elf_info_to_howto_rel		      elf_i386_info_to_howto_rel

#define bfd_elf32_mkobject		      elf_i386_mkobject

#define bfd_elf32_bfd_is_local_label_name     elf_i386_is_local_label_name
#define bfd_elf32_bfd_link_hash_table_create  elf_i386_link_hash_table_create
#define bfd_elf32_bfd_reloc_type_lookup	      elf_i386_reloc_type_lookup
#define bfd_elf32_bfd_reloc_name_lookup	      elf_i386_reloc_name_lookup

#define elf_backend_adjust_dynamic_symbol     elf_i386_adjust_dynamic_symbol
#define elf_backend_relocs_compatible	      _bfd_elf_relocs_compatible
#define elf_backend_check_relocs	      elf_i386_check_relocs
#define elf_backend_copy_indirect_symbol      elf_i386_copy_indirect_symbol
#define elf_backend_create_dynamic_sections   elf_i386_create_dynamic_sections
#define elf_backend_fake_sections	      elf_i386_fake_sections
#define elf_backend_finish_dynamic_sections   elf_i386_finish_dynamic_sections
#define elf_backend_finish_dynamic_symbol     elf_i386_finish_dynamic_symbol
#define elf_backend_gc_mark_hook	      elf_i386_gc_mark_hook
#define elf_backend_gc_sweep_hook	      elf_i386_gc_sweep_hook
#define elf_backend_grok_prstatus	      elf_i386_grok_prstatus
#define elf_backend_grok_psinfo		      elf_i386_grok_psinfo
#define elf_backend_reloc_type_class	      elf_i386_reloc_type_class
#define elf_backend_relocate_section	      elf_i386_relocate_section
#define elf_backend_size_dynamic_sections     elf_i386_size_dynamic_sections
#define elf_backend_always_size_sections      elf_i386_always_size_sections
#define elf_backend_omit_section_dynsym \
  ((bfd_boolean (*) (bfd *, struct bfd_link_info *, asection *)) bfd_true)
#define elf_backend_plt_sym_val		      elf_i386_plt_sym_val
#define elf_backend_hash_symbol		      elf_i386_hash_symbol

#include "elf32-target.h"

/* FreeBSD support.  */

#undef	TARGET_LITTLE_SYM
#define	TARGET_LITTLE_SYM		bfd_elf32_i386_freebsd_vec
#undef	TARGET_LITTLE_NAME
#define	TARGET_LITTLE_NAME		"elf32-i386-freebsd"
#undef	ELF_OSABI
#define	ELF_OSABI			ELFOSABI_FREEBSD

/* The kernel recognizes executables as valid only if they carry a
   "FreeBSD" label in the ELF header.  So we put this label on all
   executables and (for simplicity) also all other object files.  */

static void
elf_i386_post_process_headers (bfd *abfd,
			       struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  Elf_Internal_Ehdr *i_ehdrp;

  i_ehdrp = elf_elfheader (abfd);

  /* Put an ABI label supported by FreeBSD >= 4.1.  */
  i_ehdrp->e_ident[EI_OSABI] = get_elf_backend_data (abfd)->elf_osabi;
#ifdef OLD_FREEBSD_ABI_LABEL
  /* The ABI label supported by FreeBSD <= 4.0 is quite nonstandard.  */
  memcpy (&i_ehdrp->e_ident[EI_ABIVERSION], "FreeBSD", 8);
#endif
}

#undef	elf_backend_post_process_headers
#define	elf_backend_post_process_headers	elf_i386_post_process_headers
#undef	elf32_bed
#define	elf32_bed				elf32_i386_fbsd_bed

#include "elf32-target.h"

/* VxWorks support.  */

#undef	TARGET_LITTLE_SYM
#define TARGET_LITTLE_SYM		bfd_elf32_i386_vxworks_vec
#undef	TARGET_LITTLE_NAME
#define TARGET_LITTLE_NAME		"elf32-i386-vxworks"
#undef	ELF_OSABI

/* Like elf_i386_link_hash_table_create but with tweaks for VxWorks.  */

static struct bfd_link_hash_table *
elf_i386_vxworks_link_hash_table_create (bfd *abfd)
{
  struct bfd_link_hash_table *ret;
  struct elf_i386_link_hash_table *htab;

  ret = elf_i386_link_hash_table_create (abfd);
  if (ret)
    {
      htab = (struct elf_i386_link_hash_table *) ret;
      htab->is_vxworks = 1;
      htab->plt0_pad_byte = 0x90;
    }

  return ret;
}


#undef elf_backend_relocs_compatible
#undef elf_backend_post_process_headers
#undef bfd_elf32_bfd_link_hash_table_create
#define bfd_elf32_bfd_link_hash_table_create \
  elf_i386_vxworks_link_hash_table_create
#undef elf_backend_add_symbol_hook
#define elf_backend_add_symbol_hook \
  elf_vxworks_add_symbol_hook
#undef elf_backend_link_output_symbol_hook
#define elf_backend_link_output_symbol_hook \
  elf_vxworks_link_output_symbol_hook
#undef elf_backend_emit_relocs
#define elf_backend_emit_relocs			elf_vxworks_emit_relocs
#undef elf_backend_final_write_processing
#define elf_backend_final_write_processing \
  elf_vxworks_final_write_processing

/* On VxWorks, we emit relocations against _PROCEDURE_LINKAGE_TABLE_, so
   define it.  */
#undef elf_backend_want_plt_sym
#define elf_backend_want_plt_sym	1

#undef	elf32_bed
#define elf32_bed				elf32_i386_vxworks_bed

#include "elf32-target.h"
