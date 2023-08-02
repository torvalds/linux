/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef __DPU_CORE_IRQ_H__
#define __DPU_CORE_IRQ_H__

#include "dpu_kms.h"
#include "dpu_hw_interrupts.h"

/**
 * dpu_core_irq_preinstall - perform pre-installation of core IRQ handler
 * @kms:		MSM KMS handle
 * @return:		none
 */
void dpu_core_irq_preinstall(struct msm_kms *kms);

/**
 * dpu_core_irq_uninstall - uninstall core IRQ handler
 * @kms:		MSM KMS handle
 * @return:		none
 */
void dpu_core_irq_uninstall(struct msm_kms *kms);

/**
 * dpu_core_irq - core IRQ handler
 * @kms:		MSM KMS handle
 * @return:		interrupt handling status
 */
irqreturn_t dpu_core_irq(struct msm_kms *kms);

/**
 * dpu_core_irq_read - IRQ helper function for reading IRQ status
 * @dpu_kms:		DPU handle
 * @irq_idx:		irq index
 * @return:		non-zero if irq detected; otherwise no irq detected
 */
u32 dpu_core_irq_read(
		struct dpu_kms *dpu_kms,
		int irq_idx);

/**
 * dpu_core_irq_register_callback - For registering callback function on IRQ
 *                             interrupt
 * @dpu_kms:		DPU handle
 * @irq_idx:		irq index
 * @irq_cb:		IRQ callback funcion.
 * @irq_arg:		IRQ callback argument.
 * @return:		0 for success registering callback, otherwise failure
 *
 * This function supports registration of multiple callbacks for each interrupt.
 */
int dpu_core_irq_register_callback(
		struct dpu_kms *dpu_kms,
		int irq_idx,
		void (*irq_cb)(void *arg),
		void *irq_arg);

/**
 * dpu_core_irq_unregister_callback - For unregistering callback function on IRQ
 *                             interrupt
 * @dpu_kms:		DPU handle
 * @irq_idx:		irq index
 * @return:		0 for success registering callback, otherwise failure
 *
 * This function supports registration of multiple callbacks for each interrupt.
 */
int dpu_core_irq_unregister_callback(
		struct dpu_kms *dpu_kms,
		int irq_idx);

/**
 * dpu_debugfs_core_irq_init - register core irq debugfs
 * @dpu_kms: pointer to kms
 * @parent: debugfs directory root
 */
void dpu_debugfs_core_irq_init(struct dpu_kms *dpu_kms,
		struct dentry *parent);

#endif /* __DPU_CORE_IRQ_H__ */
