// SPDX-License-Identifier: GPL-2.0-only
/*
 * SolidRun DPU driver for control plane
 *
 * Copyright (C) 2022-2023 SolidRun
 *
 * Author: Alvaro Karsz <alvaro.karsz@solid-run.com>
 *
 */
#include <linux/iopoll.h>

#include "snet_vdpa.h"

/* SNET DPU device ID */
#define SNET_DEVICE_ID          0x1000
/* SNET signature */
#define SNET_SIGNATURE          0xD0D06363
/* Max. config version that we can work with */
#define SNET_CFG_VERSION        0x2
/* Queue align */
#define SNET_QUEUE_ALIGNMENT    PAGE_SIZE
/* Kick value to notify that new data is available */
#define SNET_KICK_VAL           0x1
#define SNET_CONFIG_OFF         0x0
/* How long we are willing to wait for a SNET device */
#define SNET_DETECT_TIMEOUT	5000000
/* How long should we wait for the DPU to read our config */
#define SNET_READ_CFG_TIMEOUT	3000000
/* Size of configs written to the DPU */
#define SNET_GENERAL_CFG_LEN	36
#define SNET_GENERAL_CFG_VQ_LEN	40

static struct snet *vdpa_to_snet(struct vdpa_device *vdpa)
{
	return container_of(vdpa, struct snet, vdpa);
}

static irqreturn_t snet_cfg_irq_hndlr(int irq, void *data)
{
	struct snet *snet = data;
	/* Call callback if any */
	if (likely(snet->cb.callback))
		return snet->cb.callback(snet->cb.private);

	return IRQ_HANDLED;
}

static irqreturn_t snet_vq_irq_hndlr(int irq, void *data)
{
	struct snet_vq *vq = data;
	/* Call callback if any */
	if (likely(vq->cb.callback))
		return vq->cb.callback(vq->cb.private);

	return IRQ_HANDLED;
}

static void snet_free_irqs(struct snet *snet)
{
	struct psnet *psnet = snet->psnet;
	struct pci_dev *pdev;
	u32 i;

	/* Which Device allcoated the IRQs? */
	if (PSNET_FLAG_ON(psnet, SNET_CFG_FLAG_IRQ_PF))
		pdev = snet->pdev->physfn;
	else
		pdev = snet->pdev;

	/* Free config's IRQ */
	if (snet->cfg_irq != -1) {
		devm_free_irq(&pdev->dev, snet->cfg_irq, snet);
		snet->cfg_irq = -1;
	}
	/* Free VQ IRQs */
	for (i = 0; i < snet->cfg->vq_num; i++) {
		if (snet->vqs[i] && snet->vqs[i]->irq != -1) {
			devm_free_irq(&pdev->dev, snet->vqs[i]->irq, snet->vqs[i]);
			snet->vqs[i]->irq = -1;
		}
	}

	/* IRQ vectors are freed when the pci remove callback is called */
}

static int snet_set_vq_address(struct vdpa_device *vdev, u16 idx, u64 desc_area,
			       u64 driver_area, u64 device_area)
{
	struct snet *snet = vdpa_to_snet(vdev);
	/* save received parameters in vqueue sturct */
	snet->vqs[idx]->desc_area = desc_area;
	snet->vqs[idx]->driver_area = driver_area;
	snet->vqs[idx]->device_area = device_area;

	return 0;
}

static void snet_set_vq_num(struct vdpa_device *vdev, u16 idx, u32 num)
{
	struct snet *snet = vdpa_to_snet(vdev);
	/* save num in vqueue */
	snet->vqs[idx]->num = num;
}

static void snet_kick_vq(struct vdpa_device *vdev, u16 idx)
{
	struct snet *snet = vdpa_to_snet(vdev);
	/* not ready - ignore */
	if (unlikely(!snet->vqs[idx]->ready))
		return;

	iowrite32(SNET_KICK_VAL, snet->vqs[idx]->kick_ptr);
}

static void snet_kick_vq_with_data(struct vdpa_device *vdev, u32 data)
{
	struct snet *snet = vdpa_to_snet(vdev);
	u16 idx = data & 0xFFFF;

	/* not ready - ignore */
	if (unlikely(!snet->vqs[idx]->ready))
		return;

	iowrite32((data & 0xFFFF0000) | SNET_KICK_VAL, snet->vqs[idx]->kick_ptr);
}

static void snet_set_vq_cb(struct vdpa_device *vdev, u16 idx, struct vdpa_callback *cb)
{
	struct snet *snet = vdpa_to_snet(vdev);

	snet->vqs[idx]->cb.callback = cb->callback;
	snet->vqs[idx]->cb.private = cb->private;
}

static void snet_set_vq_ready(struct vdpa_device *vdev, u16 idx, bool ready)
{
	struct snet *snet = vdpa_to_snet(vdev);

	snet->vqs[idx]->ready = ready;
}

static bool snet_get_vq_ready(struct vdpa_device *vdev, u16 idx)
{
	struct snet *snet = vdpa_to_snet(vdev);

	return snet->vqs[idx]->ready;
}

static bool snet_vq_state_is_initial(struct snet *snet, const struct vdpa_vq_state *state)
{
	if (SNET_HAS_FEATURE(snet, VIRTIO_F_RING_PACKED)) {
		const struct vdpa_vq_state_packed *p = &state->packed;

		if (p->last_avail_counter == 1 && p->last_used_counter == 1 &&
		    p->last_avail_idx == 0 && p->last_used_idx == 0)
			return true;
	} else {
		const struct vdpa_vq_state_split *s = &state->split;

		if (s->avail_index == 0)
			return true;
	}

	return false;
}

static int snet_set_vq_state(struct vdpa_device *vdev, u16 idx, const struct vdpa_vq_state *state)
{
	struct snet *snet = vdpa_to_snet(vdev);

	/* We can set any state for config version 2+ */
	if (SNET_CFG_VER(snet, 2)) {
		memcpy(&snet->vqs[idx]->vq_state, state, sizeof(*state));
		return 0;
	}

	/* Older config - we can't set the VQ state.
	 * Return 0 only if this is the initial state we use in the DPU.
	 */
	if (snet_vq_state_is_initial(snet, state))
		return 0;

	return -EOPNOTSUPP;
}

static int snet_get_vq_state(struct vdpa_device *vdev, u16 idx, struct vdpa_vq_state *state)
{
	struct snet *snet = vdpa_to_snet(vdev);

	return snet_read_vq_state(snet, idx, state);
}

static int snet_get_vq_irq(struct vdpa_device *vdev, u16 idx)
{
	struct snet *snet = vdpa_to_snet(vdev);

	return snet->vqs[idx]->irq;
}

static u32 snet_get_vq_align(struct vdpa_device *vdev)
{
	return (u32)SNET_QUEUE_ALIGNMENT;
}

static int snet_reset_dev(struct snet *snet)
{
	struct pci_dev *pdev = snet->pdev;
	int ret = 0;
	u32 i;

	/* If status is 0, nothing to do */
	if (!snet->status)
		return 0;

	/* If DPU started, destroy it */
	if (snet->status & VIRTIO_CONFIG_S_DRIVER_OK)
		ret = snet_destroy_dev(snet);

	/* Clear VQs */
	for (i = 0; i < snet->cfg->vq_num; i++) {
		if (!snet->vqs[i])
			continue;
		snet->vqs[i]->cb.callback = NULL;
		snet->vqs[i]->cb.private = NULL;
		snet->vqs[i]->desc_area = 0;
		snet->vqs[i]->device_area = 0;
		snet->vqs[i]->driver_area = 0;
		snet->vqs[i]->ready = false;
	}

	/* Clear config callback */
	snet->cb.callback = NULL;
	snet->cb.private = NULL;
	/* Free IRQs */
	snet_free_irqs(snet);
	/* Reset status */
	snet->status = 0;
	snet->dpu_ready = false;

	if (ret)
		SNET_WARN(pdev, "Incomplete reset to SNET[%u] device, err: %d\n", snet->sid, ret);
	else
		SNET_DBG(pdev, "Reset SNET[%u] device\n", snet->sid);

	return 0;
}

static int snet_reset(struct vdpa_device *vdev)
{
	struct snet *snet = vdpa_to_snet(vdev);

	return snet_reset_dev(snet);
}

static size_t snet_get_config_size(struct vdpa_device *vdev)
{
	struct snet *snet = vdpa_to_snet(vdev);

	return (size_t)snet->cfg->cfg_size;
}

static u64 snet_get_features(struct vdpa_device *vdev)
{
	struct snet *snet = vdpa_to_snet(vdev);

	return snet->cfg->features;
}

static int snet_set_drv_features(struct vdpa_device *vdev, u64 features)
{
	struct snet *snet = vdpa_to_snet(vdev);

	snet->negotiated_features = snet->cfg->features & features;
	return 0;
}

static u64 snet_get_drv_features(struct vdpa_device *vdev)
{
	struct snet *snet = vdpa_to_snet(vdev);

	return snet->negotiated_features;
}

static u16 snet_get_vq_num_max(struct vdpa_device *vdev)
{
	struct snet *snet = vdpa_to_snet(vdev);

	return (u16)snet->cfg->vq_size;
}

static void snet_set_config_cb(struct vdpa_device *vdev, struct vdpa_callback *cb)
{
	struct snet *snet = vdpa_to_snet(vdev);

	snet->cb.callback = cb->callback;
	snet->cb.private = cb->private;
}

static u32 snet_get_device_id(struct vdpa_device *vdev)
{
	struct snet *snet = vdpa_to_snet(vdev);

	return snet->cfg->virtio_id;
}

static u32 snet_get_vendor_id(struct vdpa_device *vdev)
{
	return (u32)PCI_VENDOR_ID_SOLIDRUN;
}

static u8 snet_get_status(struct vdpa_device *vdev)
{
	struct snet *snet = vdpa_to_snet(vdev);

	return snet->status;
}

static int snet_write_conf(struct snet *snet)
{
	u32 off, i, tmp;
	int ret;

	/* No need to write the config twice */
	if (snet->dpu_ready)
		return true;

	/* Snet data :
	 *
	 * General data: SNET_GENERAL_CFG_LEN bytes long
	 *  0             0x4       0x8        0xC               0x10      0x14        0x1C     0x24
	 *  | MAGIC NUMBER | CFG VER | SNET SID | NUMBER OF QUEUES | IRQ IDX | FEATURES |  RSVD  |
	 *
	 * For every VQ: SNET_GENERAL_CFG_VQ_LEN bytes long
	 * 0                          0x4        0x8
	 * |  VQ SID  AND  QUEUE SIZE | IRQ Index |
	 * |             DESC AREA                |
	 * |            DEVICE AREA               |
	 * |            DRIVER AREA               |
	 * |    VQ STATE (CFG 2+)     |   RSVD    |
	 *
	 * Magic number should be written last, this is the DPU indication that the data is ready
	 */

	/* Init offset */
	off = snet->psnet->cfg.host_cfg_off;

	/* Ignore magic number for now */
	off += 4;
	snet_write32(snet, off, snet->psnet->negotiated_cfg_ver);
	off += 4;
	snet_write32(snet, off, snet->sid);
	off += 4;
	snet_write32(snet, off, snet->cfg->vq_num);
	off += 4;
	snet_write32(snet, off, snet->cfg_irq_idx);
	off += 4;
	snet_write64(snet, off, snet->negotiated_features);
	off += 8;
	/* Ignore reserved */
	off += 8;
	/* Write VQs */
	for (i = 0 ; i < snet->cfg->vq_num ; i++) {
		tmp = (i << 16) | (snet->vqs[i]->num & 0xFFFF);
		snet_write32(snet, off, tmp);
		off += 4;
		snet_write32(snet, off, snet->vqs[i]->irq_idx);
		off += 4;
		snet_write64(snet, off, snet->vqs[i]->desc_area);
		off += 8;
		snet_write64(snet, off, snet->vqs[i]->device_area);
		off += 8;
		snet_write64(snet, off, snet->vqs[i]->driver_area);
		off += 8;
		/* Write VQ state if config version is 2+ */
		if (SNET_CFG_VER(snet, 2))
			snet_write32(snet, off, *(u32 *)&snet->vqs[i]->vq_state);
		off += 4;

		/* Ignore reserved */
		off += 4;
	}

	/* Write magic number - data is ready */
	snet_write32(snet, snet->psnet->cfg.host_cfg_off, SNET_SIGNATURE);

	/* The DPU will ACK the config by clearing the signature */
	ret = readx_poll_timeout(ioread32, snet->bar + snet->psnet->cfg.host_cfg_off,
				 tmp, !tmp, 10, SNET_READ_CFG_TIMEOUT);
	if (ret) {
		SNET_ERR(snet->pdev, "Timeout waiting for the DPU to read the config\n");
		return false;
	}

	/* set DPU flag */
	snet->dpu_ready = true;

	return true;
}

static int snet_request_irqs(struct pci_dev *pdev, struct snet *snet)
{
	int ret, i, irq;

	/* Request config IRQ */
	irq = pci_irq_vector(pdev, snet->cfg_irq_idx);
	ret = devm_request_irq(&pdev->dev, irq, snet_cfg_irq_hndlr, 0,
			       snet->cfg_irq_name, snet);
	if (ret) {
		SNET_ERR(pdev, "Failed to request IRQ\n");
		return ret;
	}
	snet->cfg_irq = irq;

	/* Request IRQ for every VQ */
	for (i = 0; i < snet->cfg->vq_num; i++) {
		irq = pci_irq_vector(pdev, snet->vqs[i]->irq_idx);
		ret = devm_request_irq(&pdev->dev, irq, snet_vq_irq_hndlr, 0,
				       snet->vqs[i]->irq_name, snet->vqs[i]);
		if (ret) {
			SNET_ERR(pdev, "Failed to request IRQ\n");
			return ret;
		}
		snet->vqs[i]->irq = irq;
	}
	return 0;
}

static void snet_set_status(struct vdpa_device *vdev, u8 status)
{
	struct snet *snet = vdpa_to_snet(vdev);
	struct psnet *psnet = snet->psnet;
	struct pci_dev *pdev = snet->pdev;
	int ret;
	bool pf_irqs;

	if (status == snet->status)
		return;

	if ((status & VIRTIO_CONFIG_S_DRIVER_OK) &&
	    !(snet->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
		/* Request IRQs */
		pf_irqs = PSNET_FLAG_ON(psnet, SNET_CFG_FLAG_IRQ_PF);
		ret = snet_request_irqs(pf_irqs ? pdev->physfn : pdev, snet);
		if (ret)
			goto set_err;

		/* Write config to the DPU */
		if (snet_write_conf(snet)) {
			SNET_INFO(pdev, "Create SNET[%u] device\n", snet->sid);
		} else {
			snet_free_irqs(snet);
			goto set_err;
		}
	}

	/* Save the new status */
	snet->status = status;
	return;

set_err:
	snet->status |= VIRTIO_CONFIG_S_FAILED;
}

static void snet_get_config(struct vdpa_device *vdev, unsigned int offset,
			    void *buf, unsigned int len)
{
	struct snet *snet = vdpa_to_snet(vdev);
	void __iomem *cfg_ptr = snet->cfg->virtio_cfg + offset;
	u8 *buf_ptr = buf;
	u32 i;

	/* check for offset error */
	if (offset + len > snet->cfg->cfg_size)
		return;

	/* Write into buffer */
	for (i = 0; i < len; i++)
		*buf_ptr++ = ioread8(cfg_ptr + i);
}

static void snet_set_config(struct vdpa_device *vdev, unsigned int offset,
			    const void *buf, unsigned int len)
{
	struct snet *snet = vdpa_to_snet(vdev);
	void __iomem *cfg_ptr = snet->cfg->virtio_cfg + offset;
	const u8 *buf_ptr = buf;
	u32 i;

	/* check for offset error */
	if (offset + len > snet->cfg->cfg_size)
		return;

	/* Write into PCI BAR */
	for (i = 0; i < len; i++)
		iowrite8(*buf_ptr++, cfg_ptr + i);
}

static int snet_suspend(struct vdpa_device *vdev)
{
	struct snet *snet = vdpa_to_snet(vdev);
	int ret;

	ret = snet_suspend_dev(snet);
	if (ret)
		SNET_ERR(snet->pdev, "SNET[%u] suspend failed, err: %d\n", snet->sid, ret);
	else
		SNET_DBG(snet->pdev, "Suspend SNET[%u] device\n", snet->sid);

	return ret;
}

static const struct vdpa_config_ops snet_config_ops = {
	.set_vq_address         = snet_set_vq_address,
	.set_vq_num             = snet_set_vq_num,
	.kick_vq                = snet_kick_vq,
	.kick_vq_with_data	= snet_kick_vq_with_data,
	.set_vq_cb              = snet_set_vq_cb,
	.set_vq_ready           = snet_set_vq_ready,
	.get_vq_ready           = snet_get_vq_ready,
	.set_vq_state           = snet_set_vq_state,
	.get_vq_state           = snet_get_vq_state,
	.get_vq_irq		= snet_get_vq_irq,
	.get_vq_align           = snet_get_vq_align,
	.reset                  = snet_reset,
	.get_config_size        = snet_get_config_size,
	.get_device_features    = snet_get_features,
	.set_driver_features    = snet_set_drv_features,
	.get_driver_features    = snet_get_drv_features,
	.get_vq_num_min         = snet_get_vq_num_max,
	.get_vq_num_max         = snet_get_vq_num_max,
	.set_config_cb          = snet_set_config_cb,
	.get_device_id          = snet_get_device_id,
	.get_vendor_id          = snet_get_vendor_id,
	.get_status             = snet_get_status,
	.set_status             = snet_set_status,
	.get_config             = snet_get_config,
	.set_config             = snet_set_config,
	.suspend		= snet_suspend,
};

static int psnet_open_pf_bar(struct pci_dev *pdev, struct psnet *psnet)
{
	char name[50];
	int ret, i, mask = 0;
	/* We don't know which BAR will be used to communicate..
	 * We will map every bar with len > 0.
	 *
	 * Later, we will discover the BAR and unmap all other BARs.
	 */
	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (pci_resource_len(pdev, i))
			mask |= (1 << i);
	}

	/* No BAR can be used.. */
	if (!mask) {
		SNET_ERR(pdev, "Failed to find a PCI BAR\n");
		return -ENODEV;
	}

	snprintf(name, sizeof(name), "psnet[%s]-bars", pci_name(pdev));
	ret = pcim_iomap_regions(pdev, mask, name);
	if (ret) {
		SNET_ERR(pdev, "Failed to request and map PCI BARs\n");
		return ret;
	}

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (mask & (1 << i))
			psnet->bars[i] = pcim_iomap_table(pdev)[i];
	}

	return 0;
}

static int snet_open_vf_bar(struct pci_dev *pdev, struct snet *snet)
{
	char name[50];
	int ret;

	snprintf(name, sizeof(name), "snet[%s]-bar", pci_name(pdev));
	/* Request and map BAR */
	ret = pcim_iomap_regions(pdev, BIT(snet->psnet->cfg.vf_bar), name);
	if (ret) {
		SNET_ERR(pdev, "Failed to request and map PCI BAR for a VF\n");
		return ret;
	}

	snet->bar = pcim_iomap_table(pdev)[snet->psnet->cfg.vf_bar];

	return 0;
}

static void snet_free_cfg(struct snet_cfg *cfg)
{
	u32 i;

	if (!cfg->devs)
		return;

	/* Free devices */
	for (i = 0; i < cfg->devices_num; i++) {
		if (!cfg->devs[i])
			break;

		kfree(cfg->devs[i]);
	}
	/* Free pointers to devices */
	kfree(cfg->devs);
}

/* Detect which BAR is used for communication with the device. */
static int psnet_detect_bar(struct psnet *psnet, u32 off)
{
	unsigned long exit_time;
	int i;

	exit_time = jiffies + usecs_to_jiffies(SNET_DETECT_TIMEOUT);

	/* SNET DPU will write SNET's signature when the config is ready. */
	while (time_before(jiffies, exit_time)) {
		for (i = 0; i < PCI_STD_NUM_BARS; i++) {
			/* Is this BAR mapped? */
			if (!psnet->bars[i])
				continue;

			if (ioread32(psnet->bars[i] + off) == SNET_SIGNATURE)
				return i;
		}
		usleep_range(1000, 10000);
	}

	return -ENODEV;
}

static void psnet_unmap_unused_bars(struct pci_dev *pdev, struct psnet *psnet)
{
	int i, mask = 0;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (psnet->bars[i] && i != psnet->barno)
			mask |= (1 << i);
	}

	if (mask)
		pcim_iounmap_regions(pdev, mask);
}

/* Read SNET config from PCI BAR */
static int psnet_read_cfg(struct pci_dev *pdev, struct psnet *psnet)
{
	struct snet_cfg *cfg = &psnet->cfg;
	u32 i, off;
	int barno;

	/* Move to where the config starts */
	off = SNET_CONFIG_OFF;

	/* Find BAR used for communication */
	barno = psnet_detect_bar(psnet, off);
	if (barno < 0) {
		SNET_ERR(pdev, "SNET config is not ready.\n");
		return barno;
	}

	/* Save used BAR number and unmap all other BARs */
	psnet->barno = barno;
	SNET_DBG(pdev, "Using BAR number %d\n", barno);

	psnet_unmap_unused_bars(pdev, psnet);

	/* load config from BAR */
	cfg->key = psnet_read32(psnet, off);
	off += 4;
	cfg->cfg_size = psnet_read32(psnet, off);
	off += 4;
	cfg->cfg_ver = psnet_read32(psnet, off);
	off += 4;
	/* The negotiated config version is the lower one between this driver's config
	 * and the DPU's.
	 */
	psnet->negotiated_cfg_ver = min_t(u32, cfg->cfg_ver, SNET_CFG_VERSION);
	SNET_DBG(pdev, "SNET config version %u\n", psnet->negotiated_cfg_ver);

	cfg->vf_num = psnet_read32(psnet, off);
	off += 4;
	cfg->vf_bar = psnet_read32(psnet, off);
	off += 4;
	cfg->host_cfg_off = psnet_read32(psnet, off);
	off += 4;
	cfg->max_size_host_cfg = psnet_read32(psnet, off);
	off += 4;
	cfg->virtio_cfg_off = psnet_read32(psnet, off);
	off += 4;
	cfg->kick_off = psnet_read32(psnet, off);
	off += 4;
	cfg->hwmon_off = psnet_read32(psnet, off);
	off += 4;
	cfg->ctrl_off = psnet_read32(psnet, off);
	off += 4;
	cfg->flags = psnet_read32(psnet, off);
	off += 4;
	/* Ignore Reserved */
	off += sizeof(cfg->rsvd);

	cfg->devices_num = psnet_read32(psnet, off);
	off += 4;
	/* Allocate memory to hold pointer to the devices */
	cfg->devs = kcalloc(cfg->devices_num, sizeof(void *), GFP_KERNEL);
	if (!cfg->devs)
		return -ENOMEM;

	/* Load device configuration from BAR */
	for (i = 0; i < cfg->devices_num; i++) {
		cfg->devs[i] = kzalloc(sizeof(*cfg->devs[i]), GFP_KERNEL);
		if (!cfg->devs[i]) {
			snet_free_cfg(cfg);
			return -ENOMEM;
		}
		/* Read device config */
		cfg->devs[i]->virtio_id = psnet_read32(psnet, off);
		off += 4;
		cfg->devs[i]->vq_num = psnet_read32(psnet, off);
		off += 4;
		cfg->devs[i]->vq_size = psnet_read32(psnet, off);
		off += 4;
		cfg->devs[i]->vfid = psnet_read32(psnet, off);
		off += 4;
		cfg->devs[i]->features = psnet_read64(psnet, off);
		off += 8;
		/* Ignore Reserved */
		off += sizeof(cfg->devs[i]->rsvd);

		cfg->devs[i]->cfg_size = psnet_read32(psnet, off);
		off += 4;

		/* Is the config witten to the DPU going to be too big? */
		if (SNET_GENERAL_CFG_LEN + SNET_GENERAL_CFG_VQ_LEN * cfg->devs[i]->vq_num >
		    cfg->max_size_host_cfg) {
			SNET_ERR(pdev, "Failed to read SNET config, the config is too big..\n");
			snet_free_cfg(cfg);
			return -EINVAL;
		}
	}
	return 0;
}

static int psnet_alloc_irq_vector(struct pci_dev *pdev, struct psnet *psnet)
{
	int ret = 0;
	u32 i, irq_num = 0;

	/* Let's count how many IRQs we need, 1 for every VQ + 1 for config change */
	for (i = 0; i < psnet->cfg.devices_num; i++)
		irq_num += psnet->cfg.devs[i]->vq_num + 1;

	ret = pci_alloc_irq_vectors(pdev, irq_num, irq_num, PCI_IRQ_MSIX);
	if (ret != irq_num) {
		SNET_ERR(pdev, "Failed to allocate IRQ vectors\n");
		return ret;
	}
	SNET_DBG(pdev, "Allocated %u IRQ vectors from physical function\n", irq_num);

	return 0;
}

static int snet_alloc_irq_vector(struct pci_dev *pdev, struct snet_dev_cfg *snet_cfg)
{
	int ret = 0;
	u32 irq_num;

	/* We want 1 IRQ for every VQ + 1 for config change events */
	irq_num = snet_cfg->vq_num + 1;

	ret = pci_alloc_irq_vectors(pdev, irq_num, irq_num, PCI_IRQ_MSIX);
	if (ret <= 0) {
		SNET_ERR(pdev, "Failed to allocate IRQ vectors\n");
		return ret;
	}

	return 0;
}

static void snet_free_vqs(struct snet *snet)
{
	u32 i;

	if (!snet->vqs)
		return;

	for (i = 0 ; i < snet->cfg->vq_num ; i++) {
		if (!snet->vqs[i])
			break;

		kfree(snet->vqs[i]);
	}
	kfree(snet->vqs);
}

static int snet_build_vqs(struct snet *snet)
{
	u32 i;
	/* Allocate the VQ pointers array */
	snet->vqs = kcalloc(snet->cfg->vq_num, sizeof(void *), GFP_KERNEL);
	if (!snet->vqs)
		return -ENOMEM;

	/* Allocate the VQs */
	for (i = 0; i < snet->cfg->vq_num; i++) {
		snet->vqs[i] = kzalloc(sizeof(*snet->vqs[i]), GFP_KERNEL);
		if (!snet->vqs[i]) {
			snet_free_vqs(snet);
			return -ENOMEM;
		}
		/* Reset IRQ num */
		snet->vqs[i]->irq = -1;
		/* VQ serial ID */
		snet->vqs[i]->sid = i;
		/* Kick address - every VQ gets 4B */
		snet->vqs[i]->kick_ptr = snet->bar + snet->psnet->cfg.kick_off +
					 snet->vqs[i]->sid * 4;
		/* Clear kick address for this VQ */
		iowrite32(0, snet->vqs[i]->kick_ptr);
	}
	return 0;
}

static int psnet_get_next_irq_num(struct psnet *psnet)
{
	int irq;

	spin_lock(&psnet->lock);
	irq = psnet->next_irq++;
	spin_unlock(&psnet->lock);

	return irq;
}

static void snet_reserve_irq_idx(struct pci_dev *pdev, struct snet *snet)
{
	struct psnet *psnet = snet->psnet;
	int  i;

	/* one IRQ for every VQ, and one for config changes */
	snet->cfg_irq_idx = psnet_get_next_irq_num(psnet);
	snprintf(snet->cfg_irq_name, SNET_NAME_SIZE, "snet[%s]-cfg[%d]",
		 pci_name(pdev), snet->cfg_irq_idx);

	for (i = 0; i < snet->cfg->vq_num; i++) {
		/* Get next free IRQ ID */
		snet->vqs[i]->irq_idx = psnet_get_next_irq_num(psnet);
		/* Write IRQ name */
		snprintf(snet->vqs[i]->irq_name, SNET_NAME_SIZE, "snet[%s]-vq[%d]",
			 pci_name(pdev), snet->vqs[i]->irq_idx);
	}
}

/* Find a device config based on virtual function id */
static struct snet_dev_cfg *snet_find_dev_cfg(struct snet_cfg *cfg, u32 vfid)
{
	u32 i;

	for (i = 0; i < cfg->devices_num; i++) {
		if (cfg->devs[i]->vfid == vfid)
			return cfg->devs[i];
	}
	/* Oppss.. no config found.. */
	return NULL;
}

/* Probe function for a physical PCI function */
static int snet_vdpa_probe_pf(struct pci_dev *pdev)
{
	struct psnet *psnet;
	int ret = 0;
	bool pf_irqs = false;

	ret = pcim_enable_device(pdev);
	if (ret) {
		SNET_ERR(pdev, "Failed to enable PCI device\n");
		return ret;
	}

	/* Allocate a PCI physical function device */
	psnet = kzalloc(sizeof(*psnet), GFP_KERNEL);
	if (!psnet)
		return -ENOMEM;

	/* Init PSNET spinlock */
	spin_lock_init(&psnet->lock);

	pci_set_master(pdev);
	pci_set_drvdata(pdev, psnet);

	/* Open SNET MAIN BAR */
	ret = psnet_open_pf_bar(pdev, psnet);
	if (ret)
		goto free_psnet;

	/* Try to read SNET's config from PCI BAR */
	ret = psnet_read_cfg(pdev, psnet);
	if (ret)
		goto free_psnet;

	/* If SNET_CFG_FLAG_IRQ_PF flag is set, we should use
	 * PF MSI-X vectors
	 */
	pf_irqs = PSNET_FLAG_ON(psnet, SNET_CFG_FLAG_IRQ_PF);

	if (pf_irqs) {
		ret = psnet_alloc_irq_vector(pdev, psnet);
		if (ret)
			goto free_cfg;
	}

	SNET_DBG(pdev, "Enable %u virtual functions\n", psnet->cfg.vf_num);
	ret = pci_enable_sriov(pdev, psnet->cfg.vf_num);
	if (ret) {
		SNET_ERR(pdev, "Failed to enable SR-IOV\n");
		goto free_irq;
	}

	/* Create HW monitor device */
	if (PSNET_FLAG_ON(psnet, SNET_CFG_FLAG_HWMON)) {
#if IS_ENABLED(CONFIG_HWMON)
		psnet_create_hwmon(pdev);
#else
		SNET_WARN(pdev, "Can't start HWMON, CONFIG_HWMON is not enabled\n");
#endif
	}

	return 0;

free_irq:
	if (pf_irqs)
		pci_free_irq_vectors(pdev);
free_cfg:
	snet_free_cfg(&psnet->cfg);
free_psnet:
	kfree(psnet);
	return ret;
}

/* Probe function for a virtual PCI function */
static int snet_vdpa_probe_vf(struct pci_dev *pdev)
{
	struct pci_dev *pdev_pf = pdev->physfn;
	struct psnet *psnet = pci_get_drvdata(pdev_pf);
	struct snet_dev_cfg *dev_cfg;
	struct snet *snet;
	u32 vfid;
	int ret;
	bool pf_irqs = false;

	/* Get virtual function id.
	 * (the DPU counts the VFs from 1)
	 */
	ret = pci_iov_vf_id(pdev);
	if (ret < 0) {
		SNET_ERR(pdev, "Failed to find a VF id\n");
		return ret;
	}
	vfid = ret + 1;

	/* Find the snet_dev_cfg based on vfid */
	dev_cfg = snet_find_dev_cfg(&psnet->cfg, vfid);
	if (!dev_cfg) {
		SNET_WARN(pdev, "Failed to find a VF config..\n");
		return -ENODEV;
	}

	/* Which PCI device should allocate the IRQs?
	 * If the SNET_CFG_FLAG_IRQ_PF flag set, the PF device allocates the IRQs
	 */
	pf_irqs = PSNET_FLAG_ON(psnet, SNET_CFG_FLAG_IRQ_PF);

	ret = pcim_enable_device(pdev);
	if (ret) {
		SNET_ERR(pdev, "Failed to enable PCI VF device\n");
		return ret;
	}

	/* Request for MSI-X IRQs */
	if (!pf_irqs) {
		ret = snet_alloc_irq_vector(pdev, dev_cfg);
		if (ret)
			return ret;
	}

	/* Allocate vdpa device */
	snet = vdpa_alloc_device(struct snet, vdpa, &pdev->dev, &snet_config_ops, 1, 1, NULL,
				 false);
	if (!snet) {
		SNET_ERR(pdev, "Failed to allocate a vdpa device\n");
		ret = -ENOMEM;
		goto free_irqs;
	}

	/* Init control mutex and spinlock */
	mutex_init(&snet->ctrl_lock);
	spin_lock_init(&snet->ctrl_spinlock);

	/* Save pci device pointer */
	snet->pdev = pdev;
	snet->psnet = psnet;
	snet->cfg = dev_cfg;
	snet->dpu_ready = false;
	snet->sid = vfid;
	/* Reset IRQ value */
	snet->cfg_irq = -1;

	ret = snet_open_vf_bar(pdev, snet);
	if (ret)
		goto put_device;

	/* Create a VirtIO config pointer */
	snet->cfg->virtio_cfg = snet->bar + snet->psnet->cfg.virtio_cfg_off;

	/* Clear control registers */
	snet_ctrl_clear(snet);

	pci_set_master(pdev);
	pci_set_drvdata(pdev, snet);

	ret = snet_build_vqs(snet);
	if (ret)
		goto put_device;

	/* Reserve IRQ indexes,
	 * The IRQs may be requested and freed multiple times,
	 * but the indexes won't change.
	 */
	snet_reserve_irq_idx(pf_irqs ? pdev_pf : pdev, snet);

	/*set DMA device*/
	snet->vdpa.dma_dev = &pdev->dev;

	/* Register VDPA device */
	ret = vdpa_register_device(&snet->vdpa, snet->cfg->vq_num);
	if (ret) {
		SNET_ERR(pdev, "Failed to register vdpa device\n");
		goto free_vqs;
	}

	return 0;

free_vqs:
	snet_free_vqs(snet);
put_device:
	put_device(&snet->vdpa.dev);
free_irqs:
	if (!pf_irqs)
		pci_free_irq_vectors(pdev);
	return ret;
}

static int snet_vdpa_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	if (pdev->is_virtfn)
		return snet_vdpa_probe_vf(pdev);
	else
		return snet_vdpa_probe_pf(pdev);
}

static void snet_vdpa_remove_pf(struct pci_dev *pdev)
{
	struct psnet *psnet = pci_get_drvdata(pdev);

	pci_disable_sriov(pdev);
	/* If IRQs are allocated from the PF, we should free the IRQs */
	if (PSNET_FLAG_ON(psnet, SNET_CFG_FLAG_IRQ_PF))
		pci_free_irq_vectors(pdev);

	snet_free_cfg(&psnet->cfg);
	kfree(psnet);
}

static void snet_vdpa_remove_vf(struct pci_dev *pdev)
{
	struct snet *snet = pci_get_drvdata(pdev);
	struct psnet *psnet = snet->psnet;

	vdpa_unregister_device(&snet->vdpa);
	snet_free_vqs(snet);
	/* If IRQs are allocated from the VF, we should free the IRQs */
	if (!PSNET_FLAG_ON(psnet, SNET_CFG_FLAG_IRQ_PF))
		pci_free_irq_vectors(pdev);
}

static void snet_vdpa_remove(struct pci_dev *pdev)
{
	if (pdev->is_virtfn)
		snet_vdpa_remove_vf(pdev);
	else
		snet_vdpa_remove_pf(pdev);
}

static struct pci_device_id snet_driver_pci_ids[] = {
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_SOLIDRUN, SNET_DEVICE_ID,
			 PCI_VENDOR_ID_SOLIDRUN, SNET_DEVICE_ID) },
	{ 0 },
};

MODULE_DEVICE_TABLE(pci, snet_driver_pci_ids);

static struct pci_driver snet_vdpa_driver = {
	.name		= "snet-vdpa-driver",
	.id_table	= snet_driver_pci_ids,
	.probe		= snet_vdpa_probe,
	.remove		= snet_vdpa_remove,
};

module_pci_driver(snet_vdpa_driver);

MODULE_AUTHOR("Alvaro Karsz <alvaro.karsz@solid-run.com>");
MODULE_DESCRIPTION("SolidRun vDPA driver");
MODULE_LICENSE("GPL v2");
