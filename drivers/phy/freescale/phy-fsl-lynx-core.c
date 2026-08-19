// SPDX-License-Identifier: GPL-2.0+
/* Copyright 2025-2026 NXP */

#include <linux/module.h>
#include <linux/platform_device.h>

#include "phy-fsl-lynx-core.h"

const char *lynx_lane_mode_str(enum lynx_lane_mode lane_mode)
{
	switch (lane_mode) {
	case LANE_MODE_1000BASEX_SGMII:
		return "1000Base-X/SGMII";
	case LANE_MODE_2500BASEX:
		return "2500Base-X";
	case LANE_MODE_QSGMII:
		return "QSGMII";
	case LANE_MODE_10G_QXGMII:
		return "10G-QXGMII";
	case LANE_MODE_10GBASER:
		return "10GBase-R";
	case LANE_MODE_USXGMII:
		return "USXGMII";
	case LANE_MODE_25GBASER:
		return "25GBase-R";
	default:
		return "unknown";
	}
}
EXPORT_SYMBOL_NS_GPL(lynx_lane_mode_str, "PHY_FSL_LYNX");

enum lynx_lane_mode phy_interface_to_lane_mode(phy_interface_t intf)
{
	switch (intf) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		return LANE_MODE_1000BASEX_SGMII;
	case PHY_INTERFACE_MODE_2500BASEX:
		return LANE_MODE_2500BASEX;
	case PHY_INTERFACE_MODE_QSGMII:
		return LANE_MODE_QSGMII;
	case PHY_INTERFACE_MODE_10G_QXGMII:
		return LANE_MODE_10G_QXGMII;
	case PHY_INTERFACE_MODE_10GBASER:
		return LANE_MODE_10GBASER;
	case PHY_INTERFACE_MODE_USXGMII:
		return LANE_MODE_USXGMII;
	case PHY_INTERFACE_MODE_25GBASER:
		return LANE_MODE_25GBASER;
	default:
		return LANE_MODE_UNKNOWN;
	}
}
EXPORT_SYMBOL_NS_GPL(phy_interface_to_lane_mode, "PHY_FSL_LYNX");

/* By default, assume that if we know how to get the PCCR register and
 * protocol converter for a lane, that protocol is supported.
 */
static bool lynx_lane_supports_mode_default(struct lynx_lane *lane,
					    enum lynx_lane_mode mode)
{
	struct lynx_priv *priv = lane->priv;
	struct lynx_pccr pccr;

	if (!priv->info->get_pccr || !priv->info->get_pcvt_offset)
		return false;

	if (priv->info->get_pccr(mode, lane->id, &pccr) < 0)
		return false;

	if (priv->info->get_pcvt_offset(lane->id, mode) < 0)
		return false;

	return true;
}

/* A lane mode is supported if we have a PLL that can provide its required
 * clock net, and if there is a protocol converter for that mode on that lane.
 */
bool lynx_lane_supports_mode(struct lynx_lane *lane, enum lynx_lane_mode mode)
{
	struct lynx_priv *priv = lane->priv;
	int i;

	if (priv->info->lane_supports_mode) {
		if (!priv->info->lane_supports_mode(lane->id, mode))
			return false;
	} else if (!lynx_lane_supports_mode_default(lane, mode)) {
		return false;
	}

	for (i = 0; i < LYNX_NUM_PLL; i++) {
		if (!priv->pll[i].enabled)
			continue;

		if (test_bit(mode, priv->pll[i].supported))
			return true;
	}

	return false;
}
EXPORT_SYMBOL_NS_GPL(lynx_lane_supports_mode, "PHY_FSL_LYNX");

/* The quad protocols are fixed because the lane has multiple consumers, and
 * one phy_set_mode_ext() affects the other consumers as well. We have no use
 * case for dynamic protocol changing here, so disallow it.
 */
static enum lynx_lane_mode lynx_fixed_protocols[] = {
	LANE_MODE_QSGMII,
	LANE_MODE_10G_QXGMII,
};

static bool lynx_lane_restrict_fixed_mode_change(struct lynx_lane *lane,
						 enum lynx_lane_mode new)
{
	enum lynx_lane_mode curr = lane->mode;

	for (int i = 0; i < ARRAY_SIZE(lynx_fixed_protocols); i++)
		if ((curr == lynx_fixed_protocols[i] ||
		     new == lynx_fixed_protocols[i]) &&
		     curr != new)
			return true;

	return false;
}

/* Translate the mode/submode from phy_validate() and phy_set_mode_ext() to a
 * lane_mode and return 0 if it is supported and we can transition to it from
 * the current lane mode, or return negative error otherwise.
 */
int lynx_phy_mode_to_lane_mode(struct phy *phy, enum phy_mode mode,
			       int submode, enum lynx_lane_mode *lane_mode)
{
	struct lynx_lane *lane = phy_get_drvdata(phy);
	enum lynx_lane_mode tmp_lane_mode;

	/* The protocol configuration tables are incomplete for full lane
	 * reconfiguration from an arbitrary protocol.
	 */
	if (lane->mode == LANE_MODE_UNKNOWN)
		return -EINVAL;

	if (mode != PHY_MODE_ETHERNET)
		return -EINVAL;

	tmp_lane_mode = phy_interface_to_lane_mode(submode);
	if (!lynx_lane_supports_mode(lane, tmp_lane_mode))
		return -EINVAL;

	if (lynx_lane_restrict_fixed_mode_change(lane, tmp_lane_mode))
		return -EINVAL;

	if (lane_mode)
		*lane_mode = tmp_lane_mode;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(lynx_phy_mode_to_lane_mode, "PHY_FSL_LYNX");

struct lynx_pll *lynx_pll_get(struct lynx_priv *priv, enum lynx_lane_mode mode)
{
	struct lynx_pll *pll;
	int i;

	for (i = 0; i < LYNX_NUM_PLL; i++) {
		pll = &priv->pll[i];

		if (!pll->enabled)
			continue;

		if (test_bit(mode, pll->supported))
			return pll;
	}

	/* no pll supports requested mode, either caller forgot to check
	 * lynx_lane_supports_mode(), or this is a bug.
	 */
	dev_WARN_ONCE(priv->dev, 1, "no pll for lane mode %s\n",
		      lynx_lane_mode_str(mode));
	return NULL;
}
EXPORT_SYMBOL_NS_GPL(lynx_pll_get, "PHY_FSL_LYNX");

int lynx_pccr_read(struct lynx_lane *lane, enum lynx_lane_mode mode, u32 *val)
{
	struct lynx_priv *priv = lane->priv;
	struct lynx_pccr pccr;
	u32 tmp;
	int err;

	err = priv->info->get_pccr(mode, lane->id, &pccr);
	if (err)
		return err;

	tmp = lynx_read(priv, pccr.offset);
	*val = (tmp >> pccr.shift) & GENMASK(pccr.width - 1, 0);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(lynx_pccr_read, "PHY_FSL_LYNX");

int lynx_pccr_write(struct lynx_lane *lane, enum lynx_lane_mode mode, u32 val)
{
	struct lynx_priv *priv = lane->priv;
	struct lynx_pccr pccr;
	u32 old, tmp, mask;
	int err;

	err = priv->info->get_pccr(mode, lane->id, &pccr);
	if (err)
		return err;

	old = lynx_read(priv, pccr.offset);
	mask = GENMASK(pccr.width - 1, 0) << pccr.shift;
	tmp = (old & ~mask) | (val << pccr.shift);
	lynx_write(priv, pccr.offset, tmp);

	dev_dbg(&lane->phy->dev, "PCCR@0x%x: 0x%x -> 0x%x\n",
		pccr.offset, old, tmp);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(lynx_pccr_write, "PHY_FSL_LYNX");

int lynx_pcvt_read(struct lynx_lane *lane, enum lynx_lane_mode mode, int cr,
		   u32 *val)
{
	struct lynx_priv *priv = lane->priv;
	int offset;

	offset = priv->info->get_pcvt_offset(lane->id, mode);
	if (offset < 0)
		return offset;

	*val = lynx_read(priv, offset + cr);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(lynx_pcvt_read, "PHY_FSL_LYNX");

int lynx_pcvt_write(struct lynx_lane *lane, enum lynx_lane_mode mode, int cr,
		    u32 val)
{
	struct lynx_priv *priv = lane->priv;
	int offset;

	offset = priv->info->get_pcvt_offset(lane->id, mode);
	if (offset < 0)
		return offset;

	lynx_write(priv, offset + cr, val);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(lynx_pcvt_write, "PHY_FSL_LYNX");

int lynx_pcvt_rmw(struct lynx_lane *lane, enum lynx_lane_mode mode, int cr,
		  u32 val, u32 mask)
{
	int err;
	u32 tmp;

	err = lynx_pcvt_read(lane, mode, cr, &tmp);
	if (err)
		return err;

	tmp &= ~mask;
	tmp |= val;

	return lynx_pcvt_write(lane, mode, cr, tmp);
}
EXPORT_SYMBOL_NS_GPL(lynx_pcvt_rmw, "PHY_FSL_LYNX");

#define work_to_lynx(w) container_of((w), struct lynx_priv, cdr_check.work)

static void lynx_cdr_lock_check(struct work_struct *work)
{
	struct lynx_priv *priv = work_to_lynx(work);
	struct lynx_lane *lane;

	for (int i = priv->info->first_lane; i < priv->info->num_lanes; i++) {
		lane = &priv->lane[i];
		if (!lane->phy)
			continue;

		mutex_lock(&lane->phy->mutex);

		if (!lane->init || !lane->powered_up) {
			mutex_unlock(&lane->phy->mutex);
			continue;
		}

		priv->info->cdr_lock_check(lane);

		mutex_unlock(&lane->phy->mutex);
	}

	queue_delayed_work(system_power_efficient_wq, &priv->cdr_check,
			   msecs_to_jiffies(1000));
}

static struct phy *lynx_xlate(struct device *dev,
			      const struct of_phandle_args *args)
{
	struct lynx_priv *priv = dev_get_drvdata(dev);
	int idx;

	if (args->args_count == 0)
		return of_phy_simple_xlate(dev, args);
	else if (args->args_count != 1)
		return ERR_PTR(-ENODEV);

	idx = args->args[0];

	if (WARN_ON(idx >= priv->info->num_lanes ||
		    idx < priv->info->first_lane))
		return ERR_PTR(-EINVAL);

	return priv->lane[idx].phy ?: ERR_PTR(-ENODEV);
}

static int lynx_probe_lane(struct lynx_priv *priv, int id,
			   struct device_node *dn,
			   const struct phy_ops *phy_ops)
{
	struct lynx_lane *lane = &priv->lane[id];
	struct phy *phy;

	phy = devm_phy_create(priv->dev, dn, phy_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	lane->priv = priv;
	lane->phy = phy;
	lane->id = id;
	phy_set_drvdata(phy, lane);
	priv->info->lane_read_configuration(lane);

	return 0;
}

int lynx_probe(struct platform_device *pdev, const struct lynx_info *info,
	       const struct phy_ops *phy_ops)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct device_node *dn;
	struct lynx_priv *priv;
	int err;

	dn = dev_of_node(dev);
	if (!dn) {
		dev_err(dev, "Device requires an OF node\n");
		return -EINVAL;
	}

	if (!info)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->info = info;
	priv->big_endian = device_property_read_bool(dev, "big-endian");
	dev_set_drvdata(dev, priv);
	spin_lock_init(&priv->pcc_lock);
	INIT_DELAYED_WORK(&priv->cdr_check, lynx_cdr_lock_check);

	priv->lane = devm_kcalloc(dev, priv->info->num_lanes,
				  sizeof(*priv->lane), GFP_KERNEL);
	if (!priv->lane)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	for (int i = 0; i < LYNX_NUM_PLL; i++) {
		struct lynx_pll *pll = &priv->pll[i];

		pll->priv = priv;
		pll->id = i;
		priv->info->pll_read_configuration(pll);
	}

	if (of_get_child_count(dn)) {
		struct device_node *child;

		for_each_available_child_of_node(dn, child) {
			u32 reg;

			/* PHY subnode name must be 'phy'. */
			if (!(of_node_name_eq(child, "phy")))
				continue;

			if (of_property_read_u32(child, "reg", &reg)) {
				dev_err(dev, "No \"reg\" property for %pOF\n", child);
				of_node_put(child);
				return -EINVAL;
			}

			if (reg < priv->info->first_lane || reg >= priv->info->num_lanes) {
				dev_err(dev, "\"reg\" property out of range for %pOF\n", child);
				of_node_put(child);
				return -EINVAL;
			}

			err = lynx_probe_lane(priv, reg, child, phy_ops);
			if (err) {
				of_node_put(child);
				return err;
			}
		}
	} else {
		for (int i = priv->info->first_lane; i < priv->info->num_lanes; i++) {
			err = lynx_probe_lane(priv, i, NULL, phy_ops);
			if (err)
				return err;
		}
	}

	provider = devm_of_phy_provider_register(dev, lynx_xlate);
	if (IS_ERR(provider))
		return PTR_ERR(provider);

	queue_delayed_work(system_power_efficient_wq, &priv->cdr_check,
			   msecs_to_jiffies(1000));

	return 0;
}
EXPORT_SYMBOL_NS_GPL(lynx_probe, "PHY_FSL_LYNX");

void lynx_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lynx_priv *priv = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&priv->cdr_check);
}
EXPORT_SYMBOL_NS_GPL(lynx_remove, "PHY_FSL_LYNX");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Freescale Lynx SerDes core functionality");
