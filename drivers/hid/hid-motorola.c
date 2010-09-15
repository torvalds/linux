/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <linux/switch.h>
#include <linux/spi/cpcap.h>

#include "hid-ids.h"

#define MOT_SEMU        0x0001
#define MOT_IR_REMOTE	0x0002
#define MOT_AUDIO_JACK	0x0004

#define AUDIO_JACK_STATUS_REPORT    0x3E

static struct switch_dev sdev;

struct motorola_sc {
	unsigned long quirks;
	unsigned long audio_cable_inserted;
	struct work_struct work;
};

static void audio_jack_status_work(struct work_struct *work)
{
	struct motorola_sc *sc = container_of(work, struct motorola_sc, work);

	cpcap_accy_whisper_spdif_set_state(sc->audio_cable_inserted);
}

static int mot_rawevent(struct hid_device *hdev, struct hid_report *report,
		     u8 *data, int size)
{
	struct motorola_sc *sc = hid_get_drvdata(hdev);

	dbg_hid("%s\n", __func__);

	if (sc->quirks & MOT_AUDIO_JACK) {
		if (data[0] == AUDIO_JACK_STATUS_REPORT) {
			sc->audio_cable_inserted = data[1];
			schedule_work(&sc->work);
			return 1;
		}
	}

	return 0;
}

static int mot_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	unsigned long quirks = id->driver_data;
	struct motorola_sc *sc;
	unsigned int connect_mask = 0;

	dbg_hid("%s %d\n", __func__, __LINE__);

	sc = kzalloc(sizeof(*sc), GFP_KERNEL);
	if (sc == NULL) {
		dev_err(&hdev->dev, "can't alloc motorola descriptor\n");
		return -ENOMEM;
	}

	sc->quirks = quirks;
	hid_set_drvdata(hdev, sc);

	ret = hid_parse(hdev);
	if (ret) {
		dev_err(&hdev->dev, "parse failed\n");
		goto err_free;
	}

	if (quirks & MOT_SEMU)
		connect_mask |= HID_CONNECT_HIDRAW;
	if (quirks & MOT_IR_REMOTE)
		connect_mask |= (HID_CONNECT_HIDINPUT |
				HID_CONNECT_HIDINPUT_FORCE);
	if (quirks & MOT_AUDIO_JACK)
		INIT_WORK(&sc->work, audio_jack_status_work);

	ret = hid_hw_start(hdev, connect_mask);
	if (ret) {
		dev_err(&hdev->dev, "hw start failed\n");
		goto err_free_cancel;
	}

	switch_set_state(&sdev, 1);

	dbg_hid("%s %d\n", __func__, __LINE__);
	return 0;

err_free_cancel:
	cancel_work_sync(&sc->work);
err_free:
	kfree(sc);
	return ret;
}

static void mot_remove(struct hid_device *hdev)
{
	struct motorola_sc *sc = hid_get_drvdata(hdev);

	dbg_hid("%s\n", __func__);

	cancel_work_sync(&sc->work);

	switch_set_state(&sdev, 0);

	hid_hw_stop(hdev);
	kfree(hid_get_drvdata(hdev));
}

static const struct hid_device_id mot_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_MOTOROLA, USB_DEVICE_ID_HD_DOCK),
	.driver_data = MOT_SEMU | MOT_IR_REMOTE | MOT_AUDIO_JACK },
	{}
};
MODULE_DEVICE_TABLE(hid, mot_devices);

static const struct hid_report_id mot_reports[] = {
	{ HID_INPUT_REPORT },
	{ HID_TERMINATOR }
};

static struct hid_driver motorola_driver = {
	.name = "motorola",
	.id_table = mot_devices,
	.probe = mot_probe,
	.remove = mot_remove,
	.raw_event = mot_rawevent,
	.report_table = mot_reports,
};

static int motorola_init(void)
{
	int ret;

	dbg_hid("Registering MOT HID driver\n");

	ret = hid_register_driver(&motorola_driver);
	if (ret)
		printk(KERN_ERR "Can't register Motorola driver\n");

	sdev.name = "whisper_hid";
	switch_dev_register(&sdev);

	return ret;
}

static void motorola_exit(void)
{
	switch_dev_unregister(&sdev);
	hid_unregister_driver(&motorola_driver);
}

module_init(motorola_init);
module_exit(motorola_exit);
MODULE_LICENSE("GPL");
