/* Copyright 2008-2009 Broadcom Corporation
 *
 * Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available
 * at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a
 * license other than the GPL, without Broadcom's express prior written
 * consent.
 *
 * Written by Yaniv Rosner
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mutex.h>

#include "bnx2x_reg.h"
#include "bnx2x_fw_defs.h"
#include "bnx2x_hsi.h"
#include "bnx2x_link.h"
#include "bnx2x.h"

/********************************************************/
#define SUPPORT_CL73 0 /* Currently no */
#define ETH_HLEN			14
#define ETH_OVREHEAD		(ETH_HLEN + 8)/* 8 for CRC + VLAN*/
#define ETH_MIN_PACKET_SIZE		60
#define ETH_MAX_PACKET_SIZE		1500
#define ETH_MAX_JUMBO_PACKET_SIZE	9600
#define MDIO_ACCESS_TIMEOUT		1000
#define BMAC_CONTROL_RX_ENABLE	2

/***********************************************************/
/*			Shortcut definitions		   */
/***********************************************************/

#define NIG_STATUS_XGXS0_LINK10G \
		NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_LINK10G
#define NIG_STATUS_XGXS0_LINK_STATUS \
		NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_LINK_STATUS
#define NIG_STATUS_XGXS0_LINK_STATUS_SIZE \
		NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_LINK_STATUS_SIZE
#define NIG_STATUS_SERDES0_LINK_STATUS \
		NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_LINK_STATUS
#define NIG_MASK_MI_INT \
		NIG_MASK_INTERRUPT_PORT0_REG_MASK_EMAC0_MISC_MI_INT
#define NIG_MASK_XGXS0_LINK10G \
		NIG_MASK_INTERRUPT_PORT0_REG_MASK_XGXS0_LINK10G
#define NIG_MASK_XGXS0_LINK_STATUS \
		NIG_MASK_INTERRUPT_PORT0_REG_MASK_XGXS0_LINK_STATUS
#define NIG_MASK_SERDES0_LINK_STATUS \
		NIG_MASK_INTERRUPT_PORT0_REG_MASK_SERDES0_LINK_STATUS

#define MDIO_AN_CL73_OR_37_COMPLETE \
		(MDIO_GP_STATUS_TOP_AN_STATUS1_CL73_AUTONEG_COMPLETE | \
		 MDIO_GP_STATUS_TOP_AN_STATUS1_CL37_AUTONEG_COMPLETE)

#define XGXS_RESET_BITS \
	(MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_XGXS0_RSTB_HW |   \
	 MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_XGXS0_IDDQ |      \
	 MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_XGXS0_PWRDWN |    \
	 MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_XGXS0_PWRDWN_SD | \
	 MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_XGXS0_TXD_FIFO_RSTB)

#define SERDES_RESET_BITS \
	(MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_SERDES0_RSTB_HW | \
	 MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_SERDES0_IDDQ |    \
	 MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_SERDES0_PWRDWN |  \
	 MISC_REGISTERS_RESET_REG_3_MISC_NIG_MUX_SERDES0_PWRDWN_SD)

#define AUTONEG_CL37		SHARED_HW_CFG_AN_ENABLE_CL37
#define AUTONEG_CL73		SHARED_HW_CFG_AN_ENABLE_CL73
#define AUTONEG_BAM 		SHARED_HW_CFG_AN_ENABLE_BAM
#define AUTONEG_PARALLEL \
				SHARED_HW_CFG_AN_ENABLE_PARALLEL_DETECTION
#define AUTONEG_SGMII_FIBER_AUTODET \
				SHARED_HW_CFG_AN_EN_SGMII_FIBER_AUTO_DETECT
#define AUTONEG_REMOTE_PHY	SHARED_HW_CFG_AN_ENABLE_REMOTE_PHY

#define GP_STATUS_PAUSE_RSOLUTION_TXSIDE \
			MDIO_GP_STATUS_TOP_AN_STATUS1_PAUSE_RSOLUTION_TXSIDE
#define GP_STATUS_PAUSE_RSOLUTION_RXSIDE \
			MDIO_GP_STATUS_TOP_AN_STATUS1_PAUSE_RSOLUTION_RXSIDE
#define GP_STATUS_SPEED_MASK \
			MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_MASK
#define GP_STATUS_10M	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10M
#define GP_STATUS_100M	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_100M
#define GP_STATUS_1G	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_1G
#define GP_STATUS_2_5G	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_2_5G
#define GP_STATUS_5G	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_5G
#define GP_STATUS_6G	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_6G
#define GP_STATUS_10G_HIG \
			MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10G_HIG
#define GP_STATUS_10G_CX4 \
			MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10G_CX4
#define GP_STATUS_12G_HIG \
			MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_12G_HIG
#define GP_STATUS_12_5G MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_12_5G
#define GP_STATUS_13G	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_13G
#define GP_STATUS_15G	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_15G
#define GP_STATUS_16G	MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_16G
#define GP_STATUS_1G_KX MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_1G_KX
#define GP_STATUS_10G_KX4 \
			MDIO_GP_STATUS_TOP_AN_STATUS1_ACTUAL_SPEED_10G_KX4

#define LINK_10THD			LINK_STATUS_SPEED_AND_DUPLEX_10THD
#define LINK_10TFD			LINK_STATUS_SPEED_AND_DUPLEX_10TFD
#define LINK_100TXHD		LINK_STATUS_SPEED_AND_DUPLEX_100TXHD
#define LINK_100T4			LINK_STATUS_SPEED_AND_DUPLEX_100T4
#define LINK_100TXFD		LINK_STATUS_SPEED_AND_DUPLEX_100TXFD
#define LINK_1000THD		LINK_STATUS_SPEED_AND_DUPLEX_1000THD
#define LINK_1000TFD		LINK_STATUS_SPEED_AND_DUPLEX_1000TFD
#define LINK_1000XFD		LINK_STATUS_SPEED_AND_DUPLEX_1000XFD
#define LINK_2500THD		LINK_STATUS_SPEED_AND_DUPLEX_2500THD
#define LINK_2500TFD		LINK_STATUS_SPEED_AND_DUPLEX_2500TFD
#define LINK_2500XFD		LINK_STATUS_SPEED_AND_DUPLEX_2500XFD
#define LINK_10GTFD			LINK_STATUS_SPEED_AND_DUPLEX_10GTFD
#define LINK_10GXFD			LINK_STATUS_SPEED_AND_DUPLEX_10GXFD
#define LINK_12GTFD			LINK_STATUS_SPEED_AND_DUPLEX_12GTFD
#define LINK_12GXFD			LINK_STATUS_SPEED_AND_DUPLEX_12GXFD
#define LINK_12_5GTFD		LINK_STATUS_SPEED_AND_DUPLEX_12_5GTFD
#define LINK_12_5GXFD		LINK_STATUS_SPEED_AND_DUPLEX_12_5GXFD
#define LINK_13GTFD			LINK_STATUS_SPEED_AND_DUPLEX_13GTFD
#define LINK_13GXFD			LINK_STATUS_SPEED_AND_DUPLEX_13GXFD
#define LINK_15GTFD			LINK_STATUS_SPEED_AND_DUPLEX_15GTFD
#define LINK_15GXFD			LINK_STATUS_SPEED_AND_DUPLEX_15GXFD
#define LINK_16GTFD			LINK_STATUS_SPEED_AND_DUPLEX_16GTFD
#define LINK_16GXFD			LINK_STATUS_SPEED_AND_DUPLEX_16GXFD

#define PHY_XGXS_FLAG			0x1
#define PHY_SGMII_FLAG			0x2
#define PHY_SERDES_FLAG			0x4

/**********************************************************/
/*                     INTERFACE                          */
/**********************************************************/
#define CL45_WR_OVER_CL22(_bp, _port, _phy_addr, _bank, _addr, _val) \
	bnx2x_cl45_write(_bp, _port, 0, _phy_addr, \
		DEFAULT_PHY_DEV_ADDR, \
		(_bank + (_addr & 0xf)), \
		_val)

#define CL45_RD_OVER_CL22(_bp, _port, _phy_addr, _bank, _addr, _val) \
	bnx2x_cl45_read(_bp, _port, 0, _phy_addr, \
		DEFAULT_PHY_DEV_ADDR, \
		(_bank + (_addr & 0xf)), \
		_val)

static void bnx2x_set_phy_mdio(struct link_params *params)
{
	struct bnx2x *bp = params->bp;
	REG_WR(bp, NIG_REG_XGXS0_CTRL_MD_ST +
		   params->port*0x18, 0);
	REG_WR(bp, NIG_REG_XGXS0_CTRL_MD_DEVAD + params->port*0x18,
		   DEFAULT_PHY_DEV_ADDR);
}

static u32 bnx2x_bits_en(struct bnx2x *bp, u32 reg, u32 bits)
{
	u32 val = REG_RD(bp, reg);

	val |= bits;
	REG_WR(bp, reg, val);
	return val;
}

static u32 bnx2x_bits_dis(struct bnx2x *bp, u32 reg, u32 bits)
{
	u32 val = REG_RD(bp, reg);

	val &= ~bits;
	REG_WR(bp, reg, val);
	return val;
}

static void bnx2x_emac_init(struct link_params *params,
			   struct link_vars *vars)
{
	/* reset and unreset the emac core */
	struct bnx2x *bp = params->bp;
	u8 port = params->port;
	u32 emac_base = port ? GRCBASE_EMAC1 : GRCBASE_EMAC0;
	u32 val;
	u16 timeout;

	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR,
		   (MISC_REGISTERS_RESET_REG_2_RST_EMAC0_HARD_CORE << port));
	udelay(5);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET,
		   (MISC_REGISTERS_RESET_REG_2_RST_EMAC0_HARD_CORE << port));

	/* init emac - use read-modify-write */
	/* self clear reset */
	val = REG_RD(bp, emac_base + EMAC_REG_EMAC_MODE);
	EMAC_WR(bp, EMAC_REG_EMAC_MODE, (val | EMAC_MODE_RESET));

	timeout = 200;
	do {
		val = REG_RD(bp, emac_base + EMAC_REG_EMAC_MODE);
		DP(NETIF_MSG_LINK, "EMAC reset reg is %u\n", val);
		if (!timeout) {
			DP(NETIF_MSG_LINK, "EMAC timeout!\n");
			return;
		}
		timeout--;
	} while (val & EMAC_MODE_RESET);

	/* Set mac address */
	val = ((params->mac_addr[0] << 8) |
		params->mac_addr[1]);
	EMAC_WR(bp, EMAC_REG_EMAC_MAC_MATCH, val);

	val = ((params->mac_addr[2] << 24) |
	       (params->mac_addr[3] << 16) |
	       (params->mac_addr[4] << 8) |
		params->mac_addr[5]);
	EMAC_WR(bp, EMAC_REG_EMAC_MAC_MATCH + 4, val);
}

static u8 bnx2x_emac_enable(struct link_params *params,
			  struct link_vars *vars, u8 lb)
{
	struct bnx2x *bp = params->bp;
	u8 port = params->port;
	u32 emac_base = port ? GRCBASE_EMAC1 : GRCBASE_EMAC0;
	u32 val;

	DP(NETIF_MSG_LINK, "enabling EMAC\n");

	/* enable emac and not bmac */
	REG_WR(bp, NIG_REG_EGRESS_EMAC0_PORT + port*4, 1);

	/* for paladium */
	if (CHIP_REV_IS_EMUL(bp)) {
		/* Use lane 1 (of lanes 0-3) */
		REG_WR(bp, NIG_REG_XGXS_LANE_SEL_P0 + port*4, 1);
		REG_WR(bp, NIG_REG_XGXS_SERDES0_MODE_SEL +
			    port*4, 1);
	}
	/* for fpga */
	else

	if (CHIP_REV_IS_FPGA(bp)) {
		/* Use lane 1 (of lanes 0-3) */
		DP(NETIF_MSG_LINK, "bnx2x_emac_enable: Setting FPGA\n");

		REG_WR(bp, NIG_REG_XGXS_LANE_SEL_P0 + port*4, 1);
		REG_WR(bp, NIG_REG_XGXS_SERDES0_MODE_SEL + port*4,
			    0);
	} else
	/* ASIC */
	if (vars->phy_flags & PHY_XGXS_FLAG) {
		u32 ser_lane = ((params->lane_config &
			    PORT_HW_CFG_LANE_SWAP_CFG_MASTER_MASK) >>
			    PORT_HW_CFG_LANE_SWAP_CFG_MASTER_SHIFT);

		DP(NETIF_MSG_LINK, "XGXS\n");
		/* select the master lanes (out of 0-3) */
		REG_WR(bp, NIG_REG_XGXS_LANE_SEL_P0 +
			   port*4, ser_lane);
		/* select XGXS */
		REG_WR(bp, NIG_REG_XGXS_SERDES0_MODE_SEL +
			   port*4, 1);

	} else { /* SerDes */
		DP(NETIF_MSG_LINK, "SerDes\n");
		/* select SerDes */
		REG_WR(bp, NIG_REG_XGXS_SERDES0_MODE_SEL +
			   port*4, 0);
	}

	/* enable emac */
	REG_WR(bp, NIG_REG_NIG_EMAC0_EN + port*4, 1);

	if (CHIP_REV_IS_SLOW(bp)) {
		/* config GMII mode */
		val = REG_RD(bp, emac_base + EMAC_REG_EMAC_MODE);
		EMAC_WR(bp, EMAC_REG_EMAC_MODE,
			    (val | EMAC_MODE_PORT_GMII));
	} else { /* ASIC */
		/* pause enable/disable */
		bnx2x_bits_dis(bp, emac_base + EMAC_REG_EMAC_RX_MODE,
			       EMAC_RX_MODE_FLOW_EN);
		if (vars->flow_ctrl & BNX2X_FLOW_CTRL_RX)
			bnx2x_bits_en(bp, emac_base +
				    EMAC_REG_EMAC_RX_MODE,
				    EMAC_RX_MODE_FLOW_EN);

		bnx2x_bits_dis(bp,  emac_base + EMAC_REG_EMAC_TX_MODE,
			     (EMAC_TX_MODE_EXT_PAUSE_EN |
			      EMAC_TX_MODE_FLOW_EN));
		if (vars->flow_ctrl & BNX2X_FLOW_CTRL_TX)
			bnx2x_bits_en(bp, emac_base +
				    EMAC_REG_EMAC_TX_MODE,
				   (EMAC_TX_MODE_EXT_PAUSE_EN |
				    EMAC_TX_MODE_FLOW_EN));
	}

	/* KEEP_VLAN_TAG, promiscuous */
	val = REG_RD(bp, emac_base + EMAC_REG_EMAC_RX_MODE);
	val |= EMAC_RX_MODE_KEEP_VLAN_TAG | EMAC_RX_MODE_PROMISCUOUS;
	EMAC_WR(bp, EMAC_REG_EMAC_RX_MODE, val);

	/* Set Loopback */
	val = REG_RD(bp, emac_base + EMAC_REG_EMAC_MODE);
	if (lb)
		val |= 0x810;
	else
		val &= ~0x810;
	EMAC_WR(bp, EMAC_REG_EMAC_MODE, val);

	/* enable emac */
	REG_WR(bp, NIG_REG_NIG_EMAC0_EN + port*4, 1);

	/* enable emac for jumbo packets */
	EMAC_WR(bp, EMAC_REG_EMAC_RX_MTU_SIZE,
		(EMAC_RX_MTU_SIZE_JUMBO_ENA |
		 (ETH_MAX_JUMBO_PACKET_SIZE + ETH_OVREHEAD)));

	/* strip CRC */
	REG_WR(bp, NIG_REG_NIG_INGRESS_EMAC0_NO_CRC + port*4, 0x1);

	/* disable the NIG in/out to the bmac */
	REG_WR(bp, NIG_REG_BMAC0_IN_EN + port*4, 0x0);
	REG_WR(bp, NIG_REG_BMAC0_PAUSE_OUT_EN + port*4, 0x0);
	REG_WR(bp, NIG_REG_BMAC0_OUT_EN + port*4, 0x0);

	/* enable the NIG in/out to the emac */
	REG_WR(bp, NIG_REG_EMAC0_IN_EN + port*4, 0x1);
	val = 0;
	if (vars->flow_ctrl & BNX2X_FLOW_CTRL_TX)
		val = 1;

	REG_WR(bp, NIG_REG_EMAC0_PAUSE_OUT_EN + port*4, val);
	REG_WR(bp, NIG_REG_EGRESS_EMAC0_OUT_EN + port*4, 0x1);

	if (CHIP_REV_IS_EMUL(bp)) {
		/* take the BigMac out of reset */
		REG_WR(bp,
			   GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET,
			   (MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port));

		/* enable access for bmac registers */
		REG_WR(bp, NIG_REG_BMAC0_REGS_OUT_EN + port*4, 0x1);
	}

	vars->mac_type = MAC_TYPE_EMAC;
	return 0;
}



static u8 bnx2x_bmac_enable(struct link_params *params, struct link_vars *vars,
			  u8 is_lb)
{
	struct bnx2x *bp = params->bp;
	u8 port = params->port;
	u32 bmac_addr = port ? NIG_REG_INGRESS_BMAC1_MEM :
			       NIG_REG_INGRESS_BMAC0_MEM;
	u32 wb_data[2];
	u32 val;

	DP(NETIF_MSG_LINK, "Enabling BigMAC\n");
	/* reset and unreset the BigMac */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR,
	       (MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port));
	msleep(1);

	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET,
	       (MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port));

	/* enable access for bmac registers */
	REG_WR(bp, NIG_REG_BMAC0_REGS_OUT_EN + port*4, 0x1);

	/* XGXS control */
	wb_data[0] = 0x3c;
	wb_data[1] = 0;
	REG_WR_DMAE(bp, bmac_addr +
		      BIGMAC_REGISTER_BMAC_XGXS_CONTROL,
		      wb_data, 2);

	/* tx MAC SA */
	wb_data[0] = ((params->mac_addr[2] << 24) |
		       (params->mac_addr[3] << 16) |
		       (params->mac_addr[4] << 8) |
			params->mac_addr[5]);
	wb_data[1] = ((params->mac_addr[0] << 8) |
			params->mac_addr[1]);
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_TX_SOURCE_ADDR,
		    wb_data, 2);

	/* tx control */
	val = 0xc0;
	if (vars->flow_ctrl & BNX2X_FLOW_CTRL_TX)
		val |= 0x800000;
	wb_data[0] = val;
	wb_data[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_TX_CONTROL,
			wb_data, 2);

	/* mac control */
	val = 0x3;
	if (is_lb) {
		val |= 0x4;
		DP(NETIF_MSG_LINK, "enable bmac loopback\n");
	}
	wb_data[0] = val;
	wb_data[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_BMAC_CONTROL,
		    wb_data, 2);


	/* set rx mtu */
	wb_data[0] = ETH_MAX_JUMBO_PACKET_SIZE + ETH_OVREHEAD;
	wb_data[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_RX_MAX_SIZE,
			wb_data, 2);

	/* rx control set to don't strip crc */
	val = 0x14;
	if (vars->flow_ctrl & BNX2X_FLOW_CTRL_RX)
		val |= 0x20;
	wb_data[0] = val;
	wb_data[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_RX_CONTROL,
			wb_data, 2);

	/* set tx mtu */
	wb_data[0] = ETH_MAX_JUMBO_PACKET_SIZE + ETH_OVREHEAD;
	wb_data[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_TX_MAX_SIZE,
			wb_data, 2);

	/* set cnt max size */
	wb_data[0] = ETH_MAX_JUMBO_PACKET_SIZE + ETH_OVREHEAD;
	wb_data[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_CNT_MAX_SIZE,
		    wb_data, 2);

	/* configure safc */
	wb_data[0] = 0x1000200;
	wb_data[1] = 0;
	REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_RX_LLFC_MSG_FLDS,
		    wb_data, 2);
	/* fix for emulation */
	if (CHIP_REV_IS_EMUL(bp)) {
		wb_data[0] = 0xf000;
		wb_data[1] = 0;
		REG_WR_DMAE(bp,
			    bmac_addr + BIGMAC_REGISTER_TX_PAUSE_THRESHOLD,
			    wb_data, 2);
	}

	REG_WR(bp, NIG_REG_XGXS_SERDES0_MODE_SEL + port*4, 0x1);
	REG_WR(bp, NIG_REG_XGXS_LANE_SEL_P0 + port*4, 0x0);
	REG_WR(bp, NIG_REG_EGRESS_EMAC0_PORT + port*4, 0x0);
	val = 0;
	if (vars->flow_ctrl & BNX2X_FLOW_CTRL_TX)
		val = 1;
	REG_WR(bp, NIG_REG_BMAC0_PAUSE_OUT_EN + port*4, val);
	REG_WR(bp, NIG_REG_EGRESS_EMAC0_OUT_EN + port*4, 0x0);
	REG_WR(bp, NIG_REG_EMAC0_IN_EN + port*4, 0x0);
	REG_WR(bp, NIG_REG_EMAC0_PAUSE_OUT_EN + port*4, 0x0);
	REG_WR(bp, NIG_REG_BMAC0_IN_EN + port*4, 0x1);
	REG_WR(bp, NIG_REG_BMAC0_OUT_EN + port*4, 0x1);

	vars->mac_type = MAC_TYPE_BMAC;
	return 0;
}

static void bnx2x_phy_deassert(struct link_params *params, u8 phy_flags)
{
	struct bnx2x *bp = params->bp;
	u32 val;

	if (phy_flags & PHY_XGXS_FLAG) {
		DP(NETIF_MSG_LINK, "bnx2x_phy_deassert:XGXS\n");
		val = XGXS_RESET_BITS;

	} else { /* SerDes */
		DP(NETIF_MSG_LINK, "bnx2x_phy_deassert:SerDes\n");
		val = SERDES_RESET_BITS;
	}

	val = val << (params->port*16);

	/* reset and unreset the SerDes/XGXS */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_3_CLEAR,
		    val);
	udelay(500);
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_3_SET,
		    val);
	bnx2x_set_phy_mdio(params);
}

void bnx2x_link_status_update(struct link_params *params,
			    struct link_vars   *vars)
{
	struct bnx2x *bp = params->bp;
	u8 link_10g;
	u8 port = params->port;

	if (params->switch_cfg ==  SWITCH_CFG_1G)
		vars->phy_flags = PHY_SERDES_FLAG;
	else
		vars->phy_flags = PHY_XGXS_FLAG;
	vars->link_status = REG_RD(bp, params->shmem_base +
					  offsetof(struct shmem_region,
					   port_mb[port].link_status));

	vars->link_up = (vars->link_status & LINK_STATUS_LINK_UP);

	if (vars->link_up) {
		DP(NETIF_MSG_LINK, "phy link up\n");

		vars->phy_link_up = 1;
		vars->duplex = DUPLEX_FULL;
		switch (vars->link_status &
					LINK_STATUS_SPEED_AND_DUPLEX_MASK) {
			case LINK_10THD:
				vars->duplex = DUPLEX_HALF;
				/* fall thru */
			case LINK_10TFD:
				vars->line_speed = SPEED_10;
				break;

			case LINK_100TXHD:
				vars->duplex = DUPLEX_HALF;
				/* fall thru */
			case LINK_100T4:
			case LINK_100TXFD:
				vars->line_speed = SPEED_100;
				break;

			case LINK_1000THD:
				vars->duplex = DUPLEX_HALF;
				/* fall thru */
			case LINK_1000TFD:
				vars->line_speed = SPEED_1000;
				break;

			case LINK_2500THD:
				vars->duplex = DUPLEX_HALF;
				/* fall thru */
			case LINK_2500TFD:
				vars->line_speed = SPEED_2500;
				break;

			case LINK_10GTFD:
				vars->line_speed = SPEED_10000;
				break;

			case LINK_12GTFD:
				vars->line_speed = SPEED_12000;
				break;

			case LINK_12_5GTFD:
				vars->line_speed = SPEED_12500;
				break;

			case LINK_13GTFD:
				vars->line_speed = SPEED_13000;
				break;

			case LINK_15GTFD:
				vars->line_speed = SPEED_15000;
				break;

			case LINK_16GTFD:
				vars->line_speed = SPEED_16000;
				break;

			default:
				break;
		}

		if (vars->link_status & LINK_STATUS_TX_FLOW_CONTROL_ENABLED)
			vars->flow_ctrl |= BNX2X_FLOW_CTRL_TX;
		else
			vars->flow_ctrl &= ~BNX2X_FLOW_CTRL_TX;

		if (vars->link_status & LINK_STATUS_RX_FLOW_CONTROL_ENABLED)
			vars->flow_ctrl |= BNX2X_FLOW_CTRL_RX;
		else
			vars->flow_ctrl &= ~BNX2X_FLOW_CTRL_RX;

		if (vars->phy_flags & PHY_XGXS_FLAG) {
			if (vars->line_speed &&
			    ((vars->line_speed == SPEED_10) ||
			     (vars->line_speed == SPEED_100))) {
				vars->phy_flags |= PHY_SGMII_FLAG;
			} else {
				vars->phy_flags &= ~PHY_SGMII_FLAG;
			}
		}

		/* anything 10 and over uses the bmac */
		link_10g = ((vars->line_speed == SPEED_10000) ||
			    (vars->line_speed == SPEED_12000) ||
			    (vars->line_speed == SPEED_12500) ||
			    (vars->line_speed == SPEED_13000) ||
			    (vars->line_speed == SPEED_15000) ||
			    (vars->line_speed == SPEED_16000));
		if (link_10g)
			vars->mac_type = MAC_TYPE_BMAC;
		else
			vars->mac_type = MAC_TYPE_EMAC;

	} else { /* link down */
		DP(NETIF_MSG_LINK, "phy link down\n");

		vars->phy_link_up = 0;

		vars->line_speed = 0;
		vars->duplex = DUPLEX_FULL;
		vars->flow_ctrl = BNX2X_FLOW_CTRL_NONE;

		/* indicate no mac active */
		vars->mac_type = MAC_TYPE_NONE;
	}

	DP(NETIF_MSG_LINK, "link_status 0x%x  phy_link_up %x\n",
		 vars->link_status, vars->phy_link_up);
	DP(NETIF_MSG_LINK, "line_speed %x  duplex %x  flow_ctrl 0x%x\n",
		 vars->line_speed, vars->duplex, vars->flow_ctrl);
}

static void bnx2x_update_mng(struct link_params *params, u32 link_status)
{
	struct bnx2x *bp = params->bp;
	REG_WR(bp, params->shmem_base +
		   offsetof(struct shmem_region,
			    port_mb[params->port].link_status),
			link_status);
}

static void bnx2x_bmac_rx_disable(struct bnx2x *bp, u8 port)
{
	u32 bmac_addr = port ? NIG_REG_INGRESS_BMAC1_MEM :
		NIG_REG_INGRESS_BMAC0_MEM;
	u32 wb_data[2];
	u32 nig_bmac_enable = REG_RD(bp, NIG_REG_BMAC0_REGS_OUT_EN + port*4);

	/* Only if the bmac is out of reset */
	if (REG_RD(bp, MISC_REG_RESET_REG_2) &
			(MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port) &&
	    nig_bmac_enable) {

		/* Clear Rx Enable bit in BMAC_CONTROL register */
		REG_RD_DMAE(bp, bmac_addr + BIGMAC_REGISTER_BMAC_CONTROL,
			    wb_data, 2);
		wb_data[0] &= ~BMAC_CONTROL_RX_ENABLE;
		REG_WR_DMAE(bp, bmac_addr + BIGMAC_REGISTER_BMAC_CONTROL,
			    wb_data, 2);

		msleep(1);
	}
}

static u8 bnx2x_pbf_update(struct link_params *params, u32 flow_ctrl,
			 u32 line_speed)
{
	struct bnx2x *bp = params->bp;
	u8 port = params->port;
	u32 init_crd, crd;
	u32 count = 1000;

	/* disable port */
	REG_WR(bp, PBF_REG_DISABLE_NEW_TASK_PROC_P0 + port*4, 0x1);

	/* wait for init credit */
	init_crd = REG_RD(bp, PBF_REG_P0_INIT_CRD + port*4);
	crd = REG_RD(bp, PBF_REG_P0_CREDIT + port*8);
	DP(NETIF_MSG_LINK, "init_crd 0x%x  crd 0x%x\n", init_crd, crd);

	while ((init_crd != crd) && count) {
		msleep(5);

		crd = REG_RD(bp, PBF_REG_P0_CREDIT + port*8);
		count--;
	}
	crd = REG_RD(bp, PBF_REG_P0_CREDIT + port*8);
	if (init_crd != crd) {
		DP(NETIF_MSG_LINK, "BUG! init_crd 0x%x != crd 0x%x\n",
			  init_crd, crd);
		return -EINVAL;
	}

	if (flow_ctrl & BNX2X_FLOW_CTRL_RX ||
	    line_speed == SPEED_10 ||
	    line_speed == SPEED_100 ||
	    line_speed == SPEED_1000 ||
	    line_speed == SPEED_2500) {
		REG_WR(bp, PBF_REG_P0_PAUSE_ENABLE + port*4, 1);
		/* update threshold */
		REG_WR(bp, PBF_REG_P0_ARB_THRSH + port*4, 0);
		/* update init credit */
		init_crd = 778; 	/* (800-18-4) */

	} else {
		u32 thresh = (ETH_MAX_JUMBO_PACKET_SIZE +
			      ETH_OVREHEAD)/16;
		REG_WR(bp, PBF_REG_P0_PAUSE_ENABLE + port*4, 0);
		/* update threshold */
		REG_WR(bp, PBF_REG_P0_ARB_THRSH + port*4, thresh);
		/* update init credit */
		switch (line_speed) {
		case SPEED_10000:
			init_crd = thresh + 553 - 22;
			break;

		case SPEED_12000:
			init_crd = thresh + 664 - 22;
			break;

		case SPEED_13000:
			init_crd = thresh + 742 - 22;
			break;

		case SPEED_16000:
			init_crd = thresh + 778 - 22;
			break;
		default:
			DP(NETIF_MSG_LINK, "Invalid line_speed 0x%x\n",
				  line_speed);
			return -EINVAL;
			break;
		}
	}
	REG_WR(bp, PBF_REG_P0_INIT_CRD + port*4, init_crd);
	DP(NETIF_MSG_LINK, "PBF updated to speed %d credit %d\n",
		 line_speed, init_crd);

	/* probe the credit changes */
	REG_WR(bp, PBF_REG_INIT_P0 + port*4, 0x1);
	msleep(5);
	REG_WR(bp, PBF_REG_INIT_P0 + port*4, 0x0);

	/* enable port */
	REG_WR(bp, PBF_REG_DISABLE_NEW_TASK_PROC_P0 + port*4, 0x0);
	return 0;
}

static u32 bnx2x_get_emac_base(u32 ext_phy_type, u8 port)
{
	u32 emac_base;
	switch (ext_phy_type) {
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
		emac_base = GRCBASE_EMAC0;
		break;
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073:
		emac_base = (port) ? GRCBASE_EMAC0 : GRCBASE_EMAC1;
		break;
	default:
		emac_base = (port) ? GRCBASE_EMAC1 : GRCBASE_EMAC0;
		break;
	}
	return emac_base;

}

u8 bnx2x_cl45_write(struct bnx2x *bp, u8 port, u32 ext_phy_type,
		  u8 phy_addr, u8 devad, u16 reg, u16 val)
{
	u32 tmp, saved_mode;
	u8 i, rc = 0;
	u32 mdio_ctrl = bnx2x_get_emac_base(ext_phy_type, port);

	/* set clause 45 mode, slow down the MDIO clock to 2.5MHz
	 * (a value of 49==0x31) and make sure that the AUTO poll is off
	 */
	saved_mode = REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE);
	tmp = saved_mode & ~(EMAC_MDIO_MODE_AUTO_POLL |
			     EMAC_MDIO_MODE_CLOCK_CNT);
	tmp |= (EMAC_MDIO_MODE_CLAUSE_45 |
		(49 << EMAC_MDIO_MODE_CLOCK_CNT_BITSHIFT));
	REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE, tmp);
	REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE);
	udelay(40);

	/* address */

	tmp = ((phy_addr << 21) | (devad << 16) | reg |
	       EMAC_MDIO_COMM_COMMAND_ADDRESS |
	       EMAC_MDIO_COMM_START_BUSY);
	REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM, tmp);

	for (i = 0; i < 50; i++) {
		udelay(10);

		tmp = REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM);
		if (!(tmp & EMAC_MDIO_COMM_START_BUSY)) {
			udelay(5);
			break;
		}
	}
	if (tmp & EMAC_MDIO_COMM_START_BUSY) {
		DP(NETIF_MSG_LINK, "write phy register failed\n");
		rc = -EFAULT;
	} else {
		/* data */
		tmp = ((phy_addr << 21) | (devad << 16) | val |
		       EMAC_MDIO_COMM_COMMAND_WRITE_45 |
		       EMAC_MDIO_COMM_START_BUSY);
		REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM, tmp);

		for (i = 0; i < 50; i++) {
			udelay(10);

			tmp = REG_RD(bp, mdio_ctrl +
					 EMAC_REG_EMAC_MDIO_COMM);
			if (!(tmp & EMAC_MDIO_COMM_START_BUSY)) {
				udelay(5);
				break;
			}
		}
		if (tmp & EMAC_MDIO_COMM_START_BUSY) {
			DP(NETIF_MSG_LINK, "write phy register failed\n");
			rc = -EFAULT;
		}
	}

	/* Restore the saved mode */
	REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE, saved_mode);

	return rc;
}

u8 bnx2x_cl45_read(struct bnx2x *bp, u8 port, u32 ext_phy_type,
		 u8 phy_addr, u8 devad, u16 reg, u16 *ret_val)
{
	u32 val, saved_mode;
	u16 i;
	u8 rc = 0;

	u32 mdio_ctrl = bnx2x_get_emac_base(ext_phy_type, port);
	/* set clause 45 mode, slow down the MDIO clock to 2.5MHz
	 * (a value of 49==0x31) and make sure that the AUTO poll is off
	 */
	saved_mode = REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE);
	val = saved_mode & ((EMAC_MDIO_MODE_AUTO_POLL |
			     EMAC_MDIO_MODE_CLOCK_CNT));
	val |= (EMAC_MDIO_MODE_CLAUSE_45 |
		(49 << EMAC_MDIO_MODE_CLOCK_CNT_BITSHIFT));
	REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE, val);
	REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE);
	udelay(40);

	/* address */
	val = ((phy_addr << 21) | (devad << 16) | reg |
	       EMAC_MDIO_COMM_COMMAND_ADDRESS |
	       EMAC_MDIO_COMM_START_BUSY);
	REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM, val);

	for (i = 0; i < 50; i++) {
		udelay(10);

		val = REG_RD(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM);
		if (!(val & EMAC_MDIO_COMM_START_BUSY)) {
			udelay(5);
			break;
		}
	}
	if (val & EMAC_MDIO_COMM_START_BUSY) {
		DP(NETIF_MSG_LINK, "read phy register failed\n");

		*ret_val = 0;
		rc = -EFAULT;

	} else {
		/* data */
		val = ((phy_addr << 21) | (devad << 16) |
		       EMAC_MDIO_COMM_COMMAND_READ_45 |
		       EMAC_MDIO_COMM_START_BUSY);
		REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_COMM, val);

		for (i = 0; i < 50; i++) {
			udelay(10);

			val = REG_RD(bp, mdio_ctrl +
					  EMAC_REG_EMAC_MDIO_COMM);
			if (!(val & EMAC_MDIO_COMM_START_BUSY)) {
				*ret_val = (u16)(val & EMAC_MDIO_COMM_DATA);
				break;
			}
		}
		if (val & EMAC_MDIO_COMM_START_BUSY) {
			DP(NETIF_MSG_LINK, "read phy register failed\n");

			*ret_val = 0;
			rc = -EFAULT;
		}
	}

	/* Restore the saved mode */
	REG_WR(bp, mdio_ctrl + EMAC_REG_EMAC_MDIO_MODE, saved_mode);

	return rc;
}

static void bnx2x_set_aer_mmd(struct link_params *params,
			    struct link_vars   *vars)
{
	struct bnx2x *bp = params->bp;
	u32 ser_lane;
	u16 offset;

	ser_lane = ((params->lane_config &
		     PORT_HW_CFG_LANE_SWAP_CFG_MASTER_MASK) >>
		     PORT_HW_CFG_LANE_SWAP_CFG_MASTER_SHIFT);

	offset = (vars->phy_flags & PHY_XGXS_FLAG) ?
		(params->phy_addr + ser_lane) : 0;

	CL45_WR_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_AER_BLOCK,
			      MDIO_AER_BLOCK_AER_REG, 0x3800 + offset);
}

static void bnx2x_set_master_ln(struct link_params *params)
{
	struct bnx2x *bp = params->bp;
	u16 new_master_ln, ser_lane;
	ser_lane =  ((params->lane_config &
		     PORT_HW_CFG_LANE_SWAP_CFG_MASTER_MASK) >>
		     PORT_HW_CFG_LANE_SWAP_CFG_MASTER_SHIFT);

	/* set the master_ln for AN */
	CL45_RD_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_XGXS_BLOCK2,
			      MDIO_XGXS_BLOCK2_TEST_MODE_LANE,
			      &new_master_ln);

	CL45_WR_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_XGXS_BLOCK2 ,
			      MDIO_XGXS_BLOCK2_TEST_MODE_LANE,
			      (new_master_ln | ser_lane));
}

static u8 bnx2x_reset_unicore(struct link_params *params)
{
	struct bnx2x *bp = params->bp;
	u16 mii_control;
	u16 i;

	CL45_RD_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_COMBO_IEEE0,
			      MDIO_COMBO_IEEE0_MII_CONTROL, &mii_control);

	/* reset the unicore */
	CL45_WR_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_COMBO_IEEE0,
			      MDIO_COMBO_IEEE0_MII_CONTROL,
			      (mii_control |
			       MDIO_COMBO_IEEO_MII_CONTROL_RESET));

	/* wait for the reset to self clear */
	for (i = 0; i < MDIO_ACCESS_TIMEOUT; i++) {
		udelay(5);

		/* the reset erased the previous bank value */
		CL45_RD_OVER_CL22(bp, params->port,
				      params->phy_addr,
			      MDIO_REG_BANK_COMBO_IEEE0,
			      MDIO_COMBO_IEEE0_MII_CONTROL,
			      &mii_control);

		if (!(mii_control & MDIO_COMBO_IEEO_MII_CONTROL_RESET)) {
			udelay(5);
			return 0;
		}
	}

	DP(NETIF_MSG_LINK, "BUG! XGXS is still in reset!\n");
	return -EINVAL;

}

static void bnx2x_set_swap_lanes(struct link_params *params)
{
	struct bnx2x *bp = params->bp;
	/* Each two bits represents a lane number:
	   No swap is 0123 => 0x1b no need to enable the swap */
	u16 ser_lane, rx_lane_swap, tx_lane_swap;

	ser_lane = ((params->lane_config &
			 PORT_HW_CFG_LANE_SWAP_CFG_MASTER_MASK) >>
			PORT_HW_CFG_LANE_SWAP_CFG_MASTER_SHIFT);
	rx_lane_swap = ((params->lane_config &
			     PORT_HW_CFG_LANE_SWAP_CFG_RX_MASK) >>
			    PORT_HW_CFG_LANE_SWAP_CFG_RX_SHIFT);
	tx_lane_swap = ((params->lane_config &
			     PORT_HW_CFG_LANE_SWAP_CFG_TX_MASK) >>
			    PORT_HW_CFG_LANE_SWAP_CFG_TX_SHIFT);

	if (rx_lane_swap != 0x1b) {
		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				    MDIO_REG_BANK_XGXS_BLOCK2,
				    MDIO_XGXS_BLOCK2_RX_LN_SWAP,
				    (rx_lane_swap |
				    MDIO_XGXS_BLOCK2_RX_LN_SWAP_ENABLE |
				    MDIO_XGXS_BLOCK2_RX_LN_SWAP_FORCE_ENABLE));
	} else {
		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_XGXS_BLOCK2,
				      MDIO_XGXS_BLOCK2_RX_LN_SWAP, 0);
	}

	if (tx_lane_swap != 0x1b) {
		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_XGXS_BLOCK2,
				      MDIO_XGXS_BLOCK2_TX_LN_SWAP,
				      (tx_lane_swap |
				       MDIO_XGXS_BLOCK2_TX_LN_SWAP_ENABLE));
	} else {
		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_XGXS_BLOCK2,
				      MDIO_XGXS_BLOCK2_TX_LN_SWAP, 0);
	}
}

static void bnx2x_set_parallel_detection(struct link_params *params,
				       u8       	 phy_flags)
{
	struct bnx2x *bp = params->bp;
	u16 control2;

	CL45_RD_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_SERDES_DIGITAL,
			      MDIO_SERDES_DIGITAL_A_1000X_CONTROL2,
			      &control2);


	control2 |= MDIO_SERDES_DIGITAL_A_1000X_CONTROL2_PRL_DT_EN;


	CL45_WR_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_SERDES_DIGITAL,
			      MDIO_SERDES_DIGITAL_A_1000X_CONTROL2,
			      control2);

	if (phy_flags & PHY_XGXS_FLAG) {
		DP(NETIF_MSG_LINK, "XGXS\n");

		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				MDIO_REG_BANK_10G_PARALLEL_DETECT,
				MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_LINK,
				MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_LINK_CNT);

		CL45_RD_OVER_CL22(bp, params->port,
				      params->phy_addr,
				MDIO_REG_BANK_10G_PARALLEL_DETECT,
				MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_CONTROL,
				&control2);


		control2 |=
		    MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_CONTROL_PARDET10G_EN;

		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				MDIO_REG_BANK_10G_PARALLEL_DETECT,
				MDIO_10G_PARALLEL_DETECT_PAR_DET_10G_CONTROL,
				control2);

		/* Disable parallel detection of HiG */
		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				MDIO_REG_BANK_XGXS_BLOCK2,
				MDIO_XGXS_BLOCK2_UNICORE_MODE_10G,
				MDIO_XGXS_BLOCK2_UNICORE_MODE_10G_CX4_XGXS |
				MDIO_XGXS_BLOCK2_UNICORE_MODE_10G_HIGIG_XGXS);
	}
}

static void bnx2x_set_autoneg(struct link_params *params,
			    struct link_vars   *vars)
{
	struct bnx2x *bp = params->bp;
	u16 reg_val;

	/* CL37 Autoneg */

	CL45_RD_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_COMBO_IEEE0,
			      MDIO_COMBO_IEEE0_MII_CONTROL, &reg_val);

	/* CL37 Autoneg Enabled */
	if (vars->line_speed == SPEED_AUTO_NEG)
		reg_val |= MDIO_COMBO_IEEO_MII_CONTROL_AN_EN;
	else /* CL37 Autoneg Disabled */
		reg_val &= ~(MDIO_COMBO_IEEO_MII_CONTROL_AN_EN |
			     MDIO_COMBO_IEEO_MII_CONTROL_RESTART_AN);

	CL45_WR_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_COMBO_IEEE0,
			      MDIO_COMBO_IEEE0_MII_CONTROL, reg_val);

	/* Enable/Disable Autodetection */

	CL45_RD_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_SERDES_DIGITAL,
			      MDIO_SERDES_DIGITAL_A_1000X_CONTROL1, &reg_val);
	reg_val &= ~MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_SIGNAL_DETECT_EN;
	if (vars->line_speed == SPEED_AUTO_NEG)
		reg_val |= MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_AUTODET;
	else
		reg_val &= ~MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_AUTODET;

	CL45_WR_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_SERDES_DIGITAL,
			      MDIO_SERDES_DIGITAL_A_1000X_CONTROL1, reg_val);

	/* Enable TetonII and BAM autoneg */
	CL45_RD_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_BAM_NEXT_PAGE,
			      MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL,
			  &reg_val);
	if (vars->line_speed == SPEED_AUTO_NEG) {
		/* Enable BAM aneg Mode and TetonII aneg Mode */
		reg_val |= (MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL_BAM_MODE |
			    MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL_TETON_AN);
	} else {
		/* TetonII and BAM Autoneg Disabled */
		reg_val &= ~(MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL_BAM_MODE |
			     MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL_TETON_AN);
	}
	CL45_WR_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_BAM_NEXT_PAGE,
			      MDIO_BAM_NEXT_PAGE_MP5_NEXT_PAGE_CTRL,
			      reg_val);

	/* Enable Clause 73 Aneg */
	if ((vars->line_speed == SPEED_AUTO_NEG) &&
	    (SUPPORT_CL73)) {
		/* Enable BAM Station Manager */

		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_CL73_USERB0,
				      MDIO_CL73_USERB0_CL73_BAM_CTRL1,
				   (MDIO_CL73_USERB0_CL73_BAM_CTRL1_BAM_EN |
			MDIO_CL73_USERB0_CL73_BAM_CTRL1_BAM_STATION_MNGR_EN |
			MDIO_CL73_USERB0_CL73_BAM_CTRL1_BAM_NP_AFTER_BP_EN));

		/* Merge CL73 and CL37 aneg resolution */
		CL45_RD_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_CL73_USERB0,
				      MDIO_CL73_USERB0_CL73_BAM_CTRL3,
				      &reg_val);

		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
			MDIO_REG_BANK_CL73_USERB0,
			MDIO_CL73_USERB0_CL73_BAM_CTRL3,
			(reg_val |
			MDIO_CL73_USERB0_CL73_BAM_CTRL3_USE_CL73_HCD_MR));

		/* Set the CL73 AN speed */

		CL45_RD_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_CL73_IEEEB1,
				      MDIO_CL73_IEEEB1_AN_ADV2, &reg_val);
		/* In the SerDes we support only the 1G.
		   In the XGXS we support the 10G KX4
		   but we currently do not support the KR */
		if (vars->phy_flags & PHY_XGXS_FLAG) {
			DP(NETIF_MSG_LINK, "XGXS\n");
			/* 10G KX4 */
			reg_val |= MDIO_CL73_IEEEB1_AN_ADV2_ADVR_10G_KX4;
		} else {
			DP(NETIF_MSG_LINK, "SerDes\n");
			/* 1000M KX */
			reg_val |= MDIO_CL73_IEEEB1_AN_ADV2_ADVR_1000M_KX;
		}
		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_CL73_IEEEB1,
				      MDIO_CL73_IEEEB1_AN_ADV2, reg_val);

		/* CL73 Autoneg Enabled */
		reg_val = MDIO_CL73_IEEEB0_CL73_AN_CONTROL_AN_EN;
	} else {
		/* CL73 Autoneg Disabled */
		reg_val = 0;
	}
	CL45_WR_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_CL73_IEEEB0,
			      MDIO_CL73_IEEEB0_CL73_AN_CONTROL, reg_val);
}

/* program SerDes, forced speed */
static void bnx2x_program_serdes(struct link_params *params,
			       struct link_vars *vars)
{
	struct bnx2x *bp = params->bp;
	u16 reg_val;

	/* program duplex, disable autoneg */

	CL45_RD_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_COMBO_IEEE0,
			      MDIO_COMBO_IEEE0_MII_CONTROL, &reg_val);
	reg_val &= ~(MDIO_COMBO_IEEO_MII_CONTROL_FULL_DUPLEX |
		     MDIO_COMBO_IEEO_MII_CONTROL_AN_EN);
	if (params->req_duplex == DUPLEX_FULL)
		reg_val |= MDIO_COMBO_IEEO_MII_CONTROL_FULL_DUPLEX;
	CL45_WR_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_COMBO_IEEE0,
			      MDIO_COMBO_IEEE0_MII_CONTROL, reg_val);

	/* program speed
	   - needed only if the speed is greater than 1G (2.5G or 10G) */
	CL45_RD_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_SERDES_DIGITAL,
				      MDIO_SERDES_DIGITAL_MISC1, &reg_val);
	/* clearing the speed value before setting the right speed */
	DP(NETIF_MSG_LINK, "MDIO_REG_BANK_SERDES_DIGITAL = 0x%x\n", reg_val);

	reg_val &= ~(MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_MASK |
		     MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_SEL);

	if (!((vars->line_speed == SPEED_1000) ||
	      (vars->line_speed == SPEED_100) ||
	      (vars->line_speed == SPEED_10))) {

		reg_val |= (MDIO_SERDES_DIGITAL_MISC1_REFCLK_SEL_156_25M |
			    MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_SEL);
		if (vars->line_speed == SPEED_10000)
			reg_val |=
				MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_10G_CX4;
		if (vars->line_speed == SPEED_13000)
			reg_val |=
				MDIO_SERDES_DIGITAL_MISC1_FORCE_SPEED_13G;
	}

	CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_SERDES_DIGITAL,
				      MDIO_SERDES_DIGITAL_MISC1, reg_val);

}

static void bnx2x_set_brcm_cl37_advertisment(struct link_params *params)
{
	struct bnx2x *bp = params->bp;
	u16 val = 0;

	/* configure the 48 bits for BAM AN */

	/* set extended capabilities */
	if (params->speed_cap_mask & PORT_HW_CFG_SPEED_CAPABILITY_D0_2_5G)
		val |= MDIO_OVER_1G_UP1_2_5G;
	if (params->speed_cap_mask & PORT_HW_CFG_SPEED_CAPABILITY_D0_10G)
		val |= MDIO_OVER_1G_UP1_10G;
	CL45_WR_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_OVER_1G,
			      MDIO_OVER_1G_UP1, val);

	CL45_WR_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_OVER_1G,
			      MDIO_OVER_1G_UP3, 0);
}

static void bnx2x_calc_ieee_aneg_adv(struct link_params *params, u32 *ieee_fc)
{
	*ieee_fc = MDIO_COMBO_IEEE0_AUTO_NEG_ADV_FULL_DUPLEX;
	/* resolve pause mode and advertisement
	 * Please refer to Table 28B-3 of the 802.3ab-1999 spec */

	switch (params->req_flow_ctrl) {
	case BNX2X_FLOW_CTRL_AUTO:
		if (params->req_fc_auto_adv == BNX2X_FLOW_CTRL_BOTH) {
			*ieee_fc |=
			     MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH;
		} else {
			*ieee_fc |=
		       MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC;
		}
		break;
	case BNX2X_FLOW_CTRL_TX:
		*ieee_fc |=
		       MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC;
		break;

	case BNX2X_FLOW_CTRL_RX:
	case BNX2X_FLOW_CTRL_BOTH:
		*ieee_fc |= MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH;
		break;

	case BNX2X_FLOW_CTRL_NONE:
	default:
		*ieee_fc |= MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_NONE;
		break;
	}
}

static void bnx2x_set_ieee_aneg_advertisment(struct link_params *params,
					   u32 ieee_fc)
{
	struct bnx2x *bp = params->bp;
	/* for AN, we are always publishing full duplex */

	CL45_WR_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_COMBO_IEEE0,
			      MDIO_COMBO_IEEE0_AUTO_NEG_ADV, (u16)ieee_fc);
}

static void bnx2x_restart_autoneg(struct link_params *params)
{
	struct bnx2x *bp = params->bp;
	DP(NETIF_MSG_LINK, "bnx2x_restart_autoneg\n");
	if (SUPPORT_CL73) {
		/* enable and restart clause 73 aneg */
		u16 an_ctrl;

		CL45_RD_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_CL73_IEEEB0,
				      MDIO_CL73_IEEEB0_CL73_AN_CONTROL,
				  &an_ctrl);
		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				MDIO_REG_BANK_CL73_IEEEB0,
				MDIO_CL73_IEEEB0_CL73_AN_CONTROL,
				(an_ctrl |
				MDIO_CL73_IEEEB0_CL73_AN_CONTROL_AN_EN |
				MDIO_CL73_IEEEB0_CL73_AN_CONTROL_RESTART_AN));

	} else {
		/* Enable and restart BAM/CL37 aneg */
		u16 mii_control;

		CL45_RD_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_COMBO_IEEE0,
				      MDIO_COMBO_IEEE0_MII_CONTROL,
				      &mii_control);
		DP(NETIF_MSG_LINK,
			 "bnx2x_restart_autoneg mii_control before = 0x%x\n",
			 mii_control);
		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_COMBO_IEEE0,
				      MDIO_COMBO_IEEE0_MII_CONTROL,
				      (mii_control |
				MDIO_COMBO_IEEO_MII_CONTROL_AN_EN |
				MDIO_COMBO_IEEO_MII_CONTROL_RESTART_AN));
	}
}

static void bnx2x_initialize_sgmii_process(struct link_params *params,
					 struct link_vars *vars)
{
	struct bnx2x *bp = params->bp;
	u16 control1;

	/* in SGMII mode, the unicore is always slave */

	CL45_RD_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_SERDES_DIGITAL,
			      MDIO_SERDES_DIGITAL_A_1000X_CONTROL1,
		      &control1);
	control1 |= MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_INVERT_SIGNAL_DETECT;
	/* set sgmii mode (and not fiber) */
	control1 &= ~(MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_FIBER_MODE |
		      MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_AUTODET |
		      MDIO_SERDES_DIGITAL_A_1000X_CONTROL1_MSTR_MODE);
	CL45_WR_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_SERDES_DIGITAL,
			      MDIO_SERDES_DIGITAL_A_1000X_CONTROL1,
			      control1);

	/* if forced speed */
	if (!(vars->line_speed == SPEED_AUTO_NEG)) {
		/* set speed, disable autoneg */
		u16 mii_control;

		CL45_RD_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_COMBO_IEEE0,
				      MDIO_COMBO_IEEE0_MII_CONTROL,
				      &mii_control);
		mii_control &= ~(MDIO_COMBO_IEEO_MII_CONTROL_AN_EN |
				 MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_MASK|
				 MDIO_COMBO_IEEO_MII_CONTROL_FULL_DUPLEX);

		switch (vars->line_speed) {
		case SPEED_100:
			mii_control |=
				MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_100;
			break;
		case SPEED_1000:
			mii_control |=
				MDIO_COMBO_IEEO_MII_CONTROL_MAN_SGMII_SP_1000;
			break;
		case SPEED_10:
			/* there is nothing to set for 10M */
			break;
		default:
			/* invalid speed for SGMII */
			DP(NETIF_MSG_LINK, "Invalid line_speed 0x%x\n",
				  vars->line_speed);
			break;
		}

		/* setting the full duplex */
		if (params->req_duplex == DUPLEX_FULL)
			mii_control |=
				MDIO_COMBO_IEEO_MII_CONTROL_FULL_DUPLEX;
		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_COMBO_IEEE0,
				      MDIO_COMBO_IEEE0_MII_CONTROL,
				      mii_control);

	} else { /* AN mode */
		/* enable and restart AN */
		bnx2x_restart_autoneg(params);
	}
}


/*
 * link management
 */

static void bnx2x_pause_resolve(struct link_vars *vars, u32 pause_result)
{						/*  LD	    LP	 */
	switch (pause_result) { 		/* ASYM P ASYM P */
	case 0xb:       			/*   1  0   1  1 */
		vars->flow_ctrl = BNX2X_FLOW_CTRL_TX;
		break;

	case 0xe:       			/*   1  1   1  0 */
		vars->flow_ctrl = BNX2X_FLOW_CTRL_RX;
		break;

	case 0x5:       			/*   0  1   0  1 */
	case 0x7:       			/*   0  1   1  1 */
	case 0xd:       			/*   1  1   0  1 */
	case 0xf:       			/*   1  1   1  1 */
		vars->flow_ctrl = BNX2X_FLOW_CTRL_BOTH;
		break;

	default:
		break;
	}
}

static u8 bnx2x_ext_phy_resove_fc(struct link_params *params,
				  struct link_vars *vars)
{
	struct bnx2x *bp = params->bp;
	u8 ext_phy_addr;
	u16 ld_pause;	/* local */
	u16 lp_pause;	/* link partner */
	u16 an_complete; /* AN complete */
	u16 pause_result;
	u8 ret = 0;
	u32 ext_phy_type;
	u8 port = params->port;
	ext_phy_addr = ((params->ext_phy_config &
			 PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);

	ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);
	/* read twice */

	bnx2x_cl45_read(bp, port,
		      ext_phy_type,
		      ext_phy_addr,
		      MDIO_AN_DEVAD,
		      MDIO_AN_REG_STATUS, &an_complete);
	bnx2x_cl45_read(bp, port,
		      ext_phy_type,
		      ext_phy_addr,
		      MDIO_AN_DEVAD,
		      MDIO_AN_REG_STATUS, &an_complete);

	if (an_complete & MDIO_AN_REG_STATUS_AN_COMPLETE) {
		ret = 1;
		bnx2x_cl45_read(bp, port,
			      ext_phy_type,
			      ext_phy_addr,
			      MDIO_AN_DEVAD,
			      MDIO_AN_REG_ADV_PAUSE, &ld_pause);
		bnx2x_cl45_read(bp, port,
			      ext_phy_type,
			      ext_phy_addr,
			      MDIO_AN_DEVAD,
			      MDIO_AN_REG_LP_AUTO_NEG, &lp_pause);
		pause_result = (ld_pause &
				MDIO_AN_REG_ADV_PAUSE_MASK) >> 8;
		pause_result |= (lp_pause &
				 MDIO_AN_REG_ADV_PAUSE_MASK) >> 10;
		DP(NETIF_MSG_LINK, "Ext PHY pause result 0x%x \n",
		   pause_result);
		bnx2x_pause_resolve(vars, pause_result);
		if (vars->flow_ctrl == BNX2X_FLOW_CTRL_NONE &&
		     ext_phy_type == PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073) {
			bnx2x_cl45_read(bp, port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_AN_DEVAD,
				      MDIO_AN_REG_CL37_FC_LD, &ld_pause);

			bnx2x_cl45_read(bp, port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_AN_DEVAD,
				      MDIO_AN_REG_CL37_FC_LP, &lp_pause);
			pause_result = (ld_pause &
				MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH) >> 5;
			pause_result |= (lp_pause &
				MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH) >> 7;

			bnx2x_pause_resolve(vars, pause_result);
			DP(NETIF_MSG_LINK, "Ext PHY CL37 pause result 0x%x \n",
				 pause_result);
		}
	}
	return ret;
}


static void bnx2x_flow_ctrl_resolve(struct link_params *params,
				  struct link_vars *vars,
				  u32 gp_status)
{
	struct bnx2x *bp = params->bp;
	u16 ld_pause;   /* local driver */
	u16 lp_pause;   /* link partner */
	u16 pause_result;

	vars->flow_ctrl = BNX2X_FLOW_CTRL_NONE;

	/* resolve from gp_status in case of AN complete and not sgmii */
	if ((params->req_flow_ctrl == BNX2X_FLOW_CTRL_AUTO) &&
	    (gp_status & MDIO_AN_CL73_OR_37_COMPLETE) &&
	    (!(vars->phy_flags & PHY_SGMII_FLAG)) &&
	    (XGXS_EXT_PHY_TYPE(params->ext_phy_config) ==
	     PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT)) {
		CL45_RD_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_COMBO_IEEE0,
				      MDIO_COMBO_IEEE0_AUTO_NEG_ADV,
				      &ld_pause);
		CL45_RD_OVER_CL22(bp, params->port,
				      params->phy_addr,
			MDIO_REG_BANK_COMBO_IEEE0,
			MDIO_COMBO_IEEE0_AUTO_NEG_LINK_PARTNER_ABILITY1,
			&lp_pause);
		pause_result = (ld_pause &
				MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_MASK)>>5;
		pause_result |= (lp_pause &
				 MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_MASK)>>7;
		DP(NETIF_MSG_LINK, "pause_result 0x%x\n", pause_result);
		bnx2x_pause_resolve(vars, pause_result);
	} else if ((params->req_flow_ctrl == BNX2X_FLOW_CTRL_AUTO) &&
		   (bnx2x_ext_phy_resove_fc(params, vars))) {
		return;
	} else {
		if (params->req_flow_ctrl == BNX2X_FLOW_CTRL_AUTO)
			vars->flow_ctrl = params->req_fc_auto_adv;
		else
			vars->flow_ctrl = params->req_flow_ctrl;
	}
	DP(NETIF_MSG_LINK, "flow_ctrl 0x%x\n", vars->flow_ctrl);
}


static u8 bnx2x_link_settings_status(struct link_params *params,
				      struct link_vars *vars,
				      u32 gp_status)
{
	struct bnx2x *bp = params->bp;
	u16 new_line_speed;
	u8 rc = 0;
	vars->link_status = 0;

	if (gp_status & MDIO_GP_STATUS_TOP_AN_STATUS1_LINK_STATUS) {
		DP(NETIF_MSG_LINK, "phy link up gp_status=0x%x\n",
			 gp_status);

		vars->phy_link_up = 1;
		vars->link_status |= LINK_STATUS_LINK_UP;

		if (gp_status & MDIO_GP_STATUS_TOP_AN_STATUS1_DUPLEX_STATUS)
			vars->duplex = DUPLEX_FULL;
		else
			vars->duplex = DUPLEX_HALF;

		bnx2x_flow_ctrl_resolve(params, vars, gp_status);

		switch (gp_status & GP_STATUS_SPEED_MASK) {
		case GP_STATUS_10M:
			new_line_speed = SPEED_10;
			if (vars->duplex == DUPLEX_FULL)
				vars->link_status |= LINK_10TFD;
			else
				vars->link_status |= LINK_10THD;
			break;

		case GP_STATUS_100M:
			new_line_speed = SPEED_100;
			if (vars->duplex == DUPLEX_FULL)
				vars->link_status |= LINK_100TXFD;
			else
				vars->link_status |= LINK_100TXHD;
			break;

		case GP_STATUS_1G:
		case GP_STATUS_1G_KX:
			new_line_speed = SPEED_1000;
			if (vars->duplex == DUPLEX_FULL)
				vars->link_status |= LINK_1000TFD;
			else
				vars->link_status |= LINK_1000THD;
			break;

		case GP_STATUS_2_5G:
			new_line_speed = SPEED_2500;
			if (vars->duplex == DUPLEX_FULL)
				vars->link_status |= LINK_2500TFD;
			else
				vars->link_status |= LINK_2500THD;
			break;

		case GP_STATUS_5G:
		case GP_STATUS_6G:
			DP(NETIF_MSG_LINK,
				 "link speed unsupported  gp_status 0x%x\n",
				  gp_status);
			return -EINVAL;
			break;
		case GP_STATUS_10G_KX4:
		case GP_STATUS_10G_HIG:
		case GP_STATUS_10G_CX4:
			new_line_speed = SPEED_10000;
			vars->link_status |= LINK_10GTFD;
			break;

		case GP_STATUS_12G_HIG:
			new_line_speed = SPEED_12000;
			vars->link_status |= LINK_12GTFD;
			break;

		case GP_STATUS_12_5G:
			new_line_speed = SPEED_12500;
			vars->link_status |= LINK_12_5GTFD;
			break;

		case GP_STATUS_13G:
			new_line_speed = SPEED_13000;
			vars->link_status |= LINK_13GTFD;
			break;

		case GP_STATUS_15G:
			new_line_speed = SPEED_15000;
			vars->link_status |= LINK_15GTFD;
			break;

		case GP_STATUS_16G:
			new_line_speed = SPEED_16000;
			vars->link_status |= LINK_16GTFD;
			break;

		default:
			DP(NETIF_MSG_LINK,
				  "link speed unsupported gp_status 0x%x\n",
				  gp_status);
		return -EINVAL;
			break;
		}

		/* Upon link speed change set the NIG into drain mode.
		Comes to deals with possible FIFO glitch due to clk change
		when speed is decreased without link down indicator */
		if (new_line_speed != vars->line_speed) {
			REG_WR(bp, NIG_REG_EGRESS_DRAIN0_MODE
				    + params->port*4, 0);
			msleep(1);
		}
		vars->line_speed = new_line_speed;
		vars->link_status |= LINK_STATUS_SERDES_LINK;

		if ((params->req_line_speed == SPEED_AUTO_NEG) &&
		    ((XGXS_EXT_PHY_TYPE(params->ext_phy_config) ==
		     PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT) ||
		    (XGXS_EXT_PHY_TYPE(params->ext_phy_config) ==
		     PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705))) {
			vars->autoneg = AUTO_NEG_ENABLED;

			if (gp_status & MDIO_AN_CL73_OR_37_COMPLETE) {
				vars->autoneg |= AUTO_NEG_COMPLETE;
				vars->link_status |=
					LINK_STATUS_AUTO_NEGOTIATE_COMPLETE;
			}

			vars->autoneg |= AUTO_NEG_PARALLEL_DETECTION_USED;
			vars->link_status |=
				LINK_STATUS_PARALLEL_DETECTION_USED;

		}
		if (vars->flow_ctrl & BNX2X_FLOW_CTRL_TX)
			vars->link_status |=
				LINK_STATUS_TX_FLOW_CONTROL_ENABLED;

		if (vars->flow_ctrl & BNX2X_FLOW_CTRL_RX)
			vars->link_status |=
				LINK_STATUS_RX_FLOW_CONTROL_ENABLED;

	} else { /* link_down */
		DP(NETIF_MSG_LINK, "phy link down\n");

		vars->phy_link_up = 0;

		vars->duplex = DUPLEX_FULL;
		vars->flow_ctrl = BNX2X_FLOW_CTRL_NONE;
		vars->autoneg = AUTO_NEG_DISABLED;
		vars->mac_type = MAC_TYPE_NONE;
	}

	DP(NETIF_MSG_LINK, "gp_status 0x%x  phy_link_up %x line_speed %x \n",
		 gp_status, vars->phy_link_up, vars->line_speed);
	DP(NETIF_MSG_LINK, "duplex %x  flow_ctrl 0x%x"
		 " autoneg 0x%x\n",
		 vars->duplex,
		 vars->flow_ctrl, vars->autoneg);
	DP(NETIF_MSG_LINK, "link_status 0x%x\n", vars->link_status);

	return rc;
}

static void bnx2x_set_sgmii_tx_driver(struct link_params *params)
{
	struct bnx2x *bp = params->bp;
	u16 lp_up2;
	u16 tx_driver;

	/* read precomp */

	CL45_RD_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_OVER_1G,
			      MDIO_OVER_1G_LP_UP2, &lp_up2);

	CL45_RD_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_TX0,
			      MDIO_TX0_TX_DRIVER, &tx_driver);

	/* bits [10:7] at lp_up2, positioned at [15:12] */
	lp_up2 = (((lp_up2 & MDIO_OVER_1G_LP_UP2_PREEMPHASIS_MASK) >>
		   MDIO_OVER_1G_LP_UP2_PREEMPHASIS_SHIFT) <<
		  MDIO_TX0_TX_DRIVER_PREEMPHASIS_SHIFT);

	if ((lp_up2 != 0) &&
	    (lp_up2 != (tx_driver & MDIO_TX0_TX_DRIVER_PREEMPHASIS_MASK))) {
		/* replace tx_driver bits [15:12] */
		tx_driver &= ~MDIO_TX0_TX_DRIVER_PREEMPHASIS_MASK;
		tx_driver |= lp_up2;
		CL45_WR_OVER_CL22(bp, params->port,
				      params->phy_addr,
				      MDIO_REG_BANK_TX0,
				      MDIO_TX0_TX_DRIVER, tx_driver);
	}
}

static u8 bnx2x_emac_program(struct link_params *params,
			   u32 line_speed, u32 duplex)
{
	struct bnx2x *bp = params->bp;
	u8 port = params->port;
	u16 mode = 0;

	DP(NETIF_MSG_LINK, "setting link speed & duplex\n");
	bnx2x_bits_dis(bp, GRCBASE_EMAC0 + port*0x400 +
		     EMAC_REG_EMAC_MODE,
		     (EMAC_MODE_25G_MODE |
		     EMAC_MODE_PORT_MII_10M |
		     EMAC_MODE_HALF_DUPLEX));
	switch (line_speed) {
	case SPEED_10:
		mode |= EMAC_MODE_PORT_MII_10M;
		break;

	case SPEED_100:
		mode |= EMAC_MODE_PORT_MII;
		break;

	case SPEED_1000:
		mode |= EMAC_MODE_PORT_GMII;
		break;

	case SPEED_2500:
		mode |= (EMAC_MODE_25G_MODE | EMAC_MODE_PORT_GMII);
		break;

	default:
		/* 10G not valid for EMAC */
		DP(NETIF_MSG_LINK, "Invalid line_speed 0x%x\n", line_speed);
		return -EINVAL;
	}

	if (duplex == DUPLEX_HALF)
		mode |= EMAC_MODE_HALF_DUPLEX;
	bnx2x_bits_en(bp,
		    GRCBASE_EMAC0 + port*0x400 + EMAC_REG_EMAC_MODE,
		    mode);

	bnx2x_set_led(bp, params->port, LED_MODE_OPER,
		    line_speed, params->hw_led_mode, params->chip_id);
	return 0;
}

/*****************************************************************************/
/*      		     External Phy section       		     */
/*****************************************************************************/
static void bnx2x_hw_reset(struct bnx2x *bp, u8 port)
{
	bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_1,
		       MISC_REGISTERS_GPIO_OUTPUT_LOW, port);
	msleep(1);
	bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_1,
		      MISC_REGISTERS_GPIO_OUTPUT_HIGH, port);
}

static void bnx2x_ext_phy_reset(struct link_params *params,
			      struct link_vars   *vars)
{
	struct bnx2x *bp = params->bp;
	u32 ext_phy_type;
	u8 ext_phy_addr = ((params->ext_phy_config &
			    PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
			   PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);
	DP(NETIF_MSG_LINK, "Port %x: bnx2x_ext_phy_reset\n", params->port);
	ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);
	/* The PHY reset is controled by GPIO 1
	 * Give it 1ms of reset pulse
	 */
	if (vars->phy_flags & PHY_XGXS_FLAG) {

		switch (ext_phy_type) {
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT:
			DP(NETIF_MSG_LINK, "XGXS Direct\n");
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705:
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706:
			DP(NETIF_MSG_LINK, "XGXS 8705/8706\n");

			/* Restore normal power mode*/
			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_2,
				      MISC_REGISTERS_GPIO_OUTPUT_HIGH,
					  params->port);

			/* HW reset */
			bnx2x_hw_reset(bp, params->port);

			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_PMA_DEVAD,
				       MDIO_PMA_REG_CTRL, 0xa040);
			break;
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
			/* Unset Low Power Mode and SW reset */
			/* Restore normal power mode*/
			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_2,
				      MISC_REGISTERS_GPIO_OUTPUT_HIGH,
					  params->port);

			DP(NETIF_MSG_LINK, "XGXS 8072\n");
			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_PMA_DEVAD,
				       MDIO_PMA_REG_CTRL,
				       1<<15);
			break;
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073:
			{
			u16 emac_base;
			emac_base = (params->port) ? GRCBASE_EMAC0 :
					GRCBASE_EMAC1;

			/* Restore normal power mode*/
			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_2,
				      MISC_REGISTERS_GPIO_OUTPUT_HIGH,
					  params->port);

			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_1,
				      MISC_REGISTERS_GPIO_OUTPUT_HIGH,
					  params->port);

			DP(NETIF_MSG_LINK, "XGXS 8073\n");
			}
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
			DP(NETIF_MSG_LINK, "XGXS SFX7101\n");

			/* Restore normal power mode*/
			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_2,
				      MISC_REGISTERS_GPIO_OUTPUT_HIGH,
					  params->port);

			/* HW reset */
			bnx2x_hw_reset(bp, params->port);

			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE:
			DP(NETIF_MSG_LINK, "XGXS PHY Failure detected\n");
			break;

		default:
			DP(NETIF_MSG_LINK, "BAD XGXS ext_phy_config 0x%x\n",
			   params->ext_phy_config);
			break;
		}

	} else { /* SerDes */
		ext_phy_type = SERDES_EXT_PHY_TYPE(params->ext_phy_config);
		switch (ext_phy_type) {
		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT:
			DP(NETIF_MSG_LINK, "SerDes Direct\n");
			break;

		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_BCM5482:
			DP(NETIF_MSG_LINK, "SerDes 5482\n");
			bnx2x_hw_reset(bp, params->port);
			break;

		default:
			DP(NETIF_MSG_LINK,
				 "BAD SerDes ext_phy_config 0x%x\n",
				 params->ext_phy_config);
			break;
		}
	}
}

static void bnx2x_bcm8072_external_rom_boot(struct link_params *params)
{
	struct bnx2x *bp = params->bp;
	u8 port = params->port;
	u8 ext_phy_addr = ((params->ext_phy_config &
			     PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
			    PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);
	u32 ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);
	u16 fw_ver1, fw_ver2;

	/* Need to wait 200ms after reset */
	msleep(200);
	/* Boot port from external ROM
	 * Set ser_boot_ctl bit in the MISC_CTRL1 register
	 */
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
			    MDIO_PMA_DEVAD,
			    MDIO_PMA_REG_MISC_CTRL1, 0x0001);

	/* Reset internal microprocessor */
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
			  MDIO_PMA_DEVAD,
			  MDIO_PMA_REG_GEN_CTRL,
			  MDIO_PMA_REG_GEN_CTRL_ROM_RESET_INTERNAL_MP);
	/* set micro reset = 0 */
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
			    MDIO_PMA_DEVAD,
			    MDIO_PMA_REG_GEN_CTRL,
			    MDIO_PMA_REG_GEN_CTRL_ROM_MICRO_RESET);
	/* Reset internal microprocessor */
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
			  MDIO_PMA_DEVAD,
			  MDIO_PMA_REG_GEN_CTRL,
			  MDIO_PMA_REG_GEN_CTRL_ROM_RESET_INTERNAL_MP);
	/* wait for 100ms for code download via SPI port */
	msleep(100);

	/* Clear ser_boot_ctl bit */
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
			    MDIO_PMA_DEVAD,
			    MDIO_PMA_REG_MISC_CTRL1, 0x0000);
	/* Wait 100ms */
	msleep(100);

	/* Print the PHY FW version */
	bnx2x_cl45_read(bp, port, ext_phy_type, ext_phy_addr,
			    MDIO_PMA_DEVAD,
			    MDIO_PMA_REG_ROM_VER1, &fw_ver1);
	bnx2x_cl45_read(bp, port, ext_phy_type, ext_phy_addr,
			    MDIO_PMA_DEVAD,
			    MDIO_PMA_REG_ROM_VER2, &fw_ver2);
	DP(NETIF_MSG_LINK, "8072 FW version 0x%x:0x%x\n", fw_ver1, fw_ver2);
}

static u8 bnx2x_8073_is_snr_needed(struct link_params *params)
{
	/* This is only required for 8073A1, version 102 only */

	struct bnx2x *bp = params->bp;
	u8 ext_phy_addr = ((params->ext_phy_config &
			     PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
			    PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);
	u16 val;

	/* Read 8073 HW revision*/
	bnx2x_cl45_read(bp, params->port,
		      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
		      ext_phy_addr,
		      MDIO_PMA_DEVAD,
		      0xc801, &val);

	if (val != 1) {
		/* No need to workaround in 8073 A1 */
		return 0;
	}

	bnx2x_cl45_read(bp, params->port,
		      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
		      ext_phy_addr,
		      MDIO_PMA_DEVAD,
		      MDIO_PMA_REG_ROM_VER2, &val);

	/* SNR should be applied only for version 0x102 */
	if (val != 0x102)
		return 0;

	return 1;
}

static u8 bnx2x_bcm8073_xaui_wa(struct link_params *params)
{
	struct bnx2x *bp = params->bp;
	u8 ext_phy_addr = ((params->ext_phy_config &
			     PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
			    PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);
	u16 val, cnt, cnt1 ;

	bnx2x_cl45_read(bp, params->port,
		      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
		      ext_phy_addr,
		      MDIO_PMA_DEVAD,
		      0xc801, &val);

	if (val > 0) {
		/* No need to workaround in 8073 A1 */
		return 0;
	}
	/* XAUI workaround in 8073 A0: */

	/* After loading the boot ROM and restarting Autoneg,
	poll Dev1, Reg $C820: */

	for (cnt = 0; cnt < 1000; cnt++) {
		bnx2x_cl45_read(bp, params->port,
			      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
			      ext_phy_addr,
			      MDIO_PMA_DEVAD,
			      0xc820, &val);
		  /* If bit [14] = 0 or bit [13] = 0, continue on with
		   system initialization (XAUI work-around not required,
		    as these bits indicate 2.5G or 1G link up). */
		if (!(val & (1<<14)) || !(val & (1<<13))) {
			DP(NETIF_MSG_LINK, "XAUI work-around not required\n");
			return 0;
		} else if (!(val & (1<<15))) {
			DP(NETIF_MSG_LINK, "clc bit 15 went off\n");
			 /* If bit 15 is 0, then poll Dev1, Reg $C841 until
			  it's MSB (bit 15) goes to 1 (indicating that the
			  XAUI workaround has completed),
			  then continue on with system initialization.*/
			for (cnt1 = 0; cnt1 < 1000; cnt1++) {
				bnx2x_cl45_read(bp, params->port,
					PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
					ext_phy_addr,
					MDIO_PMA_DEVAD,
					0xc841, &val);
				if (val & (1<<15)) {
					DP(NETIF_MSG_LINK,
					  "XAUI workaround has completed\n");
					return 0;
				 }
				 msleep(3);
			}
			break;
		}
		msleep(3);
	}
	DP(NETIF_MSG_LINK, "Warning: XAUI work-around timeout !!!\n");
	return -EINVAL;

}

static void bnx2x_bcm8073_external_rom_boot(struct bnx2x *bp, u8 port,
					  u8 ext_phy_addr)
{
	u16 fw_ver1, fw_ver2;
	/* Boot port from external ROM  */
	/* EDC grst */
	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
		       ext_phy_addr,
		       MDIO_PMA_DEVAD,
		       MDIO_PMA_REG_GEN_CTRL,
		       0x0001);

	/* ucode reboot and rst */
	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
		       ext_phy_addr,
		       MDIO_PMA_DEVAD,
		       MDIO_PMA_REG_GEN_CTRL,
		       0x008c);

	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
		       ext_phy_addr,
		       MDIO_PMA_DEVAD,
		       MDIO_PMA_REG_MISC_CTRL1, 0x0001);

	/* Reset internal microprocessor */
	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
		       ext_phy_addr,
		       MDIO_PMA_DEVAD,
		       MDIO_PMA_REG_GEN_CTRL,
		       MDIO_PMA_REG_GEN_CTRL_ROM_MICRO_RESET);

	/* Release srst bit */
	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
		       ext_phy_addr,
		       MDIO_PMA_DEVAD,
		       MDIO_PMA_REG_GEN_CTRL,
		       MDIO_PMA_REG_GEN_CTRL_ROM_RESET_INTERNAL_MP);

	/* wait for 100ms for code download via SPI port */
	msleep(100);

	/* Clear ser_boot_ctl bit */
	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
		       ext_phy_addr,
		       MDIO_PMA_DEVAD,
		       MDIO_PMA_REG_MISC_CTRL1, 0x0000);

	bnx2x_cl45_read(bp, port, PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
		      ext_phy_addr,
		      MDIO_PMA_DEVAD,
		      MDIO_PMA_REG_ROM_VER1, &fw_ver1);
	bnx2x_cl45_read(bp, port,
		      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
		      ext_phy_addr,
		      MDIO_PMA_DEVAD,
		      MDIO_PMA_REG_ROM_VER2, &fw_ver2);
	DP(NETIF_MSG_LINK, "8073 FW version 0x%x:0x%x\n", fw_ver1, fw_ver2);

}

static void bnx2x_bcm807x_force_10G(struct link_params *params)
{
	struct bnx2x *bp = params->bp;
	u8 port = params->port;
	u8 ext_phy_addr = ((params->ext_phy_config &
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);
	u32 ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);

	/* Force KR or KX */
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_PMA_DEVAD,
		       MDIO_PMA_REG_CTRL,
		       0x2040);
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_PMA_DEVAD,
		       MDIO_PMA_REG_10G_CTRL2,
		       0x000b);
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_PMA_DEVAD,
		       MDIO_PMA_REG_BCM_CTRL,
		       0x0000);
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_AN_DEVAD,
		       MDIO_AN_REG_CTRL,
		       0x0000);
}
static void bnx2x_bcm8073_set_xaui_low_power_mode(struct link_params *params)
{
	struct bnx2x *bp = params->bp;
	u8 port = params->port;
	u16 val;
	u8 ext_phy_addr = ((params->ext_phy_config &
			     PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
			    PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);
	u32 ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);

	bnx2x_cl45_read(bp, params->port,
		      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
		      ext_phy_addr,
		      MDIO_PMA_DEVAD,
		      0xc801, &val);

	if (val == 0) {
		/* Mustn't set low power mode in 8073 A0 */
		return;
	}

	/* Disable PLL sequencer (use read-modify-write to clear bit 13) */
	bnx2x_cl45_read(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD,
		       MDIO_XS_PLL_SEQUENCER, &val);
	val &= ~(1<<13);
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD, MDIO_XS_PLL_SEQUENCER, val);

	/* PLL controls */
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD, 0x805E, 0x1077);
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD, 0x805D, 0x0000);
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD, 0x805C, 0x030B);
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD, 0x805B, 0x1240);
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD, 0x805A, 0x2490);

	/* Tx Controls */
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD, 0x80A7, 0x0C74);
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD, 0x80A6, 0x9041);
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD, 0x80A5, 0x4640);

	/* Rx Controls */
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD, 0x80FE, 0x01C4);
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD, 0x80FD, 0x9249);
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD, 0x80FC, 0x2015);

	/* Enable PLL sequencer  (use read-modify-write to set bit 13) */
	bnx2x_cl45_read(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD,
		       MDIO_XS_PLL_SEQUENCER, &val);
	val |= (1<<13);
	bnx2x_cl45_write(bp, port, ext_phy_type, ext_phy_addr,
		       MDIO_XS_DEVAD, MDIO_XS_PLL_SEQUENCER, val);
}

static void bnx2x_8073_set_pause_cl37(struct link_params *params,
				  struct link_vars *vars)
{

	struct bnx2x *bp = params->bp;
	u16 cl37_val;
	u8 ext_phy_addr = ((params->ext_phy_config &
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);
	u32 ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);

	bnx2x_cl45_read(bp, params->port,
		      ext_phy_type,
		      ext_phy_addr,
		      MDIO_AN_DEVAD,
		      MDIO_AN_REG_CL37_FC_LD, &cl37_val);

	cl37_val &= ~MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH;
	/* Please refer to Table 28B-3 of 802.3ab-1999 spec. */

	if ((vars->ieee_fc &
	    MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_SYMMETRIC) ==
	    MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_SYMMETRIC) {
		cl37_val |=  MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_SYMMETRIC;
	}
	if ((vars->ieee_fc &
	    MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC) ==
	    MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC) {
		cl37_val |=  MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC;
	}
	if ((vars->ieee_fc &
	    MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH) ==
	    MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH) {
		cl37_val |= MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH;
	}
	DP(NETIF_MSG_LINK,
		 "Ext phy AN advertize cl37 0x%x\n", cl37_val);

	bnx2x_cl45_write(bp, params->port,
		       ext_phy_type,
		       ext_phy_addr,
		       MDIO_AN_DEVAD,
		       MDIO_AN_REG_CL37_FC_LD, cl37_val);
	msleep(500);
}

static void bnx2x_ext_phy_set_pause(struct link_params *params,
				  struct link_vars *vars)
{
	struct bnx2x *bp = params->bp;
	u16 val;
	u8 ext_phy_addr = ((params->ext_phy_config &
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);
	u32 ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);

	/* read modify write pause advertizing */
	bnx2x_cl45_read(bp, params->port,
		      ext_phy_type,
		      ext_phy_addr,
		      MDIO_AN_DEVAD,
		      MDIO_AN_REG_ADV_PAUSE, &val);

	val &= ~MDIO_AN_REG_ADV_PAUSE_BOTH;

	/* Please refer to Table 28B-3 of 802.3ab-1999 spec. */

	if ((vars->ieee_fc &
	    MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC) ==
	    MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_ASYMMETRIC) {
		val |=  MDIO_AN_REG_ADV_PAUSE_ASYMMETRIC;
	}
	if ((vars->ieee_fc &
	    MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH) ==
	    MDIO_COMBO_IEEE0_AUTO_NEG_ADV_PAUSE_BOTH) {
		val |=
		 MDIO_AN_REG_ADV_PAUSE_PAUSE;
	}
	DP(NETIF_MSG_LINK,
		 "Ext phy AN advertize 0x%x\n", val);
	bnx2x_cl45_write(bp, params->port,
		       ext_phy_type,
		       ext_phy_addr,
		       MDIO_AN_DEVAD,
		       MDIO_AN_REG_ADV_PAUSE, val);
}


static void bnx2x_init_internal_phy(struct link_params *params,
				struct link_vars *vars)
{
	struct bnx2x *bp = params->bp;
	u8 port = params->port;
	if (!(vars->phy_flags & PHY_SGMII_FLAG)) {
		u16 bank, rx_eq;

		rx_eq = ((params->serdes_config &
			  PORT_HW_CFG_SERDES_RX_DRV_EQUALIZER_MASK) >>
			 PORT_HW_CFG_SERDES_RX_DRV_EQUALIZER_SHIFT);

		DP(NETIF_MSG_LINK, "setting rx eq to 0x%x\n", rx_eq);
		for (bank = MDIO_REG_BANK_RX0; bank <= MDIO_REG_BANK_RX_ALL;
		      bank += (MDIO_REG_BANK_RX1-MDIO_REG_BANK_RX0)) {
			CL45_WR_OVER_CL22(bp, port,
					      params->phy_addr,
					      bank ,
					      MDIO_RX0_RX_EQ_BOOST,
					      ((rx_eq &
				MDIO_RX0_RX_EQ_BOOST_EQUALIZER_CTRL_MASK) |
				MDIO_RX0_RX_EQ_BOOST_OFFSET_CTRL));
		}

		/* forced speed requested? */
		if (vars->line_speed != SPEED_AUTO_NEG) {
			DP(NETIF_MSG_LINK, "not SGMII, no AN\n");

			/* disable autoneg */
			bnx2x_set_autoneg(params, vars);

			/* program speed and duplex */
			bnx2x_program_serdes(params, vars);

		} else { /* AN_mode */
			DP(NETIF_MSG_LINK, "not SGMII, AN\n");

			/* AN enabled */
			bnx2x_set_brcm_cl37_advertisment(params);

			/* program duplex & pause advertisement (for aneg) */
			bnx2x_set_ieee_aneg_advertisment(params,
						       vars->ieee_fc);

			/* enable autoneg */
			bnx2x_set_autoneg(params, vars);

			/* enable and restart AN */
			bnx2x_restart_autoneg(params);
		}

	} else { /* SGMII mode */
		DP(NETIF_MSG_LINK, "SGMII\n");

		bnx2x_initialize_sgmii_process(params, vars);
	}
}

static u8 bnx2x_ext_phy_init(struct link_params *params, struct link_vars *vars)
{
	struct bnx2x *bp = params->bp;
	u32 ext_phy_type;
	u8 ext_phy_addr;
	u16 cnt;
	u16 ctrl = 0;
	u16 val = 0;
	u8 rc = 0;
	if (vars->phy_flags & PHY_XGXS_FLAG) {
		ext_phy_addr = ((params->ext_phy_config &
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);

		ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);
		/* Make sure that the soft reset is off (expect for the 8072:
		 * due to the lock, it will be done inside the specific
		 * handling)
		 */
		if ((ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT) &&
		    (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE) &&
		   (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_NOT_CONN) &&
		    (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072) &&
		    (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073)) {
			/* Wait for soft reset to get cleared upto 1 sec */
			for (cnt = 0; cnt < 1000; cnt++) {
				bnx2x_cl45_read(bp, params->port,
					      ext_phy_type,
					      ext_phy_addr,
					      MDIO_PMA_DEVAD,
					      MDIO_PMA_REG_CTRL, &ctrl);
				if (!(ctrl & (1<<15)))
					break;
				msleep(1);
			}
			DP(NETIF_MSG_LINK, "control reg 0x%x (after %d ms)\n",
				 ctrl, cnt);
		}

		switch (ext_phy_type) {
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT:
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705:
			DP(NETIF_MSG_LINK, "XGXS 8705\n");

			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_PMA_DEVAD,
				       MDIO_PMA_REG_MISC_CTRL,
				       0x8288);
			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_PMA_DEVAD,
				       MDIO_PMA_REG_PHY_IDENTIFIER,
				       0x7fbf);
			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_PMA_DEVAD,
				       MDIO_PMA_REG_CMU_PLL_BYPASS,
				       0x0100);
			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_WIS_DEVAD,
				       MDIO_WIS_REG_LASI_CNTL, 0x1);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706:
			DP(NETIF_MSG_LINK, "XGXS 8706\n");

			msleep(10);
			/* Force speed */
			/* First enable LASI */
			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_PMA_DEVAD,
				       MDIO_PMA_REG_RX_ALARM_CTRL,
				       0x0400);
			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_PMA_DEVAD,
				       MDIO_PMA_REG_LASI_CTRL, 0x0004);

			if (params->req_line_speed == SPEED_10000) {
				DP(NETIF_MSG_LINK, "XGXS 8706 force 10Gbps\n");

				bnx2x_cl45_write(bp, params->port,
					       ext_phy_type,
					       ext_phy_addr,
					       MDIO_PMA_DEVAD,
					       MDIO_PMA_REG_DIGITAL_CTRL,
					       0x400);
			} else {
				/* Force 1Gbps using autoneg with 1G
				advertisment */

				/* Allow CL37 through CL73 */
				DP(NETIF_MSG_LINK, "XGXS 8706 AutoNeg\n");
				bnx2x_cl45_write(bp, params->port,
					       ext_phy_type,
					       ext_phy_addr,
					       MDIO_AN_DEVAD,
					       MDIO_AN_REG_CL37_CL73,
					       0x040c);

				/* Enable Full-Duplex advertisment on CL37 */
				bnx2x_cl45_write(bp, params->port,
					       ext_phy_type,
					       ext_phy_addr,
					       MDIO_AN_DEVAD,
					       MDIO_AN_REG_CL37_FC_LP,
					       0x0020);
				/* Enable CL37 AN */
				bnx2x_cl45_write(bp, params->port,
					       ext_phy_type,
					       ext_phy_addr,
					       MDIO_AN_DEVAD,
					       MDIO_AN_REG_CL37_AN,
					       0x1000);
				/* 1G support */
				bnx2x_cl45_write(bp, params->port,
					       ext_phy_type,
					       ext_phy_addr,
					       MDIO_AN_DEVAD,
					       MDIO_AN_REG_ADV, (1<<5));

				/* Enable clause 73 AN */
				bnx2x_cl45_write(bp, params->port,
					       ext_phy_type,
					       ext_phy_addr,
					       MDIO_AN_DEVAD,
					       MDIO_AN_REG_CTRL,
					       0x1200);

			}

			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073:
		{
			u16 tmp1;
			u16 rx_alarm_ctrl_val;
			u16 lasi_ctrl_val;
			if (ext_phy_type ==
			    PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072) {
				rx_alarm_ctrl_val = 0x400;
				lasi_ctrl_val = 0x0004;
			} else {
				rx_alarm_ctrl_val = (1<<2);
				lasi_ctrl_val = 0x0004;
			}

			/* enable LASI */
			bnx2x_cl45_write(bp, params->port,
				   ext_phy_type,
				   ext_phy_addr,
				   MDIO_PMA_DEVAD,
				   MDIO_PMA_REG_RX_ALARM_CTRL,
				   rx_alarm_ctrl_val);

			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_PMA_DEVAD,
				       MDIO_PMA_REG_LASI_CTRL,
				       lasi_ctrl_val);

			bnx2x_8073_set_pause_cl37(params, vars);

			if (ext_phy_type ==
			    PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072){
				bnx2x_bcm8072_external_rom_boot(params);
			} else {

				/* In case of 8073 with long xaui lines,
				don't set the 8073 xaui low power*/
				bnx2x_bcm8073_set_xaui_low_power_mode(params);
			}

			bnx2x_cl45_read(bp, params->port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      0xca13,
				      &tmp1);

			bnx2x_cl45_read(bp, params->port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      MDIO_PMA_REG_RX_ALARM, &tmp1);

			DP(NETIF_MSG_LINK, "Before rom RX_ALARM(port1):"
					     "0x%x\n", tmp1);

			/* If this is forced speed, set to KR or KX
			 * (all other are not supported)
			 */
			if (params->loopback_mode == LOOPBACK_EXT) {
				bnx2x_bcm807x_force_10G(params);
				DP(NETIF_MSG_LINK,
					"Forced speed 10G on 807X\n");
				break;
			} else {
				bnx2x_cl45_write(bp, params->port,
					       ext_phy_type, ext_phy_addr,
					       MDIO_PMA_DEVAD,
					       MDIO_PMA_REG_BCM_CTRL,
					       0x0002);
			}
			if (params->req_line_speed != SPEED_AUTO_NEG) {
				if (params->req_line_speed == SPEED_10000) {
					val = (1<<7);
				} else if (params->req_line_speed ==
					   SPEED_2500) {
					val = (1<<5);
					/* Note that 2.5G works only
					when used with 1G advertisment */
				} else
					val = (1<<5);
			} else {

				val = 0;
				if (params->speed_cap_mask &
					PORT_HW_CFG_SPEED_CAPABILITY_D0_10G)
					val |= (1<<7);

				/* Note that 2.5G works only when
				used with 1G advertisment */
				if (params->speed_cap_mask &
					(PORT_HW_CFG_SPEED_CAPABILITY_D0_1G |
					 PORT_HW_CFG_SPEED_CAPABILITY_D0_2_5G))
					val |= (1<<5);
				DP(NETIF_MSG_LINK,
					 "807x autoneg val = 0x%x\n", val);
			}

			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_AN_DEVAD,
				       MDIO_AN_REG_ADV, val);

			if (ext_phy_type ==
			    PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073) {

				bnx2x_cl45_read(bp, params->port,
					      ext_phy_type,
					      ext_phy_addr,
					      MDIO_AN_DEVAD,
					      0x8329, &tmp1);

				if (((params->speed_cap_mask &
				      PORT_HW_CFG_SPEED_CAPABILITY_D0_2_5G) &&
				     (params->req_line_speed ==
				      SPEED_AUTO_NEG)) ||
				    (params->req_line_speed ==
				     SPEED_2500)) {
					u16 phy_ver;
					/* Allow 2.5G for A1 and above */
					bnx2x_cl45_read(bp, params->port,
					 PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
					 ext_phy_addr,
					 MDIO_PMA_DEVAD,
					 0xc801, &phy_ver);
					DP(NETIF_MSG_LINK, "Add 2.5G\n");
					if (phy_ver > 0)
						tmp1 |= 1;
					else
						tmp1 &= 0xfffe;
				} else {
					DP(NETIF_MSG_LINK, "Disable 2.5G\n");
					tmp1 &= 0xfffe;
				}

				bnx2x_cl45_write(bp, params->port,
					       ext_phy_type,
					       ext_phy_addr,
					       MDIO_AN_DEVAD,
					       0x8329, tmp1);
			}

			/* Add support for CL37 (passive mode) II */

			bnx2x_cl45_read(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_AN_DEVAD,
				       MDIO_AN_REG_CL37_FC_LD,
				       &tmp1);

			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_AN_DEVAD,
				       MDIO_AN_REG_CL37_FC_LD, (tmp1 |
				       ((params->req_duplex == DUPLEX_FULL) ?
				       0x20 : 0x40)));

			/* Add support for CL37 (passive mode) III */
			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_AN_DEVAD,
				       MDIO_AN_REG_CL37_AN, 0x1000);

			if (ext_phy_type ==
			    PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073) {
				/* The SNR will improve about 2db by changing
				BW and FEE main tap. Rest commands are executed
				after link is up*/
				/*Change FFE main cursor to 5 in EDC register*/
				if (bnx2x_8073_is_snr_needed(params))
					bnx2x_cl45_write(bp, params->port,
						    ext_phy_type,
						    ext_phy_addr,
						    MDIO_PMA_DEVAD,
						    MDIO_PMA_REG_EDC_FFE_MAIN,
						    0xFB0C);

				/* Enable FEC (Forware Error Correction)
				Request in the AN */
				bnx2x_cl45_read(bp, params->port,
					      ext_phy_type,
					      ext_phy_addr,
					      MDIO_AN_DEVAD,
					      MDIO_AN_REG_ADV2, &tmp1);

				tmp1 |= (1<<15);

				bnx2x_cl45_write(bp, params->port,
					       ext_phy_type,
					       ext_phy_addr,
					       MDIO_AN_DEVAD,
					       MDIO_AN_REG_ADV2, tmp1);

			}

			bnx2x_ext_phy_set_pause(params, vars);

			/* Restart autoneg */
			msleep(500);
			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_AN_DEVAD,
				       MDIO_AN_REG_CTRL, 0x1200);
			DP(NETIF_MSG_LINK, "807x Autoneg Restart: "
			   "Advertise 1G=%x, 10G=%x\n",
			   ((val & (1<<5)) > 0),
			   ((val & (1<<7)) > 0));
			break;
		}
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
			DP(NETIF_MSG_LINK,
				"Setting the SFX7101 LASI indication\n");

			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_PMA_DEVAD,
				       MDIO_PMA_REG_LASI_CTRL, 0x1);
			DP(NETIF_MSG_LINK,
			  "Setting the SFX7101 LED to blink on traffic\n");
			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_PMA_DEVAD,
				       MDIO_PMA_REG_7107_LED_CNTL, (1<<3));

			bnx2x_ext_phy_set_pause(params, vars);
			/* Restart autoneg */
			bnx2x_cl45_read(bp, params->port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_AN_DEVAD,
				      MDIO_AN_REG_CTRL, &val);
			val |= 0x200;
			bnx2x_cl45_write(bp, params->port,
				       ext_phy_type,
				       ext_phy_addr,
				       MDIO_AN_DEVAD,
				       MDIO_AN_REG_CTRL, val);
			break;
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE:
			DP(NETIF_MSG_LINK,
				 "XGXS PHY Failure detected 0x%x\n",
				 params->ext_phy_config);
			rc = -EINVAL;
			break;
		default:
			DP(NETIF_MSG_LINK, "BAD XGXS ext_phy_config 0x%x\n",
				  params->ext_phy_config);
			rc = -EINVAL;
			break;
		}

	} else { /* SerDes */

		ext_phy_type = SERDES_EXT_PHY_TYPE(params->ext_phy_config);
		switch (ext_phy_type) {
		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT:
			DP(NETIF_MSG_LINK, "SerDes Direct\n");
			break;

		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_BCM5482:
			DP(NETIF_MSG_LINK, "SerDes 5482\n");
			break;

		default:
			DP(NETIF_MSG_LINK, "BAD SerDes ext_phy_config 0x%x\n",
			   params->ext_phy_config);
			break;
		}
	}
	return rc;
}


static u8 bnx2x_ext_phy_is_link_up(struct link_params *params,
				 struct link_vars *vars)
{
	struct bnx2x *bp = params->bp;
	u32 ext_phy_type;
	u8 ext_phy_addr;
	u16 val1 = 0, val2;
	u16 rx_sd, pcs_status;
	u8 ext_phy_link_up = 0;
	u8 port = params->port;
	if (vars->phy_flags & PHY_XGXS_FLAG) {
		ext_phy_addr = ((params->ext_phy_config &
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);

		ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);
		switch (ext_phy_type) {
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT:
			DP(NETIF_MSG_LINK, "XGXS Direct\n");
			ext_phy_link_up = 1;
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705:
			DP(NETIF_MSG_LINK, "XGXS 8705\n");
			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_WIS_DEVAD,
				      MDIO_WIS_REG_LASI_STATUS, &val1);
			DP(NETIF_MSG_LINK, "8705 LASI status 0x%x\n", val1);

			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_WIS_DEVAD,
				      MDIO_WIS_REG_LASI_STATUS, &val1);
			DP(NETIF_MSG_LINK, "8705 LASI status 0x%x\n", val1);

			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      MDIO_PMA_REG_RX_SD, &rx_sd);
			DP(NETIF_MSG_LINK, "8705 rx_sd 0x%x\n", rx_sd);
			ext_phy_link_up = (rx_sd & 0x1);
			if (ext_phy_link_up)
				vars->line_speed = SPEED_10000;
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706:
			DP(NETIF_MSG_LINK, "XGXS 8706\n");
			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      MDIO_PMA_REG_LASI_STATUS, &val1);
			DP(NETIF_MSG_LINK, "8706 LASI status 0x%x\n", val1);

			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      MDIO_PMA_REG_LASI_STATUS, &val1);
			DP(NETIF_MSG_LINK, "8706 LASI status 0x%x\n", val1);

			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      MDIO_PMA_REG_RX_SD, &rx_sd);
			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_PCS_DEVAD,
				      MDIO_PCS_REG_STATUS, &pcs_status);

			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_AN_DEVAD,
				      MDIO_AN_REG_LINK_STATUS, &val2);
			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_AN_DEVAD,
				      MDIO_AN_REG_LINK_STATUS, &val2);

			DP(NETIF_MSG_LINK, "8706 rx_sd 0x%x"
			   "  pcs_status 0x%x 1Gbps link_status 0x%x\n",
			   rx_sd, pcs_status, val2);
			/* link is up if both bit 0 of pmd_rx_sd and
			 * bit 0 of pcs_status are set, or if the autoneg bit
			   1 is set
			 */
			ext_phy_link_up = ((rx_sd & pcs_status & 0x1) ||
					   (val2 & (1<<1)));
			if (ext_phy_link_up) {
				if (val2 & (1<<1))
					vars->line_speed = SPEED_1000;
				else
					vars->line_speed = SPEED_10000;
			}

			/* clear LASI indication*/
			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      MDIO_PMA_REG_RX_ALARM, &val2);
			break;

		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073:
		{
			u16 link_status = 0;
			u16 an1000_status = 0;
			if (ext_phy_type ==
			     PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072) {
				bnx2x_cl45_read(bp, params->port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_PCS_DEVAD,
				      MDIO_PCS_REG_LASI_STATUS, &val1);
			bnx2x_cl45_read(bp, params->port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_PCS_DEVAD,
				      MDIO_PCS_REG_LASI_STATUS, &val2);
			DP(NETIF_MSG_LINK,
				 "870x LASI status 0x%x->0x%x\n",
				  val1, val2);

			} else {
				/* In 8073, port1 is directed through emac0 and
				 * port0 is directed through emac1
				 */
				bnx2x_cl45_read(bp, params->port,
					      ext_phy_type,
					      ext_phy_addr,
					      MDIO_PMA_DEVAD,
					      MDIO_PMA_REG_LASI_STATUS, &val1);

				DP(NETIF_MSG_LINK,
					 "8703 LASI status 0x%x\n",
					  val1);
			}

			/* clear the interrupt LASI status register */
			bnx2x_cl45_read(bp, params->port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_PCS_DEVAD,
				      MDIO_PCS_REG_STATUS, &val2);
			bnx2x_cl45_read(bp, params->port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_PCS_DEVAD,
				      MDIO_PCS_REG_STATUS, &val1);
			DP(NETIF_MSG_LINK, "807x PCS status 0x%x->0x%x\n",
			   val2, val1);
			/* Clear MSG-OUT */
			bnx2x_cl45_read(bp, params->port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      0xca13,
				      &val1);

			/* Check the LASI */
			bnx2x_cl45_read(bp, params->port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      MDIO_PMA_REG_RX_ALARM, &val2);

			DP(NETIF_MSG_LINK, "KR 0x9003 0x%x\n", val2);

			/* Check the link status */
			bnx2x_cl45_read(bp, params->port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_PCS_DEVAD,
				      MDIO_PCS_REG_STATUS, &val2);
			DP(NETIF_MSG_LINK, "KR PCS status 0x%x\n", val2);

			bnx2x_cl45_read(bp, params->port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      MDIO_PMA_REG_STATUS, &val2);
			bnx2x_cl45_read(bp, params->port,
				      ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      MDIO_PMA_REG_STATUS, &val1);
			ext_phy_link_up = ((val1 & 4) == 4);
			DP(NETIF_MSG_LINK, "PMA_REG_STATUS=0x%x\n", val1);
			if (ext_phy_type ==
			    PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073) {

				if (ext_phy_link_up &&
				    ((params->req_line_speed !=
					SPEED_10000))) {
					if (bnx2x_bcm8073_xaui_wa(params)
					     != 0) {
						ext_phy_link_up = 0;
						break;
					}
				}
				bnx2x_cl45_read(bp, params->port,
						      ext_phy_type,
						      ext_phy_addr,
						      MDIO_AN_DEVAD,
						      0x8304,
						      &an1000_status);
				bnx2x_cl45_read(bp, params->port,
						      ext_phy_type,
						      ext_phy_addr,
						      MDIO_AN_DEVAD,
						      0x8304,
						      &an1000_status);

				/* Check the link status on 1.1.2 */
				bnx2x_cl45_read(bp, params->port,
					      ext_phy_type,
					      ext_phy_addr,
					      MDIO_PMA_DEVAD,
					      MDIO_PMA_REG_STATUS, &val2);
				bnx2x_cl45_read(bp, params->port,
					      ext_phy_type,
					      ext_phy_addr,
					      MDIO_PMA_DEVAD,
					      MDIO_PMA_REG_STATUS, &val1);
				DP(NETIF_MSG_LINK, "KR PMA status 0x%x->0x%x,"
					     "an_link_status=0x%x\n",
					  val2, val1, an1000_status);

					ext_phy_link_up = (((val1 & 4) == 4) ||
						(an1000_status & (1<<1)));
				if (ext_phy_link_up &&
				    bnx2x_8073_is_snr_needed(params)) {
					/* The SNR will improve about 2dbby
					changing the BW and FEE main tap.*/

					/* The 1st write to change FFE main
					tap is set before restart AN */
					/* Change PLL Bandwidth in EDC
					register */
					bnx2x_cl45_write(bp, port, ext_phy_type,
						    ext_phy_addr,
						    MDIO_PMA_DEVAD,
						    MDIO_PMA_REG_PLL_BANDWIDTH,
						    0x26BC);

					/* Change CDR Bandwidth in EDC
					register */
					bnx2x_cl45_write(bp, port, ext_phy_type,
						    ext_phy_addr,
						    MDIO_PMA_DEVAD,
						    MDIO_PMA_REG_CDR_BANDWIDTH,
						    0x0333);


				}
				bnx2x_cl45_read(bp, params->port,
						      ext_phy_type,
						      ext_phy_addr,
						      MDIO_PMA_DEVAD,
						      0xc820,
						      &link_status);

				/* Bits 0..2 --> speed detected,
				   bits 13..15--> link is down */
				if ((link_status & (1<<2)) &&
				    (!(link_status & (1<<15)))) {
					ext_phy_link_up = 1;
					vars->line_speed = SPEED_10000;
					DP(NETIF_MSG_LINK,
						 "port %x: External link"
						 " up in 10G\n", params->port);
				} else if ((link_status & (1<<1)) &&
					   (!(link_status & (1<<14)))) {
					ext_phy_link_up = 1;
					vars->line_speed = SPEED_2500;
					DP(NETIF_MSG_LINK,
						 "port %x: External link"
						 " up in 2.5G\n", params->port);
				} else if ((link_status & (1<<0)) &&
					   (!(link_status & (1<<13)))) {
					ext_phy_link_up = 1;
					vars->line_speed = SPEED_1000;
					DP(NETIF_MSG_LINK,
						 "port %x: External link"
						 " up in 1G\n", params->port);
				} else {
					ext_phy_link_up = 0;
					DP(NETIF_MSG_LINK,
						 "port %x: External link"
						 " is down\n", params->port);
				}
			} else {
				/* See if 1G link is up for the 8072 */
				bnx2x_cl45_read(bp, params->port,
						      ext_phy_type,
						      ext_phy_addr,
						      MDIO_AN_DEVAD,
						      0x8304,
						      &an1000_status);
				bnx2x_cl45_read(bp, params->port,
						      ext_phy_type,
						      ext_phy_addr,
						      MDIO_AN_DEVAD,
						      0x8304,
						      &an1000_status);
				if (an1000_status & (1<<1)) {
					ext_phy_link_up = 1;
					vars->line_speed = SPEED_1000;
					DP(NETIF_MSG_LINK,
						 "port %x: External link"
						 " up in 1G\n", params->port);
				} else if (ext_phy_link_up) {
					ext_phy_link_up = 1;
					vars->line_speed = SPEED_10000;
					DP(NETIF_MSG_LINK,
						 "port %x: External link"
						 " up in 10G\n", params->port);
				}
			}


			break;
		}
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      MDIO_PMA_REG_LASI_STATUS, &val2);
			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      MDIO_PMA_REG_LASI_STATUS, &val1);
			DP(NETIF_MSG_LINK,
				 "10G-base-T LASI status 0x%x->0x%x\n",
				  val2, val1);
			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      MDIO_PMA_REG_STATUS, &val2);
			bnx2x_cl45_read(bp, params->port, ext_phy_type,
				      ext_phy_addr,
				      MDIO_PMA_DEVAD,
				      MDIO_PMA_REG_STATUS, &val1);
			DP(NETIF_MSG_LINK,
				 "10G-base-T PMA status 0x%x->0x%x\n",
				 val2, val1);
			ext_phy_link_up = ((val1 & 4) == 4);
			/* if link is up
			 * print the AN outcome of the SFX7101 PHY
			 */
			if (ext_phy_link_up) {
				bnx2x_cl45_read(bp, params->port,
					      ext_phy_type,
					      ext_phy_addr,
					      MDIO_AN_DEVAD,
					      MDIO_AN_REG_MASTER_STATUS,
					      &val2);
				vars->line_speed = SPEED_10000;
				DP(NETIF_MSG_LINK,
					 "SFX7101 AN status 0x%x->Master=%x\n",
					  val2,
					 (val2 & (1<<14)));
			}
			break;

		default:
			DP(NETIF_MSG_LINK, "BAD XGXS ext_phy_config 0x%x\n",
			   params->ext_phy_config);
			ext_phy_link_up = 0;
			break;
		}

	} else { /* SerDes */
		ext_phy_type = SERDES_EXT_PHY_TYPE(params->ext_phy_config);
		switch (ext_phy_type) {
		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT:
			DP(NETIF_MSG_LINK, "SerDes Direct\n");
			ext_phy_link_up = 1;
			break;

		case PORT_HW_CFG_SERDES_EXT_PHY_TYPE_BCM5482:
			DP(NETIF_MSG_LINK, "SerDes 5482\n");
			ext_phy_link_up = 1;
			break;

		default:
			DP(NETIF_MSG_LINK,
				 "BAD SerDes ext_phy_config 0x%x\n",
				 params->ext_phy_config);
			ext_phy_link_up = 0;
			break;
		}
	}

	return ext_phy_link_up;
}

static void bnx2x_link_int_enable(struct link_params *params)
{
	u8 port = params->port;
	u32 ext_phy_type;
	u32 mask;
	struct bnx2x *bp = params->bp;
	/* setting the status to report on link up
	   for either XGXS or SerDes */

	if (params->switch_cfg == SWITCH_CFG_10G) {
		mask = (NIG_MASK_XGXS0_LINK10G |
			NIG_MASK_XGXS0_LINK_STATUS);
		DP(NETIF_MSG_LINK, "enabled XGXS interrupt\n");
		ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);
		if ((ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT) &&
		    (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE) &&
		    (ext_phy_type !=
				PORT_HW_CFG_XGXS_EXT_PHY_TYPE_NOT_CONN)) {
			mask |= NIG_MASK_MI_INT;
			DP(NETIF_MSG_LINK, "enabled external phy int\n");
		}

	} else { /* SerDes */
		mask = NIG_MASK_SERDES0_LINK_STATUS;
		DP(NETIF_MSG_LINK, "enabled SerDes interrupt\n");
		ext_phy_type = SERDES_EXT_PHY_TYPE(params->ext_phy_config);
		if ((ext_phy_type !=
				PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT) &&
		    (ext_phy_type !=
				PORT_HW_CFG_SERDES_EXT_PHY_TYPE_NOT_CONN)) {
			mask |= NIG_MASK_MI_INT;
			DP(NETIF_MSG_LINK, "enabled external phy int\n");
		}
	}
	bnx2x_bits_en(bp,
		      NIG_REG_MASK_INTERRUPT_PORT0 + port*4,
		      mask);
	DP(NETIF_MSG_LINK, "port %x, is_xgxs=%x, int_status 0x%x\n", port,
		 (params->switch_cfg == SWITCH_CFG_10G),
		 REG_RD(bp, NIG_REG_STATUS_INTERRUPT_PORT0 + port*4));

	DP(NETIF_MSG_LINK, " int_mask 0x%x, MI_INT %x, SERDES_LINK %x\n",
		 REG_RD(bp, NIG_REG_MASK_INTERRUPT_PORT0 + port*4),
		 REG_RD(bp, NIG_REG_EMAC0_STATUS_MISC_MI_INT + port*0x18),
		 REG_RD(bp, NIG_REG_SERDES0_STATUS_LINK_STATUS+port*0x3c));
	DP(NETIF_MSG_LINK, " 10G %x, XGXS_LINK %x\n",
	   REG_RD(bp, NIG_REG_XGXS0_STATUS_LINK10G + port*0x68),
	   REG_RD(bp, NIG_REG_XGXS0_STATUS_LINK_STATUS + port*0x68));
}


/*
 * link management
 */
static void bnx2x_link_int_ack(struct link_params *params,
			     struct link_vars *vars, u8 is_10g)
{
	struct bnx2x *bp = params->bp;
	u8 port = params->port;

	/* first reset all status
	 * we assume only one line will be change at a time */
	bnx2x_bits_dis(bp, NIG_REG_STATUS_INTERRUPT_PORT0 + port*4,
		     (NIG_STATUS_XGXS0_LINK10G |
		      NIG_STATUS_XGXS0_LINK_STATUS |
		      NIG_STATUS_SERDES0_LINK_STATUS));
	if (vars->phy_link_up) {
		if (is_10g) {
			/* Disable the 10G link interrupt
			 * by writing 1 to the status register
			 */
			DP(NETIF_MSG_LINK, "10G XGXS phy link up\n");
			bnx2x_bits_en(bp,
				      NIG_REG_STATUS_INTERRUPT_PORT0 + port*4,
				      NIG_STATUS_XGXS0_LINK10G);

		} else if (params->switch_cfg == SWITCH_CFG_10G) {
			/* Disable the link interrupt
			 * by writing 1 to the relevant lane
			 * in the status register
			 */
			u32 ser_lane = ((params->lane_config &
				    PORT_HW_CFG_LANE_SWAP_CFG_MASTER_MASK) >>
				    PORT_HW_CFG_LANE_SWAP_CFG_MASTER_SHIFT);

			DP(NETIF_MSG_LINK, "1G XGXS phy link up\n");
			bnx2x_bits_en(bp,
				      NIG_REG_STATUS_INTERRUPT_PORT0 + port*4,
				      ((1 << ser_lane) <<
				       NIG_STATUS_XGXS0_LINK_STATUS_SIZE));

		} else { /* SerDes */
			DP(NETIF_MSG_LINK, "SerDes phy link up\n");
			/* Disable the link interrupt
			 * by writing 1 to the status register
			 */
			bnx2x_bits_en(bp,
				      NIG_REG_STATUS_INTERRUPT_PORT0 + port*4,
				      NIG_STATUS_SERDES0_LINK_STATUS);
		}

	} else { /* link_down */
	}
}

static u8 bnx2x_format_ver(u32 num, u8 *str, u16 len)
{
	u8 *str_ptr = str;
	u32 mask = 0xf0000000;
	u8 shift = 8*4;
	u8 digit;
	if (len < 10) {
		/* Need more than 10chars for this format */
		*str_ptr = '\0';
		return -EINVAL;
	}
	while (shift > 0) {

		shift -= 4;
		digit = ((num & mask) >> shift);
		if (digit < 0xa)
			*str_ptr = digit + '0';
		else
			*str_ptr = digit - 0xa + 'a';
		str_ptr++;
		mask = mask >> 4;
		if (shift == 4*4) {
			*str_ptr = ':';
			str_ptr++;
		}
	}
	*str_ptr = '\0';
	return 0;
}


static void bnx2x_turn_on_ef(struct bnx2x *bp, u8 port, u8 ext_phy_addr,
			   u32 ext_phy_type)
{
	u32 cnt = 0;
	u16 ctrl = 0;
	/* Enable EMAC0 in to enable MDIO */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_SET,
	       (MISC_REGISTERS_RESET_REG_2_RST_EMAC0_HARD_CORE << port));
	msleep(5);

	/* take ext phy out of reset */
	bnx2x_set_gpio(bp,
			  MISC_REGISTERS_GPIO_2,
			  MISC_REGISTERS_GPIO_HIGH,
			  port);

	bnx2x_set_gpio(bp,
			  MISC_REGISTERS_GPIO_1,
			  MISC_REGISTERS_GPIO_HIGH,
			  port);

	/* wait for 5ms */
	msleep(5);

	for (cnt = 0; cnt < 1000; cnt++) {
		msleep(1);
		bnx2x_cl45_read(bp, port,
			      ext_phy_type,
			      ext_phy_addr,
			      MDIO_PMA_DEVAD,
			      MDIO_PMA_REG_CTRL,
			       &ctrl);
		if (!(ctrl & (1<<15))) {
			DP(NETIF_MSG_LINK, "Reset completed\n\n");
				break;
		}
	}
}

static void bnx2x_turn_off_sf(struct bnx2x *bp, u8 port)
{
	/* put sf to reset */
	bnx2x_set_gpio(bp,
			  MISC_REGISTERS_GPIO_1,
			  MISC_REGISTERS_GPIO_LOW,
			  port);
	bnx2x_set_gpio(bp,
			  MISC_REGISTERS_GPIO_2,
			  MISC_REGISTERS_GPIO_LOW,
			  port);
}

u8 bnx2x_get_ext_phy_fw_version(struct link_params *params, u8 driver_loaded,
			      u8 *version, u16 len)
{
	struct bnx2x *bp = params->bp;
	u32 ext_phy_type = 0;
	u16 val = 0;
	u8 ext_phy_addr = 0 ;
	u8 status = 0 ;
	u32 ver_num;

	if (version == NULL || params == NULL)
		return -EINVAL;

	/* reset the returned value to zero */
	ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);
	ext_phy_addr = ((params->ext_phy_config &
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
				PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);

	switch (ext_phy_type) {
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:

		if (len < 5)
			return -EINVAL;

		/* Take ext phy out of reset */
		if (!driver_loaded)
			bnx2x_turn_on_ef(bp, params->port, ext_phy_addr,
				       ext_phy_type);

		/*  wait for 1ms */
		msleep(1);

		bnx2x_cl45_read(bp, params->port,
			      ext_phy_type,
			      ext_phy_addr,
			      MDIO_PMA_DEVAD,
			      MDIO_PMA_REG_7101_VER1, &val);
		version[2] = (val & 0xFF);
		version[3] = ((val & 0xFF00)>>8);

		bnx2x_cl45_read(bp, params->port,
			      ext_phy_type,
			      ext_phy_addr,
			      MDIO_PMA_DEVAD, MDIO_PMA_REG_7101_VER2,
			      &val);
		version[0] = (val & 0xFF);
		version[1] = ((val & 0xFF00)>>8);
		version[4] = '\0';

		if (!driver_loaded)
			bnx2x_turn_off_sf(bp, params->port);
		break;
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073:
	{
		/* Take ext phy out of reset */
		if (!driver_loaded)
			bnx2x_turn_on_ef(bp, params->port, ext_phy_addr,
				       ext_phy_type);

		bnx2x_cl45_read(bp, params->port, ext_phy_type,
			      ext_phy_addr,
			      MDIO_PMA_DEVAD,
			      MDIO_PMA_REG_ROM_VER1, &val);
		ver_num = val<<16;
		bnx2x_cl45_read(bp, params->port, ext_phy_type,
			      ext_phy_addr,
			      MDIO_PMA_DEVAD,
			      MDIO_PMA_REG_ROM_VER2, &val);
		ver_num |= val;
		status = bnx2x_format_ver(ver_num, version, len);
		break;
	}
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705:
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706:

		bnx2x_cl45_read(bp, params->port, ext_phy_type,
			      ext_phy_addr,
			      MDIO_PMA_DEVAD,
			      MDIO_PMA_REG_ROM_VER1, &val);
		ver_num = val<<16;
		bnx2x_cl45_read(bp, params->port, ext_phy_type,
			      ext_phy_addr,
			      MDIO_PMA_DEVAD,
			      MDIO_PMA_REG_ROM_VER2, &val);
		ver_num |= val;
		status = bnx2x_format_ver(ver_num, version, len);
		break;

	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT:
		break;

	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE:
		DP(NETIF_MSG_LINK, "bnx2x_get_ext_phy_fw_version:"
				    " type is FAILURE!\n");
		status = -EINVAL;
		break;

	default:
		break;
	}
	return status;
}

static void bnx2x_set_xgxs_loopback(struct link_params *params,
				  struct link_vars *vars,
				  u8 is_10g)
{
	u8 port = params->port;
	struct bnx2x *bp = params->bp;

	if (is_10g) {
		u32 md_devad;

		DP(NETIF_MSG_LINK, "XGXS 10G loopback enable\n");

		/* change the uni_phy_addr in the nig */
		md_devad = REG_RD(bp, (NIG_REG_XGXS0_CTRL_MD_DEVAD +
					  port*0x18));

		REG_WR(bp, NIG_REG_XGXS0_CTRL_MD_DEVAD + port*0x18, 0x5);

		bnx2x_cl45_write(bp, port, 0,
			       params->phy_addr,
			       5,
			       (MDIO_REG_BANK_AER_BLOCK +
				(MDIO_AER_BLOCK_AER_REG & 0xf)),
			       0x2800);

		bnx2x_cl45_write(bp, port, 0,
			       params->phy_addr,
			       5,
			       (MDIO_REG_BANK_CL73_IEEEB0 +
				(MDIO_CL73_IEEEB0_CL73_AN_CONTROL & 0xf)),
			       0x6041);
		msleep(200);
		/* set aer mmd back */
		bnx2x_set_aer_mmd(params, vars);

		/* and md_devad */
		REG_WR(bp, NIG_REG_XGXS0_CTRL_MD_DEVAD + port*0x18,
			    md_devad);

	} else {
		u16 mii_control;

		DP(NETIF_MSG_LINK, "XGXS 1G loopback enable\n");

		CL45_RD_OVER_CL22(bp, port,
				      params->phy_addr,
				      MDIO_REG_BANK_COMBO_IEEE0,
				      MDIO_COMBO_IEEE0_MII_CONTROL,
				      &mii_control);

		CL45_WR_OVER_CL22(bp, port,
				      params->phy_addr,
				      MDIO_REG_BANK_COMBO_IEEE0,
				      MDIO_COMBO_IEEE0_MII_CONTROL,
				      (mii_control |
				       MDIO_COMBO_IEEO_MII_CONTROL_LOOPBACK));
	}
}


static void bnx2x_ext_phy_loopback(struct link_params *params)
{
	struct bnx2x *bp = params->bp;
	u8 ext_phy_addr;
	u32 ext_phy_type;

	if (params->switch_cfg == SWITCH_CFG_10G) {
		ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);
		/* CL37 Autoneg Enabled */
		ext_phy_addr = ((params->ext_phy_config &
					PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
					PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);
		switch (ext_phy_type) {
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT:
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_NOT_CONN:
			DP(NETIF_MSG_LINK,
				"ext_phy_loopback: We should not get here\n");
			break;
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705:
			DP(NETIF_MSG_LINK, "ext_phy_loopback: 8705\n");
			break;
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706:
			DP(NETIF_MSG_LINK, "ext_phy_loopback: 8706\n");
			break;
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
			/* SFX7101_XGXS_TEST1 */
			bnx2x_cl45_write(bp, params->port, ext_phy_type,
				       ext_phy_addr,
				       MDIO_XS_DEVAD,
				       MDIO_XS_SFX7101_XGXS_TEST1,
				       0x100);
			DP(NETIF_MSG_LINK,
				"ext_phy_loopback: set ext phy loopback\n");
			break;
		case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:

			break;
		} /* switch external PHY type */
	} else {
		/* serdes */
		ext_phy_type = SERDES_EXT_PHY_TYPE(params->ext_phy_config);
		ext_phy_addr = (params->ext_phy_config  &
		PORT_HW_CFG_SERDES_EXT_PHY_ADDR_MASK)
		>> PORT_HW_CFG_SERDES_EXT_PHY_ADDR_SHIFT;
	}
}


/*
 *------------------------------------------------------------------------
 * bnx2x_override_led_value -
 *
 * Override the led value of the requsted led
 *
 *------------------------------------------------------------------------
 */
u8 bnx2x_override_led_value(struct bnx2x *bp, u8 port,
			  u32 led_idx, u32 value)
{
	u32 reg_val;

	/* If port 0 then use EMAC0, else use EMAC1*/
	u32 emac_base = (port) ? GRCBASE_EMAC1 : GRCBASE_EMAC0;

	DP(NETIF_MSG_LINK,
		 "bnx2x_override_led_value() port %x led_idx %d value %d\n",
		 port, led_idx, value);

	switch (led_idx) {
	case 0: /* 10MB led */
		/* Read the current value of the LED register in
		the EMAC block */
		reg_val = REG_RD(bp, emac_base + EMAC_REG_EMAC_LED);
		/* Set the OVERRIDE bit to 1 */
		reg_val |= EMAC_LED_OVERRIDE;
		/* If value is 1, set the 10M_OVERRIDE bit,
		otherwise reset it.*/
		reg_val = (value == 1) ? (reg_val | EMAC_LED_10MB_OVERRIDE) :
			(reg_val & ~EMAC_LED_10MB_OVERRIDE);
		REG_WR(bp, emac_base + EMAC_REG_EMAC_LED, reg_val);
		break;
	case 1: /*100MB led    */
		/*Read the current value of the LED register in
		the EMAC block */
		reg_val = REG_RD(bp, emac_base + EMAC_REG_EMAC_LED);
		/*  Set the OVERRIDE bit to 1 */
		reg_val |= EMAC_LED_OVERRIDE;
		/*  If value is 1, set the 100M_OVERRIDE bit,
		otherwise reset it.*/
		reg_val = (value == 1) ? (reg_val | EMAC_LED_100MB_OVERRIDE) :
			(reg_val & ~EMAC_LED_100MB_OVERRIDE);
		REG_WR(bp, emac_base + EMAC_REG_EMAC_LED, reg_val);
		break;
	case 2: /* 1000MB led */
		/* Read the current value of the LED register in the
		EMAC block */
		reg_val = REG_RD(bp, emac_base + EMAC_REG_EMAC_LED);
		/* Set the OVERRIDE bit to 1 */
		reg_val |= EMAC_LED_OVERRIDE;
		/* If value is 1, set the 1000M_OVERRIDE bit, otherwise
		reset it. */
		reg_val = (value == 1) ? (reg_val | EMAC_LED_1000MB_OVERRIDE) :
			(reg_val & ~EMAC_LED_1000MB_OVERRIDE);
		REG_WR(bp, emac_base + EMAC_REG_EMAC_LED, reg_val);
		break;
	case 3: /* 2500MB led */
		/*  Read the current value of the LED register in the
		EMAC block*/
		reg_val = REG_RD(bp, emac_base + EMAC_REG_EMAC_LED);
		/* Set the OVERRIDE bit to 1 */
		reg_val |= EMAC_LED_OVERRIDE;
		/*  If value is 1, set the 2500M_OVERRIDE bit, otherwise
		reset it.*/
		reg_val = (value == 1) ? (reg_val | EMAC_LED_2500MB_OVERRIDE) :
			(reg_val & ~EMAC_LED_2500MB_OVERRIDE);
		REG_WR(bp, emac_base + EMAC_REG_EMAC_LED, reg_val);
		break;
	case 4: /*10G led */
		if (port == 0) {
			REG_WR(bp, NIG_REG_LED_10G_P0,
				    value);
		} else {
			REG_WR(bp, NIG_REG_LED_10G_P1,
				    value);
		}
		break;
	case 5: /* TRAFFIC led */
		/* Find if the traffic control is via BMAC or EMAC */
		if (port == 0)
			reg_val = REG_RD(bp, NIG_REG_NIG_EMAC0_EN);
		else
			reg_val = REG_RD(bp, NIG_REG_NIG_EMAC1_EN);

		/*  Override the traffic led in the EMAC:*/
		if (reg_val == 1) {
			/* Read the current value of the LED register in
			the EMAC block */
			reg_val = REG_RD(bp, emac_base +
					     EMAC_REG_EMAC_LED);
			/* Set the TRAFFIC_OVERRIDE bit to 1 */
			reg_val |= EMAC_LED_OVERRIDE;
			/* If value is 1, set the TRAFFIC bit, otherwise
			reset it.*/
			reg_val = (value == 1) ? (reg_val | EMAC_LED_TRAFFIC) :
				(reg_val & ~EMAC_LED_TRAFFIC);
			REG_WR(bp, emac_base + EMAC_REG_EMAC_LED, reg_val);
		} else { /* Override the traffic led in the BMAC: */
			REG_WR(bp, NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P0
				   + port*4, 1);
			REG_WR(bp, NIG_REG_LED_CONTROL_TRAFFIC_P0 + port*4,
				    value);
		}
		break;
	default:
		DP(NETIF_MSG_LINK,
			 "bnx2x_override_led_value() unknown led index %d "
			 "(should be 0-5)\n", led_idx);
		return -EINVAL;
	}

	return 0;
}


u8 bnx2x_set_led(struct bnx2x *bp, u8 port, u8 mode, u32 speed,
	       u16 hw_led_mode, u32 chip_id)
{
	u8 rc = 0;
	u32 tmp;
	u32 emac_base = port ? GRCBASE_EMAC1 : GRCBASE_EMAC0;
	DP(NETIF_MSG_LINK, "bnx2x_set_led: port %x, mode %d\n", port, mode);
	DP(NETIF_MSG_LINK, "speed 0x%x, hw_led_mode 0x%x\n",
		 speed, hw_led_mode);
	switch (mode) {
	case LED_MODE_OFF:
		REG_WR(bp, NIG_REG_LED_10G_P0 + port*4, 0);
		REG_WR(bp, NIG_REG_LED_MODE_P0 + port*4,
			   SHARED_HW_CFG_LED_MAC1);

		tmp = EMAC_RD(bp, EMAC_REG_EMAC_LED);
		EMAC_WR(bp, EMAC_REG_EMAC_LED, (tmp | EMAC_LED_OVERRIDE));
		break;

	case LED_MODE_OPER:
		REG_WR(bp, NIG_REG_LED_MODE_P0 + port*4, hw_led_mode);
		REG_WR(bp, NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P0 +
			   port*4, 0);
		/* Set blinking rate to ~15.9Hz */
		REG_WR(bp, NIG_REG_LED_CONTROL_BLINK_RATE_P0 + port*4,
			   LED_BLINK_RATE_VAL);
		REG_WR(bp, NIG_REG_LED_CONTROL_BLINK_RATE_ENA_P0 +
			   port*4, 1);
		tmp = EMAC_RD(bp, EMAC_REG_EMAC_LED);
		EMAC_WR(bp, EMAC_REG_EMAC_LED,
			    (tmp & (~EMAC_LED_OVERRIDE)));

		if (!CHIP_IS_E1H(bp) &&
		    ((speed == SPEED_2500) ||
		     (speed == SPEED_1000) ||
		     (speed == SPEED_100) ||
		     (speed == SPEED_10))) {
			/* On Everest 1 Ax chip versions for speeds less than
			10G LED scheme is different */
			REG_WR(bp, NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P0
				   + port*4, 1);
			REG_WR(bp, NIG_REG_LED_CONTROL_TRAFFIC_P0 +
				   port*4, 0);
			REG_WR(bp, NIG_REG_LED_CONTROL_BLINK_TRAFFIC_P0 +
				   port*4, 1);
		}
		break;

	default:
		rc = -EINVAL;
		DP(NETIF_MSG_LINK, "bnx2x_set_led: Invalid led mode %d\n",
			 mode);
		break;
	}
	return rc;

}

u8 bnx2x_test_link(struct link_params *params, struct link_vars *vars)
{
	struct bnx2x *bp = params->bp;
	u16 gp_status = 0;

	CL45_RD_OVER_CL22(bp, params->port,
			      params->phy_addr,
			      MDIO_REG_BANK_GP_STATUS,
			      MDIO_GP_STATUS_TOP_AN_STATUS1,
			      &gp_status);
	/* link is up only if both local phy and external phy are up */
	if ((gp_status & MDIO_GP_STATUS_TOP_AN_STATUS1_LINK_STATUS) &&
	    bnx2x_ext_phy_is_link_up(params, vars))
		return 0;

	return -ESRCH;
}

static u8 bnx2x_link_initialize(struct link_params *params,
			      struct link_vars *vars)
{
	struct bnx2x *bp = params->bp;
	u8 port = params->port;
	u8 rc = 0;
	u8 non_ext_phy;
	u32 ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);
	/* Activate the external PHY */
	bnx2x_ext_phy_reset(params, vars);

	bnx2x_set_aer_mmd(params, vars);

	if (vars->phy_flags & PHY_XGXS_FLAG)
		bnx2x_set_master_ln(params);

	rc = bnx2x_reset_unicore(params);
	/* reset the SerDes and wait for reset bit return low */
	if (rc != 0)
		return rc;

	bnx2x_set_aer_mmd(params, vars);

	/* setting the masterLn_def again after the reset */
	if (vars->phy_flags & PHY_XGXS_FLAG) {
		bnx2x_set_master_ln(params);
		bnx2x_set_swap_lanes(params);
	}

	if (vars->phy_flags & PHY_XGXS_FLAG) {
		if ((params->req_line_speed &&
		    ((params->req_line_speed == SPEED_100) ||
		     (params->req_line_speed == SPEED_10))) ||
		    (!params->req_line_speed &&
		     (params->speed_cap_mask >=
		       PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_FULL) &&
		     (params->speed_cap_mask <
		       PORT_HW_CFG_SPEED_CAPABILITY_D0_1G)
		     ))  {
			vars->phy_flags |= PHY_SGMII_FLAG;
		} else {
			vars->phy_flags &= ~PHY_SGMII_FLAG;
		}
	}
	/* In case of external phy existance, the line speed would be the
	 line speed linked up by the external phy. In case it is direct only,
	  then the line_speed during initialization will be equal to the
	   req_line_speed*/
	vars->line_speed = params->req_line_speed;

	bnx2x_calc_ieee_aneg_adv(params, &vars->ieee_fc);

	/* init ext phy and enable link state int */
	non_ext_phy = ((ext_phy_type == PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT) ||
		       (params->loopback_mode == LOOPBACK_XGXS_10) ||
		       (params->loopback_mode == LOOPBACK_EXT_PHY));

	if (non_ext_phy ||
	    (ext_phy_type == PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705)) {
		if (params->req_line_speed == SPEED_AUTO_NEG)
			bnx2x_set_parallel_detection(params, vars->phy_flags);
		bnx2x_init_internal_phy(params, vars);
	}

	if (!non_ext_phy)
		rc |= bnx2x_ext_phy_init(params, vars);

	bnx2x_bits_dis(bp, NIG_REG_STATUS_INTERRUPT_PORT0 + port*4,
		     (NIG_STATUS_XGXS0_LINK10G |
		      NIG_STATUS_XGXS0_LINK_STATUS |
		      NIG_STATUS_SERDES0_LINK_STATUS));

	return rc;

}


u8 bnx2x_phy_init(struct link_params *params, struct link_vars *vars)
{
	struct bnx2x *bp = params->bp;

	u32 val;
	DP(NETIF_MSG_LINK, "Phy Initialization started \n");
	DP(NETIF_MSG_LINK, "req_speed = %d, req_flowctrl=%d\n",
		  params->req_line_speed, params->req_flow_ctrl);
	vars->link_status = 0;
	vars->phy_link_up = 0;
	vars->link_up = 0;
	vars->line_speed = 0;
	vars->duplex = DUPLEX_FULL;
	vars->flow_ctrl = BNX2X_FLOW_CTRL_NONE;
	vars->mac_type = MAC_TYPE_NONE;

	if (params->switch_cfg ==  SWITCH_CFG_1G)
		vars->phy_flags = PHY_SERDES_FLAG;
	else
		vars->phy_flags = PHY_XGXS_FLAG;


	/* disable attentions */
	bnx2x_bits_dis(bp, NIG_REG_MASK_INTERRUPT_PORT0 + params->port*4,
		       (NIG_MASK_XGXS0_LINK_STATUS |
			NIG_MASK_XGXS0_LINK10G |
			NIG_MASK_SERDES0_LINK_STATUS |
			NIG_MASK_MI_INT));

	bnx2x_emac_init(params, vars);

	if (CHIP_REV_IS_FPGA(bp)) {
		vars->link_up = 1;
		vars->line_speed = SPEED_10000;
		vars->duplex = DUPLEX_FULL;
		vars->flow_ctrl = BNX2X_FLOW_CTRL_NONE;
		vars->link_status = (LINK_STATUS_LINK_UP | LINK_10GTFD);
		/* enable on E1.5 FPGA */
		if (CHIP_IS_E1H(bp)) {
			vars->flow_ctrl |=
				(BNX2X_FLOW_CTRL_TX | BNX2X_FLOW_CTRL_RX);
			vars->link_status |=
					(LINK_STATUS_TX_FLOW_CONTROL_ENABLED |
					 LINK_STATUS_RX_FLOW_CONTROL_ENABLED);
		}

		bnx2x_emac_enable(params, vars, 0);
		bnx2x_pbf_update(params, vars->flow_ctrl, vars->line_speed);
		/* disable drain */
		REG_WR(bp, NIG_REG_EGRESS_DRAIN0_MODE
				    + params->port*4, 0);

		/* update shared memory */
		bnx2x_update_mng(params, vars->link_status);

		return 0;

	} else
	if (CHIP_REV_IS_EMUL(bp)) {

		vars->link_up = 1;
		vars->line_speed = SPEED_10000;
		vars->duplex = DUPLEX_FULL;
		vars->flow_ctrl = BNX2X_FLOW_CTRL_NONE;
		vars->link_status = (LINK_STATUS_LINK_UP | LINK_10GTFD);

		bnx2x_bmac_enable(params, vars, 0);

		bnx2x_pbf_update(params, vars->flow_ctrl, vars->line_speed);
		/* Disable drain */
		REG_WR(bp, NIG_REG_EGRESS_DRAIN0_MODE
				    + params->port*4, 0);

		/* update shared memory */
		bnx2x_update_mng(params, vars->link_status);

		return 0;

	} else
	if (params->loopback_mode == LOOPBACK_BMAC) {
		vars->link_up = 1;
		vars->line_speed = SPEED_10000;
		vars->duplex = DUPLEX_FULL;
		vars->flow_ctrl = BNX2X_FLOW_CTRL_NONE;
		vars->mac_type = MAC_TYPE_BMAC;

		vars->phy_flags = PHY_XGXS_FLAG;

		bnx2x_phy_deassert(params, vars->phy_flags);
		/* set bmac loopback */
		bnx2x_bmac_enable(params, vars, 1);

		REG_WR(bp, NIG_REG_EGRESS_DRAIN0_MODE +
		    params->port*4, 0);
	} else if (params->loopback_mode == LOOPBACK_EMAC) {
		vars->link_up = 1;
		vars->line_speed = SPEED_1000;
		vars->duplex = DUPLEX_FULL;
		vars->flow_ctrl = BNX2X_FLOW_CTRL_NONE;
		vars->mac_type = MAC_TYPE_EMAC;

		vars->phy_flags = PHY_XGXS_FLAG;

		bnx2x_phy_deassert(params, vars->phy_flags);
		/* set bmac loopback */
		bnx2x_emac_enable(params, vars, 1);
		bnx2x_emac_program(params, vars->line_speed,
					      vars->duplex);
		REG_WR(bp, NIG_REG_EGRESS_DRAIN0_MODE +
		    params->port*4, 0);
	} else if ((params->loopback_mode == LOOPBACK_XGXS_10) ||
		  (params->loopback_mode == LOOPBACK_EXT_PHY)) {
		vars->link_up = 1;
		vars->line_speed = SPEED_10000;
		vars->duplex = DUPLEX_FULL;
		vars->flow_ctrl = BNX2X_FLOW_CTRL_NONE;

		vars->phy_flags = PHY_XGXS_FLAG;

		val = REG_RD(bp,
				 NIG_REG_XGXS0_CTRL_PHY_ADDR+
				 params->port*0x18);
		params->phy_addr = (u8)val;

		bnx2x_phy_deassert(params, vars->phy_flags);
		bnx2x_link_initialize(params, vars);

		vars->mac_type = MAC_TYPE_BMAC;

		bnx2x_bmac_enable(params, vars, 0);

		if (params->loopback_mode == LOOPBACK_XGXS_10) {
			/* set 10G XGXS loopback */
			bnx2x_set_xgxs_loopback(params, vars, 1);
		} else {
			/* set external phy loopback */
			bnx2x_ext_phy_loopback(params);
		}
		REG_WR(bp, NIG_REG_EGRESS_DRAIN0_MODE +
			    params->port*4, 0);
	} else
	/* No loopback */
	{

		bnx2x_phy_deassert(params, vars->phy_flags);
		switch (params->switch_cfg) {
		case SWITCH_CFG_1G:
			vars->phy_flags |= PHY_SERDES_FLAG;
			if ((params->ext_phy_config &
			     PORT_HW_CFG_SERDES_EXT_PHY_TYPE_MASK) ==
			     PORT_HW_CFG_SERDES_EXT_PHY_TYPE_BCM5482) {
				vars->phy_flags |=
					PHY_SGMII_FLAG;
			}

			val = REG_RD(bp,
					 NIG_REG_SERDES0_CTRL_PHY_ADDR+
					 params->port*0x10);

			params->phy_addr = (u8)val;

			break;
		case SWITCH_CFG_10G:
			vars->phy_flags |= PHY_XGXS_FLAG;
			val = REG_RD(bp,
				 NIG_REG_XGXS0_CTRL_PHY_ADDR+
				 params->port*0x18);
			params->phy_addr = (u8)val;

			break;
		default:
			DP(NETIF_MSG_LINK, "Invalid switch_cfg\n");
			return -EINVAL;
			break;
		}

		bnx2x_link_initialize(params, vars);
		msleep(30);
		bnx2x_link_int_enable(params);
	}
	return 0;
}

u8 bnx2x_link_reset(struct link_params *params, struct link_vars *vars)
{

	struct bnx2x *bp = params->bp;
	u32 ext_phy_config = params->ext_phy_config;
	u16 hw_led_mode = params->hw_led_mode;
	u32 chip_id = params->chip_id;
	u8 port = params->port;
	u32 ext_phy_type = XGXS_EXT_PHY_TYPE(ext_phy_config);
	/* disable attentions */

	vars->link_status = 0;
	bnx2x_update_mng(params, vars->link_status);
	bnx2x_bits_dis(bp, NIG_REG_MASK_INTERRUPT_PORT0 + port*4,
		     (NIG_MASK_XGXS0_LINK_STATUS |
		      NIG_MASK_XGXS0_LINK10G |
		      NIG_MASK_SERDES0_LINK_STATUS |
		      NIG_MASK_MI_INT));

	/* activate nig drain */
	REG_WR(bp, NIG_REG_EGRESS_DRAIN0_MODE + port*4, 1);

	/* disable nig egress interface */
	REG_WR(bp, NIG_REG_BMAC0_OUT_EN + port*4, 0);
	REG_WR(bp, NIG_REG_EGRESS_EMAC0_OUT_EN + port*4, 0);

	/* Stop BigMac rx */
	bnx2x_bmac_rx_disable(bp, port);

	/* disable emac */
	REG_WR(bp, NIG_REG_NIG_EMAC0_EN + port*4, 0);

	msleep(10);
	/* The PHY reset is controled by GPIO 1
	 * Hold it as vars low
	 */
	 /* clear link led */
	bnx2x_set_led(bp, port, LED_MODE_OFF, 0, hw_led_mode, chip_id);
	if (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT) {
		if ((ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072) &&
		    (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073)) {
			/* HW reset */

			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_1,
					  MISC_REGISTERS_GPIO_OUTPUT_LOW,
					  port);

			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_2,
					  MISC_REGISTERS_GPIO_OUTPUT_LOW,
					  port);

			DP(NETIF_MSG_LINK, "reset external PHY\n");
		} else if (ext_phy_type ==
			   PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073) {
				DP(NETIF_MSG_LINK, "Setting 8073 port %d into "
					 "low power mode\n",
					 port);
				bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_2,
					MISC_REGISTERS_GPIO_OUTPUT_LOW,
						  port);
		}
	}
	/* reset the SerDes/XGXS */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_3_CLEAR,
	       (0x1ff << (port*16)));

	/* reset BigMac */
	REG_WR(bp, GRCBASE_MISC + MISC_REGISTERS_RESET_REG_2_CLEAR,
	       (MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port));

	/* disable nig ingress interface */
	REG_WR(bp, NIG_REG_BMAC0_IN_EN + port*4, 0);
	REG_WR(bp, NIG_REG_EMAC0_IN_EN + port*4, 0);
	REG_WR(bp, NIG_REG_BMAC0_OUT_EN + port*4, 0);
	REG_WR(bp, NIG_REG_EGRESS_EMAC0_OUT_EN + port*4, 0);
	vars->link_up = 0;
	return 0;
}

static u8 bnx2x_update_link_down(struct link_params *params,
			       struct link_vars *vars)
{
	struct bnx2x *bp = params->bp;
	u8 port = params->port;
	DP(NETIF_MSG_LINK, "Port %x: Link is down\n", port);
	bnx2x_set_led(bp, port, LED_MODE_OFF,
		    0, params->hw_led_mode,
		    params->chip_id);

	/* indicate no mac active */
	vars->mac_type = MAC_TYPE_NONE;

	/* update shared memory */
	vars->link_status = 0;
	vars->line_speed = 0;
	bnx2x_update_mng(params, vars->link_status);

	/* activate nig drain */
	REG_WR(bp, NIG_REG_EGRESS_DRAIN0_MODE + port*4, 1);

	/* disable emac */
	REG_WR(bp, NIG_REG_NIG_EMAC0_EN + port*4, 0);

	msleep(10);

	/* reset BigMac */
	bnx2x_bmac_rx_disable(bp, params->port);
	REG_WR(bp, GRCBASE_MISC +
		   MISC_REGISTERS_RESET_REG_2_CLEAR,
		   (MISC_REGISTERS_RESET_REG_2_RST_BMAC0 << port));
	return 0;
}

static u8 bnx2x_update_link_up(struct link_params *params,
			     struct link_vars *vars,
			     u8 link_10g, u32 gp_status)
{
	struct bnx2x *bp = params->bp;
	u8 port = params->port;
	u8 rc = 0;
	vars->link_status |= LINK_STATUS_LINK_UP;
	if (link_10g) {
		bnx2x_bmac_enable(params, vars, 0);
		bnx2x_set_led(bp, port, LED_MODE_OPER,
			    SPEED_10000, params->hw_led_mode,
			    params->chip_id);

	} else {
		bnx2x_emac_enable(params, vars, 0);
		rc = bnx2x_emac_program(params, vars->line_speed,
				      vars->duplex);

		/* AN complete? */
		if (gp_status & MDIO_AN_CL73_OR_37_COMPLETE) {
			if (!(vars->phy_flags &
			      PHY_SGMII_FLAG))
				bnx2x_set_sgmii_tx_driver(params);
		}
	}

	/* PBF - link up */
	rc |= bnx2x_pbf_update(params, vars->flow_ctrl,
			      vars->line_speed);

	/* disable drain */
	REG_WR(bp, NIG_REG_EGRESS_DRAIN0_MODE + port*4, 0);

	/* update shared memory */
	bnx2x_update_mng(params, vars->link_status);
	msleep(20);
	return rc;
}
/* This function should called upon link interrupt */
/* In case vars->link_up, driver needs to
	1. Update the pbf
	2. Disable drain
	3. Update the shared memory
	4. Indicate link up
	5. Set LEDs
   Otherwise,
	1. Update shared memory
	2. Reset BigMac
	3. Report link down
	4. Unset LEDs
*/
u8 bnx2x_link_update(struct link_params *params, struct link_vars *vars)
{
	struct bnx2x *bp = params->bp;
	u8 port = params->port;
	u16 gp_status;
	u8 link_10g;
	u8 ext_phy_link_up, rc = 0;
	u32 ext_phy_type;

	DP(NETIF_MSG_LINK, "port %x, XGXS?%x, int_status 0x%x\n",
	 port,
	(vars->phy_flags & PHY_XGXS_FLAG),
	 REG_RD(bp, NIG_REG_STATUS_INTERRUPT_PORT0 + port*4));

	DP(NETIF_MSG_LINK, "int_mask 0x%x MI_INT %x, SERDES_LINK %x\n",
	REG_RD(bp, NIG_REG_MASK_INTERRUPT_PORT0 + port*4),
	REG_RD(bp, NIG_REG_EMAC0_STATUS_MISC_MI_INT + port*0x18),
	REG_RD(bp, NIG_REG_SERDES0_STATUS_LINK_STATUS + port*0x3c));

	DP(NETIF_MSG_LINK, " 10G %x, XGXS_LINK %x\n",
	  REG_RD(bp, NIG_REG_XGXS0_STATUS_LINK10G + port*0x68),
	  REG_RD(bp, NIG_REG_XGXS0_STATUS_LINK_STATUS + port*0x68));

	/* disable emac */
	REG_WR(bp, NIG_REG_NIG_EMAC0_EN + port*4, 0);

	ext_phy_type = XGXS_EXT_PHY_TYPE(params->ext_phy_config);

	/* Check external link change only for non-direct */
	ext_phy_link_up = bnx2x_ext_phy_is_link_up(params, vars);

	/* Read gp_status */
	CL45_RD_OVER_CL22(bp, port, params->phy_addr,
			      MDIO_REG_BANK_GP_STATUS,
			      MDIO_GP_STATUS_TOP_AN_STATUS1,
			      &gp_status);

	rc = bnx2x_link_settings_status(params, vars, gp_status);
	if (rc != 0)
		return rc;

	/* anything 10 and over uses the bmac */
	link_10g = ((vars->line_speed == SPEED_10000) ||
		    (vars->line_speed == SPEED_12000) ||
		    (vars->line_speed == SPEED_12500) ||
		    (vars->line_speed == SPEED_13000) ||
		    (vars->line_speed == SPEED_15000) ||
		    (vars->line_speed == SPEED_16000));

	bnx2x_link_int_ack(params, vars, link_10g);

	/* In case external phy link is up, and internal link is down
	( not initialized yet probably after link initialization, it needs
	to be initialized.
	Note that after link down-up as result of cable plug,
	the xgxs link would probably become up again without the need to
	initialize it*/

	if ((ext_phy_type != PORT_HW_CFG_SERDES_EXT_PHY_TYPE_DIRECT) &&
	    (ext_phy_type != PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705) &&
	    (ext_phy_link_up && !vars->phy_link_up))
		bnx2x_init_internal_phy(params, vars);

	/* link is up only if both local phy and external phy are up */
	vars->link_up = (ext_phy_link_up && vars->phy_link_up);

	if (vars->link_up)
		rc = bnx2x_update_link_up(params, vars, link_10g, gp_status);
	else
		rc = bnx2x_update_link_down(params, vars);

	return rc;
}

static u8 bnx2x_8073_common_init_phy(struct bnx2x *bp, u32 shmem_base)
{
	u8 ext_phy_addr[PORT_MAX];
	u16 val;
	s8 port;

	/* PART1 - Reset both phys */
	for (port = PORT_MAX - 1; port >= PORT_0; port--) {
		/* Extract the ext phy address for the port */
		u32 ext_phy_config = REG_RD(bp, shmem_base +
					offsetof(struct shmem_region,
		   dev_info.port_hw_config[port].external_phy_config));

		/* disable attentions */
		bnx2x_bits_dis(bp, NIG_REG_MASK_INTERRUPT_PORT0 + port*4,
			     (NIG_MASK_XGXS0_LINK_STATUS |
			      NIG_MASK_XGXS0_LINK10G |
			      NIG_MASK_SERDES0_LINK_STATUS |
			      NIG_MASK_MI_INT));

		ext_phy_addr[port] =
			((ext_phy_config &
			      PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
			      PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);

		/* Need to take the phy out of low power mode in order
			to write to access its registers */
		bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_2,
				  MISC_REGISTERS_GPIO_OUTPUT_HIGH, port);

		/* Reset the phy */
		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
			       ext_phy_addr[port],
			       MDIO_PMA_DEVAD,
			       MDIO_PMA_REG_CTRL,
			       1<<15);
	}

	/* Add delay of 150ms after reset */
	msleep(150);

	/* PART2 - Download firmware to both phys */
	for (port = PORT_MAX - 1; port >= PORT_0; port--) {
		u16 fw_ver1;

		bnx2x_bcm8073_external_rom_boot(bp, port,
						      ext_phy_addr[port]);

		bnx2x_cl45_read(bp, port, PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
			      ext_phy_addr[port],
			      MDIO_PMA_DEVAD,
			      MDIO_PMA_REG_ROM_VER1, &fw_ver1);
		if (fw_ver1 == 0 || fw_ver1 == 0x4321) {
			DP(NETIF_MSG_LINK,
				 "bnx2x_8073_common_init_phy port %x:"
				 "Download failed. fw version = 0x%x\n",
				 port, fw_ver1);
			return -EINVAL;
		}

		/* Only set bit 10 = 1 (Tx power down) */
		bnx2x_cl45_read(bp, port,
			      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
			      ext_phy_addr[port],
			      MDIO_PMA_DEVAD,
			      MDIO_PMA_REG_TX_POWER_DOWN, &val);

		/* Phase1 of TX_POWER_DOWN reset */
		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
			       ext_phy_addr[port],
			       MDIO_PMA_DEVAD,
			       MDIO_PMA_REG_TX_POWER_DOWN,
			       (val | 1<<10));
	}

	/* Toggle Transmitter: Power down and then up with 600ms
	   delay between */
	msleep(600);

	/* PART3 - complete TX_POWER_DOWN process, and set GPIO2 back to low */
	for (port = PORT_MAX - 1; port >= PORT_0; port--) {
		/* Phase2 of POWER_DOWN_RESET*/
		/* Release bit 10 (Release Tx power down) */
		bnx2x_cl45_read(bp, port,
			      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
			      ext_phy_addr[port],
			      MDIO_PMA_DEVAD,
			      MDIO_PMA_REG_TX_POWER_DOWN, &val);

		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
			       ext_phy_addr[port],
			       MDIO_PMA_DEVAD,
			       MDIO_PMA_REG_TX_POWER_DOWN, (val & (~(1<<10))));
		msleep(15);

		/* Read modify write the SPI-ROM version select register */
		bnx2x_cl45_read(bp, port,
			      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
			      ext_phy_addr[port],
			      MDIO_PMA_DEVAD,
			      MDIO_PMA_REG_EDC_FFE_MAIN, &val);
		bnx2x_cl45_write(bp, port,
			      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073,
			      ext_phy_addr[port],
			      MDIO_PMA_DEVAD,
			      MDIO_PMA_REG_EDC_FFE_MAIN, (val | (1<<12)));

		/* set GPIO2 back to LOW */
		bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_2,
				  MISC_REGISTERS_GPIO_OUTPUT_LOW, port);
	}
	return 0;

}

u8 bnx2x_common_init_phy(struct bnx2x *bp, u32 shmem_base)
{
	u8 rc = 0;
	u32 ext_phy_type;

	DP(NETIF_MSG_LINK, "bnx2x_common_init_phy\n");

	/* Read the ext_phy_type for arbitrary port(0) */
	ext_phy_type = XGXS_EXT_PHY_TYPE(
			REG_RD(bp, shmem_base +
			   offsetof(struct shmem_region,
			     dev_info.port_hw_config[0].external_phy_config)));

	switch (ext_phy_type) {
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073:
	{
		rc = bnx2x_8073_common_init_phy(bp, shmem_base);
		break;
	}
	default:
		DP(NETIF_MSG_LINK,
			 "bnx2x_common_init_phy: ext_phy 0x%x not required\n",
			 ext_phy_type);
		break;
	}

	return rc;
}



static void bnx2x_sfx7101_sp_sw_reset(struct bnx2x *bp, u8 port, u8 phy_addr)
{
	u16 val, cnt;

	bnx2x_cl45_read(bp, port,
		      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
		      phy_addr,
		      MDIO_PMA_DEVAD,
		      MDIO_PMA_REG_7101_RESET, &val);

	for (cnt = 0; cnt < 10; cnt++) {
		msleep(50);
		/* Writes a self-clearing reset */
		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			       phy_addr,
			       MDIO_PMA_DEVAD,
			       MDIO_PMA_REG_7101_RESET,
			       (val | (1<<15)));
		/* Wait for clear */
		bnx2x_cl45_read(bp, port,
			      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			      phy_addr,
			      MDIO_PMA_DEVAD,
			      MDIO_PMA_REG_7101_RESET, &val);

		if ((val & (1<<15)) == 0)
			break;
	}
}
#define RESERVED_SIZE 256
/* max application is 160K bytes - data at end of RAM */
#define MAX_APP_SIZE (160*1024 - RESERVED_SIZE)

/* Header is 14 bytes */
#define HEADER_SIZE 14
#define DATA_OFFSET HEADER_SIZE

#define SPI_START_TRANSFER(bp, port, ext_phy_addr) \
	bnx2x_cl45_write(bp, port, PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101, \
			ext_phy_addr, \
			MDIO_PCS_DEVAD, \
			MDIO_PCS_REG_7101_SPI_CTRL_ADDR, 1)

/* Programs an image to DSP's flash via the SPI port*/
static u8 bnx2x_sfx7101_flash_download(struct bnx2x *bp, u8 port,
				     u8 ext_phy_addr,
				     char data[], u32 size)
{
	const u16 num_trans = size/4; /* 4 bytes can be sent at a time */
	/* Doesn't include last trans!*/
	const u16 last_trans_size = size%4; /* Num bytes on last trans */
	u16 trans_cnt, byte_cnt;
	u32 data_index;
	u16 tmp;
	u16 code_started = 0;
	u16 image_revision1, image_revision2;
	u16 cnt;

	DP(NETIF_MSG_LINK, "bnx2x_sfx7101_flash_download file_size=%d\n", size);
	/* Going to flash*/
	if ((size-HEADER_SIZE) > MAX_APP_SIZE) {
		/* This very often will be the case, because the image is built
		with 160Kbytes size whereas the total image size must actually
		be 160Kbytes-RESERVED_SIZE */
		DP(NETIF_MSG_LINK, "Warning, file size was %d bytes "
			 "truncated to %d bytes\n", size, MAX_APP_SIZE);
		size = MAX_APP_SIZE+HEADER_SIZE;
	}
	DP(NETIF_MSG_LINK, "File version is %c%c\n", data[0x14e], data[0x14f]);
	DP(NETIF_MSG_LINK, "  	      %c%c\n", data[0x150], data[0x151]);
	/* Put the DSP in download mode by setting FLASH_CFG[2] to 1
	   and issuing a reset.*/

	bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_0,
			  MISC_REGISTERS_GPIO_HIGH, port);

	bnx2x_sfx7101_sp_sw_reset(bp, port, ext_phy_addr);

	/* wait 0.5 sec */
	for (cnt = 0; cnt < 100; cnt++)
		msleep(5);

	/* Make sure we can access the DSP
	   And it's in the correct mode (waiting for download) */

	bnx2x_cl45_read(bp, port,
		      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
		      ext_phy_addr,
		      MDIO_PCS_DEVAD,
		      MDIO_PCS_REG_7101_DSP_ACCESS, &tmp);

	if (tmp != 0x000A) {
		DP(NETIF_MSG_LINK, "DSP is not in waiting on download mode. "
			 "Expected 0x000A, read 0x%04X\n", tmp);
		DP(NETIF_MSG_LINK, "Download failed\n");
		return -EINVAL;
	}

	/* Mux the SPI interface away from the internal processor */
	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
		       ext_phy_addr,
		       MDIO_PCS_DEVAD,
		       MDIO_PCS_REG_7101_SPI_MUX, 1);

	/* Reset the SPI port */
	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
		       ext_phy_addr,
		       MDIO_PCS_DEVAD,
		       MDIO_PCS_REG_7101_SPI_CTRL_ADDR, 0);
	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
		       ext_phy_addr,
		       MDIO_PCS_DEVAD,
		       MDIO_PCS_REG_7101_SPI_CTRL_ADDR,
		       (1<<MDIO_PCS_REG_7101_SPI_RESET_BIT));
	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
		       ext_phy_addr,
		       MDIO_PCS_DEVAD,
		       MDIO_PCS_REG_7101_SPI_CTRL_ADDR, 0);

	/* Erase the flash */
	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
		       ext_phy_addr,
		       MDIO_PCS_DEVAD,
		       MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
		       MDIO_PCS_REG_7101_SPI_FIFO_ADDR_WRITE_ENABLE_CMD);

	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
		       ext_phy_addr,
		       MDIO_PCS_DEVAD,
		       MDIO_PCS_REG_7101_SPI_BYTES_TO_TRANSFER_ADDR,
		       1);

	SPI_START_TRANSFER(bp, port, ext_phy_addr);
	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
		       ext_phy_addr,
		       MDIO_PCS_DEVAD,
		       MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
		       MDIO_PCS_REG_7101_SPI_FIFO_ADDR_BULK_ERASE_CMD);

	bnx2x_cl45_write(bp, port,
		       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
		       ext_phy_addr,
		       MDIO_PCS_DEVAD,
		       MDIO_PCS_REG_7101_SPI_BYTES_TO_TRANSFER_ADDR,
		       1);
	SPI_START_TRANSFER(bp, port, ext_phy_addr);

	/* Wait 10 seconds, the maximum time for the erase to complete */
	DP(NETIF_MSG_LINK, "Erasing flash, this takes 10 seconds...\n");
	for (cnt = 0; cnt < 1000; cnt++)
		msleep(10);

	DP(NETIF_MSG_LINK, "Downloading flash, please wait...\n");
	data_index = 0;
	for (trans_cnt = 0; trans_cnt < num_trans; trans_cnt++) {
		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			     ext_phy_addr,
			     MDIO_PCS_DEVAD,
			     MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
			     MDIO_PCS_REG_7101_SPI_FIFO_ADDR_WRITE_ENABLE_CMD);

		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			       ext_phy_addr,
			       MDIO_PCS_DEVAD,
			       MDIO_PCS_REG_7101_SPI_BYTES_TO_TRANSFER_ADDR,
			       1);
		SPI_START_TRANSFER(bp, port, ext_phy_addr);

		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			       ext_phy_addr,
			       MDIO_PCS_DEVAD,
			       MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
			     MDIO_PCS_REG_7101_SPI_FIFO_ADDR_PAGE_PROGRAM_CMD);

		/* Bits 23-16 of address */
		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			       ext_phy_addr,
			       MDIO_PCS_DEVAD,
			       MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
			       (data_index>>16));
		/* Bits 15-8 of address */
		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			       ext_phy_addr,
			       MDIO_PCS_DEVAD,
			       MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
			       (data_index>>8));

		/* Bits 7-0 of address */
		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			       ext_phy_addr,
			       MDIO_PCS_DEVAD,
			       MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
			       ((u16)data_index));

		byte_cnt = 0;
		while (byte_cnt < 4 && data_index < size) {
			bnx2x_cl45_write(bp, port,
				       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
				       ext_phy_addr,
			       MDIO_PCS_DEVAD,
			       MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
			       data[data_index++]);
			byte_cnt++;
		}

		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			       ext_phy_addr,
			       MDIO_PCS_DEVAD,
			       MDIO_PCS_REG_7101_SPI_BYTES_TO_TRANSFER_ADDR,
			       byte_cnt+4);

		SPI_START_TRANSFER(bp, port, ext_phy_addr);
		msleep(5); /* Wait 5 ms minimum between transs */

		/* Let the user know something's going on.*/
		/* a pacifier ever 4K */
		if ((data_index % 1023) == 0)
			DP(NETIF_MSG_LINK, "Download %d%%\n", data_index/size);
	}

	DP(NETIF_MSG_LINK, "\n");
	/* Transfer the last block if there is data remaining */
	if (last_trans_size) {
		bnx2x_cl45_write(bp, port,
			PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			ext_phy_addr,
			MDIO_PCS_DEVAD,
			MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
			MDIO_PCS_REG_7101_SPI_FIFO_ADDR_WRITE_ENABLE_CMD);

		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			       ext_phy_addr,
			       MDIO_PCS_DEVAD,
			       MDIO_PCS_REG_7101_SPI_BYTES_TO_TRANSFER_ADDR,
			       1);

		SPI_START_TRANSFER(bp, port, ext_phy_addr);

		bnx2x_cl45_write(bp, port,
			     PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			     ext_phy_addr,
			     MDIO_PCS_DEVAD,
			     MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
			     MDIO_PCS_REG_7101_SPI_FIFO_ADDR_PAGE_PROGRAM_CMD);

		/* Bits 23-16 of address */
		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			       ext_phy_addr,
			       MDIO_PCS_DEVAD,
			       MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
			       (data_index>>16));
		/* Bits 15-8 of address */
		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			       ext_phy_addr,
			       MDIO_PCS_DEVAD,
			       MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
			       (data_index>>8));

		/* Bits 7-0 of address */
		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			       ext_phy_addr,
			       MDIO_PCS_DEVAD,
			       MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
			       ((u16)data_index));

		byte_cnt = 0;
		while (byte_cnt < last_trans_size && data_index < size) {
			/* Bits 7-0 of address */
			bnx2x_cl45_write(bp, port,
				PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
				ext_phy_addr,
				MDIO_PCS_DEVAD,
				MDIO_PCS_REG_7101_SPI_FIFO_ADDR,
				data[data_index++]);
			byte_cnt++;
		}

		bnx2x_cl45_write(bp, port,
			       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
			       ext_phy_addr,
			       MDIO_PCS_DEVAD,
			       MDIO_PCS_REG_7101_SPI_BYTES_TO_TRANSFER_ADDR,
			       byte_cnt+4);

		SPI_START_TRANSFER(bp, port, ext_phy_addr);
	}

	/* DSP Remove Download Mode */
	bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_0,
			  MISC_REGISTERS_GPIO_LOW, port);

	bnx2x_sfx7101_sp_sw_reset(bp, port, ext_phy_addr);

	/* wait 0.5 sec to allow it to run */
	for (cnt = 0; cnt < 100; cnt++)
		msleep(5);

	bnx2x_hw_reset(bp, port);

	for (cnt = 0; cnt < 100; cnt++)
		msleep(5);

	/* Check that the code is started. In case the download
	checksum failed, the code won't be started. */
	bnx2x_cl45_read(bp, port,
		      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
		      ext_phy_addr,
		      MDIO_PCS_DEVAD,
		      MDIO_PCS_REG_7101_DSP_ACCESS,
		      &tmp);

	code_started = (tmp & (1<<4));
	if (!code_started) {
		DP(NETIF_MSG_LINK, "Download failed. Please check file.\n");
		return -EINVAL;
	}

	/* Verify that the file revision is now equal to the image
	revision within the DSP */
	bnx2x_cl45_read(bp, port,
		      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
		      ext_phy_addr,
		      MDIO_PMA_DEVAD,
		      MDIO_PMA_REG_7101_VER1,
		      &image_revision1);

	bnx2x_cl45_read(bp, port,
		      PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101,
		      ext_phy_addr,
		      MDIO_PMA_DEVAD,
		      MDIO_PMA_REG_7101_VER2,
		      &image_revision2);

	if (data[0x14e] != (image_revision2&0xFF) ||
	    data[0x14f] != ((image_revision2&0xFF00)>>8) ||
	    data[0x150] != (image_revision1&0xFF) ||
	    data[0x151] != ((image_revision1&0xFF00)>>8)) {
		DP(NETIF_MSG_LINK, "Download failed.\n");
		return -EINVAL;
	}
	DP(NETIF_MSG_LINK, "Download %d%%\n", data_index/size);
	return 0;
}

u8 bnx2x_flash_download(struct bnx2x *bp, u8 port, u32 ext_phy_config,
		      u8 driver_loaded, char data[], u32 size)
{
	u8 rc = 0;
	u32 ext_phy_type;
	u8 ext_phy_addr;
	ext_phy_addr = ((ext_phy_config &
			PORT_HW_CFG_XGXS_EXT_PHY_ADDR_MASK) >>
			PORT_HW_CFG_XGXS_EXT_PHY_ADDR_SHIFT);

	ext_phy_type = XGXS_EXT_PHY_TYPE(ext_phy_config);

	switch (ext_phy_type) {
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8072:
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8073:
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8705:
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM8706:
		DP(NETIF_MSG_LINK,
			"Flash download not supported for this ext phy\n");
		rc = -EINVAL;
		break;
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
		/* Take ext phy out of reset */
		if (!driver_loaded)
			bnx2x_turn_on_ef(bp, port, ext_phy_addr, ext_phy_type);
		rc = bnx2x_sfx7101_flash_download(bp, port, ext_phy_addr,
						data, size);
		if (!driver_loaded)
			bnx2x_turn_off_sf(bp, port);
		break;
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_DIRECT:
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE:
	case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_NOT_CONN:
	default:
		DP(NETIF_MSG_LINK, "Invalid ext phy type\n");
		rc = -EINVAL;
		break;
	}
	return rc;
}

