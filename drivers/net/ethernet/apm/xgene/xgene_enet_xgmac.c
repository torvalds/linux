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

#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include "xgene_enet_main.h"
#include "xgene_enet_hw.h"
#include "xgene_enet_xgmac.h"

static void xgene_enet_wr_csr(struct xgene_enet_pdata *pdata,
			      u32 offset, u32 val)
{
	void __iomem *addr = pdata->eth_csr_addr + offset;

	iowrite32(val, addr);
}

static void xgene_enet_wr_ring_if(struct xgene_enet_pdata *pdata,
				  u32 offset, u32 val)
{
	void __iomem *addr = pdata->eth_ring_if_addr + offset;

	iowrite32(val, addr);
}

static void xgene_enet_wr_diag_csr(struct xgene_enet_pdata *pdata,
				   u32 offset, u32 val)
{
	void __iomem *addr = pdata->eth_diag_csr_addr + offset;

	iowrite32(val, addr);
}

static bool xgene_enet_wr_indirect(void __iomem *addr, void __iomem *wr,
				   void __iomem *cmd, void __iomem *cmd_done,
				   u32 wr_addr, u32 wr_data)
{
	u32 done;
	u8 wait = 10;

	iowrite32(wr_addr, addr);
	iowrite32(wr_data, wr);
	iowrite32(XGENE_ENET_WR_CMD, cmd);

	/* wait for write command to complete */
	while (!(done = ioread32(cmd_done)) && wait--)
		udelay(1);

	if (!done)
		return false;

	iowrite32(0, cmd);

	return true;
}

static void xgene_enet_wr_mac(struct xgene_enet_pdata *pdata,
			      u32 wr_addr, u32 wr_data)
{
	void __iomem *addr, *wr, *cmd, *cmd_done;

	addr = pdata->mcx_mac_addr + MAC_ADDR_REG_OFFSET;
	wr = pdata->mcx_mac_addr + MAC_WRITE_REG_OFFSET;
	cmd = pdata->mcx_mac_addr + MAC_COMMAND_REG_OFFSET;
	cmd_done = pdata->mcx_mac_addr + MAC_COMMAND_DONE_REG_OFFSET;

	if (!xgene_enet_wr_indirect(addr, wr, cmd, cmd_done, wr_addr, wr_data))
		netdev_err(pdata->ndev, "MCX mac write failed, addr: %04x\n",
			   wr_addr);
}

static void xgene_enet_wr_pcs(struct xgene_enet_pdata *pdata,
			      u32 wr_addr, u32 wr_data)
{
	void __iomem *addr, *wr, *cmd, *cmd_done;

	addr = pdata->pcs_addr + PCS_ADDR_REG_OFFSET;
	wr = pdata->pcs_addr + PCS_WRITE_REG_OFFSET;
	cmd = pdata->pcs_addr + PCS_COMMAND_REG_OFFSET;
	cmd_done = pdata->pcs_addr + PCS_COMMAND_DONE_REG_OFFSET;

	if (!xgene_enet_wr_indirect(addr, wr, cmd, cmd_done, wr_addr, wr_data))
		netdev_err(pdata->ndev, "PCS write failed, addr: %04x\n",
			   wr_addr);
}

static void xgene_enet_rd_csr(struct xgene_enet_pdata *pdata,
			      u32 offset, u32 *val)
{
	void __iomem *addr = pdata->eth_csr_addr + offset;

	*val = ioread32(addr);
}

static void xgene_enet_rd_diag_csr(struct xgene_enet_pdata *pdata,
				   u32 offset, u32 *val)
{
	void __iomem *addr = pdata->eth_diag_csr_addr + offset;

	*val = ioread32(addr);
}

static bool xgene_enet_rd_indirect(void __iomem *addr, void __iomem *rd,
				   void __iomem *cmd, void __iomem *cmd_done,
				   u32 rd_addr, u32 *rd_data)
{
	u32 done;
	u8 wait = 10;

	iowrite32(rd_addr, addr);
	iowrite32(XGENE_ENET_RD_CMD, cmd);

	/* wait for read command to complete */
	while (!(done = ioread32(cmd_done)) && wait--)
		udelay(1);

	if (!done)
		return false;

	*rd_data = ioread32(rd);
	iowrite32(0, cmd);

	return true;
}

static void xgene_enet_rd_mac(struct xgene_enet_pdata *pdata,
			      u32 rd_addr, u32 *rd_data)
{
	void __iomem *addr, *rd, *cmd, *cmd_done;

	addr = pdata->mcx_mac_addr + MAC_ADDR_REG_OFFSET;
	rd = pdata->mcx_mac_addr + MAC_READ_REG_OFFSET;
	cmd = pdata->mcx_mac_addr + MAC_COMMAND_REG_OFFSET;
	cmd_done = pdata->mcx_mac_addr + MAC_COMMAND_DONE_REG_OFFSET;

	if (!xgene_enet_rd_indirect(addr, rd, cmd, cmd_done, rd_addr, rd_data))
		netdev_err(pdata->ndev, "MCX mac read failed, addr: %04x\n",
			   rd_addr);
}

static bool xgene_enet_rd_pcs(struct xgene_enet_pdata *pdata,
			      u32 rd_addr, u32 *rd_data)
{
	void __iomem *addr, *rd, *cmd, *cmd_done;
	bool success;

	addr = pdata->pcs_addr + PCS_ADDR_REG_OFFSET;
	rd = pdata->pcs_addr + PCS_READ_REG_OFFSET;
	cmd = pdata->pcs_addr + PCS_COMMAND_REG_OFFSET;
	cmd_done = pdata->pcs_addr + PCS_COMMAND_DONE_REG_OFFSET;

	success = xgene_enet_rd_indirect(addr, rd, cmd, cmd_done, rd_addr, rd_data);
	if (!success)
		netdev_err(pdata->ndev, "PCS read failed, addr: %04x\n",
			   rd_addr);

	return success;
}

static int xgene_enet_ecc_init(struct xgene_enet_pdata *pdata)
{
	struct net_device *ndev = pdata->ndev;
	u32 data;
	u8 wait = 10;

	xgene_enet_wr_diag_csr(pdata, ENET_CFG_MEM_RAM_SHUTDOWN_ADDR, 0x0);
	do {
		usleep_range(100, 110);
		xgene_enet_rd_diag_csr(pdata, ENET_BLOCK_MEM_RDY_ADDR, &data);
	} while ((data != 0xffffffff) && wait--);

	if (data != 0xffffffff) {
		netdev_err(ndev, "Failed to release memory from shutdown\n");
		return -ENODEV;
	}

	return 0;
}

static void xgene_enet_config_ring_if_assoc(struct xgene_enet_pdata *pdata)
{
	xgene_enet_wr_ring_if(pdata, ENET_CFGSSQMIWQASSOC_ADDR, 0);
	xgene_enet_wr_ring_if(pdata, ENET_CFGSSQMIFPQASSOC_ADDR, 0);
	xgene_enet_wr_ring_if(pdata, ENET_CFGSSQMIQMLITEWQASSOC_ADDR, 0);
	xgene_enet_wr_ring_if(pdata, ENET_CFGSSQMIQMLITEFPQASSOC_ADDR, 0);
}

static void xgene_xgmac_reset(struct xgene_enet_pdata *pdata)
{
	xgene_enet_wr_mac(pdata, AXGMAC_CONFIG_0, HSTMACRST);
	xgene_enet_wr_mac(pdata, AXGMAC_CONFIG_0, 0);
}

static void xgene_pcs_reset(struct xgene_enet_pdata *pdata)
{
	u32 data;

	if (!xgene_enet_rd_pcs(pdata, PCS_CONTROL_1, &data))
		return;

	xgene_enet_wr_pcs(pdata, PCS_CONTROL_1, data | PCS_CTRL_PCS_RST);
	xgene_enet_wr_pcs(pdata, PCS_CONTROL_1, data & ~PCS_CTRL_PCS_RST);
}

static void xgene_xgmac_set_mac_addr(struct xgene_enet_pdata *pdata)
{
	u32 addr0, addr1;
	u8 *dev_addr = pdata->ndev->dev_addr;

	addr0 = (dev_addr[3] << 24) | (dev_addr[2] << 16) |
		(dev_addr[1] << 8) | dev_addr[0];
	addr1 = (dev_addr[5] << 24) | (dev_addr[4] << 16);

	xgene_enet_wr_mac(pdata, HSTMACADR_LSW_ADDR, addr0);
	xgene_enet_wr_mac(pdata, HSTMACADR_MSW_ADDR, addr1);
}

static void xgene_xgmac_set_mss(struct xgene_enet_pdata *pdata,
				u16 mss, u8 index)
{
	u8 offset;
	u32 data;

	offset = (index < 2) ? 0 : 4;
	xgene_enet_rd_csr(pdata, XG_TSIF_MSS_REG0_ADDR + offset, &data);

	if (!(index & 0x1))
		data = SET_VAL(TSO_MSS1, data >> TSO_MSS1_POS) |
			SET_VAL(TSO_MSS0, mss);
	else
		data = SET_VAL(TSO_MSS1, mss) | SET_VAL(TSO_MSS0, data);

	xgene_enet_wr_csr(pdata, XG_TSIF_MSS_REG0_ADDR + offset, data);
}

static u32 xgene_enet_link_status(struct xgene_enet_pdata *pdata)
{
	u32 data;

	xgene_enet_rd_csr(pdata, XG_LINK_STATUS_ADDR, &data);

	return data;
}

static void xgene_xgmac_init(struct xgene_enet_pdata *pdata)
{
	u32 data;

	xgene_xgmac_reset(pdata);

	xgene_enet_rd_mac(pdata, AXGMAC_CONFIG_1, &data);
	data |= HSTPPEN;
	data &= ~HSTLENCHK;
	xgene_enet_wr_mac(pdata, AXGMAC_CONFIG_1, data);

	xgene_xgmac_set_mac_addr(pdata);

	xgene_enet_rd_csr(pdata, XG_RSIF_CONFIG_REG_ADDR, &data);
	data |= CFG_RSIF_FPBUFF_TIMEOUT_EN;
	xgene_enet_wr_csr(pdata, XG_RSIF_CONFIG_REG_ADDR, data);

	xgene_enet_rd_csr(pdata, XG_ENET_SPARE_CFG_REG_ADDR, &data);
	data |= BIT(12);
	xgene_enet_wr_csr(pdata, XG_ENET_SPARE_CFG_REG_ADDR, data);
	xgene_enet_wr_csr(pdata, XG_ENET_SPARE_CFG_REG_1_ADDR, 0x82);
	xgene_enet_wr_csr(pdata, XGENET_RX_DV_GATE_REG_0_ADDR, 0);
	xgene_enet_wr_csr(pdata, XG_CFG_BYPASS_ADDR, RESUME_TX);
}

static void xgene_xgmac_rx_enable(struct xgene_enet_pdata *pdata)
{
	u32 data;

	xgene_enet_rd_mac(pdata, AXGMAC_CONFIG_1, &data);
	xgene_enet_wr_mac(pdata, AXGMAC_CONFIG_1, data | HSTRFEN);
}

static void xgene_xgmac_tx_enable(struct xgene_enet_pdata *pdata)
{
	u32 data;

	xgene_enet_rd_mac(pdata, AXGMAC_CONFIG_1, &data);
	xgene_enet_wr_mac(pdata, AXGMAC_CONFIG_1, data | HSTTFEN);
}

static void xgene_xgmac_rx_disable(struct xgene_enet_pdata *pdata)
{
	u32 data;

	xgene_enet_rd_mac(pdata, AXGMAC_CONFIG_1, &data);
	xgene_enet_wr_mac(pdata, AXGMAC_CONFIG_1, data & ~HSTRFEN);
}

static void xgene_xgmac_tx_disable(struct xgene_enet_pdata *pdata)
{
	u32 data;

	xgene_enet_rd_mac(pdata, AXGMAC_CONFIG_1, &data);
	xgene_enet_wr_mac(pdata, AXGMAC_CONFIG_1, data & ~HSTTFEN);
}

static int xgene_enet_reset(struct xgene_enet_pdata *pdata)
{
	struct device *dev = &pdata->pdev->dev;

	if (!xgene_ring_mgr_init(pdata))
		return -ENODEV;

	if (dev->of_node) {
		clk_prepare_enable(pdata->clk);
		udelay(5);
		clk_disable_unprepare(pdata->clk);
		udelay(5);
		clk_prepare_enable(pdata->clk);
		udelay(5);
	} else {
#ifdef CONFIG_ACPI
		if (acpi_has_method(ACPI_HANDLE(&pdata->pdev->dev), "_RST")) {
			acpi_evaluate_object(ACPI_HANDLE(&pdata->pdev->dev),
					     "_RST", NULL, NULL);
		} else if (acpi_has_method(ACPI_HANDLE(&pdata->pdev->dev),
					   "_INI")) {
			acpi_evaluate_object(ACPI_HANDLE(&pdata->pdev->dev),
					     "_INI", NULL, NULL);
		}
#endif
	}

	xgene_enet_ecc_init(pdata);
	xgene_enet_config_ring_if_assoc(pdata);

	return 0;
}

static void xgene_enet_xgcle_bypass(struct xgene_enet_pdata *pdata,
				    u32 dst_ring_num, u16 bufpool_id)
{
	u32 cb, fpsel;

	xgene_enet_rd_csr(pdata, XCLE_BYPASS_REG0_ADDR, &cb);
	cb |= CFG_CLE_BYPASS_EN0;
	CFG_CLE_IP_PROTOCOL0_SET(&cb, 3);
	xgene_enet_wr_csr(pdata, XCLE_BYPASS_REG0_ADDR, cb);

	fpsel = xgene_enet_ring_bufnum(bufpool_id) - 0x20;
	xgene_enet_rd_csr(pdata, XCLE_BYPASS_REG1_ADDR, &cb);
	CFG_CLE_DSTQID0_SET(&cb, dst_ring_num);
	CFG_CLE_FPSEL0_SET(&cb, fpsel);
	xgene_enet_wr_csr(pdata, XCLE_BYPASS_REG1_ADDR, cb);
}

static void xgene_enet_shutdown(struct xgene_enet_pdata *pdata)
{
	struct device *dev = &pdata->pdev->dev;
	struct xgene_enet_desc_ring *ring;
	u32 pb, val;
	int i;

	pb = 0;
	for (i = 0; i < pdata->rxq_cnt; i++) {
		ring = pdata->rx_ring[i]->buf_pool;

		val = xgene_enet_ring_bufnum(ring->id);
		pb |= BIT(val - 0x20);
	}
	xgene_enet_wr_ring_if(pdata, ENET_CFGSSQMIFPRESET_ADDR, pb);

	pb = 0;
	for (i = 0; i < pdata->txq_cnt; i++) {
		ring = pdata->tx_ring[i];

		val = xgene_enet_ring_bufnum(ring->id);
		pb |= BIT(val);
	}
	xgene_enet_wr_ring_if(pdata, ENET_CFGSSQMIWQRESET_ADDR, pb);

	if (dev->of_node) {
		if (!IS_ERR(pdata->clk))
			clk_disable_unprepare(pdata->clk);
	}
}

static void xgene_enet_clear(struct xgene_enet_pdata *pdata,
			     struct xgene_enet_desc_ring *ring)
{
	u32 addr, val, data;

	val = xgene_enet_ring_bufnum(ring->id);

	if (xgene_enet_is_bufpool(ring->id)) {
		addr = ENET_CFGSSQMIFPRESET_ADDR;
		data = BIT(val - 0x20);
	} else {
		addr = ENET_CFGSSQMIWQRESET_ADDR;
		data = BIT(val);
	}

	xgene_enet_wr_ring_if(pdata, addr, data);
}

static void xgene_enet_link_state(struct work_struct *work)
{
	struct xgene_enet_pdata *pdata = container_of(to_delayed_work(work),
					 struct xgene_enet_pdata, link_work);
	struct gpio_desc *sfp_rdy = pdata->sfp_rdy;
	struct net_device *ndev = pdata->ndev;
	u32 link_status, poll_interval;

	link_status = xgene_enet_link_status(pdata);
	if (link_status && !IS_ERR(sfp_rdy) && !gpiod_get_value(sfp_rdy))
		link_status = 0;

	if (link_status) {
		if (!netif_carrier_ok(ndev)) {
			netif_carrier_on(ndev);
			xgene_xgmac_rx_enable(pdata);
			xgene_xgmac_tx_enable(pdata);
			netdev_info(ndev, "Link is Up - 10Gbps\n");
		}
		poll_interval = PHY_POLL_LINK_ON;
	} else {
		if (netif_carrier_ok(ndev)) {
			xgene_xgmac_rx_disable(pdata);
			xgene_xgmac_tx_disable(pdata);
			netif_carrier_off(ndev);
			netdev_info(ndev, "Link is Down\n");
		}
		poll_interval = PHY_POLL_LINK_OFF;

		xgene_pcs_reset(pdata);
	}

	schedule_delayed_work(&pdata->link_work, poll_interval);
}

const struct xgene_mac_ops xgene_xgmac_ops = {
	.init = xgene_xgmac_init,
	.reset = xgene_xgmac_reset,
	.rx_enable = xgene_xgmac_rx_enable,
	.tx_enable = xgene_xgmac_tx_enable,
	.rx_disable = xgene_xgmac_rx_disable,
	.tx_disable = xgene_xgmac_tx_disable,
	.set_mac_addr = xgene_xgmac_set_mac_addr,
	.set_mss = xgene_xgmac_set_mss,
	.link_state = xgene_enet_link_state
};

const struct xgene_port_ops xgene_xgport_ops = {
	.reset = xgene_enet_reset,
	.clear = xgene_enet_clear,
	.cle_bypass = xgene_enet_xgcle_bypass,
	.shutdown = xgene_enet_shutdown,
};
