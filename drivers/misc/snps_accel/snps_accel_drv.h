/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _SNPS_ACCEL_DRV_H
#define _SNPS_ACCEL_DRV_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/snps_arcsync.h>

#include "snps_accel_mem.h"

/**
 * struct snps_accel_device - accelerator top level description
 */
struct snps_accel_device {
	struct list_head devs_list;
	resource_size_t shared_base;
	resource_size_t shared_size;
	u32 minor_count;
};

/**
 * struct snps_accel_ctrl_fn - ctrl unit driver functions needed by accelerator driver
 */
struct snps_accel_ctrl_fn {
	int (*set_interrupt_callback)(struct device *dev, u32 irq, intr_callback_t cb, void *data);
	int (*remove_interrupt_callback)(struct device *dev, u32 irq, void *data);
};

/**
 * struct snps_accel_ctrl - description of the control unit used by the accelerator driver
 */
struct snps_accel_ctrl {
	struct device *dev;
	struct snps_accel_ctrl_fn fn;
	u32 arcnet_id;
};

/**
 * struct snps_accel_app - accelerator application description structure
 */
struct snps_accel_app {
	struct cdev cdev;
	struct device *device;
	struct list_head link;
	dev_t devt;
	struct snps_accel_ctrl ctrl;
	s32 irq_num;
	atomic_t irq_event;
	wait_queue_head_t wait;
	resource_size_t shmem_base;
	resource_size_t shmem_size;
	resource_size_t ctrl_base;
	resource_size_t ctrl_size;
};

/**
 * struct snps_accel_file_priv - context for each driver client
 */
struct snps_accel_file_priv {
	struct kref ref;
	struct snps_accel_app *app;
	struct snps_accel_mem_ctx mem;
	u32 handled_irq_event;
};

static inline struct snps_accel_file_priv *
to_snps_accel_file_priv(struct snps_accel_mem_ctx *ctx)
{
	return container_of(ctx, struct snps_accel_file_priv, mem);
}

void snps_accel_file_priv_get(struct snps_accel_file_priv *fpriv);
void snps_accel_file_priv_put(struct snps_accel_file_priv *fpriv);

#endif /* _SNPS_ACCEL_DRV_H */
