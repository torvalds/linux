/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)          */
/* Copyright(c) 2020 Intel Corporation. All rights reserved.   */
#ifndef _NTB_INTEL_GEN4_H_
#define _NTB_INTEL_GEN4_H_

#include "ntb_hw_intel.h"

/* Supported PCI device revision range for ICX */
#define PCI_DEVICE_REVISION_ICX_MIN	0x2
#define PCI_DEVICE_REVISION_ICX_MAX	0xF

/* Intel Gen4 NTB hardware */
/* PCIe config space */
#define GEN4_IMBAR23SZ_OFFSET		0x00c4
#define GEN4_IMBAR45SZ_OFFSET		0x00c5
#define GEN4_EMBAR23SZ_OFFSET		0x00c6
#define GEN4_EMBAR45SZ_OFFSET		0x00c7
#define GEN4_DEVCTRL_OFFSET		0x0048
#define GEN4_DEVSTS_OFFSET		0x004a
#define GEN4_UNCERRSTS_OFFSET		0x0104
#define GEN4_CORERRSTS_OFFSET		0x0110

/* BAR0 MMIO */
#define GEN4_NTBCNTL_OFFSET		0x0000
#define GEN4_IM23XBASE_OFFSET		0x0010	/* IMBAR1XBASE */
#define GEN4_IM23XLMT_OFFSET		0x0018  /* IMBAR1XLMT */
#define GEN4_IM45XBASE_OFFSET		0x0020	/* IMBAR2XBASE */
#define GEN4_IM45XLMT_OFFSET		0x0028  /* IMBAR2XLMT */
#define GEN4_IM_INT_STATUS_OFFSET	0x0040
#define GEN4_IM_INT_DISABLE_OFFSET	0x0048
#define GEN4_INTVEC_OFFSET		0x0050  /* 0-32 vecs */
#define GEN4_IM23XBASEIDX_OFFSET	0x0074
#define GEN4_IM45XBASEIDX_OFFSET	0x0076
#define GEN4_IM_SPAD_OFFSET		0x0080  /* 0-15 SPADs */
#define GEN4_IM_SPAD_SEM_OFFSET		0x00c0	/* SPAD hw semaphore */
#define GEN4_IM_SPAD_STICKY_OFFSET	0x00c4  /* sticky SPAD */
#define GEN4_IM_DOORBELL_OFFSET		0x0100  /* 0-31 doorbells */
#define GEN4_LTR_SWSEL_OFFSET		0x30ec
#define GEN4_LTR_ACTIVE_OFFSET		0x30f0
#define GEN4_LTR_IDLE_OFFSET		0x30f4
#define GEN4_EM_SPAD_OFFSET		0x8080
/* note, link status is now in MMIO and not config space for NTB */
#define GEN4_LINK_CTRL_OFFSET		0xb050
#define GEN4_LINK_STATUS_OFFSET		0xb052
#define GEN4_PPD0_OFFSET		0xb0d4
#define GEN4_PPD1_OFFSET		0xb4c0
#define GEN4_LTSSMSTATEJMP		0xf040

#define GEN4_PPD_CLEAR_TRN		0x0001
#define GEN4_PPD_LINKTRN		0x0008
#define GEN4_PPD_CONN_MASK		0x0300
#define GEN4_PPD_CONN_B2B		0x0200
#define GEN4_PPD_DEV_MASK		0x1000
#define GEN4_PPD_DEV_DSD		0x1000
#define GEN4_PPD_DEV_USD		0x0000
#define GEN4_LINK_CTRL_LINK_DISABLE	0x0010

#define GEN4_SLOTSTS			0xb05a
#define GEN4_SLOTSTS_DLLSCS		0x100

#define GEN4_PPD_TOPO_MASK	(GEN4_PPD_CONN_MASK | GEN4_PPD_DEV_MASK)
#define GEN4_PPD_TOPO_B2B_USD	(GEN4_PPD_CONN_B2B | GEN4_PPD_DEV_USD)
#define GEN4_PPD_TOPO_B2B_DSD	(GEN4_PPD_CONN_B2B | GEN4_PPD_DEV_DSD)

#define GEN4_DB_COUNT			32
#define GEN4_DB_LINK			32
#define GEN4_DB_LINK_BIT		BIT_ULL(GEN4_DB_LINK)
#define GEN4_DB_MSIX_VECTOR_COUNT	33
#define GEN4_DB_MSIX_VECTOR_SHIFT	1
#define GEN4_DB_TOTAL_SHIFT		33
#define GEN4_SPAD_COUNT			16

#define NTB_CTL_E2I_BAR23_SNOOP		0x000004
#define NTB_CTL_E2I_BAR23_NOSNOOP	0x000008
#define NTB_CTL_I2E_BAR23_SNOOP		0x000010
#define NTB_CTL_I2E_BAR23_NOSNOOP	0x000020
#define NTB_CTL_E2I_BAR45_SNOOP		0x000040
#define NTB_CTL_E2I_BAR45_NOSNOO	0x000080
#define NTB_CTL_I2E_BAR45_SNOOP		0x000100
#define NTB_CTL_I2E_BAR45_NOSNOOP	0x000200
#define NTB_CTL_BUSNO_DIS_INC		0x000400
#define NTB_CTL_LINK_DOWN		0x010000

#define NTB_SJC_FORCEDETECT		0x000004

#define NTB_LTR_SWSEL_ACTIVE		0x0
#define NTB_LTR_SWSEL_IDLE		0x1

#define NTB_LTR_NS_SHIFT		16
#define NTB_LTR_ACTIVE_VAL		0x0000  /* 0 us */
#define NTB_LTR_ACTIVE_LATSCALE		0x0800  /* 1us scale */
#define NTB_LTR_ACTIVE_REQMNT		0x8000  /* snoop req enable */

#define NTB_LTR_IDLE_VAL		0x0258  /* 600 us */
#define NTB_LTR_IDLE_LATSCALE		0x0800  /* 1us scale */
#define NTB_LTR_IDLE_REQMNT		0x8000  /* snoop req enable */

ssize_t ndev_ntb4_debugfs_read(struct file *filp, char __user *ubuf,
				      size_t count, loff_t *offp);
int gen4_init_dev(struct intel_ntb_dev *ndev);
ssize_t ndev_ntb4_debugfs_read(struct file *filp, char __user *ubuf,
				      size_t count, loff_t *offp);

extern const struct ntb_dev_ops intel_ntb4_ops;

static inline int pdev_is_ICX(struct pci_dev *pdev)
{
	if (pdev_is_gen4(pdev) &&
	    pdev->revision >= PCI_DEVICE_REVISION_ICX_MIN &&
	    pdev->revision <= PCI_DEVICE_REVISION_ICX_MAX)
		return 1;
	return 0;
}

#endif
