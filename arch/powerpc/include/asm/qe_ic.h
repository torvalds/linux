/*
 * Copyright (C) 2006 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Authors: 	Shlomi Gridish <gridish@freescale.com>
 * 		Li Yang <leoli@freescale.com>
 *
 * Description:
 * QE IC external definitions and structure.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef _ASM_POWERPC_QE_IC_H
#define _ASM_POWERPC_QE_IC_H

#include <linux/irq.h>

struct device_node;
struct qe_ic;

#define NUM_OF_QE_IC_GROUPS	6

/* Flags when we init the QE IC */
#define QE_IC_SPREADMODE_GRP_W			0x00000001
#define QE_IC_SPREADMODE_GRP_X			0x00000002
#define QE_IC_SPREADMODE_GRP_Y			0x00000004
#define QE_IC_SPREADMODE_GRP_Z			0x00000008
#define QE_IC_SPREADMODE_GRP_RISCA		0x00000010
#define QE_IC_SPREADMODE_GRP_RISCB		0x00000020

#define QE_IC_LOW_SIGNAL			0x00000100
#define QE_IC_HIGH_SIGNAL			0x00000200

#define QE_IC_GRP_W_PRI0_DEST_SIGNAL_HIGH	0x00001000
#define QE_IC_GRP_W_PRI1_DEST_SIGNAL_HIGH	0x00002000
#define QE_IC_GRP_X_PRI0_DEST_SIGNAL_HIGH	0x00004000
#define QE_IC_GRP_X_PRI1_DEST_SIGNAL_HIGH	0x00008000
#define QE_IC_GRP_Y_PRI0_DEST_SIGNAL_HIGH	0x00010000
#define QE_IC_GRP_Y_PRI1_DEST_SIGNAL_HIGH	0x00020000
#define QE_IC_GRP_Z_PRI0_DEST_SIGNAL_HIGH	0x00040000
#define QE_IC_GRP_Z_PRI1_DEST_SIGNAL_HIGH	0x00080000
#define QE_IC_GRP_RISCA_PRI0_DEST_SIGNAL_HIGH	0x00100000
#define QE_IC_GRP_RISCA_PRI1_DEST_SIGNAL_HIGH	0x00200000
#define QE_IC_GRP_RISCB_PRI0_DEST_SIGNAL_HIGH	0x00400000
#define QE_IC_GRP_RISCB_PRI1_DEST_SIGNAL_HIGH	0x00800000
#define QE_IC_GRP_W_DEST_SIGNAL_SHIFT		(12)

/* QE interrupt sources groups */
enum qe_ic_grp_id {
	QE_IC_GRP_W = 0,	/* QE interrupt controller group W */
	QE_IC_GRP_X,		/* QE interrupt controller group X */
	QE_IC_GRP_Y,		/* QE interrupt controller group Y */
	QE_IC_GRP_Z,		/* QE interrupt controller group Z */
	QE_IC_GRP_RISCA,	/* QE interrupt controller RISC group A */
	QE_IC_GRP_RISCB		/* QE interrupt controller RISC group B */
};

#ifdef CONFIG_QUICC_ENGINE
void qe_ic_init(struct device_node *node, unsigned int flags,
		void (*low_handler)(unsigned int irq, struct irq_desc *desc),
		void (*high_handler)(unsigned int irq, struct irq_desc *desc));
unsigned int qe_ic_get_low_irq(struct qe_ic *qe_ic);
unsigned int qe_ic_get_high_irq(struct qe_ic *qe_ic);
#else
static inline void qe_ic_init(struct device_node *node, unsigned int flags,
		void (*low_handler)(unsigned int irq, struct irq_desc *desc),
		void (*high_handler)(unsigned int irq, struct irq_desc *desc))
{}
static inline unsigned int qe_ic_get_low_irq(struct qe_ic *qe_ic)
{ return 0; }
static inline unsigned int qe_ic_get_high_irq(struct qe_ic *qe_ic)
{ return 0; }
#endif /* CONFIG_QUICC_ENGINE */

void qe_ic_set_highest_priority(unsigned int virq, int high);
int qe_ic_set_priority(unsigned int virq, unsigned int priority);
int qe_ic_set_high_priority(unsigned int virq, unsigned int priority, int high);

static inline void qe_ic_cascade_low_ipic(unsigned int irq,
					  struct irq_desc *desc)
{
	struct qe_ic *qe_ic = irq_desc_get_handler_data(desc);
	unsigned int cascade_irq = qe_ic_get_low_irq(qe_ic);

	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);
}

static inline void qe_ic_cascade_high_ipic(unsigned int irq,
					   struct irq_desc *desc)
{
	struct qe_ic *qe_ic = irq_desc_get_handler_data(desc);
	unsigned int cascade_irq = qe_ic_get_high_irq(qe_ic);

	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);
}

static inline void qe_ic_cascade_low_mpic(unsigned int irq,
					  struct irq_desc *desc)
{
	struct qe_ic *qe_ic = irq_desc_get_handler_data(desc);
	unsigned int cascade_irq = qe_ic_get_low_irq(qe_ic);
	struct irq_chip *chip = irq_desc_get_chip(desc);

	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);

	chip->irq_eoi(&desc->irq_data);
}

static inline void qe_ic_cascade_high_mpic(unsigned int irq,
					   struct irq_desc *desc)
{
	struct qe_ic *qe_ic = irq_desc_get_handler_data(desc);
	unsigned int cascade_irq = qe_ic_get_high_irq(qe_ic);
	struct irq_chip *chip = irq_desc_get_chip(desc);

	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);

	chip->irq_eoi(&desc->irq_data);
}

static inline void qe_ic_cascade_muxed_mpic(unsigned int irq,
					    struct irq_desc *desc)
{
	struct qe_ic *qe_ic = irq_desc_get_handler_data(desc);
	unsigned int cascade_irq;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	cascade_irq = qe_ic_get_high_irq(qe_ic);
	if (cascade_irq == NO_IRQ)
		cascade_irq = qe_ic_get_low_irq(qe_ic);

	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);

	chip->irq_eoi(&desc->irq_data);
}

#endif /* _ASM_POWERPC_QE_IC_H */
