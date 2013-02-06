/* linux/arch/arm/mach-exynos/include/mach/dwmci.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Synopsys DesignWare Mobile Storage for EXYNOS4210
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARM_ARCH_DWMCI_H
#define __ASM_ARM_ARCH_DWMCI_H __FILE__

#include <linux/mmc/dw_mmc.h>

extern void exynos_dwmci_set_platdata(struct dw_mci_board *pd, u32 slot_id);

#endif /* __ASM_ARM_ARCH_DWMCI_H */
