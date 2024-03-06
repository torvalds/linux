// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/vdpa.h>
#include <linux/virtio_pci_modern.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>
#include <linux/pds/pds_adminq.h>
#include <linux/pds/pds_auxbus.h>

#include "vdpa_dev.h"
#include "aux_drv.h"
#include "cmds.h"

int pds_vdpa_init_hw(struct pds_vdpa_device *pdsv)
{
	struct pds_auxiliary_dev *padev = pdsv->vdpa_aux->padev;
	struct device *dev = &padev->aux_dev.dev;
	union pds_core_adminq_cmd cmd = {
		.vdpa_init.opcode = PDS_VDPA_CMD_INIT,
		.vdpa_init.vdpa_index = pdsv->vdpa_index,
		.vdpa_init.vf_id = cpu_to_le16(pdsv->vdpa_aux->vf_id),
	};
	union pds_core_adminq_comp comp = {};
	int err;

	/* Initialize the vdpa/virtio device */
	err = pds_client_adminq_cmd(padev, &cmd, sizeof(cmd.vdpa_init),
				    &comp, 0);
	if (err)
		dev_dbg(dev, "Failed to init hw, status %d: %pe\n",
			comp.status, ERR_PTR(err));

	return err;
}

int pds_vdpa_cmd_reset(struct pds_vdpa_device *pdsv)
{
	struct pds_auxiliary_dev *padev = pdsv->vdpa_aux->padev;
	struct device *dev = &padev->aux_dev.dev;
	union pds_core_adminq_cmd cmd = {
		.vdpa.opcode = PDS_VDPA_CMD_RESET,
		.vdpa.vdpa_index = pdsv->vdpa_index,
		.vdpa.vf_id = cpu_to_le16(pdsv->vdpa_aux->vf_id),
	};
	union pds_core_adminq_comp comp = {};
	int err;

	err = pds_client_adminq_cmd(padev, &cmd, sizeof(cmd.vdpa), &comp, 0);
	if (err)
		dev_dbg(dev, "Failed to reset hw, status %d: %pe\n",
			comp.status, ERR_PTR(err));

	return err;
}

int pds_vdpa_cmd_set_status(struct pds_vdpa_device *pdsv, u8 status)
{
	struct pds_auxiliary_dev *padev = pdsv->vdpa_aux->padev;
	struct device *dev = &padev->aux_dev.dev;
	union pds_core_adminq_cmd cmd = {
		.vdpa_status.opcode = PDS_VDPA_CMD_STATUS_UPDATE,
		.vdpa_status.vdpa_index = pdsv->vdpa_index,
		.vdpa_status.vf_id = cpu_to_le16(pdsv->vdpa_aux->vf_id),
		.vdpa_status.status = status,
	};
	union pds_core_adminq_comp comp = {};
	int err;

	err = pds_client_adminq_cmd(padev, &cmd, sizeof(cmd.vdpa_status), &comp, 0);
	if (err)
		dev_dbg(dev, "Failed to set status to %#x, error status %d: %pe\n",
			status, comp.status, ERR_PTR(err));

	return err;
}

int pds_vdpa_cmd_set_mac(struct pds_vdpa_device *pdsv, u8 *mac)
{
	struct pds_auxiliary_dev *padev = pdsv->vdpa_aux->padev;
	struct device *dev = &padev->aux_dev.dev;
	union pds_core_adminq_cmd cmd = {
		.vdpa_setattr.opcode = PDS_VDPA_CMD_SET_ATTR,
		.vdpa_setattr.vdpa_index = pdsv->vdpa_index,
		.vdpa_setattr.vf_id = cpu_to_le16(pdsv->vdpa_aux->vf_id),
		.vdpa_setattr.attr = PDS_VDPA_ATTR_MAC,
	};
	union pds_core_adminq_comp comp = {};
	int err;

	ether_addr_copy(cmd.vdpa_setattr.mac, mac);
	err = pds_client_adminq_cmd(padev, &cmd, sizeof(cmd.vdpa_setattr),
				    &comp, 0);
	if (err)
		dev_dbg(dev, "Failed to set mac address %pM, status %d: %pe\n",
			mac, comp.status, ERR_PTR(err));

	return err;
}

int pds_vdpa_cmd_set_max_vq_pairs(struct pds_vdpa_device *pdsv, u16 max_vqp)
{
	struct pds_auxiliary_dev *padev = pdsv->vdpa_aux->padev;
	struct device *dev = &padev->aux_dev.dev;
	union pds_core_adminq_cmd cmd = {
		.vdpa_setattr.opcode = PDS_VDPA_CMD_SET_ATTR,
		.vdpa_setattr.vdpa_index = pdsv->vdpa_index,
		.vdpa_setattr.vf_id = cpu_to_le16(pdsv->vdpa_aux->vf_id),
		.vdpa_setattr.attr = PDS_VDPA_ATTR_MAX_VQ_PAIRS,
		.vdpa_setattr.max_vq_pairs = cpu_to_le16(max_vqp),
	};
	union pds_core_adminq_comp comp = {};
	int err;

	err = pds_client_adminq_cmd(padev, &cmd, sizeof(cmd.vdpa_setattr),
				    &comp, 0);
	if (err)
		dev_dbg(dev, "Failed to set max vq pairs %u, status %d: %pe\n",
			max_vqp, comp.status, ERR_PTR(err));

	return err;
}

int pds_vdpa_cmd_init_vq(struct pds_vdpa_device *pdsv, u16 qid, u16 invert_idx,
			 struct pds_vdpa_vq_info *vq_info)
{
	struct pds_auxiliary_dev *padev = pdsv->vdpa_aux->padev;
	struct device *dev = &padev->aux_dev.dev;
	union pds_core_adminq_cmd cmd = {
		.vdpa_vq_init.opcode = PDS_VDPA_CMD_VQ_INIT,
		.vdpa_vq_init.vdpa_index = pdsv->vdpa_index,
		.vdpa_vq_init.vf_id = cpu_to_le16(pdsv->vdpa_aux->vf_id),
		.vdpa_vq_init.qid = cpu_to_le16(qid),
		.vdpa_vq_init.len = cpu_to_le16(ilog2(vq_info->q_len)),
		.vdpa_vq_init.desc_addr = cpu_to_le64(vq_info->desc_addr),
		.vdpa_vq_init.avail_addr = cpu_to_le64(vq_info->avail_addr),
		.vdpa_vq_init.used_addr = cpu_to_le64(vq_info->used_addr),
		.vdpa_vq_init.intr_index = cpu_to_le16(qid),
		.vdpa_vq_init.avail_index = cpu_to_le16(vq_info->avail_idx ^ invert_idx),
		.vdpa_vq_init.used_index = cpu_to_le16(vq_info->used_idx ^ invert_idx),
	};
	union pds_core_adminq_comp comp = {};
	int err;

	dev_dbg(dev, "%s: qid %d len %d desc_addr %#llx avail_addr %#llx used_addr %#llx\n",
		__func__, qid, ilog2(vq_info->q_len),
		vq_info->desc_addr, vq_info->avail_addr, vq_info->used_addr);

	err = pds_client_adminq_cmd(padev, &cmd, sizeof(cmd.vdpa_vq_init),
				    &comp, 0);
	if (err)
		dev_dbg(dev, "Failed to init vq %d, status %d: %pe\n",
			qid, comp.status, ERR_PTR(err));

	return err;
}

int pds_vdpa_cmd_reset_vq(struct pds_vdpa_device *pdsv, u16 qid, u16 invert_idx,
			  struct pds_vdpa_vq_info *vq_info)
{
	struct pds_auxiliary_dev *padev = pdsv->vdpa_aux->padev;
	struct device *dev = &padev->aux_dev.dev;
	union pds_core_adminq_cmd cmd = {
		.vdpa_vq_reset.opcode = PDS_VDPA_CMD_VQ_RESET,
		.vdpa_vq_reset.vdpa_index = pdsv->vdpa_index,
		.vdpa_vq_reset.vf_id = cpu_to_le16(pdsv->vdpa_aux->vf_id),
		.vdpa_vq_reset.qid = cpu_to_le16(qid),
	};
	union pds_core_adminq_comp comp = {};
	int err;

	err = pds_client_adminq_cmd(padev, &cmd, sizeof(cmd.vdpa_vq_reset),
				    &comp, 0);
	if (err) {
		dev_dbg(dev, "Failed to reset vq %d, status %d: %pe\n",
			qid, comp.status, ERR_PTR(err));
		return err;
	}

	vq_info->avail_idx = le16_to_cpu(comp.vdpa_vq_reset.avail_index) ^ invert_idx;
	vq_info->used_idx = le16_to_cpu(comp.vdpa_vq_reset.used_index) ^ invert_idx;

	return 0;
}
