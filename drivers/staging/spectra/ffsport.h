/*
 * NAND Flash Controller Device Driver
 * Copyright (c) 2009, Intel Corporation and its suppliers.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef _FFSPORT_
#define _FFSPORT_

#include "ffsdefs.h"

#if defined __GNUC__
#define PACKED
#define PACKED_GNU __attribute__ ((packed))
#define UNALIGNED
#endif

#include <linux/semaphore.h>
#include <linux/string.h>	/* for strcpy(), stricmp(), etc */
#include <linux/mm.h>		/* for kmalloc(), kfree() */
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>	/* printk() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include "flash.h"

#define VERBOSE    1

#define NAND_DBG_WARN  1
#define NAND_DBG_DEBUG 2
#define NAND_DBG_TRACE 3

extern int nand_debug_level;

#ifdef VERBOSE
#define nand_dbg_print(level, args...)			\
	do {						\
		if (level <= nand_debug_level)		\
			printk(KERN_ALERT args);	\
	} while (0)
#else
#define nand_dbg_print(level, args...)
#endif

#ifdef SUPPORT_BIG_ENDIAN
#define INVERTUINT16(w)   ((u16)(((u16)(w)) << 8) | \
			   (u16)((u16)(w) >> 8))

#define INVERTUINT32(dw)  (((u32)(dw) << 24) | \
			   (((u32)(dw) << 8) & 0x00ff0000) | \
			   (((u32)(dw) >> 8) & 0x0000ff00) | \
			   ((u32)(dw) >> 24))
#else
#define INVERTUINT16(w)   w
#define INVERTUINT32(dw)  dw
#endif

extern int GLOB_Calc_Used_Bits(u32 n);
extern u64 GLOB_u64_Div(u64 addr, u32 divisor);
extern u64 GLOB_u64_Remainder(u64 addr, u32 divisor_type);
extern int register_spectra_ftl(void);

#endif /* _FFSPORT_ */
