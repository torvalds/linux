/*
 * arch/arm/plat-spear/include/plat/shirq.h
 *
 * SPEAr platform shared irq layer header file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PLAT_SHIRQ_H
#define __PLAT_SHIRQ_H

#include <linux/irq.h>
#include <linux/types.h>

/*
 * struct shirq_regs: shared irq register configuration
 *
 * enb_reg: enable register offset
 * reset_to_enb: val 1 indicates, we need to clear bit for enabling interrupt
 * status_reg: status register offset
 * status_reg_mask: status register valid mask
 * clear_reg: clear register offset
 * reset_to_clear: val 1 indicates, we need to clear bit for clearing interrupt
 */
struct shirq_regs {
	u32 enb_reg;
	u32 reset_to_enb;
	u32 status_reg;
	u32 clear_reg;
	u32 reset_to_clear;
};

/*
 * struct spear_shirq: shared irq structure
 *
 * irq: hardware irq number
 * irq_base: base irq in linux domain
 * irq_nr: no. of shared interrupts in a particular block
 * irq_bit_off: starting bit offset in the status register
 * invalid_irq: irq group is currently disabled
 * base: base address of shared irq register
 * regs: register configuration for shared irq block
 */
struct spear_shirq {
	u32 irq;
	u32 irq_base;
	u32 irq_nr;
	u32 irq_bit_off;
	int invalid_irq;
	void __iomem *base;
	struct shirq_regs regs;
};

int __init spear300_shirq_of_init(struct device_node *np,
		struct device_node *parent);
int __init spear310_shirq_of_init(struct device_node *np,
		struct device_node *parent);
int __init spear320_shirq_of_init(struct device_node *np,
		struct device_node *parent);

#endif /* __PLAT_SHIRQ_H */
