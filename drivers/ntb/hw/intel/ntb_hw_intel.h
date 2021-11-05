/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright(c) 2012 Intel Corporation. All rights reserved.
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2012 Intel Corporation. All rights reserved.
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copy
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Intel PCIe NTB Linux driver
 */

#ifndef NTB_HW_INTEL_H
#define NTB_HW_INTEL_H

#include <linux/ntb.h>
#include <linux/pci.h>
#include <linux/io-64-nonatomic-lo-hi.h>

/* PCI device IDs */
#define PCI_DEVICE_ID_INTEL_NTB_B2B_JSF	0x3725
#define PCI_DEVICE_ID_INTEL_NTB_PS_JSF	0x3726
#define PCI_DEVICE_ID_INTEL_NTB_SS_JSF	0x3727
#define PCI_DEVICE_ID_INTEL_NTB_B2B_SNB	0x3C0D
#define PCI_DEVICE_ID_INTEL_NTB_PS_SNB	0x3C0E
#define PCI_DEVICE_ID_INTEL_NTB_SS_SNB	0x3C0F
#define PCI_DEVICE_ID_INTEL_NTB_B2B_IVT	0x0E0D
#define PCI_DEVICE_ID_INTEL_NTB_PS_IVT	0x0E0E
#define PCI_DEVICE_ID_INTEL_NTB_SS_IVT	0x0E0F
#define PCI_DEVICE_ID_INTEL_NTB_B2B_HSX	0x2F0D
#define PCI_DEVICE_ID_INTEL_NTB_PS_HSX	0x2F0E
#define PCI_DEVICE_ID_INTEL_NTB_SS_HSX	0x2F0F
#define PCI_DEVICE_ID_INTEL_NTB_B2B_BDX	0x6F0D
#define PCI_DEVICE_ID_INTEL_NTB_PS_BDX	0x6F0E
#define PCI_DEVICE_ID_INTEL_NTB_SS_BDX	0x6F0F
#define PCI_DEVICE_ID_INTEL_NTB_B2B_SKX	0x201C
#define PCI_DEVICE_ID_INTEL_NTB_B2B_ICX	0x347e

/* Ntb control and link status */
#define NTB_CTL_CFG_LOCK		BIT(0)
#define NTB_CTL_DISABLE			BIT(1)
#define NTB_CTL_S2P_BAR2_SNOOP		BIT(2)
#define NTB_CTL_P2S_BAR2_SNOOP		BIT(4)
#define NTB_CTL_S2P_BAR4_SNOOP		BIT(6)
#define NTB_CTL_P2S_BAR4_SNOOP		BIT(8)
#define NTB_CTL_S2P_BAR5_SNOOP		BIT(12)
#define NTB_CTL_P2S_BAR5_SNOOP		BIT(14)

#define NTB_LNK_STA_ACTIVE_BIT		0x2000
#define NTB_LNK_STA_SPEED_MASK		0x000f
#define NTB_LNK_STA_WIDTH_MASK		0x03f0
#define NTB_LNK_STA_ACTIVE(x)		(!!((x) & NTB_LNK_STA_ACTIVE_BIT))
#define NTB_LNK_STA_SPEED(x)		((x) & NTB_LNK_STA_SPEED_MASK)
#define NTB_LNK_STA_WIDTH(x)		(((x) & NTB_LNK_STA_WIDTH_MASK) >> 4)

/* flags to indicate unsafe api */
#define NTB_UNSAFE_DB			BIT_ULL(0)
#define NTB_UNSAFE_SPAD			BIT_ULL(1)

#define NTB_BAR_MASK_64			~(0xfull)
#define NTB_BAR_MASK_32			~(0xfu)

struct intel_ntb_dev;

struct intel_ntb_reg {
	int (*poll_link)(struct intel_ntb_dev *ndev);
	int (*link_is_up)(struct intel_ntb_dev *ndev);
	u64 (*db_ioread)(const void __iomem *mmio);
	void (*db_iowrite)(u64 db_bits, void __iomem *mmio);
	unsigned long			ntb_ctl;
	resource_size_t			db_size;
	int				mw_bar[];
};

struct intel_ntb_alt_reg {
	unsigned long			db_bell;
	unsigned long			db_mask;
	unsigned long			db_clear;
	unsigned long			spad;
};

struct intel_ntb_xlat_reg {
	unsigned long			bar0_base;
	unsigned long			bar2_xlat;
	unsigned long			bar2_limit;
	unsigned short			bar2_idx;
};

struct intel_b2b_addr {
	phys_addr_t			bar0_addr;
	phys_addr_t			bar2_addr64;
	phys_addr_t			bar4_addr64;
	phys_addr_t			bar4_addr32;
	phys_addr_t			bar5_addr32;
};

struct intel_ntb_vec {
	struct intel_ntb_dev		*ndev;
	int				num;
};

struct intel_ntb_dev {
	struct ntb_dev			ntb;

	/* offset of peer bar0 in b2b bar */
	unsigned long			b2b_off;
	/* mw idx used to access peer bar0 */
	unsigned int			b2b_idx;

	/* BAR45 is split into BAR4 and BAR5 */
	bool				bar4_split;

	u32				ntb_ctl;
	u32				lnk_sta;

	unsigned char			mw_count;
	unsigned char			spad_count;
	unsigned char			db_count;
	unsigned char			db_vec_count;
	unsigned char			db_vec_shift;

	u64				db_valid_mask;
	u64				db_link_mask;
	u64				db_mask;

	/* synchronize rmw access of db_mask and hw reg */
	spinlock_t			db_mask_lock;

	struct msix_entry		*msix;
	struct intel_ntb_vec		*vec;

	const struct intel_ntb_reg	*reg;
	const struct intel_ntb_alt_reg	*self_reg;
	const struct intel_ntb_alt_reg	*peer_reg;
	const struct intel_ntb_xlat_reg	*xlat_reg;
	void				__iomem *self_mmio;
	void				__iomem *peer_mmio;
	phys_addr_t			peer_addr;

	unsigned long			last_ts;
	struct delayed_work		hb_timer;

	unsigned long			hwerr_flags;
	unsigned long			unsafe_flags;
	unsigned long			unsafe_flags_ignore;

	struct dentry			*debugfs_dir;
	struct dentry			*debugfs_info;

	/* gen4 entries */
	int				dev_up;
};

#define ntb_ndev(__ntb) container_of(__ntb, struct intel_ntb_dev, ntb)
#define hb_ndev(__work) container_of(__work, struct intel_ntb_dev, \
				     hb_timer.work)

static inline int pdev_is_gen1(struct pci_dev *pdev)
{
	switch (pdev->device) {
	case PCI_DEVICE_ID_INTEL_NTB_SS_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_SS_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_SS_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_SS_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_SS_BDX:
	case PCI_DEVICE_ID_INTEL_NTB_PS_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_PS_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_PS_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_PS_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_PS_BDX:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_BDX:
		return 1;
	}
	return 0;
}

static inline int pdev_is_gen3(struct pci_dev *pdev)
{
	if (pdev->device == PCI_DEVICE_ID_INTEL_NTB_B2B_SKX)
		return 1;

	return 0;
}

static inline int pdev_is_gen4(struct pci_dev *pdev)
{
	if (pdev->device == PCI_DEVICE_ID_INTEL_NTB_B2B_ICX)
		return 1;

	return 0;
}
#endif
