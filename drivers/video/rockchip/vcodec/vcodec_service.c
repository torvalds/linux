/**
 * Copyright (C) 2015 Fuzhou Rockchip Electronics Co., Ltd
 * author: chenhengming, chm@rock-chips.com
 *	   Alpha Lin, alpha.lin@rock-chips.com
 *	   Jung Zhao, jung.zhao@rock-chips.com
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wakelock.h>
#include <linux/cdev.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/iopoll.h>

#include <linux/regulator/consumer.h>
#include <linux/rockchip/pmu.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/thermal.h>

#include <linux/dma-iommu.h>
#include <linux/dma-buf.h>
#include <linux/rockchip-iovmm.h>
#include <video/rk_vpu_service.h>
#include <soc/rockchip/rockchip_opp_select.h>

#include "vcodec_hw_info.h"
#include "vcodec_hw_vpu.h"
#include "vcodec_hw_rkv.h"
#include "vcodec_hw_vpu2.h"

#include "vcodec_service.h"

#include "vcodec_iommu_ops.h"

#include "../../../devfreq/governor.h"

/*
 * debug flag usage:
 * +------+-------------------+
 * | 8bit |      24bit        |
 * +------+-------------------+
 *  0~23 bit is for different information type
 * 24~31 bit is for information print format
 */

#define DEBUG_POWER				0x00000001
#define DEBUG_CLOCK				0x00000002
#define DEBUG_IRQ_STATUS			0x00000004
#define DEBUG_IOMMU				0x00000008
#define DEBUG_IOCTL				0x00000010
#define DEBUG_FUNCTION				0x00000020
#define DEBUG_REGISTER				0x00000040
#define DEBUG_EXTRA_INFO			0x00000080
#define DEBUG_TIMING				0x00000100
#define DEBUG_TASK_INFO				0x00000200

#define DEBUG_SET_REG				0x00001000
#define DEBUG_GET_REG				0x00002000
#define DEBUG_PPS_FILL				0x00004000
#define DEBUG_IRQ_CHECK				0x00008000
#define DEBUG_CACHE_32B				0x00010000

#define PRINT_FUNCTION				0x80000000
#define PRINT_LINE				0x40000000

#define MHZ					(1000 * 1000)
#define SIZE_REG(reg)				((reg) * 4)

#define VCODEC_CLOCK_ENABLE			1
#define IOMMU_PAGE_SIZE				4096
#define EXTRA_INFO_MAGIC			0x4C4A46

#define RK3328_VCODEC_RATE_ON			(500 * MHZ)
#define RK3328_CODE_RATE_ON			(250 * MHZ)
#define RK3328_CABAC_RATE_ON			(400 * MHZ)
#define RK3328_VCODEC_RATE_OFF			(100 * MHZ)
#define RK3328_CODE_RATE_OFF			(100 * MHZ)
#define RK3328_CABAC_RATE_OFF			(100 * MHZ)

#define FALLBACK_STATIC_TEMPERATURE		55000

static int debug;
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "bit switch for vcodec_service debug information");
/*
 * hardware information organization
 *
 * In order to support multiple hardware with different version the hardware
 * information is organized as follow:
 *
 * 1. First, index hardware by register size / position.
 *    These information is fix for each hardware and do not relate to runtime
 *    work flow. It only related to resource allocation.
 *    Descriptor: struct vpu_hw_info
 *
 * 2. Then, index hardware by runtime configuration
 *    These information is related to runtime setting behave including enable
 *    register, irq register and other key control flag
 *    Descriptor: struct vpu_task_info
 *
 * 3. Final, on iommu case the fd translation is required
 *    Descriptor: struct vpu_trans_info
 */

enum SET_CLK_EVENT {
	EVENT_POWER_ON = 0,
	EVENT_POWER_OFF,
	EVENT_ADJUST,
	EVENT_THERMAL,
};

enum VPU_FREQ {
	VPU_FREQ_200M,
	VPU_FREQ_266M,
	VPU_FREQ_300M,
	VPU_FREQ_400M,
	VPU_FREQ_500M,
	VPU_FREQ_600M,
	VPU_FREQ_DEFAULT,
	VPU_FREQ_BUT,
};

struct extra_info_elem {
	u32 index;
	u32 offset;
};

struct extra_info_for_iommu {
	u32 magic;
	u32 cnt;
	struct extra_info_elem elem[20];
};

struct vcodec_iommu_drm_info {
	struct iommu_domain *domain;
	bool attached;
};

static const struct vcodec_info vcodec_info_set[] = {
	{
		.hw_id		= VPU_ID_8270,
		.hw_info	= &hw_vpu_8270,
		.task_info	= task_vpu,
		.trans_info	= trans_vpu,
	},
	{
		.hw_id		= VPU_ID_4831,
		.hw_info	= &hw_vpu_4831,
		.task_info	= task_vpu,
		.trans_info	= trans_vpu,
	},
	{
		.hw_id		= VPU_DEC_ID_9190,
		.hw_info	= &hw_vpu_9190,
		.task_info	= task_vpu,
		.trans_info	= trans_vpu,
	},
	{
		.hw_id		= HEVC_ID,
		.hw_info	= &hw_rkhevc,
		.task_info	= task_rkv,
		.trans_info	= trans_rkv,
	},
	{
		.hw_id		= RKV_DEC_ID,
		.hw_info	= &hw_rkvdec,
		.task_info	= task_rkv,
		.trans_info	= trans_rkv,
	},
	{
		.hw_id          = VPU2_ID,
		.hw_info        = &hw_vpu2,
		.task_info      = task_vpu2,
		.trans_info     = trans_vpu2,
	},
	{
		.hw_id		= RKV_DEC_ID2,
		.hw_info	= &hw_rkvdec,
		.task_info	= task_rkv,
		.trans_info	= trans_rkv,
	},
};

#define DEBUG
#ifdef DEBUG
#define vpu_debug_func(type, fmt, args...)			\
	do {							\
		if (unlikely(debug & type)) {			\
			pr_info("%s:%d: " fmt,			\
				 __func__, __LINE__, ##args);	\
		}						\
	} while (0)
#define vpu_debug(type, fmt, args...)				\
	do {							\
		if (unlikely(debug & type)) {			\
			pr_info(fmt, ##args);			\
		}						\
	} while (0)
#else
#define vpu_debug_func(level, fmt, args...)
#define vpu_debug(level, fmt, args...)
#endif

#define vpu_debug_enter() vpu_debug_func(DEBUG_FUNCTION, "enter\n")
#define vpu_debug_leave() vpu_debug_func(DEBUG_FUNCTION, "leave\n")

#define vpu_err(fmt, args...)				\
		pr_err("%s:%d: " fmt, __func__, __LINE__, ##args)

struct vpu_device {
	atomic_t irq_count_codec;
	atomic_t irq_count_pp;
	unsigned int iosize;
	u32 *regs;
};

enum VCODEC_RUNNING_MODE {
	VCODEC_RUNNING_MODE_NONE = -1,
	VCODEC_RUNNING_MODE_VPU,
	VCODEC_RUNNING_MODE_HEVC,
	VCODEC_RUNNING_MODE_RKVDEC
};

struct vcodec_mem_region {
	struct list_head srv_lnk;
	struct list_head reg_lnk;
	struct list_head session_lnk;
	/* virtual address for iommu */
	dma_addr_t iova;
	unsigned long len;
	u32 reg_idx;
	int hdl;
};

/* struct for process register set */
struct vpu_reg {
	enum VPU_CLIENT_TYPE type;
	enum VPU_FREQ freq;
	struct vpu_session *session;
	struct vpu_subdev_data *data;
	struct vpu_task_info *task;
	const struct vpu_trans_info *trans;

	/* link to vpu service session */
	struct list_head session_link;
	/* link to register set list */
	struct list_head status_link;

	unsigned long size;
	struct list_head mem_region_list;
	u32 dec_base;
	u32 *reg;
};

enum vpu_ctx_state {
	MMU_ACTIVATED	= BIT(0),
	MMU_PAGEFAULT	= BIT(1)
};

struct vpu_subdev_data {
	struct cdev cdev;
	dev_t dev_t;
	struct class *cls;
	struct device *child_dev;

	int irq_enc;
	int irq_dec;
	struct vpu_service_info *pservice;

	u32 *regs;
	enum VCODEC_RUNNING_MODE mode;
	struct list_head lnk_service;

	struct device *dev;

	struct vpu_device enc_dev;
	struct vpu_device dec_dev;

	enum VPU_HW_ID hw_id;
	struct vpu_hw_info *hw_info;
	struct vpu_task_info *task_info;
	const struct vpu_trans_info *trans_info;

	u32 reg_size;
	unsigned long state;

	struct device *mmu_dev;
	struct vcodec_iommu_info *iommu_info;
	int pa_hdl;
	unsigned long pa_iova;
	struct work_struct set_work;
};

struct vpu_service_info {
	struct wake_lock wake_lock;
	struct delayed_work power_off_work;
	struct wake_lock set_wake_lock;
	struct workqueue_struct *set_workq;
	ktime_t last; /* record previous power-on time */
	/* vpu service structure global lock */
	struct mutex lock;
	/* link to link_reg in struct vpu_reg */
	struct list_head waiting;
	/* link to link_reg in struct vpu_reg */
	struct list_head running;
	/* link to link_reg in struct vpu_reg */
	struct list_head done;
	/* link to list_session in struct vpu_session */
	struct list_head session;
	atomic_t total_running;
	atomic_t enabled;
	atomic_t power_on_cnt;
	atomic_t power_off_cnt;
	atomic_t service_on;
	struct mutex shutdown_lock;
	/*
	 * FIXME: if someone call iommu translate function during vpu_reset,
	 * it may cause system core dump without any message. we suggest
	 * modify iommu driver to avoid this situation. before that,
	 * this is a temporary solution.
	 */
	struct mutex reset_lock;
	struct mutex sip_reset_lock; /* sip smc reset lock */
	struct vpu_reg *reg_codec;
	struct vpu_reg *reg_pproc;
	struct vpu_reg *reg_resev;
	struct vpu_dec_config dec_config;
	struct vpu_enc_config enc_config;

	bool auto_freq;
	bool bug_dec_addr;
	atomic_t freq_status;

	bool secure_isr;
	bool secure_irq_status;
	atomic_t secure_mode;
	wait_queue_head_t *wait_secure_isr;

	struct devfreq *devfreq;
	struct regulator *vdd_vcodec;
	struct mutex set_clk_lock; /* set clk lock */

	unsigned long volt;
	unsigned long vcodec_rate;
	unsigned long core_rate;
	unsigned long cabac_rate;

	struct thermal_cooling_device *devfreq_cooling;
	u32 static_coefficient;
	s32 ts[4];
	struct thermal_zone_device *vcodec_tz;

	struct clk *aclk_vcodec;
	struct clk *hclk_vcodec;
	struct clk *clk_core;
	struct clk *clk_cabac;
	struct clk *pd_video;

	unsigned long aclk_vcodec_default_rate;
	unsigned long clk_core_default_rate;
	unsigned long clk_cabac_default_rate;

#ifdef CONFIG_RESET_CONTROLLER
	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct reset_control *rst_v;
	struct reset_control *rst_core;
	struct reset_control *rst_cabac;
	struct reset_control *rst_niu_a;
	struct reset_control *rst_niu_h;
#endif
	struct device *dev;

	u32 irq_status;
	atomic_t reset_request;
	struct list_head mem_region_list;

	enum vcodec_device_id dev_id;

	enum VCODEC_RUNNING_MODE curr_mode;
	u32 prev_mode;

	struct delayed_work simulate_work;

	u32 mode_bit;
	u32 mode_ctrl;
	u32 *reg_base;
	u32 ioaddr;
	struct regmap *grf;

	char *name;

	u32 subcnt;
	struct list_head subdev_list;

	u32 alloc_type;

	struct vcodec_hw_ops *hw_ops;
	const struct vcodec_hw_var *hw_var;
	struct devfreq *parent_devfreq;
	struct notifier_block devfreq_nb;
};

struct vcodec_hw_ops {
	void (*power_on)(struct vpu_service_info *pservice);
	void (*power_off)(struct vpu_service_info *pservice);
	void (*get_freq)(struct vpu_subdev_data *data, struct vpu_reg *reg);
	void (*set_freq)(struct vpu_service_info *pservice,
			 struct vpu_reg *reg);
	void (*reduce_freq)(struct vpu_service_info *pservice);
};

struct vcodec_hw_var {
	s32 device_type;
	u8 *name;
	enum pmu_idle_req pmu_type;
	struct vcodec_hw_ops *ops;
	int (*init)(struct vpu_service_info *pservice);
	void (*config)(struct vpu_subdev_data *data);
};

#ifdef CONFIG_COMPAT
struct compat_vpu_request {
	compat_uptr_t req;
	u32 size;
};
#endif

#define VDPU_SOFT_RESET_REG	101
#define VDPU_CLEAN_CACHE_REG	516
#define VEPU_CLEAN_CACHE_REG	772
#define HEVC_CLEAN_CACHE_REG	260

#define VPU_REG_ENABLE(base, reg)	writel_relaxed(1, base + reg)

#define VDPU_SOFT_RESET(base)	VPU_REG_ENABLE(base, VDPU_SOFT_RESET_REG)
#define VDPU_CLEAN_CACHE(base)	VPU_REG_ENABLE(base, VDPU_CLEAN_CACHE_REG)
#define VEPU_CLEAN_CACHE(base)	VPU_REG_ENABLE(base, VEPU_CLEAN_CACHE_REG)
#define HEVC_CLEAN_CACHE(base)	VPU_REG_ENABLE(base, HEVC_CLEAN_CACHE_REG)

#define VPU_POWER_OFF_DELAY		(4 * HZ) /* 4s */
#define VPU_TIMEOUT_DELAY		(2 * HZ) /* 2s */

static void vcodec_reduce_freq_rk3328(struct vpu_service_info *pservice);

static void vpu_service_power_on(struct vpu_subdev_data *data,
				 struct vpu_service_info *pservice);

static void time_record(struct vpu_task_info *task, int is_end)
{
	if (unlikely(debug & DEBUG_TIMING) && task)
		do_gettimeofday((is_end) ? (&task->end) : (&task->start));
}

static void time_diff(struct vpu_task_info *task)
{
	vpu_debug(DEBUG_TIMING, "%s task: %ld ms\n", task->name,
		  (task->end.tv_sec  - task->start.tv_sec)  * 1000 +
		  (task->end.tv_usec - task->start.tv_usec) / 1000);
}

static inline int try_reset_assert(struct reset_control *rst)
{
	if (rst)
		return reset_control_assert(rst);
	return -EINVAL;
}

static inline int try_reset_deassert(struct reset_control *rst)
{
	if (rst)
		return reset_control_deassert(rst);
	return -EINVAL;
}

static inline int grf_combo_switch(const struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;
	int bits;
	u32 raw = 0;

	bits = 1 << pservice->mode_bit;
	if (pservice->grf) {
		regmap_read(pservice->grf, pservice->mode_ctrl, &raw);

		if (data->mode == VCODEC_RUNNING_MODE_HEVC)
			regmap_write(pservice->grf, pservice->mode_ctrl,
				     raw | bits | (bits << 16));
		else
			regmap_write(pservice->grf, pservice->mode_ctrl,
				     (raw & (~bits)) | (bits << 16));
	} else {
		vpu_err("no grf resource define, switch decoder failed\n");
		return -EINVAL;
	}

	return 0;
}

static void vcodec_enter_mode(struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_subdev_data *subdata, *n;

	/*
	 * For the RK3228H, it is not necessary to write a register to
	 * switch vpu combo mode, it is unsafe to write the grf.
	 */
	if (pservice->subcnt < 2 || pservice->mode_ctrl == 0) {
		if (data->mmu_dev && !test_bit(MMU_ACTIVATED, &data->state)) {
			set_bit(MMU_ACTIVATED, &data->state);

			if (atomic_read(&pservice->enabled)) {
				if (vcodec_iommu_attach(data->iommu_info))
					dev_err(data->dev,
						"vcodec service attach failed\n"
						);
				else
					/* Stop here is enough */
					return;
			}
		}
		return;
	}

	if (pservice->curr_mode == data->mode)
		return;

	vpu_debug(DEBUG_REGISTER, "vcodec enter mode %d\n", data->mode);
	list_for_each_entry_safe(subdata, n,
				 &pservice->subdev_list, lnk_service) {
		if (data != subdata && subdata->mmu_dev &&
		    test_bit(MMU_ACTIVATED, &subdata->state)) {
			clear_bit(MMU_ACTIVATED, &subdata->state);
			vcodec_iommu_detach(subdata->iommu_info);
		}
	}

	if (grf_combo_switch(data))
		return;

	if (data->mmu_dev && !test_bit(MMU_ACTIVATED, &data->state)) {
		set_bit(MMU_ACTIVATED, &data->state);
		if (atomic_read(&pservice->enabled))
			vcodec_iommu_attach(data->iommu_info);
		else
			/* FIXME BUG_ON should not be used in mass produce */
			BUG_ON(!atomic_read(&pservice->enabled));
	}

	pservice->prev_mode = pservice->curr_mode;
	pservice->curr_mode = data->mode;
}

static void vcodec_exit_mode(struct vpu_subdev_data *data)
{
	/*
	 * In case of VPU Combo, it require HW switch its running mode
	 * before the other HW component start work. set current HW running
	 * mode to none, can ensure HW switch to its reqired mode properly.
	 */
	data->pservice->curr_mode = VCODEC_RUNNING_MODE_NONE;
}

static int vpu_get_clk(struct vpu_service_info *pservice)
{
#if VCODEC_CLOCK_ENABLE
	struct device *dev = pservice->dev;

	switch (pservice->dev_id) {
	case VCODEC_DEVICE_ID_HEVC:
		/* We won't regard the power domain as clocks at 4.4 */
		pservice->pd_video = devm_clk_get(dev, "pd_hevc");
		if (IS_ERR(pservice->pd_video)) {
			pservice->pd_video = NULL;
			dev_dbg(dev, "failed on clk_get pd_hevc\n");
		}
	case VCODEC_DEVICE_ID_COMBO:
	case VCODEC_DEVICE_ID_RKVDEC:
		pservice->clk_cabac = devm_clk_get(dev, "clk_cabac");
		if (IS_ERR(pservice->clk_cabac)) {
			dev_err(dev, "failed on clk_get clk_cabac\n");
			pservice->clk_cabac = NULL;
		} else {
			pservice->clk_cabac_default_rate =
				clk_get_rate(pservice->clk_cabac);
		}
		pservice->clk_core = devm_clk_get(dev, "clk_core");
		if (IS_ERR(pservice->clk_core)) {
			dev_err(dev, "failed on clk_get clk_core\n");
			pservice->clk_core = NULL;
		} else {
			pservice->clk_core_default_rate =
				clk_get_rate(pservice->clk_core);
		}
	case VCODEC_DEVICE_ID_VPU:
		pservice->aclk_vcodec = devm_clk_get(dev, "aclk_vcodec");
		if (IS_ERR(pservice->aclk_vcodec)) {
			dev_err(dev, "failed on clk_get aclk_vcodec\n");
			return -1;
		} else {
			pservice->aclk_vcodec_default_rate =
				clk_get_rate(pservice->aclk_vcodec);
		}

		pservice->hclk_vcodec = devm_clk_get(dev, "hclk_vcodec");
		if (IS_ERR(pservice->hclk_vcodec)) {
			dev_err(dev, "failed on clk_get hclk_vcodec\n");
			pservice->hclk_vcodec = NULL;
			return -1;
		}
		if (pservice->pd_video == NULL) {
			pservice->pd_video = devm_clk_get(dev, "pd_video");
			if (IS_ERR(pservice->pd_video)) {
				pservice->pd_video = NULL;
				dev_dbg(dev, "do not have pd_video\n");
			}
		}
		break;
	default:
		break;
	}

	return 0;
#else
	return 0;
#endif
}

static void rkvdec_dvfs_set_clk(struct vpu_service_info *pservice,
				u32 vcodec_rate, u32 core_rate,
				u32 cabac_rate)
{
	struct devfreq *devfreq = pservice->devfreq;

	mutex_lock(&devfreq->lock);

	pservice->vcodec_rate = vcodec_rate;
	pservice->core_rate = core_rate;
	pservice->cabac_rate = cabac_rate;

	devfreq->min_freq = vcodec_rate;
	devfreq->max_freq = vcodec_rate;
	update_devfreq(devfreq);

	mutex_unlock(&devfreq->lock);
}

static void _vpu_reset(struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;

	dev_info(pservice->dev, "resetting...\n");
	WARN_ON(pservice->reg_codec != NULL);
	WARN_ON(pservice->reg_pproc != NULL);
	WARN_ON(pservice->reg_resev != NULL);
	pservice->reg_codec = NULL;
	pservice->reg_pproc = NULL;
	pservice->reg_resev = NULL;

#ifdef CONFIG_RESET_CONTROLLER

	rockchip_save_qos(pservice->dev);

	/* rk3328 need to use sip_smc_vpu_reset to reset vpu */
	if (pservice->hw_ops->reduce_freq == vcodec_reduce_freq_rk3328) {
		mutex_lock(&pservice->sip_reset_lock);
		sip_smc_vpu_reset(0, 0, 0);
		mutex_unlock(&pservice->sip_reset_lock);
	} else {
		rockchip_pmu_idle_request(pservice->dev, true);
		if (pservice->hw_ops->reduce_freq)
			pservice->hw_ops->reduce_freq(pservice);

		try_reset_assert(pservice->rst_niu_a);
		try_reset_assert(pservice->rst_niu_h);
		try_reset_assert(pservice->rst_v);
		try_reset_assert(pservice->rst_a);
		try_reset_assert(pservice->rst_h);
		try_reset_assert(pservice->rst_core);
		try_reset_assert(pservice->rst_cabac);
		udelay(5);

		try_reset_deassert(pservice->rst_niu_h);
		try_reset_deassert(pservice->rst_niu_a);
		try_reset_deassert(pservice->rst_v);
		try_reset_deassert(pservice->rst_h);
		try_reset_deassert(pservice->rst_a);
		try_reset_deassert(pservice->rst_core);
		try_reset_deassert(pservice->rst_cabac);

		rockchip_pmu_idle_request(pservice->dev, false);
	}

	rockchip_restore_qos(pservice->dev);

	dev_info(pservice->dev, "reset done\n");
#endif
}

static void vpu_reset(struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;

	_vpu_reset(data);
	if (data->mmu_dev && test_bit(MMU_ACTIVATED, &data->state)) {
		clear_bit(MMU_ACTIVATED, &data->state);
		clear_bit(MMU_PAGEFAULT, &data->state);
		if (atomic_read(&pservice->enabled)) {
			/* Need to reset iommu */
			vcodec_iommu_detach(data->iommu_info);
		} else {
			/* FIXME BUG_ON should not be used in mass produce */
			BUG_ON(!atomic_read(&pservice->enabled));
		}
	}

	atomic_set(&pservice->reset_request, 0);
	dev_info(pservice->dev, "reset done\n");
}

static void reg_deinit(struct vpu_subdev_data *data, struct vpu_reg *reg);
static void vpu_service_session_clear(struct vpu_subdev_data *data,
				      struct vpu_session *session)
{
	struct vpu_reg *reg, *n;

	list_for_each_entry_safe(reg, n, &session->waiting, session_link) {
		reg_deinit(data, reg);
	}
	list_for_each_entry_safe(reg, n, &session->running, session_link) {
		reg_deinit(data, reg);
	}
	list_for_each_entry_safe(reg, n, &session->done, session_link) {
		reg_deinit(data, reg);
	}
}

#if VCODEC_CLOCK_ENABLE
static unsigned long get_div_rate(struct clk *clock, int divide)
{
	struct clk *parent = clk_get_parent(clock);
	unsigned long rate = clk_get_rate(parent);

	return (rate / divide) + 1;
}
#endif

static void vpu_service_power_off(struct vpu_service_info *pservice)
{
	int total_running;
	struct vpu_subdev_data *data = NULL, *n;
	int ret = atomic_add_unless(&pservice->enabled, -1, 0);

	if (!ret)
		return;

	total_running = atomic_read(&pservice->total_running);
	if (total_running) {
		pr_alert("alert: power off when %d task running!!\n",
			 total_running);
		mdelay(50);
		pr_alert("alert: delay 50 ms for running task\n");
	}

	dev_dbg(pservice->dev, "power off...\n");

	udelay(5);

	list_for_each_entry_safe(data, n, &pservice->subdev_list, lnk_service) {
		if (data->mmu_dev && test_bit(MMU_ACTIVATED, &data->state)) {
			clear_bit(MMU_ACTIVATED, &data->state);
			vcodec_iommu_detach(data->iommu_info);
		}
	}
	pservice->curr_mode = VCODEC_RUNNING_MODE_NONE;
	if (pservice->hw_ops->power_off)
		pservice->hw_ops->power_off(pservice);

	atomic_add(1, &pservice->power_off_cnt);
	wake_unlock(&pservice->wake_lock);
	dev_dbg(pservice->dev, "power off done\n");
}

static inline void vpu_queue_power_off_work(struct vpu_service_info *pservice)
{
	queue_delayed_work(system_wq, &pservice->power_off_work,
			   VPU_POWER_OFF_DELAY);
}

static void vpu_power_off_work(struct work_struct *work_s)
{
	struct delayed_work *dlwork = container_of(work_s,
			struct delayed_work, work);
	struct vpu_service_info *pservice = container_of(dlwork,
			struct vpu_service_info, power_off_work);

	if (mutex_trylock(&pservice->lock)) {
		vpu_service_power_off(pservice);
		mutex_unlock(&pservice->lock);
	} else {
		/* Come back later if the device is busy... */
		vpu_queue_power_off_work(pservice);
	}
}

static void vpu_service_power_on(struct vpu_subdev_data *data,
				 struct vpu_service_info *pservice)
{
	int ret;
	ktime_t now = ktime_get();

	if (ktime_to_ns(ktime_sub(now, pservice->last)) > NSEC_PER_SEC ||
	    atomic_read(&pservice->power_on_cnt)) {
		/* NSEC_PER_SEC */
		cancel_delayed_work_sync(&pservice->power_off_work);
		vpu_queue_power_off_work(pservice);
		pservice->last = now;
	}
	ret = atomic_add_unless(&pservice->enabled, 1, 1);
	if (!ret)
		return;

	dev_dbg(pservice->dev, "power on\n");

	if (pservice->hw_ops->power_on)
		pservice->hw_ops->power_on(pservice);

	udelay(5);
	atomic_add(1, &pservice->power_on_cnt);
	wake_lock(&pservice->wake_lock);
}

static inline bool reg_check_interlace(struct vpu_reg *reg)
{
	u32 type = (reg->reg[3] & (1 << 23));

	return (type > 0);
}

static inline enum VPU_DEC_FMT reg_check_fmt(struct vpu_reg *reg)
{
	enum VPU_DEC_FMT type = (enum VPU_DEC_FMT)((reg->reg[3] >> 28) & 0xf);

	return type;
}

static inline int reg_probe_width(struct vpu_reg *reg)
{
	int width_in_mb = reg->reg[4] >> 23;

	return width_in_mb * 16;
}

static inline int reg_probe_hevc_y_stride(struct vpu_reg *reg)
{
	int y_virstride = reg->reg[8];

	return y_virstride;
}

static dma_addr_t vcodec_fd_to_iova(struct vpu_subdev_data *data,
				    struct vpu_session *session,
				    struct vpu_reg *reg, int fd)
{
	int hdl;
	int ret = 0;
	struct vcodec_mem_region *mem_region;

	hdl = vcodec_iommu_import(data->iommu_info, session, fd);
	if (hdl < 0)
		return hdl;

	mem_region = kzalloc(sizeof(*mem_region), GFP_KERNEL);
	if (mem_region == NULL) {
		vpu_err("allocate memory for iommu memory region failed\n");
		vcodec_iommu_free(data->iommu_info, session, hdl);
		return -ENOMEM;
	}

	mem_region->hdl = hdl;
	ret = vcodec_iommu_map_iommu(data->iommu_info, session, mem_region->hdl,
				     &mem_region->iova, &mem_region->len);
	if (ret < 0) {
		vpu_err("fd %d iommu map to device failed\n", fd);
		kfree(mem_region);
		vcodec_iommu_free(data->iommu_info, session, hdl);

		return -EFAULT;
	}
	INIT_LIST_HEAD(&mem_region->reg_lnk);
	list_add_tail(&mem_region->reg_lnk, &reg->mem_region_list);

	return mem_region->iova;
}

/*
 * NOTE: rkvdec/rkhevc put scaling list address in pps buffer hardware will read
 * it by pps id in video stream data.
 *
 * So we need to translate the address in iommu case. The address data is also
 * 10bit fd + 22bit offset mode.
 * Because userspace decoder do not give the pps id in the register file sets
 * kernel driver need to translate each scaling list address in pps buffer which
 * means 256 pps for H.264, 64 pps for H.265.
 *
 * In order to optimize the performance kernel driver ask userspace decoder to
 * set all scaling list address in pps buffer to the same one which will be used
 * on current decoding task. Then kernel driver can only translate the first
 * address then copy it all pps buffer.
 */
static int fill_scaling_list_pps(struct vpu_subdev_data *data,
				 struct vpu_reg *reg, int fd,
				 int offset, int count,
				 int pps_info_size,
				 int sub_addr_offset)
{
	struct dma_buf *dmabuf = NULL;
	void *vaddr = NULL;
	u8 *pps = NULL;
	u32 base = sub_addr_offset;
	u32 scaling_fd = 0;
	u32 scaling_offset;
	int ret = 0;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dmabuf)) {
		dev_err(data->dev, "invliad pps buffer\n");
		return -ENOENT;
	}

	ret = dma_buf_begin_cpu_access(dmabuf, 0, dmabuf->size,
				       DMA_FROM_DEVICE);
	if (ret) {
		dev_err(data->dev, "can't access the pps buffer\n");
		return ret;
	}

	vaddr = dma_buf_vmap(dmabuf);
	if (!vaddr) {
		dev_err(data->dev, "can't access the pps buffer\n");
		return -EIO;
	}
	pps = vaddr + offset;

	memcpy(&scaling_offset, pps + base, sizeof(scaling_offset));
	scaling_offset = le32_to_cpu(scaling_offset);

	scaling_fd = scaling_offset & 0x3ff;
	scaling_offset = scaling_offset >> 10;

	if (scaling_fd > 0) {
		int i = 0;
		u32 tmp = vcodec_fd_to_iova(data, reg->session, reg,
						   scaling_fd);

		if (IS_ERR_VALUE(tmp))
			return tmp;
		tmp += scaling_offset;
		tmp = cpu_to_le32(tmp);

		/* Fill the scaling list address in each pps entries */
		for (i = 0; i < count; i++, base += pps_info_size)
			memcpy(pps + base, &tmp, sizeof(tmp));
	}

	dma_buf_vunmap(dmabuf, vaddr);
	dma_buf_end_cpu_access(dmabuf, 0, dmabuf->size, DMA_FROM_DEVICE);
	dma_buf_put(dmabuf);

	return 0;
}

static int vcodec_bufid_to_iova(struct vpu_subdev_data *data,
				struct vpu_session *session,
				const u8 *tbl,
				int size, struct vpu_reg *reg,
				struct extra_info_for_iommu *ext_inf)
{
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_task_info *task = reg->task;
	enum FORMAT_TYPE type;
	int offset = 0;
	int ret = 0;
	int i;

	if (tbl == NULL || size <= 0) {
		dev_err(pservice->dev, "input arguments invalidate\n");
		return -EINVAL;
	}

	if (task->get_fmt) {
		type = task->get_fmt(reg->reg);
	} else {
		dev_err(pservice->dev, "invalid task with NULL get_fmt\n");
		return -EINVAL;
	}

	for (i = 0; i < size; i++) {
		int usr_fd = reg->reg[tbl[i]] & 0x3FF;
		dma_addr_t iova = 0;

		/* if userspace do not set the fd at this register, skip */
		if (usr_fd == 0)
			continue;

		/*
		 * special offset scale case
		 *
		 * This translation is for fd + offset translation.
		 * One register has 32bits. We need to transfer both buffer file
		 * handle and the start address offset so we packet file handle
		 * and offset together using below format.
		 *
		 *  0~9  bit for buffer file handle range 0 ~ 1023
		 * 10~31 bit for offset range 0 ~ 4M
		 *
		 * But on 4K case the offset can be larger the 4M
		 * So on H.264 4K vpu/vpu2 decoder we scale the offset by 16
		 * But MPEG4 will use the same register for colmv and it do not
		 * need scale.
		 *
		 * RKVdec do not have this issue.
		 */
		if ((type == FMT_H264D || type == FMT_VP9D) &&
		    task->reg_dir_mv > 0 && task->reg_dir_mv == tbl[i])
			offset = reg->reg[tbl[i]] >> 10 << 4;
		else
			offset = reg->reg[tbl[i]] >> 10;

		vpu_debug(DEBUG_IOMMU, "pos %3d fd %3d offset %10d i %d\n",
			  tbl[i], usr_fd, offset, i);

		/* Only apply for RKVDEC */
		if (task->reg_pps > 0 && task->reg_pps == tbl[i]) {
			int pps_info_count;
			int pps_info_size;
			int scaling_offset;

			switch (type) {
			case FMT_H264D: {
				pps_info_count = 256;
				pps_info_size = 32;
				scaling_offset = 23;
			} break;
			case FMT_H265D: {
				pps_info_count = 64;
				pps_info_size = 80;
				scaling_offset = 74;
				offset = 0;
			} break;
			default: {
				offset = 0;
				pps_info_count = 0;
				pps_info_size = 0;
				scaling_offset = 0;
			} break;
			}

			vpu_debug(DEBUG_PPS_FILL,
				  "scaling list filling parameter:\n");
			vpu_debug(DEBUG_PPS_FILL,
				  "pps_info_count = %d\n", pps_info_count);
			vpu_debug(DEBUG_PPS_FILL,
				  "pps_info_size = %d\n", pps_info_size);
			vpu_debug(DEBUG_PPS_FILL,
				  "scaling_list_addr_offset = %d\n",
				  scaling_offset);

			if (pps_info_count) {
				ret = fill_scaling_list_pps(data, reg, usr_fd,
							    offset,
							    pps_info_count,
							    pps_info_size,
							    scaling_offset);
				if (ret)
					return ret;
			}
		}

		iova = vcodec_fd_to_iova(data, session, reg, usr_fd);
		if (IS_ERR_VALUE(iova))
			return iova;

		/*
		 * special for vpu dec num 12: record decoded length
		 * hacking for decoded length
		 * NOTE: not a perfect fix, the fd is not recorded
		 */
		if (task->reg_len > 0 && task->reg_len == tbl[i]) {
			reg->dec_base = iova + offset;
			vpu_debug(DEBUG_REGISTER, "dec_set %08x\n",
				  reg->dec_base);
		}

		reg->reg[tbl[i]] = iova + offset;
	}

	if (ext_inf != NULL && ext_inf->magic == EXTRA_INFO_MAGIC) {
		for (i = 0; i < ext_inf->cnt; i++) {
			vpu_debug(DEBUG_IOMMU, "reg[%d] + offset %d\n",
				  ext_inf->elem[i].index,
				  ext_inf->elem[i].offset);
			reg->reg[ext_inf->elem[i].index] +=
				ext_inf->elem[i].offset;
		}
	}

	return 0;
}

static int vcodec_reg_address_translate(struct vpu_subdev_data *data,
					struct vpu_session *session,
					struct vpu_reg *reg,
					struct extra_info_for_iommu *ext_inf)
{
	struct vpu_service_info *pservice = data->pservice;
	enum FORMAT_TYPE type = reg->task->get_fmt(reg->reg);

	if (type < FMT_TYPE_BUTT) {
		const struct vpu_trans_info *info = &reg->trans[type];
		const u8 *tbl = info->table;
		int size = info->count;

		return vcodec_bufid_to_iova(data, session, tbl, size, reg,
					    ext_inf);
	}

	dev_err(pservice->dev, "found invalid format type!\n");
	return -EINVAL;
}

static struct vpu_reg *reg_init(struct vpu_subdev_data *data,
				struct vpu_session *session,
				void __user *src, u32 size)
{
	struct vpu_service_info *pservice = data->pservice;
	int extra_size = 0;
	struct extra_info_for_iommu extra_info;
	struct vpu_reg *reg = kzalloc(sizeof(*reg) + data->reg_size,
				      GFP_KERNEL);

	vpu_debug_enter();

	if (!reg) {
		vpu_err("error: kzalloc failed\n");
		return NULL;
	}

	if (size > data->reg_size) {
		extra_size = size - data->reg_size;
		size = data->reg_size;
	}
	reg->session = session;
	reg->data = data;
	reg->type = session->type;
	reg->size = size;
	reg->freq = VPU_FREQ_DEFAULT;
	reg->task = &data->task_info[session->type];
	reg->trans = data->trans_info;
	reg->reg = (u32 *)&reg[1];
	INIT_LIST_HEAD(&reg->session_link);
	INIT_LIST_HEAD(&reg->status_link);

	INIT_LIST_HEAD(&reg->mem_region_list);

	if (copy_from_user(&reg->reg[0], (void __user *)src, size)) {
		vpu_err("error: copy_from_user failed\n");
		kfree(reg);
		return NULL;
	}

	if (copy_from_user(&extra_info, (u8 *)src + size, extra_size)) {
		vpu_err("error: copy_from_user failed\n");
		kfree(reg);
		return NULL;
	}

	mutex_lock(&pservice->reset_lock);
	if (vcodec_reg_address_translate(data, session, reg, &extra_info) < 0) {
		int i = 0;

		mutex_unlock(&pservice->reset_lock);
		vpu_err("error: translate reg address failed, dumping regs\n");
		for (i = 0; i < size >> 2; i++)
			dev_err(pservice->dev, "reg[%02d]: %08x\n",
				i, reg->reg[i]);

		kfree(reg);
		return NULL;
	}
	mutex_unlock(&pservice->reset_lock);

	mutex_lock(&pservice->lock);
	list_add_tail(&reg->status_link, &pservice->waiting);
	list_add_tail(&reg->session_link, &session->waiting);
	mutex_unlock(&pservice->lock);

	if (pservice->auto_freq && pservice->hw_ops->get_freq)
		pservice->hw_ops->get_freq(data, reg);

	vpu_debug_leave();

	return reg;
}

static void reg_deinit(struct vpu_subdev_data *data, struct vpu_reg *reg)
{
	struct vpu_service_info *pservice = data->pservice;
	struct vcodec_mem_region *mem_region = NULL, *n;

	list_del_init(&reg->session_link);
	list_del_init(&reg->status_link);
	if (reg == pservice->reg_codec)
		pservice->reg_codec = NULL;
	if (reg == pservice->reg_pproc)
		pservice->reg_pproc = NULL;

	/* release memory region attach to this registers table. */
	list_for_each_entry_safe(mem_region, n,
			&reg->mem_region_list, reg_lnk) {
		vcodec_iommu_unmap_iommu(data->iommu_info, reg->session,
					 mem_region->hdl);
		vcodec_iommu_free(data->iommu_info, reg->session,
				  mem_region->hdl);
		list_del_init(&mem_region->reg_lnk);
		kfree(mem_region);
	}

	kfree(reg);
}

static void reg_from_wait_to_run(struct vpu_service_info *pservice,
				 struct vpu_reg *reg)
{
	vpu_debug_enter();
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &pservice->running);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->running);
	vpu_debug_leave();
}

static void reg_copy_from_hw(struct vpu_reg *reg, u32 *src, u32 count)
{
	int i;
	u32 *dst = reg->reg;

	vpu_debug_enter();
	for (i = 0; i < count; i++, src++)
		*dst++ = readl_relaxed(src);

	dst = (u32 *)&reg->reg[0];
	for (i = 0; i < count; i++)
		vpu_debug(DEBUG_GET_REG, "get reg[%02d] %08x\n", i, dst[i]);

	vpu_debug_leave();
}

static void reg_from_run_to_done(struct vpu_subdev_data *data,
				 struct vpu_reg *reg)
{
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_hw_info *hw_info = data->hw_info;
	struct vpu_task_info *task = reg->task;

	vpu_debug_enter();

	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &pservice->done);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->done);

	switch (reg->type) {
	case VPU_ENC: {
		pservice->reg_codec = NULL;
		reg_copy_from_hw(reg, data->enc_dev.regs, hw_info->enc_reg_num);
		reg->reg[task->reg_irq] = pservice->irq_status;
	} break;
	case VPU_DEC: {
		pservice->reg_codec = NULL;
		reg_copy_from_hw(reg, data->dec_dev.regs, hw_info->dec_reg_num);

		/* revert hack for decoded length */
		if (task->reg_len > 0) {
			int reg_len = task->reg_len;
			u32 dec_get = reg->reg[reg_len];
			s32 dec_length = dec_get - reg->dec_base;

			vpu_debug(DEBUG_REGISTER,
				  "dec_get %08x dec_length %d\n",
				  dec_get, dec_length);
			reg->reg[reg_len] = dec_length << 10;
		}

		reg->reg[task->reg_irq] = pservice->irq_status;
	} break;
	case VPU_PP: {
		pservice->reg_pproc = NULL;
		reg_copy_from_hw(reg, data->dec_dev.regs, hw_info->dec_reg_num);
		writel_relaxed(0, data->dec_dev.regs + task->reg_irq);
	} break;
	case VPU_DEC_PP: {
		u32 pipe_mode;
		u32 *regs = data->dec_dev.regs;

		pservice->reg_codec = NULL;
		pservice->reg_pproc = NULL;

		reg_copy_from_hw(reg, data->dec_dev.regs, hw_info->dec_reg_num);

		/* NOTE: remove pp pipeline mode flag first */
		pipe_mode = readl_relaxed(regs + task->reg_pipe);
		pipe_mode &= ~task->pipe_mask;
		writel_relaxed(pipe_mode, regs + task->reg_pipe);

		/* revert hack for decoded length */
		if (task->reg_len > 0) {
			int reg_len = task->reg_len;
			u32 dec_get = reg->reg[reg_len];
			s32 dec_length = dec_get - reg->dec_base;

			vpu_debug(DEBUG_REGISTER,
				  "dec_get %08x dec_length %d\n",
				  dec_get, dec_length);
			reg->reg[reg_len] = dec_length << 10;
		}

		reg->reg[task->reg_irq] = pservice->irq_status;
	} break;
	default: {
		vpu_err("error: copy reg from hw with unknown type %d\n",
			reg->type);
	} break;
	}
	vcodec_exit_mode(data);

	atomic_sub(1, &reg->session->task_running);
	atomic_sub(1, &pservice->total_running);
	wake_up(&reg->session->wait);

	vpu_debug_leave();
}

static void reg_copy_to_hw(struct vpu_subdev_data *data, struct vpu_reg *reg)
{
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_task_info *task = reg->task;
	struct vpu_hw_info *hw_info = data->hw_info;
	int i;
	u32 *src = (u32 *)&reg->reg[0];
	u32 enable_mask = task->enable_mask;
	u32 gating_mask = task->gating_mask;
	u32 reg_en = task->reg_en;

	vpu_debug_enter();

	atomic_add(1, &pservice->total_running);
	atomic_add(1, &reg->session->task_running);

	if (pservice->auto_freq && pservice->hw_ops->set_freq)
		pservice->hw_ops->set_freq(pservice, reg);

	vcodec_enter_mode(data);

	switch (reg->type) {
	case VPU_ENC: {
		u32 *dst = data->enc_dev.regs;
		u32 base = 0;
		u32 end  = hw_info->enc_reg_num;
		/* u32 reg_gating = task->reg_gating; */

		pservice->reg_codec = reg;

		vpu_debug(DEBUG_TASK_INFO,
			  "reg: base %3d end %d en %2d mask: en %x gate %x\n",
			  base, end, reg_en, enable_mask, gating_mask);

		VEPU_CLEAN_CACHE(dst);

		if (debug & DEBUG_SET_REG)
			for (i = base; i < end; i++)
				vpu_debug(DEBUG_SET_REG, "set reg[%02d] %08x\n",
					  i, src[i]);

		/*
		 * NOTE: encoder need to setup mode first
		 */
		writel_relaxed(src[reg_en] & enable_mask, dst + reg_en);

		/* NOTE: encoder gating is not on enable register */
		/* src[reg_gating] |= gating_mask; */

		for (i = base; i < end; i++) {
			if (i != reg_en)
				writel_relaxed(src[i], dst + i);
		}

		writel(src[reg_en], dst + reg_en);
		dsb(sy);

		time_record(reg->task, 0);
	} break;
	case VPU_DEC: {
		u32 *dst = data->dec_dev.regs;
		u32 len = hw_info->dec_reg_num;
		u32 base = hw_info->base_dec;
		u32 end  = hw_info->end_dec;

		pservice->reg_codec = reg;

		vpu_debug(DEBUG_TASK_INFO,
			  "reg: base %3d end %d en %2d mask: en %x gate %x\n",
			  base, end, reg_en, enable_mask, gating_mask);

		VDPU_CLEAN_CACHE(dst);

		/* on rkvdec set cache size to 64byte */
		if (pservice->dev_id == VCODEC_DEVICE_ID_RKVDEC) {
			u32 *cache_base = dst + 0x100;
			u32 val = (debug & DEBUG_CACHE_32B) ? (0x3) : (0x13);

			writel_relaxed(val, cache_base + 0x07);
			writel_relaxed(val, cache_base + 0x17);
		}

		if (debug & DEBUG_SET_REG)
			for (i = 0; i < len; i++)
				vpu_debug(DEBUG_SET_REG, "set reg[%02d] %08x\n",
					  i, src[i]);
		/*
		 * NOTE: The end register is invalid. Do NOT write to it
		 *       Also the base register must be written
		 */
		for (i = base; i < end; i++) {
			if (i != reg_en)
				writel_relaxed(src[i], dst + i);
		}

		if (pservice->hw_var && pservice->hw_var->config)
			pservice->hw_var->config(data);

		writel(src[reg_en] | gating_mask, dst + reg_en);
		dsb(sy);

		time_record(reg->task, 0);
	} break;
	case VPU_PP: {
		u32 *dst = data->dec_dev.regs;
		u32 base = hw_info->base_pp;
		u32 end  = hw_info->end_pp;

		pservice->reg_pproc = reg;

		vpu_debug(DEBUG_TASK_INFO,
			  "reg: base %3d end %d en %2d mask: en %x gate %x\n",
			  base, end, reg_en, enable_mask, gating_mask);

		if (debug & DEBUG_SET_REG)
			for (i = base; i < end; i++)
				vpu_debug(DEBUG_SET_REG, "set reg[%02d] %08x\n",
					  i, src[i]);

		for (i = base; i < end; i++) {
			if (i != reg_en)
				writel_relaxed(src[i], dst + i);
		}

		writel(src[reg_en] | gating_mask, dst + reg_en);
		dsb(sy);

		time_record(reg->task, 0);
	} break;
	case VPU_DEC_PP: {
		u32 *dst = data->dec_dev.regs;
		u32 base = hw_info->base_dec_pp;
		u32 end  = hw_info->end_dec_pp;

		pservice->reg_codec = reg;
		pservice->reg_pproc = reg;

		vpu_debug(DEBUG_TASK_INFO,
			  "reg: base %3d end %d en %2d mask: en %x gate %x\n",
			  base, end, reg_en, enable_mask, gating_mask);

		/* VDPU_SOFT_RESET(dst); */
		VDPU_CLEAN_CACHE(dst);

		if (debug & DEBUG_SET_REG)
			for (i = base; i < end; i++)
				vpu_debug(DEBUG_SET_REG, "set reg[%02d] %08x\n",
					  i, src[i]);

		for (i = base; i < end; i++) {
			if (i != reg_en)
				writel_relaxed(src[i], dst + i);
		}

		/* NOTE: dec output must be disabled */

		writel(src[reg_en] | gating_mask, dst + reg_en);
		dsb(sy);

		time_record(reg->task, 0);
	} break;
	default: {
		vpu_err("error: unsupport session type %d", reg->type);

		atomic_sub(1, &pservice->total_running);
		atomic_sub(1, &reg->session->task_running);
	} break;
	}

	vpu_debug_leave();
}

static void try_set_reg(struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;
	int reset_request = atomic_read(&pservice->reset_request);
	struct vpu_reg *reg_codec = pservice->reg_codec;
	struct vpu_reg *reg_pproc = pservice->reg_pproc;
	bool change_able = (!reg_codec) && (!reg_pproc);

	vpu_debug_enter();

	mutex_lock(&pservice->shutdown_lock);
	if (atomic_read(&pservice->service_on) == 0) {
		mutex_unlock(&pservice->shutdown_lock);
		return;
	}

	if (change_able && reset_request) {
		vpu_service_power_on(data, pservice);
		mutex_lock(&pservice->reset_lock);
		vpu_reset(data);
		mutex_unlock(&pservice->reset_lock);
	}

	if (!list_empty(&pservice->waiting)) {
		int can_set = 0;
		struct vpu_reg *reg = list_entry(pservice->waiting.next,
				struct vpu_reg, status_link);

		vpu_service_power_on(data, pservice);

		if (change_able) {
			switch (reg->type) {
			case VPU_ENC:
			case VPU_DEC:
			case VPU_PP:
			case VPU_DEC_PP: {
				can_set = 1;
			} break;
			default: {
				dev_err(pservice->dev,
					"undefined reg type %d\n",
					reg->type);
			} break;
			}
		}

		if (can_set) {
			reg_from_wait_to_run(pservice, reg);
			reg_copy_to_hw(reg->data, reg);
		}
	} else {
		if (pservice->hw_ops->reduce_freq)
			pservice->hw_ops->reduce_freq(pservice);
	}

	mutex_unlock(&pservice->shutdown_lock);
	vpu_debug_leave();
}

static void vpu_set_register_work(struct work_struct *work_s)
{
	struct vpu_subdev_data *data = container_of(work_s,
						    struct vpu_subdev_data,
						    set_work);
	struct vpu_service_info *pservice = data->pservice;

	mutex_lock(&pservice->lock);
	try_set_reg(data);
	mutex_unlock(&pservice->lock);
}

static int return_reg(struct vpu_subdev_data *data,
		      struct vpu_reg *reg, u32 __user *dst)
{
	struct vpu_hw_info *hw_info = data->hw_info;
	size_t size = reg->size;
	u32 base;

	vpu_debug_enter();
	switch (reg->type) {
	case VPU_ENC: {
		base = 0;
	} break;
	case VPU_DEC: {
		base = hw_info->base_dec_pp;
	} break;
	case VPU_PP: {
		base = hw_info->base_pp;
	} break;
	case VPU_DEC_PP: {
		base = hw_info->base_dec_pp;
	} break;
	default: {
		vpu_err("error: copy reg to user with unknown type %d\n",
			reg->type);
		return -EFAULT;
	} break;
	}

	if (copy_to_user(dst, &reg->reg[base], size)) {
		vpu_err("error: copy_to_user failed\n");
		return -EFAULT;
	}

	reg_deinit(data, reg);
	vpu_debug_leave();
	return 0;
}

static long vpu_service_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg)
{
	struct vpu_subdev_data *data =
		container_of(filp->f_path.dentry->d_inode->i_cdev,
			     struct vpu_subdev_data, cdev);
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_session *session = (struct vpu_session *)filp->private_data;

	vpu_debug_enter();

	vpu_debug(DEBUG_IOCTL, "cmd %x, VPU_IOC_SET_CLIENT_TYPE %x\n", cmd,
		  (u32)VPU_IOC_SET_CLIENT_TYPE);

	if (NULL == session)
		return -EINVAL;

	switch (cmd) {
	case VPU_IOC_SET_CLIENT_TYPE: {
		int secure_mode;

		secure_mode = (arg & 0xffff0000) >> 16;
		session->type = (enum VPU_CLIENT_TYPE)(arg & 0xffff);
		atomic_set(&pservice->secure_mode, secure_mode);

		session->type = (enum VPU_CLIENT_TYPE)arg;
		vpu_debug(DEBUG_IOCTL, "pid %d set client type %d, secure mode = %d\n",
			  session->pid, session->type, secure_mode);
	} break;
	case VPU_IOC_GET_HW_FUSE_STATUS: {
		struct vpu_request req;

		vpu_debug(DEBUG_IOCTL, "pid %d get hw status %d\n",
			  session->pid, session->type);
		if (copy_from_user(&req, (void __user *)arg, sizeof(req))) {
			vpu_err("error: get hw status copy_from_user failed\n");
			return -EFAULT;
		} else {
			void *config = (session->type != VPU_ENC) ?
				       ((void *)&pservice->dec_config) :
				       ((void *)&pservice->enc_config);
			size_t size = (session->type != VPU_ENC) ?
				      (sizeof(struct vpu_dec_config)) :
				      (sizeof(struct vpu_enc_config));
			if (copy_to_user((void __user *)req.req,
					 config, size)) {
				vpu_err("error: get hw status copy_to_user failed type %d\n",
					session->type);
				return -EFAULT;
			}
		}
	} break;
	case VPU_IOC_SET_REG: {
		struct vpu_request req;
		struct vpu_reg *reg;

		vpu_debug(DEBUG_IOCTL, "pid %d set reg type %d\n",
			  session->pid, session->type);

		if (atomic_read(&pservice->secure_mode) == 1) {
			vpu_service_power_on(data, pservice);
			pservice->wait_secure_isr = &session->wait;
			if (!pservice->secure_isr &&
			    !pservice->secure_irq_status)
				enable_irq(data->irq_dec);
			break;
		}

		if (copy_from_user(&req, (void __user *)arg,
				   sizeof(struct vpu_request))) {
			vpu_err("error: set reg copy_from_user failed\n");
			return -EFAULT;
		}

		reg = reg_init(data, session, (void __user *)req.req, req.size);
		if (NULL == reg) {
			return -EFAULT;
		} else {
			queue_work(pservice->set_workq, &data->set_work);
		}
	} break;
	case VPU_IOC_GET_REG: {
		struct vpu_request req;
		struct vpu_reg *reg;
		int ret;

		vpu_debug(DEBUG_IOCTL, "pid %d get reg type %d\n",
			  session->pid, session->type);

		if (atomic_read(&pservice->secure_mode) == 1) {
			ret = wait_event_timeout(session->wait,
						 pservice->secure_isr,
						 VPU_TIMEOUT_DELAY);
			if (ret < 0)
				pr_info("warning: secure wait timeout\n");
			pservice->secure_isr = false;
			break;
		}

		if (copy_from_user(&req, (void __user *)arg,
				   sizeof(struct vpu_request))) {
			vpu_err("error: get reg copy_from_user failed\n");
			return -EFAULT;
		}

		ret = wait_event_timeout(session->wait,
					 !list_empty(&session->done),
					 VPU_TIMEOUT_DELAY);

		while (atomic_read(&pservice->reset_request))
			msleep(10);

		if (!list_empty(&session->done)) {
			if (ret < 0)
				vpu_err("warning: pid %d wait task error ret %d\n",
					session->pid, ret);
			ret = 0;
		} else {
			if (unlikely(ret < 0)) {
				vpu_err("error: pid %d wait task ret %d\n",
					session->pid, ret);
			} else if (ret == 0) {
				vpu_err("error: pid %d wait %d task done timeout\n",
					session->pid,
					atomic_read(&session->task_running));
				ret = -ETIMEDOUT;
			}
		}

		if (ret < 0) {
			int task_running = atomic_read(&session->task_running);

			/*
			 * if reaching here, no irq return, and need to do
			 * vpu_reset immediately. there is no need to care
			 * reset_lock at this situation.
			 */
			mutex_lock(&pservice->lock);
			if (task_running) {
				atomic_set(&session->task_running, 0);
				atomic_sub(task_running,
					   &pservice->total_running);
				dev_err(pservice->dev,
					"%d task is running but not return, reset hardware...",
					task_running);
				vpu_reset(data);
			}
			vpu_service_session_clear(data, session);
			mutex_unlock(&pservice->lock);

			return ret;
		}
		mutex_lock(&pservice->lock);
		reg = list_entry(session->done.next,
				 struct vpu_reg, session_link);
		return_reg(data, reg, (u32 __user *)req.req);
		mutex_unlock(&pservice->lock);
	} break;
	case VPU_IOC_PROBE_IOMMU_STATUS: {
		/* It must be 1, keeping backward compatibility */
		int iommu_enable = 1;

		vpu_debug(DEBUG_IOCTL, "VPU_IOC_PROBE_IOMMU_STATUS\n");

		if (copy_to_user((void __user *)arg,
				 &iommu_enable, sizeof(int))) {
			vpu_err("error: iommu status copy_to_user failed\n");
			return -EFAULT;
		}
	} break;
	case VPU_IOC_SET_DRIVER_DATA: {
		u32 val;

		if (copy_from_user(&val, (void __user *)arg,
				   sizeof(int))) {
			vpu_err("error: COMPAT_VPU_IOC_SET_DRIVER_DATA copy_from_user failed\n");
			return -EFAULT;
		}

		if (pservice->grf)
			regmap_write(pservice->grf, 0x5d8, val);
	} break;
	default: {
		vpu_err("error: unknow vpu service ioctl cmd %x\n", cmd);
		return -ENOIOCTLCMD;
	} break;
	}

	vpu_debug_leave();
	return 0;
}

#ifdef CONFIG_COMPAT
#define VPU_IOC_SET_CLIENT_TYPE32          _IOW(VPU_IOC_MAGIC, 1, u32)
#define VPU_IOC_GET_HW_FUSE_STATUS32       _IOW(VPU_IOC_MAGIC, 2, \
						compat_ulong_t)
#define VPU_IOC_SET_REG32                  _IOW(VPU_IOC_MAGIC, 3, \
						compat_ulong_t)
#define VPU_IOC_GET_REG32                  _IOW(VPU_IOC_MAGIC, 4, \
						compat_ulong_t)
#define VPU_IOC_PROBE_IOMMU_STATUS32       _IOR(VPU_IOC_MAGIC, 5, u32)
#define VPU_IOC_SET_DRIVER_DATA32		_IOW(VPU_IOC_MAGIC, 64, u32)

static long native_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = -ENOIOCTLCMD;

	if (file->f_op->unlocked_ioctl)
		ret = file->f_op->unlocked_ioctl(file, cmd, arg);

	return ret;
}

static long compat_vpu_service_ioctl(struct file *file, unsigned int cmd,
				     unsigned long arg)
{
	struct vpu_request req;
	void __user *up = compat_ptr(arg);
	int compatible_arg = 1;
	long err = 0;

	vpu_debug_enter();
	vpu_debug(DEBUG_IOCTL, "cmd %x, VPU_IOC_SET_CLIENT_TYPE32 %x\n", cmd,
		  (u32)VPU_IOC_SET_CLIENT_TYPE32);
	/* First, convert the command. */
	switch (cmd) {
	case VPU_IOC_SET_CLIENT_TYPE32:
		cmd = VPU_IOC_SET_CLIENT_TYPE;
		break;
	case VPU_IOC_GET_HW_FUSE_STATUS32:
		cmd = VPU_IOC_GET_HW_FUSE_STATUS;
		break;
	case VPU_IOC_SET_REG32:
		cmd = VPU_IOC_SET_REG;
		break;
	case VPU_IOC_GET_REG32:
		cmd = VPU_IOC_GET_REG;
		break;
	case VPU_IOC_PROBE_IOMMU_STATUS32:
		cmd = VPU_IOC_PROBE_IOMMU_STATUS;
		break;
	case VPU_IOC_SET_DRIVER_DATA32:
		cmd = VPU_IOC_SET_DRIVER_DATA;
		break;
	}

	switch (cmd) {
	case VPU_IOC_SET_REG:
	case VPU_IOC_GET_REG:
	case VPU_IOC_GET_HW_FUSE_STATUS: {
		compat_uptr_t req_ptr;
		struct compat_vpu_request __user *req32 = NULL;

		req32 = (struct compat_vpu_request __user *)up;
		memset(&req, 0, sizeof(req));

		if (get_user(req_ptr, &req32->req) ||
		    get_user(req.size, &req32->size)) {
			vpu_err("error: compat get hw status copy_from_user failed\n");
			return -EFAULT;
		}
		req.req = compat_ptr(req_ptr);
		compatible_arg = 0;
	} break;
	}

	if (compatible_arg) {
		err = native_ioctl(file, cmd, (unsigned long)up);
	} else {
		mm_segment_t old_fs = get_fs();

		set_fs(KERNEL_DS);
		err = native_ioctl(file, cmd, (unsigned long)&req);
		set_fs(old_fs);
	}

	vpu_debug_leave();
	return err;
}
#endif

static int vpu_service_check_hw(struct vpu_subdev_data *data)
{
	int ret = -EINVAL, i = 0;
	u32 hw_id = readl_relaxed(data->regs);

	hw_id = (hw_id >> 16) & 0xFFFF;
	dev_info(data->dev, "checking hw id %x\n", hw_id);
	data->hw_info = NULL;

	for (i = 0; i < ARRAY_SIZE(vcodec_info_set); i++) {
		const struct vcodec_info *info = &vcodec_info_set[i];

		if (hw_id == info->hw_id) {
			data->hw_id = info->hw_id;
			data->hw_info = info->hw_info;
			data->task_info = info->task_info;
			data->trans_info = info->trans_info;
			ret = 0;
			break;
		}
	}
	return ret;
}

static int vpu_service_open(struct inode *inode, struct file *filp)
{
	struct vpu_subdev_data *data = container_of(
			inode->i_cdev, struct vpu_subdev_data, cdev);
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_session *session = NULL;

	vpu_debug_enter();

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session) {
		vpu_err("error: unable to allocate memory for vpu_session.");
		return -ENOMEM;
	}

	data->iommu_info->debug_level = debug;

	session->type	= VPU_TYPE_BUTT;
	session->pid	= current->pid;
	INIT_LIST_HEAD(&session->waiting);
	INIT_LIST_HEAD(&session->running);
	INIT_LIST_HEAD(&session->done);
	INIT_LIST_HEAD(&session->list_session);
	init_waitqueue_head(&session->wait);
	atomic_set(&session->task_running, 0);
	mutex_lock(&pservice->lock);
	list_add_tail(&session->list_session, &pservice->session);
	filp->private_data = (void *)session;
	mutex_unlock(&pservice->lock);

	dev_dbg(pservice->dev, "dev opened\n");
	vpu_debug_leave();
	return nonseekable_open(inode, filp);
}

static int vpu_service_release(struct inode *inode, struct file *filp)
{
	struct vpu_subdev_data *data = container_of(
			inode->i_cdev, struct vpu_subdev_data, cdev);
	struct vpu_service_info *pservice = data->pservice;
	int task_running;
	struct vpu_session *session = (struct vpu_session *)filp->private_data;

	vpu_debug_enter();
	if (NULL == session)
		return -EINVAL;

	task_running = atomic_read(&session->task_running);
	if (task_running) {
		dev_err(pservice->dev,
			"error: session %d still has %d task running when closing\n",
			session->pid, task_running);
		msleep(50);
	}
	wake_up(&session->wait);

	if (atomic_read(&pservice->secure_mode)) {
		atomic_set(&pservice->secure_mode, 0);
		if (!pservice->secure_irq_status) {
			enable_irq(data->irq_dec);
			pservice->secure_irq_status = true;
		}
	}

	mutex_lock(&pservice->lock);
	/* remove this filp from the asynchronusly notified filp's */
	list_del_init(&session->list_session);
	vpu_service_session_clear(data, session);
	vcodec_iommu_clear(data->iommu_info, session);
	kfree(session);
	filp->private_data = NULL;
	mutex_unlock(&pservice->lock);

	dev_info(pservice->dev, "closed\n");
	vpu_debug_leave();
	return 0;
}

static const struct file_operations vpu_service_fops = {
	.unlocked_ioctl = vpu_service_ioctl,
	.open		= vpu_service_open,
	.release	= vpu_service_release,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = compat_vpu_service_ioctl,
#endif
};

static irqreturn_t vdpu_irq(int irq, void *dev_id);
static irqreturn_t vdpu_isr(int irq, void *dev_id);
static irqreturn_t vepu_irq(int irq, void *dev_id);
static irqreturn_t vepu_isr(int irq, void *dev_id);
static void get_hw_info(struct vpu_subdev_data *data);

static struct device *rockchip_get_sysmmu_dev(const char *compt)
{
	struct device_node *dn = NULL;
	struct platform_device *pd = NULL;
	struct device *ret = NULL;

	dn = of_find_compatible_node(NULL, NULL, compt);
	if (!dn) {
		pr_err("can't find device node %s \r\n", compt);
		return NULL;
	}

	pd = of_find_device_by_node(dn);
	if (!pd) {
		pr_err("can't find platform device in device node %s\n", compt);
		return  NULL;
	}
	ret = &pd->dev;

	return ret;
}

/* special hw ops */
static void vcodec_power_on_default(struct vpu_service_info *pservice)
{
#if VCODEC_CLOCK_ENABLE
	if (pservice->aclk_vcodec)
		clk_prepare_enable(pservice->aclk_vcodec);
	if (pservice->hclk_vcodec)
		clk_prepare_enable(pservice->hclk_vcodec);
	if (pservice->clk_core)
		clk_prepare_enable(pservice->clk_core);
	if (pservice->clk_cabac)
		clk_prepare_enable(pservice->clk_cabac);
	if (pservice->pd_video)
		clk_prepare_enable(pservice->pd_video);
#endif
	pm_runtime_get_sync(pservice->dev);
}

static void vcodec_power_on_rk312x(struct vpu_service_info *pservice)
{
#define BIT_VCODEC_CLK_SEL	BIT(10)
	if (of_machine_is_compatible("rockchip,rk3126") ||
	    of_machine_is_compatible("rockchip,rk3128"))
		regmap_write(pservice->grf, RK312X_GRF_SOC_CON1,
			     BIT_VCODEC_CLK_SEL |
			     (BIT_VCODEC_CLK_SEL << 16));

	vcodec_power_on_default(pservice);
}

static void vcodec_power_on_rk322x(struct vpu_service_info *pservice)
{
	struct devfreq_dev_status *stat;
	unsigned long rate = 300 * MHZ;

	if (pservice->dev_id == VCODEC_DEVICE_ID_RKVDEC)
		rate = 500 * MHZ;

	if (pservice->devfreq) {
		stat = &pservice->devfreq->last_status;
		stat->busy_time = 1;
		stat->total_time = 1;
		rkvdec_dvfs_set_clk(pservice,
				    rate,
				    300 * MHZ,
				    300 * MHZ);
	} else {
		clk_set_rate(pservice->aclk_vcodec, rate);
		clk_set_rate(pservice->clk_core, 300 * MHZ);
		clk_set_rate(pservice->clk_cabac, 300 * MHZ);
	}

	vcodec_power_on_default(pservice);
}

static void vcodec_power_on_rk3328(struct vpu_service_info *pservice)
{
	struct devfreq_dev_status *stat;

	if (pservice->devfreq) {
		stat = &pservice->devfreq->last_status;
		stat->busy_time = 1;
		stat->total_time = 1;
		rkvdec_dvfs_set_clk(pservice,
				    RK3328_VCODEC_RATE_ON,
				    RK3328_CODE_RATE_ON,
				    RK3328_CABAC_RATE_ON);
	} else {
		clk_set_rate(pservice->aclk_vcodec, RK3328_VCODEC_RATE_ON);
		clk_set_rate(pservice->clk_core, RK3328_CODE_RATE_ON);
		clk_set_rate(pservice->clk_cabac, RK3328_CABAC_RATE_ON);
	}

	vcodec_power_on_default(pservice);
}

static void vcodec_power_off_default(struct vpu_service_info *pservice)
{
	if (pservice->pd_video)
		clk_disable_unprepare(pservice->pd_video);
	if (pservice->hclk_vcodec)
		clk_disable_unprepare(pservice->hclk_vcodec);
	if (pservice->aclk_vcodec)
		clk_disable_unprepare(pservice->aclk_vcodec);
	if (pservice->clk_core)
		clk_disable_unprepare(pservice->clk_core);
	if (pservice->clk_cabac)
		clk_disable_unprepare(pservice->clk_cabac);
	pm_runtime_put(pservice->dev);
}

static void vcodec_power_off_rk322x(struct vpu_service_info *pservice)
{
	struct devfreq_dev_status *stat;

	vcodec_power_off_default(pservice);

	if (pservice->devfreq) {
		stat = &pservice->devfreq->last_status;
		stat->busy_time = 0;
		stat->total_time = 1;
		rkvdec_dvfs_set_clk(pservice,
				    get_div_rate(pservice->aclk_vcodec, 32),
				    get_div_rate(pservice->clk_core, 32),
				    get_div_rate(pservice->clk_cabac, 32));
	} else {
		clk_set_rate(pservice->aclk_vcodec,
			     get_div_rate(pservice->aclk_vcodec, 32));
		clk_set_rate(pservice->clk_core,
			     get_div_rate(pservice->clk_core, 32));
		clk_set_rate(pservice->clk_cabac,
			     get_div_rate(pservice->clk_cabac, 32));
	}
}

static void vcodec_power_off_rk3328(struct vpu_service_info *pservice)
{
	struct devfreq_dev_status *stat;

	vcodec_power_off_default(pservice);

	if (pservice->devfreq) {
		stat = &pservice->devfreq->last_status;
		stat->busy_time = 0;
		stat->total_time = 1;
		rkvdec_dvfs_set_clk(pservice,
				    RK3328_VCODEC_RATE_OFF,
				    RK3328_CODE_RATE_OFF,
				    RK3328_CABAC_RATE_OFF);
	} else {
		clk_set_rate(pservice->aclk_vcodec, RK3328_VCODEC_RATE_OFF);
		clk_set_rate(pservice->clk_core, RK3328_CODE_RATE_OFF);
		clk_set_rate(pservice->clk_cabac, RK3328_CABAC_RATE_OFF);
	}
}

static void vcodec_get_reg_freq_default(struct vpu_subdev_data *data,
					struct vpu_reg *reg)
{
	reg->freq = VPU_FREQ_300M;

	if (data->hw_id == HEVC_ID) {
		if (reg_probe_hevc_y_stride(reg) > 60000)
			reg->freq = VPU_FREQ_400M;
	}

	if (reg->type == VPU_PP)
		reg->freq = VPU_FREQ_400M;
}

static void vcodec_get_reg_freq_rk3288(struct vpu_subdev_data *data,
				       struct vpu_reg *reg)
{
	vcodec_get_reg_freq_default(data, reg);

	if (reg->type == VPU_DEC || reg->type == VPU_DEC_PP) {
		if (reg_check_fmt(reg) == VPU_DEC_FMT_H264) {
			if (reg_probe_width(reg) > 2560) {
				/*
				 * raise frequency for resolution larger
				 * than 1440p avc.
				 */
				reg->freq = VPU_FREQ_600M;
			}
		} else if (reg_check_interlace(reg)) {
			reg->freq = VPU_FREQ_400M;
		}
	}
}

static void vcodec_set_freq_default(struct vpu_service_info *pservice,
				    struct vpu_reg *reg)
{
	enum VPU_FREQ curr = atomic_read(&pservice->freq_status);

	if (curr == reg->freq)
		return;

	atomic_set(&pservice->freq_status, reg->freq);
	switch (reg->freq) {
	case VPU_FREQ_200M: {
		clk_set_rate(pservice->aclk_vcodec, 200 * MHZ);
	} break;
	case VPU_FREQ_266M: {
		clk_set_rate(pservice->aclk_vcodec, 266 * MHZ);
	} break;
	case VPU_FREQ_300M: {
		clk_set_rate(pservice->aclk_vcodec, 300 * MHZ);
	} break;
	case VPU_FREQ_400M: {
		clk_set_rate(pservice->aclk_vcodec, 400 * MHZ);
	} break;
	case VPU_FREQ_500M: {
		clk_set_rate(pservice->aclk_vcodec, 500 * MHZ);
	} break;
	case VPU_FREQ_600M: {
		clk_set_rate(pservice->aclk_vcodec, 600 * MHZ);
	} break;
	default: {
		clk_set_rate(pservice->aclk_vcodec,
			     pservice->aclk_vcodec_default_rate);
		clk_set_rate(pservice->clk_core,
			     pservice->clk_core_default_rate);
		clk_set_rate(pservice->clk_cabac,
			     pservice->clk_cabac_default_rate);
		break;
	}
	}
}

static void rkvdec_set_clk(struct vpu_service_info *pservice,
			   unsigned long vcodec_rate,
			   unsigned long core_rate,
			   unsigned long cabac_rate,
			   unsigned int event)
{
	static unsigned int div;

	mutex_lock(&pservice->set_clk_lock);

	switch (event) {
	case EVENT_POWER_ON:
		clk_set_rate(pservice->aclk_vcodec, pservice->vcodec_rate);
		clk_set_rate(pservice->clk_core, pservice->core_rate);
		clk_set_rate(pservice->clk_cabac, pservice->cabac_rate);
		div = 0;
		break;
	case EVENT_POWER_OFF:
		clk_set_rate(pservice->aclk_vcodec, vcodec_rate);
		clk_set_rate(pservice->clk_core, core_rate);
		clk_set_rate(pservice->clk_cabac, cabac_rate);
		div = 0;
		break;
	case EVENT_ADJUST:
		if (!div) {
			clk_set_rate(pservice->aclk_vcodec, vcodec_rate);
			clk_set_rate(pservice->clk_core, core_rate);
			clk_set_rate(pservice->clk_cabac, cabac_rate);
		} else {
			clk_set_rate(pservice->aclk_vcodec, vcodec_rate / div);
			clk_set_rate(pservice->clk_core, vcodec_rate / div);
			clk_set_rate(pservice->clk_cabac, vcodec_rate / div);
		}
		pservice->vcodec_rate = vcodec_rate;
		pservice->core_rate = core_rate;
		pservice->cabac_rate = cabac_rate;
		break;
	case EVENT_THERMAL:
		div = pservice->vcodec_rate / vcodec_rate;
		if (div > 4)
			div = 4;
		if (div) {
			clk_set_rate(pservice->aclk_vcodec,
				     pservice->vcodec_rate / div);
			clk_set_rate(pservice->clk_core,
				     pservice->core_rate / div);
			clk_set_rate(pservice->clk_cabac,
				     pservice->cabac_rate / div);
		}
		break;
	}

	mutex_unlock(&pservice->set_clk_lock);
}

static void vcodec_set_freq_rk3328(struct vpu_service_info *pservice,
				    struct vpu_reg *reg)
{
	enum VPU_FREQ curr = atomic_read(&pservice->freq_status);

	if (curr == reg->freq)
		return;

	if (pservice->dev_id == VCODEC_DEVICE_ID_RKVDEC) {
		if (reg->reg[1] & 0x00800000) {
			if (rkv_dec_get_fmt(reg->reg) == FMT_H264D)
				rkvdec_set_clk(pservice, 400 * MHZ, 250 * MHZ,
					       400 * MHZ, EVENT_ADJUST);
			else
				rkvdec_set_clk(pservice, 500 * MHZ, 250 * MHZ,
					       400 * MHZ, EVENT_ADJUST);
		} else {
			if (rkv_dec_get_fmt(reg->reg) == FMT_H264D)
				rkvdec_set_clk(pservice, 400 * MHZ, 300 * MHZ,
					       400 * MHZ, EVENT_ADJUST);
			else
				rkvdec_set_clk(pservice, 500 * MHZ, 300 * MHZ,
					       400 * MHZ, EVENT_ADJUST);
		}
	}
}

static void vcodec_set_freq_rk322x(struct vpu_service_info *pservice,
				   struct vpu_reg *reg)
{
	enum VPU_FREQ curr = atomic_read(&pservice->freq_status);

	if (curr == reg->freq)
		return;

	/*
	 * special process for rk322x
	 * rk322x rkvdec has more clocks to set
	 * vpu/vpu2 still only need to set aclk
	 */
	if (pservice->dev_id == VCODEC_DEVICE_ID_RKVDEC) {
		rkvdec_set_clk(pservice,
			       500 * MHZ,
			       300 * MHZ,
			       300 * MHZ,
			       EVENT_ADJUST);
	} else {
		rkvdec_set_clk(pservice,
			       300 * MHZ,
			       300 * MHZ,
			       300 * MHZ,
			       EVENT_ADJUST);

	}
}

#ifdef CONFIG_IOMMU_API
static inline void platform_set_sysmmu(struct device *iommu,
				       struct device *dev)
{
	dev->archdata.iommu = iommu;
}
#else
static inline void platform_set_sysmmu(struct device *iommu,
				       struct device *dev)
{
}
#endif

#define IOMMU_GET_BUS_ID(x)    (((x) >> 6) & 0x1f)

static int
_vcodec_sys_fault_hdl(struct device *dev, unsigned long iova, int status)
{
	struct platform_device *pdev;
	struct vpu_service_info *pservice;
	struct vpu_subdev_data *data;
	struct vpu_session *session = NULL;

	vpu_debug_enter();

	if (!dev) {
		pr_err("invalid NULL dev\n");
		return 0;
	}

	pdev = container_of(dev, struct platform_device, dev);
	if (!pdev) {
		pr_err("invalid NULL platform_device\n");
		return 0;
	}

	data = platform_get_drvdata(pdev);
	if (!data) {
		pr_err("invalid NULL vpu_subdev_data\n");
		return 0;
	}

	pservice = data->pservice;
	if (!pservice) {
		pr_err("invalid NULL vpu_service_info\n");
		return 0;
	}

	session = list_first_entry(&pservice->session,
				   struct vpu_session, list_session);

	if (pservice->reg_codec) {
		struct vpu_reg *reg = pservice->reg_codec;
		struct vcodec_mem_region *mem = NULL, *n = NULL;
		int i = 0;

		dev_err(dev, "vcodec, fault addr 0x%08lx status %x\n", iova,
			status);
		if (!list_empty(&reg->mem_region_list)) {
			list_for_each_entry_safe(mem, n, &reg->mem_region_list,
						 reg_lnk) {
				unsigned long tmp_iova = mem->iova;

				dev_err(dev, "vcodec, reg[%02u] mem region [%02d] 0x%lx %lx\n",
					mem->reg_idx, i, tmp_iova, mem->len);
				i++;
			}
		} else {
			dev_err(dev, "no memory region mapped\n");
		}

		if (reg->data) {
			struct vpu_subdev_data *data = reg->data;
			u32 *base = (u32 *)data->dec_dev.regs;
			u32 len = data->hw_info->dec_reg_num;

			dev_err(dev, "current errror register set:\n");

			for (i = 0; i < len; i++)
				dev_err(dev, "reg[%02d] %08x\n",
					i, readl_relaxed(base + i));
		}

		set_bit(MMU_PAGEFAULT, &data->state);

		/*
		 * defeat workaround, invalidate address generated when rk322x
		 * vdec pre-fetch colmv data.
		 */
		if (IOMMU_GET_BUS_ID(status) == 2 && data->pa_hdl >= 0) {
			/* avoid another page fault occur after page fault */
			if (data->pa_iova != -1)
				vcodec_iommu_unmap_iommu_with_iova(data->iommu_info,
								   session,
								   data->pa_hdl);

			/* get the page align iova */
			data->pa_iova = round_down(iova, IOMMU_PAGE_SIZE);

			vcodec_iommu_map_iommu_with_iova(data->iommu_info,
							 session,
							 data->pa_hdl,
							 data->pa_iova,
							 IOMMU_PAGE_SIZE);
		} else {
			clear_bit(MMU_PAGEFAULT, &data->state);
		}
	}

	return 0;
}

int vcodec_iommu_fault_hdl(struct iommu_domain *iommu,
			   struct device *iommu_dev,
			   unsigned long iova, int status, void *arg)
{
	struct device *dev = (struct device *)arg;

	return _vcodec_sys_fault_hdl(dev, iova, status);
}

int vcodec_iovmm_fault_hdl(struct device *dev,
			   enum rk_iommu_inttype itype,
			   unsigned long pgtable_base,
			   unsigned long fault_addr, unsigned int status)
{
	return _vcodec_sys_fault_hdl(dev, fault_addr, status);
}

static void vcodec_reduce_freq_rk322x(struct vpu_service_info *pservice)
{
	if (list_empty(&pservice->running)) {
		unsigned long rate = clk_get_rate(pservice->aclk_vcodec);

		rkvdec_set_clk(pservice,
			       get_div_rate(pservice->aclk_vcodec, 32),
			       get_div_rate(pservice->clk_core, 32),
			       get_div_rate(pservice->clk_cabac, 32),
			       EVENT_ADJUST);
		atomic_set(&pservice->freq_status, rate / 32);
	}
}

static void vcodec_reduce_freq_rk3328(struct vpu_service_info *pservice)
{
	if (list_empty(&pservice->running))
		rkvdec_set_clk(pservice, 100 * MHZ, 100 * MHZ, 100 * MHZ,
			       EVENT_ADJUST);
}

static int vcodec_spec_init_rk3328(struct vpu_service_info *pservice)
{
	/* todo */

	return 0;
}

static void vcodec_spec_config_rk3328(struct vpu_subdev_data *data)
{
	u32 *dst = data->dec_dev.regs;
	struct vpu_reg *reg = data->pservice->reg_codec;
	u32 cfg;

	/*
	 * HW defeat workaround: VP9 power save optimization cause decoding
	 * corruption, disable optimization here.
	 */
	if (rkv_dec_get_fmt(reg->reg) == FMT_VP9D) {
		cfg = readl(dst + 99);
		writel(cfg & (~(1 << 12)), dst + 99);
	}
}

static struct vcodec_hw_ops hw_ops_default = {
	.power_on = vcodec_power_on_default,
	.power_off = vcodec_power_off_default,
	.get_freq = vcodec_get_reg_freq_default,
	.set_freq = vcodec_set_freq_default,
	.reduce_freq = NULL,
};

static struct vcodec_hw_ops hw_ops_rk3328_rkvdec = {
	.power_on = vcodec_power_on_rk3328,
	.power_off = vcodec_power_off_rk3328,
	.get_freq = vcodec_get_reg_freq_default,
	.set_freq = vcodec_set_freq_rk3328,
	.reduce_freq = vcodec_reduce_freq_rk3328,
};

static struct vcodec_hw_var rk3328_rkvdec_var = {
	.device_type = VCODEC_DEVICE_TYPE_RKVD,
	.name = "rkvdec",
	.pmu_type = -1,
	.ops = &hw_ops_rk3328_rkvdec,
	.init = vcodec_spec_init_rk3328,
	.config = vcodec_spec_config_rk3328,
};

static struct vcodec_hw_var rk3328_vpucombo_var = {
	.device_type = VCODEC_DEVICE_TYPE_VPUC,
	.name = "vpu-combo",
	.pmu_type = -1,
	.ops = NULL,
	.init = NULL,
	.config = NULL,
};

/* Both VPU1 and VPU2 */
static struct vcodec_hw_var vpu_device_info = {
	.device_type = VCODEC_DEVICE_TYPE_VPUX,
	.name = "vpu-service",
	.pmu_type = -1,
	.ops = NULL,
	.init = NULL,
	.config = NULL,
};

static struct vcodec_hw_var vpu_combo_device_info = {
	.device_type = VCODEC_DEVICE_TYPE_VPUC,
	.name = "vpu-combo",
	.pmu_type = -1,
	.ops = NULL,
	.init = NULL,
	.config = NULL,
};

static struct vcodec_hw_var hevc_device_info = {
	.device_type = VCODEC_DEVICE_TYPE_HEVC,
	.name = "hevc-service",
	.pmu_type = -1,
	.ops = NULL,
	.init = NULL,
	.config = NULL,
};

static struct vcodec_hw_var rkvd_device_info = {
	.device_type = VCODEC_DEVICE_TYPE_RKVD,
	.name = "rkvdec",
	.pmu_type = -1,
	.ops = NULL,
	.init = NULL,
	.config = NULL,
};

static void vcodec_set_hw_ops(struct vpu_service_info *pservice)
{
	if (pservice->hw_var) {
		pservice->hw_ops = pservice->hw_var->ops;
	}

	if (!pservice->hw_ops) {
		pservice->hw_ops = &hw_ops_default;
		if (of_machine_is_compatible("rockchip,rk3328") ||
		    of_machine_is_compatible("rockchip,rk3228") ||
		    of_machine_is_compatible("rockchip,rk3229")) {
			pservice->hw_ops->power_on = vcodec_power_on_rk322x;
			pservice->hw_ops->power_off = vcodec_power_off_rk322x;
			pservice->hw_ops->get_freq = NULL;
			pservice->hw_ops->set_freq = vcodec_set_freq_rk322x;
			pservice->hw_ops->reduce_freq =
						vcodec_reduce_freq_rk322x;
		} else if (of_machine_is_compatible("rockchip,rk3126") ||
				of_machine_is_compatible("rockchip,rk3128")) {
			pservice->hw_ops->power_on = vcodec_power_on_rk312x;
		} else if (of_machine_is_compatible("rockchip,rk3288")) {
			pservice->hw_ops->get_freq = vcodec_get_reg_freq_rk3288;
		}
	}
}

static int vcodec_subdev_probe(struct platform_device *pdev,
			       struct vpu_service_info *pservice)
{
	u8 *regs = NULL;
	s32 ret = 0;
	struct resource *res = NULL;
	struct vpu_hw_info *hw_info = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct vpu_subdev_data *data = NULL;
	struct platform_device *sub_dev = NULL;
	struct device_node *sub_np = NULL;
	struct vpu_session *session = list_first_entry(&pservice->session,
						       struct vpu_session,
						       list_session);
	const char *name  = np->name;
	char mmu_dev_dts_name[40];

	dev_info(dev, "probe device");

	data = devm_kzalloc(dev, sizeof(struct vpu_subdev_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pservice = pservice;
	data->dev = dev;
	platform_set_drvdata(pdev, data);

	INIT_WORK(&data->set_work, vpu_set_register_work);

	switch (pservice->dev_id) {
	case VCODEC_DEVICE_ID_VPU:
		data->mode = VCODEC_RUNNING_MODE_VPU;
		break;
	case VCODEC_DEVICE_ID_HEVC:
		data->mode = VCODEC_RUNNING_MODE_HEVC;
		break;
	case VCODEC_DEVICE_ID_RKVDEC:
		data->mode = VCODEC_RUNNING_MODE_RKVDEC;
		break;
	case VCODEC_DEVICE_ID_COMBO:
	default:
		of_property_read_u32(np, "dev_mode", (u32 *)&data->mode);
		break;
	}

	if (!pservice->reg_base) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		data->regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(data->regs)) {
			ret = PTR_ERR(data->regs);
			goto err;
		}
	} else {
		data->regs = pservice->reg_base;
	}

	sub_np = of_parse_phandle(np, "iommus", 0);
	if (sub_np) {
		sub_dev = of_find_device_by_node(sub_np);
		data->mmu_dev = &sub_dev->dev;
	}

	/* Back to legacy iommu probe */
	if (!data->mmu_dev) {
		switch (data->mode) {
		case VCODEC_RUNNING_MODE_VPU:
			sprintf(mmu_dev_dts_name,
				VPU_IOMMU_COMPATIBLE_NAME);
			break;
		case VCODEC_RUNNING_MODE_RKVDEC:
			sprintf(mmu_dev_dts_name,
				VDEC_IOMMU_COMPATIBLE_NAME);
			break;
		case VCODEC_RUNNING_MODE_HEVC:
		default:
			sprintf(mmu_dev_dts_name,
				HEVC_IOMMU_COMPATIBLE_NAME);
			break;
		}

		data->mmu_dev =
			rockchip_get_sysmmu_dev(mmu_dev_dts_name);
		if (data->mmu_dev)
			platform_set_sysmmu(data->mmu_dev, dev);

		rockchip_iovmm_set_fault_handler
			(dev, vcodec_iovmm_fault_hdl);
	}

	dev_info(dev, "vpu mmu dec %p\n", data->mmu_dev);

#define BIT_VCODEC_CLK_SEL	BIT(10)
	if (of_machine_is_compatible("rockchip,rk3126") ||
	    of_machine_is_compatible("rockchip,rk3128"))
		regmap_write(pservice->grf, RK312X_GRF_SOC_CON1,
			     BIT_VCODEC_CLK_SEL |
			     (BIT_VCODEC_CLK_SEL << 16));

	clear_bit(MMU_ACTIVATED, &data->state);
	vpu_service_power_on(data, pservice);

	of_property_read_u32(np, "allocator", (u32 *)&pservice->alloc_type);
	data->iommu_info = vcodec_iommu_info_create(dev, data->mmu_dev,
						    pservice->alloc_type);
	dev_info(dev, "allocator is %s\n", pservice->alloc_type == 1 ? "drm" :
		(pservice->alloc_type == 2 ? "ion" : "null"));
	if (pservice->alloc_type == 1) {
		struct vcodec_iommu_drm_info *drm_info =
			data->iommu_info->private;
		iommu_set_fault_handler(drm_info->domain,
					vcodec_iommu_fault_hdl, dev);
	}
	vcodec_enter_mode(data);
	ret = vpu_service_check_hw(data);
	if (ret < 0) {
		dev_err(dev, "error: hw info check failed\n");
		goto err;
	}
	data->pa_hdl = vcodec_iommu_alloc(data->iommu_info,
					  session, 4096, 4096);

	if (data->pa_hdl < 0) {
		dev_err(dev, "allocate page fault hdl failed\n");
		return -1;
	}

	vcodec_iommu_map_iommu(data->iommu_info, session,
			       data->pa_hdl, NULL, NULL);
	data->pa_iova = -1;
	vcodec_exit_mode(data);

	hw_info = data->hw_info;
	regs = (u8 *)data->regs;

	if (hw_info->dec_reg_num) {
		data->dec_dev.iosize = hw_info->dec_io_size;
		data->dec_dev.regs = (u32 *)(regs + hw_info->dec_offset);
	}

	if (hw_info->enc_reg_num) {
		data->enc_dev.iosize = hw_info->enc_io_size;
		data->enc_dev.regs = (u32 *)(regs + hw_info->enc_offset);
	}

	data->reg_size = max(hw_info->dec_io_size, hw_info->enc_io_size);

	data->irq_enc = platform_get_irq_byname(pdev, "irq_enc");
	if (data->irq_enc > 0) {
		ret = devm_request_threaded_irq(dev, data->irq_enc,
						vepu_irq, vepu_isr,
						IRQF_SHARED, dev_name(dev),
						(void *)data);
		if (ret) {
			dev_err(dev, "error: can't request vepu irq %d\n",
				data->irq_enc);
			goto err;
		}
	}

	data->irq_dec = platform_get_irq_byname(pdev, "irq_dec");
	if (data->irq_dec > 0) {
		ret = devm_request_threaded_irq(dev, data->irq_dec,
						vdpu_irq, vdpu_isr,
						IRQF_SHARED, dev_name(dev),
						(void *)data);
		if (ret) {
			dev_err(dev, "error: can't request vdpu irq %d\n",
				data->irq_dec);
			goto err;
		}
	}
	atomic_set(&data->dec_dev.irq_count_codec, 0);
	atomic_set(&data->dec_dev.irq_count_pp, 0);
	atomic_set(&data->enc_dev.irq_count_codec, 0);
	atomic_set(&data->enc_dev.irq_count_pp, 0);

	get_hw_info(data);

	/* create device node */
	ret = alloc_chrdev_region(&data->dev_t, 0, 1, name);
	if (ret) {
		dev_err(dev, "alloc dev_t failed\n");
		goto err;
	}

	cdev_init(&data->cdev, &vpu_service_fops);

	data->cdev.owner = THIS_MODULE;
	data->cdev.ops = &vpu_service_fops;

	ret = cdev_add(&data->cdev, data->dev_t, 1);

	if (ret) {
		dev_err(dev, "add dev_t failed\n");
		goto err;
	}

	data->cls = class_create(THIS_MODULE, name);

	if (IS_ERR(data->cls)) {
		ret = PTR_ERR(data->cls);
		dev_err(dev, "class_create err:%d\n", ret);
		goto err;
	}

	data->child_dev = device_create(data->cls, dev,
		data->dev_t, NULL, "%s", name);

	INIT_LIST_HEAD(&data->lnk_service);
	list_add_tail(&data->lnk_service, &pservice->subdev_list);

	/* After the subdev was appened to the list of pservice */
	vpu_service_power_off(pservice);

	return 0;
err:
	dev_err(dev, "probe err:%d\n", ret);
	if (data->child_dev) {
		device_destroy(data->cls, data->dev_t);
		cdev_del(&data->cdev);
		unregister_chrdev_region(data->dev_t, 1);
	}

	if (data->cls)
		class_destroy(data->cls);
	vpu_service_power_off(pservice);
	return -1;
}

static void vcodec_subdev_remove(struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_session *session = list_first_entry(&pservice->session,
						       struct vpu_session,
						       list_session);

	vcodec_iommu_clear(data->iommu_info, session);
	vcodec_iommu_info_destroy(data->iommu_info);
	data->iommu_info = NULL;
	kfree(session);

	mutex_lock(&pservice->lock);
	cancel_delayed_work_sync(&pservice->power_off_work);
	vpu_service_power_off(pservice);
	mutex_unlock(&pservice->lock);

	device_destroy(data->cls, data->dev_t);
	class_destroy(data->cls);
	cdev_del(&data->cdev);
	unregister_chrdev_region(data->dev_t, 1);
}

static void vcodec_read_property(struct device_node *np,
				 struct vpu_service_info *pservice)
{
	pservice->mode_bit = 0;
	pservice->mode_ctrl = 0;
	pservice->subcnt = 0;

	of_property_read_u32(np, "subcnt", &pservice->subcnt);

	if (pservice->subcnt > 1) {
		of_property_read_u32(np, "mode_bit", &pservice->mode_bit);
		of_property_read_u32(np, "mode_ctrl", &pservice->mode_ctrl);
	}

	pservice->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR_OR_NULL(pservice->grf)) {
		pservice->grf = NULL;
		vpu_err("can't find vpu grf property\n");
		return;
	}

#ifdef CONFIG_RESET_CONTROLLER
	pservice->rst_a = devm_reset_control_get(pservice->dev, "video_a");
	pservice->rst_h = devm_reset_control_get(pservice->dev, "video_h");
	pservice->rst_v = devm_reset_control_get(pservice->dev, "video");
	pservice->rst_niu_a = devm_reset_control_get(pservice->dev, "niu_a");
	pservice->rst_niu_h = devm_reset_control_get(pservice->dev, "niu_h");
	pservice->rst_core = devm_reset_control_get(pservice->dev,
						    "video_core");
	pservice->rst_cabac = devm_reset_control_get(pservice->dev,
						     "video_cabac");

	if (IS_ERR_OR_NULL(pservice->rst_a)) {
		dev_dbg(pservice->dev, "No aclk reset resource define\n");
		pservice->rst_a = NULL;
	}

	if (IS_ERR_OR_NULL(pservice->rst_h)) {
		dev_dbg(pservice->dev, "No hclk reset resource define\n");
		pservice->rst_h = NULL;
	}

	if (IS_ERR_OR_NULL(pservice->rst_v)) {
		dev_dbg(pservice->dev, "No core rst_v reset resource define\n");
		pservice->rst_v = NULL;
	}

	if (IS_ERR_OR_NULL(pservice->rst_niu_a)) {
		dev_dbg(pservice->dev, "No NIU aclk reset resource define\n");
		pservice->rst_niu_a = NULL;
	}

	if (IS_ERR_OR_NULL(pservice->rst_niu_h)) {
		dev_dbg(pservice->dev, "No NIU hclk reset resource define\n");
		pservice->rst_niu_h = NULL;
	}

	if (IS_ERR_OR_NULL(pservice->rst_core)) {
		dev_dbg(pservice->dev, "No core reset resource define\n");
		pservice->rst_core = NULL;
	}

	if (IS_ERR_OR_NULL(pservice->rst_cabac)) {
		dev_dbg(pservice->dev, "No cabac reset resource define\n");
		pservice->rst_cabac = NULL;
	}
#endif

	of_property_read_string(np, "name", (const char **)&pservice->name);
}

static const struct of_device_id vcodec_service_dt_ids[] = {
	{
		.compatible = "rockchip,rk3328-rkvdec",
		.data = &rk3328_rkvdec_var,
	},
	{
		.compatible = "rockchip,rk3328-vpu-combo",
		.data = &rk3328_vpucombo_var,
	},
	{
		.compatible = "rockchip,vpu_service",
		.data = &vpu_device_info,
	},
	{
		.compatible = "rockchip,hevc_service",
		.data = &hevc_device_info,
	},
	{
		.compatible = "rockchip,vpu_combo",
		.data = &vpu_combo_device_info,
	},
	{
		.compatible = "rockchip,rkvdec",
		.data = &rkvd_device_info,
	},
	{},
};

static void vcodec_init_drvdata(struct vpu_service_info *pservice)
{
	pservice->dev_id = VCODEC_DEVICE_ID_VPU;
	pservice->curr_mode = -1;

	wake_lock_init(&pservice->wake_lock, WAKE_LOCK_SUSPEND, "vpu");
	INIT_LIST_HEAD(&pservice->waiting);
	INIT_LIST_HEAD(&pservice->running);
	mutex_init(&pservice->lock);
	mutex_init(&pservice->shutdown_lock);
	mutex_init(&pservice->reset_lock);
	mutex_init(&pservice->sip_reset_lock);
	mutex_init(&pservice->set_clk_lock);
	atomic_set(&pservice->service_on, 1);

	INIT_LIST_HEAD(&pservice->done);
	INIT_LIST_HEAD(&pservice->session);
	INIT_LIST_HEAD(&pservice->subdev_list);

	pservice->reg_pproc	= NULL;
	atomic_set(&pservice->total_running, 0);
	atomic_set(&pservice->enabled,       0);
	atomic_set(&pservice->power_on_cnt,  0);
	atomic_set(&pservice->power_off_cnt, 0);
	atomic_set(&pservice->reset_request, 0);

	INIT_DELAYED_WORK(&pservice->power_off_work, vpu_power_off_work);
	pservice->last.tv64 = 0;

	pservice->alloc_type = 0;

	vcodec_set_hw_ops(pservice);
	if (pservice->hw_var && pservice->hw_var->init)
		pservice->hw_var->init(pservice);
}

static int devfreq_vcodec_target(struct device *dev, unsigned long *freq,
				 u32 flags)
{
	struct vpu_subdev_data *data = dev_get_drvdata(dev);
	struct vpu_service_info *pservice = data->pservice;
	struct devfreq *devfreq = pservice->devfreq;
	struct devfreq_dev_status *stat = &devfreq->last_status;
	struct dev_pm_opp *opp;
	unsigned long old_clk_rate = stat->current_frequency;
	unsigned long target_volt, target_rate;
	unsigned long vcodec_rate, core_rate, cabac_rate;
	unsigned long req_rate = *freq;
	unsigned int clk_event;
	int ret = 0;

	rcu_read_lock();

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		return PTR_ERR(opp);
	}
	target_rate = dev_pm_opp_get_freq(opp);
	target_volt = dev_pm_opp_get_voltage(opp);

	rcu_read_unlock();

	if (target_rate < req_rate) {
		clk_event = EVENT_THERMAL;
		vcodec_rate = target_rate;
		core_rate = target_rate;
		cabac_rate = target_rate;
	} else {
		clk_event = stat->busy_time ? EVENT_POWER_ON : EVENT_POWER_OFF;
		vcodec_rate = pservice->vcodec_rate;
		core_rate = pservice->core_rate;
		cabac_rate = pservice->cabac_rate;
	}

	if (old_clk_rate == target_rate) {
		if (pservice->volt == target_volt)
			return ret;
		ret = regulator_set_voltage(pservice->vdd_vcodec, target_volt,
					    INT_MAX);
		if (ret)
			dev_err(dev, "Cannot set voltage %lu uV\n",
				target_volt);
		return ret;
	}

	if (old_clk_rate < target_rate) {
		ret = regulator_set_voltage(pservice->vdd_vcodec, target_volt,
					    INT_MAX);
		if (ret) {
			dev_err(dev, "Cannot set voltage %lu uV\n",
				target_volt);
			return ret;
		}
	}

	dev_dbg(dev, "%lu-->%lu\n", old_clk_rate, target_rate);
	rkvdec_set_clk(pservice, vcodec_rate, core_rate, cabac_rate, clk_event);

	stat->current_frequency = target_rate;

	if (old_clk_rate > target_rate) {
		ret = regulator_set_voltage(pservice->vdd_vcodec, target_volt,
					    INT_MAX);
		if (ret) {
			dev_err(dev, "Cannot set vol %lu uV\n", target_volt);
			return ret;
		}
	}

	pservice->volt = target_volt;

	return ret;
}

static int devfreq_vcodec_get_cur_freq(struct device *dev,
				       unsigned long *freq)
{
	struct vpu_subdev_data *data = dev_get_drvdata(dev);
	struct vpu_service_info *pservice = data->pservice;

	*freq = clk_get_rate(pservice->aclk_vcodec);

	return 0;
}

static struct devfreq_dev_profile devfreq_vcodec_profile = {
	.target		= devfreq_vcodec_target,
	.get_cur_freq	= devfreq_vcodec_get_cur_freq,
};

static unsigned long model_static_power(struct devfreq *devfreq,
					unsigned long voltage)
{
	struct device *dev = devfreq->dev.parent;
	struct vpu_subdev_data *data = dev_get_drvdata(dev);
	struct vpu_service_info *pservice = data->pservice;

	int temperature;
	unsigned long temp;
	unsigned long temp_squared, temp_cubed, temp_scaling_factor;
	const unsigned long voltage_cubed = (voltage * voltage * voltage) >> 10;

	if (!IS_ERR_OR_NULL(pservice->vcodec_tz) &&
	    pservice->vcodec_tz->ops->get_temp) {
		int ret;

		ret = pservice->vcodec_tz->ops->get_temp(pservice->vcodec_tz,
							 &temperature);
		if (ret) {
			dev_warn_ratelimited(dev,
					     "failed to read temp for ddr thermal zone: %d\n",
					     ret);
			temperature = FALLBACK_STATIC_TEMPERATURE;
		}
	} else {
		temperature = FALLBACK_STATIC_TEMPERATURE;
	}

	/*
	 * Calculate the temperature scaling factor. To be applied to the
	 * voltage scaled power.
	 */
	temp = temperature / 1000;
	temp_squared = temp * temp;
	temp_cubed = temp_squared * temp;
	temp_scaling_factor = (pservice->ts[3] * temp_cubed)
	    + (pservice->ts[2] * temp_squared)
	    + (pservice->ts[1] * temp)
	    + pservice->ts[0];

	return (((pservice->static_coefficient * voltage_cubed) >> 20)
		* temp_scaling_factor) / 1000000;
}

static struct devfreq_cooling_power vcodec_cooling_power_data = {
	.get_static_power = model_static_power,
	.dyn_power_coeff = 120,
};

static int vcodec_power_model_simple_init(struct vpu_service_info *pservice)
{
	struct device_node *power_model_node;
	const char *tz_name;
	u32 temp;

	power_model_node = of_get_child_by_name(pservice->dev->of_node,
						"vcodec_power_model");
	if (!power_model_node) {
		dev_err(pservice->dev, "could not find power_model node\n");
		return -ENODEV;
	}

	if (of_property_read_string(power_model_node, "thermal-zone", &tz_name)) {
		dev_err(pservice->dev, "ts in power_model not available\n");
		return -EINVAL;
	}

	pservice->vcodec_tz = thermal_zone_get_zone_by_name(tz_name);
	if (IS_ERR(pservice->vcodec_tz)) {
		pr_warn_ratelimited
		    ("Error getting ddr thermal zone (%ld), not yet ready?\n",
		     PTR_ERR(pservice->vcodec_tz));
		pservice->vcodec_tz = NULL;

		return -EPROBE_DEFER;
	}

	if (of_property_read_u32(power_model_node, "static-power-coefficient",
				 &pservice->static_coefficient)) {
		dev_err(pservice->dev,
			"static-power-coefficient not available\n");
		return -EINVAL;
	}
	if (of_property_read_u32(power_model_node, "dynamic-power-coefficient",
				 &temp)) {
		dev_err(pservice->dev,
			"dynamic-power-coefficient not available\n");
		return -EINVAL;
	}
	vcodec_cooling_power_data.dyn_power_coeff = (unsigned long)temp;

	if (of_property_read_u32_array
	    (power_model_node, "ts", (u32 *)pservice->ts, 4)) {
		dev_err(pservice->dev, "ts in power_model not available\n");
		return -EINVAL;
	}

	return 0;
}

static int vcodec_devfreq_notifier_call(struct notifier_block *nb,
					unsigned long event,
					void *data)
{
	struct vpu_service_info *ps = container_of(nb, struct vpu_service_info,
						   devfreq_nb);

	if (!ps)
		return NOTIFY_OK;

	if (event == DEVFREQ_PRECHANGE)
		mutex_lock(&ps->sip_reset_lock);
	else if (event == DEVFREQ_POSTCHANGE)
		mutex_unlock(&ps->sip_reset_lock);

	return NOTIFY_OK;
}

static int vcodec_probe(struct platform_device *pdev)
{
	int i;
	int ret = 0;
	const struct of_device_id *match = NULL;
	struct resource *res = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *parent_np;
	struct devfreq_dev_profile *devp = &devfreq_vcodec_profile;
	struct devfreq_dev_status *stat;
	struct vpu_service_info *pservice = NULL;
	struct vpu_session *session = NULL;
#define MAX_PROP_NAME_LEN	3
	char name[MAX_PROP_NAME_LEN];
	int lkg_volt_sel;

	pservice = devm_kzalloc(dev, sizeof(struct vpu_service_info),
				GFP_KERNEL);
	if (!pservice)
		return -ENOMEM;
	pservice->dev = dev;

	pservice->vdd_vcodec = devm_regulator_get_optional(dev, "vcodec");
	if (IS_ERR(pservice->vdd_vcodec)) {
		ret = PTR_ERR(pservice->vdd_vcodec);
		if (ret == -EPROBE_DEFER) {
			dev_warn(dev, "vcodec regulator not ready, retry\n");
			return ret;
		}
		dev_warn(dev, "no regulator for vcodec\n");
	}

	match = of_match_node(vcodec_service_dt_ids, pservice->dev->of_node);
	if (match)
		pservice->hw_var = match->data;
	else
		return -EINVAL;

	if (pservice->hw_var->ops &&
	    pservice->hw_var->ops->reduce_freq == vcodec_reduce_freq_rk3328) {
		pservice->parent_devfreq = devfreq_get_devfreq_by_phandle(dev,
									  0);
		if (IS_ERR(pservice->parent_devfreq)) {
			ret = PTR_ERR(pservice->parent_devfreq);
			if (ret == -EPROBE_DEFER) {
				parent_np = of_parse_phandle(np, "devfreq", 0);
				if (parent_np &&
				    of_device_is_available(parent_np)) {
					dev_warn(dev,
						 "parent devfreq retry\n");
					return ret;
				}
				dev_warn(dev, "parent devfreq is disabled\n");
				pservice->parent_devfreq = NULL;
			} else {
				dev_err(dev, "parent devfreq is error\n");
				return -EINVAL;
			}
		} else {
			dev_info(dev, "parent devfreq is ok\n");
		}
	}

	pservice->set_workq = create_singlethread_workqueue("vcodec");
	if (!pservice->set_workq) {
		dev_err(dev, "failed to create workqueue\n");
		return -ENOMEM;
	}

	vcodec_read_property(np, pservice);
	vcodec_init_drvdata(pservice);

	if (pservice->parent_devfreq) {
		pservice->devfreq_nb.notifier_call =
			vcodec_devfreq_notifier_call;
		devm_devfreq_register_notifier(dev,
					       pservice->parent_devfreq,
					       &pservice->devfreq_nb,
					       DEVFREQ_TRANSITION_NOTIFIER);
	}

	/* Underscore for label, hyphens for name */
	switch (pservice->hw_var->device_type) {
	case VCODEC_DEVICE_TYPE_VPUX:
		pservice->dev_id = VCODEC_DEVICE_ID_VPU;
		break;
	case VCODEC_DEVICE_TYPE_VPUC:
		pservice->dev_id = VCODEC_DEVICE_ID_COMBO;
		break;
	case VCODEC_DEVICE_TYPE_HEVC:
		pservice->dev_id = VCODEC_DEVICE_ID_HEVC;
		break;
	case VCODEC_DEVICE_TYPE_RKVD:
		pservice->dev_id = VCODEC_DEVICE_ID_RKVDEC;
		break;
	default:
		dev_err(dev, "unsupported device type\n");
		ret = -ENODEV;
		goto err;
	}

	pservice->secure_isr = false;
	pservice->secure_irq_status = true;

	if (0 > vpu_get_clk(pservice))
		goto err;

	if (!IS_ERR(pservice->vdd_vcodec)) {
		lkg_volt_sel = rockchip_of_get_lkg_volt_sel(dev,
							    "rkvdec_leakage");
		if (lkg_volt_sel >= 0) {
			snprintf(name, MAX_PROP_NAME_LEN, "L%d", lkg_volt_sel);
			ret = dev_pm_opp_set_prop_name(dev, name);
			if (ret)
				dev_err(dev, "Failed to set prop name\n");
		}

		if (dev_pm_opp_of_add_table(dev)) {
			dev_err(dev, "Invalid operating-points\n");
			ret = -EINVAL;
			goto err;
		}

		pservice->devfreq = devm_devfreq_add_device(dev, devp,
							    "userspace", NULL);
		if (IS_ERR(pservice->devfreq)) {
			ret = PTR_ERR(pservice->devfreq);
			goto err;
		}

		stat = &pservice->devfreq->last_status;
		stat->current_frequency = clk_get_rate(pservice->aclk_vcodec);
		pservice->volt = regulator_get_voltage(pservice->vdd_vcodec);

		ret = devfreq_register_opp_notifier(dev, pservice->devfreq);
		if (ret)
			goto err;
	}

	if (of_property_read_bool(np, "reg")) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

		pservice->reg_base = devm_ioremap_resource(pservice->dev, res);
		if (IS_ERR(pservice->reg_base)) {
			vpu_err("ioremap registers base failed\n");
			ret = PTR_ERR(pservice->reg_base);
			goto err;
		}
		pservice->ioaddr = res->start;
	} else {
		pservice->reg_base = 0;
	}

	pm_runtime_enable(dev);

	/*
	 * this session is global session, each dev
	 * only has one global session, and will be
	 * release when dev remove
	 */
	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session) {
		vpu_err("error: unable to allocate memory for vpu_session.");
		ret = -ENOMEM;
		goto err;
	}

	session->type	= VPU_TYPE_BUTT;
	session->pid	= current->pid;
	INIT_LIST_HEAD(&session->waiting);
	INIT_LIST_HEAD(&session->running);
	INIT_LIST_HEAD(&session->done);
	INIT_LIST_HEAD(&session->list_session);
	init_waitqueue_head(&session->wait);
	atomic_set(&session->task_running, 0);
	mutex_lock(&pservice->lock);
	list_add_tail(&session->list_session, &pservice->session);
	mutex_unlock(&pservice->lock);

	if (of_property_read_bool(np, "subcnt")) {
		struct vpu_subdev_data *data = NULL;

		data = devm_kzalloc(dev, sizeof(struct vpu_subdev_data),
				    GFP_KERNEL);
		if (!data) {
			vpu_err("error: unable to allocate memory for vpu_subdev_data");
			ret = -ENOMEM;
			goto err;
		}

		data->pservice = pservice;
		platform_set_drvdata(pdev, data);

		for (i = 0; i < pservice->subcnt; i++) {
			struct device_node *sub_np;
			struct platform_device *sub_pdev;

			sub_np = of_parse_phandle(np, "rockchip,sub", i);
			sub_pdev = of_find_device_by_node(sub_np);

			vcodec_subdev_probe(sub_pdev, pservice);
		}
	} else {
		vcodec_subdev_probe(pdev, pservice);
	}

	ret = vcodec_power_model_simple_init(pservice);

	if (!ret && pservice->devfreq) {
		pservice->devfreq_cooling =
		    of_devfreq_cooling_register_power(pservice->dev->of_node,
						      pservice->devfreq,
						      &vcodec_cooling_power_data);
		if (IS_ERR_OR_NULL(pservice->devfreq_cooling)) {
			ret = PTR_ERR(pservice->devfreq_cooling);
			dev_err(pservice->dev,
				"Failed to register cooling device (%d)\n",
				ret);
		}
	}

	dev_info(dev, "init success\n");

	return 0;

err:
	dev_info(dev, "init failed\n");
	kfree(session);
	destroy_workqueue(pservice->set_workq);
	wake_lock_destroy(&pservice->wake_lock);

	return ret;
}

static int vcodec_remove(struct platform_device *pdev)
{
	struct vpu_subdev_data *data = platform_get_drvdata(pdev);

	vcodec_subdev_remove(data);

	pm_runtime_disable(data->pservice->dev);

	return 0;
}

static void vcodec_shutdown(struct platform_device *pdev)
{
	struct vpu_subdev_data *data = platform_get_drvdata(pdev);
	struct vpu_service_info *pservice = data->pservice;
	int val;
	int ret;

	dev_info(&pdev->dev, "vcodec shutdown");

	/*
	 * just wait for hardware finishing his work
	 * and do nothing else.
	 */
	mutex_lock(&pservice->shutdown_lock);
	atomic_set(&pservice->service_on, 0);
	mutex_unlock(&pservice->shutdown_lock);

	ret = readx_poll_timeout(atomic_read,
				 &pservice->total_running,
				 val, val == 0, 20000, 200000);
	if (ret == -ETIMEDOUT)
		dev_err(&pdev->dev, "wait total running time out\n");

	data->pservice->curr_mode = VCODEC_RUNNING_MODE_NONE;
}

MODULE_DEVICE_TABLE(of, vcodec_service_dt_ids);

static struct platform_driver vcodec_driver = {
	.probe = vcodec_probe,
	.remove = vcodec_remove,
	.shutdown = vcodec_shutdown,
	.driver = {
		.name = "rk-vcodec",
		.of_match_table = of_match_ptr(vcodec_service_dt_ids),
	},
};

static void get_hw_info(struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_dec_config *dec = &pservice->dec_config;
	struct vpu_enc_config *enc = &pservice->enc_config;

	switch (data->mode) {
	case VCODEC_RUNNING_MODE_VPU:
		dec->h264_support = 3;
		dec->jpeg_support = 1;
		dec->mpeg4_support = 2;
		dec->vc1_support = 3;
		dec->mpeg2_support = 1;
		dec->pp_support = 1;
		dec->sorenson_support = 1;
		dec->ref_buf_support = 3;
		dec->vp6_support = 1;
		dec->vp7_support = 1;
		dec->vp8_support = 1;
		dec->avs_support = 1;
		dec->jpeg_ext_support = 0;
		dec->custom_mpeg4_support = 1;
		dec->reserve = 0;
		dec->mvc_support = 1;

		if (data->enc_dev.regs) {
			u32 config_reg = readl_relaxed(data->enc_dev.regs + 63);

			enc->max_encoded_width = config_reg & ((1 << 11) - 1);
			enc->h264_enabled = 1;
			enc->mpeg4_enabled = (config_reg >> 26) & 1;
			enc->jpeg_enabled = 1;
			enc->vs_enabled = (config_reg >> 24) & 1;
			enc->rgb_enabled = (config_reg >> 28) & 1;
			enc->reg_size = data->reg_size;
			enc->reserv[0] = 0;
			enc->reserv[1] = 0;
		}

		pservice->auto_freq = true;
		vpu_debug(DEBUG_EXTRA_INFO,
			  "vpu_service set to auto frequency mode\n");
		atomic_set(&pservice->freq_status, VPU_FREQ_BUT);

		pservice->bug_dec_addr = of_machine_is_compatible
			("rockchip,rk30xx");
		break;
	case VCODEC_RUNNING_MODE_HEVC:
	case VCODEC_RUNNING_MODE_RKVDEC:
		pservice->auto_freq = true;
		atomic_set(&pservice->freq_status, VPU_FREQ_BUT);
		break;
	default:
		/* disable frequency switch in hevc.*/
		pservice->auto_freq = false;
	}

	if (of_machine_is_compatible("rockchip,rk2928") ||
			of_machine_is_compatible("rockchip,rk3036") ||
			of_machine_is_compatible("rockchip,rk3066") ||
			of_machine_is_compatible("rockchip,rk3126") ||
			of_machine_is_compatible("rockchip,rk3128") ||
			of_machine_is_compatible("rockchip,rk3188")) {
		dec->max_dec_pic_width = 1920;
		pservice->auto_freq = false;
	} else {
		dec->max_dec_pic_width = 4096;
	}
}

static bool check_irq_err(struct vpu_task_info *task, u32 irq_status)
{
	vpu_debug(DEBUG_IRQ_CHECK, "task %s status %08x mask %08x\n",
		  task->name, irq_status, task->error_mask);

	return (task->error_mask & irq_status) ? true : false;
}

static irqreturn_t vdpu_irq(int irq, void *dev_id)
{
	struct vpu_subdev_data *data = (struct vpu_subdev_data *)dev_id;
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_task_info *task = NULL;
	struct vpu_device *dev = &data->dec_dev;
	u32 hw_id = data->hw_info->hw_id;
	u32 raw_status;
	u32 dec_status;

	/* this interrupt can be cleared here, no need in security zone */
	if (atomic_read(&pservice->secure_mode)) {
		disable_irq_nosync(data->irq_dec);
		pservice->secure_isr = true;
		pservice->secure_irq_status = false;
		wake_up(pservice->wait_secure_isr);
		return IRQ_WAKE_THREAD;
	}

	task = &data->task_info[TASK_DEC];

	raw_status = readl_relaxed(dev->regs + task->reg_irq);
	dec_status = raw_status;

	vpu_debug(DEBUG_TASK_INFO,
		  "vdpu_irq reg %d status %x mask: irq %x ready %x error %0x\n",
		  task->reg_irq, dec_status,
		  task->irq_mask, task->ready_mask, task->error_mask);

	if (dec_status & task->irq_mask) {
		time_record(task, 1);
		vpu_debug(DEBUG_IRQ_STATUS, "vdpu_irq dec status %08x\n",
			  dec_status);
		if ((dec_status & 0x40001) == 0x40001) {
			do {
				dec_status = readl_relaxed(dev->regs +
							   task->reg_irq);
			} while ((dec_status & 0x40001) == 0x40001);
		}

		if (check_irq_err(task, dec_status))
			atomic_add(1, &pservice->reset_request);

		writel_relaxed(0, dev->regs + task->reg_irq);

		/* set clock gating to save power */
		writel(task->gating_mask, dev->regs + task->reg_en);

		atomic_add(1, &dev->irq_count_codec);
		time_diff(task);
		pservice->irq_status = raw_status;
	}

	task = &data->task_info[TASK_PP];
	if (hw_id != HEVC_ID && hw_id != RKV_DEC_ID) {
		u32 pp_status = readl_relaxed(dev->regs + task->irq_mask);

		if (pp_status & task->irq_mask) {
			time_record(task, 1);
			vpu_debug(DEBUG_IRQ_STATUS, "vdpu_irq pp status %08x\n",
				  pp_status);

			if (check_irq_err(task, dec_status))
				atomic_add(1, &pservice->reset_request);

			/* clear pp IRQ */
			writel_relaxed(pp_status & (~task->reg_irq),
				       dev->regs + task->irq_mask);
			atomic_add(1, &dev->irq_count_pp);
			time_diff(task);
		}
	}

	if (atomic_read(&dev->irq_count_pp) ||
	    atomic_read(&dev->irq_count_codec))
		return IRQ_WAKE_THREAD;
	else
		return IRQ_NONE;
}

static irqreturn_t vdpu_isr(int irq, void *dev_id)
{
	struct vpu_subdev_data *data = (struct vpu_subdev_data *)dev_id;
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_device *dev = &data->dec_dev;
	struct vpu_session *session = list_first_entry(&pservice->session,
						       struct vpu_session,
						       list_session);


	if (atomic_read(&pservice->secure_mode))
		return IRQ_HANDLED;

	mutex_lock(&pservice->lock);
	if (atomic_read(&dev->irq_count_codec)) {
		atomic_sub(1, &dev->irq_count_codec);
		if (pservice->reg_codec == NULL) {
			vpu_err("error: dec isr with no task waiting\n");
		} else {
			if (test_bit(MMU_PAGEFAULT, &data->state) &&
			    data->pa_iova != -1) {
				vcodec_iommu_unmap_iommu_with_iova(data->iommu_info,
								   session,
								   data->pa_hdl);
				data->pa_iova = -1;
				clear_bit(MMU_PAGEFAULT, &data->state);
			}
			reg_from_run_to_done(data, pservice->reg_codec);
			/* avoid vpu timeout and can't recover problem */
			if (data->mode == VCODEC_RUNNING_MODE_VPU)
				VDPU_SOFT_RESET(data->regs);
		}
	}

	if (atomic_read(&dev->irq_count_pp)) {
		atomic_sub(1, &dev->irq_count_pp);
		if (pservice->reg_pproc == NULL)
			vpu_err("error: pp isr with no task waiting\n");
		else
			reg_from_run_to_done(data, pservice->reg_pproc);
	}

	queue_work(pservice->set_workq, &data->set_work);
	mutex_unlock(&pservice->lock);
	return IRQ_HANDLED;
}

static irqreturn_t vepu_irq(int irq, void *dev_id)
{
	struct vpu_subdev_data *data = (struct vpu_subdev_data *)dev_id;
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_task_info *task = &data->task_info[TASK_ENC];
	struct vpu_device *dev = &data->enc_dev;
	u32 irq_status;

	irq_status = readl_relaxed(dev->regs + task->reg_irq);

	vpu_debug(DEBUG_TASK_INFO,
		  "vepu_irq reg %d status %x mask: irq %x ready %x error %0x\n",
		  task->reg_irq, irq_status,
		  task->irq_mask, task->ready_mask, task->error_mask);

	vpu_debug(DEBUG_IRQ_STATUS, "vepu_irq enc status %08x\n", irq_status);

	if (likely(irq_status & task->irq_mask)) {
		time_record(task, 1);

		if (check_irq_err(task, irq_status))
			atomic_add(1, &pservice->reset_request);

		/* clear enc IRQ */
		writel_relaxed(irq_status & (~task->irq_mask),
			       dev->regs + task->reg_irq);

		atomic_add(1, &dev->irq_count_codec);
		time_diff(task);
	}

	pservice->irq_status = irq_status;

	if (atomic_read(&dev->irq_count_codec))
		return IRQ_WAKE_THREAD;
	else
		return IRQ_NONE;
}

static irqreturn_t vepu_isr(int irq, void *dev_id)
{
	struct vpu_subdev_data *data = (struct vpu_subdev_data *)dev_id;
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_device *dev = &data->enc_dev;

	mutex_lock(&pservice->lock);
	if (atomic_read(&dev->irq_count_codec)) {
		atomic_sub(1, &dev->irq_count_codec);
		if (NULL == pservice->reg_codec)
			vpu_err("error: enc isr with no task waiting\n");
		else
			reg_from_run_to_done(data, pservice->reg_codec);
	}
	queue_work(pservice->set_workq, &data->set_work);
	mutex_unlock(&pservice->lock);

	return IRQ_HANDLED;
}

module_platform_driver(vcodec_driver);
MODULE_LICENSE("GPL v2");
