/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Linaro Ltd.
 * Copyright (C) 2021 Dávid Virág <virag.david003@gmail.com>
 * Author: Sam Protsenko <semen.protsenko@linaro.org>
 * Author: Dávid Virág <virag.david003@gmail.com>
 *
 * This file contains shared functions used by some arm64 Exynos SoCs,
 * such as Exynos7885 or Exynos850 to register and init CMUs.
 */

#ifndef __CLK_EXYNOS_ARM64_H
#define __CLK_EXYNOS_ARM64_H

#include "clk.h"

void exynos_arm64_register_cmu(struct device *dev,
		struct device_node *np, const struct samsung_cmu_info *cmu);

#endif /* __CLK_EXYNOS_ARM64_H */
