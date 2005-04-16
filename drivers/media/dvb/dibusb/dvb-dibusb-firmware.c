/*
 * dvb-dibusb-firmware.c is part of the driver for mobile USB Budget DVB-T devices 
 * based on reference design made by DiBcom (http://www.dibcom.fr/)
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * see dvb-dibusb-core.c for more copyright details.
 *
 * This file contains functions for downloading the firmware to the device.
 */
#include "dvb-dibusb.h"

#include <linux/firmware.h>
#include <linux/usb.h>

/*
 * load a firmware packet to the device 
 */
static int dibusb_writemem(struct usb_device *udev,u16 addr,u8 *data, u8 len)
{
	return usb_control_msg(udev, usb_sndctrlpipe(udev,0),
			0xa0, USB_TYPE_VENDOR, addr, 0x00, data, len, 5000);
}

int dibusb_loadfirmware(struct usb_device *udev, struct dibusb_usb_device *dibdev)
{
	const struct firmware *fw = NULL;
	u16 addr;
	u8 *b,*p;
	int ret = 0,i;
	
	if ((ret = request_firmware(&fw, dibdev->dev_cl->firmware, &udev->dev)) != 0) {
		err("did not find the firmware file. (%s) "
			"Please see linux/Documentation/dvb/ for more details on firmware-problems.",
			dibdev->dev_cl->firmware);
		return ret;
	}

	info("downloading firmware from file '%s'.",dibdev->dev_cl->firmware);
	
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
		if ((ret = dibusb_writemem(udev,dibdev->dev_cl->usb_ctrl->cpu_cs_register,&reset,1)) != 1) 
			err("could not stop the USB controller CPU.");
		for(i = 0; p[i+3] == 0 && i < fw->size; ) { 
			b = (u8 *) &p[i];
			addr = *((u16 *) &b[1]);

			ret = dibusb_writemem(udev,addr,&b[4],b[0]);
		
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
		if (ret || dibusb_writemem(udev,dibdev->dev_cl->usb_ctrl->cpu_cs_register,&reset,1) != 1) {
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
