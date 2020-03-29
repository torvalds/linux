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
 *
 * core driver device
 */

#define V4L2_CTRL_COUNT 29 /* number of v4l controls */

enum {
	COMP_CAMERA = 0,
	COMP_PREVIEW,
	COMP_IMAGE_ENCODE,
	COMP_VIDEO_ENCODE,
	COMP_COUNT
};

enum {
	CAM_PORT_PREVIEW = 0,
	CAM_PORT_VIDEO,
	CAM_PORT_CAPTURE,
	CAM_PORT_COUNT
};

extern int bcm2835_v4l2_debug;

struct bm2835_mmal_dev {
	/* v4l2 devices */
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct mutex mutex;

	/* controls */
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *ctrls[V4L2_CTRL_COUNT];
	enum v4l2_scene_mode scene_mode;
	struct mmal_colourfx colourfx;
	int hflip;
	int vflip;
	int red_gain;
	int blue_gain;
	enum mmal_parameter_exposuremode exposure_mode_user;
	enum v4l2_exposure_auto_type exposure_mode_v4l2_user;
	/* active exposure mode may differ if selected via a scene mode */
	enum mmal_parameter_exposuremode exposure_mode_active;
	enum mmal_parameter_exposuremeteringmode metering_mode;
	unsigned int manual_shutter_speed;
	bool exp_auto_priority;
	bool manual_iso_enabled;
	u32 iso;

	/* allocated mmal instance and components */
	struct vchiq_mmal_instance *instance;
	struct vchiq_mmal_component *component[COMP_COUNT];
	int camera_use_count;

	struct v4l2_window overlay;

	struct {
		unsigned int width;  /* width */
		unsigned int height;  /* height */
		unsigned int stride;  /* stride */
		unsigned int buffersize; /* buffer size with padding */
		struct mmal_fmt *fmt;
		struct v4l2_fract timeperframe;

		/* H264 encode bitrate */
		int encode_bitrate;
		/* H264 bitrate mode. CBR/VBR */
		int encode_bitrate_mode;
		/* H264 profile */
		enum v4l2_mpeg_video_h264_profile enc_profile;
		/* H264 level */
		enum v4l2_mpeg_video_h264_level enc_level;
		/* JPEG Q-factor */
		int q_factor;

		struct vb2_queue vb_vidq;

		/* VC start timestamp for streaming */
		s64 vc_start_timestamp;
		/* Kernel start timestamp for streaming */
		ktime_t kernel_start_ts;
		/* Sequence number of last buffer */
		u32 sequence;

		struct vchiq_mmal_port *port; /* port being used for capture */
		/* camera port being used for capture */
		struct vchiq_mmal_port *camera_port;
		/* component being used for encode */
		struct vchiq_mmal_component *encode_component;
		/* number of frames remaining which driver should capture */
		unsigned int frame_count;
		/* last frame completion */
		struct completion frame_cmplt;

	} capture;

	unsigned int camera_num;
	unsigned int max_width;
	unsigned int max_height;
	unsigned int rgb_bgr_swapped;
};

int bm2835_mmal_init_controls(
			struct bm2835_mmal_dev *dev,
			struct v4l2_ctrl_handler *hdl);

int bm2835_mmal_set_all_camera_controls(struct bm2835_mmal_dev *dev);
int set_framerate_params(struct bm2835_mmal_dev *dev);

/* Debug helpers */

#define v4l2_dump_pix_format(level, debug, dev, pix_fmt, desc)	\
{	\
	v4l2_dbg(level, debug, dev,	\
"%s: w %u h %u field %u pfmt 0x%x bpl %u sz_img %u colorspace 0x%x priv %u\n", \
		desc,	\
		(pix_fmt)->width, (pix_fmt)->height, (pix_fmt)->field,	\
		(pix_fmt)->pixelformat, (pix_fmt)->bytesperline,	\
		(pix_fmt)->sizeimage, (pix_fmt)->colorspace, (pix_fmt)->priv); \
}

#define v4l2_dump_win_format(level, debug, dev, win_fmt, desc)	\
{	\
	v4l2_dbg(level, debug, dev,	\
"%s: w %u h %u l %u t %u  field %u chromakey %06X clip %p " \
"clipcount %u bitmap %p\n", \
		desc,	\
		(win_fmt)->w.width, (win_fmt)->w.height, \
		(win_fmt)->w.left, (win_fmt)->w.top, \
		(win_fmt)->field,	\
		(win_fmt)->chromakey,	\
		(win_fmt)->clips, (win_fmt)->clipcount,	\
		(win_fmt)->bitmap); \
}
