/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom BM2835 V4L2 driver
 *
 * Copyright © 2013 Raspberry Pi (Trading) Ltd.
 *
 * Authors: Vincent Sanders @ Collabora
 *          Dave Stevenson @ Broadcom
 *		(now dave.stevenson@raspberrypi.org)
 *          Simon Mellor @ Broadcom
 *          Luke Diamand @ Broadcom
 */

/* common parameters */

/** @name Parameter groups
 * Parameters are divided into groups, and then allocated sequentially within
 * a group using an enum.
 * @{
 */

#ifndef MMAL_PARAMETERS_H
#define MMAL_PARAMETERS_H

/** Common parameter ID group, used with many types of component. */
#define MMAL_PARAMETER_GROUP_COMMON            (0 << 16)
/** Camera-specific parameter ID group. */
#define MMAL_PARAMETER_GROUP_CAMERA            (1 << 16)
/** Video-specific parameter ID group. */
#define MMAL_PARAMETER_GROUP_VIDEO             (2 << 16)
/** Audio-specific parameter ID group. */
#define MMAL_PARAMETER_GROUP_AUDIO             (3 << 16)
/** Clock-specific parameter ID group. */
#define MMAL_PARAMETER_GROUP_CLOCK             (4 << 16)
/** Miracast-specific parameter ID group. */
#define MMAL_PARAMETER_GROUP_MIRACAST       (5 << 16)

/* Common parameters */
enum mmal_parameter_common_type {
		/**< Never a valid parameter ID */
	MMAL_PARAMETER_UNUSED = MMAL_PARAMETER_GROUP_COMMON,

		/**< MMAL_PARAMETER_ENCODING_T */
	MMAL_PARAMETER_SUPPORTED_ENCODINGS,
		/**< MMAL_PARAMETER_URI_T */
	MMAL_PARAMETER_URI,
		/** MMAL_PARAMETER_CHANGE_EVENT_REQUEST_T */
	MMAL_PARAMETER_CHANGE_EVENT_REQUEST,
		/** MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_ZERO_COPY,
		/**< MMAL_PARAMETER_BUFFER_REQUIREMENTS_T */
	MMAL_PARAMETER_BUFFER_REQUIREMENTS,
		/**< MMAL_PARAMETER_STATISTICS_T */
	MMAL_PARAMETER_STATISTICS,
		/**< MMAL_PARAMETER_CORE_STATISTICS_T */
	MMAL_PARAMETER_CORE_STATISTICS,
		/**< MMAL_PARAMETER_MEM_USAGE_T */
	MMAL_PARAMETER_MEM_USAGE,
		/**< MMAL_PARAMETER_UINT32_T */
	MMAL_PARAMETER_BUFFER_FLAG_FILTER,
		/**< MMAL_PARAMETER_SEEK_T */
	MMAL_PARAMETER_SEEK,
		/**< MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_POWERMON_ENABLE,
		/**< MMAL_PARAMETER_LOGGING_T */
	MMAL_PARAMETER_LOGGING,
		/**< MMAL_PARAMETER_UINT64_T */
	MMAL_PARAMETER_SYSTEM_TIME,
		/**< MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_NO_IMAGE_PADDING,
};

/* camera parameters */

enum mmal_parameter_camera_type {
	/* 0 */
		/** @ref MMAL_PARAMETER_THUMBNAIL_CONFIG_T */
	MMAL_PARAMETER_THUMBNAIL_CONFIGURATION =
		MMAL_PARAMETER_GROUP_CAMERA,
		/**< Unused? */
	MMAL_PARAMETER_CAPTURE_QUALITY,
		/**< @ref MMAL_PARAMETER_INT32_T */
	MMAL_PARAMETER_ROTATION,
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_EXIF_DISABLE,
		/**< @ref MMAL_PARAMETER_EXIF_T */
	MMAL_PARAMETER_EXIF,
		/**< @ref MMAL_PARAM_AWBMODE_T */
	MMAL_PARAMETER_AWB_MODE,
		/**< @ref MMAL_PARAMETER_IMAGEFX_T */
	MMAL_PARAMETER_IMAGE_EFFECT,
		/**< @ref MMAL_PARAMETER_COLOURFX_T */
	MMAL_PARAMETER_COLOUR_EFFECT,
		/**< @ref MMAL_PARAMETER_FLICKERAVOID_T */
	MMAL_PARAMETER_FLICKER_AVOID,
		/**< @ref MMAL_PARAMETER_FLASH_T */
	MMAL_PARAMETER_FLASH,
		/**< @ref MMAL_PARAMETER_REDEYE_T */
	MMAL_PARAMETER_REDEYE,
		/**< @ref MMAL_PARAMETER_FOCUS_T */
	MMAL_PARAMETER_FOCUS,
		/**< Unused? */
	MMAL_PARAMETER_FOCAL_LENGTHS,
		/**< @ref MMAL_PARAMETER_INT32_T */
	MMAL_PARAMETER_EXPOSURE_COMP,
		/**< @ref MMAL_PARAMETER_SCALEFACTOR_T */
	MMAL_PARAMETER_ZOOM,
		/**< @ref MMAL_PARAMETER_MIRROR_T */
	MMAL_PARAMETER_MIRROR,

	/* 0x10 */
		/**< @ref MMAL_PARAMETER_UINT32_T */
	MMAL_PARAMETER_CAMERA_NUM,
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_CAPTURE,
		/**< @ref MMAL_PARAMETER_EXPOSUREMODE_T */
	MMAL_PARAMETER_EXPOSURE_MODE,
		/**< @ref MMAL_PARAMETER_EXPOSUREMETERINGMODE_T */
	MMAL_PARAMETER_EXP_METERING_MODE,
		/**< @ref MMAL_PARAMETER_FOCUS_STATUS_T */
	MMAL_PARAMETER_FOCUS_STATUS,
		/**< @ref MMAL_PARAMETER_CAMERA_CONFIG_T */
	MMAL_PARAMETER_CAMERA_CONFIG,
		/**< @ref MMAL_PARAMETER_CAPTURE_STATUS_T */
	MMAL_PARAMETER_CAPTURE_STATUS,
		/**< @ref MMAL_PARAMETER_FACE_TRACK_T */
	MMAL_PARAMETER_FACE_TRACK,
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_DRAW_BOX_FACES_AND_FOCUS,
		/**< @ref MMAL_PARAMETER_UINT32_T */
	MMAL_PARAMETER_JPEG_Q_FACTOR,
		/**< @ref MMAL_PARAMETER_FRAME_RATE_T */
	MMAL_PARAMETER_FRAME_RATE,
		/**< @ref MMAL_PARAMETER_CAMERA_STC_MODE_T */
	MMAL_PARAMETER_USE_STC,
		/**< @ref MMAL_PARAMETER_CAMERA_INFO_T */
	MMAL_PARAMETER_CAMERA_INFO,
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_VIDEO_STABILISATION,
		/**< @ref MMAL_PARAMETER_FACE_TRACK_RESULTS_T */
	MMAL_PARAMETER_FACE_TRACK_RESULTS,
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_ENABLE_RAW_CAPTURE,

	/* 0x20 */
		/**< @ref MMAL_PARAMETER_URI_T */
	MMAL_PARAMETER_DPF_FILE,
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_ENABLE_DPF_FILE,
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_DPF_FAIL_IS_FATAL,
		/**< @ref MMAL_PARAMETER_CAPTUREMODE_T */
	MMAL_PARAMETER_CAPTURE_MODE,
		/**< @ref MMAL_PARAMETER_FOCUS_REGIONS_T */
	MMAL_PARAMETER_FOCUS_REGIONS,
		/**< @ref MMAL_PARAMETER_INPUT_CROP_T */
	MMAL_PARAMETER_INPUT_CROP,
		/**< @ref MMAL_PARAMETER_SENSOR_INFORMATION_T */
	MMAL_PARAMETER_SENSOR_INFORMATION,
		/**< @ref MMAL_PARAMETER_FLASH_SELECT_T */
	MMAL_PARAMETER_FLASH_SELECT,
		/**< @ref MMAL_PARAMETER_FIELD_OF_VIEW_T */
	MMAL_PARAMETER_FIELD_OF_VIEW,
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_HIGH_DYNAMIC_RANGE,
		/**< @ref MMAL_PARAMETER_DRC_T */
	MMAL_PARAMETER_DYNAMIC_RANGE_COMPRESSION,
		/**< @ref MMAL_PARAMETER_ALGORITHM_CONTROL_T */
	MMAL_PARAMETER_ALGORITHM_CONTROL,
		/**< @ref MMAL_PARAMETER_RATIONAL_T */
	MMAL_PARAMETER_SHARPNESS,
		/**< @ref MMAL_PARAMETER_RATIONAL_T */
	MMAL_PARAMETER_CONTRAST,
		/**< @ref MMAL_PARAMETER_RATIONAL_T */
	MMAL_PARAMETER_BRIGHTNESS,
		/**< @ref MMAL_PARAMETER_RATIONAL_T */
	MMAL_PARAMETER_SATURATION,

	/* 0x30 */
		/**< @ref MMAL_PARAMETER_UINT32_T */
	MMAL_PARAMETER_ISO,
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_ANTISHAKE,
		/** @ref MMAL_PARAMETER_IMAGEFX_PARAMETERS_T */
	MMAL_PARAMETER_IMAGE_EFFECT_PARAMETERS,
		/** @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_CAMERA_BURST_CAPTURE,
		/** @ref MMAL_PARAMETER_UINT32_T */
	MMAL_PARAMETER_CAMERA_MIN_ISO,
		/** @ref MMAL_PARAMETER_CAMERA_USE_CASE_T */
	MMAL_PARAMETER_CAMERA_USE_CASE,
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_CAPTURE_STATS_PASS,
		/** @ref MMAL_PARAMETER_UINT32_T */
	MMAL_PARAMETER_CAMERA_CUSTOM_SENSOR_CONFIG,
		/** @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_ENABLE_REGISTER_FILE,
		/** @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_REGISTER_FAIL_IS_FATAL,
		/** @ref MMAL_PARAMETER_CONFIGFILE_T */
	MMAL_PARAMETER_CONFIGFILE_REGISTERS,
		/** @ref MMAL_PARAMETER_CONFIGFILE_CHUNK_T */
	MMAL_PARAMETER_CONFIGFILE_CHUNK_REGISTERS,
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_JPEG_ATTACH_LOG,
		/**< @ref MMAL_PARAMETER_ZEROSHUTTERLAG_T */
	MMAL_PARAMETER_ZERO_SHUTTER_LAG,
		/**< @ref MMAL_PARAMETER_FPS_RANGE_T */
	MMAL_PARAMETER_FPS_RANGE,
		/**< @ref MMAL_PARAMETER_INT32_T */
	MMAL_PARAMETER_CAPTURE_EXPOSURE_COMP,

	/* 0x40 */
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_SW_SHARPEN_DISABLE,
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_FLASH_REQUIRED,
		/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_SW_SATURATION_DISABLE,
		/**< Takes a @ref MMAL_PARAMETER_UINT32_T */
	MMAL_PARAMETER_SHUTTER_SPEED,
		/**< Takes a @ref MMAL_PARAMETER_AWB_GAINS_T */
	MMAL_PARAMETER_CUSTOM_AWB_GAINS,
};

struct mmal_parameter_rational {
	s32 num;    /**< Numerator */
	s32 den;    /**< Denominator */
};

enum mmal_parameter_camera_config_timestamp_mode {
	MMAL_PARAM_TIMESTAMP_MODE_ZERO = 0, /* Always timestamp frames as 0 */
	MMAL_PARAM_TIMESTAMP_MODE_RAW_STC,  /* Use the raw STC value
					     * for the frame timestamp
					     */
	MMAL_PARAM_TIMESTAMP_MODE_RESET_STC, /* Use the STC timestamp
					      * but subtract the
					      * timestamp of the first
					      * frame sent to give a
					      * zero based timestamp.
					      */
};

struct mmal_parameter_fps_range {
	/**< Low end of the permitted framerate range */
	struct mmal_parameter_rational	fps_low;
	/**< High end of the permitted framerate range */
	struct mmal_parameter_rational	fps_high;
};

/* camera configuration parameter */
struct mmal_parameter_camera_config {
	/* Parameters for setting up the image pools */
	u32 max_stills_w; /* Max size of stills capture */
	u32 max_stills_h;
	u32 stills_yuv422; /* Allow YUV422 stills capture */
	u32 one_shot_stills; /* Continuous or one shot stills captures. */

	u32 max_preview_video_w; /* Max size of the preview or video
				  * capture frames
				  */
	u32 max_preview_video_h;
	u32 num_preview_video_frames;

	/** Sets the height of the circular buffer for stills capture. */
	u32 stills_capture_circular_buffer_height;

	/** Allows preview/encode to resume as fast as possible after the stills
	 * input frame has been received, and then processes the still frame in
	 * the background whilst preview/encode has resumed.
	 * Actual mode is controlled by MMAL_PARAMETER_CAPTURE_MODE.
	 */
	u32 fast_preview_resume;

	/** Selects algorithm for timestamping frames if
	 * there is no clock component connected.
	 * enum mmal_parameter_camera_config_timestamp_mode
	 */
	s32 use_stc_timestamp;
};

enum mmal_parameter_exposuremode {
	MMAL_PARAM_EXPOSUREMODE_OFF,
	MMAL_PARAM_EXPOSUREMODE_AUTO,
	MMAL_PARAM_EXPOSUREMODE_NIGHT,
	MMAL_PARAM_EXPOSUREMODE_NIGHTPREVIEW,
	MMAL_PARAM_EXPOSUREMODE_BACKLIGHT,
	MMAL_PARAM_EXPOSUREMODE_SPOTLIGHT,
	MMAL_PARAM_EXPOSUREMODE_SPORTS,
	MMAL_PARAM_EXPOSUREMODE_SNOW,
	MMAL_PARAM_EXPOSUREMODE_BEACH,
	MMAL_PARAM_EXPOSUREMODE_VERYLONG,
	MMAL_PARAM_EXPOSUREMODE_FIXEDFPS,
	MMAL_PARAM_EXPOSUREMODE_ANTISHAKE,
	MMAL_PARAM_EXPOSUREMODE_FIREWORKS,
};

enum mmal_parameter_exposuremeteringmode {
	MMAL_PARAM_EXPOSUREMETERINGMODE_AVERAGE,
	MMAL_PARAM_EXPOSUREMETERINGMODE_SPOT,
	MMAL_PARAM_EXPOSUREMETERINGMODE_BACKLIT,
	MMAL_PARAM_EXPOSUREMETERINGMODE_MATRIX,
};

enum mmal_parameter_awbmode {
	MMAL_PARAM_AWBMODE_OFF,
	MMAL_PARAM_AWBMODE_AUTO,
	MMAL_PARAM_AWBMODE_SUNLIGHT,
	MMAL_PARAM_AWBMODE_CLOUDY,
	MMAL_PARAM_AWBMODE_SHADE,
	MMAL_PARAM_AWBMODE_TUNGSTEN,
	MMAL_PARAM_AWBMODE_FLUORESCENT,
	MMAL_PARAM_AWBMODE_INCANDESCENT,
	MMAL_PARAM_AWBMODE_FLASH,
	MMAL_PARAM_AWBMODE_HORIZON,
};

enum mmal_parameter_imagefx {
	MMAL_PARAM_IMAGEFX_NONE,
	MMAL_PARAM_IMAGEFX_NEGATIVE,
	MMAL_PARAM_IMAGEFX_SOLARIZE,
	MMAL_PARAM_IMAGEFX_POSTERIZE,
	MMAL_PARAM_IMAGEFX_WHITEBOARD,
	MMAL_PARAM_IMAGEFX_BLACKBOARD,
	MMAL_PARAM_IMAGEFX_SKETCH,
	MMAL_PARAM_IMAGEFX_DENOISE,
	MMAL_PARAM_IMAGEFX_EMBOSS,
	MMAL_PARAM_IMAGEFX_OILPAINT,
	MMAL_PARAM_IMAGEFX_HATCH,
	MMAL_PARAM_IMAGEFX_GPEN,
	MMAL_PARAM_IMAGEFX_PASTEL,
	MMAL_PARAM_IMAGEFX_WATERCOLOUR,
	MMAL_PARAM_IMAGEFX_FILM,
	MMAL_PARAM_IMAGEFX_BLUR,
	MMAL_PARAM_IMAGEFX_SATURATION,
	MMAL_PARAM_IMAGEFX_COLOURSWAP,
	MMAL_PARAM_IMAGEFX_WASHEDOUT,
	MMAL_PARAM_IMAGEFX_POSTERISE,
	MMAL_PARAM_IMAGEFX_COLOURPOINT,
	MMAL_PARAM_IMAGEFX_COLOURBALANCE,
	MMAL_PARAM_IMAGEFX_CARTOON,
};

enum MMAL_PARAM_FLICKERAVOID_T {
	MMAL_PARAM_FLICKERAVOID_OFF,
	MMAL_PARAM_FLICKERAVOID_AUTO,
	MMAL_PARAM_FLICKERAVOID_50HZ,
	MMAL_PARAM_FLICKERAVOID_60HZ,
	MMAL_PARAM_FLICKERAVOID_MAX = 0x7FFFFFFF
};

struct mmal_parameter_awbgains {
	struct mmal_parameter_rational r_gain;	/**< Red gain */
	struct mmal_parameter_rational b_gain;	/**< Blue gain */
};

/** Manner of video rate control */
enum mmal_parameter_rate_control_mode {
	MMAL_VIDEO_RATECONTROL_DEFAULT,
	MMAL_VIDEO_RATECONTROL_VARIABLE,
	MMAL_VIDEO_RATECONTROL_CONSTANT,
	MMAL_VIDEO_RATECONTROL_VARIABLE_SKIP_FRAMES,
	MMAL_VIDEO_RATECONTROL_CONSTANT_SKIP_FRAMES
};

enum mmal_video_profile {
	MMAL_VIDEO_PROFILE_H263_BASELINE,
	MMAL_VIDEO_PROFILE_H263_H320CODING,
	MMAL_VIDEO_PROFILE_H263_BACKWARDCOMPATIBLE,
	MMAL_VIDEO_PROFILE_H263_ISWV2,
	MMAL_VIDEO_PROFILE_H263_ISWV3,
	MMAL_VIDEO_PROFILE_H263_HIGHCOMPRESSION,
	MMAL_VIDEO_PROFILE_H263_INTERNET,
	MMAL_VIDEO_PROFILE_H263_INTERLACE,
	MMAL_VIDEO_PROFILE_H263_HIGHLATENCY,
	MMAL_VIDEO_PROFILE_MP4V_SIMPLE,
	MMAL_VIDEO_PROFILE_MP4V_SIMPLESCALABLE,
	MMAL_VIDEO_PROFILE_MP4V_CORE,
	MMAL_VIDEO_PROFILE_MP4V_MAIN,
	MMAL_VIDEO_PROFILE_MP4V_NBIT,
	MMAL_VIDEO_PROFILE_MP4V_SCALABLETEXTURE,
	MMAL_VIDEO_PROFILE_MP4V_SIMPLEFACE,
	MMAL_VIDEO_PROFILE_MP4V_SIMPLEFBA,
	MMAL_VIDEO_PROFILE_MP4V_BASICANIMATED,
	MMAL_VIDEO_PROFILE_MP4V_HYBRID,
	MMAL_VIDEO_PROFILE_MP4V_ADVANCEDREALTIME,
	MMAL_VIDEO_PROFILE_MP4V_CORESCALABLE,
	MMAL_VIDEO_PROFILE_MP4V_ADVANCEDCODING,
	MMAL_VIDEO_PROFILE_MP4V_ADVANCEDCORE,
	MMAL_VIDEO_PROFILE_MP4V_ADVANCEDSCALABLE,
	MMAL_VIDEO_PROFILE_MP4V_ADVANCEDSIMPLE,
	MMAL_VIDEO_PROFILE_H264_BASELINE,
	MMAL_VIDEO_PROFILE_H264_MAIN,
	MMAL_VIDEO_PROFILE_H264_EXTENDED,
	MMAL_VIDEO_PROFILE_H264_HIGH,
	MMAL_VIDEO_PROFILE_H264_HIGH10,
	MMAL_VIDEO_PROFILE_H264_HIGH422,
	MMAL_VIDEO_PROFILE_H264_HIGH444,
	MMAL_VIDEO_PROFILE_H264_CONSTRAINED_BASELINE,
	MMAL_VIDEO_PROFILE_DUMMY = 0x7FFFFFFF
};

enum mmal_video_level {
	MMAL_VIDEO_LEVEL_H263_10,
	MMAL_VIDEO_LEVEL_H263_20,
	MMAL_VIDEO_LEVEL_H263_30,
	MMAL_VIDEO_LEVEL_H263_40,
	MMAL_VIDEO_LEVEL_H263_45,
	MMAL_VIDEO_LEVEL_H263_50,
	MMAL_VIDEO_LEVEL_H263_60,
	MMAL_VIDEO_LEVEL_H263_70,
	MMAL_VIDEO_LEVEL_MP4V_0,
	MMAL_VIDEO_LEVEL_MP4V_0b,
	MMAL_VIDEO_LEVEL_MP4V_1,
	MMAL_VIDEO_LEVEL_MP4V_2,
	MMAL_VIDEO_LEVEL_MP4V_3,
	MMAL_VIDEO_LEVEL_MP4V_4,
	MMAL_VIDEO_LEVEL_MP4V_4a,
	MMAL_VIDEO_LEVEL_MP4V_5,
	MMAL_VIDEO_LEVEL_MP4V_6,
	MMAL_VIDEO_LEVEL_H264_1,
	MMAL_VIDEO_LEVEL_H264_1b,
	MMAL_VIDEO_LEVEL_H264_11,
	MMAL_VIDEO_LEVEL_H264_12,
	MMAL_VIDEO_LEVEL_H264_13,
	MMAL_VIDEO_LEVEL_H264_2,
	MMAL_VIDEO_LEVEL_H264_21,
	MMAL_VIDEO_LEVEL_H264_22,
	MMAL_VIDEO_LEVEL_H264_3,
	MMAL_VIDEO_LEVEL_H264_31,
	MMAL_VIDEO_LEVEL_H264_32,
	MMAL_VIDEO_LEVEL_H264_4,
	MMAL_VIDEO_LEVEL_H264_41,
	MMAL_VIDEO_LEVEL_H264_42,
	MMAL_VIDEO_LEVEL_H264_5,
	MMAL_VIDEO_LEVEL_H264_51,
	MMAL_VIDEO_LEVEL_DUMMY = 0x7FFFFFFF
};

struct mmal_parameter_video_profile {
	enum mmal_video_profile profile;
	enum mmal_video_level level;
};

/* video parameters */

enum mmal_parameter_video_type {
	/** @ref MMAL_DISPLAYREGION_T */
	MMAL_PARAMETER_DISPLAYREGION = MMAL_PARAMETER_GROUP_VIDEO,

	/** @ref MMAL_PARAMETER_VIDEO_PROFILE_T */
	MMAL_PARAMETER_SUPPORTED_PROFILES,

	/** @ref MMAL_PARAMETER_VIDEO_PROFILE_T */
	MMAL_PARAMETER_PROFILE,

	/** @ref MMAL_PARAMETER_UINT32_T */
	MMAL_PARAMETER_INTRAPERIOD,

	/** @ref MMAL_PARAMETER_VIDEO_RATECONTROL_T */
	MMAL_PARAMETER_RATECONTROL,

	/** @ref MMAL_PARAMETER_VIDEO_NALUNITFORMAT_T */
	MMAL_PARAMETER_NALUNITFORMAT,

	/** @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_MINIMISE_FRAGMENTATION,

	/** @ref MMAL_PARAMETER_UINT32_T.
	 * Setting the value to zero resets to the default (one slice per
	 * frame).
	 */
	MMAL_PARAMETER_MB_ROWS_PER_SLICE,

	/** @ref MMAL_PARAMETER_VIDEO_LEVEL_EXTENSION_T */
	MMAL_PARAMETER_VIDEO_LEVEL_EXTENSION,

	/** @ref MMAL_PARAMETER_VIDEO_EEDE_ENABLE_T */
	MMAL_PARAMETER_VIDEO_EEDE_ENABLE,

	/** @ref MMAL_PARAMETER_VIDEO_EEDE_LOSSRATE_T */
	MMAL_PARAMETER_VIDEO_EEDE_LOSSRATE,

	/** @ref MMAL_PARAMETER_BOOLEAN_T. Request an I-frame. */
	MMAL_PARAMETER_VIDEO_REQUEST_I_FRAME,
	/** @ref MMAL_PARAMETER_VIDEO_INTRA_REFRESH_T */
	MMAL_PARAMETER_VIDEO_INTRA_REFRESH,

	/** @ref MMAL_PARAMETER_BOOLEAN_T. */
	MMAL_PARAMETER_VIDEO_IMMUTABLE_INPUT,

	/** @ref MMAL_PARAMETER_UINT32_T. Run-time bit rate control */
	MMAL_PARAMETER_VIDEO_BIT_RATE,

	/** @ref MMAL_PARAMETER_FRAME_RATE_T */
	MMAL_PARAMETER_VIDEO_FRAME_RATE,

	/** @ref MMAL_PARAMETER_UINT32_T. */
	MMAL_PARAMETER_VIDEO_ENCODE_MIN_QUANT,

	/** @ref MMAL_PARAMETER_UINT32_T. */
	MMAL_PARAMETER_VIDEO_ENCODE_MAX_QUANT,

	/** @ref MMAL_PARAMETER_VIDEO_ENCODE_RC_MODEL_T. */
	MMAL_PARAMETER_VIDEO_ENCODE_RC_MODEL,

	MMAL_PARAMETER_EXTRA_BUFFERS, /**< @ref MMAL_PARAMETER_UINT32_T. */
	/** @ref MMAL_PARAMETER_UINT32_T.
	 * Changing this parameter from the default can reduce frame rate
	 * because image buffers need to be re-pitched.
	 */
	MMAL_PARAMETER_VIDEO_ALIGN_HORIZ,

	/** @ref MMAL_PARAMETER_UINT32_T.
	 * Changing this parameter from the default can reduce frame rate
	 * because image buffers need to be re-pitched.
	 */
	MMAL_PARAMETER_VIDEO_ALIGN_VERT,

	/** @ref MMAL_PARAMETER_BOOLEAN_T. */
	MMAL_PARAMETER_VIDEO_DROPPABLE_PFRAMES,

	/** @ref MMAL_PARAMETER_UINT32_T. */
	MMAL_PARAMETER_VIDEO_ENCODE_INITIAL_QUANT,

	/**< @ref MMAL_PARAMETER_UINT32_T. */
	MMAL_PARAMETER_VIDEO_ENCODE_QP_P,

	/**< @ref MMAL_PARAMETER_UINT32_T. */
	MMAL_PARAMETER_VIDEO_ENCODE_RC_SLICE_DQUANT,

	/** @ref MMAL_PARAMETER_UINT32_T */
	MMAL_PARAMETER_VIDEO_ENCODE_FRAME_LIMIT_BITS,

	/** @ref MMAL_PARAMETER_UINT32_T. */
	MMAL_PARAMETER_VIDEO_ENCODE_PEAK_RATE,

	/* H264 specific parameters */

	/** @ref MMAL_PARAMETER_BOOLEAN_T. */
	MMAL_PARAMETER_VIDEO_ENCODE_H264_DISABLE_CABAC,

	/** @ref MMAL_PARAMETER_BOOLEAN_T. */
	MMAL_PARAMETER_VIDEO_ENCODE_H264_LOW_LATENCY,

	/** @ref MMAL_PARAMETER_BOOLEAN_T. */
	MMAL_PARAMETER_VIDEO_ENCODE_H264_AU_DELIMITERS,

	/** @ref MMAL_PARAMETER_UINT32_T. */
	MMAL_PARAMETER_VIDEO_ENCODE_H264_DEBLOCK_IDC,

	/** @ref MMAL_PARAMETER_VIDEO_ENCODER_H264_MB_INTRA_MODES_T. */
	MMAL_PARAMETER_VIDEO_ENCODE_H264_MB_INTRA_MODE,

	/** @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_VIDEO_ENCODE_HEADER_ON_OPEN,

	/** @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_VIDEO_ENCODE_PRECODE_FOR_QP,

	/** @ref MMAL_PARAMETER_VIDEO_DRM_INIT_INFO_T. */
	MMAL_PARAMETER_VIDEO_DRM_INIT_INFO,

	/** @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_VIDEO_TIMESTAMP_FIFO,

	/** @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_VIDEO_DECODE_ERROR_CONCEALMENT,

	/** @ref MMAL_PARAMETER_VIDEO_DRM_PROTECT_BUFFER_T. */
	MMAL_PARAMETER_VIDEO_DRM_PROTECT_BUFFER,

	/** @ref MMAL_PARAMETER_BYTES_T */
	MMAL_PARAMETER_VIDEO_DECODE_CONFIG_VD3,

	/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_VIDEO_ENCODE_H264_VCL_HRD_PARAMETERS,

	/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_VIDEO_ENCODE_H264_LOW_DELAY_HRD_FLAG,

	/**< @ref MMAL_PARAMETER_BOOLEAN_T */
	MMAL_PARAMETER_VIDEO_ENCODE_INLINE_HEADER
};

/** Valid mirror modes */
enum mmal_parameter_mirror {
	MMAL_PARAM_MIRROR_NONE,
	MMAL_PARAM_MIRROR_VERTICAL,
	MMAL_PARAM_MIRROR_HORIZONTAL,
	MMAL_PARAM_MIRROR_BOTH,
};

enum mmal_parameter_displaytransform {
	MMAL_DISPLAY_ROT0 = 0,
	MMAL_DISPLAY_MIRROR_ROT0 = 1,
	MMAL_DISPLAY_MIRROR_ROT180 = 2,
	MMAL_DISPLAY_ROT180 = 3,
	MMAL_DISPLAY_MIRROR_ROT90 = 4,
	MMAL_DISPLAY_ROT270 = 5,
	MMAL_DISPLAY_ROT90 = 6,
	MMAL_DISPLAY_MIRROR_ROT270 = 7,
};

enum mmal_parameter_displaymode {
	MMAL_DISPLAY_MODE_FILL = 0,
	MMAL_DISPLAY_MODE_LETTERBOX = 1,
};

enum mmal_parameter_displayset {
	MMAL_DISPLAY_SET_NONE = 0,
	MMAL_DISPLAY_SET_NUM = 1,
	MMAL_DISPLAY_SET_FULLSCREEN = 2,
	MMAL_DISPLAY_SET_TRANSFORM = 4,
	MMAL_DISPLAY_SET_DEST_RECT = 8,
	MMAL_DISPLAY_SET_SRC_RECT = 0x10,
	MMAL_DISPLAY_SET_MODE = 0x20,
	MMAL_DISPLAY_SET_PIXEL = 0x40,
	MMAL_DISPLAY_SET_NOASPECT = 0x80,
	MMAL_DISPLAY_SET_LAYER = 0x100,
	MMAL_DISPLAY_SET_COPYPROTECT = 0x200,
	MMAL_DISPLAY_SET_ALPHA = 0x400,
};

/* rectangle, used lots so it gets its own struct */
struct vchiq_mmal_rect {
	s32 x;
	s32 y;
	s32 width;
	s32 height;
};

struct mmal_parameter_displayregion {
	/** Bitfield that indicates which fields are set and should be
	 * used. All other fields will maintain their current value.
	 * \ref MMAL_DISPLAYSET_T defines the bits that can be
	 * combined.
	 */
	u32 set;

	/** Describes the display output device, with 0 typically
	 * being a directly connected LCD display.  The actual values
	 * will depend on the hardware.  Code using hard-wired numbers
	 * (e.g. 2) is certain to fail.
	 */

	u32 display_num;
	/** Indicates that we are using the full device screen area,
	 * rather than a window of the display.  If zero, then
	 * dest_rect is used to specify a region of the display to
	 * use.
	 */

	s32 fullscreen;
	/** Indicates any rotation or flipping used to map frames onto
	 * the natural display orientation.
	 */
	u32 transform; /* enum mmal_parameter_displaytransform */

	/** Where to display the frame within the screen, if
	 * fullscreen is zero.
	 */
	struct vchiq_mmal_rect dest_rect;

	/** Indicates which area of the frame to display. If all
	 * values are zero, the whole frame will be used.
	 */
	struct vchiq_mmal_rect src_rect;

	/** If set to non-zero, indicates that any display scaling
	 * should disregard the aspect ratio of the frame region being
	 * displayed.
	 */
	s32 noaspect;

	/** Indicates how the image should be scaled to fit the
	 * display. \code MMAL_DISPLAY_MODE_FILL \endcode indicates
	 * that the image should fill the screen by potentially
	 * cropping the frames.  Setting \code mode \endcode to \code
	 * MMAL_DISPLAY_MODE_LETTERBOX \endcode indicates that all the
	 * source region should be displayed and black bars added if
	 * necessary.
	 */
	u32 mode; /* enum mmal_parameter_displaymode */

	/** If non-zero, defines the width of a source pixel relative
	 * to \code pixel_y \endcode.  If zero, then pixels default to
	 * being square.
	 */
	u32 pixel_x;

	/** If non-zero, defines the height of a source pixel relative
	 * to \code pixel_x \endcode.  If zero, then pixels default to
	 * being square.
	 */
	u32 pixel_y;

	/** Sets the relative depth of the images, with greater values
	 * being in front of smaller values.
	 */
	u32 layer;

	/** Set to non-zero to ensure copy protection is used on
	 * output.
	 */
	s32 copyprotect_required;

	/** Level of opacity of the layer, where zero is fully
	 * transparent and 255 is fully opaque.
	 */
	u32 alpha;
};

#define MMAL_MAX_IMAGEFX_PARAMETERS 5

struct mmal_parameter_imagefx_parameters {
	enum mmal_parameter_imagefx effect;
	u32 num_effect_params;
	u32 effect_parameter[MMAL_MAX_IMAGEFX_PARAMETERS];
};

#define MMAL_PARAMETER_CAMERA_INFO_MAX_CAMERAS 4
#define MMAL_PARAMETER_CAMERA_INFO_MAX_FLASHES 2
#define MMAL_PARAMETER_CAMERA_INFO_MAX_STR_LEN 16

struct mmal_parameter_camera_info_camera_t {
	u32    port_id;
	u32    max_width;
	u32    max_height;
	u32    lens_present;
	u8     camera_name[MMAL_PARAMETER_CAMERA_INFO_MAX_STR_LEN];
};

enum mmal_parameter_camera_info_flash_type_t {
	/* Make values explicit to ensure they match values in config ini */
	MMAL_PARAMETER_CAMERA_INFO_FLASH_TYPE_XENON = 0,
	MMAL_PARAMETER_CAMERA_INFO_FLASH_TYPE_LED   = 1,
	MMAL_PARAMETER_CAMERA_INFO_FLASH_TYPE_OTHER = 2,
	MMAL_PARAMETER_CAMERA_INFO_FLASH_TYPE_MAX = 0x7FFFFFFF
};

struct mmal_parameter_camera_info_flash_t {
	enum mmal_parameter_camera_info_flash_type_t flash_type;
};

struct mmal_parameter_camera_info_t {
	u32                            num_cameras;
	u32                            num_flashes;
	struct mmal_parameter_camera_info_camera_t
				cameras[MMAL_PARAMETER_CAMERA_INFO_MAX_CAMERAS];
	struct mmal_parameter_camera_info_flash_t
				flashes[MMAL_PARAMETER_CAMERA_INFO_MAX_FLASHES];
};

#endif
