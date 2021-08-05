/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd
 *
 * author:
 *	Herman Chen <herman.chen@rock-chips.com>
 */
#ifndef __ROCKCHIP_MPP_RKVDEC2_LINK_H__
#define __ROCKCHIP_MPP_RKVDEC2_LINK_H__

#include "mpp_rkvdec2.h"

#define RKVDEC_REG_SECOND_EN_BASE	0x30
#define RKVDEC_REG_SECOND_EN_INDEX	12
#define RKVDEC_WAIT_RESET_EN		BIT(7)

/* define for link hardware */
#define RKVDEC_LINK_ADD_CFG_NUM		1

#define RKVDEC_LINK_IRQ_BASE		0x000
#define RKVDEC_LINK_BIT_IRQ_DIS		BIT(2)
#define RKVDEC_LINK_BIT_IRQ		BIT(8)
#define RKVDEC_LINK_BIT_IRQ_RAW		BIT(9)

#define RKVDEC_LINK_CFG_ADDR_BASE	0x004

#define RKVDEC_LINK_MODE_BASE		0x008
#define RKVDEC_LINK_BIT_ADD_MODE	BIT(31)

#define RKVDEC_LINK_CFG_CTRL_BASE	0x00c
#define RKVDEC_LINK_BIT_CFG_DONE	BIT(0)

#define RKVDEC_LINK_DEC_NUM_BASE	0x010
#define RKVDEC_LINK_BIT_DEC_ERROR	BIT(31)
#define	RKVDEC_LINK_GET_DEC_NUM(x)	((x) & 0x3fffffff)

#define RKVDEC_LINK_TOTAL_NUM_BASE	0x014

#define RKVDEC_LINK_EN_BASE		0x018
#define RKVDEC_LINK_BIT_EN		BIT(0)

#define RKVDEC_LINK_NEXT_ADDR_BASE	0x01c

#define RKVDEC_LINK_REG_CYCLE_CNT	179

struct rkvdec_link_dev {
	struct device *dev;
	struct mpp_dev *mpp;
	void __iomem *reg_base;
	u32 enabled;
	u32 link_mode;
	u32 decoded_status;
	u32 irq_status;
	u32 iova_curr;
	u32 iova_next;
	u32 decoded;
	u32 total;
	u32 error;
	u32 stuff_err;
	u32 stuff_total;
	u32 stuff_on_error;

	struct rkvdec_link_info *info;
	struct mpp_dma_buffer *table;
	u32 link_node_size;
	u32 link_reg_count;

	struct mpp_task **tasks_hw;
	u32 task_capacity;
	s32 task_total;
	s32 task_decoded;
	s32 task_size;
	s32 task_count;
	s32 task_write;
	s32 task_read;
	s32 task_send;
	s32 task_recv;

	/* taskqueue variables */
	u32 task_running;
	u32 task_prepared;
	s32 task_to_run;
	u32 task_on_timeout;

	/* taskqueue trigger variables */
	u32 task_irq;
	u32 task_irq_prev;
	/* timeout can be trigger in different thread so atomic is needed */
	atomic_t task_timeout;
	u32 task_timeout_prev;

	/* link mode hardware status */
	atomic_t power_enabled;
	u32 irq_enabled;

	/* debug variable */
	u32 statistic_count;
	u64 task_cycle_sum;
	u32 task_cnt;
	u64 stuff_cycle_sum;
	u32 stuff_cnt;
};

int rkvdec_link_dump(struct mpp_dev *mpp);

int rkvdec2_link_init(struct platform_device *pdev, struct rkvdec2_dev *dec);
int rkvdec2_link_procfs_init(struct mpp_dev *mpp);
int rkvdec2_link_remove(struct mpp_dev *mpp, struct rkvdec_link_dev *link_dec);

irqreturn_t rkvdec2_link_irq_proc(int irq, void *param);
int rkvdec2_link_process_task(struct mpp_session *session,
			      struct mpp_task_msgs *msgs);
int rkvdec2_link_wait_result(struct mpp_session *session,
			     struct mpp_task_msgs *msgs);
void rkvdec2_link_worker(struct kthread_work *work_s);
void rkvdec2_link_session_deinit(struct mpp_session *session);

#endif
