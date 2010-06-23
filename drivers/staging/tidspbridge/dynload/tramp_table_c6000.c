/*
 * tramp_table_c6000.c
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

#include "dload_internal.h"

/*  These are defined in coff.h, but may not be available on all platforms
	so we'll go ahead and define them here. */
#ifndef R_C60LO16
#define R_C60LO16	  0x54	/* C60: MVK Low Half Register */
#define R_C60HI16	  0x55	/* C60: MVKH/MVKLH High Half Register */
#endif

#define C6X_TRAMP_WORD_COUNT			8
#define C6X_TRAMP_MAX_RELOS			 8

/*  THIS HASH FUNCTION MUST MATCH THE ONE IN reloc_table_c6000.c */
#define HASH_FUNC(zz) (((((zz) + 1) * UINT32_C(1845)) >> 11) & 63)

/*  THIS MUST MATCH reloc_record_t FOR A SYMBOL BASED RELO */
struct c6000_relo_record {
	s32 vaddr;
	s32 symndx;
#ifndef _BIG_ENDIAN
	u16 disp;
	u16 type;
#else
	u16 type;
	u16 disp;
#endif
};

struct c6000_gen_code {
	struct tramp_gen_code_hdr hdr;
	u32 tramp_instrs[C6X_TRAMP_WORD_COUNT];
	struct c6000_relo_record relos[C6X_TRAMP_MAX_RELOS];
};

/*  Hash mapping for relos that can cause trampolines. */
static const u16 tramp_map[] = {
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	0,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535,
	65535
};

static const struct c6000_gen_code tramp_gen_info[] = {
	/*  Tramp caused by R_C60PCR21 */
	{
	 /*  Header - 8 instructions, 2 relos */
	 {
	  sizeof(u32) * C6X_TRAMP_WORD_COUNT,
	  2,
	  FIELD_OFFSET(struct c6000_gen_code, relos)
	  },

	 /*  Trampoline instructions */
	 {
	  0x053C54F7,		/*       STW.D2T2  B10, *sp--[2] */
	  0x0500002A,		/*  || MVK.S2   <blank>, B10 */
	  0x0500006A,		/*       MVKH.S2   <blank>, B10 */
	  0x00280362,		/*       B.S2     B10 */
	  0x053C52E6,		/*       LDW.D2T2  *++sp[2], B10 */
	  0x00006000,		/*       NOP       4 */
	  0x00000000,		/*       NOP */
	  0x00000000		/*       NOP */
	  },

	 /*  Relocations */
	 {
	  {4, 0, 0, R_C60LO16},
	  {8, 0, 0, R_C60HI16},
	  {0, 0, 0, 0x0000},
	  {0, 0, 0, 0x0000},
	  {0, 0, 0, 0x0000},
	  {0, 0, 0, 0x0000},
	  {0, 0, 0, 0x0000},
	  {0, 0, 0, 0x0000}
	  }
	 }
};

/*  TARGET SPECIFIC FUNCTIONS THAT MUST BE DEFINED */
static u32 tramp_size_get(void)
{
	return sizeof(u32) * C6X_TRAMP_WORD_COUNT;
}

static u32 tramp_img_pkt_size_get(void)
{
	return sizeof(struct c6000_gen_code);
}
