/*
 * doff.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Structures & definitions used for dynamically loaded modules file format.
 * This format is a reformatted version of COFF. It optimizes the layout for
 * the dynamic loader.
 *
 * .dof files, when viewed as a sequence of 32-bit integers, look the same
 * on big-endian and little-endian machines.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _DOFF_H
#define _DOFF_H


#define BYTE_RESHUFFLE_VALUE 0x00010203

/* DOFF file header containing fields categorizing the remainder of the file */
struct doff_filehdr_t {

	/* string table size, including filename, in bytes */
	u32 df_strtab_size;

	/* entry point if one exists */
	u32 df_entrypt;

	/* identifies byte ordering of file;
	 * always set to BYTE_RESHUFFLE_VALUE */
	u32 df_byte_reshuffle;

	/* Size of the string table up to and including the last section name */
	/* Size includes the name of the COFF file also */
	u32 df_scn_name_size;

#ifndef _BIG_ENDIAN
	/* number of symbols */
	u16 df_no_syms;

	/* length in bytes of the longest string, including terminating NULL */
	/* excludes the name of the file */
	u16 df_max_str_len;

	/* total number of sections including no-load ones */
	u16 df_no_scns;

	/* number of sections containing target code allocated or downloaded */
	u16 df_target_scns;

	/* unique id for dll file format & version */
	u16 df_doff_version;

	/* identifies ISA */
	u16 df_target_id;

	/* useful file flags */
	u16 df_flags;

	/* section reference for entry point, N_UNDEF for none, */
	/* N_ABS for absolute address */
	s16 df_entry_secn;
#else
	/* length of the longest string, including terminating NULL */
	u16 df_max_str_len;

	/* number of symbols */
	u16 df_no_syms;

	/* number of sections containing target code allocated or downloaded */
	u16 df_target_scns;

	/* total number of sections including no-load ones */
	u16 df_no_scns;

	/* identifies ISA */
	u16 df_target_id;

	/* unique id for dll file format & version */
	u16 df_doff_version;

	/* section reference for entry point, N_UNDEF for none, */
	/* N_ABS for absolute address */
	s16 df_entry_secn;

	/* useful file flags */
	u16 df_flags;
#endif
	/* checksum for file header record */
	u32 df_checksum;

};

/* flags in the df_flags field */
#define  DF_LITTLE   0x100
#define  DF_BIG      0x200
#define  DF_BYTE_ORDER (DF_LITTLE | DF_BIG)

/* Supported processors */
#define TMS470_ID   0x97
#define LEAD_ID     0x98
#define TMS32060_ID 0x99
#define LEAD3_ID    0x9c

/* Primary processor for loading */
#if TMS32060
#define TARGET_ID   TMS32060_ID
#endif

/* Verification record containing values used to test integrity of the bits */
struct doff_verify_rec_t {

	/* time and date stamp */
	u32 dv_timdat;

	/* checksum for all section records */
	u32 dv_scn_rec_checksum;

	/* checksum for string table */
	u32 dv_str_tab_checksum;

	/* checksum for symbol table */
	u32 dv_sym_tab_checksum;

	/* checksum for verification record */
	u32 dv_verify_rec_checksum;

};

/* String table is an array of null-terminated strings.  The first entry is
 * the filename, which is added by DLLcreate.  No new structure definitions
 * are required.
 */

/* Section Records including information on the corresponding image packets */
/*
 *      !!WARNING!!
 *
 * This structure is expected to match in form ldr_section_info in
 * dynamic_loader.h
 */

struct doff_scnhdr_t {

	s32 ds_offset;		/* offset into string table of name */
	s32 ds_paddr;		/* RUN address, in target AU */
	s32 ds_vaddr;		/* LOAD address, in target AU */
	s32 ds_size;		/* section size, in target AU */
#ifndef _BIG_ENDIAN
	u16 ds_page;		/* memory page id */
	u16 ds_flags;		/* section flags */
#else
	u16 ds_flags;		/* section flags */
	u16 ds_page;		/* memory page id */
#endif
	u32 ds_first_pkt_offset;
	/* Absolute byte offset into the file */
	/* where the first image record resides */

	s32 ds_nipacks;		/* number of image packets */

};

/* Symbol table entry */
struct doff_syment_t {

	s32 dn_offset;		/* offset into string table of name */
	s32 dn_value;		/* value of symbol */
#ifndef _BIG_ENDIAN
	s16 dn_scnum;		/* section number */
	s16 dn_sclass;		/* storage class */
#else
	s16 dn_sclass;		/* storage class */
	s16 dn_scnum;		/* section number, 1-based */
#endif

};

/* special values for dn_scnum */
#define  DN_UNDEF  0		/* undefined symbol */
#define  DN_ABS    (-1)		/* value of symbol is absolute */
/* special values for dn_sclass */
#define DN_EXT     2
#define DN_STATLAB 20
#define DN_EXTLAB  21

/* Default value of image bits in packet */
/* Configurable by user on the command line */
#define IMAGE_PACKET_SIZE 1024

/* An image packet contains a chunk of data from a section along with */
/* information necessary for its processing. */
struct image_packet_t {

	s32 num_relocs;		/* number of relocations for */
	/* this packet */

	s32 packet_size;	/* number of bytes in array */
	/* "bits" occupied  by */
	/* valid data.  Could be */
	/* < IMAGE_PACKET_SIZE to */
	/* prevent splitting a */
	/* relocation across packets. */
	/* Last packet of a section */
	/* will most likely contain */
	/* < IMAGE_PACKET_SIZE bytes */
	/* of valid data */

	s32 img_chksum;		/* Checksum for image packet */
	/* and the corresponding */
	/* relocation records */

	u8 *img_data;		/* Actual data in section */

};

/* The relocation structure definition matches the COFF version.  Offsets */
/* however are relative to the image packet base not the section base. */
struct reloc_record_t {

	s32 vaddr;

	/* expressed in target AUs */

	union {
		struct {
#ifndef _BIG_ENDIAN
			u8 _offset;	/* bit offset of rel fld */
			u8 _fieldsz;	/* size of rel fld */
			u8 _wordsz;	/* # bytes containing rel fld */
			u8 _dum1;
			u16 _dum2;
			u16 _type;
#else
			unsigned _dum1:8;
			unsigned _wordsz:8;	/* # bytes containing rel fld */
			unsigned _fieldsz:8;	/* size of rel fld */
			unsigned _offset:8;	/* bit offset of rel fld */
			u16 _type;
			u16 _dum2;
#endif
		} _r_field;

		struct {
			u32 _spc;	/* image packet relative PC */
#ifndef _BIG_ENDIAN
			u16 _dum;
			u16 _type;	/* relocation type */
#else
			u16 _type;	/* relocation type */
			u16 _dum;
#endif
		} _r_spc;

		struct {
			u32 _uval;	/* constant value */
#ifndef _BIG_ENDIAN
			u16 _dum;
			u16 _type;	/* relocation type */
#else
			u16 _type;	/* relocation type */
			u16 _dum;
#endif
		} _r_uval;

		struct {
			s32 _symndx;	/* 32-bit sym tbl index */
#ifndef _BIG_ENDIAN
			u16 _disp;	/* extra addr encode data */
			u16 _type;	/* relocation type */
#else
			u16 _type;	/* relocation type */
			u16 _disp;	/* extra addr encode data */
#endif
		} _r_sym;
	} _u_reloc;

};

/* abbreviations for convenience */
#ifndef TYPE
#define TYPE      _u_reloc._r_sym._type
#define UVAL      _u_reloc._r_uval._uval
#define SYMNDX    _u_reloc._r_sym._symndx
#define OFFSET    _u_reloc._r_field._offset
#define FIELDSZ   _u_reloc._r_field._fieldsz
#define WORDSZ    _u_reloc._r_field._wordsz
#define R_DISP      _u_reloc._r_sym._disp
#endif

/**************************************************************************** */
/* */
/* Important DOFF macros used for file processing */
/* */
/**************************************************************************** */

/* DOFF Versions */
#define         DOFF0                       0

/* Return the address/size >= to addr that is at a 32-bit boundary */
/* This assumes that a byte is 8 bits */
#define         DOFF_ALIGN(addr)            (((addr) + 3) & ~3UL)

/**************************************************************************** */
/* */
/* The DOFF section header flags field is laid out as follows: */
/* */
/*  Bits 0-3 : Section Type */
/*  Bit    4 : Set when section requires target memory to be allocated by DL */
/*  Bit    5 : Set when section requires downloading */
/*  Bits 8-11: Alignment, same as COFF */
/* */
/**************************************************************************** */

/* Enum for DOFF section types (bits 0-3 of flag): See dynamic_loader.h */
#define DS_SECTION_TYPE_MASK	0xF
/* DS_ALLOCATE indicates whether a section needs space on the target */
#define DS_ALLOCATE_MASK            0x10
/* DS_DOWNLOAD indicates that the loader needs to copy bits */
#define DS_DOWNLOAD_MASK            0x20
/* Section alignment requirement in AUs */
#define DS_ALIGNMENT_SHIFT	8

static inline bool dload_check_type(struct doff_scnhdr_t *sptr, u32 flag)
{
	return (sptr->ds_flags & DS_SECTION_TYPE_MASK) == flag;
}
static inline bool ds_needs_allocation(struct doff_scnhdr_t *sptr)
{
	return sptr->ds_flags & DS_ALLOCATE_MASK;
}

static inline bool ds_needs_download(struct doff_scnhdr_t *sptr)
{
	return sptr->ds_flags & DS_DOWNLOAD_MASK;
}

static inline int ds_alignment(u16 ds_flags)
{
	return 1 << ((ds_flags >> DS_ALIGNMENT_SHIFT) & DS_SECTION_TYPE_MASK);
}


#endif /* _DOFF_H */
