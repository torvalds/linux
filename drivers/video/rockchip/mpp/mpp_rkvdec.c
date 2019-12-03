// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Alpha Lin, alpha.lin@rock-chips.com
 *	Randy Li, randy.li@rock-chips.com
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */
#include <asm/cacheflush.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/thermal.h>
#include <linux/notifier.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/regulator/consumer.h>

#include <soc/rockchip/pm_domains.h>
#include <soc/rockchip/rockchip_sip.h>
#include <soc/rockchip/rockchip_opp_select.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

#define RKVDEC_DRIVER_NAME		"mpp_rkvdec"

#define IOMMU_GET_BUS_ID(x)		(((x) >> 6) & 0x1f)
#define IOMMU_PAGE_SIZE			SZ_4K

#define	RKVDEC_SESSION_MAX_BUFFERS	40
/* The maximum registers number of all the version */
#define HEVC_DEC_REG_NUM		68
#define HEVC_DEC_REG_START_INDEX	0
#define HEVC_DEC_REG_END_INDEX		67

#define RKVDEC_V1_REG_NUM		78
#define RKVDEC_V1_REG_START_INDEX	0
#define RKVDEC_V1_REG_END_INDEX		77

#define RKVDEC_V2_REG_NUM		109
#define RKVDEC_V2_REG_START_INDEX	0
#define RKVDEC_V2_REG_END_INDEX		108

#define RKVDEC_REG_INT_EN		0x004
#define RKVDEC_REG_INT_EN_INDEX		(1)
#define RKVDEC_WR_DDR_ALIGN_EN		BIT(23)
#define RKVDEC_FORCE_SOFT_RESET_VALID	BIT(21)
#define RKVDEC_SOFTWARE_RESET_EN	BIT(20)
#define RKVDEC_INT_COLMV_REF_ERROR	BIT(17)
#define RKVDEC_INT_BUF_EMPTY		BIT(16)
#define RKVDEC_INT_TIMEOUT		BIT(15)
#define RKVDEC_INT_STRM_ERROR		BIT(14)
#define RKVDEC_INT_BUS_ERROR		BIT(13)
#define RKVDEC_DEC_INT_RAW		BIT(9)
#define RKVDEC_DEC_INT			BIT(8)
#define RKVDEC_DEC_TIMEOUT_EN		BIT(5)
#define RKVDEC_DEC_IRQ_DIS		BIT(4)
#define RKVDEC_CLOCK_GATE_EN		BIT(1)
#define RKVDEC_DEC_START		BIT(0)

#define RKVDEC_REG_SYS_CTRL		0x008
#define RKVDEC_REG_SYS_CTRL_INDEX	(2)
#define RKVDEC_GET_FORMAT(x)		(((x) >> 20) & 0x3)
#define RKVDEC_FMT_H265D		(0)
#define RKVDEC_FMT_H264D		(1)
#define RKVDEC_FMT_VP9D			(2)

#define RKVDEC_REG_RLC_BASE		0x010
#define RKVDEC_REG_RLC_BASE_INDEX	(4)

#define RKVDEC_REG_PPS_BASE		0x0a0
#define RKVDEC_REG_PPS_BASE_INDEX	(42)

#define RKVDEC_REG_VP9_REFCOLMV_BASE		0x0d0
#define RKVDEC_REG_VP9_REFCOLMV_BASE_INDEX	(52)

#define RKVDEC_REG_CACHE0_SIZE_BASE	0x41c
#define RKVDEC_REG_CACHE1_SIZE_BASE	0x45c
#define RKVDEC_REG_CLR_CACHE0_BASE	0x410
#define RKVDEC_REG_CLR_CACHE1_BASE	0x450

#define RKVDEC_CACHE_PERMIT_CACHEABLE_ACCESS	BIT(0)
#define RKVDEC_CACHE_PERMIT_READ_ALLOCATE	BIT(1)
#define RKVDEC_CACHE_LINE_SIZE_64_BYTES		BIT(4)

#define RKVDEC_POWER_CTL_INDEX		(99)
#define RKVDEC_POWER_CTL_BASE		0x018c

#define FALLBACK_STATIC_TEMPERATURE	55000

#define to_rkvdec_task(task)		\
		container_of(task, struct rkvdec_task, mpp_task)
#define to_rkvdec_dev(dev)		\
		container_of(dev, struct rkvdec_dev, mpp)

enum RKVDEC_STATE {
	RKVDEC_STATE_NORMAL,
	RKVDEC_STATE_LT_START,
	RKVDEC_STATE_LT_RUN,
};

enum SET_CLK_EVENT {
	EVENT_POWER_ON = 0,
	EVENT_POWER_OFF,
	EVENT_ADJUST,
	EVENT_THERMAL,
	EVENT_BUTT,
};

struct rkvdec_task {
	struct mpp_task mpp_task;
	struct mpp_hw_info *hw_info;

	unsigned long aclk_freq;
	unsigned long clk_core_freq;
	unsigned long clk_cabac_freq;

	u32 reg[RKVDEC_V2_REG_NUM];
	u32 idx;

	u32 strm_addr;
	u32 irq_status;
};

struct rkvdec_dev {
	struct mpp_dev mpp;
	/* sip smc reset lock */
	struct mutex sip_reset_lock;

	struct clk *aclk;
	struct clk *hclk;
	struct clk *clk_core;
	struct clk *clk_cabac;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
#endif
	u32 aclk_debug;
	u32 clk_core_debug;
	u32 clk_cabac_debug;
	u32 session_max_buffers_debug;

	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct reset_control *rst_niu_a;
	struct reset_control *rst_niu_h;
	struct reset_control *rst_core;
	struct reset_control *rst_cabac;

	enum RKVDEC_STATE state;
	unsigned long aux_iova;
	struct page *aux_page;
	struct rkvdec_task *current_task;
	/* regulator and devfreq */
	struct regulator *vdd;
	struct devfreq *devfreq;
	struct devfreq *parent_devfreq;
	struct notifier_block devfreq_nb;
	struct thermal_cooling_device *devfreq_cooling;
	struct thermal_zone_device *thermal_zone;
	u32 static_power_coeff;
	s32 ts[4];
	struct mutex set_clk_lock; /* set clk lock */
	unsigned int thermal_div;
	unsigned long volt;
	unsigned long aclk_devf;
	unsigned long clk_core_devf;
	unsigned long clk_cabac_devf;
};

/*
 * hardware information
 */
static struct mpp_hw_info rk_hevcdec_hw_info = {
	.reg_num = HEVC_DEC_REG_NUM,
	.regidx_start = HEVC_DEC_REG_START_INDEX,
	.regidx_end = HEVC_DEC_REG_END_INDEX,
	.regidx_en = RKVDEC_REG_INT_EN_INDEX,
};

static struct mpp_hw_info rkvdec_v1_hw_info = {
	.reg_num = RKVDEC_V1_REG_NUM,
	.regidx_start = RKVDEC_V1_REG_START_INDEX,
	.regidx_end = RKVDEC_V1_REG_END_INDEX,
	.regidx_en = RKVDEC_REG_INT_EN_INDEX,
};

/*
 * file handle translate information
 */
static const char trans_tbl_h264d[] = {
	4, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
	23, 24, 41, 42, 43, 48, 75
};

static const char trans_tbl_h265d[] = {
	4, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
	23, 24, 42, 43
};

static const char trans_tbl_vp9d[] = {
	4, 6, 7, 11, 12, 13, 14, 15, 16
};

static struct mpp_trans_info rk_hevcdec_trans[] = {
	[RKVDEC_FMT_H265D] = {
		.count = sizeof(trans_tbl_h265d),
		.table = trans_tbl_h265d,
	},
};

static struct mpp_trans_info rkvdec_v1_trans[] = {
	[RKVDEC_FMT_H265D] = {
		.count = sizeof(trans_tbl_h265d),
		.table = trans_tbl_h265d,
	},
	[RKVDEC_FMT_H264D] = {
		.count = sizeof(trans_tbl_h264d),
		.table = trans_tbl_h264d,
	},
	[RKVDEC_FMT_VP9D] = {
		.count = sizeof(trans_tbl_vp9d),
		.table = trans_tbl_vp9d,
	},
};

static int rkvdec_set_clk(struct rkvdec_dev *dec,
			  unsigned long aclk_freq,
			  unsigned long core_freq,
			  unsigned long cabac_freq,
			  unsigned int event)
{
	mutex_lock(&dec->set_clk_lock);

	switch (event) {
	case EVENT_POWER_ON:
		clk_set_rate(dec->aclk, dec->aclk_devf);
		clk_set_rate(dec->clk_core, dec->clk_core_devf);
		clk_set_rate(dec->clk_cabac, dec->clk_cabac_devf);
		dec->thermal_div = 0;
		break;
	case EVENT_POWER_OFF:
		clk_set_rate(dec->aclk, aclk_freq);
		clk_set_rate(dec->clk_core, core_freq);
		clk_set_rate(dec->clk_cabac, cabac_freq);
		dec->thermal_div = 0;
		break;
	case EVENT_ADJUST:
		if (!dec->thermal_div) {
			clk_set_rate(dec->aclk, aclk_freq);
			clk_set_rate(dec->clk_core, core_freq);
			clk_set_rate(dec->clk_cabac, cabac_freq);
		} else {
			clk_set_rate(dec->aclk,
				     aclk_freq / dec->thermal_div);
			clk_set_rate(dec->clk_core,
				     core_freq / dec->thermal_div);
			clk_set_rate(dec->clk_cabac,
				     cabac_freq / dec->thermal_div);
		}
		dec->aclk_devf = aclk_freq;
		dec->clk_core_devf = core_freq;
		dec->clk_cabac_devf = cabac_freq;
		break;
	case EVENT_THERMAL:
		dec->thermal_div = dec->aclk_devf / aclk_freq;
		if (dec->thermal_div > 4)
			dec->thermal_div = 4;
		if (dec->thermal_div) {
			clk_set_rate(dec->aclk,
				     dec->aclk_devf / dec->thermal_div);
			clk_set_rate(dec->clk_core,
				     dec->clk_core_devf / dec->thermal_div);
			clk_set_rate(dec->clk_cabac,
				     dec->clk_cabac_devf / dec->thermal_div);
		}
		break;
	}

	mutex_unlock(&dec->set_clk_lock);

	return 0;
}

static int devfreq_target(struct device *dev,
			  unsigned long *freq, u32 flags)
{
	int ret = 0;
	unsigned int clk_event;
	struct dev_pm_opp *opp;
	unsigned long target_volt, target_freq;
	unsigned long aclk_freq, core_freq, cabac_freq;

	struct rkvdec_dev *dec = dev_get_drvdata(dev);
	struct devfreq *devfreq = dec->devfreq;
	struct devfreq_dev_status *stat = &devfreq->last_status;
	unsigned long old_clk_rate = stat->current_frequency;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp)) {
		dev_err(dev, "Failed to find opp for %lu Hz\n", *freq);
		return PTR_ERR(opp);
	}
	target_freq = dev_pm_opp_get_freq(opp);
	target_volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	if (target_freq < *freq) {
		clk_event = EVENT_THERMAL;
		aclk_freq = target_freq;
		core_freq = target_freq;
		cabac_freq = target_freq;
	} else {
		clk_event = stat->busy_time ? EVENT_POWER_ON : EVENT_POWER_OFF;
		aclk_freq = dec->aclk_devf;
		core_freq = dec->clk_core_devf;
		cabac_freq = dec->clk_cabac_devf;
	}

	if (old_clk_rate == target_freq) {
		if (dec->volt == target_volt)
			return ret;
		ret = regulator_set_voltage(dec->vdd, target_volt, INT_MAX);
		if (ret) {
			dev_err(dev, "Cannot set voltage %lu uV\n",
				target_volt);
			return ret;
		}
		dec->volt = target_volt;
		return 0;
	}

	if (old_clk_rate < target_freq) {
		ret = regulator_set_voltage(dec->vdd, target_volt, INT_MAX);
		if (ret) {
			dev_err(dev, "set voltage %lu uV\n", target_volt);
			return ret;
		}
	}

	dev_dbg(dev, "%lu-->%lu\n", old_clk_rate, target_freq);
	rkvdec_set_clk(dec, aclk_freq, core_freq, cabac_freq, clk_event);
	stat->current_frequency = target_freq;

	if (old_clk_rate > target_freq) {
		ret = regulator_set_voltage(dec->vdd, target_volt, INT_MAX);
		if (ret) {
			dev_err(dev, "set vol %lu uV\n", target_volt);
			return ret;
		}
	}
	dec->volt = target_volt;

	return ret;
}

static int devfreq_get_cur_freq(struct device *dev,
				unsigned long *freq)
{
	struct rkvdec_dev *dec = dev_get_drvdata(dev);

	*freq = clk_get_rate(dec->aclk);

	return 0;
}

static int devfreq_get_dev_status(struct device *dev,
				  struct devfreq_dev_status *stat)
{
	struct rkvdec_dev *dec = dev_get_drvdata(dev);
	struct devfreq *devfreq = dec->devfreq;

	memcpy(stat, &devfreq->last_status, sizeof(*stat));

	return 0;
}

static struct devfreq_dev_profile devfreq_profile = {
	.target	= devfreq_target,
	.get_cur_freq = devfreq_get_cur_freq,
	.get_dev_status	= devfreq_get_dev_status,
};

static unsigned long
model_static_power(struct devfreq *devfreq,
		   unsigned long voltage)
{
	struct device *dev = devfreq->dev.parent;
	struct rkvdec_dev *dec = dev_get_drvdata(dev);
	struct thermal_zone_device *tz = dec->thermal_zone;

	int temperature;
	unsigned long temp;
	unsigned long temp_squared, temp_cubed, temp_scaling_factor;
	const unsigned long voltage_cubed = (voltage * voltage * voltage) >> 10;

	if (!IS_ERR_OR_NULL(tz) && tz->ops->get_temp) {
		int ret;

		ret = tz->ops->get_temp(tz, &temperature);
		if (ret) {
			dev_warn_ratelimited(dev, "ddr thermal zone failed\n");
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
	temp_scaling_factor = (dec->ts[3] * temp_cubed)
	    + (dec->ts[2] * temp_squared) + (dec->ts[1] * temp) + dec->ts[0];

	return (((dec->static_power_coeff * voltage_cubed) >> 20)
		* temp_scaling_factor) / 1000000;
}

static struct devfreq_cooling_power cooling_power_data = {
	.get_static_power = model_static_power,
	.dyn_power_coeff = 120,
};

static int power_model_simple_init(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);
	struct device_node *np = mpp->dev->of_node;

	u32 temp;
	const char *tz_name;
	struct device_node *power_model_node;

	power_model_node = of_get_child_by_name(np, "vcodec_power_model");
	if (!power_model_node) {
		dev_err(mpp->dev, "could not find power_model node\n");
		return -ENODEV;
	}

	if (of_property_read_string(power_model_node,
				    "thermal-zone",
				    &tz_name)) {
		dev_err(mpp->dev, "ts in power_model not available\n");
		return -EINVAL;
	}

	dec->thermal_zone = thermal_zone_get_zone_by_name(tz_name);
	if (IS_ERR(dec->thermal_zone)) {
		pr_warn("Error getting ddr thermal zone, not yet ready?\n");
		dec->thermal_zone = NULL;
		return -EPROBE_DEFER;
	}

	if (of_property_read_u32(power_model_node,
				 "static-power-coefficient",
				 &dec->static_power_coeff)) {
		dev_err(mpp->dev, "static-power-coefficient not available\n");
		return -EINVAL;
	}
	if (of_property_read_u32(power_model_node,
				 "dynamic-power-coefficient",
				 &temp)) {
		dev_err(mpp->dev, "dynamic-power-coefficient not available\n");
		return -EINVAL;
	}
	cooling_power_data.dyn_power_coeff = (unsigned long)temp;

	if (of_property_read_u32_array(power_model_node,
				       "ts",
				       (u32 *)dec->ts,
				       4)) {
		dev_err(mpp->dev, "ts in power_model not available\n");
		return -EINVAL;
	}

	return 0;
}

static int devfreq_notifier_call(struct notifier_block *nb,
				 unsigned long event,
				 void *data)
{
	struct rkvdec_dev *dec = container_of(nb,
					      struct rkvdec_dev,
					      devfreq_nb);

	if (!dec)
		return NOTIFY_OK;

	if (event == DEVFREQ_PRECHANGE)
		mutex_lock(&dec->sip_reset_lock);
	else if (event == DEVFREQ_POSTCHANGE)
		mutex_unlock(&dec->sip_reset_lock);

	return NOTIFY_OK;
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
static int fill_scaling_list_pps(struct rkvdec_task *task,
				 int fd, int offset, int count,
				 int pps_info_size, int sub_addr_offset)
{
	struct dma_buf *dmabuf = NULL;
	void *vaddr = NULL;
	u8 *pps = NULL;
	u32 scaling_fd = 0;
	u32 scaling_offset;
	int ret = 0;
	u32 base = sub_addr_offset;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dmabuf)) {
		mpp_err("invliad pps buffer\n");
		return -ENOENT;
	}

	ret = dma_buf_begin_cpu_access(dmabuf, DMA_FROM_DEVICE);
	if (ret) {
		mpp_err("can't access the pps buffer\n");
		goto done;
	}

	vaddr = dma_buf_vmap(dmabuf);
	if (!vaddr) {
		mpp_err("can't access the pps buffer\n");
		ret = -EIO;
		goto done;
	}
	pps = vaddr + offset;

	memcpy(&scaling_offset, pps + base, sizeof(scaling_offset));
	scaling_offset = le32_to_cpu(scaling_offset);

	scaling_fd = scaling_offset & 0x3ff;
	scaling_offset = scaling_offset >> 10;

	if (scaling_fd > 0) {
		struct mpp_mem_region *mem_region = NULL;
		u32 tmp = 0;
		int i = 0;

		mem_region = mpp_task_attach_fd(&task->mpp_task,
						scaling_fd);
		if (IS_ERR(mem_region)) {
			ret = PTR_ERR(mem_region);
			goto done;
		}

		tmp = mem_region->iova & 0xffffffff;
		tmp += scaling_offset;
		tmp = cpu_to_le32(tmp);
		mpp_debug(DEBUG_PPS_FILL,
			  "pps at %p, scaling fd: %3d => %pad + offset %10d\n",
			  pps, scaling_fd, &mem_region->iova, offset);

		/* Fill the scaling list address in each pps entries */
		for (i = 0; i < count; i++, base += pps_info_size)
			memcpy(pps + base, &tmp, sizeof(tmp));
	}

done:
	dma_buf_vunmap(dmabuf, vaddr);
	dma_buf_end_cpu_access(dmabuf, DMA_FROM_DEVICE);
	dma_buf_put(dmabuf);

	return ret;
}

static void *rkvdec_alloc_task(struct mpp_session *session,
			       void __user *src, u32 size)
{
	u32 fmt;
	int ret;
	u32 reg_len;
	int pps_fd;
	u32 pps_offset;
	struct rkvdec_task *task = NULL;
	u32 dwsize = size / sizeof(u32);
	struct mpp_dev *mpp = session->mpp;

	mpp_debug_enter();

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;

	mpp_task_init(session, &task->mpp_task);

	task->hw_info = mpp->var->hw_info;
	reg_len = min(task->hw_info->reg_num, dwsize);
	if (copy_from_user(task->reg, src, reg_len * 4)) {
		mpp_err("error: copy_from_user failed in reg_init\n");
		goto fail;
	}

	fmt = RKVDEC_GET_FORMAT(task->reg[RKVDEC_REG_SYS_CTRL_INDEX]);
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
	 * So on VP9 4K decoder colmv base we scale the offset by 16
	 */
	if (fmt == RKVDEC_FMT_VP9D) {
		struct mpp_mem_region *mem_region = NULL;
		dma_addr_t iova = 0;
		u32 offset = task->reg[RKVDEC_REG_VP9_REFCOLMV_BASE_INDEX];
		int fd = task->reg[RKVDEC_REG_VP9_REFCOLMV_BASE_INDEX] & 0x3ff;

		offset = offset >> 10 << 4;
		mem_region = mpp_task_attach_fd(&task->mpp_task, fd);
		if (IS_ERR(mem_region))
			goto fail;

		iova = mem_region->iova;
		task->reg[RKVDEC_REG_VP9_REFCOLMV_BASE_INDEX] = iova + offset;
	}

	pps_fd = task->reg[RKVDEC_REG_PPS_BASE_INDEX] & 0x3ff;
	pps_offset = task->reg[RKVDEC_REG_PPS_BASE_INDEX] >> 10;
	if (pps_fd > 0) {
		int pps_info_offset;
		int pps_info_count;
		int pps_info_size;
		int scaling_list_addr_offset;

		switch (fmt) {
		case RKVDEC_FMT_H264D:
			pps_info_offset = pps_offset;
			pps_info_count = 256;
			pps_info_size = 32;
			scaling_list_addr_offset = 23;
			break;
		case RKVDEC_FMT_H265D:
			pps_info_offset = pps_offset;
			pps_info_count = 64;
			pps_info_size = 80;
			scaling_list_addr_offset = 74;
			break;
		default:
			pps_info_offset = 0;
			pps_info_count = 0;
			pps_info_size = 0;
			scaling_list_addr_offset = 0;
			break;
		}

		mpp_debug(DEBUG_PPS_FILL,
			  "scaling list filling parameter:\n");
		mpp_debug(DEBUG_PPS_FILL,
			  "pps_info_offset %d\n", pps_info_offset);
		mpp_debug(DEBUG_PPS_FILL,
			  "pps_info_count  %d\n", pps_info_count);
		mpp_debug(DEBUG_PPS_FILL,
			  "pps_info_size   %d\n", pps_info_size);
		mpp_debug(DEBUG_PPS_FILL,
			  "scaling_list_addr_offset %d\n",
			  scaling_list_addr_offset);

		if (pps_info_count) {
			ret = fill_scaling_list_pps(task, pps_fd,
						    pps_info_offset,
						    pps_info_count,
						    pps_info_size,
						    scaling_list_addr_offset);
			if (ret) {
				mpp_err("fill pps failed\n");
				goto fail;
			}
		}
	}

	ret = mpp_translate_reg_address(session->mpp,
					&task->mpp_task,
					fmt, task->reg);
	if (ret) {
		mpp_err("error: translate reg address failed.\n");
		mpp_dump_reg(task->reg,
			     task->hw_info->regidx_start,
			     task->hw_info->regidx_end);
		goto fail;
	}

	task->strm_addr = task->reg[RKVDEC_REG_RLC_BASE_INDEX];

	mpp_debug_leave();

	return &task->mpp_task;

fail:
	mpp_task_finalize(session, &task->mpp_task);
	kfree(task);
	return NULL;
}

static int rkvdec_prepare(struct mpp_dev *mpp,
			  struct mpp_task *task)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	if (dec->state == RKVDEC_STATE_NORMAL)
		return -EINVAL;
	/*
	 * Don't do soft reset before running or you will meet 0x00408322
	 * if you will decode a HEVC stream. Different error for the AVC.
	 */

	return 0;
}

static int rkvdec_run(struct mpp_dev *mpp,
		      struct mpp_task *mpp_task)
{
	u32 i;
	u32 regidx_start;
	u32 regidx_end;
	u32 regidx_en;
	u32 reg = 0;
	struct rkvdec_dev *dec = NULL;
	struct rkvdec_task *task = NULL;

	mpp_debug_enter();

	dec = to_rkvdec_dev(mpp);
	task = to_rkvdec_task(mpp_task);

	switch (dec->state) {
	case RKVDEC_STATE_NORMAL:
		/* FIXME: spin lock here */
		dec->current_task = task;

		/* set cache size */
		reg = RKVDEC_CACHE_PERMIT_CACHEABLE_ACCESS
			| RKVDEC_CACHE_PERMIT_READ_ALLOCATE;
		if (!mpp_debug_unlikely(DEBUG_CACHE_32B))
			reg |= RKVDEC_CACHE_LINE_SIZE_64_BYTES;

		mpp_write_relaxed(mpp, RKVDEC_REG_CACHE0_SIZE_BASE, reg);
		mpp_write_relaxed(mpp, RKVDEC_REG_CACHE0_SIZE_BASE, reg);
		/* clear cache */
		mpp_write_relaxed(mpp, RKVDEC_REG_CLR_CACHE0_BASE, 1);
		mpp_write_relaxed(mpp, RKVDEC_REG_CLR_CACHE1_BASE, 1);
		/* set registers for hardware */
		regidx_start = task->hw_info->regidx_start;
		regidx_end = task->hw_info->regidx_end;
		regidx_en = task->hw_info->regidx_en;
		for (i = regidx_start; i <= regidx_end; i++) {
			if (i == regidx_en)
				continue;
			mpp_write_relaxed(mpp, i * sizeof(u32), task->reg[i]);
		}
		/* Flush the register before the start the device */
		wmb();
		mpp_write(mpp, RKVDEC_REG_INT_EN,
			  task->reg[regidx_en] | RKVDEC_DEC_START);
		break;
	default:
		break;
	}

	mpp_debug_leave();

	return 0;
}

static int rkvdec_3328_run(struct mpp_dev *mpp,
			   struct mpp_task *mpp_task)
{
	u32 fmt = 0;
	u32 cfg = 0;
	struct rkvdec_task *task = NULL;

	mpp_debug_enter();

	task = to_rkvdec_task(mpp_task);

	/*
	 * HW defeat workaround: VP9 power save optimization cause decoding
	 * corruption, disable optimization here.
	 */
	fmt = RKVDEC_GET_FORMAT(task->reg[RKVDEC_REG_SYS_CTRL_INDEX]);
	if (fmt == RKVDEC_FMT_VP9D) {
		cfg = task->reg[RKVDEC_POWER_CTL_INDEX] | 0xFFFF;
		task->reg[RKVDEC_POWER_CTL_INDEX] = cfg & (~(1 << 12));
		mpp_write_relaxed(mpp, RKVDEC_POWER_CTL_BASE,
				  task->reg[RKVDEC_POWER_CTL_INDEX]);
	}

	rkvdec_run(mpp, mpp_task);

	mpp_debug_leave();

	return 0;
}

static int rkvdec_irq(struct mpp_dev *mpp)
{
	mpp->irq_status = mpp_read(mpp, RKVDEC_REG_INT_EN);
	if (!(mpp->irq_status & RKVDEC_DEC_INT_RAW))
		return IRQ_NONE;

	mpp_write(mpp, RKVDEC_REG_INT_EN, 0);

	return IRQ_WAKE_THREAD;
}

static int rkvdec_isr(struct mpp_dev *mpp)
{
	u32 err_mask;
	struct rkvdec_task *task = NULL;
	struct mpp_task *mpp_task = NULL;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	/* FIXME use a spin lock here */
	task = dec->current_task;
	if (!task) {
		dev_err(dec->mpp.dev, "no current task\n");
		return IRQ_HANDLED;
	}
	mpp_time_diff(&task->mpp_task);
	dec->current_task = NULL;
	task->irq_status = mpp->irq_status;
	switch (dec->state) {
	case RKVDEC_STATE_NORMAL:
		mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n",
			  task->irq_status);

		err_mask = RKVDEC_INT_BUF_EMPTY
			| RKVDEC_INT_BUS_ERROR
			| RKVDEC_INT_COLMV_REF_ERROR
			| RKVDEC_INT_STRM_ERROR
			| RKVDEC_INT_TIMEOUT;

		if (err_mask & task->irq_status)
			atomic_inc(&mpp->reset_request);

		mpp_task = &task->mpp_task;
		mpp_task_finish(mpp_task->session, mpp_task);

		mpp_debug_leave();
		return IRQ_HANDLED;
	default:
		goto fail;
	}
fail:
	return IRQ_HANDLED;
}

static int rkvdec_finish(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	u32 i;
	u32 regidx_start;
	u32 regidx_end;
	u32 dec_get;
	s32 dec_length;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);
	struct rkvdec_task *task = to_rkvdec_task(mpp_task);

	mpp_debug_enter();

	switch (dec->state) {
	case RKVDEC_STATE_NORMAL: {
		/* read register after running */
		regidx_start = 2;//task->hw_info->regidx_start;
		regidx_end = task->hw_info->regidx_end;
		for (i = regidx_start; i <= regidx_end; i++)
			task->reg[i] = mpp_read_relaxed(mpp, i * sizeof(u32));
		task->reg[RKVDEC_REG_INT_EN_INDEX] = task->irq_status;
		/* revert hack for decoded length */
		dec_get = task->reg[RKVDEC_REG_RLC_BASE_INDEX];
		dec_length = dec_get - task->strm_addr;
		task->reg[RKVDEC_REG_RLC_BASE_INDEX] = dec_length << 10;
		mpp_debug(DEBUG_REGISTER,
			  "dec_get %08x dec_length %d\n",
			  dec_get, dec_length);
	} break;
	default:
		break;
	}

	mpp_debug_leave();

	return 0;
}

static int rkvdec_result(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task,
			 u32 __user *dst, u32 size)
{
	struct rkvdec_task *task = to_rkvdec_task(mpp_task);

	/* FIXME may overflow the kernel */
	if (copy_to_user(dst, task->reg, size)) {
		mpp_err("copy_to_user failed\n");
		return -EIO;
	}

	return 0;
}

static int rkvdec_free_task(struct mpp_session *session,
			    struct mpp_task *mpp_task)
{
	struct rkvdec_task *task = to_rkvdec_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	kfree(task);

	return 0;
}

static int rkvdec_debugfs_remove(struct mpp_dev *mpp)
{
#ifdef CONFIG_DEBUG_FS
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	debugfs_remove_recursive(dec->debugfs);
#endif
	return 0;
}

static int rkvdec_debugfs_init(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);
	struct device_node *np = mpp->dev->of_node;

	dec->aclk_debug = 0;
	dec->clk_core_debug = 0;
	dec->clk_cabac_debug = 0;
	dec->session_max_buffers_debug = 0;
#ifdef CONFIG_DEBUG_FS
	dec->debugfs = debugfs_create_dir(np->name, mpp->srv->debugfs);
	if (IS_ERR_OR_NULL(dec->debugfs)) {
		mpp_err("failed on open debugfs\n");
		dec->debugfs = NULL;
		return -EIO;
	}
	debugfs_create_u32("aclk", 0644,
			   dec->debugfs, &dec->aclk_debug);
	debugfs_create_u32("clk_core", 0644,
			   dec->debugfs, &dec->clk_core_debug);
	debugfs_create_u32("clk_cabac", 0644,
			   dec->debugfs, &dec->clk_cabac_debug);
	debugfs_create_u32("session_buffers", 0644,
			   dec->debugfs, &dec->session_max_buffers_debug);
#endif
	if (dec->session_max_buffers_debug)
		mpp->session_max_buffers = dec->session_max_buffers_debug;

	return 0;
}

static int rkvdec_init(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	mutex_init(&dec->sip_reset_lock);
	mutex_init(&dec->set_clk_lock);

	mpp->grf_info = &mpp->srv->grf_infos[MPP_DRIVER_RKVDEC];

	dec->aclk = devm_clk_get(mpp->dev, "aclk_vcodec");
	if (IS_ERR_OR_NULL(dec->aclk)) {
		mpp_err("failed on clk_get aclk_vcodec\n");
		dec->aclk = NULL;
	}
	dec->hclk = devm_clk_get(mpp->dev, "hclk_vcodec");
	if (IS_ERR_OR_NULL(dec->hclk)) {
		mpp_err("failed on clk_get hclk_vcodec\n");
		dec->hclk = NULL;
	}
	dec->clk_cabac = devm_clk_get(mpp->dev, "clk_cabac");
	if (IS_ERR_OR_NULL(dec->clk_cabac)) {
		mpp_err("failed on clk_get clk_cabac\n");
		dec->clk_cabac = NULL;
	}
	dec->clk_core = devm_clk_get(mpp->dev, "clk_core");
	if (IS_ERR_OR_NULL(dec->clk_core)) {
		mpp_err("failed on clk_get clk_core\n");
		dec->clk_core = NULL;
	}

	dec->rst_a = devm_reset_control_get(mpp->dev, "video_a");
	if (IS_ERR_OR_NULL(dec->rst_a)) {
		mpp_err("No aclk reset resource define\n");
		dec->rst_a = NULL;
	}
	dec->rst_h = devm_reset_control_get(mpp->dev, "video_h");
	if (IS_ERR_OR_NULL(dec->rst_h)) {
		mpp_err("No hclk reset resource define\n");
		dec->rst_h = NULL;
	}
	dec->rst_niu_a = devm_reset_control_get(mpp->dev, "niu_a");
	if (IS_ERR_OR_NULL(dec->rst_niu_a)) {
		mpp_err("No niu aclk reset resource define\n");
		dec->rst_niu_a = NULL;
	}
	dec->rst_niu_h = devm_reset_control_get(mpp->dev, "niu_h");
	if (IS_ERR_OR_NULL(dec->rst_niu_h)) {
		mpp_err("No niu hclk reset resource define\n");
		dec->rst_niu_h = NULL;
	}
	dec->rst_cabac = devm_reset_control_get(mpp->dev, "video_cabac");
	if (IS_ERR_OR_NULL(dec->rst_cabac)) {
		mpp_err("No cabac reset resource define\n");
		dec->rst_cabac = NULL;
	}
	dec->rst_core = devm_reset_control_get(mpp->dev, "video_core");
	if (IS_ERR_OR_NULL(dec->rst_core)) {
		mpp_err("No core reset resource define\n");
		dec->rst_core = NULL;
	}

	return 0;
}

static int rkvdec_3328_iommu_hdl(struct iommu_domain *iommu,
				 struct device *iommu_dev,
				 unsigned long iova,
				 int status, void *arg)
{
	int ret = 0;
	struct device *dev = (struct device *)arg;
	struct platform_device *pdev = NULL;
	struct rkvdec_dev *dec = NULL;
	struct mpp_dev *mpp = NULL;

	pdev = container_of(dev, struct platform_device, dev);
	if (!pdev) {
		dev_err(dev, "invalid platform_device\n");
		ret = -ENXIO;
		goto done;
	}

	dec = platform_get_drvdata(pdev);
	if (!dec) {
		dev_err(dev, "invalid device instance\n");
		ret = -ENXIO;
		goto done;
	}
	mpp = &dec->mpp;

	/*
	 * defeat workaround, invalidate address generated when rk322x
	 * hevc decoder tile mode pre-fetch colmv data.
	 */
	if (IOMMU_GET_BUS_ID(status) == 2) {
		unsigned long page_iova = 0;
		/* avoid another page fault occur after page fault */
		if (dec->aux_iova)
			iommu_unmap(mpp->iommu_info->domain,
				    dec->aux_iova,
				    IOMMU_PAGE_SIZE);

		page_iova = round_down(iova, IOMMU_PAGE_SIZE);
		ret = iommu_map(mpp->iommu_info->domain,
				page_iova,
				page_to_phys(dec->aux_page),
				IOMMU_PAGE_SIZE,
				DMA_FROM_DEVICE);
		if (!ret)
			dec->aux_iova = page_iova;
	}

done:
	return ret;
}

static int rkvdec_devfreq_remove(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	devfreq_unregister_opp_notifier(mpp->dev, dec->devfreq);
	dev_pm_opp_of_remove_table(mpp->dev);

	return 0;
}

static int rkvdec_devfreq_init(struct mpp_dev *mpp)
{
	int ret = 0;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	dec->vdd = devm_regulator_get_optional(mpp->dev, "vcodec");
	if (IS_ERR_OR_NULL(dec->vdd)) {
		dev_warn(mpp->dev, "no regulator for %s\n",
			 dev_name(mpp->dev));
		dec->vdd = NULL;
		ret = -EINVAL;
		goto done;
	} else {
		struct devfreq_dev_status *stat;

		ret = rockchip_init_opp_table(mpp->dev, NULL,
					      "rkvdec_leakage", "vcodec");
		if (ret) {
			dev_err(mpp->dev, "Failed to init_opp_table\n");
			goto done;
		}
		dec->devfreq = devm_devfreq_add_device(mpp->dev,
						       &devfreq_profile,
						       "userspace",
						       NULL);
		if (IS_ERR(dec->devfreq)) {
			ret = PTR_ERR(dec->devfreq);
			goto done;
		}

		stat = &dec->devfreq->last_status;
		stat->current_frequency = clk_get_rate(dec->aclk);

		ret = devfreq_register_opp_notifier(mpp->dev, dec->devfreq);
		if (ret)
			goto done;
	}

	dec->parent_devfreq = devfreq_get_devfreq_by_phandle(mpp->dev, 0);
	if (IS_ERR_OR_NULL(dec->parent_devfreq)) {
		dev_warn(mpp->dev, "parent devfreq is error\n");
		dec->parent_devfreq = NULL;
		ret = -EINVAL;
		goto done;
	} else {
		dec->devfreq_nb.notifier_call = devfreq_notifier_call;
		devm_devfreq_register_notifier(mpp->dev,
					       dec->parent_devfreq,
					       &dec->devfreq_nb,
					       DEVFREQ_TRANSITION_NOTIFIER);
	}
	/* power simplle init */
	ret = power_model_simple_init(mpp);
	if (!ret && dec->devfreq) {
		dec->devfreq_cooling =
			of_devfreq_cooling_register_power(mpp->dev->of_node,
							  dec->devfreq,
							  &cooling_power_data);
		if (IS_ERR_OR_NULL(dec->devfreq_cooling)) {
			ret = -ENXIO;
			dev_err(mpp->dev, "Failed to register cooling\n");
			goto done;
		}
	}

done:
	return ret;
}

static int rkvdec_3328_init(struct mpp_dev *mpp)
{
	int ret = 0;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	rkvdec_init(mpp);

	/* warkaround for mmu pagefault */
	dec->aux_page = alloc_page(GFP_KERNEL);
	if (!dec->aux_page) {
		dev_err(mpp->dev, "allocate a page for auxiliary usage\n");
		ret = -ENOMEM;
		goto done;
	}
	dec->aux_iova = 0;
	iommu_set_fault_handler(mpp->iommu_info->domain,
				rkvdec_3328_iommu_hdl,
				mpp->dev);

	ret = rkvdec_devfreq_init(mpp);
done:
	return ret;
}

static int rkvdec_3328_exit(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	if (dec->aux_page)
		__free_page(dec->aux_page);

	if (dec->aux_iova)
		iommu_unmap(mpp->iommu_info->domain,
			    dec->aux_iova,
			    IOMMU_PAGE_SIZE);
	rkvdec_devfreq_remove(mpp);

	return 0;
}

static int rkvdec_power_on(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	if (dec->aclk)
		clk_prepare_enable(dec->aclk);
	if (dec->hclk)
		clk_prepare_enable(dec->hclk);
	if (dec->clk_cabac)
		clk_prepare_enable(dec->clk_cabac);
	if (dec->clk_core)
		clk_prepare_enable(dec->clk_core);

	return 0;
}

static int rkvdec_power_off(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	if (dec->aclk)
		clk_disable_unprepare(dec->aclk);
	if (dec->hclk)
		clk_disable_unprepare(dec->hclk);
	if (dec->clk_cabac)
		clk_disable_unprepare(dec->clk_cabac);
	if (dec->clk_core)
		clk_disable_unprepare(dec->clk_core);

	return 0;
}

static int rkvdec_get_freq(struct mpp_dev *mpp,
			   struct mpp_task *mpp_task)
{
	struct rkvdec_task *task = to_rkvdec_task(mpp_task);

	task->aclk_freq = 300;
	task->clk_cabac_freq = 200;
	task->clk_core_freq = 200;

	return 0;
}

static int rkvdec_3328_get_freq(struct mpp_dev *mpp,
				struct mpp_task *mpp_task)
{
	struct rkvdec_task *task =  to_rkvdec_task(mpp_task);
	u32 ddr_align_en = task->reg[RKVDEC_REG_INT_EN_INDEX] &
					RKVDEC_WR_DDR_ALIGN_EN;
	u32 fmt = RKVDEC_GET_FORMAT(task->reg[RKVDEC_REG_SYS_CTRL_INDEX]);

	if (ddr_align_en) {
		if (fmt == RKVDEC_FMT_H264D) {
			task->aclk_freq = 400;
			task->clk_cabac_freq = 400;
			task->clk_core_freq = 250;
		} else {
			task->aclk_freq = 500;
			task->clk_cabac_freq = 400;
			task->clk_core_freq = 250;
		}
	} else {
		if (fmt == RKVDEC_FMT_H264D) {
			task->aclk_freq = 400;
			task->clk_cabac_freq = 400;
			task->clk_core_freq = 300;
		} else {
			task->aclk_freq = 500;
			task->clk_cabac_freq = 300;
			task->clk_core_freq = 400;
		}
	}

	return 0;
}

static int rkvdec_set_freq(struct mpp_dev *mpp,
			   struct mpp_task *mpp_task)
{
	struct devfreq_dev_status *stat;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);
	struct rkvdec_task *task =  to_rkvdec_task(mpp_task);

	task->aclk_freq = dec->aclk_debug ?
			dec->aclk_debug : task->aclk_freq;
	task->clk_cabac_freq = dec->clk_cabac_debug ?
			dec->clk_cabac_debug : task->clk_cabac_freq;
	task->clk_core_freq = dec->clk_core_debug ?
			dec->clk_core_debug : task->clk_core_freq;

	if (dec->devfreq) {
		stat = &dec->devfreq->last_status;
		stat->busy_time = 1;
		stat->total_time = 1;
		rkvdec_set_clk(dec, task->aclk_freq * MHZ,
			       task->clk_core_freq * MHZ,
			       task->clk_cabac_freq * MHZ,
			       EVENT_ADJUST);
	} else {
		clk_set_rate(dec->aclk, task->aclk_freq * MHZ);
		clk_set_rate(dec->clk_cabac, task->clk_cabac_freq * MHZ);
		clk_set_rate(dec->clk_core, task->clk_core_freq * MHZ);
	}

	return 0;
}

static int rkvdec_reduce_freq(struct mpp_dev *mpp)
{
	struct devfreq_dev_status *stat;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	if (dec->devfreq) {
		stat = &dec->devfreq->last_status;
		stat->busy_time = 0;
		stat->total_time = 1;
		rkvdec_set_clk(dec, 50 * MHZ, 50 * MHZ, 50 * MHZ, EVENT_ADJUST);
	} else {
		if (dec->aclk)
			clk_set_rate(dec->aclk, 50 * MHZ);
		if (dec->clk_cabac)
			clk_set_rate(dec->clk_cabac, 50 * MHZ);
		if (dec->clk_core)
			clk_set_rate(dec->clk_core, 50 * MHZ);
	}

	return 0;
}

static int rkvdec_reset(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	mpp_debug_enter();
	if (dec->rst_a && dec->rst_h) {
		rockchip_pmu_idle_request(mpp->dev, true);
		mpp_safe_reset(dec->rst_niu_a);
		mpp_safe_reset(dec->rst_niu_h);
		mpp_safe_reset(dec->rst_a);
		mpp_safe_reset(dec->rst_h);
		mpp_safe_reset(dec->rst_core);
		mpp_safe_reset(dec->rst_cabac);
		udelay(5);
		mpp_safe_unreset(dec->rst_niu_h);
		mpp_safe_unreset(dec->rst_niu_a);
		mpp_safe_unreset(dec->rst_a);
		mpp_safe_unreset(dec->rst_h);
		mpp_safe_unreset(dec->rst_core);
		mpp_safe_unreset(dec->rst_cabac);
		rockchip_pmu_idle_request(mpp->dev, false);
	}
	mpp_debug_leave();

	return 0;
}

static int rkvdec_sip_reset(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

/* The reset flow in arm trustzone firmware */
#if CONFIG_ROCKCHIP_SIP
	mutex_lock(&dec->sip_reset_lock);
	sip_smc_vpu_reset(0, 0, 0);
	mutex_unlock(&dec->sip_reset_lock);

	return 0;
#else
	return rkvdec_reset(mpp);
#endif
}

static struct mpp_hw_ops rkvdec_v1_hw_ops = {
	.init = rkvdec_init,
	.power_on = rkvdec_power_on,
	.power_off = rkvdec_power_off,
	.get_freq = rkvdec_get_freq,
	.set_freq = rkvdec_set_freq,
	.reduce_freq = rkvdec_reduce_freq,
	.reset = rkvdec_reset,
};

static struct mpp_hw_ops rkvdec_3399_hw_ops = {
	.init = rkvdec_init,
	.power_on = rkvdec_power_on,
	.power_off = rkvdec_power_off,
	.get_freq = rkvdec_get_freq,
	.set_freq = rkvdec_set_freq,
	.reduce_freq = rkvdec_reduce_freq,
	.reset = rkvdec_sip_reset,
};

static struct mpp_dev_ops rkvdec_v1_dev_ops = {
	.alloc_task = rkvdec_alloc_task,
	.prepare = rkvdec_prepare,
	.run = rkvdec_run,
	.irq = rkvdec_irq,
	.isr = rkvdec_isr,
	.finish = rkvdec_finish,
	.result = rkvdec_result,
	.free_task = rkvdec_free_task,
};

static struct mpp_hw_ops rkvdec_3328_hw_ops = {
	.init = rkvdec_3328_init,
	.exit = rkvdec_3328_exit,
	.power_on = rkvdec_power_on,
	.power_off = rkvdec_power_off,
	.get_freq = rkvdec_3328_get_freq,
	.set_freq = rkvdec_set_freq,
	.reduce_freq = rkvdec_reduce_freq,
	.reset = rkvdec_sip_reset,
};

static struct mpp_dev_ops rkvdec_3328_dev_ops = {
	.alloc_task = rkvdec_alloc_task,
	.prepare = rkvdec_prepare,
	.run = rkvdec_3328_run,
	.irq = rkvdec_irq,
	.isr = rkvdec_isr,
	.finish = rkvdec_finish,
	.result = rkvdec_result,
	.free_task = rkvdec_free_task,
};

static const struct mpp_dev_var rk_hevcdec_data = {
	.device_type = MPP_DEVICE_DEC_RKV,
	.hw_info = &rk_hevcdec_hw_info,
	.trans_info = rk_hevcdec_trans,
	.hw_ops = &rkvdec_v1_hw_ops,
	.dev_ops = &rkvdec_v1_dev_ops,
};

static const struct mpp_dev_var rkvdec_v1_data = {
	.device_type = MPP_DEVICE_DEC_RKV,
	.hw_info = &rkvdec_v1_hw_info,
	.trans_info = rkvdec_v1_trans,
	.hw_ops = &rkvdec_v1_hw_ops,
	.dev_ops = &rkvdec_v1_dev_ops,
};

static const struct mpp_dev_var rkvdec_3399_data = {
	.device_type = MPP_DEVICE_DEC_RKV,
	.hw_info = &rkvdec_v1_hw_info,
	.trans_info = rkvdec_v1_trans,
	.hw_ops = &rkvdec_3399_hw_ops,
	.dev_ops = &rkvdec_v1_dev_ops,
};

static const struct mpp_dev_var rkvdec_3328_data = {
	.device_type = MPP_DEVICE_DEC_RKV,
	.hw_info = &rkvdec_v1_hw_info,
	.trans_info = rkvdec_v1_trans,
	.hw_ops = &rkvdec_3328_hw_ops,
	.dev_ops = &rkvdec_3328_dev_ops,
};

static const struct of_device_id mpp_rkvdec_dt_match[] = {
	{
		.compatible = "rockchip,hevc-decoder",
		.data = &rk_hevcdec_data,
	},
	{
		.compatible = "rockchip,rkv-decoder-v1",
		.data = &rkvdec_v1_data,
	},
	{
		.compatible = "rockchip,rkv-decoder-rk3399",
		.data = &rkvdec_3399_data,
	},
	{
		.compatible = "rockchip,rkv-decoder-rk3328",
		.data = &rkvdec_3328_data,
	},
	{},
};

static int rkvdec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rkvdec_dev *dec = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;
	int ret = 0;

	dev_info(dev, "probing start\n");
	dec = devm_kzalloc(dev, sizeof(*dec), GFP_KERNEL);
	if (!dec)
		return -ENOMEM;

	mpp = &dec->mpp;
	platform_set_drvdata(pdev, dec);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_rkvdec_dt_match,
				      pdev->dev.of_node);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;
	}

	ret = mpp_dev_probe(mpp, pdev);
	if (ret) {
		dev_err(dev, "probe sub driver failed\n");
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(dev, mpp->irq,
					mpp_dev_irq,
					mpp_dev_isr_sched,
					IRQF_SHARED,
					dev_name(dev), mpp);
	if (ret) {
		dev_err(dev, "register interrupter runtime failed\n");
		return -EINVAL;
	}

	dec->state = RKVDEC_STATE_NORMAL;
	mpp->session_max_buffers = RKVDEC_SESSION_MAX_BUFFERS;
	rkvdec_debugfs_init(mpp);
	dev_info(dev, "probing finish\n");

	return 0;
}

static int rkvdec_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rkvdec_dev *dec = platform_get_drvdata(pdev);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(&dec->mpp);
	rkvdec_debugfs_remove(&dec->mpp);

	return 0;
}

static void rkvdec_shutdown(struct platform_device *pdev)
{
	int ret;
	int val;
	struct device *dev = &pdev->dev;
	struct rkvdec_dev *dec = platform_get_drvdata(pdev);
	struct mpp_dev *mpp = &dec->mpp;

	dev_info(dev, "shutdown device\n");

	atomic_inc(&mpp->srv->shutdown_request);
	ret = readx_poll_timeout(atomic_read,
				 &mpp->total_running,
				 val, val == 0, 20000, 200000);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "wait total running time out\n");
}

struct platform_driver rockchip_rkvdec_driver = {
	.probe = rkvdec_probe,
	.remove = rkvdec_remove,
	.shutdown = rkvdec_shutdown,
	.driver = {
		.name = RKVDEC_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_rkvdec_dt_match),
	},
};
EXPORT_SYMBOL(rockchip_rkvdec_driver);
