/*
    Functions to query card hardware
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ivtv-driver.h"
#include "ivtv-cards.h"
#include "ivtv-i2c.h"

#include <media/msp3400.h>
#include <media/m52790.h>
#include <media/wm8775.h>
#include <media/cs53l32a.h>
#include <media/cx25840.h>
#include <media/upd64031a.h>

#define MSP_TUNER  MSP_INPUT(MSP_IN_SCART1, MSP_IN_TUNER1, \
				MSP_DSP_IN_TUNER, MSP_DSP_IN_TUNER)
#define MSP_SCART1 MSP_INPUT(MSP_IN_SCART1, MSP_IN_TUNER1, \
				MSP_DSP_IN_SCART, MSP_DSP_IN_SCART)
#define MSP_SCART2 MSP_INPUT(MSP_IN_SCART2, MSP_IN_TUNER1, \
				MSP_DSP_IN_SCART, MSP_DSP_IN_SCART)
#define MSP_SCART3 MSP_INPUT(MSP_IN_SCART3, MSP_IN_TUNER1, \
				MSP_DSP_IN_SCART, MSP_DSP_IN_SCART)
#define MSP_MONO   MSP_INPUT(MSP_IN_MONO, MSP_IN_TUNER1, \
				MSP_DSP_IN_SCART, MSP_DSP_IN_SCART)

#define V4L2_STD_PAL_SECAM (V4L2_STD_PAL|V4L2_STD_SECAM)

/* usual i2c tuner addresses to probe */
static struct ivtv_card_tuner_i2c ivtv_i2c_std = {
	.radio = { I2C_CLIENT_END },
	.demod = { 0x43, I2C_CLIENT_END },
	.tv    = { 0x61, 0x60, I2C_CLIENT_END },
};

/* as above, but with possible radio tuner */
static struct ivtv_card_tuner_i2c ivtv_i2c_radio = {
	.radio = { 0x60, I2C_CLIENT_END },
	.demod = { 0x43, I2C_CLIENT_END },
	.tv    = { 0x61, I2C_CLIENT_END },
};

/* using the tda8290+75a combo */
static struct ivtv_card_tuner_i2c ivtv_i2c_tda8290 = {
	.radio = { I2C_CLIENT_END },
	.demod = { I2C_CLIENT_END },
	.tv    = { 0x4b, I2C_CLIENT_END },
};

/********************** card configuration *******************************/

/* Please add new PCI IDs to: http://pci-ids.ucw.cz/ 
   This keeps the PCI ID database up to date. Note that the entries
   must be added under vendor 0x4444 (Conexant) as subsystem IDs.
   New vendor IDs should still be added to the vendor ID list. */

/* Hauppauge PVR-250 cards */

/* Note: for Hauppauge cards the tveeprom information is used instead of PCI IDs */
static const struct ivtv_card ivtv_card_pvr250 = {
	.type = IVTV_CARD_PVR_250,
	.name = "Hauppauge WinTV PVR-250",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA7115,
	.hw_audio = IVTV_HW_MSP34XX,
	.hw_audio_ctrl = IVTV_HW_MSP34XX,
	.hw_all = IVTV_HW_MSP34XX | IVTV_HW_SAA7115 |
		  IVTV_HW_TVEEPROM | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_COMPOSITE4 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO0    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_COMPOSITE0 },
		{ IVTV_CARD_INPUT_SVIDEO2,    2, IVTV_SAA71XX_SVIDEO1    },
		{ IVTV_CARD_INPUT_COMPOSITE2, 2, IVTV_SAA71XX_COMPOSITE1 },
		{ IVTV_CARD_INPUT_COMPOSITE3, 1, IVTV_SAA71XX_COMPOSITE5 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  MSP_TUNER  },
		{ IVTV_CARD_INPUT_LINE_IN1,   MSP_SCART1 },
		{ IVTV_CARD_INPUT_LINE_IN2,   MSP_SCART3 },
	},
	.radio_input = { IVTV_CARD_INPUT_AUD_TUNER, MSP_SCART2 },
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Hauppauge PVR-350 cards */

/* Outputs for Hauppauge PVR350 cards */
static struct ivtv_card_output ivtv_pvr350_outputs[] = {
	{
		.name = "S-Video + Composite",
		.video_output = 0,
	}, {
		.name = "Composite",
		.video_output = 1,
	}, {
		.name = "S-Video",
		.video_output = 2,
	}, {
		.name = "RGB",
		.video_output = 3,
	}, {
		.name = "YUV C",
		.video_output = 4,
	}, {
		.name = "YUV V",
		.video_output = 5,
	}
};

static const struct ivtv_card ivtv_card_pvr350 = {
	.type = IVTV_CARD_PVR_350,
	.name = "Hauppauge WinTV PVR-350",
	.v4l2_capabilities = IVTV_CAP_ENCODER | IVTV_CAP_DECODER,
	.video_outputs = ivtv_pvr350_outputs,
	.nof_outputs = ARRAY_SIZE(ivtv_pvr350_outputs),
	.hw_video = IVTV_HW_SAA7115,
	.hw_audio = IVTV_HW_MSP34XX,
	.hw_audio_ctrl = IVTV_HW_MSP34XX,
	.hw_all = IVTV_HW_MSP34XX | IVTV_HW_SAA7115 |
		  IVTV_HW_SAA7127 | IVTV_HW_TVEEPROM | IVTV_HW_TUNER |
		  IVTV_HW_I2C_IR_RX_HAUP_EXT | IVTV_HW_I2C_IR_RX_HAUP_INT,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_COMPOSITE4 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO0    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_COMPOSITE0 },
		{ IVTV_CARD_INPUT_SVIDEO2,    2, IVTV_SAA71XX_SVIDEO1    },
		{ IVTV_CARD_INPUT_COMPOSITE2, 2, IVTV_SAA71XX_COMPOSITE1 },
		{ IVTV_CARD_INPUT_COMPOSITE3, 1, IVTV_SAA71XX_COMPOSITE5 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  MSP_TUNER  },
		{ IVTV_CARD_INPUT_LINE_IN1,   MSP_SCART1 },
		{ IVTV_CARD_INPUT_LINE_IN2,   MSP_SCART3 },
	},
	.radio_input = { IVTV_CARD_INPUT_AUD_TUNER, MSP_SCART2 },
	.i2c = &ivtv_i2c_std,
};

/* PVR-350 V1 boards have a different audio tuner input and use a
   saa7114 instead of a saa7115.
   Note that the info below comes from a pre-production model so it may
   not be correct. Especially the audio behaves strangely (mono only it seems) */
static const struct ivtv_card ivtv_card_pvr350_v1 = {
	.type = IVTV_CARD_PVR_350_V1,
	.name = "Hauppauge WinTV PVR-350 (V1)",
	.v4l2_capabilities = IVTV_CAP_ENCODER | IVTV_CAP_DECODER,
	.video_outputs = ivtv_pvr350_outputs,
	.nof_outputs = ARRAY_SIZE(ivtv_pvr350_outputs),
	.hw_video = IVTV_HW_SAA7114,
	.hw_audio = IVTV_HW_MSP34XX,
	.hw_audio_ctrl = IVTV_HW_MSP34XX,
	.hw_all = IVTV_HW_MSP34XX | IVTV_HW_SAA7114 |
		  IVTV_HW_SAA7127 | IVTV_HW_TVEEPROM | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_COMPOSITE4 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO0    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_COMPOSITE0 },
		{ IVTV_CARD_INPUT_SVIDEO2,    2, IVTV_SAA71XX_SVIDEO1    },
		{ IVTV_CARD_INPUT_COMPOSITE2, 2, IVTV_SAA71XX_COMPOSITE1 },
		{ IVTV_CARD_INPUT_COMPOSITE3, 1, IVTV_SAA71XX_COMPOSITE5 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  MSP_MONO   },
		{ IVTV_CARD_INPUT_LINE_IN1,   MSP_SCART1 },
		{ IVTV_CARD_INPUT_LINE_IN2,   MSP_SCART3 },
	},
	.radio_input = { IVTV_CARD_INPUT_AUD_TUNER, MSP_SCART2 },
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Hauppauge PVR-150/PVR-500 cards */

static const struct ivtv_card ivtv_card_pvr150 = {
	.type = IVTV_CARD_PVR_150,
	.name = "Hauppauge WinTV PVR-150",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_CX25840,
	.hw_audio = IVTV_HW_CX25840,
	.hw_audio_ctrl = IVTV_HW_CX25840,
	.hw_muxer = IVTV_HW_WM8775,
	.hw_all = IVTV_HW_WM8775 | IVTV_HW_CX25840 |
		  IVTV_HW_TVEEPROM | IVTV_HW_TUNER |
		  IVTV_HW_I2C_IR_RX_HAUP_EXT | IVTV_HW_I2C_IR_RX_HAUP_INT |
		  IVTV_HW_Z8F0811_IR_HAUP,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, CX25840_COMPOSITE7 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, CX25840_SVIDEO1    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, CX25840_COMPOSITE3 },
		{ IVTV_CARD_INPUT_SVIDEO2,    2, CX25840_SVIDEO2    },
		{ IVTV_CARD_INPUT_COMPOSITE2, 2, CX25840_COMPOSITE4 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,
		  CX25840_AUDIO8, WM8775_AIN2 },
		{ IVTV_CARD_INPUT_LINE_IN1,
		  CX25840_AUDIO_SERIAL, WM8775_AIN2 },
		{ IVTV_CARD_INPUT_LINE_IN2,
		  CX25840_AUDIO_SERIAL, WM8775_AIN3 },
	},
	.radio_input = { IVTV_CARD_INPUT_AUD_TUNER,
			 CX25840_AUDIO_SERIAL, WM8775_AIN4 },
	/* apparently needed for the IR blaster */
	.gpio_init = { .direction = 0x1f01, .initial_value = 0x26f3 },
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* AVerMedia M179 cards */

static const struct ivtv_card_pci_info ivtv_pci_m179[] = {
	{ PCI_DEVICE_ID_IVTV15, IVTV_PCI_ID_AVERMEDIA, 0xa3cf },
	{ PCI_DEVICE_ID_IVTV15, IVTV_PCI_ID_AVERMEDIA, 0xa3ce },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_m179 = {
	.type = IVTV_CARD_M179,
	.name = "AVerMedia M179",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA7114,
	.hw_audio = IVTV_HW_GPIO,
	.hw_audio_ctrl = IVTV_HW_GPIO,
	.hw_all = IVTV_HW_GPIO | IVTV_HW_SAA7114 | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_COMPOSITE4 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO0    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_COMPOSITE3 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  IVTV_GPIO_TUNER   },
		{ IVTV_CARD_INPUT_LINE_IN1,   IVTV_GPIO_LINE_IN },
	},
	.gpio_init = { .direction = 0xe380, .initial_value = 0x8290 },
	.gpio_audio_input  = { .mask = 0x8040, .tuner  = 0x8000, .linein = 0x0000 },
	.gpio_audio_mute   = { .mask = 0x2000, .mute   = 0x2000 },
	.gpio_audio_mode   = { .mask = 0x4300, .mono   = 0x4000, .stereo = 0x0200,
			      .lang1 = 0x0200, .lang2  = 0x0100, .both   = 0x0000 },
	.gpio_audio_freq   = { .mask = 0x0018, .f32000 = 0x0000,
			     .f44100 = 0x0008, .f48000 = 0x0010 },
	.gpio_audio_detect = { .mask = 0x4000, .stereo = 0x0000 },
	.tuners = {
		/* As far as we know all M179 cards use this tuner */
		{ .std = V4L2_STD_ALL, .tuner = TUNER_PHILIPS_NTSC },
	},
	.pci_list = ivtv_pci_m179,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Yuan MPG600/Kuroutoshikou ITVC16-STVLP cards */

static const struct ivtv_card_pci_info ivtv_pci_mpg600[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_YUAN1, 0xfff3 },
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_YUAN1, 0xffff },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_mpg600 = {
	.type = IVTV_CARD_MPG600,
	.name = "Yuan MPG600, Kuroutoshikou ITVC16-STVLP",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA7115,
	.hw_audio = IVTV_HW_GPIO,
	.hw_audio_ctrl = IVTV_HW_GPIO,
	.hw_all = IVTV_HW_GPIO | IVTV_HW_SAA7115 | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_COMPOSITE4 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO0    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_COMPOSITE3 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  IVTV_GPIO_TUNER   },
		{ IVTV_CARD_INPUT_LINE_IN1,   IVTV_GPIO_LINE_IN },
	},
	.gpio_init = { .direction = 0x3080, .initial_value = 0x0004 },
	.gpio_audio_input  = { .mask = 0x3000, .tuner  = 0x0000, .linein = 0x2000 },
	.gpio_audio_mute   = { .mask = 0x0001, .mute   = 0x0001 },
	.gpio_audio_mode   = { .mask = 0x000e, .mono   = 0x0006, .stereo = 0x0004,
			      .lang1 = 0x0004, .lang2  = 0x0000, .both   = 0x0008 },
	.gpio_audio_detect = { .mask = 0x0900, .stereo = 0x0100 },
	.tuners = {
		/* The PAL tuner is confirmed */
		{ .std = V4L2_STD_PAL_SECAM, .tuner = TUNER_PHILIPS_FQ1216ME },
		{ .std = V4L2_STD_ALL, .tuner = TUNER_PHILIPS_FQ1286 },
	},
	.pci_list = ivtv_pci_mpg600,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Yuan MPG160/Kuroutoshikou ITVC15-STVLP cards */

static const struct ivtv_card_pci_info ivtv_pci_mpg160[] = {
	{ PCI_DEVICE_ID_IVTV15, IVTV_PCI_ID_YUAN1, 0 },
	{ PCI_DEVICE_ID_IVTV15, IVTV_PCI_ID_IODATA, 0x40a0 },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_mpg160 = {
	.type = IVTV_CARD_MPG160,
	.name = "YUAN MPG160, Kuroutoshikou ITVC15-STVLP, I/O Data GV-M2TV/PCI",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA7114,
	.hw_audio = IVTV_HW_GPIO,
	.hw_audio_ctrl = IVTV_HW_GPIO,
	.hw_all = IVTV_HW_GPIO | IVTV_HW_SAA7114 | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_COMPOSITE4 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO0    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_COMPOSITE3 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  IVTV_GPIO_TUNER   },
		{ IVTV_CARD_INPUT_LINE_IN1,   IVTV_GPIO_LINE_IN },
	},
	.gpio_init = { .direction = 0x7080, .initial_value = 0x400c },
	.gpio_audio_input  = { .mask = 0x3000, .tuner  = 0x0000, .linein = 0x2000 },
	.gpio_audio_mute   = { .mask = 0x0001, .mute   = 0x0001 },
	.gpio_audio_mode   = { .mask = 0x000e, .mono   = 0x0006, .stereo = 0x0004,
			      .lang1 = 0x0004, .lang2  = 0x0000, .both   = 0x0008 },
	.gpio_audio_detect = { .mask = 0x0900, .stereo = 0x0100 },
	.tuners = {
		{ .std = V4L2_STD_PAL_SECAM, .tuner = TUNER_PHILIPS_FQ1216ME },
		{ .std = V4L2_STD_ALL, .tuner = TUNER_PHILIPS_FQ1286 },
	},
	.pci_list = ivtv_pci_mpg160,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Yuan PG600/Diamond PVR-550 cards */

static const struct ivtv_card_pci_info ivtv_pci_pg600[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_DIAMONDMM, 0x0070 },
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_YUAN3,     0x0600 },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_pg600 = {
	.type = IVTV_CARD_PG600,
	.name = "Yuan PG600, Diamond PVR-550",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_CX25840,
	.hw_audio = IVTV_HW_CX25840,
	.hw_audio_ctrl = IVTV_HW_CX25840,
	.hw_all = IVTV_HW_CX25840 | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, CX25840_COMPOSITE2 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1,
		  CX25840_SVIDEO_LUMA3 | CX25840_SVIDEO_CHROMA4 },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, CX25840_COMPOSITE1 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  CX25840_AUDIO5       },
		{ IVTV_CARD_INPUT_LINE_IN1,   CX25840_AUDIO_SERIAL },
	},
	.tuners = {
		{ .std = V4L2_STD_PAL_SECAM, .tuner = TUNER_PHILIPS_FQ1216ME },
		{ .std = V4L2_STD_ALL, .tuner = TUNER_PHILIPS_FQ1286 },
	},
	.pci_list = ivtv_pci_pg600,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Adaptec VideOh! AVC-2410 card */

static const struct ivtv_card_pci_info ivtv_pci_avc2410[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_ADAPTEC, 0x0093 },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_avc2410 = {
	.type = IVTV_CARD_AVC2410,
	.name = "Adaptec VideOh! AVC-2410",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA7115,
	.hw_audio = IVTV_HW_MSP34XX,
	.hw_audio_ctrl = IVTV_HW_MSP34XX,
	.hw_muxer = IVTV_HW_CS53L32A,
	.hw_all = IVTV_HW_MSP34XX | IVTV_HW_CS53L32A |
		  IVTV_HW_SAA7115 | IVTV_HW_TUNER |
		  IVTV_HW_I2C_IR_RX_ADAPTEC,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_COMPOSITE4 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO0    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_COMPOSITE3 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,
		  MSP_TUNER, CS53L32A_IN0 },
		{ IVTV_CARD_INPUT_LINE_IN1,
		  MSP_SCART1, CS53L32A_IN2 },
	},
	/* This card has no eeprom and in fact the Windows driver relies
	   on the country/region setting of the user to decide which tuner
	   is available. */
	.tuners = {
		{ .std = V4L2_STD_PAL_SECAM, .tuner = TUNER_PHILIPS_FM1216ME_MK3 },
		{ .std = V4L2_STD_ALL - V4L2_STD_NTSC_M_JP,
			.tuner = TUNER_PHILIPS_FM1236_MK3 },
		{ .std = V4L2_STD_NTSC_M_JP, .tuner = TUNER_PHILIPS_FQ1286 },
	},
	.pci_list = ivtv_pci_avc2410,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Adaptec VideOh! AVC-2010 card */

static const struct ivtv_card_pci_info ivtv_pci_avc2010[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_ADAPTEC, 0x0092 },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_avc2010 = {
	.type = IVTV_CARD_AVC2010,
	.name = "Adaptec VideOh! AVC-2010",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA7115,
	.hw_audio = IVTV_HW_CS53L32A,
	.hw_audio_ctrl = IVTV_HW_CS53L32A,
	.hw_all = IVTV_HW_CS53L32A | IVTV_HW_SAA7115,
	.video_inputs = {
		{ IVTV_CARD_INPUT_SVIDEO1,    0, IVTV_SAA71XX_SVIDEO0    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 0, IVTV_SAA71XX_COMPOSITE3 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_LINE_IN1,   CS53L32A_IN2 },
	},
	/* Does not have a tuner */
	.pci_list = ivtv_pci_avc2010,
};

/* ------------------------------------------------------------------------- */

/* Nagase Transgear 5000TV card */

static const struct ivtv_card_pci_info ivtv_pci_tg5000tv[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_AVERMEDIA, 0xbfff },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_tg5000tv = {
	.type = IVTV_CARD_TG5000TV,
	.name = "Nagase Transgear 5000TV",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA7114 | IVTV_HW_UPD64031A | IVTV_HW_UPD6408X |
	IVTV_HW_GPIO,
	.hw_audio = IVTV_HW_GPIO,
	.hw_audio_ctrl = IVTV_HW_GPIO,
	.hw_all = IVTV_HW_GPIO | IVTV_HW_SAA7114 | IVTV_HW_TUNER |
		  IVTV_HW_UPD64031A | IVTV_HW_UPD6408X,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_SVIDEO0 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO2 },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_SVIDEO2 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  IVTV_GPIO_TUNER   },
		{ IVTV_CARD_INPUT_LINE_IN1,   IVTV_GPIO_LINE_IN },
	},
	.gr_config = UPD64031A_VERTICAL_EXTERNAL,
	.gpio_init = { .direction = 0xe080, .initial_value = 0x8000 },
	.gpio_audio_input  = { .mask = 0x8080, .tuner  = 0x8000, .linein = 0x0080 },
	.gpio_audio_mute   = { .mask = 0x6000, .mute   = 0x6000 },
	.gpio_audio_mode   = { .mask = 0x4300, .mono   = 0x4000, .stereo = 0x0200,
			      .lang1 = 0x0300, .lang2  = 0x0000, .both   = 0x0200 },
	.gpio_video_input  = { .mask = 0x0030, .tuner  = 0x0000,
			  .composite = 0x0010, .svideo = 0x0020 },
	.tuners = {
		{ .std = V4L2_STD_MN, .tuner = TUNER_PHILIPS_FQ1286 },
	},
	.pci_list = ivtv_pci_tg5000tv,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* AOpen VA2000MAX-SNT6 card */

static const struct ivtv_card_pci_info ivtv_pci_va2000[] = {
	{ PCI_DEVICE_ID_IVTV16, 0, 0xff5f },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_va2000 = {
	.type = IVTV_CARD_VA2000MAX_SNT6,
	.name = "AOpen VA2000MAX-SNT6",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA7115 | IVTV_HW_UPD6408X,
	.hw_audio = IVTV_HW_MSP34XX,
	.hw_audio_ctrl = IVTV_HW_MSP34XX,
	.hw_all = IVTV_HW_MSP34XX | IVTV_HW_SAA7115 |
		  IVTV_HW_UPD6408X | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER, 0, IVTV_SAA71XX_SVIDEO0 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER, MSP_TUNER },
	},
	.tuners = {
		{ .std = V4L2_STD_MN, .tuner = TUNER_PHILIPS_FQ1286 },
	},
	.pci_list = ivtv_pci_va2000,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Yuan MPG600GR/Kuroutoshikou CX23416GYC-STVLP cards */

static const struct ivtv_card_pci_info ivtv_pci_cx23416gyc[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_YUAN1, 0x0600 },
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_YUAN4, 0x0600 },
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_MELCO, 0x0523 },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_cx23416gyc = {
	.type = IVTV_CARD_CX23416GYC,
	.name = "Yuan MPG600GR, Kuroutoshikou CX23416GYC-STVLP",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA717X | IVTV_HW_GPIO |
		IVTV_HW_UPD64031A | IVTV_HW_UPD6408X,
	.hw_audio = IVTV_HW_SAA717X,
	.hw_audio_ctrl = IVTV_HW_SAA717X,
	.hw_all = IVTV_HW_GPIO | IVTV_HW_SAA717X | IVTV_HW_TUNER |
		  IVTV_HW_UPD64031A | IVTV_HW_UPD6408X,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_SVIDEO3 |
						 IVTV_SAA717X_TUNER_FLAG },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO0 },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_SVIDEO3 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  IVTV_SAA717X_IN2 },
		{ IVTV_CARD_INPUT_LINE_IN1,   IVTV_SAA717X_IN0 },
	},
	.gr_config = UPD64031A_VERTICAL_EXTERNAL,
	.gpio_init = { .direction = 0xf880, .initial_value = 0x8800 },
	.gpio_video_input  = { .mask = 0x0020, .tuner  = 0x0000,
			       .composite = 0x0020, .svideo = 0x0020 },
	.gpio_audio_freq   = { .mask = 0xc000, .f32000 = 0x0000,
			     .f44100 = 0x4000, .f48000 = 0x8000 },
	.tuners = {
		{ .std = V4L2_STD_PAL_SECAM, .tuner = TUNER_PHILIPS_FM1216ME_MK3 },
		{ .std = V4L2_STD_ALL, .tuner = TUNER_PHILIPS_FM1236_MK3 },
	},
	.pci_list = ivtv_pci_cx23416gyc,
	.i2c = &ivtv_i2c_std,
};

static const struct ivtv_card ivtv_card_cx23416gyc_nogr = {
	.type = IVTV_CARD_CX23416GYC_NOGR,
	.name = "Yuan MPG600GR, Kuroutoshikou CX23416GYC-STVLP (no GR)",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA717X | IVTV_HW_GPIO | IVTV_HW_UPD6408X,
	.hw_audio = IVTV_HW_SAA717X,
	.hw_audio_ctrl = IVTV_HW_SAA717X,
	.hw_all = IVTV_HW_GPIO | IVTV_HW_SAA717X | IVTV_HW_TUNER |
		  IVTV_HW_UPD6408X,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_COMPOSITE4 |
						 IVTV_SAA717X_TUNER_FLAG },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO0    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_COMPOSITE0 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  IVTV_SAA717X_IN2 },
		{ IVTV_CARD_INPUT_LINE_IN1,   IVTV_SAA717X_IN0 },
	},
	.gpio_init = { .direction = 0xf880, .initial_value = 0x8800 },
	.gpio_video_input  = { .mask = 0x0020, .tuner  = 0x0000,
			       .composite = 0x0020, .svideo = 0x0020 },
	.gpio_audio_freq   = { .mask = 0xc000, .f32000 = 0x0000,
			     .f44100 = 0x4000, .f48000 = 0x8000 },
	.tuners = {
		{ .std = V4L2_STD_PAL_SECAM, .tuner = TUNER_PHILIPS_FM1216ME_MK3 },
		{ .std = V4L2_STD_ALL, .tuner = TUNER_PHILIPS_FM1236_MK3 },
	},
	.i2c = &ivtv_i2c_std,
};

static const struct ivtv_card ivtv_card_cx23416gyc_nogrycs = {
	.type = IVTV_CARD_CX23416GYC_NOGRYCS,
	.name = "Yuan MPG600GR, Kuroutoshikou CX23416GYC-STVLP (no GR/YCS)",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA717X | IVTV_HW_GPIO,
	.hw_audio = IVTV_HW_SAA717X,
	.hw_audio_ctrl = IVTV_HW_SAA717X,
	.hw_all = IVTV_HW_GPIO | IVTV_HW_SAA717X | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_COMPOSITE4 |
						 IVTV_SAA717X_TUNER_FLAG },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO0    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_COMPOSITE0 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  IVTV_SAA717X_IN2 },
		{ IVTV_CARD_INPUT_LINE_IN1,   IVTV_SAA717X_IN0 },
	},
	.gpio_init = { .direction = 0xf880, .initial_value = 0x8800 },
	.gpio_video_input  = { .mask = 0x0020, .tuner  = 0x0000,
			       .composite = 0x0020, .svideo = 0x0020 },
	.gpio_audio_freq   = { .mask = 0xc000, .f32000 = 0x0000,
			     .f44100 = 0x4000, .f48000 = 0x8000 },
	.tuners = {
		{ .std = V4L2_STD_PAL_SECAM, .tuner = TUNER_PHILIPS_FM1216ME_MK3 },
		{ .std = V4L2_STD_ALL, .tuner = TUNER_PHILIPS_FM1236_MK3 },
	},
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* I/O Data GV-MVP/RX & GV-MVP/RX2W (dual tuner) cards */

static const struct ivtv_card_pci_info ivtv_pci_gv_mvprx[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_IODATA, 0xd01e },
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_IODATA, 0xd038 }, /* 2W unit #1 */
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_IODATA, 0xd039 }, /* 2W unit #2 */
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_gv_mvprx = {
	.type = IVTV_CARD_GV_MVPRX,
	.name = "I/O Data GV-MVP/RX, GV-MVP/RX2W (dual tuner)",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA7115 | IVTV_HW_UPD64031A | IVTV_HW_UPD6408X,
	.hw_audio = IVTV_HW_GPIO,
	.hw_audio_ctrl = IVTV_HW_WM8739,
	.hw_all = IVTV_HW_GPIO | IVTV_HW_SAA7115 | IVTV_HW_VP27SMPX |
		  IVTV_HW_TUNER | IVTV_HW_WM8739 |
		  IVTV_HW_UPD64031A | IVTV_HW_UPD6408X,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_SVIDEO0    },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO1    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_SVIDEO2    },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  IVTV_GPIO_TUNER   },
		{ IVTV_CARD_INPUT_LINE_IN1,   IVTV_GPIO_LINE_IN },
	},
	.gpio_init = { .direction = 0xc301, .initial_value = 0x0200 },
	.gpio_audio_input  = { .mask = 0xffff, .tuner  = 0x0200, .linein = 0x0300 },
	.tuners = {
		/* This card has the Panasonic VP27 tuner */
		{ .std = V4L2_STD_MN, .tuner = TUNER_PANASONIC_VP27 },
	},
	.pci_list = ivtv_pci_gv_mvprx,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* I/O Data GV-MVP/RX2E card */

static const struct ivtv_card_pci_info ivtv_pci_gv_mvprx2e[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_IODATA, 0xd025 },
	{0, 0, 0}
};

static const struct ivtv_card ivtv_card_gv_mvprx2e = {
	.type = IVTV_CARD_GV_MVPRX2E,
	.name = "I/O Data GV-MVP/RX2E",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA7115,
	.hw_audio = IVTV_HW_GPIO,
	.hw_audio_ctrl = IVTV_HW_WM8739,
	.hw_all = IVTV_HW_GPIO | IVTV_HW_SAA7115 | IVTV_HW_TUNER |
		  IVTV_HW_VP27SMPX | IVTV_HW_WM8739,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_COMPOSITE4 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO0    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_COMPOSITE3 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  IVTV_GPIO_TUNER   },
		{ IVTV_CARD_INPUT_LINE_IN1,   IVTV_GPIO_LINE_IN },
	},
	.gpio_init = { .direction = 0xc301, .initial_value = 0x0200 },
	.gpio_audio_input  = { .mask = 0xffff, .tuner  = 0x0200, .linein = 0x0300 },
	.tuners = {
		/* This card has the Panasonic VP27 tuner */
		{ .std = V4L2_STD_MN, .tuner = TUNER_PANASONIC_VP27 },
	},
	.pci_list = ivtv_pci_gv_mvprx2e,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* GotVIEW PCI DVD card */

static const struct ivtv_card_pci_info ivtv_pci_gotview_pci_dvd[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_YUAN1, 0x0600 },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_gotview_pci_dvd = {
	.type = IVTV_CARD_GOTVIEW_PCI_DVD,
	.name = "GotView PCI DVD",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA717X,
	.hw_audio = IVTV_HW_SAA717X,
	.hw_audio_ctrl = IVTV_HW_SAA717X,
	.hw_all = IVTV_HW_SAA717X | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_COMPOSITE1 },  /* pin 116 */
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO0 },     /* pin 114/109 */
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_COMPOSITE3 },  /* pin 118 */
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  IVTV_SAA717X_IN0 },
		{ IVTV_CARD_INPUT_LINE_IN1,   IVTV_SAA717X_IN2 },
	},
	.gpio_init = { .direction = 0xf000, .initial_value = 0xA000 },
	.tuners = {
		/* This card has a Philips FQ1216ME MK3 tuner */
		{ .std = V4L2_STD_PAL_SECAM, .tuner = TUNER_PHILIPS_FM1216ME_MK3 },
	},
	.pci_list = ivtv_pci_gotview_pci_dvd,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* GotVIEW PCI DVD2 Deluxe card */

static const struct ivtv_card_pci_info ivtv_pci_gotview_pci_dvd2[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_GOTVIEW1, 0x0600 },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_gotview_pci_dvd2 = {
	.type = IVTV_CARD_GOTVIEW_PCI_DVD2,
	.name = "GotView PCI DVD2 Deluxe",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_CX25840,
	.hw_audio = IVTV_HW_CX25840,
	.hw_audio_ctrl = IVTV_HW_CX25840,
	.hw_muxer = IVTV_HW_GPIO,
	.hw_all = IVTV_HW_CX25840 | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, CX25840_COMPOSITE2 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1,
		  CX25840_SVIDEO_LUMA3 | CX25840_SVIDEO_CHROMA4 },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, CX25840_COMPOSITE1 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  CX25840_AUDIO5,       0 },
		{ IVTV_CARD_INPUT_LINE_IN1,   CX25840_AUDIO_SERIAL, 1 },
	},
	.radio_input = { IVTV_CARD_INPUT_AUD_TUNER, CX25840_AUDIO_SERIAL, 2 },
	.gpio_init = { .direction = 0x0800, .initial_value = 0 },
	.gpio_audio_input  = { .mask = 0x0800, .tuner = 0, .linein = 0, .radio = 0x0800 },
	.tuners = {
		/* This card has a Philips FQ1216ME MK5 tuner */
		{ .std = V4L2_STD_PAL_SECAM, .tuner = TUNER_PHILIPS_FM1216ME_MK3 },
	},
	.pci_list = ivtv_pci_gotview_pci_dvd2,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Yuan MPC622 miniPCI card */

static const struct ivtv_card_pci_info ivtv_pci_yuan_mpc622[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_YUAN2, 0xd998 },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_yuan_mpc622 = {
	.type = IVTV_CARD_YUAN_MPC622,
	.name = "Yuan MPC622",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_CX25840,
	.hw_audio = IVTV_HW_CX25840,
	.hw_audio_ctrl = IVTV_HW_CX25840,
	.hw_all = IVTV_HW_CX25840 | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, CX25840_COMPOSITE2 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1,
		  CX25840_SVIDEO_LUMA3 | CX25840_SVIDEO_CHROMA4 },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, CX25840_COMPOSITE1 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  CX25840_AUDIO5       },
		{ IVTV_CARD_INPUT_LINE_IN1,   CX25840_AUDIO_SERIAL },
	},
	.gpio_init = { .direction = 0x00ff, .initial_value = 0x0002 },
	.tuners = {
		/* This card has the TDA8290/TDA8275 tuner chips */
		{ .std = V4L2_STD_ALL, .tuner = TUNER_PHILIPS_TDA8290 },
	},
	.pci_list = ivtv_pci_yuan_mpc622,
	.i2c = &ivtv_i2c_tda8290,
};

/* ------------------------------------------------------------------------- */

/* DIGITAL COWBOY DCT-MTVP1 card */

static const struct ivtv_card_pci_info ivtv_pci_dctmvtvp1[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_AVERMEDIA, 0xbfff },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_dctmvtvp1 = {
	.type = IVTV_CARD_DCTMTVP1,
	.name = "Digital Cowboy DCT-MTVP1",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA7115 | IVTV_HW_UPD64031A | IVTV_HW_UPD6408X |
		IVTV_HW_GPIO,
	.hw_audio = IVTV_HW_GPIO,
	.hw_audio_ctrl = IVTV_HW_GPIO,
	.hw_all = IVTV_HW_GPIO | IVTV_HW_SAA7115 | IVTV_HW_TUNER |
		IVTV_HW_UPD64031A | IVTV_HW_UPD6408X,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_SVIDEO0    },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO2    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_SVIDEO2 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  IVTV_GPIO_TUNER   },
		{ IVTV_CARD_INPUT_LINE_IN1,   IVTV_GPIO_LINE_IN },
	},
	.gpio_init = { .direction = 0xe080, .initial_value = 0x8000 },
	.gpio_audio_input  = { .mask = 0x8080, .tuner  = 0x8000, .linein = 0x0080 },
	.gpio_audio_mute   = { .mask = 0x6000, .mute   = 0x6000 },
	.gpio_audio_mode   = { .mask = 0x4300, .mono   = 0x4000, .stereo = 0x0200,
			      .lang1 = 0x0300, .lang2  = 0x0000, .both   = 0x0200 },
	.gpio_video_input  = { .mask = 0x0030, .tuner  = 0x0000,
			       .composite = 0x0010, .svideo = 0x0020},
	.tuners = {
		{ .std = V4L2_STD_MN, .tuner = TUNER_PHILIPS_FQ1286 },
	},
	.pci_list = ivtv_pci_dctmvtvp1,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Yuan PG600-2/GotView PCI DVD Lite cards */

static const struct ivtv_card_pci_info ivtv_pci_pg600v2[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_YUAN3,     0x0600 },
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_GOTVIEW2,  0x0600 },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_pg600v2 = {
	.type = IVTV_CARD_PG600V2,
	.name = "Yuan PG600-2, GotView PCI DVD Lite",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_CX25840,
	.hw_audio = IVTV_HW_CX25840,
	.hw_audio_ctrl = IVTV_HW_CX25840,
	.hw_all = IVTV_HW_CX25840 | IVTV_HW_TUNER,
	/* XC2028 support apparently works for the Yuan, it's still
	   uncertain whether it also works with the GotView. */
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, CX25840_COMPOSITE2 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1,
		  CX25840_SVIDEO_LUMA3 | CX25840_SVIDEO_CHROMA4 },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, CX25840_COMPOSITE1 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  CX25840_AUDIO5       },
		{ IVTV_CARD_INPUT_LINE_IN1,   CX25840_AUDIO_SERIAL },
	},
	.radio_input = { IVTV_CARD_INPUT_AUD_TUNER, CX25840_AUDIO5 },
	.xceive_pin = 12,
	.tuners = {
		{ .std = V4L2_STD_ALL, .tuner = TUNER_XC2028 },
	},
	.pci_list = ivtv_pci_pg600v2,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Club3D ZAP-TV1x01 cards */

static const struct ivtv_card_pci_info ivtv_pci_club3d[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_YUAN3,     0x0600 },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_club3d = {
	.type = IVTV_CARD_CLUB3D,
	.name = "Club3D ZAP-TV1x01",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_CX25840,
	.hw_audio = IVTV_HW_CX25840,
	.hw_audio_ctrl = IVTV_HW_CX25840,
	.hw_all = IVTV_HW_CX25840 | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, CX25840_COMPOSITE2 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1,
		  CX25840_SVIDEO_LUMA3 | CX25840_SVIDEO_CHROMA4 },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, CX25840_COMPOSITE3 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  CX25840_AUDIO5       },
		{ IVTV_CARD_INPUT_LINE_IN1,   CX25840_AUDIO_SERIAL },
	},
	.radio_input = { IVTV_CARD_INPUT_AUD_TUNER, CX25840_AUDIO5 },
	.xceive_pin = 12,
	.tuners = {
		{ .std = V4L2_STD_ALL, .tuner = TUNER_XC2028 },
	},
	.pci_list = ivtv_pci_club3d,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* AVerTV MCE 116 Plus (M116) card */

static const struct ivtv_card_pci_info ivtv_pci_avertv_mce116[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_AVERMEDIA, 0xc439 },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_avertv_mce116 = {
	.type = IVTV_CARD_AVERTV_MCE116,
	.name = "AVerTV MCE 116 Plus",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_CX25840,
	.hw_audio = IVTV_HW_CX25840,
	.hw_audio_ctrl = IVTV_HW_CX25840,
	.hw_all = IVTV_HW_CX25840 | IVTV_HW_TUNER | IVTV_HW_WM8739 |
		  IVTV_HW_I2C_IR_RX_AVER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, CX25840_COMPOSITE2 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, CX25840_SVIDEO3    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, CX25840_COMPOSITE1 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  CX25840_AUDIO5       },
		{ IVTV_CARD_INPUT_LINE_IN1,   CX25840_AUDIO_SERIAL, 1 },
	},
	.radio_input = { IVTV_CARD_INPUT_AUD_TUNER,  CX25840_AUDIO5 },
	/* enable line-in */
	.gpio_init = { .direction = 0xe000, .initial_value = 0x4000 },
	.xceive_pin = 10,
	.tuners = {
		{ .std = V4L2_STD_ALL, .tuner = TUNER_XC2028 },
	},
	.pci_list = ivtv_pci_avertv_mce116,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* AVerMedia PVR-150 Plus / AVerTV M113 cards with a Daewoo/Partsnic Tuner */

static const struct ivtv_card_pci_info ivtv_pci_aver_pvr150[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_AVERMEDIA, 0xc034 }, /* NTSC */
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_AVERMEDIA, 0xc035 }, /* NTSC FM */
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_aver_pvr150 = {
	.type = IVTV_CARD_AVER_PVR150PLUS,
	.name = "AVerMedia PVR-150 Plus / AVerTV M113 Partsnic (Daewoo) Tuner",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_CX25840,
	.hw_audio = IVTV_HW_CX25840,
	.hw_audio_ctrl = IVTV_HW_CX25840,
	.hw_muxer = IVTV_HW_GPIO,
	.hw_all = IVTV_HW_CX25840 | IVTV_HW_TUNER |
		  IVTV_HW_WM8739 | IVTV_HW_GPIO,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, CX25840_COMPOSITE2 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, CX25840_SVIDEO3    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, CX25840_COMPOSITE1 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  CX25840_AUDIO5,       0 },
		{ IVTV_CARD_INPUT_LINE_IN1,   CX25840_AUDIO_SERIAL, 1 },
	},
	.radio_input = { IVTV_CARD_INPUT_AUD_TUNER, CX25840_AUDIO_SERIAL, 2 },
	/* The 74HC4052 Dual 4:1 multiplexer is controlled by 2 GPIO lines */
	.gpio_init = { .direction = 0xc000, .initial_value = 0 },
	.gpio_audio_input  = { .mask   = 0xc000,
			       .tuner  = 0x0000,
			       .linein = 0x4000,
			       .radio  = 0x8000 },
	.tuners = {
		/* Subsystem ID's 0xc03[45] have a Partsnic PTI-5NF05 tuner */
		{ .std = V4L2_STD_MN, .tuner = TUNER_PARTSNIC_PTI_5NF05 },
	},
	.pci_list = ivtv_pci_aver_pvr150,
	/* Subsystem ID 0xc035 has a TEA5767(?) FM tuner, 0xc034 does not */
	.i2c = &ivtv_i2c_radio,
};

/* ------------------------------------------------------------------------- */

/* AVerMedia UltraTV 1500 MCE (newer non-cx88 version, M113 variant) card */

static const struct ivtv_card_pci_info ivtv_pci_aver_ultra1500mce[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_AVERMEDIA, 0xc019 }, /* NTSC */
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_AVERMEDIA, 0xc01b }, /* PAL/SECAM */
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_aver_ultra1500mce = {
	.type = IVTV_CARD_AVER_ULTRA1500MCE,
	.name = "AVerMedia UltraTV 1500 MCE / AVerTV M113 Philips Tuner",
	.comment = "For non-NTSC tuners, use the pal= or secam= module options",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_CX25840,
	.hw_audio = IVTV_HW_CX25840,
	.hw_audio_ctrl = IVTV_HW_CX25840,
	.hw_muxer = IVTV_HW_GPIO,
	.hw_all = IVTV_HW_CX25840 | IVTV_HW_TUNER |
		  IVTV_HW_WM8739 | IVTV_HW_GPIO,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, CX25840_COMPOSITE2 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, CX25840_SVIDEO3    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, CX25840_COMPOSITE1 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  CX25840_AUDIO5,       0 },
		{ IVTV_CARD_INPUT_LINE_IN1,   CX25840_AUDIO_SERIAL, 1 },
	},
	.radio_input = { IVTV_CARD_INPUT_AUD_TUNER, CX25840_AUDIO_SERIAL, 2 },
	/* The 74HC4052 Dual 4:1 multiplexer is controlled by 2 GPIO lines */
	.gpio_init = { .direction = 0xc000, .initial_value = 0 },
	.gpio_audio_input  = { .mask   = 0xc000,
			       .tuner  = 0x0000,
			       .linein = 0x4000,
			       .radio  = 0x8000 },
	.tuners = {
		/* The UltraTV 1500 MCE has a Philips FM1236 MK5 TV/FM tuner */
		{ .std = V4L2_STD_MN, .tuner = TUNER_PHILIPS_FM1236_MK3 },
		{ .std = V4L2_STD_PAL_SECAM, .tuner = TUNER_PHILIPS_FM1216MK5 },
	},
	.pci_list = ivtv_pci_aver_ultra1500mce,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* AVerMedia EZMaker PCI Deluxe card */

static const struct ivtv_card_pci_info ivtv_pci_aver_ezmaker[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_AVERMEDIA, 0xc03f },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_aver_ezmaker = {
	.type = IVTV_CARD_AVER_EZMAKER,
	.name = "AVerMedia EZMaker PCI Deluxe",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_CX25840,
	.hw_audio = IVTV_HW_CX25840,
	.hw_audio_ctrl = IVTV_HW_CX25840,
	.hw_all = IVTV_HW_CX25840 | IVTV_HW_WM8739,
	.video_inputs = {
		{ IVTV_CARD_INPUT_SVIDEO1,    0, CX25840_SVIDEO3 },
		{ IVTV_CARD_INPUT_COMPOSITE1, 0, CX25840_COMPOSITE1 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_LINE_IN1,   CX25840_AUDIO_SERIAL, 0 },
	},
	.gpio_init = { .direction = 0x4000, .initial_value = 0x4000 },
	/* Does not have a tuner */
	.pci_list = ivtv_pci_aver_ezmaker,
};

/* ------------------------------------------------------------------------- */

/* ASUS Falcon2 */

static const struct ivtv_card_pci_info ivtv_pci_asus_falcon2[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_ASUSTEK, 0x4b66 },
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_ASUSTEK, 0x462e },
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_ASUSTEK, 0x4b2e },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_asus_falcon2 = {
	.type = IVTV_CARD_ASUS_FALCON2,
	.name = "ASUS Falcon2",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_CX25840,
	.hw_audio = IVTV_HW_CX25840,
	.hw_audio_ctrl = IVTV_HW_CX25840,
	.hw_muxer = IVTV_HW_M52790,
	.hw_all = IVTV_HW_CX25840 | IVTV_HW_M52790 | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, CX25840_COMPOSITE2 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1, CX25840_SVIDEO3    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 2, CX25840_COMPOSITE2 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER, CX25840_AUDIO5, M52790_IN_TUNER },
		{ IVTV_CARD_INPUT_LINE_IN1, CX25840_AUDIO_SERIAL,
			M52790_IN_V2 | M52790_SW1_YCMIX | M52790_SW2_YCMIX },
		{ IVTV_CARD_INPUT_LINE_IN1, CX25840_AUDIO_SERIAL, M52790_IN_V2 },
	},
	.radio_input = { IVTV_CARD_INPUT_AUD_TUNER, CX25840_AUDIO_SERIAL, M52790_IN_TUNER },
	.tuners = {
		{ .std = V4L2_STD_MN, .tuner = TUNER_PHILIPS_FM1236_MK3 },
	},
	.pci_list = ivtv_pci_asus_falcon2,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* AVerMedia M104 miniPCI card */

static const struct ivtv_card_pci_info ivtv_pci_aver_m104[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_AVERMEDIA, 0xc136 },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_aver_m104 = {
	.type = IVTV_CARD_AVER_M104,
	.name = "AVerMedia M104",
	.comment = "Not yet supported!\n",
	.v4l2_capabilities = 0, /*IVTV_CAP_ENCODER,*/
	.hw_video = IVTV_HW_CX25840,
	.hw_audio = IVTV_HW_CX25840,
	.hw_audio_ctrl = IVTV_HW_CX25840,
	.hw_all = IVTV_HW_CX25840 | IVTV_HW_TUNER | IVTV_HW_WM8739,
	.video_inputs = {
		{ IVTV_CARD_INPUT_SVIDEO1,    0, CX25840_SVIDEO3    },
		{ IVTV_CARD_INPUT_COMPOSITE1, 0, CX25840_COMPOSITE1 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_LINE_IN1,   CX25840_AUDIO_SERIAL, 1 },
	},
	.radio_input = { IVTV_CARD_INPUT_AUD_TUNER, CX25840_AUDIO_SERIAL, 2 },
	/* enable line-in + reset tuner */
	.gpio_init = { .direction = 0xe000, .initial_value = 0x4000 },
	.xceive_pin = 10,
	.tuners = {
		{ .std = V4L2_STD_ALL, .tuner = TUNER_XC2028 },
	},
	.pci_list = ivtv_pci_aver_m104,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Buffalo PC-MV5L/PCI cards */

static const struct ivtv_card_pci_info ivtv_pci_buffalo[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_MELCO, 0x052b },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_buffalo = {
	.type = IVTV_CARD_BUFFALO_MV5L,
	.name = "Buffalo PC-MV5L/PCI",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_CX25840,
	.hw_audio = IVTV_HW_CX25840,
	.hw_audio_ctrl = IVTV_HW_CX25840,
	.hw_all = IVTV_HW_CX25840 | IVTV_HW_TUNER,
	.video_inputs = {
		{ IVTV_CARD_INPUT_VID_TUNER,  0, CX25840_COMPOSITE2 },
		{ IVTV_CARD_INPUT_SVIDEO1,    1,
			CX25840_SVIDEO_LUMA3 | CX25840_SVIDEO_CHROMA4 },
		{ IVTV_CARD_INPUT_COMPOSITE1, 1, CX25840_COMPOSITE1 },
	},
	.audio_inputs = {
		{ IVTV_CARD_INPUT_AUD_TUNER,  CX25840_AUDIO5       },
		{ IVTV_CARD_INPUT_LINE_IN1,   CX25840_AUDIO_SERIAL },
	},
	.xceive_pin = 12,
	.tuners = {
		{ .std = V4L2_STD_ALL, .tuner = TUNER_XC2028 },
	},
	.pci_list = ivtv_pci_buffalo,
	.i2c = &ivtv_i2c_std,
};

/* ------------------------------------------------------------------------- */
/* Sony Kikyou */

static const struct ivtv_card_pci_info ivtv_pci_kikyou[] = {
	{ PCI_DEVICE_ID_IVTV16, IVTV_PCI_ID_SONY, 0x813d },
	{ 0, 0, 0 }
};

static const struct ivtv_card ivtv_card_kikyou = {
	.type = IVTV_CARD_KIKYOU,
	.name = "Sony VAIO Giga Pocket (ENX Kikyou)",
	.v4l2_capabilities = IVTV_CAP_ENCODER,
	.hw_video = IVTV_HW_SAA7115,
	.hw_audio = IVTV_HW_GPIO,
	.hw_audio_ctrl = IVTV_HW_GPIO,
	.hw_all = IVTV_HW_GPIO | IVTV_HW_SAA7115 | IVTV_HW_TUNER,
	.video_inputs = {
	{ IVTV_CARD_INPUT_VID_TUNER,  0, IVTV_SAA71XX_COMPOSITE1 },
	{ IVTV_CARD_INPUT_COMPOSITE1, 1, IVTV_SAA71XX_COMPOSITE1 },
	{ IVTV_CARD_INPUT_SVIDEO1,    1, IVTV_SAA71XX_SVIDEO1 },
	},
	.audio_inputs = {
	     { IVTV_CARD_INPUT_AUD_TUNER,  IVTV_GPIO_TUNER },
	     { IVTV_CARD_INPUT_LINE_IN1,   IVTV_GPIO_LINE_IN },
	     { IVTV_CARD_INPUT_LINE_IN2,   IVTV_GPIO_LINE_IN },
	},
	.gpio_init = { .direction = 0x03e1, .initial_value = 0x0320 },
	.gpio_audio_input = { .mask   = 0x0060,
			      .tuner  = 0x0020,
			      .linein = 0x0000,
			      .radio  = 0x0060 },
	.gpio_audio_mute  = { .mask = 0x0000,
			      .mute = 0x0000 }, /* 0x200? Disable for now. */
	.gpio_audio_mode  = { .mask   = 0x0080,
			      .mono   = 0x0000,
			      .stereo = 0x0000, /* SAP */
			      .lang1  = 0x0080,
			      .lang2  = 0x0000,
			      .both   = 0x0080 },
	.tuners = {
	     { .std = V4L2_STD_ALL, .tuner = TUNER_SONY_BTF_PXN01Z },
	},
	.pci_list = ivtv_pci_kikyou,
	.i2c = &ivtv_i2c_std,
};


static const struct ivtv_card *ivtv_card_list[] = {
	&ivtv_card_pvr250,
	&ivtv_card_pvr350,
	&ivtv_card_pvr150,
	&ivtv_card_m179,
	&ivtv_card_mpg600,
	&ivtv_card_mpg160,
	&ivtv_card_pg600,
	&ivtv_card_avc2410,
	&ivtv_card_avc2010,
	&ivtv_card_tg5000tv,
	&ivtv_card_va2000,
	&ivtv_card_cx23416gyc,
	&ivtv_card_gv_mvprx,
	&ivtv_card_gv_mvprx2e,
	&ivtv_card_gotview_pci_dvd,
	&ivtv_card_gotview_pci_dvd2,
	&ivtv_card_yuan_mpc622,
	&ivtv_card_dctmvtvp1,
	&ivtv_card_pg600v2,
	&ivtv_card_club3d,
	&ivtv_card_avertv_mce116,
	&ivtv_card_asus_falcon2,
	&ivtv_card_aver_pvr150,
	&ivtv_card_aver_ezmaker,
	&ivtv_card_aver_m104,
	&ivtv_card_buffalo,
	&ivtv_card_aver_ultra1500mce,
	&ivtv_card_kikyou,

	/* Variations of standard cards but with the same PCI IDs.
	   These cards must come last in this list. */
	&ivtv_card_pvr350_v1,
	&ivtv_card_cx23416gyc_nogr,
	&ivtv_card_cx23416gyc_nogrycs,
};

const struct ivtv_card *ivtv_get_card(u16 index)
{
	if (index >= ARRAY_SIZE(ivtv_card_list))
		return NULL;
	return ivtv_card_list[index];
}

int ivtv_get_input(struct ivtv *itv, u16 index, struct v4l2_input *input)
{
	const struct ivtv_card_video_input *card_input = itv->card->video_inputs + index;
	static const char * const input_strs[] = {
		"Tuner 1",
		"S-Video 1",
		"S-Video 2",
		"Composite 1",
		"Composite 2",
		"Composite 3"
	};

	if (index >= itv->nof_inputs)
		return -EINVAL;
	input->index = index;
	strlcpy(input->name, input_strs[card_input->video_type - 1],
			sizeof(input->name));
	input->type = (card_input->video_type == IVTV_CARD_INPUT_VID_TUNER ?
			V4L2_INPUT_TYPE_TUNER : V4L2_INPUT_TYPE_CAMERA);
	input->audioset = (1 << itv->nof_audio_inputs) - 1;
	input->std = (input->type == V4L2_INPUT_TYPE_TUNER) ?
				itv->tuner_std : V4L2_STD_ALL;
	return 0;
}

int ivtv_get_output(struct ivtv *itv, u16 index, struct v4l2_output *output)
{
	const struct ivtv_card_output *card_output = itv->card->video_outputs + index;

	if (index >= itv->card->nof_outputs)
		return -EINVAL;
	output->index = index;
	strlcpy(output->name, card_output->name, sizeof(output->name));
	output->type = V4L2_OUTPUT_TYPE_ANALOG;
	output->audioset = 1;
	output->std = V4L2_STD_ALL;
	return 0;
}

int ivtv_get_audio_input(struct ivtv *itv, u16 index, struct v4l2_audio *audio)
{
	const struct ivtv_card_audio_input *aud_input = itv->card->audio_inputs + index;
	static const char * const input_strs[] = {
		"Tuner 1",
		"Line In 1",
		"Line In 2"
	};

	memset(audio, 0, sizeof(*audio));
	if (index >= itv->nof_audio_inputs)
		return -EINVAL;
	strlcpy(audio->name, input_strs[aud_input->audio_type - 1],
			sizeof(audio->name));
	audio->index = index;
	audio->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

int ivtv_get_audio_output(struct ivtv *itv, u16 index, struct v4l2_audioout *aud_output)
{
	memset(aud_output, 0, sizeof(*aud_output));
	if (itv->card->video_outputs == NULL || index != 0)
		return -EINVAL;
	strlcpy(aud_output->name, "A/V Audio Out", sizeof(aud_output->name));
	return 0;
}
