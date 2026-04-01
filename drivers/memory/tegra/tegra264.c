// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025-2026, NVIDIA CORPORATION.  All rights reserved.
 */

#include <dt-bindings/memory/nvidia,tegra264.h>

#include <linux/interconnect.h>
#include <linux/of_device.h>
#include <linux/tegra-icc.h>

#include <soc/tegra/bpmp.h>
#include <soc/tegra/mc.h>

#include "mc.h"
#include "tegra264-bwmgr.h"

/*
 * MC Client entries are sorted in the increasing order of the
 * override and security register offsets.
 */
static const struct tegra_mc_client tegra264_mc_clients[] = {
	{
		.id = TEGRA264_MEMORY_CLIENT_HDAR,
		.name = "hdar",
		.bpmp_id = TEGRA264_BWMGR_HDA,
		.type = TEGRA_ICC_ISO_AUDIO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_HDAW,
		.name = "hdaw",
		.bpmp_id = TEGRA264_BWMGR_HDA,
		.type = TEGRA_ICC_ISO_AUDIO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE0R,
		.name = "mgbe0r",
		.bpmp_id = TEGRA264_BWMGR_EQOS,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE0W,
		.name = "mgbe0w",
		.bpmp_id = TEGRA264_BWMGR_EQOS,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE1R,
		.name = "mgbe1r",
		.bpmp_id = TEGRA264_BWMGR_EQOS,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_MGBE1W,
		.name = "mgbe1w",
		.bpmp_id = TEGRA264_BWMGR_EQOS,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SDMMC0R,
		.name = "sdmmc0r",
		.bpmp_id = TEGRA264_BWMGR_SDMMC_1,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_SDMMC0W,
		.name = "sdmmc0w",
		.bpmp_id = TEGRA264_BWMGR_SDMMC_1,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VICR,
		.name = "vicr",
		.bpmp_id = TEGRA264_BWMGR_VIC,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VICW,
		.name = "vicw",
		.bpmp_id = TEGRA264_BWMGR_VIC,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_APER,
		.name = "aper",
		.bpmp_id = TEGRA264_BWMGR_APE,
		.type = TEGRA_ICC_ISO_AUDIO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_APEW,
		.name = "apew",
		.bpmp_id = TEGRA264_BWMGR_APE,
		.type = TEGRA_ICC_ISO_AUDIO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_APEDMAR,
		.name = "apedmar",
		.bpmp_id = TEGRA264_BWMGR_APEDMA,
		.type = TEGRA_ICC_ISO_AUDIO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_APEDMAW,
		.name = "apedmaw",
		.bpmp_id = TEGRA264_BWMGR_APEDMA,
		.type = TEGRA_ICC_ISO_AUDIO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VIFALCONR,
		.name = "vifalconr",
		.bpmp_id = TEGRA264_BWMGR_VIFAL,
		.type = TEGRA_ICC_ISO_VIFAL,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_VIFALCONW,
		.name = "vifalconw",
		.bpmp_id = TEGRA264_BWMGR_VIFAL,
		.type = TEGRA_ICC_ISO_VIFAL,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_RCER,
		.name = "rcer",
		.bpmp_id = TEGRA264_BWMGR_RCE,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_RCEW,
		.name = "rcew",
		.bpmp_id = TEGRA264_BWMGR_RCE,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE0W,
		.name = "pcie0w",
		.bpmp_id = TEGRA264_BWMGR_PCIE_0,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE1R,
		.name = "pcie1r",
		.bpmp_id = TEGRA264_BWMGR_PCIE_1,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE1W,
		.name = "pcie1w",
		.bpmp_id = TEGRA264_BWMGR_PCIE_1,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE2AR,
		.name = "pcie2ar",
		.bpmp_id = TEGRA264_BWMGR_PCIE_2,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE2AW,
		.name = "pcie2aw",
		.bpmp_id = TEGRA264_BWMGR_PCIE_2,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE3R,
		.name = "pcie3r",
		.bpmp_id = TEGRA264_BWMGR_PCIE_3,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE3W,
		.name = "pcie3w",
		.bpmp_id = TEGRA264_BWMGR_PCIE_3,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE4R,
		.name = "pcie4r",
		.bpmp_id = TEGRA264_BWMGR_PCIE_4,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE4W,
		.name = "pcie4w",
		.bpmp_id = TEGRA264_BWMGR_PCIE_4,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE5R,
		.name = "pcie5r",
		.bpmp_id = TEGRA264_BWMGR_PCIE_5,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_PCIE5W,
		.name = "pcie5w",
		.bpmp_id = TEGRA264_BWMGR_PCIE_5,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_GPUR02MC,
		.name = "gpur02mc",
		.bpmp_id = TEGRA264_BWMGR_GPU,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_GPUW02MC,
		.name = "gpuw02mc",
		.bpmp_id = TEGRA264_BWMGR_GPU,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_NVDECSRD2MC,
		.name = "nvdecsrd2mc",
		.bpmp_id = TEGRA264_BWMGR_NVDEC,
		.type = TEGRA_ICC_NISO,
	}, {
		.id = TEGRA264_MEMORY_CLIENT_NVDECSWR2MC,
		.name = "nvdecswr2mc",
		.bpmp_id = TEGRA264_BWMGR_NVDEC,
		.type = TEGRA_ICC_NISO,
	},
};

static const char *const tegra264_hub_error_names[32] = {
	[0] = "coalescer error",
	[1] = "SMMU BYPASS ALLOW error",
	[2] = "Illegal tbugrp_id error",
	[3] = "Malformed MSI request error",
	[4] = "Read response with poison bit error",
	[5] = "Restricted access violation error",
	[6] = "Reserved PA error",
};

static const char *const tegra264_mc_error_names[4] = {
	[1] = "EMEM decode error",
	[2] = "TrustZone violation",
	[3] = "Carveout violation",
};

static const char *const tegra264_rt_error_names[16] = {
	[1] = "DECERR_PARTIAL_POPULATED",
	[2] = "DECERR_SMMU_BYPASS",
	[3] = "DECERR_INVALID_MMIO",
	[4] = "DECERR_INVALID_GIC_MSI",
	[5] = "DECERR_ATOMIC_SYSRAM",
	[9] = "DECERR_REMOTE_REQ_PRE_BOOT",
	[10] = "DECERR_ISO_OVER_C2C",
	[11] = "DECERR_UNSUPPORTED_SBS_OPCODE",
	[12] = "DECERR_SBS_REQ_OVER_SISO_LL",
};

/*
 * MC instance aperture mapping for hubc registers
 */
static const int mc_hubc_aperture_number[5] = {
	7, 8, 9, 10, 11
};

/*
 * tegra264_mc_icc_set() - Pass MC client info to the BPMP-FW
 * @src: ICC node for Memory Controller's (MC) Client
 * @dst: ICC node for Memory Controller (MC)
 *
 * Passing the current request info from the MC to the BPMP-FW where
 * LA and PTSA registers are accessed and the final EMC freq is set
 * based on client_id, type, latency and bandwidth.
 * icc_set_bw() makes set_bw calls for both MC and EMC providers in
 * sequence. Both the calls are protected by 'mutex_lock(&icc_lock)'.
 * So, the data passed won't be updated by concurrent set calls from
 * other clients.
 */
static int tegra264_mc_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct tegra_mc *mc = icc_provider_to_tegra_mc(dst->provider);
	struct mrq_bwmgr_int_request bwmgr_req = { 0 };
	struct mrq_bwmgr_int_response bwmgr_resp = { 0 };
	const struct tegra_mc_client *pclient = src->data;
	struct tegra_bpmp_message msg;
	int ret;

	/*
	 * Same Src and Dst node will happen during boot from icc_node_add().
	 * This can be used to pre-initialize and set bandwidth for all clients
	 * before their drivers are loaded. We are skipping this case as for us,
	 * the pre-initialization already happened in Bootloader(MB2) and BPMP-FW.
	 */
	if (src->id == dst->id)
		return 0;

	if (!mc->bwmgr_mrq_supported)
		return 0;

	if (!mc->bpmp) {
		dev_err(mc->dev, "BPMP reference NULL\n");
		return -ENOENT;
	}

	if (pclient->type == TEGRA_ICC_NISO)
		bwmgr_req.bwmgr_calc_set_req.niso_bw = src->avg_bw;
	else
		bwmgr_req.bwmgr_calc_set_req.iso_bw = src->avg_bw;

	bwmgr_req.bwmgr_calc_set_req.client_id = pclient->bpmp_id;

	bwmgr_req.cmd = CMD_BWMGR_INT_CALC_AND_SET;
	bwmgr_req.bwmgr_calc_set_req.mc_floor = src->peak_bw;
	bwmgr_req.bwmgr_calc_set_req.floor_unit = BWMGR_INT_UNIT_KBPS;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_BWMGR_INT;
	msg.tx.data = &bwmgr_req;
	msg.tx.size = sizeof(bwmgr_req);
	msg.rx.data = &bwmgr_resp;
	msg.rx.size = sizeof(bwmgr_resp);

	ret = tegra_bpmp_transfer(mc->bpmp, &msg);
	if (ret < 0) {
		dev_err(mc->dev, "BPMP transfer failed: %d\n", ret);
		goto error;
	}
	if (msg.rx.ret < 0) {
		pr_err("failed to set bandwidth for %u: %d\n",
		       bwmgr_req.bwmgr_calc_set_req.client_id, msg.rx.ret);
		ret = -EINVAL;
	}

error:
	return ret;
}

static int tegra264_mc_icc_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
				     u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	struct icc_provider *p = node->provider;
	struct tegra_mc *mc = icc_provider_to_tegra_mc(p);

	if (!mc->bwmgr_mrq_supported)
		return 0;

	*agg_avg += avg_bw;
	*agg_peak = max(*agg_peak, peak_bw);

	return 0;
}

static int tegra264_mc_icc_get_init_bw(struct icc_node *node, u32 *avg, u32 *peak)
{
	*avg = 0;
	*peak = 0;

	return 0;
}

static void mcf_log_fault(struct tegra_mc *mc, u32 channel, unsigned long mcf_ch_intstatus)
{
	unsigned int bit;

	for_each_set_bit(bit, &mcf_ch_intstatus, 32) {
		const char *client = "unknown", *desc = "NA";
		u32 status_reg, status1_reg = 0, addr_reg, addr_hi_reg = 0, err_type_mask = 0;
		u32 value, client_id, i, addr_hi_shift = 0, addr_hi_mask = 0, status1;
		u32 mc_rw_bit = MC_ERR_STATUS_RW, mc_sec_bit = MC_ERR_STATUS_SECURITY;
		phys_addr_t addr = 0;
		u8 type;

		switch (BIT(bit)) {
		case MC_INT_DECERR_EMEM:
		case MC_INT_SECURITY_VIOLATION:
			status_reg = mc->soc->regs->err_status;
			addr_reg = mc->soc->regs->err_add;
			addr_hi_reg = mc->soc->regs->err_add_hi;
			err_type_mask = mc->soc->mc_err_status_type_mask;
			break;

		case MC_INT_DECERR_VPR:
			status_reg = mc->soc->regs->err_vpr_status;
			addr_reg = mc->soc->regs->err_vpr_add;
			addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT;
			addr_hi_mask = mc->soc->mc_addr_hi_mask;
			break;

		case MC_INT_SECERR_SEC:
			status_reg = mc->soc->regs->err_sec_status;
			addr_reg = mc->soc->regs->err_sec_add;
			addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT;
			addr_hi_mask = mc->soc->mc_addr_hi_mask;
			break;

		case MC_INT_DECERR_MTS:
			status_reg = mc->soc->regs->err_mts_status;
			addr_reg = mc->soc->regs->err_mts_add;
			addr_hi_shift = MC_ERR_STATUS_ADR_HI_SHIFT;
			addr_hi_mask = mc->soc->mc_addr_hi_mask;
			break;

		case MC_INT_DECERR_GENERALIZED_CARVEOUT:
			status_reg = mc->soc->regs->err_gen_co_status;
			status1_reg = MC_ERR_GENERALIZED_CARVEOUT_STATUS_1_0;
			addr_reg = mc->soc->regs->err_gen_co_add;
			addr_hi_shift = MC_ERR_STATUS_GSC_ADR_HI_SHIFT;
			addr_hi_mask = MC_ERR_STATUS_GSC_ADR_HI_MASK;
			break;

		case MC_INT_DECERR_ROUTE_SANITY:
		case MC_INT_DECERR_ROUTE_SANITY_GIC_MSI:
			status_reg = mc->soc->regs->err_route_status;
			addr_reg = mc->soc->regs->err_route_add;
			addr_hi_shift = MC_ERR_STATUS_RT_ADR_HI_SHIFT;
			addr_hi_mask = mc->soc->mc_addr_hi_mask;
			mc_sec_bit = MC_ERR_ROUTE_SANITY_SEC;
			mc_rw_bit = MC_ERR_ROUTE_SANITY_RW;
			err_type_mask = MC_ERR_STATUS_RT_TYPE_MASK;
			break;

		default:
			dev_err_ratelimited(mc->dev, "Incorrect MC interrupt mask\n");
			return;
		}

		value = mc_ch_readl(mc, channel, status_reg);
		if (addr_hi_reg) {
			addr = mc_ch_readl(mc, channel, addr_hi_reg);
		} else {
			if (!status1_reg) {
				addr = ((value >> addr_hi_shift) & addr_hi_mask);
			} else {
				status1 = mc_ch_readl(mc, channel, status1_reg);
				addr = ((status1 >> addr_hi_shift) & addr_hi_mask);
			}
		}

		addr <<= 32;
		addr |= mc_ch_readl(mc, channel, addr_reg);

		client_id = value & mc->soc->client_id_mask;
		for (i = 0; i < mc->soc->num_clients; i++) {
			if (mc->soc->clients[i].id == client_id) {
				client = mc->soc->clients[i].name;
				break;
			}
		}

		if (err_type_mask == MC_ERR_STATUS_RT_TYPE_MASK) {
			type = (value & err_type_mask) >>
				MC_ERR_STATUS_RT_TYPE_SHIFT;
			desc = tegra264_rt_error_names[type];
		} else if (err_type_mask) {
			type = (value & err_type_mask) >>
					MC_ERR_STATUS_TYPE_SHIFT;
			desc = tegra264_mc_error_names[type];
		}

		dev_err_ratelimited(mc->dev, "%s: %s %s @%pa: %s (%s)\n",
				    client, value & mc_sec_bit ? "secure" : "non-secure",
				    value & mc_rw_bit ? "write" : "read", &addr,
				    tegra_mc_status_names[bit] ?: "unknown", desc);
		if (status1_reg)
			dev_err_ratelimited(mc->dev, "gsc_apr_id=%u gsc_co_apr_id=%u\n",
					    ((status1 >> ERR_GENERALIZED_APERTURE_ID_SHIFT)
					    & ERR_GENERALIZED_APERTURE_ID_MASK),
					    ((status1 >> ERR_GENERALIZED_CARVEOUT_APERTURE_ID_SHIFT)
					    & ERR_GENERALIZED_CARVEOUT_APERTURE_ID_MASK));
	}

	/* clear interrupts */
	mc_ch_writel(mc, channel, mcf_ch_intstatus, MCF_INTSTATUS_0);
}

static irqreturn_t handle_mcf_irq(int irq, void *data)
{
	struct tegra_mc *mc = data;
	unsigned long common_intstat, intstatus;
	u32 slice;

	/* Read MCF_COMMON_INTSTATUS0_0_0 from MCB block */
	common_intstat = mc_ch_readl(mc, MC_BROADCAST_CHANNEL, MCF_COMMON_INTSTATUS0_0_0);
	if (common_intstat == 0) {
		dev_warn(mc->dev, "No interrupt in MCF\n");
		return IRQ_NONE;
	}

	for_each_set_bit(slice, &common_intstat, 32) {
		/* Find out the slice number on which interrupt occurred */
		if (slice > 4) {
			dev_err(mc->dev, "Slice index out of bounds: %u\n", slice);
			return IRQ_NONE;
		}

		intstatus = mc_ch_readl(mc, slice, MCF_INTSTATUS_0);
		if (intstatus != 0)
			mcf_log_fault(mc, slice, intstatus);
	}

	return IRQ_HANDLED;
}

static void hub_log_fault(struct tegra_mc *mc, u32 hub, unsigned long hub_intstat)
{
	unsigned int bit;

	for_each_set_bit(bit, &hub_intstat, 32) {
		const char *client = "unknown";
		u32 client_id, status_reg, value, i;
		phys_addr_t addr = 0;

		switch (BIT(bit)) {
		case MSS_HUB_COALESCER_ERR_INTMASK:
			status_reg = MSS_HUB_COALESCE_ERR_STATUS_0;
			addr = mc_ch_readl(mc, hub, MSS_HUB_COALESCE_ERR_ADR_HI_0);
			addr <<= 32;
			addr |= mc_ch_readl(mc, hub, MSS_HUB_COALESCE_ERR_ADR_0);
			break;

		case MSS_HUB_SMMU_BYPASS_ALLOW_ERR_INTMASK:
			status_reg = MSS_HUB_SMMU_BYPASS_ALLOW_ERR_STATUS_0;
			break;

		case MSS_HUB_ILLEGAL_TBUGRP_ID_INTMASK:
			status_reg = MSS_HUB_ILLEGAL_TBUGRP_ID_ERR_STATUS_0;
			break;

		case MSS_HUB_MSI_ERR_INTMASK:
			status_reg = MSS_HUB_MSI_ERR_STATUS_0;
			break;

		case MSS_HUB_POISON_RSP_INTMASK:
			status_reg = MSS_HUB_POISON_RSP_STATUS_0;
			break;

		case MSS_HUB_RESTRICTED_ACCESS_ERR_INTMASK:
			status_reg = MSS_HUB_RESTRICTED_ACCESS_ERR_STATUS_0;
			break;

		case MSS_HUB_RESERVED_PA_ERR_INTMASK:
			status_reg = MSS_HUB_RESERVED_PA_ERR_STATUS_0;
			break;

		default:
			dev_err_ratelimited(mc->dev, "Incorrect HUB interrupt mask\n");
			return;
		}

		value = mc_ch_readl(mc, hub, status_reg);

		client_id = value & mc->soc->client_id_mask;
		for (i = 0; i < mc->soc->num_clients; i++) {
			if (mc->soc->clients[i].id == client_id) {
				client = mc->soc->clients[i].name;
				break;
			}
		}

		dev_err_ratelimited(mc->dev, "%s: @%pa: %s status: 0x%x\n",
				    client, &addr, tegra264_hub_error_names[bit] ?: "unknown",
				    value);
	}

	/* clear interrupts */
	mc_ch_writel(mc, hub, hub_intstat, MSS_HUB_INTRSTATUS_0);
}

static irqreturn_t handle_hub_irq(int irq, void *data, int mc_hubc_aperture_number)
{
	struct tegra_mc *mc = data;
	u32 global_intstat;
	unsigned long hub_interrupt, intstat, hub;

	/* Read MSS_HUB_GLOBAL_INTSTATUS_0 from mc_hubc_aperture_number block */
	global_intstat = mc_ch_readl(mc, mc_hubc_aperture_number, MSS_HUB_GLOBAL_INTSTATUS_0);
	if (global_intstat == 0) {
		dev_warn(mc->dev, "No interrupt in HUB/HUBC\n");
		return IRQ_NONE;
	}

	/* Handle interrupt from hubc */
	if (global_intstat & MSS_HUBC_INTR) {
		/* Read MSS_HUB_HUBC_INTSTATUS_0 from block mc_hubc_aperture_number */
		intstat = mc_ch_readl(mc, mc_hubc_aperture_number, MSS_HUB_HUBC_INTSTATUS_0);
		if (intstat != 0) {
			dev_err_ratelimited(mc->dev, "Scrubber operation status: 0x%lx\n",
					    intstat);
			/* Clear hubc interrupt */
			mc_ch_writel(mc, mc_hubc_aperture_number, intstat,
				     MSS_HUB_HUBC_INTSTATUS_0);
		}
	}

	hub_interrupt = (global_intstat & MSS_HUB_GLOBAL_MASK) >> MSS_HUB_GLOBAL_SHIFT;
	/* Handle interrupt from hub */
	for_each_set_bit(hub, &hub_interrupt, 32) {
		/* Read MSS_HUB_INTRSTATUS_0 from block MCi */
		intstat = mc_ch_readl(mc, hub, MSS_HUB_INTRSTATUS_0);
		if (intstat != 0)
			hub_log_fault(mc, hub, intstat);
	}

	/* Clear global interrupt status register */
	mc_ch_writel(mc, mc_hubc_aperture_number, global_intstat, MSS_HUB_GLOBAL_INTSTATUS_0);
	return IRQ_HANDLED;
}

static irqreturn_t handle_disp_hub_irq(int irq, void *data)
{
	return handle_hub_irq(irq, data, mc_hubc_aperture_number[0]);
}

static irqreturn_t handle_system_hub_irq(int irq, void *data)
{
	return handle_hub_irq(irq, data, mc_hubc_aperture_number[1]);
}

static irqreturn_t handle_vision_hub_irq(int irq, void *data)
{
	return handle_hub_irq(irq, data, mc_hubc_aperture_number[2]);
}

static irqreturn_t handle_uphy_hub_irq(int irq, void *data)
{
	return handle_hub_irq(irq, data, mc_hubc_aperture_number[3]);
}

static irqreturn_t handle_top_hub_irq(int irq, void *data)
{
	return handle_hub_irq(irq, data, mc_hubc_aperture_number[4]);
}

static irqreturn_t handle_generic_irq(struct tegra_mc *mc, unsigned long intstat_reg)
{
	u32 intstat, i;

	/* Iterate over all MC blocks to read INTSTATUS */
	for (i = 0; i < mc->num_channels; i++) {
		intstat = mc_ch_readl(mc, i, intstat_reg);
		if (intstat) {
			dev_err_ratelimited(mc->dev, "channel: %i status: 0x%x\n", i, intstat);
			/* Clear interrupt */
			mc_ch_writel(mc, i, intstat, intstat_reg);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t handle_sbs_irq(int irq, void *data)
{
	return handle_generic_irq((struct tegra_mc *)data, MSS_SBS_INTSTATUS_0);
}

static irqreturn_t handle_channel_irq(int irq, void *data)
{
	return handle_generic_irq((struct tegra_mc *)data, MC_CH_INTSTATUS_0);
}

static const irq_handler_t tegra264_mc_irq_handlers[8] = {
	handle_mcf_irq, handle_disp_hub_irq, handle_vision_hub_irq,
	handle_system_hub_irq, handle_uphy_hub_irq, handle_top_hub_irq,
	handle_sbs_irq, handle_channel_irq
};

static const struct tegra_mc_icc_ops tegra264_mc_icc_ops = {
	.xlate = tegra_mc_icc_xlate,
	.aggregate = tegra264_mc_icc_aggregate,
	.get_bw = tegra264_mc_icc_get_init_bw,
	.set = tegra264_mc_icc_set,
};

static const struct tegra_mc_regs tegra264_mc_regs = {
	.cfg_channel_enable = 0x8870,
	.err_status = 0xbc00,
	.err_add = 0xbc04,
	.err_add_hi = 0xbc08,
	.err_vpr_status = 0xbc20,
	.err_vpr_add = 0xbc24,
	.err_sec_status = 0xbc3c,
	.err_sec_add = 0xbc40,
	.err_mts_status = 0xbc5c,
	.err_mts_add = 0xbc60,
	.err_gen_co_status = 0xbc78,
	.err_gen_co_add = 0xbc7c,
	.err_route_status = 0xbc64,
	.err_route_add = 0xbc68,
};

static const struct tegra_mc_intmask tegra264_mc_intmasks[] = {
	{
		.reg = MCF_INTMASK_0,
		.mask = MC_INT_DECERR_ROUTE_SANITY_GIC_MSI | MC_INT_DECERR_ROUTE_SANITY |
			MC_INT_DECERR_GENERALIZED_CARVEOUT | MC_INT_DECERR_MTS |
			MC_INT_SECERR_SEC | MC_INT_DECERR_VPR | MC_INT_SECURITY_VIOLATION |
			MC_INT_DECERR_EMEM,
	},
	{
		.reg = MCF_INTPRIORITY_0,
		.mask = MC_INT_DECERR_ROUTE_SANITY_GIC_MSI | MC_INT_DECERR_ROUTE_SANITY |
			MC_INT_DECERR_GENERALIZED_CARVEOUT | MC_INT_DECERR_MTS |
			MC_INT_SECERR_SEC | MC_INT_DECERR_VPR | MC_INT_SECURITY_VIOLATION |
			MC_INT_DECERR_EMEM,
	},
	{
		.reg = MSS_HUB_INTRMASK_0,
		.mask = MSS_HUB_COALESCER_ERR_INTMASK | MSS_HUB_SMMU_BYPASS_ALLOW_ERR_INTMASK |
			MSS_HUB_ILLEGAL_TBUGRP_ID_INTMASK | MSS_HUB_MSI_ERR_INTMASK |
			MSS_HUB_POISON_RSP_INTMASK | MSS_HUB_RESTRICTED_ACCESS_ERR_INTMASK |
			MSS_HUB_RESERVED_PA_ERR_INTMASK,
	},
	{
		.reg = MSS_HUB_INTRPRIORITY_0,
		.mask = MSS_HUB_COALESCER_ERR_INTMASK | MSS_HUB_SMMU_BYPASS_ALLOW_ERR_INTMASK |
			MSS_HUB_ILLEGAL_TBUGRP_ID_INTMASK | MSS_HUB_MSI_ERR_INTMASK |
			MSS_HUB_POISON_RSP_INTMASK | MSS_HUB_RESTRICTED_ACCESS_ERR_INTMASK |
			MSS_HUB_RESERVED_PA_ERR_INTMASK,
	},
	{
		.reg = MSS_HUB_HUBC_INTMASK_0,
		.mask = MSS_HUB_HUBC_SCRUB_DONE_INTMASK,
	},
	{
		.reg = MSS_HUB_HUBC_INTPRIORITY_0,
		.mask = MSS_HUB_HUBC_SCRUB_DONE_INTMASK,
	},
	{
		.reg = MSS_SBS_INTMASK_0,
		.mask = MSS_SBS_FILL_FIFO_ISO_OVERFLOW_INTMASK |
			MSS_SBS_FILL_FIFO_SISO_OVERFLOW_INTMASK |
			MSS_SBS_FILL_FIFO_NISO_OVERFLOW_INTMASK,
	},
	{
		.reg = MC_CH_INTMASK_0,
		.mask = WCAM_ERR_INTMASK,
	}
};

const struct tegra_mc_soc tegra264_mc_soc = {
	.num_clients = ARRAY_SIZE(tegra264_mc_clients),
	.clients = tegra264_mc_clients,
	.num_address_bits = 40,
	.num_channels = 16,
	.client_id_mask = 0x1ff,
	.intmasks = tegra264_mc_intmasks,
	.num_intmasks = ARRAY_SIZE(tegra264_mc_intmasks),
	.has_addr_hi_reg = true,
	.ops = &tegra186_mc_ops,
	.icc_ops = &tegra264_mc_icc_ops,
	.ch_intmask = 0x0000ff00,
	.global_intstatus_channel_shift = 8,
	/*
	 * Additionally, there are lite carveouts but those are not currently
	 * supported.
	 */
	.num_carveouts = 32,
	.mc_addr_hi_mask = 0xff,
	.mc_err_status_type_mask = (0x3 << 28),
	.regs = &tegra264_mc_regs,
	.handle_irq = tegra264_mc_irq_handlers,
	.num_interrupts = ARRAY_SIZE(tegra264_mc_irq_handlers),
};
