/*
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 * author: hehua,hh@rock-chips.com
 * lixinhuang, buluess.li@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __ROCKCHIP_MPP_DEV_H265E_H
#define __ROCKCHIP_MPP_DEV_H265E_H

#include "mpp_dev_h265e_define.h"
#include "mpp_service.h"
#include <linux/ioctl.h>
#include <linux/wakelock.h>

#define MPP_DEV_H265E_SET_COLOR_PALETTE	\
		_IOW(VPU_IOC_MAGIC, MPP_IOC_CUSTOM_BASE + 1, u32)

#define MPP_DEV_H265E_SET_PARAMETER	\
		_IOW(VPU_IOC_MAGIC, \
		MPP_IOC_CUSTOM_BASE + 6, struct mpp_h265e_cfg)

#define MPP_DEV_H265E_GET_HEAD_PARAMETER	\
		_IOW(VPU_IOC_MAGIC, \
		MPP_IOC_CUSTOM_BASE + 7, struct hal_h265e_header)

#define H265E_INSTANCE_NUM 4

enum H265E_MODE {
	H265E_MODE_NONE,
	H265E_MODE_ONEFRAME,
	H265E_MODE_LINKTABLE_FIX,
	H265E_MODE_LINKTABLE_UPDATE,
	H265E_MODE_NUM
};

struct regmap;

struct h265e_result {
	u32 bs_size;
	u32 enc_pic_cnt;
	u32 pic_type;
	u32 num_of_slice;
	u32 pick_skipped;
	u32 num_intra;
	u32 num_merge;
	u32 num_skip_block;
	u32 avg_ctu_qp;
	int recon_frame_index;
	u32 gop_idx;
	u32 poc;
	u32 src_idx;
	u32 fail_reason;
};

struct mpp_h265e_buffer {
	unsigned long dma_addr;
	u32 size;
	int hdl;
};

struct mpp_h265e_frame_buffer {
	struct mpp_h265e_buffer buffer;
	u32 y;
	u32 cb;
	u32 cr;
};

struct h265e_ctx {
	struct mpp_ctx ictx;
	enum H265E_MODE mode;
	struct mpp_h265e_buffer bs;
	char __iomem *bs_data;/*for debug read data*/
	struct mpp_h265e_buffer src;
	struct mpp_h265e_buffer roi;
	struct mpp_h265e_buffer ctu;

	struct mpp_h265e_encode_info cfg;

	/* store status read from hw, oneframe mode used only */
	struct h265e_result result;
};

enum H265E_INSTANCE_STATUS {
	H265E_INSTANCE_STATUS_ERROR,
	H265E_INSTANCE_STATUS_OPENED,
	H265E_INSTANCE_STATUS_SET_PARAMETER,
	H265E_INSTANCE_STATUS_ENCODE,
	H265E_INSTANCE_STATUS_CLOSE
};

struct rockchip_h265e_instance {
	int index;
	atomic_t is_used;
	struct mpp_h265e_buffer work;
	struct mpp_h265e_buffer temp;
	struct mpp_h265e_buffer mv;
	struct mpp_h265e_buffer fbc_luma;
	struct mpp_h265e_buffer fbc_chroma;
	struct mpp_h265e_buffer sub_sample;
	/*
	 * for recon frames
	 */
	struct mpp_h265e_frame_buffer frame_buffer[16];

	int min_frame_buffer_count;
	int min_src_frame_count;
	int src_idx;
	int status;
	struct mpp_h265e_cfg cfg;
	struct mpp_session *session;
};

struct rockchip_h265e_dev {
	struct rockchip_mpp_dev dev;
	struct rockchip_h265e_instance instance[H265E_INSTANCE_NUM];
	struct mpp_h265e_buffer temp;

	u32 lkt_index;
	u32 irq_status;
	atomic_t is_init;
	atomic_t load_firmware;

	struct delayed_work work_list;
	struct mutex lock;

	char __iomem *firmware_cpu_addr;
	struct mpp_h265e_buffer firmware;

	struct clk *aclk;
	struct clk *aclk_axi2sram;
	struct clk *pclk;
	struct clk *core;
	struct clk *dsp;
	void __iomem *grf_base;
	u32 mode_bit;
	u32 mode_ctrl;
	struct regmap *grf;
};

struct h265e_session {
	struct mpp_session isession;
	int instance_index;
};

#endif

