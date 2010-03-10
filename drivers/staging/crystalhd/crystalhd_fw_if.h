/***************************************************************************
 * Copyright (c) 2005-2009, Broadcom Corporation.
 *
 *  Name: crystalhd_fw_if . h
 *
 *  Description:
 *		BCM70012 Firmware interface definitions.
 *
 *  HISTORY:
 *
 **********************************************************************
 * This file is part of the crystalhd device driver.
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#ifndef _CRYSTALHD_FW_IF_H_
#define _CRYSTALHD_FW_IF_H_

/* TBD: Pull in only required defs into this file.. */



/* User Data Header */
typedef struct user_data {
   struct user_data	*next;
   uint32_t		type;
   uint32_t		size;
} UD_HDR;



/*------------------------------------------------------*
 *    MPEG Extension to the PPB			 *
 *------------------------------------------------------*/
typedef struct {
   uint32_t		to_be_defined;
   uint32_t		valid;

   /* Always valid, defaults to picture size if no
      sequence display extension in the stream. */
   uint32_t		display_horizontal_size;
   uint32_t		display_vertical_size;

   /* MPEG_VALID_PANSCAN
      Offsets are a copy values from the MPEG stream. */
   uint32_t		offset_count;
   int32_t		horizontal_offset[3];
   int32_t		vertical_offset[3];

   /* MPEG_VALID_USERDATA
      User data is in the form of a linked list. */
   int32_t		userDataSize;
   UD_HDR		*userData;

} PPB_MPEG;


/*------------------------------------------------------*
 *    VC1 Extension to the PPB			  *
 *------------------------------------------------------*/
typedef struct {
   uint32_t		to_be_defined;
   uint32_t		valid;

   /* Always valid, defaults to picture size if no
      sequence display extension in the stream. */
   uint32_t		display_horizontal_size;
   uint32_t		display_vertical_size;

  /* VC1 pan scan windows */
   uint32_t		num_panscan_windows;
   int32_t		ps_horiz_offset[4];
   int32_t		ps_vert_offset[4];
   int32_t		ps_width[4];
   int32_t		ps_height[4];

   /* VC1_VALID_USERDATA
      User data is in the form of a linked list. */
   int32_t		userDataSize;
   UD_HDR		*userData;

} PPB_VC1;

/*------------------------------------------------------*
 *    H.264 Extension to the PPB			*
 *------------------------------------------------------*/

/**
 * @brief Film grain SEI message.
 *
 * Content of the film grain SEI message.
 */

/* maximum number of model-values as for Thomson spec(standard says 5) */
#define MAX_FGT_MODEL_VALUE	 (3)

/* maximum number of intervals(as many as 256 intervals?) */
#define MAX_FGT_VALUE_INTERVAL	(256)

typedef struct FGT_SEI {
    struct FGT_SEI *next;
    unsigned char model_values[3][MAX_FGT_VALUE_INTERVAL][MAX_FGT_MODEL_VALUE];
    unsigned char upper_bound[3][MAX_FGT_VALUE_INTERVAL];
    unsigned char lower_bound[3][MAX_FGT_VALUE_INTERVAL];

    unsigned char cancel_flag;	/* Cancel flag: 1 no film grain. */
    unsigned char model_id;	/* Model id. */

    /* +unused SE based on Thomson spec */
    unsigned char color_desc_flag;	/* Separate color descrition flag. */
    unsigned char bit_depth_luma;	/* Bit depth luma minus 8. */
    unsigned char bit_depth_chroma;	/* Bit depth chroma minus 8. */
    unsigned char full_range_flag;	/* Full range flag. */
    unsigned char color_primaries;	/* Color primaries. */
    unsigned char transfer_charact;	/* Transfer characteristics. */
    unsigned char matrix_coeff;		/*< Matrix coefficients. */
    /* -unused SE based on Thomson spec */

    unsigned char blending_mode_id;	/* Blending mode. */
    unsigned char log2_scale_factor;	/* Log2 scale factor (2-7). */
    unsigned char comp_flag[3];		/* Components [0,2] parameters present flag. */
    unsigned char num_intervals_minus1[3]; /* Number of intensity level intervals. */
    unsigned char num_model_values[3];	/* Number of model values. */
    uint16_t      repetition_period;	/* Repetition period (0-16384) */

} FGT_SEI;

typedef struct {
   /* 'valid' specifies which fields (or sets of
    * fields) below are valid.  If the corresponding
    * bit in 'valid' is NOT set then that field(s)
    * is (are) not initialized. */
   uint32_t	valid;

   int32_t		poc_top;	/* POC for Top Field/Frame */
   int32_t		poc_bottom;	/* POC for Bottom Field    */
   uint32_t		idr_pic_id;

   /* H264_VALID_PANSCAN */
   uint32_t		pan_scan_count;
   int32_t		pan_scan_left[3];
   int32_t		pan_scan_right[3];
   int32_t		pan_scan_top[3];
   int32_t		pan_scan_bottom[3];

   /* H264_VALID_CT_TYPE */
   uint32_t		ct_type_count;
   uint32_t		ct_type[3];

   /* H264_VALID_SPS_CROP */
   int32_t		sps_crop_left;
   int32_t		sps_crop_right;
   int32_t		sps_crop_top;
   int32_t		sps_crop_bottom;

   /* H264_VALID_VUI */
   uint32_t		chroma_top;
   uint32_t		chroma_bottom;

   /* H264_VALID_USER */
   uint32_t		user_data_size;
   UD_HDR		*user_data;

   /* H264 VALID FGT */
   FGT_SEI		*pfgt;

} PPB_H264;

typedef struct {
   /* Common fields. */
   uint32_t	picture_number;	/* Ordinal display number */
   uint32_t	video_buffer;	/* Video (picbuf) number */
   uint32_t	video_address;	/* Address of picbuf Y */
   uint32_t	video_address_uv; /* Address of picbuf UV */
   uint32_t	video_stripe;	/* Picbuf stripe */
   uint32_t	video_width;	/* Picbuf width */
   uint32_t	video_height;	/* Picbuf height */

   uint32_t	channel_id;	/* Decoder channel ID */
   uint32_t	status;		/* reserved */
   uint32_t	width;		/* pixels */
   uint32_t	height;		/* pixels */
   uint32_t	chroma_format;	/* see above */
   uint32_t	pulldown;	/* see above */
   uint32_t	flags;		/* see above */
   uint32_t	pts;		/* 32 LSBs of PTS */
   uint32_t	protocol;	/* protocolXXX (above) */

   uint32_t	frame_rate;	/* see above */
   uint32_t	matrix_coeff;	/* see above */
   uint32_t	aspect_ratio;	/* see above */
   uint32_t	colour_primaries; /* see above */
   uint32_t	transfer_char;	/* see above */
   uint32_t	pcr_offset;	/* 45kHz if PCR type; else 27MHz */
   uint32_t	n_drop;		/* Number of pictures to be dropped */

   uint32_t	custom_aspect_ratio_width_height;
			/* upper 16-bits is Y and lower 16-bits is X */

   uint32_t	picture_tag;	/* Indexing tag from BUD packets */
   uint32_t	picture_done_payload;
   uint32_t	picture_meta_payload;
   uint32_t	reserved[1];

   /* Protocol-specific extensions. */
   union {
      PPB_H264	h264;
      PPB_MPEG	mpeg;
      PPB_VC1	 vc1;
   } other;

} PPB;

typedef struct {
   uint32_t	bFormatChange;
   uint32_t	resolution;
   uint32_t	channelId;
   uint32_t	ppbPtr;
   int32_t	ptsStcOffset;
   uint32_t	zeroPanscanValid;
   uint32_t	dramOutBufAddr;
   uint32_t	yComponent;
   PPB			ppb;

} C011_PIB;



typedef struct {
    uint32_t	command;
    uint32_t	sequence;
    uint32_t	status;
    uint32_t	picBuf;
    uint32_t	picRelBuf;
    uint32_t	picInfoDeliveryQ;
    uint32_t	picInfoReleaseQ;
    uint32_t	channelStatus;
    uint32_t	userDataDeliveryQ;
    uint32_t	userDataReleaseQ;
    uint32_t	transportStreamCaptureAddr;
    uint32_t	asyncEventQ;

} DecRspChannelStartVideo;

#define eCMD_C011_CMD_BASE	  (0x73763000)

/* host commands */
typedef enum {
    eCMD_TS_GET_NEXT_PIC	= 0x7376F100, /* debug get next picture */
    eCMD_TS_GET_LAST_PIC	= 0x7376F102, /* debug get last pic status */
    eCMD_TS_READ_WRITE_MEM	= 0x7376F104, /* debug read write memory */

    /* New API commands */
    /* General commands */
    eCMD_C011_INIT		= eCMD_C011_CMD_BASE + 0x01,
    eCMD_C011_RESET		= eCMD_C011_CMD_BASE + 0x02,
    eCMD_C011_SELF_TEST		= eCMD_C011_CMD_BASE + 0x03,
    eCMD_C011_GET_VERSION	= eCMD_C011_CMD_BASE + 0x04,
    eCMD_C011_GPIO		= eCMD_C011_CMD_BASE + 0x05,
    eCMD_C011_DEBUG_SETUP	= eCMD_C011_CMD_BASE + 0x06,

    /* Decoding commands */
    eCMD_C011_DEC_CHAN_OPEN			= eCMD_C011_CMD_BASE + 0x100,
    eCMD_C011_DEC_CHAN_CLOSE			= eCMD_C011_CMD_BASE + 0x101,
    eCMD_C011_DEC_CHAN_ACTIVATE			= eCMD_C011_CMD_BASE + 0x102,
    eCMD_C011_DEC_CHAN_STATUS			= eCMD_C011_CMD_BASE + 0x103,
    eCMD_C011_DEC_CHAN_FLUSH			= eCMD_C011_CMD_BASE + 0x104,
    eCMD_C011_DEC_CHAN_TRICK_PLAY		= eCMD_C011_CMD_BASE + 0x105,
    eCMD_C011_DEC_CHAN_TS_PIDS			= eCMD_C011_CMD_BASE + 0x106,
    eCMD_C011_DEC_CHAN_PS_STREAM_ID		= eCMD_C011_CMD_BASE + 0x107,
    eCMD_C011_DEC_CHAN_INPUT_PARAMS		= eCMD_C011_CMD_BASE + 0x108,
    eCMD_C011_DEC_CHAN_VIDEO_OUTPUT		= eCMD_C011_CMD_BASE + 0x109,
    eCMD_C011_DEC_CHAN_OUTPUT_FORMAT		= eCMD_C011_CMD_BASE + 0x10A,
    eCMD_C011_DEC_CHAN_SCALING_FILTERS		= eCMD_C011_CMD_BASE + 0x10B,
    eCMD_C011_DEC_CHAN_OSD_MODE			= eCMD_C011_CMD_BASE + 0x10D,
    eCMD_C011_DEC_CHAN_DROP			= eCMD_C011_CMD_BASE + 0x10E,
    eCMD_C011_DEC_CHAN_RELEASE			= eCMD_C011_CMD_BASE + 0x10F,
    eCMD_C011_DEC_CHAN_STREAM_SETTINGS		= eCMD_C011_CMD_BASE + 0x110,
    eCMD_C011_DEC_CHAN_PAUSE_OUTPUT		= eCMD_C011_CMD_BASE + 0x111,
    eCMD_C011_DEC_CHAN_CHANGE			= eCMD_C011_CMD_BASE + 0x112,
    eCMD_C011_DEC_CHAN_SET_STC			= eCMD_C011_CMD_BASE + 0x113,
    eCMD_C011_DEC_CHAN_SET_PTS			= eCMD_C011_CMD_BASE + 0x114,
    eCMD_C011_DEC_CHAN_CC_MODE			= eCMD_C011_CMD_BASE + 0x115,
    eCMD_C011_DEC_CREATE_AUDIO_CONTEXT		= eCMD_C011_CMD_BASE + 0x116,
    eCMD_C011_DEC_COPY_AUDIO_CONTEXT		= eCMD_C011_CMD_BASE + 0x117,
    eCMD_C011_DEC_DELETE_AUDIO_CONTEXT		= eCMD_C011_CMD_BASE + 0x118,
    eCMD_C011_DEC_CHAN_SET_DECYPTION		= eCMD_C011_CMD_BASE + 0x119,
    eCMD_C011_DEC_CHAN_START_VIDEO		= eCMD_C011_CMD_BASE + 0x11A,
    eCMD_C011_DEC_CHAN_STOP_VIDEO		= eCMD_C011_CMD_BASE + 0x11B,
    eCMD_C011_DEC_CHAN_PIC_CAPTURE		= eCMD_C011_CMD_BASE + 0x11C,
    eCMD_C011_DEC_CHAN_PAUSE			= eCMD_C011_CMD_BASE + 0x11D,
    eCMD_C011_DEC_CHAN_PAUSE_STATE		= eCMD_C011_CMD_BASE + 0x11E,
    eCMD_C011_DEC_CHAN_SET_SLOWM_RATE		= eCMD_C011_CMD_BASE + 0x11F,
    eCMD_C011_DEC_CHAN_GET_SLOWM_RATE		= eCMD_C011_CMD_BASE + 0x120,
    eCMD_C011_DEC_CHAN_SET_FF_RATE		= eCMD_C011_CMD_BASE + 0x121,
    eCMD_C011_DEC_CHAN_GET_FF_RATE		= eCMD_C011_CMD_BASE + 0x122,
    eCMD_C011_DEC_CHAN_FRAME_ADVANCE		= eCMD_C011_CMD_BASE + 0x123,
    eCMD_C011_DEC_CHAN_SET_SKIP_PIC_MODE	= eCMD_C011_CMD_BASE + 0x124,
    eCMD_C011_DEC_CHAN_GET_SKIP_PIC_MODE	= eCMD_C011_CMD_BASE + 0x125,
    eCMD_C011_DEC_CHAN_FILL_PIC_BUF		= eCMD_C011_CMD_BASE + 0x126,
    eCMD_C011_DEC_CHAN_SET_CONTINUITY_CHECK	= eCMD_C011_CMD_BASE + 0x127,
    eCMD_C011_DEC_CHAN_GET_CONTINUITY_CHECK	= eCMD_C011_CMD_BASE + 0x128,
    eCMD_C011_DEC_CHAN_SET_BRCM_TRICK_MODE	= eCMD_C011_CMD_BASE + 0x129,
    eCMD_C011_DEC_CHAN_GET_BRCM_TRICK_MODE	= eCMD_C011_CMD_BASE + 0x12A,
    eCMD_C011_DEC_CHAN_REVERSE_FIELD_STATUS	= eCMD_C011_CMD_BASE + 0x12B,
    eCMD_C011_DEC_CHAN_I_PICTURE_FOUND		= eCMD_C011_CMD_BASE + 0x12C,
    eCMD_C011_DEC_CHAN_SET_PARAMETER		= eCMD_C011_CMD_BASE + 0x12D,
    eCMD_C011_DEC_CHAN_SET_USER_DATA_MODE	= eCMD_C011_CMD_BASE + 0x12E,
    eCMD_C011_DEC_CHAN_SET_PAUSE_DISPLAY_MODE	= eCMD_C011_CMD_BASE + 0x12F,
    eCMD_C011_DEC_CHAN_SET_SLOW_DISPLAY_MODE	= eCMD_C011_CMD_BASE + 0x130,
    eCMD_C011_DEC_CHAN_SET_FF_DISPLAY_MODE	= eCMD_C011_CMD_BASE + 0x131,
    eCMD_C011_DEC_CHAN_SET_DISPLAY_TIMING_MODE	= eCMD_C011_CMD_BASE + 0x132,
    eCMD_C011_DEC_CHAN_SET_DISPLAY_MODE		= eCMD_C011_CMD_BASE + 0x133,
    eCMD_C011_DEC_CHAN_GET_DISPLAY_MODE		= eCMD_C011_CMD_BASE + 0x134,
    eCMD_C011_DEC_CHAN_SET_REVERSE_FIELD	= eCMD_C011_CMD_BASE + 0x135,
    eCMD_C011_DEC_CHAN_STREAM_OPEN		= eCMD_C011_CMD_BASE + 0x136,
    eCMD_C011_DEC_CHAN_SET_PCR_PID		= eCMD_C011_CMD_BASE + 0x137,
    eCMD_C011_DEC_CHAN_SET_VID_PID		= eCMD_C011_CMD_BASE + 0x138,
    eCMD_C011_DEC_CHAN_SET_PAN_SCAN_MODE	= eCMD_C011_CMD_BASE + 0x139,
    eCMD_C011_DEC_CHAN_START_DISPLAY_AT_PTS	= eCMD_C011_CMD_BASE + 0x140,
    eCMD_C011_DEC_CHAN_STOP_DISPLAY_AT_PTS	= eCMD_C011_CMD_BASE + 0x141,
    eCMD_C011_DEC_CHAN_SET_DISPLAY_ORDER	= eCMD_C011_CMD_BASE + 0x142,
    eCMD_C011_DEC_CHAN_GET_DISPLAY_ORDER	= eCMD_C011_CMD_BASE + 0x143,
    eCMD_C011_DEC_CHAN_SET_HOST_TRICK_MODE	= eCMD_C011_CMD_BASE + 0x144,
    eCMD_C011_DEC_CHAN_SET_OPERATION_MODE	= eCMD_C011_CMD_BASE + 0x145,
    eCMD_C011_DEC_CHAN_DISPLAY_PAUSE_UNTO_PTS	= eCMD_C011_CMD_BASE + 0x146,
    eCMD_C011_DEC_CHAN_SET_PTS_STC_DIFF_THRESHOLD = eCMD_C011_CMD_BASE + 0x147,
    eCMD_C011_DEC_CHAN_SEND_COMPRESSED_BUF	= eCMD_C011_CMD_BASE + 0x148,
    eCMD_C011_DEC_CHAN_SET_CLIPPING		= eCMD_C011_CMD_BASE + 0x149,
    eCMD_C011_DEC_CHAN_SET_PARAMETERS_FOR_HARD_RESET_INTERRUPT_TO_HOST
						= eCMD_C011_CMD_BASE + 0x150,

    /* Decoder RevD commands */
    eCMD_C011_DEC_CHAN_SET_CSC	= eCMD_C011_CMD_BASE + 0x180, /* color space conversion */
    eCMD_C011_DEC_CHAN_SET_RANGE_REMAP	= eCMD_C011_CMD_BASE + 0x181,
    eCMD_C011_DEC_CHAN_SET_FGT		= eCMD_C011_CMD_BASE + 0x182,
    /* Note: 0x183 not implemented yet in Rev D main */
    eCMD_C011_DEC_CHAN_SET_LASTPICTURE_PADDING = eCMD_C011_CMD_BASE + 0x183,

    /* Decoder 7412 commands (7412-only) */
    eCMD_C011_DEC_CHAN_SET_CONTENT_KEY	= eCMD_C011_CMD_BASE + 0x190,
    eCMD_C011_DEC_CHAN_SET_SESSION_KEY	= eCMD_C011_CMD_BASE + 0x191,
    eCMD_C011_DEC_CHAN_FMT_CHANGE_ACK	= eCMD_C011_CMD_BASE + 0x192,

    eCMD_C011_DEC_CHAN_CUSTOM_VIDOUT    = eCMD_C011_CMD_BASE + 0x1FF,

    /* Encoding commands */
    eCMD_C011_ENC_CHAN_OPEN		= eCMD_C011_CMD_BASE + 0x200,
    eCMD_C011_ENC_CHAN_CLOSE		= eCMD_C011_CMD_BASE + 0x201,
    eCMD_C011_ENC_CHAN_ACTIVATE		= eCMD_C011_CMD_BASE + 0x202,
    eCMD_C011_ENC_CHAN_CONTROL		= eCMD_C011_CMD_BASE + 0x203,
    eCMD_C011_ENC_CHAN_STATISTICS	= eCMD_C011_CMD_BASE + 0x204,

    eNOTIFY_C011_ENC_CHAN_EVENT		= eCMD_C011_CMD_BASE + 0x210,

} eC011_TS_CMD;

#endif
