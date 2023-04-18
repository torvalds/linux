/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd
 *
 * author:
 *	Herman Chen <herman.chen@rock-chips.com>
 *
 */
#ifndef __ROCKCHIP_MPP_RKVDEC2_H__
#define __ROCKCHIP_MPP_RKVDEC2_H__

#include <linux/dma-iommu.h>
#include <linux/iopoll.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/kernel.h>
#include <linux/thermal.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/nospec.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/regulator/consumer.h>

#include <soc/rockchip/pm_domains.h>
#include <soc/rockchip/rockchip_sip.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

#define RKVDEC_DRIVER_NAME		"mpp_rkvdec2"

#define RKVDEC_REG_IMPORTANT_BASE	0x2c
#define RKVDEC_REG_IMPORTANT_INDEX	11
#define RKVDEC_SOFTREST_EN		BIT(20)

#define	RKVDEC_SESSION_MAX_BUFFERS	40
/* The maximum registers number of all the version */
#define RKVDEC_REG_NUM			279
#define RKVDEC_REG_HW_ID_INDEX		0
#define RKVDEC_REG_START_INDEX		0
#define RKVDEC_REG_END_INDEX		278

#define REVDEC_GET_PROD_NUM(x)		(((x) >> 16) & 0xffff)
#define RKVDEC_REG_FORMAT_INDEX		9
#define RKVDEC_GET_FORMAT(x)		((x) & 0x3ff)

#define RKVDEC_REG_START_EN_BASE       0x28

#define RKVDEC_REG_START_EN_INDEX      10

#define RKVDEC_START_EN			BIT(0)

#define RKVDEC_REG_YSTRIDE_INDEX	20
#define RKVDEC_REG_CORE_CTRL_INDEX	28
#define RKVDEC_REG_FILM_IDX_MASK	(0x3ff0000)

#define RKVDEC_REG_RLC_BASE		0x200
#define RKVDEC_REG_RLC_BASE_INDEX	(128)

#define RKVDEC_REG_INT_EN		0x380
#define RKVDEC_REG_INT_EN_INDEX		(224)
#define RKVDEC_SOFT_RESET_READY		BIT(9)
#define RKVDEC_CABAC_END_STA		BIT(8)
#define RKVDEC_COLMV_REF_ERR_STA	BIT(7)
#define RKVDEC_BUF_EMPTY_STA		BIT(6)
#define RKVDEC_TIMEOUT_STA		BIT(5)
#define RKVDEC_ERROR_STA		BIT(4)
#define RKVDEC_BUS_STA			BIT(3)
#define RKVDEC_READY_STA		BIT(2)
#define RKVDEC_IRQ_RAW			BIT(1)
#define RKVDEC_IRQ			BIT(0)
#define RKVDEC_INT_ERROR_MASK		(RKVDEC_COLMV_REF_ERR_STA |\
					RKVDEC_BUF_EMPTY_STA |\
					RKVDEC_TIMEOUT_STA |\
					RKVDEC_ERROR_STA)
#define RKVDEC_PERF_WORKING_CNT		0x41c

/* perf sel reference register */
#define RKVDEC_PERF_SEL_OFFSET		0x20000
#define RKVDEC_PERF_SEL_NUM		64
#define RKVDEC_PERF_SEL_BASE		0x424
#define RKVDEC_SEL_VAL0_BASE		0x428
#define RKVDEC_SEL_VAL1_BASE		0x42c
#define RKVDEC_SEL_VAL2_BASE		0x430
#define RKVDEC_SET_PERF_SEL(a, b, c)	((a) | ((b) << 8) | ((c) << 16))

/* cache reference register */
#define RKVDEC_REG_CACHE0_SIZE_BASE	0x51c
#define RKVDEC_REG_CACHE1_SIZE_BASE	0x55c
#define RKVDEC_REG_CACHE2_SIZE_BASE	0x59c
#define RKVDEC_REG_CLR_CACHE0_BASE	0x510
#define RKVDEC_REG_CLR_CACHE1_BASE	0x550
#define RKVDEC_REG_CLR_CACHE2_BASE	0x590

#define RKVDEC_CACHE_PERMIT_CACHEABLE_ACCESS	BIT(0)
#define RKVDEC_CACHE_PERMIT_READ_ALLOCATE	BIT(1)
#define RKVDEC_CACHE_LINE_SIZE_64_BYTES		BIT(4)

#define to_rkvdec2_task(task)		\
		container_of(task, struct rkvdec2_task, mpp_task)
#define to_rkvdec2_dev(dev)		\
		container_of(dev, struct rkvdec2_dev, mpp)

enum RKVDEC_FMT {
	RKVDEC_FMT_H265D	= 0,
	RKVDEC_FMT_H264D	= 1,
	RKVDEC_FMT_VP9D		= 2,
	RKVDEC_FMT_AVS2		= 3,
};

#define RKVDEC_MAX_RCB_NUM		(16)

struct rcb_info_elem {
	u32 index;
	u32 size;
};

struct rkvdec2_rcb_info {
	u32 cnt;
	struct rcb_info_elem elem[RKVDEC_MAX_RCB_NUM];
};

struct rkvdec2_task {
	struct mpp_task mpp_task;

	enum MPP_CLOCK_MODE clk_mode;
	u32 reg[RKVDEC_REG_NUM];
	struct reg_offset_info off_inf;

	/* perf sel data back */
	u32 reg_sel[RKVDEC_PERF_SEL_NUM];

	u32 strm_addr;
	u32 irq_status;
	/* req for current task */
	u32 w_req_cnt;
	struct mpp_request w_reqs[MPP_MAX_MSG_NUM];
	u32 r_req_cnt;
	struct mpp_request r_reqs[MPP_MAX_MSG_NUM];
	/* image info */
	u32 width;
	u32 height;
	u32 pixels;

	/* task index for link table rnunning list */
	int slot_idx;
	u32 need_hack;

	/* link table DMA buffer */
	struct mpp_dma_buffer *table;
};

struct rkvdec2_session_priv {
	/* codec info from user */
	struct {
		/* show mode */
		u32 flag;
		/* item data */
		u64 val;
	} codec_info[DEC_INFO_BUTT];
	/* rcb_info for sram */
	struct rkvdec2_rcb_info rcb_inf;
};

struct rkvdec2_dev {
	struct mpp_dev mpp;
	/* sip smc reset lock */
	struct mutex sip_reset_lock;

	struct mpp_clk_info aclk_info;
	struct mpp_clk_info hclk_info;
	struct mpp_clk_info core_clk_info;
	struct mpp_clk_info cabac_clk_info;
	struct mpp_clk_info hevc_cabac_clk_info;
	struct mpp_clk_info *cycle_clk;

	u32 default_max_load;
#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
	struct proc_dir_entry *procfs;
#endif
	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct reset_control *rst_niu_a;
	struct reset_control *rst_niu_h;
	struct reset_control *rst_core;
	struct reset_control *rst_cabac;
	struct reset_control *rst_hevc_cabac;

#ifdef CONFIG_PM_DEVFREQ
	struct regulator *vdd;
	struct devfreq *devfreq;
	unsigned long volt;
	unsigned long core_rate_hz;
	unsigned long core_last_rate_hz;
	struct ipa_power_model_data *model_data;
	struct thermal_cooling_device *devfreq_cooling;
	struct monitor_dev_info *mdev_info;
#endif

	/* internal rcb-memory */
	u32 sram_size;
	u32 rcb_size;
	dma_addr_t rcb_iova;
	struct page *rcb_page;
	u32 rcb_min_width;
	u32 rcb_info_count;
	u32 rcb_infos[RKVDEC_MAX_RCB_NUM * 2];

	/* for link mode */
	struct rkvdec_link_dev *link_dec;
	struct mpp_dma_buffer *fix;

	/* for ccu link mode */
	struct rkvdec2_ccu *ccu;
	u32 core_mask;
	u32 task_index;
	/* mmu info */
	void __iomem *mmu_base;
	u32 fault_iova;
	u32 mmu_fault;
	u32 mmu0_st;
	u32 mmu1_st;
	u32 mmu0_pta;
	u32 mmu1_pta;
};

int mpp_set_rcbbuf(struct mpp_dev *mpp, struct mpp_session *session,
		   struct mpp_task *task);
int rkvdec2_task_init(struct mpp_dev *mpp, struct mpp_session *session,
		      struct rkvdec2_task *task, struct mpp_task_msgs *msgs);
void *rkvdec2_alloc_task(struct mpp_session *session,
			 struct mpp_task_msgs *msgs);
int rkvdec2_free_task(struct mpp_session *session, struct mpp_task *mpp_task);

int rkvdec2_free_session(struct mpp_session *session);

int rkvdec2_result(struct mpp_dev *mpp, struct mpp_task *mpp_task,
		   struct mpp_task_msgs *msgs);
int rkvdec2_reset(struct mpp_dev *mpp);

void mpp_devfreq_set_core_rate(struct mpp_dev *mpp, enum MPP_CLOCK_MODE mode);

#endif
