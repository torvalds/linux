/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright 2025-2026 NXP */

#ifndef _PHY_FSL_LYNX_CORE_H
#define _PHY_FSL_LYNX_CORE_H

#include <linux/phy/phy.h>
#include <linux/phy.h>
#include <soc/fsl/phy-fsl-lynx.h>

#define LYNX_NUM_PLL				2
#define LYNX_QUIRK_HAS_HARDCODED_USXGMII	BIT(0)

struct lynx_priv;
struct lynx_lane;

struct lynx_pccr {
	int offset;
	int width;
	int shift;
};

struct lynx_pll {
	struct lynx_priv *priv;
	int id;
	int refclk_sel;
	int frate_sel;
	bool enabled;
	bool locked;
	DECLARE_BITMAP(supported, LANE_MODE_MAX);
};

struct lynx_lane {
	struct lynx_priv *priv;
	struct phy *phy;
	bool powered_up;
	bool init;
	unsigned int id;
	enum lynx_lane_mode mode;
	u32 default_pccr[LANE_MODE_MAX];
};

struct lynx_info {
	int (*get_pccr)(enum lynx_lane_mode lane_mode, int lane,
			struct lynx_pccr *pccr);
	int (*get_pcvt_offset)(int lane, enum lynx_lane_mode mode);
	bool (*lane_supports_mode)(int lane, enum lynx_lane_mode mode);
	void (*pll_read_configuration)(struct lynx_pll *pll);
	void (*lane_read_configuration)(struct lynx_lane *lane);
	void (*cdr_lock_check)(struct lynx_lane *lane);
	int first_lane;
	int num_lanes;
	int index;
	unsigned long quirks;
};

struct lynx_priv {
	void __iomem *base;
	struct device *dev;
	const struct lynx_info *info;
	/* Serialize concurrent access to registers shared between lanes,
	 * like PCCn
	 */
	spinlock_t pcc_lock;
	bool big_endian;
	struct lynx_pll pll[LYNX_NUM_PLL];
	struct lynx_lane *lane;

	struct delayed_work cdr_check;
};

static inline u32 lynx_read(struct lynx_priv *priv, unsigned long off)
{
	void __iomem *reg = priv->base + off;

	if (priv->big_endian)
		return ioread32be(reg);

	return ioread32(reg);
}

static inline void lynx_write(struct lynx_priv *priv, unsigned long off, u32 val)
{
	void __iomem *reg = priv->base + off;

	if (priv->big_endian)
		return iowrite32be(val, reg);

	return iowrite32(val, reg);
}

static inline void lynx_rmw(struct lynx_priv *priv, unsigned long off, u32 val,
			    u32 mask)
{
	u32 orig, tmp;

	orig = lynx_read(priv, off);
	tmp = orig & ~mask;
	tmp |= val;
	if (orig != tmp)
		lynx_write(priv, off, tmp);
}

#define lynx_lane_rmw(lane, reg, val, mask)	\
	lynx_rmw((lane)->priv, reg(lane->id), val, mask)
#define lynx_lane_read(lane, reg)			\
	lynx_read((lane)->priv, reg((lane)->id))
#define lynx_lane_write(lane, reg, val)		\
	lynx_write((lane)->priv, reg((lane)->id), val)
#define lynx_pll_read(pll, reg)			\
	lynx_read((pll)->priv, reg((pll)->id))

int lynx_probe(struct platform_device *pdev, const struct lynx_info *info,
	       const struct phy_ops *phy_ops);
void lynx_remove(struct platform_device *pdev);

const char *lynx_lane_mode_str(enum lynx_lane_mode lane_mode);
enum lynx_lane_mode phy_interface_to_lane_mode(phy_interface_t intf);
bool lynx_lane_supports_mode(struct lynx_lane *lane, enum lynx_lane_mode mode);
int lynx_phy_mode_to_lane_mode(struct phy *phy, enum phy_mode mode,
			       int submode, enum lynx_lane_mode *lane_mode);

struct lynx_pll *lynx_pll_get(struct lynx_priv *priv, enum lynx_lane_mode mode);

int lynx_pccr_read(struct lynx_lane *lane, enum lynx_lane_mode mode, u32 *val);
int lynx_pccr_write(struct lynx_lane *lane, enum lynx_lane_mode mode, u32 val);
int lynx_pcvt_read(struct lynx_lane *lane, enum lynx_lane_mode mode, int cr,
		   u32 *val);
int lynx_pcvt_write(struct lynx_lane *lane, enum lynx_lane_mode mode, int cr,
		    u32 val);
int lynx_pcvt_rmw(struct lynx_lane *lane, enum lynx_lane_mode mode, int cr,
		  u32 val, u32 mask);

#endif
