/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/kthread.h>

#include "dpu_irq.h"
#include "dpu_core_irq.h"

irqreturn_t dpu_irq(struct msm_kms *kms)
{
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);

	return dpu_core_irq(dpu_kms);
}

void dpu_irq_preinstall(struct msm_kms *kms)
{
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);

	if (!dpu_kms->dev || !dpu_kms->dev->dev) {
		pr_err("invalid device handles\n");
		return;
	}

	dpu_core_irq_preinstall(dpu_kms);
}

int dpu_irq_postinstall(struct msm_kms *kms)
{
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);
	int rc;

	if (!kms) {
		DPU_ERROR("invalid parameters\n");
		return -EINVAL;
	}

	rc = dpu_core_irq_postinstall(dpu_kms);

	return rc;
}

void dpu_irq_uninstall(struct msm_kms *kms)
{
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);

	if (!kms) {
		DPU_ERROR("invalid parameters\n");
		return;
	}

	dpu_core_irq_uninstall(dpu_kms);
}
