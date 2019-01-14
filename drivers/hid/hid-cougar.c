// SPDX-License-Identifier: GPL-2.0+
/*
 *  HID driver for Cougar 500k Gaming Keyboard
 *
 *  Copyright (c) 2018 Daniel M. Lambea <dmlambea@gmail.com>
 */

#include <linux/hid.h>
#include <linux/module.h>
#include <linux/printk.h>

#include "hid-ids.h"

MODULE_AUTHOR("Daniel M. Lambea <dmlambea@gmail.com>");
MODULE_DESCRIPTION("Cougar 500k Gaming Keyboard");
MODULE_LICENSE("GPL");
MODULE_INFO(key_mappings, "G1-G6 are mapped to F13-F18");

static bool g6_is_space = true;
MODULE_PARM_DESC(g6_is_space,
	"If true, G6 programmable key sends SPACE instead of F18 (default=true)");

#define COUGAR_VENDOR_USAGE	0xff00ff00

#define COUGAR_FIELD_CODE	1
#define COUGAR_FIELD_ACTION	2

#define COUGAR_KEY_G1		0x83
#define COUGAR_KEY_G2		0x84
#define COUGAR_KEY_G3		0x85
#define COUGAR_KEY_G4		0x86
#define COUGAR_KEY_G5		0x87
#define COUGAR_KEY_G6		0x78
#define COUGAR_KEY_FN		0x0d
#define COUGAR_KEY_MR		0x6f
#define COUGAR_KEY_M1		0x80
#define COUGAR_KEY_M2		0x81
#define COUGAR_KEY_M3		0x82
#define COUGAR_KEY_LEDS		0x67
#define COUGAR_KEY_LOCK		0x6e


/* Default key mappings. The special key COUGAR_KEY_G6 is defined first
 * because it is more frequent to use the spacebar rather than any other
 * special keys. Depending on the value of the parameter 'g6_is_space',
 * the mapping will be updated in the probe function.
 */
static unsigned char cougar_mapping[][2] = {
	{ COUGAR_KEY_G6,   KEY_SPACE },
	{ COUGAR_KEY_G1,   KEY_F13 },
	{ COUGAR_KEY_G2,   KEY_F14 },
	{ COUGAR_KEY_G3,   KEY_F15 },
	{ COUGAR_KEY_G4,   KEY_F16 },
	{ COUGAR_KEY_G5,   KEY_F17 },
	{ COUGAR_KEY_LOCK, KEY_SCREENLOCK },
/* The following keys are handled by the hardware itself, so no special
 * treatment is required:
	{ COUGAR_KEY_FN, KEY_RESERVED },
	{ COUGAR_KEY_MR, KEY_RESERVED },
	{ COUGAR_KEY_M1, KEY_RESERVED },
	{ COUGAR_KEY_M2, KEY_RESERVED },
	{ COUGAR_KEY_M3, KEY_RESERVED },
	{ COUGAR_KEY_LEDS, KEY_RESERVED },
*/
	{ 0, 0 },
};

struct cougar_shared {
	struct list_head list;
	struct kref kref;
	bool enabled;
	struct hid_device *dev;
	struct input_dev *input;
};

struct cougar {
	bool special_intf;
	struct cougar_shared *shared;
};

static LIST_HEAD(cougar_udev_list);
static DEFINE_MUTEX(cougar_udev_list_lock);

/**
 * cougar_fix_g6_mapping - configure the mapping for key G6/Spacebar
 */
static void cougar_fix_g6_mapping(void)
{
	int i;

	for (i = 0; cougar_mapping[i][0]; i++) {
		if (cougar_mapping[i][0] == COUGAR_KEY_G6) {
			cougar_mapping[i][1] =
				g6_is_space ? KEY_SPACE : KEY_F18;
			pr_info("cougar: G6 mapped to %s\n",
				g6_is_space ? "space" : "F18");
			return;
		}
	}
	pr_warn("cougar: no mappings defined for G6/spacebar");
}

/*
 * Constant-friendly rdesc fixup for mouse interface
 */
static __u8 *cougar_report_fixup(struct hid_device *hdev, __u8 *rdesc,
				 unsigned int *rsize)
{
	if (rdesc[2] == 0x09 && rdesc[3] == 0x02 &&
	    (rdesc[115] | rdesc[116] << 8) >= HID_MAX_USAGES) {
		hid_info(hdev,
			"usage count exceeds max: fixing up report descriptor\n");
		rdesc[115] = ((HID_MAX_USAGES-1) & 0xff);
		rdesc[116] = ((HID_MAX_USAGES-1) >> 8);
	}
	return rdesc;
}

static struct cougar_shared *cougar_get_shared_data(struct hid_device *hdev)
{
	struct cougar_shared *shared;

	/* Try to find an already-probed interface from the same device */
	list_for_each_entry(shared, &cougar_udev_list, list) {
		if (hid_compare_device_paths(hdev, shared->dev, '/')) {
			kref_get(&shared->kref);
			return shared;
		}
	}
	return NULL;
}

static void cougar_release_shared_data(struct kref *kref)
{
	struct cougar_shared *shared = container_of(kref,
						    struct cougar_shared, kref);

	mutex_lock(&cougar_udev_list_lock);
	list_del(&shared->list);
	mutex_unlock(&cougar_udev_list_lock);

	kfree(shared);
}

static void cougar_remove_shared_data(void *resource)
{
	struct cougar *cougar = resource;

	if (cougar->shared) {
		kref_put(&cougar->shared->kref, cougar_release_shared_data);
		cougar->shared = NULL;
	}
}

/*
 * Bind the device group's shared data to this cougar struct.
 * If no shared data exists for this group, create and initialize it.
 */
static int cougar_bind_shared_data(struct hid_device *hdev,
				   struct cougar *cougar)
{
	struct cougar_shared *shared;
	int error = 0;

	mutex_lock(&cougar_udev_list_lock);

	shared = cougar_get_shared_data(hdev);
	if (!shared) {
		shared = kzalloc(sizeof(*shared), GFP_KERNEL);
		if (!shared) {
			error = -ENOMEM;
			goto out;
		}

		kref_init(&shared->kref);
		shared->dev = hdev;
		list_add_tail(&shared->list, &cougar_udev_list);
	}

	cougar->shared = shared;

	error = devm_add_action(&hdev->dev, cougar_remove_shared_data, cougar);
	if (error) {
		mutex_unlock(&cougar_udev_list_lock);
		cougar_remove_shared_data(cougar);
		return error;
	}

out:
	mutex_unlock(&cougar_udev_list_lock);
	return error;
}

static int cougar_probe(struct hid_device *hdev,
			const struct hid_device_id *id)
{
	struct cougar *cougar;
	struct hid_input *next, *hidinput = NULL;
	unsigned int connect_mask;
	int error;

	cougar = devm_kzalloc(&hdev->dev, sizeof(*cougar), GFP_KERNEL);
	if (!cougar)
		return -ENOMEM;
	hid_set_drvdata(hdev, cougar);

	error = hid_parse(hdev);
	if (error) {
		hid_err(hdev, "parse failed\n");
		goto fail;
	}

	if (hdev->collection->usage == COUGAR_VENDOR_USAGE) {
		cougar->special_intf = true;
		connect_mask = HID_CONNECT_HIDRAW;
	} else
		connect_mask = HID_CONNECT_DEFAULT;

	error = hid_hw_start(hdev, connect_mask);
	if (error) {
		hid_err(hdev, "hw start failed\n");
		goto fail;
	}

	error = cougar_bind_shared_data(hdev, cougar);
	if (error)
		goto fail_stop_and_cleanup;

	/* The custom vendor interface will use the hid_input registered
	 * for the keyboard interface, in order to send translated key codes
	 * to it.
	 */
	if (hdev->collection->usage == HID_GD_KEYBOARD) {
		list_for_each_entry_safe(hidinput, next, &hdev->inputs, list) {
			if (hidinput->registered && hidinput->input != NULL) {
				cougar->shared->input = hidinput->input;
				cougar->shared->enabled = true;
				break;
			}
		}
	} else if (hdev->collection->usage == COUGAR_VENDOR_USAGE) {
		/* Preinit the mapping table */
		cougar_fix_g6_mapping();
		error = hid_hw_open(hdev);
		if (error)
			goto fail_stop_and_cleanup;
	}
	return 0;

fail_stop_and_cleanup:
	hid_hw_stop(hdev);
fail:
	hid_set_drvdata(hdev, NULL);
	return error;
}

/*
 * Convert events from vendor intf to input key events
 */
static int cougar_raw_event(struct hid_device *hdev, struct hid_report *report,
			    u8 *data, int size)
{
	struct cougar *cougar;
	struct cougar_shared *shared;
	unsigned char code, action;
	int i;

	cougar = hid_get_drvdata(hdev);
	shared = cougar->shared;
	if (!cougar->special_intf || !shared)
		return 0;

	if (!shared->enabled || !shared->input)
		return -EPERM;

	code = data[COUGAR_FIELD_CODE];
	action = data[COUGAR_FIELD_ACTION];
	for (i = 0; cougar_mapping[i][0]; i++) {
		if (code == cougar_mapping[i][0]) {
			input_event(shared->input, EV_KEY,
				    cougar_mapping[i][1], action);
			input_sync(shared->input);
			return -EPERM;
		}
	}
	/* Avoid warnings on the same unmapped key twice */
	if (action != 0)
		hid_warn(hdev, "unmapped special key code %0x: ignoring\n", code);
	return -EPERM;
}

static void cougar_remove(struct hid_device *hdev)
{
	struct cougar *cougar = hid_get_drvdata(hdev);

	if (cougar) {
		/* Stop the vendor intf to process more events */
		if (cougar->shared)
			cougar->shared->enabled = false;
		if (cougar->special_intf)
			hid_hw_close(hdev);
	}
	hid_hw_stop(hdev);
}

static int cougar_param_set_g6_is_space(const char *val,
					const struct kernel_param *kp)
{
	int ret;

	ret = param_set_bool(val, kp);
	if (ret)
		return ret;

	cougar_fix_g6_mapping();

	return 0;
}

static const struct kernel_param_ops cougar_g6_is_space_ops = {
	.set	= cougar_param_set_g6_is_space,
	.get	= param_get_bool,
};
module_param_cb(g6_is_space, &cougar_g6_is_space_ops, &g6_is_space, 0644);

static struct hid_device_id cougar_id_table[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SOLID_YEAR,
			 USB_DEVICE_ID_COUGAR_500K_GAMING_KEYBOARD) },
	{}
};
MODULE_DEVICE_TABLE(hid, cougar_id_table);

static struct hid_driver cougar_driver = {
	.name			= "cougar",
	.id_table		= cougar_id_table,
	.report_fixup		= cougar_report_fixup,
	.probe			= cougar_probe,
	.remove			= cougar_remove,
	.raw_event		= cougar_raw_event,
};

module_hid_driver(cougar_driver);
