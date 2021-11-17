// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2015 - 2021 Intel Corporation */
#include <linux/pci.h>
#include "adf_accel_devices.h"
#include "adf_pfvf_msg.h"
#include "adf_pfvf_pf_msg.h"
#include "adf_pfvf_pf_proto.h"

void adf_pf2vf_notify_restarting(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_vf_info *vf;
	u32 msg = (ADF_PF2VF_MSGORIGIN_SYSTEM |
		(ADF_PF2VF_MSGTYPE_RESTARTING << ADF_PF2VF_MSGTYPE_SHIFT));
	int i, num_vfs = pci_num_vf(accel_to_pci_dev(accel_dev));

	for (i = 0, vf = accel_dev->pf.vf_info; i < num_vfs; i++, vf++) {
		if (vf->init && adf_send_pf2vf_msg(accel_dev, i, msg))
			dev_err(&GET_DEV(accel_dev),
				"Failed to send restarting msg to VF%d\n", i);
	}
}
