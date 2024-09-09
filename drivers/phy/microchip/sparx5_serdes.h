/* SPDX-License-Identifier: GPL-2.0+
 * Microchip Sparx5 SerDes driver
 *
 * Copyright (c) 2020 Microchip Technology Inc.
 */

#ifndef _SPARX5_SERDES_H_
#define _SPARX5_SERDES_H_

#include "sparx5_serdes_regs.h"

#define SPX5_SERDES_MAX       33

enum sparx5_serdes_type {
	SPX5_SDT_6G  = 6,
	SPX5_SDT_10G = 10,
	SPX5_SDT_25G = 25,
};

enum sparx5_serdes_mode {
	SPX5_SD_MODE_NONE,
	SPX5_SD_MODE_2G5,
	SPX5_SD_MODE_QSGMII,
	SPX5_SD_MODE_100FX,
	SPX5_SD_MODE_1000BASEX,
	SPX5_SD_MODE_SFI,
};

enum sparx5_10g28cmu_mode {
	SPX5_SD10G28_CMU_MAIN = 0,
	SPX5_SD10G28_CMU_AUX1 = 1,
	SPX5_SD10G28_CMU_AUX2 = 3,
	SPX5_SD10G28_CMU_NONE = 4,
	SPX5_SD10G28_CMU_MAX,
};

enum sparx5_target {
	SPX5_TARGET_SPARX5,
	SPX5_TARGET_LAN969X,

};

struct sparx5_serdes_macro {
	struct sparx5_serdes_private *priv;
	u32 sidx;
	u32 stpidx;
	enum sparx5_serdes_type serdestype;
	enum sparx5_serdes_mode serdesmode;
	phy_interface_t portmode;
	int speed;
	enum phy_media media;
};

struct sparx5_serdes_consts {
	int sd_max;
	int cmu_max;
};

struct sparx5_serdes_ops {
	void (*serdes_type_set)(struct sparx5_serdes_macro *macro, int sidx);
	int (*serdes_cmu_get)(enum sparx5_10g28cmu_mode mode, int sd_index);
};

struct sparx5_serdes_match_data {
	enum sparx5_target type;
	const struct sparx5_serdes_consts consts;
	const struct sparx5_serdes_ops ops;
	const struct sparx5_serdes_io_resource *iomap;
	int iomap_size;
	const unsigned int *tsize;
};

struct sparx5_serdes_private {
	struct device *dev;
	void __iomem *regs[NUM_TARGETS];
	struct phy *phys[SPX5_SERDES_MAX];
	unsigned long coreclock;
	const struct sparx5_serdes_match_data *data;
};

/* Read, Write and modify registers content.
 * The register definition macros start at the id
 */
static inline void __iomem *sdx5_addr(void __iomem *base[],
				      int id, int tinst, int tcnt,
				      int gbase, int ginst,
				      int gcnt, int gwidth,
				      int raddr, int rinst,
				      int rcnt, int rwidth)
{
	WARN_ON((tinst) >= tcnt);
	WARN_ON((ginst) >= gcnt);
	WARN_ON((rinst) >= rcnt);
	return base[id + (tinst)] +
		gbase + ((ginst) * gwidth) +
		raddr + ((rinst) * rwidth);
}

static inline void __iomem *sdx5_inst_baseaddr(void __iomem *base,
					       int gbase, int ginst,
					       int gcnt, int gwidth,
					       int raddr, int rinst,
					       int rcnt, int rwidth)
{
	WARN_ON((ginst) >= gcnt);
	WARN_ON((rinst) >= rcnt);
	return base +
		gbase + ((ginst) * gwidth) +
		raddr + ((rinst) * rwidth);
}

static inline void sdx5_rmw(u32 val, u32 mask, struct sparx5_serdes_private *priv,
			    int id, int tinst, int tcnt,
			    int gbase, int ginst, int gcnt, int gwidth,
			    int raddr, int rinst, int rcnt, int rwidth)
{
	u32 nval;
	void __iomem *addr =
		sdx5_addr(priv->regs, id, tinst, tcnt,
			  gbase, ginst, gcnt, gwidth,
			  raddr, rinst, rcnt, rwidth);
	nval = readl(addr);
	nval = (nval & ~mask) | (val & mask);
	writel(nval, addr);
}

static inline void sdx5_inst_rmw(u32 val, u32 mask, void __iomem *iomem,
				 int id, int tinst, int tcnt,
				 int gbase, int ginst, int gcnt, int gwidth,
				 int raddr, int rinst, int rcnt, int rwidth)
{
	u32 nval;
	void __iomem *addr =
		sdx5_inst_baseaddr(iomem,
				   gbase, ginst, gcnt, gwidth,
				   raddr, rinst, rcnt, rwidth);
	nval = readl(addr);
	nval = (nval & ~mask) | (val & mask);
	writel(nval, addr);
}

static inline void sdx5_rmw_addr(u32 val, u32 mask, void __iomem *addr)
{
	u32 nval;

	nval = readl(addr);
	nval = (nval & ~mask) | (val & mask);
	writel(nval, addr);
}

static inline void __iomem *sdx5_inst_get(struct sparx5_serdes_private *priv,
					  int id, int tinst)
{
	return priv->regs[id + tinst];
}

static inline void __iomem *sdx5_inst_addr(void __iomem *iomem,
					   int id, int tinst, int tcnt,
					   int gbase,
					   int ginst, int gcnt, int gwidth,
					   int raddr,
					   int rinst, int rcnt, int rwidth)
{
	return sdx5_inst_baseaddr(iomem, gbase, ginst, gcnt, gwidth,
				  raddr, rinst, rcnt, rwidth);
}


#endif /* _SPARX5_SERDES_REGS_H_ */
