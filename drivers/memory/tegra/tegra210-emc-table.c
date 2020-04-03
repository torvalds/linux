// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/of_reserved_mem.h>

#include "tegra210-emc.h"

#define TEGRA_EMC_MAX_FREQS		16

static int tegra210_emc_table_device_init(struct reserved_mem *rmem,
					  struct device *dev)
{
	struct tegra210_emc *emc = dev_get_drvdata(dev);
	struct tegra210_emc_timing *timings;
	unsigned int i, count = 0;

	timings = memremap(rmem->base, rmem->size, MEMREMAP_WB);
	if (!timings) {
		dev_err(dev, "failed to map EMC table\n");
		return -ENOMEM;
	}

	count = 0;

	for (i = 0; i < TEGRA_EMC_MAX_FREQS; i++) {
		if (timings[i].revision == 0)
			break;

		count++;
	}

	/* only the nominal and derated tables are expected */
	if (emc->derated) {
		dev_warn(dev, "excess EMC table '%s'\n", rmem->name);
		goto out;
	}

	if (emc->nominal) {
		if (count != emc->num_timings) {
			dev_warn(dev, "%u derated vs. %u nominal entries\n",
				 count, emc->num_timings);
			memunmap(timings);
			return -EINVAL;
		}

		emc->derated = timings;
	} else {
		emc->num_timings = count;
		emc->nominal = timings;
	}

out:
	/* keep track of which table this is */
	rmem->priv = timings;

	return 0;
}

static void tegra210_emc_table_device_release(struct reserved_mem *rmem,
					      struct device *dev)
{
	struct tegra210_emc_timing *timings = rmem->priv;
	struct tegra210_emc *emc = dev_get_drvdata(dev);

	if ((emc->nominal && timings != emc->nominal) &&
	    (emc->derated && timings != emc->derated))
		dev_warn(dev, "trying to release unassigned EMC table '%s'\n",
			 rmem->name);

	memunmap(timings);
}

static const struct reserved_mem_ops tegra210_emc_table_ops = {
	.device_init = tegra210_emc_table_device_init,
	.device_release = tegra210_emc_table_device_release,
};

static int tegra210_emc_table_init(struct reserved_mem *rmem)
{
	pr_debug("Tegra210 EMC table at %pa, size %lu bytes\n", &rmem->base,
		 (unsigned long)rmem->size);

	rmem->ops = &tegra210_emc_table_ops;

	return 0;
}
RESERVEDMEM_OF_DECLARE(tegra210_emc_table, "nvidia,tegra210-emc-table",
		       tegra210_emc_table_init);
