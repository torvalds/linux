/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_COMMON_H
#define _RKISPP_COMMON_H

#include <linux/clk.h>
#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

#include "../isp/isp_ispp.h"

#define RKISPP_PLANE_Y		0
#define RKISPP_PLANE_UV		1

#define RKISPP_MAX_WIDTH	4416
#define RKISPP_MAX_HEIGHT	3312
#define RKISPP_MIN_WIDTH	66
#define RKISPP_MIN_HEIGHT	258
#define RKISPP_VIDEO_NAME_LEN   16

#define RKISPP_BUF_POOL_MAX	RKISP_ISPP_BUF_MAX

struct rkispp_device;

enum rkispp_ver {
	ISPP_V10 = 0x00,
};

enum rkispp_event_cmd {
	CMD_STREAM,
	CMD_INIT_POOL,
	CMD_FREE_POOL,
	CMD_QUEUE_DMABUF,
};

struct rkispp_isp_buf_pool {
	struct rkisp_ispp_buf *dbufs;
	void *mem_priv[GROUP_BUF_MAX];
	dma_addr_t dma[GROUP_BUF_MAX];
	void *vaddr[GROUP_BUF_MAX];
};

/* One structure per video node */
struct rkispp_vdev_node {
	struct vb2_queue buf_queue;
	struct video_device vdev;
	struct media_pad pad;
};

struct rkispp_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	union {
		u32 buff_addr[VIDEO_MAX_PLANES];
		void *vaddr[VIDEO_MAX_PLANES];
	};
};

struct rkispp_dummy_buffer {
	struct list_head list;
	struct dma_buf *dbuf;
	dma_addr_t dma_addr;
	void *mem_priv;
	void *vaddr;
	struct page **pages;
	/* timestamp in ns */
	u64 timestamp;
	u32 size;
	u32 id;
	u32 index;
	int dma_fd;
	bool is_need_vaddr;
	bool is_need_dbuf;
	bool is_need_dmafd;
};

static inline struct rkispp_vdev_node *vdev_to_node(struct video_device *vdev)
{
	return container_of(vdev, struct rkispp_vdev_node, vdev);
}

static inline struct rkispp_vdev_node *queue_to_node(struct vb2_queue *q)
{
	return container_of(q, struct rkispp_vdev_node, buf_queue);
}

static inline struct rkispp_buffer *to_rkispp_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct rkispp_buffer, vb);
}

static inline struct vb2_queue *to_vb2_queue(struct file *file)
{
	struct rkispp_vdev_node *vnode = video_drvdata(file);

	return &vnode->buf_queue;
}

extern int rkispp_debug;
extern bool rkispp_monitor;
extern bool rkispp_reg_withstream;
extern char rkispp_reg_withstream_video_name[RKISPP_VIDEO_NAME_LEN];
extern unsigned int rkispp_debug_reg;
extern struct platform_driver rkispp_plat_drv;
extern char rkispp_dump_path[128];

void rkispp_write(struct rkispp_device *dev, u32 reg, u32 val);
void rkispp_set_bits(struct rkispp_device *dev, u32 reg, u32 mask, u32 val);
u32 rkispp_read(struct rkispp_device *dev, u32 reg);
void rkispp_clear_bits(struct rkispp_device *dev, u32 reg, u32 mask);
void rkispp_update_regs(struct rkispp_device *dev, u32 start, u32 end);
int rkispp_allow_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf);
void rkispp_free_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf);
void rkispp_prepare_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf);
void rkispp_finish_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf);

int rkispp_attach_hw(struct rkispp_device *ispp);
int rkispp_event_handle(struct rkispp_device *ispp, u32 cmd, void *arg);
int rkispp_alloc_common_dummy_buf(struct rkispp_device *dev);
void rkispp_free_common_dummy_buf(struct rkispp_device *dev);
int rkispp_find_regbuf_by_id(struct rkispp_device *ispp, struct rkisp_ispp_reg **free_buf,
			     u32 dev_id, u32 frame_id);
void rkispp_release_regbuf(struct rkispp_device *ispp, struct rkisp_ispp_reg *freebuf);
void rkispp_request_regbuf(struct rkispp_device *dev, struct rkisp_ispp_reg **free_buf);
bool rkispp_is_reg_withstream_global(void);
bool rkispp_is_reg_withstream_local(struct device *dev);
void rkispp_set_clk_rate(struct clk *clk, unsigned long rate);
#endif
