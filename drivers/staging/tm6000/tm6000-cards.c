/*
   tm6000-cards.c - driver for TM5600/TM6000/TM6010 USB video capture devices

   Copyright (C) 2006-2007 Mauro Carvalho Chehab <mchehab@infradead.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation version 2

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/usb.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <media/v4l2-common.h>
#include <media/tuner.h>
#include <media/tvaudio.h>
#include <media/i2c-addr.h>
#include <media/rc-map.h>

#include "tm6000.h"
#include "tm6000-regs.h"
#include "tuner-xc2028.h"
#include "xc5000.h"

#define TM6000_BOARD_UNKNOWN			0
#define TM5600_BOARD_GENERIC			1
#define TM6000_BOARD_GENERIC			2
#define TM6010_BOARD_GENERIC			3
#define TM5600_BOARD_10MOONS_UT821		4
#define TM5600_BOARD_10MOONS_UT330		5
#define TM6000_BOARD_ADSTECH_DUAL_TV		6
#define TM6000_BOARD_FREECOM_AND_SIMILAR	7
#define TM6000_BOARD_ADSTECH_MINI_DUAL_TV	8
#define TM6010_BOARD_HAUPPAUGE_900H		9
#define TM6010_BOARD_BEHOLD_WANDER		10
#define TM6010_BOARD_BEHOLD_VOYAGER		11
#define TM6010_BOARD_TERRATEC_CINERGY_HYBRID_XE	12
#define TM6010_BOARD_TWINHAN_TU501		13

#define TM6000_MAXBOARDS        16
static unsigned int card[]     = {[0 ... (TM6000_MAXBOARDS - 1)] = UNSET };

module_param_array(card,  int, NULL, 0444);

static unsigned long tm6000_devused;


struct tm6000_board {
	char            *name;

	struct tm6000_capabilities caps;

	enum		tm6000_devtype type;	/* variant of the chipset */
	int             tuner_type;     /* type of the tuner */
	int             tuner_addr;     /* tuner address */
	int             demod_addr;     /* demodulator address */

	struct tm6000_gpio gpio;

	char		*ir_codes;
};

struct tm6000_board tm6000_boards[] = {
	[TM6000_BOARD_UNKNOWN] = {
		.name         = "Unknown tm6000 video grabber",
		.caps = {
			.has_tuner    = 1,
		},
		.gpio = {
			.tuner_reset	= TM6000_GPIO_1,
		},
	},
	[TM5600_BOARD_GENERIC] = {
		.name         = "Generic tm5600 board",
		.type         = TM5600,
		.tuner_type   = TUNER_XC2028,
		.tuner_addr   = 0xc2 >> 1,
		.caps = {
			.has_tuner	= 1,
		},
		.gpio = {
			.tuner_reset	= TM6000_GPIO_1,
		},
	},
	[TM6000_BOARD_GENERIC] = {
		.name         = "Generic tm6000 board",
		.tuner_type   = TUNER_XC2028,
		.tuner_addr   = 0xc2 >> 1,
		.caps = {
			.has_tuner	= 1,
			.has_dvb	= 1,
		},
		.gpio = {
			.tuner_reset	= TM6000_GPIO_1,
		},
	},
	[TM6010_BOARD_GENERIC] = {
		.name         = "Generic tm6010 board",
		.type         = TM6010,
		.tuner_type   = TUNER_XC2028,
		.tuner_addr   = 0xc2 >> 1,
		.demod_addr   = 0x1e >> 1,
		.caps = {
			.has_tuner	= 1,
			.has_dvb	= 1,
			.has_zl10353	= 1,
			.has_eeprom	= 1,
			.has_remote	= 1,
		},
		.gpio = {
			.tuner_reset	= TM6010_GPIO_2,
			.tuner_on	= TM6010_GPIO_3,
			.demod_reset	= TM6010_GPIO_1,
			.demod_on	= TM6010_GPIO_4,
			.power_led	= TM6010_GPIO_7,
			.dvb_led	= TM6010_GPIO_5,
			.ir		= TM6010_GPIO_0,
		},
	},
	[TM5600_BOARD_10MOONS_UT821] = {
		.name         = "10Moons UT 821",
		.tuner_type   = TUNER_XC2028,
		.type         = TM5600,
		.tuner_addr   = 0xc2 >> 1,
		.caps = {
			.has_tuner    = 1,
			.has_eeprom   = 1,
		},
		.gpio = {
			.tuner_reset	= TM6000_GPIO_1,
		},
	},
	[TM5600_BOARD_10MOONS_UT330] = {
		.name         = "10Moons UT 330",
		.tuner_type   = TUNER_PHILIPS_FQ1216AME_MK4,
		.tuner_addr   = 0xc8 >> 1,
		.caps = {
			.has_tuner    = 1,
			.has_dvb      = 0,
			.has_zl10353  = 0,
			.has_eeprom   = 1,
		},
	},
	[TM6000_BOARD_ADSTECH_DUAL_TV] = {
		.name         = "ADSTECH Dual TV USB",
		.tuner_type   = TUNER_XC2028,
		.tuner_addr   = 0xc8 >> 1,
		.caps = {
			.has_tuner    = 1,
			.has_tda9874  = 1,
			.has_dvb      = 1,
			.has_zl10353  = 1,
			.has_eeprom   = 1,
		},
	},
	[TM6000_BOARD_FREECOM_AND_SIMILAR] = {
		.name         = "Freecom Hybrid Stick / Moka DVB-T Receiver Dual",
		.tuner_type   = TUNER_XC2028, /* has a XC3028 */
		.tuner_addr   = 0xc2 >> 1,
		.demod_addr   = 0x1e >> 1,
		.caps = {
			.has_tuner    = 1,
			.has_dvb      = 1,
			.has_zl10353  = 1,
			.has_eeprom   = 0,
			.has_remote   = 1,
		},
		.gpio = {
			.tuner_reset	= TM6000_GPIO_4,
		},
	},
	[TM6000_BOARD_ADSTECH_MINI_DUAL_TV] = {
		.name         = "ADSTECH Mini Dual TV USB",
		.tuner_type   = TUNER_XC2028, /* has a XC3028 */
		.tuner_addr   = 0xc8 >> 1,
		.demod_addr   = 0x1e >> 1,
		.caps = {
			.has_tuner    = 1,
			.has_dvb      = 1,
			.has_zl10353  = 1,
			.has_eeprom   = 0,
		},
		.gpio = {
			.tuner_reset	= TM6000_GPIO_4,
		},
	},
	[TM6010_BOARD_HAUPPAUGE_900H] = {
		.name         = "Hauppauge WinTV HVR-900H / WinTV USB2-Stick",
		.tuner_type   = TUNER_XC2028, /* has a XC3028 */
		.tuner_addr   = 0xc2 >> 1,
		.demod_addr   = 0x1e >> 1,
		.type         = TM6010,
		.caps = {
			.has_tuner    = 1,
			.has_dvb      = 1,
			.has_zl10353  = 1,
			.has_eeprom   = 1,
			.has_remote   = 1,
		},
		.gpio = {
			.tuner_reset	= TM6010_GPIO_2,
			.tuner_on	= TM6010_GPIO_3,
			.demod_reset	= TM6010_GPIO_1,
			.demod_on	= TM6010_GPIO_4,
			.power_led	= TM6010_GPIO_7,
			.dvb_led	= TM6010_GPIO_5,
			.ir		= TM6010_GPIO_0,
		},
	},
	[TM6010_BOARD_BEHOLD_WANDER] = {
		.name         = "Beholder Wander DVB-T/TV/FM USB2.0",
		.tuner_type   = TUNER_XC5000,
		.tuner_addr   = 0xc2 >> 1,
		.demod_addr   = 0x1e >> 1,
		.type         = TM6010,
		.caps = {
			.has_tuner    = 1,
			.has_dvb      = 1,
			.has_zl10353  = 1,
			.has_eeprom   = 1,
			.has_remote   = 1,
		},
		.gpio = {
			.tuner_reset	= TM6010_GPIO_0,
			.demod_reset	= TM6010_GPIO_1,
			.power_led	= TM6010_GPIO_6,
		},
	},
	[TM6010_BOARD_BEHOLD_VOYAGER] = {
		.name         = "Beholder Voyager TV/FM USB2.0",
		.tuner_type   = TUNER_XC5000,
		.tuner_addr   = 0xc2 >> 1,
		.type         = TM6010,
		.caps = {
			.has_tuner    = 1,
			.has_dvb      = 0,
			.has_zl10353  = 0,
			.has_eeprom   = 1,
			.has_remote   = 1,
		},
		.gpio = {
			.tuner_reset	= TM6010_GPIO_0,
			.power_led	= TM6010_GPIO_6,
		},
	},
	[TM6010_BOARD_TERRATEC_CINERGY_HYBRID_XE] = {
		.name         = "Terratec Cinergy Hybrid XE / Cinergy Hybrid-Stick",
		.tuner_type   = TUNER_XC2028, /* has a XC3028 */
		.tuner_addr   = 0xc2 >> 1,
		.demod_addr   = 0x1e >> 1,
		.type         = TM6010,
		.caps = {
			.has_tuner    = 1,
			.has_dvb      = 1,
			.has_zl10353  = 1,
			.has_eeprom   = 1,
			.has_remote   = 1,
		},
		.gpio = {
			.tuner_reset	= TM6010_GPIO_2,
			.tuner_on	= TM6010_GPIO_3,
			.demod_reset	= TM6010_GPIO_1,
			.demod_on	= TM6010_GPIO_4,
			.power_led	= TM6010_GPIO_7,
			.dvb_led	= TM6010_GPIO_5,
			.ir		= TM6010_GPIO_0,
		},
		.ir_codes = RC_MAP_NEC_TERRATEC_CINERGY_XS,
	},
	[TM6010_BOARD_TWINHAN_TU501] = {
		.name         = "Twinhan TU501(704D1)",
		.tuner_type   = TUNER_XC2028, /* has a XC3028 */
		.tuner_addr   = 0xc2 >> 1,
		.demod_addr   = 0x1e >> 1,
		.type         = TM6010,
		.caps = {
			.has_tuner    = 1,
			.has_dvb      = 1,
			.has_zl10353  = 1,
			.has_eeprom   = 1,
			.has_remote   = 1,
		},
		.gpio = {
			.tuner_reset	= TM6010_GPIO_2,
			.tuner_on	= TM6010_GPIO_3,
			.demod_reset	= TM6010_GPIO_1,
			.demod_on	= TM6010_GPIO_4,
			.power_led	= TM6010_GPIO_7,
			.dvb_led	= TM6010_GPIO_5,
			.ir		= TM6010_GPIO_0,
		},
	}
};

/* table of devices that work with this driver */
struct usb_device_id tm6000_id_table[] = {
	{ USB_DEVICE(0x6000, 0x0001), .driver_info = TM5600_BOARD_10MOONS_UT821 },
	{ USB_DEVICE(0x6000, 0x0002), .driver_info = TM6010_BOARD_GENERIC },
	{ USB_DEVICE(0x06e1, 0xf332), .driver_info = TM6000_BOARD_ADSTECH_DUAL_TV },
	{ USB_DEVICE(0x14aa, 0x0620), .driver_info = TM6000_BOARD_FREECOM_AND_SIMILAR },
	{ USB_DEVICE(0x06e1, 0xb339), .driver_info = TM6000_BOARD_ADSTECH_MINI_DUAL_TV },
	{ USB_DEVICE(0x2040, 0x6600), .driver_info = TM6010_BOARD_HAUPPAUGE_900H },
	{ USB_DEVICE(0x2040, 0x6601), .driver_info = TM6010_BOARD_HAUPPAUGE_900H },
	{ USB_DEVICE(0x2040, 0x6610), .driver_info = TM6010_BOARD_HAUPPAUGE_900H },
	{ USB_DEVICE(0x2040, 0x6611), .driver_info = TM6010_BOARD_HAUPPAUGE_900H },
	{ USB_DEVICE(0x6000, 0xdec0), .driver_info = TM6010_BOARD_BEHOLD_WANDER },
	{ USB_DEVICE(0x6000, 0xdec1), .driver_info = TM6010_BOARD_BEHOLD_VOYAGER },
	{ USB_DEVICE(0x0ccd, 0x0086), .driver_info = TM6010_BOARD_TERRATEC_CINERGY_HYBRID_XE },
	{ USB_DEVICE(0x0ccd, 0x00A5), .driver_info = TM6010_BOARD_TERRATEC_CINERGY_HYBRID_XE },
	{ USB_DEVICE(0x13d3, 0x3240), .driver_info = TM6010_BOARD_TWINHAN_TU501 },
	{ USB_DEVICE(0x13d3, 0x3241), .driver_info = TM6010_BOARD_TWINHAN_TU501 },
	{ USB_DEVICE(0x13d3, 0x3243), .driver_info = TM6010_BOARD_TWINHAN_TU501 },
	{ USB_DEVICE(0x13d3, 0x3264), .driver_info = TM6010_BOARD_TWINHAN_TU501 },
	{ },
};

/* Tuner callback to provide the proper gpio changes needed for xc5000 */
int tm6000_xc5000_callback(void *ptr, int component, int command, int arg)
{
	int rc = 0;
	struct tm6000_core *dev = ptr;

	if (dev->tuner_type != TUNER_XC5000)
		return 0;

	switch (command) {
	case XC5000_TUNER_RESET:
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
			       dev->gpio.tuner_reset, 0x01);
		msleep(15);
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
			       dev->gpio.tuner_reset, 0x00);
		msleep(15);
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
			       dev->gpio.tuner_reset, 0x01);
		break;
	}
	return (rc);
}
EXPORT_SYMBOL_GPL(tm6000_xc5000_callback);

/* Tuner callback to provide the proper gpio changes needed for xc2028 */

int tm6000_tuner_callback(void *ptr, int component, int command, int arg)
{
	int rc = 0;
	struct tm6000_core *dev = ptr;

	if (dev->tuner_type != TUNER_XC2028)
		return 0;

	switch (command) {
	case XC2028_RESET_CLK:
		tm6000_ir_wait(dev, 0);

		tm6000_set_reg(dev, REQ_04_EN_DISABLE_MCU_INT,
					0x02, arg);
		msleep(10);
		rc = tm6000_i2c_reset(dev, 10);
		break;
	case XC2028_TUNER_RESET:
		/* Reset codes during load firmware */
		switch (arg) {
		case 0:
			/* newer tuner can faster reset */
			switch (dev->model) {
			case TM5600_BOARD_10MOONS_UT821:
				tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
					       dev->gpio.tuner_reset, 0x01);
				tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
					       0x300, 0x01);
				msleep(10);
				tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
					       dev->gpio.tuner_reset, 0x00);
				tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
					       0x300, 0x00);
				msleep(10);
				tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
					       dev->gpio.tuner_reset, 0x01);
				tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
					       0x300, 0x01);
				break;
			case TM6010_BOARD_HAUPPAUGE_900H:
			case TM6010_BOARD_TERRATEC_CINERGY_HYBRID_XE:
			case TM6010_BOARD_TWINHAN_TU501:
				tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
					       dev->gpio.tuner_reset, 0x01);
				msleep(60);
				tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
					       dev->gpio.tuner_reset, 0x00);
				msleep(75);
				tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
					       dev->gpio.tuner_reset, 0x01);
				msleep(60);
				break;
			default:
				tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
					       dev->gpio.tuner_reset, 0x00);
				msleep(130);
				tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
					       dev->gpio.tuner_reset, 0x01);
				msleep(130);
				break;
			}

			tm6000_ir_wait(dev, 1);
			break;
		case 1:
			tm6000_set_reg(dev, REQ_04_EN_DISABLE_MCU_INT,
						0x02, 0x01);
			msleep(10);
			break;
		case 2:
			rc = tm6000_i2c_reset(dev, 100);
			break;
		}
	}
	return rc;
}
EXPORT_SYMBOL_GPL(tm6000_tuner_callback);

int tm6000_cards_setup(struct tm6000_core *dev)
{
	int i, rc;

	/*
	 * Board-specific initialization sequence. Handles all GPIO
	 * initialization sequences that are board-specific.
	 * Up to now, all found devices use GPIO1 and GPIO4 at the same way.
	 * Probably, they're all based on some reference device. Due to that,
	 * there's a common routine at the end to handle those GPIO's. Devices
	 * that use different pinups or init sequences can just return at
	 * the board-specific session.
	 */
	switch (dev->model) {
	case TM6010_BOARD_HAUPPAUGE_900H:
	case TM6010_BOARD_TERRATEC_CINERGY_HYBRID_XE:
	case TM6010_BOARD_TWINHAN_TU501:
	case TM6010_BOARD_GENERIC:
		/* Turn xceive 3028 on */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, dev->gpio.tuner_on, 0x01);
		msleep(15);
		/* Turn zarlink zl10353 on */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, dev->gpio.demod_on, 0x00);
		msleep(15);
		/* Reset zarlink zl10353 */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, dev->gpio.demod_reset, 0x00);
		msleep(50);
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, dev->gpio.demod_reset, 0x01);
		msleep(15);
		/* Turn zarlink zl10353 off */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, dev->gpio.demod_on, 0x01);
		msleep(15);
		/* ir ? */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, dev->gpio.ir, 0x01);
		msleep(15);
		/* Power led on (blue) */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, dev->gpio.power_led, 0x00);
		msleep(15);
		/* DVB led off (orange) */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, dev->gpio.dvb_led, 0x01);
		msleep(15);
		/* Turn zarlink zl10353 on */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, dev->gpio.demod_on, 0x00);
		msleep(15);
		break;
	case TM6010_BOARD_BEHOLD_WANDER:
		/* Power led on (blue) */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, dev->gpio.power_led, 0x01);
		msleep(15);
		/* Reset zarlink zl10353 */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, dev->gpio.demod_reset, 0x00);
		msleep(50);
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, dev->gpio.demod_reset, 0x01);
		msleep(15);
		break;
	case TM6010_BOARD_BEHOLD_VOYAGER:
		/* Power led on (blue) */
		tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN, dev->gpio.power_led, 0x01);
		msleep(15);
		break;
	default:
		break;
	}

	/*
	 * Default initialization. Most of the devices seem to use GPIO1
	 * and GPIO4.on the same way, so, this handles the common sequence
	 * used by most devices.
	 * If a device uses a different sequence or different GPIO pins for
	 * reset, just add the code at the board-specific part
	 */

	if (dev->gpio.tuner_reset) {
		for (i = 0; i < 2; i++) {
			rc = tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
						dev->gpio.tuner_reset, 0x00);
			if (rc < 0) {
				printk(KERN_ERR "Error %i doing tuner reset\n", rc);
				return rc;
			}

			msleep(10); /* Just to be conservative */
			rc = tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
						dev->gpio.tuner_reset, 0x01);
			if (rc < 0) {
				printk(KERN_ERR "Error %i doing tuner reset\n", rc);
				return rc;
			}
			msleep(10);

			if (!i) {
				rc = tm6000_get_reg32(dev, REQ_40_GET_VERSION, 0, 0);
				if (rc >= 0)
					printk(KERN_DEBUG "board=0x%08x\n", rc);
			}
		}
	} else {
		printk(KERN_ERR "Tuner reset is not configured\n");
		return -1;
	}

	msleep(50);

	return 0;
};

static void tm6000_config_tuner(struct tm6000_core *dev)
{
	struct tuner_setup tun_setup;

	/* Load tuner module */
	v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
		"tuner", "tuner", dev->tuner_addr, NULL);

	memset(&tun_setup, 0, sizeof(tun_setup));
	tun_setup.type = dev->tuner_type;
	tun_setup.addr = dev->tuner_addr;

	tun_setup.mode_mask = 0;
	if (dev->caps.has_tuner)
		tun_setup.mode_mask |= (T_ANALOG_TV | T_RADIO);
	if (dev->caps.has_dvb)
		tun_setup.mode_mask |= T_DIGITAL_TV;

	switch (dev->tuner_type) {
	case TUNER_XC2028:
		tun_setup.tuner_callback = tm6000_tuner_callback;
		break;
	case TUNER_XC5000:
		tun_setup.tuner_callback = tm6000_xc5000_callback;
		break;
	}

	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_type_addr, &tun_setup);

	switch (dev->tuner_type) {
	case TUNER_XC2028: {
		struct v4l2_priv_tun_config xc2028_cfg;
		struct xc2028_ctrl ctl;

		memset(&xc2028_cfg, 0, sizeof(xc2028_cfg));
		memset(&ctl, 0, sizeof(ctl));

		ctl.input1 = 1;
		ctl.read_not_reliable = 0;
		ctl.msleep = 10;
		ctl.demod = XC3028_FE_ZARLINK456;
		ctl.vhfbw7 = 1;
		ctl.uhfbw8 = 1;
		xc2028_cfg.tuner = TUNER_XC2028;
		xc2028_cfg.priv  = &ctl;

		switch (dev->model) {
		case TM6010_BOARD_HAUPPAUGE_900H:
		case TM6010_BOARD_TERRATEC_CINERGY_HYBRID_XE:
		case TM6010_BOARD_TWINHAN_TU501:
			ctl.fname = "xc3028L-v36.fw";
			break;
		default:
			if (dev->dev_type == TM6010)
				ctl.fname = "xc3028-v27.fw";
			else
				ctl.fname = "xc3028-v24.fw";
		}

		printk(KERN_INFO "Setting firmware parameters for xc2028\n");
		v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_config,
				     &xc2028_cfg);

		}
		break;
	case TUNER_XC5000:
		{
		struct v4l2_priv_tun_config  xc5000_cfg;
		struct xc5000_config ctl = {
			.i2c_address = dev->tuner_addr,
			.if_khz      = 4570,
			.radio_input = XC5000_RADIO_FM1,
			};

		xc5000_cfg.tuner = TUNER_XC5000;
		xc5000_cfg.priv  = &ctl;


		v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_config,
				     &xc5000_cfg);
		}
		break;
	default:
		printk(KERN_INFO "Unknown tuner type. Tuner is not configured.\n");
		break;
	}
}

static int tm6000_init_dev(struct tm6000_core *dev)
{
	struct v4l2_frequency f;
	int rc = 0;

	mutex_init(&dev->lock);

	mutex_lock(&dev->lock);

	/* Initializa board-specific data */
	dev->dev_type   = tm6000_boards[dev->model].type;
	dev->tuner_type = tm6000_boards[dev->model].tuner_type;
	dev->tuner_addr = tm6000_boards[dev->model].tuner_addr;

	dev->gpio = tm6000_boards[dev->model].gpio;

	dev->ir_codes = tm6000_boards[dev->model].ir_codes;

	dev->demod_addr = tm6000_boards[dev->model].demod_addr;

	dev->caps = tm6000_boards[dev->model].caps;

	/* initialize hardware */
	rc = tm6000_init(dev);
	if (rc < 0)
		goto err;

	rc = v4l2_device_register(&dev->udev->dev, &dev->v4l2_dev);
	if (rc < 0)
		goto err;

	/* register i2c bus */
	rc = tm6000_i2c_register(dev);
	if (rc < 0)
		goto err;

	/* Default values for STD and resolutions */
	dev->width = 720;
	dev->height = 480;
	dev->norm = V4L2_STD_PAL_M;

	/* Configure tuner */
	tm6000_config_tuner(dev);

	/* Set video standard */
	v4l2_device_call_all(&dev->v4l2_dev, 0, core, s_std, dev->norm);

	/* Set tuner frequency - also loads firmware on xc2028/xc3028 */
	f.tuner = 0;
	f.type = V4L2_TUNER_ANALOG_TV;
	f.frequency = 3092;	/* 193.25 MHz */
	dev->freq = f.frequency;
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_frequency, &f);

	if (dev->caps.has_tda9874)
		v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
			"tvaudio", "tvaudio", I2C_ADDR_TDA9874, NULL);

	/* register and initialize V4L2 */
	rc = tm6000_v4l2_register(dev);
	if (rc < 0)
		goto err;

	tm6000_add_into_devlist(dev);
	tm6000_init_extension(dev);

	tm6000_ir_init(dev);

	mutex_unlock(&dev->lock);
	return 0;

err:
	mutex_unlock(&dev->lock);
	return rc;
}

/* high bandwidth multiplier, as encoded in highspeed endpoint descriptors */
#define hb_mult(wMaxPacketSize) (1 + (((wMaxPacketSize) >> 11) & 0x03))

static void get_max_endpoint(struct usb_device *udev,
			     struct usb_host_interface *alt,
			     char *msgtype,
			     struct usb_host_endpoint *curr_e,
			     struct tm6000_endpoint *tm_ep)
{
	u16 tmp = le16_to_cpu(curr_e->desc.wMaxPacketSize);
	unsigned int size = tmp & 0x7ff;

	if (udev->speed == USB_SPEED_HIGH)
		size = size * hb_mult(tmp);

	if (size > tm_ep->maxsize) {
		tm_ep->endp = curr_e;
		tm_ep->maxsize = size;
		tm_ep->bInterfaceNumber = alt->desc.bInterfaceNumber;
		tm_ep->bAlternateSetting = alt->desc.bAlternateSetting;

		printk(KERN_INFO "tm6000: %s endpoint: 0x%02x (max size=%u bytes)\n",
					msgtype, curr_e->desc.bEndpointAddress,
					size);
	}
}

/*
 * tm6000_usb_probe()
 * checks for supported devices
 */
static int tm6000_usb_probe(struct usb_interface *interface,
			    const struct usb_device_id *id)
{
	struct usb_device *usbdev;
	struct tm6000_core *dev = NULL;
	int i, rc = 0;
	int nr = 0;
	char *speed;

	usbdev = usb_get_dev(interface_to_usbdev(interface));

	/* Selects the proper interface */
	rc = usb_set_interface(usbdev, 0, 1);
	if (rc < 0)
		goto err;

	/* Check to see next free device and mark as used */
	nr = find_first_zero_bit(&tm6000_devused, TM6000_MAXBOARDS);
	if (nr >= TM6000_MAXBOARDS) {
		printk(KERN_ERR "tm6000: Supports only %i tm60xx boards.\n", TM6000_MAXBOARDS);
		usb_put_dev(usbdev);
		return -ENOMEM;
	}

	/* Create and initialize dev struct */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		printk(KERN_ERR "tm6000" ": out of memory!\n");
		usb_put_dev(usbdev);
		return -ENOMEM;
	}
	spin_lock_init(&dev->slock);

	/* Increment usage count */
	tm6000_devused |= 1<<nr;
	snprintf(dev->name, 29, "tm6000 #%d", nr);

	dev->model = id->driver_info;
	if ((card[nr] >= 0) && (card[nr] < ARRAY_SIZE(tm6000_boards)))
		dev->model = card[nr];

	dev->udev = usbdev;
	dev->devno = nr;

	switch (usbdev->speed) {
	case USB_SPEED_LOW:
		speed = "1.5";
		break;
	case USB_SPEED_UNKNOWN:
	case USB_SPEED_FULL:
		speed = "12";
		break;
	case USB_SPEED_HIGH:
		speed = "480";
		break;
	default:
		speed = "unknown";
	}



	/* Get endpoints */
	for (i = 0; i < interface->num_altsetting; i++) {
		int ep;

		for (ep = 0; ep < interface->altsetting[i].desc.bNumEndpoints; ep++) {
			struct usb_host_endpoint	*e;
			int dir_out;

			e = &interface->altsetting[i].endpoint[ep];

			dir_out = ((e->desc.bEndpointAddress &
					USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT);

			printk(KERN_INFO "tm6000: alt %d, interface %i, class %i\n",
			       i,
			       interface->altsetting[i].desc.bInterfaceNumber,
			       interface->altsetting[i].desc.bInterfaceClass);

			switch (e->desc.bmAttributes) {
			case USB_ENDPOINT_XFER_BULK:
				if (!dir_out) {
					get_max_endpoint(usbdev,
							 &interface->altsetting[i],
							 "Bulk IN", e,
							 &dev->bulk_in);
				} else {
					get_max_endpoint(usbdev,
							 &interface->altsetting[i],
							 "Bulk OUT", e,
							 &dev->bulk_out);
				}
				break;
			case USB_ENDPOINT_XFER_ISOC:
				if (!dir_out) {
					get_max_endpoint(usbdev,
							 &interface->altsetting[i],
							 "ISOC IN", e,
							 &dev->isoc_in);
				} else {
					get_max_endpoint(usbdev,
							 &interface->altsetting[i],
							 "ISOC OUT", e,
							 &dev->isoc_out);
				}
				break;
			case USB_ENDPOINT_XFER_INT:
				if (!dir_out) {
					get_max_endpoint(usbdev,
							&interface->altsetting[i],
							"INT IN", e,
							&dev->int_in);
				} else {
					get_max_endpoint(usbdev,
							&interface->altsetting[i],
							"INT OUT", e,
							&dev->int_out);
				}
				break;
			}
		}
	}


	printk(KERN_INFO "tm6000: New video device @ %s Mbps (%04x:%04x, ifnum %d)\n",
		speed,
		le16_to_cpu(dev->udev->descriptor.idVendor),
		le16_to_cpu(dev->udev->descriptor.idProduct),
		interface->altsetting->desc.bInterfaceNumber);

/* check if the the device has the iso in endpoint at the correct place */
	if (!dev->isoc_in.endp) {
		printk(KERN_ERR "tm6000: probing error: no IN ISOC endpoint!\n");
		rc = -ENODEV;

		goto err;
	}

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	printk(KERN_INFO "tm6000: Found %s\n", tm6000_boards[dev->model].name);

	rc = tm6000_init_dev(dev);

	if (rc < 0)
		goto err;

	return 0;

err:
	printk(KERN_ERR "tm6000: Error %d while registering\n", rc);

	tm6000_devused &= ~(1<<nr);
	usb_put_dev(usbdev);

	kfree(dev);
	return rc;
}

/*
 * tm6000_usb_disconnect()
 * called when the device gets diconencted
 * video device will be unregistered on v4l2_close in case it is still open
 */
static void tm6000_usb_disconnect(struct usb_interface *interface)
{
	struct tm6000_core *dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	if (!dev)
		return;

	printk(KERN_INFO "tm6000: disconnecting %s\n", dev->name);

	mutex_lock(&dev->lock);

	tm6000_ir_fini(dev);

	if (dev->gpio.power_led) {
		switch (dev->model) {
		case TM6010_BOARD_HAUPPAUGE_900H:
		case TM6010_BOARD_TERRATEC_CINERGY_HYBRID_XE:
		case TM6010_BOARD_TWINHAN_TU501:
			/* Power led off */
			tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
				dev->gpio.power_led, 0x01);
			msleep(15);
			break;
		case TM6010_BOARD_BEHOLD_WANDER:
		case TM6010_BOARD_BEHOLD_VOYAGER:
			/* Power led off */
			tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
				dev->gpio.power_led, 0x00);
			msleep(15);
			break;
		}
	}
	tm6000_v4l2_unregister(dev);

	tm6000_i2c_unregister(dev);

	v4l2_device_unregister(&dev->v4l2_dev);

	dev->state |= DEV_DISCONNECTED;

	usb_put_dev(dev->udev);

	tm6000_close_extension(dev);
	tm6000_remove_from_devlist(dev);

	mutex_unlock(&dev->lock);
	kfree(dev);
}

static struct usb_driver tm6000_usb_driver = {
		.name = "tm6000",
		.probe = tm6000_usb_probe,
		.disconnect = tm6000_usb_disconnect,
		.id_table = tm6000_id_table,
};

static int __init tm6000_module_init(void)
{
	int result;

	printk(KERN_INFO "tm6000" " v4l2 driver version %d.%d.%d loaded\n",
	       (TM6000_VERSION  >> 16) & 0xff,
	       (TM6000_VERSION  >> 8) & 0xff, TM6000_VERSION  & 0xff);

	/* register this driver with the USB subsystem */
	result = usb_register(&tm6000_usb_driver);
	if (result)
		printk(KERN_ERR "tm6000"
			   " usb_register failed. Error number %d.\n", result);

	return result;
}

static void __exit tm6000_module_exit(void)
{
	/* deregister at USB subsystem */
	usb_deregister(&tm6000_usb_driver);
}

module_init(tm6000_module_init);
module_exit(tm6000_module_exit);

MODULE_DESCRIPTION("Trident TVMaster TM5600/TM6000/TM6010 USB2 adapter");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_LICENSE("GPL");
