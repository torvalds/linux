/*
 * include/asm-sh/cpu-sh4/watchdog.h
 *
 * Copyright (C) 2002, 2003 Paul Mundt
 * Copyright (C) 2009 Siemens AG
 * Copyright (C) 2009 Sitdikov Valentin
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH4_WATCHDOG_H
#define __ASM_CPU_SH4_WATCHDOG_H

#if defined(CONFIG_CPU_SUBTYPE_SH7785) || defined(CONFIG_CPU_SUBTYPE_SH7780)
/* Prefix definition */
#define WTBST_HIGH	0x55
/* Register definitions */
#define WTCNT_R		0xffcc0010 /*WDTCNT*/
#define WTCSR		0xffcc0004 /*WDTCSR*/
#define WTCNT		0xffcc0000 /*WDTST*/
#define WTST		WTCNT
#define WTBST		0xffcc0008 /*WDTBST*/
#else
/* Register definitions */
#define WTCNT		0xffc00008
#define WTCSR		0xffc0000c
#endif

/* Bit definitions */
#define WTCSR_TME	0x80
#define WTCSR_WT	0x40
#define WTCSR_RSTS	0x20
#define WTCSR_WOVF	0x10
#define WTCSR_IOVF	0x08

#endif /* __ASM_CPU_SH4_WATCHDOG_H */

