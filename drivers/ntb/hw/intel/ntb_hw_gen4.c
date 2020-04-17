// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/ntb.h>
#include <linux/log2.h>

#include "ntb_hw_intel.h"
#include "ntb_hw_gen1.h"
#include "ntb_hw_gen3.h"
#include "ntb_hw_gen4.h"

static int gen4_poll_link(struct intel_ntb_dev *ndev);
static int gen4_link_is_up(struct intel_ntb_dev *ndev);

static const struct intel_ntb_reg gen4_reg = {
	.poll_link		= gen4_poll_link,
	.link_is_up		= gen4_link_is_up,
	.db_ioread		= gen3_db_ioread,
	.db_iowrite		= gen3_db_iowrite,
	.db_size		= sizeof(u32),
	.ntb_ctl		= GEN4_NTBCNTL_OFFSET,
	.mw_bar			= {2, 4},
};

static const struct intel_ntb_alt_reg gen4_pri_reg = {
	.db_clear		= GEN4_IM_INT_STATUS_OFFSET,
	.db_mask		= GEN4_IM_INT_DISABLE_OFFSET,
	.spad			= GEN4_IM_SPAD_OFFSET,
};

static const struct intel_ntb_xlat_reg gen4_sec_xlat = {
	.bar2_limit		= GEN4_IM23XLMT_OFFSET,
	.bar2_xlat		= GEN4_IM23XBASE_OFFSET,
	.bar2_idx		= GEN4_IM23XBASEIDX_OFFSET,
};

static const struct intel_ntb_alt_reg gen4_b2b_reg = {
	.db_bell		= GEN4_IM_DOORBELL_OFFSET,
	.spad			= GEN4_EM_SPAD_OFFSET,
};

static int gen4_poll_link(struct intel_ntb_dev *ndev)
{
	u16 reg_val;

	/*
	 * We need to write to DLLSCS bit in the SLOTSTS before we
	 * can clear the hardware link interrupt on ICX NTB.
	 */
	iowrite16(GEN4_SLOTSTS_DLLSCS, ndev->self_mmio + GEN4_SLOTSTS);
	ndev->reg->db_iowrite(ndev->db_link_mask,
			      ndev->self_mmio +
			      ndev->self_reg->db_clear);

	reg_val = ioread16(ndev->self_mmio + GEN4_LINK_STATUS_OFFSET);
	if (reg_val == ndev->lnk_sta)
		return 0;

	ndev->lnk_sta = reg_val;

	return 1;
}

static int gen4_link_is_up(struct intel_ntb_dev *ndev)
{
	return NTB_LNK_STA_ACTIVE(ndev->lnk_sta);
}

static int gen4_init_isr(struct intel_ntb_dev *ndev)
{
	int i;

	/*
	 * The MSIX vectors and the interrupt status bits are not lined up
	 * on Gen3 (Skylake) and Gen4. By default the link status bit is bit
	 * 32, however it is by default MSIX vector0. We need to fixup to
	 * line them up. The vectors at reset is 1-32,0. We need to reprogram
	 * to 0-32.
	 */
	for (i = 0; i < GEN4_DB_MSIX_VECTOR_COUNT; i++)
		iowrite8(i, ndev->self_mmio + GEN4_INTVEC_OFFSET + i);

	return ndev_init_isr(ndev, GEN4_DB_MSIX_VECTOR_COUNT,
			     GEN4_DB_MSIX_VECTOR_COUNT,
			     GEN4_DB_MSIX_VECTOR_SHIFT,
			     GEN4_DB_TOTAL_SHIFT);
}

static int gen4_setup_b2b_mw(struct intel_ntb_dev *ndev,
			    const struct intel_b2b_addr *addr,
			    const struct intel_b2b_addr *peer_addr)
{
	struct pci_dev *pdev;
	void __iomem *mmio;
	phys_addr_t bar_addr;

	pdev = ndev->ntb.pdev;
	mmio = ndev->self_mmio;

	/* setup incoming bar limits == base addrs (zero length windows) */
	bar_addr = addr->bar2_addr64;
	iowrite64(bar_addr, mmio + GEN4_IM23XLMT_OFFSET);
	bar_addr = ioread64(mmio + GEN4_IM23XLMT_OFFSET);
	dev_dbg(&pdev->dev, "IM23XLMT %#018llx\n", bar_addr);

	bar_addr = addr->bar4_addr64;
	iowrite64(bar_addr, mmio + GEN4_IM45XLMT_OFFSET);
	bar_addr = ioread64(mmio + GEN4_IM45XLMT_OFFSET);
	dev_dbg(&pdev->dev, "IM45XLMT %#018llx\n", bar_addr);

	/* zero incoming translation addrs */
	iowrite64(0, mmio + GEN4_IM23XBASE_OFFSET);
	iowrite64(0, mmio + GEN4_IM45XBASE_OFFSET);

	ndev->peer_mmio = ndev->self_mmio;

	return 0;
}

static int gen4_init_ntb(struct intel_ntb_dev *ndev)
{
	int rc;


	ndev->mw_count = XEON_MW_COUNT;
	ndev->spad_count = GEN4_SPAD_COUNT;
	ndev->db_count = GEN4_DB_COUNT;
	ndev->db_link_mask = GEN4_DB_LINK_BIT;

	ndev->self_reg = &gen4_pri_reg;
	ndev->xlat_reg = &gen4_sec_xlat;
	ndev->peer_reg = &gen4_b2b_reg;

	if (ndev->ntb.topo == NTB_TOPO_B2B_USD)
		rc = gen4_setup_b2b_mw(ndev, &xeon_b2b_dsd_addr,
				&xeon_b2b_usd_addr);
	else
		rc = gen4_setup_b2b_mw(ndev, &xeon_b2b_usd_addr,
				&xeon_b2b_dsd_addr);
	if (rc)
		return rc;

	ndev->db_valid_mask = BIT_ULL(ndev->db_count) - 1;

	ndev->reg->db_iowrite(ndev->db_valid_mask,
			      ndev->self_mmio +
			      ndev->self_reg->db_mask);

	return 0;
}

static enum ntb_topo gen4_ppd_topo(struct intel_ntb_dev *ndev, u32 ppd)
{
	switch (ppd & GEN4_PPD_TOPO_MASK) {
	case GEN4_PPD_TOPO_B2B_USD:
		return NTB_TOPO_B2B_USD;
	case GEN4_PPD_TOPO_B2B_DSD:
		return NTB_TOPO_B2B_DSD;
	}

	return NTB_TOPO_NONE;
}

int gen4_init_dev(struct intel_ntb_dev *ndev)
{
	struct pci_dev *pdev = ndev->ntb.pdev;
	u32 ppd1/*, ppd0*/;
	u16 lnkctl;
	int rc;

	ndev->reg = &gen4_reg;

	ppd1 = ioread32(ndev->self_mmio + GEN4_PPD1_OFFSET);
	ndev->ntb.topo = gen4_ppd_topo(ndev, ppd1);
	dev_dbg(&pdev->dev, "ppd %#x topo %s\n", ppd1,
		ntb_topo_string(ndev->ntb.topo));
	if (ndev->ntb.topo == NTB_TOPO_NONE)
		return -EINVAL;

	rc = gen4_init_ntb(ndev);
	if (rc)
		return rc;

	/* init link setup */
	lnkctl = ioread16(ndev->self_mmio + GEN4_LINK_CTRL_OFFSET);
	lnkctl |= GEN4_LINK_CTRL_LINK_DISABLE;
	iowrite16(lnkctl, ndev->self_mmio + GEN4_LINK_CTRL_OFFSET);

	return gen4_init_isr(ndev);
}

ssize_t ndev_ntb4_debugfs_read(struct file *filp, char __user *ubuf,
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
			 "LNK STA (cached) -\t\t%#06x\n", ndev->lnk_sta);

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

	off += scnprintf(buf + off, buf_size - off,
			 "\nNTB Incoming XLAT:\n");

	u.v64 = ioread64(mmio + GEN4_IM23XBASE_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "IM23XBASE -\t\t%#018llx\n", u.v64);

	u.v64 = ioread64(mmio + GEN4_IM45XBASE_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "IM45XBASE -\t\t%#018llx\n", u.v64);

	u.v64 = ioread64(mmio + GEN4_IM23XLMT_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "IM23XLMT -\t\t\t%#018llx\n", u.v64);

	u.v64 = ioread64(mmio + GEN4_IM45XLMT_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "IM45XLMT -\t\t\t%#018llx\n", u.v64);

	off += scnprintf(buf + off, buf_size - off,
			 "\nNTB Statistics:\n");

	off += scnprintf(buf + off, buf_size - off,
			 "\nNTB Hardware Errors:\n");

	if (!pci_read_config_word(ndev->ntb.pdev,
				  GEN4_DEVSTS_OFFSET, &u.v16))
		off += scnprintf(buf + off, buf_size - off,
				"DEVSTS -\t\t%#06x\n", u.v16);

	u.v16 = ioread16(mmio + GEN4_LINK_STATUS_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			"LNKSTS -\t\t%#06x\n", u.v16);

	if (!pci_read_config_dword(ndev->ntb.pdev,
				   GEN4_UNCERRSTS_OFFSET, &u.v32))
		off += scnprintf(buf + off, buf_size - off,
				 "UNCERRSTS -\t\t%#06x\n", u.v32);

	if (!pci_read_config_dword(ndev->ntb.pdev,
				   GEN4_CORERRSTS_OFFSET, &u.v32))
		off += scnprintf(buf + off, buf_size - off,
				 "CORERRSTS -\t\t%#06x\n", u.v32);

	ret = simple_read_from_buffer(ubuf, count, offp, buf, off);
	kfree(buf);
	return ret;
}

static int intel_ntb4_mw_set_trans(struct ntb_dev *ntb, int pidx, int idx,
				   dma_addr_t addr, resource_size_t size)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);
	unsigned long xlat_reg, limit_reg, idx_reg;
	unsigned short base_idx, reg_val16;
	resource_size_t bar_size, mw_size;
	void __iomem *mmio;
	u64 base, limit, reg_val;
	int bar;

	if (pidx != NTB_DEF_PEER_IDX)
		return -EINVAL;

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
	idx_reg = ndev->xlat_reg->bar2_idx + (idx * 0x2);
	base = pci_resource_start(ndev->ntb.pdev, bar);

	/* Set the limit if supported, if size is not mw_size */
	if (limit_reg && size != mw_size) {
		limit = base + size;
		base_idx = __ilog2_u64(size);
	} else {
		limit = base + mw_size;
		base_idx = __ilog2_u64(mw_size);
	}


	/* set and verify setting the translation address */
	iowrite64(addr, mmio + xlat_reg);
	reg_val = ioread64(mmio + xlat_reg);
	if (reg_val != addr) {
		iowrite64(0, mmio + xlat_reg);
		return -EIO;
	}

	dev_dbg(&ntb->pdev->dev, "BAR %d IMXBASE: %#Lx\n", bar, reg_val);

	/* set and verify setting the limit */
	iowrite64(limit, mmio + limit_reg);
	reg_val = ioread64(mmio + limit_reg);
	if (reg_val != limit) {
		iowrite64(base, mmio + limit_reg);
		iowrite64(0, mmio + xlat_reg);
		return -EIO;
	}

	dev_dbg(&ntb->pdev->dev, "BAR %d IMXLMT: %#Lx\n", bar, reg_val);

	iowrite16(base_idx, mmio + idx_reg);
	reg_val16 = ioread16(mmio + idx_reg);
	if (reg_val16 != base_idx) {
		iowrite64(base, mmio + limit_reg);
		iowrite64(0, mmio + xlat_reg);
		iowrite16(0, mmio + idx_reg);
		return -EIO;
	}

	dev_dbg(&ntb->pdev->dev, "BAR %d IMBASEIDX: %#x\n", bar, reg_val16);

	return 0;
}

static int intel_ntb4_link_enable(struct ntb_dev *ntb,
		enum ntb_speed max_speed, enum ntb_width max_width)
{
	struct intel_ntb_dev *ndev;
	u32 ntb_ctl, ppd0;
	u16 lnkctl;

	ndev = container_of(ntb, struct intel_ntb_dev, ntb);

	dev_dbg(&ntb->pdev->dev,
			"Enabling link with max_speed %d max_width %d\n",
			max_speed, max_width);

	if (max_speed != NTB_SPEED_AUTO)
		dev_dbg(&ntb->pdev->dev,
				"ignoring max_speed %d\n", max_speed);
	if (max_width != NTB_WIDTH_AUTO)
		dev_dbg(&ntb->pdev->dev,
				"ignoring max_width %d\n", max_width);

	ntb_ctl = NTB_CTL_E2I_BAR23_SNOOP | NTB_CTL_I2E_BAR23_SNOOP;
	ntb_ctl |= NTB_CTL_E2I_BAR45_SNOOP | NTB_CTL_I2E_BAR45_SNOOP;
	iowrite32(ntb_ctl, ndev->self_mmio + ndev->reg->ntb_ctl);

	lnkctl = ioread16(ndev->self_mmio + GEN4_LINK_CTRL_OFFSET);
	lnkctl &= ~GEN4_LINK_CTRL_LINK_DISABLE;
	iowrite16(lnkctl, ndev->self_mmio + GEN4_LINK_CTRL_OFFSET);

	/* start link training in PPD0 */
	ppd0 = ioread32(ndev->self_mmio + GEN4_PPD0_OFFSET);
	ppd0 |= GEN4_PPD_LINKTRN;
	iowrite32(ppd0, ndev->self_mmio + GEN4_PPD0_OFFSET);

	/* make sure link training has started */
	ppd0 = ioread32(ndev->self_mmio + GEN4_PPD0_OFFSET);
	if (!(ppd0 & GEN4_PPD_LINKTRN)) {
		dev_warn(&ntb->pdev->dev, "Link is not training\n");
		return -ENXIO;
	}

	ndev->dev_up = 1;

	return 0;
}

int intel_ntb4_link_disable(struct ntb_dev *ntb)
{
	struct intel_ntb_dev *ndev;
	u32 ntb_cntl;
	u16 lnkctl;

	ndev = container_of(ntb, struct intel_ntb_dev, ntb);

	dev_dbg(&ntb->pdev->dev, "Disabling link\n");

	/* clear the snoop bits */
	ntb_cntl = ioread32(ndev->self_mmio + ndev->reg->ntb_ctl);
	ntb_cntl &= ~(NTB_CTL_E2I_BAR23_SNOOP | NTB_CTL_I2E_BAR23_SNOOP);
	ntb_cntl &= ~(NTB_CTL_E2I_BAR45_SNOOP | NTB_CTL_I2E_BAR45_SNOOP);
	iowrite32(ntb_cntl, ndev->self_mmio + ndev->reg->ntb_ctl);

	lnkctl = ioread16(ndev->self_mmio + GEN4_LINK_CTRL_OFFSET);
	lnkctl |= GEN4_LINK_CTRL_LINK_DISABLE;
	iowrite16(lnkctl, ndev->self_mmio + GEN4_LINK_CTRL_OFFSET);

	ndev->dev_up = 0;

	return 0;
}

const struct ntb_dev_ops intel_ntb4_ops = {
	.mw_count		= intel_ntb_mw_count,
	.mw_get_align		= intel_ntb_mw_get_align,
	.mw_set_trans		= intel_ntb4_mw_set_trans,
	.peer_mw_count		= intel_ntb_peer_mw_count,
	.peer_mw_get_addr	= intel_ntb_peer_mw_get_addr,
	.link_is_up		= intel_ntb_link_is_up,
	.link_enable		= intel_ntb4_link_enable,
	.link_disable		= intel_ntb4_link_disable,
	.db_valid_mask		= intel_ntb_db_valid_mask,
	.db_vector_count	= intel_ntb_db_vector_count,
	.db_vector_mask		= intel_ntb_db_vector_mask,
	.db_read		= intel_ntb3_db_read,
	.db_clear		= intel_ntb3_db_clear,
	.db_set_mask		= intel_ntb_db_set_mask,
	.db_clear_mask		= intel_ntb_db_clear_mask,
	.peer_db_addr		= intel_ntb3_peer_db_addr,
	.peer_db_set		= intel_ntb3_peer_db_set,
	.spad_is_unsafe		= intel_ntb_spad_is_unsafe,
	.spad_count		= intel_ntb_spad_count,
	.spad_read		= intel_ntb_spad_read,
	.spad_write		= intel_ntb_spad_write,
	.peer_spad_addr		= intel_ntb_peer_spad_addr,
	.peer_spad_read		= intel_ntb_peer_spad_read,
	.peer_spad_write	= intel_ntb_peer_spad_write,
};

