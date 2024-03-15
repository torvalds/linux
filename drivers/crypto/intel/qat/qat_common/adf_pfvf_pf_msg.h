/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2021 Intel Corporation */
#ifndef ADF_PFVF_PF_MSG_H
#define ADF_PFVF_PF_MSG_H

#include "adf_accel_devices.h"

#if defined(CONFIG_PCI_IOV)
void adf_pf2vf_notify_restarting(struct adf_accel_dev *accel_dev);
void adf_pf2vf_wait_for_restarting_complete(struct adf_accel_dev *accel_dev);
void adf_pf2vf_notify_restarted(struct adf_accel_dev *accel_dev);
void adf_pf2vf_notify_fatal_error(struct adf_accel_dev *accel_dev);
#else
static inline void adf_pf2vf_notify_restarting(struct adf_accel_dev *accel_dev)
{
}

static inline void adf_pf2vf_wait_for_restarting_complete(struct adf_accel_dev *accel_dev)
{
}

static inline void adf_pf2vf_notify_restarted(struct adf_accel_dev *accel_dev)
{
}

static inline void adf_pf2vf_notify_fatal_error(struct adf_accel_dev *accel_dev)
{
}
#endif

typedef int (*adf_pf2vf_blkmsg_provider)(struct adf_accel_dev *accel_dev,
					 u8 *buffer, u8 compat);

int adf_pf_capabilities_msg_provider(struct adf_accel_dev *accel_dev,
				     u8 *buffer, u8 comapt);
int adf_pf_ring_to_svc_msg_provider(struct adf_accel_dev *accel_dev,
				    u8 *buffer, u8 comapt);

#endif /* ADF_PFVF_PF_MSG_H */
