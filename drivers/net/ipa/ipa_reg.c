// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */

#include <linux/io.h>

#include "ipa.h"
#include "ipa_reg.h"

/* Is this register valid for the current IPA version? */
static bool ipa_reg_valid(struct ipa *ipa, enum ipa_reg_id reg_id)
{
	enum ipa_version version = ipa->version;
	bool valid;

	/* Check for bogus (out of range) register IDs */
	if ((u32)reg_id >= IPA_REG_ID_COUNT)
		return false;

	switch (reg_id) {
	case IPA_BCR:
	case COUNTER_CFG:
		valid = version < IPA_VERSION_4_5;
		break;

	case IPA_TX_CFG:
	case FLAVOR_0:
	case IDLE_INDICATION_CFG:
		valid = version >= IPA_VERSION_3_5;
		break;

	case QTIME_TIMESTAMP_CFG:
	case TIMERS_XO_CLK_DIV_CFG:
	case TIMERS_PULSE_GRAN_CFG:
		valid = version >= IPA_VERSION_4_5;
		break;

	case SRC_RSRC_GRP_45_RSRC_TYPE:
	case DST_RSRC_GRP_45_RSRC_TYPE:
		valid = version <= IPA_VERSION_3_1 ||
			version == IPA_VERSION_4_5;
		break;

	case SRC_RSRC_GRP_67_RSRC_TYPE:
	case DST_RSRC_GRP_67_RSRC_TYPE:
		valid = version <= IPA_VERSION_3_1;
		break;

	case ENDP_FILTER_ROUTER_HSH_CFG:
		valid = version != IPA_VERSION_4_2;
		break;

	case IRQ_SUSPEND_EN:
	case IRQ_SUSPEND_CLR:
		valid = version >= IPA_VERSION_3_1;
		break;

	default:
		valid = true;	/* Others should be defined for all versions */
		break;
	}

	return valid;
}

/* Assumes the endpoint transfer direction is valid; 0 is a bad offset */
u32 __ipa_reg_offset(struct ipa *ipa, enum ipa_reg_id reg_id, u32 n)
{
	enum ipa_version version;

	if (!ipa_reg_valid(ipa, reg_id))
		return 0;

	version = ipa->version;

	switch (reg_id) {
	case COMP_CFG:
		return IPA_REG_COMP_CFG_OFFSET;
	case CLKON_CFG:
		return IPA_REG_CLKON_CFG_OFFSET;
	case ROUTE:
		return IPA_REG_ROUTE_OFFSET;
	case SHARED_MEM_SIZE:
		return IPA_REG_SHARED_MEM_SIZE_OFFSET;
	case QSB_MAX_WRITES:
		return IPA_REG_QSB_MAX_WRITES_OFFSET;
	case QSB_MAX_READS:
		return IPA_REG_QSB_MAX_READS_OFFSET;
	case FILT_ROUT_HASH_EN:
		return ipa_reg_filt_rout_hash_en_offset(version);
	case FILT_ROUT_HASH_FLUSH:
		return ipa_reg_filt_rout_hash_flush_offset(version);
	case STATE_AGGR_ACTIVE:
		return ipa_reg_state_aggr_active_offset(version);
	case IPA_BCR:
		return IPA_REG_BCR_OFFSET;
	case LOCAL_PKT_PROC_CNTXT:
		return IPA_REG_LOCAL_PKT_PROC_CNTXT_OFFSET;
	case AGGR_FORCE_CLOSE:
		return IPA_REG_AGGR_FORCE_CLOSE_OFFSET;
	case COUNTER_CFG:
		return IPA_REG_COUNTER_CFG_OFFSET;
	case IPA_TX_CFG:
		return IPA_REG_TX_CFG_OFFSET;
	case FLAVOR_0:
		return IPA_REG_FLAVOR_0_OFFSET;
	case IDLE_INDICATION_CFG:
		return ipa_reg_idle_indication_cfg_offset(version);
	case QTIME_TIMESTAMP_CFG:
		return IPA_REG_QTIME_TIMESTAMP_CFG_OFFSET;
	case TIMERS_XO_CLK_DIV_CFG:
		return IPA_REG_TIMERS_XO_CLK_DIV_CFG_OFFSET;
	case TIMERS_PULSE_GRAN_CFG:
		return IPA_REG_TIMERS_PULSE_GRAN_CFG_OFFSET;
	case SRC_RSRC_GRP_01_RSRC_TYPE:
		return IPA_REG_SRC_RSRC_GRP_01_RSRC_TYPE_N_OFFSET(n);
	case SRC_RSRC_GRP_23_RSRC_TYPE:
		return IPA_REG_SRC_RSRC_GRP_23_RSRC_TYPE_N_OFFSET(n);
	case SRC_RSRC_GRP_45_RSRC_TYPE:
		return IPA_REG_SRC_RSRC_GRP_45_RSRC_TYPE_N_OFFSET(n);
	case SRC_RSRC_GRP_67_RSRC_TYPE:
		return IPA_REG_SRC_RSRC_GRP_67_RSRC_TYPE_N_OFFSET(n);
	case DST_RSRC_GRP_01_RSRC_TYPE:
		return IPA_REG_DST_RSRC_GRP_01_RSRC_TYPE_N_OFFSET(n);
	case DST_RSRC_GRP_23_RSRC_TYPE:
		return IPA_REG_DST_RSRC_GRP_23_RSRC_TYPE_N_OFFSET(n);
	case DST_RSRC_GRP_45_RSRC_TYPE:
		return IPA_REG_DST_RSRC_GRP_45_RSRC_TYPE_N_OFFSET(n);
	case DST_RSRC_GRP_67_RSRC_TYPE:
		return IPA_REG_DST_RSRC_GRP_67_RSRC_TYPE_N_OFFSET(n);
	case ENDP_INIT_CTRL:
		return IPA_REG_ENDP_INIT_CTRL_N_OFFSET(n);
	case ENDP_INIT_CFG:
		return IPA_REG_ENDP_INIT_CFG_N_OFFSET(n);
	case ENDP_INIT_NAT:
		return IPA_REG_ENDP_INIT_NAT_N_OFFSET(n);
	case ENDP_INIT_HDR:
		return IPA_REG_ENDP_INIT_HDR_N_OFFSET(n);
	case ENDP_INIT_HDR_EXT:
		return IPA_REG_ENDP_INIT_HDR_EXT_N_OFFSET(n);
	case ENDP_INIT_HDR_METADATA_MASK:
		return IPA_REG_ENDP_INIT_HDR_METADATA_MASK_N_OFFSET(n);
	case ENDP_INIT_MODE:
		return IPA_REG_ENDP_INIT_MODE_N_OFFSET(n);
	case ENDP_INIT_AGGR:
		return IPA_REG_ENDP_INIT_AGGR_N_OFFSET(n);
	case ENDP_INIT_HOL_BLOCK_EN:
		return IPA_REG_ENDP_INIT_HOL_BLOCK_EN_N_OFFSET(n);
	case ENDP_INIT_HOL_BLOCK_TIMER:
		return IPA_REG_ENDP_INIT_HOL_BLOCK_TIMER_N_OFFSET(n);
	case ENDP_INIT_DEAGGR:
		return IPA_REG_ENDP_INIT_DEAGGR_N_OFFSET(n);
	case ENDP_INIT_RSRC_GRP:
		return IPA_REG_ENDP_INIT_RSRC_GRP_N_OFFSET(n);
	case ENDP_INIT_SEQ:
		return IPA_REG_ENDP_INIT_SEQ_N_OFFSET(n);
	case ENDP_STATUS:
		return IPA_REG_ENDP_STATUS_N_OFFSET(n);
	case ENDP_FILTER_ROUTER_HSH_CFG:
		return IPA_REG_ENDP_FILTER_ROUTER_HSH_CFG_N_OFFSET(n);
	/* The IRQ registers below are only used for GSI_EE_AP */
	case IPA_IRQ_STTS:
		return ipa_reg_irq_stts_offset(version);
	case IPA_IRQ_EN:
		return ipa_reg_irq_en_offset(version);
	case IPA_IRQ_CLR:
		return ipa_reg_irq_clr_offset(version);
	case IPA_IRQ_UC:
		return ipa_reg_irq_uc_offset(version);
	case IRQ_SUSPEND_INFO:
		return ipa_reg_irq_suspend_info_offset(version);
	case IRQ_SUSPEND_EN:
		return ipa_reg_irq_suspend_en_offset(version);
	case IRQ_SUSPEND_CLR:
		return ipa_reg_irq_suspend_clr_offset(version);
	default:
		WARN(true, "bad register id %u???\n", reg_id);
		return 0;
	}
}

int ipa_reg_init(struct ipa *ipa)
{
	struct device *dev = &ipa->pdev->dev;
	struct resource *res;

	/* Setup IPA register memory  */
	res = platform_get_resource_byname(ipa->pdev, IORESOURCE_MEM,
					   "ipa-reg");
	if (!res) {
		dev_err(dev, "DT error getting \"ipa-reg\" memory property\n");
		return -ENODEV;
	}

	ipa->reg_virt = ioremap(res->start, resource_size(res));
	if (!ipa->reg_virt) {
		dev_err(dev, "unable to remap \"ipa-reg\" memory\n");
		return -ENOMEM;
	}
	ipa->reg_addr = res->start;

	return 0;
}

void ipa_reg_exit(struct ipa *ipa)
{
	iounmap(ipa->reg_virt);
}
