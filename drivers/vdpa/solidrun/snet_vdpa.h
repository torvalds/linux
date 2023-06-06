/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SolidRun DPU driver for control plane
 *
 * Copyright (C) 2022-2023 SolidRun
 *
 * Author: Alvaro Karsz <alvaro.karsz@solid-run.com>
 *
 */
#ifndef _SNET_VDPA_H_
#define _SNET_VDPA_H_

#include <linux/vdpa.h>
#include <linux/pci.h>

#define SNET_NAME_SIZE 256

#define SNET_ERR(pdev, fmt, ...) dev_err(&(pdev)->dev, "%s"fmt, "snet_vdpa: ", ##__VA_ARGS__)
#define SNET_WARN(pdev, fmt, ...) dev_warn(&(pdev)->dev, "%s"fmt, "snet_vdpa: ", ##__VA_ARGS__)
#define SNET_INFO(pdev, fmt, ...) dev_info(&(pdev)->dev, "%s"fmt, "snet_vdpa: ", ##__VA_ARGS__)
#define SNET_DBG(pdev, fmt, ...) dev_dbg(&(pdev)->dev, "%s"fmt, "snet_vdpa: ", ##__VA_ARGS__)
#define SNET_HAS_FEATURE(s, f) ((s)->negotiated_features & BIT_ULL(f))
/* Check if negotiated config version is at least @ver */
#define SNET_CFG_VER(snet, ver) ((snet)->psnet->negotiated_cfg_ver >= (ver))

/* VQ struct */
struct snet_vq {
	/* VQ callback */
	struct vdpa_callback cb;
	/* VQ state received from bus */
	struct vdpa_vq_state vq_state;
	/* desc base address */
	u64 desc_area;
	/* device base address */
	u64 device_area;
	/* driver base address */
	u64 driver_area;
	/* Queue size */
	u32 num;
	/* Serial ID for VQ */
	u32 sid;
	/* is ready flag */
	bool ready;
	/* IRQ number */
	u32 irq;
	/* IRQ index, DPU uses this to parse data from MSI-X table */
	u32 irq_idx;
	/* IRQ name */
	char irq_name[SNET_NAME_SIZE];
	/* pointer to mapped PCI BAR register used by this VQ to kick */
	void __iomem *kick_ptr;
};

struct snet {
	/* vdpa device */
	struct vdpa_device vdpa;
	/* Config callback */
	struct vdpa_callback cb;
	/* To lock the control mechanism */
	struct mutex ctrl_lock;
	/* Spinlock to protect critical parts in the control mechanism */
	spinlock_t ctrl_spinlock;
	/* array of virqueues */
	struct snet_vq **vqs;
	/* Used features */
	u64 negotiated_features;
	/* Device serial ID */
	u32 sid;
	/* device status */
	u8 status;
	/* boolean indicating if snet config was passed to the device */
	bool dpu_ready;
	/* IRQ number */
	u32 cfg_irq;
	/* IRQ index, DPU uses this to parse data from MSI-X table */
	u32 cfg_irq_idx;
	/* IRQ name */
	char cfg_irq_name[SNET_NAME_SIZE];
	/* BAR to access the VF */
	void __iomem *bar;
	/* PCI device */
	struct pci_dev *pdev;
	/* Pointer to snet pdev parent device */
	struct psnet *psnet;
	/* Pointer to snet config device */
	struct snet_dev_cfg *cfg;
};

struct snet_dev_cfg {
	/* Device ID following VirtIO spec. */
	u32 virtio_id;
	/* Number of VQs for this device */
	u32 vq_num;
	/* Size of every VQ */
	u32 vq_size;
	/* Virtual Function id */
	u32 vfid;
	/* Device features, following VirtIO spec */
	u64 features;
	/* Reserved for future usage */
	u32 rsvd[6];
	/* VirtIO device specific config size */
	u32 cfg_size;
	/* VirtIO device specific config address */
	void __iomem *virtio_cfg;
} __packed;

struct snet_cfg {
	/* Magic key */
	u32 key;
	/* Size of total config in bytes */
	u32 cfg_size;
	/* Config version */
	u32 cfg_ver;
	/* Number of Virtual Functions to create */
	u32 vf_num;
	/* BAR to use for the VFs */
	u32 vf_bar;
	/* Where should we write the SNET's config */
	u32 host_cfg_off;
	/* Max. allowed size for a SNET's config */
	u32 max_size_host_cfg;
	/* VirtIO config offset in BAR */
	u32 virtio_cfg_off;
	/* Offset in PCI BAR for VQ kicks */
	u32 kick_off;
	/* Offset in PCI BAR for HW monitoring */
	u32 hwmon_off;
	/* Offset in PCI BAR for Control mechanism */
	u32 ctrl_off;
	/* Config general flags - enum snet_cfg_flags */
	u32 flags;
	/* Reserved for future usage */
	u32 rsvd[6];
	/* Number of snet devices */
	u32 devices_num;
	/* The actual devices */
	struct snet_dev_cfg **devs;
} __packed;

/* SolidNET PCIe device, one device per PCIe physical function */
struct psnet {
	/* PCI BARs */
	void __iomem *bars[PCI_STD_NUM_BARS];
	/* Negotiated config version */
	u32 negotiated_cfg_ver;
	/* Next IRQ index to use in case when the IRQs are allocated from this device */
	u32 next_irq;
	/* BAR number used to communicate with the device */
	u8 barno;
	/* spinlock to protect data that can be changed by SNET devices */
	spinlock_t lock;
	/* Pointer to the device's config read from BAR */
	struct snet_cfg cfg;
	/* Name of monitor device */
	char hwmon_name[SNET_NAME_SIZE];
};

enum snet_cfg_flags {
	/* Create a HWMON device */
	SNET_CFG_FLAG_HWMON = BIT(0),
	/* USE IRQs from the physical function */
	SNET_CFG_FLAG_IRQ_PF = BIT(1),
};

#define PSNET_FLAG_ON(p, f)	((p)->cfg.flags & (f))

static inline u32 psnet_read32(struct psnet *psnet, u32 off)
{
	return ioread32(psnet->bars[psnet->barno] + off);
}

static inline u32 snet_read32(struct snet *snet, u32 off)
{
	return ioread32(snet->bar + off);
}

static inline void snet_write32(struct snet *snet, u32 off, u32 val)
{
	iowrite32(val, snet->bar + off);
}

static inline u64 psnet_read64(struct psnet *psnet, u32 off)
{
	u64 val;
	/* 64bits are written in 2 halves, low part first */
	val = (u64)psnet_read32(psnet, off);
	val |= ((u64)psnet_read32(psnet, off + 4) << 32);
	return val;
}

static inline void snet_write64(struct snet *snet, u32 off, u64 val)
{
	/* The DPU expects a 64bit integer in 2 halves, the low part first */
	snet_write32(snet, off, (u32)val);
	snet_write32(snet, off + 4, (u32)(val >> 32));
}

#if IS_ENABLED(CONFIG_HWMON)
void psnet_create_hwmon(struct pci_dev *pdev);
#endif

void snet_ctrl_clear(struct snet *snet);
int snet_destroy_dev(struct snet *snet);
int snet_read_vq_state(struct snet *snet, u16 idx, struct vdpa_vq_state *state);
int snet_suspend_dev(struct snet *snet);

#endif //_SNET_VDPA_H_
