// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2022 Linaro Ltd.
 */

#include <linux/io.h>

#include "ipa.h"
#include "ipa_reg.h"

/* Is this register valid and defined for the current IPA version? */
static bool ipa_reg_valid(struct ipa *ipa, enum ipa_reg_id reg_id)
{
	enum ipa_version version = ipa->version;
	bool valid;

	/* Check for bogus (out of range) register IDs */
	if ((u32)reg_id >= ipa->regs->reg_count)
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

	/* To be valid, it must be defined */

	return valid && ipa->regs->reg[reg_id];
}

const struct ipa_reg *ipa_reg(struct ipa *ipa, enum ipa_reg_id reg_id)
{
	if (WARN_ON(!ipa_reg_valid(ipa, reg_id)))
		return NULL;

	return ipa->regs->reg[reg_id];
}

static const struct ipa_regs *ipa_regs(enum ipa_version version)
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
	default:
		return NULL;
	}
}

int ipa_reg_init(struct ipa *ipa)
{
	struct device *dev = &ipa->pdev->dev;
	const struct ipa_regs *regs;
	struct resource *res;

	regs = ipa_regs(ipa->version);
	if (!regs)
		return -EINVAL;

	if (WARN_ON(regs->reg_count > IPA_REG_ID_COUNT))
		return -EINVAL;

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
	ipa->regs = regs;

	return 0;
}

void ipa_reg_exit(struct ipa *ipa)
{
	iounmap(ipa->reg_virt);
}
