/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018-2019 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _EFA_COM_H_
#define _EFA_COM_H_

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/semaphore.h>
#include <linux/sched.h>

#include <rdma/ib_verbs.h>

#include "efa_common_defs.h"
#include "efa_admin_defs.h"
#include "efa_admin_cmds_defs.h"
#include "efa_regs_defs.h"

#define EFA_MAX_HANDLERS 256

struct efa_com_admin_cq {
	struct efa_admin_acq_entry *entries;
	dma_addr_t dma_addr;
	spinlock_t lock; /* Protects ACQ */

	u16 cc; /* consumer counter */
	u8 phase;
};

struct efa_com_admin_sq {
	struct efa_admin_aq_entry *entries;
	dma_addr_t dma_addr;
	spinlock_t lock; /* Protects ASQ */

	u32 __iomem *db_addr;

	u16 cc; /* consumer counter */
	u16 pc; /* producer counter */
	u8 phase;

};

/* Don't use anything other than atomic64 */
struct efa_com_stats_admin {
	atomic64_t submitted_cmd;
	atomic64_t completed_cmd;
	atomic64_t no_completion;
};

enum {
	EFA_AQ_STATE_RUNNING_BIT = 0,
	EFA_AQ_STATE_POLLING_BIT = 1,
};

struct efa_com_admin_queue {
	void *dmadev;
	void *efa_dev;
	struct efa_comp_ctx *comp_ctx;
	u32 completion_timeout; /* usecs */
	u16 poll_interval; /* msecs */
	u16 depth;
	struct efa_com_admin_cq cq;
	struct efa_com_admin_sq sq;
	u16 msix_vector_idx;

	unsigned long state;

	/* Count the number of available admin commands */
	struct semaphore avail_cmds;

	struct efa_com_stats_admin stats;

	spinlock_t comp_ctx_lock; /* Protects completion context pool */
	u32 *comp_ctx_pool;
	u16 comp_ctx_pool_next;
};

struct efa_aenq_handlers;

struct efa_com_aenq {
	struct efa_admin_aenq_entry *entries;
	struct efa_aenq_handlers *aenq_handlers;
	dma_addr_t dma_addr;
	u32 cc; /* consumer counter */
	u16 msix_vector_idx;
	u16 depth;
	u8 phase;
};

struct efa_com_mmio_read {
	struct efa_admin_mmio_req_read_less_resp *read_resp;
	dma_addr_t read_resp_dma_addr;
	u16 seq_num;
	u16 mmio_read_timeout; /* usecs */
	/* serializes mmio reads */
	spinlock_t lock;
};

struct efa_com_dev {
	struct efa_com_admin_queue aq;
	struct efa_com_aenq aenq;
	u8 __iomem *reg_bar;
	void *dmadev;
	void *efa_dev;
	u32 supported_features;
	u32 dma_addr_bits;

	struct efa_com_mmio_read mmio_read;
};

typedef void (*efa_aenq_handler)(void *data,
	      struct efa_admin_aenq_entry *aenq_e);

/* Holds aenq handlers. Indexed by AENQ event group */
struct efa_aenq_handlers {
	efa_aenq_handler handlers[EFA_MAX_HANDLERS];
	efa_aenq_handler unimplemented_handler;
};

int efa_com_admin_init(struct efa_com_dev *edev,
		       struct efa_aenq_handlers *aenq_handlers);
void efa_com_admin_destroy(struct efa_com_dev *edev);
int efa_com_dev_reset(struct efa_com_dev *edev,
		      enum efa_regs_reset_reason_types reset_reason);
void efa_com_set_admin_polling_mode(struct efa_com_dev *edev, bool polling);
void efa_com_admin_q_comp_intr_handler(struct efa_com_dev *edev);
int efa_com_mmio_reg_read_init(struct efa_com_dev *edev);
void efa_com_mmio_reg_read_destroy(struct efa_com_dev *edev);

int efa_com_validate_version(struct efa_com_dev *edev);
int efa_com_get_dma_width(struct efa_com_dev *edev);

int efa_com_cmd_exec(struct efa_com_admin_queue *aq,
		     struct efa_admin_aq_entry *cmd,
		     size_t cmd_size,
		     struct efa_admin_acq_entry *comp,
		     size_t comp_size);
void efa_com_aenq_intr_handler(struct efa_com_dev *edev, void *data);

#endif /* _EFA_COM_H_ */
