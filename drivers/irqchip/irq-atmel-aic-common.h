/*
 * Atmel AT91 common AIC (Advanced Interrupt Controller) header file
 *
 *  Copyright (C) 2004 SAN People
 *  Copyright (C) 2004 ATMEL
 *  Copyright (C) Rick Bronson
 *  Copyright (C) 2014 Free Electrons
 *
 *  Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __IRQ_ATMEL_AIC_COMMON_H
#define __IRQ_ATMEL_AIC_COMMON_H


int aic_common_set_type(struct irq_data *d, unsigned type, unsigned *val);

int aic_common_set_priority(int priority, unsigned *val);

int aic_common_irq_domain_xlate(struct irq_domain *d,
				struct device_node *ctrlr,
				const u32 *intspec,
				unsigned int intsize,
				irq_hw_number_t *out_hwirq,
				unsigned int *out_type);

struct irq_domain *__init aic_common_of_init(struct device_node *node,
					     const struct irq_domain_ops *ops,
					     const char *name, int nirqs);

void __init aic_common_rtc_irq_fixup(struct device_node *root);

void __init aic_common_irq_fixup(const struct of_device_id *matches);

#endif /* __IRQ_ATMEL_AIC_COMMON_H */
