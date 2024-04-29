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
	/*
	 * Due to the sequential nature of function "mdp_mm_subsys_deploy",
	 * adding new enum. necessitates careful consideration.
	 */
	MDP_INFRA_MMSYS,
	MDP_INFRA_MMSYS2,
	MDP_INFRA_MUTEX,
	MDP_INFRA_MUTEX2,
	MDP_INFRA_SCP,
	MDP_INFRA_MAX
};

enum mdp_mm_subsys_id {
	MDP_MM_SUBSYS_0,
	MDP_MM_SUBSYS_1,
	MDP_MM_SUBSYS_MAX,
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
	bool	rdma_esl_setting;
	u32	rdma_event_num;
	bool	rsz_disable_dcm_small_sample;
	bool	rsz_etc_control;
	bool	wrot_filter_constraint;
	bool	wrot_support_10bit;
	u32	wrot_event_num;
	u32	tdshp_hist_num;
	bool	tdshp_constrain;
	bool	tdshp_contour;
};

/* indicate which mutex is used by each pipepline */
enum mdp_pipe_id {
	MDP_PIPE_WPEI,
	MDP_PIPE_WPEI2,
	MDP_PIPE_IMGI,
	MDP_PIPE_RDMA0,
	MDP_PIPE_RDMA1,
	MDP_PIPE_RDMA2,
	MDP_PIPE_RDMA3,
	MDP_PIPE_SPLIT,
	MDP_PIPE_SPLIT2,
	MDP_PIPE_VPP0_SOUT,
	MDP_PIPE_VPP1_SOUT,
	MDP_PIPE_MAX
};

/* MDP parallel pipe control */
enum {
	MDP_PP_USED_1 = 1,
	MDP_PP_USED_2 = 2,
};

#define MDP_PP_MAX MDP_PP_USED_2

struct mtk_mdp_driver_data {
	const int mdp_plat_id;
	const resource_size_t mdp_con_res;
	const struct of_device_id *mdp_probe_infra;
	const struct mdp_platform_config *mdp_cfg;
	const u32 *mdp_mutex_table_idx;
	const struct mdp_comp_data *comp_data;
	unsigned int comp_data_len;
	const struct of_device_id *mdp_sub_comp_dt_ids;
	const struct mdp_format *format;
	unsigned int format_len;
	const struct mdp_limit *def_limit;
	const struct mdp_pipe_info *pipe_info;
	unsigned int pipe_info_len;
	const struct v4l2_rect *pp_criteria;
	const u8 pp_used;
};

struct mdp_mm_subsys {
	struct device *mmsys;
	struct device *mutex;
	struct mtk_mutex *mdp_mutex[MDP_PIPE_MAX];
};

struct mdp_dev {
	struct platform_device			*pdev;
	struct mdp_mm_subsys			mm_subsys[MDP_MM_SUBSYS_MAX];
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
	struct cmdq_client			*cmdq_clt[MDP_PP_MAX];
	wait_queue_head_t			callback_wq;

	struct v4l2_device			v4l2_dev;
	struct video_device			*m2m_vdev;
	struct v4l2_m2m_dev			*m2m_dev;
	/* synchronization protect for m2m device operation */
	struct mutex				m2m_lock;
	atomic_t				suspended;
	refcount_t				job_count;
};

struct mdp_pipe_info {
	enum mdp_pipe_id pipe_id;
	enum mdp_mm_subsys_id sub_id;
	u32 mutex_id;
};

int mdp_vpu_get_locked(struct mdp_dev *mdp);
void mdp_vpu_put_locked(struct mdp_dev *mdp);
int mdp_vpu_register(struct mdp_dev *mdp);
void mdp_vpu_unregister(struct mdp_dev *mdp);
void mdp_video_device_release(struct video_device *vdev);

#endif  /* __MTK_MDP3_CORE_H__ */
