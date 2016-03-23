/*
 * Copyright 2012 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * OF helpers for mtd.
 *
 * This file is released under the GPLv2
 *
 */
#include <linux/kernel.h>
#include <linux/of_mtd.h>
#include <linux/mtd/nand.h>
#include <linux/export.h>

/**
 * It maps 'enum nand_ecc_modes_t' found in include/linux/mtd/nand.h
 * into the device tree binding of 'nand-ecc', so that MTD
 * device driver can get nand ecc from device tree.
 */
static const char *nand_ecc_modes[] = {
	[NAND_ECC_NONE]		= "none",
	[NAND_ECC_SOFT]		= "soft",
	[NAND_ECC_HW]		= "hw",
	[NAND_ECC_HW_SYNDROME]	= "hw_syndrome",
	[NAND_ECC_HW_OOB_FIRST]	= "hw_oob_first",
	[NAND_ECC_SOFT_BCH]	= "soft_bch",
};

/**
 * of_get_nand_ecc_mode - Get nand ecc mode for given device_node
 * @np:	Pointer to the given device_node
 *
 * The function gets ecc mode string from property 'nand-ecc-mode',
 * and return its index in nand_ecc_modes table, or errno in error case.
 */
int of_get_nand_ecc_mode(struct device_node *np)
{
	const char *pm;
	int err, i;

	err = of_property_read_string(np, "nand-ecc-mode", &pm);
	if (err < 0)
		return err;

	for (i = 0; i < ARRAY_SIZE(nand_ecc_modes); i++)
		if (!strcasecmp(pm, nand_ecc_modes[i]))
			return i;

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(of_get_nand_ecc_mode);

/**
 * of_get_nand_ecc_algo - Get nand ecc algorithm for given device_node
 * @np:	Pointer to the given device_node
 *
 * The function gets ecc algorithm and returns its enum value, or errno in error
 * case.
 */
int of_get_nand_ecc_algo(struct device_node *np)
{
	const char *pm;
	int err;

	/*
	 * TODO: Read ECC algo OF property and map it to enum nand_ecc_algo.
	 * It's not implemented yet as currently NAND subsystem ignores
	 * algorithm explicitly set this way. Once it's handled we should
	 * document & support new property.
	 */

	/*
	 * For backward compatibility we also read "nand-ecc-mode" checking
	 * for some obsoleted values that were specifying ECC algorithm.
	 */
	err = of_property_read_string(np, "nand-ecc-mode", &pm);
	if (err < 0)
		return err;

	if (!strcasecmp(pm, "soft"))
		return NAND_ECC_HAMMING;
	else if (!strcasecmp(pm, "soft_bch"))
		return NAND_ECC_BCH;

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(of_get_nand_ecc_algo);

/**
 * of_get_nand_ecc_step_size - Get ECC step size associated to
 * the required ECC strength (see below).
 * @np:	Pointer to the given device_node
 *
 * return the ECC step size, or errno in error case.
 */
int of_get_nand_ecc_step_size(struct device_node *np)
{
	int ret;
	u32 val;

	ret = of_property_read_u32(np, "nand-ecc-step-size", &val);
	return ret ? ret : val;
}
EXPORT_SYMBOL_GPL(of_get_nand_ecc_step_size);

/**
 * of_get_nand_ecc_strength - Get required ECC strength over the
 * correspnding step size as defined by 'nand-ecc-size'
 * @np:	Pointer to the given device_node
 *
 * return the ECC strength, or errno in error case.
 */
int of_get_nand_ecc_strength(struct device_node *np)
{
	int ret;
	u32 val;

	ret = of_property_read_u32(np, "nand-ecc-strength", &val);
	return ret ? ret : val;
}
EXPORT_SYMBOL_GPL(of_get_nand_ecc_strength);

/**
 * of_get_nand_bus_width - Get nand bus witdh for given device_node
 * @np:	Pointer to the given device_node
 *
 * return bus width option, or errno in error case.
 */
int of_get_nand_bus_width(struct device_node *np)
{
	u32 val;

	if (of_property_read_u32(np, "nand-bus-width", &val))
		return 8;

	switch(val) {
	case 8:
	case 16:
		return val;
	default:
		return -EIO;
	}
}
EXPORT_SYMBOL_GPL(of_get_nand_bus_width);

/**
 * of_get_nand_on_flash_bbt - Get nand on flash bbt for given device_node
 * @np:	Pointer to the given device_node
 *
 * return true if present false other wise
 */
bool of_get_nand_on_flash_bbt(struct device_node *np)
{
	return of_property_read_bool(np, "nand-on-flash-bbt");
}
EXPORT_SYMBOL_GPL(of_get_nand_on_flash_bbt);
