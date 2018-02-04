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
 *
 * Contact Information:
 * Jon Mason <jon.mason@intel.com>
 */

#ifndef NTB_HW_INTEL_H
#define NTB_HW_INTEL_H

#include <linux/ntb.h>
#include <linux/pci.h>

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

/* Intel Xeon hardware */

#define XEON_PBAR23LMT_OFFSET		0x0000
#define XEON_PBAR45LMT_OFFSET		0x0008
#define XEON_PBAR4LMT_OFFSET		0x0008
#define XEON_PBAR5LMT_OFFSET		0x000c
#define XEON_PBAR23XLAT_OFFSET		0x0010
#define XEON_PBAR45XLAT_OFFSET		0x0018
#define XEON_PBAR4XLAT_OFFSET		0x0018
#define XEON_PBAR5XLAT_OFFSET		0x001c
#define XEON_SBAR23LMT_OFFSET		0x0020
#define XEON_SBAR45LMT_OFFSET		0x0028
#define XEON_SBAR4LMT_OFFSET		0x0028
#define XEON_SBAR5LMT_OFFSET		0x002c
#define XEON_SBAR23XLAT_OFFSET		0x0030
#define XEON_SBAR45XLAT_OFFSET		0x0038
#define XEON_SBAR4XLAT_OFFSET		0x0038
#define XEON_SBAR5XLAT_OFFSET		0x003c
#define XEON_SBAR0BASE_OFFSET		0x0040
#define XEON_SBAR23BASE_OFFSET		0x0048
#define XEON_SBAR45BASE_OFFSET		0x0050
#define XEON_SBAR4BASE_OFFSET		0x0050
#define XEON_SBAR5BASE_OFFSET		0x0054
#define XEON_SBDF_OFFSET		0x005c
#define XEON_NTBCNTL_OFFSET		0x0058
#define XEON_PDOORBELL_OFFSET		0x0060
#define XEON_PDBMSK_OFFSET		0x0062
#define XEON_SDOORBELL_OFFSET		0x0064
#define XEON_SDBMSK_OFFSET		0x0066
#define XEON_USMEMMISS_OFFSET		0x0070
#define XEON_SPAD_OFFSET		0x0080
#define XEON_PBAR23SZ_OFFSET		0x00d0
#define XEON_PBAR45SZ_OFFSET		0x00d1
#define XEON_PBAR4SZ_OFFSET		0x00d1
#define XEON_SBAR23SZ_OFFSET		0x00d2
#define XEON_SBAR45SZ_OFFSET		0x00d3
#define XEON_SBAR4SZ_OFFSET		0x00d3
#define XEON_PPD_OFFSET			0x00d4
#define XEON_PBAR5SZ_OFFSET		0x00d5
#define XEON_SBAR5SZ_OFFSET		0x00d6
#define XEON_WCCNTRL_OFFSET		0x00e0
#define XEON_UNCERRSTS_OFFSET		0x014c
#define XEON_CORERRSTS_OFFSET		0x0158
#define XEON_LINK_STATUS_OFFSET		0x01a2
#define XEON_SPCICMD_OFFSET		0x0504
#define XEON_DEVCTRL_OFFSET		0x0598
#define XEON_DEVSTS_OFFSET		0x059a
#define XEON_SLINK_STATUS_OFFSET	0x05a2
#define XEON_B2B_SPAD_OFFSET		0x0100
#define XEON_B2B_DOORBELL_OFFSET	0x0140
#define XEON_B2B_XLAT_OFFSETL		0x0144
#define XEON_B2B_XLAT_OFFSETU		0x0148
#define XEON_PPD_CONN_MASK		0x03
#define XEON_PPD_CONN_TRANSPARENT	0x00
#define XEON_PPD_CONN_B2B		0x01
#define XEON_PPD_CONN_RP		0x02
#define XEON_PPD_DEV_MASK		0x10
#define XEON_PPD_DEV_USD		0x00
#define XEON_PPD_DEV_DSD		0x10
#define XEON_PPD_SPLIT_BAR_MASK		0x40

#define XEON_PPD_TOPO_MASK	(XEON_PPD_CONN_MASK | XEON_PPD_DEV_MASK)
#define XEON_PPD_TOPO_PRI_USD	(XEON_PPD_CONN_RP | XEON_PPD_DEV_USD)
#define XEON_PPD_TOPO_PRI_DSD	(XEON_PPD_CONN_RP | XEON_PPD_DEV_DSD)
#define XEON_PPD_TOPO_SEC_USD	(XEON_PPD_CONN_TRANSPARENT | XEON_PPD_DEV_USD)
#define XEON_PPD_TOPO_SEC_DSD	(XEON_PPD_CONN_TRANSPARENT | XEON_PPD_DEV_DSD)
#define XEON_PPD_TOPO_B2B_USD	(XEON_PPD_CONN_B2B | XEON_PPD_DEV_USD)
#define XEON_PPD_TOPO_B2B_DSD	(XEON_PPD_CONN_B2B | XEON_PPD_DEV_DSD)

#define XEON_MW_COUNT			2
#define HSX_SPLIT_BAR_MW_COUNT		3
#define XEON_DB_COUNT			15
#define XEON_DB_LINK			15
#define XEON_DB_LINK_BIT			BIT_ULL(XEON_DB_LINK)
#define XEON_DB_MSIX_VECTOR_COUNT	4
#define XEON_DB_MSIX_VECTOR_SHIFT	5
#define XEON_DB_TOTAL_SHIFT		16
#define XEON_SPAD_COUNT			16

/* Intel Skylake Xeon hardware */
#define SKX_IMBAR1SZ_OFFSET		0x00d0
#define SKX_IMBAR2SZ_OFFSET		0x00d1
#define SKX_EMBAR1SZ_OFFSET		0x00d2
#define SKX_EMBAR2SZ_OFFSET		0x00d3
#define SKX_DEVCTRL_OFFSET		0x0098
#define SKX_DEVSTS_OFFSET		0x009a
#define SKX_UNCERRSTS_OFFSET		0x014c
#define SKX_CORERRSTS_OFFSET		0x0158
#define SKX_LINK_STATUS_OFFSET		0x01a2

#define SKX_NTBCNTL_OFFSET		0x0000
#define SKX_IMBAR1XBASE_OFFSET		0x0010		/* SBAR2XLAT */
#define SKX_IMBAR1XLMT_OFFSET		0x0018		/* SBAR2LMT */
#define SKX_IMBAR2XBASE_OFFSET		0x0020		/* SBAR4XLAT */
#define SKX_IMBAR2XLMT_OFFSET		0x0028		/* SBAR4LMT */
#define SKX_IM_INT_STATUS_OFFSET	0x0040
#define SKX_IM_INT_DISABLE_OFFSET	0x0048
#define SKX_IM_SPAD_OFFSET		0x0080		/* SPAD */
#define SKX_USMEMMISS_OFFSET		0x0070
#define SKX_INTVEC_OFFSET		0x00d0
#define SKX_IM_DOORBELL_OFFSET		0x0100		/* SDOORBELL0 */
#define SKX_B2B_SPAD_OFFSET		0x0180		/* B2B SPAD */
#define SKX_EMBAR0XBASE_OFFSET		0x4008		/* B2B_XLAT */
#define SKX_EMBAR1XBASE_OFFSET		0x4010		/* PBAR2XLAT */
#define SKX_EMBAR1XLMT_OFFSET		0x4018		/* PBAR2LMT */
#define SKX_EMBAR2XBASE_OFFSET		0x4020		/* PBAR4XLAT */
#define SKX_EMBAR2XLMT_OFFSET		0x4028		/* PBAR4LMT */
#define SKX_EM_INT_STATUS_OFFSET	0x4040
#define SKX_EM_INT_DISABLE_OFFSET	0x4048
#define SKX_EM_SPAD_OFFSET		0x4080		/* remote SPAD */
#define SKX_EM_DOORBELL_OFFSET		0x4100		/* PDOORBELL0 */
#define SKX_SPCICMD_OFFSET		0x4504		/* SPCICMD */
#define SKX_EMBAR0_OFFSET		0x4510		/* SBAR0BASE */
#define SKX_EMBAR1_OFFSET		0x4518		/* SBAR23BASE */
#define SKX_EMBAR2_OFFSET		0x4520		/* SBAR45BASE */

#define SKX_DB_COUNT			32
#define SKX_DB_LINK			32
#define SKX_DB_LINK_BIT			BIT_ULL(SKX_DB_LINK)
#define SKX_DB_MSIX_VECTOR_COUNT	33
#define SKX_DB_MSIX_VECTOR_SHIFT	1
#define SKX_DB_TOTAL_SHIFT		33
#define SKX_SPAD_COUNT			16

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

/* Use the following addresses for translation between b2b ntb devices in case
 * the hardware default values are not reliable. */
#define XEON_B2B_BAR0_ADDR	0x1000000000000000ull
#define XEON_B2B_BAR2_ADDR64	0x2000000000000000ull
#define XEON_B2B_BAR4_ADDR64	0x4000000000000000ull
#define XEON_B2B_BAR4_ADDR32	0x20000000u
#define XEON_B2B_BAR5_ADDR32	0x40000000u

/* The peer ntb secondary config space is 32KB fixed size */
#define XEON_B2B_MIN_SIZE		0x8000

/* flags to indicate hardware errata */
#define NTB_HWERR_SDOORBELL_LOCKUP	BIT_ULL(0)
#define NTB_HWERR_SB01BASE_LOCKUP	BIT_ULL(1)
#define NTB_HWERR_B2BDOORBELL_BIT14	BIT_ULL(2)
#define NTB_HWERR_MSIX_VECTOR32_BAD	BIT_ULL(3)

/* flags to indicate unsafe api */
#define NTB_UNSAFE_DB			BIT_ULL(0)
#define NTB_UNSAFE_SPAD			BIT_ULL(1)

#define NTB_BAR_MASK_64			~(0xfull)
#define NTB_BAR_MASK_32			~(0xfu)

struct intel_ntb_dev;

struct intel_ntb_reg {
	int (*poll_link)(struct intel_ntb_dev *ndev);
	int (*link_is_up)(struct intel_ntb_dev *ndev);
	u64 (*db_ioread)(void __iomem *mmio);
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
};

#define ntb_ndev(__ntb) container_of(__ntb, struct intel_ntb_dev, ntb)
#define hb_ndev(__work) container_of(__work, struct intel_ntb_dev, \
				     hb_timer.work)

#endif
