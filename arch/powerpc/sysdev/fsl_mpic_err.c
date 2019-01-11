/*
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 *
 * Author: Varun Sethi <varun.sethi@freescale.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 */

#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mpic.h>

#include "mpic.h"

#define MPIC_ERR_INT_BASE	0x3900
#define MPIC_ERR_INT_EISR	0x0000
#define MPIC_ERR_INT_EIMR	0x0010

static inline u32 mpic_fsl_err_read(u32 __iomem *base, unsigned int err_reg)
{
	return in_be32(base + (err_reg >> 2));
}

static inline void mpic_fsl_err_write(u32 __iomem *base, u32 value)
{
	out_be32(base + (MPIC_ERR_INT_EIMR >> 2), value);
}

static void fsl_mpic_mask_err(struct irq_data *d)
{
	u32 eimr;
	struct mpic *mpic = irq_data_get_irq_chip_data(d);
	unsigned int src = virq_to_hw(d->irq) - mpic->err_int_vecs[0];

	eimr = mpic_fsl_err_read(mpic->err_regs, MPIC_ERR_INT_EIMR);
	eimr |= (1 << (31 - src));
	mpic_fsl_err_write(mpic->err_regs, eimr);
}

static void fsl_mpic_unmask_err(struct irq_data *d)
{
	u32 eimr;
	struct mpic *mpic = irq_data_get_irq_chip_data(d);
	unsigned int src = virq_to_hw(d->irq) - mpic->err_int_vecs[0];

	eimr = mpic_fsl_err_read(mpic->err_regs, MPIC_ERR_INT_EIMR);
	eimr &= ~(1 << (31 - src));
	mpic_fsl_err_write(mpic->err_regs, eimr);
}

static struct irq_chip fsl_mpic_err_chip = {
	.irq_disable	= fsl_mpic_mask_err,
	.irq_mask	= fsl_mpic_mask_err,
	.irq_unmask	= fsl_mpic_unmask_err,
};

int mpic_setup_error_int(struct mpic *mpic, int intvec)
{
	int i;

	mpic->err_regs = ioremap(mpic->paddr + MPIC_ERR_INT_BASE, 0x1000);
	if (!mpic->err_regs) {
		pr_err("could not map mpic error registers\n");
		return -ENOMEM;
	}
	mpic->hc_err = fsl_mpic_err_chip;
	mpic->hc_err.name = mpic->name;
	mpic->flags |= MPIC_FSL_HAS_EIMR;
	/* allocate interrupt vectors for error interrupts */
	for (i = MPIC_MAX_ERR - 1; i >= 0; i--)
		mpic->err_int_vecs[i] = intvec--;

	return 0;
}

int mpic_map_error_int(struct mpic *mpic, unsigned int virq, irq_hw_number_t  hw)
{
	if ((mpic->flags & MPIC_FSL_HAS_EIMR) &&
	    (hw >= mpic->err_int_vecs[0] &&
	     hw <= mpic->err_int_vecs[MPIC_MAX_ERR - 1])) {
		WARN_ON(mpic->flags & MPIC_SECONDARY);

		pr_debug("mpic: mapping as Error Interrupt\n");
		irq_set_chip_data(virq, mpic);
		irq_set_chip_and_handler(virq, &mpic->hc_err,
					 handle_level_irq);
		return 1;
	}

	return 0;
}

static irqreturn_t fsl_error_int_handler(int irq, void *data)
{
	struct mpic *mpic = (struct mpic *) data;
	u32 eisr, eimr;
	int errint;
	unsigned int cascade_irq;

	eisr = mpic_fsl_err_read(mpic->err_regs, MPIC_ERR_INT_EISR);
	eimr = mpic_fsl_err_read(mpic->err_regs, MPIC_ERR_INT_EIMR);

	if (!(eisr & ~eimr))
		return IRQ_NONE;

	while (eisr) {
		errint = __builtin_clz(eisr);
		cascade_irq = irq_linear_revmap(mpic->irqhost,
				 mpic->err_int_vecs[errint]);
		WARN_ON(!cascade_irq);
		if (cascade_irq) {
			generic_handle_irq(cascade_irq);
		} else {
			eimr |=  1 << (31 - errint);
			mpic_fsl_err_write(mpic->err_regs, eimr);
		}
		eisr &= ~(1 << (31 - errint));
	}

	return IRQ_HANDLED;
}

void mpic_err_int_init(struct mpic *mpic, irq_hw_number_t irqnum)
{
	unsigned int virq;
	int ret;

	virq = irq_create_mapping(mpic->irqhost, irqnum);
	if (!virq) {
		pr_err("Error interrupt setup failed\n");
		return;
	}

	/* Mask all error interrupts */
	mpic_fsl_err_write(mpic->err_regs, ~0);

	ret = request_irq(virq, fsl_error_int_handler, IRQF_NO_THREAD,
		    "mpic-error-int", mpic);
	if (ret)
		pr_err("Failed to register error interrupt handler\n");
}
