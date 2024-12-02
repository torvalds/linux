/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 2002, Greg Ungerer (gerg@snapgear.com)
 *
 *  Based on older entry.S files, the following copyrights apply:
 *
 *  Copyright (C) 1998  D. Jeff Dionne <jeff@lineo.ca>,
 *                      Kenneth Albanowski <kjahds@kjahds.com>,
 *  Copyright (C) 2000  Lineo Inc. (www.lineo.com) 
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Linux/m68k support by Hamish Macdonald
 */

#include <linux/linkage.h>

#ifndef CONFIG_MMU
#define sys_mmap2	sys_mmap_pgoff
#endif

#define __SYSCALL(nr, entry) .long entry
	.section .rodata
ALIGN
ENTRY(sys_call_table)
#include <asm/syscall_table.h>
