// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#define pr_fmt(fmt) "%s " fmt, KBUILD_MODNAME

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/ipc_logging.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#include <soc/qcom/crm.h>

#define CREATE_TRACE_POINTS
#include "trace-crm.h"

#define CRM_DRV_IPC_LOG_SIZE		2
#define MAX_NAME_LENGTH			20

#define PERF_OL_VCD			0
#define BW_VOTE_VCD			1
#define MAX_VCD_TYPE			2

#define VPAGE_SHIFT_BITS		0xFFF

/* Common CRM Registers */

#define CRM_VERSION			0
/* Offsets for CRM_VERSION Register */
#define MAJOR_VER_MASK			0xFF
#define MAJOR_VER_SHIFT			16
#define MINOR_VER_MASK			0xFF
#define MINOR_VER_SHIFT			8

#define CRM_CFG_PARAM_1			0x4
/* Offsets for CRM_CFG_PARAM_1 Register */
#define NUM_SW_DRVS_MASK		0xF
#define NUM_SW_DRVS_SHIFT		20
#define NUM_HW_DRVS_MASK		0xF
#define NUM_HW_DRVS_SHIFT		16
#define NUM_VCD_VOTED_BY_BW_MASK	0xF
#define NUM_VCD_VOTED_BY_BW_SHIFT	24
#define NUM_VCD_VOTED_BY_PERF_OL_MASK	0xF
#define NUM_VCD_VOTED_BY_PERF_OL_SHIFT	8
#define NUM_CH_MASK			0xF
#define NUM_CH_SHIFT			4
#define NUM_PWR_STATES_PER_CH_MASK	0xF
#define NUM_PWR_STATES_PER_CH_SHIFT	0

#define CRM_CFG_PARAM_2			0x8
/* Offsets for CRM_CFG_PARAM_2 Register */
#define NUM_OF_NODES_MASK		0x1F
#define NUM_OF_NODES_SHIFT		26

#define CRM_ENABLE			0xC

/* Applicable for HW & SW DRVs BW Registers */
#define PERF_OL_VALUE_BITS		0x7

/* Applicable for HW & SW DRVs BW Registers */
#define BW_VOTE_VALID			BIT(29)
/* Applicable only for SW DRVs BW Registers */
#define BW_VOTE_COMMIT			BIT(30)
/* Applicable only for SW DRVs BW Registers */
#define BW_VOTE_RESP_REQ		BIT(31)

/* Set 1 to Enable IRQ for each VCD */
#define IRQ_ENABLE_BIT			BIT(0)
#define IRQ_CLEAR_BIT			BIT(0)

/* Set 1 to Enable CHN_BEHAVE for each HW DRV */
#define CHN_BEHAVE_BIT			BIT(0)

/* SW DRV has ACTIVE, SLEEP and WAKE PWR STATES */
#define MAX_SW_DRV_PWR_STATES		3

/* Time out for ACTIVE Only PWR STATE completion IRQ */
#define CRM_TIMEOUT_MS			5000

#define CH0				0
#define CH0_CHN_BUSY			BIT(0)
#define CH1				1
#define CH1_CHN_BUSY			BIT(1)

#define crm_print_reg(addr, val)\
			pr_debug("addr:0x%x, val:0x%x\n", addr, val)
#define crm_print_hw_reg(drv_num, channel, res_type, res_num, pwr_st, addr, val)\
			pr_debug("drv:%d, chn:%d, %s:%d, pwr_st:%d, addr:0x%x, val:0x%x\n",\
						drv_num, channel, res_type == PERF_OL_VCD ?\
						"vcd" : "node", res_num, pwr_st, addr, val)

enum {
/* CRM DRV Register */
	DRV_BASE,
	DRV_DISTANCE,
/* VCD or ND Distance */
	DRV_RESOURCE_DISTANCE,
/* DRV's PWR_ST Registers */
	PWR_ST0,
	PWR_ST1,
	PWR_ST2,
	PWR_ST3,
	PWR_ST4,
/* Offset for power state distances in a channel */
	PWR_ST_CHN_DISTANCE,
/* VCD's IRQ Registers, one per VCD at VCD_DISTANCE */
	IRQ_STATUS,
	IRQ_CLEAR,
	IRQ_ENABLE,
/* DRV's Channel Registers, one per DRV at CH_DRV_DISTANCE */
	CHN_BUSY,
	CHN_UPDATE,
	CHN_BEHAVE,
	CHN_DRV_DISTANCE,
/* SW DRV's PWR_ST mapped to PWR_ST0/1/2 for ACTIVE/SLEEP/WAKE */
	ACTIVE_VOTE = PWR_ST0,
	SLEEP_VOTE = PWR_ST1,
	WAKE_VOTE = PWR_ST2,
/* Status Registers */
	PERF_OL_STATUS = IRQ_STATUS,
	PWR_IDX_STATUS = IRQ_CLEAR,
};

enum channel_type {
	CHN_IN_USE,
	CHN_FREE,
};

enum {
/* CRM Register */
	CRM_BASE,
	CRM_DISTANCE,
/* CRMB Registers */
	STATUS_BE,
	STATUS_FE,
/* CRMC Registers */
	AGGR_PERF_OL,
	CURR_PERF_OL,
	SEQ_STATUS,
};

static u32 chn_regs[] = {
	[CHN_BUSY]			= 0x1000,
	[CHN_UPDATE]			= 0x1020,
	[CHN_BEHAVE]			= 0x1040,
	[CHN_DRV_DISTANCE]		= 0x4,
};

static u32 crmb_regs[] = {
	[CRM_BASE]			= 0x0,
	[CRM_DISTANCE]		= 0x400,
	[STATUS_BE]			= 0x120,
	[STATUS_FE]			= 0x124,
};

static u32 crmc_regs[] = {
	[CRM_BASE]			= 0x0,
	[CRM_DISTANCE]		= 0x200,
	[AGGR_PERF_OL]		= 0x4,
	[CURR_PERF_OL]		= 0xc,
	[SEQ_STATUS]		= 0x1a0,
};

static u32 hw_drv_perf_ol_vcd_regs[] = {
	[DRV_BASE]			= 0x200,
	[DRV_DISTANCE]			= 0x200,
	[DRV_RESOURCE_DISTANCE]		= 0x4,
	[PWR_ST0]			= 0x0,
	[PWR_ST1]			= 0x40,
	[PWR_ST2]			= 0x80,
	[PWR_ST3]			= 0x600,
	[PWR_ST4]			= 0x640,
	[PWR_ST_CHN_DISTANCE]		= 0x4,
	[PERF_OL_STATUS]		= 0x1C0,
};

static u32 hw_drv_bw_vote_vcd_regs[] = {
	[DRV_BASE]			= 0x200,
	[DRV_DISTANCE]			= 0x200,
	[DRV_RESOURCE_DISTANCE]		= 0x4,
	[PWR_ST0]			= 0xC0,
	[PWR_ST1]			= 0x140,
	[PWR_ST2]			= 0x680,
	[PWR_ST3]			= 0x700,
	[PWR_ST4]			= 0x780,
	[PWR_ST_CHN_DISTANCE]		= 0x4,
	[PWR_IDX_STATUS]		= 0x1F0,
};

static u32 sw_drv_perf_ol_vcd_regs[] = {
	[DRV_BASE]			= 0x1060,
	[DRV_DISTANCE]			= 0x1000,
	[DRV_RESOURCE_DISTANCE]		= 0x4,
	[PWR_ST0]			= 0x0,
	[PWR_ST1]			= 0x40,
	[PWR_ST2]			= 0x80,
	[PWR_ST_CHN_DISTANCE]		= 0x0,
	[IRQ_STATUS]			= 0x100,
	[IRQ_CLEAR]			= 0x140,
	[IRQ_ENABLE]			= 0x180,
};

static u32 sw_drv_bw_vote_vcd_regs[] = {
	[DRV_BASE]			= 0x1060,
	[DRV_DISTANCE]			= 0x1000,
	[DRV_RESOURCE_DISTANCE]		= 0x4,
	[PWR_ST0]			= 0x3A0,
	[PWR_ST1]			= 0x3E0,
	[PWR_ST2]			= 0x420,
	[PWR_ST_CHN_DISTANCE]		= 0x0,
	[IRQ_STATUS]			= 0x4A0,
	[IRQ_CLEAR]			= 0x4E0,
	[IRQ_ENABLE]			= 0x520,
};

struct crm_desc {
	bool set_chn_behave;
};

/**
 * struct crm_sw_votes: SW DRV's ACTIVE_VOTEs in progress.
 * One per VCD.
 *
 * @cmd:                The ACTIVE_VOTE being sent to CRM.
 * @compl:              Wait for completion if the cmd->wait is set.
 *                      Applicable only for ACTIVE_VOTEs.
 * @in_progress:        Indicates if the cmd is in flight.
 * @wait:               Wait queue used to wait for @in_progress to be false.
 *                      This is needed because HW do not keep a record of new
 *                      requests issued until current one is completed.
 */
struct crm_sw_votes {
	struct crm_cmd cmd;
	struct completion compl;
	bool in_progress;
	wait_queue_head_t wait;
};

/**
 * struct crm_vcd: The Virtual Clock Domain's (VCDs) of the CRM.
 * One per VCD type.
 *
 * @cache:              Cache of vcd's power_state to data
 * @num_pwr_states:     Number of pwr state that DRV VCD can vote for.
 * @num_resources:      Number of VCD resources (for PERF_OL votes) OR
 *                      Number of Node resoureces (for BW votes)
 * @cache_dirty:        Flag to indicate if all the votes are applied.
 * @offsets:            Register offsets for DRV controller.
 * @sw_votes:           Cache of SW DRV's ACTIVE_VOTEs.
 */
struct crm_vcd {
	u32 **cache;
	u32 num_pwr_states;
	u32 num_resources;
	u32 *offsets;
	bool cache_dirty;
	struct crm_sw_votes *sw_votes;
};

/**
 * struct crm_drv: The Direct Resource Voter (DRV) of the
 * CESTA Resource manager (CRM).
 *
 * @name:               Controller identifier.
 * @base:               Base address of the CRM device.
 * @drv_id:             DRV (Direct Resource Voter) number.
 * @num_channels:       Number of Channels, Applicable only for HW DRV
 * @vcd:                VCDs in this DRV.
 * @irq:                IRQ at gic.
 * @initialized:        Whether DRV is initialized
 * @lock:               Synchronize state of the controller.  If CRM's cache's
 *                      lock will also be held, the order is: drv->cache_lock
 *                      then drv->lock.
 * @cache_lock:         Synchronize VCD cache updates
 * @client:             Handle to the DRV's client.
 * @ipc_log_ctx:        IPC logger handle
 */
struct crm_drv {
	enum crm_drv_type drv_type;
	char name[MAX_NAME_LENGTH];
	void __iomem *base;
	u32 drv_id;
	u32 num_channels;
	u32 *offsets;
	struct crm_vcd vcd[MAX_VCD_TYPE];
	spinlock_t lock;
	spinlock_t cache_lock;
	int irq;
	bool initialized;
	void *ipc_log_ctx;
};

/**
 * struct crm_mgr: The CRM HW block used for aggregating votes
 * from SW and HW DRVs.
 *
 * @name:                   CRM HW block name.
 * @base:                   Base address of the CRM block.
 * @num_resources:          Number of PERF_OL or BW_VOTE resources.
 * @offsets:                Register offsets for CRM manager.
 */
struct crm_mgr {
	char name[MAX_NAME_LENGTH];
	void __iomem *base;
	u32 num_resources;
	u32 *offsets;
};

/**
 * struct crm_drv_top: Our representation of the top CRM device.
 *
 * @name:               CRM device name.
 * @base:               Base address of the CRM device.
 * @hw_drvs:            Controller for each HW DRV
 * @num_hw_drvs:        Number of HW DRV controllers in the CRM device
 * @num_channels:       Number of Channels, Applicable only for HW DRV
 * @sw_drvs:            Controller for each SW DRV
 * @num_sw_drvs:        Number of SW DRV controllers in the CRM device
 * @crmb_mgr:           Controller for CRMB device.
 * @crmc_mgr:           Controller for CRMC device.
 * @list:               CRM device added in crm_dev_list.
 * @desc:               CRM description
 * @dev:                CRM dev
 * @pdev:               CRM platform device
 */
struct crm_drv_top {
	char name[MAX_NAME_LENGTH];
	void __iomem *base;
	struct crm_drv *hw_drvs;
	int num_hw_drvs;
	u32 num_channels;
	struct crm_drv *sw_drvs;
	int num_sw_drvs;
	struct crm_mgr crmb_mgr;
	struct crm_mgr crmc_mgr;
	struct list_head list;
	const struct crm_desc *desc;
	struct device *dev;
	struct platform_device *pdev;
};

static LIST_HEAD(crm_dev_list);

static inline u32 get_crm_phy_addr(void __iomem *base)
{
	return page_to_phys(vmalloc_to_page(base));
}

static inline u32 crm_get_channel_offset(const struct crm_drv *drv, u32 reg)
{
	return drv->offsets[reg] + drv->drv_id * drv->offsets[CHN_DRV_DISTANCE];
}

static void write_crm_channel(const struct crm_drv *drv, u32 reg, u32 data)
{
	u32 offset = crm_get_channel_offset(drv, reg);

	writel_relaxed(data, drv->base + offset);
}

static u32 read_crm_channel(const struct crm_drv *drv, u32 reg)
{
	u32 offset = crm_get_channel_offset(drv, reg);

	return readl_relaxed(drv->base + offset);
}

static inline u32 crm_get_offset(const struct crm_drv *drv, u32 reg, u32 ch, u32 vcd_type,
			  u32 resource_idx)
{
	const struct crm_vcd *vcd = &drv->vcd[vcd_type];
	u32 offset;

	offset = vcd->offsets[DRV_BASE] + drv->drv_id * vcd->offsets[DRV_DISTANCE];
	offset += vcd->offsets[reg];
	offset += ch * vcd->num_resources * vcd->offsets[PWR_ST_CHN_DISTANCE];
	offset += resource_idx * vcd->offsets[DRV_RESOURCE_DISTANCE];

	return offset;
}

static void write_crm_reg(const struct crm_drv *drv, u32 reg, u32 ch, u32 vcd_type,
			  u32 resource_idx, u32 data)
{
	u32 offset = crm_get_offset(drv, reg, ch, vcd_type, resource_idx);

	writel_relaxed(data, drv->base + offset);
}

static u32 read_crm_reg(const struct crm_drv *drv, u32 reg, u32 ch, u32 vcd_type,
			u32 resource_idx)
{
	u32 offset = crm_get_offset(drv, reg, ch, vcd_type, resource_idx);

	return readl_relaxed(drv->base + offset);
}

static inline u32 crm_mgr_get_offset(const struct crm_mgr *mgr, u32 reg, u32 resource_idx)
{
	u32 offset;

	offset = mgr->offsets[CRM_BASE] + resource_idx * mgr->offsets[CRM_DISTANCE];
	offset += mgr->offsets[reg];

	return offset;
}

static u32 read_crm_mgr_reg(const struct crm_mgr *mgr, u32 reg, u32 resource_idx)
{
	u32 offset = crm_mgr_get_offset(mgr, reg, resource_idx);

	return readl_relaxed(mgr->base + offset);
}

static struct crm_drv *get_crm_drv(const struct device *dev, enum crm_drv_type drv_type,
				   u32 drv_id)
{
	struct crm_drv_top *crm;

	if (!dev)
		return NULL;

	crm = dev_get_drvdata(dev);
	if (drv_type == CRM_HW_DRV && drv_id < crm->num_hw_drvs)
		return &crm->hw_drvs[drv_id];
	else if (drv_type == CRM_SW_DRV && drv_id < crm->num_sw_drvs)
		return &crm->sw_drvs[drv_id];

	return NULL;
}

/**
 * crm_get_channel() - Get the channel to Update the data
 * @drv:       The CRM DRV controller.
 * @chn_type:  The type of channel to find.
 *
 * Return:
 * * 0			- Success
 * * -Error             - Error code
 */
static int crm_get_channel(struct crm_drv *drv, enum channel_type ch_type, u32 *ch)
{
	u32 chn_update;

	if (drv->num_channels == 0)
		return -EBUSY;

	/* Select Unused channel */
	chn_update = read_crm_channel(drv, CHN_UPDATE);
	if (!chn_update) {
		/* Start with ch0 if none are in use */
		*ch = CH0;
		return 0;
	}

	if (chn_update & CH0_CHN_BUSY)
		*ch = ch_type == CHN_FREE ? CH1 : CH0;
	else if (chn_update & CH1_CHN_BUSY)
		*ch = ch_type == CHN_FREE ? CH0 : CH1;
	else
		return -EBUSY;

	return 0;
}

int crm_channel_switch_complete(const struct crm_drv *drv, u32 ch)
{
	u32 sts;
	int retry = 50, ret = 0;

	do {
		sts = read_crm_channel(drv, CHN_BUSY);
		if (ch == 0)
			sts &= CH0_CHN_BUSY;
		else
			sts &= CH1_CHN_BUSY;

		retry--;
		/*
		 * Wait till all the votes are applied to new
		 * channel during channel switch.
		 * Maximum delay of 5 msec.
		 */
		udelay(100);
	} while ((sts != BIT(ch)) && retry);

	if (!retry)
		ret = -EBUSY;

	trace_crm_switch_channel(drv->name, ch, ret);
	ipc_log_string(drv->ipc_log_ctx, "Switch Channel: ch: %u ret: %d", ch, ret);

	return ret;
}

/**
 * crm_switch_channel() - Switch to the channel
 * @drv:     The controller DRV.
 * @ch:      The channel number to switch to.
 *
 * NOTE: Caller should ensure serialization before making this call.
 * Return:
 * * 0			- Success
 * * -Error             - Error code
 */
int crm_switch_channel(const struct crm_drv *drv, u32 ch)
{
	write_crm_channel(drv, CHN_UPDATE, BIT(ch));
	return crm_channel_switch_complete(drv, ch);
}

static u32 crm_get_pwr_state_reg(int pwr_state)
{
	u32 reg;

	switch (pwr_state) {
	case 0:
		reg = PWR_ST0;
		break;
	case 1:
		reg = PWR_ST1;
		break;
	case 2:
		reg = PWR_ST2;
		break;
	case 3:
		reg = PWR_ST3;
		break;
	case 4:
		reg = PWR_ST4;
		break;
	default:
		WARN_ON(1);
		reg = PWR_ST0;
	}

	return reg;
}

static void crm_flush_cache(struct crm_drv *drv, struct crm_vcd *vcd, u32 ch, u32 vcd_type)
{
	int i, j;
	u32 reg;

	for (i = 0; i < vcd->num_resources; i++) {
		for (j = 0; j < vcd->num_pwr_states; j++) {
			reg = crm_get_pwr_state_reg(j);
			write_crm_reg(drv, reg, ch, vcd_type, i, vcd->cache[i][j]);
			trace_crm_write_vcd_votes(drv->name, vcd_type, i, j, vcd->cache[i][j]);
			ipc_log_string(drv->ipc_log_ctx,
				       "Flush: type: %u resource_idx:%u pwr_state: %u data: %#x",
				       vcd_type, i, j, vcd->cache[i][j]);
		}
	}
}
/**
 * crm_write_pwr_states() - Flush the power state votes for HW DRVs.
 * @dev:      The CRM device
 * @drv_id:   HW DRV ID for which to flush the power state votes.
 *
 * Find the non-active channel, writes various power states that
 * were cached with crm_write_perf_ol() and crm_write_bw_vote()
 * APIs and does a channel switch.
 *
 * Applicable only for HW DRVs for which the votes are cached.
 * SW DRVs votes are immediately written.
 *
 * Return:
 * * 0			- Success
 * * -Error             - Error code
 */
int crm_write_pwr_states(const struct device *dev, u32 drv_id)
{
	struct crm_drv *drv = get_crm_drv(dev, CRM_HW_DRV, drv_id);
	struct crm_vcd *vcd;
	u32 ch;
	int i;
	int ret;

	if (!drv || drv->drv_type == CRM_SW_DRV)
		return -EINVAL;

	spin_lock(&drv->cache_lock);

	ret = crm_get_channel(drv, CHN_FREE, &ch);
	if (ret)
		goto exit;

	for (i = 0; i < MAX_VCD_TYPE; i++) {
		vcd = &drv->vcd[i];
		crm_flush_cache(drv, vcd, ch, i);
	}

	ret = crm_switch_channel(drv, ch);
	if (ret)
		goto exit;

exit:
	spin_unlock(&drv->cache_lock);

	return ret;
}
EXPORT_SYMBOL(crm_write_pwr_states);

/**
 * crm_dump_drv_regs() - Dump CRM DRV registers for debug purposes.
 * @name:      The name of the crm device to dump for.
 * @drv_id:    DRV ID for which to dump for.
 *
 * Return:
 * * 0        - Success
 * * -Error   - Error code
 */
int crm_dump_drv_regs(const char *name, u32 drv_id)
{
	struct crm_drv_top *crm;
	struct crm_drv *drv;
	struct crm_vcd *vcd;
	const struct device *dev;
	u32 chn, reg;
	u32 phy_base, data, offset;
	int m, j, k;
	int ret = 0;

	dev = crm_get_device(name);
	if (IS_ERR(dev))
		return -EINVAL;

	crm = dev_get_drvdata(dev);
	drv = get_crm_drv(dev, CRM_HW_DRV, drv_id);
	if (!drv)
		return -EINVAL;

	phy_base = get_crm_phy_addr(drv->base);
	pr_debug("HW DRV%d Regs\n", drv->drv_id);

	spin_lock(&drv->cache_lock);
	ret = crm_get_channel(drv, CHN_IN_USE, &chn);
	if (ret) {
		spin_unlock(&drv->cache_lock);
		return ret;
	}

	for (m = 0; m < MAX_VCD_TYPE; m++) {
		vcd = &drv->vcd[m];
		for (k = 0; k < vcd->num_resources; k++) {
			for (j = 0; j < vcd->num_pwr_states; j++) {
				reg = crm_get_pwr_state_reg(j);

				offset = crm_get_offset(drv, reg, chn, m, k);
				data = readl_relaxed(drv->base + offset);
				crm_print_hw_reg(drv->drv_id, chn, m, k,
						reg-PWR_ST0, phy_base + offset, data);
			}
		}
	}

	vcd = &drv->vcd[PERF_OL_VCD];
	pr_debug("DRV%d HW PERF_OL Status\n", drv->drv_id);
	for (k = 0; k < vcd->num_resources; k++) {

		offset = crm_get_offset(drv, PERF_OL_STATUS, 0, PERF_OL_VCD, k);
		data = readl_relaxed(drv->base + offset);
		crm_print_reg(phy_base + offset, data);
	}

	pr_debug("DRV%d HW BW Status\n", drv->drv_id);
	offset = crm_get_offset(drv, PWR_IDX_STATUS, 0, BW_VOTE_VCD, 0);
	data = readl_relaxed(drv->base + offset);
	crm_print_reg(phy_base + offset, data);

	offset = crm_get_channel_offset(drv, CHN_BUSY);
	data = readl_relaxed(drv->base + offset);
	crm_print_reg(phy_base + offset, data);

	spin_unlock(&drv->cache_lock);

	return ret;
}

/**
 * crm_dump_regs() - Dump CRM registers for debug purposes.
 * @name:      The name of the crm device to dump for.
 *
 * Return:
 * * 0        - Success
 * * -Error   - Error code
 */
int crm_dump_regs(const char *name)
{
	struct crm_drv_top *crm;
	const struct device *dev;
	u32 phy_base, data, offset;
	int i, ret = 0;

	dev = crm_get_device(name);
	if (IS_ERR(dev))
		return -EINVAL;

	crm = dev_get_drvdata(dev);
	pr_debug("CRMB Regs\n");
	phy_base = get_crm_phy_addr(crm->crmb_mgr.base) +
					((unsigned long) crm->crmb_mgr.base & VPAGE_SHIFT_BITS);

	for (i = 0; i < crm->crmb_mgr.num_resources; i++) {
		offset = crm_mgr_get_offset(&crm->crmb_mgr, STATUS_BE, i);
		data = readl_relaxed(crm->crmb_mgr.base + offset);
		crm_print_reg(phy_base + offset, data);

		offset = crm_mgr_get_offset(&crm->crmb_mgr, STATUS_FE, i);
		data = readl_relaxed(crm->crmb_mgr.base + offset);
		crm_print_reg(phy_base + offset, data);
	}

	pr_debug("CRMC Regs\n");
	phy_base = get_crm_phy_addr(crm->crmc_mgr.base) +
					((unsigned long) crm->crmc_mgr.base & VPAGE_SHIFT_BITS);
	for (i = 0; i < crm->crmc_mgr.num_resources; i++) {
		offset = crm_mgr_get_offset(&crm->crmc_mgr, AGGR_PERF_OL, i);
		data = readl_relaxed(crm->crmc_mgr.base + offset);
		crm_print_reg(phy_base + offset, data);
	}

	for (i = 0; i < (crm->crmc_mgr.num_resources + crm->crmb_mgr.num_resources); i++) {
		offset = crm_mgr_get_offset(&crm->crmc_mgr, CURR_PERF_OL, i);
		data = readl_relaxed(crm->crmc_mgr.base + offset);
		crm_print_reg(phy_base + offset, data);

		offset = crm_mgr_get_offset(&crm->crmc_mgr, SEQ_STATUS, i);
		data = readl_relaxed(crm->crmc_mgr.base + offset);
		crm_print_reg(phy_base + offset, data);
	}

	return ret;
}
EXPORT_SYMBOL(crm_dump_regs);

/**
 * crm_dump_regs() - Dump CRM registers for debug purposes.
 * @name:      The name of the crm device to dump for.
 * @vcd_idx:   The VCD index to read from.
 * @data:      Read CURR_PERF_OL register value into this.
 *
 * Return:
 * * 0        - Success
 * * -Error   - Error code
 */
int crm_read_curr_perf_ol(const char *name, int vcd_idx, u32 *data)
{
	struct crm_drv_top *crm;
	const struct device *dev;

	dev = crm_get_device(name);
	if (IS_ERR(dev))
		return -EINVAL;

	crm = dev_get_drvdata(dev);
	if (vcd_idx >= crm->crmc_mgr.num_resources)
		return -EINVAL;

	*data = read_crm_mgr_reg(&crm->crmc_mgr, CURR_PERF_OL, vcd_idx);

	return 0;
}
EXPORT_SYMBOL(crm_read_curr_perf_ol);

static void crm_vote_completion(struct crm_sw_votes *votes)
{
	struct completion *compl = &votes->compl;

	votes->in_progress = false;
	complete(compl);
}

/**
 * crm_vote_complete_irq() - Vote completion interrupt handler for SW DRVs.
 * @irq: The IRQ number (ignored).
 * @p:   Pointer to "struct crm_drv".
 *
 * Called for ACTIVE_VOTE transfers (those are the only ones we enable the
 * IRQ for) when a transfer is done.
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t crm_vote_complete_irq(int irq, void *p)
{
	struct crm_drv_top *crm = p;
	struct crm_drv *drv;
	struct crm_vcd *vcd;
	struct crm_sw_votes *votes;
	unsigned long irq_status;
	int i, j, k;

	for (i = 0; i < crm->num_sw_drvs; i++) {
		drv = &crm->sw_drvs[i];
		if (!drv->initialized)
			continue;

		spin_lock(&drv->lock);
		for (j = 0; j < MAX_VCD_TYPE; j++) {
			vcd = &drv->vcd[j];

			for (k = 0; k < vcd->num_resources; k++) {

				irq_status = read_crm_reg(drv, IRQ_STATUS, 0, j, k);
				if (!irq_status)
					continue;

				write_crm_reg(drv, IRQ_CLEAR, 0, j, k, IRQ_CLEAR_BIT);
				trace_crm_irq(drv->name, j, k, irq_status);
				ipc_log_string(drv->ipc_log_ctx,
					       "IRQ: type: %u resource_idx:%u irq_status: %lu"
						, j, k, irq_status);

				votes = &vcd->sw_votes[k];
				if (!votes->in_progress) {
					WARN_ON(1);
					continue;
				}

				if (votes->cmd.wait)
					crm_vote_completion(votes);
			}
		}
		spin_unlock(&drv->lock);
	}

	return IRQ_HANDLED;
}

static void crm_fill_cmd(struct crm_cmd *dest, const struct crm_cmd *src)
{
	dest->resource_idx = src->resource_idx;
	dest->pwr_state = src->pwr_state;
	dest->data = src->data;
	dest->wait = src->wait;
}

static u32 crm_get_pwr_state(struct crm_drv *drv, const struct crm_cmd *cmd)
{
	enum crm_sw_drv_state sw;
	enum crm_hw_drv_state hw;
	u32 pwr_state;

	if (drv->drv_type == CRM_HW_DRV) {
		hw = cmd->pwr_state.hw;
		pwr_state = hw;
	} else {
		sw = cmd->pwr_state.sw;
		pwr_state = sw;
	}

	return pwr_state;
}

static int crm_send_cmd(struct crm_drv *drv, u32 vcd_type, const struct crm_cmd *cmd)
{
	struct crm_vcd *vcd = &drv->vcd[vcd_type];
	u32 resource_idx = cmd->resource_idx;
	u32 pwr_state = crm_get_pwr_state(drv, cmd);
	u32 data = cmd->data;
	bool wait = cmd->wait;
	unsigned long flags;
	struct completion *compl = NULL;
	u32 time_left;

	spin_lock_irqsave(&drv->lock, flags);

	/* Set COMMIT to start aggregating votes */
	if (vcd_type == BW_VOTE_VCD) {
		data |= BW_VOTE_COMMIT;

		if (wait)
			data |= BW_VOTE_RESP_REQ;
	}

	/* Note: Set BIT(31) for RESP_REQ and BIT(30) for COMMIT */
	switch (pwr_state) {
	case CRM_ACTIVE_STATE:
		/* Wait forever for a previous request to complete */
		wait_event_lock_irq(vcd->sw_votes[resource_idx].wait,
			    !vcd->sw_votes[resource_idx].in_progress,
			    drv->lock);

		compl = &vcd->sw_votes[resource_idx].compl;
		init_completion(compl);
		crm_fill_cmd(&vcd->sw_votes[resource_idx].cmd, cmd);
		vcd->sw_votes[resource_idx].in_progress = true;
		write_crm_reg(drv, PWR_ST0, 0, vcd_type, resource_idx, data);
		break;
	case CRM_SLEEP_STATE:
		write_crm_reg(drv, PWR_ST1, 0, vcd_type, resource_idx, data);
		break;
	case CRM_WAKE_STATE:
		write_crm_reg(drv, PWR_ST2, 0, vcd_type, resource_idx, data);
		break;
	default:
		WARN_ON(1);
		break;
	}

	spin_unlock_irqrestore(&drv->lock, flags);
	trace_crm_write_vcd_votes(drv->name, vcd_type, resource_idx, pwr_state, data);
	ipc_log_string(drv->ipc_log_ctx,
		       "Write: type: %u resource_idx:%u pwr_state: %u data: %#x",
		       vcd_type, resource_idx, pwr_state, data);

	if (compl && wait) {
		time_left = CRM_TIMEOUT_MS;
		time_left = wait_for_completion_timeout(compl, time_left);
		if (!time_left) {
			WARN_ON(1);
			return -ETIMEDOUT;
		}
		/* Unblock new requests for same VCD */
		wake_up(&vcd->sw_votes[resource_idx].wait);
	}

	return 0;
}

static void crm_cache_vcd_votes(struct crm_drv *drv, u32 vcd_type, const struct crm_cmd *cmd)
{
	struct crm_vcd *vcd = &drv->vcd[vcd_type];
	u32 resource_idx = cmd->resource_idx;
	u32 pwr_state = crm_get_pwr_state(drv, cmd);
	u32 data = cmd->data;

	spin_lock(&drv->cache_lock);

	vcd->cache[resource_idx][pwr_state] = data;
	vcd->cache_dirty = true;

	spin_unlock(&drv->cache_lock);

	trace_crm_cache_vcd_votes(drv->name, vcd_type, resource_idx, pwr_state, data);
	ipc_log_string(drv->ipc_log_ctx,
		       "Cache: type: %u resource_idx:%u pwr_state: %u data: %#x",
		       vcd_type, resource_idx, pwr_state, data);

}

static bool crm_is_invalid_cmd(struct crm_drv *drv, u32 vcd_type, const struct crm_cmd *cmd)
{
	struct crm_vcd *vcd;
	u32 resource_idx;
	u32 pwr_state;
	u32 data;
	bool ret;

	if (!drv || !cmd)
		return true;

	vcd = &drv->vcd[vcd_type];
	resource_idx = cmd->resource_idx;
	pwr_state = crm_get_pwr_state(drv, cmd);
	data = cmd->data;

	if (pwr_state >= vcd->num_pwr_states)
		ret = true;
	else if (resource_idx >= vcd->num_resources)
		ret = true;
	else if (vcd_type == BW_VOTE_VCD && !(data & BW_VOTE_VALID))
		ret = true;
	else if (vcd_type == PERF_OL_VCD && (data & ~PERF_OL_VALUE_BITS))
		ret = true;
	else
		ret = false;

	return ret;
}

/**
 * crm_write_perf_ol() - Write a perf ol vote for a resource
 * @dev:       The CRM device
 * @drv_type:  The CRM DRV type, either SW or HW DRV.
 * @drv_id:    DRV ID for which the votes are sent
 * @cmd:       The CRM CMD
 *
 * Caches the votes for HW DRV and immediately returns.
 * The votes are written to unused channel with a call to
 * crm_write_pwr_states().
 *
 * Caches the votes for logging and immediately sents the votes for SW DRVs
 * if the @cmd have .wait set and is for ACTIVE_VOTE then waits for completion
 * IRQ before return. for SLEEP_VOTE and WAKE_VOTE no completion IRQ is sent
 * and they are triggered within HW during idle/awake scenarios.
 *
 * Return:
 * * 0			- Success
 * * -Error             - Error code
 */
int crm_write_perf_ol(const struct device *dev, enum crm_drv_type drv_type,
		      u32 drv_id, const struct crm_cmd *cmd)
{
	struct crm_drv *drv = get_crm_drv(dev, drv_type, drv_id);
	int ret;

	ret = crm_is_invalid_cmd(drv, PERF_OL_VCD, cmd);
	if (ret)
		return -EINVAL;

	/* Cache the votes first */
	crm_cache_vcd_votes(drv, PERF_OL_VCD, cmd);

	/* Send SW DRV votes immediately for ACTIVE/SLEEP/WAKE states */
	if (drv_type == CRM_SW_DRV)
		return crm_send_cmd(drv, PERF_OL_VCD, cmd);

	return 0;
}
EXPORT_SYMBOL(crm_write_perf_ol);

/**
 * crm_write_bw_vote() - Write a bw vote for a resource
 * @dev:       The CRM device
 * @drv_type:  The CRM DRV type, either SW or HW DRV.
 * @drv_id:    DRV ID for which the votes are sent
 * @cmd:       The CRM CMD
 *
 * Caches the votes for HW DRV and immediately returns.
 * The votes are written to unused channel with a call to
 * crm_write_pwr_states().
 *
 * Caches the votes for logging and immediately sents the votes for SW DRVs
 * if the @cmd have .wait set and is for ACTIVE_VOTE then waits for completion
 * IRQ before return. for SLEEP_VOTE and WAKE_VOTE no completion IRQ is sent
 * and they are triggered within HW during idle/awake scenarios.
 *
 * Return:
 * * 0			- Success
 * * -Error             - Error code
 */
int crm_write_bw_vote(const struct device *dev, enum crm_drv_type drv_type,
		      u32 drv_id, const struct crm_cmd *cmd)
{
	struct crm_drv *drv = get_crm_drv(dev, drv_type, drv_id);
	int ret;

	ret = crm_is_invalid_cmd(drv, BW_VOTE_VCD, cmd);
	if (ret)
		return -EINVAL;

	/* Cache the votes first */
	crm_cache_vcd_votes(drv, BW_VOTE_VCD, cmd);

	/* Send SW DRV votes immediately for ACTIVE/SLEEP/WAKE states */
	if (drv_type == CRM_SW_DRV)
		return crm_send_cmd(drv, BW_VOTE_VCD, cmd);

	return 0;
}
EXPORT_SYMBOL(crm_write_bw_vote);

/**
 * crm_get_device() - Returns a CRM device handle.
 * @name: The CRM device name for which handle is needed.
 *
 * Finds the CRM device from list of available CRM devices.
 * The @name should match the label property in device which are "cam_crm"
 * or "pcie_crm".
 *
 * Return:
 * * Device pointer	- Success
 * * -Error pointer     - Error
 */
const struct device *crm_get_device(const char *name)
{
	struct crm_drv_top *crm;

	list_for_each_entry(crm, &crm_dev_list, list) {
		if (!strcmp(name, crm->name))
			return crm->dev;
	}

	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL(crm_get_device);

static void crm_set_chn_behave(struct crm_drv_top *crm)
{
	int i;

	if (!crm->desc->set_chn_behave)
		return;

	for (i = 0; i < crm->num_hw_drvs; i++)
		write_crm_channel(&crm->hw_drvs[i], CHN_BEHAVE, CHN_BEHAVE_BIT);
}

static int crm_probe_get_irqs(struct crm_drv_top *crm)
{
	struct crm_drv *drvs = crm->sw_drvs;
	struct crm_vcd *vcd;
	int i, j, k;
	int irq;
	int ret;

	if (!crm->num_sw_drvs)
		return 0;

	irq = platform_get_irq_byname(crm->pdev, crm->name);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(crm->dev, irq, crm_vote_complete_irq,
			       IRQF_TRIGGER_RISING, crm->name, crm);
	if (ret)
		return ret;

	/* Only SW DRVs have associated vote completion IRQ */
	for (i = 0; i < crm->num_sw_drvs; i++) {
		if (!crm->sw_drvs[i].initialized)
			continue;

		drvs[i].irq = irq;
		/* SW DRV do not have any channels */
		drvs[i].num_channels = 0;

		/* Additionally allocate memory for sw_votes */
		for (j = 0; j < MAX_VCD_TYPE; j++) {
			vcd = &drvs[i].vcd[j];
			vcd->sw_votes = devm_kcalloc(crm->dev, vcd->num_resources,
						     sizeof(struct crm_sw_votes),
						     GFP_KERNEL);
			if (!vcd->sw_votes)
				return -ENOMEM;

			/* Enable IRQs for all VCDs */
			for (k = 0; k < vcd->num_resources; k++) {
				init_waitqueue_head(&vcd->sw_votes[k].wait);
				write_crm_reg(&drvs[i], IRQ_ENABLE, 0, j, k, IRQ_ENABLE_BIT);
			}
		}
	}

	return 0;
}

static int crm_probe_alloc_vcd_caches(struct crm_drv_top *crm, struct crm_vcd *vcd)
{
	u32 num_resources = vcd->num_resources;
	u32 num_pwr_states = vcd->num_pwr_states;
	int i;

	vcd->cache = devm_kcalloc(crm->dev, num_resources, sizeof(u32 *), GFP_KERNEL);
	if (!vcd->cache)
		return -ENOMEM;

	for (i = 0; i < num_resources; i++) {
		vcd->cache[i] = devm_kcalloc(crm->dev, num_pwr_states, sizeof(u32), GFP_KERNEL);
		if (!vcd->cache[i])
			return -ENOMEM;
	}

	return 0;
}

static int crm_probe_set_vcd_caches(struct crm_drv_top *crm, u32 crm_cfg, u32 crm_cfg_2)
{
	struct crm_vcd *vcd;
	struct crm_drv *drv;
	u32 num_perf_ol_vcds, num_nds, num_pwr_states;
	u32 num_bw_vote_vcds;
	int i, j, ret;

	num_perf_ol_vcds = crm_cfg & (NUM_VCD_VOTED_BY_PERF_OL_MASK <<
				      NUM_VCD_VOTED_BY_PERF_OL_SHIFT);
	num_perf_ol_vcds >>= NUM_VCD_VOTED_BY_PERF_OL_SHIFT;

	num_bw_vote_vcds = crm_cfg & (NUM_VCD_VOTED_BY_BW_MASK <<
				      NUM_VCD_VOTED_BY_BW_SHIFT);
	num_bw_vote_vcds >>= NUM_VCD_VOTED_BY_BW_SHIFT;

	num_pwr_states = crm_cfg & (NUM_PWR_STATES_PER_CH_MASK <<
				    NUM_PWR_STATES_PER_CH_SHIFT);
	num_pwr_states >>= NUM_PWR_STATES_PER_CH_SHIFT;

	num_nds = crm_cfg_2 & (NUM_OF_NODES_MASK << NUM_OF_NODES_SHIFT);
	num_nds >>= NUM_OF_NODES_SHIFT;

	for (i = 0; i < crm->num_hw_drvs; i++) {
		drv = &crm->hw_drvs[i];

		if (!drv->initialized)
			continue;

		drv->drv_type = CRM_HW_DRV;
		for (j = 0; j < MAX_VCD_TYPE; j++) {
			vcd = &drv->vcd[j];

			if (j == PERF_OL_VCD) {
				vcd->offsets = hw_drv_perf_ol_vcd_regs;
				vcd->num_resources = num_perf_ol_vcds;
			} else if (j == BW_VOTE_VCD) {
				vcd->offsets = hw_drv_bw_vote_vcd_regs;
				/* BW_VOTE_VCD can have multiple NDs with which BW can be voted */
				vcd->num_resources = num_nds;
			} else {
				continue;
			}
			vcd->num_pwr_states = num_pwr_states;
			ret = crm_probe_alloc_vcd_caches(crm, vcd);
			if (ret)
				return ret;
		}
	}

	for (i = 0; i < crm->num_sw_drvs; i++) {
		drv = &crm->sw_drvs[i];

		if (!drv->initialized)
			continue;

		drv->drv_type = CRM_SW_DRV;
		for (j = 0; j < MAX_VCD_TYPE; j++) {
			vcd = &drv->vcd[j];

			if (j == PERF_OL_VCD) {
				vcd->offsets = sw_drv_perf_ol_vcd_regs;
				vcd->num_resources = num_perf_ol_vcds;
			} else if (j == BW_VOTE_VCD) {
				vcd->offsets = sw_drv_bw_vote_vcd_regs;
				/* BW_VOTE_VCD can have multiple NDs with which BW can be voted */
				vcd->num_resources = num_nds;
			} else {
				continue;
			}

			vcd->num_pwr_states = MAX_SW_DRV_PWR_STATES;
			ret = crm_probe_alloc_vcd_caches(crm, vcd);
			if (ret)
				return ret;
		}
	}

	crm->crmb_mgr.offsets = crmb_regs;
	crm->crmb_mgr.num_resources = num_bw_vote_vcds;
	crm->crmc_mgr.offsets = crmc_regs;
	crm->crmc_mgr.num_resources = num_perf_ol_vcds;

	return 0;
}

static struct crm_drv *crm_probe_get_drvs(struct crm_drv_top *crm, int num_drvs,
					  const char *prop_name, const char *name)
{
	struct device_node *dn = crm->dev->of_node;
	u32 *drv_ids;
	int i, id;
	int ret;
	struct crm_drv *drvs;

	if (!num_drvs)
		return ERR_PTR(-EINVAL);

	drvs = devm_kcalloc(crm->dev, num_drvs, sizeof(struct crm_drv), GFP_KERNEL);
	if (!drvs)
		return ERR_PTR(-ENOMEM);

	drv_ids = kcalloc(num_drvs, sizeof(u32), GFP_KERNEL);
	if (!drv_ids)
		return ERR_PTR(-ENOMEM);

	ret = of_property_read_u32_array(dn, prop_name, drv_ids, num_drvs);
	if (ret) {
		kfree(drv_ids);
		return ERR_PTR(ret);
	}

	for (i = 0; i < num_drvs; i++) {
		id = drv_ids[i];

		scnprintf(drvs[i].name, sizeof(drvs[i].name), "%s_%s_%d", crm->name, name, id);
		drvs[i].drv_id = id;
		drvs[i].base = crm->base;
		spin_lock_init(&drvs[i].lock);
		spin_lock_init(&drvs[i].cache_lock);

		drvs[i].ipc_log_ctx = ipc_log_context_create(
						CRM_DRV_IPC_LOG_SIZE,
						drvs[i].name, 0);

		drvs[i].offsets = chn_regs;
		drvs[i].num_channels = crm->num_channels;
		drvs[i].initialized = true;
	}

	kfree(drv_ids);
	return drvs;
}

static int crm_probe_drvs(struct crm_drv_top *crm, struct device_node *dn)
{
	u32 crm_ver, major_ver, minor_ver;
	u32 crm_cfg, crm_cfg_2;
	int num_hw_drvs, num_sw_drvs;

	crm_ver = readl_relaxed(crm->base + CRM_VERSION);
	major_ver = crm_ver & (MAJOR_VER_MASK << MAJOR_VER_SHIFT);
	major_ver >>= MAJOR_VER_SHIFT;
	minor_ver = crm_ver & (MINOR_VER_MASK << MINOR_VER_SHIFT);
	minor_ver >>= MINOR_VER_SHIFT;

	pr_debug("CRM %s running version = %u.%u\n", crm->name, major_ver, minor_ver);

	crm_cfg = readl_relaxed(crm->base + CRM_CFG_PARAM_1);
	num_hw_drvs = crm_cfg & (NUM_HW_DRVS_MASK << NUM_HW_DRVS_SHIFT);
	num_hw_drvs >>= NUM_HW_DRVS_SHIFT;
	num_sw_drvs = crm_cfg & (NUM_SW_DRVS_MASK << NUM_SW_DRVS_SHIFT);
	num_sw_drvs >>= NUM_SW_DRVS_SHIFT;

	crm->num_channels = crm_cfg & (NUM_CH_MASK << NUM_CH_SHIFT);
	crm->num_channels >>= NUM_CH_SHIFT;

	crm->num_hw_drvs = of_property_count_u32_elems(dn, "qcom,hw-drv-ids");
	if (crm->num_hw_drvs < 0) {
		crm->num_hw_drvs = 0;
		goto skip_hw_drvs;
	}

	crm->hw_drvs = crm_probe_get_drvs(crm, crm->num_hw_drvs, "qcom,hw-drv-ids", "hw_drv");
	if (IS_ERR(crm->hw_drvs))
		return PTR_ERR(crm->hw_drvs);

skip_hw_drvs:
	crm->num_sw_drvs = of_property_count_u32_elems(dn, "qcom,sw-drv-ids");
	if (crm->num_sw_drvs < 0) {
		crm->num_sw_drvs = 0;
		goto skip_sw_drvs;
	}

	crm->sw_drvs = crm_probe_get_drvs(crm, crm->num_sw_drvs, "qcom,sw-drv-ids", "sw_drv");
	if (IS_ERR(crm->sw_drvs))
		return PTR_ERR(crm->sw_drvs);

skip_sw_drvs:
	if (crm->num_sw_drvs > num_sw_drvs ||
	    crm->num_hw_drvs > num_hw_drvs ||
	    (!crm->num_sw_drvs && !crm->num_hw_drvs))
		return -EINVAL;

	crm_cfg_2 = readl_relaxed(crm->base + CRM_CFG_PARAM_2);

	return crm_probe_set_vcd_caches(crm, crm_cfg, crm_cfg_2);
}

static int crm_probe_platform_resources(struct platform_device *pdev, struct crm_drv_top *crm)
{
	struct resource *res;

	crm->base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(crm->base))
		return -ENOMEM;

	crm->crmb_mgr.base = devm_platform_get_and_ioremap_resource(pdev, 1, &res);
	if (IS_ERR(crm->crmb_mgr.base))
		return -ENOMEM;
	strscpy(crm->crmb_mgr.name, res->name, sizeof(crm->crmb_mgr.name));

	crm->crmc_mgr.base = devm_platform_get_and_ioremap_resource(pdev, 2, &res);
	if (IS_ERR(crm->crmc_mgr.base))
		return -ENOMEM;
	strscpy(crm->crmc_mgr.name, res->name, sizeof(crm->crmc_mgr.name));

	return 0;
}

static int crm_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct crm_drv_top *crm;
	const char *name;
	u32 crm_en;
	int ret;

	crm = devm_kzalloc(&pdev->dev, sizeof(*crm), GFP_KERNEL);
	if (!crm)
		return -ENOMEM;

	crm->desc = of_device_get_match_data(&pdev->dev);
	if (!crm->desc)
		return -EINVAL;

	name = of_get_property(dn, "label", NULL);
	if (!name)
		name = dev_name(&pdev->dev);

	crm->pdev = pdev;
	crm->dev = &pdev->dev;
	scnprintf(crm->name, sizeof(crm->name), "%s", name);

	ret = crm_probe_platform_resources(pdev, crm);
	if (ret)
		return ret;

	crm_en = readl_relaxed(crm->base + CRM_ENABLE);
	if (!crm_en)
		return -EINVAL;

	ret = crm_probe_drvs(crm, dn);
	if (ret)
		return ret;

	ret = crm_probe_get_irqs(crm);
	if (ret)
		return ret;

	crm_set_chn_behave(crm);

	INIT_LIST_HEAD(&crm->list);
	list_add_tail(&crm->list, &crm_dev_list);
	dev_set_drvdata(&pdev->dev, crm);

	return ret;
}

struct crm_desc cam_crm_desc = {
	.set_chn_behave = true,
};

struct crm_desc pcie_crm_desc = {
	.set_chn_behave = false,
};

static const struct of_device_id crm_drv_match[] = {
	{ .compatible = "qcom,cam-crm", .data = &cam_crm_desc},
	{ .compatible = "qcom,pcie-crm", .data = &pcie_crm_desc},
	{ }
};
MODULE_DEVICE_TABLE(of, crm_drv_match);

static struct platform_driver crm_driver = {
	.probe = crm_probe,
	.driver = {
		  .name = "crm",
		  .of_match_table = crm_drv_match,
		  .suppress_bind_attrs = true,
	},
};
module_platform_driver(crm_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. (QTI) CRM Driver");
MODULE_LICENSE("GPL");
