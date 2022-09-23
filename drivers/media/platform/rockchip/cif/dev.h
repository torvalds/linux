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
#include <linux/rkcif-config.h>
#include <linux/soc/rockchip/rockchip_thunderboot_service.h>

#include "regs.h"
#include "version.h"
#include "cif-luma.h"
#include "mipi-csi2.h"
#include "hw.h"
#include "subdev-itf.h"

#if IS_ENABLED(CONFIG_CPU_RV1106)
#include <linux/soc/rockchip/rk_sdmmc.h>
#endif

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

#define RKCIF_PLANE_Y		0
#define RKCIF_PLANE_CBCR	1

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

#define RKCIF_MAX_INTERVAL_NS	5000000
/*
 * for HDR mode sync buf
 */
#define RDBK_MAX		3
#define RDBK_TOISP_MAX		2
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

enum rkcif_stream_mode {
	RKCIF_STREAM_MODE_NONE = 0x0,
	RKCIF_STREAM_MODE_CAPTURE = 0x01,
	RKCIF_STREAM_MODE_TOISP = 0x02,
	RKCIF_STREAM_MODE_TOSCALE = 0x04,
	RKCIF_STREAM_MODE_TOISP_RDBK = 0x08
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

enum rkcif_lvds_pad {
	RKCIF_LVDS_PAD_SINK = 0x0,
	RKCIF_LVDS_PAD_SRC_ID0,
	RKCIF_LVDS_PAD_SRC_ID1,
	RKCIF_LVDS_PAD_SRC_ID2,
	RKCIF_LVDS_PAD_SRC_ID3,
	RKCIF_LVDS_PAD_SCL_ID0,
	RKCIF_LVDS_PAD_SCL_ID1,
	RKCIF_LVDS_PAD_SCL_ID2,
	RKCIF_LVDS_PAD_SCL_ID3,
	RKCIF_LVDS_PAD_MAX,
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
	struct dma_buf *dbuf;
};

struct rkcif_dummy_buffer {
	struct list_head list;
	struct dma_buf *dbuf;
	dma_addr_t dma_addr;
	struct page **pages;
	void *mem_priv;
	void *vaddr;
	u32 size;
	int dma_fd;
	bool is_need_vaddr;
	bool is_need_dbuf;
	bool is_need_dmafd;
	bool is_free;
};

struct rkcif_tools_buffer {
	struct vb2_v4l2_buffer *vb;
	struct list_head list;
	int use_cnt;
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
	int dsi_input_en;
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
	unsigned char data_bit;
	unsigned char crop_en;
	unsigned char cmd_mode_en;
	unsigned char fmt_val;
	unsigned char csi_fmt_val;
	unsigned int width;
	unsigned int height;
	unsigned int virtual_width;
	unsigned int crop_st_x;
	unsigned int crop_st_y;
	unsigned int dsi_input;
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
	u64 csi_size_err_cnt;
	u64 dvp_bus_err_cnt;
	u64 dvp_overflow_cnt;
	u64 dvp_line_err_cnt;
	u64 dvp_pix_err_cnt;
	u64 dvp_size_err_cnt;
	u64 dvp_bwidth_lack_cnt;
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
	unsigned int		last_buf_wakeup_cnt[RKCIF_MAX_STREAM_MIPI];
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
	bool			is_ctrl_by_user;
	enum rkcif_monitor_mode	monitor_mode;
	enum rkmodule_reset_src	reset_src;
};

struct rkcif_extend_info {
	struct v4l2_pix_format_mplane	pixm;
	bool is_extended;
};

enum rkcif_capture_mode {
	RKCIF_TO_DDR = 0,
	RKCIF_TO_ISP_DDR,
	RKCIF_TO_ISP_DMA,
};

/*
 * list: used for buf rotation
 * list_free: only used to release buf asynchronously
 */
struct rkcif_rx_buffer {
	int buf_idx;
	struct list_head list;
	struct list_head list_free;
	struct rkisp_rx_buf dbufs;
	struct rkcif_dummy_buffer dummy;
	struct rkisp_thunderboot_shmem shmem;
};

enum rkcif_dma_en_mode {
	RKCIF_DMAEN_BY_VICAP = 0x1,
	RKCIF_DMAEN_BY_ISP = 0x2,
	RKCIF_DMAEN_BY_VICAP_TO_ISP = 0x4,
	RKCIF_DMAEN_BY_ISP_TO_VICAP = 0x8,
};

struct rkcif_skip_info {
	u8 cap_m;
	u8 skip_n;
	bool skip_en;
	bool skip_to_en;
	bool skip_to_dis;
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
	int				frame_phase_cache;
	unsigned int			crop_mask;
	/* lock between irq and buf_queue */
	struct list_head		buf_head;
	struct rkcif_buffer		*curr_buf;
	struct rkcif_buffer		*next_buf;
	struct rkcif_rx_buffer		*curr_buf_toisp;
	struct rkcif_rx_buffer		*next_buf_toisp;

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
	unsigned int			capture_mode;
	struct rkcif_scale_vdev		*scale_vdev;
	struct rkcif_tools_vdev		*tools_vdev;
	int				dma_en;
	int				to_en_dma;
	int				to_stop_dma;
	int				buf_owner;
	int				buf_replace_cnt;
	struct list_head		rx_buf_head_vicap;
	unsigned int			cur_stream_mode;
	struct rkcif_rx_buffer		rx_buf[RKISP_VICAP_BUF_CNT_MAX];
	struct list_head		rx_buf_head;
	int				buf_num_toisp;
	u64				line_int_cnt;
	int				lack_buf_cnt;
	unsigned int                    buf_wake_up_cnt;
	struct rkcif_skip_info		skip_info;
	int				last_rx_buf_idx;
	int				last_frame_idx;
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
	bool				is_high_align;
	bool				to_en_scale;
	bool				is_finish_stop_dma;
	bool				is_in_vblank;
	bool				is_change_toisp;
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

#define SCALE_DRIVER_NAME		"rkcif_scale"

#define RKCIF_SCALE_CH0		0
#define RKCIF_SCALE_CH1		1
#define RKCIF_SCALE_CH2		2
#define RKCIF_SCALE_CH3		3
#define RKCIF_MAX_SCALE_CH	4

#define CIF_SCALE_CH0_VDEV_NAME CIF_DRIVER_NAME	"_scale_ch0"
#define CIF_SCALE_CH1_VDEV_NAME CIF_DRIVER_NAME	"_scale_ch1"
#define CIF_SCALE_CH2_VDEV_NAME CIF_DRIVER_NAME	"_scale_ch2"
#define CIF_SCALE_CH3_VDEV_NAME CIF_DRIVER_NAME	"_scale_ch3"

#define RKCIF_SCALE_ENUM_SIZE_MAX	3
#define RKCIF_MAX_SDITF			4

enum scale_ch_sw {
	SCALE_MIPI0_ID0,
	SCALE_MIPI0_ID1,
	SCALE_MIPI0_ID2,
	SCALE_MIPI0_ID3,
	SCALE_MIPI1_ID0,
	SCALE_MIPI1_ID1,
	SCALE_MIPI1_ID2,
	SCALE_MIPI1_ID3,
	SCALE_MIPI2_ID0,
	SCALE_MIPI2_ID1,
	SCALE_MIPI2_ID2,
	SCALE_MIPI2_ID3,
	SCALE_MIPI3_ID0,
	SCALE_MIPI3_ID1,
	SCALE_MIPI3_ID2,
	SCALE_MIPI3_ID3,
	SCALE_MIPI4_ID0,
	SCALE_MIPI4_ID1,
	SCALE_MIPI4_ID2,
	SCALE_MIPI4_ID3,
	SCALE_MIPI5_ID0,
	SCALE_MIPI5_ID1,
	SCALE_MIPI5_ID2,
	SCALE_MIPI5_ID3,
	SCALE_DVP,
	SCALE_CH_MAX,
};

enum scale_mode {
	SCALE_8TIMES,
	SCALE_16TIMES,
	SCALE_32TIMES,
};

struct rkcif_scale_ch_info {
	u32 width;
	u32 height;
	u32 vir_width;
};

struct rkcif_scale_src_res {
	u32 width;
	u32 height;
};

/*
 * struct rkcif_scale_vdev - CIF Capture device
 *
 * @irq_lock: buffer queue lock
 * @stat: stats buffer list
 * @readout_wq: workqueue for statistics information read
 */
struct rkcif_scale_vdev {
	unsigned int ch:3;
	struct rkcif_device *cifdev;
	struct rkcif_vdev_node vnode;
	struct rkcif_stream *stream;
	struct list_head buf_head;
	spinlock_t vbq_lock; /* vfd lock */
	wait_queue_head_t wq_stopped;
	struct v4l2_pix_format_mplane	pixm;
	const struct cif_output_fmt *scale_out_fmt;
	struct rkcif_scale_ch_info ch_info;
	struct rkcif_scale_src_res src_res;
	struct rkcif_buffer *curr_buf;
	struct rkcif_buffer *next_buf;
	struct bayer_blc blc;
	enum rkcif_state state;
	unsigned int ch_src;
	unsigned int scale_mode;
	int frame_phase;
	unsigned int frame_idx;
	bool stopping;
};

static inline
struct rkcif_scale_vdev *to_rkcif_scale_vdev(struct rkcif_vdev_node *vnode)
{
	return container_of(vnode, struct rkcif_scale_vdev, vnode);
}

void rkcif_init_scale_vdev(struct rkcif_device *cif_dev, u32 ch);
int rkcif_register_scale_vdevs(struct rkcif_device *cif_dev,
				int stream_num,
				bool is_multi_input);
void rkcif_unregister_scale_vdevs(struct rkcif_device *cif_dev,
				   int stream_num);

#define TOOLS_DRIVER_NAME		"rkcif_tools"

#define RKCIF_TOOLS_CH0		0
#define RKCIF_TOOLS_CH1		1
#define RKCIF_TOOLS_CH2		2
#define RKCIF_MAX_TOOLS_CH	3

#define CIF_TOOLS_CH0_VDEV_NAME CIF_DRIVER_NAME	"_tools_id0"
#define CIF_TOOLS_CH1_VDEV_NAME CIF_DRIVER_NAME	"_tools_id1"
#define CIF_TOOLS_CH2_VDEV_NAME CIF_DRIVER_NAME	"_tools_id2"

struct rkcif_tools_work_struct {
	struct work_struct	work;
	struct rkcif_buffer *active_buf;
	unsigned int frame_idx;
	unsigned long timestamp;
};

/*
 * struct rkcif_tools_vdev - CIF Capture device
 *
 * @irq_lock: buffer queue lock
 * @stat: stats buffer list
 * @readout_wq: workqueue for statistics information read
 */
struct rkcif_tools_vdev {
	unsigned int ch:3;
	struct rkcif_device *cifdev;
	struct rkcif_vdev_node vnode;
	struct rkcif_stream *stream;
	struct list_head buf_head;
	struct list_head src_buf_head;
	spinlock_t vbq_lock; /* vfd lock */
	wait_queue_head_t wq_stopped;
	struct v4l2_pix_format_mplane	pixm;
	const struct cif_output_fmt *tools_out_fmt;
	struct rkcif_buffer *curr_buf;
	struct rkcif_tools_work_struct tools_work;
	enum rkcif_state state;
	int frame_phase;
	unsigned int frame_idx;
	bool stopping;
};

static inline
struct rkcif_tools_vdev *to_rkcif_tools_vdev(struct rkcif_vdev_node *vnode)
{
	return container_of(vnode, struct rkcif_tools_vdev, vnode);
}

void rkcif_init_tools_vdev(struct rkcif_device *cif_dev, u32 ch);
int rkcif_register_tools_vdevs(struct rkcif_device *cif_dev,
				int stream_num,
				bool is_multi_input);
void rkcif_unregister_tools_vdevs(struct rkcif_device *cif_dev,
				   int stream_num);

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
	struct rkcif_scale_vdev		scale_vdev[RKCIF_MULTI_STREAMS_NUM];
	struct rkcif_tools_vdev		tools_vdev[RKCIF_MAX_TOOLS_CH];
	struct rkcif_pipeline		pipe;

	struct csi_channel_info		channels[RKCIF_MAX_CSI_CHANNEL];
	int				num_channels;
	int				chip_id;
	atomic_t			stream_cnt;
	atomic_t			power_cnt;
	struct mutex			stream_lock; /* lock between streams */
	struct mutex			scale_lock; /* lock between scale dev */
	struct mutex                    tools_lock; /* lock between tools dev */
	enum rkcif_workmode		workmode;
	bool				can_be_reset;
	struct rkmodule_hdr_cfg		hdr;
	struct rkcif_buffer		*rdbk_buf[RDBK_MAX];
	struct rkcif_rx_buffer		*rdbk_rx_buf[RDBK_MAX];
	struct rkcif_luma_vdev		luma_vdev;
	struct rkcif_lvds_subdev	lvds_subdev;
	struct rkcif_dvp_sof_subdev	dvp_sof_subdev;
	struct rkcif_hw *hw_dev;
	irqreturn_t (*isr_hdl)(int irq, struct rkcif_device *cif_dev);
	int inf_id;

	struct sditf_priv		*sditf[RKCIF_MAX_SDITF];
	struct proc_dir_entry		*proc_dir;
	struct rkcif_irq_stats		irq_stats;
	spinlock_t			hdr_lock; /* lock for hdr buf sync */
	spinlock_t			buffree_lock;
	struct rkcif_timer		reset_watchdog_timer;
	struct rkcif_work_struct	reset_work;
	int				id_use_cnt;
	unsigned int			csi_host_idx;
	unsigned int			dvp_sof_in_oneframe;
	unsigned int			wait_line;
	unsigned int			wait_line_bak;
	unsigned int			wait_line_cache;
	struct rkcif_dummy_buffer	dummy_buf;
	struct completion		cmpl_ntf;
	struct csi2_dphy_hw		*dphy_hw;
	phys_addr_t			resmem_pa;
	size_t				resmem_size;
	struct rk_tb_client		tb_client;
	bool				is_start_hdr;
	bool				reset_work_cancel;
	bool				iommu_en;
	bool				is_use_dummybuf;
	bool				is_notifier_isp;
	bool				is_thunderboot;
	int				rdbk_debug;
	int				sync_type;
	int				sditf_cnt;
	u32				early_line;
	int				isp_runtime_max;
	int				sensor_linetime;
};

extern struct platform_driver rkcif_plat_drv;
void rkcif_set_fps(struct rkcif_stream *stream, struct rkcif_fps *fps);
int rkcif_do_start_stream(struct rkcif_stream *stream,
				enum rkcif_stream_mode mode);
void rkcif_do_stop_stream(struct rkcif_stream *stream,
				enum rkcif_stream_mode mode);
void rkcif_irq_handle_scale(struct rkcif_device *cif_dev,
				  unsigned int intstat_glb);
void rkcif_buf_queue(struct vb2_buffer *vb);
void rkcif_vb_done_oneframe(struct rkcif_stream *stream,
				  struct vb2_v4l2_buffer *vb_done);

int rkcif_scale_start(struct rkcif_scale_vdev *scale_vdev);

const struct
cif_input_fmt *get_input_fmt(struct v4l2_subdev *sd,
				 struct v4l2_rect *rect,
				 u32 pad_id, struct csi_channel_info *csi_info);

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
void rkcif_set_default_fmt(struct rkcif_device *cif_dev);
void rkcif_irq_oneframe(struct rkcif_device *cif_dev);
void rkcif_irq_pingpong(struct rkcif_device *cif_dev);
void rkcif_irq_pingpong_v1(struct rkcif_device *cif_dev);
unsigned int rkcif_irq_global(struct rkcif_device *cif_dev);
void rkcif_irq_handle_toisp(struct rkcif_device *cif_dev, unsigned int intstat_glb);
int rkcif_register_lvds_subdev(struct rkcif_device *dev);
void rkcif_unregister_lvds_subdev(struct rkcif_device *dev);
int rkcif_register_dvp_sof_subdev(struct rkcif_device *dev);
void rkcif_unregister_dvp_sof_subdev(struct rkcif_device *dev);
void rkcif_irq_lite_lvds(struct rkcif_device *cif_dev);
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

int rkcif_init_rx_buf(struct rkcif_stream *stream, int buf_num);
void rkcif_free_rx_buf(struct rkcif_stream *stream, int buf_num);

int rkcif_set_fmt(struct rkcif_stream *stream,
		       struct v4l2_pix_format_mplane *pixm,
		       bool try);
void rkcif_enable_dma_capture(struct rkcif_stream *stream, bool is_only_enable);

void rkcif_do_soft_reset(struct rkcif_device *dev);

u32 rkcif_mbus_pixelcode_to_v4l2(u32 pixelcode);

void rkcif_config_dvp_pin(struct rkcif_device *dev, bool on);

s32 rkcif_get_sensor_vblank_def(struct rkcif_device *dev);
s32 rkcif_get_sensor_vblank(struct rkcif_device *dev);
int rkcif_get_linetime(struct rkcif_stream *stream);

void rkcif_assign_check_buffer_update_toisp(struct rkcif_stream *stream);

struct rkcif_rx_buffer *to_cif_rx_buf(struct rkisp_rx_buf *dbufs);

#endif
