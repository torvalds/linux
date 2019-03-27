  /* No relocation.  */
  HOWTO (R_SH_NONE,		/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_NONE",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* 32 bit absolute relocation.  Setting partial_inplace to TRUE and
     src_mask to a non-zero value is similar to the COFF toolchain.  */
  HOWTO (R_SH_DIR32,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 SH_ELF_RELOC,		/* special_function */
	 "R_SH_DIR32",		/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* 32 bit PC relative relocation.  */
  HOWTO (R_SH_REL32,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_REL32",		/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 8 bit PC relative branch divided by 2.  */
  HOWTO (R_SH_DIR8WPN,		/* type */
	 1,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_DIR8WPN",	/* name */
	 TRUE,			/* partial_inplace */
	 0xff,			/* src_mask */
	 0xff,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 12 bit PC relative branch divided by 2.  */
  /* This cannot be partial_inplace because relaxation can't know the
     eventual value of a symbol.  */
  HOWTO (R_SH_IND12W,		/* type */
	 1,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 12,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 NULL,			/* special_function */
	 "R_SH_IND12W",		/* name */
	 FALSE,			/* partial_inplace */
	 0x0,			/* src_mask */
	 0xfff,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 8 bit unsigned PC relative divided by 4.  */
  HOWTO (R_SH_DIR8WPL,		/* type */
	 2,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_DIR8WPL",	/* name */
	 TRUE,			/* partial_inplace */
	 0xff,			/* src_mask */
	 0xff,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 8 bit unsigned PC relative divided by 2.  */
  HOWTO (R_SH_DIR8WPZ,		/* type */
	 1,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_DIR8WPZ",	/* name */
	 TRUE,			/* partial_inplace */
	 0xff,			/* src_mask */
	 0xff,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 8 bit GBR relative.  FIXME: This only makes sense if we have some
     special symbol for the GBR relative area, and that is not
     implemented.  */
  HOWTO (R_SH_DIR8BP,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_DIR8BP",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 8 bit GBR relative divided by 2.  FIXME: This only makes sense if
     we have some special symbol for the GBR relative area, and that
     is not implemented.  */
  HOWTO (R_SH_DIR8W,		/* type */
	 1,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_DIR8W",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 8 bit GBR relative divided by 4.  FIXME: This only makes sense if
     we have some special symbol for the GBR relative area, and that
     is not implemented.  */
  HOWTO (R_SH_DIR8L,		/* type */
	 2,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_DIR8L",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 8 bit PC relative divided by 2 - but specified in a very odd way.  */
  HOWTO (R_SH_LOOP_START,	/* type */
	 1,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_LOOP_START",	/* name */
	 TRUE,			/* partial_inplace */
	 0xff,			/* src_mask */
	 0xff,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* 8 bit PC relative divided by 2 - but specified in a very odd way.  */
  HOWTO (R_SH_LOOP_END,		/* type */
	 1,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_LOOP_END",	/* name */
	 TRUE,			/* partial_inplace */
	 0xff,			/* src_mask */
	 0xff,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  EMPTY_HOWTO (12),
  EMPTY_HOWTO (13),
  EMPTY_HOWTO (14),
  EMPTY_HOWTO (15),
  EMPTY_HOWTO (16),
  EMPTY_HOWTO (17),
  EMPTY_HOWTO (18),
  EMPTY_HOWTO (19),
  EMPTY_HOWTO (20),
  EMPTY_HOWTO (21),

  /* The remaining relocs are a GNU extension used for relaxing.  The
     final pass of the linker never needs to do anything with any of
     these relocs.  Any required operations are handled by the
     relaxation code.  */

  /* GNU extension to record C++ vtable hierarchy */
  HOWTO (R_SH_GNU_VTINHERIT, /* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 NULL,			/* special_function */
	 "R_SH_GNU_VTINHERIT", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* GNU extension to record C++ vtable member usage */
  HOWTO (R_SH_GNU_VTENTRY,     /* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 _bfd_elf_rel_vtable_reloc_fn,	/* special_function */
	 "R_SH_GNU_VTENTRY",   /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* An 8 bit switch table entry.  This is generated for an expression
     such as ``.word L1 - L2''.  The offset holds the difference
     between the reloc address and L2.  */
  HOWTO (R_SH_SWITCH8,		/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_SWITCH8",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* A 16 bit switch table entry.  This is generated for an expression
     such as ``.word L1 - L2''.  The offset holds the difference
     between the reloc address and L2.  */
  HOWTO (R_SH_SWITCH16,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_SWITCH16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* A 32 bit switch table entry.  This is generated for an expression
     such as ``.long L1 - L2''.  The offset holds the difference
     between the reloc address and L2.  */
  HOWTO (R_SH_SWITCH32,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_SWITCH32",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Indicates a .uses pseudo-op.  The compiler will generate .uses
     pseudo-ops when it finds a function call which can be relaxed.
     The offset field holds the PC relative offset to the instruction
     which loads the register used in the function call.  */
  HOWTO (R_SH_USES,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_USES",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* The assembler will generate this reloc for addresses referred to
     by the register loads associated with USES relocs.  The offset
     field holds the number of times the address is referenced in the
     object file.  */
  HOWTO (R_SH_COUNT,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_COUNT",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Indicates an alignment statement.  The offset field is the power
     of 2 to which subsequent portions of the object file must be
     aligned.  */
  HOWTO (R_SH_ALIGN,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_ALIGN",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* The assembler will generate this reloc before a block of
     instructions.  A section should be processed as assuming it
     contains data, unless this reloc is seen.  */
  HOWTO (R_SH_CODE,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_CODE",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* The assembler will generate this reloc after a block of
     instructions when it sees data that is not instructions.  */
  HOWTO (R_SH_DATA,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_DATA",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* The assembler generates this reloc for each label within a block
     of instructions.  This permits the linker to avoid swapping
     instructions which are the targets of branches.  */
  HOWTO (R_SH_LABEL,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_LABEL",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* The next 12 are only supported via linking in SHC-generated objects.  */
  HOWTO (R_SH_DIR16,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR16",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_DIR8,		/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR8",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_DIR8UL,		/* type */
	 2,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR8UL",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_DIR8UW,		/* type */
	 1,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR8UW",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_DIR8U,		/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR8U",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_DIR8SW,		/* type */
	 1,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR8SW",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_DIR8S,		/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 8,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR8S",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xff,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_DIR4UL,		/* type */
	 2,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 4,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR4UL",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0f,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_DIR4UW,		/* type */
	 1,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 4,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR4UW",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0f,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_DIR4U,		/* type */
	 0,			/* rightshift */
	 0,			/* size (0 = byte, 1 = short, 2 = long) */
	 4,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR4U",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0f,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_PSHA,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 7,			/* bitsize */
	 FALSE,			/* pc_relative */
	 4,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_PSHA",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0f,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_PSHL,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 7,			/* bitsize */
	 FALSE,			/* pc_relative */
	 4,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_PSHL",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x0f,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

#ifdef INCLUDE_SHMEDIA
  /* Used in SHLLI.L and SHLRI.L.  */
  HOWTO (R_SH_DIR5U,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 5,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR5U",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in SHARI, SHLLI et al.  */
  HOWTO (R_SH_DIR6U,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 6,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR6U",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in BxxI, LDHI.L et al.  */
  HOWTO (R_SH_DIR6S,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 6,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR6S",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xfc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in ADDI, ANDI et al.  */
  HOWTO (R_SH_DIR10S,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 10,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR10S",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in LD.UW, ST.W et al.	 */
  HOWTO (R_SH_DIR10SW,	/* type */
	 1,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 11,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR10SW",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in LD.L, FLD.S et al.	 */
  HOWTO (R_SH_DIR10SL,	/* type */
	 2,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 12,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR10SL",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in FLD.D, FST.P et al.  */
  HOWTO (R_SH_DIR10SQ,	/* type */
	 3,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 13,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR10SQ",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

#else
  EMPTY_HOWTO (45),
  EMPTY_HOWTO (46),
  EMPTY_HOWTO (47),
  EMPTY_HOWTO (48),
  EMPTY_HOWTO (49),
  EMPTY_HOWTO (50),
  EMPTY_HOWTO (51),
#endif

  EMPTY_HOWTO (52),

  HOWTO (R_SH_DIR16S,		/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_DIR16S",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  EMPTY_HOWTO (54),
  EMPTY_HOWTO (55),
  EMPTY_HOWTO (56),
  EMPTY_HOWTO (57),
  EMPTY_HOWTO (58),
  EMPTY_HOWTO (59),
  EMPTY_HOWTO (60),
  EMPTY_HOWTO (61),
  EMPTY_HOWTO (62),
  EMPTY_HOWTO (63),
  EMPTY_HOWTO (64),
  EMPTY_HOWTO (65),
  EMPTY_HOWTO (66),
  EMPTY_HOWTO (67),
  EMPTY_HOWTO (68),
  EMPTY_HOWTO (69),
  EMPTY_HOWTO (70),
  EMPTY_HOWTO (71),
  EMPTY_HOWTO (72),
  EMPTY_HOWTO (73),
  EMPTY_HOWTO (74),
  EMPTY_HOWTO (75),
  EMPTY_HOWTO (76),
  EMPTY_HOWTO (77),
  EMPTY_HOWTO (78),
  EMPTY_HOWTO (79),
  EMPTY_HOWTO (80),
  EMPTY_HOWTO (81),
  EMPTY_HOWTO (82),
  EMPTY_HOWTO (83),
  EMPTY_HOWTO (84),
  EMPTY_HOWTO (85),
  EMPTY_HOWTO (86),
  EMPTY_HOWTO (87),
  EMPTY_HOWTO (88),
  EMPTY_HOWTO (89),
  EMPTY_HOWTO (90),
  EMPTY_HOWTO (91),
  EMPTY_HOWTO (92),
  EMPTY_HOWTO (93),
  EMPTY_HOWTO (94),
  EMPTY_HOWTO (95),
  EMPTY_HOWTO (96),
  EMPTY_HOWTO (97),
  EMPTY_HOWTO (98),
  EMPTY_HOWTO (99),
  EMPTY_HOWTO (100),
  EMPTY_HOWTO (101),
  EMPTY_HOWTO (102),
  EMPTY_HOWTO (103),
  EMPTY_HOWTO (104),
  EMPTY_HOWTO (105),
  EMPTY_HOWTO (106),
  EMPTY_HOWTO (107),
  EMPTY_HOWTO (108),
  EMPTY_HOWTO (109),
  EMPTY_HOWTO (110),
  EMPTY_HOWTO (111),
  EMPTY_HOWTO (112),
  EMPTY_HOWTO (113),
  EMPTY_HOWTO (114),
  EMPTY_HOWTO (115),
  EMPTY_HOWTO (116),
  EMPTY_HOWTO (117),
  EMPTY_HOWTO (118),
  EMPTY_HOWTO (119),
  EMPTY_HOWTO (120),
  EMPTY_HOWTO (121),
  EMPTY_HOWTO (122),
  EMPTY_HOWTO (123),
  EMPTY_HOWTO (124),
  EMPTY_HOWTO (125),
  EMPTY_HOWTO (126),
  EMPTY_HOWTO (127),
  EMPTY_HOWTO (128),
  EMPTY_HOWTO (129),
  EMPTY_HOWTO (130),
  EMPTY_HOWTO (131),
  EMPTY_HOWTO (132),
  EMPTY_HOWTO (133),
  EMPTY_HOWTO (134),
  EMPTY_HOWTO (135),
  EMPTY_HOWTO (136),
  EMPTY_HOWTO (137),
  EMPTY_HOWTO (138),
  EMPTY_HOWTO (139),
  EMPTY_HOWTO (140),
  EMPTY_HOWTO (141),
  EMPTY_HOWTO (142),
  EMPTY_HOWTO (143),

  HOWTO (R_SH_TLS_GD_32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* */
	 "R_SH_TLS_GD_32",	/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_TLS_LD_32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* */
	 "R_SH_TLS_LD_32",	/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_TLS_LDO_32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* */
	 "R_SH_TLS_LDO_32",	/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_TLS_IE_32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* */
	 "R_SH_TLS_IE_32",	/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_TLS_LE_32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* */
	 "R_SH_TLS_LE_32",	/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_TLS_DTPMOD32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* */
	 "R_SH_TLS_DTPMOD32",	/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_TLS_DTPOFF32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* */
	 "R_SH_TLS_DTPOFF32",	/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_TLS_TPOFF32,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* */
	 "R_SH_TLS_TPOFF32",	/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  EMPTY_HOWTO (152),
  EMPTY_HOWTO (153),
  EMPTY_HOWTO (154),
  EMPTY_HOWTO (155),
  EMPTY_HOWTO (156),
  EMPTY_HOWTO (157),
  EMPTY_HOWTO (158),
  EMPTY_HOWTO (159),

  HOWTO (R_SH_GOT32,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc, /* */
	 "R_SH_GOT32",		/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_PLT32,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc, /* */
	 "R_SH_PLT32",		/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  HOWTO (R_SH_COPY,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc, /* */
	 "R_SH_COPY",		/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_GLOB_DAT,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc, /* */
	 "R_SH_GLOB_DAT",	/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_JMP_SLOT,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc, /* */
	 "R_SH_JMP_SLOT",	/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_RELATIVE,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc, /* */
	 "R_SH_RELATIVE",	/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_GOTOFF,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc, /* */
	 "R_SH_GOTOFF",		/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_GOTPC,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 TRUE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc, /* */
	 "R_SH_GOTPC",		/* name */
	 SH_PARTIAL32,		/* partial_inplace */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  HOWTO (R_SH_GOTPLT32,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 32,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_bitfield, /* complain_on_overflow */
	 bfd_elf_generic_reloc, /* */
	 "R_SH_GOTPLT32",	/* name */
	 FALSE,			/* partial_inplace */
	 /* ??? Why not 0?  */
	 SH_SRC_MASK32,		/* src_mask */
	 0xffffffff,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

#ifdef INCLUDE_SHMEDIA
  /* Used in MOVI and SHORI (x & 65536).  */
  HOWTO (R_SH_GOT_LOW16,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOT_LOW16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 16) & 65536).  */
  HOWTO (R_SH_GOT_MEDLOW16,	/* type */
	 16,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOT_MEDLOW16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 32) & 65536).  */
  HOWTO (R_SH_GOT_MEDHI16,	/* type */
	 32,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOT_MEDHI16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 48) & 65536).  */
  HOWTO (R_SH_GOT_HI16,		/* type */
	 48,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOT_HI16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI (x & 65536).  */
  HOWTO (R_SH_GOTPLT_LOW16,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTPLT_LOW16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 16) & 65536).  */
  HOWTO (R_SH_GOTPLT_MEDLOW16,	/* type */
	 16,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTPLT_MEDLOW16", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 32) & 65536).  */
  HOWTO (R_SH_GOTPLT_MEDHI16,	/* type */
	 32,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTPLT_MEDHI16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 48) & 65536).  */
  HOWTO (R_SH_GOTPLT_HI16,	/* type */
	 48,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTPLT_HI16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI (x & 65536).  */
  HOWTO (R_SH_PLT_LOW16,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_PLT_LOW16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 16) & 65536).  */
  HOWTO (R_SH_PLT_MEDLOW16,	/* type */
	 16,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_PLT_MEDLOW16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 32) & 65536).  */
  HOWTO (R_SH_PLT_MEDHI16,	/* type */
	 32,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_PLT_MEDHI16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 48) & 65536).  */
  HOWTO (R_SH_PLT_HI16,		/* type */
	 48,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_PLT_HI16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Used in MOVI and SHORI (x & 65536).  */
  HOWTO (R_SH_GOTOFF_LOW16,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTOFF_LOW16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 16) & 65536).  */
  HOWTO (R_SH_GOTOFF_MEDLOW16,	/* type */
	 16,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTOFF_MEDLOW16", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 32) & 65536).  */
  HOWTO (R_SH_GOTOFF_MEDHI16,	/* type */
	 32,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTOFF_MEDHI16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 48) & 65536).  */
  HOWTO (R_SH_GOTOFF_HI16,	/* type */
	 48,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTOFF_HI16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI (x & 65536).  */
  HOWTO (R_SH_GOTPC_LOW16,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTPC_LOW16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 16) & 65536).  */
  HOWTO (R_SH_GOTPC_MEDLOW16,	/* type */
	 16,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTPC_MEDLOW16", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 32) & 65536).  */
  HOWTO (R_SH_GOTPC_MEDHI16,	/* type */
	 32,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTPC_MEDHI16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 48) & 65536).  */
  HOWTO (R_SH_GOTPC_HI16,	/* type */
	 48,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTPC_HI16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Used in LD.L, FLD.S et al.	 */
  HOWTO (R_SH_GOT10BY4,		/* type */
	 2,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 12,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOT10BY4",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in LD.L, FLD.S et al.	 */
  HOWTO (R_SH_GOTPLT10BY4,	/* type */
	 2,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 12,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTPLT10BY4",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in FLD.D, FST.P et al.  */
  HOWTO (R_SH_GOT10BY8,		/* type */
	 3,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 13,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOT10BY8",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in FLD.D, FST.P et al.  */
  HOWTO (R_SH_GOTPLT10BY8,	/* type */
	 3,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 13,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GOTPLT10BY8",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0xffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_COPY64,		/* type */
	 0,			/* rightshift */
	 4,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_COPY64",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ((bfd_vma) 0) - 1,	/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_GLOB_DAT64,	/* type */
	 0,			/* rightshift */
	 4,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_GLOB_DAT64",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ((bfd_vma) 0) - 1,	/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_JMP_SLOT64,	/* type */
	 0,			/* rightshift */
	 4,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_JMP_SLOT64",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ((bfd_vma) 0) - 1,	/* dst_mask */
	 FALSE),		/* pcrel_offset */

  HOWTO (R_SH_RELATIVE64,	/* type */
	 0,			/* rightshift */
	 4,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_RELATIVE64",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ((bfd_vma) 0) - 1,	/* dst_mask */
	 FALSE),		/* pcrel_offset */

  EMPTY_HOWTO (197),
  EMPTY_HOWTO (198),
  EMPTY_HOWTO (199),
  EMPTY_HOWTO (200),
  EMPTY_HOWTO (201),
  EMPTY_HOWTO (202),
  EMPTY_HOWTO (203),
  EMPTY_HOWTO (204),
  EMPTY_HOWTO (205),
  EMPTY_HOWTO (206),
  EMPTY_HOWTO (207),
  EMPTY_HOWTO (208),
  EMPTY_HOWTO (209),
  EMPTY_HOWTO (210),
  EMPTY_HOWTO (211),
  EMPTY_HOWTO (212),
  EMPTY_HOWTO (213),
  EMPTY_HOWTO (214),
  EMPTY_HOWTO (215),
  EMPTY_HOWTO (216),
  EMPTY_HOWTO (217),
  EMPTY_HOWTO (218),
  EMPTY_HOWTO (219),
  EMPTY_HOWTO (220),
  EMPTY_HOWTO (221),
  EMPTY_HOWTO (222),
  EMPTY_HOWTO (223),
  EMPTY_HOWTO (224),
  EMPTY_HOWTO (225),
  EMPTY_HOWTO (226),
  EMPTY_HOWTO (227),
  EMPTY_HOWTO (228),
  EMPTY_HOWTO (229),
  EMPTY_HOWTO (230),
  EMPTY_HOWTO (231),
  EMPTY_HOWTO (232),
  EMPTY_HOWTO (233),
  EMPTY_HOWTO (234),
  EMPTY_HOWTO (235),
  EMPTY_HOWTO (236),
  EMPTY_HOWTO (237),
  EMPTY_HOWTO (238),
  EMPTY_HOWTO (239),
  EMPTY_HOWTO (240),
  EMPTY_HOWTO (241),

  /* Relocations for SHmedia code.  None of these are partial_inplace or
     use the field being relocated (except R_SH_PT_16).  */

  /* The assembler will generate this reloc before a block of SHmedia
     instructions.  A section should be processed as assuming it contains
     data, unless this reloc is seen.  Note that a block of SHcompact
     instructions are instead preceded by R_SH_CODE.
     This is currently not implemented, but should be used for SHmedia
     linker relaxation.  */
  HOWTO (R_SH_SHMEDIA_CODE,	/* type */
	 0,			/* rightshift */
	 1,			/* size (0 = byte, 1 = short, 2 = long) */
	 0,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 sh_elf_ignore_reloc,	/* special_function */
	 "R_SH_SHMEDIA_CODE",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0,			/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* The assembler will generate this reloc at a PTA or PTB instruction,
     and the linker checks the right type of target, or changes a PTA to a
     PTB, if the original insn was PT.  */
  HOWTO (R_SH_PT_16,		/* type */
	 2,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 18,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_PT_16",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Used in unexpanded MOVI.  */
  HOWTO (R_SH_IMMS16,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_signed, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_IMMS16",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in SHORI.  */
  HOWTO (R_SH_IMMU16,		/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 16,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_unsigned, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_IMMU16",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI (x & 65536).  */
  HOWTO (R_SH_IMM_LOW16,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_IMM_LOW16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI ((x - $) & 65536).  */
  HOWTO (R_SH_IMM_LOW16_PCREL,	/* type */
	 0,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_IMM_LOW16_PCREL", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 16) & 65536).  */
  HOWTO (R_SH_IMM_MEDLOW16,	/* type */
	 16,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_IMM_MEDLOW16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI (((x - $) >> 16) & 65536).  */
  HOWTO (R_SH_IMM_MEDLOW16_PCREL, /* type */
	 16,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_IMM_MEDLOW16_PCREL", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 32) & 65536).  */
  HOWTO (R_SH_IMM_MEDHI16,	/* type */
	 32,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_IMM_MEDHI16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI (((x - $) >> 32) & 65536).  */
  HOWTO (R_SH_IMM_MEDHI16_PCREL, /* type */
	 32,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_IMM_MEDHI16_PCREL", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* Used in MOVI and SHORI ((x >> 48) & 65536).  */
  HOWTO (R_SH_IMM_HI16,		/* type */
	 48,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_IMM_HI16",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* Used in MOVI and SHORI (((x - $) >> 48) & 65536).  */
  HOWTO (R_SH_IMM_HI16_PCREL,	/* type */
	 48,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_IMM_HI16_PCREL", /* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 0x3fffc00,		/* dst_mask */
	 TRUE),			/* pcrel_offset */

  /* For the .uaquad pseudo.  */
  HOWTO (R_SH_64,		/* type */
	 0,			/* rightshift */
	 4,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 FALSE,			/* pc_relative */
	 0,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_64",		/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ((bfd_vma) 0) - 1,	/* dst_mask */
	 FALSE),		/* pcrel_offset */

  /* For the .uaquad pseudo, (x - $).  */
  HOWTO (R_SH_64_PCREL,		/* type */
	 48,			/* rightshift */
	 2,			/* size (0 = byte, 1 = short, 2 = long) */
	 64,			/* bitsize */
	 TRUE,			/* pc_relative */
	 10,			/* bitpos */
	 complain_overflow_dont, /* complain_on_overflow */
	 bfd_elf_generic_reloc,	/* special_function */
	 "R_SH_64_PCREL",	/* name */
	 FALSE,			/* partial_inplace */
	 0,			/* src_mask */
	 ((bfd_vma) 0) - 1,	/* dst_mask */
	 TRUE),			/* pcrel_offset */

#endif
#undef SH_PARTIAL32
#undef SH_SRC_MASK32
#undef SH_ELF_RELOC
