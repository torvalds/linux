// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Steelseries devices
 *
 *  Copyright (c) 2013 Simon Wood
 */

/*
 */

#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/leds.h>
#include <linux/led-class-multicolor.h>
#include <linux/slab.h>

#include "hid-ids.h"

#define STEELSERIES_SRWS1		BIT(0)
#define STEELSERIES_MSI_RGB		BIT(1)

#define STEELSERIES_MSI_RGB_WVALUE 0x0300 /* Feature report, ID 0 */
#define STEELSERIES_MSI_RGB_REPORT_LEN 524
#define STEELSERIES_MSI_RGB_OPCODE 0x0c
#define STEELSERIES_MSI_RGB_KLC_MODE 0x66
#define STEELSERIES_MSI_RGB_ALC_MODE 0x06

#define STEELSERIES_HAS_LEDS_MULTICOLOR \
	(IS_BUILTIN(CONFIG_LEDS_CLASS_MULTICOLOR) || \
	 (IS_MODULE(CONFIG_LEDS_CLASS_MULTICOLOR) && IS_MODULE(CONFIG_HID_STEELSERIES)))

struct steelseries_device {
	struct hid_device *hdev;
	unsigned long quirks;

#if STEELSERIES_HAS_LEDS_MULTICOLOR
	struct led_classdev_mc mc_cdev;
	struct mc_subled subled_info[3];
	struct mutex rgb_lock; /* protects rgb_buf */
	u8 *rgb_buf;
#endif
};

#if IS_BUILTIN(CONFIG_LEDS_CLASS) || \
    (IS_MODULE(CONFIG_LEDS_CLASS) && IS_MODULE(CONFIG_HID_STEELSERIES))
#define SRWS1_NUMBER_LEDS 15
struct steelseries_srws1_data {
	__u16 led_state;
	/* the last element is used for setting all leds simultaneously */
	struct led_classdev *led[SRWS1_NUMBER_LEDS + 1];
};
#endif

/* Fixed report descriptor for Steelseries SRW-S1 wheel controller
 *
 * The original descriptor hides the sensitivity and assists dials
 * a custom vendor usage page. This inserts a patch to make them
 * appear in the 'Generic Desktop' usage.
 */

static const __u8 steelseries_srws1_rdesc_fixed[] = {
0x05, 0x01,         /*  Usage Page (Desktop)                */
0x09, 0x08,         /*  Usage (MultiAxis), Changed          */
0xA1, 0x01,         /*  Collection (Application),           */
0xA1, 0x02,         /*      Collection (Logical),           */
0x95, 0x01,         /*          Report Count (1),           */
0x05, 0x01,         /* Changed  Usage Page (Desktop),       */
0x09, 0x30,         /* Changed  Usage (X),                  */
0x16, 0xF8, 0xF8,   /*          Logical Minimum (-1800),    */
0x26, 0x08, 0x07,   /*          Logical Maximum (1800),     */
0x65, 0x14,         /*          Unit (Degrees),             */
0x55, 0x0F,         /*          Unit Exponent (15),         */
0x75, 0x10,         /*          Report Size (16),           */
0x81, 0x02,         /*          Input (Variable),           */
0x09, 0x31,         /* Changed  Usage (Y),                  */
0x15, 0x00,         /*          Logical Minimum (0),        */
0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
0x75, 0x0C,         /*          Report Size (12),           */
0x81, 0x02,         /*          Input (Variable),           */
0x09, 0x32,         /* Changed  Usage (Z),                  */
0x15, 0x00,         /*          Logical Minimum (0),        */
0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
0x75, 0x0C,         /*          Report Size (12),           */
0x81, 0x02,         /*          Input (Variable),           */
0x05, 0x01,         /*          Usage Page (Desktop),       */
0x09, 0x39,         /*          Usage (Hat Switch),         */
0x25, 0x07,         /*          Logical Maximum (7),        */
0x35, 0x00,         /*          Physical Minimum (0),       */
0x46, 0x3B, 0x01,   /*          Physical Maximum (315),     */
0x65, 0x14,         /*          Unit (Degrees),             */
0x75, 0x04,         /*          Report Size (4),            */
0x95, 0x01,         /*          Report Count (1),           */
0x81, 0x02,         /*          Input (Variable),           */
0x25, 0x01,         /*          Logical Maximum (1),        */
0x45, 0x01,         /*          Physical Maximum (1),       */
0x65, 0x00,         /*          Unit,                       */
0x75, 0x01,         /*          Report Size (1),            */
0x95, 0x03,         /*          Report Count (3),           */
0x81, 0x01,         /*          Input (Constant),           */
0x05, 0x09,         /*          Usage Page (Button),        */
0x19, 0x01,         /*          Usage Minimum (01h),        */
0x29, 0x11,         /*          Usage Maximum (11h),        */
0x95, 0x11,         /*          Report Count (17),          */
0x81, 0x02,         /*          Input (Variable),           */
                    /*   ---- Dial patch starts here ----   */
0x05, 0x01,         /*          Usage Page (Desktop),       */
0x09, 0x33,         /*          Usage (RX),                 */
0x75, 0x04,         /*          Report Size (4),            */
0x95, 0x02,         /*          Report Count (2),           */
0x15, 0x00,         /*          Logical Minimum (0),        */
0x25, 0x0b,         /*          Logical Maximum (b),        */
0x81, 0x02,         /*          Input (Variable),           */
0x09, 0x35,         /*          Usage (RZ),                 */
0x75, 0x04,         /*          Report Size (4),            */
0x95, 0x01,         /*          Report Count (1),           */
0x25, 0x03,         /*          Logical Maximum (3),        */
0x81, 0x02,         /*          Input (Variable),           */
                    /*    ---- Dial patch ends here ----    */
0x06, 0x00, 0xFF,   /*          Usage Page (FF00h),         */
0x09, 0x01,         /*          Usage (01h),                */
0x75, 0x04,         /* Changed  Report Size (4),            */
0x95, 0x0D,         /* Changed  Report Count (13),          */
0x81, 0x02,         /*          Input (Variable),           */
0xC0,               /*      End Collection,                 */
0xA1, 0x02,         /*      Collection (Logical),           */
0x09, 0x02,         /*          Usage (02h),                */
0x75, 0x08,         /*          Report Size (8),            */
0x95, 0x10,         /*          Report Count (16),          */
0x91, 0x02,         /*          Output (Variable),          */
0xC0,               /*      End Collection,                 */
0xC0                /*  End Collection                      */
};

#if IS_BUILTIN(CONFIG_LEDS_CLASS) || \
    (IS_MODULE(CONFIG_LEDS_CLASS) && IS_MODULE(CONFIG_HID_STEELSERIES))
static void steelseries_srws1_set_leds(struct hid_device *hdev, __u16 leds)
{
	struct list_head *report_list = &hdev->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next, struct hid_report, list);
	__s32 *value = report->field[0]->value;

	value[0] = 0x40;
	value[1] = leds & 0xFF;
	value[2] = leds >> 8;
	value[3] = 0x00;
	value[4] = 0x00;
	value[5] = 0x00;
	value[6] = 0x00;
	value[7] = 0x00;
	value[8] = 0x00;
	value[9] = 0x00;
	value[10] = 0x00;
	value[11] = 0x00;
	value[12] = 0x00;
	value[13] = 0x00;
	value[14] = 0x00;
	value[15] = 0x00;

	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);

	/* Note: LED change does not show on device until the device is read/polled */
}

static void steelseries_srws1_led_all_set_brightness(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hid = to_hid_device(dev);
	struct steelseries_srws1_data *drv_data = hid_get_drvdata(hid);

	if (!drv_data) {
		hid_err(hid, "Device data not found.");
		return;
	}

	if (value == LED_OFF)
		drv_data->led_state = 0;
	else
		drv_data->led_state = (1 << (SRWS1_NUMBER_LEDS + 1)) - 1;

	steelseries_srws1_set_leds(hid, drv_data->led_state);
}

static enum led_brightness steelseries_srws1_led_all_get_brightness(struct led_classdev *led_cdev)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hid = to_hid_device(dev);
	struct steelseries_srws1_data *drv_data;

	drv_data = hid_get_drvdata(hid);

	if (!drv_data) {
		hid_err(hid, "Device data not found.");
		return LED_OFF;
	}

	return (drv_data->led_state >> SRWS1_NUMBER_LEDS) ? LED_FULL : LED_OFF;
}

static void steelseries_srws1_led_set_brightness(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hid = to_hid_device(dev);
	struct steelseries_srws1_data *drv_data = hid_get_drvdata(hid);
	int i, state = 0;

	if (!drv_data) {
		hid_err(hid, "Device data not found.");
		return;
	}

	for (i = 0; i < SRWS1_NUMBER_LEDS; i++) {
		if (led_cdev != drv_data->led[i])
			continue;

		state = (drv_data->led_state >> i) & 1;
		if (value == LED_OFF && state) {
			drv_data->led_state &= ~(1 << i);
			steelseries_srws1_set_leds(hid, drv_data->led_state);
		} else if (value != LED_OFF && !state) {
			drv_data->led_state |= 1 << i;
			steelseries_srws1_set_leds(hid, drv_data->led_state);
		}
		break;
	}
}

static enum led_brightness steelseries_srws1_led_get_brightness(struct led_classdev *led_cdev)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hid = to_hid_device(dev);
	struct steelseries_srws1_data *drv_data;
	int i, value = 0;

	drv_data = hid_get_drvdata(hid);

	if (!drv_data) {
		hid_err(hid, "Device data not found.");
		return LED_OFF;
	}

	for (i = 0; i < SRWS1_NUMBER_LEDS; i++)
		if (led_cdev == drv_data->led[i]) {
			value = (drv_data->led_state >> i) & 1;
			break;
		}

	return value ? LED_FULL : LED_OFF;
}

static int steelseries_srws1_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int ret, i;
	struct led_classdev *led;
	struct steelseries_srws1_data *drv_data;
	size_t name_sz;
	char *name;

	drv_data = devm_kzalloc(&hdev->dev, sizeof(*drv_data), GFP_KERNEL);
	if (drv_data == NULL) {
		hid_err(hdev, "can't alloc SRW-S1 memory\n");
		return -ENOMEM;
	}

	hid_set_drvdata(hdev, drv_data);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err;
	}

	if (!hid_validate_values(hdev, HID_OUTPUT_REPORT, 0, 0, 16)) {
		ret = -ENODEV;
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err;
	}

	/* register led subsystem */
	drv_data->led_state = 0;
	for (i = 0; i < SRWS1_NUMBER_LEDS + 1; i++)
		drv_data->led[i] = NULL;

	steelseries_srws1_set_leds(hdev, 0);

	name_sz = strlen(hdev->uniq) + 16;

	/* 'ALL', for setting all LEDs simultaneously */
	led = devm_kzalloc(&hdev->dev, sizeof(struct led_classdev)+name_sz, GFP_KERNEL);
	if (!led) {
		hid_err(hdev, "can't allocate memory for LED ALL\n");
		goto out;
	}

	name = (void *)(&led[1]);
	snprintf(name, name_sz, "SRWS1::%s::RPMALL", hdev->uniq);
	led->name = name;
	led->brightness = 0;
	led->max_brightness = 1;
	led->brightness_get = steelseries_srws1_led_all_get_brightness;
	led->brightness_set = steelseries_srws1_led_all_set_brightness;

	drv_data->led[SRWS1_NUMBER_LEDS] = led;
	ret = devm_led_classdev_register(&hdev->dev, led);
	if (ret) {
		hid_err(hdev, "failed to register LED %d. Aborting.\n", SRWS1_NUMBER_LEDS);
		goto out; /* let the driver continue without LEDs */
	}

	/* Each individual LED */
	for (i = 0; i < SRWS1_NUMBER_LEDS; i++) {
		led = devm_kzalloc(&hdev->dev, sizeof(struct led_classdev)+name_sz, GFP_KERNEL);
		if (!led) {
			hid_err(hdev, "can't allocate memory for LED %d\n", i);
			break;
		}

		name = (void *)(&led[1]);
		snprintf(name, name_sz, "SRWS1::%s::RPM%d", hdev->uniq, i+1);
		led->name = name;
		led->brightness = 0;
		led->max_brightness = 1;
		led->brightness_get = steelseries_srws1_led_get_brightness;
		led->brightness_set = steelseries_srws1_led_set_brightness;

		drv_data->led[i] = led;
		ret = devm_led_classdev_register(&hdev->dev, led);

		if (ret) {
			hid_err(hdev, "failed to register LED %d. Aborting.\n", i);
			break;	/* but let the driver continue without LEDs */
		}
	}
out:
	return 0;
err:
	return ret;
}
#endif

static const struct dmi_system_id steelseries_msi_rgb_dmi_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International Co., Ltd."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Raider A18 HX A9WJG"),
			DMI_MATCH(DMI_BOARD_NAME, "MS-182L"),
		},
	},
	{ }
};

static struct usb_interface *steelseries_hid_to_usb_intf(struct hid_device *hdev)
{
	if (!hid_is_usb(hdev))
		return NULL;

	return to_usb_interface(hdev->dev.parent);
}

static bool steelseries_msi_rgb_is_interface0(struct hid_device *hdev)
{
	struct usb_interface *intf = steelseries_hid_to_usb_intf(hdev);
	struct usb_device *udev;

	if (!intf)
		return false;

	udev = interface_to_usbdev(intf);

	return intf == usb_ifnum_to_if(udev, 0);
}

#if STEELSERIES_HAS_LEDS_MULTICOLOR

static struct usb_device *steelseries_hid_to_usb_dev(struct hid_device *hdev)
{
	struct usb_interface *intf = steelseries_hid_to_usb_intf(hdev);

	if (!intf)
		return NULL;

	return interface_to_usbdev(intf);
}

static int steelseries_msi_rgb_set_blocking(struct led_classdev *led_cdev,
					    enum led_brightness brightness)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(led_cdev);
	struct steelseries_device *sd = container_of(mc_cdev,
						    struct steelseries_device,
						    mc_cdev);
	struct hid_device *hdev = sd->hdev;
	struct usb_device *udev = steelseries_hid_to_usb_dev(hdev);
	int i, ret;
	u8 r, g, b;

	static const u8 keys[] = {
		0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
		0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
		0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
		0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
		0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
		0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x33, 0x34,
		0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c,
		0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44,
		0x45, 0x46, 0x47, 0x49, 0x4b, 0x4c, 0x4e, 0x4f,
		0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
		0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
		0x60, 0x61, 0x62, 0x63, 0x64, 0x66, 0xe0, 0xe1,
		0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xf0
	};
	static const u8 alc_zones[] = { 0x00, 0x01, 0x02, 0x03 };

	if (!udev)
		return -ENODEV;

	mutex_lock(&sd->rgb_lock);

	led_mc_calc_color_components(mc_cdev, brightness);

	r = mc_cdev->subled_info[0].brightness;
	g = mc_cdev->subled_info[1].brightness;
	b = mc_cdev->subled_info[2].brightness;

	/*
	 * Report layout (524 bytes):
	 * Byte 0: Opcode (0x0c)
	 * Byte 1: 0x00
	 * Byte 2: Mode (0x66 for Keyboard, 0x06 for Lightbar)
	 * Byte 3: 0x00
	 * Bytes 4+: 4-byte chunks per LED (Index, R, G, B)
	 */
	memset(sd->rgb_buf, 0, STEELSERIES_MSI_RGB_REPORT_LEN);
	sd->rgb_buf[0] = STEELSERIES_MSI_RGB_OPCODE;
	sd->rgb_buf[1] = 0x00;
	sd->rgb_buf[3] = 0x00;

	for (i = 0; i < (STEELSERIES_MSI_RGB_REPORT_LEN - 4) / 4; i++)
		sd->rgb_buf[4 + i * 4] = 0xff;

	if (hdev->product == USB_DEVICE_ID_STEELSERIES_MSI_KLC) {
		sd->rgb_buf[2] = STEELSERIES_MSI_RGB_KLC_MODE;
		for (i = 0; i < ARRAY_SIZE(keys); i++) {
			sd->rgb_buf[4 + i * 4] = keys[i];
			sd->rgb_buf[5 + i * 4] = r;
			sd->rgb_buf[6 + i * 4] = g;
			sd->rgb_buf[7 + i * 4] = b;
		}
	} else {
		sd->rgb_buf[2] = STEELSERIES_MSI_RGB_ALC_MODE;
		for (i = 0; i < ARRAY_SIZE(alc_zones); i++) {
			sd->rgb_buf[4 + i * 4] = alc_zones[i];
			sd->rgb_buf[5 + i * 4] = r;
			sd->rgb_buf[6 + i * 4] = g;
			sd->rgb_buf[7 + i * 4] = b;
		}
	}

	/*
	 * Send the vendor report verbatim with usb_control_msg(): byte 0 is a
	 * protocol opcode (0x0c), not a HID report ID, and the controller
	 * expects it under report ID 0 (wValue 0x0300). hid_hw_raw_request()
	 * would write the report number into byte 0, so the direct control
	 * transfer is used to keep the payload byte-identical to the tested
	 * userspace implementation.
	 */
	ret = hid_hw_power(hdev, PM_HINT_FULLON);
	if (ret < 0)
		goto out_unlock;

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      HID_REQ_SET_REPORT,
			      USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			      STEELSERIES_MSI_RGB_WVALUE, 0,
			      sd->rgb_buf, STEELSERIES_MSI_RGB_REPORT_LEN,
			      USB_CTRL_SET_TIMEOUT);

	hid_hw_power(hdev, PM_HINT_NORMAL);

out_unlock:
	mutex_unlock(&sd->rgb_lock);
	return ret < 0 ? ret : 0;
}

static void steelseries_msi_rgb_free_buf(void *data)
{
	kfree(data);
}

static int steelseries_msi_rgb_register(struct steelseries_device *sd)
{
	struct hid_device *hdev = sd->hdev;
	struct led_classdev *led_cdev;
	int ret;

	sd->rgb_buf = kzalloc(STEELSERIES_MSI_RGB_REPORT_LEN, GFP_KERNEL);
	if (!sd->rgb_buf)
		return -ENOMEM;

	ret = devm_add_action_or_reset(&hdev->dev,
				       steelseries_msi_rgb_free_buf,
				       sd->rgb_buf);
	if (ret) {
		sd->rgb_buf = NULL;
		return ret;
	}

	ret = devm_mutex_init(&hdev->dev, &sd->rgb_lock);
	if (ret) {
		devm_remove_action(&hdev->dev, steelseries_msi_rgb_free_buf,
				   sd->rgb_buf);
		kfree(sd->rgb_buf);
		sd->rgb_buf = NULL;
		return ret;
	}

	sd->subled_info[0].color_index = LED_COLOR_ID_RED;
	sd->subled_info[1].color_index = LED_COLOR_ID_GREEN;
	sd->subled_info[2].color_index = LED_COLOR_ID_BLUE;
	sd->subled_info[0].intensity = 255;
	sd->subled_info[1].intensity = 255;
	sd->subled_info[2].intensity = 255;
	sd->subled_info[0].channel = 0;
	sd->subled_info[1].channel = 1;
	sd->subled_info[2].channel = 2;

	sd->mc_cdev.subled_info = sd->subled_info;
	sd->mc_cdev.num_colors = 3;

	led_cdev = &sd->mc_cdev.led_cdev;
	if (hdev->product == USB_DEVICE_ID_STEELSERIES_MSI_KLC)
		led_cdev->name = "steelseries::kbd_backlight";
	else
		led_cdev->name = "steelseries::lightbar";

	led_cdev->max_brightness = 255;
	led_cdev->brightness_set_blocking = steelseries_msi_rgb_set_blocking;

	ret = devm_led_classdev_multicolor_register(&hdev->dev, &sd->mc_cdev);
	if (ret) {
		devm_remove_action(&hdev->dev, steelseries_msi_rgb_free_buf,
				   sd->rgb_buf);
		kfree(sd->rgb_buf);
		sd->rgb_buf = NULL;
		return ret;
	}

	return 0;
}
#else
static int steelseries_msi_rgb_register(struct steelseries_device *sd)
{
	return -ENODEV;
}
#endif

static int steelseries_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct steelseries_device *sd;
	int ret;

	if (hdev->product == USB_DEVICE_ID_STEELSERIES_SRWS1) {
#if IS_BUILTIN(CONFIG_LEDS_CLASS) || \
    (IS_MODULE(CONFIG_LEDS_CLASS) && IS_MODULE(CONFIG_HID_STEELSERIES))
		return steelseries_srws1_probe(hdev, id);
#else
		return -ENODEV;
#endif
	}

	sd = devm_kzalloc(&hdev->dev, sizeof(*sd), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;
	hid_set_drvdata(hdev, sd);
	sd->hdev = hdev;
	sd->quirks = id->driver_data;

	if (sd->quirks & STEELSERIES_MSI_RGB) {
		if (!dmi_check_system(steelseries_msi_rgb_dmi_table) ||
		    !steelseries_msi_rgb_is_interface0(hdev)) {
			hid_dbg(hdev, "MSI RGB quirk not applicable, using generic HID path\n");
			sd->quirks &= ~STEELSERIES_MSI_RGB;
		}
	}

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret)
		return ret;

	ret = hid_hw_open(hdev);
	if (ret)
		goto err_stop;

	if (sd->quirks & STEELSERIES_MSI_RGB) {
		ret = steelseries_msi_rgb_register(sd);
		if (ret) {
			hid_warn(hdev,
				 "Failed to register MSI RGB LEDs: %d, continuing without RGB support\n",
				 ret);
			sd->quirks &= ~STEELSERIES_MSI_RGB;
		}
	}

	return 0;

err_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void steelseries_remove(struct hid_device *hdev)
{
	if (hdev->product == USB_DEVICE_ID_STEELSERIES_SRWS1) {
#if IS_BUILTIN(CONFIG_LEDS_CLASS) || \
    (IS_MODULE(CONFIG_LEDS_CLASS) && IS_MODULE(CONFIG_HID_STEELSERIES))
		hid_hw_stop(hdev);
#endif
		return;
	}

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const __u8 *steelseries_srws1_report_fixup(struct hid_device *hdev,
		__u8 *rdesc, unsigned int *rsize)
{
	if (hdev->vendor != USB_VENDOR_ID_STEELSERIES ||
	    hdev->product != USB_DEVICE_ID_STEELSERIES_SRWS1)
		return rdesc;

	if (*rsize >= 115 && rdesc[11] == 0x02 && rdesc[13] == 0xc8
			&& rdesc[29] == 0xbb && rdesc[40] == 0xc5) {
		hid_info(hdev, "Fixing up Steelseries SRW-S1 report descriptor\n");
		*rsize = sizeof(steelseries_srws1_rdesc_fixed);
		return steelseries_srws1_rdesc_fixed;
	}
	return rdesc;
}

static const struct hid_device_id steelseries_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_STEELSERIES, USB_DEVICE_ID_STEELSERIES_SRWS1),
	  .driver_data = STEELSERIES_SRWS1 },

#if STEELSERIES_HAS_LEDS_MULTICOLOR
	{ /* MSI Raider A18 KLC */
	  HID_USB_DEVICE(USB_VENDOR_ID_STEELSERIES, USB_DEVICE_ID_STEELSERIES_MSI_KLC),
	  .driver_data = STEELSERIES_MSI_RGB },

	{ /* MSI Raider A18 ALC */
	  HID_USB_DEVICE(USB_VENDOR_ID_STEELSERIES, USB_DEVICE_ID_STEELSERIES_MSI_ALC),
	  .driver_data = STEELSERIES_MSI_RGB },
#endif

	{ }
};
MODULE_DEVICE_TABLE(hid, steelseries_devices);

static struct hid_driver steelseries_driver = {
	.name = "steelseries",
	.id_table = steelseries_devices,
	.probe = steelseries_probe,
	.remove = steelseries_remove,
	.report_fixup = steelseries_srws1_report_fixup,
};

module_hid_driver(steelseries_driver);
MODULE_DESCRIPTION("HID driver for Steelseries devices");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Wood <simon@mungewell.org>");
