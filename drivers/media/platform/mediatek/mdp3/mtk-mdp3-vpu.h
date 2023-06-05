/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MTK_MDP3_VPU_H__
#define __MTK_MDP3_VPU_H__

#include <linux/platform_device.h>
#include "mtk-img-ipi.h"

enum mdp_ipi_result {
	MDP_IPI_SUCCESS	= 0,
	MDP_IPI_ENOMEM	= 12,
	MDP_IPI_EBUSY	= 16,
	MDP_IPI_EINVAL	= 22,
	MDP_IPI_EMINST	= 24,
	MDP_IPI_ERANGE	= 34,
	MDP_IPI_NR_ERRNO,

	MDP_IPI_EOTHER	= MDP_IPI_NR_ERRNO,
	MDP_IPI_PATH_CANT_MERGE,
	MDP_IPI_OP_FAIL,
};

struct mdp_ipi_init_msg {
	u32	status;
	u64	drv_data;
	u32	work_addr;	/* [in] working buffer address */
	u32	work_size;	/* [in] working buffer size */
} __packed;

struct mdp_ipi_deinit_msg {
	u32	status;
	u64	drv_data;
	u32	work_addr;
} __packed;

struct mdp_vpu_dev {
	/* synchronization protect for accessing vpu working buffer info */
	struct mutex		*lock;
	struct mtk_scp		*scp;
	struct completion	ipi_acked;
	void			*param;
	dma_addr_t		param_addr;
	size_t			param_size;
	void			*work;
	dma_addr_t		work_addr;
	size_t			work_size;
	void			*config;
	dma_addr_t		config_addr;
	size_t			config_size;
	u32			status;
};

void mdp_vpu_shared_mem_free(struct mdp_vpu_dev *vpu);
int mdp_vpu_dev_init(struct mdp_vpu_dev *vpu, struct mtk_scp *scp,
		     struct mutex *lock /* for sync */);
int mdp_vpu_dev_deinit(struct mdp_vpu_dev *vpu);
int mdp_vpu_process(struct mdp_vpu_dev *vpu, struct img_ipi_frameparam *param);

#endif  /* __MTK_MDP3_VPU_H__ */
