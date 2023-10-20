// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */
#include "adf_common_drv.h"
#include "adf_gen4_ras.h"

static void adf_gen4_enable_ras(struct adf_accel_dev *accel_dev)
{
}

static void adf_gen4_disable_ras(struct adf_accel_dev *accel_dev)
{
}

static bool adf_gen4_handle_interrupt(struct adf_accel_dev *accel_dev,
				      bool *reset_required)
{
	return false;
}

void adf_gen4_init_ras_ops(struct adf_ras_ops *ras_ops)
{
	ras_ops->enable_ras_errors = adf_gen4_enable_ras;
	ras_ops->disable_ras_errors = adf_gen4_disable_ras;
	ras_ops->handle_interrupt = adf_gen4_handle_interrupt;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_ras_ops);
