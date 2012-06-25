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
 * struct shirq_dev_config: shared irq device configuration
 *
 * virq: virtual irq number of device
 * enb_mask: enable mask of device
 * status_mask: status mask of device
 * clear_mask: clear mask of device
 */
struct shirq_dev_config {
	u32 virq;
	u32 enb_mask;
	u32 status_mask;
	u32 clear_mask;
};

/*
 * struct shirq_regs: shared irq register configuration
 *
 * base: base address of shared irq register
 * enb_reg: enable register offset
 * reset_to_enb: val 1 indicates, we need to clear bit for enabling interrupt
 * status_reg: status register offset
 * status_reg_mask: status register valid mask
 * clear_reg: clear register offset
 * reset_to_clear: val 1 indicates, we need to clear bit for clearing interrupt
 */
struct shirq_regs {
	void __iomem *base;
	u32 enb_reg;
	u32 reset_to_enb;
	u32 status_reg;
	u32 status_reg_mask;
	u32 clear_reg;
	u32 reset_to_clear;
};

/*
 * struct spear_shirq: shared irq structure
 *
 * irq: hardware irq number
 * dev_config: array of device config structures which are using "irq" line
 * dev_count: size of dev_config array
 * regs: register configuration for shared irq block
 */
struct spear_shirq {
	u32 irq;
	struct shirq_dev_config *dev_config;
	u32 dev_count;
	struct shirq_regs regs;
};

int spear_shirq_register(struct spear_shirq *shirq);

#endif /* __PLAT_SHIRQ_H */
