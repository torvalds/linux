/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2021 Intel Corporation */
#ifndef ADF_GEN4_PFVF_H
#define ADF_GEN4_PFVF_H

#include "adf_accel_devices.h"

#ifdef CONFIG_PCI_IOV
void adf_gen4_init_pf_pfvf_ops(struct adf_pfvf_ops *pfvf_ops);
#else
static inline void adf_gen4_init_pf_pfvf_ops(struct adf_pfvf_ops *pfvf_ops)
{
	pfvf_ops->enable_comms = adf_pfvf_comms_disabled;
}
#endif

#endif /* ADF_GEN4_PFVF_H */
