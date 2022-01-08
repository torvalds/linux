// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2019-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

void hl_set_pll_profile(struct hl_device *hdev, enum hl_pll_frequency freq)
{
	hl_set_frequency(hdev, hdev->asic_prop.clk_pll_index,
			hdev->asic_prop.max_freq_value);
}

int hl_get_clk_rate(struct hl_device *hdev, u32 *cur_clk, u32 *max_clk)
{
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	if (!hdev->pdev) {
		*cur_clk = 0;
		*max_clk = 0;
		return 0;
	}

	value = hl_get_frequency(hdev, hdev->asic_prop.clk_pll_index, false);

	if (value < 0) {
		dev_err(hdev->dev, "Failed to retrieve device max clock %ld\n", value);
		return value;
	}

	*max_clk = (value / 1000 / 1000);

	value = hl_get_frequency(hdev, hdev->asic_prop.clk_pll_index, true);

	if (value < 0) {
		dev_err(hdev->dev, "Failed to retrieve device current clock %ld\n", value);
		return value;
	}

	*cur_clk = (value / 1000 / 1000);

	return 0;
}
