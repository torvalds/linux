// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2025 Intel Corporation */
#include <linux/export.h>

#include "adf_gen4_config.h"
#include "adf_gen4_hw_csr_data.h"
#include "adf_gen4_pfvf.h"
#include "adf_gen4_vf_mig.h"
#include "adf_gen6_shared.h"

struct adf_accel_dev;
struct adf_pfvf_ops;
struct adf_hw_csr_ops;

/*
 * QAT GEN4 and GEN6 devices often differ in terms of supported features,
 * options and internal logic. However, some of the mechanisms and register
 * layout are shared between those two GENs. This file serves as an abstraction
 * layer that allows to use existing GEN4 implementation that is also
 * applicable to GEN6 without additional overhead and complexity.
 */
void adf_gen6_init_pf_pfvf_ops(struct adf_pfvf_ops *pfvf_ops)
{
	adf_gen4_init_pf_pfvf_ops(pfvf_ops);
}
EXPORT_SYMBOL_GPL(adf_gen6_init_pf_pfvf_ops);

void adf_gen6_init_hw_csr_ops(struct adf_hw_csr_ops *csr_ops)
{
	return adf_gen4_init_hw_csr_ops(csr_ops);
}
EXPORT_SYMBOL_GPL(adf_gen6_init_hw_csr_ops);

int adf_gen6_cfg_dev_init(struct adf_accel_dev *accel_dev)
{
	return adf_gen4_cfg_dev_init(accel_dev);
}
EXPORT_SYMBOL_GPL(adf_gen6_cfg_dev_init);

int adf_gen6_comp_dev_config(struct adf_accel_dev *accel_dev)
{
	return adf_comp_dev_config(accel_dev);
}
EXPORT_SYMBOL_GPL(adf_gen6_comp_dev_config);

int adf_gen6_no_dev_config(struct adf_accel_dev *accel_dev)
{
	return adf_no_dev_config(accel_dev);
}
EXPORT_SYMBOL_GPL(adf_gen6_no_dev_config);

void adf_gen6_init_vf_mig_ops(struct qat_migdev_ops *vfmig_ops)
{
	adf_gen4_init_vf_mig_ops(vfmig_ops);
}
EXPORT_SYMBOL_GPL(adf_gen6_init_vf_mig_ops);
