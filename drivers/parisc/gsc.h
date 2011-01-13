/*
 * drivers/parisc/gsc.h
 * Declarations for functions in gsc.c
 * Copyright (c) 2000-2002 Helge Deller, Matthew Wilcox
 *
 * Distributed under the terms of the GPL, version 2
 */

#include <linux/interrupt.h>
#include <asm/hardware.h>
#include <asm/parisc-device.h>

#define OFFSET_IRR 0x0000   /* Interrupt request register */
#define OFFSET_IMR 0x0004   /* Interrupt mask register */
#define OFFSET_IPR 0x0008   /* Interrupt pending register */
#define OFFSET_ICR 0x000C   /* Interrupt control register */
#define OFFSET_IAR 0x0010   /* Interrupt address register */

/* PA I/O Architected devices support at least 5 bits in the EIM register. */
#define GSC_EIM_WIDTH 5

struct gsc_irq {
	unsigned long txn_addr;	/* IRQ "target" */
	int txn_data;		/* HW "IRQ" */
	int irq;		/* virtual IRQ */
};

struct gsc_asic {
	struct parisc_device *gsc;
	unsigned long hpa;
	char *name;
	int version;
	int type;
	int eim;
	int global_irq[32];
};

int gsc_common_setup(struct parisc_device *parent, struct gsc_asic *gsc_asic);
int gsc_alloc_irq(struct gsc_irq *dev);			/* dev needs an irq */
int gsc_claim_irq(struct gsc_irq *dev, int irq);	/* dev needs this irq */
int gsc_assign_irq(struct irq_chip *type, void *data);
int gsc_find_local_irq(unsigned int irq, int *global_irq, int limit);
void gsc_fixup_irqs(struct parisc_device *parent, void *ctrl,
		void (*choose)(struct parisc_device *child, void *ctrl));
void gsc_asic_assign_irq(struct gsc_asic *asic, int local_irq, int *irqp);

irqreturn_t gsc_asic_intr(int irq, void *dev);
