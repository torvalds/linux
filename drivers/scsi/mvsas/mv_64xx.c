/*
 * Marvell 88SE64xx hardware specific
 *
 * Copyright 2007 Red Hat, Inc.
 * Copyright 2008 Marvell. <kewei@marvell.com>
 * Copyright 2009-2011 Marvell. <yuxiangl@marvell.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
*/

#include "mv_sas.h"
#include "mv_64xx.h"
#include "mv_chips.h"

static void mvs_64xx_detect_porttype(struct mvs_info *mvi, int i)
{
	void __iomem *regs = mvi->regs;
	u32 reg;
	struct mvs_phy *phy = &mvi->phy[i];

	reg = mr32(MVS_GBL_PORT_TYPE);
	phy->phy_type &= ~(PORT_TYPE_SAS | PORT_TYPE_SATA);
	if (reg & MODE_SAS_SATA & (1 << i))
		phy->phy_type |= PORT_TYPE_SAS;
	else
		phy->phy_type |= PORT_TYPE_SATA;
}

static void mvs_64xx_enable_xmt(struct mvs_info *mvi, int phy_id)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	tmp = mr32(MVS_PCS);
	if (mvi->chip->n_phy <= MVS_SOC_PORTS)
		tmp |= 1 << (phy_id + PCS_EN_PORT_XMT_SHIFT);
	else
		tmp |= 1 << (phy_id + PCS_EN_PORT_XMT_SHIFT2);
	mw32(MVS_PCS, tmp);
}

static void mvs_64xx_phy_hacks(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	int i;

	mvs_phy_hacks(mvi);

	if (!(mvi->flags & MVF_FLAG_SOC)) {
		for (i = 0; i < MVS_SOC_PORTS; i++) {
			mvs_write_port_vsr_addr(mvi, i, VSR_PHY_MODE8);
			mvs_write_port_vsr_data(mvi, i, 0x2F0);
		}
	} else {
		/* disable auto port detection */
		mw32(MVS_GBL_PORT_TYPE, 0);
		for (i = 0; i < mvi->chip->n_phy; i++) {
			mvs_write_port_vsr_addr(mvi, i, VSR_PHY_MODE7);
			mvs_write_port_vsr_data(mvi, i, 0x90000000);
			mvs_write_port_vsr_addr(mvi, i, VSR_PHY_MODE9);
			mvs_write_port_vsr_data(mvi, i, 0x50f2);
			mvs_write_port_vsr_addr(mvi, i, VSR_PHY_MODE11);
			mvs_write_port_vsr_data(mvi, i, 0x0e);
		}
	}
}

static void mvs_64xx_stp_reset(struct mvs_info *mvi, u32 phy_id)
{
	void __iomem *regs = mvi->regs;
	u32 reg, tmp;

	if (!(mvi->flags & MVF_FLAG_SOC)) {
		if (phy_id < MVS_SOC_PORTS)
			pci_read_config_dword(mvi->pdev, PCR_PHY_CTL, &reg);
		else
			pci_read_config_dword(mvi->pdev, PCR_PHY_CTL2, &reg);

	} else
		reg = mr32(MVS_PHY_CTL);

	tmp = reg;
	if (phy_id < MVS_SOC_PORTS)
		tmp |= (1U << phy_id) << PCTL_LINK_OFFS;
	else
		tmp |= (1U << (phy_id - MVS_SOC_PORTS)) << PCTL_LINK_OFFS;

	if (!(mvi->flags & MVF_FLAG_SOC)) {
		if (phy_id < MVS_SOC_PORTS) {
			pci_write_config_dword(mvi->pdev, PCR_PHY_CTL, tmp);
			mdelay(10);
			pci_write_config_dword(mvi->pdev, PCR_PHY_CTL, reg);
		} else {
			pci_write_config_dword(mvi->pdev, PCR_PHY_CTL2, tmp);
			mdelay(10);
			pci_write_config_dword(mvi->pdev, PCR_PHY_CTL2, reg);
		}
	} else {
		mw32(MVS_PHY_CTL, tmp);
		mdelay(10);
		mw32(MVS_PHY_CTL, reg);
	}
}

static void mvs_64xx_phy_reset(struct mvs_info *mvi, u32 phy_id, int hard)
{
	u32 tmp;
	tmp = mvs_read_port_irq_stat(mvi, phy_id);
	tmp &= ~PHYEV_RDY_CH;
	mvs_write_port_irq_stat(mvi, phy_id, tmp);
	tmp = mvs_read_phy_ctl(mvi, phy_id);
	if (hard == MVS_HARD_RESET)
		tmp |= PHY_RST_HARD;
	else if (hard == MVS_SOFT_RESET)
		tmp |= PHY_RST;
	mvs_write_phy_ctl(mvi, phy_id, tmp);
	if (hard) {
		do {
			tmp = mvs_read_phy_ctl(mvi, phy_id);
		} while (tmp & PHY_RST_HARD);
	}
}

static void
mvs_64xx_clear_srs_irq(struct mvs_info *mvi, u8 reg_set, u8 clear_all)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;
	if (clear_all) {
		tmp = mr32(MVS_INT_STAT_SRS_0);
		if (tmp) {
			printk(KERN_DEBUG "check SRS 0 %08X.\n", tmp);
			mw32(MVS_INT_STAT_SRS_0, tmp);
		}
	} else {
		tmp = mr32(MVS_INT_STAT_SRS_0);
		if (tmp &  (1 << (reg_set % 32))) {
			printk(KERN_DEBUG "register set 0x%x was stopped.\n",
			       reg_set);
			mw32(MVS_INT_STAT_SRS_0, 1 << (reg_set % 32));
		}
	}
}

static int mvs_64xx_chip_reset(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;
	int i;

	/* make sure interrupts are masked immediately (paranoia) */
	mw32(MVS_GBL_CTL, 0);
	tmp = mr32(MVS_GBL_CTL);

	/* Reset Controller */
	if (!(tmp & HBA_RST)) {
		if (mvi->flags & MVF_PHY_PWR_FIX) {
			pci_read_config_dword(mvi->pdev, PCR_PHY_CTL, &tmp);
			tmp &= ~PCTL_PWR_OFF;
			tmp |= PCTL_PHY_DSBL;
			pci_write_config_dword(mvi->pdev, PCR_PHY_CTL, tmp);

			pci_read_config_dword(mvi->pdev, PCR_PHY_CTL2, &tmp);
			tmp &= ~PCTL_PWR_OFF;
			tmp |= PCTL_PHY_DSBL;
			pci_write_config_dword(mvi->pdev, PCR_PHY_CTL2, tmp);
		}
	}

	/* make sure interrupts are masked immediately (paranoia) */
	mw32(MVS_GBL_CTL, 0);
	tmp = mr32(MVS_GBL_CTL);

	/* Reset Controller */
	if (!(tmp & HBA_RST)) {
		/* global reset, incl. COMRESET/H_RESET_N (self-clearing) */
		mw32_f(MVS_GBL_CTL, HBA_RST);
	}

	/* wait for reset to finish; timeout is just a guess */
	i = 1000;
	while (i-- > 0) {
		msleep(10);

		if (!(mr32(MVS_GBL_CTL) & HBA_RST))
			break;
	}
	if (mr32(MVS_GBL_CTL) & HBA_RST) {
		dev_printk(KERN_ERR, mvi->dev, "HBA reset failed\n");
		return -EBUSY;
	}
	return 0;
}

static void mvs_64xx_phy_disable(struct mvs_info *mvi, u32 phy_id)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;
	if (!(mvi->flags & MVF_FLAG_SOC)) {
		u32 offs;
		if (phy_id < 4)
			offs = PCR_PHY_CTL;
		else {
			offs = PCR_PHY_CTL2;
			phy_id -= 4;
		}
		pci_read_config_dword(mvi->pdev, offs, &tmp);
		tmp |= 1U << (PCTL_PHY_DSBL_OFFS + phy_id);
		pci_write_config_dword(mvi->pdev, offs, tmp);
	} else {
		tmp = mr32(MVS_PHY_CTL);
		tmp |= 1U << (PCTL_PHY_DSBL_OFFS + phy_id);
		mw32(MVS_PHY_CTL, tmp);
	}
}

static void mvs_64xx_phy_enable(struct mvs_info *mvi, u32 phy_id)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;
	if (!(mvi->flags & MVF_FLAG_SOC)) {
		u32 offs;
		if (phy_id < 4)
			offs = PCR_PHY_CTL;
		else {
			offs = PCR_PHY_CTL2;
			phy_id -= 4;
		}
		pci_read_config_dword(mvi->pdev, offs, &tmp);
		tmp &= ~(1U << (PCTL_PHY_DSBL_OFFS + phy_id));
		pci_write_config_dword(mvi->pdev, offs, tmp);
	} else {
		tmp = mr32(MVS_PHY_CTL);
		tmp &= ~(1U << (PCTL_PHY_DSBL_OFFS + phy_id));
		mw32(MVS_PHY_CTL, tmp);
	}
}

static int mvs_64xx_init(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	int i;
	u32 tmp, cctl;

	if (mvi->pdev && mvi->pdev->revision == 0)
		mvi->flags |= MVF_PHY_PWR_FIX;
	if (!(mvi->flags & MVF_FLAG_SOC)) {
		mvs_show_pcie_usage(mvi);
		tmp = mvs_64xx_chip_reset(mvi);
		if (tmp)
			return tmp;
	} else {
		tmp = mr32(MVS_PHY_CTL);
		tmp &= ~PCTL_PWR_OFF;
		tmp |= PCTL_PHY_DSBL;
		mw32(MVS_PHY_CTL, tmp);
	}

	/* Init Chip */
	/* make sure RST is set; HBA_RST /should/ have done that for us */
	cctl = mr32(MVS_CTL) & 0xFFFF;
	if (cctl & CCTL_RST)
		cctl &= ~CCTL_RST;
	else
		mw32_f(MVS_CTL, cctl | CCTL_RST);

	if (!(mvi->flags & MVF_FLAG_SOC)) {
		/* write to device control _AND_ device status register */
		pci_read_config_dword(mvi->pdev, PCR_DEV_CTRL, &tmp);
		tmp &= ~PRD_REQ_MASK;
		tmp |= PRD_REQ_SIZE;
		pci_write_config_dword(mvi->pdev, PCR_DEV_CTRL, tmp);

		pci_read_config_dword(mvi->pdev, PCR_PHY_CTL, &tmp);
		tmp &= ~PCTL_PWR_OFF;
		tmp &= ~PCTL_PHY_DSBL;
		pci_write_config_dword(mvi->pdev, PCR_PHY_CTL, tmp);

		pci_read_config_dword(mvi->pdev, PCR_PHY_CTL2, &tmp);
		tmp &= PCTL_PWR_OFF;
		tmp &= ~PCTL_PHY_DSBL;
		pci_write_config_dword(mvi->pdev, PCR_PHY_CTL2, tmp);
	} else {
		tmp = mr32(MVS_PHY_CTL);
		tmp &= ~PCTL_PWR_OFF;
		tmp |= PCTL_COM_ON;
		tmp &= ~PCTL_PHY_DSBL;
		tmp |= PCTL_LINK_RST;
		mw32(MVS_PHY_CTL, tmp);
		msleep(100);
		tmp &= ~PCTL_LINK_RST;
		mw32(MVS_PHY_CTL, tmp);
		msleep(100);
	}

	/* reset control */
	mw32(MVS_PCS, 0);		/* MVS_PCS */
	/* init phys */
	mvs_64xx_phy_hacks(mvi);

	tmp = mvs_cr32(mvi, CMD_PHY_MODE_21);
	tmp &= 0x0000ffff;
	tmp |= 0x00fa0000;
	mvs_cw32(mvi, CMD_PHY_MODE_21, tmp);

	/* enable auto port detection */
	mw32(MVS_GBL_PORT_TYPE, MODE_AUTO_DET_EN);

	mw32(MVS_CMD_LIST_LO, mvi->slot_dma);
	mw32(MVS_CMD_LIST_HI, (mvi->slot_dma >> 16) >> 16);

	mw32(MVS_RX_FIS_LO, mvi->rx_fis_dma);
	mw32(MVS_RX_FIS_HI, (mvi->rx_fis_dma >> 16) >> 16);

	mw32(MVS_TX_CFG, MVS_CHIP_SLOT_SZ);
	mw32(MVS_TX_LO, mvi->tx_dma);
	mw32(MVS_TX_HI, (mvi->tx_dma >> 16) >> 16);

	mw32(MVS_RX_CFG, MVS_RX_RING_SZ);
	mw32(MVS_RX_LO, mvi->rx_dma);
	mw32(MVS_RX_HI, (mvi->rx_dma >> 16) >> 16);

	for (i = 0; i < mvi->chip->n_phy; i++) {
		/* set phy local SAS address */
		/* should set little endian SAS address to 64xx chip */
		mvs_set_sas_addr(mvi, i, PHYR_ADDR_LO, PHYR_ADDR_HI,
				cpu_to_be64(mvi->phy[i].dev_sas_addr));

		mvs_64xx_enable_xmt(mvi, i);

		mvs_64xx_phy_reset(mvi, i, MVS_HARD_RESET);
		msleep(500);
		mvs_64xx_detect_porttype(mvi, i);
	}
	if (mvi->flags & MVF_FLAG_SOC) {
		/* set select registers */
		writel(0x0E008000, regs + 0x000);
		writel(0x59000008, regs + 0x004);
		writel(0x20, regs + 0x008);
		writel(0x20, regs + 0x00c);
		writel(0x20, regs + 0x010);
		writel(0x20, regs + 0x014);
		writel(0x20, regs + 0x018);
		writel(0x20, regs + 0x01c);
	}
	for (i = 0; i < mvi->chip->n_phy; i++) {
		/* clear phy int status */
		tmp = mvs_read_port_irq_stat(mvi, i);
		tmp &= ~PHYEV_SIG_FIS;
		mvs_write_port_irq_stat(mvi, i, tmp);

		/* set phy int mask */
		tmp = PHYEV_RDY_CH | PHYEV_BROAD_CH | PHYEV_UNASSOC_FIS |
			PHYEV_ID_DONE | PHYEV_DCDR_ERR | PHYEV_CRC_ERR |
			PHYEV_DEC_ERR;
		mvs_write_port_irq_mask(mvi, i, tmp);

		msleep(100);
		mvs_update_phyinfo(mvi, i, 1);
	}

	/* little endian for open address and command table, etc. */
	cctl = mr32(MVS_CTL);
	cctl |= CCTL_ENDIAN_CMD;
	cctl |= CCTL_ENDIAN_DATA;
	cctl &= ~CCTL_ENDIAN_OPEN;
	cctl |= CCTL_ENDIAN_RSP;
	mw32_f(MVS_CTL, cctl);

	/* reset CMD queue */
	tmp = mr32(MVS_PCS);
	tmp |= PCS_CMD_RST;
	tmp &= ~PCS_SELF_CLEAR;
	mw32(MVS_PCS, tmp);
	/*
	 * the max count is 0x1ff, while our max slot is 0x200,
	 * it will make count 0.
	 */
	tmp = 0;
	if (MVS_CHIP_SLOT_SZ > 0x1ff)
		mw32(MVS_INT_COAL, 0x1ff | COAL_EN);
	else
		mw32(MVS_INT_COAL, MVS_CHIP_SLOT_SZ | COAL_EN);

	tmp = 0x10000 | interrupt_coalescing;
	mw32(MVS_INT_COAL_TMOUT, tmp);

	/* ladies and gentlemen, start your engines */
	mw32(MVS_TX_CFG, 0);
	mw32(MVS_TX_CFG, MVS_CHIP_SLOT_SZ | TX_EN);
	mw32(MVS_RX_CFG, MVS_RX_RING_SZ | RX_EN);
	/* enable CMD/CMPL_Q/RESP mode */
	mw32(MVS_PCS, PCS_SATA_RETRY | PCS_FIS_RX_EN |
		PCS_CMD_EN | PCS_CMD_STOP_ERR);

	/* enable completion queue interrupt */
	tmp = (CINT_PORT_MASK | CINT_DONE | CINT_MEM | CINT_SRS | CINT_CI_STOP |
		CINT_DMA_PCIE);

	mw32(MVS_INT_MASK, tmp);

	/* Enable SRS interrupt */
	mw32(MVS_INT_MASK_SRS_0, 0xFFFF);

	return 0;
}

static int mvs_64xx_ioremap(struct mvs_info *mvi)
{
	if (!mvs_ioremap(mvi, 4, 2))
		return 0;
	return -1;
}

static void mvs_64xx_iounmap(struct mvs_info *mvi)
{
	mvs_iounmap(mvi->regs);
	mvs_iounmap(mvi->regs_ex);
}

static void mvs_64xx_interrupt_enable(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	tmp = mr32(MVS_GBL_CTL);
	mw32(MVS_GBL_CTL, tmp | INT_EN);
}

static void mvs_64xx_interrupt_disable(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	tmp = mr32(MVS_GBL_CTL);
	mw32(MVS_GBL_CTL, tmp & ~INT_EN);
}

static u32 mvs_64xx_isr_status(struct mvs_info *mvi, int irq)
{
	void __iomem *regs = mvi->regs;
	u32 stat;

	if (!(mvi->flags & MVF_FLAG_SOC)) {
		stat = mr32(MVS_GBL_INT_STAT);

		if (stat == 0 || stat == 0xffffffff)
			return 0;
	} else
		stat = 1;
	return stat;
}

static irqreturn_t mvs_64xx_isr(struct mvs_info *mvi, int irq, u32 stat)
{
	void __iomem *regs = mvi->regs;

	/* clear CMD_CMPLT ASAP */
	mw32_f(MVS_INT_STAT, CINT_DONE);

	spin_lock(&mvi->lock);
	mvs_int_full(mvi);
	spin_unlock(&mvi->lock);

	return IRQ_HANDLED;
}

static void mvs_64xx_command_active(struct mvs_info *mvi, u32 slot_idx)
{
	u32 tmp;
	mvs_cw32(mvi, 0x40 + (slot_idx >> 3), 1 << (slot_idx % 32));
	mvs_cw32(mvi, 0x00 + (slot_idx >> 3), 1 << (slot_idx % 32));
	do {
		tmp = mvs_cr32(mvi, 0x00 + (slot_idx >> 3));
	} while (tmp & 1 << (slot_idx % 32));
	do {
		tmp = mvs_cr32(mvi, 0x40 + (slot_idx >> 3));
	} while (tmp & 1 << (slot_idx % 32));
}

static void mvs_64xx_issue_stop(struct mvs_info *mvi, enum mvs_port_type type,
				u32 tfs)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	if (type == PORT_TYPE_SATA) {
		tmp = mr32(MVS_INT_STAT_SRS_0) | (1U << tfs);
		mw32(MVS_INT_STAT_SRS_0, tmp);
	}
	mw32(MVS_INT_STAT, CINT_CI_STOP);
	tmp = mr32(MVS_PCS) | 0xFF00;
	mw32(MVS_PCS, tmp);
}

static void mvs_64xx_free_reg_set(struct mvs_info *mvi, u8 *tfs)
{
	void __iomem *regs = mvi->regs;
	u32 tmp, offs;

	if (*tfs == MVS_ID_NOT_MAPPED)
		return;

	offs = 1U << ((*tfs & 0x0f) + PCS_EN_SATA_REG_SHIFT);
	if (*tfs < 16) {
		tmp = mr32(MVS_PCS);
		mw32(MVS_PCS, tmp & ~offs);
	} else {
		tmp = mr32(MVS_CTL);
		mw32(MVS_CTL, tmp & ~offs);
	}

	tmp = mr32(MVS_INT_STAT_SRS_0) & (1U << *tfs);
	if (tmp)
		mw32(MVS_INT_STAT_SRS_0, tmp);

	*tfs = MVS_ID_NOT_MAPPED;
	return;
}

static u8 mvs_64xx_assign_reg_set(struct mvs_info *mvi, u8 *tfs)
{
	int i;
	u32 tmp, offs;
	void __iomem *regs = mvi->regs;

	if (*tfs != MVS_ID_NOT_MAPPED)
		return 0;

	tmp = mr32(MVS_PCS);

	for (i = 0; i < mvi->chip->srs_sz; i++) {
		if (i == 16)
			tmp = mr32(MVS_CTL);
		offs = 1U << ((i & 0x0f) + PCS_EN_SATA_REG_SHIFT);
		if (!(tmp & offs)) {
			*tfs = i;

			if (i < 16)
				mw32(MVS_PCS, tmp | offs);
			else
				mw32(MVS_CTL, tmp | offs);
			tmp = mr32(MVS_INT_STAT_SRS_0) & (1U << i);
			if (tmp)
				mw32(MVS_INT_STAT_SRS_0, tmp);
			return 0;
		}
	}
	return MVS_ID_NOT_MAPPED;
}

static void mvs_64xx_make_prd(struct scatterlist *scatter, int nr, void *prd)
{
	int i;
	struct scatterlist *sg;
	struct mvs_prd *buf_prd = prd;
	for_each_sg(scatter, sg, nr, i) {
		buf_prd->addr = cpu_to_le64(sg_dma_address(sg));
		buf_prd->len = cpu_to_le32(sg_dma_len(sg));
		buf_prd++;
	}
}

static int mvs_64xx_oob_done(struct mvs_info *mvi, int i)
{
	u32 phy_st;
	mvs_write_port_cfg_addr(mvi, i,
			PHYR_PHY_STAT);
	phy_st = mvs_read_port_cfg_data(mvi, i);
	if (phy_st & PHY_OOB_DTCTD)
		return 1;
	return 0;
}

static void mvs_64xx_fix_phy_info(struct mvs_info *mvi, int i,
				struct sas_identify_frame *id)

{
	struct mvs_phy *phy = &mvi->phy[i];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;

	sas_phy->linkrate =
		(phy->phy_status & PHY_NEG_SPP_PHYS_LINK_RATE_MASK) >>
			PHY_NEG_SPP_PHYS_LINK_RATE_MASK_OFFSET;

	phy->minimum_linkrate =
		(phy->phy_status &
			PHY_MIN_SPP_PHYS_LINK_RATE_MASK) >> 8;
	phy->maximum_linkrate =
		(phy->phy_status &
			PHY_MAX_SPP_PHYS_LINK_RATE_MASK) >> 12;

	mvs_write_port_cfg_addr(mvi, i, PHYR_IDENTIFY);
	phy->dev_info = mvs_read_port_cfg_data(mvi, i);

	mvs_write_port_cfg_addr(mvi, i, PHYR_ATT_DEV_INFO);
	phy->att_dev_info = mvs_read_port_cfg_data(mvi, i);

	mvs_write_port_cfg_addr(mvi, i, PHYR_ATT_ADDR_HI);
	phy->att_dev_sas_addr =
	     (u64) mvs_read_port_cfg_data(mvi, i) << 32;
	mvs_write_port_cfg_addr(mvi, i, PHYR_ATT_ADDR_LO);
	phy->att_dev_sas_addr |= mvs_read_port_cfg_data(mvi, i);
	phy->att_dev_sas_addr = SAS_ADDR(&phy->att_dev_sas_addr);
}

static void mvs_64xx_phy_work_around(struct mvs_info *mvi, int i)
{
	u32 tmp;
	struct mvs_phy *phy = &mvi->phy[i];
	mvs_write_port_vsr_addr(mvi, i, VSR_PHY_MODE6);
	tmp = mvs_read_port_vsr_data(mvi, i);
	if (((phy->phy_status & PHY_NEG_SPP_PHYS_LINK_RATE_MASK) >>
	     PHY_NEG_SPP_PHYS_LINK_RATE_MASK_OFFSET) ==
		SAS_LINK_RATE_1_5_GBPS)
		tmp &= ~PHY_MODE6_LATECLK;
	else
		tmp |= PHY_MODE6_LATECLK;
	mvs_write_port_vsr_data(mvi, i, tmp);
}

static void mvs_64xx_phy_set_link_rate(struct mvs_info *mvi, u32 phy_id,
			struct sas_phy_linkrates *rates)
{
	u32 lrmin = 0, lrmax = 0;
	u32 tmp;

	tmp = mvs_read_phy_ctl(mvi, phy_id);
	lrmin = (rates->minimum_linkrate << 8);
	lrmax = (rates->maximum_linkrate << 12);

	if (lrmin) {
		tmp &= ~(0xf << 8);
		tmp |= lrmin;
	}
	if (lrmax) {
		tmp &= ~(0xf << 12);
		tmp |= lrmax;
	}
	mvs_write_phy_ctl(mvi, phy_id, tmp);
	mvs_64xx_phy_reset(mvi, phy_id, MVS_HARD_RESET);
}

static void mvs_64xx_clear_active_cmds(struct mvs_info *mvi)
{
	u32 tmp;
	void __iomem *regs = mvi->regs;
	tmp = mr32(MVS_PCS);
	mw32(MVS_PCS, tmp & 0xFFFF);
	mw32(MVS_PCS, tmp);
	tmp = mr32(MVS_CTL);
	mw32(MVS_CTL, tmp & 0xFFFF);
	mw32(MVS_CTL, tmp);
}


static u32 mvs_64xx_spi_read_data(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs_ex;
	return ior32(SPI_DATA_REG_64XX);
}

static void mvs_64xx_spi_write_data(struct mvs_info *mvi, u32 data)
{
	void __iomem *regs = mvi->regs_ex;
	 iow32(SPI_DATA_REG_64XX, data);
}


static int mvs_64xx_spi_buildcmd(struct mvs_info *mvi,
			u32      *dwCmd,
			u8       cmd,
			u8       read,
			u8       length,
			u32      addr
			)
{
	u32  dwTmp;

	dwTmp = ((u32)cmd << 24) | ((u32)length << 19);
	if (read)
		dwTmp |= 1U<<23;

	if (addr != MV_MAX_U32) {
		dwTmp |= 1U<<22;
		dwTmp |= (addr & 0x0003FFFF);
	}

	*dwCmd = dwTmp;
	return 0;
}


static int mvs_64xx_spi_issuecmd(struct mvs_info *mvi, u32 cmd)
{
	void __iomem *regs = mvi->regs_ex;
	int     retry;

	for (retry = 0; retry < 1; retry++) {
		iow32(SPI_CTRL_REG_64XX, SPI_CTRL_VENDOR_ENABLE);
		iow32(SPI_CMD_REG_64XX, cmd);
		iow32(SPI_CTRL_REG_64XX,
			SPI_CTRL_VENDOR_ENABLE | SPI_CTRL_SPISTART);
	}

	return 0;
}

static int mvs_64xx_spi_waitdataready(struct mvs_info *mvi, u32 timeout)
{
	void __iomem *regs = mvi->regs_ex;
	u32 i, dwTmp;

	for (i = 0; i < timeout; i++) {
		dwTmp = ior32(SPI_CTRL_REG_64XX);
		if (!(dwTmp & SPI_CTRL_SPISTART))
			return 0;
		msleep(10);
	}

	return -1;
}

static void mvs_64xx_fix_dma(struct mvs_info *mvi, u32 phy_mask,
				int buf_len, int from, void *prd)
{
	int i;
	struct mvs_prd *buf_prd = prd;
	dma_addr_t buf_dma = mvi->bulk_buffer_dma;

	buf_prd	+= from;
	for (i = 0; i < MAX_SG_ENTRY - from; i++) {
		buf_prd->addr = cpu_to_le64(buf_dma);
		buf_prd->len = cpu_to_le32(buf_len);
		++buf_prd;
	}
}

static void mvs_64xx_tune_interrupt(struct mvs_info *mvi, u32 time)
{
	void __iomem *regs = mvi->regs;
	u32 tmp = 0;
	/*
	 * the max count is 0x1ff, while our max slot is 0x200,
	 * it will make count 0.
	 */
	if (time == 0) {
		mw32(MVS_INT_COAL, 0);
		mw32(MVS_INT_COAL_TMOUT, 0x10000);
	} else {
		if (MVS_CHIP_SLOT_SZ > 0x1ff)
			mw32(MVS_INT_COAL, 0x1ff|COAL_EN);
		else
			mw32(MVS_INT_COAL, MVS_CHIP_SLOT_SZ|COAL_EN);

		tmp = 0x10000 | time;
		mw32(MVS_INT_COAL_TMOUT, tmp);
	}
}

const struct mvs_dispatch mvs_64xx_dispatch = {
	"mv64xx",
	mvs_64xx_init,
	NULL,
	mvs_64xx_ioremap,
	mvs_64xx_iounmap,
	mvs_64xx_isr,
	mvs_64xx_isr_status,
	mvs_64xx_interrupt_enable,
	mvs_64xx_interrupt_disable,
	mvs_read_phy_ctl,
	mvs_write_phy_ctl,
	mvs_read_port_cfg_data,
	mvs_write_port_cfg_data,
	mvs_write_port_cfg_addr,
	mvs_read_port_vsr_data,
	mvs_write_port_vsr_data,
	mvs_write_port_vsr_addr,
	mvs_read_port_irq_stat,
	mvs_write_port_irq_stat,
	mvs_read_port_irq_mask,
	mvs_write_port_irq_mask,
	mvs_64xx_command_active,
	mvs_64xx_clear_srs_irq,
	mvs_64xx_issue_stop,
	mvs_start_delivery,
	mvs_rx_update,
	mvs_int_full,
	mvs_64xx_assign_reg_set,
	mvs_64xx_free_reg_set,
	mvs_get_prd_size,
	mvs_get_prd_count,
	mvs_64xx_make_prd,
	mvs_64xx_detect_porttype,
	mvs_64xx_oob_done,
	mvs_64xx_fix_phy_info,
	mvs_64xx_phy_work_around,
	mvs_64xx_phy_set_link_rate,
	mvs_hw_max_link_rate,
	mvs_64xx_phy_disable,
	mvs_64xx_phy_enable,
	mvs_64xx_phy_reset,
	mvs_64xx_stp_reset,
	mvs_64xx_clear_active_cmds,
	mvs_64xx_spi_read_data,
	mvs_64xx_spi_write_data,
	mvs_64xx_spi_buildcmd,
	mvs_64xx_spi_issuecmd,
	mvs_64xx_spi_waitdataready,
	mvs_64xx_fix_dma,
	mvs_64xx_tune_interrupt,
	NULL,
};

