/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright (C) 2016 Advanced Micro Devices, Inc. All Rights Reserved.
 *   Copyright (C) 2016 T-Platforms. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   BSD LICENSE
 *
 *   Copyright (C) 2016 Advanced Micro Devices, Inc. All Rights Reserved.
 *   Copyright (C) 2016 T-Platforms. All Rights Reserved.
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
 *     * Neither the name of AMD Corporation nor the names of its
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
 * AMD PCIe NTB Linux driver
 *
 * Contact Information:
 * Xiangliang Yu <Xiangliang.Yu@amd.com>
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/ntb.h>

#include "ntb_hw_amd.h"

#define NTB_NAME	"ntb_hw_amd"
#define NTB_DESC	"AMD(R) PCI-E Non-Transparent Bridge Driver"
#define NTB_VER		"1.0"

MODULE_DESCRIPTION(NTB_DESC);
MODULE_VERSION(NTB_VER);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("AMD Inc.");

static const struct file_operations amd_ntb_debugfs_info;
static struct dentry *debugfs_dir;

static int ndev_mw_to_bar(struct amd_ntb_dev *ndev, int idx)
{
	if (idx < 0 || idx > ndev->mw_count)
		return -EINVAL;

	return ndev->dev_data->mw_idx << idx;
}

static int amd_ntb_mw_count(struct ntb_dev *ntb, int pidx)
{
	if (pidx != NTB_DEF_PEER_IDX)
		return -EINVAL;

	return ntb_ndev(ntb)->mw_count;
}

static int amd_ntb_mw_get_align(struct ntb_dev *ntb, int pidx, int idx,
				resource_size_t *addr_align,
				resource_size_t *size_align,
				resource_size_t *size_max)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	int bar;

	if (pidx != NTB_DEF_PEER_IDX)
		return -EINVAL;

	bar = ndev_mw_to_bar(ndev, idx);
	if (bar < 0)
		return bar;

	if (addr_align)
		*addr_align = SZ_4K;

	if (size_align)
		*size_align = 1;

	if (size_max)
		*size_max = pci_resource_len(ndev->ntb.pdev, bar);

	return 0;
}

static int amd_ntb_mw_set_trans(struct ntb_dev *ntb, int pidx, int idx,
				dma_addr_t addr, resource_size_t size)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	unsigned long xlat_reg, limit_reg = 0;
	resource_size_t mw_size;
	void __iomem *mmio, *peer_mmio;
	u64 base_addr, limit, reg_val;
	int bar;

	if (pidx != NTB_DEF_PEER_IDX)
		return -EINVAL;

	bar = ndev_mw_to_bar(ndev, idx);
	if (bar < 0)
		return bar;

	mw_size = pci_resource_len(ntb->pdev, bar);

	/* make sure the range fits in the usable mw size */
	if (size > mw_size)
		return -EINVAL;

	mmio = ndev->self_mmio;
	peer_mmio = ndev->peer_mmio;

	base_addr = pci_resource_start(ntb->pdev, bar);

	if (bar != 1) {
		xlat_reg = AMD_BAR23XLAT_OFFSET + ((bar - 2) << 2);
		limit_reg = AMD_BAR23LMT_OFFSET + ((bar - 2) << 2);

		/* Set the limit if supported */
		limit = size;

		/* set and verify setting the translation address */
		write64(addr, peer_mmio + xlat_reg);
		reg_val = read64(peer_mmio + xlat_reg);
		if (reg_val != addr) {
			write64(0, peer_mmio + xlat_reg);
			return -EIO;
		}

		/* set and verify setting the limit */
		write64(limit, peer_mmio + limit_reg);
		reg_val = read64(peer_mmio + limit_reg);
		if (reg_val != limit) {
			write64(base_addr, mmio + limit_reg);
			write64(0, peer_mmio + xlat_reg);
			return -EIO;
		}
	} else {
		xlat_reg = AMD_BAR1XLAT_OFFSET;
		limit_reg = AMD_BAR1LMT_OFFSET;

		/* Set the limit if supported */
		limit = size;

		/* set and verify setting the translation address */
		write64(addr, peer_mmio + xlat_reg);
		reg_val = read64(peer_mmio + xlat_reg);
		if (reg_val != addr) {
			write64(0, peer_mmio + xlat_reg);
			return -EIO;
		}

		/* set and verify setting the limit */
		writel(limit, peer_mmio + limit_reg);
		reg_val = readl(peer_mmio + limit_reg);
		if (reg_val != limit) {
			writel(base_addr, mmio + limit_reg);
			writel(0, peer_mmio + xlat_reg);
			return -EIO;
		}
	}

	return 0;
}

static int amd_link_is_up(struct amd_ntb_dev *ndev)
{
	if (!ndev->peer_sta)
		return NTB_LNK_STA_ACTIVE(ndev->cntl_sta);

	if (ndev->peer_sta & AMD_LINK_UP_EVENT) {
		ndev->peer_sta = 0;
		return 1;
	}

	/* If peer_sta is reset or D0 event, the ISR has
	 * started a timer to check link status of hardware.
	 * So here just clear status bit. And if peer_sta is
	 * D3 or PME_TO, D0/reset event will be happened when
	 * system wakeup/poweron, so do nothing here.
	 */
	if (ndev->peer_sta & AMD_PEER_RESET_EVENT)
		ndev->peer_sta &= ~AMD_PEER_RESET_EVENT;
	else if (ndev->peer_sta & (AMD_PEER_D0_EVENT | AMD_LINK_DOWN_EVENT))
		ndev->peer_sta = 0;

	return 0;
}

static u64 amd_ntb_link_is_up(struct ntb_dev *ntb,
			      enum ntb_speed *speed,
			      enum ntb_width *width)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	int ret = 0;

	if (amd_link_is_up(ndev)) {
		if (speed)
			*speed = NTB_LNK_STA_SPEED(ndev->lnk_sta);
		if (width)
			*width = NTB_LNK_STA_WIDTH(ndev->lnk_sta);

		dev_dbg(&ntb->pdev->dev, "link is up.\n");

		ret = 1;
	} else {
		if (speed)
			*speed = NTB_SPEED_NONE;
		if (width)
			*width = NTB_WIDTH_NONE;

		dev_dbg(&ntb->pdev->dev, "link is down.\n");
	}

	return ret;
}

static int amd_ntb_link_enable(struct ntb_dev *ntb,
			       enum ntb_speed max_speed,
			       enum ntb_width max_width)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	void __iomem *mmio = ndev->self_mmio;
	u32 ntb_ctl;

	/* Enable event interrupt */
	ndev->int_mask &= ~AMD_EVENT_INTMASK;
	writel(ndev->int_mask, mmio + AMD_INTMASK_OFFSET);

	if (ndev->ntb.topo == NTB_TOPO_SEC)
		return -EINVAL;
	dev_dbg(&ntb->pdev->dev, "Enabling Link.\n");

	ntb_ctl = readl(mmio + AMD_CNTL_OFFSET);
	ntb_ctl |= (PMM_REG_CTL | SMM_REG_CTL);
	writel(ntb_ctl, mmio + AMD_CNTL_OFFSET);

	return 0;
}

static int amd_ntb_link_disable(struct ntb_dev *ntb)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	void __iomem *mmio = ndev->self_mmio;
	u32 ntb_ctl;

	/* Disable event interrupt */
	ndev->int_mask |= AMD_EVENT_INTMASK;
	writel(ndev->int_mask, mmio + AMD_INTMASK_OFFSET);

	if (ndev->ntb.topo == NTB_TOPO_SEC)
		return -EINVAL;
	dev_dbg(&ntb->pdev->dev, "Enabling Link.\n");

	ntb_ctl = readl(mmio + AMD_CNTL_OFFSET);
	ntb_ctl &= ~(PMM_REG_CTL | SMM_REG_CTL);
	writel(ntb_ctl, mmio + AMD_CNTL_OFFSET);

	return 0;
}

static int amd_ntb_peer_mw_count(struct ntb_dev *ntb)
{
	/* The same as for inbound MWs */
	return ntb_ndev(ntb)->mw_count;
}

static int amd_ntb_peer_mw_get_addr(struct ntb_dev *ntb, int idx,
				    phys_addr_t *base, resource_size_t *size)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	int bar;

	bar = ndev_mw_to_bar(ndev, idx);
	if (bar < 0)
		return bar;

	if (base)
		*base = pci_resource_start(ndev->ntb.pdev, bar);

	if (size)
		*size = pci_resource_len(ndev->ntb.pdev, bar);

	return 0;
}

static u64 amd_ntb_db_valid_mask(struct ntb_dev *ntb)
{
	return ntb_ndev(ntb)->db_valid_mask;
}

static int amd_ntb_db_vector_count(struct ntb_dev *ntb)
{
	return ntb_ndev(ntb)->db_count;
}

static u64 amd_ntb_db_vector_mask(struct ntb_dev *ntb, int db_vector)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);

	if (db_vector < 0 || db_vector > ndev->db_count)
		return 0;

	return ntb_ndev(ntb)->db_valid_mask & (1ULL << db_vector);
}

static u64 amd_ntb_db_read(struct ntb_dev *ntb)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	void __iomem *mmio = ndev->self_mmio;

	return (u64)readw(mmio + AMD_DBSTAT_OFFSET);
}

static int amd_ntb_db_clear(struct ntb_dev *ntb, u64 db_bits)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	void __iomem *mmio = ndev->self_mmio;

	writew((u16)db_bits, mmio + AMD_DBSTAT_OFFSET);

	return 0;
}

static int amd_ntb_db_set_mask(struct ntb_dev *ntb, u64 db_bits)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	void __iomem *mmio = ndev->self_mmio;
	unsigned long flags;

	if (db_bits & ~ndev->db_valid_mask)
		return -EINVAL;

	spin_lock_irqsave(&ndev->db_mask_lock, flags);
	ndev->db_mask |= db_bits;
	writew((u16)ndev->db_mask, mmio + AMD_DBMASK_OFFSET);
	spin_unlock_irqrestore(&ndev->db_mask_lock, flags);

	return 0;
}

static int amd_ntb_db_clear_mask(struct ntb_dev *ntb, u64 db_bits)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	void __iomem *mmio = ndev->self_mmio;
	unsigned long flags;

	if (db_bits & ~ndev->db_valid_mask)
		return -EINVAL;

	spin_lock_irqsave(&ndev->db_mask_lock, flags);
	ndev->db_mask &= ~db_bits;
	writew((u16)ndev->db_mask, mmio + AMD_DBMASK_OFFSET);
	spin_unlock_irqrestore(&ndev->db_mask_lock, flags);

	return 0;
}

static int amd_ntb_peer_db_set(struct ntb_dev *ntb, u64 db_bits)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	void __iomem *mmio = ndev->self_mmio;

	writew((u16)db_bits, mmio + AMD_DBREQ_OFFSET);

	return 0;
}

static int amd_ntb_spad_count(struct ntb_dev *ntb)
{
	return ntb_ndev(ntb)->spad_count;
}

static u32 amd_ntb_spad_read(struct ntb_dev *ntb, int idx)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	void __iomem *mmio = ndev->self_mmio;
	u32 offset;

	if (idx < 0 || idx >= ndev->spad_count)
		return 0;

	offset = ndev->self_spad + (idx << 2);
	return readl(mmio + AMD_SPAD_OFFSET + offset);
}

static int amd_ntb_spad_write(struct ntb_dev *ntb,
			      int idx, u32 val)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	void __iomem *mmio = ndev->self_mmio;
	u32 offset;

	if (idx < 0 || idx >= ndev->spad_count)
		return -EINVAL;

	offset = ndev->self_spad + (idx << 2);
	writel(val, mmio + AMD_SPAD_OFFSET + offset);

	return 0;
}

static u32 amd_ntb_peer_spad_read(struct ntb_dev *ntb, int pidx, int sidx)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	void __iomem *mmio = ndev->self_mmio;
	u32 offset;

	if (sidx < 0 || sidx >= ndev->spad_count)
		return -EINVAL;

	offset = ndev->peer_spad + (sidx << 2);
	return readl(mmio + AMD_SPAD_OFFSET + offset);
}

static int amd_ntb_peer_spad_write(struct ntb_dev *ntb, int pidx,
				   int sidx, u32 val)
{
	struct amd_ntb_dev *ndev = ntb_ndev(ntb);
	void __iomem *mmio = ndev->self_mmio;
	u32 offset;

	if (sidx < 0 || sidx >= ndev->spad_count)
		return -EINVAL;

	offset = ndev->peer_spad + (sidx << 2);
	writel(val, mmio + AMD_SPAD_OFFSET + offset);

	return 0;
}

static const struct ntb_dev_ops amd_ntb_ops = {
	.mw_count		= amd_ntb_mw_count,
	.mw_get_align		= amd_ntb_mw_get_align,
	.mw_set_trans		= amd_ntb_mw_set_trans,
	.peer_mw_count		= amd_ntb_peer_mw_count,
	.peer_mw_get_addr	= amd_ntb_peer_mw_get_addr,
	.link_is_up		= amd_ntb_link_is_up,
	.link_enable		= amd_ntb_link_enable,
	.link_disable		= amd_ntb_link_disable,
	.db_valid_mask		= amd_ntb_db_valid_mask,
	.db_vector_count	= amd_ntb_db_vector_count,
	.db_vector_mask		= amd_ntb_db_vector_mask,
	.db_read		= amd_ntb_db_read,
	.db_clear		= amd_ntb_db_clear,
	.db_set_mask		= amd_ntb_db_set_mask,
	.db_clear_mask		= amd_ntb_db_clear_mask,
	.peer_db_set		= amd_ntb_peer_db_set,
	.spad_count		= amd_ntb_spad_count,
	.spad_read		= amd_ntb_spad_read,
	.spad_write		= amd_ntb_spad_write,
	.peer_spad_read		= amd_ntb_peer_spad_read,
	.peer_spad_write	= amd_ntb_peer_spad_write,
};

static void amd_ack_smu(struct amd_ntb_dev *ndev, u32 bit)
{
	void __iomem *mmio = ndev->self_mmio;
	int reg;

	reg = readl(mmio + AMD_SMUACK_OFFSET);
	reg |= bit;
	writel(reg, mmio + AMD_SMUACK_OFFSET);

	ndev->peer_sta |= bit;
}

static void amd_handle_event(struct amd_ntb_dev *ndev, int vec)
{
	void __iomem *mmio = ndev->self_mmio;
	struct device *dev = &ndev->ntb.pdev->dev;
	u32 status;

	status = readl(mmio + AMD_INTSTAT_OFFSET);
	if (!(status & AMD_EVENT_INTMASK))
		return;

	dev_dbg(dev, "status = 0x%x and vec = %d\n", status, vec);

	status &= AMD_EVENT_INTMASK;
	switch (status) {
	case AMD_PEER_FLUSH_EVENT:
		dev_info(dev, "Flush is done.\n");
		break;
	case AMD_PEER_RESET_EVENT:
		amd_ack_smu(ndev, AMD_PEER_RESET_EVENT);

		/* link down first */
		ntb_link_event(&ndev->ntb);
		/* polling peer status */
		schedule_delayed_work(&ndev->hb_timer, AMD_LINK_HB_TIMEOUT);

		break;
	case AMD_PEER_D3_EVENT:
	case AMD_PEER_PMETO_EVENT:
	case AMD_LINK_UP_EVENT:
	case AMD_LINK_DOWN_EVENT:
		amd_ack_smu(ndev, status);

		/* link down */
		ntb_link_event(&ndev->ntb);

		break;
	case AMD_PEER_D0_EVENT:
		mmio = ndev->peer_mmio;
		status = readl(mmio + AMD_PMESTAT_OFFSET);
		/* check if this is WAKEUP event */
		if (status & 0x1)
			dev_info(dev, "Wakeup is done.\n");

		amd_ack_smu(ndev, AMD_PEER_D0_EVENT);

		/* start a timer to poll link status */
		schedule_delayed_work(&ndev->hb_timer,
				      AMD_LINK_HB_TIMEOUT);
		break;
	default:
		dev_info(dev, "event status = 0x%x.\n", status);
		break;
	}
}

static irqreturn_t ndev_interrupt(struct amd_ntb_dev *ndev, int vec)
{
	dev_dbg(&ndev->ntb.pdev->dev, "vec %d\n", vec);

	if (vec > (AMD_DB_CNT - 1) || (ndev->msix_vec_count == 1))
		amd_handle_event(ndev, vec);

	if (vec < AMD_DB_CNT)
		ntb_db_event(&ndev->ntb, vec);

	return IRQ_HANDLED;
}

static irqreturn_t ndev_vec_isr(int irq, void *dev)
{
	struct amd_ntb_vec *nvec = dev;

	return ndev_interrupt(nvec->ndev, nvec->num);
}

static irqreturn_t ndev_irq_isr(int irq, void *dev)
{
	struct amd_ntb_dev *ndev = dev;

	return ndev_interrupt(ndev, irq - ndev->ntb.pdev->irq);
}

static int ndev_init_isr(struct amd_ntb_dev *ndev,
			 int msix_min, int msix_max)
{
	struct pci_dev *pdev;
	int rc, i, msix_count, node;

	pdev = ndev->ntb.pdev;

	node = dev_to_node(&pdev->dev);

	ndev->db_mask = ndev->db_valid_mask;

	/* Try to set up msix irq */
	ndev->vec = kcalloc_node(msix_max, sizeof(*ndev->vec),
				 GFP_KERNEL, node);
	if (!ndev->vec)
		goto err_msix_vec_alloc;

	ndev->msix = kcalloc_node(msix_max, sizeof(*ndev->msix),
				  GFP_KERNEL, node);
	if (!ndev->msix)
		goto err_msix_alloc;

	for (i = 0; i < msix_max; ++i)
		ndev->msix[i].entry = i;

	msix_count = pci_enable_msix_range(pdev, ndev->msix,
					   msix_min, msix_max);
	if (msix_count < 0)
		goto err_msix_enable;

	/* NOTE: Disable MSIX if msix count is less than 16 because of
	 * hardware limitation.
	 */
	if (msix_count < msix_min) {
		pci_disable_msix(pdev);
		goto err_msix_enable;
	}

	for (i = 0; i < msix_count; ++i) {
		ndev->vec[i].ndev = ndev;
		ndev->vec[i].num = i;
		rc = request_irq(ndev->msix[i].vector, ndev_vec_isr, 0,
				 "ndev_vec_isr", &ndev->vec[i]);
		if (rc)
			goto err_msix_request;
	}

	dev_dbg(&pdev->dev, "Using msix interrupts\n");
	ndev->db_count = msix_min;
	ndev->msix_vec_count = msix_max;
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

	dev_dbg(&pdev->dev, "Using msi interrupts\n");
	ndev->db_count = 1;
	ndev->msix_vec_count = 1;
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

	dev_dbg(&pdev->dev, "Using intx interrupts\n");
	ndev->db_count = 1;
	ndev->msix_vec_count = 1;
	return 0;

err_intx_request:
	return rc;
}

static void ndev_deinit_isr(struct amd_ntb_dev *ndev)
{
	struct pci_dev *pdev;
	void __iomem *mmio = ndev->self_mmio;
	int i;

	pdev = ndev->ntb.pdev;

	/* Mask all doorbell interrupts */
	ndev->db_mask = ndev->db_valid_mask;
	writel(ndev->db_mask, mmio + AMD_DBMASK_OFFSET);

	if (ndev->msix) {
		i = ndev->msix_vec_count;
		while (i--)
			free_irq(ndev->msix[i].vector, &ndev->vec[i]);
		pci_disable_msix(pdev);
		kfree(ndev->msix);
		kfree(ndev->vec);
	} else {
		free_irq(pdev->irq, ndev);
		if (pci_dev_msi_enabled(pdev))
			pci_disable_msi(pdev);
		else
			pci_intx(pdev, 0);
	}
}

static ssize_t ndev_debugfs_read(struct file *filp, char __user *ubuf,
				 size_t count, loff_t *offp)
{
	struct amd_ntb_dev *ndev;
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
			 "LNK STA -\t\t%#06x\n", ndev->lnk_sta);

	if (!amd_link_is_up(ndev)) {
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
			 "MSIX Vector Count -\t%u\n", ndev->msix_vec_count);

	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Valid Mask -\t%#llx\n", ndev->db_valid_mask);

	u.v32 = readl(ndev->self_mmio + AMD_DBMASK_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Mask -\t\t\t%#06x\n", u.v32);

	u.v32 = readl(mmio + AMD_DBSTAT_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "Doorbell Bell -\t\t\t%#06x\n", u.v32);

	off += scnprintf(buf + off, buf_size - off,
			 "\nNTB Incoming XLAT:\n");

	u.v64 = read64(mmio + AMD_BAR1XLAT_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "XLAT1 -\t\t%#018llx\n", u.v64);

	u.v64 = read64(ndev->self_mmio + AMD_BAR23XLAT_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "XLAT23 -\t\t%#018llx\n", u.v64);

	u.v64 = read64(ndev->self_mmio + AMD_BAR45XLAT_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "XLAT45 -\t\t%#018llx\n", u.v64);

	u.v32 = readl(mmio + AMD_BAR1LMT_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "LMT1 -\t\t\t%#06x\n", u.v32);

	u.v64 = read64(ndev->self_mmio + AMD_BAR23LMT_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "LMT23 -\t\t\t%#018llx\n", u.v64);

	u.v64 = read64(ndev->self_mmio + AMD_BAR45LMT_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "LMT45 -\t\t\t%#018llx\n", u.v64);

	ret = simple_read_from_buffer(ubuf, count, offp, buf, off);
	kfree(buf);
	return ret;
}

static void ndev_init_debugfs(struct amd_ntb_dev *ndev)
{
	if (!debugfs_dir) {
		ndev->debugfs_dir = NULL;
		ndev->debugfs_info = NULL;
	} else {
		ndev->debugfs_dir =
			debugfs_create_dir(pci_name(ndev->ntb.pdev),
					   debugfs_dir);
		if (!ndev->debugfs_dir)
			ndev->debugfs_info = NULL;
		else
			ndev->debugfs_info =
				debugfs_create_file("info", S_IRUSR,
						    ndev->debugfs_dir, ndev,
						    &amd_ntb_debugfs_info);
	}
}

static void ndev_deinit_debugfs(struct amd_ntb_dev *ndev)
{
	debugfs_remove_recursive(ndev->debugfs_dir);
}

static inline void ndev_init_struct(struct amd_ntb_dev *ndev,
				    struct pci_dev *pdev)
{
	ndev->ntb.pdev = pdev;
	ndev->ntb.topo = NTB_TOPO_NONE;
	ndev->ntb.ops = &amd_ntb_ops;
	ndev->int_mask = AMD_EVENT_INTMASK;
	spin_lock_init(&ndev->db_mask_lock);
}

static int amd_poll_link(struct amd_ntb_dev *ndev)
{
	void __iomem *mmio = ndev->peer_mmio;
	u32 reg, stat;
	int rc;

	reg = readl(mmio + AMD_SIDEINFO_OFFSET);
	reg &= NTB_LIN_STA_ACTIVE_BIT;

	dev_dbg(&ndev->ntb.pdev->dev, "%s: reg_val = 0x%x.\n", __func__, reg);

	if (reg == ndev->cntl_sta)
		return 0;

	ndev->cntl_sta = reg;

	rc = pci_read_config_dword(ndev->ntb.pdev,
				   AMD_LINK_STATUS_OFFSET, &stat);
	if (rc)
		return 0;
	ndev->lnk_sta = stat;

	return 1;
}

static void amd_link_hb(struct work_struct *work)
{
	struct amd_ntb_dev *ndev = hb_ndev(work);

	if (amd_poll_link(ndev))
		ntb_link_event(&ndev->ntb);

	if (!amd_link_is_up(ndev))
		schedule_delayed_work(&ndev->hb_timer, AMD_LINK_HB_TIMEOUT);
}

static int amd_init_isr(struct amd_ntb_dev *ndev)
{
	return ndev_init_isr(ndev, AMD_DB_CNT, AMD_MSIX_VECTOR_CNT);
}

static void amd_init_side_info(struct amd_ntb_dev *ndev)
{
	void __iomem *mmio = ndev->self_mmio;
	unsigned int reg;

	reg = readl(mmio + AMD_SIDEINFO_OFFSET);
	if (!(reg & AMD_SIDE_READY)) {
		reg |= AMD_SIDE_READY;
		writel(reg, mmio + AMD_SIDEINFO_OFFSET);
	}
}

static void amd_deinit_side_info(struct amd_ntb_dev *ndev)
{
	void __iomem *mmio = ndev->self_mmio;
	unsigned int reg;

	reg = readl(mmio + AMD_SIDEINFO_OFFSET);
	if (reg & AMD_SIDE_READY) {
		reg &= ~AMD_SIDE_READY;
		writel(reg, mmio + AMD_SIDEINFO_OFFSET);
		readl(mmio + AMD_SIDEINFO_OFFSET);
	}
}

static int amd_init_ntb(struct amd_ntb_dev *ndev)
{
	void __iomem *mmio = ndev->self_mmio;

	ndev->mw_count = ndev->dev_data->mw_count;
	ndev->spad_count = AMD_SPADS_CNT;
	ndev->db_count = AMD_DB_CNT;

	switch (ndev->ntb.topo) {
	case NTB_TOPO_PRI:
	case NTB_TOPO_SEC:
		ndev->spad_count >>= 1;
		if (ndev->ntb.topo == NTB_TOPO_PRI) {
			ndev->self_spad = 0;
			ndev->peer_spad = 0x20;
		} else {
			ndev->self_spad = 0x20;
			ndev->peer_spad = 0;
		}

		INIT_DELAYED_WORK(&ndev->hb_timer, amd_link_hb);
		schedule_delayed_work(&ndev->hb_timer, AMD_LINK_HB_TIMEOUT);

		break;
	default:
		dev_err(&ndev->ntb.pdev->dev,
			"AMD NTB does not support B2B mode.\n");
		return -EINVAL;
	}

	ndev->db_valid_mask = BIT_ULL(ndev->db_count) - 1;

	/* Mask event interrupts */
	writel(ndev->int_mask, mmio + AMD_INTMASK_OFFSET);

	return 0;
}

static enum ntb_topo amd_get_topo(struct amd_ntb_dev *ndev)
{
	void __iomem *mmio = ndev->self_mmio;
	u32 info;

	info = readl(mmio + AMD_SIDEINFO_OFFSET);
	if (info & AMD_SIDE_MASK)
		return NTB_TOPO_SEC;
	else
		return NTB_TOPO_PRI;
}

static int amd_init_dev(struct amd_ntb_dev *ndev)
{
	struct pci_dev *pdev;
	int rc = 0;

	pdev = ndev->ntb.pdev;

	ndev->ntb.topo = amd_get_topo(ndev);
	dev_dbg(&pdev->dev, "AMD NTB topo is %s\n",
		ntb_topo_string(ndev->ntb.topo));

	rc = amd_init_ntb(ndev);
	if (rc)
		return rc;

	rc = amd_init_isr(ndev);
	if (rc) {
		dev_err(&pdev->dev, "fail to init isr.\n");
		return rc;
	}

	ndev->db_valid_mask = BIT_ULL(ndev->db_count) - 1;

	return 0;
}

static void amd_deinit_dev(struct amd_ntb_dev *ndev)
{
	cancel_delayed_work_sync(&ndev->hb_timer);

	ndev_deinit_isr(ndev);
}

static int amd_ntb_init_pci(struct amd_ntb_dev *ndev,
			    struct pci_dev *pdev)
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
		dev_warn(&pdev->dev, "Cannot DMA highmem\n");
	}

	rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc) {
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc)
			goto err_dma_mask;
		dev_warn(&pdev->dev, "Cannot DMA consistent highmem\n");
	}
	rc = dma_coerce_mask_and_coherent(&ndev->ntb.dev,
					  dma_get_mask(&pdev->dev));
	if (rc)
		goto err_dma_mask;

	ndev->self_mmio = pci_iomap(pdev, 0, 0);
	if (!ndev->self_mmio) {
		rc = -EIO;
		goto err_dma_mask;
	}
	ndev->peer_mmio = ndev->self_mmio + AMD_PEER_OFFSET;

	return 0;

err_dma_mask:
	pci_clear_master(pdev);
err_pci_regions:
	pci_disable_device(pdev);
err_pci_enable:
	pci_set_drvdata(pdev, NULL);
	return rc;
}

static void amd_ntb_deinit_pci(struct amd_ntb_dev *ndev)
{
	struct pci_dev *pdev = ndev->ntb.pdev;

	pci_iounmap(pdev, ndev->self_mmio);

	pci_clear_master(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static int amd_ntb_pci_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	struct amd_ntb_dev *ndev;
	int rc, node;

	node = dev_to_node(&pdev->dev);

	ndev = kzalloc_node(sizeof(*ndev), GFP_KERNEL, node);
	if (!ndev) {
		rc = -ENOMEM;
		goto err_ndev;
	}

	ndev->dev_data = (struct ntb_dev_data *)id->driver_data;

	ndev_init_struct(ndev, pdev);

	rc = amd_ntb_init_pci(ndev, pdev);
	if (rc)
		goto err_init_pci;

	rc = amd_init_dev(ndev);
	if (rc)
		goto err_init_dev;

	/* write side info */
	amd_init_side_info(ndev);

	amd_poll_link(ndev);

	ndev_init_debugfs(ndev);

	rc = ntb_register_device(&ndev->ntb);
	if (rc)
		goto err_register;

	dev_info(&pdev->dev, "NTB device registered.\n");

	return 0;

err_register:
	ndev_deinit_debugfs(ndev);
	amd_deinit_dev(ndev);
err_init_dev:
	amd_ntb_deinit_pci(ndev);
err_init_pci:
	kfree(ndev);
err_ndev:
	return rc;
}

static void amd_ntb_pci_remove(struct pci_dev *pdev)
{
	struct amd_ntb_dev *ndev = pci_get_drvdata(pdev);

	ntb_unregister_device(&ndev->ntb);
	ndev_deinit_debugfs(ndev);
	amd_deinit_side_info(ndev);
	amd_deinit_dev(ndev);
	amd_ntb_deinit_pci(ndev);
	kfree(ndev);
}

static const struct file_operations amd_ntb_debugfs_info = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = ndev_debugfs_read,
};

static const struct ntb_dev_data dev_data[] = {
	{ /* for device 145b */
		.mw_count = 3,
		.mw_idx = 1,
	},
	{ /* for device 148b */
		.mw_count = 2,
		.mw_idx = 2,
	},
};

static const struct pci_device_id amd_ntb_pci_tbl[] = {
	{ PCI_VDEVICE(AMD, 0x145b), (kernel_ulong_t)&dev_data[0] },
	{ PCI_VDEVICE(AMD, 0x148b), (kernel_ulong_t)&dev_data[1] },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, amd_ntb_pci_tbl);

static struct pci_driver amd_ntb_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= amd_ntb_pci_tbl,
	.probe		= amd_ntb_pci_probe,
	.remove		= amd_ntb_pci_remove,
};

static int __init amd_ntb_pci_driver_init(void)
{
	pr_info("%s %s\n", NTB_DESC, NTB_VER);

	if (debugfs_initialized())
		debugfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	return pci_register_driver(&amd_ntb_pci_driver);
}
module_init(amd_ntb_pci_driver_init);

static void __exit amd_ntb_pci_driver_exit(void)
{
	pci_unregister_driver(&amd_ntb_pci_driver);
	debugfs_remove_recursive(debugfs_dir);
}
module_exit(amd_ntb_pci_driver_exit);
