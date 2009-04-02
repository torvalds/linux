/*
 *  cx18 functions to query card hardware
 *
 *  Derived from ivtv-cards.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@radix.net>
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* hardware flags */
#define CX18_HW_TUNER		(1 << 0)
#define CX18_HW_TVEEPROM	(1 << 1)
#define CX18_HW_CS5345		(1 << 2)
#define CX18_HW_DVB		(1 << 3)
#define CX18_HW_418_AV		(1 << 4)
#define CX18_HW_GPIO_MUX	(1 << 5)
#define CX18_HW_GPIO_RESET_CTRL	(1 << 6)

/* video inputs */
#define	CX18_CARD_INPUT_VID_TUNER	1
#define	CX18_CARD_INPUT_SVIDEO1 	2
#define	CX18_CARD_INPUT_SVIDEO2 	3
#define	CX18_CARD_INPUT_COMPOSITE1 	4
#define	CX18_CARD_INPUT_COMPOSITE2 	5
#define	CX18_CARD_INPUT_COMPOSITE3 	6

/* audio inputs */
#define	CX18_CARD_INPUT_AUD_TUNER	1
#define	CX18_CARD_INPUT_LINE_IN1 	2
#define	CX18_CARD_INPUT_LINE_IN2 	3

#define CX18_CARD_MAX_VIDEO_INPUTS 6
#define CX18_CARD_MAX_AUDIO_INPUTS 3
#define CX18_CARD_MAX_TUNERS  	   2

/* V4L2 capability aliases */
#define CX18_CAP_ENCODER (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_TUNER | \
			  V4L2_CAP_AUDIO | V4L2_CAP_READWRITE | \
			  V4L2_CAP_VBI_CAPTURE | V4L2_CAP_SLICED_VBI_CAPTURE)

struct cx18_card_video_input {
	u8  video_type; 	/* video input type */
	u8  audio_index;	/* index in cx18_card_audio_input array */
	u16 video_input;	/* hardware video input */
};

struct cx18_card_audio_input {
	u8  audio_type;		/* audio input type */
	u32 audio_input;	/* hardware audio input */
	u16 muxer_input;	/* hardware muxer input for boards with a
				   multiplexer chip */
};

struct cx18_card_pci_info {
	u16 device;
	u16 subsystem_vendor;
	u16 subsystem_device;
};

/* GPIO definitions */

/* The mask is the set of bits used by the operation */

struct cx18_gpio_init { /* set initial GPIO DIR and OUT values */
	u32 direction; 	/* DIR setting. Leave to 0 if no init is needed */
	u32 initial_value;
};

struct cx18_gpio_i2c_slave_reset {
	u32 active_lo_mask; /* GPIO outputs that reset i2c chips when low */
	u32 active_hi_mask; /* GPIO outputs that reset i2c chips when high */
	int msecs_asserted; /* time period reset must remain asserted */
	int msecs_recovery; /* time after deassert for chips to be ready */
	u32 ir_reset_mask;  /* GPIO to reset the Zilog Z8F0811 IR contoller */
};

struct cx18_gpio_audio_input { 	/* select tuner/line in input */
	u32 mask; 		/* leave to 0 if not supported */
	u32 tuner;
	u32 linein;
	u32 radio;
};

struct cx18_card_tuner {
	v4l2_std_id std; 	/* standard for which the tuner is suitable */
	int 	    tuner; 	/* tuner ID (from tuner.h) */
};

struct cx18_card_tuner_i2c {
	unsigned short radio[2];/* radio tuner i2c address to probe */
	unsigned short demod[2];/* demodulator i2c address to probe */
	unsigned short tv[4];	/* tv tuner i2c addresses to probe */
};

struct cx18_ddr {		/* DDR config data */
	u32 chip_config;
	u32 refresh;
	u32 timing1;
	u32 timing2;
	u32 tune_lane;
	u32 initial_emrs;
};

/* for card information/parameters */
struct cx18_card {
	int type;
	char *name;
	char *comment;
	u32 v4l2_capabilities;
	u32 hw_audio_ctrl;	/* hardware used for the V4L2 controls (only
				   1 dev allowed currently) */
	u32 hw_muxer;		/* hardware used to multiplex audio input */
	u32 hw_all;		/* all hardware used by the board */
	struct cx18_card_video_input video_inputs[CX18_CARD_MAX_VIDEO_INPUTS];
	struct cx18_card_audio_input audio_inputs[CX18_CARD_MAX_AUDIO_INPUTS];
	struct cx18_card_audio_input radio_input;

	/* GPIO card-specific settings */
	u8 xceive_pin; 		/* XCeive tuner GPIO reset pin */
	struct cx18_gpio_init 		 gpio_init;
	struct cx18_gpio_i2c_slave_reset gpio_i2c_slave_reset;
	struct cx18_gpio_audio_input    gpio_audio_input;

	struct cx18_card_tuner tuners[CX18_CARD_MAX_TUNERS];
	struct cx18_card_tuner_i2c *i2c;

	struct cx18_ddr ddr;

	/* list of device and subsystem vendor/devices that
	   correspond to this card type. */
	const struct cx18_card_pci_info *pci_list;
};

int cx18_get_input(struct cx18 *cx, u16 index, struct v4l2_input *input);
int cx18_get_audio_input(struct cx18 *cx, u16 index, struct v4l2_audio *input);
const struct cx18_card *cx18_get_card(u16 index);
