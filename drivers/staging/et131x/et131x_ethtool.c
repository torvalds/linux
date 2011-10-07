/*
 *  Copyright (c) 2011 Mark Einon <mark.einon@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "et131x_defs.h"

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/pci.h>

#include "et131x_adapter.h"
#include "et1310_phy.h"
#include "et131x.h"

static int et131x_get_settings(struct net_device *netdev,
			       struct ethtool_cmd *cmd)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	return phy_ethtool_gset(adapter->phydev, cmd);
}

static int et131x_set_settings(struct net_device *netdev,
			       struct ethtool_cmd *cmd)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	return phy_ethtool_sset(adapter->phydev, cmd);
}

static int et131x_get_regs_len(struct net_device *netdev)
{
#define ET131X_REGS_LEN 256
	return ET131X_REGS_LEN * sizeof(u32);
}

static void et131x_get_regs(struct net_device *netdev,
			    struct ethtool_regs *regs, void *regs_data)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);
	struct address_map __iomem *aregs = adapter->regs;
	u32 *regs_buff = regs_data;
	u32 num = 0;

	memset(regs_data, 0, et131x_get_regs_len(netdev));

	regs->version = (1 << 24) | (adapter->pdev->revision << 16) |
			adapter->pdev->device;

	/* PHY regs */
	et131x_mii_read(adapter, MII_BMCR, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_BMSR, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_PHYSID1, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_PHYSID2, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_ADVERTISE, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_LPA, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_EXPANSION, (u16 *)&regs_buff[num++]);
	/* Autoneg next page transmit reg */
	et131x_mii_read(adapter, 0x07, (u16 *)&regs_buff[num++]);
	/* Link partner next page reg */
	et131x_mii_read(adapter, 0x08, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_CTRL1000, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_STAT1000, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, MII_ESTATUS, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_INDEX_REG, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_DATA_REG, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_MPHY_CONTROL_REG,
			(u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_LOOPBACK_CONTROL,
			(u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_LOOPBACK_CONTROL+1,
			(u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_REGISTER_MGMT_CONTROL,
			(u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_CONFIG, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_PHY_CONTROL, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_INTERRUPT_MASK, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_INTERRUPT_STATUS,
			(u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_PHY_STATUS, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_LED_1, (u16 *)&regs_buff[num++]);
	et131x_mii_read(adapter, PHY_LED_2, (u16 *)&regs_buff[num++]);

	/* Global regs */
	regs_buff[num++] = readl(&aregs->global.txq_start_addr);
	regs_buff[num++] = readl(&aregs->global.txq_end_addr);
	regs_buff[num++] = readl(&aregs->global.rxq_start_addr);
	regs_buff[num++] = readl(&aregs->global.rxq_end_addr);
	regs_buff[num++] = readl(&aregs->global.pm_csr);
	regs_buff[num++] = adapter->stats.interrupt_status;
	regs_buff[num++] = readl(&aregs->global.int_mask);
	regs_buff[num++] = readl(&aregs->global.int_alias_clr_en);
	regs_buff[num++] = readl(&aregs->global.int_status_alias);
	regs_buff[num++] = readl(&aregs->global.sw_reset);
	regs_buff[num++] = readl(&aregs->global.slv_timer);
	regs_buff[num++] = readl(&aregs->global.msi_config);
	regs_buff[num++] = readl(&aregs->global.loopback);
	regs_buff[num++] = readl(&aregs->global.watchdog_timer);

	/* TXDMA regs */
	regs_buff[num++] = readl(&aregs->txdma.csr);
	regs_buff[num++] = readl(&aregs->txdma.pr_base_hi);
	regs_buff[num++] = readl(&aregs->txdma.pr_base_lo);
	regs_buff[num++] = readl(&aregs->txdma.pr_num_des);
	regs_buff[num++] = readl(&aregs->txdma.txq_wr_addr);
	regs_buff[num++] = readl(&aregs->txdma.txq_wr_addr_ext);
	regs_buff[num++] = readl(&aregs->txdma.txq_rd_addr);
	regs_buff[num++] = readl(&aregs->txdma.dma_wb_base_hi);
	regs_buff[num++] = readl(&aregs->txdma.dma_wb_base_lo);
	regs_buff[num++] = readl(&aregs->txdma.service_request);
	regs_buff[num++] = readl(&aregs->txdma.service_complete);
	regs_buff[num++] = readl(&aregs->txdma.cache_rd_index);
	regs_buff[num++] = readl(&aregs->txdma.cache_wr_index);
	regs_buff[num++] = readl(&aregs->txdma.tx_dma_error);
	regs_buff[num++] = readl(&aregs->txdma.desc_abort_cnt);
	regs_buff[num++] = readl(&aregs->txdma.payload_abort_cnt);
	regs_buff[num++] = readl(&aregs->txdma.writeback_abort_cnt);
	regs_buff[num++] = readl(&aregs->txdma.desc_timeout_cnt);
	regs_buff[num++] = readl(&aregs->txdma.payload_timeout_cnt);
	regs_buff[num++] = readl(&aregs->txdma.writeback_timeout_cnt);
	regs_buff[num++] = readl(&aregs->txdma.desc_error_cnt);
	regs_buff[num++] = readl(&aregs->txdma.payload_error_cnt);
	regs_buff[num++] = readl(&aregs->txdma.writeback_error_cnt);
	regs_buff[num++] = readl(&aregs->txdma.dropped_tlp_cnt);
	regs_buff[num++] = readl(&aregs->txdma.new_service_complete);
	regs_buff[num++] = readl(&aregs->txdma.ethernet_packet_cnt);

	/* RXDMA regs */
	regs_buff[num++] = readl(&aregs->rxdma.csr);
	regs_buff[num++] = readl(&aregs->rxdma.dma_wb_base_hi);
	regs_buff[num++] = readl(&aregs->rxdma.dma_wb_base_lo);
	regs_buff[num++] = readl(&aregs->rxdma.num_pkt_done);
	regs_buff[num++] = readl(&aregs->rxdma.max_pkt_time);
	regs_buff[num++] = readl(&aregs->rxdma.rxq_rd_addr);
	regs_buff[num++] = readl(&aregs->rxdma.rxq_rd_addr_ext);
	regs_buff[num++] = readl(&aregs->rxdma.rxq_wr_addr);
	regs_buff[num++] = readl(&aregs->rxdma.psr_base_hi);
	regs_buff[num++] = readl(&aregs->rxdma.psr_base_lo);
	regs_buff[num++] = readl(&aregs->rxdma.psr_num_des);
	regs_buff[num++] = readl(&aregs->rxdma.psr_avail_offset);
	regs_buff[num++] = readl(&aregs->rxdma.psr_full_offset);
	regs_buff[num++] = readl(&aregs->rxdma.psr_access_index);
	regs_buff[num++] = readl(&aregs->rxdma.psr_min_des);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_base_lo);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_base_hi);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_num_des);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_avail_offset);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_full_offset);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_rd_index);
	regs_buff[num++] = readl(&aregs->rxdma.fbr0_min_des);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_base_lo);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_base_hi);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_num_des);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_avail_offset);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_full_offset);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_rd_index);
	regs_buff[num++] = readl(&aregs->rxdma.fbr1_min_des);
}

#define ET131X_DRVINFO_LEN 32 /* value from ethtool.h */
static void et131x_get_drvinfo(struct net_device *netdev,
			       struct ethtool_drvinfo *info)
{
	struct et131x_adapter *adapter = netdev_priv(netdev);

	strncpy(info->driver, DRIVER_NAME, ET131X_DRVINFO_LEN);
	strncpy(info->version, DRIVER_VERSION, ET131X_DRVINFO_LEN);
	strncpy(info->bus_info, pci_name(adapter->pdev), ET131X_DRVINFO_LEN);
}

static struct ethtool_ops et131x_ethtool_ops = {
	.get_settings	= et131x_get_settings,
	.set_settings	= et131x_set_settings,
	.get_drvinfo	= et131x_get_drvinfo,
	.get_regs_len	= et131x_get_regs_len,
	.get_regs	= et131x_get_regs,
	.get_link = ethtool_op_get_link,
};

void et131x_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &et131x_ethtool_ops);
}

