/*
 *  Driver for the Auvitek USB bridge
 *
 *  Copyright (c) 2008 Steven Toth <stoth@hauppauge.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "au0828.h"
#include "au0828-cards.h"

struct au0828_board au0828_boards[] = {
	[AU0828_BOARD_UNKNOWN] = {
		.name	= "Unknown board",
	},
	[AU0828_BOARD_HAUPPAUGE_HVR850] = {
		.name	= "Hauppauge HVR850",
	},
	[AU0828_BOARD_HAUPPAUGE_HVR950Q] = {
		.name	= "Hauppauge HVR950Q",
	},
	[AU0828_BOARD_HAUPPAUGE_HVR950Q_MXL] = {
		.name	= "Hauppauge HVR950Q rev xxF8",
	},
	[AU0828_BOARD_DVICO_FUSIONHDTV7] = {
		.name	= "DViCO FusionHDTV USB",
	},
	[AU0828_BOARD_HAUPPAUGE_WOODBURY] = {
		.name = "Hauppauge Woodbury",
	},
};

/* Tuner callback function for au0828 boards. Currently only needed
 * for HVR1500Q, which has an xc5000 tuner.
 */
int au0828_tuner_callback(void *priv, int command, int arg)
{
	struct au0828_dev *dev = priv;

	dprintk(1, "%s()\n", __func__);

	switch (dev->board) {
	case AU0828_BOARD_HAUPPAUGE_HVR850:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q_MXL:
	case AU0828_BOARD_DVICO_FUSIONHDTV7:
		if (command == 0) {
			/* Tuner Reset Command from xc5000 */
			/* Drive the tuner into reset and out */
			au0828_clear(dev, REG_001, 2);
			mdelay(200);
			au0828_set(dev, REG_001, 2);
			mdelay(50);
			return 0;
		} else {
			printk(KERN_ERR
				"%s(): Unknown command.\n", __func__);
			return -EINVAL;
		}
		break;
	}

	return 0; /* Should never be here */
}

static void hauppauge_eeprom(struct au0828_dev *dev, u8 *eeprom_data)
{
	struct tveeprom tv;

	tveeprom_hauppauge_analog(&dev->i2c_client, &tv, eeprom_data);

	/* Make sure we support the board model */
	switch (tv.model) {
	case 72000: /* WinTV-HVR950q (Retail, IR, ATSC/QAM */
	case 72001: /* WinTV-HVR950q (Retail, IR, ATSC/QAM and basic analog video */
	case 72211: /* WinTV-HVR950q (OEM, IR, ATSC/QAM and basic analog video */
	case 72221: /* WinTV-HVR950q (OEM, IR, ATSC/QAM and basic analog video */
	case 72231: /* WinTV-HVR950q (OEM, IR, ATSC/QAM and basic analog video */
	case 72241: /* WinTV-HVR950q (OEM, No IR, ATSC/QAM and basic analog video */
	case 72301: /* WinTV-HVR850 (Retail, IR, ATSC and basic analog video */
	case 72500: /* WinTV-HVR950q (OEM, No IR, ATSC/QAM */
		break;
	default:
		printk(KERN_WARNING "%s: warning: "
		       "unknown hauppauge model #%d\n", __func__, tv.model);
		break;
	}

	printk(KERN_INFO "%s: hauppauge eeprom: model=%d\n",
	       __func__, tv.model);
}

void au0828_card_setup(struct au0828_dev *dev)
{
	static u8 eeprom[256];

	dprintk(1, "%s()\n", __func__);

	if (dev->i2c_rc == 0) {
		dev->i2c_client.addr = 0xa0 >> 1;
		tveeprom_read(&dev->i2c_client, eeprom, sizeof(eeprom));
	}

	switch (dev->board) {
	case AU0828_BOARD_HAUPPAUGE_HVR850:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q_MXL:
	case AU0828_BOARD_HAUPPAUGE_WOODBURY:
		if (dev->i2c_rc == 0)
			hauppauge_eeprom(dev, eeprom+0xa0);
		break;
	}
}

/*
 * The bridge has between 8 and 12 gpios.
 * Regs 1 and 0 deal with output enables.
 * Regs 3 and 2 deal with direction.
 */
void au0828_gpio_setup(struct au0828_dev *dev)
{
	dprintk(1, "%s()\n", __func__);

	switch (dev->board) {
	case AU0828_BOARD_HAUPPAUGE_HVR850:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q_MXL:
	case AU0828_BOARD_HAUPPAUGE_WOODBURY:
		/* GPIO's
		 * 4 - CS5340
		 * 5 - AU8522 Demodulator
		 * 6 - eeprom W/P
		 * 9 - XC5000 Tuner
		 */

		/* Into reset */
		au0828_write(dev, REG_003, 0x02);
		au0828_write(dev, REG_002, 0x88 | 0x20);
		au0828_write(dev, REG_001, 0x0);
		au0828_write(dev, REG_000, 0x0);
		msleep(100);

		/* Out of reset */
		au0828_write(dev, REG_003, 0x02);
		au0828_write(dev, REG_001, 0x02);
		au0828_write(dev, REG_002, 0x88 | 0x20);
		au0828_write(dev, REG_000, 0x88 | 0x20 | 0x40);
		msleep(250);
		break;
	case AU0828_BOARD_DVICO_FUSIONHDTV7:
		/* GPIO's
		 * 6 - ?
		 * 8 - AU8522 Demodulator
		 * 9 - XC5000 Tuner
		 */

		/* Into reset */
		au0828_write(dev, REG_003, 0x02);
		au0828_write(dev, REG_002, 0xa0);
		au0828_write(dev, REG_001, 0x0);
		au0828_write(dev, REG_000, 0x0);
		msleep(100);

		/* Out of reset */
		au0828_write(dev, REG_003, 0x02);
		au0828_write(dev, REG_002, 0xa0);
		au0828_write(dev, REG_001, 0x02);
		au0828_write(dev, REG_000, 0xa0);
		msleep(250);
		break;
	}
}

/* table of devices that work with this driver */
struct usb_device_id au0828_usb_id_table [] = {
	{ USB_DEVICE(0x2040, 0x7200),
		.driver_info = AU0828_BOARD_HAUPPAUGE_HVR950Q },
	{ USB_DEVICE(0x2040, 0x7240),
		.driver_info = AU0828_BOARD_HAUPPAUGE_HVR850 },
	{ USB_DEVICE(0x0fe9, 0xd620),
		.driver_info = AU0828_BOARD_DVICO_FUSIONHDTV7 },
	{ USB_DEVICE(0x2040, 0x7210),
		.driver_info = AU0828_BOARD_HAUPPAUGE_HVR950Q },
	{ USB_DEVICE(0x2040, 0x7217),
		.driver_info = AU0828_BOARD_HAUPPAUGE_HVR950Q },
	{ USB_DEVICE(0x2040, 0x721b),
		.driver_info = AU0828_BOARD_HAUPPAUGE_HVR950Q },
	{ USB_DEVICE(0x2040, 0x721f),
		.driver_info = AU0828_BOARD_HAUPPAUGE_HVR950Q },
	{ USB_DEVICE(0x2040, 0x7280),
		.driver_info = AU0828_BOARD_HAUPPAUGE_HVR950Q },
	{ USB_DEVICE(0x0fd9, 0x0008),
		.driver_info = AU0828_BOARD_HAUPPAUGE_HVR950Q },
	{ USB_DEVICE(0x2040, 0x7201),
		.driver_info = AU0828_BOARD_HAUPPAUGE_HVR950Q_MXL },
	{ USB_DEVICE(0x2040, 0x7211),
		.driver_info = AU0828_BOARD_HAUPPAUGE_HVR950Q_MXL },
	{ USB_DEVICE(0x2040, 0x7281),
		.driver_info = AU0828_BOARD_HAUPPAUGE_HVR950Q_MXL },
	{ USB_DEVICE(0x2040, 0x8200),
		.driver_info = AU0828_BOARD_HAUPPAUGE_WOODBURY },
	{ },
};

MODULE_DEVICE_TABLE(usb, au0828_usb_id_table);
