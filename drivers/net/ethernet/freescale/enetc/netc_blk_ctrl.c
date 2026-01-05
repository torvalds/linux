// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * NXP NETC Blocks Control Driver
 *
 * Copyright 2024 NXP
 *
 * This driver is used for pre-initialization of NETC, such as PCS and MII
 * protocols, LDID, warm reset, etc. Therefore, all NETC device drivers can
 * only be probed after the netc-blk-crtl driver has completed initialization.
 * In addition, when the system enters suspend mode, IERB, PRB, and NETCMIX
 * will be powered off, except for WOL. Therefore, when the system resumes,
 * these blocks need to be reinitialized.
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/fsl/netc_global.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

/* NETCMIX registers */
#define IMX95_CFG_LINK_IO_VAR		0x0
#define  IO_VAR_16FF_16G_SERDES		0x1
#define  IO_VAR(port, var)		(((var) & 0xf) << ((port) << 2))

#define IMX95_CFG_LINK_MII_PROT		0x4
#define CFG_LINK_MII_PORT_0		GENMASK(3, 0)
#define CFG_LINK_MII_PORT_1		GENMASK(7, 4)
#define  MII_PROT_MII			0x0
#define  MII_PROT_RMII			0x1
#define  MII_PROT_RGMII			0x2
#define  MII_PROT_SERIAL		0x3
#define  MII_PROT(port, prot)		(((prot) & 0xf) << ((port) << 2))

#define IMX95_CFG_LINK_PCS_PROT(a)	(0x8 + (a) * 4)
#define PCS_PROT_1G_SGMII		BIT(0)
#define PCS_PROT_2500M_SGMII		BIT(1)
#define PCS_PROT_XFI			BIT(3)
#define PCS_PROT_SFI			BIT(4)
#define PCS_PROT_10G_SXGMII		BIT(6)

#define IMX94_EXT_PIN_CONTROL		0x10
#define  MAC2_MAC3_SEL			BIT(1)

#define IMX94_NETC_LINK_CFG(a)		(0x4c + (a) * 4)
#define  NETC_LINK_CFG_MII_PROT		GENMASK(3, 0)
#define  NETC_LINK_CFG_IO_VAR		GENMASK(19, 16)

/* NETC privileged register block register */
#define PRB_NETCRR			0x100
#define  NETCRR_SR			BIT(0)
#define  NETCRR_LOCK			BIT(1)

#define PRB_NETCSR			0x104
#define  NETCSR_ERROR			BIT(0)
#define  NETCSR_STATE			BIT(1)

/* NETC integrated endpoint register block register */
#define IERB_EMDIOFAUXR			0x344
#define IERB_T0FAUXR			0x444
#define IERB_ETBCR(a)			(0x300c + 0x100 * (a))
#define IERB_LBCR(a)			(0x1010 + 0x40 * (a))
#define  LBCR_MDIO_PHYAD_PRTAD(addr)	(((addr) & 0x1f) << 8)

#define IERB_EFAUXR(a)			(0x3044 + 0x100 * (a))
#define IERB_VFAUXR(a)			(0x4004 + 0x40 * (a))
#define FAUXR_LDID			GENMASK(3, 0)

/* Platform information */
#define IMX95_ENETC0_BUS_DEVFN		0x0
#define IMX95_ENETC1_BUS_DEVFN		0x40
#define IMX95_ENETC2_BUS_DEVFN		0x80

#define IMX94_ENETC0_BUS_DEVFN		0x100
#define IMX94_ENETC1_BUS_DEVFN		0x140
#define IMX94_ENETC2_BUS_DEVFN		0x180
#define IMX94_TIMER0_BUS_DEVFN		0x1
#define IMX94_TIMER1_BUS_DEVFN		0x101
#define IMX94_TIMER2_BUS_DEVFN		0x181
#define IMX94_ENETC0_LINK		3
#define IMX94_ENETC1_LINK		4
#define IMX94_ENETC2_LINK		5

#define NETC_ENETC_ID(a)		(a)
#define NETC_TIMER_ID(a)		(a)

/* Flags for different platforms */
#define NETC_HAS_NETCMIX		BIT(0)

struct netc_devinfo {
	u32 flags;
	int (*netcmix_init)(struct platform_device *pdev);
	int (*ierb_init)(struct platform_device *pdev);
};

struct netc_blk_ctrl {
	void __iomem *prb;
	void __iomem *ierb;
	void __iomem *netcmix;

	const struct netc_devinfo *devinfo;
	struct platform_device *pdev;
	struct dentry *debugfs_root;
};

static void netc_reg_write(void __iomem *base, u32 offset, u32 val)
{
	netc_write(base + offset, val);
}

static u32 netc_reg_read(void __iomem *base, u32 offset)
{
	return netc_read(base + offset);
}

static int netc_of_pci_get_bus_devfn(struct device_node *np)
{
	u32 reg[5];
	int error;

	error = of_property_read_u32_array(np, "reg", reg, ARRAY_SIZE(reg));
	if (error)
		return error;

	return (reg[0] >> 8) & 0xffff;
}

static int netc_get_link_mii_protocol(phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_MII:
		return MII_PROT_MII;
	case PHY_INTERFACE_MODE_RMII:
		return MII_PROT_RMII;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		return MII_PROT_RGMII;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_XGMII:
	case PHY_INTERFACE_MODE_USXGMII:
		return MII_PROT_SERIAL;
	default:
		return -EINVAL;
	}
}

static int imx95_netcmix_init(struct platform_device *pdev)
{
	struct netc_blk_ctrl *priv = platform_get_drvdata(pdev);
	struct device_node *np = pdev->dev.of_node;
	phy_interface_t interface;
	int bus_devfn, mii_proto;
	u32 val;
	int err;

	/* Default setting of MII protocol */
	val = MII_PROT(0, MII_PROT_RGMII) | MII_PROT(1, MII_PROT_RGMII) |
	      MII_PROT(2, MII_PROT_SERIAL);

	/* Update the link MII protocol through parsing phy-mode */
	for_each_available_child_of_node_scoped(np, child) {
		for_each_available_child_of_node_scoped(child, gchild) {
			if (!of_device_is_compatible(gchild, "pci1131,e101"))
				continue;

			bus_devfn = netc_of_pci_get_bus_devfn(gchild);
			if (bus_devfn < 0)
				return -EINVAL;

			if (bus_devfn == IMX95_ENETC2_BUS_DEVFN)
				continue;

			err = of_get_phy_mode(gchild, &interface);
			if (err)
				continue;

			mii_proto = netc_get_link_mii_protocol(interface);
			if (mii_proto < 0)
				return -EINVAL;

			switch (bus_devfn) {
			case IMX95_ENETC0_BUS_DEVFN:
				val = u32_replace_bits(val, mii_proto,
						       CFG_LINK_MII_PORT_0);
				break;
			case IMX95_ENETC1_BUS_DEVFN:
				val = u32_replace_bits(val, mii_proto,
						       CFG_LINK_MII_PORT_1);
				break;
			default:
				return -EINVAL;
			}
		}
	}

	/* Configure Link I/O variant */
	netc_reg_write(priv->netcmix, IMX95_CFG_LINK_IO_VAR,
		       IO_VAR(2, IO_VAR_16FF_16G_SERDES));
	/* Configure Link 2 PCS protocol */
	netc_reg_write(priv->netcmix, IMX95_CFG_LINK_PCS_PROT(2),
		       PCS_PROT_10G_SXGMII);
	netc_reg_write(priv->netcmix, IMX95_CFG_LINK_MII_PROT, val);

	return 0;
}

static int imx94_enetc_get_link_id(struct device_node *np)
{
	int bus_devfn = netc_of_pci_get_bus_devfn(np);

	/* Parse ENETC link number */
	switch (bus_devfn) {
	case IMX94_ENETC0_BUS_DEVFN:
		return IMX94_ENETC0_LINK;
	case IMX94_ENETC1_BUS_DEVFN:
		return IMX94_ENETC1_LINK;
	case IMX94_ENETC2_BUS_DEVFN:
		return IMX94_ENETC2_LINK;
	default:
		return -EINVAL;
	}
}

static int imx94_link_config(struct netc_blk_ctrl *priv,
			     struct device_node *np, int link_id)
{
	phy_interface_t interface;
	int mii_proto;
	u32 val;

	/* The node may be disabled and does not have a 'phy-mode'
	 * or 'phy-connection-type' property.
	 */
	if (of_get_phy_mode(np, &interface))
		return 0;

	mii_proto = netc_get_link_mii_protocol(interface);
	if (mii_proto < 0)
		return mii_proto;

	val = mii_proto & NETC_LINK_CFG_MII_PROT;
	if (val == MII_PROT_SERIAL)
		val = u32_replace_bits(val, IO_VAR_16FF_16G_SERDES,
				       NETC_LINK_CFG_IO_VAR);

	netc_reg_write(priv->netcmix, IMX94_NETC_LINK_CFG(link_id), val);

	return 0;
}

static int imx94_enetc_link_config(struct netc_blk_ctrl *priv,
				   struct device_node *np)
{
	int link_id = imx94_enetc_get_link_id(np);

	if (link_id < 0)
		return link_id;

	return imx94_link_config(priv, np, link_id);
}

static int imx94_netcmix_init(struct platform_device *pdev)
{
	struct netc_blk_ctrl *priv = platform_get_drvdata(pdev);
	struct device_node *np = pdev->dev.of_node;
	u32 val;
	int err;

	for_each_child_of_node_scoped(np, child) {
		for_each_child_of_node_scoped(child, gchild) {
			if (!of_device_is_compatible(gchild, "pci1131,e101"))
				continue;

			err = imx94_enetc_link_config(priv, gchild);
			if (err)
				return err;
		}
	}

	/* ENETC 0 and switch port 2 share the same parallel interface.
	 * Currently, the switch is not supported, so this interface is
	 * used by ENETC 0 by default.
	 */
	val = netc_reg_read(priv->netcmix, IMX94_EXT_PIN_CONTROL);
	val |= MAC2_MAC3_SEL;
	netc_reg_write(priv->netcmix, IMX94_EXT_PIN_CONTROL, val);

	return 0;
}

static bool netc_ierb_is_locked(struct netc_blk_ctrl *priv)
{
	return !!(netc_reg_read(priv->prb, PRB_NETCRR) & NETCRR_LOCK);
}

static int netc_lock_ierb(struct netc_blk_ctrl *priv)
{
	u32 val;

	netc_reg_write(priv->prb, PRB_NETCRR, NETCRR_LOCK);

	return read_poll_timeout(netc_reg_read, val, !(val & NETCSR_STATE),
				 100, 2000, false, priv->prb, PRB_NETCSR);
}

static int netc_unlock_ierb_with_warm_reset(struct netc_blk_ctrl *priv)
{
	u32 val;

	netc_reg_write(priv->prb, PRB_NETCRR, 0);

	return read_poll_timeout(netc_reg_read, val, !(val & NETCRR_LOCK),
				 1000, 100000, true, priv->prb, PRB_NETCRR);
}

static int netc_get_phy_addr(struct device_node *np)
{
	struct device_node *mdio_node, *phy_node;
	u32 addr = 0;
	int err = 0;

	mdio_node = of_get_child_by_name(np, "mdio");
	if (!mdio_node)
		return 0;

	phy_node = of_get_next_child(mdio_node, NULL);
	if (!phy_node)
		goto of_put_mdio_node;

	err = of_property_read_u32(phy_node, "reg", &addr);
	if (err)
		goto of_put_phy_node;

	if (addr >= PHY_MAX_ADDR)
		err = -EINVAL;

of_put_phy_node:
	of_node_put(phy_node);

of_put_mdio_node:
	of_node_put(mdio_node);

	return err ? err : addr;
}

static int netc_parse_emdio_phy_mask(struct device_node *np, u32 *phy_mask)
{
	u32 mask = 0;

	for_each_child_of_node_scoped(np, child) {
		u32 addr;
		int err;

		err = of_property_read_u32(child, "reg", &addr);
		if (err)
			return err;

		if (addr >= PHY_MAX_ADDR)
			return -EINVAL;

		mask |= BIT(addr);
	}

	*phy_mask = mask;

	return 0;
}

static int netc_get_emdio_phy_mask(struct device_node *np, u32 *phy_mask)
{
	for_each_child_of_node_scoped(np, child) {
		for_each_child_of_node_scoped(child, gchild) {
			if (!of_device_is_compatible(gchild, "pci1131,ee00"))
				continue;

			return netc_parse_emdio_phy_mask(gchild, phy_mask);
		}
	}

	return 0;
}

static int imx95_enetc_mdio_phyaddr_config(struct platform_device *pdev)
{
	struct netc_blk_ctrl *priv = platform_get_drvdata(pdev);
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	int bus_devfn, addr, err;
	u32 phy_mask = 0;

	err = netc_get_emdio_phy_mask(np, &phy_mask);
	if (err) {
		dev_err(dev, "Failed to get PHY address mask\n");
		return err;
	}

	/* Update the port EMDIO PHY address through parsing phy properties.
	 * This is needed when using the port EMDIO but it's harmless when
	 * using the central EMDIO. So apply it on all cases.
	 */
	for_each_child_of_node_scoped(np, child) {
		for_each_child_of_node_scoped(child, gchild) {
			if (!of_device_is_compatible(gchild, "pci1131,e101"))
				continue;

			bus_devfn = netc_of_pci_get_bus_devfn(gchild);
			if (bus_devfn < 0) {
				dev_err(dev, "Failed to get BDF number\n");
				return bus_devfn;
			}

			addr = netc_get_phy_addr(gchild);
			if (addr < 0) {
				dev_err(dev, "Failed to get PHY address\n");
				return addr;
			}

			if (phy_mask & BIT(addr)) {
				dev_err(dev,
					"Find same PHY address in EMDIO and ENETC node\n");
				return -EINVAL;
			}

			/* The default value of LaBCR[MDIO_PHYAD_PRTAD ] is
			 * 0, so no need to set the register.
			 */
			if (!addr)
				continue;

			switch (bus_devfn) {
			case IMX95_ENETC0_BUS_DEVFN:
				netc_reg_write(priv->ierb, IERB_LBCR(0),
					       LBCR_MDIO_PHYAD_PRTAD(addr));
				break;
			case IMX95_ENETC1_BUS_DEVFN:
				netc_reg_write(priv->ierb, IERB_LBCR(1),
					       LBCR_MDIO_PHYAD_PRTAD(addr));
				break;
			case IMX95_ENETC2_BUS_DEVFN:
				netc_reg_write(priv->ierb, IERB_LBCR(2),
					       LBCR_MDIO_PHYAD_PRTAD(addr));
				break;
			default:
				break;
			}
		}
	}

	return 0;
}

static int imx95_ierb_init(struct platform_device *pdev)
{
	struct netc_blk_ctrl *priv = platform_get_drvdata(pdev);

	/* EMDIO : No MSI-X intterupt */
	netc_reg_write(priv->ierb, IERB_EMDIOFAUXR, 0);
	/* ENETC0 PF */
	netc_reg_write(priv->ierb, IERB_EFAUXR(0), 0);
	/* ENETC0 VF0 */
	netc_reg_write(priv->ierb, IERB_VFAUXR(0), 1);
	/* ENETC0 VF1 */
	netc_reg_write(priv->ierb, IERB_VFAUXR(1), 2);
	/* ENETC1 PF */
	netc_reg_write(priv->ierb, IERB_EFAUXR(1), 3);
	/* ENETC1 VF0 */
	netc_reg_write(priv->ierb, IERB_VFAUXR(2), 5);
	/* ENETC1 VF1 */
	netc_reg_write(priv->ierb, IERB_VFAUXR(3), 6);
	/* ENETC2 PF */
	netc_reg_write(priv->ierb, IERB_EFAUXR(2), 4);
	/* ENETC2 VF0 */
	netc_reg_write(priv->ierb, IERB_VFAUXR(4), 5);
	/* ENETC2 VF1 */
	netc_reg_write(priv->ierb, IERB_VFAUXR(5), 6);
	/* NETC TIMER */
	netc_reg_write(priv->ierb, IERB_T0FAUXR, 7);

	return imx95_enetc_mdio_phyaddr_config(pdev);
}

static int imx94_get_enetc_id(struct device_node *np)
{
	int bus_devfn = netc_of_pci_get_bus_devfn(np);

	/* Parse ENETC offset */
	switch (bus_devfn) {
	case IMX94_ENETC0_BUS_DEVFN:
		return NETC_ENETC_ID(0);
	case IMX94_ENETC1_BUS_DEVFN:
		return NETC_ENETC_ID(1);
	case IMX94_ENETC2_BUS_DEVFN:
		return NETC_ENETC_ID(2);
	default:
		return -EINVAL;
	}
}

static int imx94_get_timer_id(struct device_node *np)
{
	int bus_devfn = netc_of_pci_get_bus_devfn(np);

	/* Parse NETC PTP timer ID, the timer0 is on bus 0,
	 * the timer 1 and timer2 is on bus 1.
	 */
	switch (bus_devfn) {
	case IMX94_TIMER0_BUS_DEVFN:
		return NETC_TIMER_ID(0);
	case IMX94_TIMER1_BUS_DEVFN:
		return NETC_TIMER_ID(1);
	case IMX94_TIMER2_BUS_DEVFN:
		return NETC_TIMER_ID(2);
	default:
		return -EINVAL;
	}
}

static int imx94_enetc_update_tid(struct netc_blk_ctrl *priv,
				  struct device_node *np)
{
	struct device *dev = &priv->pdev->dev;
	struct device_node *timer_np;
	int eid, tid;

	eid = imx94_get_enetc_id(np);
	if (eid < 0) {
		dev_err(dev, "Failed to get ENETC ID\n");
		return eid;
	}

	timer_np = of_parse_phandle(np, "ptp-timer", 0);
	if (!timer_np) {
		/* If 'ptp-timer' is not present, the timer1 is the default
		 * timer of all standalone ENETCs, which is on the same PCIe
		 * bus as these ENETCs.
		 */
		tid = NETC_TIMER_ID(1);
		goto end;
	}

	tid = imx94_get_timer_id(timer_np);
	of_node_put(timer_np);
	if (tid < 0) {
		dev_err(dev, "Failed to get NETC Timer ID\n");
		return tid;
	}

end:
	netc_reg_write(priv->ierb, IERB_ETBCR(eid), tid);

	return 0;
}

static int imx94_enetc_mdio_phyaddr_config(struct netc_blk_ctrl *priv,
					   struct device_node *np,
					   u32 phy_mask)
{
	struct device *dev = &priv->pdev->dev;
	int bus_devfn, addr;

	bus_devfn = netc_of_pci_get_bus_devfn(np);
	if (bus_devfn < 0) {
		dev_err(dev, "Failed to get BDF number\n");
		return bus_devfn;
	}

	addr = netc_get_phy_addr(np);
	if (addr < 0) {
		dev_err(dev, "Failed to get PHY address\n");
		return addr;
	}

	/* The default value of LaBCR[MDIO_PHYAD_PRTAD] is 0,
	 * so no need to set the register.
	 */
	if (!addr)
		return 0;

	if (phy_mask & BIT(addr)) {
		dev_err(dev,
			"Find same PHY address in EMDIO and ENETC node\n");
		return -EINVAL;
	}

	switch (bus_devfn) {
	case IMX94_ENETC0_BUS_DEVFN:
		netc_reg_write(priv->ierb, IERB_LBCR(IMX94_ENETC0_LINK),
			       LBCR_MDIO_PHYAD_PRTAD(addr));
		break;
	case IMX94_ENETC1_BUS_DEVFN:
		netc_reg_write(priv->ierb, IERB_LBCR(IMX94_ENETC1_LINK),
			       LBCR_MDIO_PHYAD_PRTAD(addr));
		break;
	case IMX94_ENETC2_BUS_DEVFN:
		netc_reg_write(priv->ierb, IERB_LBCR(IMX94_ENETC2_LINK),
			       LBCR_MDIO_PHYAD_PRTAD(addr));
		break;
	default:
		break;
	}

	return 0;
}

static int imx94_ierb_init(struct platform_device *pdev)
{
	struct netc_blk_ctrl *priv = platform_get_drvdata(pdev);
	struct device_node *np = pdev->dev.of_node;
	u32 phy_mask = 0;
	int err;

	err = netc_get_emdio_phy_mask(np, &phy_mask);
	if (err) {
		dev_err(&pdev->dev, "Failed to get PHY address mask\n");
		return err;
	}

	for_each_child_of_node_scoped(np, child) {
		for_each_child_of_node_scoped(child, gchild) {
			if (!of_device_is_compatible(gchild, "pci1131,e101"))
				continue;

			err = imx94_enetc_update_tid(priv, gchild);
			if (err)
				return err;

			err = imx94_enetc_mdio_phyaddr_config(priv, gchild,
							      phy_mask);
			if (err)
				return err;
		}
	}

	return 0;
}

static int netc_ierb_init(struct platform_device *pdev)
{
	struct netc_blk_ctrl *priv = platform_get_drvdata(pdev);
	const struct netc_devinfo *devinfo = priv->devinfo;
	int err;

	if (netc_ierb_is_locked(priv)) {
		err = netc_unlock_ierb_with_warm_reset(priv);
		if (err) {
			dev_err(&pdev->dev, "Unlock IERB failed.\n");
			return err;
		}
	}

	if (devinfo->ierb_init) {
		err = devinfo->ierb_init(pdev);
		if (err)
			return err;
	}

	err = netc_lock_ierb(priv);
	if (err) {
		dev_err(&pdev->dev, "Lock IERB failed.\n");
		return err;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int netc_prb_show(struct seq_file *s, void *data)
{
	struct netc_blk_ctrl *priv = s->private;
	u32 val;

	val = netc_reg_read(priv->prb, PRB_NETCRR);
	seq_printf(s, "[PRB NETCRR] Lock:%d SR:%d\n",
		   (val & NETCRR_LOCK) ? 1 : 0,
		   (val & NETCRR_SR) ? 1 : 0);

	val = netc_reg_read(priv->prb, PRB_NETCSR);
	seq_printf(s, "[PRB NETCSR] State:%d Error:%d\n",
		   (val & NETCSR_STATE) ? 1 : 0,
		   (val & NETCSR_ERROR) ? 1 : 0);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(netc_prb);

static void netc_blk_ctrl_create_debugfs(struct netc_blk_ctrl *priv)
{
	struct dentry *root;

	root = debugfs_create_dir("netc_blk_ctrl", NULL);
	if (IS_ERR(root))
		return;

	priv->debugfs_root = root;

	debugfs_create_file("prb", 0444, root, priv, &netc_prb_fops);
}

static void netc_blk_ctrl_remove_debugfs(struct netc_blk_ctrl *priv)
{
	debugfs_remove_recursive(priv->debugfs_root);
	priv->debugfs_root = NULL;
}

#else

static void netc_blk_ctrl_create_debugfs(struct netc_blk_ctrl *priv)
{
}

static void netc_blk_ctrl_remove_debugfs(struct netc_blk_ctrl *priv)
{
}
#endif

static int netc_prb_check_error(struct netc_blk_ctrl *priv)
{
	if (netc_reg_read(priv->prb, PRB_NETCSR) & NETCSR_ERROR)
		return -1;

	return 0;
}

static const struct netc_devinfo imx95_devinfo = {
	.flags = NETC_HAS_NETCMIX,
	.netcmix_init = imx95_netcmix_init,
	.ierb_init = imx95_ierb_init,
};

static const struct netc_devinfo imx94_devinfo = {
	.flags = NETC_HAS_NETCMIX,
	.netcmix_init = imx94_netcmix_init,
	.ierb_init = imx94_ierb_init,
};

static const struct of_device_id netc_blk_ctrl_match[] = {
	{ .compatible = "nxp,imx95-netc-blk-ctrl", .data = &imx95_devinfo },
	{ .compatible = "nxp,imx94-netc-blk-ctrl", .data = &imx94_devinfo },
	{},
};
MODULE_DEVICE_TABLE(of, netc_blk_ctrl_match);

static int netc_blk_ctrl_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	const struct netc_devinfo *devinfo;
	struct device *dev = &pdev->dev;
	const struct of_device_id *id;
	struct netc_blk_ctrl *priv;
	struct clk *ipg_clk;
	void __iomem *regs;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pdev = pdev;
	ipg_clk = devm_clk_get_optional_enabled(dev, "ipg");
	if (IS_ERR(ipg_clk))
		return dev_err_probe(dev, PTR_ERR(ipg_clk),
				     "Set ipg clock failed\n");

	id = of_match_device(netc_blk_ctrl_match, dev);
	if (!id)
		return dev_err_probe(dev, -EINVAL, "Cannot match device\n");

	devinfo = (struct netc_devinfo *)id->data;
	if (!devinfo)
		return dev_err_probe(dev, -EINVAL, "No device information\n");

	priv->devinfo = devinfo;
	regs = devm_platform_ioremap_resource_byname(pdev, "ierb");
	if (IS_ERR(regs))
		return dev_err_probe(dev, PTR_ERR(regs),
				     "Missing IERB resource\n");

	priv->ierb = regs;
	regs = devm_platform_ioremap_resource_byname(pdev, "prb");
	if (IS_ERR(regs))
		return dev_err_probe(dev, PTR_ERR(regs),
				     "Missing PRB resource\n");

	priv->prb = regs;
	if (devinfo->flags & NETC_HAS_NETCMIX) {
		regs = devm_platform_ioremap_resource_byname(pdev, "netcmix");
		if (IS_ERR(regs))
			return dev_err_probe(dev, PTR_ERR(regs),
					     "Missing NETCMIX resource\n");
		priv->netcmix = regs;
	}

	platform_set_drvdata(pdev, priv);
	if (devinfo->netcmix_init) {
		err = devinfo->netcmix_init(pdev);
		if (err)
			return dev_err_probe(dev, err,
					     "Initializing NETCMIX failed\n");
	}

	err = netc_ierb_init(pdev);
	if (err)
		return dev_err_probe(dev, err, "Initializing IERB failed\n");

	if (netc_prb_check_error(priv) < 0)
		dev_warn(dev, "The current IERB configuration is invalid\n");

	netc_blk_ctrl_create_debugfs(priv);

	err = of_platform_populate(node, NULL, NULL, dev);
	if (err) {
		netc_blk_ctrl_remove_debugfs(priv);
		return dev_err_probe(dev, err, "of_platform_populate failed\n");
	}

	return 0;
}

static void netc_blk_ctrl_remove(struct platform_device *pdev)
{
	struct netc_blk_ctrl *priv = platform_get_drvdata(pdev);

	of_platform_depopulate(&pdev->dev);
	netc_blk_ctrl_remove_debugfs(priv);
}

static struct platform_driver netc_blk_ctrl_driver = {
	.driver = {
		.name = "nxp-netc-blk-ctrl",
		.of_match_table = netc_blk_ctrl_match,
	},
	.probe = netc_blk_ctrl_probe,
	.remove = netc_blk_ctrl_remove,
};

module_platform_driver(netc_blk_ctrl_driver);

MODULE_DESCRIPTION("NXP NETC Blocks Control Driver");
MODULE_LICENSE("Dual BSD/GPL");
