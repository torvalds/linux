/* SPDX-License-Identifier: GPL-2.0+
 *
 * include/asm-sh/watchdog.h
 *
 * Copyright (C) 2002, 2003 Paul Mundt
 * Copyright (C) 2009 Siemens AG
 * Copyright (C) 2009 Valentin Sitdikov
 */
#ifndef __ASM_SH_WATCHDOG_H
#define __ASM_SH_WATCHDOG_H

#include <linux/types.h>
#include <linux/io.h>

#define WTCNT_HIGH	0x5a
#define WTCSR_HIGH	0xa5

#define WTCSR_CKS2	0x04
#define WTCSR_CKS1	0x02
#define WTCSR_CKS0	0x01

#include <cpu/watchdog.h>

/*
 * See cpu-sh2/watchdog.h for explanation of this stupidity..
 */
#ifndef WTCNT_R
#  define WTCNT_R	WTCNT
#endif

#ifndef WTCSR_R
#  define WTCSR_R	WTCSR
#endif

/*
 * CKS0-2 supports a number of clock division ratios. At the time the watchdog
 * is enabled, it defaults to a 41 usec overflow period .. we overload this to
 * something a little more reasonable, and really can't deal with anything
 * lower than WTCSR_CKS_1024, else we drop back into the usec range.
 *
 * Clock Division Ratio         Overflow Period
 * --------------------------------------------
 *     1/32 (initial value)       41 usecs
 *     1/64                       82 usecs
 *     1/128                     164 usecs
 *     1/256                     328 usecs
 *     1/512                     656 usecs
 *     1/1024                   1.31 msecs
 *     1/2048                   2.62 msecs
 *     1/4096                   5.25 msecs
 */
#define WTCSR_CKS_32	0x00
#define WTCSR_CKS_64	0x01
#define WTCSR_CKS_128	0x02
#define WTCSR_CKS_256	0x03
#define WTCSR_CKS_512	0x04
#define WTCSR_CKS_1024	0x05
#define WTCSR_CKS_2048	0x06
#define WTCSR_CKS_4096	0x07

#if defined(CONFIG_CPU_SUBTYPE_SH7785) || defined(CONFIG_CPU_SUBTYPE_SH7780)
/**
 * 	sh_wdt_read_cnt - Read from Counter
 * 	Reads back the WTCNT value.
 */
static inline __u32 sh_wdt_read_cnt(void)
{
	return __raw_readl(WTCNT_R);
}

/**
 *	sh_wdt_write_cnt - Write to Counter
 *	@val: Value to write
 *
 *	Writes the given value @val to the lower byte of the timer counter.
 *	The upper byte is set manually on each write.
 */
static inline void sh_wdt_write_cnt(__u32 val)
{
	__raw_writel((WTCNT_HIGH << 24) | (__u32)val, WTCNT);
}

/**
 *	sh_wdt_write_bst - Write to Counter
 *	@val: Value to write
 *
 *	Writes the given value @val to the lower byte of the timer counter.
 *	The upper byte is set manually on each write.
 */
static inline void sh_wdt_write_bst(__u32 val)
{
	__raw_writel((WTBST_HIGH << 24) | (__u32)val, WTBST);
}
/**
 * 	sh_wdt_read_csr - Read from Control/Status Register
 *
 *	Reads back the WTCSR value.
 */
static inline __u32 sh_wdt_read_csr(void)
{
	return __raw_readl(WTCSR_R);
}

/**
 * 	sh_wdt_write_csr - Write to Control/Status Register
 * 	@val: Value to write
 *
 * 	Writes the given value @val to the lower byte of the control/status
 * 	register. The upper byte is set manually on each write.
 */
static inline void sh_wdt_write_csr(__u32 val)
{
	__raw_writel((WTCSR_HIGH << 24) | (__u32)val, WTCSR);
}
#else
/**
 * 	sh_wdt_read_cnt - Read from Counter
 * 	Reads back the WTCNT value.
 */
static inline __u8 sh_wdt_read_cnt(void)
{
	return __raw_readb(WTCNT_R);
}

/**
 *	sh_wdt_write_cnt - Write to Counter
 *	@val: Value to write
 *
 *	Writes the given value @val to the lower byte of the timer counter.
 *	The upper byte is set manually on each write.
 */
static inline void sh_wdt_write_cnt(__u8 val)
{
	__raw_writew((WTCNT_HIGH << 8) | (__u16)val, WTCNT);
}

/**
 * 	sh_wdt_read_csr - Read from Control/Status Register
 *
 *	Reads back the WTCSR value.
 */
static inline __u8 sh_wdt_read_csr(void)
{
	return __raw_readb(WTCSR_R);
}

/**
 * 	sh_wdt_write_csr - Write to Control/Status Register
 * 	@val: Value to write
 *
 * 	Writes the given value @val to the lower byte of the control/status
 * 	register. The upper byte is set manually on each write.
 */
static inline void sh_wdt_write_csr(__u8 val)
{
	__raw_writew((WTCSR_HIGH << 8) | (__u16)val, WTCSR);
}
#endif /* CONFIG_CPU_SUBTYPE_SH7785 || CONFIG_CPU_SUBTYPE_SH7780 */
#endif /* __ASM_SH_WATCHDOG_H */
