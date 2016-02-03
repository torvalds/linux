/*
 * Linux V4L2 radio driver for the Griffin radioSHARK2 USB radio receiver
 *
 * Note the radioSHARK2 offers the audio through a regular USB audio device,
 * this driver only handles the tuning.
 *
 * The info necessary to drive the shark2 was taken from the small userspace
 * shark2.c program by Hisaaki Shibata, which he kindly placed in the Public
 * Domain.
 *
 * Copyright (c) 2012 Hans de Goede <hdegoede@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <media/v4l2-device.h>
#include "radio-tea5777.h"

#if defined(CONFIG_LEDS_CLASS) || \
    (defined(CONFIG_LEDS_CLASS_MODULE) && defined(CONFIG_RADIO_SHARK2_MODULE))
#define SHARK_USE_LEDS 1
#endif

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Griffin radioSHARK2, USB radio receiver driver");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

#define SHARK_IN_EP		0x83
#define SHARK_OUT_EP		0x05

#define TB_LEN 7
#define DRV_NAME "radioshark2"

#define v4l2_dev_to_shark(d) container_of(d, struct shark_device, v4l2_dev)

enum { BLUE_LED, RED_LED, NO_LEDS };

struct shark_device {
	struct usb_device *usbdev;
	struct v4l2_device v4l2_dev;
	struct radio_tea5777 tea;

#ifdef SHARK_USE_LEDS
	struct work_struct led_work;
	struct led_classdev leds[NO_LEDS];
	char led_names[NO_LEDS][32];
	atomic_t brightness[NO_LEDS];
	unsigned long brightness_new;
#endif

	u8 *transfer_buffer;
};

static atomic_t shark_instance = ATOMIC_INIT(0);

static int shark_write_reg(struct radio_tea5777 *tea, u64 reg)
{
	struct shark_device *shark = tea->private_data;
	int i, res, actual_len;

	memset(shark->transfer_buffer, 0, TB_LEN);
	shark->transfer_buffer[0] = 0x81; /* Write register command */
	for (i = 0; i < 6; i++)
		shark->transfer_buffer[i + 1] = (reg >> (40 - i * 8)) & 0xff;

	v4l2_dbg(1, debug, tea->v4l2_dev, "shark2-write: %*ph\n",
		 7, shark->transfer_buffer);

	res = usb_interrupt_msg(shark->usbdev,
				usb_sndintpipe(shark->usbdev, SHARK_OUT_EP),
				shark->transfer_buffer, TB_LEN,
				&actual_len, 1000);
	if (res < 0) {
		v4l2_err(tea->v4l2_dev, "write error: %d\n", res);
		return res;
	}

	return 0;
}

static int shark_read_reg(struct radio_tea5777 *tea, u32 *reg_ret)
{
	struct shark_device *shark = tea->private_data;
	int i, res, actual_len;
	u32 reg = 0;

	memset(shark->transfer_buffer, 0, TB_LEN);
	shark->transfer_buffer[0] = 0x82;
	res = usb_interrupt_msg(shark->usbdev,
				usb_sndintpipe(shark->usbdev, SHARK_OUT_EP),
				shark->transfer_buffer, TB_LEN,
				&actual_len, 1000);
	if (res < 0) {
		v4l2_err(tea->v4l2_dev, "request-read error: %d\n", res);
		return res;
	}

	res = usb_interrupt_msg(shark->usbdev,
				usb_rcvintpipe(shark->usbdev, SHARK_IN_EP),
				shark->transfer_buffer, TB_LEN,
				&actual_len, 1000);
	if (res < 0) {
		v4l2_err(tea->v4l2_dev, "read error: %d\n", res);
		return res;
	}

	for (i = 0; i < 3; i++)
		reg |= shark->transfer_buffer[i] << (16 - i * 8);

	v4l2_dbg(1, debug, tea->v4l2_dev, "shark2-read: %*ph\n",
		 3, shark->transfer_buffer);

	*reg_ret = reg;
	return 0;
}

static const struct radio_tea5777_ops shark_tea_ops = {
	.write_reg = shark_write_reg,
	.read_reg  = shark_read_reg,
};

#ifdef SHARK_USE_LEDS
static void shark_led_work(struct work_struct *work)
{
	struct shark_device *shark =
		container_of(work, struct shark_device, led_work);
	int i, res, brightness, actual_len;

	for (i = 0; i < 2; i++) {
		if (!test_and_clear_bit(i, &shark->brightness_new))
			continue;

		brightness = atomic_read(&shark->brightness[i]);
		memset(shark->transfer_buffer, 0, TB_LEN);
		shark->transfer_buffer[0] = 0x83 + i;
		shark->transfer_buffer[1] = brightness;
		res = usb_interrupt_msg(shark->usbdev,
					usb_sndintpipe(shark->usbdev,
						       SHARK_OUT_EP),
					shark->transfer_buffer, TB_LEN,
					&actual_len, 1000);
		if (res < 0)
			v4l2_err(&shark->v4l2_dev, "set LED %s error: %d\n",
				 shark->led_names[i], res);
	}
}

static void shark_led_set_blue(struct led_classdev *led_cdev,
			       enum led_brightness value)
{
	struct shark_device *shark =
		container_of(led_cdev, struct shark_device, leds[BLUE_LED]);

	atomic_set(&shark->brightness[BLUE_LED], value);
	set_bit(BLUE_LED, &shark->brightness_new);
	schedule_work(&shark->led_work);
}

static void shark_led_set_red(struct led_classdev *led_cdev,
			      enum led_brightness value)
{
	struct shark_device *shark =
		container_of(led_cdev, struct shark_device, leds[RED_LED]);

	atomic_set(&shark->brightness[RED_LED], value);
	set_bit(RED_LED, &shark->brightness_new);
	schedule_work(&shark->led_work);
}

static const struct led_classdev shark_led_templates[NO_LEDS] = {
	[BLUE_LED] = {
		.name		= "%s:blue:",
		.brightness	= LED_OFF,
		.max_brightness = 127,
		.brightness_set = shark_led_set_blue,
	},
	[RED_LED] = {
		.name		= "%s:red:",
		.brightness	= LED_OFF,
		.max_brightness = 1,
		.brightness_set = shark_led_set_red,
	},
};

static int shark_register_leds(struct shark_device *shark, struct device *dev)
{
	int i, retval;

	atomic_set(&shark->brightness[BLUE_LED], 127);
	INIT_WORK(&shark->led_work, shark_led_work);
	for (i = 0; i < NO_LEDS; i++) {
		shark->leds[i] = shark_led_templates[i];
		snprintf(shark->led_names[i], sizeof(shark->led_names[0]),
			 shark->leds[i].name, shark->v4l2_dev.name);
		shark->leds[i].name = shark->led_names[i];
		retval = led_classdev_register(dev, &shark->leds[i]);
		if (retval) {
			v4l2_err(&shark->v4l2_dev,
				 "couldn't register led: %s\n",
				 shark->led_names[i]);
			return retval;
		}
	}
	return 0;
}

static void shark_unregister_leds(struct shark_device *shark)
{
	int i;

	for (i = 0; i < NO_LEDS; i++)
		led_classdev_unregister(&shark->leds[i]);

	cancel_work_sync(&shark->led_work);
}

static inline void shark_resume_leds(struct shark_device *shark)
{
	int i;

	for (i = 0; i < NO_LEDS; i++)
		set_bit(i, &shark->brightness_new);

	schedule_work(&shark->led_work);
}
#else
static int shark_register_leds(struct shark_device *shark, struct device *dev)
{
	v4l2_warn(&shark->v4l2_dev,
		  "CONFIG_LEDS_CLASS not enabled, LED support disabled\n");
	return 0;
}
static inline void shark_unregister_leds(struct shark_device *shark) { }
static inline void shark_resume_leds(struct shark_device *shark) { }
#endif

static void usb_shark_disconnect(struct usb_interface *intf)
{
	struct v4l2_device *v4l2_dev = usb_get_intfdata(intf);
	struct shark_device *shark = v4l2_dev_to_shark(v4l2_dev);

	mutex_lock(&shark->tea.mutex);
	v4l2_device_disconnect(&shark->v4l2_dev);
	radio_tea5777_exit(&shark->tea);
	mutex_unlock(&shark->tea.mutex);

	shark_unregister_leds(shark);

	v4l2_device_put(&shark->v4l2_dev);
}

static void usb_shark_release(struct v4l2_device *v4l2_dev)
{
	struct shark_device *shark = v4l2_dev_to_shark(v4l2_dev);

	v4l2_device_unregister(&shark->v4l2_dev);
	kfree(shark->transfer_buffer);
	kfree(shark);
}

static int usb_shark_probe(struct usb_interface *intf,
			   const struct usb_device_id *id)
{
	struct shark_device *shark;
	int retval = -ENOMEM;

	shark = kzalloc(sizeof(struct shark_device), GFP_KERNEL);
	if (!shark)
		return retval;

	shark->transfer_buffer = kmalloc(TB_LEN, GFP_KERNEL);
	if (!shark->transfer_buffer)
		goto err_alloc_buffer;

	v4l2_device_set_name(&shark->v4l2_dev, DRV_NAME, &shark_instance);

	retval = shark_register_leds(shark, &intf->dev);
	if (retval)
		goto err_reg_leds;

	shark->v4l2_dev.release = usb_shark_release;
	retval = v4l2_device_register(&intf->dev, &shark->v4l2_dev);
	if (retval) {
		v4l2_err(&shark->v4l2_dev, "couldn't register v4l2_device\n");
		goto err_reg_dev;
	}

	shark->usbdev = interface_to_usbdev(intf);
	shark->tea.v4l2_dev = &shark->v4l2_dev;
	shark->tea.private_data = shark;
	shark->tea.ops = &shark_tea_ops;
	shark->tea.has_am = true;
	shark->tea.write_before_read = true;
	strlcpy(shark->tea.card, "Griffin radioSHARK2",
		sizeof(shark->tea.card));
	usb_make_path(shark->usbdev, shark->tea.bus_info,
		sizeof(shark->tea.bus_info));

	retval = radio_tea5777_init(&shark->tea, THIS_MODULE);
	if (retval) {
		v4l2_err(&shark->v4l2_dev, "couldn't init tea5777\n");
		goto err_init_tea;
	}

	return 0;

err_init_tea:
	v4l2_device_unregister(&shark->v4l2_dev);
err_reg_dev:
	shark_unregister_leds(shark);
err_reg_leds:
	kfree(shark->transfer_buffer);
err_alloc_buffer:
	kfree(shark);

	return retval;
}

#ifdef CONFIG_PM
static int usb_shark_suspend(struct usb_interface *intf, pm_message_t message)
{
	return 0;
}

static int usb_shark_resume(struct usb_interface *intf)
{
	struct v4l2_device *v4l2_dev = usb_get_intfdata(intf);
	struct shark_device *shark = v4l2_dev_to_shark(v4l2_dev);
	int ret;

	mutex_lock(&shark->tea.mutex);
	ret = radio_tea5777_set_freq(&shark->tea);
	mutex_unlock(&shark->tea.mutex);

	shark_resume_leds(shark);

	return ret;
}
#endif

/* Specify the bcdDevice value, as the radioSHARK and radioSHARK2 share ids */
static struct usb_device_id usb_shark_device_table[] = {
	{ .match_flags = USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION |
			 USB_DEVICE_ID_MATCH_INT_CLASS,
	  .idVendor     = 0x077d,
	  .idProduct    = 0x627a,
	  .bcdDevice_lo = 0x0010,
	  .bcdDevice_hi = 0x0010,
	  .bInterfaceClass = 3,
	},
	{ }
};
MODULE_DEVICE_TABLE(usb, usb_shark_device_table);

static struct usb_driver usb_shark_driver = {
	.name			= DRV_NAME,
	.probe			= usb_shark_probe,
	.disconnect		= usb_shark_disconnect,
	.id_table		= usb_shark_device_table,
#ifdef CONFIG_PM
	.suspend		= usb_shark_suspend,
	.resume			= usb_shark_resume,
	.reset_resume		= usb_shark_resume,
#endif
};
module_usb_driver(usb_shark_driver);
