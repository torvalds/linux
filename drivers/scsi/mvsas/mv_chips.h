#ifndef _MV_CHIPS_H_
#define _MV_CHIPS_H_

#define mr32(reg)	readl(regs + MVS_##reg)
#define mw32(reg,val)	writel((val), regs + MVS_##reg)
#define mw32_f(reg,val)	do {			\
	writel((val), regs + MVS_##reg);	\
	readl(regs + MVS_##reg);		\
	} while (0)

static inline u32 mvs_cr32(void __iomem *regs, u32 addr)
{
	mw32(CMD_ADDR, addr);
	return mr32(CMD_DATA);
}

static inline void mvs_cw32(void __iomem *regs, u32 addr, u32 val)
{
	mw32(CMD_ADDR, addr);
	mw32(CMD_DATA, val);
}

static inline u32 mvs_read_phy_ctl(struct mvs_info *mvi, u32 port)
{
	void __iomem *regs = mvi->regs;
	return (port < 4)?mr32(P0_SER_CTLSTAT + port * 4):
		mr32(P4_SER_CTLSTAT + (port - 4) * 4);
}

static inline void mvs_write_phy_ctl(struct mvs_info *mvi, u32 port, u32 val)
{
	void __iomem *regs = mvi->regs;
	if (port < 4)
		mw32(P0_SER_CTLSTAT + port * 4, val);
	else
		mw32(P4_SER_CTLSTAT + (port - 4) * 4, val);
}

static inline u32 mvs_read_port(struct mvs_info *mvi, u32 off, u32 off2, u32 port)
{
	void __iomem *regs = mvi->regs + off;
	void __iomem *regs2 = mvi->regs + off2;
	return (port < 4)?readl(regs + port * 8):
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

static inline void mvs_write_port_cfg_data(struct mvs_info *mvi, u32 port, u32 val)
{
	mvs_write_port(mvi, MVS_P0_CFG_DATA,
			MVS_P4_CFG_DATA, port, val);
}

static inline void mvs_write_port_cfg_addr(struct mvs_info *mvi, u32 port, u32 addr)
{
	mvs_write_port(mvi, MVS_P0_CFG_ADDR,
			MVS_P4_CFG_ADDR, port, addr);
}

static inline u32 mvs_read_port_vsr_data(struct mvs_info *mvi, u32 port)
{
	return mvs_read_port(mvi, MVS_P0_VSR_DATA,
			MVS_P4_VSR_DATA, port);
}

static inline void mvs_write_port_vsr_data(struct mvs_info *mvi, u32 port, u32 val)
{
	mvs_write_port(mvi, MVS_P0_VSR_DATA,
			MVS_P4_VSR_DATA, port, val);
}

static inline void mvs_write_port_vsr_addr(struct mvs_info *mvi, u32 port, u32 addr)
{
	mvs_write_port(mvi, MVS_P0_VSR_ADDR,
			MVS_P4_VSR_ADDR, port, addr);
}

static inline u32 mvs_read_port_irq_stat(struct mvs_info *mvi, u32 port)
{
	return mvs_read_port(mvi, MVS_P0_INT_STAT,
			MVS_P4_INT_STAT, port);
}

static inline void mvs_write_port_irq_stat(struct mvs_info *mvi, u32 port, u32 val)
{
	mvs_write_port(mvi, MVS_P0_INT_STAT,
			MVS_P4_INT_STAT, port, val);
}

static inline u32 mvs_read_port_irq_mask(struct mvs_info *mvi, u32 port)
{
	return mvs_read_port(mvi, MVS_P0_INT_MASK,
			MVS_P4_INT_MASK, port);
}

static inline void mvs_write_port_irq_mask(struct mvs_info *mvi, u32 port, u32 val)
{
	mvs_write_port(mvi, MVS_P0_INT_MASK,
			MVS_P4_INT_MASK, port, val);
}

#endif
