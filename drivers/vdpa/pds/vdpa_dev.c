// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/pci.h>
#include <linux/vdpa.h>
#include <uapi/linux/vdpa.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>
#include <linux/pds/pds_adminq.h>
#include <linux/pds/pds_auxbus.h>

#include "vdpa_dev.h"
#include "aux_drv.h"

static struct virtio_device_id pds_vdpa_id_table[] = {
	{VIRTIO_ID_NET, VIRTIO_DEV_ANY_ID},
	{0},
};

static int pds_vdpa_dev_add(struct vdpa_mgmt_dev *mdev, const char *name,
			    const struct vdpa_dev_set_config *add_config)
{
	return -EOPNOTSUPP;
}

static void pds_vdpa_dev_del(struct vdpa_mgmt_dev *mdev,
			     struct vdpa_device *vdpa_dev)
{
}

static const struct vdpa_mgmtdev_ops pds_vdpa_mgmt_dev_ops = {
	.dev_add = pds_vdpa_dev_add,
	.dev_del = pds_vdpa_dev_del
};

int pds_vdpa_get_mgmt_info(struct pds_vdpa_aux *vdpa_aux)
{
	union pds_core_adminq_cmd cmd = {
		.vdpa_ident.opcode = PDS_VDPA_CMD_IDENT,
		.vdpa_ident.vf_id = cpu_to_le16(vdpa_aux->vf_id),
	};
	union pds_core_adminq_comp comp = {};
	struct vdpa_mgmt_dev *mgmt;
	struct pci_dev *pf_pdev;
	struct device *pf_dev;
	struct pci_dev *pdev;
	dma_addr_t ident_pa;
	struct device *dev;
	u16 dev_intrs;
	u16 max_vqs;
	int err;

	dev = &vdpa_aux->padev->aux_dev.dev;
	pdev = vdpa_aux->padev->vf_pdev;
	mgmt = &vdpa_aux->vdpa_mdev;

	/* Get resource info through the PF's adminq.  It is a block of info,
	 * so we need to map some memory for PF to make available to the
	 * firmware for writing the data.
	 */
	pf_pdev = pci_physfn(vdpa_aux->padev->vf_pdev);
	pf_dev = &pf_pdev->dev;
	ident_pa = dma_map_single(pf_dev, &vdpa_aux->ident,
				  sizeof(vdpa_aux->ident), DMA_FROM_DEVICE);
	if (dma_mapping_error(pf_dev, ident_pa)) {
		dev_err(dev, "Failed to map ident space\n");
		return -ENOMEM;
	}

	cmd.vdpa_ident.ident_pa = cpu_to_le64(ident_pa);
	cmd.vdpa_ident.len = cpu_to_le32(sizeof(vdpa_aux->ident));
	err = pds_client_adminq_cmd(vdpa_aux->padev, &cmd,
				    sizeof(cmd.vdpa_ident), &comp, 0);
	dma_unmap_single(pf_dev, ident_pa,
			 sizeof(vdpa_aux->ident), DMA_FROM_DEVICE);
	if (err) {
		dev_err(dev, "Failed to ident hw, status %d: %pe\n",
			comp.status, ERR_PTR(err));
		return err;
	}

	max_vqs = le16_to_cpu(vdpa_aux->ident.max_vqs);
	dev_intrs = pci_msix_vec_count(pdev);
	dev_dbg(dev, "ident.max_vqs %d dev_intrs %d\n", max_vqs, dev_intrs);

	max_vqs = min_t(u16, dev_intrs, max_vqs);
	mgmt->max_supported_vqs = min_t(u16, PDS_VDPA_MAX_QUEUES, max_vqs);
	vdpa_aux->nintrs = mgmt->max_supported_vqs;

	mgmt->ops = &pds_vdpa_mgmt_dev_ops;
	mgmt->id_table = pds_vdpa_id_table;
	mgmt->device = dev;
	mgmt->supported_features = le64_to_cpu(vdpa_aux->ident.hw_features);
	mgmt->config_attr_mask = BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MACADDR);
	mgmt->config_attr_mask |= BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MAX_VQP);

	err = pci_alloc_irq_vectors(pdev, vdpa_aux->nintrs, vdpa_aux->nintrs,
				    PCI_IRQ_MSIX);
	if (err < 0) {
		dev_err(dev, "Couldn't get %d msix vectors: %pe\n",
			vdpa_aux->nintrs, ERR_PTR(err));
		return err;
	}
	vdpa_aux->nintrs = err;

	return 0;
}
