/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2022 Intel Corporation */
#ifndef _QAT_COMPRESSION_H_
#define _QAT_COMPRESSION_H_

#include <linux/list.h>
#include <linux/types.h>
#include "adf_accel_devices.h"
#include "qat_algs_send.h"

#define QAT_COMP_MAX_SKID 4096

struct qat_compression_instance {
	struct adf_etr_ring_data *dc_tx;
	struct adf_etr_ring_data *dc_rx;
	struct adf_accel_dev *accel_dev;
	struct list_head list;
	unsigned long state;
	int id;
	atomic_t refctr;
	struct qat_instance_backlog backlog;
	struct adf_dc_data *dc_data;
	void (*build_deflate_ctx)(void *ctx);
};

static inline bool adf_hw_dev_has_compression(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	u32 mask = ~hw_device->accel_capabilities_mask;

	if (mask & ADF_ACCEL_CAPABILITIES_COMPRESSION)
		return false;

	return true;
}

#endif
