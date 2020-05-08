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
#include "regs.h"
#include "version.h"

#define CIF_DRIVER_NAME		"rkcif"
#define CIF_VIDEODEVICE_NAME	"stream_cif"

#define RKCIF_SINGLE_STREAM	1
#define RKCIF_STREAM_CIF	0
#define CIF_DVP_VDEV_NAME CIF_VIDEODEVICE_NAME		"_dvp"
#define CIF_MIPI_ID0_VDEV_NAME CIF_VIDEODEVICE_NAME	"_mipi_id0"
#define CIF_MIPI_ID1_VDEV_NAME CIF_VIDEODEVICE_NAME	"_mipi_id1"
#define CIF_MIPI_ID2_VDEV_NAME CIF_VIDEODEVICE_NAME	"_mipi_id2"
#define CIF_MIPI_ID3_VDEV_NAME CIF_VIDEODEVICE_NAME	"_mipi_id3"

/*
 * Rk1808 support 5 channel inputs simultaneously:
 * dvp + 4 mipi virtual channels
 */
#define RKCIF_MULTI_STREAMS_NUM	5
#define RKCIF_STREAM_MIPI_ID0	0
#define RKCIF_STREAM_MIPI_ID1	1
#define RKCIF_STREAM_MIPI_ID2	2
#define RKCIF_STREAM_MIPI_ID3	3
#define RKCIF_MAX_STREAM_MIPI	4
#define RKCIF_STREAM_DVP	4

#define RKCIF_MAX_BUS_CLK	8
#define RKCIF_MAX_SENSOR	2
#define RKCIF_MAX_RESET		15
#define RKCIF_MAX_CSI_CHANNEL	4
#define RKCIF_MAX_PIPELINE	4

#define RKCIF_DEFAULT_WIDTH	640
#define RKCIF_DEFAULT_HEIGHT	480

#define write_cif_reg(base, addr, val) writel(val, (addr) + (base))
#define read_cif_reg(base, addr) readl((addr) + (base))
#define write_cif_reg_or(base, addr, val) \
	writel(readl((addr) + (base)) | (val), (addr) + (base))
#define write_cif_reg_and(base, addr, val) \
	writel(readl((addr) + (base)) & (val), (addr) + (base))

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
	RKCIF_STATE_STREAMING
};

enum rkcif_chip_id {
	CHIP_PX30_CIF,
	CHIP_RK1808_CIF,
	CHIP_RK3128_CIF,
	CHIP_RK3288_CIF,
	CHIP_RK3328_CIF,
};

enum host_type_t {
	RK_CSI_RXHOST,
	RK_DSI_RXHOST
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

struct rkcif_dummy_buffer {
	void *vaddr;
	dma_addr_t dma_addr;
	u32 size;
};

extern int rkcif_debug;

/*
 * struct rkcif_sensor_info - Sensor infomations
 * @mbus: media bus configuration
 */
struct rkcif_sensor_info {
	struct v4l2_subdev *sd;
	struct v4l2_mbus_config mbus;
	int lanes;
};

/*
 * struct cif_output_fmt - The output format
 *
 * @fourcc: pixel format in fourcc
 * @cplanes: number of colour planes
 * @fmt_val: the fmt val corresponding to CIF_FOR register
 * @bpp: bits per pixel for each cplanes
 */
struct cif_output_fmt {
	u32 fourcc;
	u8 cplanes;
	u8 mplanes;
	u32 fmt_val;
	u8 bpp[VIDEO_MAX_PLANES];
};

enum cif_fmt_type {
	CIF_FMT_TYPE_YUV = 0,
	CIF_FMT_TYPE_RAW,
};

/*
 * struct cif_input_fmt - The input mbus format from sensor
 *
 * @mbus_code: mbus format
 * @dvp_fmt_val: the fmt val corresponding to CIF_FOR register
 * @csi_fmt_val: the fmt val corresponding to CIF_CSI_ID_CTRL
 * @field: the field type of the input from sensor
 */
struct cif_input_fmt {
	u32 mbus_code;
	u32 dvp_fmt_val;
	u32 csi_fmt_val;
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

/*
 * struct rkcif_stream - Stream states TODO
 *
 * @vbq_lock: lock to protect buf_queue
 * @buf_queue: queued buffer list
 * @dummy_buf: dummy space to store dropped data
 *
 * rkcif use shadowsock registers, so it need two buffer at a time
 * @curr_buf: the buffer used for current frame
 * @next_buf: the buffer used for next frame
 */
struct rkcif_stream {
	unsigned id:3;
	struct rkcif_device		*cifdev;
	struct rkcif_vdev_node		vnode;
	enum rkcif_state		state;
	bool				stopping;
	wait_queue_head_t		wq_stopped;
	int				frame_idx;
	int				frame_phase;

	/* lock between irq and buf_queue */
	struct list_head		buf_head;
	struct rkcif_dummy_buffer	dummy_buf;
	struct rkcif_buffer		*curr_buf;
	struct rkcif_buffer		*next_buf;

	spinlock_t vbq_lock; /* vfd lock */
	/* TODO: pad for dvp and mipi separately? */
	struct media_pad		pad;

	const struct cif_output_fmt	*cif_fmt_out;
	const struct cif_input_fmt	*cif_fmt_in;
	struct v4l2_pix_format_mplane	pixm;
	struct v4l2_rect		crop;
	int				crop_enable;
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
	int				irq;
	void __iomem			*base_addr;
	void __iomem			*csi_base;
	struct clk			*clks[RKCIF_MAX_BUS_CLK];
	int				clk_size;
	bool				iommu_en;
	struct iommu_domain		*domain;
	struct reset_control		*cif_rst[RKCIF_MAX_RESET];

	struct v4l2_device		v4l2_dev;
	struct media_device		media_dev;
	struct v4l2_ctrl_handler	ctrl_handler;
	struct v4l2_async_notifier	notifier;

	struct rkcif_sensor_info	sensors[RKCIF_MAX_SENSOR];
	u32				num_sensors;
	struct rkcif_sensor_info	*active_sensor;

	struct rkcif_stream		stream[RKCIF_MULTI_STREAMS_NUM];
	struct rkcif_pipeline		pipe;

	struct csi_channel_info		channels[RKCIF_MAX_CSI_CHANNEL];
	int				num_channels;
	int				chip_id;
	atomic_t			stream_cnt;
	atomic_t			fh_cnt;
	struct mutex                    stream_lock; /* lock between streams */
	enum rkcif_workmode		workmode;
	const struct cif_reg *cif_regs;
};

void rkcif_write_register(struct rkcif_device *dev,
			  enum cif_reg_index index, u32 val);
unsigned int rkcif_read_register(struct rkcif_device *dev,
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

#endif
