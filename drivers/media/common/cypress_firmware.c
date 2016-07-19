/*  cypress_firmware.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@posteo.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for downloading the firmware to Cypress FX 1
 * and 2 based devices.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include "cypress_firmware.h"

struct usb_cypress_controller {
	u8 id;
	const char *name;	/* name of the usb controller */
	u16 cs_reg;		/* needs to be restarted,
				 * when the firmware has been downloaded */
};

static const struct usb_cypress_controller cypress[] = {
	{ .id = CYPRESS_AN2135, .name = "Cypress AN2135", .cs_reg = 0x7f92 },
	{ .id = CYPRESS_AN2235, .name = "Cypress AN2235", .cs_reg = 0x7f92 },
	{ .id = CYPRESS_FX2,    .name = "Cypress FX2",    .cs_reg = 0xe600 },
};

/*
 * load a firmware packet to the device
 */
static int usb_cypress_writemem(struct usb_device *udev, u16 addr, u8 *data,
		u8 len)
{
	return usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			0xa0, USB_TYPE_VENDOR, addr, 0x00, data, len, 5000);
}

static int cypress_get_hexline(const struct firmware *fw,
				struct hexline *hx, int *pos)
{
	u8 *b = (u8 *) &fw->data[*pos];
	int data_offs = 4;

	if (*pos >= fw->size)
		return 0;

	memset(hx, 0, sizeof(struct hexline));
	hx->len = b[0];

	if ((*pos + hx->len + 4) >= fw->size)
		return -EINVAL;

	hx->addr = b[1] | (b[2] << 8);
	hx->type = b[3];

	if (hx->type == 0x04) {
		/* b[4] and b[5] are the Extended linear address record data
		 * field */
		hx->addr |= (b[4] << 24) | (b[5] << 16);
	}

	memcpy(hx->data, &b[data_offs], hx->len);
	hx->chk = b[hx->len + data_offs];
	*pos += hx->len + 5;

	return *pos;
}

int cypress_load_firmware(struct usb_device *udev,
		const struct firmware *fw, int type)
{
	struct hexline *hx;
	int ret, pos = 0;

	hx = kmalloc(sizeof(struct hexline), GFP_KERNEL);
	if (!hx) {
		dev_err(&udev->dev, "%s: kmalloc() failed\n", KBUILD_MODNAME);
		return -ENOMEM;
	}

	/* stop the CPU */
	hx->data[0] = 1;
	ret = usb_cypress_writemem(udev, cypress[type].cs_reg, hx->data, 1);
	if (ret != 1) {
		dev_err(&udev->dev, "%s: CPU stop failed=%d\n",
				KBUILD_MODNAME, ret);
		ret = -EIO;
		goto err_kfree;
	}

	/* write firmware to memory */
	for (;;) {
		ret = cypress_get_hexline(fw, hx, &pos);
		if (ret < 0)
			goto err_kfree;
		else if (ret == 0)
			break;

		ret = usb_cypress_writemem(udev, hx->addr, hx->data, hx->len);
		if (ret < 0) {
			goto err_kfree;
		} else if (ret != hx->len) {
			dev_err(&udev->dev,
					"%s: error while transferring firmware (transferred size=%d, block size=%d)\n",
					KBUILD_MODNAME, ret, hx->len);
			ret = -EIO;
			goto err_kfree;
		}
	}

	/* start the CPU */
	hx->data[0] = 0;
	ret = usb_cypress_writemem(udev, cypress[type].cs_reg, hx->data, 1);
	if (ret != 1) {
		dev_err(&udev->dev, "%s: CPU start failed=%d\n",
				KBUILD_MODNAME, ret);
		ret = -EIO;
		goto err_kfree;
	}

	ret = 0;
err_kfree:
	kfree(hx);
	return ret;
}
EXPORT_SYMBOL(cypress_load_firmware);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Cypress firmware download");
MODULE_LICENSE("GPL");
