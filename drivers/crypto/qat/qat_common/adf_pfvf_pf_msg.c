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
	struct pfvf_message msg = { .type = ADF_PF2VF_MSGTYPE_RESTARTING };
	int i, num_vfs = pci_num_vf(accel_to_pci_dev(accel_dev));

	for (i = 0, vf = accel_dev->pf.vf_info; i < num_vfs; i++, vf++) {
		if (vf->init && adf_send_pf2vf_msg(accel_dev, i, msg))
			dev_err(&GET_DEV(accel_dev),
				"Failed to send restarting msg to VF%d\n", i);
	}
}

int adf_pf_capabilities_msg_provider(struct adf_accel_dev *accel_dev,
				     u8 *buffer, u8 compat)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct capabilities_v2 caps_msg;

	caps_msg.ext_dc_caps = hw_data->extended_dc_capabilities;
	caps_msg.capabilities = hw_data->accel_capabilities_mask;

	caps_msg.hdr.version = ADF_PFVF_CAPABILITIES_V2_VERSION;
	caps_msg.hdr.payload_size =
			ADF_PFVF_BLKMSG_PAYLOAD_SIZE(struct capabilities_v2);

	memcpy(buffer, &caps_msg, sizeof(caps_msg));

	return 0;
}

int adf_pf_ring_to_svc_msg_provider(struct adf_accel_dev *accel_dev,
				    u8 *buffer, u8 compat)
{
	struct ring_to_svc_map_v1 rts_map_msg;

	rts_map_msg.map = accel_dev->hw_device->ring_to_svc_map;
	rts_map_msg.hdr.version = ADF_PFVF_RING_TO_SVC_VERSION;
	rts_map_msg.hdr.payload_size = ADF_PFVF_BLKMSG_PAYLOAD_SIZE(rts_map_msg);

	memcpy(buffer, &rts_map_msg, sizeof(rts_map_msg));

	return 0;
}
