/* dvb-usb-firmware.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for downloading the firmware to Cypress FX 1 and 2 based devices.
 *
 * FIXME: This part does actually not belong to dvb-usb, but to the usb-subsystem.
 */
#include "dvb-usb-common.h"

#include <linux/firmware.h>
#include <linux/usb.h>

struct usb_cypress_controller {
	int id;
	const char *name;       /* name of the usb controller */
	u16 cpu_cs_register;    /* needs to be restarted, when the firmware has been downloaded. */
};

static struct usb_cypress_controller cypress[] = {
	{ .id = CYPRESS_AN2135, .name = "Cypress AN2135", .cpu_cs_register = 0x7f92 },
	{ .id = CYPRESS_AN2235, .name = "Cypress AN2235", .cpu_cs_register = 0x7f92 },
	{ .id = CYPRESS_FX2,    .name = "Cypress FX2",    .cpu_cs_register = 0xe600 },
};

/*
 * load a firmware packet to the device
 */
static int usb_cypress_writemem(struct usb_device *udev,u16 addr,u8 *data, u8 len)
{
	return usb_control_msg(udev, usb_sndctrlpipe(udev,0),
			0xa0, USB_TYPE_VENDOR, addr, 0x00, data, len, 5*HZ);
}

int usb_cypress_load_firmware(struct usb_device *udev, const char *filename, int type)
{
	const struct firmware *fw = NULL;
	u16 addr;
	u8 *b,*p;
	int ret = 0,i;

	if ((ret = request_firmware(&fw, filename, &udev->dev)) != 0) {
		err("did not find the firmware file. (%s) "
			"Please see linux/Documentation/dvb/ for more details on firmware-problems.",
			filename);
		return ret;
	}

	info("downloading firmware from file '%s' to the '%s'",filename,cypress[type].name);

	p = kmalloc(fw->size,GFP_KERNEL);
	if (p != NULL) {
		u8 reset;
		/*
		 * you cannot use the fw->data as buffer for
		 * usb_control_msg, a new buffer has to be
		 * created
		 */
		memcpy(p,fw->data,fw->size);

		/* stop the CPU */
		reset = 1;
		if ((ret = usb_cypress_writemem(udev,cypress[type].cpu_cs_register,&reset,1)) != 1)
			err("could not stop the USB controller CPU.");
		for(i = 0; p[i+3] == 0 && i < fw->size; ) {
			b = (u8 *) &p[i];
			addr = cpu_to_le16( *((u16 *) &b[1]) );

			deb_fw("writing to address 0x%04x (buffer: 0x%02x%02x)\n",addr,b[1],b[2]);

			ret = usb_cypress_writemem(udev,addr,&b[4],b[0]);

			if (ret != b[0]) {
				err("error while transferring firmware "
					"(transferred size: %d, block size: %d)",
					ret,b[0]);
				ret = -EINVAL;
				break;
			}
			i += 5 + b[0];
		}
		/* length in ret */
		if (ret > 0)
			ret = 0;
		/* restart the CPU */
		reset = 0;
		if (ret || usb_cypress_writemem(udev,cypress[type].cpu_cs_register,&reset,1) != 1) {
			err("could not restart the USB controller CPU.");
			ret = -EINVAL;
		}

		kfree(p);
	} else {
		ret = -ENOMEM;
	}
	release_firmware(fw);

	return ret;
}
