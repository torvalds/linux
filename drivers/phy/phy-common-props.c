// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * phy-common-props.c  --  Common PHY properties
 *
 * Copyright 2025-2026 NXP
 */
#include <linux/export.h>
#include <linux/fwnode.h>
#include <linux/phy/phy-common-props.h>
#include <linux/printk.h>
#include <linux/property.h>
#include <linux/slab.h>

/**
 * fwnode_get_u32_prop_for_name - Find u32 property by name, or default value
 * @fwnode: Pointer to firmware node, or NULL to use @default_val
 * @name: Property name used as lookup key in @names_title (must not be NULL)
 * @props_title: Name of u32 array property holding values
 * @names_title: Name of string array property holding lookup keys
 * @default_val: Default value if @fwnode is NULL or @props_title is empty
 * @val: Pointer to store the returned value
 *
 * This function retrieves a u32 value from @props_title based on a name lookup
 * in @names_title. The value stored in @val is determined as follows:
 *
 * - If @fwnode is NULL or @props_title is empty: @default_val is used
 * - If @props_title has exactly one element and @names_title is empty:
 *   that element is used
 * - Otherwise: @val is set to the element at the same index where @name is
 *   found in @names_title.
 * - If @name is not found, the function looks for a "default" entry in
 *   @names_title and uses the corresponding value from @props_title
 *
 * When both @props_title and @names_title are present, they must have the
 * same number of elements (except when @props_title has exactly one element).
 *
 * Return: zero on success, negative error on failure.
 */
static int fwnode_get_u32_prop_for_name(struct fwnode_handle *fwnode,
					const char *name,
					const char *props_title,
					const char *names_title,
					unsigned int default_val,
					unsigned int *val)
{
	int err, n_props, n_names, idx;
	u32 *props;

	if (!name) {
		pr_err("Lookup key inside \"%s\" is mandatory\n", names_title);
		return -EINVAL;
	}

	n_props = fwnode_property_count_u32(fwnode, props_title);
	if (n_props <= 0) {
		/* fwnode is NULL, or is missing requested property */
		*val = default_val;
		return 0;
	}

	n_names = fwnode_property_string_array_count(fwnode, names_title);
	if (n_names >= 0 && n_props != n_names) {
		pr_err("%pfw mismatch between \"%s\" and \"%s\" property count (%d vs %d)\n",
		       fwnode, props_title, names_title, n_props, n_names);
		return -EINVAL;
	}

	idx = fwnode_property_match_string(fwnode, names_title, name);
	if (idx < 0)
		idx = fwnode_property_match_string(fwnode, names_title, "default");
	/*
	 * If the mode name is missing, it can only mean the specified property
	 * is the default one for all modes, so reject any other property count
	 * than 1.
	 */
	if (idx < 0 && n_props != 1) {
		pr_err("%pfw \"%s \" property has %d elements, but cannot find \"%s\" in \"%s\" and there is no default value\n",
		       fwnode, props_title, n_props, name, names_title);
		return -EINVAL;
	}

	if (n_props == 1) {
		err = fwnode_property_read_u32(fwnode, props_title, val);
		if (err)
			return err;

		return 0;
	}

	/* We implicitly know idx >= 0 here */
	props = kcalloc(n_props, sizeof(*props), GFP_KERNEL);
	if (!props)
		return -ENOMEM;

	err = fwnode_property_read_u32_array(fwnode, props_title, props, n_props);
	if (err >= 0)
		*val = props[idx];

	kfree(props);

	return err;
}

static int phy_get_polarity_for_mode(struct fwnode_handle *fwnode,
				     const char *mode_name,
				     unsigned int supported,
				     unsigned int default_val,
				     const char *polarity_prop,
				     const char *names_prop,
				     unsigned int *val)
{
	int err;

	err = fwnode_get_u32_prop_for_name(fwnode, mode_name, polarity_prop,
					   names_prop, default_val, val);
	if (err)
		return err;

	if (!(supported & BIT(*val))) {
		pr_err("%d is not a supported value for %pfw '%s' element '%s'\n",
		       *val, fwnode, polarity_prop, mode_name);
		err = -EOPNOTSUPP;
	}

	return err;
}

/**
 * phy_get_rx_polarity - Get RX polarity for PHY differential lane
 * @fwnode: Pointer to the PHY's firmware node.
 * @mode_name: The name of the PHY mode to look up.
 * @supported: Bit mask of PHY_POL_NORMAL, PHY_POL_INVERT and PHY_POL_AUTO
 * @default_val: Default polarity value if property is missing
 * @val: Pointer to returned polarity.
 *
 * Return: zero on success, negative error on failure.
 */
int __must_check phy_get_rx_polarity(struct fwnode_handle *fwnode,
				     const char *mode_name,
				     unsigned int supported,
				     unsigned int default_val,
				     unsigned int *val)
{
	return phy_get_polarity_for_mode(fwnode, mode_name, supported,
					 default_val, "rx-polarity",
					 "rx-polarity-names", val);
}
EXPORT_SYMBOL_GPL(phy_get_rx_polarity);

/**
 * phy_get_tx_polarity - Get TX polarity for PHY differential lane
 * @fwnode: Pointer to the PHY's firmware node.
 * @mode_name: The name of the PHY mode to look up.
 * @supported: Bit mask of PHY_POL_NORMAL and PHY_POL_INVERT
 * @default_val: Default polarity value if property is missing
 * @val: Pointer to returned polarity.
 *
 * Return: zero on success, negative error on failure.
 */
int __must_check phy_get_tx_polarity(struct fwnode_handle *fwnode,
				     const char *mode_name, unsigned int supported,
				     unsigned int default_val, unsigned int *val)
{
	return phy_get_polarity_for_mode(fwnode, mode_name, supported,
					 default_val, "tx-polarity",
					 "tx-polarity-names", val);
}
EXPORT_SYMBOL_GPL(phy_get_tx_polarity);

/**
 * phy_get_manual_rx_polarity - Get manual RX polarity for PHY differential lane
 * @fwnode: Pointer to the PHY's firmware node.
 * @mode_name: The name of the PHY mode to look up.
 * @val: Pointer to returned polarity.
 *
 * Helper for PHYs which do not support protocols with automatic RX polarity
 * detection and correction.
 *
 * Return: zero on success, negative error on failure.
 */
int __must_check phy_get_manual_rx_polarity(struct fwnode_handle *fwnode,
					    const char *mode_name,
					    unsigned int *val)
{
	return phy_get_rx_polarity(fwnode, mode_name,
				   BIT(PHY_POL_NORMAL) | BIT(PHY_POL_INVERT),
				   PHY_POL_NORMAL, val);
}
EXPORT_SYMBOL_GPL(phy_get_manual_rx_polarity);

/**
 * phy_get_manual_tx_polarity - Get manual TX polarity for PHY differential lane
 * @fwnode: Pointer to the PHY's firmware node.
 * @mode_name: The name of the PHY mode to look up.
 * @val: Pointer to returned polarity.
 *
 * Helper for PHYs without any custom default value for the TX polarity.
 *
 * Return: zero on success, negative error on failure.
 */
int __must_check phy_get_manual_tx_polarity(struct fwnode_handle *fwnode,
					    const char *mode_name,
					    unsigned int *val)
{
	return phy_get_tx_polarity(fwnode, mode_name,
				   BIT(PHY_POL_NORMAL) | BIT(PHY_POL_INVERT),
				   PHY_POL_NORMAL, val);
}
EXPORT_SYMBOL_GPL(phy_get_manual_tx_polarity);
