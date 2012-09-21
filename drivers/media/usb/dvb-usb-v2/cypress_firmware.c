/*  cypress_firmware.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for downloading the firmware to Cypress FX 1
 * and 2 based devices.
 *
 */

#include "dvb_usb.h"
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
	dvb_usb_dbg_usb_control_msg(udev,
			0xa0, USB_TYPE_VENDOR, addr, 0x00, data, len);

	return usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			0xa0, USB_TYPE_VENDOR, addr, 0x00, data, len, 5000);
}

int usbv2_cypress_load_firmware(struct usb_device *udev,
		const struct firmware *fw, int type)
{
	struct hexline hx;
	u8 reset;
	int ret, pos = 0;

	/* stop the CPU */
	reset = 1;
	ret = usb_cypress_writemem(udev, cypress[type].cs_reg, &reset, 1);
	if (ret != 1)
		dev_err(&udev->dev,
				"%s: could not stop the USB controller CPU\n",
				KBUILD_MODNAME);

	while ((ret = dvb_usbv2_get_hexline(fw, &hx, &pos)) > 0) {
		ret = usb_cypress_writemem(udev, hx.addr, hx.data, hx.len);
		if (ret != hx.len) {
			dev_err(&udev->dev, "%s: error while transferring " \
					"firmware (transferred size=%d, " \
					"block size=%d)\n",
					KBUILD_MODNAME, ret, hx.len);
			ret = -EINVAL;
			break;
		}
	}
	if (ret < 0) {
		dev_err(&udev->dev,
				"%s: firmware download failed at %d with %d\n",
				KBUILD_MODNAME, pos, ret);
		return ret;
	}

	if (ret == 0) {
		/* restart the CPU */
		reset = 0;
		if (ret || usb_cypress_writemem(
				udev, cypress[type].cs_reg, &reset, 1) != 1) {
			dev_err(&udev->dev, "%s: could not restart the USB " \
					"controller CPU\n", KBUILD_MODNAME);
			ret = -EINVAL;
		}
	} else
		ret = -EIO;

	return ret;
}
EXPORT_SYMBOL(usbv2_cypress_load_firmware);

int dvb_usbv2_get_hexline(const struct firmware *fw, struct hexline *hx,
		int *pos)
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
		/*
		hx->len -= 2;
		data_offs += 2;
		*/
	}
	memcpy(hx->data, &b[data_offs], hx->len);
	hx->chk = b[hx->len + data_offs];

	*pos += hx->len + 5;

	return *pos;
}
EXPORT_SYMBOL(dvb_usbv2_get_hexline);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Cypress firmware download");
MODULE_LICENSE("GPL");
