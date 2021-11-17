// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simple USB RGB LED driver
 *
 * Copyright 2016 Heiner Kallweit <hkallweit1@gmail.com>
 * Based on drivers/hid/hid-thingm.c and
 * drivers/usb/misc/usbled.c
 */

#include <linux/hid.h>
#include <linux/hidraw.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include "hid-ids.h"

enum hidled_report_type {
	RAW_REQUEST,
	OUTPUT_REPORT
};

enum hidled_type {
	RISO_KAGAKU,
	DREAM_CHEEKY,
	THINGM,
	DELCOM,
	LUXAFOR,
};

static unsigned const char riso_kagaku_tbl[] = {
/* R+2G+4B -> riso kagaku color index */
	[0] = 0, /* black   */
	[1] = 2, /* red     */
	[2] = 1, /* green   */
	[3] = 5, /* yellow  */
	[4] = 3, /* blue    */
	[5] = 6, /* magenta */
	[6] = 4, /* cyan    */
	[7] = 7  /* white   */
};

#define RISO_KAGAKU_IX(r, g, b) riso_kagaku_tbl[((r)?1:0)+((g)?2:0)+((b)?4:0)]

union delcom_packet {
	__u8 data[8];
	struct {
		__u8 major_cmd;
		__u8 minor_cmd;
		__u8 data_lsb;
		__u8 data_msb;
	} tx;
	struct {
		__u8 cmd;
	} rx;
	struct {
		__le16 family_code;
		__le16 security_code;
		__u8 fw_version;
	} fw;
};

#define DELCOM_GREEN_LED	0
#define DELCOM_RED_LED		1
#define DELCOM_BLUE_LED		2

struct hidled_device;
struct hidled_rgb;

struct hidled_config {
	enum hidled_type	type;
	const char		*name;
	const char		*short_name;
	enum led_brightness	max_brightness;
	int			num_leds;
	size_t			report_size;
	enum hidled_report_type	report_type;
	int (*init)(struct hidled_device *ldev);
	int (*write)(struct led_classdev *cdev, enum led_brightness br);
};

struct hidled_led {
	struct led_classdev	cdev;
	struct hidled_rgb	*rgb;
	char			name[32];
};

struct hidled_rgb {
	struct hidled_device	*ldev;
	struct hidled_led	red;
	struct hidled_led	green;
	struct hidled_led	blue;
	u8			num;
};

struct hidled_device {
	const struct hidled_config *config;
	struct hid_device       *hdev;
	struct hidled_rgb	*rgb;
	u8			*buf;
	struct mutex		lock;
};

#define MAX_REPORT_SIZE		16

#define to_hidled_led(arg) container_of(arg, struct hidled_led, cdev)

static bool riso_kagaku_switch_green_blue;
module_param(riso_kagaku_switch_green_blue, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(riso_kagaku_switch_green_blue,
	"switch green and blue RGB component for Riso Kagaku devices");

static int hidled_send(struct hidled_device *ldev, __u8 *buf)
{
	int ret;

	mutex_lock(&ldev->lock);

	/*
	 * buffer provided to hid_hw_raw_request must not be on the stack
	 * and must not be part of a data structure
	 */
	memcpy(ldev->buf, buf, ldev->config->report_size);

	if (ldev->config->report_type == RAW_REQUEST)
		ret = hid_hw_raw_request(ldev->hdev, buf[0], ldev->buf,
					 ldev->config->report_size,
					 HID_FEATURE_REPORT,
					 HID_REQ_SET_REPORT);
	else if (ldev->config->report_type == OUTPUT_REPORT)
		ret = hid_hw_output_report(ldev->hdev, ldev->buf,
					   ldev->config->report_size);
	else
		ret = -EINVAL;

	mutex_unlock(&ldev->lock);

	if (ret < 0)
		return ret;

	return ret == ldev->config->report_size ? 0 : -EMSGSIZE;
}

/* reading data is supported for report type RAW_REQUEST only */
static int hidled_recv(struct hidled_device *ldev, __u8 *buf)
{
	int ret;

	if (ldev->config->report_type != RAW_REQUEST)
		return -EINVAL;

	mutex_lock(&ldev->lock);

	memcpy(ldev->buf, buf, ldev->config->report_size);

	ret = hid_hw_raw_request(ldev->hdev, buf[0], ldev->buf,
				 ldev->config->report_size,
				 HID_FEATURE_REPORT,
				 HID_REQ_SET_REPORT);
	if (ret < 0)
		goto err;

	ret = hid_hw_raw_request(ldev->hdev, buf[0], ldev->buf,
				 ldev->config->report_size,
				 HID_FEATURE_REPORT,
				 HID_REQ_GET_REPORT);

	memcpy(buf, ldev->buf, ldev->config->report_size);
err:
	mutex_unlock(&ldev->lock);

	return ret < 0 ? ret : 0;
}

static u8 riso_kagaku_index(struct hidled_rgb *rgb)
{
	enum led_brightness r, g, b;

	r = rgb->red.cdev.brightness;
	g = rgb->green.cdev.brightness;
	b = rgb->blue.cdev.brightness;

	if (riso_kagaku_switch_green_blue)
		return RISO_KAGAKU_IX(r, b, g);
	else
		return RISO_KAGAKU_IX(r, g, b);
}

static int riso_kagaku_write(struct led_classdev *cdev, enum led_brightness br)
{
	struct hidled_led *led = to_hidled_led(cdev);
	struct hidled_rgb *rgb = led->rgb;
	__u8 buf[MAX_REPORT_SIZE] = {};

	buf[1] = riso_kagaku_index(rgb);

	return hidled_send(rgb->ldev, buf);
}

static int dream_cheeky_write(struct led_classdev *cdev, enum led_brightness br)
{
	struct hidled_led *led = to_hidled_led(cdev);
	struct hidled_rgb *rgb = led->rgb;
	__u8 buf[MAX_REPORT_SIZE] = {};

	buf[1] = rgb->red.cdev.brightness;
	buf[2] = rgb->green.cdev.brightness;
	buf[3] = rgb->blue.cdev.brightness;
	buf[7] = 0x1a;
	buf[8] = 0x05;

	return hidled_send(rgb->ldev, buf);
}

static int dream_cheeky_init(struct hidled_device *ldev)
{
	__u8 buf[MAX_REPORT_SIZE] = {};

	/* Dream Cheeky magic */
	buf[1] = 0x1f;
	buf[2] = 0x02;
	buf[4] = 0x5f;
	buf[7] = 0x1a;
	buf[8] = 0x03;

	return hidled_send(ldev, buf);
}

static int _thingm_write(struct led_classdev *cdev, enum led_brightness br,
			 u8 offset)
{
	struct hidled_led *led = to_hidled_led(cdev);
	__u8 buf[MAX_REPORT_SIZE] = { 1, 'c' };

	buf[2] = led->rgb->red.cdev.brightness;
	buf[3] = led->rgb->green.cdev.brightness;
	buf[4] = led->rgb->blue.cdev.brightness;
	buf[7] = led->rgb->num + offset;

	return hidled_send(led->rgb->ldev, buf);
}

static int thingm_write_v1(struct led_classdev *cdev, enum led_brightness br)
{
	return _thingm_write(cdev, br, 0);
}

static int thingm_write(struct led_classdev *cdev, enum led_brightness br)
{
	return _thingm_write(cdev, br, 1);
}

static const struct hidled_config hidled_config_thingm_v1 = {
	.name = "ThingM blink(1) v1",
	.short_name = "thingm",
	.max_brightness = 255,
	.num_leds = 1,
	.report_size = 9,
	.report_type = RAW_REQUEST,
	.write = thingm_write_v1,
};

static int thingm_init(struct hidled_device *ldev)
{
	__u8 buf[MAX_REPORT_SIZE] = { 1, 'v' };
	int ret;

	ret = hidled_recv(ldev, buf);
	if (ret)
		return ret;

	/* Check for firmware major version 1 */
	if (buf[3] == '1')
		ldev->config = &hidled_config_thingm_v1;

	return 0;
}

static inline int delcom_get_lednum(const struct hidled_led *led)
{
	if (led == &led->rgb->red)
		return DELCOM_RED_LED;
	else if (led == &led->rgb->green)
		return DELCOM_GREEN_LED;
	else
		return DELCOM_BLUE_LED;
}

static int delcom_enable_led(struct hidled_led *led)
{
	union delcom_packet dp = { .tx.major_cmd = 101, .tx.minor_cmd = 12 };

	dp.tx.data_lsb = 1 << delcom_get_lednum(led);
	dp.tx.data_msb = 0;

	return hidled_send(led->rgb->ldev, dp.data);
}

static int delcom_set_pwm(struct hidled_led *led)
{
	union delcom_packet dp = { .tx.major_cmd = 101, .tx.minor_cmd = 34 };

	dp.tx.data_lsb = delcom_get_lednum(led);
	dp.tx.data_msb = led->cdev.brightness;

	return hidled_send(led->rgb->ldev, dp.data);
}

static int delcom_write(struct led_classdev *cdev, enum led_brightness br)
{
	struct hidled_led *led = to_hidled_led(cdev);
	int ret;

	/*
	 * enable LED
	 * We can't do this in the init function already because the device
	 * is internally reset later.
	 */
	ret = delcom_enable_led(led);
	if (ret)
		return ret;

	return delcom_set_pwm(led);
}

static int delcom_init(struct hidled_device *ldev)
{
	union delcom_packet dp = { .rx.cmd = 104 };
	int ret;

	ret = hidled_recv(ldev, dp.data);
	if (ret)
		return ret;
	/*
	 * Several Delcom devices share the same USB VID/PID
	 * Check for family id 2 for Visual Signal Indicator
	 */
	return le16_to_cpu(dp.fw.family_code) == 2 ? 0 : -ENODEV;
}

static int luxafor_write(struct led_classdev *cdev, enum led_brightness br)
{
	struct hidled_led *led = to_hidled_led(cdev);
	__u8 buf[MAX_REPORT_SIZE] = { [1] = 1 };

	buf[2] = led->rgb->num + 1;
	buf[3] = led->rgb->red.cdev.brightness;
	buf[4] = led->rgb->green.cdev.brightness;
	buf[5] = led->rgb->blue.cdev.brightness;

	return hidled_send(led->rgb->ldev, buf);
}

static const struct hidled_config hidled_configs[] = {
	{
		.type = RISO_KAGAKU,
		.name = "Riso Kagaku Webmail Notifier",
		.short_name = "riso_kagaku",
		.max_brightness = 1,
		.num_leds = 1,
		.report_size = 6,
		.report_type = OUTPUT_REPORT,
		.write = riso_kagaku_write,
	},
	{
		.type = DREAM_CHEEKY,
		.name = "Dream Cheeky Webmail Notifier",
		.short_name = "dream_cheeky",
		.max_brightness = 31,
		.num_leds = 1,
		.report_size = 9,
		.report_type = RAW_REQUEST,
		.init = dream_cheeky_init,
		.write = dream_cheeky_write,
	},
	{
		.type = THINGM,
		.name = "ThingM blink(1)",
		.short_name = "thingm",
		.max_brightness = 255,
		.num_leds = 2,
		.report_size = 9,
		.report_type = RAW_REQUEST,
		.init = thingm_init,
		.write = thingm_write,
	},
	{
		.type = DELCOM,
		.name = "Delcom Visual Signal Indicator G2",
		.short_name = "delcom",
		.max_brightness = 100,
		.num_leds = 1,
		.report_size = 8,
		.report_type = RAW_REQUEST,
		.init = delcom_init,
		.write = delcom_write,
	},
	{
		.type = LUXAFOR,
		.name = "Greynut Luxafor",
		.short_name = "luxafor",
		.max_brightness = 255,
		.num_leds = 6,
		.report_size = 9,
		.report_type = OUTPUT_REPORT,
		.write = luxafor_write,
	},
};

static int hidled_init_led(struct hidled_led *led, const char *color_name,
			   struct hidled_rgb *rgb, unsigned int minor)
{
	const struct hidled_config *config = rgb->ldev->config;

	if (config->num_leds > 1)
		snprintf(led->name, sizeof(led->name), "%s%u:%s:led%u",
			 config->short_name, minor, color_name, rgb->num);
	else
		snprintf(led->name, sizeof(led->name), "%s%u:%s",
			 config->short_name, minor, color_name);
	led->cdev.name = led->name;
	led->cdev.max_brightness = config->max_brightness;
	led->cdev.brightness_set_blocking = config->write;
	led->cdev.flags = LED_HW_PLUGGABLE;
	led->rgb = rgb;

	return devm_led_classdev_register(&rgb->ldev->hdev->dev, &led->cdev);
}

static int hidled_init_rgb(struct hidled_rgb *rgb, unsigned int minor)
{
	int ret;

	/* Register the red diode */
	ret = hidled_init_led(&rgb->red, "red", rgb, minor);
	if (ret)
		return ret;

	/* Register the green diode */
	ret = hidled_init_led(&rgb->green, "green", rgb, minor);
	if (ret)
		return ret;

	/* Register the blue diode */
	return hidled_init_led(&rgb->blue, "blue", rgb, minor);
}

static int hidled_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct hidled_device *ldev;
	unsigned int minor;
	int ret, i;

	ldev = devm_kzalloc(&hdev->dev, sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return -ENOMEM;

	ldev->buf = devm_kmalloc(&hdev->dev, MAX_REPORT_SIZE, GFP_KERNEL);
	if (!ldev->buf)
		return -ENOMEM;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	ldev->hdev = hdev;
	mutex_init(&ldev->lock);

	for (i = 0; !ldev->config && i < ARRAY_SIZE(hidled_configs); i++)
		if (hidled_configs[i].type == id->driver_data)
			ldev->config = &hidled_configs[i];

	if (!ldev->config)
		return -EINVAL;

	if (ldev->config->init) {
		ret = ldev->config->init(ldev);
		if (ret)
			return ret;
	}

	ldev->rgb = devm_kcalloc(&hdev->dev, ldev->config->num_leds,
				 sizeof(struct hidled_rgb), GFP_KERNEL);
	if (!ldev->rgb)
		return -ENOMEM;

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret)
		return ret;

	minor = ((struct hidraw *) hdev->hidraw)->minor;

	for (i = 0; i < ldev->config->num_leds; i++) {
		ldev->rgb[i].ldev = ldev;
		ldev->rgb[i].num = i;
		ret = hidled_init_rgb(&ldev->rgb[i], minor);
		if (ret) {
			hid_hw_stop(hdev);
			return ret;
		}
	}

	hid_info(hdev, "%s initialized\n", ldev->config->name);

	return 0;
}

static const struct hid_device_id hidled_table[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_RISO_KAGAKU,
	  USB_DEVICE_ID_RI_KA_WEBMAIL), .driver_data = RISO_KAGAKU },
	{ HID_USB_DEVICE(USB_VENDOR_ID_DREAM_CHEEKY,
	  USB_DEVICE_ID_DREAM_CHEEKY_WN), .driver_data = DREAM_CHEEKY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_DREAM_CHEEKY,
	  USB_DEVICE_ID_DREAM_CHEEKY_FA), .driver_data = DREAM_CHEEKY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_THINGM,
	  USB_DEVICE_ID_BLINK1), .driver_data = THINGM },
	{ HID_USB_DEVICE(USB_VENDOR_ID_DELCOM,
	  USB_DEVICE_ID_DELCOM_VISUAL_IND), .driver_data = DELCOM },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROCHIP,
	  USB_DEVICE_ID_LUXAFOR), .driver_data = LUXAFOR },
	{ }
};
MODULE_DEVICE_TABLE(hid, hidled_table);

static struct hid_driver hidled_driver = {
	.name = "hid-led",
	.probe = hidled_probe,
	.id_table = hidled_table,
};

module_hid_driver(hidled_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Heiner Kallweit <hkallweit1@gmail.com>");
MODULE_DESCRIPTION("Simple USB RGB LED driver");
