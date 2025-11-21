/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#ifndef _IONIC_API_H_
#define _IONIC_API_H_

#include <linux/auxiliary_bus.h>
#include "ionic_if.h"
#include "ionic_regs.h"

/**
 * struct ionic_aux_dev - Auxiliary device information
 * @lif:        Logical interface
 * @idx:        Index identifier
 * @adev:       Auxiliary device
 */
struct ionic_aux_dev {
	struct ionic_lif *lif;
	int idx;
	struct auxiliary_device adev;
};

/**
 * struct ionic_admin_ctx - Admin command context
 * @work:       Work completion wait queue element
 * @cmd:        Admin command (64B) to be copied to the queue
 * @comp:       Admin completion (16B) copied from the queue
 */
struct ionic_admin_ctx {
	struct completion work;
	union ionic_adminq_cmd cmd;
	union ionic_adminq_comp comp;
};

#define IONIC_INTR_INDEX_NOT_ASSIGNED	-1
#define IONIC_INTR_NAME_MAX_SZ		32

/**
 * struct ionic_intr_info - Interrupt information
 * @name:          Name identifier
 * @rearm_count:   Interrupt rearm count
 * @index:         Interrupt index position
 * @vector:        Interrupt number
 * @dim_coal_hw:   Interrupt coalesce value in hardware units
 * @affinity_mask: CPU affinity mask
 * @aff_notify:    context for notification of IRQ affinity changes
 */
struct ionic_intr_info {
	char name[IONIC_INTR_NAME_MAX_SZ];
	u64 rearm_count;
	unsigned int index;
	unsigned int vector;
	u32 dim_coal_hw;
	cpumask_var_t *affinity_mask;
	struct irq_affinity_notify aff_notify;
};

/**
 * ionic_adminq_post_wait - Post an admin command and wait for response
 * @lif:        Logical interface
 * @ctx:        API admin command context
 *
 * Post the command to an admin queue in the ethernet driver.  If this command
 * succeeds, then the command has been posted, but that does not indicate a
 * completion.  If this command returns success, then the completion callback
 * will eventually be called.
 *
 * Return: zero or negative error status
 */
int ionic_adminq_post_wait(struct ionic_lif *lif, struct ionic_admin_ctx *ctx);

/**
 * ionic_error_to_errno - Transform ionic_if errors to os errno
 * @code:       Ionic error number
 *
 * Return:      Negative OS error number or zero
 */
int ionic_error_to_errno(enum ionic_status_code code);

/**
 * ionic_request_rdma_reset - request reset or disable the device or lif
 * @lif:        Logical interface
 *
 * The reset is triggered asynchronously. It will wait until reset request
 * completes or times out.
 */
void ionic_request_rdma_reset(struct ionic_lif *lif);

/**
 * ionic_intr_alloc - Reserve a device interrupt
 * @lif:        Logical interface
 * @intr:       Reserved ionic interrupt structure
 *
 * Reserve an interrupt index and get irq number for that index.
 *
 * Return: zero or negative error status
 */
int ionic_intr_alloc(struct ionic_lif *lif, struct ionic_intr_info *intr);

/**
 * ionic_intr_free - Release a device interrupt index
 * @lif:        Logical interface
 * @intr:       Interrupt index
 *
 * Mark the interrupt index unused so that it can be reserved again.
 */
void ionic_intr_free(struct ionic_lif *lif, int intr);

/**
 * ionic_get_cmb - Reserve cmb pages
 * @lif:         Logical interface
 * @pgid:        First page index
 * @pgaddr:      First page bus addr (contiguous)
 * @order:       Log base two number of pages (PAGE_SIZE)
 * @stride_log2: Size of stride to determine CMB pool
 * @expdb:       Will be set to true if this CMB region has expdb enabled
 *
 * Return: zero or negative error status
 */
int ionic_get_cmb(struct ionic_lif *lif, u32 *pgid, phys_addr_t *pgaddr,
		  int order, u8 stride_log2, bool *expdb);

/**
 * ionic_put_cmb - Release cmb pages
 * @lif:        Logical interface
 * @pgid:       First page index
 * @order:      Log base two number of pages (PAGE_SIZE)
 */
void ionic_put_cmb(struct ionic_lif *lif, u32 pgid, int order);

#endif /* _IONIC_API_H_ */
