/*
 * Marvell 88SE94xx hardware specific
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
#include "mv_94xx.h"
#include "mv_chips.h"

static void mvs_94xx_detect_porttype(struct mvs_info *mvi, int i)
{
	u32 reg;
	struct mvs_phy *phy = &mvi->phy[i];
	u32 phy_status;

	mvs_write_port_vsr_addr(mvi, i, VSR_PHY_MODE3);
	reg = mvs_read_port_vsr_data(mvi, i);
	phy_status = ((reg & 0x3f0000) >> 16) & 0xff;
	phy->phy_type &= ~(PORT_TYPE_SAS | PORT_TYPE_SATA);
	switch (phy_status) {
	case 0x10:
		phy->phy_type |= PORT_TYPE_SAS;
		break;
	case 0x1d:
	default:
		phy->phy_type |= PORT_TYPE_SATA;
		break;
	}
}

void set_phy_tuning(struct mvs_info *mvi, int phy_id,
			struct phy_tuning phy_tuning)
{
	u32 tmp, setting_0 = 0, setting_1 = 0;
	u8 i;

	/* Remap information for B0 chip:
	*
	* R0Ch -> R118h[15:0] (Adapted DFE F3 - F5 coefficient)
	* R0Dh -> R118h[31:16] (Generation 1 Setting 0)
	* R0Eh -> R11Ch[15:0]  (Generation 1 Setting 1)
	* R0Fh -> R11Ch[31:16] (Generation 2 Setting 0)
	* R10h -> R120h[15:0]  (Generation 2 Setting 1)
	* R11h -> R120h[31:16] (Generation 3 Setting 0)
	* R12h -> R124h[15:0]  (Generation 3 Setting 1)
	* R13h -> R124h[31:16] (Generation 4 Setting 0 (Reserved))
	*/

	/* A0 has a different set of registers */
	if (mvi->pdev->revision == VANIR_A0_REV)
		return;

	for (i = 0; i < 3; i++) {
		/* loop 3 times, set Gen 1, Gen 2, Gen 3 */
		switch (i) {
		case 0:
			setting_0 = GENERATION_1_SETTING;
			setting_1 = GENERATION_1_2_SETTING;
			break;
		case 1:
			setting_0 = GENERATION_1_2_SETTING;
			setting_1 = GENERATION_2_3_SETTING;
			break;
		case 2:
			setting_0 = GENERATION_2_3_SETTING;
			setting_1 = GENERATION_3_4_SETTING;
			break;
		}

		/* Set:
		*
		* Transmitter Emphasis Enable
		* Transmitter Emphasis Amplitude
		* Transmitter Amplitude
		*/
		mvs_write_port_vsr_addr(mvi, phy_id, setting_0);
		tmp = mvs_read_port_vsr_data(mvi, phy_id);
		tmp &= ~(0xFBE << 16);
		tmp |= (((phy_tuning.trans_emp_en << 11) |
			(phy_tuning.trans_emp_amp << 7) |
			(phy_tuning.trans_amp << 1)) << 16);
		mvs_write_port_vsr_data(mvi, phy_id, tmp);

		/* Set Transmitter Amplitude Adjust */
		mvs_write_port_vsr_addr(mvi, phy_id, setting_1);
		tmp = mvs_read_port_vsr_data(mvi, phy_id);
		tmp &= ~(0xC000);
		tmp |= (phy_tuning.trans_amp_adj << 14);
		mvs_write_port_vsr_data(mvi, phy_id, tmp);
	}
}

void set_phy_ffe_tuning(struct mvs_info *mvi, int phy_id,
				struct ffe_control ffe)
{
	u32 tmp;

	/* Don't run this if A0/B0 */
	if ((mvi->pdev->revision == VANIR_A0_REV)
		|| (mvi->pdev->revision == VANIR_B0_REV))
		return;

	/* FFE Resistor and Capacitor */
	/* R10Ch DFE Resolution Control/Squelch and FFE Setting
	 *
	 * FFE_FORCE            [7]
	 * FFE_RES_SEL          [6:4]
	 * FFE_CAP_SEL          [3:0]
	 */
	mvs_write_port_vsr_addr(mvi, phy_id, VSR_PHY_FFE_CONTROL);
	tmp = mvs_read_port_vsr_data(mvi, phy_id);
	tmp &= ~0xFF;

	/* Read from HBA_Info_Page */
	tmp |= ((0x1 << 7) |
		(ffe.ffe_rss_sel << 4) |
		(ffe.ffe_cap_sel << 0));

	mvs_write_port_vsr_data(mvi, phy_id, tmp);

	/* R064h PHY Mode Register 1
	 *
	 * DFE_DIS		18
	 */
	mvs_write_port_vsr_addr(mvi, phy_id, VSR_REF_CLOCK_CRTL);
	tmp = mvs_read_port_vsr_data(mvi, phy_id);
	tmp &= ~0x40001;
	/* Hard coding */
	/* No defines in HBA_Info_Page */
	tmp |= (0 << 18);
	mvs_write_port_vsr_data(mvi, phy_id, tmp);

	/* R110h DFE F0-F1 Coefficient Control/DFE Update Control
	 *
	 * DFE_UPDATE_EN        [11:6]
	 * DFE_FX_FORCE         [5:0]
	 */
	mvs_write_port_vsr_addr(mvi, phy_id, VSR_PHY_DFE_UPDATE_CRTL);
	tmp = mvs_read_port_vsr_data(mvi, phy_id);
	tmp &= ~0xFFF;
	/* Hard coding */
	/* No defines in HBA_Info_Page */
	tmp |= ((0x3F << 6) | (0x0 << 0));
	mvs_write_port_vsr_data(mvi, phy_id, tmp);

	/* R1A0h Interface and Digital Reference Clock Control/Reserved_50h
	 *
	 * FFE_TRAIN_EN         3
	 */
	mvs_write_port_vsr_addr(mvi, phy_id, VSR_REF_CLOCK_CRTL);
	tmp = mvs_read_port_vsr_data(mvi, phy_id);
	tmp &= ~0x8;
	/* Hard coding */
	/* No defines in HBA_Info_Page */
	tmp |= (0 << 3);
	mvs_write_port_vsr_data(mvi, phy_id, tmp);
}

/*Notice: this function must be called when phy is disabled*/
void set_phy_rate(struct mvs_info *mvi, int phy_id, u8 rate)
{
	union reg_phy_cfg phy_cfg, phy_cfg_tmp;
	mvs_write_port_vsr_addr(mvi, phy_id, VSR_PHY_MODE2);
	phy_cfg_tmp.v = mvs_read_port_vsr_data(mvi, phy_id);
	phy_cfg.v = 0;
	phy_cfg.u.disable_phy = phy_cfg_tmp.u.disable_phy;
	phy_cfg.u.sas_support = 1;
	phy_cfg.u.sata_support = 1;
	phy_cfg.u.sata_host_mode = 1;

	switch (rate) {
	case 0x0:
		/* support 1.5 Gbps */
		phy_cfg.u.speed_support = 1;
		phy_cfg.u.snw_3_support = 0;
		phy_cfg.u.tx_lnk_parity = 1;
		phy_cfg.u.tx_spt_phs_lnk_rate = 0x30;
		break;
	case 0x1:

		/* support 1.5, 3.0 Gbps */
		phy_cfg.u.speed_support = 3;
		phy_cfg.u.tx_spt_phs_lnk_rate = 0x3c;
		phy_cfg.u.tx_lgcl_lnk_rate = 0x08;
		break;
	case 0x2:
	default:
		/* support 1.5, 3.0, 6.0 Gbps */
		phy_cfg.u.speed_support = 7;
		phy_cfg.u.snw_3_support = 1;
		phy_cfg.u.tx_lnk_parity = 1;
		phy_cfg.u.tx_spt_phs_lnk_rate = 0x3f;
		phy_cfg.u.tx_lgcl_lnk_rate = 0x09;
		break;
	}
	mvs_write_port_vsr_data(mvi, phy_id, phy_cfg.v);
}

static void mvs_94xx_config_reg_from_hba(struct mvs_info *mvi, int phy_id)
{
	u32 temp;
	temp = (u32)(*(u32 *)&mvi->hba_info_param.phy_tuning[phy_id]);
	if (temp == 0xFFFFFFFFL) {
		mvi->hba_info_param.phy_tuning[phy_id].trans_emp_amp = 0x6;
		mvi->hba_info_param.phy_tuning[phy_id].trans_amp = 0x1A;
		mvi->hba_info_param.phy_tuning[phy_id].trans_amp_adj = 0x3;
	}

	temp = (u8)(*(u8 *)&mvi->hba_info_param.ffe_ctl[phy_id]);
	if (temp == 0xFFL) {
		switch (mvi->pdev->revision) {
		case VANIR_A0_REV:
		case VANIR_B0_REV:
			mvi->hba_info_param.ffe_ctl[phy_id].ffe_rss_sel = 0x7;
			mvi->hba_info_param.ffe_ctl[phy_id].ffe_cap_sel = 0x7;
			break;
		case VANIR_C0_REV:
		case VANIR_C1_REV:
		case VANIR_C2_REV:
		default:
			mvi->hba_info_param.ffe_ctl[phy_id].ffe_rss_sel = 0x7;
			mvi->hba_info_param.ffe_ctl[phy_id].ffe_cap_sel = 0xC;
			break;
		}
	}

	temp = (u8)(*(u8 *)&mvi->hba_info_param.phy_rate[phy_id]);
	if (temp == 0xFFL)
		/*set default phy_rate = 6Gbps*/
		mvi->hba_info_param.phy_rate[phy_id] = 0x2;

	set_phy_tuning(mvi, phy_id,
		mvi->hba_info_param.phy_tuning[phy_id]);
	set_phy_ffe_tuning(mvi, phy_id,
		mvi->hba_info_param.ffe_ctl[phy_id]);
	set_phy_rate(mvi, phy_id,
		mvi->hba_info_param.phy_rate[phy_id]);
}

static void mvs_94xx_enable_xmt(struct mvs_info *mvi, int phy_id)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	tmp = mr32(MVS_PCS);
	tmp |= 1 << (phy_id + PCS_EN_PORT_XMT_SHIFT2);
	mw32(MVS_PCS, tmp);
}

static void mvs_94xx_phy_reset(struct mvs_info *mvi, u32 phy_id, int hard)
{
	u32 tmp;
	u32 delay = 5000;
	if (hard == MVS_PHY_TUNE) {
		mvs_write_port_cfg_addr(mvi, phy_id, PHYR_SATA_CTL);
		tmp = mvs_read_port_cfg_data(mvi, phy_id);
		mvs_write_port_cfg_data(mvi, phy_id, tmp|0x20000000);
		mvs_write_port_cfg_data(mvi, phy_id, tmp|0x100000);
		return;
	}
	tmp = mvs_read_port_irq_stat(mvi, phy_id);
	tmp &= ~PHYEV_RDY_CH;
	mvs_write_port_irq_stat(mvi, phy_id, tmp);
	if (hard) {
		tmp = mvs_read_phy_ctl(mvi, phy_id);
		tmp |= PHY_RST_HARD;
		mvs_write_phy_ctl(mvi, phy_id, tmp);
		do {
			tmp = mvs_read_phy_ctl(mvi, phy_id);
			udelay(10);
			delay--;
		} while ((tmp & PHY_RST_HARD) && delay);
		if (!delay)
			mv_dprintk("phy hard reset failed.\n");
	} else {
		tmp = mvs_read_phy_ctl(mvi, phy_id);
		tmp |= PHY_RST;
		mvs_write_phy_ctl(mvi, phy_id, tmp);
	}
}

static void mvs_94xx_phy_disable(struct mvs_info *mvi, u32 phy_id)
{
	u32 tmp;
	mvs_write_port_vsr_addr(mvi, phy_id, VSR_PHY_MODE2);
	tmp = mvs_read_port_vsr_data(mvi, phy_id);
	mvs_write_port_vsr_data(mvi, phy_id, tmp | 0x00800000);
}

static void mvs_94xx_phy_enable(struct mvs_info *mvi, u32 phy_id)
{
	u32 tmp;
	u8 revision = 0;

	revision = mvi->pdev->revision;
	if (revision == VANIR_A0_REV) {
		mvs_write_port_vsr_addr(mvi, phy_id, CMD_HOST_RD_DATA);
		mvs_write_port_vsr_data(mvi, phy_id, 0x8300ffc1);
	}
	if (revision == VANIR_B0_REV) {
		mvs_write_port_vsr_addr(mvi, phy_id, CMD_APP_MEM_CTL);
		mvs_write_port_vsr_data(mvi, phy_id, 0x08001006);
		mvs_write_port_vsr_addr(mvi, phy_id, CMD_HOST_RD_DATA);
		mvs_write_port_vsr_data(mvi, phy_id, 0x0000705f);
	}

	mvs_write_port_vsr_addr(mvi, phy_id, VSR_PHY_MODE2);
	tmp = mvs_read_port_vsr_data(mvi, phy_id);
	tmp |= bit(0);
	mvs_write_port_vsr_data(mvi, phy_id, tmp & 0xfd7fffff);
}

static void mvs_94xx_sgpio_init(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs_ex - 0x10200;
	u32 tmp;

	tmp = mr32(MVS_HST_CHIP_CONFIG);
	tmp |= 0x100;
	mw32(MVS_HST_CHIP_CONFIG, tmp);

	mw32(MVS_SGPIO_CTRL + MVS_SGPIO_HOST_OFFSET * mvi->id,
		MVS_SGPIO_CTRL_SDOUT_AUTO << MVS_SGPIO_CTRL_SDOUT_SHIFT);

	mw32(MVS_SGPIO_CFG1 + MVS_SGPIO_HOST_OFFSET * mvi->id,
		8 << MVS_SGPIO_CFG1_LOWA_SHIFT |
		8 << MVS_SGPIO_CFG1_HIA_SHIFT |
		4 << MVS_SGPIO_CFG1_LOWB_SHIFT |
		4 << MVS_SGPIO_CFG1_HIB_SHIFT |
		2 << MVS_SGPIO_CFG1_MAXACTON_SHIFT |
		1 << MVS_SGPIO_CFG1_FORCEACTOFF_SHIFT
	);

	mw32(MVS_SGPIO_CFG2 + MVS_SGPIO_HOST_OFFSET * mvi->id,
		(300000 / 100) << MVS_SGPIO_CFG2_CLK_SHIFT | /* 100kHz clock */
		66 << MVS_SGPIO_CFG2_BLINK_SHIFT /* (66 * 0,121 Hz?)*/
	);

	mw32(MVS_SGPIO_CFG0 + MVS_SGPIO_HOST_OFFSET * mvi->id,
		MVS_SGPIO_CFG0_ENABLE |
		MVS_SGPIO_CFG0_BLINKA |
		MVS_SGPIO_CFG0_BLINKB |
		/* 3*4 data bits / PDU */
		(12 - 1) << MVS_SGPIO_CFG0_AUT_BITLEN_SHIFT
	);

	mw32(MVS_SGPIO_DCTRL + MVS_SGPIO_HOST_OFFSET * mvi->id,
		DEFAULT_SGPIO_BITS);

	mw32(MVS_SGPIO_DSRC + MVS_SGPIO_HOST_OFFSET * mvi->id,
		((mvi->id * 4) + 3) << (8 * 3) |
		((mvi->id * 4) + 2) << (8 * 2) |
		((mvi->id * 4) + 1) << (8 * 1) |
		((mvi->id * 4) + 0) << (8 * 0));

}

static int mvs_94xx_init(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	int i;
	u32 tmp, cctl;
	u8 revision;

	revision = mvi->pdev->revision;
	mvs_show_pcie_usage(mvi);
	if (mvi->flags & MVF_FLAG_SOC) {
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

	if (mvi->flags & MVF_FLAG_SOC) {
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

	/* disable Multiplexing, enable phy implemented */
	mw32(MVS_PORTS_IMP, 0xFF);

	if (revision == VANIR_A0_REV) {
		mw32(MVS_PA_VSR_ADDR, CMD_CMWK_OOB_DET);
		mw32(MVS_PA_VSR_PORT, 0x00018080);
	}
	mw32(MVS_PA_VSR_ADDR, VSR_PHY_MODE2);
	if (revision == VANIR_A0_REV || revision == VANIR_B0_REV)
		/* set 6G/3G/1.5G, multiplexing, without SSC */
		mw32(MVS_PA_VSR_PORT, 0x0084d4fe);
	else
		/* set 6G/3G/1.5G, multiplexing, with and without SSC */
		mw32(MVS_PA_VSR_PORT, 0x0084fffe);

	if (revision == VANIR_B0_REV) {
		mw32(MVS_PA_VSR_ADDR, CMD_APP_MEM_CTL);
		mw32(MVS_PA_VSR_PORT, 0x08001006);
		mw32(MVS_PA_VSR_ADDR, CMD_HOST_RD_DATA);
		mw32(MVS_PA_VSR_PORT, 0x0000705f);
	}

	/* reset control */
	mw32(MVS_PCS, 0);		/* MVS_PCS */
	mw32(MVS_STP_REG_SET_0, 0);
	mw32(MVS_STP_REG_SET_1, 0);

	/* init phys */
	mvs_phy_hacks(mvi);

	/* disable non data frame retry */
	tmp = mvs_cr32(mvi, CMD_SAS_CTL1);
	if ((revision == VANIR_A0_REV) ||
		(revision == VANIR_B0_REV) ||
		(revision == VANIR_C0_REV)) {
		tmp &= ~0xffff;
		tmp |= 0x007f;
		mvs_cw32(mvi, CMD_SAS_CTL1, tmp);
	}

	/* set LED blink when IO*/
	mw32(MVS_PA_VSR_ADDR, VSR_PHY_ACT_LED);
	tmp = mr32(MVS_PA_VSR_PORT);
	tmp &= 0xFFFF00FF;
	tmp |= 0x00003300;
	mw32(MVS_PA_VSR_PORT, tmp);

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
		mvs_94xx_phy_disable(mvi, i);
		/* set phy local SAS address */
		mvs_set_sas_addr(mvi, i, CONFIG_ID_FRAME3, CONFIG_ID_FRAME4,
						cpu_to_le64(mvi->phy[i].dev_sas_addr));

		mvs_94xx_enable_xmt(mvi, i);
		mvs_94xx_config_reg_from_hba(mvi, i);
		mvs_94xx_phy_enable(mvi, i);

		mvs_94xx_phy_reset(mvi, i, PHY_RST_HARD);
		msleep(500);
		mvs_94xx_detect_porttype(mvi, i);
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
		tmp = PHYEV_RDY_CH | PHYEV_BROAD_CH |
			PHYEV_ID_DONE  | PHYEV_DCDR_ERR | PHYEV_CRC_ERR ;
		mvs_write_port_irq_mask(mvi, i, tmp);

		msleep(100);
		mvs_update_phyinfo(mvi, i, 1);
	}

	/* little endian for open address and command table, etc. */
	cctl = mr32(MVS_CTL);
	cctl |= CCTL_ENDIAN_CMD;
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

	/* default interrupt coalescing time is 128us */
	tmp = 0x10000 | interrupt_coalescing;
	mw32(MVS_INT_COAL_TMOUT, tmp);

	/* ladies and gentlemen, start your engines */
	mw32(MVS_TX_CFG, 0);
	mw32(MVS_TX_CFG, MVS_CHIP_SLOT_SZ | TX_EN);
	mw32(MVS_RX_CFG, MVS_RX_RING_SZ | RX_EN);
	/* enable CMD/CMPL_Q/RESP mode */
	mw32(MVS_PCS, PCS_SATA_RETRY_2 | PCS_FIS_RX_EN |
		PCS_CMD_EN | PCS_CMD_STOP_ERR);

	/* enable completion queue interrupt */
	tmp = (CINT_PORT_MASK | CINT_DONE | CINT_MEM | CINT_SRS | CINT_CI_STOP |
		CINT_DMA_PCIE | CINT_NON_SPEC_NCQ_ERROR);
	tmp |= CINT_PHY_MASK;
	mw32(MVS_INT_MASK, tmp);

	tmp = mvs_cr32(mvi, CMD_LINK_TIMER);
	tmp |= 0xFFFF0000;
	mvs_cw32(mvi, CMD_LINK_TIMER, tmp);

	/* tune STP performance */
	tmp = 0x003F003F;
	mvs_cw32(mvi, CMD_PL_TIMER, tmp);

	/* This can improve expander large block size seq write performance */
	tmp = mvs_cr32(mvi, CMD_PORT_LAYER_TIMER1);
	tmp |= 0xFFFF007F;
	mvs_cw32(mvi, CMD_PORT_LAYER_TIMER1, tmp);

	/* change the connection open-close behavior (bit 9)
	 * set bit8 to 1 for performance tuning */
	tmp = mvs_cr32(mvi, CMD_SL_MODE0);
	tmp |= 0x00000300;
	/* set bit0 to 0 to enable retry for no_dest reject case */
	tmp &= 0xFFFFFFFE;
	mvs_cw32(mvi, CMD_SL_MODE0, tmp);

	/* Enable SRS interrupt */
	mw32(MVS_INT_MASK_SRS_0, 0xFFFF);

	mvs_94xx_sgpio_init(mvi);

	return 0;
}

static int mvs_94xx_ioremap(struct mvs_info *mvi)
{
	if (!mvs_ioremap(mvi, 2, -1)) {
		mvi->regs_ex = mvi->regs + 0x10200;
		mvi->regs += 0x20000;
		if (mvi->id == 1)
			mvi->regs += 0x4000;
		return 0;
	}
	return -1;
}

static void mvs_94xx_iounmap(struct mvs_info *mvi)
{
	if (mvi->regs) {
		mvi->regs -= 0x20000;
		if (mvi->id == 1)
			mvi->regs -= 0x4000;
		mvs_iounmap(mvi->regs);
	}
}

static void mvs_94xx_interrupt_enable(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs_ex;
	u32 tmp;

	tmp = mr32(MVS_GBL_CTL);
	tmp |= (MVS_IRQ_SAS_A | MVS_IRQ_SAS_B);
	mw32(MVS_GBL_INT_STAT, tmp);
	writel(tmp, regs + 0x0C);
	writel(tmp, regs + 0x10);
	writel(tmp, regs + 0x14);
	writel(tmp, regs + 0x18);
	mw32(MVS_GBL_CTL, tmp);
}

static void mvs_94xx_interrupt_disable(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs_ex;
	u32 tmp;

	tmp = mr32(MVS_GBL_CTL);

	tmp &= ~(MVS_IRQ_SAS_A | MVS_IRQ_SAS_B);
	mw32(MVS_GBL_INT_STAT, tmp);
	writel(tmp, regs + 0x0C);
	writel(tmp, regs + 0x10);
	writel(tmp, regs + 0x14);
	writel(tmp, regs + 0x18);
	mw32(MVS_GBL_CTL, tmp);
}

static u32 mvs_94xx_isr_status(struct mvs_info *mvi, int irq)
{
	void __iomem *regs = mvi->regs_ex;
	u32 stat = 0;
	if (!(mvi->flags & MVF_FLAG_SOC)) {
		stat = mr32(MVS_GBL_INT_STAT);

		if (!(stat & (MVS_IRQ_SAS_A | MVS_IRQ_SAS_B)))
			return 0;
	}
	return stat;
}

static irqreturn_t mvs_94xx_isr(struct mvs_info *mvi, int irq, u32 stat)
{
	void __iomem *regs = mvi->regs;

	if (((stat & MVS_IRQ_SAS_A) && mvi->id == 0) ||
			((stat & MVS_IRQ_SAS_B) && mvi->id == 1)) {
		mw32_f(MVS_INT_STAT, CINT_DONE);

		spin_lock(&mvi->lock);
		mvs_int_full(mvi);
		spin_unlock(&mvi->lock);
	}
	return IRQ_HANDLED;
}

static void mvs_94xx_command_active(struct mvs_info *mvi, u32 slot_idx)
{
	u32 tmp;
	tmp = mvs_cr32(mvi, MVS_COMMAND_ACTIVE+(slot_idx >> 3));
	if (tmp && 1 << (slot_idx % 32)) {
		mv_printk("command active %08X,  slot [%x].\n", tmp, slot_idx);
		mvs_cw32(mvi, MVS_COMMAND_ACTIVE + (slot_idx >> 3),
			1 << (slot_idx % 32));
		do {
			tmp = mvs_cr32(mvi,
				MVS_COMMAND_ACTIVE + (slot_idx >> 3));
		} while (tmp & 1 << (slot_idx % 32));
	}
}

void mvs_94xx_clear_srs_irq(struct mvs_info *mvi, u8 reg_set, u8 clear_all)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	if (clear_all) {
		tmp = mr32(MVS_INT_STAT_SRS_0);
		if (tmp) {
			mv_dprintk("check SRS 0 %08X.\n", tmp);
			mw32(MVS_INT_STAT_SRS_0, tmp);
		}
		tmp = mr32(MVS_INT_STAT_SRS_1);
		if (tmp) {
			mv_dprintk("check SRS 1 %08X.\n", tmp);
			mw32(MVS_INT_STAT_SRS_1, tmp);
		}
	} else {
		if (reg_set > 31)
			tmp = mr32(MVS_INT_STAT_SRS_1);
		else
			tmp = mr32(MVS_INT_STAT_SRS_0);

		if (tmp & (1 << (reg_set % 32))) {
			mv_dprintk("register set 0x%x was stopped.\n", reg_set);
			if (reg_set > 31)
				mw32(MVS_INT_STAT_SRS_1, 1 << (reg_set % 32));
			else
				mw32(MVS_INT_STAT_SRS_0, 1 << (reg_set % 32));
		}
	}
}

static void mvs_94xx_issue_stop(struct mvs_info *mvi, enum mvs_port_type type,
				u32 tfs)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;
	mvs_94xx_clear_srs_irq(mvi, 0, 1);

	tmp = mr32(MVS_INT_STAT);
	mw32(MVS_INT_STAT, tmp | CINT_CI_STOP);
	tmp = mr32(MVS_PCS) | 0xFF00;
	mw32(MVS_PCS, tmp);
}

static void mvs_94xx_non_spec_ncq_error(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 err_0, err_1;
	u8 i;
	struct mvs_device *device;

	err_0 = mr32(MVS_NON_NCQ_ERR_0);
	err_1 = mr32(MVS_NON_NCQ_ERR_1);

	mv_dprintk("non specific ncq error err_0:%x,err_1:%x.\n",
			err_0, err_1);
	for (i = 0; i < 32; i++) {
		if (err_0 & bit(i)) {
			device = mvs_find_dev_by_reg_set(mvi, i);
			if (device)
				mvs_release_task(mvi, device->sas_device);
		}
		if (err_1 & bit(i)) {
			device = mvs_find_dev_by_reg_set(mvi, i+32);
			if (device)
				mvs_release_task(mvi, device->sas_device);
		}
	}

	mw32(MVS_NON_NCQ_ERR_0, err_0);
	mw32(MVS_NON_NCQ_ERR_1, err_1);
}

static void mvs_94xx_free_reg_set(struct mvs_info *mvi, u8 *tfs)
{
	void __iomem *regs = mvi->regs;
	u8 reg_set = *tfs;

	if (*tfs == MVS_ID_NOT_MAPPED)
		return;

	mvi->sata_reg_set &= ~bit(reg_set);
	if (reg_set < 32)
		w_reg_set_enable(reg_set, (u32)mvi->sata_reg_set);
	else
		w_reg_set_enable(reg_set, (u32)(mvi->sata_reg_set >> 32));

	*tfs = MVS_ID_NOT_MAPPED;

	return;
}

static u8 mvs_94xx_assign_reg_set(struct mvs_info *mvi, u8 *tfs)
{
	int i;
	void __iomem *regs = mvi->regs;

	if (*tfs != MVS_ID_NOT_MAPPED)
		return 0;

	i = mv_ffc64(mvi->sata_reg_set);
	if (i >= 32) {
		mvi->sata_reg_set |= bit(i);
		w_reg_set_enable(i, (u32)(mvi->sata_reg_set >> 32));
		*tfs = i;
		return 0;
	} else if (i >= 0) {
		mvi->sata_reg_set |= bit(i);
		w_reg_set_enable(i, (u32)mvi->sata_reg_set);
		*tfs = i;
		return 0;
	}
	return MVS_ID_NOT_MAPPED;
}

static void mvs_94xx_make_prd(struct scatterlist *scatter, int nr, void *prd)
{
	int i;
	struct scatterlist *sg;
	struct mvs_prd *buf_prd = prd;
	struct mvs_prd_imt im_len;
	*(u32 *)&im_len = 0;
	for_each_sg(scatter, sg, nr, i) {
		buf_prd->addr = cpu_to_le64(sg_dma_address(sg));
		im_len.len = sg_dma_len(sg);
		buf_prd->im_len = cpu_to_le32(*(u32 *)&im_len);
		buf_prd++;
	}
}

static int mvs_94xx_oob_done(struct mvs_info *mvi, int i)
{
	u32 phy_st;
	phy_st = mvs_read_phy_ctl(mvi, i);
	if (phy_st & PHY_READY_MASK)
		return 1;
	return 0;
}

static void mvs_94xx_get_dev_identify_frame(struct mvs_info *mvi, int port_id,
					struct sas_identify_frame *id)
{
	int i;
	u32 id_frame[7];

	for (i = 0; i < 7; i++) {
		mvs_write_port_cfg_addr(mvi, port_id,
					CONFIG_ID_FRAME0 + i * 4);
		id_frame[i] = cpu_to_le32(mvs_read_port_cfg_data(mvi, port_id));
	}
	memcpy(id, id_frame, 28);
}

static void mvs_94xx_get_att_identify_frame(struct mvs_info *mvi, int port_id,
					struct sas_identify_frame *id)
{
	int i;
	u32 id_frame[7];

	for (i = 0; i < 7; i++) {
		mvs_write_port_cfg_addr(mvi, port_id,
					CONFIG_ATT_ID_FRAME0 + i * 4);
		id_frame[i] = cpu_to_le32(mvs_read_port_cfg_data(mvi, port_id));
		mv_dprintk("94xx phy %d atta frame %d %x.\n",
			port_id + mvi->id * mvi->chip->n_phy, i, id_frame[i]);
	}
	memcpy(id, id_frame, 28);
}

static u32 mvs_94xx_make_dev_info(struct sas_identify_frame *id)
{
	u32 att_dev_info = 0;

	att_dev_info |= id->dev_type;
	if (id->stp_iport)
		att_dev_info |= PORT_DEV_STP_INIT;
	if (id->smp_iport)
		att_dev_info |= PORT_DEV_SMP_INIT;
	if (id->ssp_iport)
		att_dev_info |= PORT_DEV_SSP_INIT;
	if (id->stp_tport)
		att_dev_info |= PORT_DEV_STP_TRGT;
	if (id->smp_tport)
		att_dev_info |= PORT_DEV_SMP_TRGT;
	if (id->ssp_tport)
		att_dev_info |= PORT_DEV_SSP_TRGT;

	att_dev_info |= (u32)id->phy_id<<24;
	return att_dev_info;
}

static u32 mvs_94xx_make_att_info(struct sas_identify_frame *id)
{
	return mvs_94xx_make_dev_info(id);
}

static void mvs_94xx_fix_phy_info(struct mvs_info *mvi, int i,
				struct sas_identify_frame *id)
{
	struct mvs_phy *phy = &mvi->phy[i];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	mv_dprintk("get all reg link rate is 0x%x\n", phy->phy_status);
	sas_phy->linkrate =
		(phy->phy_status & PHY_NEG_SPP_PHYS_LINK_RATE_MASK) >>
			PHY_NEG_SPP_PHYS_LINK_RATE_MASK_OFFSET;
	sas_phy->linkrate += 0x8;
	mv_dprintk("get link rate is %d\n", sas_phy->linkrate);
	phy->minimum_linkrate = SAS_LINK_RATE_1_5_GBPS;
	phy->maximum_linkrate = SAS_LINK_RATE_6_0_GBPS;
	mvs_94xx_get_dev_identify_frame(mvi, i, id);
	phy->dev_info = mvs_94xx_make_dev_info(id);

	if (phy->phy_type & PORT_TYPE_SAS) {
		mvs_94xx_get_att_identify_frame(mvi, i, id);
		phy->att_dev_info = mvs_94xx_make_att_info(id);
		phy->att_dev_sas_addr = *(u64 *)id->sas_addr;
	} else {
		phy->att_dev_info = PORT_DEV_STP_TRGT | 1;
	}

	/* enable spin up bit */
	mvs_write_port_cfg_addr(mvi, i, PHYR_PHY_STAT);
	mvs_write_port_cfg_data(mvi, i, 0x04);

}

void mvs_94xx_phy_set_link_rate(struct mvs_info *mvi, u32 phy_id,
			struct sas_phy_linkrates *rates)
{
	u32 lrmax = 0;
	u32 tmp;

	tmp = mvs_read_phy_ctl(mvi, phy_id);
	lrmax = (rates->maximum_linkrate - SAS_LINK_RATE_1_5_GBPS) << 12;

	if (lrmax) {
		tmp &= ~(0x3 << 12);
		tmp |= lrmax;
	}
	mvs_write_phy_ctl(mvi, phy_id, tmp);
	mvs_94xx_phy_reset(mvi, phy_id, PHY_RST_HARD);
}

static void mvs_94xx_clear_active_cmds(struct mvs_info *mvi)
{
	u32 tmp;
	void __iomem *regs = mvi->regs;
	tmp = mr32(MVS_STP_REG_SET_0);
	mw32(MVS_STP_REG_SET_0, 0);
	mw32(MVS_STP_REG_SET_0, tmp);
	tmp = mr32(MVS_STP_REG_SET_1);
	mw32(MVS_STP_REG_SET_1, 0);
	mw32(MVS_STP_REG_SET_1, tmp);
}


u32 mvs_94xx_spi_read_data(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs_ex - 0x10200;
	return mr32(SPI_RD_DATA_REG_94XX);
}

void mvs_94xx_spi_write_data(struct mvs_info *mvi, u32 data)
{
	void __iomem *regs = mvi->regs_ex - 0x10200;
	 mw32(SPI_RD_DATA_REG_94XX, data);
}


int mvs_94xx_spi_buildcmd(struct mvs_info *mvi,
				u32      *dwCmd,
				u8       cmd,
				u8       read,
				u8       length,
				u32      addr
				)
{
	void __iomem *regs = mvi->regs_ex - 0x10200;
	u32  dwTmp;

	dwTmp = ((u32)cmd << 8) | ((u32)length << 4);
	if (read)
		dwTmp |= SPI_CTRL_READ_94XX;

	if (addr != MV_MAX_U32) {
		mw32(SPI_ADDR_REG_94XX, (addr & 0x0003FFFFL));
		dwTmp |= SPI_ADDR_VLD_94XX;
	}

	*dwCmd = dwTmp;
	return 0;
}


int mvs_94xx_spi_issuecmd(struct mvs_info *mvi, u32 cmd)
{
	void __iomem *regs = mvi->regs_ex - 0x10200;
	mw32(SPI_CTRL_REG_94XX, cmd | SPI_CTRL_SpiStart_94XX);

	return 0;
}

int mvs_94xx_spi_waitdataready(struct mvs_info *mvi, u32 timeout)
{
	void __iomem *regs = mvi->regs_ex - 0x10200;
	u32   i, dwTmp;

	for (i = 0; i < timeout; i++) {
		dwTmp = mr32(SPI_CTRL_REG_94XX);
		if (!(dwTmp & SPI_CTRL_SpiStart_94XX))
			return 0;
		msleep(10);
	}

	return -1;
}

void mvs_94xx_fix_dma(struct mvs_info *mvi, u32 phy_mask,
				int buf_len, int from, void *prd)
{
	int i;
	struct mvs_prd *buf_prd = prd;
	dma_addr_t buf_dma;
	struct mvs_prd_imt im_len;

	*(u32 *)&im_len = 0;
	buf_prd += from;

#define PRD_CHAINED_ENTRY 0x01
	if ((mvi->pdev->revision == VANIR_A0_REV) ||
			(mvi->pdev->revision == VANIR_B0_REV))
		buf_dma = (phy_mask <= 0x08) ?
				mvi->bulk_buffer_dma : mvi->bulk_buffer_dma1;
	else
		return;

	for (i = from; i < MAX_SG_ENTRY; i++, ++buf_prd) {
		if (i == MAX_SG_ENTRY - 1) {
			buf_prd->addr = cpu_to_le64(virt_to_phys(buf_prd - 1));
			im_len.len = 2;
			im_len.misc_ctl = PRD_CHAINED_ENTRY;
		} else {
			buf_prd->addr = cpu_to_le64(buf_dma);
			im_len.len = buf_len;
		}
		buf_prd->im_len = cpu_to_le32(*(u32 *)&im_len);
	}
}

static void mvs_94xx_tune_interrupt(struct mvs_info *mvi, u32 time)
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

static int mvs_94xx_gpio_write(struct mvs_prv_info *mvs_prv,
			u8 reg_type, u8 reg_index,
			u8 reg_count, u8 *write_data)
{
	int i;

	switch (reg_type) {

	case SAS_GPIO_REG_TX_GP:
		if (reg_index == 0)
			return -EINVAL;

		if (reg_count > 1)
			return -EINVAL;

		if (reg_count == 0)
			return 0;

		/* maximum supported bits = hosts * 4 drives * 3 bits */
		for (i = 0; i < mvs_prv->n_host * 4 * 3; i++) {

			/* select host */
			struct mvs_info *mvi = mvs_prv->mvi[i/(4*3)];

			void __iomem *regs = mvi->regs_ex - 0x10200;

			int drive = (i/3) & (4-1); /* drive number on host */
			u32 block = mr32(MVS_SGPIO_DCTRL +
				MVS_SGPIO_HOST_OFFSET * mvi->id);


			/*
			* if bit is set then create a mask with the first
			* bit of the drive set in the mask ...
			*/
			u32 bit = (write_data[i/8] & (1 << (i&(8-1)))) ?
				1<<(24-drive*8) : 0;

			/*
			* ... and then shift it to the right position based
			* on the led type (activity/id/fail)
			*/
			switch (i%3) {
			case 0: /* activity */
				block &= ~((0x7 << MVS_SGPIO_DCTRL_ACT_SHIFT)
					<< (24-drive*8));
					/* hardwire activity bit to SOF */
				block |= LED_BLINKA_SOF << (
					MVS_SGPIO_DCTRL_ACT_SHIFT +
					(24-drive*8));
				break;
			case 1: /* id */
				block &= ~((0x3 << MVS_SGPIO_DCTRL_LOC_SHIFT)
					<< (24-drive*8));
				block |= bit << MVS_SGPIO_DCTRL_LOC_SHIFT;
				break;
			case 2: /* fail */
				block &= ~((0x7 << MVS_SGPIO_DCTRL_ERR_SHIFT)
					<< (24-drive*8));
				block |= bit << MVS_SGPIO_DCTRL_ERR_SHIFT;
				break;
			}

			mw32(MVS_SGPIO_DCTRL + MVS_SGPIO_HOST_OFFSET * mvi->id,
				block);

		}

		return reg_count;

	case SAS_GPIO_REG_TX:
		if (reg_index + reg_count > mvs_prv->n_host)
			return -EINVAL;

		for (i = 0; i < reg_count; i++) {
			struct mvs_info *mvi = mvs_prv->mvi[i+reg_index];
			void __iomem *regs = mvi->regs_ex - 0x10200;

			mw32(MVS_SGPIO_DCTRL + MVS_SGPIO_HOST_OFFSET * mvi->id,
				be32_to_cpu(((u32 *) write_data)[i]));
		}
		return reg_count;
	}
	return -ENOSYS;
}

const struct mvs_dispatch mvs_94xx_dispatch = {
	"mv94xx",
	mvs_94xx_init,
	NULL,
	mvs_94xx_ioremap,
	mvs_94xx_iounmap,
	mvs_94xx_isr,
	mvs_94xx_isr_status,
	mvs_94xx_interrupt_enable,
	mvs_94xx_interrupt_disable,
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
	mvs_94xx_command_active,
	mvs_94xx_clear_srs_irq,
	mvs_94xx_issue_stop,
	mvs_start_delivery,
	mvs_rx_update,
	mvs_int_full,
	mvs_94xx_assign_reg_set,
	mvs_94xx_free_reg_set,
	mvs_get_prd_size,
	mvs_get_prd_count,
	mvs_94xx_make_prd,
	mvs_94xx_detect_porttype,
	mvs_94xx_oob_done,
	mvs_94xx_fix_phy_info,
	NULL,
	mvs_94xx_phy_set_link_rate,
	mvs_hw_max_link_rate,
	mvs_94xx_phy_disable,
	mvs_94xx_phy_enable,
	mvs_94xx_phy_reset,
	NULL,
	mvs_94xx_clear_active_cmds,
	mvs_94xx_spi_read_data,
	mvs_94xx_spi_write_data,
	mvs_94xx_spi_buildcmd,
	mvs_94xx_spi_issuecmd,
	mvs_94xx_spi_waitdataready,
	mvs_94xx_fix_dma,
	mvs_94xx_tune_interrupt,
	mvs_94xx_non_spec_ncq_error,
	mvs_94xx_gpio_write,
};

