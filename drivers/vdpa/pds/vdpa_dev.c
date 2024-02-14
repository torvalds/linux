// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/pci.h>
#include <linux/vdpa.h>
#include <uapi/linux/vdpa.h>
#include <linux/virtio_pci_modern.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>
#include <linux/pds/pds_adminq.h>
#include <linux/pds/pds_auxbus.h>

#include "vdpa_dev.h"
#include "aux_drv.h"
#include "cmds.h"
#include "debugfs.h"

static u64 pds_vdpa_get_driver_features(struct vdpa_device *vdpa_dev);

static struct pds_vdpa_device *vdpa_to_pdsv(struct vdpa_device *vdpa_dev)
{
	return container_of(vdpa_dev, struct pds_vdpa_device, vdpa_dev);
}

static int pds_vdpa_notify_handler(struct notifier_block *nb,
				   unsigned long ecode,
				   void *data)
{
	struct pds_vdpa_device *pdsv = container_of(nb, struct pds_vdpa_device, nb);
	struct device *dev = &pdsv->vdpa_aux->padev->aux_dev.dev;

	dev_dbg(dev, "%s: event code %lu\n", __func__, ecode);

	if (ecode == PDS_EVENT_RESET || ecode == PDS_EVENT_LINK_CHANGE) {
		if (pdsv->config_cb.callback)
			pdsv->config_cb.callback(pdsv->config_cb.private);
	}

	return 0;
}

static int pds_vdpa_register_event_handler(struct pds_vdpa_device *pdsv)
{
	struct device *dev = &pdsv->vdpa_aux->padev->aux_dev.dev;
	struct notifier_block *nb = &pdsv->nb;
	int err;

	if (!nb->notifier_call) {
		nb->notifier_call = pds_vdpa_notify_handler;
		err = pdsc_register_notify(nb);
		if (err) {
			nb->notifier_call = NULL;
			dev_err(dev, "failed to register pds event handler: %ps\n",
				ERR_PTR(err));
			return -EINVAL;
		}
		dev_dbg(dev, "pds event handler registered\n");
	}

	return 0;
}

static void pds_vdpa_unregister_event_handler(struct pds_vdpa_device *pdsv)
{
	if (pdsv->nb.notifier_call) {
		pdsc_unregister_notify(&pdsv->nb);
		pdsv->nb.notifier_call = NULL;
	}
}

static int pds_vdpa_set_vq_address(struct vdpa_device *vdpa_dev, u16 qid,
				   u64 desc_addr, u64 driver_addr, u64 device_addr)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);

	pdsv->vqs[qid].desc_addr = desc_addr;
	pdsv->vqs[qid].avail_addr = driver_addr;
	pdsv->vqs[qid].used_addr = device_addr;

	return 0;
}

static void pds_vdpa_set_vq_num(struct vdpa_device *vdpa_dev, u16 qid, u32 num)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);

	pdsv->vqs[qid].q_len = num;
}

static void pds_vdpa_kick_vq(struct vdpa_device *vdpa_dev, u16 qid)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);

	iowrite16(qid, pdsv->vqs[qid].notify);
}

static void pds_vdpa_set_vq_cb(struct vdpa_device *vdpa_dev, u16 qid,
			       struct vdpa_callback *cb)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);

	pdsv->vqs[qid].event_cb = *cb;
}

static irqreturn_t pds_vdpa_isr(int irq, void *data)
{
	struct pds_vdpa_vq_info *vq;

	vq = data;
	if (vq->event_cb.callback)
		vq->event_cb.callback(vq->event_cb.private);

	return IRQ_HANDLED;
}

static void pds_vdpa_release_irq(struct pds_vdpa_device *pdsv, int qid)
{
	if (pdsv->vqs[qid].irq == VIRTIO_MSI_NO_VECTOR)
		return;

	free_irq(pdsv->vqs[qid].irq, &pdsv->vqs[qid]);
	pdsv->vqs[qid].irq = VIRTIO_MSI_NO_VECTOR;
}

static void pds_vdpa_set_vq_ready(struct vdpa_device *vdpa_dev, u16 qid, bool ready)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);
	struct device *dev = &pdsv->vdpa_dev.dev;
	u64 driver_features;
	u16 invert_idx = 0;
	int err;

	dev_dbg(dev, "%s: qid %d ready %d => %d\n",
		__func__, qid, pdsv->vqs[qid].ready, ready);
	if (ready == pdsv->vqs[qid].ready)
		return;

	driver_features = pds_vdpa_get_driver_features(vdpa_dev);
	if (driver_features & BIT_ULL(VIRTIO_F_RING_PACKED))
		invert_idx = PDS_VDPA_PACKED_INVERT_IDX;

	if (ready) {
		/* Pass vq setup info to DSC using adminq to gather up and
		 * send all info at once so FW can do its full set up in
		 * one easy operation
		 */
		err = pds_vdpa_cmd_init_vq(pdsv, qid, invert_idx, &pdsv->vqs[qid]);
		if (err) {
			dev_err(dev, "Failed to init vq %d: %pe\n",
				qid, ERR_PTR(err));
			ready = false;
		}
	} else {
		err = pds_vdpa_cmd_reset_vq(pdsv, qid, invert_idx, &pdsv->vqs[qid]);
		if (err)
			dev_err(dev, "%s: reset_vq failed qid %d: %pe\n",
				__func__, qid, ERR_PTR(err));
	}

	pdsv->vqs[qid].ready = ready;
}

static bool pds_vdpa_get_vq_ready(struct vdpa_device *vdpa_dev, u16 qid)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);

	return pdsv->vqs[qid].ready;
}

static int pds_vdpa_set_vq_state(struct vdpa_device *vdpa_dev, u16 qid,
				 const struct vdpa_vq_state *state)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);
	struct pds_auxiliary_dev *padev = pdsv->vdpa_aux->padev;
	struct device *dev = &padev->aux_dev.dev;
	u64 driver_features;
	u16 avail;
	u16 used;

	if (pdsv->vqs[qid].ready) {
		dev_err(dev, "Setting device position is denied while vq is enabled\n");
		return -EINVAL;
	}

	driver_features = pds_vdpa_get_driver_features(vdpa_dev);
	if (driver_features & BIT_ULL(VIRTIO_F_RING_PACKED)) {
		avail = state->packed.last_avail_idx |
			(state->packed.last_avail_counter << 15);
		used = state->packed.last_used_idx |
		       (state->packed.last_used_counter << 15);

		/* The avail and used index are stored with the packed wrap
		 * counter bit inverted.  This way, in case set_vq_state is
		 * not called, the initial value can be set to zero prior to
		 * feature negotiation, and it is good for both packed and
		 * split vq.
		 */
		avail ^= PDS_VDPA_PACKED_INVERT_IDX;
		used ^= PDS_VDPA_PACKED_INVERT_IDX;
	} else {
		avail = state->split.avail_index;
		/* state->split does not provide a used_index:
		 * the vq will be set to "empty" here, and the vq will read
		 * the current used index the next time the vq is kicked.
		 */
		used = avail;
	}

	if (used != avail) {
		dev_dbg(dev, "Setting used equal to avail, for interoperability\n");
		used = avail;
	}

	pdsv->vqs[qid].avail_idx = avail;
	pdsv->vqs[qid].used_idx = used;

	return 0;
}

static int pds_vdpa_get_vq_state(struct vdpa_device *vdpa_dev, u16 qid,
				 struct vdpa_vq_state *state)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);
	struct pds_auxiliary_dev *padev = pdsv->vdpa_aux->padev;
	struct device *dev = &padev->aux_dev.dev;
	u64 driver_features;
	u16 avail;
	u16 used;

	if (pdsv->vqs[qid].ready) {
		dev_err(dev, "Getting device position is denied while vq is enabled\n");
		return -EINVAL;
	}

	avail = pdsv->vqs[qid].avail_idx;
	used = pdsv->vqs[qid].used_idx;

	driver_features = pds_vdpa_get_driver_features(vdpa_dev);
	if (driver_features & BIT_ULL(VIRTIO_F_RING_PACKED)) {
		avail ^= PDS_VDPA_PACKED_INVERT_IDX;
		used ^= PDS_VDPA_PACKED_INVERT_IDX;

		state->packed.last_avail_idx = avail & 0x7fff;
		state->packed.last_avail_counter = avail >> 15;
		state->packed.last_used_idx = used & 0x7fff;
		state->packed.last_used_counter = used >> 15;
	} else {
		state->split.avail_index = avail;
		/* state->split does not provide a used_index. */
	}

	return 0;
}

static struct vdpa_notification_area
pds_vdpa_get_vq_notification(struct vdpa_device *vdpa_dev, u16 qid)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);
	struct virtio_pci_modern_device *vd_mdev;
	struct vdpa_notification_area area;

	area.addr = pdsv->vqs[qid].notify_pa;

	vd_mdev = &pdsv->vdpa_aux->vd_mdev;
	if (!vd_mdev->notify_offset_multiplier)
		area.size = PDS_PAGE_SIZE;
	else
		area.size = vd_mdev->notify_offset_multiplier;

	return area;
}

static int pds_vdpa_get_vq_irq(struct vdpa_device *vdpa_dev, u16 qid)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);

	return pdsv->vqs[qid].irq;
}

static u32 pds_vdpa_get_vq_align(struct vdpa_device *vdpa_dev)
{
	return PDS_PAGE_SIZE;
}

static u32 pds_vdpa_get_vq_group(struct vdpa_device *vdpa_dev, u16 idx)
{
	return 0;
}

static u64 pds_vdpa_get_device_features(struct vdpa_device *vdpa_dev)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);

	return pdsv->supported_features;
}

static int pds_vdpa_set_driver_features(struct vdpa_device *vdpa_dev, u64 features)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);
	struct device *dev = &pdsv->vdpa_dev.dev;
	u64 driver_features;
	u64 nego_features;
	u64 hw_features;
	u64 missing;

	if (!(features & BIT_ULL(VIRTIO_F_ACCESS_PLATFORM)) && features) {
		dev_err(dev, "VIRTIO_F_ACCESS_PLATFORM is not negotiated\n");
		return -EOPNOTSUPP;
	}

	/* Check for valid feature bits */
	nego_features = features & pdsv->supported_features;
	missing = features & ~nego_features;
	if (missing) {
		dev_err(dev, "Can't support all requested features in %#llx, missing %#llx features\n",
			features, missing);
		return -EOPNOTSUPP;
	}

	driver_features = pds_vdpa_get_driver_features(vdpa_dev);
	pdsv->negotiated_features = nego_features;
	dev_dbg(dev, "%s: %#llx => %#llx\n",
		__func__, driver_features, nego_features);

	/* if we're faking the F_MAC, strip it before writing to device */
	hw_features = le64_to_cpu(pdsv->vdpa_aux->ident.hw_features);
	if (!(hw_features & BIT_ULL(VIRTIO_NET_F_MAC)))
		nego_features &= ~BIT_ULL(VIRTIO_NET_F_MAC);

	if (driver_features == nego_features)
		return 0;

	vp_modern_set_features(&pdsv->vdpa_aux->vd_mdev, nego_features);

	return 0;
}

static u64 pds_vdpa_get_driver_features(struct vdpa_device *vdpa_dev)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);

	return pdsv->negotiated_features;
}

static void pds_vdpa_set_config_cb(struct vdpa_device *vdpa_dev,
				   struct vdpa_callback *cb)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);

	pdsv->config_cb.callback = cb->callback;
	pdsv->config_cb.private = cb->private;
}

static u16 pds_vdpa_get_vq_num_max(struct vdpa_device *vdpa_dev)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);

	/* qemu has assert() that vq_num_max <= VIRTQUEUE_MAX_SIZE (1024) */
	return min_t(u16, 1024, BIT(le16_to_cpu(pdsv->vdpa_aux->ident.max_qlen)));
}

static u32 pds_vdpa_get_device_id(struct vdpa_device *vdpa_dev)
{
	return VIRTIO_ID_NET;
}

static u32 pds_vdpa_get_vendor_id(struct vdpa_device *vdpa_dev)
{
	return PCI_VENDOR_ID_PENSANDO;
}

static u8 pds_vdpa_get_status(struct vdpa_device *vdpa_dev)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);

	return vp_modern_get_status(&pdsv->vdpa_aux->vd_mdev);
}

static int pds_vdpa_request_irqs(struct pds_vdpa_device *pdsv)
{
	struct pci_dev *pdev = pdsv->vdpa_aux->padev->vf_pdev;
	struct pds_vdpa_aux *vdpa_aux = pdsv->vdpa_aux;
	struct device *dev = &pdsv->vdpa_dev.dev;
	int max_vq, nintrs, qid, err;

	max_vq = vdpa_aux->vdpa_mdev.max_supported_vqs;

	nintrs = pci_alloc_irq_vectors(pdev, max_vq, max_vq, PCI_IRQ_MSIX);
	if (nintrs < 0) {
		dev_err(dev, "Couldn't get %d msix vectors: %pe\n",
			max_vq, ERR_PTR(nintrs));
		return nintrs;
	}

	for (qid = 0; qid < pdsv->num_vqs; ++qid) {
		int irq = pci_irq_vector(pdev, qid);

		snprintf(pdsv->vqs[qid].irq_name, sizeof(pdsv->vqs[qid].irq_name),
			 "vdpa-%s-%d", dev_name(dev), qid);

		err = request_irq(irq, pds_vdpa_isr, 0,
				  pdsv->vqs[qid].irq_name,
				  &pdsv->vqs[qid]);
		if (err) {
			dev_err(dev, "%s: no irq for qid %d: %pe\n",
				__func__, qid, ERR_PTR(err));
			goto err_release;
		}

		pdsv->vqs[qid].irq = irq;
	}

	vdpa_aux->nintrs = nintrs;

	return 0;

err_release:
	while (qid--)
		pds_vdpa_release_irq(pdsv, qid);

	pci_free_irq_vectors(pdev);

	vdpa_aux->nintrs = 0;

	return err;
}

static void pds_vdpa_release_irqs(struct pds_vdpa_device *pdsv)
{
	struct pci_dev *pdev = pdsv->vdpa_aux->padev->vf_pdev;
	struct pds_vdpa_aux *vdpa_aux = pdsv->vdpa_aux;
	int qid;

	if (!vdpa_aux->nintrs)
		return;

	for (qid = 0; qid < pdsv->num_vqs; qid++)
		pds_vdpa_release_irq(pdsv, qid);

	pci_free_irq_vectors(pdev);

	vdpa_aux->nintrs = 0;
}

static void pds_vdpa_set_status(struct vdpa_device *vdpa_dev, u8 status)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);
	struct device *dev = &pdsv->vdpa_dev.dev;
	u8 old_status;
	int i;

	old_status = pds_vdpa_get_status(vdpa_dev);
	dev_dbg(dev, "%s: old %#x new %#x\n", __func__, old_status, status);

	if (status & ~old_status & VIRTIO_CONFIG_S_DRIVER_OK) {
		if (pds_vdpa_request_irqs(pdsv))
			status = old_status | VIRTIO_CONFIG_S_FAILED;
	}

	pds_vdpa_cmd_set_status(pdsv, status);

	if (status == 0) {
		struct vdpa_callback null_cb = { };

		pds_vdpa_set_config_cb(vdpa_dev, &null_cb);
		pds_vdpa_cmd_reset(pdsv);

		for (i = 0; i < pdsv->num_vqs; i++) {
			pdsv->vqs[i].avail_idx = 0;
			pdsv->vqs[i].used_idx = 0;
		}

		pds_vdpa_cmd_set_mac(pdsv, pdsv->mac);
	}

	if (status & ~old_status & VIRTIO_CONFIG_S_FEATURES_OK) {
		for (i = 0; i < pdsv->num_vqs; i++) {
			pdsv->vqs[i].notify =
				vp_modern_map_vq_notify(&pdsv->vdpa_aux->vd_mdev,
							i, &pdsv->vqs[i].notify_pa);
		}
	}

	if (old_status & ~status & VIRTIO_CONFIG_S_DRIVER_OK)
		pds_vdpa_release_irqs(pdsv);
}

static void pds_vdpa_init_vqs_entry(struct pds_vdpa_device *pdsv, int qid,
				    void __iomem *notify)
{
	memset(&pdsv->vqs[qid], 0, sizeof(pdsv->vqs[0]));
	pdsv->vqs[qid].qid = qid;
	pdsv->vqs[qid].pdsv = pdsv;
	pdsv->vqs[qid].ready = false;
	pdsv->vqs[qid].irq = VIRTIO_MSI_NO_VECTOR;
	pdsv->vqs[qid].notify = notify;
}

static int pds_vdpa_reset(struct vdpa_device *vdpa_dev)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);
	struct device *dev;
	int err = 0;
	u8 status;
	int i;

	dev = &pdsv->vdpa_aux->padev->aux_dev.dev;
	status = pds_vdpa_get_status(vdpa_dev);

	if (status == 0)
		return 0;

	if (status & VIRTIO_CONFIG_S_DRIVER_OK) {
		/* Reset the vqs */
		for (i = 0; i < pdsv->num_vqs && !err; i++) {
			err = pds_vdpa_cmd_reset_vq(pdsv, i, 0, &pdsv->vqs[i]);
			if (err)
				dev_err(dev, "%s: reset_vq failed qid %d: %pe\n",
					__func__, i, ERR_PTR(err));
		}
	}

	pds_vdpa_set_status(vdpa_dev, 0);

	if (status & VIRTIO_CONFIG_S_DRIVER_OK) {
		/* Reset the vq info */
		for (i = 0; i < pdsv->num_vqs && !err; i++)
			pds_vdpa_init_vqs_entry(pdsv, i, pdsv->vqs[i].notify);
	}

	return 0;
}

static size_t pds_vdpa_get_config_size(struct vdpa_device *vdpa_dev)
{
	return sizeof(struct virtio_net_config);
}

static void pds_vdpa_get_config(struct vdpa_device *vdpa_dev,
				unsigned int offset,
				void *buf, unsigned int len)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);
	void __iomem *device;

	if (offset + len > sizeof(struct virtio_net_config)) {
		WARN(true, "%s: bad read, offset %d len %d\n", __func__, offset, len);
		return;
	}

	device = pdsv->vdpa_aux->vd_mdev.device;
	memcpy_fromio(buf, device + offset, len);
}

static void pds_vdpa_set_config(struct vdpa_device *vdpa_dev,
				unsigned int offset, const void *buf,
				unsigned int len)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);
	void __iomem *device;

	if (offset + len > sizeof(struct virtio_net_config)) {
		WARN(true, "%s: bad read, offset %d len %d\n", __func__, offset, len);
		return;
	}

	device = pdsv->vdpa_aux->vd_mdev.device;
	memcpy_toio(device + offset, buf, len);
}

static const struct vdpa_config_ops pds_vdpa_ops = {
	.set_vq_address		= pds_vdpa_set_vq_address,
	.set_vq_num		= pds_vdpa_set_vq_num,
	.kick_vq		= pds_vdpa_kick_vq,
	.set_vq_cb		= pds_vdpa_set_vq_cb,
	.set_vq_ready		= pds_vdpa_set_vq_ready,
	.get_vq_ready		= pds_vdpa_get_vq_ready,
	.set_vq_state		= pds_vdpa_set_vq_state,
	.get_vq_state		= pds_vdpa_get_vq_state,
	.get_vq_notification	= pds_vdpa_get_vq_notification,
	.get_vq_irq		= pds_vdpa_get_vq_irq,
	.get_vq_align		= pds_vdpa_get_vq_align,
	.get_vq_group		= pds_vdpa_get_vq_group,

	.get_device_features	= pds_vdpa_get_device_features,
	.set_driver_features	= pds_vdpa_set_driver_features,
	.get_driver_features	= pds_vdpa_get_driver_features,
	.set_config_cb		= pds_vdpa_set_config_cb,
	.get_vq_num_max		= pds_vdpa_get_vq_num_max,
	.get_device_id		= pds_vdpa_get_device_id,
	.get_vendor_id		= pds_vdpa_get_vendor_id,
	.get_status		= pds_vdpa_get_status,
	.set_status		= pds_vdpa_set_status,
	.reset			= pds_vdpa_reset,
	.get_config_size	= pds_vdpa_get_config_size,
	.get_config		= pds_vdpa_get_config,
	.set_config		= pds_vdpa_set_config,
};
static struct virtio_device_id pds_vdpa_id_table[] = {
	{VIRTIO_ID_NET, VIRTIO_DEV_ANY_ID},
	{0},
};

static int pds_vdpa_dev_add(struct vdpa_mgmt_dev *mdev, const char *name,
			    const struct vdpa_dev_set_config *add_config)
{
	struct pds_vdpa_aux *vdpa_aux;
	struct pds_vdpa_device *pdsv;
	struct vdpa_mgmt_dev *mgmt;
	u16 fw_max_vqs, vq_pairs;
	struct device *dma_dev;
	struct pci_dev *pdev;
	struct device *dev;
	int err;
	int i;

	vdpa_aux = container_of(mdev, struct pds_vdpa_aux, vdpa_mdev);
	dev = &vdpa_aux->padev->aux_dev.dev;
	mgmt = &vdpa_aux->vdpa_mdev;

	if (vdpa_aux->pdsv) {
		dev_warn(dev, "Multiple vDPA devices on a VF is not supported.\n");
		return -EOPNOTSUPP;
	}

	pdsv = vdpa_alloc_device(struct pds_vdpa_device, vdpa_dev,
				 dev, &pds_vdpa_ops, 1, 1, name, false);
	if (IS_ERR(pdsv)) {
		dev_err(dev, "Failed to allocate vDPA structure: %pe\n", pdsv);
		return PTR_ERR(pdsv);
	}

	vdpa_aux->pdsv = pdsv;
	pdsv->vdpa_aux = vdpa_aux;

	pdev = vdpa_aux->padev->vf_pdev;
	dma_dev = &pdev->dev;
	pdsv->vdpa_dev.dma_dev = dma_dev;

	pdsv->supported_features = mgmt->supported_features;

	if (add_config->mask & BIT_ULL(VDPA_ATTR_DEV_FEATURES)) {
		u64 unsupp_features =
			add_config->device_features & ~pdsv->supported_features;

		if (unsupp_features) {
			dev_err(dev, "Unsupported features: %#llx\n", unsupp_features);
			err = -EOPNOTSUPP;
			goto err_unmap;
		}

		pdsv->supported_features = add_config->device_features;
	}

	err = pds_vdpa_cmd_reset(pdsv);
	if (err) {
		dev_err(dev, "Failed to reset hw: %pe\n", ERR_PTR(err));
		goto err_unmap;
	}

	err = pds_vdpa_init_hw(pdsv);
	if (err) {
		dev_err(dev, "Failed to init hw: %pe\n", ERR_PTR(err));
		goto err_unmap;
	}

	fw_max_vqs = le16_to_cpu(pdsv->vdpa_aux->ident.max_vqs);
	vq_pairs = fw_max_vqs / 2;

	/* Make sure we have the queues being requested */
	if (add_config->mask & (1 << VDPA_ATTR_DEV_NET_CFG_MAX_VQP))
		vq_pairs = add_config->net.max_vq_pairs;

	pdsv->num_vqs = 2 * vq_pairs;
	if (pdsv->supported_features & BIT_ULL(VIRTIO_NET_F_CTRL_VQ))
		pdsv->num_vqs++;

	if (pdsv->num_vqs > fw_max_vqs) {
		dev_err(dev, "%s: queue count requested %u greater than max %u\n",
			__func__, pdsv->num_vqs, fw_max_vqs);
		err = -ENOSPC;
		goto err_unmap;
	}

	if (pdsv->num_vqs != fw_max_vqs) {
		err = pds_vdpa_cmd_set_max_vq_pairs(pdsv, vq_pairs);
		if (err) {
			dev_err(dev, "Failed to set max_vq_pairs: %pe\n",
				ERR_PTR(err));
			goto err_unmap;
		}
	}

	/* Set a mac, either from the user config if provided
	 * or use the device's mac if not 00:..:00
	 * or set a random mac
	 */
	if (add_config->mask & BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MACADDR)) {
		ether_addr_copy(pdsv->mac, add_config->net.mac);
	} else {
		struct virtio_net_config __iomem *vc;

		vc = pdsv->vdpa_aux->vd_mdev.device;
		memcpy_fromio(pdsv->mac, vc->mac, sizeof(pdsv->mac));
		if (is_zero_ether_addr(pdsv->mac) &&
		    (pdsv->supported_features & BIT_ULL(VIRTIO_NET_F_MAC))) {
			eth_random_addr(pdsv->mac);
			dev_info(dev, "setting random mac %pM\n", pdsv->mac);
		}
	}
	pds_vdpa_cmd_set_mac(pdsv, pdsv->mac);

	for (i = 0; i < pdsv->num_vqs; i++) {
		void __iomem *notify;

		notify = vp_modern_map_vq_notify(&pdsv->vdpa_aux->vd_mdev,
						 i, &pdsv->vqs[i].notify_pa);
		pds_vdpa_init_vqs_entry(pdsv, i, notify);
	}

	pdsv->vdpa_dev.mdev = &vdpa_aux->vdpa_mdev;

	err = pds_vdpa_register_event_handler(pdsv);
	if (err) {
		dev_err(dev, "Failed to register for PDS events: %pe\n", ERR_PTR(err));
		goto err_unmap;
	}

	/* We use the _vdpa_register_device() call rather than the
	 * vdpa_register_device() to avoid a deadlock because our
	 * dev_add() is called with the vdpa_dev_lock already set
	 * by vdpa_nl_cmd_dev_add_set_doit()
	 */
	err = _vdpa_register_device(&pdsv->vdpa_dev, pdsv->num_vqs);
	if (err) {
		dev_err(dev, "Failed to register to vDPA bus: %pe\n", ERR_PTR(err));
		goto err_unevent;
	}

	pds_vdpa_debugfs_add_vdpadev(vdpa_aux);

	return 0;

err_unevent:
	pds_vdpa_unregister_event_handler(pdsv);
err_unmap:
	put_device(&pdsv->vdpa_dev.dev);
	vdpa_aux->pdsv = NULL;
	return err;
}

static void pds_vdpa_dev_del(struct vdpa_mgmt_dev *mdev,
			     struct vdpa_device *vdpa_dev)
{
	struct pds_vdpa_device *pdsv = vdpa_to_pdsv(vdpa_dev);
	struct pds_vdpa_aux *vdpa_aux;

	pds_vdpa_unregister_event_handler(pdsv);

	vdpa_aux = container_of(mdev, struct pds_vdpa_aux, vdpa_mdev);
	_vdpa_unregister_device(vdpa_dev);

	pds_vdpa_cmd_reset(vdpa_aux->pdsv);
	pds_vdpa_debugfs_reset_vdpadev(vdpa_aux);

	vdpa_aux->pdsv = NULL;

	dev_info(&vdpa_aux->padev->aux_dev.dev, "Removed vdpa device\n");
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
	vdpa_aux->nintrs = 0;

	mgmt->ops = &pds_vdpa_mgmt_dev_ops;
	mgmt->id_table = pds_vdpa_id_table;
	mgmt->device = dev;
	mgmt->supported_features = le64_to_cpu(vdpa_aux->ident.hw_features);

	/* advertise F_MAC even if the device doesn't */
	mgmt->supported_features |= BIT_ULL(VIRTIO_NET_F_MAC);

	mgmt->config_attr_mask = BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MACADDR);
	mgmt->config_attr_mask |= BIT_ULL(VDPA_ATTR_DEV_NET_CFG_MAX_VQP);
	mgmt->config_attr_mask |= BIT_ULL(VDPA_ATTR_DEV_FEATURES);

	return 0;
}
