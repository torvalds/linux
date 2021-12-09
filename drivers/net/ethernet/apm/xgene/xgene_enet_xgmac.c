// SPDX-License-Identifier: GPL-2.0-or-later
/* Applied Micro X-Gene SoC Ethernet Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Authors: Iyappan Subramanian <isubramanian@apm.com>
 *	    Keyur Chudgar <kchudgar@apm.com>
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

static void xgene_enet_wr_axg_csr(struct xgene_enet_pdata *pdata,
				  u32 offset, u32 val)
{
	void __iomem *addr = pdata->mcx_mac_csr_addr + offset;

	iowrite32(val, addr);
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

static void xgene_enet_rd_axg_csr(struct xgene_enet_pdata *pdata,
				  u32 offset, u32 *val)
{
	void __iomem *addr = pdata->mcx_mac_csr_addr + offset;

	*val = ioread32(addr);
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

static void xgene_xgmac_get_drop_cnt(struct xgene_enet_pdata *pdata,
				     u32 *rx, u32 *tx)
{
	u32 count;

	xgene_enet_rd_axg_csr(pdata, XGENET_ICM_ECM_DROP_COUNT_REG0, &count);
	*rx = ICM_DROP_COUNT(count);
	*tx = ECM_DROP_COUNT(count);
	/* Errata: 10GE_4 - ICM_ECM_DROP_COUNT not clear-on-read */
	xgene_enet_rd_axg_csr(pdata, XGENET_ECM_CONFIG0_REG_0, &count);
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
	const u8 *dev_addr = pdata->ndev->dev_addr;
	u32 addr0, addr1;

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

static void xgene_xgmac_set_frame_size(struct xgene_enet_pdata *pdata, int size)
{
	xgene_enet_wr_mac(pdata, HSTMAXFRAME_LENGTH_ADDR,
			  ((((size + 2) >> 2) << 16) | size));
}

static u32 xgene_enet_link_status(struct xgene_enet_pdata *pdata)
{
	u32 data;

	xgene_enet_rd_csr(pdata, XG_LINK_STATUS_ADDR, &data);

	return data;
}

static void xgene_xgmac_enable_tx_pause(struct xgene_enet_pdata *pdata,
					bool enable)
{
	u32 data;

	xgene_enet_rd_axg_csr(pdata, XGENET_CSR_ECM_CFG_0_ADDR, &data);

	if (enable)
		data |= MULTI_DPF_AUTOCTRL | PAUSE_XON_EN;
	else
		data &= ~(MULTI_DPF_AUTOCTRL | PAUSE_XON_EN);

	xgene_enet_wr_axg_csr(pdata, XGENET_CSR_ECM_CFG_0_ADDR, data);
}

static void xgene_xgmac_flowctl_tx(struct xgene_enet_pdata *pdata, bool enable)
{
	u32 data;

	data = xgene_enet_rd_mac(pdata, AXGMAC_CONFIG_1);

	if (enable)
		data |= HSTTCTLEN;
	else
		data &= ~HSTTCTLEN;

	xgene_enet_wr_mac(pdata, AXGMAC_CONFIG_1, data);

	pdata->mac_ops->enable_tx_pause(pdata, enable);
}

static void xgene_xgmac_flowctl_rx(struct xgene_enet_pdata *pdata, bool enable)
{
	u32 data;

	data = xgene_enet_rd_mac(pdata, AXGMAC_CONFIG_1);

	if (enable)
		data |= HSTRCTLEN;
	else
		data &= ~HSTRCTLEN;

	xgene_enet_wr_mac(pdata, AXGMAC_CONFIG_1, data);
}

static void xgene_xgmac_init(struct xgene_enet_pdata *pdata)
{
	u32 data;

	xgene_xgmac_reset(pdata);

	data = xgene_enet_rd_mac(pdata, AXGMAC_CONFIG_1);
	data |= HSTPPEN;
	data &= ~HSTLENCHK;
	xgene_enet_wr_mac(pdata, AXGMAC_CONFIG_1, data);

	xgene_xgmac_set_mac_addr(pdata);

	xgene_enet_rd_csr(pdata, XG_RSIF_CONFIG_REG_ADDR, &data);
	data |= CFG_RSIF_FPBUFF_TIMEOUT_EN;
	/* Errata 10GE_1 - FIFO threshold default value incorrect */
	RSIF_CLE_BUFF_THRESH_SET(&data, XG_RSIF_CLE_BUFF_THRESH);
	xgene_enet_wr_csr(pdata, XG_RSIF_CONFIG_REG_ADDR, data);

	/* Errata 10GE_1 - FIFO threshold default value incorrect */
	xgene_enet_rd_csr(pdata, XG_RSIF_CONFIG1_REG_ADDR, &data);
	RSIF_PLC_CLE_BUFF_THRESH_SET(&data, XG_RSIF_PLC_CLE_BUFF_THRESH);
	xgene_enet_wr_csr(pdata, XG_RSIF_CONFIG1_REG_ADDR, data);

	xgene_enet_rd_csr(pdata, XG_ENET_SPARE_CFG_REG_ADDR, &data);
	data |= BIT(12);
	xgene_enet_wr_csr(pdata, XG_ENET_SPARE_CFG_REG_ADDR, data);
	xgene_enet_wr_csr(pdata, XG_ENET_SPARE_CFG_REG_1_ADDR, 0x82);
	xgene_enet_wr_csr(pdata, XGENET_RX_DV_GATE_REG_0_ADDR, 0);
	xgene_enet_wr_csr(pdata, XG_CFG_BYPASS_ADDR, RESUME_TX);

	/* Configure HW pause frame generation */
	xgene_enet_rd_axg_csr(pdata, XGENET_CSR_MULTI_DPF0_ADDR, &data);
	data = (DEF_QUANTA << 16) | (data & 0xFFFF);
	xgene_enet_wr_axg_csr(pdata, XGENET_CSR_MULTI_DPF0_ADDR, data);

	if (pdata->enet_id != XGENE_ENET1) {
		xgene_enet_rd_axg_csr(pdata, XGENET_CSR_MULTI_DPF1_ADDR, &data);
		data = (NORM_PAUSE_OPCODE << 16) | (data & 0xFFFF);
		xgene_enet_wr_axg_csr(pdata, XGENET_CSR_MULTI_DPF1_ADDR, data);
	}

	data = (XG_DEF_PAUSE_OFF_THRES << 16) | XG_DEF_PAUSE_THRES;
	xgene_enet_wr_csr(pdata, XG_RXBUF_PAUSE_THRESH, data);

	xgene_xgmac_flowctl_tx(pdata, pdata->tx_pause);
	xgene_xgmac_flowctl_rx(pdata, pdata->rx_pause);
}

static void xgene_xgmac_rx_enable(struct xgene_enet_pdata *pdata)
{
	u32 data;

	data = xgene_enet_rd_mac(pdata, AXGMAC_CONFIG_1);
	xgene_enet_wr_mac(pdata, AXGMAC_CONFIG_1, data | HSTRFEN);
}

static void xgene_xgmac_tx_enable(struct xgene_enet_pdata *pdata)
{
	u32 data;

	data = xgene_enet_rd_mac(pdata, AXGMAC_CONFIG_1);
	xgene_enet_wr_mac(pdata, AXGMAC_CONFIG_1, data | HSTTFEN);
}

static void xgene_xgmac_rx_disable(struct xgene_enet_pdata *pdata)
{
	u32 data;

	data = xgene_enet_rd_mac(pdata, AXGMAC_CONFIG_1);
	xgene_enet_wr_mac(pdata, AXGMAC_CONFIG_1, data & ~HSTRFEN);
}

static void xgene_xgmac_tx_disable(struct xgene_enet_pdata *pdata)
{
	u32 data;

	data = xgene_enet_rd_mac(pdata, AXGMAC_CONFIG_1);
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
		acpi_status status;

		status = acpi_evaluate_object(ACPI_HANDLE(&pdata->pdev->dev),
					      "_RST", NULL, NULL);
		if (ACPI_FAILURE(status)) {
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
				    u32 dst_ring_num, u16 bufpool_id,
				    u16 nxtbufpool_id)
{
	u32 cb, fpsel, nxtfpsel;

	xgene_enet_rd_csr(pdata, XCLE_BYPASS_REG0_ADDR, &cb);
	cb |= CFG_CLE_BYPASS_EN0;
	CFG_CLE_IP_PROTOCOL0_SET(&cb, 3);
	xgene_enet_wr_csr(pdata, XCLE_BYPASS_REG0_ADDR, cb);

	fpsel = xgene_enet_get_fpsel(bufpool_id);
	nxtfpsel = xgene_enet_get_fpsel(nxtbufpool_id);
	xgene_enet_rd_csr(pdata, XCLE_BYPASS_REG1_ADDR, &cb);
	CFG_CLE_DSTQID0_SET(&cb, dst_ring_num);
	CFG_CLE_FPSEL0_SET(&cb, fpsel);
	CFG_CLE_NXTFPSEL0_SET(&cb, nxtfpsel);
	xgene_enet_wr_csr(pdata, XCLE_BYPASS_REG1_ADDR, cb);
	pr_info("+ cle_bypass: fpsel: %d nxtfpsel: %d\n", fpsel, nxtfpsel);
}

static void xgene_enet_shutdown(struct xgene_enet_pdata *pdata)
{
	struct device *dev = &pdata->pdev->dev;

	if (dev->of_node) {
		if (!IS_ERR(pdata->clk))
			clk_disable_unprepare(pdata->clk);
	}
}

static void xgene_enet_clear(struct xgene_enet_pdata *pdata,
			     struct xgene_enet_desc_ring *ring)
{
	u32 addr, data;

	if (xgene_enet_is_bufpool(ring->id)) {
		addr = ENET_CFGSSQMIFPRESET_ADDR;
		data = BIT(xgene_enet_get_fpsel(ring->id));
	} else {
		addr = ENET_CFGSSQMIWQRESET_ADDR;
		data = BIT(xgene_enet_ring_bufnum(ring->id));
	}

	xgene_enet_wr_ring_if(pdata, addr, data);
}

static int xgene_enet_gpio_lookup(struct xgene_enet_pdata *pdata)
{
	struct device *dev = &pdata->pdev->dev;

	pdata->sfp_rdy = gpiod_get(dev, "rxlos", GPIOD_IN);
	if (IS_ERR(pdata->sfp_rdy))
		pdata->sfp_rdy = gpiod_get(dev, "sfp", GPIOD_IN);

	if (IS_ERR(pdata->sfp_rdy))
		return -ENODEV;

	return 0;
}

static void xgene_enet_link_state(struct work_struct *work)
{
	struct xgene_enet_pdata *pdata = container_of(to_delayed_work(work),
					 struct xgene_enet_pdata, link_work);
	struct net_device *ndev = pdata->ndev;
	u32 link_status, poll_interval;

	link_status = xgene_enet_link_status(pdata);
	if (pdata->sfp_gpio_en && link_status &&
	    (!IS_ERR(pdata->sfp_rdy) || !xgene_enet_gpio_lookup(pdata)) &&
	    !gpiod_get_value(pdata->sfp_rdy))
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
	.set_framesize = xgene_xgmac_set_frame_size,
	.set_mss = xgene_xgmac_set_mss,
	.get_drop_cnt = xgene_xgmac_get_drop_cnt,
	.link_state = xgene_enet_link_state,
	.enable_tx_pause = xgene_xgmac_enable_tx_pause,
	.flowctl_rx = xgene_xgmac_flowctl_rx,
	.flowctl_tx = xgene_xgmac_flowctl_tx
};

const struct xgene_port_ops xgene_xgport_ops = {
	.reset = xgene_enet_reset,
	.clear = xgene_enet_clear,
	.cle_bypass = xgene_enet_xgcle_bypass,
	.shutdown = xgene_enet_shutdown,
};
