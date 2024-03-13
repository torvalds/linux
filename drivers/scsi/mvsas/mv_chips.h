/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Marvell 88SE64xx/88SE94xx register IO interface
 *
 * Copyright 2007 Red Hat, Inc.
 * Copyright 2008 Marvell. <kewei@marvell.com>
 * Copyright 2009-2011 Marvell. <yuxiangl@marvell.com>
*/


#ifndef _MV_CHIPS_H_
#define _MV_CHIPS_H_

#define mr32(reg)	readl(regs + reg)
#define mw32(reg, val)	writel((val), regs + reg)
#define mw32_f(reg, val)	do {			\
				mw32(reg, val);	\
				mr32(reg);	\
			} while (0)

#define iow32(reg, val) 	outl(val, (unsigned long)(regs + reg))
#define ior32(reg) 		inl((unsigned long)(regs + reg))
#define iow16(reg, val) 	outw((unsigned long)(val, regs + reg))
#define ior16(reg) 		inw((unsigned long)(regs + reg))
#define iow8(reg, val) 		outb((unsigned long)(val, regs + reg))
#define ior8(reg) 		inb((unsigned long)(regs + reg))

static inline u32 mvs_cr32(struct mvs_info *mvi, u32 addr)
{
	void __iomem *regs = mvi->regs;
	mw32(MVS_CMD_ADDR, addr);
	return mr32(MVS_CMD_DATA);
}

static inline void mvs_cw32(struct mvs_info *mvi, u32 addr, u32 val)
{
	void __iomem *regs = mvi->regs;
	mw32(MVS_CMD_ADDR, addr);
	mw32(MVS_CMD_DATA, val);
}

static inline u32 mvs_read_phy_ctl(struct mvs_info *mvi, u32 port)
{
	void __iomem *regs = mvi->regs;
	return (port < 4) ? mr32(MVS_P0_SER_CTLSTAT + port * 4) :
		mr32(MVS_P4_SER_CTLSTAT + (port - 4) * 4);
}

static inline void mvs_write_phy_ctl(struct mvs_info *mvi, u32 port, u32 val)
{
	void __iomem *regs = mvi->regs;
	if (port < 4)
		mw32(MVS_P0_SER_CTLSTAT + port * 4, val);
	else
		mw32(MVS_P4_SER_CTLSTAT + (port - 4) * 4, val);
}

static inline u32 mvs_read_port(struct mvs_info *mvi, u32 off,
				u32 off2, u32 port)
{
	void __iomem *regs = mvi->regs + off;
	void __iomem *regs2 = mvi->regs + off2;
	return (port < 4) ? readl(regs + port * 8) :
		readl(regs2 + (port - 4) * 8);
}

static inline void mvs_write_port(struct mvs_info *mvi, u32 off, u32 off2,
				u32 port, u32 val)
{
	void __iomem *regs = mvi->regs + off;
	void __iomem *regs2 = mvi->regs + off2;
	if (port < 4)
		writel(val, regs + port * 8);
	else
		writel(val, regs2 + (port - 4) * 8);
}

static inline u32 mvs_read_port_cfg_data(struct mvs_info *mvi, u32 port)
{
	return mvs_read_port(mvi, MVS_P0_CFG_DATA,
			MVS_P4_CFG_DATA, port);
}

static inline void mvs_write_port_cfg_data(struct mvs_info *mvi,
						u32 port, u32 val)
{
	mvs_write_port(mvi, MVS_P0_CFG_DATA,
			MVS_P4_CFG_DATA, port, val);
}

static inline void mvs_write_port_cfg_addr(struct mvs_info *mvi,
						u32 port, u32 addr)
{
	mvs_write_port(mvi, MVS_P0_CFG_ADDR,
			MVS_P4_CFG_ADDR, port, addr);
	mdelay(10);
}

static inline u32 mvs_read_port_vsr_data(struct mvs_info *mvi, u32 port)
{
	return mvs_read_port(mvi, MVS_P0_VSR_DATA,
			MVS_P4_VSR_DATA, port);
}

static inline void mvs_write_port_vsr_data(struct mvs_info *mvi,
						u32 port, u32 val)
{
	mvs_write_port(mvi, MVS_P0_VSR_DATA,
			MVS_P4_VSR_DATA, port, val);
}

static inline void mvs_write_port_vsr_addr(struct mvs_info *mvi,
						u32 port, u32 addr)
{
	mvs_write_port(mvi, MVS_P0_VSR_ADDR,
			MVS_P4_VSR_ADDR, port, addr);
	mdelay(10);
}

static inline u32 mvs_read_port_irq_stat(struct mvs_info *mvi, u32 port)
{
	return mvs_read_port(mvi, MVS_P0_INT_STAT,
			MVS_P4_INT_STAT, port);
}

static inline void mvs_write_port_irq_stat(struct mvs_info *mvi,
						u32 port, u32 val)
{
	mvs_write_port(mvi, MVS_P0_INT_STAT,
			MVS_P4_INT_STAT, port, val);
}

static inline u32 mvs_read_port_irq_mask(struct mvs_info *mvi, u32 port)
{
	return mvs_read_port(mvi, MVS_P0_INT_MASK,
			MVS_P4_INT_MASK, port);

}

static inline void mvs_write_port_irq_mask(struct mvs_info *mvi,
						u32 port, u32 val)
{
	mvs_write_port(mvi, MVS_P0_INT_MASK,
			MVS_P4_INT_MASK, port, val);
}

static inline void mvs_phy_hacks(struct mvs_info *mvi)
{
	u32 tmp;

	tmp = mvs_cr32(mvi, CMD_PHY_TIMER);
	tmp &= ~(1 << 9);
	tmp |= (1 << 10);
	mvs_cw32(mvi, CMD_PHY_TIMER, tmp);

	/* enable retry 127 times */
	mvs_cw32(mvi, CMD_SAS_CTL1, 0x7f7f);

	/* extend open frame timeout to max */
	tmp = mvs_cr32(mvi, CMD_SAS_CTL0);
	tmp &= ~0xffff;
	tmp |= 0x3fff;
	mvs_cw32(mvi, CMD_SAS_CTL0, tmp);

	mvs_cw32(mvi, CMD_WD_TIMER, 0x7a0000);

	/* not to halt for different port op during wideport link change */
	mvs_cw32(mvi, CMD_APP_ERR_CONFIG, 0xffefbf7d);
}

static inline void mvs_int_sata(struct mvs_info *mvi)
{
	u32 tmp;
	void __iomem *regs = mvi->regs;
	tmp = mr32(MVS_INT_STAT_SRS_0);
	if (tmp)
		mw32(MVS_INT_STAT_SRS_0, tmp);
	MVS_CHIP_DISP->clear_active_cmds(mvi);
}

static inline void mvs_int_full(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 tmp, stat;
	int i;

	stat = mr32(MVS_INT_STAT);
	mvs_int_rx(mvi, false);

	for (i = 0; i < mvi->chip->n_phy; i++) {
		tmp = (stat >> i) & (CINT_PORT | CINT_PORT_STOPPED);
		if (tmp)
			mvs_int_port(mvi, i, tmp);
	}

	if (stat & CINT_NON_SPEC_NCQ_ERROR)
		MVS_CHIP_DISP->non_spec_ncq_error(mvi);

	if (stat & CINT_SRS)
		mvs_int_sata(mvi);

	mw32(MVS_INT_STAT, stat);
}

static inline void mvs_start_delivery(struct mvs_info *mvi, u32 tx)
{
	void __iomem *regs = mvi->regs;
	mw32(MVS_TX_PROD_IDX, tx);
}

static inline u32 mvs_rx_update(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	return mr32(MVS_RX_CONS_IDX);
}

static inline u32 mvs_get_prd_size(void)
{
	return sizeof(struct mvs_prd);
}

static inline u32 mvs_get_prd_count(void)
{
	return MAX_SG_ENTRY;
}

static inline void mvs_show_pcie_usage(struct mvs_info *mvi)
{
	u16 link_stat, link_spd;
	const char *spd[] = {
		"UnKnown",
		"2.5",
		"5.0",
	};
	if (mvi->flags & MVF_FLAG_SOC || mvi->id > 0)
		return;

	pci_read_config_word(mvi->pdev, PCR_LINK_STAT, &link_stat);
	link_spd = (link_stat & PLS_LINK_SPD) >> PLS_LINK_SPD_OFFS;
	if (link_spd >= 3)
		link_spd = 0;
	dev_printk(KERN_INFO, mvi->dev,
		"mvsas: PCI-E x%u, Bandwidth Usage: %s Gbps\n",
	       (link_stat & PLS_NEG_LINK_WD) >> PLS_NEG_LINK_WD_OFFS,
	       spd[link_spd]);
}

static inline u32 mvs_hw_max_link_rate(void)
{
	return MAX_LINK_RATE;
}

#endif  /* _MV_CHIPS_H_ */

