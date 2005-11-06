/* 
 * Copyright (C) 2000 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.                            
 *
 * Looks after interrupts on the HARP board.
 *
 * Bases on the IPR irq system
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/harp/harp.h>


#define NUM_EXTERNAL_IRQS 16

// Early versions of the STB1 Overdrive required this nasty frig
//#define INVERT_INTMASK_WRITES

static void enable_harp_irq(unsigned int irq);
static void disable_harp_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define shutdown_harp_irq disable_harp_irq

static void mask_and_ack_harp(unsigned int);
static void end_harp_irq(unsigned int irq);

static unsigned int startup_harp_irq(unsigned int irq)
{
	enable_harp_irq(irq);
	return 0;		/* never anything pending */
}

static struct hw_interrupt_type harp_irq_type = {
	.typename = "Harp-IRQ",
	.startup = startup_harp_irq,
	.shutdown = shutdown_harp_irq,
	.enable = enable_harp_irq,
	.disable = disable_harp_irq,
	.ack = mask_and_ack_harp,
	.end = end_harp_irq
};

static void disable_harp_irq(unsigned int irq)
{
	unsigned val, flags;
	unsigned maskReg;
	unsigned mask;
	int pri;

	if (irq < 0 || irq >= NUM_EXTERNAL_IRQS)
		return;

	pri = 15 - irq;

	if (pri < 8) {
		maskReg = EPLD_INTMASK0;
	} else {
		maskReg = EPLD_INTMASK1;
		pri -= 8;
	}

	local_irq_save(flags);
	mask = ctrl_inl(maskReg);
	mask &= (~(1 << pri));
#if defined(INVERT_INTMASK_WRITES)
	mask ^= 0xff;
#endif
	ctrl_outl(mask, maskReg);
	local_irq_restore(flags);
}

static void enable_harp_irq(unsigned int irq)
{
	unsigned flags;
	unsigned maskReg;
	unsigned mask;
	int pri;

	if (irq < 0 || irq >= NUM_EXTERNAL_IRQS)
		return;

	pri = 15 - irq;

	if (pri < 8) {
		maskReg = EPLD_INTMASK0;
	} else {
		maskReg = EPLD_INTMASK1;
		pri -= 8;
	}

	local_irq_save(flags);
	mask = ctrl_inl(maskReg);


	mask |= (1 << pri);

#if defined(INVERT_INTMASK_WRITES)
	mask ^= 0xff;
#endif
	ctrl_outl(mask, maskReg);

	local_irq_restore(flags);
}

/* This functions sets the desired irq handler to be an overdrive type */
static void __init make_harp_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].handler = &harp_irq_type;
	disable_harp_irq(irq);
}

static void mask_and_ack_harp(unsigned int irq)
{
	disable_harp_irq(irq);
}

static void end_harp_irq(unsigned int irq)
{
	enable_harp_irq(irq);
}

void __init init_harp_irq(void)
{
	int i;

#if !defined(INVERT_INTMASK_WRITES)
	// On the harp these are set to enable an interrupt
	ctrl_outl(0x00, EPLD_INTMASK0);
	ctrl_outl(0x00, EPLD_INTMASK1);
#else
	// On the Overdrive the data is inverted before being stored in the reg
	ctrl_outl(0xff, EPLD_INTMASK0);
	ctrl_outl(0xff, EPLD_INTMASK1);
#endif

	for (i = 0; i < NUM_EXTERNAL_IRQS; i++) {
		make_harp_irq(i);
	}
}
