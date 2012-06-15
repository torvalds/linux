/*
 * hp6x0 Power Management Routines
 *
 * Copyright (c) 2006 Andriy Skulysh <askulsyh@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */
#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <asm/io.h>
#include <asm/hd64461.h>
#include <asm/bl_bit.h>
#include <mach/hp6xx.h>
#include <cpu/dac.h>
#include <asm/freq.h>
#include <asm/watchdog.h>

#define INTR_OFFSET	0x600

#define STBCR		0xffffff82
#define STBCR2		0xffffff88

#define STBCR_STBY	0x80
#define STBCR_MSTP2	0x04

#define MCR		0xffffff68
#define RTCNT		0xffffff70

#define MCR_RMODE	2
#define MCR_RFSH	4

extern u8 wakeup_start;
extern u8 wakeup_end;

static void pm_enter(void)
{
	u8 stbcr, csr;
	u16 frqcr, mcr;
	u32 vbr_new, vbr_old;

	set_bl_bit();

	/* set wdt */
	csr = sh_wdt_read_csr();
	csr &= ~WTCSR_TME;
	csr |= WTCSR_CKS_4096;
	sh_wdt_write_csr(csr);
	csr = sh_wdt_read_csr();
	sh_wdt_write_cnt(0);

	/* disable PLL1 */
	frqcr = __raw_readw(FRQCR);
	frqcr &= ~(FRQCR_PLLEN | FRQCR_PSTBY);
	__raw_writew(frqcr, FRQCR);

	/* enable standby */
	stbcr = __raw_readb(STBCR);
	__raw_writeb(stbcr | STBCR_STBY | STBCR_MSTP2, STBCR);

	/* set self-refresh */
	mcr = __raw_readw(MCR);
	__raw_writew(mcr & ~MCR_RFSH, MCR);

	/* set interrupt handler */
	asm volatile("stc vbr, %0" : "=r" (vbr_old));
	vbr_new = get_zeroed_page(GFP_ATOMIC);
	udelay(50);
	memcpy((void*)(vbr_new + INTR_OFFSET),
	       &wakeup_start, &wakeup_end - &wakeup_start);
	asm volatile("ldc %0, vbr" : : "r" (vbr_new));

	__raw_writew(0, RTCNT);
	__raw_writew(mcr | MCR_RFSH | MCR_RMODE, MCR);

	cpu_sleep();

	asm volatile("ldc %0, vbr" : : "r" (vbr_old));

	free_page(vbr_new);

	/* enable PLL1 */
	frqcr = __raw_readw(FRQCR);
	frqcr |= FRQCR_PSTBY;
	__raw_writew(frqcr, FRQCR);
	udelay(50);
	frqcr |= FRQCR_PLLEN;
	__raw_writew(frqcr, FRQCR);

	__raw_writeb(stbcr, STBCR);

	clear_bl_bit();
}

static int hp6x0_pm_enter(suspend_state_t state)
{
	u8 stbcr, stbcr2;
#ifdef CONFIG_HD64461_ENABLER
	u8 scr;
	u16 hd64461_stbcr;
#endif

#ifdef CONFIG_HD64461_ENABLER
	outb(0, HD64461_PCC1CSCIER);

	scr = inb(HD64461_PCC1SCR);
	scr |= HD64461_PCCSCR_VCC1;
	outb(scr, HD64461_PCC1SCR);

	hd64461_stbcr = inw(HD64461_STBCR);
	hd64461_stbcr |= HD64461_STBCR_SPC1ST;
	outw(hd64461_stbcr, HD64461_STBCR);
#endif

	__raw_writeb(0x1f, DACR);

	stbcr = __raw_readb(STBCR);
	__raw_writeb(0x01, STBCR);

	stbcr2 = __raw_readb(STBCR2);
	__raw_writeb(0x7f , STBCR2);

	outw(0xf07f, HD64461_SCPUCR);

	pm_enter();

	outw(0, HD64461_SCPUCR);
	__raw_writeb(stbcr, STBCR);
	__raw_writeb(stbcr2, STBCR2);

#ifdef CONFIG_HD64461_ENABLER
	hd64461_stbcr = inw(HD64461_STBCR);
	hd64461_stbcr &= ~HD64461_STBCR_SPC1ST;
	outw(hd64461_stbcr, HD64461_STBCR);

	outb(0x4c, HD64461_PCC1CSCIER);
	outb(0x00, HD64461_PCC1CSCR);
#endif

	return 0;
}

static const struct platform_suspend_ops hp6x0_pm_ops = {
	.enter		= hp6x0_pm_enter,
	.valid		= suspend_valid_only_mem,
};

static int __init hp6x0_pm_init(void)
{
	suspend_set_ops(&hp6x0_pm_ops);
	return 0;
}

late_initcall(hp6x0_pm_init);
