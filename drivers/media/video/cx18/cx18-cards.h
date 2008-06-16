/*
 *  cx18 functions to query card hardware
 *
 *  Derived from ivtv-cards.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
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
#define CX18_HW_TUNER     (1 << 0)
#define CX18_HW_TVEEPROM  (1 << 1)
#define CX18_HW_CS5345    (1 << 2)
#define CX18_HW_GPIO      (1 << 3)
#define CX18_HW_CX23418   (1 << 4)
#define CX18_HW_DVB   	  (1 << 5)

/* video inputs */
#define	CX18_CARD_INPUT_VID_TUNER	1
#define	CX18_CARD_INPUT_SVIDEO1 	2
#define	CX18_CARD_INPUT_SVIDEO2 	3
#define	CX18_CARD_INPUT_COMPOSITE1 	4
#define	CX18_CARD_INPUT_COMPOSITE2 	5
#define	CX18_CARD_INPUT_COMPOSITE3 	6

enum cx34180_video_input {
	/* Composite video inputs In1-In8 */
	CX23418_COMPOSITE1 = 1,
	CX23418_COMPOSITE2,
	CX23418_COMPOSITE3,
	CX23418_COMPOSITE4,
	CX23418_COMPOSITE5,
	CX23418_COMPOSITE6,
	CX23418_COMPOSITE7,
	CX23418_COMPOSITE8,

	/* S-Video inputs consist of one luma input (In1-In4) ORed with one
	   chroma input (In5-In8) */
	CX23418_SVIDEO_LUMA1 = 0x10,
	CX23418_SVIDEO_LUMA2 = 0x20,
	CX23418_SVIDEO_LUMA3 = 0x30,
	CX23418_SVIDEO_LUMA4 = 0x40,
	CX23418_SVIDEO_CHROMA4 = 0x400,
	CX23418_SVIDEO_CHROMA5 = 0x500,
	CX23418_SVIDEO_CHROMA6 = 0x600,
	CX23418_SVIDEO_CHROMA7 = 0x700,
	CX23418_SVIDEO_CHROMA8 = 0x800,

	/* S-Video aliases for common luma/chroma combinations */
	CX23418_SVIDEO1 = 0x510,
	CX23418_SVIDEO2 = 0x620,
	CX23418_SVIDEO3 = 0x730,
	CX23418_SVIDEO4 = 0x840,
};

/* audio inputs */
#define	CX18_CARD_INPUT_AUD_TUNER	1
#define	CX18_CARD_INPUT_LINE_IN1 	2
#define	CX18_CARD_INPUT_LINE_IN2 	3

#define CX18_CARD_MAX_VIDEO_INPUTS 6
#define CX18_CARD_MAX_AUDIO_INPUTS 3
#define CX18_CARD_MAX_TUNERS  	   2

enum cx23418_audio_input {
	/* Audio inputs: serial or In4-In8 */
	CX23418_AUDIO_SERIAL,
	CX23418_AUDIO4 = 4,
	CX23418_AUDIO5,
	CX23418_AUDIO6,
	CX23418_AUDIO7,
	CX23418_AUDIO8,
};

/* V4L2 capability aliases */
#define CX18_CAP_ENCODER (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_TUNER | \
			  V4L2_CAP_AUDIO | V4L2_CAP_READWRITE)
/* | V4L2_CAP_VBI_CAPTURE | V4L2_CAP_SLICED_VBI_CAPTURE) not yet */

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
				   1 dev allowed) */
	u32 hw_muxer;		/* hardware used to multiplex audio input */
	u32 hw_all;		/* all hardware used by the board */
	struct cx18_card_video_input video_inputs[CX18_CARD_MAX_VIDEO_INPUTS];
	struct cx18_card_audio_input audio_inputs[CX18_CARD_MAX_AUDIO_INPUTS];
	struct cx18_card_audio_input radio_input;

	/* GPIO card-specific settings */
	u8 xceive_pin; 		/* XCeive tuner GPIO reset pin */
	struct cx18_gpio_init 		gpio_init;

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
