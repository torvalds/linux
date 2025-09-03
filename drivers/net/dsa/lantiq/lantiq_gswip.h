// SPDX-License-Identifier: GPL-2.0
#ifndef __LANTIQ_GSWIP_H
#define __LANTIQ_GSWIP_H

#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/phylink.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/swab.h>
#include <net/dsa.h>

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
#define GSWIP_MII_CFGp(p)		(0x2 * (p))
#define  GSWIP_MII_CFG_RESET		BIT(15)
#define  GSWIP_MII_CFG_EN		BIT(14)
#define  GSWIP_MII_CFG_ISOLATE		BIT(13)
#define  GSWIP_MII_CFG_LDCLKDIS		BIT(12)
#define  GSWIP_MII_CFG_RGMII_IBS	BIT(8)
#define  GSWIP_MII_CFG_RMII_CLK		BIT(7)
#define  GSWIP_MII_CFG_MODE_MIIP	0x0
#define  GSWIP_MII_CFG_MODE_MIIM	0x1
#define  GSWIP_MII_CFG_MODE_RMIIP	0x2
#define  GSWIP_MII_CFG_MODE_RMIIM	0x3
#define  GSWIP_MII_CFG_MODE_RGMII	0x4
#define  GSWIP_MII_CFG_MODE_GMII	0x9
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
#define  GSWIP_VERSION_REV_MASK		GENMASK(7, 0)
#define  GSWIP_VERSION_MOD_MASK		GENMASK(15, 8)
#define  GSWIP_VERSION_REV(v)		FIELD_GET(GSWIP_VERSION_REV_MASK, v)
#define  GSWIP_VERSION_MOD(v)		FIELD_GET(GSWIP_VERSION_MOD_MASK, v)
#define   GSWIP_VERSION_2_0		0x100
#define   GSWIP_VERSION_2_1		0x021
#define   GSWIP_VERSION_2_2		0x122
#define   GSWIP_VERSION_2_2_ETC		0x022
/* The hardware has the 'major/minor' version bytes in the wrong order
 * preventing numerical comparisons. Swap the bytes of the 16-bit value
 * to end up with REV being the most significant byte and MOD being the
 * least significant byte, which then allows comparing it with the
 * value stored in struct gswip_priv.
 */
#define GSWIP_VERSION_GE(priv, ver)	((priv)->version >= swab16(ver))

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
#define GSWIP_MAC_CTRL_0p(p)		(0x903 + ((p) * 0xC))
#define  GSWIP_MAC_CTRL_0_PADEN		BIT(8)
#define  GSWIP_MAC_CTRL_0_FCS_EN	BIT(7)
#define  GSWIP_MAC_CTRL_0_FCON_MASK	0x0070
#define  GSWIP_MAC_CTRL_0_FCON_AUTO	0x0000
#define  GSWIP_MAC_CTRL_0_FCON_RX	0x0010
#define  GSWIP_MAC_CTRL_0_FCON_TX	0x0020
#define  GSWIP_MAC_CTRL_0_FCON_RXTX	0x0030
#define  GSWIP_MAC_CTRL_0_FCON_NONE	0x0040
#define  GSWIP_MAC_CTRL_0_FDUP_MASK	0x000C
#define  GSWIP_MAC_CTRL_0_FDUP_AUTO	0x0000
#define  GSWIP_MAC_CTRL_0_FDUP_EN	0x0004
#define  GSWIP_MAC_CTRL_0_FDUP_DIS	0x000C
#define  GSWIP_MAC_CTRL_0_GMII_MASK	0x0003
#define  GSWIP_MAC_CTRL_0_GMII_AUTO	0x0000
#define  GSWIP_MAC_CTRL_0_GMII_MII	0x0001
#define  GSWIP_MAC_CTRL_0_GMII_RGMII	0x0002
#define GSWIP_MAC_CTRL_2p(p)		(0x905 + ((p) * 0xC))
#define GSWIP_MAC_CTRL_2_LCHKL		BIT(2) /* Frame Length Check Long Enable */
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
#define  GSWIP_SDMA_PCTRL_PAUFWD	BIT(3)	/* Pause Frame Forwarding */

#define GSWIP_TABLE_ACTIVE_VLAN		0x01
#define GSWIP_TABLE_VLAN_MAPPING	0x02
#define GSWIP_TABLE_MAC_BRIDGE		0x0b
#define  GSWIP_TABLE_MAC_BRIDGE_KEY3_FID	GENMASK(5, 0)	/* Filtering identifier */
#define  GSWIP_TABLE_MAC_BRIDGE_VAL0_PORT	GENMASK(7, 4)	/* Port on learned entries */
#define  GSWIP_TABLE_MAC_BRIDGE_VAL1_STATIC	BIT(0)		/* Static, non-aging entry */

#define XRX200_GPHY_FW_ALIGN	(16 * 1024)

/* Maximum packet size supported by the switch. In theory this should be 10240,
 * but long packets currently cause lock-ups with an MTU of over 2526. Medium
 * packets are sometimes dropped (e.g. TCP over 2477, UDP over 2516-2519, ICMP
 * over 2526), hence an MTU value of 2400 seems safe. This issue only affects
 * packet reception. This is probably caused by the PPA engine, which is on the
 * RX part of the device. Packet transmission works properly up to 10240.
 */
#define GSWIP_MAX_PACKET_LENGTH	2400

struct gswip_pce_microcode {
	u16 val_3;
	u16 val_2;
	u16 val_1;
	u16 val_0;
};

struct gswip_hw_info {
	int max_ports;
	unsigned int allowed_cpu_ports;
	unsigned int mii_ports;
	int mii_port_reg_offset;
	const struct gswip_pce_microcode (*pce_microcode)[];
	size_t pce_microcode_size;
	enum dsa_tag_protocol tag_protocol;
	void (*phylink_get_caps)(struct dsa_switch *ds, int port,
				 struct phylink_config *config);
	struct phylink_pcs *(*mac_select_pcs)(struct phylink_config *config,
					      phy_interface_t interface);
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
	struct mutex pce_table_lock;
	u16 version;
};

#endif /* __LANTIQ_GSWIP_H */
