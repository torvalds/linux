/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001  Hiroyuki Kondo, Hirokazu Takata, and Hitoshi Yamamoto
 * Copyright (C) 2004, 2006  Hirokazu Takata <takata at linux-m32r.org>
 */
#ifndef _ASM_M32R_DCACHE_CLEAR_H
#define _ASM_M32R_DCACHE_CLEAR_H

#ifdef CONFIG_CHIP_M32700_TS1
#define DCACHE_CLEAR(reg0, reg1, addr)				\
	"seth	"reg1", #high(dcache_dummy);		\n\t"	\
	"or3	"reg1", "reg1", #low(dcache_dummy);	\n\t"	\
	"lock	"reg0", @"reg1";			\n\t"	\
	"add3	"reg0", "addr", #0x1000;		\n\t"	\
	"ld	"reg0", @"reg0";			\n\t"	\
	"add3	"reg0", "addr", #0x2000;		\n\t"	\
	"ld	"reg0", @"reg0";			\n\t"	\
	"unlock	"reg0", @"reg1";			\n\t"
	/* FIXME: This workaround code cannot handle kernel modules
	 * correctly under SMP environment.
	 */
#else	/* CONFIG_CHIP_M32700_TS1 */
#define DCACHE_CLEAR(reg0, reg1, addr)
#endif	/* CONFIG_CHIP_M32700_TS1 */

#endif /* _ASM_M32R_DCACHE_CLEAR_H */
