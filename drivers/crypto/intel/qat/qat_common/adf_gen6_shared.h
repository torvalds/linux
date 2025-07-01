/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2025 Intel Corporation */
#ifndef ADF_GEN6_SHARED_H_
#define ADF_GEN6_SHARED_H_

struct adf_hw_csr_ops;
struct qat_migdev_ops;
struct adf_accel_dev;
struct adf_pfvf_ops;

void adf_gen6_init_pf_pfvf_ops(struct adf_pfvf_ops *pfvf_ops);
void adf_gen6_init_hw_csr_ops(struct adf_hw_csr_ops *csr_ops);
int adf_gen6_cfg_dev_init(struct adf_accel_dev *accel_dev);
int adf_gen6_comp_dev_config(struct adf_accel_dev *accel_dev);
int adf_gen6_no_dev_config(struct adf_accel_dev *accel_dev);
void adf_gen6_init_vf_mig_ops(struct qat_migdev_ops *vfmig_ops);
#endif/* ADF_GEN6_SHARED_H_ */
