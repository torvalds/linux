// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  arch/m68k/q40/config.c
 *
 *  Copyright (C) 1999 Richard Zidlicky
 *
 * originally based on:
 *
 *  linux/bvme/config.c
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/serial_reg.h>
#include <linux/rtc.h>
#include <linux/vt_kern.h>
#include <linux/bcd.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/bootinfo.h>
#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/q40_master.h>
#include <asm/config.h>

#include "q40.h"

static void q40_get_model(char *model);

static int q40_hwclk(int, struct rtc_time *);
static int q40_get_rtc_pll(struct rtc_pll_info *pll);
static int q40_set_rtc_pll(struct rtc_pll_info *pll);

static void q40_mem_console_write(struct console *co, const char *b,
				  unsigned int count);

extern int ql_ticks;

static struct console q40_console_driver = {
	.name	= "debug",
	.write	= q40_mem_console_write,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};


/* early debugging function:*/
extern char *q40_mem_cptr; /*=(char *)0xff020000;*/
static int _cpleft;

static void q40_mem_console_write(struct console *co, const char *s,
				  unsigned int count)
{
	const char *p = s;

	if (count < _cpleft) {
		while (count-- > 0) {
			*q40_mem_cptr = *p++;
			q40_mem_cptr += 4;
			_cpleft--;
		}
	}
}

static int __init q40_debug_setup(char *arg)
{
	/* useful for early debugging stages - writes kernel messages into SRAM */
	if (MACH_IS_Q40 && !strncmp(arg, "mem", 3)) {
		/*pr_info("using NVRAM debug, q40_mem_cptr=%p\n",q40_mem_cptr);*/
		_cpleft = 2000 - ((long)q40_mem_cptr-0xff020000) / 4;
		register_console(&q40_console_driver);
	}
	return 0;
}

early_param("debug", q40_debug_setup);

#if 0
void printq40(char *str)
{
	int l = strlen(str);
	char *p = q40_mem_cptr;

	while (l-- > 0 && _cpleft-- > 0) {
		*p = *str++;
		p += 4;
	}
	q40_mem_cptr = p;
}
#endif

static int halted;

#ifdef CONFIG_HEARTBEAT
static void q40_heartbeat(int on)
{
	if (halted)
		return;

	if (on)
		Q40_LED_ON();
	else
		Q40_LED_OFF();
}
#endif

static void q40_reset(void)
{
	halted = 1;
	pr_info("*******************************************\n"
		"Called q40_reset : press the RESET button!!\n"
		"*******************************************\n");
	Q40_LED_ON();
	while (1)
		;
}

static void q40_halt(void)
{
	halted = 1;
	pr_info("*******************\n"
		"  Called q40_halt\n"
		"*******************\n");
	Q40_LED_ON();
	while (1)
		;
}

static void q40_get_model(char *model)
{
	sprintf(model, "Q40");
}

static unsigned int serports[] =
{
	0x3f8,0x2f8,0x3e8,0x2e8,0
};

static void __init q40_disable_irqs(void)
{
	unsigned i, j;

	j = 0;
	while ((i = serports[j++]))
		outb(0, i + UART_IER);
	master_outb(0, EXT_ENABLE_REG);
	master_outb(0, KEY_IRQ_ENABLE_REG);
}

void __init config_q40(void)
{
	mach_sched_init = q40_sched_init;

	mach_init_IRQ = q40_init_IRQ;
	mach_hwclk = q40_hwclk;
	mach_get_rtc_pll = q40_get_rtc_pll;
	mach_set_rtc_pll = q40_set_rtc_pll;

	mach_reset = q40_reset;
	mach_get_model = q40_get_model;

#if IS_ENABLED(CONFIG_INPUT_M68K_BEEP)
	mach_beep = q40_mksound;
#endif
#ifdef CONFIG_HEARTBEAT
	mach_heartbeat = q40_heartbeat;
#endif
	mach_halt = q40_halt;

	/* disable a few things that SMSQ might have left enabled */
	q40_disable_irqs();
}


int __init q40_parse_bootinfo(const struct bi_record *rec)
{
	return 1;
}

/*
 * Looks like op is non-zero for setting the clock, and zero for
 * reading the clock.
 *
 *  struct hwclk_time {
 *         unsigned        sec;       0..59
 *         unsigned        min;       0..59
 *         unsigned        hour;      0..23
 *         unsigned        day;       1..31
 *         unsigned        mon;       0..11
 *         unsigned        year;      00...
 *         int             wday;      0..6, 0 is Sunday, -1 means unknown/don't set
 * };
 */

static int q40_hwclk(int op, struct rtc_time *t)
{
	if (op) {
		/* Write.... */
		Q40_RTC_CTRL |= Q40_RTC_WRITE;

		Q40_RTC_SECS = bin2bcd(t->tm_sec);
		Q40_RTC_MINS = bin2bcd(t->tm_min);
		Q40_RTC_HOUR = bin2bcd(t->tm_hour);
		Q40_RTC_DATE = bin2bcd(t->tm_mday);
		Q40_RTC_MNTH = bin2bcd(t->tm_mon + 1);
		Q40_RTC_YEAR = bin2bcd(t->tm_year%100);
		if (t->tm_wday >= 0)
			Q40_RTC_DOW = bin2bcd(t->tm_wday+1);

		Q40_RTC_CTRL &= ~(Q40_RTC_WRITE);
	} else {
		/* Read....  */
		Q40_RTC_CTRL |= Q40_RTC_READ;

		t->tm_year = bcd2bin (Q40_RTC_YEAR);
		t->tm_mon  = bcd2bin (Q40_RTC_MNTH)-1;
		t->tm_mday = bcd2bin (Q40_RTC_DATE);
		t->tm_hour = bcd2bin (Q40_RTC_HOUR);
		t->tm_min  = bcd2bin (Q40_RTC_MINS);
		t->tm_sec  = bcd2bin (Q40_RTC_SECS);

		Q40_RTC_CTRL &= ~(Q40_RTC_READ);

		if (t->tm_year < 70)
			t->tm_year += 100;
		t->tm_wday = bcd2bin(Q40_RTC_DOW)-1;
	}

	return 0;
}

/* get and set PLL calibration of RTC clock */
#define Q40_RTC_PLL_MASK ((1<<5)-1)
#define Q40_RTC_PLL_SIGN (1<<5)

static int q40_get_rtc_pll(struct rtc_pll_info *pll)
{
	int tmp = Q40_RTC_CTRL;

	pll->pll_ctrl = 0;
	pll->pll_value = tmp & Q40_RTC_PLL_MASK;
	if (tmp & Q40_RTC_PLL_SIGN)
		pll->pll_value = -pll->pll_value;
	pll->pll_max = 31;
	pll->pll_min = -31;
	pll->pll_posmult = 512;
	pll->pll_negmult = 256;
	pll->pll_clock = 125829120;

	return 0;
}

static int q40_set_rtc_pll(struct rtc_pll_info *pll)
{
	if (!pll->pll_ctrl) {
		/* the docs are a bit unclear so I am doublesetting */
		/* RTC_WRITE here ... */
		int tmp = (pll->pll_value & 31) | (pll->pll_value<0 ? 32 : 0) |
			  Q40_RTC_WRITE;
		Q40_RTC_CTRL |= Q40_RTC_WRITE;
		Q40_RTC_CTRL = tmp;
		Q40_RTC_CTRL &= ~(Q40_RTC_WRITE);
		return 0;
	} else
		return -EINVAL;
}

#define PCIDE_BASE1	0x1f0
#define PCIDE_BASE2	0x170
#define PCIDE_CTL	0x206

static const struct resource q40_pata_rsrc_0[] __initconst = {
	DEFINE_RES_MEM(q40_isa_io_base + PCIDE_BASE1 * 4, 0x38),
	DEFINE_RES_MEM(q40_isa_io_base + (PCIDE_BASE1 + PCIDE_CTL) * 4, 2),
	DEFINE_RES_IO(PCIDE_BASE1, 8),
	DEFINE_RES_IO(PCIDE_BASE1 + PCIDE_CTL, 1),
	DEFINE_RES_IRQ(14),
};

static const struct resource q40_pata_rsrc_1[] __initconst = {
	DEFINE_RES_MEM(q40_isa_io_base + PCIDE_BASE2 * 4, 0x38),
	DEFINE_RES_MEM(q40_isa_io_base + (PCIDE_BASE2 + PCIDE_CTL) * 4, 2),
	DEFINE_RES_IO(PCIDE_BASE2, 8),
	DEFINE_RES_IO(PCIDE_BASE2 + PCIDE_CTL, 1),
	DEFINE_RES_IRQ(15),
};

static __init int q40_platform_init(void)
{
	if (!MACH_IS_Q40)
		return -ENODEV;

	platform_device_register_simple("q40kbd", -1, NULL, 0);

	platform_device_register_simple("atari-falcon-ide", 0, q40_pata_rsrc_0,
					ARRAY_SIZE(q40_pata_rsrc_0));

	platform_device_register_simple("atari-falcon-ide", 1, q40_pata_rsrc_1,
					ARRAY_SIZE(q40_pata_rsrc_1));

	return 0;
}
arch_initcall(q40_platform_init);
