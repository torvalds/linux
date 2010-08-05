/*
 * params.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * This file defines host and target properties for all machines
 * supported by the dynamic loader.  To be tedious...
 *
 * host: the machine on which the dynamic loader runs
 * target: the machine that the dynamic loader is loading
 *
 * Host and target may or may not be the same, depending upon the particular
 * use.
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

/******************************************************************************
 *
 *							Host Properties
 *
 **************************************************************************** */

#define BITS_PER_BYTE 8		/* bits in the standard PC/SUN byte */
#define LOG_BITS_PER_BYTE 3	/* log base 2 of same */
#define BYTE_MASK ((1U<<BITS_PER_BYTE)-1)

#if defined(__TMS320C55X__) || defined(_TMS320C5XX)
#define BITS_PER_AU 16
#define LOG_BITS_PER_AU 4
 /* use this print string in error messages for uint32_t */
#define FMT_UI32 "0x%lx"
#define FMT8_UI32 "%08lx"	/* same but no 0x, fixed width field */
#else
/* bits in the smallest addressable data storage unit */
#define BITS_PER_AU 8
/* log base 2 of the same; useful for shift counts */
#define LOG_BITS_PER_AU 3
#define FMT_UI32 "0x%x"
#define FMT8_UI32 "%08x"
#endif

/* generic fastest method for swapping bytes and shorts */
#define SWAP32BY16(zz) (((zz) << 16) | ((zz) >> 16))
#define SWAP16BY8(zz) (((zz) << 8) | ((zz) >> 8))

/* !! don't be tempted to insert type definitions here; use <stdint.h> !! */

/******************************************************************************
 *
 *							Target Properties
 *
 **************************************************************************** */

/*-------------------------------------------------------------------------- */
/* TMS320C6x Target Specific Parameters (byte-addressable) */
/*-------------------------------------------------------------------------- */
#if TMS32060
#define MEMORG          0x0L	/* Size of configured memory */
#define MEMSIZE         0x0L	/* (full address space) */

#define CINIT_ALIGN     8	/* alignment of cinit record in TDATA AUs */
#define CINIT_COUNT	4	/* width of count field in TDATA AUs */
#define CINIT_ADDRESS	4	/* width of address field in TDATA AUs */
#define CINIT_PAGE_BITS	0	/* Number of LSBs of address that
				 * are page number */

#define LENIENT_SIGNED_RELEXPS 0	/* DOES SIGNED ALLOW MAX UNSIGNED */

#undef TARGET_ENDIANNESS	/* may be big or little endian */

/* align a target address to a word boundary */
#define TARGET_WORD_ALIGN(zz) (((zz) + 0x3) & -0x4)
#endif

/*--------------------------------------------------------------------------
 *
 *			DEFAULT SETTINGS and DERIVED PROPERTIES
 *
 * This section establishes defaults for values not specified above
 *-------------------------------------------------------------------------- */
#ifndef TARGET_AU_BITS
#define TARGET_AU_BITS 8	/* width of the target addressable unit */
#define LOG_TARGET_AU_BITS 3	/* log2 of same */
#endif

#ifndef CINIT_DEFAULT_PAGE
#define CINIT_DEFAULT_PAGE 0	/* default .cinit page number */
#endif

#ifndef DATA_RUN2LOAD
#define DATA_RUN2LOAD(zz) (zz)	/* translate data run address to load address */
#endif

#ifndef DBG_LIST_PAGE
#define DBG_LIST_PAGE 0		/* page number for .dllview section */
#endif

#ifndef TARGET_WORD_ALIGN
/* align a target address to a word boundary */
#define TARGET_WORD_ALIGN(zz) (zz)
#endif

#ifndef TDATA_TO_TADDR
#define TDATA_TO_TADDR(zz) (zz)	/* target data address to target AU address */
#define TADDR_TO_TDATA(zz) (zz)	/* target AU address to target data address */
#define TDATA_AU_BITS	TARGET_AU_BITS	/* bits per data AU */
#define LOG_TDATA_AU_BITS	LOG_TARGET_AU_BITS
#endif

/*
 *
 * Useful properties and conversions derived from the above
 *
 */

/*
 * Conversions between host and target addresses
 */
#if LOG_BITS_PER_AU == LOG_TARGET_AU_BITS
/* translate target addressable unit to host address */
#define TADDR_TO_HOST(x) (x)
/* translate host address to target addressable unit */
#define HOST_TO_TADDR(x) (x)
#elif LOG_BITS_PER_AU > LOG_TARGET_AU_BITS
#define TADDR_TO_HOST(x) ((x) >> (LOG_BITS_PER_AU-LOG_TARGET_AU_BITS))
#define HOST_TO_TADDR(x) ((x) << (LOG_BITS_PER_AU-LOG_TARGET_AU_BITS))
#else
#define TADDR_TO_HOST(x) ((x) << (LOG_TARGET_AU_BITS-LOG_BITS_PER_AU))
#define HOST_TO_TADDR(x) ((x) >> (LOG_TARGET_AU_BITS-LOG_BITS_PER_AU))
#endif

#if LOG_BITS_PER_AU == LOG_TDATA_AU_BITS
/* translate target addressable unit to host address */
#define TDATA_TO_HOST(x) (x)
/* translate host address to target addressable unit */
#define HOST_TO_TDATA(x) (x)
/* translate host address to target addressable unit, round up */
#define HOST_TO_TDATA_ROUND(x) (x)
/* byte offset to host offset, rounded up for TDATA size */
#define BYTE_TO_HOST_TDATA_ROUND(x) BYTE_TO_HOST_ROUND(x)
#elif LOG_BITS_PER_AU > LOG_TDATA_AU_BITS
#define TDATA_TO_HOST(x) ((x) >> (LOG_BITS_PER_AU-LOG_TDATA_AU_BITS))
#define HOST_TO_TDATA(x) ((x) << (LOG_BITS_PER_AU-LOG_TDATA_AU_BITS))
#define HOST_TO_TDATA_ROUND(x) ((x) << (LOG_BITS_PER_AU-LOG_TDATA_AU_BITS))
#define BYTE_TO_HOST_TDATA_ROUND(x) BYTE_TO_HOST_ROUND(x)
#else
#define TDATA_TO_HOST(x) ((x) << (LOG_TDATA_AU_BITS-LOG_BITS_PER_AU))
#define HOST_TO_TDATA(x) ((x) >> (LOG_TDATA_AU_BITS-LOG_BITS_PER_AU))
#define HOST_TO_TDATA_ROUND(x) (((x) +\
				(1<<(LOG_TDATA_AU_BITS-LOG_BITS_PER_AU))-1) >>\
				(LOG_TDATA_AU_BITS-LOG_BITS_PER_AU))
#define BYTE_TO_HOST_TDATA_ROUND(x) (BYTE_TO_HOST((x) +\
	(1<<(LOG_TDATA_AU_BITS-LOG_BITS_PER_BYTE))-1) &\
	-(TDATA_AU_BITS/BITS_PER_AU))
#endif

/*
 * Input in DOFF format is always expresed in bytes, regardless of loading host
 * so we wind up converting from bytes to target and host units even when the
 * host is not a byte machine.
 */
#if LOG_BITS_PER_AU == LOG_BITS_PER_BYTE
#define BYTE_TO_HOST(x) (x)
#define BYTE_TO_HOST_ROUND(x) (x)
#define HOST_TO_BYTE(x) (x)
#elif LOG_BITS_PER_AU >= LOG_BITS_PER_BYTE
#define BYTE_TO_HOST(x) ((x) >> (LOG_BITS_PER_AU - LOG_BITS_PER_BYTE))
#define BYTE_TO_HOST_ROUND(x) ((x + (BITS_PER_AU/BITS_PER_BYTE-1)) >>\
			      (LOG_BITS_PER_AU - LOG_BITS_PER_BYTE))
#define HOST_TO_BYTE(x) ((x) << (LOG_BITS_PER_AU - LOG_BITS_PER_BYTE))
#else
/* lets not try to deal with sub-8-bit byte machines */
#endif

#if LOG_TARGET_AU_BITS == LOG_BITS_PER_BYTE
/* translate target addressable unit to byte address */
#define TADDR_TO_BYTE(x) (x)
/* translate byte address to target addressable unit */
#define BYTE_TO_TADDR(x) (x)
#elif LOG_TARGET_AU_BITS > LOG_BITS_PER_BYTE
#define TADDR_TO_BYTE(x) ((x) << (LOG_TARGET_AU_BITS-LOG_BITS_PER_BYTE))
#define BYTE_TO_TADDR(x) ((x) >> (LOG_TARGET_AU_BITS-LOG_BITS_PER_BYTE))
#else
/* lets not try to deal with sub-8-bit byte machines */
#endif

#ifdef _BIG_ENDIAN
#define HOST_ENDIANNESS 1
#else
#define HOST_ENDIANNESS 0
#endif

#ifdef TARGET_ENDIANNESS
#define TARGET_ENDIANNESS_DIFFERS(rtend) (HOST_ENDIANNESS^TARGET_ENDIANNESS)
#elif HOST_ENDIANNESS
#define TARGET_ENDIANNESS_DIFFERS(rtend) (!(rtend))
#else
#define TARGET_ENDIANNESS_DIFFERS(rtend) (rtend)
#endif

/* the unit in which we process target image data */
#if TARGET_AU_BITS <= 8
typedef u8 tgt_au_t;
#elif TARGET_AU_BITS <= 16
typedef u16 tgt_au_t;
#else
typedef u32 tgt_au_t;
#endif

/* size of that unit */
#if TARGET_AU_BITS < BITS_PER_AU
#define TGTAU_BITS BITS_PER_AU
#define LOG_TGTAU_BITS LOG_BITS_PER_AU
#else
#define TGTAU_BITS TARGET_AU_BITS
#define LOG_TGTAU_BITS LOG_TARGET_AU_BITS
#endif
