/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Thunderbolt driver - NHI driver
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2018, Intel Corporation
 */

#ifndef DSL3510_H_
#define DSL3510_H_

#include <linux/thunderbolt.h>

enum nhi_fw_mode {
	NHI_FW_SAFE_MODE,
	NHI_FW_AUTH_MODE,
	NHI_FW_EP_MODE,
	NHI_FW_CM_MODE,
};

enum nhi_mailbox_cmd {
	NHI_MAILBOX_SAVE_DEVS = 0x05,
	NHI_MAILBOX_DISCONNECT_PCIE_PATHS = 0x06,
	NHI_MAILBOX_DRV_UNLOADS = 0x07,
	NHI_MAILBOX_DISCONNECT_PA = 0x10,
	NHI_MAILBOX_DISCONNECT_PB = 0x11,
	NHI_MAILBOX_ALLOW_ALL_DEVS = 0x23,
};

int nhi_mailbox_cmd(struct tb_nhi *nhi, enum nhi_mailbox_cmd cmd, u32 data);
enum nhi_fw_mode nhi_mailbox_mode(struct tb_nhi *nhi);
void nhi_enable_int_throttling(struct tb_nhi *nhi);
void nhi_disable_interrupts(struct tb_nhi *nhi);
void nhi_interrupt_work(struct work_struct *work);
irqreturn_t nhi_msi(int irq, void *data);
irqreturn_t ring_msix(int irq, void *data);
int nhi_probe(struct tb_nhi *nhi);
void nhi_shutdown(struct tb_nhi *nhi);
extern const struct dev_pm_ops nhi_pm_ops;

/**
 * struct tb_nhi_ops - NHI specific optional operations
 * @init: NHI specific initialization
 * @suspend_noirq: NHI specific suspend_noirq hook
 * @resume_noirq: NHI specific resume_noirq hook
 * @runtime_suspend: NHI specific runtime_suspend hook
 * @runtime_resume: NHI specific runtime_resume hook
 * @shutdown: NHI specific shutdown
 * @pre_nvm_auth: hook to run before Thunderbolt 3 NVM authentication
 * @post_nvm_auth: hook to run after Thunderbolt 3 NVM authentication
 * @request_ring_irq: NHI specific interrupt retrieval hook
 * @release_ring_irq: NHI specific interrupt release hook
 * @is_present: Whether the device is currently present on the parent bus
 * @init_interrupts: NHI specific interrupt initialization hook
 */
struct tb_nhi_ops {
	int (*init)(struct tb_nhi *nhi);
	int (*suspend_noirq)(struct tb_nhi *nhi, bool wakeup);
	int (*resume_noirq)(struct tb_nhi *nhi);
	int (*runtime_suspend)(struct tb_nhi *nhi);
	int (*runtime_resume)(struct tb_nhi *nhi);
	void (*shutdown)(struct tb_nhi *nhi);
	void (*pre_nvm_auth)(struct tb_nhi *nhi);
	void (*post_nvm_auth)(struct tb_nhi *nhi);
	int (*request_ring_irq)(struct tb_ring *ring, bool no_suspend);
	void (*release_ring_irq)(struct tb_ring *ring);
	bool (*is_present)(struct tb_nhi *nhi);
	int (*init_interrupts)(struct tb_nhi *nhi);
};

/*
 * PCI IDs used in this driver from Win Ridge forward. There is no
 * need for the PCI quirk anymore as we will use ICM also on Apple
 * hardware.
 */
#define PCI_DEVICE_ID_INTEL_MAPLE_RIDGE_2C_NHI		0x1134
#define PCI_DEVICE_ID_INTEL_MAPLE_RIDGE_4C_NHI		0x1137
#define PCI_DEVICE_ID_INTEL_WIN_RIDGE_2C_NHI            0x157d
#define PCI_DEVICE_ID_INTEL_WIN_RIDGE_2C_BRIDGE         0x157e
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_NHI		0x15bf
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_BRIDGE	0x15c0
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_NHI	0x15d2
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_BRIDGE	0x15d3
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_NHI	0x15d9
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_BRIDGE	0x15da
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_USBONLY_NHI	0x15dc
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_USBONLY_NHI	0x15dd
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_USBONLY_NHI	0x15de
#define PCI_DEVICE_ID_INTEL_TITAN_RIDGE_2C_BRIDGE	0x15e7
#define PCI_DEVICE_ID_INTEL_TITAN_RIDGE_2C_NHI		0x15e8
#define PCI_DEVICE_ID_INTEL_TITAN_RIDGE_4C_BRIDGE	0x15ea
#define PCI_DEVICE_ID_INTEL_TITAN_RIDGE_4C_NHI		0x15eb
#define PCI_DEVICE_ID_INTEL_TITAN_RIDGE_DD_BRIDGE	0x15ef
#define PCI_DEVICE_ID_INTEL_ADL_NHI0			0x463e
#define PCI_DEVICE_ID_INTEL_ADL_NHI1			0x466d
#define PCI_DEVICE_ID_INTEL_WCL_NHI0			0x4d33
#define PCI_DEVICE_ID_INTEL_BARLOW_RIDGE_HOST_80G_NHI	0x5781
#define PCI_DEVICE_ID_INTEL_BARLOW_RIDGE_HOST_40G_NHI	0x5784
#define PCI_DEVICE_ID_INTEL_BARLOW_RIDGE_HUB_80G_BRIDGE 0x5786
#define PCI_DEVICE_ID_INTEL_BARLOW_RIDGE_HUB_40G_BRIDGE 0x57a4
#define PCI_DEVICE_ID_INTEL_MTL_M_NHI0			0x7eb2
#define PCI_DEVICE_ID_INTEL_MTL_P_NHI0			0x7ec2
#define PCI_DEVICE_ID_INTEL_MTL_P_NHI1			0x7ec3
#define PCI_DEVICE_ID_INTEL_ICL_NHI1			0x8a0d
#define PCI_DEVICE_ID_INTEL_ICL_NHI0			0x8a17
#define PCI_DEVICE_ID_INTEL_TGL_NHI0			0x9a1b
#define PCI_DEVICE_ID_INTEL_TGL_NHI1			0x9a1d
#define PCI_DEVICE_ID_INTEL_TGL_H_NHI0			0x9a1f
#define PCI_DEVICE_ID_INTEL_TGL_H_NHI1			0x9a21
#define PCI_DEVICE_ID_INTEL_RPL_NHI0			0xa73e
#define PCI_DEVICE_ID_INTEL_RPL_NHI1			0xa76d
#define PCI_DEVICE_ID_INTEL_LNL_NHI0			0xa833
#define PCI_DEVICE_ID_INTEL_LNL_NHI1			0xa834
#define PCI_DEVICE_ID_INTEL_PTL_M_NHI0			0xe333
#define PCI_DEVICE_ID_INTEL_PTL_M_NHI1			0xe334
#define PCI_DEVICE_ID_INTEL_PTL_P_NHI0			0xe433
#define PCI_DEVICE_ID_INTEL_PTL_P_NHI1			0xe434

#define PCI_CLASS_SERIAL_USB_USB4			0x0c0340

/* Host interface quirks */
#define QUIRK_AUTO_CLEAR_INT	BIT(0)
#define QUIRK_E2E		BIT(1)

/*
 * Minimal number of vectors when we use MSI-X. Two for control channel
 * Rx/Tx and the rest four are for cross domain DMA paths.
 */
#define MSIX_MIN_VECS		6
#define MSIX_MAX_VECS		16

#endif
