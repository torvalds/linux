// SPDX-License-Identifier: GPL-2.0-or-later

#include "internals.h"

static int
sdtnqgama_choose_interface_config(struct nand_chip *chip,
				  struct nand_interface_config *iface)
{
	onfi_fill_interface_config(chip, iface, NAND_SDR_IFACE, 0);

	return nand_choose_best_sdr_timings(chip, iface, NULL);
}

static int sandisk_nand_init(struct nand_chip *chip)
{
	if (!strncmp("SDTNQGAMA", chip->parameters.model,
		     sizeof("SDTNQGAMA") - 1))
		chip->ops.choose_interface_config =
			&sdtnqgama_choose_interface_config;

	return 0;
}

const struct nand_manufacturer_ops sandisk_nand_manuf_ops = {
	.init = sandisk_nand_init,
};
