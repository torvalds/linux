/*
 * Support for periodic interrupts (100 per second) and for getting
 * the current time from the RTC on Power Macintoshes.
 *
 * We use the decrementer register for our periodic interrupts.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996 Paul Mackerras.
 * Copyright (C) 2003-2005 Benjamin Herrenschmidt.
 *
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/interrupt.h>

#include <asm/sections.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/nvram.h>
#include <asm/smu.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

/* Apparently the RTC stores seconds since 1 Jan 1904 */
#define RTC_OFFSET	2082844800

/*
 * Calibrate the decrementer frequency with the VIA timer 1.
 */
#define VIA_TIMER_FREQ_6	4700000	/* time 1 frequency * 6 */

extern struct timezone sys_tz;
extern void to_tm(int tim, struct rtc_time * tm);

void __pmac pmac_get_rtc_time(struct rtc_time *tm)
{
	switch(sys_ctrler) {
#ifdef CONFIG_ADB_PMU
	case SYS_CTRLER_PMU: {
		/* TODO: Move that to a function in the PMU driver */
		struct adb_request req;
		unsigned int now;

		if (pmu_request(&req, NULL, 1, PMU_READ_RTC) < 0)
			return;
		pmu_wait_complete(&req);
		if (req.reply_len != 4)
			printk(KERN_ERR "pmac_get_rtc_time: PMU returned a %d"
			       " bytes reply\n", req.reply_len);
		now = (req.reply[0] << 24) + (req.reply[1] << 16)
			+ (req.reply[2] << 8) + req.reply[3];
		DBG("get: %u -> %u\n", (int)now, (int)(now - RTC_OFFSET));
		now -= RTC_OFFSET;

		to_tm(now, tm);
		tm->tm_year -= 1900;
		tm->tm_mon -= 1;
	
		DBG("-> tm_mday: %d, tm_mon: %d, tm_year: %d, %d:%02d:%02d\n",
		    tm->tm_mday, tm->tm_mon, tm->tm_year,
		    tm->tm_hour, tm->tm_min, tm->tm_sec);
		break;
	}
#endif /* CONFIG_ADB_PMU */

#ifdef CONFIG_PMAC_SMU
	case SYS_CTRLER_SMU:
		smu_get_rtc_time(tm, 1);
		break;
#endif /* CONFIG_PMAC_SMU */
	default:
		;
	}
}

int __pmac pmac_set_rtc_time(struct rtc_time *tm)
{
	switch(sys_ctrler) {
#ifdef CONFIG_ADB_PMU
	case SYS_CTRLER_PMU: {
		/* TODO: Move that to a function in the PMU driver */
		struct adb_request req;
		unsigned int nowtime;

		DBG("set: tm_mday: %d, tm_mon: %d, tm_year: %d,"
		    " %d:%02d:%02d\n",
		    tm->tm_mday, tm->tm_mon, tm->tm_year,
		    tm->tm_hour, tm->tm_min, tm->tm_sec);

		nowtime = mktime(tm->tm_year + 1900, tm->tm_mon + 1,
				 tm->tm_mday, tm->tm_hour, tm->tm_min,
				 tm->tm_sec);

		DBG("-> %u -> %u\n", (int)nowtime,
		    (int)(nowtime + RTC_OFFSET));
		nowtime += RTC_OFFSET;

		if (pmu_request(&req, NULL, 5, PMU_SET_RTC,
				nowtime >> 24, nowtime >> 16,
				nowtime >> 8, nowtime) < 0)
			return -ENXIO;
		pmu_wait_complete(&req);
		if (req.reply_len != 0)
			printk(KERN_ERR "pmac_set_rtc_time: PMU returned a %d"
			       " bytes reply\n", req.reply_len);
		return 0;
	}
#endif /* CONFIG_ADB_PMU */

#ifdef CONFIG_PMAC_SMU
	case SYS_CTRLER_SMU:
		return smu_set_rtc_time(tm, 1);
#endif /* CONFIG_PMAC_SMU */
	default:
		return -ENODEV;
	}
}

void __init pmac_get_boot_time(struct rtc_time *tm)
{
	pmac_get_rtc_time(tm);

#ifdef disabled__CONFIG_NVRAM
	s32 delta = 0;
	int dst;
	
	delta = ((s32)pmac_xpram_read(PMAC_XPRAM_MACHINE_LOC + 0x9)) << 16;
	delta |= ((s32)pmac_xpram_read(PMAC_XPRAM_MACHINE_LOC + 0xa)) << 8;
	delta |= pmac_xpram_read(PMAC_XPRAM_MACHINE_LOC + 0xb);
	if (delta & 0x00800000UL)
		delta |= 0xFF000000UL;
	dst = ((pmac_xpram_read(PMAC_XPRAM_MACHINE_LOC + 0x8) & 0x80) != 0);
	printk("GMT Delta read from XPRAM: %d minutes, DST: %s\n", delta/60,
		dst ? "on" : "off");
#endif
}

/*
 * Query the OF and get the decr frequency.
 * FIXME: merge this with generic_calibrate_decr
 */
void __init pmac_calibrate_decr(void)
{
	struct device_node *cpu;
	unsigned int freq, *fp;
	struct div_result divres;

	/*
	 * The cpu node should have a timebase-frequency property
	 * to tell us the rate at which the decrementer counts.
	 */
	cpu = find_type_devices("cpu");
	if (cpu == 0)
		panic("can't find cpu node in time_init");
	fp = (unsigned int *) get_property(cpu, "timebase-frequency", NULL);
	if (fp == 0)
		panic("can't get cpu timebase frequency");
	freq = *fp;
	printk("time_init: decrementer frequency = %u.%.6u MHz\n",
	       freq/1000000, freq%1000000);
	tb_ticks_per_jiffy = freq / HZ;
	tb_ticks_per_sec = tb_ticks_per_jiffy * HZ;
	tb_ticks_per_usec = freq / 1000000;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);
	div128_by_32( 1024*1024, 0, tb_ticks_per_sec, &divres );
	tb_to_xs = divres.result_low;
	ppc_tb_freq = freq;

	fp = (unsigned int *)get_property(cpu, "clock-frequency", NULL);
	if (fp == 0)
		panic("can't get cpu processor frequency");
	ppc_proc_freq = *fp;

	setup_default_decr();
}

