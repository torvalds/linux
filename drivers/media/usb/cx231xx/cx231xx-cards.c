/*
   cx231xx-cards.c - driver for Conexant Cx23100/101/102
				USB video capture devices

   Copyright (C) 2008 <srinivasa.deevi at conexant dot com>
				Based on em28xx driver

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

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
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/usb.h>
#include <media/tuner.h>
#include <media/tveeprom.h>
#include <media/v4l2-common.h>

#include <media/cx25840.h>
#include "dvb-usb-ids.h"
#include "xc5000.h"
#include "tda18271.h"

#include "cx231xx.h"

static int tuner = -1;
module_param(tuner, int, 0444);
MODULE_PARM_DESC(tuner, "tuner type");

static int transfer_mode = 1;
module_param(transfer_mode, int, 0444);
MODULE_PARM_DESC(transfer_mode, "transfer mode (1-ISO or 0-BULK)");

static unsigned int disable_ir;
module_param(disable_ir, int, 0444);
MODULE_PARM_DESC(disable_ir, "disable infrared remote support");

/* Bitmask marking allocated devices from 0 to CX231XX_MAXBOARDS */
static unsigned long cx231xx_devused;

/*
 *  Reset sequences for analog/digital modes
 */

static struct cx231xx_reg_seq RDE250_XCV_TUNER[] = {
	{0x03, 0x01, 10},
	{0x03, 0x00, 30},
	{0x03, 0x01, 10},
	{-1, -1, -1},
};

/*
 *  Board definitions
 */
struct cx231xx_board cx231xx_boards[] = {
	[CX231XX_BOARD_UNKNOWN] = {
		.name = "Unknown CX231xx video grabber",
		.tuner_type = TUNER_ABSENT,
		.input = {{
				.type = CX231XX_VMUX_TELEVISION,
				.vmux = CX231XX_VIN_3_1,
				.amux = CX231XX_AMUX_VIDEO,
				.gpio = NULL,
			}, {
				.type = CX231XX_VMUX_COMPOSITE1,
				.vmux = CX231XX_VIN_2_1,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}, {
				.type = CX231XX_VMUX_SVIDEO,
				.vmux = CX231XX_VIN_1_1 |
					(CX231XX_VIN_1_2 << 8) |
					CX25840_SVIDEO_ON,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}
		},
	},
	[CX231XX_BOARD_CNXT_CARRAERA] = {
		.name = "Conexant Hybrid TV - CARRAERA",
		.tuner_type = TUNER_XC5000,
		.tuner_addr = 0x61,
		.tuner_gpio = RDE250_XCV_TUNER,
		.tuner_sif_gpio = 0x05,
		.tuner_scl_gpio = 0x1a,
		.tuner_sda_gpio = 0x1b,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 1,
		.demod_i2c_master = 2,
		.has_dvb = 1,
		.demod_addr = 0x02,
		.norm = V4L2_STD_PAL,

		.input = {{
				.type = CX231XX_VMUX_TELEVISION,
				.vmux = CX231XX_VIN_3_1,
				.amux = CX231XX_AMUX_VIDEO,
				.gpio = NULL,
			}, {
				.type = CX231XX_VMUX_COMPOSITE1,
				.vmux = CX231XX_VIN_2_1,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}, {
				.type = CX231XX_VMUX_SVIDEO,
				.vmux = CX231XX_VIN_1_1 |
					(CX231XX_VIN_1_2 << 8) |
					CX25840_SVIDEO_ON,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}
		},
	},
	[CX231XX_BOARD_CNXT_SHELBY] = {
		.name = "Conexant Hybrid TV - SHELBY",
		.tuner_type = TUNER_XC5000,
		.tuner_addr = 0x61,
		.tuner_gpio = RDE250_XCV_TUNER,
		.tuner_sif_gpio = 0x05,
		.tuner_scl_gpio = 0x1a,
		.tuner_sda_gpio = 0x1b,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 1,
		.demod_i2c_master = 2,
		.has_dvb = 1,
		.demod_addr = 0x32,
		.norm = V4L2_STD_NTSC,

		.input = {{
				.type = CX231XX_VMUX_TELEVISION,
				.vmux = CX231XX_VIN_3_1,
				.amux = CX231XX_AMUX_VIDEO,
				.gpio = NULL,
			}, {
				.type = CX231XX_VMUX_COMPOSITE1,
				.vmux = CX231XX_VIN_2_1,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}, {
				.type = CX231XX_VMUX_SVIDEO,
				.vmux = CX231XX_VIN_1_1 |
					(CX231XX_VIN_1_2 << 8) |
					CX25840_SVIDEO_ON,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}
		},
	},
	[CX231XX_BOARD_CNXT_RDE_253S] = {
		.name = "Conexant Hybrid TV - RDE253S",
		.tuner_type = TUNER_NXP_TDA18271,
		.tuner_addr = 0x60,
		.tuner_gpio = RDE250_XCV_TUNER,
		.tuner_sif_gpio = 0x05,
		.tuner_scl_gpio = 0x1a,
		.tuner_sda_gpio = 0x1b,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x1c,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 1,
		.demod_i2c_master = 2,
		.has_dvb = 1,
		.demod_addr = 0x02,
		.norm = V4L2_STD_PAL,

		.input = {{
				.type = CX231XX_VMUX_TELEVISION,
				.vmux = CX231XX_VIN_3_1,
				.amux = CX231XX_AMUX_VIDEO,
				.gpio = NULL,
			}, {
				.type = CX231XX_VMUX_COMPOSITE1,
				.vmux = CX231XX_VIN_2_1,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}, {
				.type = CX231XX_VMUX_SVIDEO,
				.vmux = CX231XX_VIN_1_1 |
					(CX231XX_VIN_1_2 << 8) |
					CX25840_SVIDEO_ON,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}
		},
	},

	[CX231XX_BOARD_CNXT_RDU_253S] = {
		.name = "Conexant Hybrid TV - RDU253S",
		.tuner_type = TUNER_NXP_TDA18271,
		.tuner_addr = 0x60,
		.tuner_gpio = RDE250_XCV_TUNER,
		.tuner_sif_gpio = 0x05,
		.tuner_scl_gpio = 0x1a,
		.tuner_sda_gpio = 0x1b,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x1c,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 1,
		.demod_i2c_master = 2,
		.has_dvb = 1,
		.demod_addr = 0x02,
		.norm = V4L2_STD_PAL,

		.input = {{
				.type = CX231XX_VMUX_TELEVISION,
				.vmux = CX231XX_VIN_3_1,
				.amux = CX231XX_AMUX_VIDEO,
				.gpio = NULL,
			}, {
				.type = CX231XX_VMUX_COMPOSITE1,
				.vmux = CX231XX_VIN_2_1,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}, {
				.type = CX231XX_VMUX_SVIDEO,
				.vmux = CX231XX_VIN_1_1 |
					(CX231XX_VIN_1_2 << 8) |
					CX25840_SVIDEO_ON,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}
		},
	},
	[CX231XX_BOARD_CNXT_VIDEO_GRABBER] = {
		.name = "Conexant VIDEO GRABBER",
		.tuner_type = TUNER_ABSENT,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x1c,
		.gpio_pin_status_mask = 0x4001000,
		.norm = V4L2_STD_PAL,
		.no_alt_vanc = 1,
		.external_av = 1,
		.dont_use_port_3 = 1,
		/* Actually, it has a 417, but it isn't working correctly.
		 * So set to 0 for now until someone can manage to get this
		 * to work reliably. */
		.has_417 = 0,

		.input = {{
				.type = CX231XX_VMUX_COMPOSITE1,
				.vmux = CX231XX_VIN_2_1,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}, {
				.type = CX231XX_VMUX_SVIDEO,
				.vmux = CX231XX_VIN_1_1 |
					(CX231XX_VIN_1_2 << 8) |
					CX25840_SVIDEO_ON,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}
		},
	},
	[CX231XX_BOARD_CNXT_RDE_250] = {
		.name = "Conexant Hybrid TV - rde 250",
		.tuner_type = TUNER_XC5000,
		.tuner_addr = 0x61,
		.tuner_gpio = RDE250_XCV_TUNER,
		.tuner_sif_gpio = 0x05,
		.tuner_scl_gpio = 0x1a,
		.tuner_sda_gpio = 0x1b,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 1,
		.demod_i2c_master = 2,
		.has_dvb = 1,
		.demod_addr = 0x02,
		.norm = V4L2_STD_PAL,

		.input = {{
				.type = CX231XX_VMUX_TELEVISION,
				.vmux = CX231XX_VIN_2_1,
				.amux = CX231XX_AMUX_VIDEO,
				.gpio = NULL,
			}
		},
	},
	[CX231XX_BOARD_CNXT_RDU_250] = {
		.name = "Conexant Hybrid TV - RDU 250",
		.tuner_type = TUNER_XC5000,
		.tuner_addr = 0x61,
		.tuner_gpio = RDE250_XCV_TUNER,
		.tuner_sif_gpio = 0x05,
		.tuner_scl_gpio = 0x1a,
		.tuner_sda_gpio = 0x1b,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 1,
		.demod_i2c_master = 2,
		.has_dvb = 1,
		.demod_addr = 0x32,
		.norm = V4L2_STD_NTSC,

		.input = {{
				.type = CX231XX_VMUX_TELEVISION,
				.vmux = CX231XX_VIN_2_1,
				.amux = CX231XX_AMUX_VIDEO,
				.gpio = NULL,
			}
		},
	},
	[CX231XX_BOARD_HAUPPAUGE_EXETER] = {
		.name = "Hauppauge EXETER",
		.tuner_type = TUNER_NXP_TDA18271,
		.tuner_addr = 0x60,
		.tuner_gpio = RDE250_XCV_TUNER,
		.tuner_sif_gpio = 0x05,
		.tuner_scl_gpio = 0x1a,
		.tuner_sda_gpio = 0x1b,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 1,
		.demod_i2c_master = 2,
		.has_dvb = 1,
		.demod_addr = 0x0e,
		.norm = V4L2_STD_NTSC,

		.input = {{
			.type = CX231XX_VMUX_TELEVISION,
			.vmux = CX231XX_VIN_3_1,
			.amux = CX231XX_AMUX_VIDEO,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_COMPOSITE1,
			.vmux = CX231XX_VIN_2_1,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_SVIDEO,
			.vmux = CX231XX_VIN_1_1 |
				(CX231XX_VIN_1_2 << 8) |
				CX25840_SVIDEO_ON,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		} },
	},
	[CX231XX_BOARD_HAUPPAUGE_USBLIVE2] = {
		.name = "Hauppauge USB Live 2",
		.tuner_type = TUNER_ABSENT,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.norm = V4L2_STD_NTSC,
		.no_alt_vanc = 1,
		.external_av = 1,
		.dont_use_port_3 = 1,
		.input = {{
			.type = CX231XX_VMUX_COMPOSITE1,
			.vmux = CX231XX_VIN_2_1,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_SVIDEO,
			.vmux = CX231XX_VIN_1_1 |
				(CX231XX_VIN_1_2 << 8) |
				CX25840_SVIDEO_ON,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		} },
	},
	[CX231XX_BOARD_KWORLD_UB430_USB_HYBRID] = {
		.name = "Kworld UB430 USB Hybrid",
		.tuner_type = TUNER_NXP_TDA18271,
		.tuner_addr = 0x60,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x11,	/* According with PV cxPolaris.inf file */
		.tuner_sif_gpio = -1,
		.tuner_scl_gpio = -1,
		.tuner_sda_gpio = -1,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 2,
		.demod_i2c_master = 1,
		.ir_i2c_master = 2,
		.has_dvb = 1,
		.demod_addr = 0x10,
		.norm = V4L2_STD_PAL_M,
		.input = {{
			.type = CX231XX_VMUX_TELEVISION,
			.vmux = CX231XX_VIN_3_1,
			.amux = CX231XX_AMUX_VIDEO,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_COMPOSITE1,
			.vmux = CX231XX_VIN_2_1,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_SVIDEO,
			.vmux = CX231XX_VIN_1_1 |
				(CX231XX_VIN_1_2 << 8) |
				CX25840_SVIDEO_ON,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		} },
	},
	[CX231XX_BOARD_KWORLD_UB445_USB_HYBRID] = {
		.name = "Kworld UB445 USB Hybrid",
		.tuner_type = TUNER_NXP_TDA18271,
		.tuner_addr = 0x60,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x11,	/* According with PV cxPolaris.inf file */
		.tuner_sif_gpio = -1,
		.tuner_scl_gpio = -1,
		.tuner_sda_gpio = -1,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 2,
		.demod_i2c_master = 1,
		.ir_i2c_master = 2,
		.has_dvb = 1,
		.demod_addr = 0x10,
		.norm = V4L2_STD_NTSC_M,
		.input = {{
			.type = CX231XX_VMUX_TELEVISION,
			.vmux = CX231XX_VIN_3_1,
			.amux = CX231XX_AMUX_VIDEO,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_COMPOSITE1,
			.vmux = CX231XX_VIN_2_1,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_SVIDEO,
			.vmux = CX231XX_VIN_1_1 |
				(CX231XX_VIN_1_2 << 8) |
				CX25840_SVIDEO_ON,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		} },
	},
	[CX231XX_BOARD_PV_PLAYTV_USB_HYBRID] = {
		.name = "Pixelview PlayTV USB Hybrid",
		.tuner_type = TUNER_NXP_TDA18271,
		.tuner_addr = 0x60,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x00,	/* According with PV cxPolaris.inf file */
		.tuner_sif_gpio = -1,
		.tuner_scl_gpio = -1,
		.tuner_sda_gpio = -1,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 2,
		.demod_i2c_master = 1,
		.ir_i2c_master = 2,
		.rc_map_name = RC_MAP_PIXELVIEW_002T,
		.has_dvb = 1,
		.demod_addr = 0x10,
		.norm = V4L2_STD_PAL_M,
		.input = {{
			.type = CX231XX_VMUX_TELEVISION,
			.vmux = CX231XX_VIN_3_1,
			.amux = CX231XX_AMUX_VIDEO,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_COMPOSITE1,
			.vmux = CX231XX_VIN_2_1,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_SVIDEO,
			.vmux = CX231XX_VIN_1_1 |
				(CX231XX_VIN_1_2 << 8) |
				CX25840_SVIDEO_ON,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		} },
	},
	[CX231XX_BOARD_PV_XCAPTURE_USB] = {
		.name = "Pixelview Xcapture USB",
		.tuner_type = TUNER_ABSENT,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.norm = V4L2_STD_NTSC,
		.no_alt_vanc = 1,
		.external_av = 1,
		.dont_use_port_3 = 1,

		.input = {{
				.type = CX231XX_VMUX_COMPOSITE1,
				.vmux = CX231XX_VIN_2_1,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}, {
				.type = CX231XX_VMUX_SVIDEO,
				.vmux = CX231XX_VIN_1_1 |
					(CX231XX_VIN_1_2 << 8) |
					CX25840_SVIDEO_ON,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}
		},
	},

	[CX231XX_BOARD_ICONBIT_U100] = {
		.name = "Iconbit Analog Stick U100 FM",
		.tuner_type = TUNER_ABSENT,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x1C,
		.gpio_pin_status_mask = 0x4001000,

		.input = {{
			.type = CX231XX_VMUX_COMPOSITE1,
			.vmux = CX231XX_VIN_2_1,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_SVIDEO,
			.vmux = CX231XX_VIN_1_1 |
				(CX231XX_VIN_1_2 << 8) |
				CX25840_SVIDEO_ON,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		} },
	},
	[CX231XX_BOARD_HAUPPAUGE_USB2_FM_PAL] = {
		.name = "Hauppauge WinTV USB2 FM (PAL)",
		.tuner_type = TUNER_NXP_TDA18271,
		.tuner_addr = 0x60,
		.tuner_gpio = RDE250_XCV_TUNER,
		.tuner_sif_gpio = 0x05,
		.tuner_scl_gpio = 0x1a,
		.tuner_sda_gpio = 0x1b,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 1,
		.norm = V4L2_STD_PAL,

		.input = {{
			.type = CX231XX_VMUX_TELEVISION,
			.vmux = CX231XX_VIN_3_1,
			.amux = CX231XX_AMUX_VIDEO,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_COMPOSITE1,
			.vmux = CX231XX_VIN_2_1,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_SVIDEO,
			.vmux = CX231XX_VIN_1_1 |
				(CX231XX_VIN_1_2 << 8) |
				CX25840_SVIDEO_ON,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		} },
	},
	[CX231XX_BOARD_HAUPPAUGE_USB2_FM_NTSC] = {
		.name = "Hauppauge WinTV USB2 FM (NTSC)",
		.tuner_type = TUNER_NXP_TDA18271,
		.tuner_addr = 0x60,
		.tuner_gpio = RDE250_XCV_TUNER,
		.tuner_sif_gpio = 0x05,
		.tuner_scl_gpio = 0x1a,
		.tuner_sda_gpio = 0x1b,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 1,
		.norm = V4L2_STD_NTSC,

		.input = {{
			.type = CX231XX_VMUX_TELEVISION,
			.vmux = CX231XX_VIN_3_1,
			.amux = CX231XX_AMUX_VIDEO,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_COMPOSITE1,
			.vmux = CX231XX_VIN_2_1,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_SVIDEO,
			.vmux = CX231XX_VIN_1_1 |
				(CX231XX_VIN_1_2 << 8) |
				CX25840_SVIDEO_ON,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		} },
	},
	[CX231XX_BOARD_ELGATO_VIDEO_CAPTURE_V2] = {
		.name = "Elgato Video Capture V2",
		.tuner_type = TUNER_ABSENT,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.norm = V4L2_STD_NTSC,
		.no_alt_vanc = 1,
		.external_av = 1,
		.dont_use_port_3 = 1,
		.input = {{
			.type = CX231XX_VMUX_COMPOSITE1,
			.vmux = CX231XX_VIN_2_1,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_SVIDEO,
			.vmux = CX231XX_VIN_1_1 |
				(CX231XX_VIN_1_2 << 8) |
				CX25840_SVIDEO_ON,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		} },
	},
	[CX231XX_BOARD_OTG102] = {
		.name = "Geniatech OTG102",
		.tuner_type = TUNER_ABSENT,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c, 
			/* According with PV CxPlrCAP.inf file */
		.gpio_pin_status_mask = 0x4001000,
		.norm = V4L2_STD_NTSC,
		.no_alt_vanc = 1,
		.external_av = 1,
		.dont_use_port_3 = 1,
		/*.has_417 = 1, */
		/* This board is believed to have a hardware encoding chip
		 * supporting mpeg1/2/4, but as the 417 is apparently not
		 * working for the reference board it is not here either. */

		.input = {{
				.type = CX231XX_VMUX_COMPOSITE1,
				.vmux = CX231XX_VIN_2_1,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}, {
				.type = CX231XX_VMUX_SVIDEO,
				.vmux = CX231XX_VIN_1_1 |
					(CX231XX_VIN_1_2 << 8) |
					CX25840_SVIDEO_ON,
				.amux = CX231XX_AMUX_LINE_IN,
				.gpio = NULL,
			}
		},
	},
	[CX231XX_BOARD_HAUPPAUGE_930C_HD_1113xx] = {
		.name = "Hauppauge WinTV 930C-HD (1113xx) / HVR-900H (111xxx) / PCTV QuatroStick 521e",
		.tuner_type = TUNER_NXP_TDA18271,
		.tuner_addr = 0x60,
		.tuner_gpio = RDE250_XCV_TUNER,
		.tuner_sif_gpio = 0x05,
		.tuner_scl_gpio = 0x1a,
		.tuner_sda_gpio = 0x1b,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 1,
		.demod_i2c_master = 2,
		.has_dvb = 1,
		.demod_addr = 0x0e,
		.norm = V4L2_STD_PAL,

		.input = {{
			.type = CX231XX_VMUX_TELEVISION,
			.vmux = CX231XX_VIN_3_1,
			.amux = CX231XX_AMUX_VIDEO,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_COMPOSITE1,
			.vmux = CX231XX_VIN_2_1,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_SVIDEO,
			.vmux = CX231XX_VIN_1_1 |
				(CX231XX_VIN_1_2 << 8) |
				CX25840_SVIDEO_ON,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		} },
	},
	[CX231XX_BOARD_HAUPPAUGE_930C_HD_1114xx] = {
		.name = "Hauppauge WinTV 930C-HD (1114xx) / HVR-901H (1114xx) / PCTV QuatroStick 522e",
		.tuner_type = TUNER_ABSENT,
		.tuner_addr = 0x60,
		.tuner_gpio = RDE250_XCV_TUNER,
		.tuner_sif_gpio = 0x05,
		.tuner_scl_gpio = 0x1a,
		.tuner_sda_gpio = 0x1b,
		.decoder = CX231XX_AVDECODER,
		.output_mode = OUT_MODE_VIP11,
		.demod_xfer_mode = 0,
		.ctl_pin_status_mask = 0xFFFFFFC4,
		.agc_analog_digital_select_gpio = 0x0c,
		.gpio_pin_status_mask = 0x4001000,
		.tuner_i2c_master = 1,
		.demod_i2c_master = 2,
		.has_dvb = 1,
		.demod_addr = 0x0e,
		.norm = V4L2_STD_PAL,

		.input = {{
			.type = CX231XX_VMUX_TELEVISION,
			.vmux = CX231XX_VIN_3_1,
			.amux = CX231XX_AMUX_VIDEO,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_COMPOSITE1,
			.vmux = CX231XX_VIN_2_1,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		}, {
			.type = CX231XX_VMUX_SVIDEO,
			.vmux = CX231XX_VIN_1_1 |
				(CX231XX_VIN_1_2 << 8) |
				CX25840_SVIDEO_ON,
			.amux = CX231XX_AMUX_LINE_IN,
			.gpio = NULL,
		} },
	},
};
const unsigned int cx231xx_bcount = ARRAY_SIZE(cx231xx_boards);

/* table of devices that work with this driver */
struct usb_device_id cx231xx_id_table[] = {
	{USB_DEVICE(0x1D19, 0x6109),
	.driver_info = CX231XX_BOARD_PV_XCAPTURE_USB},
	{USB_DEVICE(0x0572, 0x5A3C),
	 .driver_info = CX231XX_BOARD_UNKNOWN},
	{USB_DEVICE(0x0572, 0x58A2),
	 .driver_info = CX231XX_BOARD_CNXT_CARRAERA},
	{USB_DEVICE(0x0572, 0x58A1),
	 .driver_info = CX231XX_BOARD_CNXT_SHELBY},
	{USB_DEVICE(0x0572, 0x58A4),
	 .driver_info = CX231XX_BOARD_CNXT_RDE_253S},
	{USB_DEVICE(0x0572, 0x58A5),
	 .driver_info = CX231XX_BOARD_CNXT_RDU_253S},
	{USB_DEVICE(0x0572, 0x58A6),
	 .driver_info = CX231XX_BOARD_CNXT_VIDEO_GRABBER},
	{USB_DEVICE(0x0572, 0x589E),
	 .driver_info = CX231XX_BOARD_CNXT_RDE_250},
	{USB_DEVICE(0x0572, 0x58A0),
	 .driver_info = CX231XX_BOARD_CNXT_RDU_250},
	{USB_DEVICE(0x2040, 0xb110),
	 .driver_info = CX231XX_BOARD_HAUPPAUGE_USB2_FM_PAL},
	{USB_DEVICE(0x2040, 0xb111),
	 .driver_info = CX231XX_BOARD_HAUPPAUGE_USB2_FM_NTSC},
	{USB_DEVICE(0x2040, 0xb120),
	 .driver_info = CX231XX_BOARD_HAUPPAUGE_EXETER},
	{USB_DEVICE(0x2040, 0xb130),
	 .driver_info = CX231XX_BOARD_HAUPPAUGE_930C_HD_1113xx},
	{USB_DEVICE(0x2040, 0xb131),
	 .driver_info = CX231XX_BOARD_HAUPPAUGE_930C_HD_1114xx},
	/* Hauppauge WinTV-HVR-900-H */
	{USB_DEVICE(0x2040, 0xb138),
	 .driver_info = CX231XX_BOARD_HAUPPAUGE_930C_HD_1113xx},
	/* Hauppauge WinTV-HVR-901-H */
	{USB_DEVICE(0x2040, 0xb139),
	 .driver_info = CX231XX_BOARD_HAUPPAUGE_930C_HD_1114xx},
	{USB_DEVICE(0x2040, 0xb140),
	 .driver_info = CX231XX_BOARD_HAUPPAUGE_EXETER},
	{USB_DEVICE(0x2040, 0xc200),
	 .driver_info = CX231XX_BOARD_HAUPPAUGE_USBLIVE2},
	/* PCTV QuatroStick 521e */
	{USB_DEVICE(0x2013, 0x0259),
	 .driver_info = CX231XX_BOARD_HAUPPAUGE_930C_HD_1113xx},
	/* PCTV QuatroStick 522e */
	{USB_DEVICE(0x2013, 0x025e),
	 .driver_info = CX231XX_BOARD_HAUPPAUGE_930C_HD_1114xx},
	{USB_DEVICE_VER(USB_VID_PIXELVIEW, USB_PID_PIXELVIEW_SBTVD, 0x4000, 0x4001),
	 .driver_info = CX231XX_BOARD_PV_PLAYTV_USB_HYBRID},
	{USB_DEVICE(USB_VID_PIXELVIEW, 0x5014),
	 .driver_info = CX231XX_BOARD_PV_XCAPTURE_USB},
	{USB_DEVICE(0x1b80, 0xe424),
	 .driver_info = CX231XX_BOARD_KWORLD_UB430_USB_HYBRID},
	{USB_DEVICE(0x1b80, 0xe421),
	 .driver_info = CX231XX_BOARD_KWORLD_UB445_USB_HYBRID},
	{USB_DEVICE(0x1f4d, 0x0237),
	 .driver_info = CX231XX_BOARD_ICONBIT_U100},
	{USB_DEVICE(0x0fd9, 0x0037),
	 .driver_info = CX231XX_BOARD_ELGATO_VIDEO_CAPTURE_V2},
	{USB_DEVICE(0x1f4d, 0x0102),
	 .driver_info = CX231XX_BOARD_OTG102},
	{},
};

MODULE_DEVICE_TABLE(usb, cx231xx_id_table);

/* cx231xx_tuner_callback
 * will be used to reset XC5000 tuner using GPIO pin
 */

int cx231xx_tuner_callback(void *ptr, int component, int command, int arg)
{
	int rc = 0;
	struct cx231xx *dev = ptr;

	if (dev->tuner_type == TUNER_XC5000) {
		if (command == XC5000_TUNER_RESET) {
			cx231xx_info
				("Tuner CB: RESET: cmd %d : tuner type %d \n",
				 command, dev->tuner_type);
			cx231xx_set_gpio_value(dev, dev->board.tuner_gpio->bit,
					       1);
			msleep(10);
			cx231xx_set_gpio_value(dev, dev->board.tuner_gpio->bit,
					       0);
			msleep(330);
			cx231xx_set_gpio_value(dev, dev->board.tuner_gpio->bit,
					       1);
			msleep(10);
		}
	} else if (dev->tuner_type == TUNER_NXP_TDA18271) {
		switch (command) {
		case TDA18271_CALLBACK_CMD_AGC_ENABLE:
			if (dev->model == CX231XX_BOARD_PV_PLAYTV_USB_HYBRID)
				rc = cx231xx_set_agc_analog_digital_mux_select(dev, arg);
			break;
		default:
			rc = -EINVAL;
			break;
		}
	}
	return rc;
}
EXPORT_SYMBOL_GPL(cx231xx_tuner_callback);

static void cx231xx_reset_out(struct cx231xx *dev)
{
	cx231xx_set_gpio_value(dev, CX23417_RESET, 1);
	msleep(200);
	cx231xx_set_gpio_value(dev, CX23417_RESET, 0);
	msleep(200);
	cx231xx_set_gpio_value(dev, CX23417_RESET, 1);
}

static void cx231xx_enable_OSC(struct cx231xx *dev)
{
	cx231xx_set_gpio_value(dev, CX23417_OSC_EN, 1);
}

static void cx231xx_sleep_s5h1432(struct cx231xx *dev)
{
	cx231xx_set_gpio_value(dev, SLEEP_S5H1432, 0);
}

static inline void cx231xx_set_model(struct cx231xx *dev)
{
	dev->board = cx231xx_boards[dev->model];
}

/* Since cx231xx_pre_card_setup() requires a proper dev->model,
 * this won't work for boards with generic PCI IDs
 */
void cx231xx_pre_card_setup(struct cx231xx *dev)
{

	cx231xx_set_model(dev);

	cx231xx_info("Identified as %s (card=%d)\n",
		     dev->board.name, dev->model);

	/* set the direction for GPIO pins */
	if (dev->board.tuner_gpio) {
		cx231xx_set_gpio_direction(dev, dev->board.tuner_gpio->bit, 1);
		cx231xx_set_gpio_value(dev, dev->board.tuner_gpio->bit, 1);
	}
	if (dev->board.tuner_sif_gpio >= 0)
		cx231xx_set_gpio_direction(dev, dev->board.tuner_sif_gpio, 1);

	/* request some modules if any required */

	/* set the mode to Analog mode initially */
	cx231xx_set_mode(dev, CX231XX_ANALOG_MODE);

	/* Unlock device */
	/* cx231xx_set_mode(dev, CX231XX_SUSPEND); */

}

static void cx231xx_config_tuner(struct cx231xx *dev)
{
	struct tuner_setup tun_setup;
	struct v4l2_frequency f;

	if (dev->tuner_type == TUNER_ABSENT)
		return;

	tun_setup.mode_mask = T_ANALOG_TV | T_RADIO;
	tun_setup.type = dev->tuner_type;
	tun_setup.addr = dev->tuner_addr;
	tun_setup.tuner_callback = cx231xx_tuner_callback;

	tuner_call(dev, tuner, s_type_addr, &tun_setup);

#if 0
	if (tun_setup.type == TUNER_XC5000) {
		static struct xc2028_ctrl ctrl = {
			.fname = XC5000_DEFAULT_FIRMWARE,
			.max_len = 64,
			.demod = 0;
		};
		struct v4l2_priv_tun_config cfg = {
			.tuner = dev->tuner_type,
			.priv = &ctrl,
		};
		tuner_call(dev, tuner, s_config, &cfg);
	}
#endif
	/* configure tuner */
	f.tuner = 0;
	f.type = V4L2_TUNER_ANALOG_TV;
	f.frequency = 9076;	/* just a magic number */
	dev->ctl_freq = f.frequency;
	call_all(dev, tuner, s_frequency, &f);

}

static int read_eeprom(struct cx231xx *dev, u8 *eedata, int len)
{
	int ret = 0;
	u8 addr = 0xa0 >> 1;
	u8 start_offset = 0;
	int len_todo = len;
	u8 *eedata_cur = eedata;
	int i;
	struct i2c_msg msg_write = { .addr = addr, .flags = 0,
		.buf = &start_offset, .len = 1 };
	struct i2c_msg msg_read = { .addr = addr, .flags = I2C_M_RD };

	/* mutex_lock(&dev->i2c_lock); */
	cx231xx_enable_i2c_port_3(dev, false);

	/* start reading at offset 0 */
	ret = i2c_transfer(&dev->i2c_bus[1].i2c_adap, &msg_write, 1);
	if (ret < 0) {
		cx231xx_err("Can't read eeprom\n");
		return ret;
	}

	while (len_todo > 0) {
		msg_read.len = (len_todo > 64) ? 64 : len_todo;
		msg_read.buf = eedata_cur;

		ret = i2c_transfer(&dev->i2c_bus[1].i2c_adap, &msg_read, 1);
		if (ret < 0) {
			cx231xx_err("Can't read eeprom\n");
			return ret;
		}
		eedata_cur += msg_read.len;
		len_todo -= msg_read.len;
	}

	cx231xx_enable_i2c_port_3(dev, true);
	/* mutex_unlock(&dev->i2c_lock); */

	for (i = 0; i + 15 < len; i += 16)
		cx231xx_info("i2c eeprom %02x: %*ph\n", i, 16, &eedata[i]);

	return 0;
}

void cx231xx_card_setup(struct cx231xx *dev)
{

	cx231xx_set_model(dev);

	dev->tuner_type = cx231xx_boards[dev->model].tuner_type;
	if (cx231xx_boards[dev->model].tuner_addr)
		dev->tuner_addr = cx231xx_boards[dev->model].tuner_addr;

	/* request some modules */
	if (dev->board.decoder == CX231XX_AVDECODER) {
		dev->sd_cx25840 = v4l2_i2c_new_subdev(&dev->v4l2_dev,
					&dev->i2c_bus[0].i2c_adap,
					"cx25840", 0x88 >> 1, NULL);
		if (dev->sd_cx25840 == NULL)
			cx231xx_info("cx25840 subdev registration failure\n");
		cx25840_call(dev, core, load_fw);

	}

	/* Initialize the tuner */
	if (dev->board.tuner_type != TUNER_ABSENT) {
		dev->sd_tuner = v4l2_i2c_new_subdev(&dev->v4l2_dev,
						    &dev->i2c_bus[dev->board.tuner_i2c_master].i2c_adap,
						    "tuner",
						    dev->tuner_addr, NULL);
		if (dev->sd_tuner == NULL)
			cx231xx_info("tuner subdev registration failure\n");
		else
			cx231xx_config_tuner(dev);
	}

	switch (dev->model) {
	case CX231XX_BOARD_HAUPPAUGE_930C_HD_1113xx:
	case CX231XX_BOARD_HAUPPAUGE_930C_HD_1114xx:
		{
			struct tveeprom tvee;
			static u8 eeprom[256];

			read_eeprom(dev, eeprom, sizeof(eeprom));
			tveeprom_hauppauge_analog(&dev->i2c_bus[1].i2c_client,
						&tvee, eeprom + 0xc0);
			break;
		}
	}

}

/*
 * cx231xx_config()
 * inits registers with sane defaults
 */
int cx231xx_config(struct cx231xx *dev)
{
	/* TBD need to add cx231xx specific code */

	return 0;
}

/*
 * cx231xx_config_i2c()
 * configure i2c attached devices
 */
void cx231xx_config_i2c(struct cx231xx *dev)
{
	/* u32 input = INPUT(dev->video_input)->vmux; */

	call_all(dev, video, s_stream, 1);
}

/*
 * cx231xx_realease_resources()
 * unregisters the v4l2,i2c and usb devices
 * called when the device gets disconected or at module unload
*/
void cx231xx_release_resources(struct cx231xx *dev)
{
	cx231xx_release_analog_resources(dev);

	cx231xx_remove_from_devlist(dev);

	cx231xx_ir_exit(dev);

	/* Release I2C buses */
	cx231xx_dev_uninit(dev);

	/* delete v4l2 device */
	v4l2_device_unregister(&dev->v4l2_dev);

	usb_put_dev(dev->udev);

	/* Mark device as unused */
	clear_bit(dev->devno, &cx231xx_devused);
}

/*
 * cx231xx_init_dev()
 * allocates and inits the device structs, registers i2c bus and v4l device
 */
static int cx231xx_init_dev(struct cx231xx *dev, struct usb_device *udev,
			    int minor)
{
	int retval = -ENOMEM;
	unsigned int maxh, maxw;

	dev->udev = udev;
	mutex_init(&dev->lock);
	mutex_init(&dev->ctrl_urb_lock);
	mutex_init(&dev->gpio_i2c_lock);
	mutex_init(&dev->i2c_lock);

	spin_lock_init(&dev->video_mode.slock);
	spin_lock_init(&dev->vbi_mode.slock);
	spin_lock_init(&dev->sliced_cc_mode.slock);

	init_waitqueue_head(&dev->open);
	init_waitqueue_head(&dev->wait_frame);
	init_waitqueue_head(&dev->wait_stream);

	dev->cx231xx_read_ctrl_reg = cx231xx_read_ctrl_reg;
	dev->cx231xx_write_ctrl_reg = cx231xx_write_ctrl_reg;
	dev->cx231xx_send_usb_command = cx231xx_send_usb_command;
	dev->cx231xx_gpio_i2c_read = cx231xx_gpio_i2c_read;
	dev->cx231xx_gpio_i2c_write = cx231xx_gpio_i2c_write;

	/* Query cx231xx to find what pcb config it is related to */
	retval = initialize_cx231xx(dev);
	if (retval < 0) {
		cx231xx_errdev("Failed to read PCB config\n");
		return retval;
	}

	/*To workaround error number=-71 on EP0 for VideoGrabber,
		 need set alt here.*/
	if (dev->model == CX231XX_BOARD_CNXT_VIDEO_GRABBER ||
	    dev->model == CX231XX_BOARD_HAUPPAUGE_USBLIVE2) {
		cx231xx_set_alt_setting(dev, INDEX_VIDEO, 3);
		cx231xx_set_alt_setting(dev, INDEX_VANC, 1);
	}
	/* Cx231xx pre card setup */
	cx231xx_pre_card_setup(dev);

	retval = cx231xx_config(dev);
	if (retval) {
		cx231xx_errdev("error configuring device\n");
		return -ENOMEM;
	}

	/* set default norm */
	dev->norm = dev->board.norm;

	/* register i2c bus */
	retval = cx231xx_dev_init(dev);
	if (retval) {
		cx231xx_errdev("%s: cx231xx_i2c_register - errCode [%d]!\n",
			       __func__, retval);
		goto err_dev_init;
	}

	/* Do board specific init */
	cx231xx_card_setup(dev);

	/* configure the device */
	cx231xx_config_i2c(dev);

	maxw = norm_maxw(dev);
	maxh = norm_maxh(dev);

	/* set default image size */
	dev->width = maxw;
	dev->height = maxh;
	dev->interlaced = 0;
	dev->video_input = 0;

	retval = cx231xx_config(dev);
	if (retval) {
		cx231xx_errdev("%s: cx231xx_config - errCode [%d]!\n",
			       __func__, retval);
		goto err_dev_init;
	}

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->video_mode.vidq.active);
	INIT_LIST_HEAD(&dev->video_mode.vidq.queued);

	/* init vbi dma queues */
	INIT_LIST_HEAD(&dev->vbi_mode.vidq.active);
	INIT_LIST_HEAD(&dev->vbi_mode.vidq.queued);

	/* Reset other chips required if they are tied up with GPIO pins */
	cx231xx_add_into_devlist(dev);

	if (dev->board.has_417) {
		printk(KERN_INFO "attach 417 %d\n", dev->model);
		if (cx231xx_417_register(dev) < 0) {
			printk(KERN_ERR
				"%s() Failed to register 417 on VID_B\n",
			       __func__);
		}
	}

	retval = cx231xx_register_analog_devices(dev);
	if (retval) {
		cx231xx_release_analog_resources(dev);
		goto err_analog;
	}

	cx231xx_ir_init(dev);

	cx231xx_init_extension(dev);

	return 0;
err_analog:
	cx231xx_remove_from_devlist(dev);
err_dev_init:
	cx231xx_dev_uninit(dev);
	return retval;
}

#if defined(CONFIG_MODULES) && defined(MODULE)
static void request_module_async(struct work_struct *work)
{
	struct cx231xx *dev = container_of(work,
					   struct cx231xx, request_module_wk);

	if (dev->has_alsa_audio)
		request_module("cx231xx-alsa");

	if (dev->board.has_dvb)
		request_module("cx231xx-dvb");

}

static void request_modules(struct cx231xx *dev)
{
	INIT_WORK(&dev->request_module_wk, request_module_async);
	schedule_work(&dev->request_module_wk);
}

static void flush_request_modules(struct cx231xx *dev)
{
	flush_work(&dev->request_module_wk);
}
#else
#define request_modules(dev)
#define flush_request_modules(dev)
#endif /* CONFIG_MODULES */

static int cx231xx_init_v4l2(struct cx231xx *dev,
			     struct usb_device *udev,
			     struct usb_interface *interface,
			     int isoc_pipe)
{
	struct usb_interface *uif;
	int i, idx;

	/* Video Init */

	/* compute alternate max packet sizes for video */
	idx = dev->current_pcb_config.hs_config_info[0].interface_info.video_index + 1;
	if (idx >= dev->max_iad_interface_count) {
		cx231xx_errdev("Video PCB interface #%d doesn't exist\n", idx);
		return -ENODEV;
	}

	uif = udev->actconfig->interface[idx];

	dev->video_mode.end_point_addr = uif->altsetting[0].endpoint[isoc_pipe].desc.bEndpointAddress;
	dev->video_mode.num_alt = uif->num_altsetting;

	cx231xx_info("EndPoint Addr 0x%x, Alternate settings: %i\n",
		     dev->video_mode.end_point_addr,
		     dev->video_mode.num_alt);

	dev->video_mode.alt_max_pkt_size = devm_kmalloc_array(&udev->dev, 32, dev->video_mode.num_alt, GFP_KERNEL);
	if (dev->video_mode.alt_max_pkt_size == NULL) {
		cx231xx_errdev("out of memory!\n");
		return -ENOMEM;
	}

	for (i = 0; i < dev->video_mode.num_alt; i++) {
		u16 tmp = le16_to_cpu(uif->altsetting[i].endpoint[isoc_pipe].desc.wMaxPacketSize);
		dev->video_mode.alt_max_pkt_size[i] = (tmp & 0x07ff) * (((tmp & 0x1800) >> 11) + 1);
		cx231xx_info("Alternate setting %i, max size= %i\n", i,
			     dev->video_mode.alt_max_pkt_size[i]);
	}

	/* VBI Init */

	idx = dev->current_pcb_config.hs_config_info[0].interface_info.vanc_index + 1;
	if (idx >= dev->max_iad_interface_count) {
		cx231xx_errdev("VBI PCB interface #%d doesn't exist\n", idx);
		return -ENODEV;
	}
	uif = udev->actconfig->interface[idx];

	dev->vbi_mode.end_point_addr =
	    uif->altsetting[0].endpoint[isoc_pipe].desc.
			bEndpointAddress;

	dev->vbi_mode.num_alt = uif->num_altsetting;
	cx231xx_info("EndPoint Addr 0x%x, Alternate settings: %i\n",
		     dev->vbi_mode.end_point_addr,
		     dev->vbi_mode.num_alt);

	/* compute alternate max packet sizes for vbi */
	dev->vbi_mode.alt_max_pkt_size = devm_kmalloc_array(&udev->dev, 32, dev->vbi_mode.num_alt, GFP_KERNEL);
	if (dev->vbi_mode.alt_max_pkt_size == NULL) {
		cx231xx_errdev("out of memory!\n");
		return -ENOMEM;
	}

	for (i = 0; i < dev->vbi_mode.num_alt; i++) {
		u16 tmp =
		    le16_to_cpu(uif->altsetting[i].endpoint[isoc_pipe].
				desc.wMaxPacketSize);
		dev->vbi_mode.alt_max_pkt_size[i] =
		    (tmp & 0x07ff) * (((tmp & 0x1800) >> 11) + 1);
		cx231xx_info("Alternate setting %i, max size= %i\n", i,
			     dev->vbi_mode.alt_max_pkt_size[i]);
	}

	/* Sliced CC VBI init */

	/* compute alternate max packet sizes for sliced CC */
	idx = dev->current_pcb_config.hs_config_info[0].interface_info.hanc_index + 1;
	if (idx >= dev->max_iad_interface_count) {
		cx231xx_errdev("Sliced CC PCB interface #%d doesn't exist\n", idx);
		return -ENODEV;
	}
	uif = udev->actconfig->interface[idx];

	dev->sliced_cc_mode.end_point_addr =
	    uif->altsetting[0].endpoint[isoc_pipe].desc.
			bEndpointAddress;

	dev->sliced_cc_mode.num_alt = uif->num_altsetting;
	cx231xx_info("EndPoint Addr 0x%x, Alternate settings: %i\n",
		     dev->sliced_cc_mode.end_point_addr,
		     dev->sliced_cc_mode.num_alt);
	dev->sliced_cc_mode.alt_max_pkt_size = devm_kmalloc_array(&udev->dev, 32, dev->sliced_cc_mode.num_alt, GFP_KERNEL);

	if (dev->sliced_cc_mode.alt_max_pkt_size == NULL) {
		cx231xx_errdev("out of memory!\n");
		return -ENOMEM;
	}

	for (i = 0; i < dev->sliced_cc_mode.num_alt; i++) {
		u16 tmp = le16_to_cpu(uif->altsetting[i].endpoint[isoc_pipe].
				desc.wMaxPacketSize);
		dev->sliced_cc_mode.alt_max_pkt_size[i] =
		    (tmp & 0x07ff) * (((tmp & 0x1800) >> 11) + 1);
		cx231xx_info("Alternate setting %i, max size= %i\n", i,
			     dev->sliced_cc_mode.alt_max_pkt_size[i]);
	}

	return 0;
}

/*
 * cx231xx_usb_probe()
 * checks for supported devices
 */
static int cx231xx_usb_probe(struct usb_interface *interface,
			     const struct usb_device_id *id)
{
	struct usb_device *udev;
	struct usb_interface *uif;
	struct cx231xx *dev = NULL;
	int retval = -ENODEV;
	int nr = 0, ifnum;
	int i, isoc_pipe = 0;
	char *speed;
	u8 idx;
	struct usb_interface_assoc_descriptor *assoc_desc;

	ifnum = interface->altsetting[0].desc.bInterfaceNumber;

	/*
	 * Interface number 0 - IR interface (handled by mceusb driver)
	 * Interface number 1 - AV interface (handled by this driver)
	 */
	if (ifnum != 1)
		return -ENODEV;

	/* Check to see next free device and mark as used */
	do {
		nr = find_first_zero_bit(&cx231xx_devused, CX231XX_MAXBOARDS);
		if (nr >= CX231XX_MAXBOARDS) {
			/* No free device slots */
			cx231xx_err(DRIVER_NAME ": Supports only %i devices.\n",
					CX231XX_MAXBOARDS);
			return -ENOMEM;
		}
	} while (test_and_set_bit(nr, &cx231xx_devused));

	udev = usb_get_dev(interface_to_usbdev(interface));

	/* allocate memory for our device state and initialize it */
	dev = devm_kzalloc(&udev->dev, sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		cx231xx_err(DRIVER_NAME ": out of memory!\n");
		clear_bit(nr, &cx231xx_devused);
		return -ENOMEM;
	}

	snprintf(dev->name, 29, "cx231xx #%d", nr);
	dev->devno = nr;
	dev->model = id->driver_info;
	dev->video_mode.alt = -1;

	dev->interface_count++;
	/* reset gpio dir and value */
	dev->gpio_dir = 0;
	dev->gpio_val = 0;
	dev->xc_fw_load_done = 0;
	dev->has_alsa_audio = 1;
	dev->power_mode = -1;
	atomic_set(&dev->devlist_count, 0);

	/* 0 - vbi ; 1 -sliced cc mode */
	dev->vbi_or_sliced_cc_mode = 0;

	/* get maximum no.of IAD interfaces */
	dev->max_iad_interface_count = udev->config->desc.bNumInterfaces;

	/* init CIR module TBD */

	/*mode_tv: digital=1 or analog=0*/
	dev->mode_tv = 0;

	dev->USE_ISO = transfer_mode;

	switch (udev->speed) {
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

	cx231xx_info("New device %s %s @ %s Mbps "
	     "(%04x:%04x) with %d interfaces\n",
	     udev->manufacturer ? udev->manufacturer : "",
	     udev->product ? udev->product : "",
	     speed,
	     le16_to_cpu(udev->descriptor.idVendor),
	     le16_to_cpu(udev->descriptor.idProduct),
	     dev->max_iad_interface_count);

	/* increment interface count */
	dev->interface_count++;

	/* get device number */
	nr = dev->devno;

	assoc_desc = udev->actconfig->intf_assoc[0];
	if (assoc_desc->bFirstInterface != ifnum) {
		cx231xx_err(DRIVER_NAME ": Not found "
			    "matching IAD interface\n");
		retval = -ENODEV;
		goto err_if;
	}

	cx231xx_info("registering interface %d\n", ifnum);

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* Create v4l2 device */
	retval = v4l2_device_register(&interface->dev, &dev->v4l2_dev);
	if (retval) {
		cx231xx_errdev("v4l2_device_register failed\n");
		goto err_v4l2;
	}

	/* allocate device struct */
	retval = cx231xx_init_dev(dev, udev, nr);
	if (retval)
		goto err_init;

	retval = cx231xx_init_v4l2(dev, udev, interface, isoc_pipe);
	if (retval)
		goto err_init;

	if (dev->current_pcb_config.ts1_source != 0xff) {
		/* compute alternate max packet sizes for TS1 */
		idx = dev->current_pcb_config.hs_config_info[0].interface_info.ts1_index + 1;
		if (idx >= dev->max_iad_interface_count) {
			cx231xx_errdev("TS1 PCB interface #%d doesn't exist\n", idx);
			retval = -ENODEV;
			goto err_video_alt;
		}
		uif = udev->actconfig->interface[idx];

		dev->ts1_mode.end_point_addr =
		    uif->altsetting[0].endpoint[isoc_pipe].
				desc.bEndpointAddress;

		dev->ts1_mode.num_alt = uif->num_altsetting;
		cx231xx_info("EndPoint Addr 0x%x, Alternate settings: %i\n",
			     dev->ts1_mode.end_point_addr,
			     dev->ts1_mode.num_alt);

		dev->ts1_mode.alt_max_pkt_size = devm_kmalloc_array(&udev->dev, 32, dev->ts1_mode.num_alt, GFP_KERNEL);
		if (dev->ts1_mode.alt_max_pkt_size == NULL) {
			cx231xx_errdev("out of memory!\n");
			retval = -ENOMEM;
			goto err_video_alt;
		}

		for (i = 0; i < dev->ts1_mode.num_alt; i++) {
			u16 tmp = le16_to_cpu(uif->altsetting[i].
						endpoint[isoc_pipe].desc.
						wMaxPacketSize);
			dev->ts1_mode.alt_max_pkt_size[i] =
			    (tmp & 0x07ff) * (((tmp & 0x1800) >> 11) + 1);
			cx231xx_info("Alternate setting %i, max size= %i\n", i,
				     dev->ts1_mode.alt_max_pkt_size[i]);
		}
	}

	if (dev->model == CX231XX_BOARD_CNXT_VIDEO_GRABBER) {
		cx231xx_enable_OSC(dev);
		cx231xx_reset_out(dev);
		cx231xx_set_alt_setting(dev, INDEX_VIDEO, 3);
	}

	if (dev->model == CX231XX_BOARD_CNXT_RDE_253S)
		cx231xx_sleep_s5h1432(dev);

	/* load other modules required */
	request_modules(dev);

	return 0;
err_video_alt:
	/* cx231xx_uninit_dev: */
	cx231xx_close_extension(dev);
	cx231xx_ir_exit(dev);
	cx231xx_release_analog_resources(dev);
	cx231xx_417_unregister(dev);
	cx231xx_remove_from_devlist(dev);
	cx231xx_dev_uninit(dev);
err_init:
	v4l2_device_unregister(&dev->v4l2_dev);
err_v4l2:
	usb_set_intfdata(interface, NULL);
err_if:
	usb_put_dev(udev);
	clear_bit(dev->devno, &cx231xx_devused);
	return retval;
}

/*
 * cx231xx_usb_disconnect()
 * called when the device gets diconencted
 * video device will be unregistered on v4l2_close in case it is still open
 */
static void cx231xx_usb_disconnect(struct usb_interface *interface)
{
	struct cx231xx *dev;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	if (!dev)
		return;

	if (!dev->udev)
		return;

	dev->state |= DEV_DISCONNECTED;

	flush_request_modules(dev);

	/* wait until all current v4l2 io is finished then deallocate
	   resources */
	mutex_lock(&dev->lock);

	wake_up_interruptible_all(&dev->open);

	if (dev->users) {
		cx231xx_warn
		    ("device %s is open! Deregistration and memory "
		     "deallocation are deferred on close.\n",
		     video_device_node_name(dev->vdev));

		/* Even having users, it is safe to remove the RC i2c driver */
		cx231xx_ir_exit(dev);

		if (dev->USE_ISO)
			cx231xx_uninit_isoc(dev);
		else
			cx231xx_uninit_bulk(dev);
		wake_up_interruptible(&dev->wait_frame);
		wake_up_interruptible(&dev->wait_stream);
	} else {
	}

	cx231xx_close_extension(dev);

	mutex_unlock(&dev->lock);

	if (!dev->users)
		cx231xx_release_resources(dev);
}

static struct usb_driver cx231xx_usb_driver = {
	.name = "cx231xx",
	.probe = cx231xx_usb_probe,
	.disconnect = cx231xx_usb_disconnect,
	.id_table = cx231xx_id_table,
};

module_usb_driver(cx231xx_usb_driver);
