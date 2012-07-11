/*
 *  Driver for the Auvitek USB bridge
 *
 *  Copyright (c) 2008 Steven Toth <stoth@linuxtv.org>
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
#include "au8522.h"
#include "media/tuner.h"
#include "media/v4l2-common.h"

void hvr950q_cs5340_audio(void *priv, int enable)
{
	/* Because the HVR-950q shares an i2s bus between the cs5340 and the
	   au8522, we need to hold cs5340 in reset when using the au8522 */
	struct au0828_dev *dev = priv;
	if (enable == 1)
		au0828_set(dev, REG_000, 0x10);
	else
		au0828_clear(dev, REG_000, 0x10);
}

struct au0828_board au0828_boards[] = {
	[AU0828_BOARD_UNKNOWN] = {
		.name	= "Unknown board",
		.tuner_type = UNSET,
		.tuner_addr = ADDR_UNSET,
	},
	[AU0828_BOARD_HAUPPAUGE_HVR850] = {
		.name	= "Hauppauge HVR850",
		.tuner_type = TUNER_XC5000,
		.tuner_addr = 0x61,
		.i2c_clk_divider = AU0828_I2C_CLK_30KHZ,
		.input = {
			{
				.type = AU0828_VMUX_TELEVISION,
				.vmux = AU8522_COMPOSITE_CH4_SIF,
				.amux = AU8522_AUDIO_SIF,
			},
			{
				.type = AU0828_VMUX_COMPOSITE,
				.vmux = AU8522_COMPOSITE_CH1,
				.amux = AU8522_AUDIO_NONE,
				.audio_setup = hvr950q_cs5340_audio,
			},
			{
				.type = AU0828_VMUX_SVIDEO,
				.vmux = AU8522_SVIDEO_CH13,
				.amux = AU8522_AUDIO_NONE,
				.audio_setup = hvr950q_cs5340_audio,
			},
		},
	},
	[AU0828_BOARD_HAUPPAUGE_HVR950Q] = {
		.name	= "Hauppauge HVR950Q",
		.tuner_type = TUNER_XC5000,
		.tuner_addr = 0x61,
		/* The au0828 hardware i2c implementation does not properly
		   support the xc5000's i2c clock stretching.  So we need to
		   lower the clock frequency enough where the 15us clock
		   stretch fits inside of a normal clock cycle, or else the
		   au0828 fails to set the STOP bit.  A 30 KHz clock puts the
		   clock pulse width at 18us */
		.i2c_clk_divider = AU0828_I2C_CLK_30KHZ,
		.input = {
			{
				.type = AU0828_VMUX_TELEVISION,
				.vmux = AU8522_COMPOSITE_CH4_SIF,
				.amux = AU8522_AUDIO_SIF,
			},
			{
				.type = AU0828_VMUX_COMPOSITE,
				.vmux = AU8522_COMPOSITE_CH1,
				.amux = AU8522_AUDIO_NONE,
				.audio_setup = hvr950q_cs5340_audio,
			},
			{
				.type = AU0828_VMUX_SVIDEO,
				.vmux = AU8522_SVIDEO_CH13,
				.amux = AU8522_AUDIO_NONE,
				.audio_setup = hvr950q_cs5340_audio,
			},
		},
	},
	[AU0828_BOARD_HAUPPAUGE_HVR950Q_MXL] = {
		.name	= "Hauppauge HVR950Q rev xxF8",
		.tuner_type = UNSET,
		.tuner_addr = ADDR_UNSET,
		.i2c_clk_divider = AU0828_I2C_CLK_250KHZ,
	},
	[AU0828_BOARD_DVICO_FUSIONHDTV7] = {
		.name	= "DViCO FusionHDTV USB",
		.tuner_type = UNSET,
		.tuner_addr = ADDR_UNSET,
		.i2c_clk_divider = AU0828_I2C_CLK_250KHZ,
	},
	[AU0828_BOARD_HAUPPAUGE_WOODBURY] = {
		.name = "Hauppauge Woodbury",
		.tuner_type = UNSET,
		.tuner_addr = ADDR_UNSET,
		.i2c_clk_divider = AU0828_I2C_CLK_250KHZ,
	},
};

/* Tuner callback function for au0828 boards. Currently only needed
 * for HVR1500Q, which has an xc5000 tuner.
 */
int au0828_tuner_callback(void *priv, int component, int command, int arg)
{
	struct au0828_dev *dev = priv;

	dprintk(1, "%s()\n", __func__);

	switch (dev->boardnr) {
	case AU0828_BOARD_HAUPPAUGE_HVR850:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q_MXL:
	case AU0828_BOARD_DVICO_FUSIONHDTV7:
		if (command == 0) {
			/* Tuner Reset Command from xc5000 */
			/* Drive the tuner into reset and out */
			au0828_clear(dev, REG_001, 2);
			mdelay(10);
			au0828_set(dev, REG_001, 2);
			mdelay(10);
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
	dev->board.tuner_type = tv.tuner_type;

	/* Make sure we support the board model */
	switch (tv.model) {
	case 72000: /* WinTV-HVR950q (Retail, IR, ATSC/QAM */
	case 72001: /* WinTV-HVR950q (Retail, IR, ATSC/QAM and analog video */
	case 72101: /* WinTV-HVR950q (Retail, IR, ATSC/QAM and analog video */
	case 72201: /* WinTV-HVR950q (OEM, IR, ATSC/QAM and analog video */
	case 72211: /* WinTV-HVR950q (OEM, IR, ATSC/QAM and analog video */
	case 72221: /* WinTV-HVR950q (OEM, IR, ATSC/QAM and analog video */
	case 72231: /* WinTV-HVR950q (OEM, IR, ATSC/QAM and analog video */
	case 72241: /* WinTV-HVR950q (OEM, No IR, ATSC/QAM and analog video */
	case 72251: /* WinTV-HVR950q (Retail, IR, ATSC/QAM and analog video */
	case 72261: /* WinTV-HVR950q (OEM, IR, ATSC/QAM and analog video */
	case 72301: /* WinTV-HVR850 (Retail, IR, ATSC and analog video */
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
	struct tuner_setup tun_setup;
	struct v4l2_subdev *sd;
	unsigned int mode_mask = T_ANALOG_TV;

	dprintk(1, "%s()\n", __func__);

	memcpy(&dev->board, &au0828_boards[dev->boardnr], sizeof(dev->board));

	if (dev->i2c_rc == 0) {
		dev->i2c_client.addr = 0xa0 >> 1;
		tveeprom_read(&dev->i2c_client, eeprom, sizeof(eeprom));
	}

	switch (dev->boardnr) {
	case AU0828_BOARD_HAUPPAUGE_HVR850:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q_MXL:
	case AU0828_BOARD_HAUPPAUGE_WOODBURY:
		if (dev->i2c_rc == 0)
			hauppauge_eeprom(dev, eeprom+0xa0);
		break;
	}

	if (AUVI_INPUT(0).type != AU0828_VMUX_UNDEFINED) {
		/* Load the analog demodulator driver (note this would need to
		   be abstracted out if we ever need to support a different
		   demod) */
		sd = v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
				"au8522", 0x8e >> 1, NULL);
		if (sd == NULL)
			printk(KERN_ERR "analog subdev registration failed\n");
	}

	/* Setup tuners */
	if (dev->board.tuner_type != TUNER_ABSENT) {
		/* Load the tuner module, which does the attach */
		sd = v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
				"tuner", dev->board.tuner_addr, NULL);
		if (sd == NULL)
			printk(KERN_ERR "tuner subdev registration fail\n");

		tun_setup.mode_mask      = mode_mask;
		tun_setup.type           = dev->board.tuner_type;
		tun_setup.addr           = dev->board.tuner_addr;
		tun_setup.tuner_callback = au0828_tuner_callback;
		v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_type_addr,
				     &tun_setup);
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

	switch (dev->boardnr) {
	case AU0828_BOARD_HAUPPAUGE_HVR850:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q_MXL:
	case AU0828_BOARD_HAUPPAUGE_WOODBURY:
		/* GPIO's
		 * 4 - CS5340
		 * 5 - AU8522 Demodulator
		 * 6 - eeprom W/P
		 * 7 - power supply
		 * 9 - XC5000 Tuner
		 */

		/* Into reset */
		au0828_write(dev, REG_003, 0x02);
		au0828_write(dev, REG_002, 0x80 | 0x20 | 0x10);
		au0828_write(dev, REG_001, 0x0);
		au0828_write(dev, REG_000, 0x0);
		msleep(100);

		/* Out of reset (leave the cs5340 in reset until needed) */
		au0828_write(dev, REG_003, 0x02);
		au0828_write(dev, REG_001, 0x02);
		au0828_write(dev, REG_002, 0x80 | 0x20 | 0x10);
		au0828_write(dev, REG_000, 0x80 | 0x40 | 0x20);

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
struct usb_device_id au0828_usb_id_table[] = {
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
	{ USB_DEVICE(0x2040, 0x721e),
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
	{ USB_DEVICE(0x05e1, 0x0480),
		.driver_info = AU0828_BOARD_HAUPPAUGE_WOODBURY },
	{ USB_DEVICE(0x2040, 0x8200),
		.driver_info = AU0828_BOARD_HAUPPAUGE_WOODBURY },
	{ USB_DEVICE(0x2040, 0x7260),
		.driver_info = AU0828_BOARD_HAUPPAUGE_HVR950Q },
	{ USB_DEVICE(0x2040, 0x7213),
		.driver_info = AU0828_BOARD_HAUPPAUGE_HVR950Q },
	{ },
};

MODULE_DEVICE_TABLE(usb, au0828_usb_id_table);
