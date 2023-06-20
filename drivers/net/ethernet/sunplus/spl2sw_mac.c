// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/bitfield.h>
#include <linux/of_mdio.h>

#include "spl2sw_register.h"
#include "spl2sw_define.h"
#include "spl2sw_desc.h"
#include "spl2sw_mac.h"

void spl2sw_mac_hw_stop(struct spl2sw_common *comm)
{
	u32 reg;

	if (comm->enable == 0) {
		/* Mask and clear all interrupts. */
		writel(0xffffffff, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
		writel(0xffffffff, comm->l2sw_reg_base + L2SW_SW_INT_STATUS_0);

		/* Disable cpu 0 and cpu 1. */
		reg = readl(comm->l2sw_reg_base + L2SW_CPU_CNTL);
		reg |= MAC_DIS_SOC1_CPU | MAC_DIS_SOC0_CPU;
		writel(reg, comm->l2sw_reg_base + L2SW_CPU_CNTL);
	}

	/* Disable LAN ports. */
	reg = readl(comm->l2sw_reg_base + L2SW_PORT_CNTL0);
	reg |= FIELD_PREP(MAC_DIS_PORT, ~comm->enable);
	writel(reg, comm->l2sw_reg_base + L2SW_PORT_CNTL0);
}

void spl2sw_mac_hw_start(struct spl2sw_common *comm)
{
	u32 reg;

	/* Enable cpu port 0 (6) & CRC padding (8) */
	reg = readl(comm->l2sw_reg_base + L2SW_CPU_CNTL);
	reg &= ~MAC_DIS_SOC0_CPU;
	reg |= MAC_EN_CRC_SOC0;
	writel(reg, comm->l2sw_reg_base + L2SW_CPU_CNTL);

	/* Enable port 0 & port 1 */
	reg = readl(comm->l2sw_reg_base + L2SW_PORT_CNTL0);
	reg &= FIELD_PREP(MAC_DIS_PORT, ~comm->enable) | ~MAC_DIS_PORT;
	writel(reg, comm->l2sw_reg_base + L2SW_PORT_CNTL0);
}

int spl2sw_mac_addr_add(struct spl2sw_mac *mac)
{
	struct spl2sw_common *comm = mac->comm;
	u32 reg;
	int ret;

	/* Write 6-octet MAC address. */
	writel((mac->mac_addr[0] << 0) + (mac->mac_addr[1] << 8),
	       comm->l2sw_reg_base + L2SW_W_MAC_15_0);
	writel((mac->mac_addr[2] << 0) + (mac->mac_addr[3] << 8) +
	       (mac->mac_addr[4] << 16) + (mac->mac_addr[5] << 24),
	       comm->l2sw_reg_base + L2SW_W_MAC_47_16);

	/* Set learn port = cpu_port, aging = 1 */
	reg = MAC_W_CPU_PORT_0 | FIELD_PREP(MAC_W_VID, mac->vlan_id) |
	      FIELD_PREP(MAC_W_AGE, 1) | MAC_W_MAC_CMD;
	writel(reg, comm->l2sw_reg_base + L2SW_WT_MAC_AD0);

	/* Wait for completing. */
	ret = read_poll_timeout(readl, reg, reg & MAC_W_MAC_DONE, 1, 200, true,
				comm->l2sw_reg_base + L2SW_WT_MAC_AD0);
	if (ret) {
		netdev_err(mac->ndev, "Failed to add address to table!\n");
		return ret;
	}

	netdev_dbg(mac->ndev, "mac_ad0 = %08x, mac_ad = %08x%04x\n",
		   readl(comm->l2sw_reg_base + L2SW_WT_MAC_AD0),
		   (u32)FIELD_GET(MAC_W_MAC_47_16,
		   readl(comm->l2sw_reg_base + L2SW_W_MAC_47_16)),
		   (u32)FIELD_GET(MAC_W_MAC_15_0,
		   readl(comm->l2sw_reg_base + L2SW_W_MAC_15_0)));
	return 0;
}

int spl2sw_mac_addr_del(struct spl2sw_mac *mac)
{
	struct spl2sw_common *comm = mac->comm;
	u32 reg;
	int ret;

	/* Write 6-octet MAC address. */
	writel((mac->mac_addr[0] << 0) + (mac->mac_addr[1] << 8),
	       comm->l2sw_reg_base + L2SW_W_MAC_15_0);
	writel((mac->mac_addr[2] << 0) + (mac->mac_addr[3] << 8) +
	       (mac->mac_addr[4] << 16) + (mac->mac_addr[5] << 24),
	       comm->l2sw_reg_base + L2SW_W_MAC_47_16);

	/* Set learn port = lan_port0 and aging = 0
	 * to wipe (age) out the entry.
	 */
	reg = MAC_W_LAN_PORT_0 | FIELD_PREP(MAC_W_VID, mac->vlan_id) | MAC_W_MAC_CMD;
	writel(reg, comm->l2sw_reg_base + L2SW_WT_MAC_AD0);

	/* Wait for completing. */
	ret = read_poll_timeout(readl, reg, reg & MAC_W_MAC_DONE, 1, 200, true,
				comm->l2sw_reg_base + L2SW_WT_MAC_AD0);
	if (ret) {
		netdev_err(mac->ndev, "Failed to delete address from table!\n");
		return ret;
	}

	netdev_dbg(mac->ndev, "mac_ad0 = %08x, mac_ad = %08x%04x\n",
		   readl(comm->l2sw_reg_base + L2SW_WT_MAC_AD0),
		   (u32)FIELD_GET(MAC_W_MAC_47_16,
		   readl(comm->l2sw_reg_base + L2SW_W_MAC_47_16)),
		   (u32)FIELD_GET(MAC_W_MAC_15_0,
		   readl(comm->l2sw_reg_base + L2SW_W_MAC_15_0)));
	return 0;
}

void spl2sw_mac_hw_init(struct spl2sw_common *comm)
{
	u32 reg;

	/* Disable cpu0 and cpu 1 port. */
	reg = readl(comm->l2sw_reg_base + L2SW_CPU_CNTL);
	reg |= MAC_DIS_SOC1_CPU | MAC_DIS_SOC0_CPU;
	writel(reg, comm->l2sw_reg_base + L2SW_CPU_CNTL);

	/* Set base addresses of TX and RX queues. */
	writel(comm->desc_dma, comm->l2sw_reg_base + L2SW_TX_LBASE_ADDR_0);
	writel(comm->desc_dma + sizeof(struct spl2sw_mac_desc) * TX_DESC_NUM,
	       comm->l2sw_reg_base + L2SW_TX_HBASE_ADDR_0);
	writel(comm->desc_dma + sizeof(struct spl2sw_mac_desc) * (TX_DESC_NUM +
	       MAC_GUARD_DESC_NUM), comm->l2sw_reg_base + L2SW_RX_HBASE_ADDR_0);
	writel(comm->desc_dma + sizeof(struct spl2sw_mac_desc) * (TX_DESC_NUM +
	       MAC_GUARD_DESC_NUM + RX_QUEUE0_DESC_NUM),
	       comm->l2sw_reg_base + L2SW_RX_LBASE_ADDR_0);

	/* Fc_rls_th=0x4a, Fc_set_th=0x3a, Drop_rls_th=0x2d, Drop_set_th=0x1d */
	writel(0x4a3a2d1d, comm->l2sw_reg_base + L2SW_FL_CNTL_TH);

	/* Cpu_rls_th=0x4a, Cpu_set_th=0x3a, Cpu_th=0x12, Port_th=0x12 */
	writel(0x4a3a1212, comm->l2sw_reg_base + L2SW_CPU_FL_CNTL_TH);

	/* mtcc_lmt=0xf, Pri_th_l=6, Pri_th_h=6, weigh_8x_en=1 */
	writel(0xf6680000, comm->l2sw_reg_base + L2SW_PRI_FL_CNTL);

	/* High-active LED */
	reg = readl(comm->l2sw_reg_base + L2SW_LED_PORT0);
	reg |= MAC_LED_ACT_HI;
	writel(reg, comm->l2sw_reg_base + L2SW_LED_PORT0);

	/* Disable aging of cpu port 0 & 1.
	 * Disable SA learning of cpu port 0 & 1.
	 * Enable UC and MC packets
	 */
	reg = readl(comm->l2sw_reg_base + L2SW_CPU_CNTL);
	reg &= ~(MAC_EN_SOC1_AGING | MAC_EN_SOC0_AGING |
		 MAC_DIS_BC2CPU_P1 | MAC_DIS_BC2CPU_P0 |
		 MAC_DIS_MC2CPU_P1 | MAC_DIS_MC2CPU_P0);
	reg |= MAC_DIS_LRN_SOC1 | MAC_DIS_LRN_SOC0;
	writel(reg, comm->l2sw_reg_base + L2SW_CPU_CNTL);

	/* Enable RMC2CPU for port 0 & 1
	 * Enable Flow control for port 0 & 1
	 * Enable Back pressure for port 0 & 1
	 */
	reg = readl(comm->l2sw_reg_base + L2SW_PORT_CNTL0);
	reg &= ~(MAC_DIS_RMC2CPU_P1 | MAC_DIS_RMC2CPU_P0);
	reg |= MAC_EN_FLOW_CTL_P1 | MAC_EN_FLOW_CTL_P0 |
	       MAC_EN_BACK_PRESS_P1 | MAC_EN_BACK_PRESS_P0;
	writel(reg, comm->l2sw_reg_base + L2SW_PORT_CNTL0);

	/* Disable LAN port SA learning. */
	reg = readl(comm->l2sw_reg_base + L2SW_PORT_CNTL1);
	reg |= MAC_DIS_SA_LRN_P1 | MAC_DIS_SA_LRN_P0;
	writel(reg, comm->l2sw_reg_base + L2SW_PORT_CNTL1);

	/* Enable rmii force mode and
	 * set both external phy-address to 31.
	 */
	reg = readl(comm->l2sw_reg_base + L2SW_MAC_FORCE_MODE);
	reg &= ~(MAC_EXT_PHY1_ADDR | MAC_EXT_PHY0_ADDR);
	reg |= FIELD_PREP(MAC_EXT_PHY1_ADDR, 31) | FIELD_PREP(MAC_EXT_PHY0_ADDR, 31);
	reg |= MAC_FORCE_RMII_EN_1 | MAC_FORCE_RMII_EN_0;
	writel(reg, comm->l2sw_reg_base + L2SW_MAC_FORCE_MODE);

	/* Port 0: VLAN group 0
	 * Port 1: VLAN group 1
	 */
	reg = FIELD_PREP(MAC_P1_PVID, 1) | FIELD_PREP(MAC_P0_PVID, 0);
	writel(reg, comm->l2sw_reg_base + L2SW_PVID_CONFIG0);

	/* VLAN group 0: cpu0 (bit3) + port0 (bit0) = 1001 = 0x9
	 * VLAN group 1: cpu0 (bit3) + port1 (bit1) = 1010 = 0xa
	 */
	reg = FIELD_PREP(MAC_VLAN_MEMSET_1, 0xa) | FIELD_PREP(MAC_VLAN_MEMSET_0, 9);
	writel(reg, comm->l2sw_reg_base + L2SW_VLAN_MEMSET_CONFIG0);

	/* RMC forward: to_cpu (1)
	 * LED: 60mS (1)
	 * BC storm prev: 31 BC (1)
	 */
	reg = readl(comm->l2sw_reg_base + L2SW_SW_GLB_CNTL);
	reg &= ~(MAC_RMC_TB_FAULT_RULE | MAC_LED_FLASH_TIME | MAC_BC_STORM_PREV);
	reg |= FIELD_PREP(MAC_RMC_TB_FAULT_RULE, 1) |
	       FIELD_PREP(MAC_LED_FLASH_TIME, 1) |
	       FIELD_PREP(MAC_BC_STORM_PREV, 1);
	writel(reg, comm->l2sw_reg_base + L2SW_SW_GLB_CNTL);

	writel(MAC_INT_MASK_DEF, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
}

void spl2sw_mac_rx_mode_set(struct spl2sw_mac *mac)
{
	struct spl2sw_common *comm = mac->comm;
	struct net_device *ndev = mac->ndev;
	u32 mask, reg, rx_mode;

	netdev_dbg(ndev, "ndev->flags = %08x\n", ndev->flags);
	mask = FIELD_PREP(MAC_DIS_MC2CPU, mac->lan_port) |
	       FIELD_PREP(MAC_DIS_UN2CPU, mac->lan_port);
	reg = readl(comm->l2sw_reg_base + L2SW_CPU_CNTL);

	if (ndev->flags & IFF_PROMISC) {
		/* Allow MC and unknown UC packets */
		rx_mode = FIELD_PREP(MAC_DIS_MC2CPU, mac->lan_port) |
			  FIELD_PREP(MAC_DIS_UN2CPU, mac->lan_port);
	} else if ((!netdev_mc_empty(ndev) && (ndev->flags & IFF_MULTICAST)) ||
		   (ndev->flags & IFF_ALLMULTI)) {
		/* Allow MC packets */
		rx_mode = FIELD_PREP(MAC_DIS_MC2CPU, mac->lan_port);
	} else {
		/* Disable MC and unknown UC packets */
		rx_mode = 0;
	}

	writel((reg & (~mask)) | ((~rx_mode) & mask), comm->l2sw_reg_base + L2SW_CPU_CNTL);
	netdev_dbg(ndev, "cpu_cntl = %08x\n", readl(comm->l2sw_reg_base + L2SW_CPU_CNTL));
}

void spl2sw_mac_init(struct spl2sw_common *comm)
{
	u32 i;

	for (i = 0; i < RX_DESC_QUEUE_NUM; i++)
		comm->rx_pos[i] = 0;
	mb();	/* make sure settings are effective. */

	spl2sw_mac_hw_init(comm);
}

void spl2sw_mac_soft_reset(struct spl2sw_common *comm)
{
	u32 i;

	spl2sw_mac_hw_stop(comm);

	spl2sw_rx_descs_flush(comm);
	comm->tx_pos = 0;
	comm->tx_done_pos = 0;
	comm->tx_desc_full = 0;

	for (i = 0; i < RX_DESC_QUEUE_NUM; i++)
		comm->rx_pos[i] = 0;
	mb();	/* make sure settings are effective. */

	spl2sw_mac_hw_init(comm);
	spl2sw_mac_hw_start(comm);
}
