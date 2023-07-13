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
#include <linux/kernel.h>
#include <linux/thermal.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/regulator/consumer.h>

#include <soc/rockchip/pm_domains.h>
#include <soc/rockchip/rockchip_sip.h>
#include <soc/rockchip/rockchip_opp_select.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"
#include <soc/rockchip/rockchip_iommu.h>

#include "hack/mpp_hack_px30.h"

#define RKVDEC_DRIVER_NAME		"mpp_rkvdec"

#define IOMMU_GET_BUS_ID(x)		(((x) >> 6) & 0x1f)
#define IOMMU_PAGE_SIZE			SZ_4K

#define	RKVDEC_SESSION_MAX_BUFFERS	40
/* The maximum registers number of all the version */
#define HEVC_DEC_REG_NUM		68
#define HEVC_DEC_REG_HW_ID_INDEX	0
#define HEVC_DEC_REG_START_INDEX	0
#define HEVC_DEC_REG_END_INDEX		67

#define RKVDEC_V1_REG_NUM		78
#define RKVDEC_V1_REG_HW_ID_INDEX	0
#define RKVDEC_V1_REG_START_INDEX	0
#define RKVDEC_V1_REG_END_INDEX		77

#define RKVDEC_V2_REG_NUM		109
#define RKVDEC_V2_REG_HW_ID_INDEX	0
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
#define RKVDEC_RGE_WIDTH_INDEX		(3)
#define RKVDEC_GET_FORMAT(x)		(((x) >> 20) & 0x3)
#define REVDEC_GET_PROD_NUM(x)		(((x) >> 16) & 0xffff)
#define RKVDEC_GET_WIDTH(x)		(((x) & 0x3ff) << 4)
#define RKVDEC_FMT_H265D		(0)
#define RKVDEC_FMT_H264D		(1)
#define RKVDEC_FMT_VP9D			(2)

#define RKVDEC_REG_RLC_BASE		0x010
#define RKVDEC_REG_RLC_BASE_INDEX	(4)

#define RKVDEC_RGE_YSTRDE_INDEX		(8)
#define RKVDEC_GET_YSTRDE(x)		(((x) & 0x1fffff) << 4)

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

enum RKVDEC_MODE {
	RKVDEC_MODE_NONE,
	RKVDEC_MODE_ONEFRAME,
	RKVDEC_MODE_BUTT
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

	enum RKVDEC_MODE link_mode;
	enum MPP_CLOCK_MODE clk_mode;
	u32 reg[RKVDEC_V2_REG_NUM];
	struct reg_offset_info off_inf;

	u32 strm_addr;
	u32 irq_status;
	/* req for current task */
	u32 w_req_cnt;
	struct mpp_request w_reqs[MPP_MAX_MSG_NUM];
	u32 r_req_cnt;
	struct mpp_request r_reqs[MPP_MAX_MSG_NUM];
	/* ystride info */
	u32 pixels;
};

struct rkvdec_dev {
	struct mpp_dev mpp;
	/* sip smc reset lock */
	struct mutex sip_reset_lock;

	struct mpp_clk_info aclk_info;
	struct mpp_clk_info hclk_info;
	struct mpp_clk_info core_clk_info;
	struct mpp_clk_info cabac_clk_info;
	struct mpp_clk_info hevc_cabac_clk_info;
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

	unsigned long aux_iova;
	struct page *aux_page;
#ifdef CONFIG_PM_DEVFREQ
	struct regulator *vdd;
	struct devfreq *devfreq;
	struct devfreq *parent_devfreq;
	struct notifier_block devfreq_nb;
	struct thermal_cooling_device *devfreq_cooling;
	struct thermal_zone_device *thermal_zone;
	u32 static_power_coeff;
	s32 ts[4];
	/* set clk lock */
	struct mutex set_clk_lock;
	unsigned int thermal_div;
	unsigned long volt;
	unsigned long devf_aclk_rate_hz;
	unsigned long devf_core_rate_hz;
	unsigned long devf_cabac_rate_hz;
#endif
	/* record last infos */
	u32 last_fmt;
	bool had_reset;
	bool grf_changed;
};

/*
 * hardware information
 */
static struct mpp_hw_info rk_hevcdec_hw_info = {
	.reg_num = HEVC_DEC_REG_NUM,
	.reg_id = HEVC_DEC_REG_HW_ID_INDEX,
	.reg_start = HEVC_DEC_REG_START_INDEX,
	.reg_end = HEVC_DEC_REG_END_INDEX,
	.reg_en = RKVDEC_REG_INT_EN_INDEX,
};

static struct mpp_hw_info rkvdec_v1_hw_info = {
	.reg_num = RKVDEC_V1_REG_NUM,
	.reg_id = RKVDEC_V1_REG_HW_ID_INDEX,
	.reg_start = RKVDEC_V1_REG_START_INDEX,
	.reg_end = RKVDEC_V1_REG_END_INDEX,
	.reg_en = RKVDEC_REG_INT_EN_INDEX,
};

/*
 * file handle translate information
 */
static const u16 trans_tbl_h264d[] = {
	4, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
	23, 24, 41, 42, 43, 48, 75
};

static const u16 trans_tbl_h265d[] = {
	4, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
	23, 24, 42, 43
};

static const u16 trans_tbl_vp9d[] = {
	4, 6, 7, 11, 12, 13, 14, 15, 16
};

static struct mpp_trans_info rk_hevcdec_trans[] = {
	[RKVDEC_FMT_H265D] = {
		.count = ARRAY_SIZE(trans_tbl_h265d),
		.table = trans_tbl_h265d,
	},
};

static struct mpp_trans_info rkvdec_v1_trans[] = {
	[RKVDEC_FMT_H265D] = {
		.count = ARRAY_SIZE(trans_tbl_h265d),
		.table = trans_tbl_h265d,
	},
	[RKVDEC_FMT_H264D] = {
		.count = ARRAY_SIZE(trans_tbl_h264d),
		.table = trans_tbl_h264d,
	},
	[RKVDEC_FMT_VP9D] = {
		.count = ARRAY_SIZE(trans_tbl_vp9d),
		.table = trans_tbl_vp9d,
	},
};

#ifdef CONFIG_PM_DEVFREQ
static int rkvdec_devf_set_clk(struct rkvdec_dev *dec,
			       unsigned long aclk_rate_hz,
			       unsigned long core_rate_hz,
			       unsigned long cabac_rate_hz,
			       unsigned int event)
{
	struct clk *aclk = dec->aclk_info.clk;
	struct clk *clk_core = dec->core_clk_info.clk;
	struct clk *clk_cabac = dec->cabac_clk_info.clk;

	mutex_lock(&dec->set_clk_lock);

	switch (event) {
	case EVENT_POWER_ON:
		clk_set_rate(aclk, dec->devf_aclk_rate_hz);
		clk_set_rate(clk_core, dec->devf_core_rate_hz);
		clk_set_rate(clk_cabac, dec->devf_cabac_rate_hz);
		dec->thermal_div = 0;
		break;
	case EVENT_POWER_OFF:
		clk_set_rate(aclk, aclk_rate_hz);
		clk_set_rate(clk_core, core_rate_hz);
		clk_set_rate(clk_cabac, cabac_rate_hz);
		dec->thermal_div = 0;
		break;
	case EVENT_ADJUST:
		if (!dec->thermal_div) {
			clk_set_rate(aclk, aclk_rate_hz);
			clk_set_rate(clk_core, core_rate_hz);
			clk_set_rate(clk_cabac, cabac_rate_hz);
		} else {
			clk_set_rate(aclk,
				     aclk_rate_hz / dec->thermal_div);
			clk_set_rate(clk_core,
				     core_rate_hz / dec->thermal_div);
			clk_set_rate(clk_cabac,
				     cabac_rate_hz / dec->thermal_div);
		}
		dec->devf_aclk_rate_hz = aclk_rate_hz;
		dec->devf_core_rate_hz = core_rate_hz;
		dec->devf_cabac_rate_hz = cabac_rate_hz;
		break;
	case EVENT_THERMAL:
		dec->thermal_div = dec->devf_aclk_rate_hz / aclk_rate_hz;
		if (dec->thermal_div > 4)
			dec->thermal_div = 4;
		if (dec->thermal_div) {
			clk_set_rate(aclk,
				     dec->devf_aclk_rate_hz / dec->thermal_div);
			clk_set_rate(clk_core,
				     dec->devf_core_rate_hz / dec->thermal_div);
			clk_set_rate(clk_cabac,
				     dec->devf_cabac_rate_hz / dec->thermal_div);
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
	unsigned long aclk_rate_hz, core_rate_hz, cabac_rate_hz;

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
		aclk_rate_hz = target_freq;
		core_rate_hz = target_freq;
		cabac_rate_hz = target_freq;
	} else {
		clk_event = stat->busy_time ? EVENT_POWER_ON : EVENT_POWER_OFF;
		aclk_rate_hz = dec->devf_aclk_rate_hz;
		core_rate_hz = dec->devf_core_rate_hz;
		cabac_rate_hz = dec->devf_cabac_rate_hz;
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
	rkvdec_devf_set_clk(dec, aclk_rate_hz, core_rate_hz, cabac_rate_hz, clk_event);
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

	*freq = clk_get_rate(dec->aclk_info.clk);

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
#endif

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
	/* NOTE: scaling buffer in pps, have no offset */
	memcpy(&scaling_fd, pps + base, sizeof(scaling_fd));
	scaling_fd = le32_to_cpu(scaling_fd);
	if (scaling_fd > 0) {
		struct mpp_mem_region *mem_region = NULL;
		u32 tmp = 0;
		int i = 0;

		mem_region = mpp_task_attach_fd(&task->mpp_task,
						scaling_fd);
		if (IS_ERR(mem_region)) {
			mpp_err("scaling list fd %d attach failed\n", scaling_fd);
			ret = PTR_ERR(mem_region);
			goto done;
		}

		tmp = mem_region->iova & 0xffffffff;
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

static int rkvdec_process_scl_fd(struct mpp_session *session,
				 struct rkvdec_task *task,
				 struct mpp_task_msgs *msgs)
{
	int ret = 0;
	int pps_fd;
	u32 pps_offset;
	int idx = RKVDEC_REG_PPS_BASE_INDEX;
	u32 fmt = RKVDEC_GET_FORMAT(task->reg[RKVDEC_REG_SYS_CTRL_INDEX]);

	if (session->msg_flags & MPP_FLAGS_REG_NO_OFFSET) {
		pps_fd = task->reg[idx];
		pps_offset = 0;
	} else {
		pps_fd = task->reg[idx] & 0x3ff;
		pps_offset = task->reg[idx] >> 10;
	}

	pps_offset += mpp_query_reg_offset_info(&task->off_inf, idx);
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

fail:
	return ret;
}

static int rkvdec_process_reg_fd(struct mpp_session *session,
				 struct rkvdec_task *task,
				 struct mpp_task_msgs *msgs)
{
	int ret = 0;
	u32 fmt = RKVDEC_GET_FORMAT(task->reg[RKVDEC_REG_SYS_CTRL_INDEX]);

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
		int fd;
		u32 offset;
		dma_addr_t iova = 0;
		struct mpp_mem_region *mem_region = NULL;
		int idx = RKVDEC_REG_VP9_REFCOLMV_BASE_INDEX;

		if (session->msg_flags & MPP_FLAGS_REG_NO_OFFSET) {
			fd = task->reg[idx];
			offset = 0;
		} else {
			fd = task->reg[idx] & 0x3ff;
			offset = task->reg[idx] >> 10 << 4;
		}
		mem_region = mpp_task_attach_fd(&task->mpp_task, fd);
		if (IS_ERR(mem_region)) {
			mpp_err("reg[%03d]: %08x fd %d attach failed\n",
				idx, task->reg[idx], fd);
			return -EFAULT;
		}

		iova = mem_region->iova;
		task->reg[idx] = iova + offset;
	}

	ret = mpp_translate_reg_address(session, &task->mpp_task,
					fmt, task->reg, &task->off_inf);
	if (ret)
		return ret;

	mpp_translate_reg_offset_info(&task->mpp_task,
				      &task->off_inf, task->reg);
	return 0;
}

static int rkvdec_extract_task_msg(struct rkvdec_task *task,
				   struct mpp_task_msgs *msgs)
{
	u32 i;
	int ret;
	struct mpp_request *req;
	struct mpp_hw_info *hw_info = task->mpp_task.hw_info;

	for (i = 0; i < msgs->req_cnt; i++) {
		u32 off_s, off_e;

		req = &msgs->reqs[i];
		if (!req->size)
			continue;

		switch (req->cmd) {
		case MPP_CMD_SET_REG_WRITE: {
			off_s = hw_info->reg_start * sizeof(u32);
			off_e = hw_info->reg_end * sizeof(u32);
			ret = mpp_check_req(req, 0, sizeof(task->reg),
					    off_s, off_e);
			if (ret)
				continue;
			if (copy_from_user((u8 *)task->reg + req->offset,
					   req->data, req->size)) {
				mpp_err("copy_from_user reg failed\n");
				return -EIO;
			}
			memcpy(&task->w_reqs[task->w_req_cnt++],
			       req, sizeof(*req));
		} break;
		case MPP_CMD_SET_REG_READ: {
			off_s = hw_info->reg_start * sizeof(u32);
			off_e = hw_info->reg_end * sizeof(u32);
			ret = mpp_check_req(req, 0, sizeof(task->reg),
					    off_s, off_e);
			if (ret)
				continue;
			memcpy(&task->r_reqs[task->r_req_cnt++],
			       req, sizeof(*req));
		} break;
		case MPP_CMD_SET_REG_ADDR_OFFSET: {
			mpp_extract_reg_offset_info(&task->off_inf, req);
		} break;
		default:
			break;
		}
	}
	mpp_debug(DEBUG_TASK_INFO, "w_req_cnt %d, r_req_cnt %d\n",
		  task->w_req_cnt, task->r_req_cnt);

	return 0;
}

static void *rkvdec_alloc_task(struct mpp_session *session,
			       struct mpp_task_msgs *msgs)
{
	int ret;
	struct mpp_task *mpp_task = NULL;
	struct rkvdec_task *task = NULL;
	struct mpp_dev *mpp = session->mpp;

	mpp_debug_enter();

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;

	mpp_task = &task->mpp_task;
	mpp_task_init(session, mpp_task);
	mpp_task->hw_info = mpp->var->hw_info;
	mpp_task->reg = task->reg;
	/* extract reqs for current task */
	ret = rkvdec_extract_task_msg(task, msgs);
	if (ret)
		goto fail;
	/* process fd in pps for 264 and 265 */
	if (!(msgs->flags & MPP_FLAGS_SCL_FD_NO_TRANS)) {
		ret = rkvdec_process_scl_fd(session, task, msgs);
		if (ret)
			goto fail;
	}
	/* process fd in register */
	if (!(msgs->flags & MPP_FLAGS_REG_FD_NO_TRANS)) {
		ret = rkvdec_process_reg_fd(session, task, msgs);
		if (ret)
			goto fail;
	}
	task->strm_addr = task->reg[RKVDEC_REG_RLC_BASE_INDEX];
	task->link_mode = RKVDEC_MODE_ONEFRAME;
	task->clk_mode = CLK_MODE_NORMAL;

	/* get resolution info */
	task->pixels = RKVDEC_GET_YSTRDE(task->reg[RKVDEC_RGE_YSTRDE_INDEX]);
	mpp_debug(DEBUG_TASK_INFO, "ystride=%d\n", task->pixels);

	mpp_debug_leave();

	return mpp_task;

fail:
	mpp_task_dump_mem_region(mpp, mpp_task);
	mpp_task_dump_reg(mpp, mpp_task);
	mpp_task_finalize(session, mpp_task);
	kfree(task);
	return NULL;
}

static void *rkvdec_prepare_with_reset(struct mpp_dev *mpp,
				       struct mpp_task *mpp_task)
{
	unsigned long flags;
	struct mpp_task *out_task = NULL;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	spin_lock_irqsave(&mpp->queue->running_lock, flags);
	out_task = list_empty(&mpp->queue->running_list) ? mpp_task : NULL;
	spin_unlock_irqrestore(&mpp->queue->running_lock, flags);

	if (out_task && !dec->had_reset) {
		struct rkvdec_task *task = to_rkvdec_task(out_task);
		u32 fmt = RKVDEC_GET_FORMAT(task->reg[RKVDEC_REG_SYS_CTRL_INDEX]);

		/* in 3399 3228 and 3229 chips, when 264 switch vp9,
		 * hardware will timeout, and can't recover problem.
		 * so reset it when 264 switch vp9, before hardware run.
		 */
		if (dec->last_fmt == RKVDEC_FMT_H264D && fmt == RKVDEC_FMT_VP9D) {
			mpp_power_on(mpp);
			mpp_dev_reset(mpp);
			mpp_power_off(mpp);
		}
	}

	return out_task;
}

static int rkvdec_run(struct mpp_dev *mpp,
		      struct mpp_task *mpp_task)
{
	int i;
	u32 reg_en;
	struct rkvdec_task *task = NULL;
	u32 timing_en = mpp->srv->timing_en;

	mpp_debug_enter();

	task = to_rkvdec_task(mpp_task);
	reg_en = mpp_task->hw_info->reg_en;
	switch (task->link_mode) {
	case RKVDEC_MODE_ONEFRAME: {
		u32 reg;

		/* set cache size */
		reg = RKVDEC_CACHE_PERMIT_CACHEABLE_ACCESS
			| RKVDEC_CACHE_PERMIT_READ_ALLOCATE;
		if (!mpp_debug_unlikely(DEBUG_CACHE_32B))
			reg |= RKVDEC_CACHE_LINE_SIZE_64_BYTES;

		mpp_write_relaxed(mpp, RKVDEC_REG_CACHE0_SIZE_BASE, reg);
		mpp_write_relaxed(mpp, RKVDEC_REG_CACHE1_SIZE_BASE, reg);
		/* clear cache */
		mpp_write_relaxed(mpp, RKVDEC_REG_CLR_CACHE0_BASE, 1);
		mpp_write_relaxed(mpp, RKVDEC_REG_CLR_CACHE1_BASE, 1);
		/* set registers for hardware */
		for (i = 0; i < task->w_req_cnt; i++) {
			int s, e;
			struct mpp_request *req = &task->w_reqs[i];

			s = req->offset / sizeof(u32);
			e = s + req->size / sizeof(u32);
			mpp_write_req(mpp, task->reg, s, e, reg_en);
		}
		/* init current task */
		mpp->cur_task = mpp_task;
		mpp_task_run_begin(mpp_task, timing_en, MPP_WORK_TIMEOUT_DELAY);
		/* Flush the register before the start the device */
		wmb();
		mpp_write(mpp, RKVDEC_REG_INT_EN,
			  task->reg[reg_en] | RKVDEC_DEC_START);

		mpp_task_run_end(mpp_task, timing_en);
	} break;
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
	 * HW defeat workaround: VP9 and H.265 power save optimization cause decoding
	 * corruption, disable optimization here.
	 */
	fmt = RKVDEC_GET_FORMAT(task->reg[RKVDEC_REG_SYS_CTRL_INDEX]);
	if (fmt == RKVDEC_FMT_VP9D || fmt == RKVDEC_FMT_H265D) {
		cfg = task->reg[RKVDEC_POWER_CTL_INDEX] | 0xFFFF;
		task->reg[RKVDEC_POWER_CTL_INDEX] = cfg & (~(1 << 12));
		mpp_write_relaxed(mpp, RKVDEC_POWER_CTL_BASE,
				  task->reg[RKVDEC_POWER_CTL_INDEX]);
	}

	rkvdec_run(mpp, mpp_task);

	mpp_debug_leave();

	return 0;
}

static int rkvdec_1126_run(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	struct rkvdec_task *task = to_rkvdec_task(mpp_task);

	if (task->link_mode == RKVDEC_MODE_ONEFRAME)
		mpp_iommu_flush_tlb(mpp->iommu_info);

	return rkvdec_run(mpp, mpp_task);
}

static int rkvdec_px30_run(struct mpp_dev *mpp,
		    struct mpp_task *mpp_task)
{
	mpp_iommu_flush_tlb(mpp->iommu_info);
	return rkvdec_run(mpp, mpp_task);
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
	struct mpp_task *mpp_task = mpp->cur_task;

	mpp_debug_enter();
	/* FIXME use a spin lock here */
	if (!mpp_task) {
		dev_err(mpp->dev, "no current task\n");
		goto done;
	}
	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;
	task = to_rkvdec_task(mpp_task);
	task->irq_status = mpp->irq_status;
	switch (task->link_mode) {
	case RKVDEC_MODE_ONEFRAME: {
		mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n", task->irq_status);

		err_mask = RKVDEC_INT_BUF_EMPTY
			| RKVDEC_INT_BUS_ERROR
			| RKVDEC_INT_COLMV_REF_ERROR
			| RKVDEC_INT_STRM_ERROR
			| RKVDEC_INT_TIMEOUT;

		if (err_mask & task->irq_status)
			atomic_inc(&mpp->reset_request);

		mpp_task_finish(mpp_task->session, mpp_task);
	} break;
	default:
		break;
	}
done:
	mpp_debug_leave();
	return IRQ_HANDLED;
}

static int rkvdec_3328_isr(struct mpp_dev *mpp)
{
	u32 err_mask;
	struct rkvdec_task *task = NULL;
	struct mpp_task *mpp_task = mpp->cur_task;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	mpp_debug_enter();
	/* FIXME use a spin lock here */
	if (!mpp_task) {
		dev_err(mpp->dev, "no current task\n");
		goto done;
	}
	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;
	task = to_rkvdec_task(mpp_task);
	task->irq_status = mpp->irq_status;
	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n", task->irq_status);

	err_mask = RKVDEC_INT_BUF_EMPTY
		| RKVDEC_INT_BUS_ERROR
		| RKVDEC_INT_COLMV_REF_ERROR
		| RKVDEC_INT_STRM_ERROR
		| RKVDEC_INT_TIMEOUT;
	if (err_mask & task->irq_status)
		atomic_inc(&mpp->reset_request);

	/* unmap reserve buffer */
	if (dec->aux_iova != -1) {
		iommu_unmap(mpp->iommu_info->domain, dec->aux_iova, IOMMU_PAGE_SIZE);
		dec->aux_iova = -1;
	}

	mpp_task_finish(mpp_task->session, mpp_task);
done:
	mpp_debug_leave();
	return IRQ_HANDLED;
}

static int rkvdec_finish(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	u32 i;
	u32 dec_get;
	s32 dec_length;
	struct rkvdec_task *task = to_rkvdec_task(mpp_task);

	mpp_debug_enter();

	switch (task->link_mode) {
	case RKVDEC_MODE_ONEFRAME: {
		u32 s, e;
		struct mpp_request *req;

		/* read register after running */
		for (i = 0; i < task->r_req_cnt; i++) {
			req = &task->r_reqs[i];
			s = req->offset / sizeof(u32);
			e = s + req->size / sizeof(u32);
			mpp_read_req(mpp, task->reg, s, e);
		}
		/* revert hack for irq status */
		task->reg[RKVDEC_REG_INT_EN_INDEX] = task->irq_status;
		/* revert hack for decoded length */
		dec_get = mpp_read_relaxed(mpp, RKVDEC_REG_RLC_BASE);
		dec_length = dec_get - task->strm_addr;
		task->reg[RKVDEC_REG_RLC_BASE_INDEX] = dec_length << 10;
		mpp_debug(DEBUG_REGISTER,
			  "dec_get %08x dec_length %d\n", dec_get, dec_length);
	} break;
	default:
		break;
	}

	mpp_debug_leave();

	return 0;
}

static int rkvdec_finish_with_record_info(struct mpp_dev *mpp,
					  struct mpp_task *mpp_task)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);
	struct rkvdec_task *task = to_rkvdec_task(mpp_task);

	rkvdec_finish(mpp, mpp_task);
	dec->last_fmt = RKVDEC_GET_FORMAT(task->reg[RKVDEC_REG_SYS_CTRL_INDEX]);
	dec->had_reset = (atomic_read(&mpp->reset_request) > 0) ? true : false;

	return 0;
}

static int rkvdec_result(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task,
			 struct mpp_task_msgs *msgs)
{
	u32 i;
	struct mpp_request *req;
	struct rkvdec_task *task = to_rkvdec_task(mpp_task);

	/* FIXME may overflow the kernel */
	for (i = 0; i < task->r_req_cnt; i++) {
		req = &task->r_reqs[i];

		if (copy_to_user(req->data,
				 (u8 *)task->reg + req->offset,
				 req->size)) {
			mpp_err("copy_to_user reg fail\n");
			return -EIO;
		}
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

#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
static int rkvdec_procfs_remove(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	if (dec->procfs) {
		proc_remove(dec->procfs);
		dec->procfs = NULL;
	}

	return 0;
}

static int rkvdec_procfs_init(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	dec->procfs = proc_mkdir(mpp->dev->of_node->name, mpp->srv->procfs);
	if (IS_ERR_OR_NULL(dec->procfs)) {
		mpp_err("failed on open procfs\n");
		dec->procfs = NULL;
		return -EIO;
	}

	/* for common mpp_dev options */
	mpp_procfs_create_common(dec->procfs, mpp);

	mpp_procfs_create_u32("aclk", 0644,
			      dec->procfs, &dec->aclk_info.debug_rate_hz);
	mpp_procfs_create_u32("clk_core", 0644,
			      dec->procfs, &dec->core_clk_info.debug_rate_hz);
	mpp_procfs_create_u32("clk_cabac", 0644,
			      dec->procfs, &dec->cabac_clk_info.debug_rate_hz);
	mpp_procfs_create_u32("clk_hevc_cabac", 0644,
			      dec->procfs, &dec->hevc_cabac_clk_info.debug_rate_hz);
	mpp_procfs_create_u32("session_buffers", 0644,
			      dec->procfs, &mpp->session_max_buffers);

	return 0;
}
#else
static inline int rkvdec_procfs_remove(struct mpp_dev *mpp)
{
	return 0;
}

static inline int rkvdec_procfs_init(struct mpp_dev *mpp)
{
	return 0;
}
#endif

static int rkvdec_init(struct mpp_dev *mpp)
{
	int ret;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	mutex_init(&dec->sip_reset_lock);
	mpp->grf_info = &mpp->srv->grf_infos[MPP_DRIVER_RKVDEC];

	/* Get clock info from dtsi */
	ret = mpp_get_clk_info(mpp, &dec->aclk_info, "aclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get aclk_vcodec\n");
	ret = mpp_get_clk_info(mpp, &dec->hclk_info, "hclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get hclk_vcodec\n");
	ret = mpp_get_clk_info(mpp, &dec->core_clk_info, "clk_core");
	if (ret)
		mpp_err("failed on clk_get clk_core\n");
	ret = mpp_get_clk_info(mpp, &dec->cabac_clk_info, "clk_cabac");
	if (ret)
		mpp_err("failed on clk_get clk_cabac\n");
	ret = mpp_get_clk_info(mpp, &dec->hevc_cabac_clk_info, "clk_hevc_cabac");
	if (ret)
		mpp_err("failed on clk_get clk_hevc_cabac\n");
	/* Set default rates */
	mpp_set_clk_info_rate_hz(&dec->aclk_info, CLK_MODE_DEFAULT, 300 * MHZ);
	mpp_set_clk_info_rate_hz(&dec->core_clk_info, CLK_MODE_DEFAULT, 200 * MHZ);
	mpp_set_clk_info_rate_hz(&dec->cabac_clk_info, CLK_MODE_DEFAULT, 200 * MHZ);
	mpp_set_clk_info_rate_hz(&dec->hevc_cabac_clk_info, CLK_MODE_DEFAULT, 300 * MHZ);

	/* Get normal max workload from dtsi */
	of_property_read_u32(mpp->dev->of_node,
			     "rockchip,default-max-load", &dec->default_max_load);
	/* Get reset control from dtsi */
	dec->rst_a = mpp_reset_control_get(mpp, RST_TYPE_A, "video_a");
	if (!dec->rst_a)
		mpp_err("No aclk reset resource define\n");
	dec->rst_h = mpp_reset_control_get(mpp, RST_TYPE_H, "video_h");
	if (!dec->rst_h)
		mpp_err("No hclk reset resource define\n");
	dec->rst_niu_a = mpp_reset_control_get(mpp, RST_TYPE_NIU_A, "niu_a");
	if (!dec->rst_niu_a)
		mpp_err("No niu aclk reset resource define\n");
	dec->rst_niu_h = mpp_reset_control_get(mpp, RST_TYPE_NIU_H, "niu_h");
	if (!dec->rst_niu_h)
		mpp_err("No niu hclk reset resource define\n");
	dec->rst_core = mpp_reset_control_get(mpp, RST_TYPE_CORE, "video_core");
	if (!dec->rst_core)
		mpp_err("No core reset resource define\n");
	dec->rst_cabac = mpp_reset_control_get(mpp, RST_TYPE_CABAC, "video_cabac");
	if (!dec->rst_cabac)
		mpp_err("No cabac reset resource define\n");
	dec->rst_hevc_cabac = mpp_reset_control_get(mpp, RST_TYPE_HEVC_CABAC, "video_hevc_cabac");
	if (!dec->rst_hevc_cabac)
		mpp_err("No hevc cabac reset resource define\n");

	return 0;
}

static int rkvdec_px30_init(struct mpp_dev *mpp)
{
	rkvdec_init(mpp);
	return px30_workaround_combo_init(mpp);
}

static int rkvdec_3036_init(struct mpp_dev *mpp)
{
	rkvdec_init(mpp);
	set_bit(mpp->var->device_type, &mpp->queue->dev_active_flags);
	return 0;
}

static int rkvdec_3328_iommu_hdl(struct iommu_domain *iommu,
				 struct device *iommu_dev,
				 unsigned long iova,
				 int status, void *arg)
{
	int ret = 0;
	struct mpp_dev *mpp = (struct mpp_dev *)arg;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	/*
	 * defeat workaround, invalidate address generated when rk322x
	 * hevc decoder tile mode pre-fetch colmv data.
	 */
	if (IOMMU_GET_BUS_ID(status) == 2) {
		unsigned long page_iova = 0;
		/* avoid another page fault occur after page fault */
		if (dec->aux_iova != -1) {
			iommu_unmap(mpp->iommu_info->domain, dec->aux_iova, IOMMU_PAGE_SIZE);
			dec->aux_iova = -1;
		}

		page_iova = round_down(iova, IOMMU_PAGE_SIZE);
		ret = iommu_map(mpp->iommu_info->domain, page_iova,
				page_to_phys(dec->aux_page), IOMMU_PAGE_SIZE,
				IOMMU_READ | IOMMU_WRITE);
		if (!ret)
			dec->aux_iova = page_iova;
	}

	return ret;
}

#ifdef CONFIG_PM_DEVFREQ
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
	struct devfreq_dev_status *stat;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	mutex_init(&dec->set_clk_lock);
	dec->parent_devfreq = devfreq_get_devfreq_by_phandle(mpp->dev, "rkvdec_devfreq", 0);
	if (IS_ERR_OR_NULL(dec->parent_devfreq)) {
		if (PTR_ERR(dec->parent_devfreq) == -EPROBE_DEFER) {
			dev_warn(mpp->dev, "parent devfreq is not ready, retry\n");

			return -EPROBE_DEFER;
		}
	} else {
		dec->devfreq_nb.notifier_call = devfreq_notifier_call;
		devm_devfreq_register_notifier(mpp->dev,
					       dec->parent_devfreq,
					       &dec->devfreq_nb,
					       DEVFREQ_TRANSITION_NOTIFIER);
	}

	dec->vdd = devm_regulator_get_optional(mpp->dev, "vcodec");
	if (IS_ERR_OR_NULL(dec->vdd)) {
		if (PTR_ERR(dec->vdd) == -EPROBE_DEFER) {
			dev_warn(mpp->dev, "vcodec regulator not ready, retry\n");

			return -EPROBE_DEFER;
		}
		dev_warn(mpp->dev, "no regulator for vcodec\n");

		return 0;
	}

	ret = rockchip_init_opp_table(mpp->dev, NULL,
				      "rkvdec_leakage", "vcodec");
	if (ret) {
		dev_err(mpp->dev, "Failed to init_opp_table\n");
		goto done;
	}
	dec->devfreq = devm_devfreq_add_device(mpp->dev, &devfreq_profile,
					       "userspace", NULL);
	if (IS_ERR(dec->devfreq)) {
		ret = PTR_ERR(dec->devfreq);
		goto done;
	}

	stat = &dec->devfreq->last_status;
	stat->current_frequency = clk_get_rate(dec->aclk_info.clk);

	ret = devfreq_register_opp_notifier(mpp->dev, dec->devfreq);
	if (ret)
		goto done;

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
#else
static inline int rkvdec_devfreq_remove(struct mpp_dev *mpp)
{
	return 0;
}

static inline int rkvdec_devfreq_init(struct mpp_dev *mpp)
{
	return 0;
}
#endif

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
	dec->aux_iova = -1;
	mpp->iommu_info->hdl = rkvdec_3328_iommu_hdl;

	ret = rkvdec_devfreq_init(mpp);
done:
	return ret;
}

static int rkvdec_3328_exit(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	if (dec->aux_page)
		__free_page(dec->aux_page);

	if (dec->aux_iova != -1) {
		iommu_unmap(mpp->iommu_info->domain, dec->aux_iova, IOMMU_PAGE_SIZE);
		dec->aux_iova = -1;
	}
	rkvdec_devfreq_remove(mpp);

	return 0;
}

static int rkvdec_clk_on(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	mpp_clk_safe_enable(dec->aclk_info.clk);
	mpp_clk_safe_enable(dec->hclk_info.clk);
	mpp_clk_safe_enable(dec->core_clk_info.clk);
	mpp_clk_safe_enable(dec->cabac_clk_info.clk);
	mpp_clk_safe_enable(dec->hevc_cabac_clk_info.clk);

	return 0;
}

static int rkvdec_clk_off(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	clk_disable_unprepare(dec->aclk_info.clk);
	clk_disable_unprepare(dec->hclk_info.clk);
	clk_disable_unprepare(dec->core_clk_info.clk);
	clk_disable_unprepare(dec->cabac_clk_info.clk);
	clk_disable_unprepare(dec->hevc_cabac_clk_info.clk);

	return 0;
}

static int rkvdec_get_freq(struct mpp_dev *mpp,
			   struct mpp_task *mpp_task)
{
	u32 task_cnt;
	u32 workload;
	struct mpp_task *loop = NULL, *n;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);
	struct rkvdec_task *task = to_rkvdec_task(mpp_task);

	/* if not set max load, consider not have advanced mode */
	if (!dec->default_max_load || !task->pixels)
		return 0;

	task_cnt = 1;
	workload = task->pixels;
	/* calc workload in pending list */
	mutex_lock(&mpp->queue->pending_lock);
	list_for_each_entry_safe(loop, n,
				 &mpp->queue->pending_list,
				 queue_link) {
		struct rkvdec_task *loop_task = to_rkvdec_task(loop);

		task_cnt++;
		workload += loop_task->pixels;
	}
	mutex_unlock(&mpp->queue->pending_lock);

	if (workload > dec->default_max_load)
		task->clk_mode = CLK_MODE_ADVANCED;

	mpp_debug(DEBUG_TASK_INFO, "pending task %d, workload %d, clk_mode=%d\n",
		  task_cnt, workload, task->clk_mode);

	return 0;
}

static int rkvdec_3328_get_freq(struct mpp_dev *mpp,
				struct mpp_task *mpp_task)
{
	u32 fmt;
	u32 ddr_align_en;
	struct rkvdec_task *task =  to_rkvdec_task(mpp_task);

	fmt = RKVDEC_GET_FORMAT(task->reg[RKVDEC_REG_SYS_CTRL_INDEX]);
	ddr_align_en = task->reg[RKVDEC_REG_INT_EN_INDEX] & RKVDEC_WR_DDR_ALIGN_EN;
	if (fmt == RKVDEC_FMT_H264D && ddr_align_en)
		task->clk_mode = CLK_MODE_ADVANCED;
	else
		rkvdec_get_freq(mpp, mpp_task);

	return 0;
}

static int rkvdec_3368_set_grf(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	dec->grf_changed = mpp_grf_is_changed(mpp->grf_info);
	mpp_set_grf(mpp->grf_info);

	return 0;
}

static int rkvdec_3036_set_grf(struct mpp_dev *mpp)
{
	int grf_changed;
	struct mpp_dev *loop = NULL, *n;
	struct mpp_taskqueue *queue = mpp->queue;
	bool pd_is_on;

	grf_changed = mpp_grf_is_changed(mpp->grf_info);
	if (grf_changed) {

		/*
		 * in this case, devices share the queue also share the same pd&clk,
		 * so use mpp->dev's pd to control all the process is okay
		 */
		pd_is_on = rockchip_pmu_pd_is_on(mpp->dev);
		if (!pd_is_on)
			rockchip_pmu_pd_on(mpp->dev);
		mpp->hw_ops->clk_on(mpp);

		list_for_each_entry_safe(loop, n, &queue->dev_list, queue_link) {
			if (test_bit(loop->var->device_type, &queue->dev_active_flags)) {
				mpp_set_grf(loop->grf_info);
				if (loop->hw_ops->clk_on)
					loop->hw_ops->clk_on(loop);
				if (loop->hw_ops->reset)
					loop->hw_ops->reset(loop);
				rockchip_iommu_disable(loop->dev);
				if (loop->hw_ops->clk_off)
					loop->hw_ops->clk_off(loop);
				clear_bit(loop->var->device_type, &queue->dev_active_flags);
			}
		}

		mpp_set_grf(mpp->grf_info);
		rockchip_iommu_enable(mpp->dev);
		set_bit(mpp->var->device_type, &queue->dev_active_flags);

		mpp->hw_ops->clk_off(mpp);
		if (!pd_is_on)
			rockchip_pmu_pd_off(mpp->dev);
	}


	return 0;
}

static int rkvdec_set_freq(struct mpp_dev *mpp,
			   struct mpp_task *mpp_task)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);
	struct rkvdec_task *task =  to_rkvdec_task(mpp_task);

	mpp_clk_set_rate(&dec->aclk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->core_clk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->cabac_clk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->hevc_cabac_clk_info, task->clk_mode);

	return 0;
}

static int rkvdec_3368_set_freq(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);
	struct rkvdec_task *task =  to_rkvdec_task(mpp_task);

	/* if grf changed, need reset iommu for rk3368 */
	if (dec->grf_changed) {
		mpp_iommu_refresh(mpp->iommu_info, mpp->dev);
		dec->grf_changed = false;
	}

	mpp_clk_set_rate(&dec->aclk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->core_clk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->cabac_clk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->hevc_cabac_clk_info, task->clk_mode);

	return 0;
}

static int rkvdec_3328_set_freq(struct mpp_dev *mpp,
				struct mpp_task *mpp_task)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);
	struct rkvdec_task *task =  to_rkvdec_task(mpp_task);

#ifdef CONFIG_PM_DEVFREQ
	if (dec->devfreq) {
		struct devfreq_dev_status *stat;
		unsigned long aclk_rate_hz, core_rate_hz, cabac_rate_hz;

		stat = &dec->devfreq->last_status;
		stat->busy_time = 1;
		stat->total_time = 1;
		aclk_rate_hz = mpp_get_clk_info_rate_hz(&dec->aclk_info,
							task->clk_mode);
		core_rate_hz = mpp_get_clk_info_rate_hz(&dec->core_clk_info,
							task->clk_mode);
		cabac_rate_hz = mpp_get_clk_info_rate_hz(&dec->cabac_clk_info,
							 task->clk_mode);
		rkvdec_devf_set_clk(dec, aclk_rate_hz,
				    core_rate_hz, cabac_rate_hz,
				    EVENT_ADJUST);
	}
#else
	mpp_clk_set_rate(&dec->aclk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->core_clk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->cabac_clk_info, task->clk_mode);
#endif

	return 0;
}

static int rkvdec_reduce_freq(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	mpp_clk_set_rate(&dec->aclk_info, CLK_MODE_REDUCE);
	mpp_clk_set_rate(&dec->core_clk_info, CLK_MODE_REDUCE);
	mpp_clk_set_rate(&dec->cabac_clk_info, CLK_MODE_REDUCE);
	mpp_clk_set_rate(&dec->hevc_cabac_clk_info, CLK_MODE_REDUCE);

	return 0;
}

static int rkvdec_3328_reduce_freq(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

#ifdef CONFIG_PM_DEVFREQ
	if (dec->devfreq) {
		struct devfreq_dev_status *stat;
		unsigned long aclk_rate_hz, core_rate_hz, cabac_rate_hz;

		stat = &dec->devfreq->last_status;
		stat->busy_time = 0;
		stat->total_time = 1;
		aclk_rate_hz = mpp_get_clk_info_rate_hz(&dec->aclk_info,
							CLK_MODE_REDUCE);
		core_rate_hz = mpp_get_clk_info_rate_hz(&dec->core_clk_info,
							CLK_MODE_REDUCE);
		cabac_rate_hz = mpp_get_clk_info_rate_hz(&dec->cabac_clk_info,
							 CLK_MODE_REDUCE);
		rkvdec_devf_set_clk(dec, aclk_rate_hz,
				    core_rate_hz, cabac_rate_hz,
				    EVENT_ADJUST);
	}
#else
	mpp_clk_set_rate(&dec->aclk_info, CLK_MODE_REDUCE);
	mpp_clk_set_rate(&dec->core_clk_info, CLK_MODE_REDUCE);
	mpp_clk_set_rate(&dec->cabac_clk_info, CLK_MODE_REDUCE);
#endif

	return 0;
}

static int rkvdec_reset(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	mpp_debug_enter();
	if (dec->rst_a && dec->rst_h) {
		mpp_pmu_idle_request(mpp, true);
		mpp_safe_reset(dec->rst_niu_a);
		mpp_safe_reset(dec->rst_niu_h);
		mpp_safe_reset(dec->rst_a);
		mpp_safe_reset(dec->rst_h);
		mpp_safe_reset(dec->rst_core);
		mpp_safe_reset(dec->rst_cabac);
		mpp_safe_reset(dec->rst_hevc_cabac);
		udelay(5);
		mpp_safe_unreset(dec->rst_niu_h);
		mpp_safe_unreset(dec->rst_niu_a);
		mpp_safe_unreset(dec->rst_a);
		mpp_safe_unreset(dec->rst_h);
		mpp_safe_unreset(dec->rst_core);
		mpp_safe_unreset(dec->rst_cabac);
		mpp_safe_unreset(dec->rst_hevc_cabac);
		mpp_pmu_idle_request(mpp, false);
	}
	mpp_debug_leave();

	return 0;
}

static int rkvdec_sip_reset(struct mpp_dev *mpp)
{
	if (IS_REACHABLE(CONFIG_ROCKCHIP_SIP)) {
		/* The reset flow in arm trustzone firmware */
		struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

		mutex_lock(&dec->sip_reset_lock);
		sip_smc_vpu_reset(0, 0, 0);
		mutex_unlock(&dec->sip_reset_lock);

		return 0;
	} else {
		return rkvdec_reset(mpp);
	}
}

static struct mpp_hw_ops rkvdec_v1_hw_ops = {
	.init = rkvdec_init,
	.clk_on = rkvdec_clk_on,
	.clk_off = rkvdec_clk_off,
	.get_freq = rkvdec_get_freq,
	.set_freq = rkvdec_set_freq,
	.reduce_freq = rkvdec_reduce_freq,
	.reset = rkvdec_reset,
};

static struct mpp_hw_ops rkvdec_px30_hw_ops = {
	.init = rkvdec_px30_init,
	.clk_on = rkvdec_clk_on,
	.clk_off = rkvdec_clk_off,
	.get_freq = rkvdec_get_freq,
	.set_freq = rkvdec_set_freq,
	.reduce_freq = rkvdec_reduce_freq,
	.reset = rkvdec_reset,
	.set_grf = px30_workaround_combo_switch_grf,
};

static struct mpp_hw_ops rkvdec_3036_hw_ops = {
	.init = rkvdec_3036_init,
	.clk_on = rkvdec_clk_on,
	.clk_off = rkvdec_clk_off,
	.get_freq = rkvdec_get_freq,
	.set_freq = rkvdec_set_freq,
	.reduce_freq = rkvdec_reduce_freq,
	.reset = rkvdec_reset,
	.set_grf = rkvdec_3036_set_grf,
};

static struct mpp_hw_ops rkvdec_3399_hw_ops = {
	.init = rkvdec_init,
	.clk_on = rkvdec_clk_on,
	.clk_off = rkvdec_clk_off,
	.get_freq = rkvdec_get_freq,
	.set_freq = rkvdec_set_freq,
	.reduce_freq = rkvdec_reduce_freq,
	.reset = rkvdec_reset,
};

static struct mpp_hw_ops rkvdec_3368_hw_ops = {
	.init = rkvdec_init,
	.clk_on = rkvdec_clk_on,
	.clk_off = rkvdec_clk_off,
	.get_freq = rkvdec_get_freq,
	.set_freq = rkvdec_3368_set_freq,
	.reduce_freq = rkvdec_reduce_freq,
	.reset = rkvdec_reset,
	.set_grf = rkvdec_3368_set_grf,
};

static struct mpp_dev_ops rkvdec_v1_dev_ops = {
	.alloc_task = rkvdec_alloc_task,
	.run = rkvdec_run,
	.irq = rkvdec_irq,
	.isr = rkvdec_isr,
	.finish = rkvdec_finish,
	.result = rkvdec_result,
	.free_task = rkvdec_free_task,
};

static struct mpp_dev_ops rkvdec_px30_dev_ops = {
	.alloc_task = rkvdec_alloc_task,
	.run = rkvdec_px30_run,
	.irq = rkvdec_irq,
	.isr = rkvdec_isr,
	.finish = rkvdec_finish,
	.result = rkvdec_result,
	.free_task = rkvdec_free_task,
};

static struct mpp_hw_ops rkvdec_3328_hw_ops = {
	.init = rkvdec_3328_init,
	.exit = rkvdec_3328_exit,
	.clk_on = rkvdec_clk_on,
	.clk_off = rkvdec_clk_off,
	.get_freq = rkvdec_3328_get_freq,
	.set_freq = rkvdec_3328_set_freq,
	.reduce_freq = rkvdec_3328_reduce_freq,
	.reset = rkvdec_sip_reset,
};

static struct mpp_dev_ops rkvdec_3328_dev_ops = {
	.alloc_task = rkvdec_alloc_task,
	.run = rkvdec_3328_run,
	.irq = rkvdec_irq,
	.isr = rkvdec_3328_isr,
	.finish = rkvdec_finish,
	.result = rkvdec_result,
	.free_task = rkvdec_free_task,
};

static struct mpp_dev_ops rkvdec_3399_dev_ops = {
	.alloc_task = rkvdec_alloc_task,
	.prepare = rkvdec_prepare_with_reset,
	.run = rkvdec_run,
	.irq = rkvdec_irq,
	.isr = rkvdec_isr,
	.finish = rkvdec_finish_with_record_info,
	.result = rkvdec_result,
	.free_task = rkvdec_free_task,
};

static struct mpp_dev_ops rkvdec_1126_dev_ops = {
	.alloc_task = rkvdec_alloc_task,
	.run = rkvdec_1126_run,
	.irq = rkvdec_irq,
	.isr = rkvdec_isr,
	.finish = rkvdec_finish,
	.result = rkvdec_result,
	.free_task = rkvdec_free_task,
};
static const struct mpp_dev_var rk_hevcdec_data = {
	.device_type = MPP_DEVICE_HEVC_DEC,
	.hw_info = &rk_hevcdec_hw_info,
	.trans_info = rk_hevcdec_trans,
	.hw_ops = &rkvdec_v1_hw_ops,
	.dev_ops = &rkvdec_v1_dev_ops,
};

static const struct mpp_dev_var rk_hevcdec_3036_data = {
	.device_type = MPP_DEVICE_HEVC_DEC,
	.hw_info = &rk_hevcdec_hw_info,
	.trans_info = rk_hevcdec_trans,
	.hw_ops = &rkvdec_3036_hw_ops,
	.dev_ops = &rkvdec_v1_dev_ops,
};

static const struct mpp_dev_var rk_hevcdec_3368_data = {
	.device_type = MPP_DEVICE_HEVC_DEC,
	.hw_info = &rk_hevcdec_hw_info,
	.trans_info = rk_hevcdec_trans,
	.hw_ops = &rkvdec_3368_hw_ops,
	.dev_ops = &rkvdec_v1_dev_ops,
};

static const struct mpp_dev_var rk_hevcdec_px30_data = {
	.device_type = MPP_DEVICE_HEVC_DEC,
	.hw_info = &rk_hevcdec_hw_info,
	.trans_info = rk_hevcdec_trans,
	.hw_ops = &rkvdec_px30_hw_ops,
	.dev_ops = &rkvdec_px30_dev_ops,
};

static const struct mpp_dev_var rkvdec_v1_data = {
	.device_type = MPP_DEVICE_RKVDEC,
	.hw_info = &rkvdec_v1_hw_info,
	.trans_info = rkvdec_v1_trans,
	.hw_ops = &rkvdec_v1_hw_ops,
	.dev_ops = &rkvdec_v1_dev_ops,
};

static const struct mpp_dev_var rkvdec_3399_data = {
	.device_type = MPP_DEVICE_RKVDEC,
	.hw_info = &rkvdec_v1_hw_info,
	.trans_info = rkvdec_v1_trans,
	.hw_ops = &rkvdec_3399_hw_ops,
	.dev_ops = &rkvdec_3399_dev_ops,
};

static const struct mpp_dev_var rkvdec_3328_data = {
	.device_type = MPP_DEVICE_RKVDEC,
	.hw_info = &rkvdec_v1_hw_info,
	.trans_info = rkvdec_v1_trans,
	.hw_ops = &rkvdec_3328_hw_ops,
	.dev_ops = &rkvdec_3328_dev_ops,
};

static const struct mpp_dev_var rkvdec_1126_data = {
	.device_type = MPP_DEVICE_RKVDEC,
	.hw_info = &rkvdec_v1_hw_info,
	.trans_info = rkvdec_v1_trans,
	.hw_ops = &rkvdec_v1_hw_ops,
	.dev_ops = &rkvdec_1126_dev_ops,
};

static const struct of_device_id mpp_rkvdec_dt_match[] = {
	{
		.compatible = "rockchip,hevc-decoder",
		.data = &rk_hevcdec_data,
	},
#ifdef CONFIG_CPU_PX30
	{
		.compatible = "rockchip,hevc-decoder-px30",
		.data = &rk_hevcdec_px30_data,
	},
#endif
#ifdef CONFIG_CPU_RK3036
	{
		.compatible = "rockchip,hevc-decoder-rk3036",
		.data = &rk_hevcdec_3036_data,
	},
#endif
#ifdef CONFIG_CPU_RK3368
	{
		.compatible = "rockchip,hevc-decoder-rk3368",
		.data = &rk_hevcdec_3368_data,
	},
#endif
	{
		.compatible = "rockchip,rkv-decoder-v1",
		.data = &rkvdec_v1_data,
	},
#ifdef CONFIG_CPU_RK3399
	{
		.compatible = "rockchip,rkv-decoder-rk3399",
		.data = &rkvdec_3399_data,
	},
#endif
#ifdef CONFIG_CPU_RK3328
	{
		.compatible = "rockchip,rkv-decoder-rk3328",
		.data = &rkvdec_3328_data,
	},
#endif
#ifdef CONFIG_CPU_RV1126
	{
		.compatible = "rockchip,rkv-decoder-rv1126",
		.data = &rkvdec_1126_data,
	},
#endif
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
	platform_set_drvdata(pdev, mpp);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_rkvdec_dt_match,
				      pdev->dev.of_node);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;
	}

	ret = mpp_dev_probe(mpp, pdev);
	if (ret) {
		dev_err(dev, "probe sub driver failed\n");
		return ret;
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

	mpp->session_max_buffers = RKVDEC_SESSION_MAX_BUFFERS;
	rkvdec_procfs_init(mpp);
	/* register current device to mpp service */
	mpp_dev_register_srv(mpp, mpp->srv);
	dev_info(dev, "probing finish\n");

	return 0;
}

static int rkvdec_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rkvdec_dev *dec = platform_get_drvdata(pdev);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(&dec->mpp);
	rkvdec_procfs_remove(&dec->mpp);

	return 0;
}

struct platform_driver rockchip_rkvdec_driver = {
	.probe = rkvdec_probe,
	.remove = rkvdec_remove,
	.shutdown = mpp_dev_shutdown,
	.driver = {
		.name = RKVDEC_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_rkvdec_dt_match),
	},
};
EXPORT_SYMBOL(rockchip_rkvdec_driver);
