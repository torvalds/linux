// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 Marvell. */

#include <linux/interrupt.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/module.h>
#include <linux/iommu.h>
#include "octep_vdpa.h"

#define OCTEP_VDPA_DRIVER_NAME "octep_vdpa"

struct octep_pf {
	u8 __iomem *base[PCI_STD_NUM_BARS];
	struct pci_dev *pdev;
	struct resource res;
	u64 vf_base;
	int enabled_vfs;
	u32 vf_stride;
	u16 vf_devid;
};

struct octep_vdpa {
	struct vdpa_device vdpa;
	struct octep_hw *oct_hw;
	struct pci_dev *pdev;
};

struct octep_vdpa_mgmt_dev {
	struct vdpa_mgmt_dev mdev;
	struct octep_hw oct_hw;
	struct pci_dev *pdev;
	/* Work entry to handle device setup */
	struct work_struct setup_task;
	/* Device status */
	atomic_t status;
};

static struct octep_hw *vdpa_to_octep_hw(struct vdpa_device *vdpa_dev)
{
	struct octep_vdpa *oct_vdpa;

	oct_vdpa = container_of(vdpa_dev, struct octep_vdpa, vdpa);

	return oct_vdpa->oct_hw;
}

static irqreturn_t octep_vdpa_intr_handler(int irq, void *data)
{
	struct octep_hw *oct_hw = data;
	int i;

	for (i = 0; i < oct_hw->nr_vring; i++) {
		if (oct_hw->vqs[i].cb.callback && ioread32(oct_hw->vqs[i].cb_notify_addr)) {
			/* Acknowledge the per queue notification to the device */
			iowrite32(0, oct_hw->vqs[i].cb_notify_addr);
			oct_hw->vqs[i].cb.callback(oct_hw->vqs[i].cb.private);
		}
	}

	return IRQ_HANDLED;
}

static void octep_free_irqs(struct octep_hw *oct_hw)
{
	struct pci_dev *pdev = oct_hw->pdev;

	if (oct_hw->irq != -1) {
		devm_free_irq(&pdev->dev, oct_hw->irq, oct_hw);
		oct_hw->irq = -1;
	}
	pci_free_irq_vectors(pdev);
}

static int octep_request_irqs(struct octep_hw *oct_hw)
{
	struct pci_dev *pdev = oct_hw->pdev;
	int ret, irq;

	/* Currently HW device provisions one IRQ per VF, hence
	 * allocate one IRQ for all virtqueues call interface.
	 */
	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSIX);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to alloc msix vector");
		return ret;
	}

	snprintf(oct_hw->vqs->msix_name, sizeof(oct_hw->vqs->msix_name),
		 OCTEP_VDPA_DRIVER_NAME "-vf-%d", pci_iov_vf_id(pdev));

	irq = pci_irq_vector(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq, octep_vdpa_intr_handler, 0,
			       oct_hw->vqs->msix_name, oct_hw);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register interrupt handler\n");
		goto free_irq_vec;
	}
	oct_hw->irq = irq;

	return 0;

free_irq_vec:
	pci_free_irq_vectors(pdev);
	return ret;
}

static u64 octep_vdpa_get_device_features(struct vdpa_device *vdpa_dev)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	return oct_hw->features;
}

static int octep_vdpa_set_driver_features(struct vdpa_device *vdpa_dev, u64 features)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);
	int ret;

	pr_debug("Driver Features: %llx\n", features);

	ret = octep_verify_features(features);
	if (ret) {
		dev_warn(&oct_hw->pdev->dev,
			 "Must negotiate minimum features 0x%llx for this device",
			 BIT_ULL(VIRTIO_F_VERSION_1) | BIT_ULL(VIRTIO_F_NOTIFICATION_DATA) |
			 BIT_ULL(VIRTIO_F_RING_PACKED));
		return ret;
	}
	octep_hw_set_drv_features(oct_hw, features);

	return 0;
}

static u64 octep_vdpa_get_driver_features(struct vdpa_device *vdpa_dev)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	return octep_hw_get_drv_features(oct_hw);
}

static u8 octep_vdpa_get_status(struct vdpa_device *vdpa_dev)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	return octep_hw_get_status(oct_hw);
}

static void octep_vdpa_set_status(struct vdpa_device *vdpa_dev, u8 status)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);
	u8 status_old;

	status_old = octep_hw_get_status(oct_hw);

	if (status_old == status)
		return;

	if ((status & VIRTIO_CONFIG_S_DRIVER_OK) &&
	    !(status_old & VIRTIO_CONFIG_S_DRIVER_OK)) {
		if (octep_request_irqs(oct_hw))
			status = status_old | VIRTIO_CONFIG_S_FAILED;
	}
	octep_hw_set_status(oct_hw, status);
}

static int octep_vdpa_reset(struct vdpa_device *vdpa_dev)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);
	u8 status = octep_hw_get_status(oct_hw);
	u16 qid;

	if (status == 0)
		return 0;

	for (qid = 0; qid < oct_hw->nr_vring; qid++) {
		oct_hw->vqs[qid].cb.callback = NULL;
		oct_hw->vqs[qid].cb.private = NULL;
		oct_hw->config_cb.callback = NULL;
		oct_hw->config_cb.private = NULL;
	}
	octep_hw_reset(oct_hw);

	if (status & VIRTIO_CONFIG_S_DRIVER_OK)
		octep_free_irqs(oct_hw);

	return 0;
}

static u16 octep_vdpa_get_vq_num_max(struct vdpa_device *vdpa_dev)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	return octep_get_vq_size(oct_hw);
}

static int octep_vdpa_get_vq_state(struct vdpa_device *vdpa_dev, u16 qid,
				   struct vdpa_vq_state *state)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	return octep_get_vq_state(oct_hw, qid, state);
}

static int octep_vdpa_set_vq_state(struct vdpa_device *vdpa_dev, u16 qid,
				   const struct vdpa_vq_state *state)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	return octep_set_vq_state(oct_hw, qid, state);
}

static void octep_vdpa_set_vq_cb(struct vdpa_device *vdpa_dev, u16 qid, struct vdpa_callback *cb)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	oct_hw->vqs[qid].cb = *cb;
}

static void octep_vdpa_set_vq_ready(struct vdpa_device *vdpa_dev, u16 qid, bool ready)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	octep_set_vq_ready(oct_hw, qid, ready);
}

static bool octep_vdpa_get_vq_ready(struct vdpa_device *vdpa_dev, u16 qid)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	return octep_get_vq_ready(oct_hw, qid);
}

static void octep_vdpa_set_vq_num(struct vdpa_device *vdpa_dev, u16 qid, u32 num)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	octep_set_vq_num(oct_hw, qid, num);
}

static int octep_vdpa_set_vq_address(struct vdpa_device *vdpa_dev, u16 qid, u64 desc_area,
				     u64 driver_area, u64 device_area)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	pr_debug("qid[%d]: desc_area: %llx\n", qid, desc_area);
	pr_debug("qid[%d]: driver_area: %llx\n", qid, driver_area);
	pr_debug("qid[%d]: device_area: %llx\n\n", qid, device_area);

	return octep_set_vq_address(oct_hw, qid, desc_area, driver_area, device_area);
}

static void octep_vdpa_kick_vq(struct vdpa_device *vdpa_dev, u16 qid)
{
	/* Not supported */
}

static void octep_vdpa_kick_vq_with_data(struct vdpa_device *vdpa_dev, u32 data)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);
	u16 idx = data & 0xFFFF;

	vp_iowrite32(data, oct_hw->vqs[idx].notify_addr);
}

static u32 octep_vdpa_get_generation(struct vdpa_device *vdpa_dev)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	return vp_ioread8(&oct_hw->common_cfg->config_generation);
}

static u32 octep_vdpa_get_device_id(struct vdpa_device *vdpa_dev)
{
	return VIRTIO_ID_NET;
}

static u32 octep_vdpa_get_vendor_id(struct vdpa_device *vdpa_dev)
{
	return PCI_VENDOR_ID_CAVIUM;
}

static u32 octep_vdpa_get_vq_align(struct vdpa_device *vdpa_dev)
{
	return PAGE_SIZE;
}

static size_t octep_vdpa_get_config_size(struct vdpa_device *vdpa_dev)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	return oct_hw->config_size;
}

static void octep_vdpa_get_config(struct vdpa_device *vdpa_dev, unsigned int offset, void *buf,
				  unsigned int len)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	octep_read_dev_config(oct_hw, offset, buf, len);
}

static void octep_vdpa_set_config(struct vdpa_device *vdpa_dev, unsigned int offset,
				  const void *buf, unsigned int len)
{
	/* Not supported */
}

static void octep_vdpa_set_config_cb(struct vdpa_device *vdpa_dev, struct vdpa_callback *cb)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);

	oct_hw->config_cb.callback = cb->callback;
	oct_hw->config_cb.private = cb->private;
}

static struct vdpa_notification_area octep_get_vq_notification(struct vdpa_device *vdpa_dev,
							       u16 idx)
{
	struct octep_hw *oct_hw = vdpa_to_octep_hw(vdpa_dev);
	struct vdpa_notification_area area;

	area.addr = oct_hw->vqs[idx].notify_pa;
	area.size = PAGE_SIZE;

	return area;
}

static struct vdpa_config_ops octep_vdpa_ops = {
	.get_device_features = octep_vdpa_get_device_features,
	.set_driver_features = octep_vdpa_set_driver_features,
	.get_driver_features = octep_vdpa_get_driver_features,
	.get_status	= octep_vdpa_get_status,
	.set_status	= octep_vdpa_set_status,
	.reset		= octep_vdpa_reset,
	.get_vq_num_max	= octep_vdpa_get_vq_num_max,
	.get_vq_state	= octep_vdpa_get_vq_state,
	.set_vq_state	= octep_vdpa_set_vq_state,
	.set_vq_cb	= octep_vdpa_set_vq_cb,
	.set_vq_ready	= octep_vdpa_set_vq_ready,
	.get_vq_ready	= octep_vdpa_get_vq_ready,
	.set_vq_num	= octep_vdpa_set_vq_num,
	.set_vq_address	= octep_vdpa_set_vq_address,
	.get_vq_irq	= NULL,
	.kick_vq	= octep_vdpa_kick_vq,
	.kick_vq_with_data	= octep_vdpa_kick_vq_with_data,
	.get_generation	= octep_vdpa_get_generation,
	.get_device_id	= octep_vdpa_get_device_id,
	.get_vendor_id	= octep_vdpa_get_vendor_id,
	.get_vq_align	= octep_vdpa_get_vq_align,
	.get_config_size	= octep_vdpa_get_config_size,
	.get_config	= octep_vdpa_get_config,
	.set_config	= octep_vdpa_set_config,
	.set_config_cb  = octep_vdpa_set_config_cb,
	.get_vq_notification = octep_get_vq_notification,
};

static int octep_iomap_region(struct pci_dev *pdev, u8 __iomem **tbl, u8 bar)
{
	int ret;

	ret = pci_request_region(pdev, bar, OCTEP_VDPA_DRIVER_NAME);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request BAR:%u region\n", bar);
		return ret;
	}

	tbl[bar] = pci_iomap(pdev, bar, pci_resource_len(pdev, bar));
	if (!tbl[bar]) {
		dev_err(&pdev->dev, "Failed to iomap BAR:%u\n", bar);
		pci_release_region(pdev, bar);
		ret = -ENOMEM;
	}

	return ret;
}

static void octep_iounmap_region(struct pci_dev *pdev, u8 __iomem **tbl, u8 bar)
{
	pci_iounmap(pdev, tbl[bar]);
	pci_release_region(pdev, bar);
}

static void octep_vdpa_pf_bar_shrink(struct octep_pf *octpf)
{
	struct pci_dev *pf_dev = octpf->pdev;
	struct resource *res = pf_dev->resource + PCI_STD_RESOURCES + 4;
	struct pci_bus_region bus_region;

	octpf->res.start = res->start;
	octpf->res.end = res->end;
	octpf->vf_base = res->start;

	bus_region.start = res->start;
	bus_region.end = res->start - 1;

	pcibios_bus_to_resource(pf_dev->bus, res, &bus_region);
}

static void octep_vdpa_pf_bar_expand(struct octep_pf *octpf)
{
	struct pci_dev *pf_dev = octpf->pdev;
	struct resource *res = pf_dev->resource + PCI_STD_RESOURCES + 4;
	struct pci_bus_region bus_region;

	bus_region.start = octpf->res.start;
	bus_region.end = octpf->res.end;

	pcibios_bus_to_resource(pf_dev->bus, res, &bus_region);
}

static void octep_vdpa_remove_pf(struct pci_dev *pdev)
{
	struct octep_pf *octpf = pci_get_drvdata(pdev);

	pci_disable_sriov(pdev);

	if (octpf->base[OCTEP_HW_CAPS_BAR])
		octep_iounmap_region(pdev, octpf->base, OCTEP_HW_CAPS_BAR);

	if (octpf->base[OCTEP_HW_MBOX_BAR])
		octep_iounmap_region(pdev, octpf->base, OCTEP_HW_MBOX_BAR);

	octep_vdpa_pf_bar_expand(octpf);
}

static void octep_vdpa_vf_bar_shrink(struct pci_dev *pdev)
{
	struct resource *vf_res = pdev->resource + PCI_STD_RESOURCES + 4;

	memset(vf_res, 0, sizeof(*vf_res));
}

static void octep_vdpa_remove_vf(struct pci_dev *pdev)
{
	struct octep_vdpa_mgmt_dev *mgmt_dev = pci_get_drvdata(pdev);
	struct octep_hw *oct_hw;
	int status;

	oct_hw = &mgmt_dev->oct_hw;
	status = atomic_read(&mgmt_dev->status);
	atomic_set(&mgmt_dev->status, OCTEP_VDPA_DEV_STATUS_UNINIT);

	cancel_work_sync(&mgmt_dev->setup_task);
	if (status == OCTEP_VDPA_DEV_STATUS_READY)
		vdpa_mgmtdev_unregister(&mgmt_dev->mdev);

	if (oct_hw->base[OCTEP_HW_CAPS_BAR])
		octep_iounmap_region(pdev, oct_hw->base, OCTEP_HW_CAPS_BAR);

	if (oct_hw->base[OCTEP_HW_MBOX_BAR])
		octep_iounmap_region(pdev, oct_hw->base, OCTEP_HW_MBOX_BAR);

	octep_vdpa_vf_bar_shrink(pdev);
}

static void octep_vdpa_remove(struct pci_dev *pdev)
{
	if (pdev->is_virtfn)
		octep_vdpa_remove_vf(pdev);
	else
		octep_vdpa_remove_pf(pdev);
}

static int octep_vdpa_dev_add(struct vdpa_mgmt_dev *mdev, const char *name,
			      const struct vdpa_dev_set_config *config)
{
	struct octep_vdpa_mgmt_dev *mgmt_dev = container_of(mdev, struct octep_vdpa_mgmt_dev, mdev);
	struct octep_hw *oct_hw = &mgmt_dev->oct_hw;
	struct pci_dev *pdev = oct_hw->pdev;
	struct vdpa_device *vdpa_dev;
	struct octep_vdpa *oct_vdpa;
	u64 device_features;
	int ret;

	oct_vdpa = vdpa_alloc_device(struct octep_vdpa, vdpa, &pdev->dev, &octep_vdpa_ops, 1, 1,
				     NULL, false);
	if (IS_ERR(oct_vdpa)) {
		dev_err(&pdev->dev, "Failed to allocate vDPA structure for octep vdpa device");
		return PTR_ERR(oct_vdpa);
	}

	oct_vdpa->pdev = pdev;
	oct_vdpa->vdpa.dma_dev = &pdev->dev;
	oct_vdpa->vdpa.mdev = mdev;
	oct_vdpa->oct_hw = oct_hw;
	vdpa_dev = &oct_vdpa->vdpa;

	device_features = oct_hw->features;
	if (config->mask & BIT_ULL(VDPA_ATTR_DEV_FEATURES)) {
		if (config->device_features & ~device_features) {
			dev_err(&pdev->dev, "The provisioned features 0x%llx are not supported by this device with features 0x%llx\n",
				config->device_features, device_features);
			ret = -EINVAL;
			goto vdpa_dev_put;
		}
		device_features &= config->device_features;
	}

	oct_hw->features = device_features;
	dev_info(&pdev->dev, "Vdpa management device features : %llx\n", device_features);

	ret = octep_verify_features(device_features);
	if (ret) {
		dev_warn(mdev->device,
			 "Must provision minimum features 0x%llx for this device",
			 BIT_ULL(VIRTIO_F_VERSION_1) | BIT_ULL(VIRTIO_F_ACCESS_PLATFORM) |
			 BIT_ULL(VIRTIO_F_NOTIFICATION_DATA) | BIT_ULL(VIRTIO_F_RING_PACKED));
		goto vdpa_dev_put;
	}
	if (name)
		ret = dev_set_name(&vdpa_dev->dev, "%s", name);
	else
		ret = dev_set_name(&vdpa_dev->dev, "vdpa%u", vdpa_dev->index);

	ret = _vdpa_register_device(&oct_vdpa->vdpa, oct_hw->nr_vring);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register to vDPA bus");
		goto vdpa_dev_put;
	}
	return 0;

vdpa_dev_put:
	put_device(&oct_vdpa->vdpa.dev);
	return ret;
}

static void octep_vdpa_dev_del(struct vdpa_mgmt_dev *mdev, struct vdpa_device *vdpa_dev)
{
	_vdpa_unregister_device(vdpa_dev);
}

static const struct vdpa_mgmtdev_ops octep_vdpa_mgmt_dev_ops = {
	.dev_add = octep_vdpa_dev_add,
	.dev_del = octep_vdpa_dev_del
};

static bool get_device_ready_status(u8 __iomem *addr)
{
	u64 signature = readq(addr + OCTEP_VF_MBOX_DATA(0));

	if (signature == OCTEP_DEV_READY_SIGNATURE) {
		writeq(0, addr + OCTEP_VF_MBOX_DATA(0));
		return true;
	}

	return false;
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_NET, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static void octep_vdpa_setup_task(struct work_struct *work)
{
	struct octep_vdpa_mgmt_dev *mgmt_dev = container_of(work, struct octep_vdpa_mgmt_dev,
							    setup_task);
	struct pci_dev *pdev = mgmt_dev->pdev;
	struct device *dev = &pdev->dev;
	struct octep_hw *oct_hw;
	unsigned long timeout;
	int ret;

	oct_hw = &mgmt_dev->oct_hw;

	atomic_set(&mgmt_dev->status, OCTEP_VDPA_DEV_STATUS_WAIT_FOR_BAR_INIT);

	/* Wait for a maximum of 5 sec */
	timeout = jiffies + msecs_to_jiffies(5000);
	while (!time_after(jiffies, timeout)) {
		if (get_device_ready_status(oct_hw->base[OCTEP_HW_MBOX_BAR])) {
			atomic_set(&mgmt_dev->status, OCTEP_VDPA_DEV_STATUS_INIT);
			break;
		}

		if (atomic_read(&mgmt_dev->status) >= OCTEP_VDPA_DEV_STATUS_READY) {
			dev_info(dev, "Stopping vDPA setup task.\n");
			return;
		}

		usleep_range(1000, 1500);
	}

	if (atomic_read(&mgmt_dev->status) != OCTEP_VDPA_DEV_STATUS_INIT) {
		dev_err(dev, "BAR initialization is timed out\n");
		return;
	}

	ret = octep_iomap_region(pdev, oct_hw->base, OCTEP_HW_CAPS_BAR);
	if (ret)
		return;

	ret = octep_hw_caps_read(oct_hw, pdev);
	if (ret < 0)
		goto unmap_region;

	mgmt_dev->mdev.ops = &octep_vdpa_mgmt_dev_ops;
	mgmt_dev->mdev.id_table = id_table;
	mgmt_dev->mdev.max_supported_vqs = oct_hw->nr_vring;
	mgmt_dev->mdev.supported_features = oct_hw->features;
	mgmt_dev->mdev.config_attr_mask = (1 << VDPA_ATTR_DEV_FEATURES);
	mgmt_dev->mdev.device = dev;

	ret = vdpa_mgmtdev_register(&mgmt_dev->mdev);
	if (ret) {
		dev_err(dev, "Failed to register vdpa management interface\n");
		goto unmap_region;
	}

	atomic_set(&mgmt_dev->status, OCTEP_VDPA_DEV_STATUS_READY);

	return;

unmap_region:
	octep_iounmap_region(pdev, oct_hw->base, OCTEP_HW_CAPS_BAR);
	oct_hw->base[OCTEP_HW_CAPS_BAR] = NULL;
}

static int octep_vdpa_probe_vf(struct pci_dev *pdev)
{
	struct octep_vdpa_mgmt_dev *mgmt_dev;
	struct device *dev = &pdev->dev;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(dev, "Failed to enable device\n");
		return ret;
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(dev, "No usable DMA configuration\n");
		return ret;
	}
	pci_set_master(pdev);

	mgmt_dev = devm_kzalloc(dev, sizeof(struct octep_vdpa_mgmt_dev), GFP_KERNEL);
	if (!mgmt_dev)
		return -ENOMEM;

	ret = octep_iomap_region(pdev, mgmt_dev->oct_hw.base, OCTEP_HW_MBOX_BAR);
	if (ret)
		return ret;

	mgmt_dev->pdev = pdev;
	pci_set_drvdata(pdev, mgmt_dev);

	atomic_set(&mgmt_dev->status, OCTEP_VDPA_DEV_STATUS_ALLOC);
	INIT_WORK(&mgmt_dev->setup_task, octep_vdpa_setup_task);
	schedule_work(&mgmt_dev->setup_task);
	dev_info(&pdev->dev, "octep vdpa mgmt device setup task is queued\n");

	return 0;
}

static void octep_vdpa_assign_barspace(struct pci_dev *vf_dev, struct pci_dev *pf_dev, u8 idx)
{
	struct resource *vf_res = vf_dev->resource + PCI_STD_RESOURCES + 4;
	struct resource *pf_res = pf_dev->resource + PCI_STD_RESOURCES + 4;
	struct octep_pf *pf = pci_get_drvdata(pf_dev);
	struct pci_bus_region bus_region;

	vf_res->name = pci_name(vf_dev);
	vf_res->flags = pf_res->flags;
	vf_res->parent = (pf_dev->resource + PCI_STD_RESOURCES)->parent;

	bus_region.start = pf->vf_base + idx * pf->vf_stride;
	bus_region.end = bus_region.start + pf->vf_stride - 1;
	pcibios_bus_to_resource(vf_dev->bus, vf_res, &bus_region);
}

static int octep_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
	struct octep_pf *pf = pci_get_drvdata(pdev);
	u8 __iomem *addr = pf->base[OCTEP_HW_MBOX_BAR];
	struct pci_dev *vf_pdev = NULL;
	bool done = false;
	int index = 0;
	int ret, i;

	ret = pci_enable_sriov(pdev, num_vfs);
	if (ret)
		return ret;

	pf->enabled_vfs = num_vfs;

	while ((vf_pdev = pci_get_device(PCI_VENDOR_ID_CAVIUM, PCI_ANY_ID, vf_pdev))) {
		if (vf_pdev->device != pf->vf_devid)
			continue;

		octep_vdpa_assign_barspace(vf_pdev, pdev, index);
		if (++index == num_vfs) {
			done = true;
			break;
		}
	}

	if (done) {
		for (i = 0; i < pf->enabled_vfs; i++)
			writeq(OCTEP_DEV_READY_SIGNATURE, addr + OCTEP_PF_MBOX_DATA(i));
	}

	return num_vfs;
}

static int octep_sriov_disable(struct pci_dev *pdev)
{
	struct octep_pf *pf = pci_get_drvdata(pdev);

	if (!pci_num_vf(pdev))
		return 0;

	pci_disable_sriov(pdev);
	pf->enabled_vfs = 0;

	return 0;
}

static int octep_vdpa_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	if (num_vfs > 0)
		return octep_sriov_enable(pdev, num_vfs);
	else
		return octep_sriov_disable(pdev);
}

static u16 octep_get_vf_devid(struct pci_dev *pdev)
{
	u16 did;

	switch (pdev->device) {
	case OCTEP_VDPA_DEVID_CN106K_PF:
		did = OCTEP_VDPA_DEVID_CN106K_VF;
		break;
	case OCTEP_VDPA_DEVID_CN105K_PF:
		did = OCTEP_VDPA_DEVID_CN105K_VF;
		break;
	case OCTEP_VDPA_DEVID_CN103K_PF:
		did = OCTEP_VDPA_DEVID_CN103K_VF;
		break;
	default:
		did = 0xFFFF;
		break;
	}

	return did;
}

static int octep_vdpa_pf_setup(struct octep_pf *octpf)
{
	u8 __iomem *addr = octpf->base[OCTEP_HW_MBOX_BAR];
	struct pci_dev *pdev = octpf->pdev;
	int totalvfs;
	size_t len;
	u64 val;

	totalvfs = pci_sriov_get_totalvfs(pdev);
	if (unlikely(!totalvfs)) {
		dev_info(&pdev->dev, "Total VFs are %d in PF sriov configuration\n", totalvfs);
		return 0;
	}

	addr = octpf->base[OCTEP_HW_MBOX_BAR];
	val = readq(addr + OCTEP_EPF_RINFO(0));
	if (val == 0) {
		dev_err(&pdev->dev, "Invalid device configuration\n");
		return -EINVAL;
	}

	if (OCTEP_EPF_RINFO_RPVF(val) != BIT_ULL(0)) {
		val &= ~GENMASK_ULL(35, 32);
		val |= BIT_ULL(32);
		writeq(val, addr + OCTEP_EPF_RINFO(0));
	}

	len = pci_resource_len(pdev, OCTEP_HW_CAPS_BAR);

	octpf->vf_stride = len / totalvfs;
	octpf->vf_devid = octep_get_vf_devid(pdev);

	octep_vdpa_pf_bar_shrink(octpf);

	return 0;
}

static int octep_vdpa_probe_pf(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct octep_pf *octpf;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(dev, "Failed to enable device\n");
		return ret;
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(dev, "No usable DMA configuration\n");
		return ret;
	}
	octpf = devm_kzalloc(dev, sizeof(*octpf), GFP_KERNEL);
	if (!octpf)
		return -ENOMEM;

	ret = octep_iomap_region(pdev, octpf->base, OCTEP_HW_MBOX_BAR);
	if (ret)
		return ret;

	pci_set_master(pdev);
	pci_set_drvdata(pdev, octpf);
	octpf->pdev = pdev;

	ret = octep_vdpa_pf_setup(octpf);
	if (ret)
		goto unmap_region;

	return 0;

unmap_region:
	octep_iounmap_region(pdev, octpf->base, OCTEP_HW_MBOX_BAR);
	return ret;
}

static int octep_vdpa_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	if (pdev->is_virtfn)
		return octep_vdpa_probe_vf(pdev);
	else
		return octep_vdpa_probe_pf(pdev);
}

static struct pci_device_id octep_pci_vdpa_map[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_VDPA_DEVID_CN106K_PF) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_VDPA_DEVID_CN106K_VF) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_VDPA_DEVID_CN105K_PF) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_VDPA_DEVID_CN105K_VF) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_VDPA_DEVID_CN103K_PF) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_VDPA_DEVID_CN103K_VF) },
	{ 0 },
};

static struct pci_driver octep_pci_vdpa = {
	.name     = OCTEP_VDPA_DRIVER_NAME,
	.id_table = octep_pci_vdpa_map,
	.probe    = octep_vdpa_probe,
	.remove   = octep_vdpa_remove,
	.sriov_configure = octep_vdpa_sriov_configure
};

module_pci_driver(octep_pci_vdpa);

MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION("Marvell Octeon PCIe endpoint vDPA driver");
MODULE_LICENSE("GPL");
