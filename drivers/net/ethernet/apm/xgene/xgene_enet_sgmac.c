/* Applied Micro X-Gene SoC Ethernet Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Authors: Iyappan Subramanian <isubramanian@apm.com>
 *	    Keyur Chudgar <kchudgar@apm.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xgene_enet_main.h"
#include "xgene_enet_hw.h"
#include "xgene_enet_sgmac.h"

static void xgene_enet_wr_csr(struct xgene_enet_pdata *p, u32 offset, u32 val)
{
	iowrite32(val, p->eth_csr_addr + offset);
}

static void xgene_enet_wr_ring_if(struct xgene_enet_pdata *p,
				  u32 offset, u32 val)
{
	iowrite32(val, p->eth_ring_if_addr + offset);
}

static void xgene_enet_wr_diag_csr(struct xgene_enet_pdata *p,
				   u32 offset, u32 val)
{
	iowrite32(val, p->eth_diag_csr_addr + offset);
}

static bool xgene_enet_wr_indirect(struct xgene_indirect_ctl *ctl,
				   u32 wr_addr, u32 wr_data)
{
	int i;

	iowrite32(wr_addr, ctl->addr);
	iowrite32(wr_data, ctl->ctl);
	iowrite32(XGENE_ENET_WR_CMD, ctl->cmd);

	/* wait for write command to complete */
	for (i = 0; i < 10; i++) {
		if (ioread32(ctl->cmd_done)) {
			iowrite32(0, ctl->cmd);
			return true;
		}
		udelay(1);
	}

	return false;
}

static void xgene_enet_wr_mac(struct xgene_enet_pdata *p,
			      u32 wr_addr, u32 wr_data)
{
	struct xgene_indirect_ctl ctl = {
		.addr = p->mcx_mac_addr + MAC_ADDR_REG_OFFSET,
		.ctl = p->mcx_mac_addr + MAC_WRITE_REG_OFFSET,
		.cmd = p->mcx_mac_addr + MAC_COMMAND_REG_OFFSET,
		.cmd_done = p->mcx_mac_addr + MAC_COMMAND_DONE_REG_OFFSET
	};

	if (!xgene_enet_wr_indirect(&ctl, wr_addr, wr_data))
		netdev_err(p->ndev, "mac write failed, addr: %04x\n", wr_addr);
}

static u32 xgene_enet_rd_csr(struct xgene_enet_pdata *p, u32 offset)
{
	return ioread32(p->eth_csr_addr + offset);
}

static u32 xgene_enet_rd_diag_csr(struct xgene_enet_pdata *p, u32 offset)
{
	return ioread32(p->eth_diag_csr_addr + offset);
}

static u32 xgene_enet_rd_indirect(struct xgene_indirect_ctl *ctl, u32 rd_addr)
{
	u32 rd_data;
	int i;

	iowrite32(rd_addr, ctl->addr);
	iowrite32(XGENE_ENET_RD_CMD, ctl->cmd);

	/* wait for read command to complete */
	for (i = 0; i < 10; i++) {
		if (ioread32(ctl->cmd_done)) {
			rd_data = ioread32(ctl->ctl);
			iowrite32(0, ctl->cmd);

			return rd_data;
		}
		udelay(1);
	}

	pr_err("%s: mac read failed, addr: %04x\n", __func__, rd_addr);

	return 0;
}

static u32 xgene_enet_rd_mac(struct xgene_enet_pdata *p, u32 rd_addr)
{
	struct xgene_indirect_ctl ctl = {
		.addr = p->mcx_mac_addr + MAC_ADDR_REG_OFFSET,
		.ctl = p->mcx_mac_addr + MAC_READ_REG_OFFSET,
		.cmd = p->mcx_mac_addr + MAC_COMMAND_REG_OFFSET,
		.cmd_done = p->mcx_mac_addr + MAC_COMMAND_DONE_REG_OFFSET
	};

	return xgene_enet_rd_indirect(&ctl, rd_addr);
}

static int xgene_enet_ecc_init(struct xgene_enet_pdata *p)
{
	struct net_device *ndev = p->ndev;
	u32 data;
	int i = 0;

	xgene_enet_wr_diag_csr(p, ENET_CFG_MEM_RAM_SHUTDOWN_ADDR, 0);
	do {
		usleep_range(100, 110);
		data = xgene_enet_rd_diag_csr(p, ENET_BLOCK_MEM_RDY_ADDR);
		if (data == ~0U)
			return 0;
	} while (++i < 10);

	netdev_err(ndev, "Failed to release memory from shutdown\n");
	return -ENODEV;
}

static void xgene_enet_config_ring_if_assoc(struct xgene_enet_pdata *p)
{
	u32 val = 0xffffffff;

	xgene_enet_wr_ring_if(p, ENET_CFGSSQMIWQASSOC_ADDR, val);
	xgene_enet_wr_ring_if(p, ENET_CFGSSQMIFPQASSOC_ADDR, val);
}

static void xgene_mii_phy_write(struct xgene_enet_pdata *p, u8 phy_id,
				u32 reg, u16 data)
{
	u32 addr, wr_data, done;
	int i;

	addr = PHY_ADDR(phy_id) | REG_ADDR(reg);
	xgene_enet_wr_mac(p, MII_MGMT_ADDRESS_ADDR, addr);

	wr_data = PHY_CONTROL(data);
	xgene_enet_wr_mac(p, MII_MGMT_CONTROL_ADDR, wr_data);

	for (i = 0; i < 10; i++) {
		done = xgene_enet_rd_mac(p, MII_MGMT_INDICATORS_ADDR);
		if (!(done & BUSY_MASK))
			return;
		usleep_range(10, 20);
	}

	netdev_err(p->ndev, "MII_MGMT write failed\n");
}

static u32 xgene_mii_phy_read(struct xgene_enet_pdata *p, u8 phy_id, u32 reg)
{
	u32 addr, data, done;
	int i;

	addr = PHY_ADDR(phy_id) | REG_ADDR(reg);
	xgene_enet_wr_mac(p, MII_MGMT_ADDRESS_ADDR, addr);
	xgene_enet_wr_mac(p, MII_MGMT_COMMAND_ADDR, READ_CYCLE_MASK);

	for (i = 0; i < 10; i++) {
		done = xgene_enet_rd_mac(p, MII_MGMT_INDICATORS_ADDR);
		if (!(done & BUSY_MASK)) {
			data = xgene_enet_rd_mac(p, MII_MGMT_STATUS_ADDR);
			xgene_enet_wr_mac(p, MII_MGMT_COMMAND_ADDR, 0);

			return data;
		}
		usleep_range(10, 20);
	}

	netdev_err(p->ndev, "MII_MGMT read failed\n");

	return 0;
}

static void xgene_sgmac_reset(struct xgene_enet_pdata *p)
{
	xgene_enet_wr_mac(p, MAC_CONFIG_1_ADDR, SOFT_RESET1);
	xgene_enet_wr_mac(p, MAC_CONFIG_1_ADDR, 0);
}

static void xgene_sgmac_set_mac_addr(struct xgene_enet_pdata *p)
{
	u32 addr0, addr1;
	u8 *dev_addr = p->ndev->dev_addr;

	addr0 = (dev_addr[3] << 24) | (dev_addr[2] << 16) |
		(dev_addr[1] << 8) | dev_addr[0];
	xgene_enet_wr_mac(p, STATION_ADDR0_ADDR, addr0);

	addr1 = xgene_enet_rd_mac(p, STATION_ADDR1_ADDR);
	addr1 |= (dev_addr[5] << 24) | (dev_addr[4] << 16);
	xgene_enet_wr_mac(p, STATION_ADDR1_ADDR, addr1);
}

static u32 xgene_enet_link_status(struct xgene_enet_pdata *p)
{
	u32 data;

	data = xgene_mii_phy_read(p, INT_PHY_ADDR,
				  SGMII_BASE_PAGE_ABILITY_ADDR >> 2);

	return data & LINK_UP;
}

static void xgene_sgmac_init(struct xgene_enet_pdata *p)
{
	u32 data, loop = 10;
	u32 offset = p->port_id * 4;

	xgene_sgmac_reset(p);

	/* Enable auto-negotiation */
	xgene_mii_phy_write(p, INT_PHY_ADDR, SGMII_CONTROL_ADDR >> 2, 0x1000);
	xgene_mii_phy_write(p, INT_PHY_ADDR, SGMII_TBI_CONTROL_ADDR >> 2, 0);

	while (loop--) {
		data = xgene_mii_phy_read(p, INT_PHY_ADDR,
					  SGMII_STATUS_ADDR >> 2);
		if ((data & AUTO_NEG_COMPLETE) && (data & LINK_STATUS))
			break;
		usleep_range(10, 20);
	}
	if (!(data & AUTO_NEG_COMPLETE) || !(data & LINK_STATUS))
		netdev_err(p->ndev, "Auto-negotiation failed\n");

	data = xgene_enet_rd_mac(p, MAC_CONFIG_2_ADDR);
	ENET_INTERFACE_MODE2_SET(&data, 2);
	xgene_enet_wr_mac(p, MAC_CONFIG_2_ADDR, data | FULL_DUPLEX2);
	xgene_enet_wr_mac(p, INTERFACE_CONTROL_ADDR, ENET_GHD_MODE);

	data = xgene_enet_rd_csr(p, ENET_SPARE_CFG_REG_ADDR);
	data |= MPA_IDLE_WITH_QMI_EMPTY;
	xgene_enet_wr_csr(p, ENET_SPARE_CFG_REG_ADDR, data);

	xgene_sgmac_set_mac_addr(p);

	data = xgene_enet_rd_csr(p, DEBUG_REG_ADDR);
	data |= CFG_BYPASS_UNISEC_TX | CFG_BYPASS_UNISEC_RX;
	xgene_enet_wr_csr(p, DEBUG_REG_ADDR, data);

	/* Adjust MDC clock frequency */
	data = xgene_enet_rd_mac(p, MII_MGMT_CONFIG_ADDR);
	MGMT_CLOCK_SEL_SET(&data, 7);
	xgene_enet_wr_mac(p, MII_MGMT_CONFIG_ADDR, data);

	/* Enable drop if bufpool not available */
	data = xgene_enet_rd_csr(p, RSIF_CONFIG_REG_ADDR);
	data |= CFG_RSIF_FPBUFF_TIMEOUT_EN;
	xgene_enet_wr_csr(p, RSIF_CONFIG_REG_ADDR, data);

	/* Rtype should be copied from FP */
	xgene_enet_wr_csr(p, RSIF_RAM_DBG_REG0_ADDR, 0);

	/* Bypass traffic gating */
	xgene_enet_wr_csr(p, CFG_LINK_AGGR_RESUME_0_ADDR + offset, TX_PORT0);
	xgene_enet_wr_csr(p, CFG_BYPASS_ADDR, RESUME_TX);
	xgene_enet_wr_csr(p, SG_RX_DV_GATE_REG_0_ADDR + offset, RESUME_RX0);
}

static void xgene_sgmac_rxtx(struct xgene_enet_pdata *p, u32 bits, bool set)
{
	u32 data;

	data = xgene_enet_rd_mac(p, MAC_CONFIG_1_ADDR);

	if (set)
		data |= bits;
	else
		data &= ~bits;

	xgene_enet_wr_mac(p, MAC_CONFIG_1_ADDR, data);
}

static void xgene_sgmac_rx_enable(struct xgene_enet_pdata *p)
{
	xgene_sgmac_rxtx(p, RX_EN, true);
}

static void xgene_sgmac_tx_enable(struct xgene_enet_pdata *p)
{
	xgene_sgmac_rxtx(p, TX_EN, true);
}

static void xgene_sgmac_rx_disable(struct xgene_enet_pdata *p)
{
	xgene_sgmac_rxtx(p, RX_EN, false);
}

static void xgene_sgmac_tx_disable(struct xgene_enet_pdata *p)
{
	xgene_sgmac_rxtx(p, TX_EN, false);
}

static int xgene_enet_reset(struct xgene_enet_pdata *p)
{
	if (!xgene_ring_mgr_init(p))
		return -ENODEV;

	clk_prepare_enable(p->clk);
	clk_disable_unprepare(p->clk);
	clk_prepare_enable(p->clk);

	xgene_enet_ecc_init(p);
	xgene_enet_config_ring_if_assoc(p);

	return 0;
}

static void xgene_enet_cle_bypass(struct xgene_enet_pdata *p,
				  u32 dst_ring_num, u16 bufpool_id)
{
	u32 data, fpsel;
	u32 offset = p->port_id * MAC_OFFSET;

	data = CFG_CLE_BYPASS_EN0;
	xgene_enet_wr_csr(p, CLE_BYPASS_REG0_0_ADDR + offset, data);

	fpsel = xgene_enet_ring_bufnum(bufpool_id) - 0x20;
	data = CFG_CLE_DSTQID0(dst_ring_num) | CFG_CLE_FPSEL0(fpsel);
	xgene_enet_wr_csr(p, CLE_BYPASS_REG1_0_ADDR + offset, data);
}

static void xgene_enet_shutdown(struct xgene_enet_pdata *p)
{
	clk_disable_unprepare(p->clk);
}

static void xgene_enet_link_state(struct work_struct *work)
{
	struct xgene_enet_pdata *p = container_of(to_delayed_work(work),
				     struct xgene_enet_pdata, link_work);
	struct net_device *ndev = p->ndev;
	u32 link, poll_interval;

	link = xgene_enet_link_status(p);
	if (link) {
		if (!netif_carrier_ok(ndev)) {
			netif_carrier_on(ndev);
			xgene_sgmac_init(p);
			xgene_sgmac_rx_enable(p);
			xgene_sgmac_tx_enable(p);
			netdev_info(ndev, "Link is Up - 1Gbps\n");
		}
		poll_interval = PHY_POLL_LINK_ON;
	} else {
		if (netif_carrier_ok(ndev)) {
			xgene_sgmac_rx_disable(p);
			xgene_sgmac_tx_disable(p);
			netif_carrier_off(ndev);
			netdev_info(ndev, "Link is Down\n");
		}
		poll_interval = PHY_POLL_LINK_OFF;
	}

	schedule_delayed_work(&p->link_work, poll_interval);
}

struct xgene_mac_ops xgene_sgmac_ops = {
	.init		= xgene_sgmac_init,
	.reset		= xgene_sgmac_reset,
	.rx_enable	= xgene_sgmac_rx_enable,
	.tx_enable	= xgene_sgmac_tx_enable,
	.rx_disable	= xgene_sgmac_rx_disable,
	.tx_disable	= xgene_sgmac_tx_disable,
	.set_mac_addr	= xgene_sgmac_set_mac_addr,
	.link_state	= xgene_enet_link_state
};

struct xgene_port_ops xgene_sgport_ops = {
	.reset		= xgene_enet_reset,
	.cle_bypass	= xgene_enet_cle_bypass,
	.shutdown	= xgene_enet_shutdown
};
