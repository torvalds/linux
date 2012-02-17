/********************************************************************
 * Copyright(c) 2006-2009 Broadcom Corporation.
 *
 *  Name: bc_dts_defs.h
 *
 *  Description: Common definitions for all components. Only types
 *		 is allowed to be included from this file.
 *
 *  AU
 *
 *  HISTORY:
 *
 ********************************************************************
 * This header is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 2.1 of the License.
 *
 * This header is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * You should have received a copy of the GNU Lesser General Public License
 * along with this header.  If not, see <http://www.gnu.org/licenses/>.
 *******************************************************************/

#ifndef _BC_DTS_DEFS_H_
#define _BC_DTS_DEFS_H_

/* BIT Mask */
#define BC_BIT(_x)		(1 << (_x))

enum BC_STATUS {
	BC_STS_SUCCESS		= 0,
	BC_STS_INV_ARG		= 1,
	BC_STS_BUSY		= 2,
	BC_STS_NOT_IMPL		= 3,
	BC_STS_PGM_QUIT		= 4,
	BC_STS_NO_ACCESS	= 5,
	BC_STS_INSUFF_RES	= 6,
	BC_STS_IO_ERROR		= 7,
	BC_STS_NO_DATA		= 8,
	BC_STS_VER_MISMATCH	= 9,
	BC_STS_TIMEOUT		= 10,
	BC_STS_FW_CMD_ERR	= 11,
	BC_STS_DEC_NOT_OPEN	= 12,
	BC_STS_ERR_USAGE	= 13,
	BC_STS_IO_USER_ABORT	= 14,
	BC_STS_IO_XFR_ERROR	= 15,
	BC_STS_DEC_NOT_STARTED	= 16,
	BC_STS_FWHEX_NOT_FOUND	= 17,
	BC_STS_FMT_CHANGE	= 18,
	BC_STS_HIF_ACCESS	= 19,
	BC_STS_CMD_CANCELLED	= 20,
	BC_STS_FW_AUTH_FAILED	= 21,
	BC_STS_BOOTLOADER_FAILED = 22,
	BC_STS_CERT_VERIFY_ERROR = 23,
	BC_STS_DEC_EXIST_OPEN	= 24,
	BC_STS_PENDING		= 25,
	BC_STS_CLK_NOCHG	= 26,

	/* Must be the last one.*/
	BC_STS_ERROR		= -1
};

/*------------------------------------------------------*
 *    Registry Key Definitions				*
 *------------------------------------------------------*/
#define BC_REG_KEY_MAIN_PATH	"Software\\Broadcom\\MediaPC\\70010"
#define BC_REG_KEY_FWPATH		"FirmwareFilePath"
#define BC_REG_KEY_SEC_OPT		"DbgOptions"

/*
 * Options:
 *
 *  b[5] = Enable RSA KEY in EEPROM Support
 *  b[6] = Enable Old PIB scheme. (0 = Use PIB with video scheme)
 *
 *  b[12] = Enable send message to NotifyIcon
 *
 */

enum BC_SW_OPTIONS {
	BC_OPT_DOSER_OUT_ENCRYPT	= BC_BIT(3),
	BC_OPT_LINK_OUT_ENCRYPT		= BC_BIT(29),
};

struct BC_REG_CONFIG {
	uint32_t		DbgOptions;
};

#if defined(__KERNEL__) || defined(__LINUX_USER__)
#else
/* Align data structures */
#define ALIGN(x)	__declspec(align(x))
#endif

/* mode
 * b[0]..b[7]	= _DtsDeviceOpenMode
 * b[8]		=  Load new FW
 * b[9]		=  Load file play back FW
 * b[10]	=  Disk format (0 for HD DVD and 1 for BLU ray)
 * b[11]-b[15]	=  default output resolution
 * b[16]	=  Skip TX CPB Buffer Check
 * b[17]	=  Adaptive Output Encrypt/Scramble Scheme
 * b[18]-b[31]	=  reserved for future use
 */

/* To allow multiple apps to open the device. */
enum DtsDeviceOpenMode {
	DTS_PLAYBACK_MODE = 0,
	DTS_DIAG_MODE,
	DTS_MONITOR_MODE,
	DTS_HWINIT_MODE
};

/* To enable the filter to selectively enable/disable fixes or erratas */
enum DtsDeviceFixMode {
	DTS_LOAD_NEW_FW		= BC_BIT(8),
	DTS_LOAD_FILE_PLAY_FW	= BC_BIT(9),
	DTS_DISK_FMT_BD		= BC_BIT(10),
	/* b[11]-b[15] : Default output resolution */
	DTS_SKIP_TX_CHK_CPB	= BC_BIT(16),
	DTS_ADAPTIVE_OUTPUT_PER	= BC_BIT(17),
	DTS_INTELLIMAP		= BC_BIT(18),
	/* b[19]-b[21] : select clock frequency */
	DTS_PLAYBACK_DROP_RPT_MODE = BC_BIT(22)
};

#define DTS_DFLT_RESOLUTION(x)	(x<<11)

#define DTS_DFLT_CLOCK(x) (x<<19)

/* F/W File Version corresponding to S/W Releases */
enum FW_FILE_VER {
	/* S/W release: 02.04.02	F/W release 2.12.2.0 */
	BC_FW_VER_020402 = ((12<<16) | (2<<8) | (0))
};

/*------------------------------------------------------*
 *    Stream Types for DtsOpenDecoder()			*
 *------------------------------------------------------*/
enum DtsOpenDecStreamTypes {
	BC_STREAM_TYPE_ES		= 0,
	BC_STREAM_TYPE_PES		= 1,
	BC_STREAM_TYPE_TS		= 2,
	BC_STREAM_TYPE_ES_TSTAMP	= 6,
};

/*------------------------------------------------------*
 *    Video Algorithms for DtsSetVideoParams()		*
 *------------------------------------------------------*/
enum DtsSetVideoParamsAlgo {
	BC_VID_ALGO_H264		= 0,
	BC_VID_ALGO_MPEG2		= 1,
	BC_VID_ALGO_VC1			= 4,
	BC_VID_ALGO_VC1MP		= 7,
};

/*------------------------------------------------------*
 *    MPEG Extension to the PPB				*
 *------------------------------------------------------*/
#define BC_MPEG_VALID_PANSCAN		(1)

struct BC_PIB_EXT_MPEG {
	uint32_t	valid;
	/* Always valid,  defaults to picture size if no
	 * sequence display extension in the stream. */
	uint32_t	display_horizontal_size;
	uint32_t	display_vertical_size;

	/* MPEG_VALID_PANSCAN
	 * Offsets are a copy values from the MPEG stream. */
	uint32_t	offset_count;
	int32_t		horizontal_offset[3];
	int32_t		vertical_offset[3];
};

/*------------------------------------------------------*
 *    H.264 Extension to the PPB			*
 *------------------------------------------------------*/
/* Bit definitions for 'other.h264.valid' field */
#define H264_VALID_PANSCAN		(1)
#define H264_VALID_SPS_CROP		(2)
#define H264_VALID_VUI			(4)

struct BC_PIB_EXT_H264 {
	/* 'valid' specifies which fields (or sets of
	 * fields) below are valid.  If the corresponding
	 * bit in 'valid' is NOT set then that field(s)
	 * is (are) not initialized. */
	uint32_t	valid;

	/* H264_VALID_PANSCAN */
	uint32_t	pan_scan_count;
	int32_t		pan_scan_left[3];
	int32_t		pan_scan_right[3];
	int32_t		pan_scan_top[3];
	int32_t		pan_scan_bottom[3];

	/* H264_VALID_SPS_CROP */
	int32_t		sps_crop_left;
	int32_t		sps_crop_right;
	int32_t		sps_crop_top;
	int32_t		sps_crop_bottom;

	/* H264_VALID_VUI */
	uint32_t	chroma_top;
	uint32_t	chroma_bottom;
};

/*------------------------------------------------------*
 *    VC1 Extension to the PPB				*
 *------------------------------------------------------*/
#define VC1_VALID_PANSCAN		(1)

struct BC_PIB_EXT_VC1 {
	uint32_t	valid;

	/* Always valid, defaults to picture size if no
	 * sequence display extension in the stream. */
	uint32_t	display_horizontal_size;
	uint32_t	display_vertical_size;

	/* VC1 pan scan windows */
	uint32_t	num_panscan_windows;
	int32_t		ps_horiz_offset[4];
	int32_t		ps_vert_offset[4];
	int32_t		ps_width[4];
	int32_t		ps_height[4];
};

/*------------------------------------------------------*
 *    Picture Information Block				*
 *------------------------------------------------------*/
#if defined(__LINUX_USER__)
/* Values for 'pulldown' field.  '0' means no pulldown information
 * was present for this picture. */
enum {
	vdecNoPulldownInfo	= 0,
	vdecTop			= 1,
	vdecBottom		= 2,
	vdecTopBottom		= 3,
	vdecBottomTop		= 4,
	vdecTopBottomTop	= 5,
	vdecBottomTopBottom	= 6,
	vdecFrame_X2		= 7,
	vdecFrame_X3		= 8,
	vdecFrame_X1		= 9,
	vdecFrame_X4		= 10,
};

/* Values for the 'frame_rate' field. */
enum {
	vdecFrameRateUnknown = 0,
	vdecFrameRate23_97,
	vdecFrameRate24,
	vdecFrameRate25,
	vdecFrameRate29_97,
	vdecFrameRate30,
	vdecFrameRate50,
	vdecFrameRate59_94,
	vdecFrameRate60,
};

/* Values for the 'aspect_ratio' field. */
enum {
	vdecAspectRatioUnknown = 0,
	vdecAspectRatioSquare,
	vdecAspectRatio12_11,
	vdecAspectRatio10_11,
	vdecAspectRatio16_11,
	vdecAspectRatio40_33,
	vdecAspectRatio24_11,
	vdecAspectRatio20_11,
	vdecAspectRatio32_11,
	vdecAspectRatio80_33,
	vdecAspectRatio18_11,
	vdecAspectRatio15_11,
	vdecAspectRatio64_33,
	vdecAspectRatio160_99,
	vdecAspectRatio4_3,
	vdecAspectRatio16_9,
	vdecAspectRatio221_1,
	vdecAspectRatioOther = 255,
};

/* Values for the 'colour_primaries' field. */
enum {
	vdecColourPrimariesUnknown = 0,
	vdecColourPrimariesBT709,
	vdecColourPrimariesUnspecified,
	vdecColourPrimariesReserved,
	vdecColourPrimariesBT470_2M = 4,
	vdecColourPrimariesBT470_2BG,
	vdecColourPrimariesSMPTE170M,
	vdecColourPrimariesSMPTE240M,
	vdecColourPrimariesGenericFilm,
};
/**
 * @vdecRESOLUTION_CUSTOM: custom
 * @vdecRESOLUTION_480i: 480i
 * @vdecRESOLUTION_1080i: 1080i (1920x1080, 60i)
 * @vdecRESOLUTION_NTSC: NTSC (720x483, 60i)
 * @vdecRESOLUTION_480p: 480p (720x480, 60p)
 * @vdecRESOLUTION_720p: 720p (1280x720, 60p)
 * @vdecRESOLUTION_PAL1: PAL_1 (720x576, 50i)
 * @vdecRESOLUTION_1080i25: 1080i25 (1920x1080, 50i)
 * @vdecRESOLUTION_720p50: 720p50 (1280x720, 50p)
 * @vdecRESOLUTION_576p: 576p (720x576, 50p)
 * @vdecRESOLUTION_1080i29_97: 1080i (1920x1080, 59.94i)
 * @vdecRESOLUTION_720p59_94: 720p (1280x720, 59.94p)
 * @vdecRESOLUTION_SD_DVD: SD DVD (720x483, 60i)
 * @vdecRESOLUTION_480p656: 480p (720x480, 60p),
 *	output bus width 8 bit, clock 74.25MHz
 * @vdecRESOLUTION_1080p23_976: 1080p23_976 (1920x1080, 23.976p)
 * @vdecRESOLUTION_720p23_976: 720p23_976 (1280x720p, 23.976p)
 * @vdecRESOLUTION_240p29_97: 240p (1440x240, 29.97p )
 * @vdecRESOLUTION_240p30: 240p (1440x240, 30p)
 * @vdecRESOLUTION_288p25: 288p (1440x288p, 25p)
 * @vdecRESOLUTION_1080p29_97: 1080p29_97 (1920x1080, 29.97p)
 * @vdecRESOLUTION_1080p30: 1080p30 (1920x1080, 30p)
 * @vdecRESOLUTION_1080p24: 1080p24 (1920x1080, 24p)
 * @vdecRESOLUTION_1080p25: 1080p25 (1920x1080, 25p)
 * @vdecRESOLUTION_720p24: 720p24 (1280x720, 25p)
 * @vdecRESOLUTION_720p29_97: 720p29.97 (1280x720, 29.97p)
 * @vdecRESOLUTION_480p23_976: 480p23.976 (720*480, 23.976)
 * @vdecRESOLUTION_480p29_97: 480p29.976 (720*480, 29.97p)
 * @vdecRESOLUTION_576p25: 576p25 (720*576, 25p)
 * @vdecRESOLUTION_480p0: 480p (720x480, 0p)
 * @vdecRESOLUTION_480i0: 480i (720x480, 0i)
 * @vdecRESOLUTION_576p0: 576p (720x576, 0p)
 * @vdecRESOLUTION_720p0: 720p (1280x720, 0p)
 * @vdecRESOLUTION_1080p0: 1080p (1920x1080, 0p)
 * @vdecRESOLUTION_1080i0: 1080i (1920x1080, 0i)
 */
enum {
	vdecRESOLUTION_CUSTOM	= 0x00000000,
	vdecRESOLUTION_480i	= 0x00000001,
	vdecRESOLUTION_1080i	= 0x00000002,
	vdecRESOLUTION_NTSC	= 0x00000003,
	vdecRESOLUTION_480p	= 0x00000004,
	vdecRESOLUTION_720p	= 0x00000005,
	vdecRESOLUTION_PAL1	= 0x00000006,
	vdecRESOLUTION_1080i25	= 0x00000007,
	vdecRESOLUTION_720p50	= 0x00000008,
	vdecRESOLUTION_576p	= 0x00000009,
	vdecRESOLUTION_1080i29_97 = 0x0000000A,
	vdecRESOLUTION_720p59_94  = 0x0000000B,
	vdecRESOLUTION_SD_DVD	= 0x0000000C,
	vdecRESOLUTION_480p656	= 0x0000000D,
	vdecRESOLUTION_1080p23_976 = 0x0000000E,
	vdecRESOLUTION_720p23_976  = 0x0000000F,
	vdecRESOLUTION_240p29_97   = 0x00000010,
	vdecRESOLUTION_240p30	= 0x00000011,
	vdecRESOLUTION_288p25	= 0x00000012,
	vdecRESOLUTION_1080p29_97 = 0x00000013,
	vdecRESOLUTION_1080p30	= 0x00000014,
	vdecRESOLUTION_1080p24	= 0x00000015,
	vdecRESOLUTION_1080p25	= 0x00000016,
	vdecRESOLUTION_720p24	= 0x00000017,
	vdecRESOLUTION_720p29_97  = 0x00000018,
	vdecRESOLUTION_480p23_976 = 0x00000019,
	vdecRESOLUTION_480p29_97  = 0x0000001A,
	vdecRESOLUTION_576p25	= 0x0000001B,
	/* For Zero Frame Rate */
	vdecRESOLUTION_480p0	= 0x0000001C,
	vdecRESOLUTION_480i0	= 0x0000001D,
	vdecRESOLUTION_576p0	= 0x0000001E,
	vdecRESOLUTION_720p0	= 0x0000001F,
	vdecRESOLUTION_1080p0	= 0x00000020,
	vdecRESOLUTION_1080i0	= 0x00000021,
};

/* Bit definitions for 'flags' field */
#define VDEC_FLAG_EOS				(0x0004)

#define VDEC_FLAG_FRAME				(0x0000)
#define VDEC_FLAG_FIELDPAIR			(0x0008)
#define VDEC_FLAG_TOPFIELD			(0x0010)
#define VDEC_FLAG_BOTTOMFIELD			(0x0018)

#define VDEC_FLAG_PROGRESSIVE_SRC		(0x0000)
#define VDEC_FLAG_INTERLACED_SRC		(0x0020)
#define VDEC_FLAG_UNKNOWN_SRC			(0x0040)

#define VDEC_FLAG_BOTTOM_FIRST			(0x0080)
#define VDEC_FLAG_LAST_PICTURE			(0x0100)

#define VDEC_FLAG_PICTURE_META_DATA_PRESENT	(0x40000)

#endif /* __LINUX_USER__ */

enum _BC_OUTPUT_FORMAT {
	MODE420				= 0x0,
	MODE422_YUY2			= 0x1,
	MODE422_UYVY			= 0x2,
};
/**
 * struct BC_PIC_INFO_BLOCK
 * @timeStam;: Timestamp
 * @picture_number: Ordinal display number
 * @width:  pixels
 * @height:  pixels
 * @chroma_format:  0x420, 0x422 or 0x444
 * @n_drop;:  number of non-reference frames
 *	remaining to be dropped
 */
struct BC_PIC_INFO_BLOCK {
	/* Common fields. */
	uint64_t	timeStamp;
	uint32_t	picture_number;
	uint32_t	width;
	uint32_t	height;
	uint32_t	chroma_format;
	uint32_t	pulldown;
	uint32_t	flags;
	uint32_t	frame_rate;
	uint32_t	aspect_ratio;
	uint32_t	colour_primaries;
	uint32_t	picture_meta_payload;
	uint32_t	sess_num;
	uint32_t	ycom;
	uint32_t	custom_aspect_ratio_width_height;
	uint32_t	n_drop;	/* number of non-reference frames
					remaining to be dropped */

	/* Protocol-specific extensions. */
	union {
		struct BC_PIB_EXT_H264	h264;
		struct BC_PIB_EXT_MPEG	mpeg;
		struct BC_PIB_EXT_VC1	 vc1;
	} other;

};

/*------------------------------------------------------*
 *    ProcOut Info					*
 *------------------------------------------------------*/

/**
 * enum POUT_OPTIONAL_IN_FLAGS - Optional flags for ProcOut Interface.
 * @BC_POUT_FLAGS_YV12:  Copy Data in YV12 format
 * @BC_POUT_FLAGS_STRIDE:  Stride size is valid.
 * @BC_POUT_FLAGS_SIZE:  Take size information from Application
 * @BC_POUT_FLAGS_INTERLACED:  copy only half the bytes
 * @BC_POUT_FLAGS_INTERLEAVED:  interleaved frame
 * @:  * @BC_POUT_FLAGS_FMT_CHANGE:  Data is not VALID when this flag is set
 * @BC_POUT_FLAGS_PIB_VALID:  PIB Information valid
 * @BC_POUT_FLAGS_ENCRYPTED:  Data is encrypted.
 * @BC_POUT_FLAGS_FLD_BOT:  Bottom Field data
 */
enum POUT_OPTIONAL_IN_FLAGS_ {
	/* Flags from App to Device */
	BC_POUT_FLAGS_YV12	  = 0x01,
	BC_POUT_FLAGS_STRIDE	  = 0x02,
	BC_POUT_FLAGS_SIZE	  = 0x04,
	BC_POUT_FLAGS_INTERLACED  = 0x08,
	BC_POUT_FLAGS_INTERLEAVED = 0x10,

	/* Flags from Device to APP */
	BC_POUT_FLAGS_FMT_CHANGE  = 0x10000,
	BC_POUT_FLAGS_PIB_VALID	  = 0x20000,
	BC_POUT_FLAGS_ENCRYPTED	  = 0x40000,
	BC_POUT_FLAGS_FLD_BOT	  = 0x80000,
};

typedef enum BC_STATUS(*dts_pout_callback)(void  *shnd, uint32_t width,
			uint32_t height, uint32_t stride, void *pOut);

/* Line 21 Closed Caption */
/* User Data */
#define MAX_UD_SIZE		1792	/* 1920 - 128 */

/**
 * struct BC_DTS_PROC_OUT
 * @Ybuff: Caller Supplied buffer for Y data
 * @YbuffSz: Caller Supplied Y buffer size
 * @YBuffDoneSz: Transferred Y datasize
 * @*UVbuff: Caller Supplied buffer for UV data
 * @UVbuffSz: Caller Supplied UV buffer size
 * @UVBuffDoneSz: Transferred UV data size
 * @StrideSz: Caller supplied Stride Size
 * @PoutFlags: Call IN Flags
 * @discCnt: Picture discontinuity count
 * @PicInfo: Picture Information Block Data
 * @b422Mode: Picture output Mode
 * @bPibEnc: PIB encrypted
 */
struct BC_DTS_PROC_OUT {
	uint8_t		*Ybuff;
	uint32_t	YbuffSz;
	uint32_t	YBuffDoneSz;

	uint8_t		*UVbuff;
	uint32_t	UVbuffSz;
	uint32_t	UVBuffDoneSz;

	uint32_t	StrideSz;
	uint32_t	PoutFlags;

	uint32_t	discCnt;

	struct BC_PIC_INFO_BLOCK PicInfo;

	/* Line 21 Closed Caption */
	/* User Data */
	uint32_t	UserDataSz;
	uint8_t		UserData[MAX_UD_SIZE];

	void		*hnd;
	dts_pout_callback AppCallBack;
	uint8_t		DropFrames;
	uint8_t		b422Mode;
	uint8_t		bPibEnc;
	uint8_t		bRevertScramble;

};
/**
 * struct BC_DTS_STATUS
 * @ReadyListCount: Number of frames in ready list (reported by driver)
 * @PowerStateChange: Number of active state power
 *	transitions (reported by driver)
 * @FramesDropped:  Number of frames dropped.  (reported by DIL)
 * @FramesCaptured: Number of frames captured. (reported by DIL)
 * @FramesRepeated: Number of frames repeated. (reported by DIL)
 * @InputCount:	Times compressed video has been sent to the HW.
 *	i.e. Successful DtsProcInput() calls (reported by DIL)
 * @InputTotalSize: Amount of compressed video that has been sent to the HW.
 *	(reported by DIL)
 * @InputBusyCount: Times compressed video has attempted to be sent to the HW
 *	but the input FIFO was full. (reported by DIL)
 * @PIBMissCount: Amount of times a PIB is invalid. (reported by DIL)
 * @cpbEmptySize: supported only for H.264, specifically changed for
 *	Adobe. Report size of CPB buffer available. (reported by DIL)
 * @NextTimeStamp: TimeStamp of the next picture that will be returned
 *	by a call to ProcOutput. Added for Adobe. Reported
 *	back from the driver
 */
struct BC_DTS_STATUS {
	uint8_t		ReadyListCount;
	uint8_t		FreeListCount;
	uint8_t		PowerStateChange;
	uint8_t		reserved_[1];
	uint32_t	FramesDropped;
	uint32_t	FramesCaptured;
	uint32_t	FramesRepeated;
	uint32_t	InputCount;
	uint64_t	InputTotalSize;
	uint32_t	InputBusyCount;
	uint32_t	PIBMissCount;
	uint32_t	cpbEmptySize;
	uint64_t	NextTimeStamp;
	uint8_t		reserved__[16];
};

#define BC_SWAP32(_v)			\
	((((_v) & 0xFF000000)>>24)|	\
	  (((_v) & 0x00FF0000)>>8)|	\
	  (((_v) & 0x0000FF00)<<8)|	\
	  (((_v) & 0x000000FF)<<24))

#define WM_AGENT_TRAYICON_DECODER_OPEN	10001
#define WM_AGENT_TRAYICON_DECODER_CLOSE	10002
#define WM_AGENT_TRAYICON_DECODER_START	10003
#define WM_AGENT_TRAYICON_DECODER_STOP	10004
#define WM_AGENT_TRAYICON_DECODER_RUN	10005
#define WM_AGENT_TRAYICON_DECODER_PAUSE	10006


#endif	/* _BC_DTS_DEFS_H_ */
