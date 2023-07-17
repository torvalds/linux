/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2021 Intel Corporation */
#ifndef ADF_PFVF_PF_PROTO_H
#define ADF_PFVF_PF_PROTO_H

#include <linux/types.h>
#include "adf_accel_devices.h"

int adf_send_pf2vf_msg(struct adf_accel_dev *accel_dev, u8 vf_nr, struct pfvf_message msg);

int adf_enable_pf2vf_comms(struct adf_accel_dev *accel_dev);

#endif /* ADF_PFVF_PF_PROTO_H */
