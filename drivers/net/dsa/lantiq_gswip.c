// SPDX-License-Identifier: GPL-2.0
/*
 * Lantiq / Intel GSWIP switch driver for VRX200 SoCs
 *
 * Copyright (C) 2010 Lantiq Deutschland
 * Copyright (C) 2012 John Crispin <john@phrozen.org>
 * Copyright (C) 2017 - 2019 Hauke Mehrtens <hauke@hauke-m.de>
 *
 * The VLAN and bridge model the GSWIP hardware uses does not directly
 * matches the model DSA uses.
 *
 * The hardware has 64 possible table entries for bridges with one VLAN
 * ID, one flow id and a list of ports for each bridge. All entries which
 * match the same flow ID are combined in the mac learning table, they
 * act as one global bridge.
 * The hardware does not support VLAN filter on the port, but on the
 * bridge, this driver converts the DSA model to the hardware.
 *
 * The CPU gets all the exception frames which do not match any forwarding
 * rule and the CPU port is also added to all bridges. This makes it possible
 * to handle all the special cases easily in software.
 * At the initialization the driver allocates one bridge table entry for
 * each switch port which is used when the port is used without an
 * explicit bridge. This prevents the frames from being forwarded
 * between all LAN ports by default.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <net/dsa.h>
#include <dt-bindings/mips/lantiq_rcu_gphy.h>

#include "lantiq_pce.h"

/* GSWIP MDIO Registers */
#define GSWIP_MDIO_GLOB			0x00
#define  GSWIP_MDIO_GLOB_ENABLE		BIT(15)
#define GSWIP_MDIO_CTRL			0x08
#define  GSWIP_MDIO_CTRL_BUSY		BIT(12)
#define  GSWIP_MDIO_CTRL_RD		BIT(11)
#define  GSWIP_MDIO_CTRL_WR		BIT(10)
#define  GSWIP_MDIO_CTRL_PHYAD_MASK	0x1f
#define  GSWIP_MDIO_CTRL_PHYAD_SHIFT	5
#define  GSWIP_MDIO_CTRL_REGAD_MASK	0x1f
#define GSWIP_MDIO_READ			0x09
#define GSWIP_MDIO_WRITE		0x0A
#define GSWIP_MDIO_MDC_CFG0		0x0B
#define GSWIP_MDIO_MDC_CFG1		0x0C
#define GSWIP_MDIO_PHYp(p)		(0x15 - (p))
#define  GSWIP_MDIO_PHY_LINK_MASK	0x6000
#define  GSWIP_MDIO_PHY_LINK_AUTO	0x0000
#define  GSWIP_MDIO_PHY_LINK_DOWN	0x4000
#define  GSWIP_MDIO_PHY_LINK_UP		0x2000
#define  GSWIP_MDIO_PHY_SPEED_MASK	0x1800
#define  GSWIP_MDIO_PHY_SPEED_AUTO	0x1800
#define  GSWIP_MDIO_PHY_SPEED_M10	0x0000
#define  GSWIP_MDIO_PHY_SPEED_M100	0x0800
#define  GSWIP_MDIO_PHY_SPEED_G1	0x1000
#define  GSWIP_MDIO_PHY_FDUP_MASK	0x0600
#define  GSWIP_MDIO_PHY_FDUP_AUTO	0x0000
#define  GSWIP_MDIO_PHY_FDUP_EN		0x0200
#define  GSWIP_MDIO_PHY_FDUP_DIS	0x0600
#define  GSWIP_MDIO_PHY_FCONTX_MASK	0x0180
#define  GSWIP_MDIO_PHY_FCONTX_AUTO	0x0000
#define  GSWIP_MDIO_PHY_FCONTX_EN	0x0100
#define  GSWIP_MDIO_PHY_FCONTX_DIS	0x0180
#define  GSWIP_MDIO_PHY_FCONRX_MASK	0x0060
#define  GSWIP_MDIO_PHY_FCONRX_AUTO	0x0000
#define  GSWIP_MDIO_PHY_FCONRX_EN	0x0020
#define  GSWIP_MDIO_PHY_FCONRX_DIS	0x0060
#define  GSWIP_MDIO_PHY_ADDR_MASK	0x001f
#define  GSWIP_MDIO_PHY_MASK		(GSWIP_MDIO_PHY_ADDR_MASK | \
					 GSWIP_MDIO_PHY_FCONRX_MASK | \
					 GSWIP_MDIO_PHY_FCONTX_MASK | \
					 GSWIP_MDIO_PHY_LINK_MASK | \
					 GSWIP_MDIO_PHY_SPEED_MASK | \
					 GSWIP_MDIO_PHY_FDUP_MASK)

/* GSWIP MII Registers */
#define GSWIP_MII_CFG0			0x00
#define GSWIP_MII_CFG1			0x02
#define GSWIP_MII_CFG5			0x04
#define  GSWIP_MII_CFG_EN		BIT(14)
#define  GSWIP_MII_CFG_LDCLKDIS		BIT(12)
#define  GSWIP_MII_CFG_MODE_MIIP	0x0
#define  GSWIP_MII_CFG_MODE_MIIM	0x1
#define  GSWIP_MII_CFG_MODE_RMIIP	0x2
#define  GSWIP_MII_CFG_MODE_RMIIM	0x3
#define  GSWIP_MII_CFG_MODE_RGMII	0x4
#define  GSWIP_MII_CFG_MODE_MASK	0xf
#define  GSWIP_MII_CFG_RATE_M2P5	0x00
#define  GSWIP_MII_CFG_RATE_M25	0x10
#define  GSWIP_MII_CFG_RATE_M125	0x20
#define  GSWIP_MII_CFG_RATE_M50	0x30
#define  GSWIP_MII_CFG_RATE_AUTO	0x40
#define  GSWIP_MII_CFG_RATE_MASK	0x70
#define GSWIP_MII_PCDU0			0x01
#define GSWIP_MII_PCDU1			0x03
#define GSWIP_MII_PCDU5			0x05
#define  GSWIP_MII_PCDU_TXDLY_MASK	GENMASK(2, 0)
#define  GSWIP_MII_PCDU_RXDLY_MASK	GENMASK(9, 7)

/* GSWIP Core Registers */
#define GSWIP_SWRES			0x000
#define  GSWIP_SWRES_R1			BIT(1)	/* GSWIP Software reset */
#define  GSWIP_SWRES_R0			BIT(0)	/* GSWIP Hardware reset */
#define GSWIP_VERSION			0x013
#define  GSWIP_VERSION_REV_SHIFT	0
#define  GSWIP_VERSION_REV_MASK		GENMASK(7, 0)
#define  GSWIP_VERSION_MOD_SHIFT	8
#define  GSWIP_VERSION_MOD_MASK		GENMASK(15, 8)
#define   GSWIP_VERSION_2_0		0x100
#define   GSWIP_VERSION_2_1		0x021
#define   GSWIP_VERSION_2_2		0x122
#define   GSWIP_VERSION_2_2_ETC		0x022

#define GSWIP_BM_RAM_VAL(x)		(0x043 - (x))
#define GSWIP_BM_RAM_ADDR		0x044
#define GSWIP_BM_RAM_CTRL		0x045
#define  GSWIP_BM_RAM_CTRL_BAS		BIT(15)
#define  GSWIP_BM_RAM_CTRL_OPMOD	BIT(5)
#define  GSWIP_BM_RAM_CTRL_ADDR_MASK	GENMASK(4, 0)
#define GSWIP_BM_QUEUE_GCTRL		0x04A
#define  GSWIP_BM_QUEUE_GCTRL_GL_MOD	BIT(10)
/* buffer management Port Configuration Register */
#define GSWIP_BM_PCFGp(p)		(0x080 + ((p) * 2))
#define  GSWIP_BM_PCFG_CNTEN		BIT(0)	/* RMON Counter Enable */
#define  GSWIP_BM_PCFG_IGCNT		BIT(1)	/* Ingres Special Tag RMON count */
/* buffer management Port Control Register */
#define GSWIP_BM_RMON_CTRLp(p)		(0x81 + ((p) * 2))
#define  GSWIP_BM_CTRL_RMON_RAM1_RES	BIT(0)	/* Software Reset for RMON RAM 1 */
#define  GSWIP_BM_CTRL_RMON_RAM2_RES	BIT(1)	/* Software Reset for RMON RAM 2 */

/* PCE */
#define GSWIP_PCE_TBL_KEY(x)		(0x447 - (x))
#define GSWIP_PCE_TBL_MASK		0x448
#define GSWIP_PCE_TBL_VAL(x)		(0x44D - (x))
#define GSWIP_PCE_TBL_ADDR		0x44E
#define GSWIP_PCE_TBL_CTRL		0x44F
#define  GSWIP_PCE_TBL_CTRL_BAS		BIT(15)
#define  GSWIP_PCE_TBL_CTRL_TYPE	BIT(13)
#define  GSWIP_PCE_TBL_CTRL_VLD		BIT(12)
#define  GSWIP_PCE_TBL_CTRL_KEYFORM	BIT(11)
#define  GSWIP_PCE_TBL_CTRL_GMAP_MASK	GENMASK(10, 7)
#define  GSWIP_PCE_TBL_CTRL_OPMOD_MASK	GENMASK(6, 5)
#define  GSWIP_PCE_TBL_CTRL_OPMOD_ADRD	0x00
#define  GSWIP_PCE_TBL_CTRL_OPMOD_ADWR	0x20
#define  GSWIP_PCE_TBL_CTRL_OPMOD_KSRD	0x40
#define  GSWIP_PCE_TBL_CTRL_OPMOD_KSWR	0x60
#define  GSWIP_PCE_TBL_CTRL_ADDR_MASK	GENMASK(4, 0)
#define GSWIP_PCE_PMAP1			0x453	/* Monitoring port map */
#define GSWIP_PCE_PMAP2			0x454	/* Default Multicast port map */
#define GSWIP_PCE_PMAP3			0x455	/* Default Unknown Unicast port map */
#define GSWIP_PCE_GCTRL_0		0x456
#define  GSWIP_PCE_GCTRL_0_MTFL		BIT(0)  /* MAC Table Flushing */
#define  GSWIP_PCE_GCTRL_0_MC_VALID	BIT(3)
#define  GSWIP_PCE_GCTRL_0_VLAN		BIT(14) /* VLAN aware Switching */
#define GSWIP_PCE_GCTRL_1		0x457
#define  GSWIP_PCE_GCTRL_1_MAC_GLOCK	BIT(2)	/* MAC Address table lock */
#define  GSWIP_PCE_GCTRL_1_MAC_GLOCK_MOD	BIT(3) /* Mac address table lock forwarding mode */
#define GSWIP_PCE_PCTRL_0p(p)		(0x480 + ((p) * 0xA))
#define  GSWIP_PCE_PCTRL_0_TVM		BIT(5)	/* Transparent VLAN mode */
#define  GSWIP_PCE_PCTRL_0_VREP		BIT(6)	/* VLAN Replace Mode */
#define  GSWIP_PCE_PCTRL_0_INGRESS	BIT(11)	/* Accept special tag in ingress */
#define  GSWIP_PCE_PCTRL_0_PSTATE_LISTEN	0x0
#define  GSWIP_PCE_PCTRL_0_PSTATE_RX		0x1
#define  GSWIP_PCE_PCTRL_0_PSTATE_TX		0x2
#define  GSWIP_PCE_PCTRL_0_PSTATE_LEARNING	0x3
#define  GSWIP_PCE_PCTRL_0_PSTATE_FORWARDING	0x7
#define  GSWIP_PCE_PCTRL_0_PSTATE_MASK	GENMASK(2, 0)
#define GSWIP_PCE_VCTRL(p)		(0x485 + ((p) * 0xA))
#define  GSWIP_PCE_VCTRL_UVR		BIT(0)	/* Unknown VLAN Rule */
#define  GSWIP_PCE_VCTRL_VIMR		BIT(3)	/* VLAN Ingress Member violation rule */
#define  GSWIP_PCE_VCTRL_VEMR		BIT(4)	/* VLAN Egress Member violation rule */
#define  GSWIP_PCE_VCTRL_VSR		BIT(5)	/* VLAN Security */
#define  GSWIP_PCE_VCTRL_VID0		BIT(6)	/* Priority Tagged Rule */
#define GSWIP_PCE_DEFPVID(p)		(0x486 + ((p) * 0xA))

#define GSWIP_MAC_FLEN			0x8C5
#define GSWIP_MAC_CTRL_2p(p)		(0x905 + ((p) * 0xC))
#define GSWIP_MAC_CTRL_2_MLEN		BIT(3) /* Maximum Untagged Frame Lnegth */

/* Ethernet Switch Fetch DMA Port Control Register */
#define GSWIP_FDMA_PCTRLp(p)		(0xA80 + ((p) * 0x6))
#define  GSWIP_FDMA_PCTRL_EN		BIT(0)	/* FDMA Port Enable */
#define  GSWIP_FDMA_PCTRL_STEN		BIT(1)	/* Special Tag Insertion Enable */
#define  GSWIP_FDMA_PCTRL_VLANMOD_MASK	GENMASK(4, 3)	/* VLAN Modification Control */
#define  GSWIP_FDMA_PCTRL_VLANMOD_SHIFT	3	/* VLAN Modification Control */
#define  GSWIP_FDMA_PCTRL_VLANMOD_DIS	(0x0 << GSWIP_FDMA_PCTRL_VLANMOD_SHIFT)
#define  GSWIP_FDMA_PCTRL_VLANMOD_PRIO	(0x1 << GSWIP_FDMA_PCTRL_VLANMOD_SHIFT)
#define  GSWIP_FDMA_PCTRL_VLANMOD_ID	(0x2 << GSWIP_FDMA_PCTRL_VLANMOD_SHIFT)
#define  GSWIP_FDMA_PCTRL_VLANMOD_BOTH	(0x3 << GSWIP_FDMA_PCTRL_VLANMOD_SHIFT)

/* Ethernet Switch Store DMA Port Control Register */
#define GSWIP_SDMA_PCTRLp(p)		(0xBC0 + ((p) * 0x6))
#define  GSWIP_SDMA_PCTRL_EN		BIT(0)	/* SDMA Port Enable */
#define  GSWIP_SDMA_PCTRL_FCEN		BIT(1)	/* Flow Control Enable */
#define  GSWIP_SDMA_PCTRL_PAUFWD	BIT(1)	/* Pause Frame Forwarding */

#define GSWIP_TABLE_ACTIVE_VLAN		0x01
#define GSWIP_TABLE_VLAN_MAPPING	0x02
#define GSWIP_TABLE_MAC_BRIDGE		0x0b
#define  GSWIP_TABLE_MAC_BRIDGE_STATIC	0x01	/* Static not, aging entry */

#define XRX200_GPHY_FW_ALIGN	(16 * 1024)

struct gswip_hw_info {
	int max_ports;
	int cpu_port;
};

struct xway_gphy_match_data {
	char *fe_firmware_name;
	char *ge_firmware_name;
};

struct gswip_gphy_fw {
	struct clk *clk_gate;
	struct reset_control *reset;
	u32 fw_addr_offset;
	char *fw_name;
};

struct gswip_vlan {
	struct net_device *bridge;
	u16 vid;
	u8 fid;
};

struct gswip_priv {
	__iomem void *gswip;
	__iomem void *mdio;
	__iomem void *mii;
	const struct gswip_hw_info *hw_info;
	const struct xway_gphy_match_data *gphy_fw_name_cfg;
	struct dsa_switch *ds;
	struct device *dev;
	struct regmap *rcu_regmap;
	struct gswip_vlan vlans[64];
	int num_gphy_fw;
	struct gswip_gphy_fw *gphy_fw;
	u32 port_vlan_filter;
};

struct gswip_pce_table_entry {
	u16 index;      // PCE_TBL_ADDR.ADDR = pData->table_index
	u16 table;      // PCE_TBL_CTRL.ADDR = pData->table
	u16 key[8];
	u16 val[5];
	u16 mask;
	u8 gmap;
	bool type;
	bool valid;
	bool key_mode;
};

struct gswip_rmon_cnt_desc {
	unsigned int size;
	unsigned int offset;
	const char *name;
};

#define MIB_DESC(_size, _offset, _name) {.size = _size, .offset = _offset, .name = _name}

static const struct gswip_rmon_cnt_desc gswip_rmon_cnt[] = {
	/** Receive Packet Count (only packets that are accepted and not discarded). */
	MIB_DESC(1, 0x1F, "RxGoodPkts"),
	MIB_DESC(1, 0x23, "RxUnicastPkts"),
	MIB_DESC(1, 0x22, "RxMulticastPkts"),
	MIB_DESC(1, 0x21, "RxFCSErrorPkts"),
	MIB_DESC(1, 0x1D, "RxUnderSizeGoodPkts"),
	MIB_DESC(1, 0x1E, "RxUnderSizeErrorPkts"),
	MIB_DESC(1, 0x1B, "RxOversizeGoodPkts"),
	MIB_DESC(1, 0x1C, "RxOversizeErrorPkts"),
	MIB_DESC(1, 0x20, "RxGoodPausePkts"),
	MIB_DESC(1, 0x1A, "RxAlignErrorPkts"),
	MIB_DESC(1, 0x12, "Rx64BytePkts"),
	MIB_DESC(1, 0x13, "Rx127BytePkts"),
	MIB_DESC(1, 0x14, "Rx255BytePkts"),
	MIB_DESC(1, 0x15, "Rx511BytePkts"),
	MIB_DESC(1, 0x16, "Rx1023BytePkts"),
	/** Receive Size 1024-1522 (or more, if configured) Packet Count. */
	MIB_DESC(1, 0x17, "RxMaxBytePkts"),
	MIB_DESC(1, 0x18, "RxDroppedPkts"),
	MIB_DESC(1, 0x19, "RxFilteredPkts"),
	MIB_DESC(2, 0x24, "RxGoodBytes"),
	MIB_DESC(2, 0x26, "RxBadBytes"),
	MIB_DESC(1, 0x11, "TxAcmDroppedPkts"),
	MIB_DESC(1, 0x0C, "TxGoodPkts"),
	MIB_DESC(1, 0x06, "TxUnicastPkts"),
	MIB_DESC(1, 0x07, "TxMulticastPkts"),
	MIB_DESC(1, 0x00, "Tx64BytePkts"),
	MIB_DESC(1, 0x01, "Tx127BytePkts"),
	MIB_DESC(1, 0x02, "Tx255BytePkts"),
	MIB_DESC(1, 0x03, "Tx511BytePkts"),
	MIB_DESC(1, 0x04, "Tx1023BytePkts"),
	/** Transmit Size 1024-1522 (or more, if configured) Packet Count. */
	MIB_DESC(1, 0x05, "TxMaxBytePkts"),
	MIB_DESC(1, 0x08, "TxSingleCollCount"),
	MIB_DESC(1, 0x09, "TxMultCollCount"),
	MIB_DESC(1, 0x0A, "TxLateCollCount"),
	MIB_DESC(1, 0x0B, "TxExcessCollCount"),
	MIB_DESC(1, 0x0D, "TxPauseCount"),
	MIB_DESC(1, 0x10, "TxDroppedPkts"),
	MIB_DESC(2, 0x0E, "TxGoodBytes"),
};

static u32 gswip_switch_r(struct gswip_priv *priv, u32 offset)
{
	return __raw_readl(priv->gswip + (offset * 4));
}

static void gswip_switch_w(struct gswip_priv *priv, u32 val, u32 offset)
{
	__raw_writel(val, priv->gswip + (offset * 4));
}

static void gswip_switch_mask(struct gswip_priv *priv, u32 clear, u32 set,
			      u32 offset)
{
	u32 val = gswip_switch_r(priv, offset);

	val &= ~(clear);
	val |= set;
	gswip_switch_w(priv, val, offset);
}

static u32 gswip_switch_r_timeout(struct gswip_priv *priv, u32 offset,
				  u32 cleared)
{
	u32 val;

	return readx_poll_timeout(__raw_readl, priv->gswip + (offset * 4), val,
				  (val & cleared) == 0, 20, 50000);
}

static u32 gswip_mdio_r(struct gswip_priv *priv, u32 offset)
{
	return __raw_readl(priv->mdio + (offset * 4));
}

static void gswip_mdio_w(struct gswip_priv *priv, u32 val, u32 offset)
{
	__raw_writel(val, priv->mdio + (offset * 4));
}

static void gswip_mdio_mask(struct gswip_priv *priv, u32 clear, u32 set,
			    u32 offset)
{
	u32 val = gswip_mdio_r(priv, offset);

	val &= ~(clear);
	val |= set;
	gswip_mdio_w(priv, val, offset);
}

static u32 gswip_mii_r(struct gswip_priv *priv, u32 offset)
{
	return __raw_readl(priv->mii + (offset * 4));
}

static void gswip_mii_w(struct gswip_priv *priv, u32 val, u32 offset)
{
	__raw_writel(val, priv->mii + (offset * 4));
}

static void gswip_mii_mask(struct gswip_priv *priv, u32 clear, u32 set,
			   u32 offset)
{
	u32 val = gswip_mii_r(priv, offset);

	val &= ~(clear);
	val |= set;
	gswip_mii_w(priv, val, offset);
}

static void gswip_mii_mask_cfg(struct gswip_priv *priv, u32 clear, u32 set,
			       int port)
{
	switch (port) {
	case 0:
		gswip_mii_mask(priv, clear, set, GSWIP_MII_CFG0);
		break;
	case 1:
		gswip_mii_mask(priv, clear, set, GSWIP_MII_CFG1);
		break;
	case 5:
		gswip_mii_mask(priv, clear, set, GSWIP_MII_CFG5);
		break;
	}
}

static void gswip_mii_mask_pcdu(struct gswip_priv *priv, u32 clear, u32 set,
				int port)
{
	switch (port) {
	case 0:
		gswip_mii_mask(priv, clear, set, GSWIP_MII_PCDU0);
		break;
	case 1:
		gswip_mii_mask(priv, clear, set, GSWIP_MII_PCDU1);
		break;
	case 5:
		gswip_mii_mask(priv, clear, set, GSWIP_MII_PCDU5);
		break;
	}
}

static int gswip_mdio_poll(struct gswip_priv *priv)
{
	int cnt = 100;

	while (likely(cnt--)) {
		u32 ctrl = gswip_mdio_r(priv, GSWIP_MDIO_CTRL);

		if ((ctrl & GSWIP_MDIO_CTRL_BUSY) == 0)
			return 0;
		usleep_range(20, 40);
	}

	return -ETIMEDOUT;
}

static int gswip_mdio_wr(struct mii_bus *bus, int addr, int reg, u16 val)
{
	struct gswip_priv *priv = bus->priv;
	int err;

	err = gswip_mdio_poll(priv);
	if (err) {
		dev_err(&bus->dev, "waiting for MDIO bus busy timed out\n");
		return err;
	}

	gswip_mdio_w(priv, val, GSWIP_MDIO_WRITE);
	gswip_mdio_w(priv, GSWIP_MDIO_CTRL_BUSY | GSWIP_MDIO_CTRL_WR |
		((addr & GSWIP_MDIO_CTRL_PHYAD_MASK) << GSWIP_MDIO_CTRL_PHYAD_SHIFT) |
		(reg & GSWIP_MDIO_CTRL_REGAD_MASK),
		GSWIP_MDIO_CTRL);

	return 0;
}

static int gswip_mdio_rd(struct mii_bus *bus, int addr, int reg)
{
	struct gswip_priv *priv = bus->priv;
	int err;

	err = gswip_mdio_poll(priv);
	if (err) {
		dev_err(&bus->dev, "waiting for MDIO bus busy timed out\n");
		return err;
	}

	gswip_mdio_w(priv, GSWIP_MDIO_CTRL_BUSY | GSWIP_MDIO_CTRL_RD |
		((addr & GSWIP_MDIO_CTRL_PHYAD_MASK) << GSWIP_MDIO_CTRL_PHYAD_SHIFT) |
		(reg & GSWIP_MDIO_CTRL_REGAD_MASK),
		GSWIP_MDIO_CTRL);

	err = gswip_mdio_poll(priv);
	if (err) {
		dev_err(&bus->dev, "waiting for MDIO bus busy timed out\n");
		return err;
	}

	return gswip_mdio_r(priv, GSWIP_MDIO_READ);
}

static int gswip_mdio(struct gswip_priv *priv, struct device_node *mdio_np)
{
	struct dsa_switch *ds = priv->ds;

	ds->slave_mii_bus = devm_mdiobus_alloc(priv->dev);
	if (!ds->slave_mii_bus)
		return -ENOMEM;

	ds->slave_mii_bus->priv = priv;
	ds->slave_mii_bus->read = gswip_mdio_rd;
	ds->slave_mii_bus->write = gswip_mdio_wr;
	ds->slave_mii_bus->name = "lantiq,xrx200-mdio";
	snprintf(ds->slave_mii_bus->id, MII_BUS_ID_SIZE, "%s-mii",
		 dev_name(priv->dev));
	ds->slave_mii_bus->parent = priv->dev;
	ds->slave_mii_bus->phy_mask = ~ds->phys_mii_mask;

	return of_mdiobus_register(ds->slave_mii_bus, mdio_np);
}

static int gswip_pce_table_entry_read(struct gswip_priv *priv,
				      struct gswip_pce_table_entry *tbl)
{
	int i;
	int err;
	u16 crtl;
	u16 addr_mode = tbl->key_mode ? GSWIP_PCE_TBL_CTRL_OPMOD_KSRD :
					GSWIP_PCE_TBL_CTRL_OPMOD_ADRD;

	err = gswip_switch_r_timeout(priv, GSWIP_PCE_TBL_CTRL,
				     GSWIP_PCE_TBL_CTRL_BAS);
	if (err)
		return err;

	gswip_switch_w(priv, tbl->index, GSWIP_PCE_TBL_ADDR);
	gswip_switch_mask(priv, GSWIP_PCE_TBL_CTRL_ADDR_MASK |
				GSWIP_PCE_TBL_CTRL_OPMOD_MASK,
			  tbl->table | addr_mode | GSWIP_PCE_TBL_CTRL_BAS,
			  GSWIP_PCE_TBL_CTRL);

	err = gswip_switch_r_timeout(priv, GSWIP_PCE_TBL_CTRL,
				     GSWIP_PCE_TBL_CTRL_BAS);
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(tbl->key); i++)
		tbl->key[i] = gswip_switch_r(priv, GSWIP_PCE_TBL_KEY(i));

	for (i = 0; i < ARRAY_SIZE(tbl->val); i++)
		tbl->val[i] = gswip_switch_r(priv, GSWIP_PCE_TBL_VAL(i));

	tbl->mask = gswip_switch_r(priv, GSWIP_PCE_TBL_MASK);

	crtl = gswip_switch_r(priv, GSWIP_PCE_TBL_CTRL);

	tbl->type = !!(crtl & GSWIP_PCE_TBL_CTRL_TYPE);
	tbl->valid = !!(crtl & GSWIP_PCE_TBL_CTRL_VLD);
	tbl->gmap = (crtl & GSWIP_PCE_TBL_CTRL_GMAP_MASK) >> 7;

	return 0;
}

static int gswip_pce_table_entry_write(struct gswip_priv *priv,
				       struct gswip_pce_table_entry *tbl)
{
	int i;
	int err;
	u16 crtl;
	u16 addr_mode = tbl->key_mode ? GSWIP_PCE_TBL_CTRL_OPMOD_KSWR :
					GSWIP_PCE_TBL_CTRL_OPMOD_ADWR;

	err = gswip_switch_r_timeout(priv, GSWIP_PCE_TBL_CTRL,
				     GSWIP_PCE_TBL_CTRL_BAS);
	if (err)
		return err;

	gswip_switch_w(priv, tbl->index, GSWIP_PCE_TBL_ADDR);
	gswip_switch_mask(priv, GSWIP_PCE_TBL_CTRL_ADDR_MASK |
				GSWIP_PCE_TBL_CTRL_OPMOD_MASK,
			  tbl->table | addr_mode,
			  GSWIP_PCE_TBL_CTRL);

	for (i = 0; i < ARRAY_SIZE(tbl->key); i++)
		gswip_switch_w(priv, tbl->key[i], GSWIP_PCE_TBL_KEY(i));

	for (i = 0; i < ARRAY_SIZE(tbl->val); i++)
		gswip_switch_w(priv, tbl->val[i], GSWIP_PCE_TBL_VAL(i));

	gswip_switch_mask(priv, GSWIP_PCE_TBL_CTRL_ADDR_MASK |
				GSWIP_PCE_TBL_CTRL_OPMOD_MASK,
			  tbl->table | addr_mode,
			  GSWIP_PCE_TBL_CTRL);

	gswip_switch_w(priv, tbl->mask, GSWIP_PCE_TBL_MASK);

	crtl = gswip_switch_r(priv, GSWIP_PCE_TBL_CTRL);
	crtl &= ~(GSWIP_PCE_TBL_CTRL_TYPE | GSWIP_PCE_TBL_CTRL_VLD |
		  GSWIP_PCE_TBL_CTRL_GMAP_MASK);
	if (tbl->type)
		crtl |= GSWIP_PCE_TBL_CTRL_TYPE;
	if (tbl->valid)
		crtl |= GSWIP_PCE_TBL_CTRL_VLD;
	crtl |= (tbl->gmap << 7) & GSWIP_PCE_TBL_CTRL_GMAP_MASK;
	crtl |= GSWIP_PCE_TBL_CTRL_BAS;
	gswip_switch_w(priv, crtl, GSWIP_PCE_TBL_CTRL);

	return gswip_switch_r_timeout(priv, GSWIP_PCE_TBL_CTRL,
				      GSWIP_PCE_TBL_CTRL_BAS);
}

/* Add the LAN port into a bridge with the CPU port by
 * default. This prevents automatic forwarding of
 * packages between the LAN ports when no explicit
 * bridge is configured.
 */
static int gswip_add_single_port_br(struct gswip_priv *priv, int port, bool add)
{
	struct gswip_pce_table_entry vlan_active = {0,};
	struct gswip_pce_table_entry vlan_mapping = {0,};
	unsigned int cpu_port = priv->hw_info->cpu_port;
	unsigned int max_ports = priv->hw_info->max_ports;
	int err;

	if (port >= max_ports) {
		dev_err(priv->dev, "single port for %i supported\n", port);
		return -EIO;
	}

	vlan_active.index = port + 1;
	vlan_active.table = GSWIP_TABLE_ACTIVE_VLAN;
	vlan_active.key[0] = 0; /* vid */
	vlan_active.val[0] = port + 1 /* fid */;
	vlan_active.valid = add;
	err = gswip_pce_table_entry_write(priv, &vlan_active);
	if (err) {
		dev_err(priv->dev, "failed to write active VLAN: %d\n", err);
		return err;
	}

	if (!add)
		return 0;

	vlan_mapping.index = port + 1;
	vlan_mapping.table = GSWIP_TABLE_VLAN_MAPPING;
	vlan_mapping.val[0] = 0 /* vid */;
	vlan_mapping.val[1] = BIT(port) | BIT(cpu_port);
	vlan_mapping.val[2] = 0;
	err = gswip_pce_table_entry_write(priv, &vlan_mapping);
	if (err) {
		dev_err(priv->dev, "failed to write VLAN mapping: %d\n", err);
		return err;
	}

	return 0;
}

static int gswip_port_enable(struct dsa_switch *ds, int port,
			     struct phy_device *phydev)
{
	struct gswip_priv *priv = ds->priv;
	int err;

	if (!dsa_is_user_port(ds, port))
		return 0;

	if (!dsa_is_cpu_port(ds, port)) {
		err = gswip_add_single_port_br(priv, port, true);
		if (err)
			return err;
	}

	/* RMON Counter Enable for port */
	gswip_switch_w(priv, GSWIP_BM_PCFG_CNTEN, GSWIP_BM_PCFGp(port));

	/* enable port fetch/store dma & VLAN Modification */
	gswip_switch_mask(priv, 0, GSWIP_FDMA_PCTRL_EN |
				   GSWIP_FDMA_PCTRL_VLANMOD_BOTH,
			 GSWIP_FDMA_PCTRLp(port));
	gswip_switch_mask(priv, 0, GSWIP_SDMA_PCTRL_EN,
			  GSWIP_SDMA_PCTRLp(port));

	if (!dsa_is_cpu_port(ds, port)) {
		u32 macconf = GSWIP_MDIO_PHY_LINK_AUTO |
			      GSWIP_MDIO_PHY_SPEED_AUTO |
			      GSWIP_MDIO_PHY_FDUP_AUTO |
			      GSWIP_MDIO_PHY_FCONTX_AUTO |
			      GSWIP_MDIO_PHY_FCONRX_AUTO |
			      (phydev->mdio.addr & GSWIP_MDIO_PHY_ADDR_MASK);

		gswip_mdio_w(priv, macconf, GSWIP_MDIO_PHYp(port));
		/* Activate MDIO auto polling */
		gswip_mdio_mask(priv, 0, BIT(port), GSWIP_MDIO_MDC_CFG0);
	}

	return 0;
}

static void gswip_port_disable(struct dsa_switch *ds, int port)
{
	struct gswip_priv *priv = ds->priv;

	if (!dsa_is_user_port(ds, port))
		return;

	if (!dsa_is_cpu_port(ds, port)) {
		gswip_mdio_mask(priv, GSWIP_MDIO_PHY_LINK_DOWN,
				GSWIP_MDIO_PHY_LINK_MASK,
				GSWIP_MDIO_PHYp(port));
		/* Deactivate MDIO auto polling */
		gswip_mdio_mask(priv, BIT(port), 0, GSWIP_MDIO_MDC_CFG0);
	}

	gswip_switch_mask(priv, GSWIP_FDMA_PCTRL_EN, 0,
			  GSWIP_FDMA_PCTRLp(port));
	gswip_switch_mask(priv, GSWIP_SDMA_PCTRL_EN, 0,
			  GSWIP_SDMA_PCTRLp(port));
}

static int gswip_pce_load_microcode(struct gswip_priv *priv)
{
	int i;
	int err;

	gswip_switch_mask(priv, GSWIP_PCE_TBL_CTRL_ADDR_MASK |
				GSWIP_PCE_TBL_CTRL_OPMOD_MASK,
			  GSWIP_PCE_TBL_CTRL_OPMOD_ADWR, GSWIP_PCE_TBL_CTRL);
	gswip_switch_w(priv, 0, GSWIP_PCE_TBL_MASK);

	for (i = 0; i < ARRAY_SIZE(gswip_pce_microcode); i++) {
		gswip_switch_w(priv, i, GSWIP_PCE_TBL_ADDR);
		gswip_switch_w(priv, gswip_pce_microcode[i].val_0,
			       GSWIP_PCE_TBL_VAL(0));
		gswip_switch_w(priv, gswip_pce_microcode[i].val_1,
			       GSWIP_PCE_TBL_VAL(1));
		gswip_switch_w(priv, gswip_pce_microcode[i].val_2,
			       GSWIP_PCE_TBL_VAL(2));
		gswip_switch_w(priv, gswip_pce_microcode[i].val_3,
			       GSWIP_PCE_TBL_VAL(3));

		/* start the table access: */
		gswip_switch_mask(priv, 0, GSWIP_PCE_TBL_CTRL_BAS,
				  GSWIP_PCE_TBL_CTRL);
		err = gswip_switch_r_timeout(priv, GSWIP_PCE_TBL_CTRL,
					     GSWIP_PCE_TBL_CTRL_BAS);
		if (err)
			return err;
	}

	/* tell the switch that the microcode is loaded */
	gswip_switch_mask(priv, 0, GSWIP_PCE_GCTRL_0_MC_VALID,
			  GSWIP_PCE_GCTRL_0);

	return 0;
}

static int gswip_port_vlan_filtering(struct dsa_switch *ds, int port,
				     bool vlan_filtering,
				     struct switchdev_trans *trans)
{
	struct gswip_priv *priv = ds->priv;

	/* Do not allow changing the VLAN filtering options while in bridge */
	if (switchdev_trans_ph_prepare(trans)) {
		struct net_device *bridge = dsa_to_port(ds, port)->bridge_dev;

		if (!bridge)
			return 0;

		if (!!(priv->port_vlan_filter & BIT(port)) != vlan_filtering)
			return -EIO;

		return 0;
	}

	if (vlan_filtering) {
		/* Use port based VLAN tag */
		gswip_switch_mask(priv,
				  GSWIP_PCE_VCTRL_VSR,
				  GSWIP_PCE_VCTRL_UVR | GSWIP_PCE_VCTRL_VIMR |
				  GSWIP_PCE_VCTRL_VEMR,
				  GSWIP_PCE_VCTRL(port));
		gswip_switch_mask(priv, GSWIP_PCE_PCTRL_0_TVM, 0,
				  GSWIP_PCE_PCTRL_0p(port));
	} else {
		/* Use port based VLAN tag */
		gswip_switch_mask(priv,
				  GSWIP_PCE_VCTRL_UVR | GSWIP_PCE_VCTRL_VIMR |
				  GSWIP_PCE_VCTRL_VEMR,
				  GSWIP_PCE_VCTRL_VSR,
				  GSWIP_PCE_VCTRL(port));
		gswip_switch_mask(priv, 0, GSWIP_PCE_PCTRL_0_TVM,
				  GSWIP_PCE_PCTRL_0p(port));
	}

	return 0;
}

static int gswip_setup(struct dsa_switch *ds)
{
	struct gswip_priv *priv = ds->priv;
	unsigned int cpu_port = priv->hw_info->cpu_port;
	int i;
	int err;

	gswip_switch_w(priv, GSWIP_SWRES_R0, GSWIP_SWRES);
	usleep_range(5000, 10000);
	gswip_switch_w(priv, 0, GSWIP_SWRES);

	/* disable port fetch/store dma on all ports */
	for (i = 0; i < priv->hw_info->max_ports; i++) {
		struct switchdev_trans trans;

		/* Skip the prepare phase, this shouldn't return an error
		 * during setup.
		 */
		trans.ph_prepare = false;

		gswip_port_disable(ds, i);
		gswip_port_vlan_filtering(ds, i, false, &trans);
	}

	/* enable Switch */
	gswip_mdio_mask(priv, 0, GSWIP_MDIO_GLOB_ENABLE, GSWIP_MDIO_GLOB);

	err = gswip_pce_load_microcode(priv);
	if (err) {
		dev_err(priv->dev, "writing PCE microcode failed, %i", err);
		return err;
	}

	/* Default unknown Broadcast/Multicast/Unicast port maps */
	gswip_switch_w(priv, BIT(cpu_port), GSWIP_PCE_PMAP1);
	gswip_switch_w(priv, BIT(cpu_port), GSWIP_PCE_PMAP2);
	gswip_switch_w(priv, BIT(cpu_port), GSWIP_PCE_PMAP3);

	/* disable PHY auto polling */
	gswip_mdio_w(priv, 0x0, GSWIP_MDIO_MDC_CFG0);
	/* Configure the MDIO Clock 2.5 MHz */
	gswip_mdio_mask(priv, 0xff, 0x09, GSWIP_MDIO_MDC_CFG1);

	/* Disable the xMII link */
	gswip_mii_mask_cfg(priv, GSWIP_MII_CFG_EN, 0, 0);
	gswip_mii_mask_cfg(priv, GSWIP_MII_CFG_EN, 0, 1);
	gswip_mii_mask_cfg(priv, GSWIP_MII_CFG_EN, 0, 5);

	/* enable special tag insertion on cpu port */
	gswip_switch_mask(priv, 0, GSWIP_FDMA_PCTRL_STEN,
			  GSWIP_FDMA_PCTRLp(cpu_port));

	/* accept special tag in ingress direction */
	gswip_switch_mask(priv, 0, GSWIP_PCE_PCTRL_0_INGRESS,
			  GSWIP_PCE_PCTRL_0p(cpu_port));

	gswip_switch_mask(priv, 0, GSWIP_MAC_CTRL_2_MLEN,
			  GSWIP_MAC_CTRL_2p(cpu_port));
	gswip_switch_w(priv, VLAN_ETH_FRAME_LEN + 8, GSWIP_MAC_FLEN);
	gswip_switch_mask(priv, 0, GSWIP_BM_QUEUE_GCTRL_GL_MOD,
			  GSWIP_BM_QUEUE_GCTRL);

	/* VLAN aware Switching */
	gswip_switch_mask(priv, 0, GSWIP_PCE_GCTRL_0_VLAN, GSWIP_PCE_GCTRL_0);

	/* Flush MAC Table */
	gswip_switch_mask(priv, 0, GSWIP_PCE_GCTRL_0_MTFL, GSWIP_PCE_GCTRL_0);

	err = gswip_switch_r_timeout(priv, GSWIP_PCE_GCTRL_0,
				     GSWIP_PCE_GCTRL_0_MTFL);
	if (err) {
		dev_err(priv->dev, "MAC flushing didn't finish\n");
		return err;
	}

	gswip_port_enable(ds, cpu_port, NULL);
	return 0;
}

static enum dsa_tag_protocol gswip_get_tag_protocol(struct dsa_switch *ds,
						    int port,
						    enum dsa_tag_protocol mp)
{
	return DSA_TAG_PROTO_GSWIP;
}

static int gswip_vlan_active_create(struct gswip_priv *priv,
				    struct net_device *bridge,
				    int fid, u16 vid)
{
	struct gswip_pce_table_entry vlan_active = {0,};
	unsigned int max_ports = priv->hw_info->max_ports;
	int idx = -1;
	int err;
	int i;

	/* Look for a free slot */
	for (i = max_ports; i < ARRAY_SIZE(priv->vlans); i++) {
		if (!priv->vlans[i].bridge) {
			idx = i;
			break;
		}
	}

	if (idx == -1)
		return -ENOSPC;

	if (fid == -1)
		fid = idx;

	vlan_active.index = idx;
	vlan_active.table = GSWIP_TABLE_ACTIVE_VLAN;
	vlan_active.key[0] = vid;
	vlan_active.val[0] = fid;
	vlan_active.valid = true;

	err = gswip_pce_table_entry_write(priv, &vlan_active);
	if (err) {
		dev_err(priv->dev, "failed to write active VLAN: %d\n",	err);
		return err;
	}

	priv->vlans[idx].bridge = bridge;
	priv->vlans[idx].vid = vid;
	priv->vlans[idx].fid = fid;

	return idx;
}

static int gswip_vlan_active_remove(struct gswip_priv *priv, int idx)
{
	struct gswip_pce_table_entry vlan_active = {0,};
	int err;

	vlan_active.index = idx;
	vlan_active.table = GSWIP_TABLE_ACTIVE_VLAN;
	vlan_active.valid = false;
	err = gswip_pce_table_entry_write(priv, &vlan_active);
	if (err)
		dev_err(priv->dev, "failed to delete active VLAN: %d\n", err);
	priv->vlans[idx].bridge = NULL;

	return err;
}

static int gswip_vlan_add_unaware(struct gswip_priv *priv,
				  struct net_device *bridge, int port)
{
	struct gswip_pce_table_entry vlan_mapping = {0,};
	unsigned int max_ports = priv->hw_info->max_ports;
	unsigned int cpu_port = priv->hw_info->cpu_port;
	bool active_vlan_created = false;
	int idx = -1;
	int i;
	int err;

	/* Check if there is already a page for this bridge */
	for (i = max_ports; i < ARRAY_SIZE(priv->vlans); i++) {
		if (priv->vlans[i].bridge == bridge) {
			idx = i;
			break;
		}
	}

	/* If this bridge is not programmed yet, add a Active VLAN table
	 * entry in a free slot and prepare the VLAN mapping table entry.
	 */
	if (idx == -1) {
		idx = gswip_vlan_active_create(priv, bridge, -1, 0);
		if (idx < 0)
			return idx;
		active_vlan_created = true;

		vlan_mapping.index = idx;
		vlan_mapping.table = GSWIP_TABLE_VLAN_MAPPING;
		/* VLAN ID byte, maps to the VLAN ID of vlan active table */
		vlan_mapping.val[0] = 0;
	} else {
		/* Read the existing VLAN mapping entry from the switch */
		vlan_mapping.index = idx;
		vlan_mapping.table = GSWIP_TABLE_VLAN_MAPPING;
		err = gswip_pce_table_entry_read(priv, &vlan_mapping);
		if (err) {
			dev_err(priv->dev, "failed to read VLAN mapping: %d\n",
				err);
			return err;
		}
	}

	/* Update the VLAN mapping entry and write it to the switch */
	vlan_mapping.val[1] |= BIT(cpu_port);
	vlan_mapping.val[1] |= BIT(port);
	err = gswip_pce_table_entry_write(priv, &vlan_mapping);
	if (err) {
		dev_err(priv->dev, "failed to write VLAN mapping: %d\n", err);
		/* In case an Active VLAN was creaetd delete it again */
		if (active_vlan_created)
			gswip_vlan_active_remove(priv, idx);
		return err;
	}

	gswip_switch_w(priv, 0, GSWIP_PCE_DEFPVID(port));
	return 0;
}

static int gswip_vlan_add_aware(struct gswip_priv *priv,
				struct net_device *bridge, int port,
				u16 vid, bool untagged,
				bool pvid)
{
	struct gswip_pce_table_entry vlan_mapping = {0,};
	unsigned int max_ports = priv->hw_info->max_ports;
	unsigned int cpu_port = priv->hw_info->cpu_port;
	bool active_vlan_created = false;
	int idx = -1;
	int fid = -1;
	int i;
	int err;

	/* Check if there is already a page for this bridge */
	for (i = max_ports; i < ARRAY_SIZE(priv->vlans); i++) {
		if (priv->vlans[i].bridge == bridge) {
			if (fid != -1 && fid != priv->vlans[i].fid)
				dev_err(priv->dev, "one bridge with multiple flow ids\n");
			fid = priv->vlans[i].fid;
			if (priv->vlans[i].vid == vid) {
				idx = i;
				break;
			}
		}
	}

	/* If this bridge is not programmed yet, add a Active VLAN table
	 * entry in a free slot and prepare the VLAN mapping table entry.
	 */
	if (idx == -1) {
		idx = gswip_vlan_active_create(priv, bridge, fid, vid);
		if (idx < 0)
			return idx;
		active_vlan_created = true;

		vlan_mapping.index = idx;
		vlan_mapping.table = GSWIP_TABLE_VLAN_MAPPING;
		/* VLAN ID byte, maps to the VLAN ID of vlan active table */
		vlan_mapping.val[0] = vid;
	} else {
		/* Read the existing VLAN mapping entry from the switch */
		vlan_mapping.index = idx;
		vlan_mapping.table = GSWIP_TABLE_VLAN_MAPPING;
		err = gswip_pce_table_entry_read(priv, &vlan_mapping);
		if (err) {
			dev_err(priv->dev, "failed to read VLAN mapping: %d\n",
				err);
			return err;
		}
	}

	vlan_mapping.val[0] = vid;
	/* Update the VLAN mapping entry and write it to the switch */
	vlan_mapping.val[1] |= BIT(cpu_port);
	vlan_mapping.val[2] |= BIT(cpu_port);
	vlan_mapping.val[1] |= BIT(port);
	if (untagged)
		vlan_mapping.val[2] &= ~BIT(port);
	else
		vlan_mapping.val[2] |= BIT(port);
	err = gswip_pce_table_entry_write(priv, &vlan_mapping);
	if (err) {
		dev_err(priv->dev, "failed to write VLAN mapping: %d\n", err);
		/* In case an Active VLAN was creaetd delete it again */
		if (active_vlan_created)
			gswip_vlan_active_remove(priv, idx);
		return err;
	}

	if (pvid)
		gswip_switch_w(priv, idx, GSWIP_PCE_DEFPVID(port));

	return 0;
}

static int gswip_vlan_remove(struct gswip_priv *priv,
			     struct net_device *bridge, int port,
			     u16 vid, bool pvid, bool vlan_aware)
{
	struct gswip_pce_table_entry vlan_mapping = {0,};
	unsigned int max_ports = priv->hw_info->max_ports;
	unsigned int cpu_port = priv->hw_info->cpu_port;
	int idx = -1;
	int i;
	int err;

	/* Check if there is already a page for this bridge */
	for (i = max_ports; i < ARRAY_SIZE(priv->vlans); i++) {
		if (priv->vlans[i].bridge == bridge &&
		    (!vlan_aware || priv->vlans[i].vid == vid)) {
			idx = i;
			break;
		}
	}

	if (idx == -1) {
		dev_err(priv->dev, "bridge to leave does not exists\n");
		return -ENOENT;
	}

	vlan_mapping.index = idx;
	vlan_mapping.table = GSWIP_TABLE_VLAN_MAPPING;
	err = gswip_pce_table_entry_read(priv, &vlan_mapping);
	if (err) {
		dev_err(priv->dev, "failed to read VLAN mapping: %d\n",	err);
		return err;
	}

	vlan_mapping.val[1] &= ~BIT(port);
	vlan_mapping.val[2] &= ~BIT(port);
	err = gswip_pce_table_entry_write(priv, &vlan_mapping);
	if (err) {
		dev_err(priv->dev, "failed to write VLAN mapping: %d\n", err);
		return err;
	}

	/* In case all ports are removed from the bridge, remove the VLAN */
	if ((vlan_mapping.val[1] & ~BIT(cpu_port)) == 0) {
		err = gswip_vlan_active_remove(priv, idx);
		if (err) {
			dev_err(priv->dev, "failed to write active VLAN: %d\n",
				err);
			return err;
		}
	}

	/* GSWIP 2.2 (GRX300) and later program here the VID directly. */
	if (pvid)
		gswip_switch_w(priv, 0, GSWIP_PCE_DEFPVID(port));

	return 0;
}

static int gswip_port_bridge_join(struct dsa_switch *ds, int port,
				  struct net_device *bridge)
{
	struct gswip_priv *priv = ds->priv;
	int err;

	/* When the bridge uses VLAN filtering we have to configure VLAN
	 * specific bridges. No bridge is configured here.
	 */
	if (!br_vlan_enabled(bridge)) {
		err = gswip_vlan_add_unaware(priv, bridge, port);
		if (err)
			return err;
		priv->port_vlan_filter &= ~BIT(port);
	} else {
		priv->port_vlan_filter |= BIT(port);
	}
	return gswip_add_single_port_br(priv, port, false);
}

static void gswip_port_bridge_leave(struct dsa_switch *ds, int port,
				    struct net_device *bridge)
{
	struct gswip_priv *priv = ds->priv;

	gswip_add_single_port_br(priv, port, true);

	/* When the bridge uses VLAN filtering we have to configure VLAN
	 * specific bridges. No bridge is configured here.
	 */
	if (!br_vlan_enabled(bridge))
		gswip_vlan_remove(priv, bridge, port, 0, true, false);
}

static int gswip_port_vlan_prepare(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_vlan *vlan)
{
	struct gswip_priv *priv = ds->priv;
	struct net_device *bridge = dsa_to_port(ds, port)->bridge_dev;
	unsigned int max_ports = priv->hw_info->max_ports;
	u16 vid;
	int i;
	int pos = max_ports;

	/* We only support VLAN filtering on bridges */
	if (!dsa_is_cpu_port(ds, port) && !bridge)
		return -EOPNOTSUPP;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid) {
		int idx = -1;

		/* Check if there is already a page for this VLAN */
		for (i = max_ports; i < ARRAY_SIZE(priv->vlans); i++) {
			if (priv->vlans[i].bridge == bridge &&
			    priv->vlans[i].vid == vid) {
				idx = i;
				break;
			}
		}

		/* If this VLAN is not programmed yet, we have to reserve
		 * one entry in the VLAN table. Make sure we start at the
		 * next position round.
		 */
		if (idx == -1) {
			/* Look for a free slot */
			for (; pos < ARRAY_SIZE(priv->vlans); pos++) {
				if (!priv->vlans[pos].bridge) {
					idx = pos;
					pos++;
					break;
				}
			}

			if (idx == -1)
				return -ENOSPC;
		}
	}

	return 0;
}

static void gswip_port_vlan_add(struct dsa_switch *ds, int port,
				const struct switchdev_obj_port_vlan *vlan)
{
	struct gswip_priv *priv = ds->priv;
	struct net_device *bridge = dsa_to_port(ds, port)->bridge_dev;
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	u16 vid;

	/* We have to receive all packets on the CPU port and should not
	 * do any VLAN filtering here. This is also called with bridge
	 * NULL and then we do not know for which bridge to configure
	 * this.
	 */
	if (dsa_is_cpu_port(ds, port))
		return;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid)
		gswip_vlan_add_aware(priv, bridge, port, vid, untagged, pvid);
}

static int gswip_port_vlan_del(struct dsa_switch *ds, int port,
			       const struct switchdev_obj_port_vlan *vlan)
{
	struct gswip_priv *priv = ds->priv;
	struct net_device *bridge = dsa_to_port(ds, port)->bridge_dev;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	u16 vid;
	int err;

	/* We have to receive all packets on the CPU port and should not
	 * do any VLAN filtering here. This is also called with bridge
	 * NULL and then we do not know for which bridge to configure
	 * this.
	 */
	if (dsa_is_cpu_port(ds, port))
		return 0;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid) {
		err = gswip_vlan_remove(priv, bridge, port, vid, pvid, true);
		if (err)
			return err;
	}

	return 0;
}

static void gswip_port_fast_age(struct dsa_switch *ds, int port)
{
	struct gswip_priv *priv = ds->priv;
	struct gswip_pce_table_entry mac_bridge = {0,};
	int i;
	int err;

	for (i = 0; i < 2048; i++) {
		mac_bridge.table = GSWIP_TABLE_MAC_BRIDGE;
		mac_bridge.index = i;

		err = gswip_pce_table_entry_read(priv, &mac_bridge);
		if (err) {
			dev_err(priv->dev, "failed to read mac bridge: %d\n",
				err);
			return;
		}

		if (!mac_bridge.valid)
			continue;

		if (mac_bridge.val[1] & GSWIP_TABLE_MAC_BRIDGE_STATIC)
			continue;

		if (((mac_bridge.val[0] & GENMASK(7, 4)) >> 4) != port)
			continue;

		mac_bridge.valid = false;
		err = gswip_pce_table_entry_write(priv, &mac_bridge);
		if (err) {
			dev_err(priv->dev, "failed to write mac bridge: %d\n",
				err);
			return;
		}
	}
}

static void gswip_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	struct gswip_priv *priv = ds->priv;
	u32 stp_state;

	switch (state) {
	case BR_STATE_DISABLED:
		gswip_switch_mask(priv, GSWIP_SDMA_PCTRL_EN, 0,
				  GSWIP_SDMA_PCTRLp(port));
		return;
	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		stp_state = GSWIP_PCE_PCTRL_0_PSTATE_LISTEN;
		break;
	case BR_STATE_LEARNING:
		stp_state = GSWIP_PCE_PCTRL_0_PSTATE_LEARNING;
		break;
	case BR_STATE_FORWARDING:
		stp_state = GSWIP_PCE_PCTRL_0_PSTATE_FORWARDING;
		break;
	default:
		dev_err(priv->dev, "invalid STP state: %d\n", state);
		return;
	}

	gswip_switch_mask(priv, 0, GSWIP_SDMA_PCTRL_EN,
			  GSWIP_SDMA_PCTRLp(port));
	gswip_switch_mask(priv, GSWIP_PCE_PCTRL_0_PSTATE_MASK, stp_state,
			  GSWIP_PCE_PCTRL_0p(port));
}

static int gswip_port_fdb(struct dsa_switch *ds, int port,
			  const unsigned char *addr, u16 vid, bool add)
{
	struct gswip_priv *priv = ds->priv;
	struct net_device *bridge = dsa_to_port(ds, port)->bridge_dev;
	struct gswip_pce_table_entry mac_bridge = {0,};
	unsigned int cpu_port = priv->hw_info->cpu_port;
	int fid = -1;
	int i;
	int err;

	if (!bridge)
		return -EINVAL;

	for (i = cpu_port; i < ARRAY_SIZE(priv->vlans); i++) {
		if (priv->vlans[i].bridge == bridge) {
			fid = priv->vlans[i].fid;
			break;
		}
	}

	if (fid == -1) {
		dev_err(priv->dev, "Port not part of a bridge\n");
		return -EINVAL;
	}

	mac_bridge.table = GSWIP_TABLE_MAC_BRIDGE;
	mac_bridge.key_mode = true;
	mac_bridge.key[0] = addr[5] | (addr[4] << 8);
	mac_bridge.key[1] = addr[3] | (addr[2] << 8);
	mac_bridge.key[2] = addr[1] | (addr[0] << 8);
	mac_bridge.key[3] = fid;
	mac_bridge.val[0] = add ? BIT(port) : 0; /* port map */
	mac_bridge.val[1] = GSWIP_TABLE_MAC_BRIDGE_STATIC;
	mac_bridge.valid = add;

	err = gswip_pce_table_entry_write(priv, &mac_bridge);
	if (err)
		dev_err(priv->dev, "failed to write mac bridge: %d\n", err);

	return err;
}

static int gswip_port_fdb_add(struct dsa_switch *ds, int port,
			      const unsigned char *addr, u16 vid)
{
	return gswip_port_fdb(ds, port, addr, vid, true);
}

static int gswip_port_fdb_del(struct dsa_switch *ds, int port,
			      const unsigned char *addr, u16 vid)
{
	return gswip_port_fdb(ds, port, addr, vid, false);
}

static int gswip_port_fdb_dump(struct dsa_switch *ds, int port,
			       dsa_fdb_dump_cb_t *cb, void *data)
{
	struct gswip_priv *priv = ds->priv;
	struct gswip_pce_table_entry mac_bridge = {0,};
	unsigned char addr[6];
	int i;
	int err;

	for (i = 0; i < 2048; i++) {
		mac_bridge.table = GSWIP_TABLE_MAC_BRIDGE;
		mac_bridge.index = i;

		err = gswip_pce_table_entry_read(priv, &mac_bridge);
		if (err) {
			dev_err(priv->dev, "failed to write mac bridge: %d\n",
				err);
			return err;
		}

		if (!mac_bridge.valid)
			continue;

		addr[5] = mac_bridge.key[0] & 0xff;
		addr[4] = (mac_bridge.key[0] >> 8) & 0xff;
		addr[3] = mac_bridge.key[1] & 0xff;
		addr[2] = (mac_bridge.key[1] >> 8) & 0xff;
		addr[1] = mac_bridge.key[2] & 0xff;
		addr[0] = (mac_bridge.key[2] >> 8) & 0xff;
		if (mac_bridge.val[1] & GSWIP_TABLE_MAC_BRIDGE_STATIC) {
			if (mac_bridge.val[0] & BIT(port))
				cb(addr, 0, true, data);
		} else {
			if (((mac_bridge.val[0] & GENMASK(7, 4)) >> 4) == port)
				cb(addr, 0, false, data);
		}
	}
	return 0;
}

static void gswip_phylink_validate(struct dsa_switch *ds, int port,
				   unsigned long *supported,
				   struct phylink_link_state *state)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	switch (port) {
	case 0:
	case 1:
		if (!phy_interface_mode_is_rgmii(state->interface) &&
		    state->interface != PHY_INTERFACE_MODE_MII &&
		    state->interface != PHY_INTERFACE_MODE_REVMII &&
		    state->interface != PHY_INTERFACE_MODE_RMII)
			goto unsupported;
		break;
	case 2:
	case 3:
	case 4:
		if (state->interface != PHY_INTERFACE_MODE_INTERNAL)
			goto unsupported;
		break;
	case 5:
		if (!phy_interface_mode_is_rgmii(state->interface) &&
		    state->interface != PHY_INTERFACE_MODE_INTERNAL)
			goto unsupported;
		break;
	default:
		bitmap_zero(supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
		dev_err(ds->dev, "Unsupported port: %i\n", port);
		return;
	}

	/* Allow all the expected bits */
	phylink_set(mask, Autoneg);
	phylink_set_port_modes(mask);
	phylink_set(mask, Pause);
	phylink_set(mask, Asym_Pause);

	/* With the exclusion of MII and Reverse MII, we support Gigabit,
	 * including Half duplex
	 */
	if (state->interface != PHY_INTERFACE_MODE_MII &&
	    state->interface != PHY_INTERFACE_MODE_REVMII) {
		phylink_set(mask, 1000baseT_Full);
		phylink_set(mask, 1000baseT_Half);
	}

	phylink_set(mask, 10baseT_Half);
	phylink_set(mask, 10baseT_Full);
	phylink_set(mask, 100baseT_Half);
	phylink_set(mask, 100baseT_Full);

	bitmap_and(supported, supported, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
	return;

unsupported:
	bitmap_zero(supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
	dev_err(ds->dev, "Unsupported interface '%s' for port %d\n",
		phy_modes(state->interface), port);
	return;
}

static void gswip_phylink_mac_config(struct dsa_switch *ds, int port,
				     unsigned int mode,
				     const struct phylink_link_state *state)
{
	struct gswip_priv *priv = ds->priv;
	u32 miicfg = 0;

	miicfg |= GSWIP_MII_CFG_LDCLKDIS;

	switch (state->interface) {
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_INTERNAL:
		miicfg |= GSWIP_MII_CFG_MODE_MIIM;
		break;
	case PHY_INTERFACE_MODE_REVMII:
		miicfg |= GSWIP_MII_CFG_MODE_MIIP;
		break;
	case PHY_INTERFACE_MODE_RMII:
		miicfg |= GSWIP_MII_CFG_MODE_RMIIM;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		miicfg |= GSWIP_MII_CFG_MODE_RGMII;
		break;
	default:
		dev_err(ds->dev,
			"Unsupported interface: %d\n", state->interface);
		return;
	}
	gswip_mii_mask_cfg(priv, GSWIP_MII_CFG_MODE_MASK, miicfg, port);

	switch (state->interface) {
	case PHY_INTERFACE_MODE_RGMII_ID:
		gswip_mii_mask_pcdu(priv, GSWIP_MII_PCDU_TXDLY_MASK |
					  GSWIP_MII_PCDU_RXDLY_MASK, 0, port);
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		gswip_mii_mask_pcdu(priv, GSWIP_MII_PCDU_RXDLY_MASK, 0, port);
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		gswip_mii_mask_pcdu(priv, GSWIP_MII_PCDU_TXDLY_MASK, 0, port);
		break;
	default:
		break;
	}
}

static void gswip_phylink_mac_link_down(struct dsa_switch *ds, int port,
					unsigned int mode,
					phy_interface_t interface)
{
	struct gswip_priv *priv = ds->priv;

	gswip_mii_mask_cfg(priv, GSWIP_MII_CFG_EN, 0, port);
}

static void gswip_phylink_mac_link_up(struct dsa_switch *ds, int port,
				      unsigned int mode,
				      phy_interface_t interface,
				      struct phy_device *phydev,
				      int speed, int duplex,
				      bool tx_pause, bool rx_pause)
{
	struct gswip_priv *priv = ds->priv;

	/* Enable the xMII interface only for the external PHY */
	if (interface != PHY_INTERFACE_MODE_INTERNAL)
		gswip_mii_mask_cfg(priv, 0, GSWIP_MII_CFG_EN, port);
}

static void gswip_get_strings(struct dsa_switch *ds, int port, u32 stringset,
			      uint8_t *data)
{
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(gswip_rmon_cnt); i++)
		strncpy(data + i * ETH_GSTRING_LEN, gswip_rmon_cnt[i].name,
			ETH_GSTRING_LEN);
}

static u32 gswip_bcm_ram_entry_read(struct gswip_priv *priv, u32 table,
				    u32 index)
{
	u32 result;
	int err;

	gswip_switch_w(priv, index, GSWIP_BM_RAM_ADDR);
	gswip_switch_mask(priv, GSWIP_BM_RAM_CTRL_ADDR_MASK |
				GSWIP_BM_RAM_CTRL_OPMOD,
			      table | GSWIP_BM_RAM_CTRL_BAS,
			      GSWIP_BM_RAM_CTRL);

	err = gswip_switch_r_timeout(priv, GSWIP_BM_RAM_CTRL,
				     GSWIP_BM_RAM_CTRL_BAS);
	if (err) {
		dev_err(priv->dev, "timeout while reading table: %u, index: %u",
			table, index);
		return 0;
	}

	result = gswip_switch_r(priv, GSWIP_BM_RAM_VAL(0));
	result |= gswip_switch_r(priv, GSWIP_BM_RAM_VAL(1)) << 16;

	return result;
}

static void gswip_get_ethtool_stats(struct dsa_switch *ds, int port,
				    uint64_t *data)
{
	struct gswip_priv *priv = ds->priv;
	const struct gswip_rmon_cnt_desc *rmon_cnt;
	int i;
	u64 high;

	for (i = 0; i < ARRAY_SIZE(gswip_rmon_cnt); i++) {
		rmon_cnt = &gswip_rmon_cnt[i];

		data[i] = gswip_bcm_ram_entry_read(priv, port,
						   rmon_cnt->offset);
		if (rmon_cnt->size == 2) {
			high = gswip_bcm_ram_entry_read(priv, port,
							rmon_cnt->offset + 1);
			data[i] |= high << 32;
		}
	}
}

static int gswip_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return 0;

	return ARRAY_SIZE(gswip_rmon_cnt);
}

static const struct dsa_switch_ops gswip_switch_ops = {
	.get_tag_protocol	= gswip_get_tag_protocol,
	.setup			= gswip_setup,
	.port_enable		= gswip_port_enable,
	.port_disable		= gswip_port_disable,
	.port_bridge_join	= gswip_port_bridge_join,
	.port_bridge_leave	= gswip_port_bridge_leave,
	.port_fast_age		= gswip_port_fast_age,
	.port_vlan_filtering	= gswip_port_vlan_filtering,
	.port_vlan_prepare	= gswip_port_vlan_prepare,
	.port_vlan_add		= gswip_port_vlan_add,
	.port_vlan_del		= gswip_port_vlan_del,
	.port_stp_state_set	= gswip_port_stp_state_set,
	.port_fdb_add		= gswip_port_fdb_add,
	.port_fdb_del		= gswip_port_fdb_del,
	.port_fdb_dump		= gswip_port_fdb_dump,
	.phylink_validate	= gswip_phylink_validate,
	.phylink_mac_config	= gswip_phylink_mac_config,
	.phylink_mac_link_down	= gswip_phylink_mac_link_down,
	.phylink_mac_link_up	= gswip_phylink_mac_link_up,
	.get_strings		= gswip_get_strings,
	.get_ethtool_stats	= gswip_get_ethtool_stats,
	.get_sset_count		= gswip_get_sset_count,
};

static const struct xway_gphy_match_data xrx200a1x_gphy_data = {
	.fe_firmware_name = "lantiq/xrx200_phy22f_a14.bin",
	.ge_firmware_name = "lantiq/xrx200_phy11g_a14.bin",
};

static const struct xway_gphy_match_data xrx200a2x_gphy_data = {
	.fe_firmware_name = "lantiq/xrx200_phy22f_a22.bin",
	.ge_firmware_name = "lantiq/xrx200_phy11g_a22.bin",
};

static const struct xway_gphy_match_data xrx300_gphy_data = {
	.fe_firmware_name = "lantiq/xrx300_phy22f_a21.bin",
	.ge_firmware_name = "lantiq/xrx300_phy11g_a21.bin",
};

static const struct of_device_id xway_gphy_match[] = {
	{ .compatible = "lantiq,xrx200-gphy-fw", .data = NULL },
	{ .compatible = "lantiq,xrx200a1x-gphy-fw", .data = &xrx200a1x_gphy_data },
	{ .compatible = "lantiq,xrx200a2x-gphy-fw", .data = &xrx200a2x_gphy_data },
	{ .compatible = "lantiq,xrx300-gphy-fw", .data = &xrx300_gphy_data },
	{ .compatible = "lantiq,xrx330-gphy-fw", .data = &xrx300_gphy_data },
	{},
};

static int gswip_gphy_fw_load(struct gswip_priv *priv, struct gswip_gphy_fw *gphy_fw)
{
	struct device *dev = priv->dev;
	const struct firmware *fw;
	void *fw_addr;
	dma_addr_t dma_addr;
	dma_addr_t dev_addr;
	size_t size;
	int ret;

	ret = clk_prepare_enable(gphy_fw->clk_gate);
	if (ret)
		return ret;

	reset_control_assert(gphy_fw->reset);

	ret = request_firmware(&fw, gphy_fw->fw_name, dev);
	if (ret) {
		dev_err(dev, "failed to load firmware: %s, error: %i\n",
			gphy_fw->fw_name, ret);
		return ret;
	}

	/* GPHY cores need the firmware code in a persistent and contiguous
	 * memory area with a 16 kB boundary aligned start address.
	 */
	size = fw->size + XRX200_GPHY_FW_ALIGN;

	fw_addr = dmam_alloc_coherent(dev, size, &dma_addr, GFP_KERNEL);
	if (fw_addr) {
		fw_addr = PTR_ALIGN(fw_addr, XRX200_GPHY_FW_ALIGN);
		dev_addr = ALIGN(dma_addr, XRX200_GPHY_FW_ALIGN);
		memcpy(fw_addr, fw->data, fw->size);
	} else {
		dev_err(dev, "failed to alloc firmware memory\n");
		release_firmware(fw);
		return -ENOMEM;
	}

	release_firmware(fw);

	ret = regmap_write(priv->rcu_regmap, gphy_fw->fw_addr_offset, dev_addr);
	if (ret)
		return ret;

	reset_control_deassert(gphy_fw->reset);

	return ret;
}

static int gswip_gphy_fw_probe(struct gswip_priv *priv,
			       struct gswip_gphy_fw *gphy_fw,
			       struct device_node *gphy_fw_np, int i)
{
	struct device *dev = priv->dev;
	u32 gphy_mode;
	int ret;
	char gphyname[10];

	snprintf(gphyname, sizeof(gphyname), "gphy%d", i);

	gphy_fw->clk_gate = devm_clk_get(dev, gphyname);
	if (IS_ERR(gphy_fw->clk_gate)) {
		dev_err(dev, "Failed to lookup gate clock\n");
		return PTR_ERR(gphy_fw->clk_gate);
	}

	ret = of_property_read_u32(gphy_fw_np, "reg", &gphy_fw->fw_addr_offset);
	if (ret)
		return ret;

	ret = of_property_read_u32(gphy_fw_np, "lantiq,gphy-mode", &gphy_mode);
	/* Default to GE mode */
	if (ret)
		gphy_mode = GPHY_MODE_GE;

	switch (gphy_mode) {
	case GPHY_MODE_FE:
		gphy_fw->fw_name = priv->gphy_fw_name_cfg->fe_firmware_name;
		break;
	case GPHY_MODE_GE:
		gphy_fw->fw_name = priv->gphy_fw_name_cfg->ge_firmware_name;
		break;
	default:
		dev_err(dev, "Unknown GPHY mode %d\n", gphy_mode);
		return -EINVAL;
	}

	gphy_fw->reset = of_reset_control_array_get_exclusive(gphy_fw_np);
	if (IS_ERR(gphy_fw->reset)) {
		if (PTR_ERR(gphy_fw->reset) != -EPROBE_DEFER)
			dev_err(dev, "Failed to lookup gphy reset\n");
		return PTR_ERR(gphy_fw->reset);
	}

	return gswip_gphy_fw_load(priv, gphy_fw);
}

static void gswip_gphy_fw_remove(struct gswip_priv *priv,
				 struct gswip_gphy_fw *gphy_fw)
{
	int ret;

	/* check if the device was fully probed */
	if (!gphy_fw->fw_name)
		return;

	ret = regmap_write(priv->rcu_regmap, gphy_fw->fw_addr_offset, 0);
	if (ret)
		dev_err(priv->dev, "can not reset GPHY FW pointer");

	clk_disable_unprepare(gphy_fw->clk_gate);

	reset_control_put(gphy_fw->reset);
}

static int gswip_gphy_fw_list(struct gswip_priv *priv,
			      struct device_node *gphy_fw_list_np, u32 version)
{
	struct device *dev = priv->dev;
	struct device_node *gphy_fw_np;
	const struct of_device_id *match;
	int err;
	int i = 0;

	/* The VRX200 rev 1.1 uses the GSWIP 2.0 and needs the older
	 * GPHY firmware. The VRX200 rev 1.2 uses the GSWIP 2.1 and also
	 * needs a different GPHY firmware.
	 */
	if (of_device_is_compatible(gphy_fw_list_np, "lantiq,xrx200-gphy-fw")) {
		switch (version) {
		case GSWIP_VERSION_2_0:
			priv->gphy_fw_name_cfg = &xrx200a1x_gphy_data;
			break;
		case GSWIP_VERSION_2_1:
			priv->gphy_fw_name_cfg = &xrx200a2x_gphy_data;
			break;
		default:
			dev_err(dev, "unknown GSWIP version: 0x%x", version);
			return -ENOENT;
		}
	}

	match = of_match_node(xway_gphy_match, gphy_fw_list_np);
	if (match && match->data)
		priv->gphy_fw_name_cfg = match->data;

	if (!priv->gphy_fw_name_cfg) {
		dev_err(dev, "GPHY compatible type not supported");
		return -ENOENT;
	}

	priv->num_gphy_fw = of_get_available_child_count(gphy_fw_list_np);
	if (!priv->num_gphy_fw)
		return -ENOENT;

	priv->rcu_regmap = syscon_regmap_lookup_by_phandle(gphy_fw_list_np,
							   "lantiq,rcu");
	if (IS_ERR(priv->rcu_regmap))
		return PTR_ERR(priv->rcu_regmap);

	priv->gphy_fw = devm_kmalloc_array(dev, priv->num_gphy_fw,
					   sizeof(*priv->gphy_fw),
					   GFP_KERNEL | __GFP_ZERO);
	if (!priv->gphy_fw)
		return -ENOMEM;

	for_each_available_child_of_node(gphy_fw_list_np, gphy_fw_np) {
		err = gswip_gphy_fw_probe(priv, &priv->gphy_fw[i],
					  gphy_fw_np, i);
		if (err)
			goto remove_gphy;
		i++;
	}

	/* The standalone PHY11G requires 300ms to be fully
	 * initialized and ready for any MDIO communication after being
	 * taken out of reset. For the SoC-internal GPHY variant there
	 * is no (known) documentation for the minimum time after a
	 * reset. Use the same value as for the standalone variant as
	 * some users have reported internal PHYs not being detected
	 * without any delay.
	 */
	msleep(300);

	return 0;

remove_gphy:
	for (i = 0; i < priv->num_gphy_fw; i++)
		gswip_gphy_fw_remove(priv, &priv->gphy_fw[i]);
	return err;
}

static int gswip_probe(struct platform_device *pdev)
{
	struct gswip_priv *priv;
	struct device_node *mdio_np, *gphy_fw_np;
	struct device *dev = &pdev->dev;
	int err;
	int i;
	u32 version;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->gswip = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->gswip))
		return PTR_ERR(priv->gswip);

	priv->mdio = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(priv->mdio))
		return PTR_ERR(priv->mdio);

	priv->mii = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(priv->mii))
		return PTR_ERR(priv->mii);

	priv->hw_info = of_device_get_match_data(dev);
	if (!priv->hw_info)
		return -EINVAL;

	priv->ds = devm_kzalloc(dev, sizeof(*priv->ds), GFP_KERNEL);
	if (!priv->ds)
		return -ENOMEM;

	priv->ds->dev = dev;
	priv->ds->num_ports = priv->hw_info->max_ports;
	priv->ds->priv = priv;
	priv->ds->ops = &gswip_switch_ops;
	priv->dev = dev;
	version = gswip_switch_r(priv, GSWIP_VERSION);

	/* bring up the mdio bus */
	gphy_fw_np = of_get_compatible_child(dev->of_node, "lantiq,gphy-fw");
	if (gphy_fw_np) {
		err = gswip_gphy_fw_list(priv, gphy_fw_np, version);
		of_node_put(gphy_fw_np);
		if (err) {
			dev_err(dev, "gphy fw probe failed\n");
			return err;
		}
	}

	/* bring up the mdio bus */
	mdio_np = of_get_compatible_child(dev->of_node, "lantiq,xrx200-mdio");
	if (mdio_np) {
		err = gswip_mdio(priv, mdio_np);
		if (err) {
			dev_err(dev, "mdio probe failed\n");
			goto put_mdio_node;
		}
	}

	err = dsa_register_switch(priv->ds);
	if (err) {
		dev_err(dev, "dsa switch register failed: %i\n", err);
		goto mdio_bus;
	}
	if (!dsa_is_cpu_port(priv->ds, priv->hw_info->cpu_port)) {
		dev_err(dev, "wrong CPU port defined, HW only supports port: %i",
			priv->hw_info->cpu_port);
		err = -EINVAL;
		goto disable_switch;
	}

	platform_set_drvdata(pdev, priv);

	dev_info(dev, "probed GSWIP version %lx mod %lx\n",
		 (version & GSWIP_VERSION_REV_MASK) >> GSWIP_VERSION_REV_SHIFT,
		 (version & GSWIP_VERSION_MOD_MASK) >> GSWIP_VERSION_MOD_SHIFT);
	return 0;

disable_switch:
	gswip_mdio_mask(priv, GSWIP_MDIO_GLOB_ENABLE, 0, GSWIP_MDIO_GLOB);
	dsa_unregister_switch(priv->ds);
mdio_bus:
	if (mdio_np)
		mdiobus_unregister(priv->ds->slave_mii_bus);
put_mdio_node:
	of_node_put(mdio_np);
	for (i = 0; i < priv->num_gphy_fw; i++)
		gswip_gphy_fw_remove(priv, &priv->gphy_fw[i]);
	return err;
}

static int gswip_remove(struct platform_device *pdev)
{
	struct gswip_priv *priv = platform_get_drvdata(pdev);
	int i;

	/* disable the switch */
	gswip_mdio_mask(priv, GSWIP_MDIO_GLOB_ENABLE, 0, GSWIP_MDIO_GLOB);

	dsa_unregister_switch(priv->ds);

	if (priv->ds->slave_mii_bus) {
		mdiobus_unregister(priv->ds->slave_mii_bus);
		of_node_put(priv->ds->slave_mii_bus->dev.of_node);
	}

	for (i = 0; i < priv->num_gphy_fw; i++)
		gswip_gphy_fw_remove(priv, &priv->gphy_fw[i]);

	return 0;
}

static const struct gswip_hw_info gswip_xrx200 = {
	.max_ports = 7,
	.cpu_port = 6,
};

static const struct of_device_id gswip_of_match[] = {
	{ .compatible = "lantiq,xrx200-gswip", .data = &gswip_xrx200 },
	{},
};
MODULE_DEVICE_TABLE(of, gswip_of_match);

static struct platform_driver gswip_driver = {
	.probe = gswip_probe,
	.remove = gswip_remove,
	.driver = {
		.name = "gswip",
		.of_match_table = gswip_of_match,
	},
};

module_platform_driver(gswip_driver);

MODULE_FIRMWARE("lantiq/xrx300_phy11g_a21.bin");
MODULE_FIRMWARE("lantiq/xrx300_phy22f_a21.bin");
MODULE_FIRMWARE("lantiq/xrx200_phy11g_a14.bin");
MODULE_FIRMWARE("lantiq/xrx200_phy11g_a22.bin");
MODULE_FIRMWARE("lantiq/xrx200_phy22f_a14.bin");
MODULE_FIRMWARE("lantiq/xrx200_phy22f_a22.bin");
MODULE_AUTHOR("Hauke Mehrtens <hauke@hauke-m.de>");
MODULE_DESCRIPTION("Lantiq / Intel GSWIP driver");
MODULE_LICENSE("GPL v2");
