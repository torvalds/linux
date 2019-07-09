/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef __DPU_CORE_IRQ_H__
#define __DPU_CORE_IRQ_H__

#include "dpu_kms.h"
#include "dpu_hw_interrupts.h"

/**
 * dpu_core_irq_preinstall - perform pre-installation of core IRQ handler
 * @dpu_kms:		DPU handle
 * @return:		none
 */
void dpu_core_irq_preinstall(struct dpu_kms *dpu_kms);

/**
 * dpu_core_irq_uninstall - uninstall core IRQ handler
 * @dpu_kms:		DPU handle
 * @return:		none
 */
void dpu_core_irq_uninstall(struct dpu_kms *dpu_kms);

/**
 * dpu_core_irq - core IRQ handler
 * @dpu_kms:		DPU handle
 * @return:		interrupt handling status
 */
irqreturn_t dpu_core_irq(struct dpu_kms *dpu_kms);

/**
 * dpu_core_irq_idx_lookup - IRQ helper function for lookup irq_idx from HW
 *                      interrupt mapping table.
 * @dpu_kms:		DPU handle
 * @intr_type:		DPU HW interrupt type for lookup
 * @instance_idx:	DPU HW block instance defined in dpu_hw_mdss.h
 * @return:		irq_idx or -EINVAL when fail to lookup
 */
int dpu_core_irq_idx_lookup(
		struct dpu_kms *dpu_kms,
		enum dpu_intr_type intr_type,
		uint32_t instance_idx);

/**
 * dpu_core_irq_enable - IRQ helper function for enabling one or more IRQs
 * @dpu_kms:		DPU handle
 * @irq_idxs:		Array of irq index
 * @irq_count:		Number of irq_idx provided in the array
 * @return:		0 for success enabling IRQ, otherwise failure
 *
 * This function increments count on each enable and decrements on each
 * disable.  Interrupts is enabled if count is 0 before increment.
 */
int dpu_core_irq_enable(
		struct dpu_kms *dpu_kms,
		int *irq_idxs,
		uint32_t irq_count);

/**
 * dpu_core_irq_disable - IRQ helper function for disabling one of more IRQs
 * @dpu_kms:		DPU handle
 * @irq_idxs:		Array of irq index
 * @irq_count:		Number of irq_idx provided in the array
 * @return:		0 for success disabling IRQ, otherwise failure
 *
 * This function increments count on each enable and decrements on each
 * disable.  Interrupts is disabled if count is 0 after decrement.
 */
int dpu_core_irq_disable(
		struct dpu_kms *dpu_kms,
		int *irq_idxs,
		uint32_t irq_count);

/**
 * dpu_core_irq_read - IRQ helper function for reading IRQ status
 * @dpu_kms:		DPU handle
 * @irq_idx:		irq index
 * @clear:		True to clear the irq after read
 * @return:		non-zero if irq detected; otherwise no irq detected
 */
u32 dpu_core_irq_read(
		struct dpu_kms *dpu_kms,
		int irq_idx,
		bool clear);

/**
 * dpu_core_irq_register_callback - For registering callback function on IRQ
 *                             interrupt
 * @dpu_kms:		DPU handle
 * @irq_idx:		irq index
 * @irq_cb:		IRQ callback structure, containing callback function
 *			and argument. Passing NULL for irq_cb will unregister
 *			the callback for the given irq_idx
 *			This must exist until un-registration.
 * @return:		0 for success registering callback, otherwise failure
 *
 * This function supports registration of multiple callbacks for each interrupt.
 */
int dpu_core_irq_register_callback(
		struct dpu_kms *dpu_kms,
		int irq_idx,
		struct dpu_irq_callback *irq_cb);

/**
 * dpu_core_irq_unregister_callback - For unregistering callback function on IRQ
 *                             interrupt
 * @dpu_kms:		DPU handle
 * @irq_idx:		irq index
 * @irq_cb:		IRQ callback structure, containing callback function
 *			and argument. Passing NULL for irq_cb will unregister
 *			the callback for the given irq_idx
 *			This must match with registration.
 * @return:		0 for success registering callback, otherwise failure
 *
 * This function supports registration of multiple callbacks for each interrupt.
 */
int dpu_core_irq_unregister_callback(
		struct dpu_kms *dpu_kms,
		int irq_idx,
		struct dpu_irq_callback *irq_cb);

/**
 * dpu_debugfs_core_irq_init - register core irq debugfs
 * @dpu_kms: pointer to kms
 * @parent: debugfs directory root
 */
void dpu_debugfs_core_irq_init(struct dpu_kms *dpu_kms,
		struct dentry *parent);

#endif /* __DPU_CORE_IRQ_H__ */
