/*
	mv_64xx.c - Marvell 88SE6440 SAS/SATA support

	Copyright 2007 Red Hat, Inc.
	Copyright 2008 Marvell. <kewei@marvell.com>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License as
	published by the Free Software Foundation; either version 2,
	or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty
	of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public
	License along with this program; see the file COPYING.	If not,
	write to the Free Software Foundation, 675 Mass Ave, Cambridge,
	MA 02139, USA.

 */

#include "mv_sas.h"
#include "mv_64xx.h"
#include "mv_chips.h"

void mvs_detect_porttype(struct mvs_info *mvi, int i)
{
	void __iomem *regs = mvi->regs;
	u32 reg;
	struct mvs_phy *phy = &mvi->phy[i];

	/* TODO check & save device type */
	reg = mr32(GBL_PORT_TYPE);

	if (reg & MODE_SAS_SATA & (1 << i))
		phy->phy_type |= PORT_TYPE_SAS;
	else
		phy->phy_type |= PORT_TYPE_SATA;
}

void mvs_enable_xmt(struct mvs_info *mvi, int PhyId)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	tmp = mr32(PCS);
	if (mvi->chip->n_phy <= 4)
		tmp |= 1 << (PhyId + PCS_EN_PORT_XMT_SHIFT);
	else
		tmp |= 1 << (PhyId + PCS_EN_PORT_XMT_SHIFT2);
	mw32(PCS, tmp);
}

void __devinit mvs_phy_hacks(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	/* workaround for SATA R-ERR, to ignore phy glitch */
	tmp = mvs_cr32(regs, CMD_PHY_TIMER);
	tmp &= ~(1 << 9);
	tmp |= (1 << 10);
	mvs_cw32(regs, CMD_PHY_TIMER, tmp);

	/* enable retry 127 times */
	mvs_cw32(regs, CMD_SAS_CTL1, 0x7f7f);

	/* extend open frame timeout to max */
	tmp = mvs_cr32(regs, CMD_SAS_CTL0);
	tmp &= ~0xffff;
	tmp |= 0x3fff;
	mvs_cw32(regs, CMD_SAS_CTL0, tmp);

	/* workaround for WDTIMEOUT , set to 550 ms */
	mvs_cw32(regs, CMD_WD_TIMER, 0x86470);

	/* not to halt for different port op during wideport link change */
	mvs_cw32(regs, CMD_APP_ERR_CONFIG, 0xffefbf7d);

	/* workaround for Seagate disk not-found OOB sequence, recv
	 * COMINIT before sending out COMWAKE */
	tmp = mvs_cr32(regs, CMD_PHY_MODE_21);
	tmp &= 0x0000ffff;
	tmp |= 0x00fa0000;
	mvs_cw32(regs, CMD_PHY_MODE_21, tmp);

	tmp = mvs_cr32(regs, CMD_PHY_TIMER);
	tmp &= 0x1fffffff;
	tmp |= (2U << 29);	/* 8 ms retry */
	mvs_cw32(regs, CMD_PHY_TIMER, tmp);

	/* TEST - for phy decoding error, adjust voltage levels */
	mw32(P0_VSR_ADDR + 0, 0x8);
	mw32(P0_VSR_DATA + 0, 0x2F0);

	mw32(P0_VSR_ADDR + 8, 0x8);
	mw32(P0_VSR_DATA + 8, 0x2F0);

	mw32(P0_VSR_ADDR + 16, 0x8);
	mw32(P0_VSR_DATA + 16, 0x2F0);

	mw32(P0_VSR_ADDR + 24, 0x8);
	mw32(P0_VSR_DATA + 24, 0x2F0);

}

void mvs_hba_interrupt_enable(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	tmp = mr32(GBL_CTL);

	mw32(GBL_CTL, tmp | INT_EN);
}

void mvs_hba_interrupt_disable(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 tmp;

	tmp = mr32(GBL_CTL);

	mw32(GBL_CTL, tmp & ~INT_EN);
}

void mvs_free_reg_set(struct mvs_info *mvi, struct mvs_port *port)
{
	void __iomem *regs = mvi->regs;
	u32 tmp, offs;
	u8 *tfs = &port->taskfileset;

	if (*tfs == MVS_ID_NOT_MAPPED)
		return;

	offs = 1U << ((*tfs & 0x0f) + PCS_EN_SATA_REG_SHIFT);
	if (*tfs < 16) {
		tmp = mr32(PCS);
		mw32(PCS, tmp & ~offs);
	} else {
		tmp = mr32(CTL);
		mw32(CTL, tmp & ~offs);
	}

	tmp = mr32(INT_STAT_SRS) & (1U << *tfs);
	if (tmp)
		mw32(INT_STAT_SRS, tmp);

	*tfs = MVS_ID_NOT_MAPPED;
}

u8 mvs_assign_reg_set(struct mvs_info *mvi, struct mvs_port *port)
{
	int i;
	u32 tmp, offs;
	void __iomem *regs = mvi->regs;

	if (port->taskfileset != MVS_ID_NOT_MAPPED)
		return 0;

	tmp = mr32(PCS);

	for (i = 0; i < mvi->chip->srs_sz; i++) {
		if (i == 16)
			tmp = mr32(CTL);
		offs = 1U << ((i & 0x0f) + PCS_EN_SATA_REG_SHIFT);
		if (!(tmp & offs)) {
			port->taskfileset = i;

			if (i < 16)
				mw32(PCS, tmp | offs);
			else
				mw32(CTL, tmp | offs);
			tmp = mr32(INT_STAT_SRS) & (1U << i);
			if (tmp)
				mw32(INT_STAT_SRS, tmp);
			return 0;
		}
	}
	return MVS_ID_NOT_MAPPED;
}

