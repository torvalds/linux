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

#ifndef __DPU_IRQ_H__
#define __DPU_IRQ_H__

#include <linux/kernel.h>
#include <linux/irqdomain.h>

#include "msm_kms.h"

/**
 * dpu_irq_controller - define MDSS level interrupt controller context
 * @enabled_mask:	enable status of MDSS level interrupt
 * @domain:		interrupt domain of this controller
 */
struct dpu_irq_controller {
	unsigned long enabled_mask;
	struct irq_domain *domain;
};

/**
 * dpu_irq_preinstall - perform pre-installation of MDSS IRQ handler
 * @kms:		pointer to kms context
 * @return:		none
 */
void dpu_irq_preinstall(struct msm_kms *kms);

/**
 * dpu_irq_postinstall - perform post-installation of MDSS IRQ handler
 * @kms:		pointer to kms context
 * @return:		0 if success; error code otherwise
 */
int dpu_irq_postinstall(struct msm_kms *kms);

/**
 * dpu_irq_uninstall - uninstall MDSS IRQ handler
 * @drm_dev:		pointer to kms context
 * @return:		none
 */
void dpu_irq_uninstall(struct msm_kms *kms);

/**
 * dpu_irq - MDSS level IRQ handler
 * @kms:		pointer to kms context
 * @return:		interrupt handling status
 */
irqreturn_t dpu_irq(struct msm_kms *kms);

#endif /* __DPU_IRQ_H__ */
