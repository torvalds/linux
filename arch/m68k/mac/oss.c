/*
 *	Operating System Services (OSS) chip handling
 *	Written by Joshua M. Thompson (funaho@jurai.org)
 *
 *
 *	This chip is used in the IIfx in place of VIA #2. It acts like a fancy
 *	VIA chip with prorammable interrupt levels.
 *
 * 990502 (jmt) - Major rewrite for new interrupt architecture as well as some
 *		  recent insights into OSS operational details.
 * 990610 (jmt) - Now taking full advantage of the OSS. Interrupts are mapped
 *		  to mostly match the A/UX interrupt scheme supported on the
 *		  VIA side. Also added support for enabling the ISM irq again
 *		  since we now have a functional IOP manager.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_via.h>
#include <asm/mac_oss.h>

int oss_present;
volatile struct mac_oss *oss;

/*
 * Initialize the OSS
 */

void __init oss_init(void)
{
	int i;

	if (macintosh_config->ident != MAC_MODEL_IIFX)
		return;

	oss = (struct mac_oss *) OSS_BASE;
	pr_debug("OSS detected at %p", oss);
	oss_present = 1;

	/* Disable all interrupts. Unlike a VIA it looks like we    */
	/* do this by setting the source's interrupt level to zero. */

	for (i = 0; i < OSS_NUM_SOURCES; i++)
		oss->irq_level[i] = 0;
}

/*
 * Handle miscellaneous OSS interrupts.
 */

static void oss_irq(struct irq_desc *desc)
{
	int events = oss->irq_pending &
		(OSS_IP_IOPSCC | OSS_IP_SCSI | OSS_IP_IOPISM);

	if (events & OSS_IP_IOPSCC) {
		oss->irq_pending &= ~OSS_IP_IOPSCC;
		generic_handle_irq(IRQ_MAC_SCC);
	}

	if (events & OSS_IP_SCSI) {
		oss->irq_pending &= ~OSS_IP_SCSI;
		generic_handle_irq(IRQ_MAC_SCSI);
	}

	if (events & OSS_IP_IOPISM) {
		oss->irq_pending &= ~OSS_IP_IOPISM;
		generic_handle_irq(IRQ_MAC_ADB);
	}
}

/*
 * Nubus IRQ handler, OSS style
 *
 * Unlike the VIA/RBV this is on its own autovector interrupt level.
 */

static void oss_nubus_irq(struct irq_desc *desc)
{
	int events, irq_bit, i;

	events = oss->irq_pending & OSS_IP_NUBUS;
	if (!events)
		return;

	/* There are only six slots on the OSS, not seven */

	i = 6;
	irq_bit = 0x40;
	do {
		--i;
		irq_bit >>= 1;
		if (events & irq_bit) {
			oss->irq_pending &= ~irq_bit;
			generic_handle_irq(NUBUS_SOURCE_BASE + i);
		}
	} while(events & (irq_bit - 1));
}

/*
 * Register the OSS and NuBus interrupt dispatchers.
 *
 * This IRQ mapping is laid out with two things in mind: first, we try to keep
 * things on their own levels to avoid having to do double-dispatches. Second,
 * the levels match as closely as possible the alternate IRQ mapping mode (aka
 * "A/UX mode") available on some VIA machines.
 */

#define OSS_IRQLEV_IOPISM    IRQ_AUTO_1
#define OSS_IRQLEV_SCSI      IRQ_AUTO_2
#define OSS_IRQLEV_NUBUS     IRQ_AUTO_3
#define OSS_IRQLEV_IOPSCC    IRQ_AUTO_4
#define OSS_IRQLEV_VIA1      IRQ_AUTO_6

void __init oss_register_interrupts(void)
{
	irq_set_chained_handler(OSS_IRQLEV_IOPISM, oss_irq);
	irq_set_chained_handler(OSS_IRQLEV_SCSI,   oss_irq);
	irq_set_chained_handler(OSS_IRQLEV_NUBUS,  oss_nubus_irq);
	irq_set_chained_handler(OSS_IRQLEV_IOPSCC, oss_irq);
	irq_set_chained_handler(OSS_IRQLEV_VIA1,   via1_irq);

	/* OSS_VIA1 gets enabled here because it has no machspec interrupt. */
	oss->irq_level[OSS_VIA1] = IRQ_AUTO_6;
}

/*
 * Enable an OSS interrupt
 *
 * It looks messy but it's rather straightforward. The switch() statement
 * just maps the machspec interrupt numbers to the right OSS interrupt
 * source (if the OSS handles that interrupt) and then sets the interrupt
 * level for that source to nonzero, thus enabling the interrupt.
 */

void oss_irq_enable(int irq) {
	switch(irq) {
		case IRQ_MAC_SCC:
			oss->irq_level[OSS_IOPSCC] = OSS_IRQLEV_IOPSCC;
			return;
		case IRQ_MAC_ADB:
			oss->irq_level[OSS_IOPISM] = OSS_IRQLEV_IOPISM;
			return;
		case IRQ_MAC_SCSI:
			oss->irq_level[OSS_SCSI] = OSS_IRQLEV_SCSI;
			return;
		case IRQ_NUBUS_9:
		case IRQ_NUBUS_A:
		case IRQ_NUBUS_B:
		case IRQ_NUBUS_C:
		case IRQ_NUBUS_D:
		case IRQ_NUBUS_E:
			irq -= NUBUS_SOURCE_BASE;
			oss->irq_level[irq] = OSS_IRQLEV_NUBUS;
			return;
	}

	if (IRQ_SRC(irq) == 1)
		via_irq_enable(irq);
}

/*
 * Disable an OSS interrupt
 *
 * Same as above except we set the source's interrupt level to zero,
 * to disable the interrupt.
 */

void oss_irq_disable(int irq) {
	switch(irq) {
		case IRQ_MAC_SCC:
			oss->irq_level[OSS_IOPSCC] = 0;
			return;
		case IRQ_MAC_ADB:
			oss->irq_level[OSS_IOPISM] = 0;
			return;
		case IRQ_MAC_SCSI:
			oss->irq_level[OSS_SCSI] = 0;
			return;
		case IRQ_NUBUS_9:
		case IRQ_NUBUS_A:
		case IRQ_NUBUS_B:
		case IRQ_NUBUS_C:
		case IRQ_NUBUS_D:
		case IRQ_NUBUS_E:
			irq -= NUBUS_SOURCE_BASE;
			oss->irq_level[irq] = 0;
			return;
	}

	if (IRQ_SRC(irq) == 1)
		via_irq_disable(irq);
}
