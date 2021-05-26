/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip CIF Driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */

#ifndef _RKCIF_DEV_H
#define _RKCIF_DEV_H

#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mc.h>
#include <linux/workqueue.h>
#include <linux/rk-camera-module.h>

#include "regs.h"
#include "version.h"
#include "cif-luma.h"
#include "mipi-csi2.h"
#include "hw.h"
#include "subdev-itf.h"

#define CIF_DRIVER_NAME		"rkcif"
#define CIF_VIDEODEVICE_NAME	"stream_cif"

#define OF_CIF_MONITOR_PARA	"rockchip,cif-monitor"
#define OF_CIF_WAIT_LINE	"wait-line"

#define CIF_MONITOR_PARA_NUM	(5)

#define RKCIF_SINGLE_STREAM	1
#define RKCIF_STREAM_CIF	0
#define CIF_DVP_VDEV_NAME CIF_VIDEODEVICE_NAME		"_dvp"
#define CIF_MIPI_ID0_VDEV_NAME CIF_VIDEODEVICE_NAME	"_mipi_id0"
#define CIF_MIPI_ID1_VDEV_NAME CIF_VIDEODEVICE_NAME	"_mipi_id1"
#define CIF_MIPI_ID2_VDEV_NAME CIF_VIDEODEVICE_NAME	"_mipi_id2"
#define CIF_MIPI_ID3_VDEV_NAME CIF_VIDEODEVICE_NAME	"_mipi_id3"

#define CIF_DVP_ID0_VDEV_NAME CIF_VIDEODEVICE_NAME	"_dvp_id0"
#define CIF_DVP_ID1_VDEV_NAME CIF_VIDEODEVICE_NAME	"_dvp_id1"
#define CIF_DVP_ID2_VDEV_NAME CIF_VIDEODEVICE_NAME	"_dvp_id2"
#define CIF_DVP_ID3_VDEV_NAME CIF_VIDEODEVICE_NAME	"_dvp_id3"

/*
 * RK1808 support 5 channel inputs simultaneously:
 * dvp + 4 mipi virtual channels;
 * RV1126/RK356X support 4 channels of BT.656/BT.1120/MIPI
 */
#define RKCIF_MULTI_STREAMS_NUM	5
#define RKCIF_STREAM_MIPI_ID0	0
#define RKCIF_STREAM_MIPI_ID1	1
#define RKCIF_STREAM_MIPI_ID2	2
#define RKCIF_STREAM_MIPI_ID3	3
#define RKCIF_MAX_STREAM_MIPI	4
#define RKCIF_MAX_STREAM_LVDS	4
#define RKCIF_MAX_STREAM_DVP	4
#define RKCIF_STREAM_DVP	4

#define RKCIF_MAX_SENSOR	2
#define RKCIF_MAX_CSI_CHANNEL	4
#define RKCIF_MAX_PIPELINE	4

#define RKCIF_DEFAULT_WIDTH	640
#define RKCIF_DEFAULT_HEIGHT	480
#define RKCIF_FS_DETECTED_NUM	2

/*
 * for HDR mode sync buf
 */
#define RDBK_MAX		3
#define RDBK_L			0
#define RDBK_M			1
#define RDBK_S			2

/*
 * for distinguishing cropping from senosr or usr
 */
#define CROP_SRC_SENSOR_MASK		(0x1 << 0)
#define CROP_SRC_USR_MASK		(0x1 << 1)

enum rkcif_workmode {
	RKCIF_WORKMODE_ONEFRAME = 0x00,
	RKCIF_WORKMODE_PINGPONG = 0x01,
	RKCIF_WORKMODE_LINELOOP = 0x02
};

enum rkcif_yuvaddr_state {
	RKCIF_YUV_ADDR_STATE_UPDATE = 0x0,
	RKCIF_YUV_ADDR_STATE_INIT = 0x1
};

enum rkcif_state {
	RKCIF_STATE_DISABLED,
	RKCIF_STATE_READY,
	RKCIF_STATE_STREAMING,
	RKCIF_STATE_RESET_IN_STREAMING,
};

enum host_type_t {
	RK_CSI_RXHOST,
	RK_DSI_RXHOST
};

enum rkcif_lvds_pad {
	RKCIF_LVDS_PAD_SINK = 0x0,
	RKCIF_LVDS_PAD_SRC_ID0,
	RKCIF_LVDS_PAD_SRC_ID1,
	RKCIF_LVDS_PAD_SRC_ID2,
	RKCIF_LVDS_PAD_SRC_ID3,
	RKCIF_LVDS_PAD_MAX
};

enum rkcif_lvds_state {
	RKCIF_LVDS_STOP = 0,
	RKCIF_LVDS_START,
};

enum rkcif_inf_id {
	RKCIF_DVP,
	RKCIF_MIPI_LVDS,
};

enum rkcif_clk_edge {
	RKCIF_CLK_RISING = 0x0,
	RKCIF_CLK_FALLING,
};

/*
 * for distinguishing cropping from senosr or usr
 */
enum rkcif_crop_src {
	CROP_SRC_ACT	= 0x0,
	CROP_SRC_SENSOR,
	CROP_SRC_USR,
	CROP_SRC_MAX
};

/*
 * struct rkcif_pipeline - An CIF hardware pipeline
 *
 * Capture device call other devices via pipeline
 *
 * @num_subdevs: number of linked subdevs
 * @power_cnt: pipeline power count
 * @stream_cnt: stream power count
 */
struct rkcif_pipeline {
	struct media_pipeline pipe;
	int num_subdevs;
	atomic_t power_cnt;
	atomic_t stream_cnt;
	struct v4l2_subdev *subdevs[RKCIF_MAX_PIPELINE];
	int (*open)(struct rkcif_pipeline *p,
		    struct media_entity *me, bool prepare);
	int (*close)(struct rkcif_pipeline *p);
	int (*set_stream)(struct rkcif_pipeline *p, bool on);
};

struct rkcif_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	union {
		u32 buff_addr[VIDEO_MAX_PLANES];
		void *vaddr[VIDEO_MAX_PLANES];
	};
};

extern int rkcif_debug;

/*
 * struct rkcif_sensor_info - Sensor infomations
 * @sd: v4l2 subdev of sensor
 * @mbus: media bus configuration
 * @fi: v4l2 subdev frame interval
 * @lanes: lane num of sensor
 * @raw_rect: raw output rectangle of sensor, not crop or selection
 * @selection: selection info of sensor
 */
struct rkcif_sensor_info {
	struct v4l2_subdev *sd;
	struct v4l2_mbus_config mbus;
	struct v4l2_subdev_frame_interval fi;
	int lanes;
	struct v4l2_rect raw_rect;
	struct v4l2_subdev_selection selection;
};

enum cif_fmt_type {
	CIF_FMT_TYPE_YUV = 0,
	CIF_FMT_TYPE_RAW,
};

/*
 * struct cif_output_fmt - The output format
 *
 * @bpp: bits per pixel for each cplanes
 * @fourcc: pixel format in fourcc
 * @fmt_val: the fmt val corresponding to CIF_FOR register
 * @csi_fmt_val: the fmt val corresponding to CIF_CSI_ID_CTRL
 * @cplanes: number of colour planes
 * @mplanes: number of planes for format
 * @raw_bpp: bits per pixel for raw format
 * @fmt_type: image format, raw or yuv
 */
struct cif_output_fmt {
	u8 bpp[VIDEO_MAX_PLANES];
	u32 fourcc;
	u32 fmt_val;
	u32 csi_fmt_val;
	u8 cplanes;
	u8 mplanes;
	u8 raw_bpp;
	enum cif_fmt_type fmt_type;
};

/*
 * struct cif_input_fmt - The input mbus format from sensor
 *
 * @mbus_code: mbus format
 * @dvp_fmt_val: the fmt val corresponding to CIF_FOR register
 * @csi_fmt_val: the fmt val corresponding to CIF_CSI_ID_CTRL
 * @fmt_type: image format, raw or yuv
 * @field: the field type of the input from sensor
 */
struct cif_input_fmt {
	u32 mbus_code;
	u32 dvp_fmt_val;
	u32 csi_fmt_val;
	u32 csi_yuv_order;
	enum cif_fmt_type fmt_type;
	enum v4l2_field field;
};

struct csi_channel_info {
	unsigned char id;
	unsigned char enable;	/* capture enable */
	unsigned char vc;
	unsigned char data_type;
	unsigned char crop_en;
	unsigned char cmd_mode_en;
	unsigned char fmt_val;
	unsigned int width;
	unsigned int height;
	unsigned int virtual_width;
	unsigned int crop_st_x;
	unsigned int crop_st_y;
	struct rkmodule_lvds_cfg lvds_cfg;
};

struct rkcif_vdev_node {
	struct vb2_queue buf_queue;
	/* vfd lock */
	struct mutex vlock;
	struct video_device vdev;
	struct media_pad pad;
};

/*
 * the mark that csi frame0/1 interrupts enabled
 * in CIF_MIPI_INTEN
 */
enum cif_frame_ready {
	CIF_CSI_FRAME0_READY = 0x1,
	CIF_CSI_FRAME1_READY
};

/* struct rkcif_hdr - hdr configured
 * @op_mode: hdr optional mode
 */
struct rkcif_hdr {
	u8 mode;
};

/* struct rkcif_fps_stats - take notes on timestamp of buf
 * @frm0_timestamp: timesstamp of buf in frm0
 * @frm1_timestamp: timesstamp of buf in frm1
 */
struct rkcif_fps_stats {
	u64 frm0_timestamp;
	u64 frm1_timestamp;
};

/* struct rkcif_fps_stats - take notes on timestamp of buf
 * @fs_timestamp: timesstamp of frame start
 * @fe_timestamp: timesstamp of frame end
 * @wk_timestamp: timesstamp of buf send to user in wake up mode
 * @readout_time: one frame of readout time
 * @early_time: early time of buf send to user
 * @total_time: totaltime of readout time in hdr
 */
struct rkcif_readout_stats {
	u64 fs_timestamp;
	u64 fe_timestamp;
	u64 wk_timestamp;
	u64 readout_time;
	u64 early_time;
	u64 total_time;
};

/* struct rkcif_irq_stats - take notes on irq number
 * @csi_overflow_cnt: count of csi overflow irq
 * @csi_bwidth_lack_cnt: count of csi bandwidth lack irq
 * @dvp_bus_err_cnt: count of dvp bus err irq
 * @dvp_overflow_cnt: count dvp overflow irq
 * @dvp_line_err_cnt: count dvp line err irq
 * @dvp_pix_err_cnt: count dvp pix err irq
 * @all_frm_end_cnt: raw frame end count
 * @all_err_cnt: all err count
 * @
 */
struct rkcif_irq_stats {
	u64 csi_overflow_cnt;
	u64 csi_bwidth_lack_cnt;
	u64 dvp_bus_err_cnt;
	u64 dvp_overflow_cnt;
	u64 dvp_line_err_cnt;
	u64 dvp_pix_err_cnt;
	u64 all_frm_end_cnt;
	u64 all_err_cnt;
};

/*
 * the detecting mode of cif reset timer
 * related with dts property:rockchip,cif-monitor
 */
enum rkcif_monitor_mode {
	RKCIF_MONITOR_MODE_IDLE = 0x0,
	RKCIF_MONITOR_MODE_CONTINUE,
	RKCIF_MONITOR_MODE_TRIGGER,
	RKCIF_MONITOR_MODE_HOTPLUG,
};

/*
 * the parameters to resume when reset cif in running
 */
struct rkcif_resume_info {
	u32 frm_sync_seq;
};

struct rkcif_work_struct {
	struct work_struct	work;
	enum rkmodule_reset_src	reset_src;
	struct rkcif_resume_info	resume_info;
};

struct rkcif_timer {
	struct timer_list	timer;
	spinlock_t		timer_lock;
	spinlock_t		csi2_err_lock;
	unsigned long		cycle;
	/* unit: us */
	unsigned long		line_end_cycle;
	unsigned int		run_cnt;
	unsigned int		max_run_cnt;
	unsigned int		stop_index_of_run_cnt;
	unsigned int		last_buf_wakeup_cnt;
	unsigned long		csi2_err_cnt_even;
	unsigned long		csi2_err_cnt_odd;
	unsigned int		csi2_err_ref_cnt;
	unsigned int		csi2_err_fs_fe_cnt;
	unsigned int		csi2_err_fs_fe_detect_cnt;
	unsigned int		frm_num_of_monitor_cycle;
	unsigned int		triggered_frame_num;
	unsigned int		vts;
	unsigned int		raw_height;
	/* unit: ms */
	unsigned int		err_time_interval;
	unsigned int		csi2_err_triggered_cnt;
	unsigned int		notifer_called_cnt;
	unsigned long		frame_end_cycle_us;
	u64			csi2_first_err_timestamp;
	bool			is_triggered;
	bool			is_buf_stop_update;
	bool			is_running;
	bool			is_csi2_err_occurred;
	bool			has_been_init;
	enum rkcif_monitor_mode	monitor_mode;
	enum rkmodule_reset_src	reset_src;
};

struct rkcif_extend_info {
	struct v4l2_pix_format_mplane	pixm;
	bool is_extended;
};

/*
 * struct rkcif_stream - Stream states TODO
 *
 * @vbq_lock: lock to protect buf_queue
 * @buf_queue: queued buffer list
 * @dummy_buf: dummy space to store dropped data
 * @crop_enable: crop status when stream off
 * @crop_dyn_en: crop status when streaming
 * rkcif use shadowsock registers, so it need two buffer at a time
 * @curr_buf: the buffer used for current frame
 * @next_buf: the buffer used for next frame
 * @fps_lock: to protect parameters about calculating fps
 */
struct rkcif_stream {
	unsigned id:3;
	struct rkcif_device		*cifdev;
	struct rkcif_vdev_node		vnode;
	enum rkcif_state		state;
	wait_queue_head_t		wq_stopped;
	unsigned int			frame_idx;
	int				frame_phase;
	unsigned int			crop_mask;
	/* lock between irq and buf_queue */
	struct list_head		buf_head;
	struct rkcif_buffer		*curr_buf;
	struct rkcif_buffer		*next_buf;

	spinlock_t vbq_lock; /* vfd lock */
	spinlock_t fps_lock;
	/* TODO: pad for dvp and mipi separately? */
	struct media_pad		pad;

	const struct cif_output_fmt	*cif_fmt_out;
	const struct cif_input_fmt	*cif_fmt_in;
	struct v4l2_pix_format_mplane	pixm;
	struct v4l2_rect		crop[CROP_SRC_MAX];
	struct rkcif_fps_stats		fps_stats;
	struct rkcif_extend_info	extend_line;
	struct rkcif_readout_stats	readout;
	unsigned int			fs_cnt_in_single_frame;
	u64				line_int_cnt;
	bool				stopping;
	bool				crop_enable;
	bool				crop_dyn_en;
	bool				is_compact;
	bool				is_dvp_yuv_addr_init;
	bool				is_fs_fe_not_paired;
	bool				is_line_wake_up;
	bool				is_line_inten;
	bool				is_can_stop;
	bool				is_buf_active;
};

struct rkcif_lvds_subdev {
	struct rkcif_device	*cifdev;
	struct v4l2_subdev sd;
	struct v4l2_subdev *remote_sd;
	struct media_pad pads[RKCIF_LVDS_PAD_MAX];
	struct v4l2_mbus_framefmt in_fmt;
	struct v4l2_rect crop;
	const struct cif_output_fmt	*cif_fmt_out;
	const struct cif_input_fmt	*cif_fmt_in;
	enum rkcif_lvds_state		state;
	struct rkcif_sensor_info	sensor_self;
	atomic_t			frm_sync_seq;
};

struct rkcif_dvp_sof_subdev {
	struct rkcif_device *cifdev;
	struct v4l2_subdev sd;
	atomic_t			frm_sync_seq;
};

static inline struct rkcif_buffer *to_rkcif_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct rkcif_buffer, vb);
}

static inline
struct rkcif_vdev_node *vdev_to_node(struct video_device *vdev)
{
	return container_of(vdev, struct rkcif_vdev_node, vdev);
}

static inline
struct rkcif_stream *to_rkcif_stream(struct rkcif_vdev_node *vnode)
{
	return container_of(vnode, struct rkcif_stream, vnode);
}

static inline struct rkcif_vdev_node *queue_to_node(struct vb2_queue *q)
{
	return container_of(q, struct rkcif_vdev_node, buf_queue);
}

static inline struct vb2_queue *to_vb2_queue(struct file *file)
{
	struct rkcif_vdev_node *vnode = video_drvdata(file);

	return &vnode->buf_queue;
}

/*
 * struct rkcif_device - ISP platform device
 * @base_addr: base register address
 * @active_sensor: sensor in-use, set when streaming on
 * @stream: capture video device
 */
struct rkcif_device {
	struct list_head		list;
	struct device			*dev;
	struct v4l2_device		v4l2_dev;
	struct media_device		media_dev;
	struct v4l2_async_notifier	notifier;

	struct rkcif_sensor_info	sensors[RKCIF_MAX_SENSOR];
	u32				num_sensors;
	struct rkcif_sensor_info	*active_sensor;
	struct rkcif_sensor_info	terminal_sensor;

	struct rkcif_stream		stream[RKCIF_MULTI_STREAMS_NUM];
	struct rkcif_pipeline		pipe;

	struct csi_channel_info		channels[RKCIF_MAX_CSI_CHANNEL];
	int				num_channels;
	int				chip_id;
	atomic_t			stream_cnt;
	atomic_t			fh_cnt;
	struct mutex			stream_lock; /* lock between streams */
	enum rkcif_workmode		workmode;
	bool				can_be_reset;
	struct rkcif_hdr		hdr;
	struct rkcif_buffer		*rdbk_buf[RDBK_MAX];
	struct rkcif_luma_vdev		luma_vdev;
	struct rkcif_lvds_subdev	lvds_subdev;
	struct rkcif_dvp_sof_subdev	dvp_sof_subdev;
	struct rkcif_hw *hw_dev;
	irqreturn_t (*isr_hdl)(int irq, struct rkcif_device *cif_dev);
	int inf_id;

	struct sditf_priv		*sditf;
	struct proc_dir_entry		*proc_dir;
	struct rkcif_irq_stats		irq_stats;
	spinlock_t			hdr_lock; /* lock for hdr buf sync */
	struct rkcif_timer		reset_watchdog_timer;
	unsigned int			buf_wake_up_cnt;
	struct notifier_block		reset_notifier; /* reset for mipi csi crc err */
	struct rkcif_work_struct	reset_work;
	unsigned int			dvp_sof_in_oneframe;
	unsigned int			wait_line;
	unsigned int			wait_line_bak;
	unsigned int			wait_line_cache;
	bool				is_start_hdr;
	bool				reset_work_cancel;
	bool				iommu_en;
};

extern struct platform_driver rkcif_plat_drv;

void rkcif_write_register(struct rkcif_device *dev,
			  enum cif_reg_index index, u32 val);
void rkcif_write_register_or(struct rkcif_device *dev,
			     enum cif_reg_index index, u32 val);
void rkcif_write_register_and(struct rkcif_device *dev,
			      enum cif_reg_index index, u32 val);
unsigned int rkcif_read_register(struct rkcif_device *dev,
				 enum cif_reg_index index);
void rkcif_write_grf_reg(struct rkcif_device *dev,
			 enum cif_reg_index index, u32 val);
u32 rkcif_read_grf_reg(struct rkcif_device *dev,
		       enum cif_reg_index index);
void rkcif_unregister_stream_vdevs(struct rkcif_device *dev,
				   int stream_num);
int rkcif_register_stream_vdevs(struct rkcif_device *dev,
				int stream_num,
				bool is_multi_input);
void rkcif_stream_init(struct rkcif_device *dev, u32 id);
void rkcif_irq_oneframe(struct rkcif_device *cif_dev);
void rkcif_irq_pingpong(struct rkcif_device *cif_dev);
void rkcif_soft_reset(struct rkcif_device *cif_dev,
		      bool is_rst_iommu);
int rkcif_register_lvds_subdev(struct rkcif_device *dev);
void rkcif_unregister_lvds_subdev(struct rkcif_device *dev);
int rkcif_register_dvp_sof_subdev(struct rkcif_device *dev);
void rkcif_unregister_dvp_sof_subdev(struct rkcif_device *dev);
void rkcif_irq_lite_lvds(struct rkcif_device *cif_dev);
u32 rkcif_get_sof(struct rkcif_device *cif_dev);
int rkcif_plat_init(struct rkcif_device *cif_dev, struct device_node *node, int inf_id);
int rkcif_plat_uninit(struct rkcif_device *cif_dev);
int rkcif_attach_hw(struct rkcif_device *cif_dev);
int rkcif_update_sensor_info(struct rkcif_stream *stream);
int rkcif_reset_notifier(struct notifier_block *nb, unsigned long action, void *data);
void rkcif_reset_watchdog_timer_handler(struct timer_list *t);
void rkcif_config_dvp_clk_sampling_edge(struct rkcif_device *dev,
					enum rkcif_clk_edge edge);
void rkcif_enable_dvp_clk_dual_edge(struct rkcif_device *dev, bool on);
void rkcif_reset_work(struct work_struct *work);
#endif
