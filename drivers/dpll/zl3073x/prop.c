// SPDX-License-Identifier: GPL-2.0-only

#include <linux/array_size.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fwnode.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "core.h"
#include "prop.h"

/**
 * zl3073x_pin_check_freq - verify frequency for given pin
 * @zldev: pointer to zl3073x device
 * @dir: pin direction
 * @id: pin index
 * @freq: frequency to check
 *
 * The function checks the given frequency is valid for the device. For input
 * pins it checks that the frequency can be factorized using supported base
 * frequencies. For output pins it checks that the frequency divides connected
 * synth frequency without remainder.
 *
 * Return: true if the frequency is valid, false if not.
 */
static bool
zl3073x_pin_check_freq(struct zl3073x_dev *zldev, enum dpll_pin_direction dir,
		       u8 id, u64 freq)
{
	if (freq > U32_MAX)
		goto err_inv_freq;

	if (dir == DPLL_PIN_DIRECTION_INPUT) {
		int rc;

		/* Check if the frequency can be factorized */
		rc = zl3073x_ref_freq_factorize(freq, NULL, NULL);
		if (rc)
			goto err_inv_freq;
	} else {
		u32 synth_freq;
		u8 out, synth;

		/* Get output pin synthesizer */
		out = zl3073x_output_pin_out_get(id);
		synth = zl3073x_out_synth_get(zldev, out);

		/* Get synth frequency */
		synth_freq = zl3073x_synth_freq_get(zldev, synth);

		/* Check the frequency divides synth frequency */
		if (synth_freq % (u32)freq)
			goto err_inv_freq;
	}

	return true;

err_inv_freq:
	dev_warn(zldev->dev,
		 "Unsupported frequency %llu Hz in firmware node\n", freq);

	return false;
}

/**
 * zl3073x_prop_pin_package_label_set - get package label for the pin
 * @zldev: pointer to zl3073x device
 * @props: pointer to pin properties
 * @dir: pin direction
 * @id: pin index
 *
 * Generates package label string and stores it into pin properties structure.
 *
 * Possible formats:
 * REF<n> - differential input reference
 * REF<n>P & REF<n>N - single-ended input reference (P or N pin)
 * OUT<n> - differential output
 * OUT<n>P & OUT<n>N - single-ended output (P or N pin)
 */
static void
zl3073x_prop_pin_package_label_set(struct zl3073x_dev *zldev,
				   struct zl3073x_pin_props *props,
				   enum dpll_pin_direction dir, u8 id)
{
	const char *prefix, *suffix;
	bool is_diff;

	if (dir == DPLL_PIN_DIRECTION_INPUT) {
		u8 ref;

		prefix = "REF";
		ref = zl3073x_input_pin_ref_get(id);
		is_diff = zl3073x_ref_is_diff(zldev, ref);
	} else {
		u8 out;

		prefix = "OUT";
		out = zl3073x_output_pin_out_get(id);
		is_diff = zl3073x_out_is_diff(zldev, out);
	}

	if (!is_diff)
		suffix = zl3073x_is_p_pin(id) ? "P" : "N";
	else
		suffix = ""; /* No suffix for differential one */

	snprintf(props->package_label, sizeof(props->package_label), "%s%u%s",
		 prefix, id / 2, suffix);

	/* Set package_label pointer in DPLL core properties to generated
	 * string.
	 */
	props->dpll_props.package_label = props->package_label;
}

/**
 * zl3073x_prop_pin_fwnode_get - get fwnode for given pin
 * @zldev: pointer to zl3073x device
 * @props: pointer to pin properties
 * @dir: pin direction
 * @id: pin index
 *
 * Return: 0 on success, -ENOENT if the firmware node does not exist
 */
static int
zl3073x_prop_pin_fwnode_get(struct zl3073x_dev *zldev,
			    struct zl3073x_pin_props *props,
			    enum dpll_pin_direction dir, u8 id)
{
	struct fwnode_handle *pins_node, *pin_node;
	const char *node_name;

	if (dir == DPLL_PIN_DIRECTION_INPUT)
		node_name = "input-pins";
	else
		node_name = "output-pins";

	/* Get node containing input or output pins */
	pins_node = device_get_named_child_node(zldev->dev, node_name);
	if (!pins_node) {
		dev_dbg(zldev->dev, "'%s' sub-node is missing\n", node_name);
		return -ENOENT;
	}

	/* Enumerate child pin nodes and find the requested one */
	fwnode_for_each_child_node(pins_node, pin_node) {
		u32 reg;

		if (fwnode_property_read_u32(pin_node, "reg", &reg))
			continue;

		if (id == reg)
			break;
	}

	/* Release pin parent node */
	fwnode_handle_put(pins_node);

	/* Save found node */
	props->fwnode = pin_node;

	dev_dbg(zldev->dev, "Firmware node for %s %sfound\n",
		props->package_label, pin_node ? "" : "NOT ");

	return pin_node ? 0 : -ENOENT;
}

/**
 * zl3073x_pin_props_get - get pin properties
 * @zldev: pointer to zl3073x device
 * @dir: pin direction
 * @index: pin index
 *
 * The function looks for firmware node for the given pin if it is provided
 * by the system firmware (DT or ACPI), allocates pin properties structure,
 * generates package label string according pin type and optionally fetches
 * board label, connection type, supported frequencies and esync capability
 * from the firmware node if it does exist.
 *
 * Pointer that is returned by this function should be freed using
 * @zl3073x_pin_props_put().
 *
 * Return:
 * * pointer to allocated pin properties structure on success
 * * error pointer in case of error
 */
struct zl3073x_pin_props *zl3073x_pin_props_get(struct zl3073x_dev *zldev,
						enum dpll_pin_direction dir,
						u8 index)
{
	struct dpll_pin_frequency *ranges;
	struct zl3073x_pin_props *props;
	int i, j, num_freqs, rc;
	const char *type;
	u64 *freqs;

	props = kzalloc(sizeof(*props), GFP_KERNEL);
	if (!props)
		return ERR_PTR(-ENOMEM);

	/* Set default pin type and capabilities */
	if (dir == DPLL_PIN_DIRECTION_INPUT) {
		props->dpll_props.type = DPLL_PIN_TYPE_EXT;
		props->dpll_props.capabilities =
			DPLL_PIN_CAPABILITIES_PRIORITY_CAN_CHANGE |
			DPLL_PIN_CAPABILITIES_STATE_CAN_CHANGE;
	} else {
		props->dpll_props.type = DPLL_PIN_TYPE_GNSS;
	}

	props->dpll_props.phase_range.min = S32_MIN;
	props->dpll_props.phase_range.max = S32_MAX;

	zl3073x_prop_pin_package_label_set(zldev, props, dir, index);

	/* Get firmware node for the given pin */
	rc = zl3073x_prop_pin_fwnode_get(zldev, props, dir, index);
	if (rc)
		return props; /* Return if it does not exist */

	/* Look for label property and store the value as board label */
	fwnode_property_read_string(props->fwnode, "label",
				    &props->dpll_props.board_label);

	/* Look for pin type property and translate its value to DPLL
	 * pin type enum if it is present.
	 */
	if (!fwnode_property_read_string(props->fwnode, "connection-type",
					 &type)) {
		if (!strcmp(type, "ext"))
			props->dpll_props.type = DPLL_PIN_TYPE_EXT;
		else if (!strcmp(type, "gnss"))
			props->dpll_props.type = DPLL_PIN_TYPE_GNSS;
		else if (!strcmp(type, "int"))
			props->dpll_props.type = DPLL_PIN_TYPE_INT_OSCILLATOR;
		else if (!strcmp(type, "synce"))
			props->dpll_props.type = DPLL_PIN_TYPE_SYNCE_ETH_PORT;
		else
			dev_warn(zldev->dev,
				 "Unknown or unsupported pin type '%s'\n",
				 type);
	}

	/* Check if the pin supports embedded sync control */
	props->esync_control = fwnode_property_read_bool(props->fwnode,
							 "esync-control");

	/* Read supported frequencies property if it is specified */
	num_freqs = fwnode_property_count_u64(props->fwnode,
					      "supported-frequencies-hz");
	if (num_freqs <= 0)
		/* Return if the property does not exist or number is 0 */
		return props;

	/* The firmware node specifies list of supported frequencies while
	 * DPLL core pin properties requires list of frequency ranges.
	 * So read the frequency list into temporary array.
	 */
	freqs = kcalloc(num_freqs, sizeof(*freqs), GFP_KERNEL);
	if (!freqs) {
		rc = -ENOMEM;
		goto err_alloc_freqs;
	}

	/* Read frequencies list from firmware node */
	fwnode_property_read_u64_array(props->fwnode,
				       "supported-frequencies-hz", freqs,
				       num_freqs);

	/* Allocate frequency ranges list and fill it */
	ranges = kcalloc(num_freqs, sizeof(*ranges), GFP_KERNEL);
	if (!ranges) {
		rc = -ENOMEM;
		goto err_alloc_ranges;
	}

	/* Convert list of frequencies to list of frequency ranges but
	 * filter-out frequencies that are not representable by device
	 */
	for (i = 0, j = 0; i < num_freqs; i++) {
		struct dpll_pin_frequency freq = DPLL_PIN_FREQUENCY(freqs[i]);

		if (zl3073x_pin_check_freq(zldev, dir, index, freqs[i])) {
			ranges[j] = freq;
			j++;
		}
	}

	/* Save number of freq ranges and pointer to them into pin properties */
	props->dpll_props.freq_supported = ranges;
	props->dpll_props.freq_supported_num = j;

	/* Free temporary array */
	kfree(freqs);

	return props;

err_alloc_ranges:
	kfree(freqs);
err_alloc_freqs:
	fwnode_handle_put(props->fwnode);
	kfree(props);

	return ERR_PTR(rc);
}

/**
 * zl3073x_pin_props_put - release pin properties
 * @props: pin properties to free
 *
 * The function deallocates given pin properties structure.
 */
void zl3073x_pin_props_put(struct zl3073x_pin_props *props)
{
	/* Free supported frequency ranges list if it is present */
	kfree(props->dpll_props.freq_supported);

	/* Put firmware handle if it is present */
	if (props->fwnode)
		fwnode_handle_put(props->fwnode);

	kfree(props);
}

/**
 * zl3073x_prop_dpll_type_get - get DPLL channel type
 * @zldev: pointer to zl3073x device
 * @index: DPLL channel index
 *
 * Return: DPLL type for given DPLL channel
 */
enum dpll_type
zl3073x_prop_dpll_type_get(struct zl3073x_dev *zldev, u8 index)
{
	const char *types[ZL3073X_MAX_CHANNELS];
	int count;

	/* Read dpll types property from firmware */
	count = device_property_read_string_array(zldev->dev, "dpll-types",
						  types, ARRAY_SIZE(types));

	/* Return default if property or entry for given channel is missing */
	if (index >= count)
		return DPLL_TYPE_PPS;

	if (!strcmp(types[index], "pps"))
		return DPLL_TYPE_PPS;
	else if (!strcmp(types[index], "eec"))
		return DPLL_TYPE_EEC;

	dev_info(zldev->dev, "Unknown DPLL type '%s', using default\n",
		 types[index]);

	return DPLL_TYPE_PPS; /* Default */
}
