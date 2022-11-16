/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MTK_MDP3_CORE_H__
#define __MTK_MDP3_CORE_H__

#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <linux/soc/mediatek/mtk-mmsys.h>
#include <linux/soc/mediatek/mtk-mutex.h>
#include "mtk-mdp3-comp.h"
#include "mtk-mdp3-vpu.h"

#define MDP_MODULE_NAME		"mtk-mdp3"
#define MDP_DEVICE_NAME		"MediaTek MDP3"
#define MDP_PHANDLE_NAME	"mediatek,mdp3"

enum mdp_infra_id {
	MDP_INFRA_MMSYS,
	MDP_INFRA_MUTEX,
	MDP_INFRA_SCP,
	MDP_INFRA_MAX
};

enum mdp_buffer_usage {
	MDP_BUFFER_USAGE_HW_READ,
	MDP_BUFFER_USAGE_MDP,
	MDP_BUFFER_USAGE_MDP2,
	MDP_BUFFER_USAGE_ISP,
	MDP_BUFFER_USAGE_WPE,
};

struct mdp_platform_config {
	bool	rdma_support_10bit;
	bool	rdma_rsz1_sram_sharing;
	bool	rdma_upsample_repeat_only;
	bool	rsz_disable_dcm_small_sample;
	bool	wrot_filter_constraint;
};

/* indicate which mutex is used by each pipepline */
enum mdp_pipe_id {
	MDP_PIPE_RDMA0,
	MDP_PIPE_IMGI,
	MDP_PIPE_WPEI,
	MDP_PIPE_WPEI2,
	MDP_PIPE_MAX
};

struct mtk_mdp_driver_data {
	const struct of_device_id *mdp_probe_infra;
	const struct mdp_platform_config *mdp_cfg;
	const u32 *mdp_mutex_table_idx;
};

struct mdp_dev {
	struct platform_device			*pdev;
	struct device				*mdp_mmsys;
	struct mtk_mutex			*mdp_mutex[MDP_PIPE_MAX];
	struct mdp_comp				*comp[MDP_MAX_COMP_COUNT];
	const struct mtk_mdp_driver_data	*mdp_data;

	struct workqueue_struct			*job_wq;
	struct workqueue_struct			*clock_wq;
	struct mdp_vpu_dev			vpu;
	struct mtk_scp				*scp;
	struct rproc				*rproc_handle;
	/* synchronization protect for accessing vpu working buffer info */
	struct mutex				vpu_lock;
	s32					vpu_count;
	u32					id_count;
	struct ida				mdp_ida;
	struct cmdq_client			*cmdq_clt;
	wait_queue_head_t			callback_wq;

	struct v4l2_device			v4l2_dev;
	struct video_device			*m2m_vdev;
	struct v4l2_m2m_dev			*m2m_dev;
	/* synchronization protect for m2m device operation */
	struct mutex				m2m_lock;
	atomic_t				suspended;
	atomic_t				job_count;
};

int mdp_vpu_get_locked(struct mdp_dev *mdp);
void mdp_vpu_put_locked(struct mdp_dev *mdp);
int mdp_vpu_register(struct mdp_dev *mdp);
void mdp_vpu_unregister(struct mdp_dev *mdp);
void mdp_video_device_release(struct video_device *vdev);

#endif  /* __MTK_MDP3_CORE_H__ */
