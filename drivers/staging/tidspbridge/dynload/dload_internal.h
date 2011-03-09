/*
 * dload_internal.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
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

#ifndef _DLOAD_INTERNAL_
#define _DLOAD_INTERNAL_

#include <linux/types.h>

/*
 * Internal state definitions for the dynamic loader
 */

/* type used for relocation intermediate results */
typedef s32 rvalue;

/* unsigned version of same; must have at least as many bits */
typedef u32 urvalue;

/*
 * Dynamic loader configuration constants
 */
/* error issued if input has more sections than this limit */
#define REASONABLE_SECTION_LIMIT 100

/* (Addressable unit) value used to clear BSS section */
#define DLOAD_FILL_BSS 0

/*
 * Reorder maps explained (?)
 *
 * The doff file format defines a 32-bit pattern used to determine the
 * byte order of an image being read.  That value is
 * BYTE_RESHUFFLE_VALUE == 0x00010203
 * For purposes of the reorder routine, we would rather have the all-is-OK
 * for 32-bits pattern be 0x03020100.  This first macro makes the
 * translation from doff file header value to MAP value: */
#define REORDER_MAP(rawmap) ((rawmap) ^ 0x3030303)
/* This translation is made in dload_headers.  Thereafter, the all-is-OK
 * value for the maps stored in dlthis is REORDER_MAP(BYTE_RESHUFFLE_VALUE).
 * But sadly, not all bits of the doff file are 32-bit integers.
 * The notable exceptions are strings and image bits.
 * Strings obey host byte order: */
#if defined(_BIG_ENDIAN)
#define HOST_BYTE_ORDER(cookedmap) ((cookedmap) ^ 0x3030303)
#else
#define HOST_BYTE_ORDER(cookedmap) (cookedmap)
#endif
/* Target bits consist of target AUs (could be bytes, or 16-bits,
 * or 32-bits) stored as an array in host order.  A target order
 * map is defined by: */
#if !defined(_BIG_ENDIAN) || TARGET_AU_BITS > 16
#define TARGET_ORDER(cookedmap) (cookedmap)
#elif TARGET_AU_BITS > 8
#define TARGET_ORDER(cookedmap) ((cookedmap) ^ 0x2020202)
#else
#define TARGET_ORDER(cookedmap) ((cookedmap) ^ 0x3030303)
#endif

/* forward declaration for handle returned by dynamic loader */
struct my_handle;

/*
 * a list of module handles, which mirrors the debug list on the target
 */
struct dbg_mirror_root {
	/* must be same as dbg_mirror_list; __DLModules address on target */
	u32 dbthis;
	struct my_handle *hnext;	/* must be same as dbg_mirror_list */
	u16 changes;		/* change counter */
	u16 refcount;		/* number of modules referencing this root */
};

struct dbg_mirror_list {
	u32 dbthis;
	struct my_handle *hnext, *hprev;
	struct dbg_mirror_root *hroot;
	u16 dbsiz;
	u32 context;	/* Save context for .dllview memory allocation */
};

#define VARIABLE_SIZE 1
/*
 * the structure we actually return as an opaque module handle
 */
struct my_handle {
	struct dbg_mirror_list dm;	/* !!! must be first !!! */
	/* sections following << 1, LSB is set for big-endian target */
	u16 secn_count;
	struct ldr_section_info secns[VARIABLE_SIZE];
};
#define MY_HANDLE_SIZE (sizeof(struct my_handle) -\
			sizeof(struct ldr_section_info))
/* real size of my_handle */

/*
 * reduced symbol structure used for symbols during relocation
 */
struct local_symbol {
	s32 value;		/* Relocated symbol value */
	s32 delta;		/* Original value in input file */
	s16 secnn;		/* section number */
	s16 sclass;		/* symbol class */
};

/*
 * Trampoline data structures
 */
#define TRAMP_NO_GEN_AVAIL              65535
#define TRAMP_SYM_PREFIX                "__$dbTR__"
#define TRAMP_SECT_NAME                 ".dbTR"
/* MUST MATCH THE LENGTH ABOVE!! */
#define TRAMP_SYM_PREFIX_LEN            9
/* Includes NULL termination */
#define TRAMP_SYM_HEX_ASCII_LEN         9

#define GET_CONTAINER(ptr, type, field) ((type *)((unsigned long)ptr -\
				(unsigned long)(&((type *)0)->field)))
#ifndef FIELD_OFFSET
#define FIELD_OFFSET(type, field)       ((unsigned long)(&((type *)0)->field))
#endif

/*
    The trampoline code for the target is located in a table called
    "tramp_gen_info" with is indexed by looking up the index in the table
    "tramp_map".  The tramp_map index is acquired using the target
    HASH_FUNC on the relocation type that caused the trampoline.  Each
    trampoline code table entry MUST follow this format:

    |----------------------------------------------|
    |  tramp_gen_code_hdr                          |
    |----------------------------------------------|
    |  Trampoline image code                       |
    |  (the raw instruction code for the target)   |
    |----------------------------------------------|
    |  Relocation entries for the image code       |
    |----------------------------------------------|

    This is very similar to how image data is laid out in the DOFF file
    itself.
 */
struct tramp_gen_code_hdr {
	u32 tramp_code_size;	/*  in BYTES */
	u32 num_relos;
	u32 relo_offset;	/*  in BYTES */
};

struct tramp_img_pkt {
	struct tramp_img_pkt *next;	/*  MUST BE FIRST */
	u32 base;
	struct tramp_gen_code_hdr hdr;
	u8 payload[VARIABLE_SIZE];
};

struct tramp_img_dup_relo {
	struct tramp_img_dup_relo *next;
	struct reloc_record_t relo;
};

struct tramp_img_dup_pkt {
	struct tramp_img_dup_pkt *next;	/*  MUST BE FIRST */
	s16 secnn;
	u32 offset;
	struct image_packet_t img_pkt;
	struct tramp_img_dup_relo *relo_chain;

	/*  PAYLOAD OF IMG PKT FOLLOWS */
};

struct tramp_sym {
	struct tramp_sym *next;	/*  MUST BE FIRST */
	u32 index;
	u32 str_index;
	struct local_symbol sym_info;
};

struct tramp_string {
	struct tramp_string *next;	/*  MUST BE FIRST */
	u32 index;
	char str[VARIABLE_SIZE];	/*  NULL terminated */
};

struct tramp_info {
	u32 tramp_sect_next_addr;
	struct ldr_section_info sect_info;

	struct tramp_sym *symbol_head;
	struct tramp_sym *symbol_tail;
	u32 tramp_sym_next_index;
	struct local_symbol *final_sym_table;

	struct tramp_string *string_head;
	struct tramp_string *string_tail;
	u32 tramp_string_next_index;
	u32 tramp_string_size;
	char *final_string_table;

	struct tramp_img_pkt *tramp_pkts;
	struct tramp_img_dup_pkt *dup_pkts;
};

/*
 * States of the .cinit state machine
 */
enum cinit_mode {
	CI_COUNT = 0,		/* expecting a count */
	CI_ADDRESS,		/* expecting an address */
#if CINIT_ALIGN < CINIT_ADDRESS	/* handle case of partial address field */
	CI_PARTADDRESS,		/* have only part of the address */
#endif
	CI_COPY,		/* in the middle of copying data */
	CI_DONE			/* end of .cinit table */
};

/*
 * The internal state of the dynamic loader, which is passed around as
 * an object
 */
struct dload_state {
	struct dynamic_loader_stream *strm;	/* The module input stream */
	struct dynamic_loader_sym *mysym;	/* Symbols for this session */
	/* target memory allocator */
	struct dynamic_loader_allocate *myalloc;
	struct dynamic_loader_initialize *myio;	/* target memory initializer */
	unsigned myoptions;	/* Options parameter dynamic_load_module */

	char *str_head;		/* Pointer to string table */
#if BITS_PER_AU > BITS_PER_BYTE
	char *str_temp;		/* Pointer to temporary buffer for strings */
	/* big enough to hold longest string */
	unsigned temp_len;	/* length of last temporary string */
	char *xstrings;		/* Pointer to buffer for expanded */
	/* strings for sec names */
#endif
	/* Total size of strings for DLLView section names */
	unsigned debug_string_size;
	/* Pointer to parallel section info for allocated sections only */
	struct doff_scnhdr_t *sect_hdrs;	/* Pointer to section table */
	struct ldr_section_info *ldr_sections;
#if TMS32060
	/* The address of the start of the .bss section */
	ldr_addr bss_run_base;
#endif
	struct local_symbol *local_symtab;	/* Relocation symbol table */

	/* pointer to DL section info for the section being relocated */
	struct ldr_section_info *image_secn;
	/* change in run address for current section during relocation */
	ldr_addr delta_runaddr;
	ldr_addr image_offset;	/* offset of current packet in section */
	enum cinit_mode cinit_state;	/* current state of cload_cinit() */
	int cinit_count;	/* the current count */
	ldr_addr cinit_addr;	/* the current address */
	s16 cinit_page;		/* the current page */
	/* Handle to be returned by dynamic_load_module */
	struct my_handle *myhandle;
	unsigned dload_errcount;	/* Total # of errors reported so far */
	/* Number of target sections that require allocation and relocation */
	unsigned allocated_secn_count;
#ifndef TARGET_ENDIANNESS
	int big_e_target;	/* Target data in big-endian format */
#endif
	/* map for reordering bytes, 0 if not needed */
	u32 reorder_map;
	struct doff_filehdr_t dfile_hdr;	/* DOFF file header structure */
	struct doff_verify_rec_t verify;	/* Verify record */

	struct tramp_info tramp;	/* Trampoline data, if needed */

	int relstkidx;		/* index into relocation value stack */
	/* relocation value stack used in relexp.c */
	rvalue relstk[STATIC_EXPR_STK_SIZE];

};

#ifdef TARGET_ENDIANNESS
#define TARGET_BIG_ENDIAN TARGET_ENDIANNESS
#else
#define TARGET_BIG_ENDIAN (dlthis->big_e_target)
#endif

/*
 * Exports from cload.c to rest of the world
 */
extern void dload_error(struct dload_state *dlthis, const char *errtxt, ...);
extern void dload_syms_error(struct dynamic_loader_sym *syms,
			     const char *errtxt, ...);
extern void dload_headers(struct dload_state *dlthis);
extern void dload_strings(struct dload_state *dlthis, bool sec_names_only);
extern void dload_sections(struct dload_state *dlthis);
extern void dload_reorder(void *data, int dsiz, u32 map);
extern u32 dload_checksum(void *data, unsigned siz);

#if HOST_ENDIANNESS
extern uint32_t dload_reverse_checksum(void *data, unsigned siz);
#if (TARGET_AU_BITS > 8) && (TARGET_AU_BITS < 32)
extern uint32_t dload_reverse_checksum16(void *data, unsigned siz);
#endif
#endif

/*
 * exported by reloc.c
 */
extern void dload_relocate(struct dload_state *dlthis, tgt_au_t * data,
			   struct reloc_record_t *rp, bool * tramps_generated,
			   bool second_pass);

extern rvalue dload_unpack(struct dload_state *dlthis, tgt_au_t * data,
			   int fieldsz, int offset, unsigned sgn);

extern int dload_repack(struct dload_state *dlthis, rvalue val, tgt_au_t * data,
			int fieldsz, int offset, unsigned sgn);

/*
 * exported by tramp.c
 */
extern bool dload_tramp_avail(struct dload_state *dlthis,
			      struct reloc_record_t *rp);

int dload_tramp_generate(struct dload_state *dlthis, s16 secnn,
			 u32 image_offset, struct image_packet_t *ipacket,
			 struct reloc_record_t *rp);

extern int dload_tramp_pkt_udpate(struct dload_state *dlthis,
				  s16 secnn, u32 image_offset,
				  struct image_packet_t *ipacket);

extern int dload_tramp_finalize(struct dload_state *dlthis);

extern void dload_tramp_cleanup(struct dload_state *dlthis);

#endif /* _DLOAD_INTERNAL_ */
