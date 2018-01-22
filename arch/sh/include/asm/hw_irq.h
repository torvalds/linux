/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_HW_IRQ_H
#define __ASM_SH_HW_IRQ_H

#include <linux/init.h>
#include <linux/sh_intc.h>
#include <linux/atomic.h>

extern atomic_t irq_err_count;

struct ipr_data {
	unsigned char irq;
	unsigned char ipr_idx;		/* Index for the IPR registered */
	unsigned char shift;		/* Number of bits to shift the data */
	unsigned char priority;		/* The priority */
};

struct ipr_desc {
	unsigned long *ipr_offsets;
	unsigned int nr_offsets;
	struct ipr_data *ipr_data;
	unsigned int nr_irqs;
	struct irq_chip chip;
};

void register_ipr_controller(struct ipr_desc *);

void __init plat_irq_setup(void);
void __init plat_irq_setup_sh3(void);
void __init plat_irq_setup_pins(int mode);

enum { IRQ_MODE_IRQ, IRQ_MODE_IRQ7654, IRQ_MODE_IRQ3210,
       IRQ_MODE_IRL7654_MASK, IRQ_MODE_IRL3210_MASK,
       IRQ_MODE_IRL7654, IRQ_MODE_IRL3210 };

#endif /* __ASM_SH_HW_IRQ_H */
