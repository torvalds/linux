/*
 * Generic Power Management Routine
 *
 * Copyright (c) 2006 Andriy Skulysh <askulsyh@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <asm/freq.h>
#include <asm/io.h>
#include <asm/watchdog.h>
#include <asm/pm.h>

#define INTR_OFFSET	0x600

#define STBCR		0xffffff82
#define STBCR2		0xffffff88

#define STBCR_STBY	0x80
#define STBCR_MSTP2	0x04

#define MCR		0xffffff68
#define RTCNT		0xffffff70

#define MCR_RMODE	2
#define MCR_RFSH	4

void pm_enter(void)
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
	frqcr = ctrl_inw(FRQCR);
	frqcr &= ~(FRQCR_PLLEN | FRQCR_PSTBY);
	ctrl_outw(frqcr, FRQCR);

	/* enable standby */
	stbcr = ctrl_inb(STBCR);
	ctrl_outb(stbcr | STBCR_STBY | STBCR_MSTP2, STBCR);

	/* set self-refresh */
	mcr = ctrl_inw(MCR);
	ctrl_outw(mcr & ~MCR_RFSH, MCR);

	/* set interrupt handler */
	asm volatile("stc vbr, %0" : "=r" (vbr_old));
	vbr_new = get_zeroed_page(GFP_ATOMIC);
	udelay(50);
	memcpy((void*)(vbr_new + INTR_OFFSET),
	       &wakeup_start, &wakeup_end - &wakeup_start);
	asm volatile("ldc %0, vbr" : : "r" (vbr_new));

	ctrl_outw(0, RTCNT);
	ctrl_outw(mcr | MCR_RFSH | MCR_RMODE, MCR);

	cpu_sleep();

	asm volatile("ldc %0, vbr" : : "r" (vbr_old));

	free_page(vbr_new);

	/* enable PLL1 */
	frqcr = ctrl_inw(FRQCR);
	frqcr |= FRQCR_PSTBY;
	ctrl_outw(frqcr, FRQCR);
	udelay(50);
	frqcr |= FRQCR_PLLEN;
	ctrl_outw(frqcr, FRQCR);

	ctrl_outb(stbcr, STBCR);

	clear_bl_bit();
}
