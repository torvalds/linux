/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2021 Intel Corporation */
#ifndef ADF_GEN2_PFVF_H
#define ADF_GEN2_PFVF_H

#include <linux/types.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"

#define ADF_GEN2_ERRSOU3 (0x3A000 + 0x0C)
#define ADF_GEN2_ERRSOU5 (0x3A000 + 0xD8)
#define ADF_GEN2_ERRMSK3 (0x3A000 + 0x1C)
#define ADF_GEN2_ERRMSK5 (0x3A000 + 0xDC)

#if defined(CONFIG_PCI_IOV)
void adf_gen2_init_pf_pfvf_ops(struct adf_pfvf_ops *pfvf_ops);
void adf_gen2_init_vf_pfvf_ops(struct adf_pfvf_ops *pfvf_ops);
#else
static inline void adf_gen2_init_pf_pfvf_ops(struct adf_pfvf_ops *pfvf_ops)
{
	pfvf_ops->enable_comms = adf_pfvf_comms_disabled;
}

static inline void adf_gen2_init_vf_pfvf_ops(struct adf_pfvf_ops *pfvf_ops)
{
	pfvf_ops->enable_comms = adf_pfvf_comms_disabled;
}
#endif

#endif /* ADF_GEN2_PFVF_H */
