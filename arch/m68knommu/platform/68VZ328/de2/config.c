/*
 *  linux/arch/m68knommu/platform/MC68VZ328/de2/config.c
 *
 *  Copyright (C) 1993 Hamish Macdonald
 *  Copyright (C) 1999 D. Jeff Dionne
 *  Copyright (C) 2001 Georges Menie, Ken Desmet
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/kd.h>
#include <linux/netdevice.h>

#include <asm/setup.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/MC68VZ328.h>

#ifdef CONFIG_INIT_LCD
#include "screen.h"
#endif

/* with a 33.16 MHz clock, this will give usec resolution to the time functions */
#define CLOCK_SOURCE TCTL_CLKSOURCE_SYSCLK
#define CLOCK_PRE 7
#define TICKS_PER_JIFFY 41450

static void
dragen2_sched_init(irqreturn_t (*timer_routine) (int, void *, struct pt_regs *))
{
	/* disable timer 1 */
	TCTL = 0;

	/* set ISR */
	if (request_irq(TMR_IRQ_NUM, timer_routine, IRQ_FLG_LOCK, "timer", NULL)) 
		panic("Unable to attach timer interrupt\n");

	/* Restart mode, Enable int, Set clock source */
	TCTL = TCTL_OM | TCTL_IRQEN | CLOCK_SOURCE;
	TPRER = CLOCK_PRE;
	TCMP = TICKS_PER_JIFFY;

	/* Enable timer 1 */
	TCTL |= TCTL_TEN;
}

static void dragen2_tick(void)
{
	/* Reset Timer1 */
	TSTAT &= 0;
}

static unsigned long dragen2_gettimeoffset(void)
{
	unsigned long ticks = TCN, offset = 0;

	/* check for pending interrupt */
	if (ticks < (TICKS_PER_JIFFY >> 1) && (ISR & (1 << TMR_IRQ_NUM)))
		offset = 1000000 / HZ;

	ticks = (ticks * 1000000 / HZ) / TICKS_PER_JIFFY;

	return ticks + offset;
}

static void dragen2_gettod(int *year, int *mon, int *day, int *hour,
						   int *min, int *sec)
{
	long now = RTCTIME;

	*year = *mon = *day = 1;
	*hour = (now >> 24) % 24;
	*min = (now >> 16) % 60;
	*sec = now % 60;
}

static void dragen2_reset(void)
{
	local_irq_disable();

#ifdef CONFIG_INIT_LCD
	PBDATA |= 0x20;				/* disable CCFL light */
	PKDATA |= 0x4;				/* disable LCD controller */
	LCKCON = 0;
#endif

	__asm__ __volatile__(
		"reset\n\t"
		"moveal #0x04000000, %a0\n\t"
		"moveal 0(%a0), %sp\n\t"
		"moveal 4(%a0), %a0\n\t"
		"jmp (%a0)"
	);
}

static void init_hardware(void)
{
#ifdef CONFIG_DIRECT_IO_ACCESS
	SCR = 0x10;					/* allow user access to internal registers */
#endif

	/* CSGB Init */
	CSGBB = 0x4000;
	CSB = 0x1a1;

	/* CS8900 init */
	/* PK3: hardware sleep function pin, active low */
	PKSEL |= PK(3);				/* select pin as I/O */
	PKDIR |= PK(3);				/* select pin as output */
	PKDATA |= PK(3);			/* set pin high */

	/* PF5: hardware reset function pin, active high */
	PFSEL |= PF(5);				/* select pin as I/O */
	PFDIR |= PF(5);				/* select pin as output */
	PFDATA &= ~PF(5);			/* set pin low */

	/* cs8900 hardware reset */
	PFDATA |= PF(5);
	{ int i; for (i = 0; i < 32000; ++i); }
	PFDATA &= ~PF(5);

	/* INT1 enable (cs8900 IRQ) */
	PDPOL &= ~PD(1);			/* active high signal */
	PDIQEG &= ~PD(1);
	PDIRQEN |= PD(1);			/* IRQ enabled */

#ifdef CONFIG_68328_SERIAL_UART2
	/* Enable RXD TXD port bits to enable UART2 */
	PJSEL &= ~(PJ(5) | PJ(4));
#endif

#ifdef CONFIG_INIT_LCD
	/* initialize LCD controller */
	LSSA = (long) screen_bits;
	LVPW = 0x14;
	LXMAX = 0x140;
	LYMAX = 0xef;
	LRRA = 0;
	LPXCD = 3;
	LPICF = 0x08;
	LPOLCF = 0;
	LCKCON = 0x80;
	PCPDEN = 0xff;
	PCSEL = 0;

	/* Enable LCD controller */
	PKDIR |= 0x4;
	PKSEL |= 0x4;
	PKDATA &= ~0x4;

	/* Enable CCFL backlighting circuit */
	PBDIR |= 0x20;
	PBSEL |= 0x20;
	PBDATA &= ~0x20;

	/* contrast control register */
	PFDIR |= 0x1;
	PFSEL &= ~0x1;
	PWMR = 0x037F;
#endif
}

void config_BSP(char *command, int size)
{
	printk(KERN_INFO "68VZ328 DragonBallVZ support (c) 2001 Lineo, Inc.\n");

#if defined(CONFIG_BOOTPARAM)
	strncpy(command, CONFIG_BOOTPARAM_STRING, size);
	command[size-1] = 0;
#else
	memset(command, 0, size);
#endif

	init_hardware();

	mach_sched_init = (void *)dragen2_sched_init;
	mach_tick = dragen2_tick;
	mach_gettimeoffset = dragen2_gettimeoffset;
	mach_reset = dragen2_reset;
	mach_gettod = dragen2_gettod;
}
