// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2023 Linaro Ltd. */

#include <linux/platform_device.h>
#include <linux/io.h>

#include "gsi.h"
#include "reg.h"
#include "gsi_reg.h"

/* Is this register ID valid for the current GSI version? */
static bool gsi_reg_id_valid(struct gsi *gsi, enum gsi_reg_id reg_id)
{
	switch (reg_id) {
	case INTER_EE_SRC_CH_IRQ_MSK:
	case INTER_EE_SRC_EV_CH_IRQ_MSK:
		return gsi->version >= IPA_VERSION_3_5;

	case HW_PARAM_2:
		return gsi->version >= IPA_VERSION_3_5_1;

	case HW_PARAM_4:
		return gsi->version >= IPA_VERSION_5_0;

	case CH_C_CNTXT_0:
	case CH_C_CNTXT_1:
	case CH_C_CNTXT_2:
	case CH_C_CNTXT_3:
	case CH_C_QOS:
	case CH_C_SCRATCH_0:
	case CH_C_SCRATCH_1:
	case CH_C_SCRATCH_2:
	case CH_C_SCRATCH_3:
	case EV_CH_E_CNTXT_0:
	case EV_CH_E_CNTXT_1:
	case EV_CH_E_CNTXT_2:
	case EV_CH_E_CNTXT_3:
	case EV_CH_E_CNTXT_4:
	case EV_CH_E_CNTXT_8:
	case EV_CH_E_CNTXT_9:
	case EV_CH_E_CNTXT_10:
	case EV_CH_E_CNTXT_11:
	case EV_CH_E_CNTXT_12:
	case EV_CH_E_CNTXT_13:
	case EV_CH_E_SCRATCH_0:
	case EV_CH_E_SCRATCH_1:
	case CH_C_DOORBELL_0:
	case EV_CH_E_DOORBELL_0:
	case GSI_STATUS:
	case CH_CMD:
	case EV_CH_CMD:
	case GENERIC_CMD:
	case CNTXT_TYPE_IRQ:
	case CNTXT_TYPE_IRQ_MSK:
	case CNTXT_SRC_CH_IRQ:
	case CNTXT_SRC_CH_IRQ_MSK:
	case CNTXT_SRC_CH_IRQ_CLR:
	case CNTXT_SRC_EV_CH_IRQ:
	case CNTXT_SRC_EV_CH_IRQ_MSK:
	case CNTXT_SRC_EV_CH_IRQ_CLR:
	case CNTXT_SRC_IEOB_IRQ:
	case CNTXT_SRC_IEOB_IRQ_MSK:
	case CNTXT_SRC_IEOB_IRQ_CLR:
	case CNTXT_GLOB_IRQ_STTS:
	case CNTXT_GLOB_IRQ_EN:
	case CNTXT_GLOB_IRQ_CLR:
	case CNTXT_GSI_IRQ_STTS:
	case CNTXT_GSI_IRQ_EN:
	case CNTXT_GSI_IRQ_CLR:
	case CNTXT_INTSET:
	case ERROR_LOG:
	case ERROR_LOG_CLR:
	case CNTXT_SCRATCH_0:
		return true;

	default:
		return false;
	}
}

const struct reg *gsi_reg(struct gsi *gsi, enum gsi_reg_id reg_id)
{
	if (WARN(!gsi_reg_id_valid(gsi, reg_id), "invalid reg %u\n", reg_id))
		return NULL;

	return reg(gsi->regs, reg_id);
}

static const struct regs *gsi_regs(struct gsi *gsi)
{
	switch (gsi->version) {
	case IPA_VERSION_3_1:
		return &gsi_regs_v3_1;

	case IPA_VERSION_3_5_1:
		return &gsi_regs_v3_5_1;

	case IPA_VERSION_4_2:
		return &gsi_regs_v4_0;

	case IPA_VERSION_4_5:
	case IPA_VERSION_4_7:
		return &gsi_regs_v4_5;

	case IPA_VERSION_4_9:
		return &gsi_regs_v4_9;

	case IPA_VERSION_4_11:
		return &gsi_regs_v4_11;

	case IPA_VERSION_5_0:
	case IPA_VERSION_5_5:
		return &gsi_regs_v5_0;

	default:
		return NULL;
	}
}

/* Sets gsi->virt and I/O maps the "gsi" memory range for registers */
int gsi_reg_init(struct gsi *gsi, struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	resource_size_t size;

	/* Get GSI memory range and map it */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gsi");
	if (!res) {
		dev_err(dev, "DT error getting \"gsi\" memory property\n");
		return -ENODEV;
	}

	size = resource_size(res);
	if (res->start > U32_MAX || size > U32_MAX - res->start) {
		dev_err(dev, "DT memory resource \"gsi\" out of range\n");
		return -EINVAL;
	}

	gsi->regs = gsi_regs(gsi);
	if (!gsi->regs) {
		dev_err(dev, "unsupported IPA version %u (?)\n", gsi->version);
		return -EINVAL;
	}

	gsi->virt = ioremap(res->start, size);
	if (!gsi->virt) {
		dev_err(dev, "unable to remap \"gsi\" memory\n");
		return -ENOMEM;
	}

	return 0;
}

/* Inverse of gsi_reg_init() */
void gsi_reg_exit(struct gsi *gsi)
{
	iounmap(gsi->virt);
	gsi->virt = NULL;
	gsi->regs = NULL;
}
