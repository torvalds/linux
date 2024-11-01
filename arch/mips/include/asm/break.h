/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 2003 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef __ASM_BREAK_H
#define __ASM_BREAK_H

#ifdef __UAPI_ASM_BREAK_H
#error "Error: Do not directly include <uapi/asm/break.h>"
#endif
#include <uapi/asm/break.h>

/*
 * Break codes used internally to the kernel.
 */
#define BRK_KDB		513	/* Used in KDB_ENTER() */
#define BRK_MEMU	514	/* Used by FPU emulator */
#define BRK_KPROBE_BP	515	/* Kprobe break */
#define BRK_KPROBE_SSTEPBP 516	/* Kprobe single step software implementation */
#define BRK_MULOVF	1023	/* Multiply overflow */

#endif /* __ASM_BREAK_H */
