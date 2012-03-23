/*
 * Common time prototypes and such for all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __POWERPC_TIME_H
#define __POWERPC_TIME_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/percpu.h>

#include <asm/processor.h>
#ifdef CONFIG_PPC_ISERIES
#include <asm/paca.h>
#include <asm/firmware.h>
#include <asm/iseries/hv_call.h>
#endif

/* time.c */
extern unsigned long tb_ticks_per_jiffy;
extern unsigned long tb_ticks_per_usec;
extern unsigned long tb_ticks_per_sec;

struct rtc_time;
extern void to_tm(int tim, struct rtc_time * tm);
extern void GregorianDay(struct rtc_time *tm);

extern void generic_calibrate_decr(void);

extern void set_dec_cpu6(unsigned int val);

/* Some sane defaults: 125 MHz timebase, 1GHz processor */
extern unsigned long ppc_proc_freq;
#define DEFAULT_PROC_FREQ	(DEFAULT_TB_FREQ * 8)
extern unsigned long ppc_tb_freq;
#define DEFAULT_TB_FREQ		125000000UL

struct div_result {
	u64 result_high;
	u64 result_low;
};

/* Accessor functions for the timebase (RTC on 601) registers. */
/* If one day CONFIG_POWER is added just define __USE_RTC as 1 */
#ifdef CONFIG_6xx
#define __USE_RTC()	(!cpu_has_feature(CPU_FTR_USE_TB))
#else
#define __USE_RTC()	0
#endif

#ifdef CONFIG_PPC64

/* For compatibility, get_tbl() is defined as get_tb() on ppc64 */
#define get_tbl		get_tb

#else

static inline unsigned long get_tbl(void)
{
#if defined(CONFIG_403GCX)
	unsigned long tbl;
	asm volatile("mfspr %0, 0x3dd" : "=r" (tbl));
	return tbl;
#else
	return mftbl();
#endif
}

static inline unsigned int get_tbu(void)
{
#ifdef CONFIG_403GCX
	unsigned int tbu;
	asm volatile("mfspr %0, 0x3dc" : "=r" (tbu));
	return tbu;
#else
	return mftbu();
#endif
}
#endif /* !CONFIG_PPC64 */

static inline unsigned int get_rtcl(void)
{
	unsigned int rtcl;

	asm volatile("mfrtcl %0" : "=r" (rtcl));
	return rtcl;
}

static inline u64 get_rtc(void)
{
	unsigned int hi, lo, hi2;

	do {
		asm volatile("mfrtcu %0; mfrtcl %1; mfrtcu %2"
			     : "=r" (hi), "=r" (lo), "=r" (hi2));
	} while (hi2 != hi);
	return (u64)hi * 1000000000 + lo;
}

#ifdef CONFIG_PPC64
static inline u64 get_tb(void)
{
	return mftb();
}
#else /* CONFIG_PPC64 */
static inline u64 get_tb(void)
{
	unsigned int tbhi, tblo, tbhi2;

	do {
		tbhi = get_tbu();
		tblo = get_tbl();
		tbhi2 = get_tbu();
	} while (tbhi != tbhi2);

	return ((u64)tbhi << 32) | tblo;
}
#endif /* !CONFIG_PPC64 */

static inline u64 get_tb_or_rtc(void)
{
	return __USE_RTC() ? get_rtc() : get_tb();
}

static inline void set_tb(unsigned int upper, unsigned int lower)
{
	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, upper);
	mtspr(SPRN_TBWL, lower);
}

/* Accessor functions for the decrementer register.
 * The 4xx doesn't even have a decrementer.  I tried to use the
 * generic timer interrupt code, which seems OK, with the 4xx PIT
 * in auto-reload mode.  The problem is PIT stops counting when it
 * hits zero.  If it would wrap, we could use it just like a decrementer.
 */
static inline unsigned int get_dec(void)
{
#if defined(CONFIG_40x)
	return (mfspr(SPRN_PIT));
#else
	return (mfspr(SPRN_DEC));
#endif
}

/*
 * Note: Book E and 4xx processors differ from other PowerPC processors
 * in when the decrementer generates its interrupt: on the 1 to 0
 * transition for Book E/4xx, but on the 0 to -1 transition for others.
 */
static inline void set_dec(int val)
{
#if defined(CONFIG_40x)
	mtspr(SPRN_PIT, val);
#elif defined(CONFIG_8xx_CPU6)
	set_dec_cpu6(val - 1);
#else
#ifndef CONFIG_BOOKE
	--val;
#endif
#ifdef CONFIG_PPC_ISERIES
	if (firmware_has_feature(FW_FEATURE_ISERIES) &&
			get_lppaca()->shared_proc) {
		get_lppaca()->virtual_decr = val;
		if (get_dec() > val)
			HvCall_setVirtualDecr();
		return;
	}
#endif
	mtspr(SPRN_DEC, val);
#endif /* not 40x or 8xx_CPU6 */
}

static inline unsigned long tb_ticks_since(unsigned long tstamp)
{
	if (__USE_RTC()) {
		int delta = get_rtcl() - (unsigned int) tstamp;
		return delta < 0 ? delta + 1000000000 : delta;
	}
	return get_tbl() - tstamp;
}

#define mulhwu(x,y) \
({unsigned z; asm ("mulhwu %0,%1,%2" : "=r" (z) : "r" (x), "r" (y)); z;})

#ifdef CONFIG_PPC64
#define mulhdu(x,y) \
({unsigned long z; asm ("mulhdu %0,%1,%2" : "=r" (z) : "r" (x), "r" (y)); z;})
#else
extern u64 mulhdu(u64, u64);
#endif

extern void div128_by_32(u64 dividend_high, u64 dividend_low,
			 unsigned divisor, struct div_result *dr);

/* Used to store Processor Utilization register (purr) values */

struct cpu_usage {
        u64 current_tb;  /* Holds the current purr register values */
};

DECLARE_PER_CPU(struct cpu_usage, cpu_usage_array);

#if defined(CONFIG_VIRT_CPU_ACCOUNTING)
#define account_process_vtime(tsk)		account_process_tick(tsk, 0)
#else
#define account_process_vtime(tsk)		do { } while (0)
#endif

extern void secondary_cpu_time_init(void);
extern void iSeries_time_init_early(void);

DECLARE_PER_CPU(u64, decrementers_next_tb);

#endif /* __KERNEL__ */
#endif /* __POWERPC_TIME_H */
