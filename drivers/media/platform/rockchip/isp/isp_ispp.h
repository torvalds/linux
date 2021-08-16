/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISPP_H
#define _RKISP_ISPP_H

#include <linux/platform_device.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>
#include <linux/rkisp1-config.h>
#include <linux/rkispp-config.h>

#define RKISP_BUF_MAX 2
#define RKISPP_BUF_MAX 2
#define RKISP_ISPP_BUF_MAX (RKISP_BUF_MAX + RKISPP_BUF_MAX + (2 * (DEV_MAX - 1)))

#define RKISP_ISPP_REGBUF_NUM		RKISPP_BUF_POOL_MAX
#define RKISP_ISP_SW_REG_SIZE		0x6000
#define RKISP_ISP_SW_MAX_SIZE		(RKISP_ISP_SW_REG_SIZE * 2)
#define RKISP_ISPP_SW_REG_SIZE		0x0d00
#define RKISP_ISPP_SW_MAX_SIZE		(RKISP_ISPP_SW_REG_SIZE * 2)

#define RKISP_ISPP_CMD_SET_MODE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 0, struct rkisp_ispp_mode)
#define RKISP_ISPP_CMD_SET_FMT \
	_IOW('V', BASE_VIDIOC_PRIVATE + 1, struct max_input)
#define RKISP_ISPP_CMD_REQUEST_REGBUF	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 2, struct rkisp_ispp_reg *)
#define RKISP_ISPP_CMD_GET_REG_WITHSTREAM	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 3, bool)

enum frame_end_state {
	FRAME_INIT,
	FRAME_IRQ,
	FRAME_WORK,
};

enum rkisp_ispp_dev {
	DEV_ID0 = 0,
	DEV_ID1,
	DEV_ID2,
	DEV_ID3,
	DEV_MAX,
};

enum rkisp_ispp_sw_reg {
	SW_REG_CACHE = 0xffffffff,
	SW_REG_CACHE_SYNC = 0xeeeeeeee,
};

enum rkisp_ispp_buf_group {
	GROUP_BUF_PIC = 0,
	GROUP_BUF_GAIN,
	GROUP_BUF_MAX,
};

enum rkisp_ispp_work_mode {
	ISP_ISPP_FBC = BIT(0),
	ISP_ISPP_422 = BIT(1),
	ISP_ISPP_QUICK = BIT(2),
	ISP_ISPP_INIT_FAIL = BIT(7),
};

enum rkisp_ispp_reg_stat {
	ISP_ISPP_FREE = 0,
	ISP_ISPP_INUSE,
};

struct frame_debug_info {
	u64 timestamp;
	u32 interval;
	u32 delay;
	u32 id;
	u32 frameloss;
};

struct max_input {
	u32 w;
	u32 h;
	u32 fps;
};

struct rkisp_ispp_mode {
	u8 work_mode;
	u8 buf_num;
};

struct rkisp_ispp_reg {
	enum rkisp_ispp_reg_stat stat;
	u32 dev_id;
	u32 frame_id;
	u32 reg_size;
	s32 isp_offset[ISP2X_ID_MAX];
	s32 ispp_offset[ISPP_ID_MAX];
	u32 isp_size[ISP2X_ID_MAX];
	u32 isp_stats_size[ISP2X_ID_MAX];
	u32 ispp_size[ISPP_ID_MAX];
	u64 sof_timestamp;
	u64 frame_timestamp;
	struct sensor_exposure_cfg exposure;
	u8 reg[RKISP_ISP_SW_REG_SIZE + RKISP_ISPP_SW_REG_SIZE];
};

struct rkisp_ispp_buf {
	struct list_head list;
	struct dma_buf *dbuf[GROUP_BUF_MAX];
	int dfd[GROUP_BUF_MAX];
	u32 didx[GROUP_BUF_MAX];
	/* timestamp in ns */
	u64 frame_timestamp;
	u32 frame_id;
	u32 index;
	bool is_isp;
	bool is_move_judge;
	u32 buf_idx;
	u32 gain_dmaidx;
	u32 mfbc_dmaidx;
	u32 gain_size;
	u32 mfbc_size;
	void *priv;
};

int __init rkispp_hw_drv_init(void);

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V20)
void rkisp_get_bridge_sd(struct platform_device *dev,
			 struct v4l2_subdev **sd);
#else
static inline void rkisp_get_bridge_sd(struct platform_device *dev,
				       struct v4l2_subdev **sd)
{
	*sd = NULL;
}
#endif

extern const struct vb2_mem_ops vb2_rdma_sg_memops;

#endif
