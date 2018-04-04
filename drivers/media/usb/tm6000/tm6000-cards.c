// SPDX-License-Identifier: GPL-2.0
// tm6000-cards.c - driver for TM5600/TM6000/TM6010 USB video capture devices
//
// Copyright (c) 2006-2007 Mauro Carvalho Chehab <mchehab@infradead.org>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <media/v4l2-common.h>
#include <media/tuner.h>
#include <media/i2c/tvaudio.h>
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
#define TM6010_BOARD_BEHOLD_WANDER_LITE		14
#define TM6010_BOARD_BEHOLD_VOYAGER_LITE	15
#define TM5600_BOARD_TERRATEC_GRABSTER		16

#define is_generic(model) ((model == TM6000_BOARD_UNKNOWN) || \
			   (model == TM5600_BOARD_GENERIC) || \
			   (model == TM6000_BOARD_GENERIC) || \
			   (model == TM6010_BOARD_GENERIC))

#define TM6000_MAXBOARDS        16
static unsigned int card[]     = {[0 ... (TM6000_MAXBOARDS - 1)] = UNSET };

module_param_array(card,  int, NULL, 0444);

static unsigned long tm6000_devused;


struct tm6000_board {
	char            *name;
	char		eename[16];		/* EEPROM name */
	unsigned	eename_size;		/* size of EEPROM name */
	unsigned	eename_pos;		/* Position where it appears at ROM */

	struct tm6000_capabilities caps;

	enum		tm6000_devtype type;	/* variant of the chipset */
	int             tuner_type;     /* type of the tuner */
	int             tuner_addr;     /* tuner address */
	int             demod_addr;     /* demodulator address */

	struct tm6000_gpio gpio;

	struct tm6000_input	vinput[3];
	struct tm6000_input	rinput;

	char		*ir_codes;
};

static struct tm6000_board tm6000_boards[] = {
	[TM6000_BOARD_UNKNOWN] = {
		.name         = "Unknown tm6000 video grabber",
		.caps = {
			.has_tuner	= 1,
			.has_eeprom	= 1,
		},
		.gpio = {
			.tuner_reset	= TM6000_GPIO_1,
		},
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_ADC1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
		},
	},
	[TM5600_BOARD_GENERIC] = {
		.name         = "Generic tm5600 board",
		.type         = TM5600,
		.tuner_type   = TUNER_XC2028,
		.tuner_addr   = 0xc2 >> 1,
		.caps = {
			.has_tuner	= 1,
			.has_eeprom	= 1,
		},
		.gpio = {
			.tuner_reset	= TM6000_GPIO_1,
		},
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_ADC1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
		},
	},
	[TM6000_BOARD_GENERIC] = {
		.name         = "Generic tm6000 board",
		.tuner_type   = TUNER_XC2028,
		.tuner_addr   = 0xc2 >> 1,
		.caps = {
			.has_tuner	= 1,
			.has_eeprom	= 1,
		},
		.gpio = {
			.tuner_reset	= TM6000_GPIO_1,
		},
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_ADC1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
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
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_SIF1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
		},
	},
	[TM5600_BOARD_10MOONS_UT821] = {
		.name         = "10Moons UT 821",
		.tuner_type   = TUNER_XC2028,
		.eename       = { '1', '0', 'M', 'O', 'O', 'N', 'S', '5', '6', '0', '0', 0xff, 0x45, 0x5b},
		.eename_size  = 14,
		.eename_pos   = 0x14,
		.type         = TM5600,
		.tuner_addr   = 0xc2 >> 1,
		.caps = {
			.has_tuner    = 1,
			.has_eeprom   = 1,
		},
		.gpio = {
			.tuner_reset	= TM6000_GPIO_1,
		},
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_ADC1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
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
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_ADC1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
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
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_ADC1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
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
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_ADC1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
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
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_ADC1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
		},
	},
	[TM6010_BOARD_HAUPPAUGE_900H] = {
		.name         = "Hauppauge WinTV HVR-900H / WinTV USB2-Stick",
		.eename       = { 'H', 0, 'V', 0, 'R', 0, '9', 0, '0', 0, '0', 0, 'H', 0 },
		.eename_size  = 14,
		.eename_pos   = 0x42,
		.tuner_type   = TUNER_XC2028, /* has a XC3028 */
		.tuner_addr   = 0xc2 >> 1,
		.demod_addr   = 0x1e >> 1,
		.type         = TM6010,
		.ir_codes = RC_MAP_HAUPPAUGE,
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
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_SIF1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
		},
	},
	[TM6010_BOARD_BEHOLD_WANDER] = {
		.name         = "Beholder Wander DVB-T/TV/FM USB2.0",
		.tuner_type   = TUNER_XC5000,
		.tuner_addr   = 0xc2 >> 1,
		.demod_addr   = 0x1e >> 1,
		.type         = TM6010,
		.caps = {
			.has_tuner      = 1,
			.has_dvb        = 1,
			.has_zl10353    = 1,
			.has_eeprom     = 1,
			.has_remote     = 1,
			.has_radio	= 1,
		},
		.gpio = {
			.tuner_reset	= TM6010_GPIO_0,
			.demod_reset	= TM6010_GPIO_1,
			.power_led	= TM6010_GPIO_6,
		},
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_SIF1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
		},
		.rinput = {
			.type	= TM6000_INPUT_RADIO,
			.amux	= TM6000_AMUX_ADC1,
		},
	},
	[TM6010_BOARD_BEHOLD_VOYAGER] = {
		.name         = "Beholder Voyager TV/FM USB2.0",
		.tuner_type   = TUNER_XC5000,
		.tuner_addr   = 0xc2 >> 1,
		.type         = TM6010,
		.caps = {
			.has_tuner      = 1,
			.has_dvb        = 0,
			.has_zl10353    = 0,
			.has_eeprom     = 1,
			.has_remote     = 1,
			.has_radio	= 1,
		},
		.gpio = {
			.tuner_reset	= TM6010_GPIO_0,
			.power_led	= TM6010_GPIO_6,
		},
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_SIF1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
		},
		.rinput = {
			.type	= TM6000_INPUT_RADIO,
			.amux	= TM6000_AMUX_ADC1,
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
			.has_radio    = 1,
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
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_SIF1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
		},
		.rinput = {
			.type = TM6000_INPUT_RADIO,
			.amux = TM6000_AMUX_SIF1,
		},
	},
	[TM5600_BOARD_TERRATEC_GRABSTER] = {
		.name         = "Terratec Grabster AV 150/250 MX",
		.type         = TM5600,
		.tuner_type   = TUNER_ABSENT,
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_ADC1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
		},
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
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_SIF1,
			}, {
			.type	= TM6000_INPUT_COMPOSITE1,
			.vmux	= TM6000_VMUX_VIDEO_A,
			.amux	= TM6000_AMUX_ADC2,
			}, {
			.type	= TM6000_INPUT_SVIDEO,
			.vmux	= TM6000_VMUX_VIDEO_AB,
			.amux	= TM6000_AMUX_ADC2,
			},
		},
	},
	[TM6010_BOARD_BEHOLD_WANDER_LITE] = {
		.name         = "Beholder Wander Lite DVB-T/TV/FM USB2.0",
		.tuner_type   = TUNER_XC5000,
		.tuner_addr   = 0xc2 >> 1,
		.demod_addr   = 0x1e >> 1,
		.type         = TM6010,
		.caps = {
			.has_tuner      = 1,
			.has_dvb        = 1,
			.has_zl10353    = 1,
			.has_eeprom     = 1,
			.has_remote     = 0,
			.has_radio	= 1,
		},
		.gpio = {
			.tuner_reset	= TM6010_GPIO_0,
			.demod_reset	= TM6010_GPIO_1,
			.power_led	= TM6010_GPIO_6,
		},
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_SIF1,
			},
		},
		.rinput = {
			.type	= TM6000_INPUT_RADIO,
			.amux	= TM6000_AMUX_ADC1,
		},
	},
	[TM6010_BOARD_BEHOLD_VOYAGER_LITE] = {
		.name         = "Beholder Voyager Lite TV/FM USB2.0",
		.tuner_type   = TUNER_XC5000,
		.tuner_addr   = 0xc2 >> 1,
		.type         = TM6010,
		.caps = {
			.has_tuner      = 1,
			.has_dvb        = 0,
			.has_zl10353    = 0,
			.has_eeprom     = 1,
			.has_remote     = 0,
			.has_radio	= 1,
		},
		.gpio = {
			.tuner_reset	= TM6010_GPIO_0,
			.power_led	= TM6010_GPIO_6,
		},
		.vinput = { {
			.type	= TM6000_INPUT_TV,
			.vmux	= TM6000_VMUX_VIDEO_B,
			.amux	= TM6000_AMUX_SIF1,
			},
		},
		.rinput = {
			.type	= TM6000_INPUT_RADIO,
			.amux	= TM6000_AMUX_ADC1,
		},
	},
};

/* table of devices that work with this driver */
static const struct usb_device_id tm6000_id_table[] = {
	{ USB_DEVICE(0x6000, 0x0001), .driver_info = TM5600_BOARD_GENERIC },
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
	{ USB_DEVICE(0x0ccd, 0x0079), .driver_info = TM5600_BOARD_TERRATEC_GRABSTER },
	{ USB_DEVICE(0x13d3, 0x3240), .driver_info = TM6010_BOARD_TWINHAN_TU501 },
	{ USB_DEVICE(0x13d3, 0x3241), .driver_info = TM6010_BOARD_TWINHAN_TU501 },
	{ USB_DEVICE(0x13d3, 0x3243), .driver_info = TM6010_BOARD_TWINHAN_TU501 },
	{ USB_DEVICE(0x13d3, 0x3264), .driver_info = TM6010_BOARD_TWINHAN_TU501 },
	{ USB_DEVICE(0x6000, 0xdec2), .driver_info = TM6010_BOARD_BEHOLD_WANDER_LITE },
	{ USB_DEVICE(0x6000, 0xdec3), .driver_info = TM6010_BOARD_BEHOLD_VOYAGER_LITE },
	{ }
};
MODULE_DEVICE_TABLE(usb, tm6000_id_table);

/* Control power led for show some activity */
void tm6000_flash_led(struct tm6000_core *dev, u8 state)
{
	/* Power LED unconfigured */
	if (!dev->gpio.power_led)
		return;

	/* ON Power LED */
	if (state) {
		switch (dev->model) {
		case TM6010_BOARD_HAUPPAUGE_900H:
		case TM6010_BOARD_TERRATEC_CINERGY_HYBRID_XE:
		case TM6010_BOARD_TWINHAN_TU501:
			tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
				dev->gpio.power_led, 0x00);
			break;
		case TM6010_BOARD_BEHOLD_WANDER:
		case TM6010_BOARD_BEHOLD_VOYAGER:
		case TM6010_BOARD_BEHOLD_WANDER_LITE:
		case TM6010_BOARD_BEHOLD_VOYAGER_LITE:
			tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
				dev->gpio.power_led, 0x01);
			break;
		}
	}
	/* OFF Power LED */
	else {
		switch (dev->model) {
		case TM6010_BOARD_HAUPPAUGE_900H:
		case TM6010_BOARD_TERRATEC_CINERGY_HYBRID_XE:
		case TM6010_BOARD_TWINHAN_TU501:
			tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
				dev->gpio.power_led, 0x01);
			break;
		case TM6010_BOARD_BEHOLD_WANDER:
		case TM6010_BOARD_BEHOLD_VOYAGER:
		case TM6010_BOARD_BEHOLD_WANDER_LITE:
		case TM6010_BOARD_BEHOLD_VOYAGER_LITE:
			tm6000_set_reg(dev, REQ_03_SET_GET_MCU_PIN,
				dev->gpio.power_led, 0x00);
			break;
		}
	}
}

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
	return rc;
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
		break;
	case XC2028_I2C_FLUSH:
		tm6000_set_reg(dev, REQ_50_SET_START, 0, 0);
		tm6000_set_reg(dev, REQ_51_SET_STOP, 0, 0);
		break;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(tm6000_tuner_callback);

int tm6000_cards_setup(struct tm6000_core *dev)
{
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
	case TM6010_BOARD_BEHOLD_WANDER_LITE:
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
	case TM6010_BOARD_BEHOLD_VOYAGER_LITE:
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
		int rc;
		int i;

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
		"tuner", dev->tuner_addr, NULL);

	memset(&tun_setup, 0, sizeof(tun_setup));
	tun_setup.type = dev->tuner_type;
	tun_setup.addr = dev->tuner_addr;

	tun_setup.mode_mask = 0;
	if (dev->caps.has_tuner)
		tun_setup.mode_mask |= (T_ANALOG_TV | T_RADIO);

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

		ctl.demod = XC3028_FE_ZARLINK456;

		xc2028_cfg.tuner = TUNER_XC2028;
		xc2028_cfg.priv  = &ctl;

		switch (dev->model) {
		case TM6010_BOARD_HAUPPAUGE_900H:
		case TM6010_BOARD_TERRATEC_CINERGY_HYBRID_XE:
		case TM6010_BOARD_TWINHAN_TU501:
			ctl.max_len = 80;
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
			.radio_input = XC5000_RADIO_FM1_MONO,
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

static int fill_board_specific_data(struct tm6000_core *dev)
{
	int rc;

	dev->dev_type   = tm6000_boards[dev->model].type;
	dev->tuner_type = tm6000_boards[dev->model].tuner_type;
	dev->tuner_addr = tm6000_boards[dev->model].tuner_addr;

	dev->gpio = tm6000_boards[dev->model].gpio;

	dev->ir_codes = tm6000_boards[dev->model].ir_codes;

	dev->demod_addr = tm6000_boards[dev->model].demod_addr;

	dev->caps = tm6000_boards[dev->model].caps;

	dev->vinput[0] = tm6000_boards[dev->model].vinput[0];
	dev->vinput[1] = tm6000_boards[dev->model].vinput[1];
	dev->vinput[2] = tm6000_boards[dev->model].vinput[2];
	dev->rinput = tm6000_boards[dev->model].rinput;

	/* setup per-model quirks */
	switch (dev->model) {
	case TM6010_BOARD_TERRATEC_CINERGY_HYBRID_XE:
	case TM6010_BOARD_HAUPPAUGE_900H:
		dev->quirks |= TM6000_QUIRK_NO_USB_DELAY;
		break;

	default:
		break;
	}

	/* initialize hardware */
	rc = tm6000_init(dev);
	if (rc < 0)
		return rc;

	return v4l2_device_register(&dev->udev->dev, &dev->v4l2_dev);
}


static void use_alternative_detection_method(struct tm6000_core *dev)
{
	int i, model = -1;

	if (!dev->eedata_size)
		return;

	for (i = 0; i < ARRAY_SIZE(tm6000_boards); i++) {
		if (!tm6000_boards[i].eename_size)
			continue;
		if (dev->eedata_size < tm6000_boards[i].eename_pos +
				       tm6000_boards[i].eename_size)
			continue;

		if (!memcmp(&dev->eedata[tm6000_boards[i].eename_pos],
			    tm6000_boards[i].eename,
			    tm6000_boards[i].eename_size)) {
			model = i;
			break;
		}
	}
	if (model < 0) {
		printk(KERN_INFO "Device has eeprom but is currently unknown\n");
		return;
	}

	dev->model = model;

	printk(KERN_INFO "Device identified via eeprom as %s (type = %d)\n",
	       tm6000_boards[model].name, model);
}

#if defined(CONFIG_MODULES) && defined(MODULE)
static void request_module_async(struct work_struct *work)
{
	struct tm6000_core *dev = container_of(work, struct tm6000_core,
					       request_module_wk);

	request_module("tm6000-alsa");

	if (dev->caps.has_dvb)
		request_module("tm6000-dvb");
}

static void request_modules(struct tm6000_core *dev)
{
	INIT_WORK(&dev->request_module_wk, request_module_async);
	schedule_work(&dev->request_module_wk);
}

static void flush_request_modules(struct tm6000_core *dev)
{
	flush_work(&dev->request_module_wk);
}
#else
#define request_modules(dev)
#define flush_request_modules(dev)
#endif /* CONFIG_MODULES */

static int tm6000_init_dev(struct tm6000_core *dev)
{
	struct v4l2_frequency f;
	int rc = 0;

	mutex_init(&dev->lock);
	mutex_lock(&dev->lock);

	if (!is_generic(dev->model)) {
		rc = fill_board_specific_data(dev);
		if (rc < 0)
			goto err;

		/* register i2c bus */
		rc = tm6000_i2c_register(dev);
		if (rc < 0)
			goto err;
	} else {
		/* register i2c bus */
		rc = tm6000_i2c_register(dev);
		if (rc < 0)
			goto err;

		use_alternative_detection_method(dev);

		rc = fill_board_specific_data(dev);
		if (rc < 0)
			goto err;
	}

	/* Default values for STD and resolutions */
	dev->width = 720;
	dev->height = 480;
	dev->norm = V4L2_STD_NTSC_M;

	/* Configure tuner */
	tm6000_config_tuner(dev);

	/* Set video standard */
	v4l2_device_call_all(&dev->v4l2_dev, 0, video, s_std, dev->norm);

	/* Set tuner frequency - also loads firmware on xc2028/xc3028 */
	f.tuner = 0;
	f.type = V4L2_TUNER_ANALOG_TV;
	f.frequency = 3092;	/* 193.25 MHz */
	dev->freq = f.frequency;
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_frequency, &f);

	if (dev->caps.has_tda9874)
		v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
			"tvaudio", I2C_ADDR_TDA9874, NULL);

	/* register and initialize V4L2 */
	rc = tm6000_v4l2_register(dev);
	if (rc < 0)
		goto err;

	tm6000_add_into_devlist(dev);
	tm6000_init_extension(dev);

	tm6000_ir_init(dev);

	request_modules(dev);

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
	struct tm6000_core *dev;
	int i, rc;
	int nr = 0;
	char *speed;

	usbdev = usb_get_dev(interface_to_usbdev(interface));

	/* Selects the proper interface */
	rc = usb_set_interface(usbdev, 0, 1);
	if (rc < 0)
		goto report_failure;

	/* Check to see next free device and mark as used */
	nr = find_first_zero_bit(&tm6000_devused, TM6000_MAXBOARDS);
	if (nr >= TM6000_MAXBOARDS) {
		printk(KERN_ERR "tm6000: Supports only %i tm60xx boards.\n", TM6000_MAXBOARDS);
		rc = -ENOMEM;
		goto put_device;
	}

	/* Create and initialize dev struct */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		rc = -ENOMEM;
		goto put_device;
	}
	spin_lock_init(&dev->slock);
	mutex_init(&dev->usb_lock);

	/* Increment usage count */
	set_bit(nr, &tm6000_devused);
	snprintf(dev->name, 29, "tm6000 #%d", nr);

	dev->model = id->driver_info;
	if (card[nr] < ARRAY_SIZE(tm6000_boards))
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
		goto free_device;
	}

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	printk(KERN_INFO "tm6000: Found %s\n", tm6000_boards[dev->model].name);

	rc = tm6000_init_dev(dev);
	if (rc < 0)
		goto free_device;

	return 0;

free_device:
	kfree(dev);
report_failure:
	printk(KERN_ERR "tm6000: Error %d while registering\n", rc);

	clear_bit(nr, &tm6000_devused);
put_device:
	usb_put_dev(usbdev);
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

	flush_request_modules(dev);

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
		case TM6010_BOARD_BEHOLD_WANDER_LITE:
		case TM6010_BOARD_BEHOLD_VOYAGER_LITE:
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

	clear_bit(dev->devno, &tm6000_devused);
	kfree(dev);
}

static struct usb_driver tm6000_usb_driver = {
		.name = "tm6000",
		.probe = tm6000_usb_probe,
		.disconnect = tm6000_usb_disconnect,
		.id_table = tm6000_id_table,
};

module_usb_driver(tm6000_usb_driver);

MODULE_DESCRIPTION("Trident TVMaster TM5600/TM6000/TM6010 USB2 adapter");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_LICENSE("GPL v2");
