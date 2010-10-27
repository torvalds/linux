/*
 *  cx18 functions to query card hardware
 *
 *  Derived from ivtv-cards.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include "cx18-driver.h"
#include "cx18-cards.h"
#include "cx18-av-core.h"
#include "cx18-i2c.h"
#include <media/cs5345.h>

#define V4L2_STD_PAL_SECAM (V4L2_STD_PAL|V4L2_STD_SECAM)

/********************** card configuration *******************************/

/* usual i2c tuner addresses to probe */
static struct cx18_card_tuner_i2c cx18_i2c_std = {
	.radio = { I2C_CLIENT_END },
	.demod = { 0x43, I2C_CLIENT_END },
	.tv    = { 0x61, 0x60, I2C_CLIENT_END },
};

/* Please add new PCI IDs to: http://pci-ids.ucw.cz/iii
   This keeps the PCI ID database up to date. Note that the entries
   must be added under vendor 0x4444 (Conexant) as subsystem IDs.
   New vendor IDs should still be added to the vendor ID list. */

/* Hauppauge HVR-1600 cards */

/* Note: for Hauppauge cards the tveeprom information is used instead
   of PCI IDs */
static const struct cx18_card cx18_card_hvr1600_esmt = {
	.type = CX18_CARD_HVR_1600_ESMT,
	.name = "Hauppauge HVR-1600",
	.comment = "Simultaneous Digital and Analog TV capture supported\n",
	.v4l2_capabilities = CX18_CAP_ENCODER,
	.hw_audio_ctrl = CX18_HW_418_AV,
	.hw_muxer = CX18_HW_CS5345,
	.hw_all = CX18_HW_TVEEPROM | CX18_HW_418_AV | CX18_HW_TUNER |
		  CX18_HW_CS5345 | CX18_HW_DVB | CX18_HW_GPIO_RESET_CTRL |
		  CX18_HW_Z8F0811_IR_HAUP,
	.video_inputs = {
		{ CX18_CARD_INPUT_VID_TUNER,  0, CX18_AV_COMPOSITE7 },
		{ CX18_CARD_INPUT_SVIDEO1,    1, CX18_AV_SVIDEO1    },
		{ CX18_CARD_INPUT_COMPOSITE1, 1, CX18_AV_COMPOSITE3 },
		{ CX18_CARD_INPUT_SVIDEO2,    2, CX18_AV_SVIDEO2    },
		{ CX18_CARD_INPUT_COMPOSITE2, 2, CX18_AV_COMPOSITE4 },
	},
	.audio_inputs = {
		{ CX18_CARD_INPUT_AUD_TUNER,
		  CX18_AV_AUDIO8, CS5345_IN_1 | CS5345_MCLK_1_5 },
		{ CX18_CARD_INPUT_LINE_IN1,
		  CX18_AV_AUDIO_SERIAL1, CS5345_IN_2 },
		{ CX18_CARD_INPUT_LINE_IN2,
		  CX18_AV_AUDIO_SERIAL1, CS5345_IN_3 },
	},
	.radio_input = { CX18_CARD_INPUT_AUD_TUNER,
			 CX18_AV_AUDIO_SERIAL1, CS5345_IN_4 },
	.ddr = {
		/* ESMT M13S128324A-5B memory */
		.chip_config = 0x003,
		.refresh = 0x30c,
		.timing1 = 0x44220e82,
		.timing2 = 0x08,
		.tune_lane = 0,
		.initial_emrs = 0,
	},
	.gpio_init.initial_value = 0x3001,
	.gpio_init.direction = 0x3001,
	.gpio_i2c_slave_reset = {
		.active_lo_mask = 0x3001,
		.msecs_asserted = 10,
		.msecs_recovery = 40,
		.ir_reset_mask  = 0x0001,
	},
	.i2c = &cx18_i2c_std,
};

static const struct cx18_card cx18_card_hvr1600_samsung = {
	.type = CX18_CARD_HVR_1600_SAMSUNG,
	.name = "Hauppauge HVR-1600 (Preproduction)",
	.comment = "Simultaneous Digital and Analog TV capture supported\n",
	.v4l2_capabilities = CX18_CAP_ENCODER,
	.hw_audio_ctrl = CX18_HW_418_AV,
	.hw_muxer = CX18_HW_CS5345,
	.hw_all = CX18_HW_TVEEPROM | CX18_HW_418_AV | CX18_HW_TUNER |
		  CX18_HW_CS5345 | CX18_HW_DVB | CX18_HW_GPIO_RESET_CTRL |
		  CX18_HW_Z8F0811_IR_HAUP,
	.video_inputs = {
		{ CX18_CARD_INPUT_VID_TUNER,  0, CX18_AV_COMPOSITE7 },
		{ CX18_CARD_INPUT_SVIDEO1,    1, CX18_AV_SVIDEO1    },
		{ CX18_CARD_INPUT_COMPOSITE1, 1, CX18_AV_COMPOSITE3 },
		{ CX18_CARD_INPUT_SVIDEO2,    2, CX18_AV_SVIDEO2    },
		{ CX18_CARD_INPUT_COMPOSITE2, 2, CX18_AV_COMPOSITE4 },
	},
	.audio_inputs = {
		{ CX18_CARD_INPUT_AUD_TUNER,
		  CX18_AV_AUDIO8, CS5345_IN_1 | CS5345_MCLK_1_5 },
		{ CX18_CARD_INPUT_LINE_IN1,
		  CX18_AV_AUDIO_SERIAL1, CS5345_IN_2 },
		{ CX18_CARD_INPUT_LINE_IN2,
		  CX18_AV_AUDIO_SERIAL1, CS5345_IN_3 },
	},
	.radio_input = { CX18_CARD_INPUT_AUD_TUNER,
			 CX18_AV_AUDIO_SERIAL1, CS5345_IN_4 },
	.ddr = {
		/* Samsung K4D263238G-VC33 memory */
		.chip_config = 0x003,
		.refresh = 0x30c,
		.timing1 = 0x23230b73,
		.timing2 = 0x08,
		.tune_lane = 0,
		.initial_emrs = 2,
	},
	.gpio_init.initial_value = 0x3001,
	.gpio_init.direction = 0x3001,
	.gpio_i2c_slave_reset = {
		.active_lo_mask = 0x3001,
		.msecs_asserted = 10,
		.msecs_recovery = 40,
		.ir_reset_mask  = 0x0001,
	},
	.i2c = &cx18_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Compro VideoMate H900: note that this card is analog only! */

static const struct cx18_card_pci_info cx18_pci_h900[] = {
	{ PCI_DEVICE_ID_CX23418, CX18_PCI_ID_COMPRO, 0xe100 },
	{ 0, 0, 0 }
};

static const struct cx18_card cx18_card_h900 = {
	.type = CX18_CARD_COMPRO_H900,
	.name = "Compro VideoMate H900",
	.comment = "Analog TV capture supported\n",
	.v4l2_capabilities = CX18_CAP_ENCODER,
	.hw_audio_ctrl = CX18_HW_418_AV,
	.hw_all = CX18_HW_418_AV | CX18_HW_TUNER | CX18_HW_GPIO_RESET_CTRL,
	.video_inputs = {
		{ CX18_CARD_INPUT_VID_TUNER,  0, CX18_AV_COMPOSITE2 },
		{ CX18_CARD_INPUT_SVIDEO1,    1,
			CX18_AV_SVIDEO_LUMA3 | CX18_AV_SVIDEO_CHROMA4 },
		{ CX18_CARD_INPUT_COMPOSITE1, 1, CX18_AV_COMPOSITE1 },
	},
	.audio_inputs = {
		{ CX18_CARD_INPUT_AUD_TUNER,
		  CX18_AV_AUDIO5, 0 },
		{ CX18_CARD_INPUT_LINE_IN1,
		  CX18_AV_AUDIO_SERIAL1, 0 },
	},
	.radio_input = { CX18_CARD_INPUT_AUD_TUNER,
			 CX18_AV_AUDIO_SERIAL1, 0 },
	.tuners = {
		{ .std = V4L2_STD_ALL, .tuner = TUNER_XC2028 },
	},
	.ddr = {
		/* EtronTech EM6A9160TS-5G memory */
		.chip_config = 0x50003,
		.refresh = 0x753,
		.timing1 = 0x24330e84,
		.timing2 = 0x1f,
		.tune_lane = 0,
		.initial_emrs = 0,
	},
	.xceive_pin = 15,
	.pci_list = cx18_pci_h900,
	.i2c = &cx18_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Yuan MPC718: not working at the moment! */

static const struct cx18_card_pci_info cx18_pci_mpc718[] = {
	{ PCI_DEVICE_ID_CX23418, CX18_PCI_ID_YUAN, 0x0718 },
	{ 0, 0, 0 }
};

static const struct cx18_card cx18_card_mpc718 = {
	.type = CX18_CARD_YUAN_MPC718,
	.name = "Yuan MPC718 MiniPCI DVB-T/Analog",
	.comment = "Experimenters needed for device to work well.\n"
		  "\tTo help, mail the ivtv-devel list (www.ivtvdriver.org).\n",
	.v4l2_capabilities = CX18_CAP_ENCODER,
	.hw_audio_ctrl = CX18_HW_418_AV,
	.hw_muxer = CX18_HW_GPIO_MUX,
	.hw_all = CX18_HW_TVEEPROM | CX18_HW_418_AV | CX18_HW_TUNER |
		  CX18_HW_GPIO_MUX | CX18_HW_DVB | CX18_HW_GPIO_RESET_CTRL,
	.video_inputs = {
		{ CX18_CARD_INPUT_VID_TUNER,  0, CX18_AV_COMPOSITE2 },
		{ CX18_CARD_INPUT_SVIDEO1,    1,
				CX18_AV_SVIDEO_LUMA3 | CX18_AV_SVIDEO_CHROMA4 },
		{ CX18_CARD_INPUT_COMPOSITE1, 1, CX18_AV_COMPOSITE1 },
		{ CX18_CARD_INPUT_SVIDEO2,    2,
				CX18_AV_SVIDEO_LUMA7 | CX18_AV_SVIDEO_CHROMA8 },
		{ CX18_CARD_INPUT_COMPOSITE2, 2, CX18_AV_COMPOSITE6 },
	},
	.audio_inputs = {
		{ CX18_CARD_INPUT_AUD_TUNER, CX18_AV_AUDIO5,        0 },
		{ CX18_CARD_INPUT_LINE_IN1,  CX18_AV_AUDIO_SERIAL1, 1 },
		{ CX18_CARD_INPUT_LINE_IN2,  CX18_AV_AUDIO_SERIAL2, 1 },
	},
	.tuners = {
		/* XC3028 tuner */
		{ .std = V4L2_STD_ALL, .tuner = TUNER_XC2028 },
	},
	/* FIXME - the FM radio is just a guess and driver doesn't use SIF */
	.radio_input = { CX18_CARD_INPUT_AUD_TUNER, CX18_AV_AUDIO5, 2 },
	.ddr = {
		/* Hynix HY5DU283222B DDR RAM */
		.chip_config = 0x303,
		.refresh = 0x3bd,
		.timing1 = 0x36320966,
		.timing2 = 0x1f,
		.tune_lane = 0,
		.initial_emrs = 2,
	},
	.gpio_init.initial_value = 0x1,
	.gpio_init.direction = 0x3,
	/* FIXME - these GPIO's are just guesses */
	.gpio_audio_input = { .mask   = 0x3,
			      .tuner  = 0x1,
			      .linein = 0x3,
			      .radio  = 0x1 },
	.xceive_pin = 0,
	.pci_list = cx18_pci_mpc718,
	.i2c = &cx18_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Conexant Raptor PAL/SECAM: note that this card is analog only! */

static const struct cx18_card_pci_info cx18_pci_cnxt_raptor_pal[] = {
	{ PCI_DEVICE_ID_CX23418, CX18_PCI_ID_CONEXANT, 0x0009 },
	{ 0, 0, 0 }
};

static const struct cx18_card cx18_card_cnxt_raptor_pal = {
	.type = CX18_CARD_CNXT_RAPTOR_PAL,
	.name = "Conexant Raptor PAL/SECAM",
	.comment = "Analog TV capture supported\n",
	.v4l2_capabilities = CX18_CAP_ENCODER,
	.hw_audio_ctrl = CX18_HW_418_AV,
	.hw_muxer = CX18_HW_GPIO_MUX,
	.hw_all = CX18_HW_418_AV | CX18_HW_TUNER | CX18_HW_GPIO_MUX,
	.video_inputs = {
		{ CX18_CARD_INPUT_VID_TUNER,  0, CX18_AV_COMPOSITE2 },
		{ CX18_CARD_INPUT_SVIDEO1,    1,
			CX18_AV_SVIDEO_LUMA3 | CX18_AV_SVIDEO_CHROMA4 },
		{ CX18_CARD_INPUT_COMPOSITE1, 1, CX18_AV_COMPOSITE1 },
		{ CX18_CARD_INPUT_SVIDEO2,    2,
			CX18_AV_SVIDEO_LUMA7 | CX18_AV_SVIDEO_CHROMA8 },
		{ CX18_CARD_INPUT_COMPOSITE2, 2, CX18_AV_COMPOSITE6 },
	},
	.audio_inputs = {
		{ CX18_CARD_INPUT_AUD_TUNER, CX18_AV_AUDIO5, 	    0 },
		{ CX18_CARD_INPUT_LINE_IN1,  CX18_AV_AUDIO_SERIAL1, 1 },
		{ CX18_CARD_INPUT_LINE_IN2,  CX18_AV_AUDIO_SERIAL2, 1 },
	},
	.tuners = {
		{ .std = V4L2_STD_PAL_SECAM, .tuner = TUNER_PHILIPS_FM1216ME_MK3 },
	},
	.radio_input = { CX18_CARD_INPUT_AUD_TUNER, CX18_AV_AUDIO_SERIAL1, 2 },
	.ddr = {
		/* MT 46V16M16 memory */
		.chip_config = 0x50306,
		.refresh = 0x753,
		.timing1 = 0x33220953,
		.timing2 = 0x09,
		.tune_lane = 0,
		.initial_emrs = 0,
	},
	.gpio_init.initial_value = 0x1002,
	.gpio_init.direction = 0xf002,
	.gpio_audio_input = { .mask   = 0xf002,
			      .tuner  = 0x1002,   /* LED D1  Tuner AF  */
			      .linein = 0x2000,   /* LED D2  Line In 1 */
			      .radio  = 0x4002 }, /* LED D3  Tuner AF  */
	.pci_list = cx18_pci_cnxt_raptor_pal,
	.i2c = &cx18_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Toshiba Qosmio laptop internal DVB-T/Analog Hybrid Tuner */

static const struct cx18_card_pci_info cx18_pci_toshiba_qosmio_dvbt[] = {
	{ PCI_DEVICE_ID_CX23418, CX18_PCI_ID_TOSHIBA, 0x0110 },
	{ 0, 0, 0 }
};

static const struct cx18_card cx18_card_toshiba_qosmio_dvbt = {
	.type = CX18_CARD_TOSHIBA_QOSMIO_DVBT,
	.name = "Toshiba Qosmio DVB-T/Analog",
	.comment = "Experimenters and photos needed for device to work well.\n"
		  "\tTo help, mail the ivtv-devel list (www.ivtvdriver.org).\n",
	.v4l2_capabilities = CX18_CAP_ENCODER,
	.hw_audio_ctrl = CX18_HW_418_AV,
	.hw_all = CX18_HW_418_AV | CX18_HW_TUNER | CX18_HW_GPIO_RESET_CTRL,
	.video_inputs = {
		{ CX18_CARD_INPUT_VID_TUNER,  0, CX18_AV_COMPOSITE6 },
		{ CX18_CARD_INPUT_SVIDEO1,    1,
			CX18_AV_SVIDEO_LUMA3 | CX18_AV_SVIDEO_CHROMA4 },
		{ CX18_CARD_INPUT_COMPOSITE1, 1, CX18_AV_COMPOSITE1 },
	},
	.audio_inputs = {
		{ CX18_CARD_INPUT_AUD_TUNER, CX18_AV_AUDIO5, 	    0 },
		{ CX18_CARD_INPUT_LINE_IN1,  CX18_AV_AUDIO_SERIAL1, 1 },
	},
	.tuners = {
		{ .std = V4L2_STD_ALL, .tuner = TUNER_XC2028 },
	},
	.ddr = {
		.chip_config = 0x202,
		.refresh = 0x3bb,
		.timing1 = 0x33320a63,
		.timing2 = 0x0a,
		.tune_lane = 0,
		.initial_emrs = 0x42,
	},
	.xceive_pin = 15,
	.pci_list = cx18_pci_toshiba_qosmio_dvbt,
	.i2c = &cx18_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Leadtek WinFast PVR2100 */

static const struct cx18_card_pci_info cx18_pci_leadtek_pvr2100[] = {
	{ PCI_DEVICE_ID_CX23418, CX18_PCI_ID_LEADTEK, 0x6f27 }, /* PVR2100   */
	{ 0, 0, 0 }
};

static const struct cx18_card cx18_card_leadtek_pvr2100 = {
	.type = CX18_CARD_LEADTEK_PVR2100,
	.name = "Leadtek WinFast PVR2100",
	.comment = "Experimenters and photos needed for device to work well.\n"
		  "\tTo help, mail the ivtv-devel list (www.ivtvdriver.org).\n",
	.v4l2_capabilities = CX18_CAP_ENCODER,
	.hw_audio_ctrl = CX18_HW_418_AV,
	.hw_muxer = CX18_HW_GPIO_MUX,
	.hw_all = CX18_HW_418_AV | CX18_HW_TUNER | CX18_HW_GPIO_MUX |
		  CX18_HW_GPIO_RESET_CTRL,
	.video_inputs = {
		{ CX18_CARD_INPUT_VID_TUNER,  0, CX18_AV_COMPOSITE2 },
		{ CX18_CARD_INPUT_SVIDEO1,    1,
			CX18_AV_SVIDEO_LUMA3 | CX18_AV_SVIDEO_CHROMA4 },
		{ CX18_CARD_INPUT_COMPOSITE1, 1, CX18_AV_COMPOSITE7 },
		{ CX18_CARD_INPUT_COMPONENT1, 1, CX18_AV_COMPONENT1 },
	},
	.audio_inputs = {
		{ CX18_CARD_INPUT_AUD_TUNER, CX18_AV_AUDIO5, 	    0 },
		{ CX18_CARD_INPUT_LINE_IN1,  CX18_AV_AUDIO_SERIAL1, 1 },
	},
	.tuners = {
		/* XC2028 tuner */
		{ .std = V4L2_STD_ALL, .tuner = TUNER_XC2028 },
	},
	.radio_input = { CX18_CARD_INPUT_AUD_TUNER, CX18_AV_AUDIO5, 2 },
	.ddr = {
		/* Pointer to proper DDR config values provided by Terry Wu */
		.chip_config = 0x303,
		.refresh = 0x3bb,
		.timing1 = 0x24220e83,
		.timing2 = 0x1f,
		.tune_lane = 0,
		.initial_emrs = 0x2,
	},
	.gpio_init.initial_value = 0x6,
	.gpio_init.direction = 0x7,
	.gpio_audio_input = { .mask   = 0x7,
			      .tuner  = 0x6, .linein = 0x2, .radio  = 0x2 },
	.xceive_pin = 1,
	.pci_list = cx18_pci_leadtek_pvr2100,
	.i2c = &cx18_i2c_std,
};

/* ------------------------------------------------------------------------- */

/* Leadtek WinFast DVR3100 H */

static const struct cx18_card_pci_info cx18_pci_leadtek_dvr3100h[] = {
	{ PCI_DEVICE_ID_CX23418, CX18_PCI_ID_LEADTEK, 0x6690 }, /* DVR3100 H */
	{ 0, 0, 0 }
};

static const struct cx18_card cx18_card_leadtek_dvr3100h = {
	.type = CX18_CARD_LEADTEK_DVR3100H,
	.name = "Leadtek WinFast DVR3100 H",
	.comment = "Simultaneous DVB-T and Analog capture supported,\n"
		  "\texcept when capturing Analog from the antenna input.\n",
	.v4l2_capabilities = CX18_CAP_ENCODER,
	.hw_audio_ctrl = CX18_HW_418_AV,
	.hw_muxer = CX18_HW_GPIO_MUX,
	.hw_all = CX18_HW_418_AV | CX18_HW_TUNER | CX18_HW_GPIO_MUX |
		  CX18_HW_DVB | CX18_HW_GPIO_RESET_CTRL,
	.video_inputs = {
		{ CX18_CARD_INPUT_VID_TUNER,  0, CX18_AV_COMPOSITE2 },
		{ CX18_CARD_INPUT_SVIDEO1,    1,
			CX18_AV_SVIDEO_LUMA3 | CX18_AV_SVIDEO_CHROMA4 },
		{ CX18_CARD_INPUT_COMPOSITE1, 1, CX18_AV_COMPOSITE7 },
		{ CX18_CARD_INPUT_COMPONENT1, 1, CX18_AV_COMPONENT1 },
	},
	.audio_inputs = {
		{ CX18_CARD_INPUT_AUD_TUNER, CX18_AV_AUDIO5, 	    0 },
		{ CX18_CARD_INPUT_LINE_IN1,  CX18_AV_AUDIO_SERIAL1, 1 },
	},
	.tuners = {
		/* XC3028 tuner */
		{ .std = V4L2_STD_ALL, .tuner = TUNER_XC2028 },
	},
	.radio_input = { CX18_CARD_INPUT_AUD_TUNER, CX18_AV_AUDIO5, 2 },
	.ddr = {
		/* Pointer to proper DDR config values provided by Terry Wu */
		.chip_config = 0x303,
		.refresh = 0x3bb,
		.timing1 = 0x24220e83,
		.timing2 = 0x1f,
		.tune_lane = 0,
		.initial_emrs = 0x2,
	},
	.gpio_init.initial_value = 0x6,
	.gpio_init.direction = 0x7,
	.gpio_audio_input = { .mask   = 0x7,
			      .tuner  = 0x6, .linein = 0x2, .radio  = 0x2 },
	.xceive_pin = 1,
	.pci_list = cx18_pci_leadtek_dvr3100h,
	.i2c = &cx18_i2c_std,
};

/* ------------------------------------------------------------------------- */

static const struct cx18_card *cx18_card_list[] = {
	&cx18_card_hvr1600_esmt,
	&cx18_card_hvr1600_samsung,
	&cx18_card_h900,
	&cx18_card_mpc718,
	&cx18_card_cnxt_raptor_pal,
	&cx18_card_toshiba_qosmio_dvbt,
	&cx18_card_leadtek_pvr2100,
	&cx18_card_leadtek_dvr3100h,
};

const struct cx18_card *cx18_get_card(u16 index)
{
	if (index >= ARRAY_SIZE(cx18_card_list))
		return NULL;
	return cx18_card_list[index];
}

int cx18_get_input(struct cx18 *cx, u16 index, struct v4l2_input *input)
{
	const struct cx18_card_video_input *card_input =
		cx->card->video_inputs + index;
	static const char * const input_strs[] = {
		"Tuner 1",
		"S-Video 1",
		"S-Video 2",
		"Composite 1",
		"Composite 2",
		"Component 1"
	};

	memset(input, 0, sizeof(*input));
	if (index >= cx->nof_inputs)
		return -EINVAL;
	input->index = index;
	strlcpy(input->name, input_strs[card_input->video_type - 1],
			sizeof(input->name));
	input->type = (card_input->video_type == CX18_CARD_INPUT_VID_TUNER ?
			V4L2_INPUT_TYPE_TUNER : V4L2_INPUT_TYPE_CAMERA);
	input->audioset = (1 << cx->nof_audio_inputs) - 1;
	input->std = (input->type == V4L2_INPUT_TYPE_TUNER) ?
				cx->tuner_std : V4L2_STD_ALL;
	return 0;
}

int cx18_get_audio_input(struct cx18 *cx, u16 index, struct v4l2_audio *audio)
{
	const struct cx18_card_audio_input *aud_input =
		cx->card->audio_inputs + index;
	static const char * const input_strs[] = {
		"Tuner 1",
		"Line In 1",
		"Line In 2"
	};

	memset(audio, 0, sizeof(*audio));
	if (index >= cx->nof_audio_inputs)
		return -EINVAL;
	strlcpy(audio->name, input_strs[aud_input->audio_type - 1],
			sizeof(audio->name));
	audio->index = index;
	audio->capability = V4L2_AUDCAP_STEREO;
	return 0;
}
