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

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/ntb.h>

#include "ntb_hw_intel.h"

#define NTB_NAME	"ntb_hw_intel"
#define NTB_DESC	"Intel(R) PCI-E Non-Transparent Bridge Driver"
#define NTB_VER		"2.0"

MODULE_DESCRIPTION(NTB_DESC);
MODULE_VERSION(NTB_VER);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel Corporation");

#define bar0_off(base, bar) ((base) + ((bar) << 2))
#define bar2_off(base, bar) bar0_off(base, (bar) - 2)

static const struct intel_ntb_reg atom_reg;
static const struct intel_ntb_alt_reg atom_pri_reg;
static const struct intel_ntb_alt_reg atom_sec_reg;
static const struct intel_ntb_alt_reg atom_b2b_reg;
static const struct intel_ntb_xlat_reg atom_pri_xlat;
static const struct intel_ntb_xlat_reg atom_sec_xlat;
static const struct intel_ntb_reg xeon_reg;
static const struct intel_ntb_alt_reg xeon_pri_reg;
static const struct intel_ntb_alt_reg xeon_sec_reg;
static const struct intel_ntb_alt_reg xeon_b2b_reg;
static const struct intel_ntb_xlat_reg xeon_pri_xlat;
static const struct intel_ntb_xlat_reg xeon_sec_xlat;
static struct intel_b2b_addr xeon_b2b_usd_addr;
static struct intel_b2b_addr xeon_b2b_dsd_addr;
static const struct intel_ntb_reg skx_reg;
static const struct intel_ntb_alt_reg skx_pri_reg;
static const struct intel_ntb_alt_reg skx_b2b_reg;
static const struct intel_ntb_xlat_reg skx_sec_xlat;
static const struct ntb_dev_ops intel_ntb_ops;
static const struct ntb_dev_ops intel_ntb3_ops;

static const struct file_operations intel_ntb_debugfs_info;
static struct dentry *debugfs_dir;

static int b2b_mw_idx = -1;
module_param(b2b_mw_idx, int, 0644);
MODULE_PARM_DESC(b2b_mw_idx, "Use this mw idx to access the peer ntb.  A "
		 "value of zero or positive starts from first mw idx, and a "
		 "negative value starts from last mw idx.  Both sides MUST "
		 "set the same value here!");

static unsigned int b2b_mw_share;
module_param(b2b_mw_share, uint, 0644);
MODULE_PARM_DESC(b2b_mw_share, "If the b2b mw is large enough, configure the "
		 "ntb so that the peer ntb only occupies the first half of "
		 "the mw, so the second half can still be used as a mw.  Both "
		 "sides MUST set the same value here!");

module_param_named(xeon_b2b_usd_bar2_addr64,
		   xeon_b2b_usd_addr.bar2_addr64, ullong, 0644);
MODULE_PARM_DESC(xeon_b2b_usd_bar2_addr64,
		 "XEON B2B USD BAR 2 64-bit address");

module_param_named(xeon_b2b_usd_bar4_addr64,
		   xeon_b2b_usd_addr.bar4_addr64, ullong, 0644);
MODULE_PARM_DESC(xeon_b2b_usd_bar4_addr64,
		 "XEON B2B USD BAR 4 64-bit address");

module_param_named(xeon_b2b_usd_bar4_addr32,
		   xeon_b2b_usd_addr.bar4_addr32, ullong, 0644);
MODULE_PARM_DESC(xeon_b2b_usd_bar4_addr32,
		 "XEON B2B USD split-BAR 4 32-bit address");

module_param_named(xeon_b2b_usd_bar5_addr32,
		   xeon_b2b_usd_addr.bar5_addr32, ullong, 0644);
MODULE_PARM_DESC(xeon_b2b_usd_bar5_addr32,
		 "XEON B2B USD split-BAR 5 32-bit address");

module_param_named(xeon_b2b_dsd_bar2_addr64,
		   xeon_b2b_dsd_addr.bar2_addr64, ullong, 0644);
MODULE_PARM_DESC(xeon_b2b_dsd_bar2_addr64,
		 "XEON B2B DSD BAR 2 64-bit address");

module_param_named(xeon_b2b_dsd_bar4_addr64,
		   xeon_b2b_dsd_addr.bar4_addr64, ullong, 0644);
MODULE_PARM_DESC(xeon_b2b_dsd_bar4_addr64,
		 "XEON B2B DSD BAR 4 64-bit address");

module_param_named(xeon_b2b_dsd_bar4_addr32,
		   xeon_b2b_dsd_addr.bar4_addr32, ullong, 0644);
MODULE_PARM_DESC(xeon_b2b_dsd_bar4_addr32,
		 "XEON B2B DSD split-BAR 4 32-bit address");

module_param_named(xeon_b2b_dsd_bar5_addr32,
		   xeon_b2b_dsd_addr.bar5_addr32, ullong, 0644);
MODULE_PARM_DESC(xeon_b2b_dsd_bar5_addr32,
		 "XEON B2B DSD split-BAR 5 32-bit address");

static inline enum ntb_topo xeon_ppd_topo(struct intel_ntb_dev *ndev, u8 ppd);
static int xeon_init_isr(struct intel_ntb_dev *ndev);

#ifndef ioread64
#ifdef readq
#define ioread64 readq
#else
#define ioread64 _ioread64
static inline u64 _ioread64(void __iomem *mmio)
{
	u64 low, high;

	low = ioread32(mmio);
	high = ioread32(mmio + sizeof(u32));
	return low | (high << 32);
}
#endif
#endif

#ifndef iowrite64
#ifdef writeq
#define iowrite64 writeq
#else
#define iowrite64 _iowrite64
static inline void _iowrite64(u64 val, void __iomem *mmio)
{
	iowrite32(val, mmio);
	iowrite32(val >> 32, mmio + sizeof(u32));
}
#endif
#endif

static inline int pdev_is_atom(struct pci_dev *pdev)
{
	switch (pdev->device) {
	case PCI_DEVICE_ID_INTEL_NTB_B2B_BWD:
		return 1;
	}
	return 0;
}

static inline int pdev_is_xeon(struct pci_dev *pdev)
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

static inline int pdev_is_skx_xeon(struct pci_dev *pdev)
{
	if (pdev->device == PCI_DEVICE_ID_INTEL_NTB_B2B_SKX)
		return 1;

	return 0;
}

static inline void ndev_reset_unsafe_flags(struct intel_ntb_dev *ndev)
{
	ndev->unsafe_flags = 0;
	ndev->unsafe_flags_ignore = 0;

	/* Only B2B has a workaround to avoid SDOORBELL */
	if (ndev->hwerr_flags & NTB_HWERR_SDOORBELL_LOCKUP)
		if (!ntb_topo_is_b2b(ndev->ntb.topo))
			ndev->unsafe_flags |= NTB_UNSAFE_DB;

	/* No low level workaround to avoid SB01BASE */
	if (ndev->hwerr_flags & NTB_HWERR_SB01BASE_LOCKUP) {
		ndev->unsafe_flags |= NTB_UNSAFE_DB;
		ndev->unsafe_flags |= NTB_UNSAFE_SPAD;
	}
}

static inline int ndev_is_unsafe(struct intel_ntb_dev *ndev,
				 unsigned long flag)
{
	return !!(flag & ndev->unsafe_flags & ~ndev->unsafe_flags_ignore);
}

static inline int ndev_ignore_unsafe(struct intel_ntb_dev *ndev,
				     unsigned long flag)
{
	flag &= ndev->unsafe_flags;
	ndev->unsafe_flags_ignore |= flag;

	return !!flag;
}

static int ndev_mw_to_bar(struct intel_ntb_dev *ndev, int idx)
{
	if (idx < 0 || idx >= ndev->mw_count)
		return -EINVAL;
	return ndev->reg->mw_bar[idx];
}

static inline int ndev_db_addr(struct intel_ntb_dev *ndev,
			       phys_addr_t *db_addr, resource_size_t *db_size,
			       phys_addr_t reg_addr, unsigned long reg)
{
	if (ndev_is_unsafe(ndev, NTB_UNSAFE_DB))
		pr_warn_once("%s: NTB unsafe doorbell access", __func__);

	if (db_addr) {
		*db_addr = reg_addr + reg;
		dev_dbg(ndev_dev(ndev), "Peer db addr %llx\n", *db_addr);
	}

	if (db_size) {
		*db_size = ndev->reg->db_size;
		dev_dbg(ndev_dev(ndev), "Peer db size %llx\n", *db_size);
	}

	return 0;
}

static inline u64 ndev_db_read(struct intel_ntb_dev *ndev,
			       void __iomem *mmio)
{
	if (ndev_is_unsafe(ndev, NTB_UNSAFE_DB))
		pr_warn_once("%s: NTB unsafe doorbell access", __func__);

	return ndev->reg->db_ioread(mmio);
}

static inline int ndev_db_write(struct intel_ntb_dev *ndev, u64 db_bits,
				void __iomem *mmio)
{
	if (ndev_is_unsafe(ndev, NTB_UNSAFE_DB))
		pr_warn_once("%s: NTB unsafe doorbell access", __func__);

	if (db_bits & ~ndev->db_valid_mask)
		return -EINVAL;

	ndev->reg->db_iowrite(db_bits, mmio);

	return 0;
}

static inline int ndev_db_set_mask(struct intel_ntb_dev *ndev, u64 db_bits,
				   void __iomem *mmio)
{
	unsigned long irqflags;

	if (ndev_is_unsafe(ndev, NTB_UNSAFE_DB))
		pr_warn_once("%s: NTB unsafe doorbell access", __func__);

	if (db_bits & ~ndev->db_valid_mask)
		return -EINVAL;

	spin_lock_irqsave(&ndev->db_mask_lock, irqflags);
	{
		ndev->db_mask |= db_bits;
		ndev->reg->db_iowrite(ndev->db_mask, mmio);
	}
	spin_unlock_irqrestore(&ndev->db_mask_lock, irqflags);

	return 0;
}

static inline int ndev_db_clear_mask(struct intel_ntb_dev *ndev, u64 db_bits,
				     void __iomem *mmio)
{
	unsigned long irqflags;

	if (ndev_is_unsafe(ndev, NTB_UNSAFE_DB))
		pr_warn_once("%s: NTB unsafe doorbell access", __func__);

	if (db_bits & ~ndev->db_valid_mask)
		return -EINVAL;

	spin_lock_irqsave(&ndev->db_mask_lock, irqflags);
	{
		ndev->db_mask &= ~db_bits;
		ndev->reg->db_iowrite(ndev->db_mask, mmio);
	}
	spin_unlock_irqrestore(&ndev->db_mask_lock, irqflags);

	return 0;
}

static inline int ndev_vec_mask(struct intel_ntb_dev *ndev, int db_vector)
{
	u64 shift, mask;

	shift = ndev->db_vec_shift;
	mask = BIT_ULL(shift) - 1;

	return mask << (shift * db_vector);
}

static inline int ndev_spad_addr(struct intel_ntb_dev *ndev, int idx,
				 phys_addr_t *spad_addr, phys_addr_t reg_addr,
				 unsigned long reg)
{
	if (ndev_is_unsafe(ndev, NTB_UNSAFE_SPAD))
		pr_warn_once("%s: NTB unsafe scratchpad access", __func__);

	if (idx < 0 || idx >= ndev->spad_count)
		return -EINVAL;

	if (spad_addr) {
		*spad_addr = reg_addr + reg + (idx << 2);
		dev_dbg(ndev_dev(ndev), "Peer spad addr %llx\n", *spad_addr);
	}

	return 0;
}

static inline u32 ndev_spad_read(struct intel_ntb_dev *ndev, int idx,
				 void __iomem *mmio)
{
	if (ndev_is_unsafe(ndev, NTB_UNSAFE_SPAD))
		pr_warn_once("%s: NTB unsafe scratchpad access", __func__);

	if (idx < 0 || idx >= ndev->spad_count)
		return 0;

	return ioread32(mmio + (idx << 2));
}

static inline int ndev_spad_write(struct intel_ntb_dev *ndev, int idx, u32 val,
				  void __iomem *mmio)
{
	if (ndev_is_unsafe(ndev, NTB_UNSAFE_SPAD))
		pr_warn_once("%s: NTB unsafe scratchpad access", __func__);

	if (idx < 0 || idx >= ndev->spad_count)
		return -EINVAL;

	iowrite32(val, mmio + (idx << 2));

	return 0;
}

static irqreturn_t ndev_interrupt(struct intel_ntb_dev *ndev, int vec)
{
	u64 vec_mask;

	vec_mask = ndev_vec_mask(ndev, vec);

	if ((ndev->hwerr_flags & NTB_HWERR_MSIX_VECTOR32_BAD) && (vec == 31))
		vec_mask |= ndev->db_link_mask;

	dev_dbg(ndev_dev(ndev), "vec %d vec_mask %llx\n", vec, vec_mask);

	ndev->last_ts = jiffies;

	if (vec_mask & ndev->db_link_mask) {
		if (ndev->reg->poll_link(ndev))
			ntb_link_event(&ndev->ntb);
	}

	if (vec_mask & ndev->db_valid_mask)
		ntb_db_event(&ndev->ntb, vec);

	return IRQ_HANDLED;
}

static irqreturn_t ndev_vec_isr(int irq, void *dev)
{
	struct intel_ntb_vec *nvec = dev;

	dev_dbg(ndev_dev(nvec->ndev), "irq: %d  nvec->num: %d\n",
		irq, nvec->num);

	return ndev_interrupt(nvec->ndev, nvec->num);
}

static irqreturn_t ndev_irq_isr(int irq, void *dev)
{
	struct intel_ntb_dev *ndev = dev;

	return ndev_interrupt(ndev, irq - ndev_pdev(ndev)->irq);
}

static int ndev_init_isr(struct intel_ntb_dev *ndev,
			 int msix_min, int msix_max,
			 int msix_shift, int total_shift)
{
	struct pci_dev *pdev;
	int rc, i, msix_count, node;

	pdev = ndev_pdev(ndev);

	node = dev_to_node(&pdev->dev);

	/* Mask all doorbell interrupts */
	ndev->db_mask = ndev->db_valid_mask;
	ndev->reg->db_iowrite(ndev->db_mask,
			      ndev->self_mmio +
			      ndev->self_reg->db_mask);

	/* Try to set up msix irq */

	ndev->vec = kzalloc_node(msix_max * sizeof(*ndev->vec),
				 GFP_KERNEL, node);
	if (!ndev->vec)
		goto err_msix_vec_alloc;

	ndev->msix = kzalloc_node(msix_max * sizeof(*ndev->msix),
				  GFP_KERNEL, node);
	if (!ndev->msix)
		goto err_msix_alloc;

	for (i = 0; i < msix_max; ++i)
		ndev->msix[i].entry = i;

	msix_count = pci_enable_msix_range(pdev, ndev->msix,
					   msix_min, msix_max);
	if (msix_count < 0)
		goto err_msix_enable;

	for (i = 0; i < msix_count; ++i) {
		ndev->vec[i].ndev = ndev;
		ndev->vec[i].num = i;
		rc = request_irq(ndev->msix[i].vector, ndev_vec_isr, 0,
				 "ndev_vec_isr", &ndev->vec[i]);
		if (rc)
			goto err_msix_request;
	}

	dev_dbg(ndev_dev(ndev), "Using %d msix interrupts\n", msix_count);
	ndev->db_vec_count = msix_count;
	ndev->db_vec_shift = msix_shift;
	return 0;

err_msix_request:
	while (i-- > 0)
		free_irq(ndev->msix[i].vector, &ndev->vec[i]);
	pci_disable_msix(pdev);
err_msix_enable:
	kfree(ndev->msix);
err_msix_alloc:
	kfree(ndev->vec);
err_msix_vec_alloc:
	ndev->msix = NULL;
	ndev->vec = NULL;

	/* Try to set up msi irq */

	rc = pci_enable_msi(pdev);
	if (rc)
		goto err_msi_enable;

	rc = request_irq(pdev->irq, ndev_irq_isr, 0,
			 "ndev_irq_isr", ndev);
	if (rc)
		goto err_msi_request;

	dev_dbg(ndev_dev(ndev), "Using msi interrupts\n");
	ndev->db_vec_count = 1;
	ndev->db_vec_shift = total_shift;
	return 0;

err_msi_request:
	pci_disable_msi(pdev);
err_msi_enable:

	/* Try to set up intx irq */

	pci_intx(pdev, 1);

	rc = request_irq(pdev->irq, ndev_irq_isr, IRQF_SHARED,
			 "ndev_irq_isr", ndev);
	if (rc)
		goto err_intx_request;

	dev_dbg(ndev_dev(ndev), "Using intx interrupts\n");
	ndev->db_vec_count = 1;
	ndev->db_vec_shift = total_shift;
	return 0;

err_intx_request:
	return rc;
}

static void ndev_deinit_isr(struct intel_ntb_dev *ndev)
{
	struct pci_dev *pdev;
	int i;

	pdev = ndev_pdev(ndev);

	/* Mask all doorbell interrupts */
	ndev->db_mask = ndev->db_valid_mask;
	ndev->reg->db_iowrite(ndev->db_mask,
			      ndev->self_mmio +
			      ndev->self_reg->db_mask);

	if (ndev->msix) {
		i = ndev->db_vec_count;
		while (i--)
			free_irq(ndev->msix[i].vector, &ndev->vec[i]);
		pci_disable_msix(pdev);
		kfree(ndev->msix);
		kfree(ndev->vec);
	} else {
		free_irq(pdev->irq, ndev);
		if (pci_dev_msi_enabled(pdev))
			pci_disable_msi(pdev);
	}
}

static ssize_t ndev_ntb3_debugfs_read(struct file *filp, char __user *ubuf,
				      size_t count, loff_t *offp)
{
	struct intel_ntb_dev *ndev;
	void __iomem *mmio;
	char *buf;
	size_t buf_size;
	ssize_t ret, off;
	union { u64 v64; u32 v32; u16 v16; } u;

	ndev = filp->private_data;
	mmio = ndev->self_mmio;

	buf_size = min(count, 0x800ul);

	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	off = 0;

	off += scnprintf(buf + off, buf_size - off,
			 "NTB Device Information:\n");

	off += scnprintf(buf + off, buf_size - off,
			 "Connection Topology -\t%s\n",
			 ntb_topo_string(ndev->ntb.topo));

	off += scnprintf(buf + off, buf_size - off,
			 "NTB CTL -\t\t%#06x\n", ndev->ntb_ctl);
	off += scnprintf(buf + off, buf_size - off,
			 "LNK STA -\t\t%#06x\n", ndev->lnk_sta);

	if (!ndev->reg->link_is_up(ndev))
		off += scnprintf(buf + off, buf_size - off,
				 "Link Status -\t\tDown\n");
	else {
		off += scnprintf(buf + off, buf_size - off,
				 "Link Status -\t\tUp\n");
		off += scnprintf(buf + off, buf_size - off,
				 "Link Speed -\t\tPCI-E Gen %u\n",
				 NTB_LNK_STA_SPEED(ndev->lnk_sta));
		off += scnprintf(buf + off, buf_size - off,
				 "Link Width -\t\tx%u\n",
				 NTB_LNK_STA_WIDTH(ndev->lnk_sta));
	}

	off += scnprintf(buf + off, buf_size - off,
			 "Memory Window Count -\t%u\n", ndev->mw_count);
	off += scnprintf(buf + off, buf_size - off,
			 "Scratchpad Count -\t%u\n", ndev->spad_count);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Count -\t%u\n", ndev->db_count);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Vector Count -\t%u\n", ndev->db_vec_count);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Vector Shift -\t%u\n", ndev->db_vec_shift);

	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Valid Mask -\t%#llx\n", ndev->db_valid_mask);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Link Mask -\t%#llx\n", ndev->db_link_mask);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Mask Cached -\t%#llx\n", ndev->db_mask);

	u.v64 = ndev_db_read(ndev, mmio + ndev->self_reg->db_mask);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Mask -\t\t%#llx\n", u.v64);

	u.v64 = ndev_db_read(ndev, mmio + ndev->self_reg->db_bell);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Bell -\t\t%#llx\n", u.v64);

	off += scnprintf(buf + off, buf_size - off,
			 "\nNTB Incoming XLAT:\n");

	u.v64 = ioread64(mmio + SKX_IMBAR1XBASE_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "IMBAR1XBASE -\t\t%#018llx\n", u.v64);

	u.v64 = ioread64(mmio + SKX_IMBAR2XBASE_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "IMBAR2XBASE -\t\t%#018llx\n", u.v64);

	u.v64 = ioread64(mmio + SKX_IMBAR1XLMT_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "IMBAR1XLMT -\t\t\t%#018llx\n", u.v64);

	u.v64 = ioread64(mmio + SKX_IMBAR2XLMT_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "IMBAR2XLMT -\t\t\t%#018llx\n", u.v64);

	if (ntb_topo_is_b2b(ndev->ntb.topo)) {
		off += scnprintf(buf + off, buf_size - off,
				 "\nNTB Outgoing B2B XLAT:\n");

		u.v64 = ioread64(mmio + SKX_EMBAR1XBASE_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR1XBASE -\t\t%#018llx\n", u.v64);

		u.v64 = ioread64(mmio + SKX_EMBAR2XBASE_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR2XBASE -\t\t%#018llx\n", u.v64);

		u.v64 = ioread64(mmio + SKX_EMBAR1XLMT_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR1XLMT -\t\t%#018llx\n", u.v64);

		u.v64 = ioread64(mmio + SKX_EMBAR2XLMT_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR2XLMT -\t\t%#018llx\n", u.v64);

		off += scnprintf(buf + off, buf_size - off,
				 "\nNTB Secondary BAR:\n");

		u.v64 = ioread64(mmio + SKX_EMBAR0_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR0 -\t\t%#018llx\n", u.v64);

		u.v64 = ioread64(mmio + SKX_EMBAR1_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR1 -\t\t%#018llx\n", u.v64);

		u.v64 = ioread64(mmio + SKX_EMBAR2_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR2 -\t\t%#018llx\n", u.v64);
	}

	off += scnprintf(buf + off, buf_size - off,
			 "\nNTB Statistics:\n");

	u.v16 = ioread16(mmio + SKX_USMEMMISS_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "Upstream Memory Miss -\t%u\n", u.v16);

	off += scnprintf(buf + off, buf_size - off,
			 "\nNTB Hardware Errors:\n");

	if (!pci_read_config_word(ndev->ntb.pdev,
				  SKX_DEVSTS_OFFSET, &u.v16))
		off += scnprintf(buf + off, buf_size - off,
				 "DEVSTS -\t\t%#06x\n", u.v16);

	if (!pci_read_config_word(ndev->ntb.pdev,
				  SKX_LINK_STATUS_OFFSET, &u.v16))
		off += scnprintf(buf + off, buf_size - off,
				 "LNKSTS -\t\t%#06x\n", u.v16);

	if (!pci_read_config_dword(ndev->ntb.pdev,
				   SKX_UNCERRSTS_OFFSET, &u.v32))
		off += scnprintf(buf + off, buf_size - off,
				 "UNCERRSTS -\t\t%#06x\n", u.v32);

	if (!pci_read_config_dword(ndev->ntb.pdev,
				   SKX_CORERRSTS_OFFSET, &u.v32))
		off += scnprintf(buf + off, buf_size - off,
				 "CORERRSTS -\t\t%#06x\n", u.v32);

	ret = simple_read_from_buffer(ubuf, count, offp, buf, off);
	kfree(buf);
	return ret;
}

static ssize_t ndev_ntb_debugfs_read(struct file *filp, char __user *ubuf,
				     size_t count, loff_t *offp)
{
	struct intel_ntb_dev *ndev;
	struct pci_dev *pdev;
	void __iomem *mmio;
	char *buf;
	size_t buf_size;
	ssize_t ret, off;
	union { u64 v64; u32 v32; u16 v16; u8 v8; } u;

	ndev = filp->private_data;
	pdev = ndev_pdev(ndev);
	mmio = ndev->self_mmio;

	buf_size = min(count, 0x800ul);

	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	off = 0;

	off += scnprintf(buf + off, buf_size - off,
			 "NTB Device Information:\n");

	off += scnprintf(buf + off, buf_size - off,
			 "Connection Topology -\t%s\n",
			 ntb_topo_string(ndev->ntb.topo));

	if (ndev->b2b_idx != UINT_MAX) {
		off += scnprintf(buf + off, buf_size - off,
				 "B2B MW Idx -\t\t%u\n", ndev->b2b_idx);
		off += scnprintf(buf + off, buf_size - off,
				 "B2B Offset -\t\t%#lx\n", ndev->b2b_off);
	}

	off += scnprintf(buf + off, buf_size - off,
			 "BAR4 Split -\t\t%s\n",
			 ndev->bar4_split ? "yes" : "no");

	off += scnprintf(buf + off, buf_size - off,
			 "NTB CTL -\t\t%#06x\n", ndev->ntb_ctl);
	off += scnprintf(buf + off, buf_size - off,
			 "LNK STA -\t\t%#06x\n", ndev->lnk_sta);

	if (!ndev->reg->link_is_up(ndev)) {
		off += scnprintf(buf + off, buf_size - off,
				 "Link Status -\t\tDown\n");
	} else {
		off += scnprintf(buf + off, buf_size - off,
				 "Link Status -\t\tUp\n");
		off += scnprintf(buf + off, buf_size - off,
				 "Link Speed -\t\tPCI-E Gen %u\n",
				 NTB_LNK_STA_SPEED(ndev->lnk_sta));
		off += scnprintf(buf + off, buf_size - off,
				 "Link Width -\t\tx%u\n",
				 NTB_LNK_STA_WIDTH(ndev->lnk_sta));
	}

	off += scnprintf(buf + off, buf_size - off,
			 "Memory Window Count -\t%u\n", ndev->mw_count);
	off += scnprintf(buf + off, buf_size - off,
			 "Scratchpad Count -\t%u\n", ndev->spad_count);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Count -\t%u\n", ndev->db_count);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Vector Count -\t%u\n", ndev->db_vec_count);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Vector Shift -\t%u\n", ndev->db_vec_shift);

	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Valid Mask -\t%#llx\n", ndev->db_valid_mask);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Link Mask -\t%#llx\n", ndev->db_link_mask);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Mask Cached -\t%#llx\n", ndev->db_mask);

	u.v64 = ndev_db_read(ndev, mmio + ndev->self_reg->db_mask);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Mask -\t\t%#llx\n", u.v64);

	u.v64 = ndev_db_read(ndev, mmio + ndev->self_reg->db_bell);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Bell -\t\t%#llx\n", u.v64);

	off += scnprintf(buf + off, buf_size - off,
			 "\nNTB Window Size:\n");

	pci_read_config_byte(pdev, XEON_PBAR23SZ_OFFSET, &u.v8);
	off += scnprintf(buf + off, buf_size - off,
			 "PBAR23SZ %hhu\n", u.v8);
	if (!ndev->bar4_split) {
		pci_read_config_byte(pdev, XEON_PBAR45SZ_OFFSET, &u.v8);
		off += scnprintf(buf + off, buf_size - off,
				 "PBAR45SZ %hhu\n", u.v8);
	} else {
		pci_read_config_byte(pdev, XEON_PBAR4SZ_OFFSET, &u.v8);
		off += scnprintf(buf + off, buf_size - off,
				 "PBAR4SZ %hhu\n", u.v8);
		pci_read_config_byte(pdev, XEON_PBAR5SZ_OFFSET, &u.v8);
		off += scnprintf(buf + off, buf_size - off,
				 "PBAR5SZ %hhu\n", u.v8);
	}

	pci_read_config_byte(pdev, XEON_SBAR23SZ_OFFSET, &u.v8);
	off += scnprintf(buf + off, buf_size - off,
			 "SBAR23SZ %hhu\n", u.v8);
	if (!ndev->bar4_split) {
		pci_read_config_byte(pdev, XEON_SBAR45SZ_OFFSET, &u.v8);
		off += scnprintf(buf + off, buf_size - off,
				 "SBAR45SZ %hhu\n", u.v8);
	} else {
		pci_read_config_byte(pdev, XEON_SBAR4SZ_OFFSET, &u.v8);
		off += scnprintf(buf + off, buf_size - off,
				 "SBAR4SZ %hhu\n", u.v8);
		pci_read_config_byte(pdev, XEON_SBAR5SZ_OFFSET, &u.v8);
		off += scnprintf(buf + off, buf_size - off,
				 "SBAR5SZ %hhu\n", u.v8);
	}

	off += scnprintf(buf + off, buf_size - off,
			 "\nNTB Incoming XLAT:\n");

	u.v64 = ioread64(mmio + bar2_off(ndev->xlat_reg->bar2_xlat, 2));
	off += scnprintf(buf + off, buf_size - off,
			 "XLAT23 -\t\t%#018llx\n", u.v64);

	if (ndev->bar4_split) {
		u.v32 = ioread32(mmio + bar2_off(ndev->xlat_reg->bar2_xlat, 4));
		off += scnprintf(buf + off, buf_size - off,
				 "XLAT4 -\t\t\t%#06x\n", u.v32);

		u.v32 = ioread32(mmio + bar2_off(ndev->xlat_reg->bar2_xlat, 5));
		off += scnprintf(buf + off, buf_size - off,
				 "XLAT5 -\t\t\t%#06x\n", u.v32);
	} else {
		u.v64 = ioread64(mmio + bar2_off(ndev->xlat_reg->bar2_xlat, 4));
		off += scnprintf(buf + off, buf_size - off,
				 "XLAT45 -\t\t%#018llx\n", u.v64);
	}

	u.v64 = ioread64(mmio + bar2_off(ndev->xlat_reg->bar2_limit, 2));
	off += scnprintf(buf + off, buf_size - off,
			 "LMT23 -\t\t\t%#018llx\n", u.v64);

	if (ndev->bar4_split) {
		u.v32 = ioread32(mmio + bar2_off(ndev->xlat_reg->bar2_limit, 4));
		off += scnprintf(buf + off, buf_size - off,
				 "LMT4 -\t\t\t%#06x\n", u.v32);
		u.v32 = ioread32(mmio + bar2_off(ndev->xlat_reg->bar2_limit, 5));
		off += scnprintf(buf + off, buf_size - off,
				 "LMT5 -\t\t\t%#06x\n", u.v32);
	} else {
		u.v64 = ioread64(mmio + bar2_off(ndev->xlat_reg->bar2_limit, 4));
		off += scnprintf(buf + off, buf_size - off,
				 "LMT45 -\t\t\t%#018llx\n", u.v64);
	}

	if (pdev_is_xeon(pdev)) {
		if (ntb_topo_is_b2b(ndev->ntb.topo)) {
			off += scnprintf(buf + off, buf_size - off,
					 "\nNTB Outgoing B2B XLAT:\n");

			u.v64 = ioread64(mmio + XEON_PBAR23XLAT_OFFSET);
			off += scnprintf(buf + off, buf_size - off,
					 "B2B XLAT23 -\t\t%#018llx\n", u.v64);

			if (ndev->bar4_split) {
				u.v32 = ioread32(mmio + XEON_PBAR4XLAT_OFFSET);
				off += scnprintf(buf + off, buf_size - off,
						 "B2B XLAT4 -\t\t%#06x\n",
						 u.v32);
				u.v32 = ioread32(mmio + XEON_PBAR5XLAT_OFFSET);
				off += scnprintf(buf + off, buf_size - off,
						 "B2B XLAT5 -\t\t%#06x\n",
						 u.v32);
			} else {
				u.v64 = ioread64(mmio + XEON_PBAR45XLAT_OFFSET);
				off += scnprintf(buf + off, buf_size - off,
						 "B2B XLAT45 -\t\t%#018llx\n",
						 u.v64);
			}

			u.v64 = ioread64(mmio + XEON_PBAR23LMT_OFFSET);
			off += scnprintf(buf + off, buf_size - off,
					 "B2B LMT23 -\t\t%#018llx\n", u.v64);

			if (ndev->bar4_split) {
				u.v32 = ioread32(mmio + XEON_PBAR4LMT_OFFSET);
				off += scnprintf(buf + off, buf_size - off,
						 "B2B LMT4 -\t\t%#06x\n",
						 u.v32);
				u.v32 = ioread32(mmio + XEON_PBAR5LMT_OFFSET);
				off += scnprintf(buf + off, buf_size - off,
						 "B2B LMT5 -\t\t%#06x\n",
						 u.v32);
			} else {
				u.v64 = ioread64(mmio + XEON_PBAR45LMT_OFFSET);
				off += scnprintf(buf + off, buf_size - off,
						 "B2B LMT45 -\t\t%#018llx\n",
						 u.v64);
			}

			off += scnprintf(buf + off, buf_size - off,
					 "\nNTB Secondary BAR:\n");

			u.v64 = ioread64(mmio + XEON_SBAR0BASE_OFFSET);
			off += scnprintf(buf + off, buf_size - off,
					 "SBAR01 -\t\t%#018llx\n", u.v64);

			u.v64 = ioread64(mmio + XEON_SBAR23BASE_OFFSET);
			off += scnprintf(buf + off, buf_size - off,
					 "SBAR23 -\t\t%#018llx\n", u.v64);

			if (ndev->bar4_split) {
				u.v32 = ioread32(mmio + XEON_SBAR4BASE_OFFSET);
				off += scnprintf(buf + off, buf_size - off,
						 "SBAR4 -\t\t\t%#06x\n", u.v32);
				u.v32 = ioread32(mmio + XEON_SBAR5BASE_OFFSET);
				off += scnprintf(buf + off, buf_size - off,
						 "SBAR5 -\t\t\t%#06x\n", u.v32);
			} else {
				u.v64 = ioread64(mmio + XEON_SBAR45BASE_OFFSET);
				off += scnprintf(buf + off, buf_size - off,
						 "SBAR45 -\t\t%#018llx\n",
						 u.v64);
			}
		}

		off += scnprintf(buf + off, buf_size - off,
				 "\nXEON NTB Statistics:\n");

		u.v16 = ioread16(mmio + XEON_USMEMMISS_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "Upstream Memory Miss -\t%u\n", u.v16);

		off += scnprintf(buf + off, buf_size - off,
				 "\nXEON NTB Hardware Errors:\n");

		if (!pci_read_config_word(pdev,
					  XEON_DEVSTS_OFFSET, &u.v16))
			off += scnprintf(buf + off, buf_size - off,
					 "DEVSTS -\t\t%#06x\n", u.v16);

		if (!pci_read_config_word(pdev,
					  XEON_LINK_STATUS_OFFSET, &u.v16))
			off += scnprintf(buf + off, buf_size - off,
					 "LNKSTS -\t\t%#06x\n", u.v16);

		if (!pci_read_config_dword(pdev,
					   XEON_UNCERRSTS_OFFSET, &u.v32))
			off += scnprintf(buf + off, buf_size - off,
					 "UNCERRSTS -\t\t%#06x\n", u.v32);

		if (!pci_read_config_dword(pdev,
					   XEON_CORERRSTS_OFFSET, &u.v32))
			off += scnprintf(buf + off, buf_size - off,
					 "CORERRSTS -\t\t%#06x\n", u.v32);
	}

	ret = simple_read_from_buffer(ubuf, count, offp, buf, off);
	kfree(buf);
	return ret;
}

static ssize_t ndev_debugfs_read(struct file *filp, char __user *ubuf,
				 size_t count, loff_t *offp)
{
	struct intel_ntb_dev *ndev = filp->private_data;

	if (pdev_is_xeon(ndev->ntb.pdev) ||
	    pdev_is_atom(ndev->ntb.pdev))
		return ndev_ntb_debugfs_read(filp, ubuf, count, offp);
	else if (pdev_is_skx_xeon(ndev->ntb.pdev))
		return ndev_ntb3_debugfs_read(filp, ubuf, count, offp);

	return -ENXIO;
}

static void ndev_init_debugfs(struct intel_ntb_dev *ndev)
{
	if (!debugfs_dir) {
		ndev->debugfs_dir = NULL;
		ndev->debugfs_info = NULL;
	} else {
		ndev->debugfs_dir =
			debugfs_create_dir(ndev_name(ndev), debugfs_dir);
		if (!ndev->debugfs_dir)
			ndev->debugfs_info = NULL;
		else
			ndev->debugfs_info =
				debugfs_create_file("info", S_IRUSR,
						    ndev->debugfs_dir, ndev,
						    &intel_ntb_debugfs_info);
	}
}

static void ndev_deinit_debugfs(struct intel_ntb_dev *ndev)
{
	debugfs_remove_recursive(ndev->debugfs_dir);
}

static int intel_ntb_mw_count(struct ntb_dev *ntb)
{
	return ntb_ndev(ntb)->mw_count;
}

static int intel_ntb_mw_get_range(struct ntb_dev *ntb, int idx,
				  phys_addr_t *base,
				  resource_size_t *size,
				  resource_size_t *align,
				  resource_size_t *align_size)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);
	int bar;

	if (idx >= ndev->b2b_idx && !ndev->b2b_off)
		idx += 1;

	bar = ndev_mw_to_bar(ndev, idx);
	if (bar < 0)
		return bar;

	if (base)
		*base = pci_resource_start(ndev->ntb.pdev, bar) +
			(idx == ndev->b2b_idx ? ndev->b2b_off : 0);

	if (size)
		*size = pci_resource_len(ndev->ntb.pdev, bar) -
			(idx == ndev->b2b_idx ? ndev->b2b_off : 0);

	if (align)
		*align = pci_resource_len(ndev->ntb.pdev, bar);

	if (align_size)
		*align_size = 1;

	return 0;
}

static int intel_ntb_mw_set_trans(struct ntb_dev *ntb, int idx,
				  dma_addr_t addr, resource_size_t size)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);
	unsigned long base_reg, xlat_reg, limit_reg;
	resource_size_t bar_size, mw_size;
	void __iomem *mmio;
	u64 base, limit, reg_val;
	int bar;

	if (idx >= ndev->b2b_idx && !ndev->b2b_off)
		idx += 1;

	bar = ndev_mw_to_bar(ndev, idx);
	if (bar < 0)
		return bar;

	bar_size = pci_resource_len(ndev->ntb.pdev, bar);

	if (idx == ndev->b2b_idx)
		mw_size = bar_size - ndev->b2b_off;
	else
		mw_size = bar_size;

	/* hardware requires that addr is aligned to bar size */
	if (addr & (bar_size - 1))
		return -EINVAL;

	/* make sure the range fits in the usable mw size */
	if (size > mw_size)
		return -EINVAL;

	mmio = ndev->self_mmio;
	base_reg = bar0_off(ndev->xlat_reg->bar0_base, bar);
	xlat_reg = bar2_off(ndev->xlat_reg->bar2_xlat, bar);
	limit_reg = bar2_off(ndev->xlat_reg->bar2_limit, bar);

	if (bar < 4 || !ndev->bar4_split) {
		base = ioread64(mmio + base_reg) & NTB_BAR_MASK_64;

		/* Set the limit if supported, if size is not mw_size */
		if (limit_reg && size != mw_size)
			limit = base + size;
		else
			limit = 0;

		/* set and verify setting the translation address */
		iowrite64(addr, mmio + xlat_reg);
		reg_val = ioread64(mmio + xlat_reg);
		if (reg_val != addr) {
			iowrite64(0, mmio + xlat_reg);
			return -EIO;
		}

		/* set and verify setting the limit */
		iowrite64(limit, mmio + limit_reg);
		reg_val = ioread64(mmio + limit_reg);
		if (reg_val != limit) {
			iowrite64(base, mmio + limit_reg);
			iowrite64(0, mmio + xlat_reg);
			return -EIO;
		}
	} else {
		/* split bar addr range must all be 32 bit */
		if (addr & (~0ull << 32))
			return -EINVAL;
		if ((addr + size) & (~0ull << 32))
			return -EINVAL;

		base = ioread32(mmio + base_reg) & NTB_BAR_MASK_32;

		/* Set the limit if supported, if size is not mw_size */
		if (limit_reg && size != mw_size)
			limit = base + size;
		else
			limit = 0;

		/* set and verify setting the translation address */
		iowrite32(addr, mmio + xlat_reg);
		reg_val = ioread32(mmio + xlat_reg);
		if (reg_val != addr) {
			iowrite32(0, mmio + xlat_reg);
			return -EIO;
		}

		/* set and verify setting the limit */
		iowrite32(limit, mmio + limit_reg);
		reg_val = ioread32(mmio + limit_reg);
		if (reg_val != limit) {
			iowrite32(base, mmio + limit_reg);
			iowrite32(0, mmio + xlat_reg);
			return -EIO;
		}
	}

	return 0;
}

static int intel_ntb_link_is_up(struct ntb_dev *ntb,
				enum ntb_speed *speed,
				enum ntb_width *width)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	if (ndev->reg->link_is_up(ndev)) {
		if (speed)
			*speed = NTB_LNK_STA_SPEED(ndev->lnk_sta);
		if (width)
			*width = NTB_LNK_STA_WIDTH(ndev->lnk_sta);
		return 1;
	} else {
		/* TODO MAYBE: is it possible to observe the link speed and
		 * width while link is training? */
		if (speed)
			*speed = NTB_SPEED_NONE;
		if (width)
			*width = NTB_WIDTH_NONE;
		return 0;
	}
}

static int intel_ntb_link_enable(struct ntb_dev *ntb,
				 enum ntb_speed max_speed,
				 enum ntb_width max_width)
{
	struct intel_ntb_dev *ndev;
	u32 ntb_ctl;

	ndev = container_of(ntb, struct intel_ntb_dev, ntb);

	if (ndev->ntb.topo == NTB_TOPO_SEC)
		return -EINVAL;

	dev_dbg(ndev_dev(ndev),
		"Enabling link with max_speed %d max_width %d\n",
		max_speed, max_width);
	if (max_speed != NTB_SPEED_AUTO)
		dev_dbg(ndev_dev(ndev), "ignoring max_speed %d\n", max_speed);
	if (max_width != NTB_WIDTH_AUTO)
		dev_dbg(ndev_dev(ndev), "ignoring max_width %d\n", max_width);

	ntb_ctl = ioread32(ndev->self_mmio + ndev->reg->ntb_ctl);
	ntb_ctl &= ~(NTB_CTL_DISABLE | NTB_CTL_CFG_LOCK);
	ntb_ctl |= NTB_CTL_P2S_BAR2_SNOOP | NTB_CTL_S2P_BAR2_SNOOP;
	ntb_ctl |= NTB_CTL_P2S_BAR4_SNOOP | NTB_CTL_S2P_BAR4_SNOOP;
	if (ndev->bar4_split)
		ntb_ctl |= NTB_CTL_P2S_BAR5_SNOOP | NTB_CTL_S2P_BAR5_SNOOP;
	iowrite32(ntb_ctl, ndev->self_mmio + ndev->reg->ntb_ctl);

	return 0;
}

static int intel_ntb_link_disable(struct ntb_dev *ntb)
{
	struct intel_ntb_dev *ndev;
	u32 ntb_cntl;

	ndev = container_of(ntb, struct intel_ntb_dev, ntb);

	if (ndev->ntb.topo == NTB_TOPO_SEC)
		return -EINVAL;

	dev_dbg(ndev_dev(ndev), "Disabling link\n");

	/* Bring NTB link down */
	ntb_cntl = ioread32(ndev->self_mmio + ndev->reg->ntb_ctl);
	ntb_cntl &= ~(NTB_CTL_P2S_BAR2_SNOOP | NTB_CTL_S2P_BAR2_SNOOP);
	ntb_cntl &= ~(NTB_CTL_P2S_BAR4_SNOOP | NTB_CTL_S2P_BAR4_SNOOP);
	if (ndev->bar4_split)
		ntb_cntl &= ~(NTB_CTL_P2S_BAR5_SNOOP | NTB_CTL_S2P_BAR5_SNOOP);
	ntb_cntl |= NTB_CTL_DISABLE | NTB_CTL_CFG_LOCK;
	iowrite32(ntb_cntl, ndev->self_mmio + ndev->reg->ntb_ctl);

	return 0;
}

static int intel_ntb_db_is_unsafe(struct ntb_dev *ntb)
{
	return ndev_ignore_unsafe(ntb_ndev(ntb), NTB_UNSAFE_DB);
}

static u64 intel_ntb_db_valid_mask(struct ntb_dev *ntb)
{
	return ntb_ndev(ntb)->db_valid_mask;
}

static int intel_ntb_db_vector_count(struct ntb_dev *ntb)
{
	struct intel_ntb_dev *ndev;

	ndev = container_of(ntb, struct intel_ntb_dev, ntb);

	return ndev->db_vec_count;
}

static u64 intel_ntb_db_vector_mask(struct ntb_dev *ntb, int db_vector)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	if (db_vector < 0 || db_vector > ndev->db_vec_count)
		return 0;

	return ndev->db_valid_mask & ndev_vec_mask(ndev, db_vector);
}

static u64 intel_ntb_db_read(struct ntb_dev *ntb)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_db_read(ndev,
			    ndev->self_mmio +
			    ndev->self_reg->db_bell);
}

static int intel_ntb_db_clear(struct ntb_dev *ntb, u64 db_bits)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_db_write(ndev, db_bits,
			     ndev->self_mmio +
			     ndev->self_reg->db_bell);
}

static int intel_ntb_db_set_mask(struct ntb_dev *ntb, u64 db_bits)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_db_set_mask(ndev, db_bits,
				ndev->self_mmio +
				ndev->self_reg->db_mask);
}

static int intel_ntb_db_clear_mask(struct ntb_dev *ntb, u64 db_bits)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_db_clear_mask(ndev, db_bits,
				  ndev->self_mmio +
				  ndev->self_reg->db_mask);
}

static int intel_ntb_peer_db_addr(struct ntb_dev *ntb,
				  phys_addr_t *db_addr,
				  resource_size_t *db_size)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_db_addr(ndev, db_addr, db_size, ndev->peer_addr,
			    ndev->peer_reg->db_bell);
}

static int intel_ntb_peer_db_set(struct ntb_dev *ntb, u64 db_bits)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_db_write(ndev, db_bits,
			     ndev->peer_mmio +
			     ndev->peer_reg->db_bell);
}

static int intel_ntb_spad_is_unsafe(struct ntb_dev *ntb)
{
	return ndev_ignore_unsafe(ntb_ndev(ntb), NTB_UNSAFE_SPAD);
}

static int intel_ntb_spad_count(struct ntb_dev *ntb)
{
	struct intel_ntb_dev *ndev;

	ndev = container_of(ntb, struct intel_ntb_dev, ntb);

	return ndev->spad_count;
}

static u32 intel_ntb_spad_read(struct ntb_dev *ntb, int idx)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_spad_read(ndev, idx,
			      ndev->self_mmio +
			      ndev->self_reg->spad);
}

static int intel_ntb_spad_write(struct ntb_dev *ntb,
				int idx, u32 val)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_spad_write(ndev, idx, val,
			       ndev->self_mmio +
			       ndev->self_reg->spad);
}

static int intel_ntb_peer_spad_addr(struct ntb_dev *ntb, int idx,
				    phys_addr_t *spad_addr)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_spad_addr(ndev, idx, spad_addr, ndev->peer_addr,
			      ndev->peer_reg->spad);
}

static u32 intel_ntb_peer_spad_read(struct ntb_dev *ntb, int idx)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_spad_read(ndev, idx,
			      ndev->peer_mmio +
			      ndev->peer_reg->spad);
}

static int intel_ntb_peer_spad_write(struct ntb_dev *ntb,
				     int idx, u32 val)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_spad_write(ndev, idx, val,
			       ndev->peer_mmio +
			       ndev->peer_reg->spad);
}

/* ATOM */

static u64 atom_db_ioread(void __iomem *mmio)
{
	return ioread64(mmio);
}

static void atom_db_iowrite(u64 bits, void __iomem *mmio)
{
	iowrite64(bits, mmio);
}

static int atom_poll_link(struct intel_ntb_dev *ndev)
{
	u32 ntb_ctl;

	ntb_ctl = ioread32(ndev->self_mmio + ATOM_NTBCNTL_OFFSET);

	if (ntb_ctl == ndev->ntb_ctl)
		return 0;

	ndev->ntb_ctl = ntb_ctl;

	ndev->lnk_sta = ioread32(ndev->self_mmio + ATOM_LINK_STATUS_OFFSET);

	return 1;
}

static int atom_link_is_up(struct intel_ntb_dev *ndev)
{
	return ATOM_NTB_CTL_ACTIVE(ndev->ntb_ctl);
}

static int atom_link_is_err(struct intel_ntb_dev *ndev)
{
	if (ioread32(ndev->self_mmio + ATOM_LTSSMSTATEJMP_OFFSET)
	    & ATOM_LTSSMSTATEJMP_FORCEDETECT)
		return 1;

	if (ioread32(ndev->self_mmio + ATOM_IBSTERRRCRVSTS0_OFFSET)
	    & ATOM_IBIST_ERR_OFLOW)
		return 1;

	return 0;
}

static inline enum ntb_topo atom_ppd_topo(struct intel_ntb_dev *ndev, u32 ppd)
{
	switch (ppd & ATOM_PPD_TOPO_MASK) {
	case ATOM_PPD_TOPO_B2B_USD:
		dev_dbg(ndev_dev(ndev), "PPD %d B2B USD\n", ppd);
		return NTB_TOPO_B2B_USD;

	case ATOM_PPD_TOPO_B2B_DSD:
		dev_dbg(ndev_dev(ndev), "PPD %d B2B DSD\n", ppd);
		return NTB_TOPO_B2B_DSD;

	case ATOM_PPD_TOPO_PRI_USD:
	case ATOM_PPD_TOPO_PRI_DSD: /* accept bogus PRI_DSD */
	case ATOM_PPD_TOPO_SEC_USD:
	case ATOM_PPD_TOPO_SEC_DSD: /* accept bogus SEC_DSD */
		dev_dbg(ndev_dev(ndev), "PPD %d non B2B disabled\n", ppd);
		return NTB_TOPO_NONE;
	}

	dev_dbg(ndev_dev(ndev), "PPD %d invalid\n", ppd);
	return NTB_TOPO_NONE;
}

static void atom_link_hb(struct work_struct *work)
{
	struct intel_ntb_dev *ndev = hb_ndev(work);
	unsigned long poll_ts;
	void __iomem *mmio;
	u32 status32;

	poll_ts = ndev->last_ts + ATOM_LINK_HB_TIMEOUT;

	/* Delay polling the link status if an interrupt was received,
	 * unless the cached link status says the link is down.
	 */
	if (time_after(poll_ts, jiffies) && atom_link_is_up(ndev)) {
		schedule_delayed_work(&ndev->hb_timer, poll_ts - jiffies);
		return;
	}

	if (atom_poll_link(ndev))
		ntb_link_event(&ndev->ntb);

	if (atom_link_is_up(ndev) || !atom_link_is_err(ndev)) {
		schedule_delayed_work(&ndev->hb_timer, ATOM_LINK_HB_TIMEOUT);
		return;
	}

	/* Link is down with error: recover the link! */

	mmio = ndev->self_mmio;

	/* Driver resets the NTB ModPhy lanes - magic! */
	iowrite8(0xe0, mmio + ATOM_MODPHY_PCSREG6);
	iowrite8(0x40, mmio + ATOM_MODPHY_PCSREG4);
	iowrite8(0x60, mmio + ATOM_MODPHY_PCSREG4);
	iowrite8(0x60, mmio + ATOM_MODPHY_PCSREG6);

	/* Driver waits 100ms to allow the NTB ModPhy to settle */
	msleep(100);

	/* Clear AER Errors, write to clear */
	status32 = ioread32(mmio + ATOM_ERRCORSTS_OFFSET);
	dev_dbg(ndev_dev(ndev), "ERRCORSTS = %x\n", status32);
	status32 &= PCI_ERR_COR_REP_ROLL;
	iowrite32(status32, mmio + ATOM_ERRCORSTS_OFFSET);

	/* Clear unexpected electrical idle event in LTSSM, write to clear */
	status32 = ioread32(mmio + ATOM_LTSSMERRSTS0_OFFSET);
	dev_dbg(ndev_dev(ndev), "LTSSMERRSTS0 = %x\n", status32);
	status32 |= ATOM_LTSSMERRSTS0_UNEXPECTEDEI;
	iowrite32(status32, mmio + ATOM_LTSSMERRSTS0_OFFSET);

	/* Clear DeSkew Buffer error, write to clear */
	status32 = ioread32(mmio + ATOM_DESKEWSTS_OFFSET);
	dev_dbg(ndev_dev(ndev), "DESKEWSTS = %x\n", status32);
	status32 |= ATOM_DESKEWSTS_DBERR;
	iowrite32(status32, mmio + ATOM_DESKEWSTS_OFFSET);

	status32 = ioread32(mmio + ATOM_IBSTERRRCRVSTS0_OFFSET);
	dev_dbg(ndev_dev(ndev), "IBSTERRRCRVSTS0 = %x\n", status32);
	status32 &= ATOM_IBIST_ERR_OFLOW;
	iowrite32(status32, mmio + ATOM_IBSTERRRCRVSTS0_OFFSET);

	/* Releases the NTB state machine to allow the link to retrain */
	status32 = ioread32(mmio + ATOM_LTSSMSTATEJMP_OFFSET);
	dev_dbg(ndev_dev(ndev), "LTSSMSTATEJMP = %x\n", status32);
	status32 &= ~ATOM_LTSSMSTATEJMP_FORCEDETECT;
	iowrite32(status32, mmio + ATOM_LTSSMSTATEJMP_OFFSET);

	/* There is a potential race between the 2 NTB devices recovering at the
	 * same time.  If the times are the same, the link will not recover and
	 * the driver will be stuck in this loop forever.  Add a random interval
	 * to the recovery time to prevent this race.
	 */
	schedule_delayed_work(&ndev->hb_timer, ATOM_LINK_RECOVERY_TIME
			      + prandom_u32() % ATOM_LINK_RECOVERY_TIME);
}

static int atom_init_isr(struct intel_ntb_dev *ndev)
{
	int rc;

	rc = ndev_init_isr(ndev, 1, ATOM_DB_MSIX_VECTOR_COUNT,
			   ATOM_DB_MSIX_VECTOR_SHIFT, ATOM_DB_TOTAL_SHIFT);
	if (rc)
		return rc;

	/* ATOM doesn't have link status interrupt, poll on that platform */
	ndev->last_ts = jiffies;
	INIT_DELAYED_WORK(&ndev->hb_timer, atom_link_hb);
	schedule_delayed_work(&ndev->hb_timer, ATOM_LINK_HB_TIMEOUT);

	return 0;
}

static void atom_deinit_isr(struct intel_ntb_dev *ndev)
{
	cancel_delayed_work_sync(&ndev->hb_timer);
	ndev_deinit_isr(ndev);
}

static int atom_init_ntb(struct intel_ntb_dev *ndev)
{
	ndev->mw_count = ATOM_MW_COUNT;
	ndev->spad_count = ATOM_SPAD_COUNT;
	ndev->db_count = ATOM_DB_COUNT;

	switch (ndev->ntb.topo) {
	case NTB_TOPO_B2B_USD:
	case NTB_TOPO_B2B_DSD:
		ndev->self_reg = &atom_pri_reg;
		ndev->peer_reg = &atom_b2b_reg;
		ndev->xlat_reg = &atom_sec_xlat;

		/* Enable Bus Master and Memory Space on the secondary side */
		iowrite16(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
			  ndev->self_mmio + ATOM_SPCICMD_OFFSET);

		break;

	default:
		return -EINVAL;
	}

	ndev->db_valid_mask = BIT_ULL(ndev->db_count) - 1;

	return 0;
}

static int atom_init_dev(struct intel_ntb_dev *ndev)
{
	u32 ppd;
	int rc;

	rc = pci_read_config_dword(ndev->ntb.pdev, ATOM_PPD_OFFSET, &ppd);
	if (rc)
		return -EIO;

	ndev->ntb.topo = atom_ppd_topo(ndev, ppd);
	if (ndev->ntb.topo == NTB_TOPO_NONE)
		return -EINVAL;

	rc = atom_init_ntb(ndev);
	if (rc)
		return rc;

	rc = atom_init_isr(ndev);
	if (rc)
		return rc;

	if (ndev->ntb.topo != NTB_TOPO_SEC) {
		/* Initiate PCI-E link training */
		rc = pci_write_config_dword(ndev->ntb.pdev, ATOM_PPD_OFFSET,
					    ppd | ATOM_PPD_INIT_LINK);
		if (rc)
			return rc;
	}

	return 0;
}

static void atom_deinit_dev(struct intel_ntb_dev *ndev)
{
	atom_deinit_isr(ndev);
}

/* Skylake Xeon NTB */

static int skx_poll_link(struct intel_ntb_dev *ndev)
{
	u16 reg_val;
	int rc;

	ndev->reg->db_iowrite(ndev->db_link_mask,
			      ndev->self_mmio +
			      ndev->self_reg->db_clear);

	rc = pci_read_config_word(ndev->ntb.pdev,
				  SKX_LINK_STATUS_OFFSET, &reg_val);
	if (rc)
		return 0;

	if (reg_val == ndev->lnk_sta)
		return 0;

	ndev->lnk_sta = reg_val;

	return 1;
}

static u64 skx_db_ioread(void __iomem *mmio)
{
	return ioread64(mmio);
}

static void skx_db_iowrite(u64 bits, void __iomem *mmio)
{
	iowrite64(bits, mmio);
}

static int skx_init_isr(struct intel_ntb_dev *ndev)
{
	int i;

	/*
	 * The MSIX vectors and the interrupt status bits are not lined up
	 * on Skylake. By default the link status bit is bit 32, however it
	 * is by default MSIX vector0. We need to fixup to line them up.
	 * The vectors at reset is 1-32,0. We need to reprogram to 0-32.
	 */

	for (i = 0; i < SKX_DB_MSIX_VECTOR_COUNT; i++)
		iowrite8(i, ndev->self_mmio + SKX_INTVEC_OFFSET + i);

	/* move link status down one as workaround */
	if (ndev->hwerr_flags & NTB_HWERR_MSIX_VECTOR32_BAD) {
		iowrite8(SKX_DB_MSIX_VECTOR_COUNT - 2,
			 ndev->self_mmio + SKX_INTVEC_OFFSET +
			 (SKX_DB_MSIX_VECTOR_COUNT - 1));
	}

	return ndev_init_isr(ndev, SKX_DB_MSIX_VECTOR_COUNT,
			     SKX_DB_MSIX_VECTOR_COUNT,
			     SKX_DB_MSIX_VECTOR_SHIFT,
			     SKX_DB_TOTAL_SHIFT);
}

static int skx_setup_b2b_mw(struct intel_ntb_dev *ndev,
			    const struct intel_b2b_addr *addr,
			    const struct intel_b2b_addr *peer_addr)
{
	struct pci_dev *pdev;
	void __iomem *mmio;
	resource_size_t bar_size;
	phys_addr_t bar_addr;
	int b2b_bar;
	u8 bar_sz;

	pdev = ndev_pdev(ndev);
	mmio = ndev->self_mmio;

	if (ndev->b2b_idx == UINT_MAX) {
		dev_dbg(ndev_dev(ndev), "not using b2b mw\n");
		b2b_bar = 0;
		ndev->b2b_off = 0;
	} else {
		b2b_bar = ndev_mw_to_bar(ndev, ndev->b2b_idx);
		if (b2b_bar < 0)
			return -EIO;

		dev_dbg(ndev_dev(ndev), "using b2b mw bar %d\n", b2b_bar);

		bar_size = pci_resource_len(ndev->ntb.pdev, b2b_bar);

		dev_dbg(ndev_dev(ndev), "b2b bar size %#llx\n", bar_size);

		if (b2b_mw_share && ((bar_size >> 1) >= XEON_B2B_MIN_SIZE)) {
			dev_dbg(ndev_dev(ndev),
				"b2b using first half of bar\n");
			ndev->b2b_off = bar_size >> 1;
		} else if (bar_size >= XEON_B2B_MIN_SIZE) {
			dev_dbg(ndev_dev(ndev),
				"b2b using whole bar\n");
			ndev->b2b_off = 0;
			--ndev->mw_count;
		} else {
			dev_dbg(ndev_dev(ndev),
				"b2b bar size is too small\n");
			return -EIO;
		}
	}

	/*
	 * Reset the secondary bar sizes to match the primary bar sizes,
	 * except disable or halve the size of the b2b secondary bar.
	 */
	pci_read_config_byte(pdev, SKX_IMBAR1SZ_OFFSET, &bar_sz);
	dev_dbg(ndev_dev(ndev), "IMBAR1SZ %#x\n", bar_sz);
	if (b2b_bar == 1) {
		if (ndev->b2b_off)
			bar_sz -= 1;
		else
			bar_sz = 0;
	}

	pci_write_config_byte(pdev, SKX_EMBAR1SZ_OFFSET, bar_sz);
	pci_read_config_byte(pdev, SKX_EMBAR1SZ_OFFSET, &bar_sz);
	dev_dbg(ndev_dev(ndev), "EMBAR1SZ %#x\n", bar_sz);

	pci_read_config_byte(pdev, SKX_IMBAR2SZ_OFFSET, &bar_sz);
	dev_dbg(ndev_dev(ndev), "IMBAR2SZ %#x\n", bar_sz);
	if (b2b_bar == 2) {
		if (ndev->b2b_off)
			bar_sz -= 1;
		else
			bar_sz = 0;
	}

	pci_write_config_byte(pdev, SKX_EMBAR2SZ_OFFSET, bar_sz);
	pci_read_config_byte(pdev, SKX_EMBAR2SZ_OFFSET, &bar_sz);
	dev_dbg(ndev_dev(ndev), "EMBAR2SZ %#x\n", bar_sz);

	/* SBAR01 hit by first part of the b2b bar */
	if (b2b_bar == 0)
		bar_addr = addr->bar0_addr;
	else if (b2b_bar == 1)
		bar_addr = addr->bar2_addr64;
	else if (b2b_bar == 2)
		bar_addr = addr->bar4_addr64;
	else
		return -EIO;

	/* setup incoming bar limits == base addrs (zero length windows) */
	bar_addr = addr->bar2_addr64 + (b2b_bar == 1 ? ndev->b2b_off : 0);
	iowrite64(bar_addr, mmio + SKX_IMBAR1XLMT_OFFSET);
	bar_addr = ioread64(mmio + SKX_IMBAR1XLMT_OFFSET);
	dev_dbg(ndev_dev(ndev), "IMBAR1XLMT %#018llx\n", bar_addr);

	bar_addr = addr->bar4_addr64 + (b2b_bar == 2 ? ndev->b2b_off : 0);
	iowrite64(bar_addr, mmio + SKX_IMBAR2XLMT_OFFSET);
	bar_addr = ioread64(mmio + SKX_IMBAR2XLMT_OFFSET);
	dev_dbg(ndev_dev(ndev), "IMBAR2XLMT %#018llx\n", bar_addr);

	/* zero incoming translation addrs */
	iowrite64(0, mmio + SKX_IMBAR1XBASE_OFFSET);
	iowrite64(0, mmio + SKX_IMBAR2XBASE_OFFSET);

	ndev->peer_mmio = ndev->self_mmio;

	return 0;
}

static int skx_init_ntb(struct intel_ntb_dev *ndev)
{
	int rc;


	ndev->mw_count = XEON_MW_COUNT;
	ndev->spad_count = SKX_SPAD_COUNT;
	ndev->db_count = SKX_DB_COUNT;
	ndev->db_link_mask = SKX_DB_LINK_BIT;

	/* DB fixup for using 31 right now */
	if (ndev->hwerr_flags & NTB_HWERR_MSIX_VECTOR32_BAD)
		ndev->db_link_mask |= BIT_ULL(31);

	switch (ndev->ntb.topo) {
	case NTB_TOPO_B2B_USD:
	case NTB_TOPO_B2B_DSD:
		ndev->self_reg = &skx_pri_reg;
		ndev->peer_reg = &skx_b2b_reg;
		ndev->xlat_reg = &skx_sec_xlat;

		if (ndev->ntb.topo == NTB_TOPO_B2B_USD) {
			rc = skx_setup_b2b_mw(ndev,
					      &xeon_b2b_dsd_addr,
					      &xeon_b2b_usd_addr);
		} else {
			rc = skx_setup_b2b_mw(ndev,
					      &xeon_b2b_usd_addr,
					      &xeon_b2b_dsd_addr);
		}

		if (rc)
			return rc;

		/* Enable Bus Master and Memory Space on the secondary side */
		iowrite16(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
			  ndev->self_mmio + SKX_SPCICMD_OFFSET);

		break;

	default:
		return -EINVAL;
	}

	ndev->db_valid_mask = BIT_ULL(ndev->db_count) - 1;

	ndev->reg->db_iowrite(ndev->db_valid_mask,
			      ndev->self_mmio +
			      ndev->self_reg->db_mask);

	return 0;
}

static int skx_init_dev(struct intel_ntb_dev *ndev)
{
	struct pci_dev *pdev;
	u8 ppd;
	int rc;

	pdev = ndev_pdev(ndev);

	ndev->reg = &skx_reg;

	rc = pci_read_config_byte(pdev, XEON_PPD_OFFSET, &ppd);
	if (rc)
		return -EIO;

	ndev->ntb.topo = xeon_ppd_topo(ndev, ppd);
	dev_dbg(ndev_dev(ndev), "ppd %#x topo %s\n", ppd,
		ntb_topo_string(ndev->ntb.topo));
	if (ndev->ntb.topo == NTB_TOPO_NONE)
		return -EINVAL;

	if (pdev_is_skx_xeon(pdev))
		ndev->hwerr_flags |= NTB_HWERR_MSIX_VECTOR32_BAD;

	rc = skx_init_ntb(ndev);
	if (rc)
		return rc;

	return skx_init_isr(ndev);
}

static int intel_ntb3_link_enable(struct ntb_dev *ntb,
				  enum ntb_speed max_speed,
				  enum ntb_width max_width)
{
	struct intel_ntb_dev *ndev;
	u32 ntb_ctl;

	ndev = container_of(ntb, struct intel_ntb_dev, ntb);

	dev_dbg(ndev_dev(ndev),
		"Enabling link with max_speed %d max_width %d\n",
		max_speed, max_width);

	if (max_speed != NTB_SPEED_AUTO)
		dev_dbg(ndev_dev(ndev), "ignoring max_speed %d\n", max_speed);
	if (max_width != NTB_WIDTH_AUTO)
		dev_dbg(ndev_dev(ndev), "ignoring max_width %d\n", max_width);

	ntb_ctl = ioread32(ndev->self_mmio + ndev->reg->ntb_ctl);
	ntb_ctl &= ~(NTB_CTL_DISABLE | NTB_CTL_CFG_LOCK);
	ntb_ctl |= NTB_CTL_P2S_BAR2_SNOOP | NTB_CTL_S2P_BAR2_SNOOP;
	ntb_ctl |= NTB_CTL_P2S_BAR4_SNOOP | NTB_CTL_S2P_BAR4_SNOOP;
	iowrite32(ntb_ctl, ndev->self_mmio + ndev->reg->ntb_ctl);

	return 0;
}
static int intel_ntb3_mw_set_trans(struct ntb_dev *ntb, int idx,
				   dma_addr_t addr, resource_size_t size)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);
	unsigned long xlat_reg, limit_reg;
	resource_size_t bar_size, mw_size;
	void __iomem *mmio;
	u64 base, limit, reg_val;
	int bar;

	if (idx >= ndev->b2b_idx && !ndev->b2b_off)
		idx += 1;

	bar = ndev_mw_to_bar(ndev, idx);
	if (bar < 0)
		return bar;

	bar_size = pci_resource_len(ndev->ntb.pdev, bar);

	if (idx == ndev->b2b_idx)
		mw_size = bar_size - ndev->b2b_off;
	else
		mw_size = bar_size;

	/* hardware requires that addr is aligned to bar size */
	if (addr & (bar_size - 1))
		return -EINVAL;

	/* make sure the range fits in the usable mw size */
	if (size > mw_size)
		return -EINVAL;

	mmio = ndev->self_mmio;
	xlat_reg = ndev->xlat_reg->bar2_xlat + (idx * 0x10);
	limit_reg = ndev->xlat_reg->bar2_limit + (idx * 0x10);
	base = pci_resource_start(ndev->ntb.pdev, bar);

	/* Set the limit if supported, if size is not mw_size */
	if (limit_reg && size != mw_size)
		limit = base + size;
	else
		limit = base + mw_size;

	/* set and verify setting the translation address */
	iowrite64(addr, mmio + xlat_reg);
	reg_val = ioread64(mmio + xlat_reg);
	if (reg_val != addr) {
		iowrite64(0, mmio + xlat_reg);
		return -EIO;
	}

	dev_dbg(ndev_dev(ndev), "BAR %d IMBARXBASE: %#Lx\n", bar, reg_val);

	/* set and verify setting the limit */
	iowrite64(limit, mmio + limit_reg);
	reg_val = ioread64(mmio + limit_reg);
	if (reg_val != limit) {
		iowrite64(base, mmio + limit_reg);
		iowrite64(0, mmio + xlat_reg);
		return -EIO;
	}

	dev_dbg(ndev_dev(ndev), "BAR %d IMBARXLMT: %#Lx\n", bar, reg_val);

	/* setup the EP */
	limit_reg = ndev->xlat_reg->bar2_limit + (idx * 0x10) + 0x4000;
	base = ioread64(mmio + SKX_EMBAR1_OFFSET + (8 * idx));
	base &= ~0xf;

	if (limit_reg && size != mw_size)
		limit = base + size;
	else
		limit = base + mw_size;

	/* set and verify setting the limit */
	iowrite64(limit, mmio + limit_reg);
	reg_val = ioread64(mmio + limit_reg);
	if (reg_val != limit) {
		iowrite64(base, mmio + limit_reg);
		iowrite64(0, mmio + xlat_reg);
		return -EIO;
	}

	dev_dbg(ndev_dev(ndev), "BAR %d EMBARXLMT: %#Lx\n", bar, reg_val);

	return 0;
}

static int intel_ntb3_peer_db_set(struct ntb_dev *ntb, u64 db_bits)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);
	int bit;

	if (db_bits & ~ndev->db_valid_mask)
		return -EINVAL;

	while (db_bits) {
		bit = __ffs(db_bits);
		iowrite32(1, ndev->peer_mmio +
				ndev->peer_reg->db_bell + (bit * 4));
		db_bits &= db_bits - 1;
	}

	return 0;
}

static u64 intel_ntb3_db_read(struct ntb_dev *ntb)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_db_read(ndev,
			    ndev->self_mmio +
			    ndev->self_reg->db_clear);
}

static int intel_ntb3_db_clear(struct ntb_dev *ntb, u64 db_bits)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_db_write(ndev, db_bits,
			     ndev->self_mmio +
			     ndev->self_reg->db_clear);
}

/* XEON */

static u64 xeon_db_ioread(void __iomem *mmio)
{
	return (u64)ioread16(mmio);
}

static void xeon_db_iowrite(u64 bits, void __iomem *mmio)
{
	iowrite16((u16)bits, mmio);
}

static int xeon_poll_link(struct intel_ntb_dev *ndev)
{
	u16 reg_val;
	int rc;

	ndev->reg->db_iowrite(ndev->db_link_mask,
			      ndev->self_mmio +
			      ndev->self_reg->db_bell);

	rc = pci_read_config_word(ndev->ntb.pdev,
				  XEON_LINK_STATUS_OFFSET, &reg_val);
	if (rc)
		return 0;

	if (reg_val == ndev->lnk_sta)
		return 0;

	ndev->lnk_sta = reg_val;

	return 1;
}

static int xeon_link_is_up(struct intel_ntb_dev *ndev)
{
	if (ndev->ntb.topo == NTB_TOPO_SEC)
		return 1;

	return NTB_LNK_STA_ACTIVE(ndev->lnk_sta);
}

static inline enum ntb_topo xeon_ppd_topo(struct intel_ntb_dev *ndev, u8 ppd)
{
	switch (ppd & XEON_PPD_TOPO_MASK) {
	case XEON_PPD_TOPO_B2B_USD:
		return NTB_TOPO_B2B_USD;

	case XEON_PPD_TOPO_B2B_DSD:
		return NTB_TOPO_B2B_DSD;

	case XEON_PPD_TOPO_PRI_USD:
	case XEON_PPD_TOPO_PRI_DSD: /* accept bogus PRI_DSD */
		return NTB_TOPO_PRI;

	case XEON_PPD_TOPO_SEC_USD:
	case XEON_PPD_TOPO_SEC_DSD: /* accept bogus SEC_DSD */
		return NTB_TOPO_SEC;
	}

	return NTB_TOPO_NONE;
}

static inline int xeon_ppd_bar4_split(struct intel_ntb_dev *ndev, u8 ppd)
{
	if (ppd & XEON_PPD_SPLIT_BAR_MASK) {
		dev_dbg(ndev_dev(ndev), "PPD %d split bar\n", ppd);
		return 1;
	}
	return 0;
}

static int xeon_init_isr(struct intel_ntb_dev *ndev)
{
	return ndev_init_isr(ndev, XEON_DB_MSIX_VECTOR_COUNT,
			     XEON_DB_MSIX_VECTOR_COUNT,
			     XEON_DB_MSIX_VECTOR_SHIFT,
			     XEON_DB_TOTAL_SHIFT);
}

static void xeon_deinit_isr(struct intel_ntb_dev *ndev)
{
	ndev_deinit_isr(ndev);
}

static int xeon_setup_b2b_mw(struct intel_ntb_dev *ndev,
			     const struct intel_b2b_addr *addr,
			     const struct intel_b2b_addr *peer_addr)
{
	struct pci_dev *pdev;
	void __iomem *mmio;
	resource_size_t bar_size;
	phys_addr_t bar_addr;
	int b2b_bar;
	u8 bar_sz;

	pdev = ndev_pdev(ndev);
	mmio = ndev->self_mmio;

	if (ndev->b2b_idx == UINT_MAX) {
		dev_dbg(ndev_dev(ndev), "not using b2b mw\n");
		b2b_bar = 0;
		ndev->b2b_off = 0;
	} else {
		b2b_bar = ndev_mw_to_bar(ndev, ndev->b2b_idx);
		if (b2b_bar < 0)
			return -EIO;

		dev_dbg(ndev_dev(ndev), "using b2b mw bar %d\n", b2b_bar);

		bar_size = pci_resource_len(ndev->ntb.pdev, b2b_bar);

		dev_dbg(ndev_dev(ndev), "b2b bar size %#llx\n", bar_size);

		if (b2b_mw_share && XEON_B2B_MIN_SIZE <= bar_size >> 1) {
			dev_dbg(ndev_dev(ndev),
				"b2b using first half of bar\n");
			ndev->b2b_off = bar_size >> 1;
		} else if (XEON_B2B_MIN_SIZE <= bar_size) {
			dev_dbg(ndev_dev(ndev),
				"b2b using whole bar\n");
			ndev->b2b_off = 0;
			--ndev->mw_count;
		} else {
			dev_dbg(ndev_dev(ndev),
				"b2b bar size is too small\n");
			return -EIO;
		}
	}

	/* Reset the secondary bar sizes to match the primary bar sizes,
	 * except disable or halve the size of the b2b secondary bar.
	 *
	 * Note: code for each specific bar size register, because the register
	 * offsets are not in a consistent order (bar5sz comes after ppd, odd).
	 */
	pci_read_config_byte(pdev, XEON_PBAR23SZ_OFFSET, &bar_sz);
	dev_dbg(ndev_dev(ndev), "PBAR23SZ %#x\n", bar_sz);
	if (b2b_bar == 2) {
		if (ndev->b2b_off)
			bar_sz -= 1;
		else
			bar_sz = 0;
	}
	pci_write_config_byte(pdev, XEON_SBAR23SZ_OFFSET, bar_sz);
	pci_read_config_byte(pdev, XEON_SBAR23SZ_OFFSET, &bar_sz);
	dev_dbg(ndev_dev(ndev), "SBAR23SZ %#x\n", bar_sz);

	if (!ndev->bar4_split) {
		pci_read_config_byte(pdev, XEON_PBAR45SZ_OFFSET, &bar_sz);
		dev_dbg(ndev_dev(ndev), "PBAR45SZ %#x\n", bar_sz);
		if (b2b_bar == 4) {
			if (ndev->b2b_off)
				bar_sz -= 1;
			else
				bar_sz = 0;
		}
		pci_write_config_byte(pdev, XEON_SBAR45SZ_OFFSET, bar_sz);
		pci_read_config_byte(pdev, XEON_SBAR45SZ_OFFSET, &bar_sz);
		dev_dbg(ndev_dev(ndev), "SBAR45SZ %#x\n", bar_sz);
	} else {
		pci_read_config_byte(pdev, XEON_PBAR4SZ_OFFSET, &bar_sz);
		dev_dbg(ndev_dev(ndev), "PBAR4SZ %#x\n", bar_sz);
		if (b2b_bar == 4) {
			if (ndev->b2b_off)
				bar_sz -= 1;
			else
				bar_sz = 0;
		}
		pci_write_config_byte(pdev, XEON_SBAR4SZ_OFFSET, bar_sz);
		pci_read_config_byte(pdev, XEON_SBAR4SZ_OFFSET, &bar_sz);
		dev_dbg(ndev_dev(ndev), "SBAR4SZ %#x\n", bar_sz);

		pci_read_config_byte(pdev, XEON_PBAR5SZ_OFFSET, &bar_sz);
		dev_dbg(ndev_dev(ndev), "PBAR5SZ %#x\n", bar_sz);
		if (b2b_bar == 5) {
			if (ndev->b2b_off)
				bar_sz -= 1;
			else
				bar_sz = 0;
		}
		pci_write_config_byte(pdev, XEON_SBAR5SZ_OFFSET, bar_sz);
		pci_read_config_byte(pdev, XEON_SBAR5SZ_OFFSET, &bar_sz);
		dev_dbg(ndev_dev(ndev), "SBAR5SZ %#x\n", bar_sz);
	}

	/* SBAR01 hit by first part of the b2b bar */
	if (b2b_bar == 0)
		bar_addr = addr->bar0_addr;
	else if (b2b_bar == 2)
		bar_addr = addr->bar2_addr64;
	else if (b2b_bar == 4 && !ndev->bar4_split)
		bar_addr = addr->bar4_addr64;
	else if (b2b_bar == 4)
		bar_addr = addr->bar4_addr32;
	else if (b2b_bar == 5)
		bar_addr = addr->bar5_addr32;
	else
		return -EIO;

	dev_dbg(ndev_dev(ndev), "SBAR01 %#018llx\n", bar_addr);
	iowrite64(bar_addr, mmio + XEON_SBAR0BASE_OFFSET);

	/* Other SBAR are normally hit by the PBAR xlat, except for b2b bar.
	 * The b2b bar is either disabled above, or configured half-size, and
	 * it starts at the PBAR xlat + offset.
	 */

	bar_addr = addr->bar2_addr64 + (b2b_bar == 2 ? ndev->b2b_off : 0);
	iowrite64(bar_addr, mmio + XEON_SBAR23BASE_OFFSET);
	bar_addr = ioread64(mmio + XEON_SBAR23BASE_OFFSET);
	dev_dbg(ndev_dev(ndev), "SBAR23 %#018llx\n", bar_addr);

	if (!ndev->bar4_split) {
		bar_addr = addr->bar4_addr64 +
			(b2b_bar == 4 ? ndev->b2b_off : 0);
		iowrite64(bar_addr, mmio + XEON_SBAR45BASE_OFFSET);
		bar_addr = ioread64(mmio + XEON_SBAR45BASE_OFFSET);
		dev_dbg(ndev_dev(ndev), "SBAR45 %#018llx\n", bar_addr);
	} else {
		bar_addr = addr->bar4_addr32 +
			(b2b_bar == 4 ? ndev->b2b_off : 0);
		iowrite32(bar_addr, mmio + XEON_SBAR4BASE_OFFSET);
		bar_addr = ioread32(mmio + XEON_SBAR4BASE_OFFSET);
		dev_dbg(ndev_dev(ndev), "SBAR4 %#010llx\n", bar_addr);

		bar_addr = addr->bar5_addr32 +
			(b2b_bar == 5 ? ndev->b2b_off : 0);
		iowrite32(bar_addr, mmio + XEON_SBAR5BASE_OFFSET);
		bar_addr = ioread32(mmio + XEON_SBAR5BASE_OFFSET);
		dev_dbg(ndev_dev(ndev), "SBAR5 %#010llx\n", bar_addr);
	}

	/* setup incoming bar limits == base addrs (zero length windows) */

	bar_addr = addr->bar2_addr64 + (b2b_bar == 2 ? ndev->b2b_off : 0);
	iowrite64(bar_addr, mmio + XEON_SBAR23LMT_OFFSET);
	bar_addr = ioread64(mmio + XEON_SBAR23LMT_OFFSET);
	dev_dbg(ndev_dev(ndev), "SBAR23LMT %#018llx\n", bar_addr);

	if (!ndev->bar4_split) {
		bar_addr = addr->bar4_addr64 +
			(b2b_bar == 4 ? ndev->b2b_off : 0);
		iowrite64(bar_addr, mmio + XEON_SBAR45LMT_OFFSET);
		bar_addr = ioread64(mmio + XEON_SBAR45LMT_OFFSET);
		dev_dbg(ndev_dev(ndev), "SBAR45LMT %#018llx\n", bar_addr);
	} else {
		bar_addr = addr->bar4_addr32 +
			(b2b_bar == 4 ? ndev->b2b_off : 0);
		iowrite32(bar_addr, mmio + XEON_SBAR4LMT_OFFSET);
		bar_addr = ioread32(mmio + XEON_SBAR4LMT_OFFSET);
		dev_dbg(ndev_dev(ndev), "SBAR4LMT %#010llx\n", bar_addr);

		bar_addr = addr->bar5_addr32 +
			(b2b_bar == 5 ? ndev->b2b_off : 0);
		iowrite32(bar_addr, mmio + XEON_SBAR5LMT_OFFSET);
		bar_addr = ioread32(mmio + XEON_SBAR5LMT_OFFSET);
		dev_dbg(ndev_dev(ndev), "SBAR5LMT %#05llx\n", bar_addr);
	}

	/* zero incoming translation addrs */
	iowrite64(0, mmio + XEON_SBAR23XLAT_OFFSET);

	if (!ndev->bar4_split) {
		iowrite64(0, mmio + XEON_SBAR45XLAT_OFFSET);
	} else {
		iowrite32(0, mmio + XEON_SBAR4XLAT_OFFSET);
		iowrite32(0, mmio + XEON_SBAR5XLAT_OFFSET);
	}

	/* zero outgoing translation limits (whole bar size windows) */
	iowrite64(0, mmio + XEON_PBAR23LMT_OFFSET);
	if (!ndev->bar4_split) {
		iowrite64(0, mmio + XEON_PBAR45LMT_OFFSET);
	} else {
		iowrite32(0, mmio + XEON_PBAR4LMT_OFFSET);
		iowrite32(0, mmio + XEON_PBAR5LMT_OFFSET);
	}

	/* set outgoing translation offsets */
	bar_addr = peer_addr->bar2_addr64;
	iowrite64(bar_addr, mmio + XEON_PBAR23XLAT_OFFSET);
	bar_addr = ioread64(mmio + XEON_PBAR23XLAT_OFFSET);
	dev_dbg(ndev_dev(ndev), "PBAR23XLAT %#018llx\n", bar_addr);

	if (!ndev->bar4_split) {
		bar_addr = peer_addr->bar4_addr64;
		iowrite64(bar_addr, mmio + XEON_PBAR45XLAT_OFFSET);
		bar_addr = ioread64(mmio + XEON_PBAR45XLAT_OFFSET);
		dev_dbg(ndev_dev(ndev), "PBAR45XLAT %#018llx\n", bar_addr);
	} else {
		bar_addr = peer_addr->bar4_addr32;
		iowrite32(bar_addr, mmio + XEON_PBAR4XLAT_OFFSET);
		bar_addr = ioread32(mmio + XEON_PBAR4XLAT_OFFSET);
		dev_dbg(ndev_dev(ndev), "PBAR4XLAT %#010llx\n", bar_addr);

		bar_addr = peer_addr->bar5_addr32;
		iowrite32(bar_addr, mmio + XEON_PBAR5XLAT_OFFSET);
		bar_addr = ioread32(mmio + XEON_PBAR5XLAT_OFFSET);
		dev_dbg(ndev_dev(ndev), "PBAR5XLAT %#010llx\n", bar_addr);
	}

	/* set the translation offset for b2b registers */
	if (b2b_bar == 0)
		bar_addr = peer_addr->bar0_addr;
	else if (b2b_bar == 2)
		bar_addr = peer_addr->bar2_addr64;
	else if (b2b_bar == 4 && !ndev->bar4_split)
		bar_addr = peer_addr->bar4_addr64;
	else if (b2b_bar == 4)
		bar_addr = peer_addr->bar4_addr32;
	else if (b2b_bar == 5)
		bar_addr = peer_addr->bar5_addr32;
	else
		return -EIO;

	/* B2B_XLAT_OFFSET is 64bit, but can only take 32bit writes */
	dev_dbg(ndev_dev(ndev), "B2BXLAT %#018llx\n", bar_addr);
	iowrite32(bar_addr, mmio + XEON_B2B_XLAT_OFFSETL);
	iowrite32(bar_addr >> 32, mmio + XEON_B2B_XLAT_OFFSETU);

	if (b2b_bar) {
		/* map peer ntb mmio config space registers */
		ndev->peer_mmio = pci_iomap(pdev, b2b_bar,
					    XEON_B2B_MIN_SIZE);
		if (!ndev->peer_mmio)
			return -EIO;

		ndev->peer_addr = pci_resource_start(pdev, b2b_bar);
	}

	return 0;
}

static int xeon_init_ntb(struct intel_ntb_dev *ndev)
{
	int rc;
	u32 ntb_ctl;

	if (ndev->bar4_split)
		ndev->mw_count = HSX_SPLIT_BAR_MW_COUNT;
	else
		ndev->mw_count = XEON_MW_COUNT;

	ndev->spad_count = XEON_SPAD_COUNT;
	ndev->db_count = XEON_DB_COUNT;
	ndev->db_link_mask = XEON_DB_LINK_BIT;

	switch (ndev->ntb.topo) {
	case NTB_TOPO_PRI:
		if (ndev->hwerr_flags & NTB_HWERR_SDOORBELL_LOCKUP) {
			dev_err(ndev_dev(ndev), "NTB Primary config disabled\n");
			return -EINVAL;
		}

		/* enable link to allow secondary side device to appear */
		ntb_ctl = ioread32(ndev->self_mmio + ndev->reg->ntb_ctl);
		ntb_ctl &= ~NTB_CTL_DISABLE;
		iowrite32(ntb_ctl, ndev->self_mmio + ndev->reg->ntb_ctl);

		/* use half the spads for the peer */
		ndev->spad_count >>= 1;
		ndev->self_reg = &xeon_pri_reg;
		ndev->peer_reg = &xeon_sec_reg;
		ndev->xlat_reg = &xeon_sec_xlat;
		break;

	case NTB_TOPO_SEC:
		if (ndev->hwerr_flags & NTB_HWERR_SDOORBELL_LOCKUP) {
			dev_err(ndev_dev(ndev), "NTB Secondary config disabled\n");
			return -EINVAL;
		}
		/* use half the spads for the peer */
		ndev->spad_count >>= 1;
		ndev->self_reg = &xeon_sec_reg;
		ndev->peer_reg = &xeon_pri_reg;
		ndev->xlat_reg = &xeon_pri_xlat;
		break;

	case NTB_TOPO_B2B_USD:
	case NTB_TOPO_B2B_DSD:
		ndev->self_reg = &xeon_pri_reg;
		ndev->peer_reg = &xeon_b2b_reg;
		ndev->xlat_reg = &xeon_sec_xlat;

		if (ndev->hwerr_flags & NTB_HWERR_SDOORBELL_LOCKUP) {
			ndev->peer_reg = &xeon_pri_reg;

			if (b2b_mw_idx < 0)
				ndev->b2b_idx = b2b_mw_idx + ndev->mw_count;
			else
				ndev->b2b_idx = b2b_mw_idx;

			if (ndev->b2b_idx >= ndev->mw_count) {
				dev_dbg(ndev_dev(ndev),
					"b2b_mw_idx %d invalid for mw_count %u\n",
					b2b_mw_idx, ndev->mw_count);
				return -EINVAL;
			}

			dev_dbg(ndev_dev(ndev),
				"setting up b2b mw idx %d means %d\n",
				b2b_mw_idx, ndev->b2b_idx);

		} else if (ndev->hwerr_flags & NTB_HWERR_B2BDOORBELL_BIT14) {
			dev_warn(ndev_dev(ndev), "Reduce doorbell count by 1\n");
			ndev->db_count -= 1;
		}

		if (ndev->ntb.topo == NTB_TOPO_B2B_USD) {
			rc = xeon_setup_b2b_mw(ndev,
					       &xeon_b2b_dsd_addr,
					       &xeon_b2b_usd_addr);
		} else {
			rc = xeon_setup_b2b_mw(ndev,
					       &xeon_b2b_usd_addr,
					       &xeon_b2b_dsd_addr);
		}
		if (rc)
			return rc;

		/* Enable Bus Master and Memory Space on the secondary side */
		iowrite16(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
			  ndev->self_mmio + XEON_SPCICMD_OFFSET);

		break;

	default:
		return -EINVAL;
	}

	ndev->db_valid_mask = BIT_ULL(ndev->db_count) - 1;

	ndev->reg->db_iowrite(ndev->db_valid_mask,
			      ndev->self_mmio +
			      ndev->self_reg->db_mask);

	return 0;
}

static int xeon_init_dev(struct intel_ntb_dev *ndev)
{
	struct pci_dev *pdev;
	u8 ppd;
	int rc, mem;

	pdev = ndev_pdev(ndev);

	switch (pdev->device) {
	/* There is a Xeon hardware errata related to writes to SDOORBELL or
	 * B2BDOORBELL in conjunction with inbound access to NTB MMIO Space,
	 * which may hang the system.  To workaround this use the second memory
	 * window to access the interrupt and scratch pad registers on the
	 * remote system.
	 */
	case PCI_DEVICE_ID_INTEL_NTB_SS_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_PS_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_SS_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_PS_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_SS_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_PS_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_SS_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_PS_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_SS_BDX:
	case PCI_DEVICE_ID_INTEL_NTB_PS_BDX:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_BDX:
		ndev->hwerr_flags |= NTB_HWERR_SDOORBELL_LOCKUP;
		break;
	}

	switch (pdev->device) {
	/* There is a hardware errata related to accessing any register in
	 * SB01BASE in the presence of bidirectional traffic crossing the NTB.
	 */
	case PCI_DEVICE_ID_INTEL_NTB_SS_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_PS_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_SS_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_PS_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_SS_BDX:
	case PCI_DEVICE_ID_INTEL_NTB_PS_BDX:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_BDX:
		ndev->hwerr_flags |= NTB_HWERR_SB01BASE_LOCKUP;
		break;
	}

	switch (pdev->device) {
	/* HW Errata on bit 14 of b2bdoorbell register.  Writes will not be
	 * mirrored to the remote system.  Shrink the number of bits by one,
	 * since bit 14 is the last bit.
	 */
	case PCI_DEVICE_ID_INTEL_NTB_SS_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_PS_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_JSF:
	case PCI_DEVICE_ID_INTEL_NTB_SS_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_PS_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_SNB:
	case PCI_DEVICE_ID_INTEL_NTB_SS_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_PS_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_IVT:
	case PCI_DEVICE_ID_INTEL_NTB_SS_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_PS_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_HSX:
	case PCI_DEVICE_ID_INTEL_NTB_SS_BDX:
	case PCI_DEVICE_ID_INTEL_NTB_PS_BDX:
	case PCI_DEVICE_ID_INTEL_NTB_B2B_BDX:
		ndev->hwerr_flags |= NTB_HWERR_B2BDOORBELL_BIT14;
		break;
	}

	ndev->reg = &xeon_reg;

	rc = pci_read_config_byte(pdev, XEON_PPD_OFFSET, &ppd);
	if (rc)
		return -EIO;

	ndev->ntb.topo = xeon_ppd_topo(ndev, ppd);
	dev_dbg(ndev_dev(ndev), "ppd %#x topo %s\n", ppd,
		ntb_topo_string(ndev->ntb.topo));
	if (ndev->ntb.topo == NTB_TOPO_NONE)
		return -EINVAL;

	if (ndev->ntb.topo != NTB_TOPO_SEC) {
		ndev->bar4_split = xeon_ppd_bar4_split(ndev, ppd);
		dev_dbg(ndev_dev(ndev), "ppd %#x bar4_split %d\n",
			ppd, ndev->bar4_split);
	} else {
		/* This is a way for transparent BAR to figure out if we are
		 * doing split BAR or not. There is no way for the hw on the
		 * transparent side to know and set the PPD.
		 */
		mem = pci_select_bars(pdev, IORESOURCE_MEM);
		ndev->bar4_split = hweight32(mem) ==
			HSX_SPLIT_BAR_MW_COUNT + 1;
		dev_dbg(ndev_dev(ndev), "mem %#x bar4_split %d\n",
			mem, ndev->bar4_split);
	}

	rc = xeon_init_ntb(ndev);
	if (rc)
		return rc;

	return xeon_init_isr(ndev);
}

static void xeon_deinit_dev(struct intel_ntb_dev *ndev)
{
	xeon_deinit_isr(ndev);
}

static int intel_ntb_init_pci(struct intel_ntb_dev *ndev, struct pci_dev *pdev)
{
	int rc;

	pci_set_drvdata(pdev, ndev);

	rc = pci_enable_device(pdev);
	if (rc)
		goto err_pci_enable;

	rc = pci_request_regions(pdev, NTB_NAME);
	if (rc)
		goto err_pci_regions;

	pci_set_master(pdev);

	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc) {
		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc)
			goto err_dma_mask;
		dev_warn(ndev_dev(ndev), "Cannot DMA highmem\n");
	}

	rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc) {
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc)
			goto err_dma_mask;
		dev_warn(ndev_dev(ndev), "Cannot DMA consistent highmem\n");
	}

	ndev->self_mmio = pci_iomap(pdev, 0, 0);
	if (!ndev->self_mmio) {
		rc = -EIO;
		goto err_mmio;
	}
	ndev->peer_mmio = ndev->self_mmio;
	ndev->peer_addr = pci_resource_start(pdev, 0);

	return 0;

err_mmio:
err_dma_mask:
	pci_clear_master(pdev);
	pci_release_regions(pdev);
err_pci_regions:
	pci_disable_device(pdev);
err_pci_enable:
	pci_set_drvdata(pdev, NULL);
	return rc;
}

static void intel_ntb_deinit_pci(struct intel_ntb_dev *ndev)
{
	struct pci_dev *pdev = ndev_pdev(ndev);

	if (ndev->peer_mmio && ndev->peer_mmio != ndev->self_mmio)
		pci_iounmap(pdev, ndev->peer_mmio);
	pci_iounmap(pdev, ndev->self_mmio);

	pci_clear_master(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static inline void ndev_init_struct(struct intel_ntb_dev *ndev,
				    struct pci_dev *pdev)
{
	ndev->ntb.pdev = pdev;
	ndev->ntb.topo = NTB_TOPO_NONE;
	ndev->ntb.ops = &intel_ntb_ops;

	ndev->b2b_off = 0;
	ndev->b2b_idx = UINT_MAX;

	ndev->bar4_split = 0;

	ndev->mw_count = 0;
	ndev->spad_count = 0;
	ndev->db_count = 0;
	ndev->db_vec_count = 0;
	ndev->db_vec_shift = 0;

	ndev->ntb_ctl = 0;
	ndev->lnk_sta = 0;

	ndev->db_valid_mask = 0;
	ndev->db_link_mask = 0;
	ndev->db_mask = 0;

	spin_lock_init(&ndev->db_mask_lock);
}

static int intel_ntb_pci_probe(struct pci_dev *pdev,
			       const struct pci_device_id *id)
{
	struct intel_ntb_dev *ndev;
	int rc, node;

	node = dev_to_node(&pdev->dev);

	if (pdev_is_atom(pdev)) {
		ndev = kzalloc_node(sizeof(*ndev), GFP_KERNEL, node);
		if (!ndev) {
			rc = -ENOMEM;
			goto err_ndev;
		}

		ndev_init_struct(ndev, pdev);

		rc = intel_ntb_init_pci(ndev, pdev);
		if (rc)
			goto err_init_pci;

		rc = atom_init_dev(ndev);
		if (rc)
			goto err_init_dev;

	} else if (pdev_is_xeon(pdev)) {
		ndev = kzalloc_node(sizeof(*ndev), GFP_KERNEL, node);
		if (!ndev) {
			rc = -ENOMEM;
			goto err_ndev;
		}

		ndev_init_struct(ndev, pdev);

		rc = intel_ntb_init_pci(ndev, pdev);
		if (rc)
			goto err_init_pci;

		rc = xeon_init_dev(ndev);
		if (rc)
			goto err_init_dev;

	} else if (pdev_is_skx_xeon(pdev)) {
		ndev = kzalloc_node(sizeof(*ndev), GFP_KERNEL, node);
		if (!ndev) {
			rc = -ENOMEM;
			goto err_ndev;
		}

		ndev_init_struct(ndev, pdev);
		ndev->ntb.ops = &intel_ntb3_ops;

		rc = intel_ntb_init_pci(ndev, pdev);
		if (rc)
			goto err_init_pci;

		rc = skx_init_dev(ndev);
		if (rc)
			goto err_init_dev;

	} else {
		rc = -EINVAL;
		goto err_ndev;
	}

	ndev_reset_unsafe_flags(ndev);

	ndev->reg->poll_link(ndev);

	ndev_init_debugfs(ndev);

	rc = ntb_register_device(&ndev->ntb);
	if (rc)
		goto err_register;

	dev_info(&pdev->dev, "NTB device registered.\n");

	return 0;

err_register:
	ndev_deinit_debugfs(ndev);
	if (pdev_is_atom(pdev))
		atom_deinit_dev(ndev);
	else if (pdev_is_xeon(pdev) || pdev_is_skx_xeon(pdev))
		xeon_deinit_dev(ndev);
err_init_dev:
	intel_ntb_deinit_pci(ndev);
err_init_pci:
	kfree(ndev);
err_ndev:
	return rc;
}

static void intel_ntb_pci_remove(struct pci_dev *pdev)
{
	struct intel_ntb_dev *ndev = pci_get_drvdata(pdev);

	ntb_unregister_device(&ndev->ntb);
	ndev_deinit_debugfs(ndev);
	if (pdev_is_atom(pdev))
		atom_deinit_dev(ndev);
	else if (pdev_is_xeon(pdev) || pdev_is_skx_xeon(pdev))
		xeon_deinit_dev(ndev);
	intel_ntb_deinit_pci(ndev);
	kfree(ndev);
}

static const struct intel_ntb_reg atom_reg = {
	.poll_link		= atom_poll_link,
	.link_is_up		= atom_link_is_up,
	.db_ioread		= atom_db_ioread,
	.db_iowrite		= atom_db_iowrite,
	.db_size		= sizeof(u64),
	.ntb_ctl		= ATOM_NTBCNTL_OFFSET,
	.mw_bar			= {2, 4},
};

static const struct intel_ntb_alt_reg atom_pri_reg = {
	.db_bell		= ATOM_PDOORBELL_OFFSET,
	.db_mask		= ATOM_PDBMSK_OFFSET,
	.spad			= ATOM_SPAD_OFFSET,
};

static const struct intel_ntb_alt_reg atom_b2b_reg = {
	.db_bell		= ATOM_B2B_DOORBELL_OFFSET,
	.spad			= ATOM_B2B_SPAD_OFFSET,
};

static const struct intel_ntb_xlat_reg atom_sec_xlat = {
	/* FIXME : .bar0_base	= ATOM_SBAR0BASE_OFFSET, */
	/* FIXME : .bar2_limit	= ATOM_SBAR2LMT_OFFSET, */
	.bar2_xlat		= ATOM_SBAR2XLAT_OFFSET,
};

static const struct intel_ntb_reg xeon_reg = {
	.poll_link		= xeon_poll_link,
	.link_is_up		= xeon_link_is_up,
	.db_ioread		= xeon_db_ioread,
	.db_iowrite		= xeon_db_iowrite,
	.db_size		= sizeof(u32),
	.ntb_ctl		= XEON_NTBCNTL_OFFSET,
	.mw_bar			= {2, 4, 5},
};

static const struct intel_ntb_alt_reg xeon_pri_reg = {
	.db_bell		= XEON_PDOORBELL_OFFSET,
	.db_mask		= XEON_PDBMSK_OFFSET,
	.spad			= XEON_SPAD_OFFSET,
};

static const struct intel_ntb_alt_reg xeon_sec_reg = {
	.db_bell		= XEON_SDOORBELL_OFFSET,
	.db_mask		= XEON_SDBMSK_OFFSET,
	/* second half of the scratchpads */
	.spad			= XEON_SPAD_OFFSET + (XEON_SPAD_COUNT << 1),
};

static const struct intel_ntb_alt_reg xeon_b2b_reg = {
	.db_bell		= XEON_B2B_DOORBELL_OFFSET,
	.spad			= XEON_B2B_SPAD_OFFSET,
};

static const struct intel_ntb_xlat_reg xeon_pri_xlat = {
	/* Note: no primary .bar0_base visible to the secondary side.
	 *
	 * The secondary side cannot get the base address stored in primary
	 * bars.  The base address is necessary to set the limit register to
	 * any value other than zero, or unlimited.
	 *
	 * WITHOUT THE BASE ADDRESS, THE SECONDARY SIDE CANNOT DISABLE the
	 * window by setting the limit equal to base, nor can it limit the size
	 * of the memory window by setting the limit to base + size.
	 */
	.bar2_limit		= XEON_PBAR23LMT_OFFSET,
	.bar2_xlat		= XEON_PBAR23XLAT_OFFSET,
};

static const struct intel_ntb_xlat_reg xeon_sec_xlat = {
	.bar0_base		= XEON_SBAR0BASE_OFFSET,
	.bar2_limit		= XEON_SBAR23LMT_OFFSET,
	.bar2_xlat		= XEON_SBAR23XLAT_OFFSET,
};

static struct intel_b2b_addr xeon_b2b_usd_addr = {
	.bar2_addr64		= XEON_B2B_BAR2_ADDR64,
	.bar4_addr64		= XEON_B2B_BAR4_ADDR64,
	.bar4_addr32		= XEON_B2B_BAR4_ADDR32,
	.bar5_addr32		= XEON_B2B_BAR5_ADDR32,
};

static struct intel_b2b_addr xeon_b2b_dsd_addr = {
	.bar2_addr64		= XEON_B2B_BAR2_ADDR64,
	.bar4_addr64		= XEON_B2B_BAR4_ADDR64,
	.bar4_addr32		= XEON_B2B_BAR4_ADDR32,
	.bar5_addr32		= XEON_B2B_BAR5_ADDR32,
};

static const struct intel_ntb_reg skx_reg = {
	.poll_link		= skx_poll_link,
	.link_is_up		= xeon_link_is_up,
	.db_ioread		= skx_db_ioread,
	.db_iowrite		= skx_db_iowrite,
	.db_size		= sizeof(u64),
	.ntb_ctl		= SKX_NTBCNTL_OFFSET,
	.mw_bar			= {2, 4},
};

static const struct intel_ntb_alt_reg skx_pri_reg = {
	.db_bell		= SKX_EM_DOORBELL_OFFSET,
	.db_clear		= SKX_IM_INT_STATUS_OFFSET,
	.db_mask		= SKX_IM_INT_DISABLE_OFFSET,
	.spad			= SKX_IM_SPAD_OFFSET,
};

static const struct intel_ntb_alt_reg skx_b2b_reg = {
	.db_bell		= SKX_IM_DOORBELL_OFFSET,
	.db_clear		= SKX_EM_INT_STATUS_OFFSET,
	.db_mask		= SKX_EM_INT_DISABLE_OFFSET,
	.spad			= SKX_B2B_SPAD_OFFSET,
};

static const struct intel_ntb_xlat_reg skx_sec_xlat = {
/*	.bar0_base		= SKX_EMBAR0_OFFSET, */
	.bar2_limit		= SKX_IMBAR1XLMT_OFFSET,
	.bar2_xlat		= SKX_IMBAR1XBASE_OFFSET,
};

/* operations for primary side of local ntb */
static const struct ntb_dev_ops intel_ntb_ops = {
	.mw_count		= intel_ntb_mw_count,
	.mw_get_range		= intel_ntb_mw_get_range,
	.mw_set_trans		= intel_ntb_mw_set_trans,
	.link_is_up		= intel_ntb_link_is_up,
	.link_enable		= intel_ntb_link_enable,
	.link_disable		= intel_ntb_link_disable,
	.db_is_unsafe		= intel_ntb_db_is_unsafe,
	.db_valid_mask		= intel_ntb_db_valid_mask,
	.db_vector_count	= intel_ntb_db_vector_count,
	.db_vector_mask		= intel_ntb_db_vector_mask,
	.db_read		= intel_ntb_db_read,
	.db_clear		= intel_ntb_db_clear,
	.db_set_mask		= intel_ntb_db_set_mask,
	.db_clear_mask		= intel_ntb_db_clear_mask,
	.peer_db_addr		= intel_ntb_peer_db_addr,
	.peer_db_set		= intel_ntb_peer_db_set,
	.spad_is_unsafe		= intel_ntb_spad_is_unsafe,
	.spad_count		= intel_ntb_spad_count,
	.spad_read		= intel_ntb_spad_read,
	.spad_write		= intel_ntb_spad_write,
	.peer_spad_addr		= intel_ntb_peer_spad_addr,
	.peer_spad_read		= intel_ntb_peer_spad_read,
	.peer_spad_write	= intel_ntb_peer_spad_write,
};

static const struct ntb_dev_ops intel_ntb3_ops = {
	.mw_count		= intel_ntb_mw_count,
	.mw_get_range		= intel_ntb_mw_get_range,
	.mw_set_trans		= intel_ntb3_mw_set_trans,
	.link_is_up		= intel_ntb_link_is_up,
	.link_enable		= intel_ntb3_link_enable,
	.link_disable		= intel_ntb_link_disable,
	.db_valid_mask		= intel_ntb_db_valid_mask,
	.db_vector_count	= intel_ntb_db_vector_count,
	.db_vector_mask		= intel_ntb_db_vector_mask,
	.db_read		= intel_ntb3_db_read,
	.db_clear		= intel_ntb3_db_clear,
	.db_set_mask		= intel_ntb_db_set_mask,
	.db_clear_mask		= intel_ntb_db_clear_mask,
	.peer_db_addr		= intel_ntb_peer_db_addr,
	.peer_db_set		= intel_ntb3_peer_db_set,
	.spad_is_unsafe		= intel_ntb_spad_is_unsafe,
	.spad_count		= intel_ntb_spad_count,
	.spad_read		= intel_ntb_spad_read,
	.spad_write		= intel_ntb_spad_write,
	.peer_spad_addr		= intel_ntb_peer_spad_addr,
	.peer_spad_read		= intel_ntb_peer_spad_read,
	.peer_spad_write	= intel_ntb_peer_spad_write,
};

static const struct file_operations intel_ntb_debugfs_info = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = ndev_debugfs_read,
};

static const struct pci_device_id intel_ntb_pci_tbl[] = {
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_B2B_BWD)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_B2B_JSF)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_B2B_SNB)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_B2B_IVT)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_B2B_HSX)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_B2B_BDX)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_PS_JSF)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_PS_SNB)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_PS_IVT)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_PS_HSX)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_PS_BDX)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_SS_JSF)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_SS_SNB)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_SS_IVT)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_SS_HSX)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_SS_BDX)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_NTB_B2B_SKX)},
	{0}
};
MODULE_DEVICE_TABLE(pci, intel_ntb_pci_tbl);

static struct pci_driver intel_ntb_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = intel_ntb_pci_tbl,
	.probe = intel_ntb_pci_probe,
	.remove = intel_ntb_pci_remove,
};

static int __init intel_ntb_pci_driver_init(void)
{
	pr_info("%s %s\n", NTB_DESC, NTB_VER);

	if (debugfs_initialized())
		debugfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	return pci_register_driver(&intel_ntb_pci_driver);
}
module_init(intel_ntb_pci_driver_init);

static void __exit intel_ntb_pci_driver_exit(void)
{
	pci_unregister_driver(&intel_ntb_pci_driver);

	debugfs_remove_recursive(debugfs_dir);
}
module_exit(intel_ntb_pci_driver_exit);

