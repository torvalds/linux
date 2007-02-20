/*
 * arch/v850/kernel/rte_me2_cb.c -- Midas labs RTE-V850E/ME2-CB board
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/irq.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include <asm/atomic.h>
#include <asm/page.h>
#include <asm/me2.h>
#include <asm/rte_me2_cb.h>
#include <asm/machdep.h>
#include <asm/v850e_intc.h>
#include <asm/v850e_cache.h>
#include <asm/irq.h>

#include "mach.h"

extern unsigned long *_intv_start;
extern unsigned long *_intv_end;

/* LED access routines.  */
extern unsigned read_leds (int pos, char *buf, int len);
extern unsigned write_leds (int pos, const char *buf, int len);


/* SDRAM are almost contiguous (with a small hole in between;
   see mach_reserve_bootmem for details), so just use both as one big area.  */
#define RAM_START 	SDRAM_ADDR
#define RAM_END		(SDRAM_ADDR + SDRAM_SIZE)


void __init mach_get_physical_ram (unsigned long *ram_start,
				   unsigned long *ram_len)
{
	*ram_start = RAM_START;
	*ram_len = RAM_END - RAM_START;
}

void mach_gettimeofday (struct timespec *tv)
{
	tv->tv_sec = 0;
	tv->tv_nsec = 0;
}

/* Called before configuring an on-chip UART.  */
void rte_me2_cb_uart_pre_configure (unsigned chan,
				    unsigned cflags, unsigned baud)
{
	/* The RTE-V850E/ME2-CB connects some general-purpose I/O
	   pins on the CPU to the RTS/CTS lines of UARTB channel 0's
	   serial connection.
	   I/O pins P21 and P22 are RTS and CTS respectively.  */
	if (chan == 0) {
		/* Put P21 & P22 in I/O port mode.  */
		ME2_PORT2_PMC &= ~0x6;
		/* Make P21 and output, and P22 an input.  */
		ME2_PORT2_PM = (ME2_PORT2_PM & ~0xC) | 0x4;
	}

	me2_uart_pre_configure (chan, cflags, baud);
}

void __init mach_init_irqs (void)
{
	/* Initialize interrupts.  */
	me2_init_irqs ();
	rte_me2_cb_init_irqs ();
}

#ifdef CONFIG_ROM_KERNEL
/* Initialization for kernel in ROM.  */
static inline rom_kernel_init (void)
{
	/* If the kernel is in ROM, we have to copy any initialized data
	   from ROM into RAM.  */
	extern unsigned long _data_load_start, _sdata, _edata;
	register unsigned long *src = &_data_load_start;
	register unsigned long *dst = &_sdata, *end = &_edata;

	while (dst != end)
		*dst++ = *src++;
}
#endif /* CONFIG_ROM_KERNEL */

static void install_interrupt_vectors (void)
{
	unsigned long *p1, *p2;

	ME2_IRAMM = 0x03; /* V850E/ME2 iRAM write mode */

	/* vector copy to iRAM */
	p1 = (unsigned long *)0; /* v85x vector start */
	p2 = (unsigned long *)&_intv_start;
	while (p2 < (unsigned long *)&_intv_end)
		*p1++ = *p2++;

	ME2_IRAMM = 0x00; /* V850E/ME2 iRAM read mode */
}

/* CompactFlash */

static void cf_power_on (void)
{
	/* CF card detected? */
	if (CB_CF_STS0 & 0x0030)
		return;

	CB_CF_REG0 = 0x0002; /* reest on */
	mdelay (10);
	CB_CF_REG0 = 0x0003; /* power on */
	mdelay (10);
	CB_CF_REG0 = 0x0001; /* reset off */
	mdelay (10);
}

static void cf_power_off (void)
{
	CB_CF_REG0 = 0x0003; /* power on */
	mdelay (10);
	CB_CF_REG0 = 0x0002; /* reest on */
	mdelay (10);
}

void __init mach_early_init (void)
{
	install_interrupt_vectors ();

	/* CS1 SDRAM instruction cache enable */
	v850e_cache_enable (0x04, 0x03, 0);

	rte_cb_early_init ();

	/* CompactFlash power on */
	cf_power_on ();

#if defined (CONFIG_ROM_KERNEL)
	rom_kernel_init ();
#endif
}


/* RTE-V850E/ME2-CB Programmable Interrupt Controller.  */

static struct cb_pic_irq_init cb_pic_irq_inits[] = {
	{ "CB_EXTTM0",       IRQ_CB_EXTTM0,       1, 1, 6 },
	{ "CB_EXTSIO",       IRQ_CB_EXTSIO,       1, 1, 6 },
	{ "CB_TOVER",        IRQ_CB_TOVER,        1, 1, 6 },
	{ "CB_GINT0",        IRQ_CB_GINT0,        1, 1, 6 },
	{ "CB_USB",          IRQ_CB_USB,          1, 1, 6 },
	{ "CB_LANC",         IRQ_CB_LANC,         1, 1, 6 },
	{ "CB_USB_VBUS_ON",  IRQ_CB_USB_VBUS_ON,  1, 1, 6 },
	{ "CB_USB_VBUS_OFF", IRQ_CB_USB_VBUS_OFF, 1, 1, 6 },
	{ "CB_EXTTM1",       IRQ_CB_EXTTM1,       1, 1, 6 },
	{ "CB_EXTTM2",       IRQ_CB_EXTTM2,       1, 1, 6 },
	{ 0 }
};
#define NUM_CB_PIC_IRQ_INITS (ARRAY_SIZE(cb_pic_irq_inits) - 1)

static struct hw_interrupt_type cb_pic_hw_itypes[NUM_CB_PIC_IRQ_INITS];
static unsigned char cb_pic_active_irqs = 0;

void __init rte_me2_cb_init_irqs (void)
{
	cb_pic_init_irq_types (cb_pic_irq_inits, cb_pic_hw_itypes);

	/* Initalize on board PIC1 (not PIC0) enable */
	CB_PIC_INT0M  = 0x0000;
	CB_PIC_INT1M  = 0x0000;
	CB_PIC_INTR   = 0x0000;
	CB_PIC_INTEN |= CB_PIC_INT1EN;

	ME2_PORT2_PMC 	 |= 0x08;	/* INTP23/SCK1 mode */
	ME2_PORT2_PFC 	 &= ~0x08;	/* INTP23 mode */
	ME2_INTR(2) 	 &= ~0x08;	/* INTP23 falling-edge detect */
	ME2_INTF(2) 	 &= ~0x08;	/*   " */

	rte_cb_init_irqs ();	/* gbus &c */
}


/* Enable interrupt handling for interrupt IRQ.  */
void cb_pic_enable_irq (unsigned irq)
{
	CB_PIC_INT1M |= 1 << (irq - CB_PIC_BASE_IRQ);
}

void cb_pic_disable_irq (unsigned irq)
{
	CB_PIC_INT1M &= ~(1 << (irq - CB_PIC_BASE_IRQ));
}

void cb_pic_shutdown_irq (unsigned irq)
{
	cb_pic_disable_irq (irq);

	if (--cb_pic_active_irqs == 0)
		free_irq (IRQ_CB_PIC, 0);

	CB_PIC_INT1M &= ~(1 << (irq - CB_PIC_BASE_IRQ));
}

static irqreturn_t cb_pic_handle_irq (int irq, void *dev_id,
				      struct pt_regs *regs)
{
	irqreturn_t rval = IRQ_NONE;
	unsigned status = CB_PIC_INTR;
	unsigned enable = CB_PIC_INT1M;

	/* Only pay attention to enabled interrupts.  */
	status &= enable;

	CB_PIC_INTEN &= ~CB_PIC_INT1EN;

	if (status) {
		unsigned mask = 1;

		irq = CB_PIC_BASE_IRQ;
		do {
			/* There's an active interrupt, find out which one,
			   and call its handler.  */
			while (! (status & mask)) {
				irq++;
				mask <<= 1;
			}
			status &= ~mask;

			CB_PIC_INTR = mask;

			/* Recursively call handle_irq to handle it. */
			handle_irq (irq, regs);
			rval = IRQ_HANDLED;
		} while (status);
	}

	CB_PIC_INTEN |= CB_PIC_INT1EN;

	return rval;
}


static void irq_nop (unsigned irq) { }

static unsigned cb_pic_startup_irq (unsigned irq)
{
	int rval;

	if (cb_pic_active_irqs == 0) {
		rval = request_irq (IRQ_CB_PIC, cb_pic_handle_irq,
				    IRQF_DISABLED, "cb_pic_handler", 0);
		if (rval != 0)
			return rval;
	}

	cb_pic_active_irqs++;

	cb_pic_enable_irq (irq);

	return 0;
}

/* Initialize HW_IRQ_TYPES for INTC-controlled irqs described in array
   INITS (which is terminated by an entry with the name field == 0).  */
void __init cb_pic_init_irq_types (struct cb_pic_irq_init *inits,
				   struct hw_interrupt_type *hw_irq_types)
{
	struct cb_pic_irq_init *init;
	for (init = inits; init->name; init++) {
		struct hw_interrupt_type *hwit = hw_irq_types++;

		hwit->typename = init->name;

		hwit->startup  = cb_pic_startup_irq;
		hwit->shutdown = cb_pic_shutdown_irq;
		hwit->enable   = cb_pic_enable_irq;
		hwit->disable  = cb_pic_disable_irq;
		hwit->ack      = irq_nop;
		hwit->end      = irq_nop;

		/* Initialize kernel IRQ infrastructure for this interrupt.  */
		init_irq_handlers(init->base, init->num, init->interval, hwit);
	}
}
