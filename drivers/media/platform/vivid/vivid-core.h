/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-core.h - core datastructures
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _VIVID_CORE_H_
#define _VIVID_CORE_H_

#include <linux/fb.h>
#include <linux/workqueue.h>
#include <media/cec.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ctrls.h>
#include <media/tpg/v4l2-tpg.h>
#include "vivid-rds-gen.h"
#include "vivid-vbi-gen.h"

#define dprintk(dev, level, fmt, arg...) \
	v4l2_dbg(level, vivid_debug, &dev->v4l2_dev, fmt, ## arg)

/* Maximum allowed frame rate
 *
 * vivid will allow setting timeperframe in [1/FPS_MAX - FPS_MAX/1] range.
 *
 * Ideally FPS_MAX should be infinity, i.e. practically UINT_MAX, but that
 * might hit application errors when they manipulate these values.
 *
 * Besides, for tpf < 10ms image-generation logic should be changed, to avoid
 * producing frames with equal content.
 */
#define FPS_MAX 100

/* The maximum number of clip rectangles */
#define MAX_CLIPS  16
/* The maximum number of inputs */
#define MAX_INPUTS 16
/* The maximum number of outputs */
#define MAX_OUTPUTS 16
/* The maximum up or down scaling factor is 4 */
#define MAX_ZOOM  4
/* The maximum image width/height are set to 4K DMT */
#define MAX_WIDTH  4096
#define MAX_HEIGHT 2160
/* The minimum image width/height */
#define MIN_WIDTH  16
#define MIN_HEIGHT 16
/* The data_offset of plane 0 for the multiplanar formats */
#define PLANE0_DATA_OFFSET 128

/* The supported TV frequency range in MHz */
#define MIN_TV_FREQ (44U * 16U)
#define MAX_TV_FREQ (958U * 16U)

/* The number of samples returned in every SDR buffer */
#define SDR_CAP_SAMPLES_PER_BUF 0x4000

/* used by the threads to know when to resync internal counters */
#define JIFFIES_PER_DAY (3600U * 24U * HZ)
#define JIFFIES_RESYNC (JIFFIES_PER_DAY * (0xf0000000U / JIFFIES_PER_DAY))

extern const struct v4l2_rect vivid_min_rect;
extern const struct v4l2_rect vivid_max_rect;
extern unsigned vivid_debug;

struct vivid_fmt {
	u32	fourcc;          /* v4l2 format id */
	enum	tgp_color_enc color_enc;
	bool	can_do_overlay;
	u8	vdownsampling[TPG_MAX_PLANES];
	u32	alpha_mask;
	u8	planes;
	u8	buffers;
	u32	data_offset[TPG_MAX_PLANES];
	u32	bit_depth[TPG_MAX_PLANES];
};

extern struct vivid_fmt vivid_formats[];

/* buffer for one video frame */
struct vivid_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer vb;
	struct list_head	list;
};

enum vivid_input {
	WEBCAM,
	TV,
	SVID,
	HDMI,
};

enum vivid_signal_mode {
	CURRENT_DV_TIMINGS,
	CURRENT_STD = CURRENT_DV_TIMINGS,
	NO_SIGNAL,
	NO_LOCK,
	OUT_OF_RANGE,
	SELECTED_DV_TIMINGS,
	SELECTED_STD = SELECTED_DV_TIMINGS,
	CYCLE_DV_TIMINGS,
	CYCLE_STD = CYCLE_DV_TIMINGS,
	CUSTOM_DV_TIMINGS,
};

enum vivid_colorspace {
	VIVID_CS_170M,
	VIVID_CS_709,
	VIVID_CS_SRGB,
	VIVID_CS_OPRGB,
	VIVID_CS_2020,
	VIVID_CS_DCI_P3,
	VIVID_CS_240M,
	VIVID_CS_SYS_M,
	VIVID_CS_SYS_BG,
};

#define VIVID_INVALID_SIGNAL(mode) \
	((mode) == NO_SIGNAL || (mode) == NO_LOCK || (mode) == OUT_OF_RANGE)

struct vivid_cec_work {
	struct list_head	list;
	struct delayed_work	work;
	struct cec_adapter	*adap;
	struct vivid_dev	*dev;
	unsigned int		usecs;
	unsigned int		timeout_ms;
	u8			tx_status;
	struct cec_msg		msg;
};

struct vivid_dev {
	unsigned			inst;
	struct v4l2_device		v4l2_dev;
#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_device		mdev;
	struct media_pad		vid_cap_pad;
	struct media_pad		vid_out_pad;
	struct media_pad		vbi_cap_pad;
	struct media_pad		vbi_out_pad;
	struct media_pad		sdr_cap_pad;
#endif
	struct v4l2_ctrl_handler	ctrl_hdl_user_gen;
	struct v4l2_ctrl_handler	ctrl_hdl_user_vid;
	struct v4l2_ctrl_handler	ctrl_hdl_user_aud;
	struct v4l2_ctrl_handler	ctrl_hdl_streaming;
	struct v4l2_ctrl_handler	ctrl_hdl_sdtv_cap;
	struct v4l2_ctrl_handler	ctrl_hdl_loop_cap;
	struct v4l2_ctrl_handler	ctrl_hdl_fb;
	struct video_device		vid_cap_dev;
	struct v4l2_ctrl_handler	ctrl_hdl_vid_cap;
	struct video_device		vid_out_dev;
	struct v4l2_ctrl_handler	ctrl_hdl_vid_out;
	struct video_device		vbi_cap_dev;
	struct v4l2_ctrl_handler	ctrl_hdl_vbi_cap;
	struct video_device		vbi_out_dev;
	struct v4l2_ctrl_handler	ctrl_hdl_vbi_out;
	struct video_device		radio_rx_dev;
	struct v4l2_ctrl_handler	ctrl_hdl_radio_rx;
	struct video_device		radio_tx_dev;
	struct v4l2_ctrl_handler	ctrl_hdl_radio_tx;
	struct video_device		sdr_cap_dev;
	struct v4l2_ctrl_handler	ctrl_hdl_sdr_cap;
	spinlock_t			slock;
	struct mutex			mutex;

	/* capabilities */
	u32				vid_cap_caps;
	u32				vid_out_caps;
	u32				vbi_cap_caps;
	u32				vbi_out_caps;
	u32				sdr_cap_caps;
	u32				radio_rx_caps;
	u32				radio_tx_caps;

	/* supported features */
	bool				multiplanar;
	unsigned			num_inputs;
	u8				input_type[MAX_INPUTS];
	u8				input_name_counter[MAX_INPUTS];
	unsigned			num_outputs;
	u8				output_type[MAX_OUTPUTS];
	u8				output_name_counter[MAX_OUTPUTS];
	bool				has_audio_inputs;
	bool				has_audio_outputs;
	bool				has_vid_cap;
	bool				has_vid_out;
	bool				has_vbi_cap;
	bool				has_raw_vbi_cap;
	bool				has_sliced_vbi_cap;
	bool				has_vbi_out;
	bool				has_raw_vbi_out;
	bool				has_sliced_vbi_out;
	bool				has_radio_rx;
	bool				has_radio_tx;
	bool				has_sdr_cap;
	bool				has_fb;

	bool				can_loop_video;

	/* controls */
	struct v4l2_ctrl		*brightness;
	struct v4l2_ctrl		*contrast;
	struct v4l2_ctrl		*saturation;
	struct v4l2_ctrl		*hue;
	struct {
		/* autogain/gain cluster */
		struct v4l2_ctrl	*autogain;
		struct v4l2_ctrl	*gain;
	};
	struct v4l2_ctrl		*volume;
	struct v4l2_ctrl		*mute;
	struct v4l2_ctrl		*alpha;
	struct v4l2_ctrl		*button;
	struct v4l2_ctrl		*boolean;
	struct v4l2_ctrl		*int32;
	struct v4l2_ctrl		*int64;
	struct v4l2_ctrl		*menu;
	struct v4l2_ctrl		*string;
	struct v4l2_ctrl		*bitmask;
	struct v4l2_ctrl		*int_menu;
	struct v4l2_ctrl		*test_pattern;
	struct v4l2_ctrl		*colorspace;
	struct v4l2_ctrl		*rgb_range_cap;
	struct v4l2_ctrl		*real_rgb_range_cap;
	struct {
		/* std_signal_mode/standard cluster */
		struct v4l2_ctrl	*ctrl_std_signal_mode;
		struct v4l2_ctrl	*ctrl_standard;
	};
	struct {
		/* dv_timings_signal_mode/timings cluster */
		struct v4l2_ctrl	*ctrl_dv_timings_signal_mode;
		struct v4l2_ctrl	*ctrl_dv_timings;
	};
	struct v4l2_ctrl		*ctrl_has_crop_cap;
	struct v4l2_ctrl		*ctrl_has_compose_cap;
	struct v4l2_ctrl		*ctrl_has_scaler_cap;
	struct v4l2_ctrl		*ctrl_has_crop_out;
	struct v4l2_ctrl		*ctrl_has_compose_out;
	struct v4l2_ctrl		*ctrl_has_scaler_out;
	struct v4l2_ctrl		*ctrl_tx_mode;
	struct v4l2_ctrl		*ctrl_tx_rgb_range;

	struct v4l2_ctrl		*radio_tx_rds_pi;
	struct v4l2_ctrl		*radio_tx_rds_pty;
	struct v4l2_ctrl		*radio_tx_rds_mono_stereo;
	struct v4l2_ctrl		*radio_tx_rds_art_head;
	struct v4l2_ctrl		*radio_tx_rds_compressed;
	struct v4l2_ctrl		*radio_tx_rds_dyn_pty;
	struct v4l2_ctrl		*radio_tx_rds_ta;
	struct v4l2_ctrl		*radio_tx_rds_tp;
	struct v4l2_ctrl		*radio_tx_rds_ms;
	struct v4l2_ctrl		*radio_tx_rds_psname;
	struct v4l2_ctrl		*radio_tx_rds_radiotext;

	struct v4l2_ctrl		*radio_rx_rds_pty;
	struct v4l2_ctrl		*radio_rx_rds_ta;
	struct v4l2_ctrl		*radio_rx_rds_tp;
	struct v4l2_ctrl		*radio_rx_rds_ms;
	struct v4l2_ctrl		*radio_rx_rds_psname;
	struct v4l2_ctrl		*radio_rx_rds_radiotext;

	unsigned			input_brightness[MAX_INPUTS];
	unsigned			osd_mode;
	unsigned			button_pressed;
	bool				sensor_hflip;
	bool				sensor_vflip;
	bool				hflip;
	bool				vflip;
	bool				vbi_cap_interlaced;
	bool				loop_video;
	bool				reduced_fps;

	/* Framebuffer */
	unsigned long			video_pbase;
	void				*video_vbase;
	u32				video_buffer_size;
	int				display_width;
	int				display_height;
	int				display_byte_stride;
	int				bits_per_pixel;
	int				bytes_per_pixel;
	struct fb_info			fb_info;
	struct fb_var_screeninfo	fb_defined;
	struct fb_fix_screeninfo	fb_fix;

	/* Error injection */
	bool				queue_setup_error;
	bool				buf_prepare_error;
	bool				start_streaming_error;
	bool				dqbuf_error;
	bool				seq_wrap;
	bool				time_wrap;
	u64				time_wrap_offset;
	unsigned			perc_dropped_buffers;
	enum vivid_signal_mode		std_signal_mode;
	unsigned			query_std_last;
	v4l2_std_id			query_std;
	enum tpg_video_aspect		std_aspect_ratio;

	enum vivid_signal_mode		dv_timings_signal_mode;
	char				**query_dv_timings_qmenu;
	unsigned			query_dv_timings_size;
	unsigned			query_dv_timings_last;
	unsigned			query_dv_timings;
	enum tpg_video_aspect		dv_timings_aspect_ratio;

	/* Input */
	unsigned			input;
	v4l2_std_id			std_cap;
	struct v4l2_dv_timings		dv_timings_cap;
	u32				service_set_cap;
	struct vivid_vbi_gen_data	vbi_gen;
	u8				*edid;
	unsigned			edid_blocks;
	unsigned			edid_max_blocks;
	unsigned			webcam_size_idx;
	unsigned			webcam_ival_idx;
	unsigned			tv_freq;
	unsigned			tv_audmode;
	unsigned			tv_field_cap;
	unsigned			tv_audio_input;

	/* Capture Overlay */
	struct v4l2_framebuffer		fb_cap;
	struct v4l2_fh			*overlay_cap_owner;
	void				*fb_vbase_cap;
	int				overlay_cap_top, overlay_cap_left;
	enum v4l2_field			overlay_cap_field;
	void				*bitmap_cap;
	struct v4l2_clip		clips_cap[MAX_CLIPS];
	struct v4l2_clip		try_clips_cap[MAX_CLIPS];
	unsigned			clipcount_cap;

	/* Output */
	unsigned			output;
	v4l2_std_id			std_out;
	struct v4l2_dv_timings		dv_timings_out;
	u32				colorspace_out;
	u32				ycbcr_enc_out;
	u32				hsv_enc_out;
	u32				quantization_out;
	u32				xfer_func_out;
	u32				service_set_out;
	unsigned			bytesperline_out[TPG_MAX_PLANES];
	unsigned			tv_field_out;
	unsigned			tv_audio_output;
	bool				vbi_out_have_wss;
	u8				vbi_out_wss[2];
	bool				vbi_out_have_cc[2];
	u8				vbi_out_cc[2][2];
	bool				dvi_d_out;
	u8				*scaled_line;
	u8				*blended_line;
	unsigned			cur_scaled_line;

	/* Output Overlay */
	void				*fb_vbase_out;
	bool				overlay_out_enabled;
	int				overlay_out_top, overlay_out_left;
	void				*bitmap_out;
	struct v4l2_clip		clips_out[MAX_CLIPS];
	struct v4l2_clip		try_clips_out[MAX_CLIPS];
	unsigned			clipcount_out;
	unsigned			fbuf_out_flags;
	u32				chromakey_out;
	u8				global_alpha_out;

	/* video capture */
	struct tpg_data			tpg;
	unsigned			ms_vid_cap;
	bool				must_blank[VIDEO_MAX_FRAME];

	const struct vivid_fmt		*fmt_cap;
	struct v4l2_fract		timeperframe_vid_cap;
	enum v4l2_field			field_cap;
	struct v4l2_rect		src_rect;
	struct v4l2_rect		fmt_cap_rect;
	struct v4l2_rect		crop_cap;
	struct v4l2_rect		compose_cap;
	struct v4l2_rect		crop_bounds_cap;
	struct vb2_queue		vb_vid_cap_q;
	struct list_head		vid_cap_active;
	struct vb2_queue		vb_vbi_cap_q;
	struct list_head		vbi_cap_active;

	/* thread for generating video capture stream */
	struct task_struct		*kthread_vid_cap;
	unsigned long			jiffies_vid_cap;
	u32				cap_seq_offset;
	u32				cap_seq_count;
	bool				cap_seq_resync;
	u32				vid_cap_seq_start;
	u32				vid_cap_seq_count;
	bool				vid_cap_streaming;
	u32				vbi_cap_seq_start;
	u32				vbi_cap_seq_count;
	bool				vbi_cap_streaming;
	bool				stream_sliced_vbi_cap;

	/* video output */
	const struct vivid_fmt		*fmt_out;
	struct v4l2_fract		timeperframe_vid_out;
	enum v4l2_field			field_out;
	struct v4l2_rect		sink_rect;
	struct v4l2_rect		fmt_out_rect;
	struct v4l2_rect		crop_out;
	struct v4l2_rect		compose_out;
	struct v4l2_rect		compose_bounds_out;
	struct vb2_queue		vb_vid_out_q;
	struct list_head		vid_out_active;
	struct vb2_queue		vb_vbi_out_q;
	struct list_head		vbi_out_active;

	/* video loop precalculated rectangles */

	/*
	 * Intersection between what the output side composes and the capture side
	 * crops. I.e., what actually needs to be copied from the output buffer to
	 * the capture buffer.
	 */
	struct v4l2_rect		loop_vid_copy;
	/* The part of the output buffer that (after scaling) corresponds to loop_vid_copy. */
	struct v4l2_rect		loop_vid_out;
	/* The part of the capture buffer that (after scaling) corresponds to loop_vid_copy. */
	struct v4l2_rect		loop_vid_cap;
	/*
	 * The intersection of the framebuffer, the overlay output window and
	 * loop_vid_copy. I.e., the part of the framebuffer that actually should be
	 * blended with the compose_out rectangle. This uses the framebuffer origin.
	 */
	struct v4l2_rect		loop_fb_copy;
	/* The same as loop_fb_copy but with compose_out origin. */
	struct v4l2_rect		loop_vid_overlay;
	/*
	 * The part of the capture buffer that (after scaling) corresponds
	 * to loop_vid_overlay.
	 */
	struct v4l2_rect		loop_vid_overlay_cap;

	/* thread for generating video output stream */
	struct task_struct		*kthread_vid_out;
	unsigned long			jiffies_vid_out;
	u32				out_seq_offset;
	u32				out_seq_count;
	bool				out_seq_resync;
	u32				vid_out_seq_start;
	u32				vid_out_seq_count;
	bool				vid_out_streaming;
	u32				vbi_out_seq_start;
	u32				vbi_out_seq_count;
	bool				vbi_out_streaming;
	bool				stream_sliced_vbi_out;

	/* SDR capture */
	struct vb2_queue		vb_sdr_cap_q;
	struct list_head		sdr_cap_active;
	u32				sdr_pixelformat; /* v4l2 format id */
	unsigned			sdr_buffersize;
	unsigned			sdr_adc_freq;
	unsigned			sdr_fm_freq;
	unsigned			sdr_fm_deviation;
	int				sdr_fixp_src_phase;
	int				sdr_fixp_mod_phase;

	bool				tstamp_src_is_soe;
	bool				has_crop_cap;
	bool				has_compose_cap;
	bool				has_scaler_cap;
	bool				has_crop_out;
	bool				has_compose_out;
	bool				has_scaler_out;

	/* thread for generating SDR stream */
	struct task_struct		*kthread_sdr_cap;
	unsigned long			jiffies_sdr_cap;
	u32				sdr_cap_seq_offset;
	u32				sdr_cap_seq_count;
	bool				sdr_cap_seq_resync;

	/* RDS generator */
	struct vivid_rds_gen		rds_gen;

	/* Radio receiver */
	unsigned			radio_rx_freq;
	unsigned			radio_rx_audmode;
	int				radio_rx_sig_qual;
	unsigned			radio_rx_hw_seek_mode;
	bool				radio_rx_hw_seek_prog_lim;
	bool				radio_rx_rds_controls;
	bool				radio_rx_rds_enabled;
	unsigned			radio_rx_rds_use_alternates;
	unsigned			radio_rx_rds_last_block;
	struct v4l2_fh			*radio_rx_rds_owner;

	/* Radio transmitter */
	unsigned			radio_tx_freq;
	unsigned			radio_tx_subchans;
	bool				radio_tx_rds_controls;
	unsigned			radio_tx_rds_last_block;
	struct v4l2_fh			*radio_tx_rds_owner;

	/* Shared between radio receiver and transmitter */
	bool				radio_rds_loop;
	ktime_t				radio_rds_init_time;

	/* CEC */
	struct cec_adapter		*cec_rx_adap;
	struct cec_adapter		*cec_tx_adap[MAX_OUTPUTS];
	struct workqueue_struct		*cec_workqueue;
	spinlock_t			cec_slock;
	struct list_head		cec_work_list;
	unsigned int			cec_xfer_time_jiffies;
	unsigned long			cec_xfer_start_jiffies;
	u8				cec_output2bus_map[MAX_OUTPUTS];

	/* CEC OSD String */
	char				osd[14];
	unsigned long			osd_jiffies;
};

static inline bool vivid_is_webcam(const struct vivid_dev *dev)
{
	return dev->input_type[dev->input] == WEBCAM;
}

static inline bool vivid_is_tv_cap(const struct vivid_dev *dev)
{
	return dev->input_type[dev->input] == TV;
}

static inline bool vivid_is_svid_cap(const struct vivid_dev *dev)
{
	return dev->input_type[dev->input] == SVID;
}

static inline bool vivid_is_hdmi_cap(const struct vivid_dev *dev)
{
	return dev->input_type[dev->input] == HDMI;
}

static inline bool vivid_is_sdtv_cap(const struct vivid_dev *dev)
{
	return vivid_is_tv_cap(dev) || vivid_is_svid_cap(dev);
}

static inline bool vivid_is_svid_out(const struct vivid_dev *dev)
{
	return dev->output_type[dev->output] == SVID;
}

static inline bool vivid_is_hdmi_out(const struct vivid_dev *dev)
{
	return dev->output_type[dev->output] == HDMI;
}

#endif
