/*
 * This header file describes this specific Xtensa processor's TIE extensions
 * that extend basic Xtensa core functionality.  It is customized to this
 * Xtensa processor configuration.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999-2007 Tensilica Inc.
 */

#ifndef _XTENSA_CORE_TIE_H
#define _XTENSA_CORE_TIE_H

#define XCHAL_CP_NUM			1	/* number of coprocessors */
#define XCHAL_CP_MAX			8	/* max CP ID + 1 (0 if none) */
#define XCHAL_CP_MASK			0x80	/* bitmask of all CPs by ID */
#define XCHAL_CP_PORT_MASK		0x80	/* bitmask of only port CPs */

/*  Basic parameters of each coprocessor:  */
#define XCHAL_CP7_NAME			"XTIOP"
#define XCHAL_CP7_IDENT			XTIOP
#define XCHAL_CP7_SA_SIZE		0	/* size of state save area */
#define XCHAL_CP7_SA_ALIGN		1	/* min alignment of save area */
#define XCHAL_CP_ID_XTIOP		7	/* coprocessor ID (0..7) */

/*  Filler info for unassigned coprocessors, to simplify arrays etc:  */
#define XCHAL_CP0_SA_SIZE		0
#define XCHAL_CP0_SA_ALIGN		1
#define XCHAL_CP1_SA_SIZE		0
#define XCHAL_CP1_SA_ALIGN		1
#define XCHAL_CP2_SA_SIZE		0
#define XCHAL_CP2_SA_ALIGN		1
#define XCHAL_CP3_SA_SIZE		0
#define XCHAL_CP3_SA_ALIGN		1
#define XCHAL_CP4_SA_SIZE		0
#define XCHAL_CP4_SA_ALIGN		1
#define XCHAL_CP5_SA_SIZE		0
#define XCHAL_CP5_SA_ALIGN		1
#define XCHAL_CP6_SA_SIZE		0
#define XCHAL_CP6_SA_ALIGN		1

/*  Save area for non-coprocessor optional and custom (TIE) state:  */
#define XCHAL_NCP_SA_SIZE		32
#define XCHAL_NCP_SA_ALIGN		4

/*  Total save area for optional and custom state (NCP + CPn):  */
#define XCHAL_TOTAL_SA_SIZE		32	/* with 16-byte align padding */
#define XCHAL_TOTAL_SA_ALIGN		4	/* actual minimum alignment */

/*
 * Detailed contents of save areas.
 * NOTE:  caller must define the XCHAL_SA_REG macro (not defined here)
 * before expanding the XCHAL_xxx_SA_LIST() macros.
 *
 * XCHAL_SA_REG(s,ccused,abikind,kind,opt,name,galign,align,asize,
 *		dbnum,base,regnum,bitsz,gapsz,reset,x...)
 *
 *	s = passed from XCHAL_*_LIST(s), eg. to select how to expand
 *	ccused = set if used by compiler without special options or code
 *	abikind = 0 (caller-saved), 1 (callee-saved), or 2 (thread-global)
 *	kind = 0 (special reg), 1 (TIE user reg), or 2 (TIE regfile reg)
 *	opt = 0 (custom TIE extension or coprocessor), or 1 (optional reg)
 *	name = lowercase reg name (no quotes)
 *	galign = group byte alignment (power of 2) (galign >= align)
 *	align = register byte alignment (power of 2)
 *	asize = allocated size in bytes (asize*8 == bitsz + gapsz + padsz)
 *	  (not including any pad bytes required to galign this or next reg)
 *	dbnum = unique target number f/debug (see <xtensa-libdb-macros.h>)
 *	base = reg shortname w/o index (or sr=special, ur=TIE user reg)
 *	regnum = reg index in regfile, or special/TIE-user reg number
 *	bitsz = number of significant bits (regfile width, or ur/sr mask bits)
 *	gapsz = intervening bits, if bitsz bits not stored contiguously
 *	(padsz = pad bits at end [TIE regfile] or at msbits [ur,sr] of asize)
 *	reset = register reset value (or 0 if undefined at reset)
 *	x = reserved for future use (0 until then)
 *
 *  To filter out certain registers, e.g. to expand only the non-global
 *  registers used by the compiler, you can do something like this:
 *
 *  #define XCHAL_SA_REG(s,ccused,p...)	SELCC##ccused(p)
 *  #define SELCC0(p...)
 *  #define SELCC1(abikind,p...)	SELAK##abikind(p)
 *  #define SELAK0(p...)		REG(p)
 *  #define SELAK1(p...)		REG(p)
 *  #define SELAK2(p...)
 *  #define REG(kind,tie,name,galn,aln,asz,csz,dbnum,base,rnum,bsz,rst,x...) \
 *		...what you want to expand...
 */

#define XCHAL_NCP_SA_NUM	8
#define XCHAL_NCP_SA_LIST(s)	\
 XCHAL_SA_REG(s,1,0,0,1,          acclo, 4, 4, 4,0x0210,  sr,16 , 32,0,0,0) \
 XCHAL_SA_REG(s,1,0,0,1,          acchi, 4, 4, 4,0x0211,  sr,17 ,  8,0,0,0) \
 XCHAL_SA_REG(s,0,0,0,1,             m0, 4, 4, 4,0x0220,  sr,32 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,0,1,             m1, 4, 4, 4,0x0221,  sr,33 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,0,1,             m2, 4, 4, 4,0x0222,  sr,34 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,0,1,             m3, 4, 4, 4,0x0223,  sr,35 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,0,1,      scompare1, 4, 4, 4,0x020C,  sr,12 , 32,0,0,0) \
 XCHAL_SA_REG(s,1,2,1,1,      threadptr, 4, 4, 4,0x03E7,  ur,231, 32,0,0,0)

#define XCHAL_CP0_SA_NUM	0
#define XCHAL_CP0_SA_LIST(s)	/* empty */

#define XCHAL_CP1_SA_NUM	0
#define XCHAL_CP1_SA_LIST(s)	/* empty */

#define XCHAL_CP2_SA_NUM	0
#define XCHAL_CP2_SA_LIST(s)	/* empty */

#define XCHAL_CP3_SA_NUM	0
#define XCHAL_CP3_SA_LIST(s)	/* empty */

#define XCHAL_CP4_SA_NUM	0
#define XCHAL_CP4_SA_LIST(s)	/* empty */

#define XCHAL_CP5_SA_NUM	0
#define XCHAL_CP5_SA_LIST(s)	/* empty */

#define XCHAL_CP6_SA_NUM	0
#define XCHAL_CP6_SA_LIST(s)	/* empty */

#define XCHAL_CP7_SA_NUM	0
#define XCHAL_CP7_SA_LIST(s)	/* empty */

/* Byte length of instruction from its first nibble (op0 field), per FLIX.  */
#define XCHAL_OP0_FORMAT_LENGTHS	3,3,3,3,3,3,3,3,2,2,2,2,2,2,3,3

#endif /*_XTENSA_CORE_TIE_H*/

