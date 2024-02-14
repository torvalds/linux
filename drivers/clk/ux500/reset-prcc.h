/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __RESET_PRCC_H
#define __RESET_PRCC_H

#include <linux/reset-controller.h>
#include <linux/io.h>

/**
 * struct u8500_prcc_reset - U8500 PRCC reset controller state
 * @rcdev: reset controller device
 * @phy_base: the physical base address for each PRCC block
 * @base: the remapped PRCC bases
 */
struct u8500_prcc_reset {
	struct reset_controller_dev rcdev;
	u32 phy_base[CLKRST_MAX];
	void __iomem *base[CLKRST_MAX];
};

void u8500_prcc_reset_init(struct device_node *np, struct u8500_prcc_reset *ur);

#endif
