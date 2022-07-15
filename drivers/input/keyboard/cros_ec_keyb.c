// SPDX-License-Identifier: GPL-2.0
// ChromeOS EC keyboard driver
//
// Copyright (C) 2012 Google, Inc.
//
// This driver uses the ChromeOS EC byte-level message-based protocol for
// communicating the keyboard state (which keys are pressed) from a keyboard EC
// to the AP over some bus (such as i2c, lpc, spi).  The EC does debouncing,
// but everything else (including deghosting) is done here.  The main
// motivation for this is to keep the EC firmware as simple as possible, since
// it cannot be easily upgraded and EC flash/IRAM space is relatively
// expensive.

#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/vivaldi-fmap.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysrq.h>
#include <linux/input/matrix_keypad.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>

#include <asm/unaligned.h>

/**
 * struct cros_ec_keyb - Structure representing EC keyboard device
 *
 * @rows: Number of rows in the keypad
 * @cols: Number of columns in the keypad
 * @row_shift: log2 or number of rows, rounded up
 * @keymap_data: Matrix keymap data used to convert to keyscan values
 * @ghost_filter: true to enable the matrix key-ghosting filter
 * @valid_keys: bitmap of existing keys for each matrix column
 * @old_kb_state: bitmap of keys pressed last scan
 * @dev: Device pointer
 * @ec: Top level ChromeOS device to use to talk to EC
 * @idev: The input device for the matrix keys.
 * @bs_idev: The input device for non-matrix buttons and switches (or NULL).
 * @notifier: interrupt event notifier for transport devices
 * @vdata: vivaldi function row data
 */
struct cros_ec_keyb {
	unsigned int rows;
	unsigned int cols;
	int row_shift;
	const struct matrix_keymap_data *keymap_data;
	bool ghost_filter;
	uint8_t *valid_keys;
	uint8_t *old_kb_state;

	struct device *dev;
	struct cros_ec_device *ec;

	struct input_dev *idev;
	struct input_dev *bs_idev;
	struct notifier_block notifier;

	struct vivaldi_data vdata;
};

/**
 * struct cros_ec_bs_map - Mapping between Linux keycodes and EC button/switch
 *	bitmap #defines
 *
 * @ev_type: The type of the input event to generate (e.g., EV_KEY).
 * @code: A linux keycode
 * @bit: A #define like EC_MKBP_POWER_BUTTON or EC_MKBP_LID_OPEN
 * @inverted: If the #define and EV_SW have opposite meanings, this is true.
 *            Only applicable to switches.
 */
struct cros_ec_bs_map {
	unsigned int ev_type;
	unsigned int code;
	u8 bit;
	bool inverted;
};

/* cros_ec_keyb_bs - Map EC button/switch #defines into kernel ones */
static const struct cros_ec_bs_map cros_ec_keyb_bs[] = {
	/* Buttons */
	{
		.ev_type	= EV_KEY,
		.code		= KEY_POWER,
		.bit		= EC_MKBP_POWER_BUTTON,
	},
	{
		.ev_type	= EV_KEY,
		.code		= KEY_VOLUMEUP,
		.bit		= EC_MKBP_VOL_UP,
	},
	{
		.ev_type	= EV_KEY,
		.code		= KEY_VOLUMEDOWN,
		.bit		= EC_MKBP_VOL_DOWN,
	},

	/* Switches */
	{
		.ev_type	= EV_SW,
		.code		= SW_LID,
		.bit		= EC_MKBP_LID_OPEN,
		.inverted	= true,
	},
	{
		.ev_type	= EV_SW,
		.code		= SW_TABLET_MODE,
		.bit		= EC_MKBP_TABLET_MODE,
	},
};

/*
 * Returns true when there is at least one combination of pressed keys that
 * results in ghosting.
 */
static bool cros_ec_keyb_has_ghosting(struct cros_ec_keyb *ckdev, uint8_t *buf)
{
	int col1, col2, buf1, buf2;
	struct device *dev = ckdev->dev;
	uint8_t *valid_keys = ckdev->valid_keys;

	/*
	 * Ghosting happens if for any pressed key X there are other keys
	 * pressed both in the same row and column of X as, for instance,
	 * in the following diagram:
	 *
	 * . . Y . g .
	 * . . . . . .
	 * . . . . . .
	 * . . X . Z .
	 *
	 * In this case only X, Y, and Z are pressed, but g appears to be
	 * pressed too (see Wikipedia).
	 */
	for (col1 = 0; col1 < ckdev->cols; col1++) {
		buf1 = buf[col1] & valid_keys[col1];
		for (col2 = col1 + 1; col2 < ckdev->cols; col2++) {
			buf2 = buf[col2] & valid_keys[col2];
			if (hweight8(buf1 & buf2) > 1) {
				dev_dbg(dev, "ghost found at: B[%02d]:0x%02x & B[%02d]:0x%02x",
					col1, buf1, col2, buf2);
				return true;
			}
		}
	}

	return false;
}


/*
 * Compares the new keyboard state to the old one and produces key
 * press/release events accordingly.  The keyboard state is 13 bytes (one byte
 * per column)
 */
static void cros_ec_keyb_process(struct cros_ec_keyb *ckdev,
			 uint8_t *kb_state, int len)
{
	struct input_dev *idev = ckdev->idev;
	int col, row;
	int new_state;
	int old_state;

	if (ckdev->ghost_filter && cros_ec_keyb_has_ghosting(ckdev, kb_state)) {
		/*
		 * Simple-minded solution: ignore this state. The obvious
		 * improvement is to only ignore changes to keys involved in
		 * the ghosting, but process the other changes.
		 */
		dev_dbg(ckdev->dev, "ghosting found\n");
		return;
	}

	for (col = 0; col < ckdev->cols; col++) {
		for (row = 0; row < ckdev->rows; row++) {
			int pos = MATRIX_SCAN_CODE(row, col, ckdev->row_shift);
			const unsigned short *keycodes = idev->keycode;

			new_state = kb_state[col] & (1 << row);
			old_state = ckdev->old_kb_state[col] & (1 << row);
			if (new_state != old_state) {
				dev_dbg(ckdev->dev,
					"changed: [r%d c%d]: byte %02x\n",
					row, col, new_state);

				input_event(idev, EV_MSC, MSC_SCAN, pos);
				input_report_key(idev, keycodes[pos],
						 new_state);
			}
		}
		ckdev->old_kb_state[col] = kb_state[col];
	}
	input_sync(ckdev->idev);
}

/**
 * cros_ec_keyb_report_bs - Report non-matrixed buttons or switches
 *
 * This takes a bitmap of buttons or switches from the EC and reports events,
 * syncing at the end.
 *
 * @ckdev: The keyboard device.
 * @ev_type: The input event type (e.g., EV_KEY).
 * @mask: A bitmap of buttons from the EC.
 */
static void cros_ec_keyb_report_bs(struct cros_ec_keyb *ckdev,
				   unsigned int ev_type, u32 mask)

{
	struct input_dev *idev = ckdev->bs_idev;
	int i;

	for (i = 0; i < ARRAY_SIZE(cros_ec_keyb_bs); i++) {
		const struct cros_ec_bs_map *map = &cros_ec_keyb_bs[i];

		if (map->ev_type != ev_type)
			continue;

		input_event(idev, ev_type, map->code,
			    !!(mask & BIT(map->bit)) ^ map->inverted);
	}
	input_sync(idev);
}

static int cros_ec_keyb_work(struct notifier_block *nb,
			     unsigned long queued_during_suspend, void *_notify)
{
	struct cros_ec_keyb *ckdev = container_of(nb, struct cros_ec_keyb,
						  notifier);
	u32 val;
	unsigned int ev_type;

	/*
	 * If not wake enabled, discard key state changes during
	 * suspend. Switches will be re-checked in
	 * cros_ec_keyb_resume() to be sure nothing is lost.
	 */
	if (queued_during_suspend && !device_may_wakeup(ckdev->dev))
		return NOTIFY_OK;

	switch (ckdev->ec->event_data.event_type) {
	case EC_MKBP_EVENT_KEY_MATRIX:
		pm_wakeup_event(ckdev->dev, 0);

		if (ckdev->ec->event_size != ckdev->cols) {
			dev_err(ckdev->dev,
				"Discarded incomplete key matrix event.\n");
			return NOTIFY_OK;
		}

		cros_ec_keyb_process(ckdev,
				     ckdev->ec->event_data.data.key_matrix,
				     ckdev->ec->event_size);
		break;

	case EC_MKBP_EVENT_SYSRQ:
		pm_wakeup_event(ckdev->dev, 0);

		val = get_unaligned_le32(&ckdev->ec->event_data.data.sysrq);
		dev_dbg(ckdev->dev, "sysrq code from EC: %#x\n", val);
		handle_sysrq(val);
		break;

	case EC_MKBP_EVENT_BUTTON:
	case EC_MKBP_EVENT_SWITCH:
		pm_wakeup_event(ckdev->dev, 0);

		if (ckdev->ec->event_data.event_type == EC_MKBP_EVENT_BUTTON) {
			val = get_unaligned_le32(
					&ckdev->ec->event_data.data.buttons);
			ev_type = EV_KEY;
		} else {
			val = get_unaligned_le32(
					&ckdev->ec->event_data.data.switches);
			ev_type = EV_SW;
		}
		cros_ec_keyb_report_bs(ckdev, ev_type, val);
		break;

	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

/*
 * Walks keycodes flipping bit in buffer COLUMNS deep where bit is ROW.  Used by
 * ghosting logic to ignore NULL or virtual keys.
 */
static void cros_ec_keyb_compute_valid_keys(struct cros_ec_keyb *ckdev)
{
	int row, col;
	int row_shift = ckdev->row_shift;
	unsigned short *keymap = ckdev->idev->keycode;
	unsigned short code;

	BUG_ON(ckdev->idev->keycodesize != sizeof(*keymap));

	for (col = 0; col < ckdev->cols; col++) {
		for (row = 0; row < ckdev->rows; row++) {
			code = keymap[MATRIX_SCAN_CODE(row, col, row_shift)];
			if (code && (code != KEY_BATTERY))
				ckdev->valid_keys[col] |= 1 << row;
		}
		dev_dbg(ckdev->dev, "valid_keys[%02d] = 0x%02x\n",
			col, ckdev->valid_keys[col]);
	}
}

/**
 * cros_ec_keyb_info - Wrap the EC command EC_CMD_MKBP_INFO
 *
 * This wraps the EC_CMD_MKBP_INFO, abstracting out all of the marshalling and
 * unmarshalling and different version nonsense into something simple.
 *
 * @ec_dev: The EC device
 * @info_type: Either EC_MKBP_INFO_SUPPORTED or EC_MKBP_INFO_CURRENT.
 * @event_type: Either EC_MKBP_EVENT_BUTTON or EC_MKBP_EVENT_SWITCH.  Actually
 *              in some cases this could be EC_MKBP_EVENT_KEY_MATRIX or
 *              EC_MKBP_EVENT_HOST_EVENT too but we don't use in this driver.
 * @result: Where we'll store the result; a union
 * @result_size: The size of the result.  Expected to be the size of one of
 *               the elements in the union.
 *
 * Returns 0 if no error or -error upon error.
 */
static int cros_ec_keyb_info(struct cros_ec_device *ec_dev,
			     enum ec_mkbp_info_type info_type,
			     enum ec_mkbp_event event_type,
			     union ec_response_get_next_data *result,
			     size_t result_size)
{
	struct ec_params_mkbp_info *params;
	struct cros_ec_command *msg;
	int ret;

	msg = kzalloc(sizeof(*msg) + max_t(size_t, result_size,
					   sizeof(*params)), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->command = EC_CMD_MKBP_INFO;
	msg->version = 1;
	msg->outsize = sizeof(*params);
	msg->insize = result_size;
	params = (struct ec_params_mkbp_info *)msg->data;
	params->info_type = info_type;
	params->event_type = event_type;

	ret = cros_ec_cmd_xfer_status(ec_dev, msg);
	if (ret == -ENOPROTOOPT) {
		/* With older ECs we just return 0 for everything */
		memset(result, 0, result_size);
		ret = 0;
	} else if (ret < 0) {
		dev_warn(ec_dev->dev, "Transfer error %d/%d: %d\n",
			 (int)info_type, (int)event_type, ret);
	} else if (ret != result_size) {
		dev_warn(ec_dev->dev, "Wrong size %d/%d: %d != %zu\n",
			 (int)info_type, (int)event_type,
			 ret, result_size);
		ret = -EPROTO;
	} else {
		memcpy(result, msg->data, result_size);
		ret = 0;
	}

	kfree(msg);

	return ret;
}

/**
 * cros_ec_keyb_query_switches - Query the state of switches and report
 *
 * This will ask the EC about the current state of switches and report to the
 * kernel.  Note that we don't query for buttons because they are more
 * transitory and we'll get an update on the next release / press.
 *
 * @ckdev: The keyboard device
 *
 * Returns 0 if no error or -error upon error.
 */
static int cros_ec_keyb_query_switches(struct cros_ec_keyb *ckdev)
{
	struct cros_ec_device *ec_dev = ckdev->ec;
	union ec_response_get_next_data event_data = {};
	int ret;

	ret = cros_ec_keyb_info(ec_dev, EC_MKBP_INFO_CURRENT,
				EC_MKBP_EVENT_SWITCH, &event_data,
				sizeof(event_data.switches));
	if (ret)
		return ret;

	cros_ec_keyb_report_bs(ckdev, EV_SW,
			       get_unaligned_le32(&event_data.switches));

	return 0;
}

/**
 * cros_ec_keyb_resume - Resume the keyboard
 *
 * We use the resume notification as a chance to query the EC for switches.
 *
 * @dev: The keyboard device
 *
 * Returns 0 if no error or -error upon error.
 */
static __maybe_unused int cros_ec_keyb_resume(struct device *dev)
{
	struct cros_ec_keyb *ckdev = dev_get_drvdata(dev);

	if (ckdev->bs_idev)
		return cros_ec_keyb_query_switches(ckdev);

	return 0;
}

/**
 * cros_ec_keyb_register_bs - Register non-matrix buttons/switches
 *
 * Handles all the bits of the keyboard driver related to non-matrix buttons
 * and switches, including asking the EC about which are present and telling
 * the kernel to expect them.
 *
 * If this device has no support for buttons and switches we'll return no error
 * but the ckdev->bs_idev will remain NULL when this function exits.
 *
 * @ckdev: The keyboard device
 * @expect_buttons_switches: Indicates that EC must report button and/or
 *   switch events
 *
 * Returns 0 if no error or -error upon error.
 */
static int cros_ec_keyb_register_bs(struct cros_ec_keyb *ckdev,
				    bool expect_buttons_switches)
{
	struct cros_ec_device *ec_dev = ckdev->ec;
	struct device *dev = ckdev->dev;
	struct input_dev *idev;
	union ec_response_get_next_data event_data = {};
	const char *phys;
	u32 buttons;
	u32 switches;
	int ret;
	int i;

	ret = cros_ec_keyb_info(ec_dev, EC_MKBP_INFO_SUPPORTED,
				EC_MKBP_EVENT_BUTTON, &event_data,
				sizeof(event_data.buttons));
	if (ret)
		return ret;
	buttons = get_unaligned_le32(&event_data.buttons);

	ret = cros_ec_keyb_info(ec_dev, EC_MKBP_INFO_SUPPORTED,
				EC_MKBP_EVENT_SWITCH, &event_data,
				sizeof(event_data.switches));
	if (ret)
		return ret;
	switches = get_unaligned_le32(&event_data.switches);

	if (!buttons && !switches)
		return expect_buttons_switches ? -EINVAL : 0;

	/*
	 * We call the non-matrix buttons/switches 'input1', if present.
	 * Allocate phys before input dev, to ensure correct tear-down
	 * ordering.
	 */
	phys = devm_kasprintf(dev, GFP_KERNEL, "%s/input1", ec_dev->phys_name);
	if (!phys)
		return -ENOMEM;

	idev = devm_input_allocate_device(dev);
	if (!idev)
		return -ENOMEM;

	idev->name = "cros_ec_buttons";
	idev->phys = phys;
	__set_bit(EV_REP, idev->evbit);

	idev->id.bustype = BUS_VIRTUAL;
	idev->id.version = 1;
	idev->id.product = 0;
	idev->dev.parent = dev;

	input_set_drvdata(idev, ckdev);
	ckdev->bs_idev = idev;

	for (i = 0; i < ARRAY_SIZE(cros_ec_keyb_bs); i++) {
		const struct cros_ec_bs_map *map = &cros_ec_keyb_bs[i];

		if ((map->ev_type == EV_KEY && (buttons & BIT(map->bit))) ||
		    (map->ev_type == EV_SW && (switches & BIT(map->bit))))
			input_set_capability(idev, map->ev_type, map->code);
	}

	ret = cros_ec_keyb_query_switches(ckdev);
	if (ret) {
		dev_err(dev, "cannot query switches\n");
		return ret;
	}

	ret = input_register_device(ckdev->bs_idev);
	if (ret) {
		dev_err(dev, "cannot register input device\n");
		return ret;
	}

	return 0;
}

/**
 * cros_ec_keyb_register_matrix - Register matrix keys
 *
 * Handles all the bits of the keyboard driver related to matrix keys.
 *
 * @ckdev: The keyboard device
 *
 * Returns 0 if no error or -error upon error.
 */
static int cros_ec_keyb_register_matrix(struct cros_ec_keyb *ckdev)
{
	struct cros_ec_device *ec_dev = ckdev->ec;
	struct device *dev = ckdev->dev;
	struct input_dev *idev;
	const char *phys;
	int err;
	struct property *prop;
	const __be32 *p;
	u32 *physmap;
	u32 key_pos;
	unsigned int row, col, scancode, n_physmap;

	err = matrix_keypad_parse_properties(dev, &ckdev->rows, &ckdev->cols);
	if (err)
		return err;

	ckdev->valid_keys = devm_kzalloc(dev, ckdev->cols, GFP_KERNEL);
	if (!ckdev->valid_keys)
		return -ENOMEM;

	ckdev->old_kb_state = devm_kzalloc(dev, ckdev->cols, GFP_KERNEL);
	if (!ckdev->old_kb_state)
		return -ENOMEM;

	/*
	 * We call the keyboard matrix 'input0'. Allocate phys before input
	 * dev, to ensure correct tear-down ordering.
	 */
	phys = devm_kasprintf(dev, GFP_KERNEL, "%s/input0", ec_dev->phys_name);
	if (!phys)
		return -ENOMEM;

	idev = devm_input_allocate_device(dev);
	if (!idev)
		return -ENOMEM;

	idev->name = CROS_EC_DEV_NAME;
	idev->phys = phys;
	__set_bit(EV_REP, idev->evbit);

	idev->id.bustype = BUS_VIRTUAL;
	idev->id.version = 1;
	idev->id.product = 0;
	idev->dev.parent = dev;

	ckdev->ghost_filter = of_property_read_bool(dev->of_node,
					"google,needs-ghost-filter");

	err = matrix_keypad_build_keymap(NULL, NULL, ckdev->rows, ckdev->cols,
					 NULL, idev);
	if (err) {
		dev_err(dev, "cannot build key matrix\n");
		return err;
	}

	ckdev->row_shift = get_count_order(ckdev->cols);

	input_set_capability(idev, EV_MSC, MSC_SCAN);
	input_set_drvdata(idev, ckdev);
	ckdev->idev = idev;
	cros_ec_keyb_compute_valid_keys(ckdev);

	physmap = ckdev->vdata.function_row_physmap;
	n_physmap = 0;
	of_property_for_each_u32(dev->of_node, "function-row-physmap",
				 prop, p, key_pos) {
		if (n_physmap == VIVALDI_MAX_FUNCTION_ROW_KEYS) {
			dev_warn(dev, "Only support up to %d top row keys\n",
				 VIVALDI_MAX_FUNCTION_ROW_KEYS);
			break;
		}
		row = KEY_ROW(key_pos);
		col = KEY_COL(key_pos);
		scancode = MATRIX_SCAN_CODE(row, col, ckdev->row_shift);
		physmap[n_physmap++] = scancode;
	}
	ckdev->vdata.num_function_row_keys = n_physmap;

	err = input_register_device(ckdev->idev);
	if (err) {
		dev_err(dev, "cannot register input device\n");
		return err;
	}

	return 0;
}

static ssize_t function_row_physmap_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	const struct cros_ec_keyb *ckdev = dev_get_drvdata(dev);
	const struct vivaldi_data *data = &ckdev->vdata;

	return vivaldi_function_row_physmap_show(data, buf);
}

static DEVICE_ATTR_RO(function_row_physmap);

static struct attribute *cros_ec_keyb_attrs[] = {
	&dev_attr_function_row_physmap.attr,
	NULL,
};

static umode_t cros_ec_keyb_attr_is_visible(struct kobject *kobj,
					    struct attribute *attr,
					    int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cros_ec_keyb *ckdev = dev_get_drvdata(dev);

	if (attr == &dev_attr_function_row_physmap.attr &&
	    !ckdev->vdata.num_function_row_keys)
		return 0;

	return attr->mode;
}

static const struct attribute_group cros_ec_keyb_attr_group = {
	.is_visible = cros_ec_keyb_attr_is_visible,
	.attrs = cros_ec_keyb_attrs,
};

static int cros_ec_keyb_probe(struct platform_device *pdev)
{
	struct cros_ec_device *ec = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct cros_ec_keyb *ckdev;
	bool buttons_switches_only = device_get_match_data(dev);
	int err;

	if (!dev->of_node)
		return -ENODEV;

	ckdev = devm_kzalloc(dev, sizeof(*ckdev), GFP_KERNEL);
	if (!ckdev)
		return -ENOMEM;

	ckdev->ec = ec;
	ckdev->dev = dev;
	dev_set_drvdata(dev, ckdev);

	if (!buttons_switches_only) {
		err = cros_ec_keyb_register_matrix(ckdev);
		if (err) {
			dev_err(dev, "cannot register matrix inputs: %d\n",
				err);
			return err;
		}
	}

	err = cros_ec_keyb_register_bs(ckdev, buttons_switches_only);
	if (err) {
		dev_err(dev, "cannot register non-matrix inputs: %d\n", err);
		return err;
	}

	err = devm_device_add_group(dev, &cros_ec_keyb_attr_group);
	if (err) {
		dev_err(dev, "failed to create attributes: %d\n", err);
		return err;
	}

	ckdev->notifier.notifier_call = cros_ec_keyb_work;
	err = blocking_notifier_chain_register(&ckdev->ec->event_notifier,
					       &ckdev->notifier);
	if (err) {
		dev_err(dev, "cannot register notifier: %d\n", err);
		return err;
	}

	device_init_wakeup(ckdev->dev, true);
	return 0;
}

static int cros_ec_keyb_remove(struct platform_device *pdev)
{
	struct cros_ec_keyb *ckdev = dev_get_drvdata(&pdev->dev);

	blocking_notifier_chain_unregister(&ckdev->ec->event_notifier,
					   &ckdev->notifier);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id cros_ec_keyb_of_match[] = {
	{ .compatible = "google,cros-ec-keyb" },
	{ .compatible = "google,cros-ec-keyb-switches", .data = (void *)true },
	{}
};
MODULE_DEVICE_TABLE(of, cros_ec_keyb_of_match);
#endif

static SIMPLE_DEV_PM_OPS(cros_ec_keyb_pm_ops, NULL, cros_ec_keyb_resume);

static struct platform_driver cros_ec_keyb_driver = {
	.probe = cros_ec_keyb_probe,
	.remove = cros_ec_keyb_remove,
	.driver = {
		.name = "cros-ec-keyb",
		.of_match_table = of_match_ptr(cros_ec_keyb_of_match),
		.pm = &cros_ec_keyb_pm_ops,
	},
};

module_platform_driver(cros_ec_keyb_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ChromeOS EC keyboard driver");
MODULE_ALIAS("platform:cros-ec-keyb");
