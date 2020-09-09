/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_TRANSPORT_INTRN_H
#define ADF_TRANSPORT_INTRN_H

#include <linux/interrupt.h>
#include <linux/spinlock_types.h>
#include "adf_transport.h"

struct adf_etr_ring_debug_entry {
	char ring_name[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	struct dentry *debug;
};

struct adf_etr_ring_data {
	void *base_addr;
	atomic_t *inflights;
	adf_callback_fn callback;
	struct adf_etr_bank_data *bank;
	dma_addr_t dma_addr;
	struct adf_etr_ring_debug_entry *ring_debug;
	spinlock_t lock;	/* protects ring data struct */
	u16 head;
	u16 tail;
	u8 ring_number;
	u8 ring_size;
	u8 msg_size;
};

struct adf_etr_bank_data {
	struct adf_etr_ring_data rings[ADF_ETR_MAX_RINGS_PER_BANK];
	struct tasklet_struct resp_handler;
	void __iomem *csr_addr;
	u32 irq_coalesc_timer;
	u32 bank_number;
	u16 ring_mask;
	u16 irq_mask;
	spinlock_t lock;	/* protects bank data struct */
	struct adf_accel_dev *accel_dev;
	struct dentry *bank_debug_dir;
	struct dentry *bank_debug_cfg;
};

struct adf_etr_data {
	struct adf_etr_bank_data *banks;
	struct dentry *debug;
};

void adf_response_handler(uintptr_t bank_addr);
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
int adf_bank_debugfs_add(struct adf_etr_bank_data *bank);
void adf_bank_debugfs_rm(struct adf_etr_bank_data *bank);
int adf_ring_debugfs_add(struct adf_etr_ring_data *ring, const char *name);
void adf_ring_debugfs_rm(struct adf_etr_ring_data *ring);
#else
static inline int adf_bank_debugfs_add(struct adf_etr_bank_data *bank)
{
	return 0;
}

#define adf_bank_debugfs_rm(bank) do {} while (0)

static inline int adf_ring_debugfs_add(struct adf_etr_ring_data *ring,
				       const char *name)
{
	return 0;
}

#define adf_ring_debugfs_rm(ring) do {} while (0)
#endif
#endif
