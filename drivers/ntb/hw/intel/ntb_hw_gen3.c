/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Intel Corporation. All rights reserved.
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
 * Intel PCIe GEN3 NTB Linux driver
 *
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
#include "ntb_hw_gen1.h"
#include "ntb_hw_gen3.h"

static int gen3_poll_link(struct intel_ntb_dev *ndev);

static const struct intel_ntb_reg gen3_reg = {
	.poll_link		= gen3_poll_link,
	.link_is_up		= xeon_link_is_up,
	.db_ioread		= gen3_db_ioread,
	.db_iowrite		= gen3_db_iowrite,
	.db_size		= sizeof(u32),
	.ntb_ctl		= GEN3_NTBCNTL_OFFSET,
	.mw_bar			= {2, 4},
};

static const struct intel_ntb_alt_reg gen3_pri_reg = {
	.db_bell		= GEN3_EM_DOORBELL_OFFSET,
	.db_clear		= GEN3_IM_INT_STATUS_OFFSET,
	.db_mask		= GEN3_IM_INT_DISABLE_OFFSET,
	.spad			= GEN3_IM_SPAD_OFFSET,
};

static const struct intel_ntb_alt_reg gen3_b2b_reg = {
	.db_bell		= GEN3_IM_DOORBELL_OFFSET,
	.db_clear		= GEN3_EM_INT_STATUS_OFFSET,
	.db_mask		= GEN3_EM_INT_DISABLE_OFFSET,
	.spad			= GEN3_B2B_SPAD_OFFSET,
};

static const struct intel_ntb_xlat_reg gen3_sec_xlat = {
/*	.bar0_base		= GEN3_EMBAR0_OFFSET, */
	.bar2_limit		= GEN3_IMBAR1XLMT_OFFSET,
	.bar2_xlat		= GEN3_IMBAR1XBASE_OFFSET,
};

static int gen3_poll_link(struct intel_ntb_dev *ndev)
{
	u16 reg_val;
	int rc;

	ndev->reg->db_iowrite(ndev->db_link_mask,
			      ndev->self_mmio +
			      ndev->self_reg->db_clear);

	rc = pci_read_config_word(ndev->ntb.pdev,
				  GEN3_LINK_STATUS_OFFSET, &reg_val);
	if (rc)
		return 0;

	if (reg_val == ndev->lnk_sta)
		return 0;

	ndev->lnk_sta = reg_val;

	return 1;
}

static int gen3_init_isr(struct intel_ntb_dev *ndev)
{
	int i;

	/*
	 * The MSIX vectors and the interrupt status bits are not lined up
	 * on Skylake. By default the link status bit is bit 32, however it
	 * is by default MSIX vector0. We need to fixup to line them up.
	 * The vectors at reset is 1-32,0. We need to reprogram to 0-32.
	 */

	for (i = 0; i < GEN3_DB_MSIX_VECTOR_COUNT; i++)
		iowrite8(i, ndev->self_mmio + GEN3_INTVEC_OFFSET + i);

	/* move link status down one as workaround */
	if (ndev->hwerr_flags & NTB_HWERR_MSIX_VECTOR32_BAD) {
		iowrite8(GEN3_DB_MSIX_VECTOR_COUNT - 2,
			 ndev->self_mmio + GEN3_INTVEC_OFFSET +
			 (GEN3_DB_MSIX_VECTOR_COUNT - 1));
	}

	return ndev_init_isr(ndev, GEN3_DB_MSIX_VECTOR_COUNT,
			     GEN3_DB_MSIX_VECTOR_COUNT,
			     GEN3_DB_MSIX_VECTOR_SHIFT,
			     GEN3_DB_TOTAL_SHIFT);
}

static int gen3_setup_b2b_mw(struct intel_ntb_dev *ndev,
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
	iowrite64(bar_addr, mmio + GEN3_IMBAR1XLMT_OFFSET);
	bar_addr = ioread64(mmio + GEN3_IMBAR1XLMT_OFFSET);
	dev_dbg(&pdev->dev, "IMBAR1XLMT %#018llx\n", bar_addr);

	bar_addr = addr->bar4_addr64;
	iowrite64(bar_addr, mmio + GEN3_IMBAR2XLMT_OFFSET);
	bar_addr = ioread64(mmio + GEN3_IMBAR2XLMT_OFFSET);
	dev_dbg(&pdev->dev, "IMBAR2XLMT %#018llx\n", bar_addr);

	/* zero incoming translation addrs */
	iowrite64(0, mmio + GEN3_IMBAR1XBASE_OFFSET);
	iowrite64(0, mmio + GEN3_IMBAR2XBASE_OFFSET);

	ndev->peer_mmio = ndev->self_mmio;

	return 0;
}

static int gen3_init_ntb(struct intel_ntb_dev *ndev)
{
	int rc;


	ndev->mw_count = XEON_MW_COUNT;
	ndev->spad_count = GEN3_SPAD_COUNT;
	ndev->db_count = GEN3_DB_COUNT;
	ndev->db_link_mask = GEN3_DB_LINK_BIT;

	/* DB fixup for using 31 right now */
	if (ndev->hwerr_flags & NTB_HWERR_MSIX_VECTOR32_BAD)
		ndev->db_link_mask |= BIT_ULL(31);

	switch (ndev->ntb.topo) {
	case NTB_TOPO_B2B_USD:
	case NTB_TOPO_B2B_DSD:
		ndev->self_reg = &gen3_pri_reg;
		ndev->peer_reg = &gen3_b2b_reg;
		ndev->xlat_reg = &gen3_sec_xlat;

		if (ndev->ntb.topo == NTB_TOPO_B2B_USD) {
			rc = gen3_setup_b2b_mw(ndev,
					      &xeon_b2b_dsd_addr,
					      &xeon_b2b_usd_addr);
		} else {
			rc = gen3_setup_b2b_mw(ndev,
					      &xeon_b2b_usd_addr,
					      &xeon_b2b_dsd_addr);
		}

		if (rc)
			return rc;

		/* Enable Bus Master and Memory Space on the secondary side */
		iowrite16(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
			  ndev->self_mmio + GEN3_SPCICMD_OFFSET);

		break;

	default:
		return -EINVAL;
	}

	ndev->db_valid_mask = BIT_ULL(ndev->db_count) - 1;
	/* Make sure we are not using DB's used for link status */
	if (ndev->hwerr_flags & NTB_HWERR_MSIX_VECTOR32_BAD)
		ndev->db_valid_mask &= ~ndev->db_link_mask;

	ndev->reg->db_iowrite(ndev->db_valid_mask,
			      ndev->self_mmio +
			      ndev->self_reg->db_mask);

	return 0;
}

int gen3_init_dev(struct intel_ntb_dev *ndev)
{
	struct pci_dev *pdev;
	u8 ppd;
	int rc;

	pdev = ndev->ntb.pdev;

	ndev->reg = &gen3_reg;

	rc = pci_read_config_byte(pdev, XEON_PPD_OFFSET, &ppd);
	if (rc)
		return -EIO;

	ndev->ntb.topo = xeon_ppd_topo(ndev, ppd);
	dev_dbg(&pdev->dev, "ppd %#x topo %s\n", ppd,
		ntb_topo_string(ndev->ntb.topo));
	if (ndev->ntb.topo == NTB_TOPO_NONE)
		return -EINVAL;

	ndev->hwerr_flags |= NTB_HWERR_MSIX_VECTOR32_BAD;

	rc = gen3_init_ntb(ndev);
	if (rc)
		return rc;

	return gen3_init_isr(ndev);
}

ssize_t ndev_ntb3_debugfs_read(struct file *filp, char __user *ubuf,
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

	u.v64 = ioread64(mmio + GEN3_IMBAR1XBASE_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "IMBAR1XBASE -\t\t%#018llx\n", u.v64);

	u.v64 = ioread64(mmio + GEN3_IMBAR2XBASE_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "IMBAR2XBASE -\t\t%#018llx\n", u.v64);

	u.v64 = ioread64(mmio + GEN3_IMBAR1XLMT_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "IMBAR1XLMT -\t\t\t%#018llx\n", u.v64);

	u.v64 = ioread64(mmio + GEN3_IMBAR2XLMT_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "IMBAR2XLMT -\t\t\t%#018llx\n", u.v64);

	if (ntb_topo_is_b2b(ndev->ntb.topo)) {
		off += scnprintf(buf + off, buf_size - off,
				 "\nNTB Outgoing B2B XLAT:\n");

		u.v64 = ioread64(mmio + GEN3_EMBAR1XBASE_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR1XBASE -\t\t%#018llx\n", u.v64);

		u.v64 = ioread64(mmio + GEN3_EMBAR2XBASE_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR2XBASE -\t\t%#018llx\n", u.v64);

		u.v64 = ioread64(mmio + GEN3_EMBAR1XLMT_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR1XLMT -\t\t%#018llx\n", u.v64);

		u.v64 = ioread64(mmio + GEN3_EMBAR2XLMT_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR2XLMT -\t\t%#018llx\n", u.v64);

		off += scnprintf(buf + off, buf_size - off,
				 "\nNTB Secondary BAR:\n");

		u.v64 = ioread64(mmio + GEN3_EMBAR0_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR0 -\t\t%#018llx\n", u.v64);

		u.v64 = ioread64(mmio + GEN3_EMBAR1_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR1 -\t\t%#018llx\n", u.v64);

		u.v64 = ioread64(mmio + GEN3_EMBAR2_OFFSET);
		off += scnprintf(buf + off, buf_size - off,
				 "EMBAR2 -\t\t%#018llx\n", u.v64);
	}

	off += scnprintf(buf + off, buf_size - off,
			 "\nNTB Statistics:\n");

	u.v16 = ioread16(mmio + GEN3_USMEMMISS_OFFSET);
	off += scnprintf(buf + off, buf_size - off,
			 "Upstream Memory Miss -\t%u\n", u.v16);

	off += scnprintf(buf + off, buf_size - off,
			 "\nNTB Hardware Errors:\n");

	if (!pci_read_config_word(ndev->ntb.pdev,
				  GEN3_DEVSTS_OFFSET, &u.v16))
		off += scnprintf(buf + off, buf_size - off,
				 "DEVSTS -\t\t%#06x\n", u.v16);

	if (!pci_read_config_word(ndev->ntb.pdev,
				  GEN3_LINK_STATUS_OFFSET, &u.v16))
		off += scnprintf(buf + off, buf_size - off,
				 "LNKSTS -\t\t%#06x\n", u.v16);

	if (!pci_read_config_dword(ndev->ntb.pdev,
				   GEN3_UNCERRSTS_OFFSET, &u.v32))
		off += scnprintf(buf + off, buf_size - off,
				 "UNCERRSTS -\t\t%#06x\n", u.v32);

	if (!pci_read_config_dword(ndev->ntb.pdev,
				   GEN3_CORERRSTS_OFFSET, &u.v32))
		off += scnprintf(buf + off, buf_size - off,
				 "CORERRSTS -\t\t%#06x\n", u.v32);

	ret = simple_read_from_buffer(ubuf, count, offp, buf, off);
	kfree(buf);
	return ret;
}

int intel_ntb3_link_enable(struct ntb_dev *ntb, enum ntb_speed max_speed,
		enum ntb_width max_width)
{
	struct intel_ntb_dev *ndev;
	u32 ntb_ctl;

	ndev = container_of(ntb, struct intel_ntb_dev, ntb);

	dev_dbg(&ntb->pdev->dev,
		"Enabling link with max_speed %d max_width %d\n",
		max_speed, max_width);

	if (max_speed != NTB_SPEED_AUTO)
		dev_dbg(&ntb->pdev->dev, "ignoring max_speed %d\n", max_speed);
	if (max_width != NTB_WIDTH_AUTO)
		dev_dbg(&ntb->pdev->dev, "ignoring max_width %d\n", max_width);

	ntb_ctl = ioread32(ndev->self_mmio + ndev->reg->ntb_ctl);
	ntb_ctl &= ~(NTB_CTL_DISABLE | NTB_CTL_CFG_LOCK);
	ntb_ctl |= NTB_CTL_P2S_BAR2_SNOOP | NTB_CTL_S2P_BAR2_SNOOP;
	ntb_ctl |= NTB_CTL_P2S_BAR4_SNOOP | NTB_CTL_S2P_BAR4_SNOOP;
	iowrite32(ntb_ctl, ndev->self_mmio + ndev->reg->ntb_ctl);

	return 0;
}
static int intel_ntb3_mw_set_trans(struct ntb_dev *ntb, int pidx, int idx,
				   dma_addr_t addr, resource_size_t size)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);
	unsigned long xlat_reg, limit_reg;
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

	dev_dbg(&ntb->pdev->dev, "BAR %d IMBARXBASE: %#Lx\n", bar, reg_val);

	/* set and verify setting the limit */
	iowrite64(limit, mmio + limit_reg);
	reg_val = ioread64(mmio + limit_reg);
	if (reg_val != limit) {
		iowrite64(base, mmio + limit_reg);
		iowrite64(0, mmio + xlat_reg);
		return -EIO;
	}

	dev_dbg(&ntb->pdev->dev, "BAR %d IMBARXLMT: %#Lx\n", bar, reg_val);

	/* setup the EP */
	limit_reg = ndev->xlat_reg->bar2_limit + (idx * 0x10) + 0x4000;
	base = ioread64(mmio + GEN3_EMBAR1_OFFSET + (8 * idx));
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

	dev_dbg(&ntb->pdev->dev, "BAR %d EMBARXLMT: %#Lx\n", bar, reg_val);

	return 0;
}

int intel_ntb3_peer_db_addr(struct ntb_dev *ntb, phys_addr_t *db_addr,
				   resource_size_t *db_size,
				   u64 *db_data, int db_bit)
{
	phys_addr_t db_addr_base;
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	if (unlikely(db_bit >= BITS_PER_LONG_LONG))
		return -EINVAL;

	if (unlikely(BIT_ULL(db_bit) & ~ntb_ndev(ntb)->db_valid_mask))
		return -EINVAL;

	ndev_db_addr(ndev, &db_addr_base, db_size, ndev->peer_addr,
				ndev->peer_reg->db_bell);

	if (db_addr) {
		*db_addr = db_addr_base + (db_bit * 4);
		dev_dbg(&ndev->ntb.pdev->dev, "Peer db addr %llx db bit %d\n",
				*db_addr, db_bit);
	}

	if (db_data) {
		*db_data = 1;
		dev_dbg(&ndev->ntb.pdev->dev, "Peer db data %llx db bit %d\n",
				*db_data, db_bit);
	}

	return 0;
}

int intel_ntb3_peer_db_set(struct ntb_dev *ntb, u64 db_bits)
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

u64 intel_ntb3_db_read(struct ntb_dev *ntb)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_db_read(ndev,
			    ndev->self_mmio +
			    ndev->self_reg->db_clear);
}

int intel_ntb3_db_clear(struct ntb_dev *ntb, u64 db_bits)
{
	struct intel_ntb_dev *ndev = ntb_ndev(ntb);

	return ndev_db_write(ndev, db_bits,
			     ndev->self_mmio +
			     ndev->self_reg->db_clear);
}

const struct ntb_dev_ops intel_ntb3_ops = {
	.mw_count		= intel_ntb_mw_count,
	.mw_get_align		= intel_ntb_mw_get_align,
	.mw_set_trans		= intel_ntb3_mw_set_trans,
	.peer_mw_count		= intel_ntb_peer_mw_count,
	.peer_mw_get_addr	= intel_ntb_peer_mw_get_addr,
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

