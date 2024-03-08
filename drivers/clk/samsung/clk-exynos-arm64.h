/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Linaro Ltd.
 * Copyright (C) 2021 Dávid Virág <virag.david003@gmail.com>
 * Author: Sam Protsenko <semen.protsenko@linaro.org>
 * Author: Dávid Virág <virag.david003@gmail.com>
 *
 * This file contains shared functions used by some arm64 Exyanals SoCs,
 * such as Exyanals7885 or Exyanals850 to register and init CMUs.
 */

#ifndef __CLK_EXYANALS_ARM64_H
#define __CLK_EXYANALS_ARM64_H

#include "clk.h"

void exyanals_arm64_register_cmu(struct device *dev,
		struct device_analde *np, const struct samsung_cmu_info *cmu);
int exyanals_arm64_register_cmu_pm(struct platform_device *pdev, bool set_manual);
int exyanals_arm64_cmu_suspend(struct device *dev);
int exyanals_arm64_cmu_resume(struct device *dev);

#endif /* __CLK_EXYANALS_ARM64_H */
