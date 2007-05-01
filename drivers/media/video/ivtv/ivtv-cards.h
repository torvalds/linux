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

/* hardware flags */
#define IVTV_HW_CX25840   (1 << 0)
#define IVTV_HW_SAA7115   (1 << 1)
#define IVTV_HW_SAA7127   (1 << 2)
#define IVTV_HW_MSP34XX   (1 << 3)
#define IVTV_HW_TUNER     (1 << 4)
#define IVTV_HW_WM8775    (1 << 5)
#define IVTV_HW_CS53L32A  (1 << 6)
#define IVTV_HW_TVEEPROM  (1 << 7)
#define IVTV_HW_SAA7114   (1 << 8)
#define IVTV_HW_TVAUDIO   (1 << 9)
#define IVTV_HW_UPD64031A (1 << 10)
#define IVTV_HW_UPD6408X  (1 << 11)
#define IVTV_HW_SAA717X   (1 << 12)
#define IVTV_HW_WM8739    (1 << 13)
#define IVTV_HW_GPIO      (1 << 14)

#define IVTV_HW_SAA711X   (IVTV_HW_SAA7115 | IVTV_HW_SAA7114)

/* video inputs */
#define	IVTV_CARD_INPUT_VID_TUNER	1
#define	IVTV_CARD_INPUT_SVIDEO1 	2
#define	IVTV_CARD_INPUT_SVIDEO2 	3
#define	IVTV_CARD_INPUT_COMPOSITE1 	4
#define	IVTV_CARD_INPUT_COMPOSITE2 	5
#define	IVTV_CARD_INPUT_COMPOSITE3 	6

/* audio inputs */
#define	IVTV_CARD_INPUT_AUD_TUNER	1
#define	IVTV_CARD_INPUT_LINE_IN1 	2
#define	IVTV_CARD_INPUT_LINE_IN2 	3

#define IVTV_CARD_MAX_VIDEO_INPUTS 6
#define IVTV_CARD_MAX_AUDIO_INPUTS 3
#define IVTV_CARD_MAX_TUNERS  	   2

/* SAA71XX HW inputs */
#define IVTV_SAA71XX_COMPOSITE0 0
#define IVTV_SAA71XX_COMPOSITE1 1
#define IVTV_SAA71XX_COMPOSITE2 2
#define IVTV_SAA71XX_COMPOSITE3 3
#define IVTV_SAA71XX_COMPOSITE4 4
#define IVTV_SAA71XX_COMPOSITE5 5
#define IVTV_SAA71XX_SVIDEO0    6
#define IVTV_SAA71XX_SVIDEO1    7
#define IVTV_SAA71XX_SVIDEO2    8
#define IVTV_SAA71XX_SVIDEO3    9

/* SAA717X needs to mark the tuner input by ORing with this flag */
#define IVTV_SAA717X_TUNER_FLAG 0x80

/* Dummy HW input */
#define IVTV_DUMMY_AUDIO        0

/* GPIO HW inputs */
#define IVTV_GPIO_TUNER   0
#define IVTV_GPIO_LINE_IN 1

/* SAA717X HW inputs */
#define IVTV_SAA717X_IN0 0
#define IVTV_SAA717X_IN1 1
#define IVTV_SAA717X_IN2 2

/* V4L2 capability aliases */
#define IVTV_CAP_ENCODER (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_TUNER | \
			  V4L2_CAP_AUDIO | V4L2_CAP_READWRITE | V4L2_CAP_VBI_CAPTURE | \
			  V4L2_CAP_SLICED_VBI_CAPTURE)
#define IVTV_CAP_DECODER (V4L2_CAP_VBI_OUTPUT | V4L2_CAP_VIDEO_OUTPUT | \
			  V4L2_CAP_SLICED_VBI_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_OVERLAY | V4L2_CAP_VIDEO_OUTPUT_POS)

struct ivtv_card_video_input {
	u8  video_type; 	/* video input type */
	u8  audio_index;	/* index in ivtv_card_audio_input array */
	u16 video_input;	/* hardware video input */
};

struct ivtv_card_audio_input {
	u8  audio_type;		/* audio input type */
	u32 audio_input;	/* hardware audio input */
	u16 muxer_input;	/* hardware muxer input for boards with a
				   multiplexer chip */
};

struct ivtv_card_output {
	u8  name[32];
	u16 video_output;  /* hardware video output */
};

struct ivtv_card_pci_info {
	u16 device;
	u16 subsystem_vendor;
	u16 subsystem_device;
};

/* GPIO definitions */

/* The mask is the set of bits used by the operation */

struct ivtv_gpio_init { 	/* set initial GPIO DIR and OUT values */
	u16 direction; 		/* DIR setting. Leave to 0 if no init is needed */
	u16 initial_value;
};

struct ivtv_gpio_video_input { 	/* select tuner/line in input */
	u16 mask; 		/* leave to 0 if not supported */
	u16 tuner;
	u16 composite;
	u16 svideo;
};

struct ivtv_gpio_audio_input { 	/* select tuner/line in input */
	u16 mask; 		/* leave to 0 if not supported */
	u16 tuner;
	u16 linein;
	u16 radio;
};

struct ivtv_gpio_audio_mute {
	u16 mask; 		/* leave to 0 if not supported */
	u16 mute;		/* set this value to mute, 0 to unmute */
};

struct ivtv_gpio_audio_mode {
	u16 mask; 		/* leave to 0 if not supported */
	u16 mono; 		/* set audio to mono */
	u16 stereo; 		/* set audio to stereo */
	u16 lang1;		/* set audio to the first language */
	u16 lang2;		/* set audio to the second language */
	u16 both; 		/* both languages are output */
};

struct ivtv_gpio_audio_freq {
	u16 mask; 		/* leave to 0 if not supported */
	u16 f32000;
	u16 f44100;
	u16 f48000;
};

struct ivtv_gpio_audio_detect {
	u16 mask; 		/* leave to 0 if not supported */
	u16 stereo; 		/* if the input matches this value then
				   stereo is detected */
};

struct ivtv_card_tuner {
	v4l2_std_id std; 	/* standard for which the tuner is suitable */
	int 	    tuner; 	/* tuner ID (from tuner.h) */
};

/* for card information/parameters */
struct ivtv_card {
	int type;
	char *name;
	u32 v4l2_capabilities;
	u32 hw_video;		/* hardware used to process video */
	u32 hw_audio;		/* hardware used to process audio */
	u32 hw_audio_ctrl;	/* hardware used for the V4L2 controls (only 1 dev allowed) */
	u32 hw_muxer;		/* hardware used to multiplex audio input */
	u32 hw_all;		/* all hardware used by the board */
	struct ivtv_card_video_input video_inputs[IVTV_CARD_MAX_VIDEO_INPUTS];
	struct ivtv_card_audio_input audio_inputs[IVTV_CARD_MAX_AUDIO_INPUTS];
	struct ivtv_card_audio_input radio_input;
	int nof_outputs;
	const struct ivtv_card_output *video_outputs;
	u8 gr_config; 		/* config byte for the ghost reduction device */

	/* GPIO card-specific settings */
	struct ivtv_gpio_init 		gpio_init;
	struct ivtv_gpio_video_input	gpio_video_input;
	struct ivtv_gpio_audio_input 	gpio_audio_input;
	struct ivtv_gpio_audio_mute 	gpio_audio_mute;
	struct ivtv_gpio_audio_mode 	gpio_audio_mode;
	struct ivtv_gpio_audio_freq 	gpio_audio_freq;
	struct ivtv_gpio_audio_detect 	gpio_audio_detect;

	struct ivtv_card_tuner tuners[IVTV_CARD_MAX_TUNERS];

	/* list of device and subsystem vendor/devices that
	   correspond to this card type. */
	const struct ivtv_card_pci_info *pci_list;
};

int ivtv_get_input(struct ivtv *itv, u16 index, struct v4l2_input *input);
int ivtv_get_output(struct ivtv *itv, u16 index, struct v4l2_output *output);
int ivtv_get_audio_input(struct ivtv *itv, u16 index, struct v4l2_audio *input);
int ivtv_get_audio_output(struct ivtv *itv, u16 index, struct v4l2_audioout *output);
const struct ivtv_card *ivtv_get_card(u16 index);
