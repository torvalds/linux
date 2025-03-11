/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef __DPU_CORE_IRQ_H__
#define __DPU_CORE_IRQ_H__

#include "dpu_kms.h"
#include "dpu_hw_interrupts.h"

void dpu_core_irq_preinstall(struct msm_kms *kms);

void dpu_core_irq_uninstall(struct msm_kms *kms);

irqreturn_t dpu_core_irq(struct msm_kms *kms);

u32 dpu_core_irq_read(
		struct dpu_kms *dpu_kms,
		unsigned int irq_idx);

int dpu_core_irq_register_callback(
		struct dpu_kms *dpu_kms,
		unsigned int irq_idx,
		void (*irq_cb)(void *arg),
		void *irq_arg);

int dpu_core_irq_unregister_callback(
		struct dpu_kms *dpu_kms,
		unsigned int irq_idx);

void dpu_debugfs_core_irq_init(struct dpu_kms *dpu_kms,
		struct dentry *parent);

#endif /* __DPU_CORE_IRQ_H__ */
