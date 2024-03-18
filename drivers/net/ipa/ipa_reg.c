// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2023 Linaro Ltd.
 */

#include <linux/platform_device.h>
#include <linux/io.h>

#include "ipa.h"
#include "ipa_reg.h"

/* Is this register ID valid for the current IPA version? */
static bool ipa_reg_id_valid(struct ipa *ipa, enum ipa_reg_id reg_id)
{
	enum ipa_version version = ipa->version;

	switch (reg_id) {
	case FILT_ROUT_HASH_EN:
		return version == IPA_VERSION_4_2;

	case FILT_ROUT_HASH_FLUSH:
		return version < IPA_VERSION_5_0 && version != IPA_VERSION_4_2;

	case FILT_ROUT_CACHE_FLUSH:
	case ENDP_FILTER_CACHE_CFG:
	case ENDP_ROUTER_CACHE_CFG:
		return version >= IPA_VERSION_5_0;

	case IPA_BCR:
	case COUNTER_CFG:
		return version < IPA_VERSION_4_5;

	case IPA_TX_CFG:
	case FLAVOR_0:
	case IDLE_INDICATION_CFG:
		return version >= IPA_VERSION_3_5;

	case QTIME_TIMESTAMP_CFG:
	case TIMERS_XO_CLK_DIV_CFG:
	case TIMERS_PULSE_GRAN_CFG:
		return version >= IPA_VERSION_4_5;

	case SRC_RSRC_GRP_45_RSRC_TYPE:
	case DST_RSRC_GRP_45_RSRC_TYPE:
		return version <= IPA_VERSION_3_1 ||
		       version == IPA_VERSION_4_5 ||
		       version >= IPA_VERSION_5_0;

	case SRC_RSRC_GRP_67_RSRC_TYPE:
	case DST_RSRC_GRP_67_RSRC_TYPE:
		return version <= IPA_VERSION_3_1 ||
		       version >= IPA_VERSION_5_0;

	case ENDP_FILTER_ROUTER_HSH_CFG:
		return version < IPA_VERSION_5_0 &&
			version != IPA_VERSION_4_2;

	case IRQ_SUSPEND_EN:
	case IRQ_SUSPEND_CLR:
		return version >= IPA_VERSION_3_1;

	case COMP_CFG:
	case CLKON_CFG:
	case ROUTE:
	case SHARED_MEM_SIZE:
	case QSB_MAX_WRITES:
	case QSB_MAX_READS:
	case STATE_AGGR_ACTIVE:
	case LOCAL_PKT_PROC_CNTXT:
	case AGGR_FORCE_CLOSE:
	case SRC_RSRC_GRP_01_RSRC_TYPE:
	case SRC_RSRC_GRP_23_RSRC_TYPE:
	case DST_RSRC_GRP_01_RSRC_TYPE:
	case DST_RSRC_GRP_23_RSRC_TYPE:
	case ENDP_INIT_CTRL:
	case ENDP_INIT_CFG:
	case ENDP_INIT_NAT:
	case ENDP_INIT_HDR:
	case ENDP_INIT_HDR_EXT:
	case ENDP_INIT_HDR_METADATA_MASK:
	case ENDP_INIT_MODE:
	case ENDP_INIT_AGGR:
	case ENDP_INIT_HOL_BLOCK_EN:
	case ENDP_INIT_HOL_BLOCK_TIMER:
	case ENDP_INIT_DEAGGR:
	case ENDP_INIT_RSRC_GRP:
	case ENDP_INIT_SEQ:
	case ENDP_STATUS:
	case IPA_IRQ_STTS:
	case IPA_IRQ_EN:
	case IPA_IRQ_CLR:
	case IPA_IRQ_UC:
	case IRQ_SUSPEND_INFO:
		return true;	/* These should be defined for all versions */

	default:
		return false;
	}
}

const struct reg *ipa_reg(struct ipa *ipa, enum ipa_reg_id reg_id)
{
	if (WARN(!ipa_reg_id_valid(ipa, reg_id), "invalid reg %u\n", reg_id))
		return NULL;

	return reg(ipa->regs, reg_id);
}

static const struct regs *ipa_regs(enum ipa_version version)
{
	switch (version) {
	case IPA_VERSION_3_1:
		return &ipa_regs_v3_1;
	case IPA_VERSION_3_5_1:
		return &ipa_regs_v3_5_1;
	case IPA_VERSION_4_2:
		return &ipa_regs_v4_2;
	case IPA_VERSION_4_5:
		return &ipa_regs_v4_5;
	case IPA_VERSION_4_7:
		return &ipa_regs_v4_7;
	case IPA_VERSION_4_9:
		return &ipa_regs_v4_9;
	case IPA_VERSION_4_11:
		return &ipa_regs_v4_11;
	case IPA_VERSION_5_0:
		return &ipa_regs_v5_0;
	case IPA_VERSION_5_5:
		return &ipa_regs_v5_5;
	default:
		return NULL;
	}
}

int ipa_reg_init(struct ipa *ipa, struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct regs *regs;
	struct resource *res;

	regs = ipa_regs(ipa->version);
	if (!regs)
		return -EINVAL;

	if (WARN_ON(regs->reg_count > IPA_REG_ID_COUNT))
		return -EINVAL;

	/* Setup IPA register memory  */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ipa-reg");
	if (!res) {
		dev_err(dev, "DT error getting \"ipa-reg\" memory property\n");
		return -ENODEV;
	}

	ipa->reg_virt = ioremap(res->start, resource_size(res));
	if (!ipa->reg_virt) {
		dev_err(dev, "unable to remap \"ipa-reg\" memory\n");
		return -ENOMEM;
	}
	ipa->regs = regs;

	return 0;
}

void ipa_reg_exit(struct ipa *ipa)
{
	iounmap(ipa->reg_virt);
}
