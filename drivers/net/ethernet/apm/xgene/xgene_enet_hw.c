/* Applied Micro X-Gene SoC Ethernet Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Authors: Iyappan Subramanian <isubramanian@apm.com>
 *	    Ravi Patel <rapatel@apm.com>
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

static void xgene_enet_ring_init(struct xgene_enet_desc_ring *ring)
{
	u32 *ring_cfg = ring->state;
	u64 addr = ring->dma;
	enum xgene_enet_ring_cfgsize cfgsize = ring->cfgsize;

	ring_cfg[4] |= (1 << SELTHRSH_POS) &
			CREATE_MASK(SELTHRSH_POS, SELTHRSH_LEN);
	ring_cfg[3] |= ACCEPTLERR;
	ring_cfg[2] |= QCOHERENT;

	addr >>= 8;
	ring_cfg[2] |= (addr << RINGADDRL_POS) &
			CREATE_MASK_ULL(RINGADDRL_POS, RINGADDRL_LEN);
	addr >>= RINGADDRL_LEN;
	ring_cfg[3] |= addr & CREATE_MASK_ULL(RINGADDRH_POS, RINGADDRH_LEN);
	ring_cfg[3] |= ((u32)cfgsize << RINGSIZE_POS) &
			CREATE_MASK(RINGSIZE_POS, RINGSIZE_LEN);
}

static void xgene_enet_ring_set_type(struct xgene_enet_desc_ring *ring)
{
	u32 *ring_cfg = ring->state;
	bool is_bufpool;
	u32 val;

	is_bufpool = xgene_enet_is_bufpool(ring->id);
	val = (is_bufpool) ? RING_BUFPOOL : RING_REGULAR;
	ring_cfg[4] |= (val << RINGTYPE_POS) &
			CREATE_MASK(RINGTYPE_POS, RINGTYPE_LEN);

	if (is_bufpool) {
		ring_cfg[3] |= (BUFPOOL_MODE << RINGMODE_POS) &
				CREATE_MASK(RINGMODE_POS, RINGMODE_LEN);
	}
}

static void xgene_enet_ring_set_recombbuf(struct xgene_enet_desc_ring *ring)
{
	u32 *ring_cfg = ring->state;

	ring_cfg[3] |= RECOMBBUF;
	ring_cfg[3] |= (0xf << RECOMTIMEOUTL_POS) &
			CREATE_MASK(RECOMTIMEOUTL_POS, RECOMTIMEOUTL_LEN);
	ring_cfg[4] |= 0x7 & CREATE_MASK(RECOMTIMEOUTH_POS, RECOMTIMEOUTH_LEN);
}

static void xgene_enet_ring_wr32(struct xgene_enet_desc_ring *ring,
				 u32 offset, u32 data)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ring->ndev);

	iowrite32(data, pdata->ring_csr_addr + offset);
}

static void xgene_enet_ring_rd32(struct xgene_enet_desc_ring *ring,
				 u32 offset, u32 *data)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ring->ndev);

	*data = ioread32(pdata->ring_csr_addr + offset);
}

static void xgene_enet_write_ring_state(struct xgene_enet_desc_ring *ring)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ring->ndev);
	int i;

	xgene_enet_ring_wr32(ring, CSR_RING_CONFIG, ring->num);
	for (i = 0; i < pdata->ring_ops->num_ring_config; i++) {
		xgene_enet_ring_wr32(ring, CSR_RING_WR_BASE + (i * 4),
				     ring->state[i]);
	}
}

static void xgene_enet_clr_ring_state(struct xgene_enet_desc_ring *ring)
{
	memset(ring->state, 0, sizeof(ring->state));
	xgene_enet_write_ring_state(ring);
}

static void xgene_enet_set_ring_state(struct xgene_enet_desc_ring *ring)
{
	xgene_enet_ring_set_type(ring);

	if (xgene_enet_ring_owner(ring->id) == RING_OWNER_ETH0 ||
	    xgene_enet_ring_owner(ring->id) == RING_OWNER_ETH1)
		xgene_enet_ring_set_recombbuf(ring);

	xgene_enet_ring_init(ring);
	xgene_enet_write_ring_state(ring);
}

static void xgene_enet_set_ring_id(struct xgene_enet_desc_ring *ring)
{
	u32 ring_id_val, ring_id_buf;
	bool is_bufpool;

	is_bufpool = xgene_enet_is_bufpool(ring->id);

	ring_id_val = ring->id & GENMASK(9, 0);
	ring_id_val |= OVERWRITE;

	ring_id_buf = (ring->num << 9) & GENMASK(18, 9);
	ring_id_buf |= PREFETCH_BUF_EN;
	if (is_bufpool)
		ring_id_buf |= IS_BUFFER_POOL;

	xgene_enet_ring_wr32(ring, CSR_RING_ID, ring_id_val);
	xgene_enet_ring_wr32(ring, CSR_RING_ID_BUF, ring_id_buf);
}

static void xgene_enet_clr_desc_ring_id(struct xgene_enet_desc_ring *ring)
{
	u32 ring_id;

	ring_id = ring->id | OVERWRITE;
	xgene_enet_ring_wr32(ring, CSR_RING_ID, ring_id);
	xgene_enet_ring_wr32(ring, CSR_RING_ID_BUF, 0);
}

static struct xgene_enet_desc_ring *xgene_enet_setup_ring(
				    struct xgene_enet_desc_ring *ring)
{
	u32 size = ring->size;
	u32 i, data;
	bool is_bufpool;

	xgene_enet_clr_ring_state(ring);
	xgene_enet_set_ring_state(ring);
	xgene_enet_set_ring_id(ring);

	ring->slots = xgene_enet_get_numslots(ring->id, size);

	is_bufpool = xgene_enet_is_bufpool(ring->id);
	if (is_bufpool || xgene_enet_ring_owner(ring->id) != RING_OWNER_CPU)
		return ring;

	for (i = 0; i < ring->slots; i++)
		xgene_enet_mark_desc_slot_empty(&ring->raw_desc[i]);

	xgene_enet_ring_rd32(ring, CSR_RING_NE_INT_MODE, &data);
	data |= BIT(31 - xgene_enet_ring_bufnum(ring->id));
	xgene_enet_ring_wr32(ring, CSR_RING_NE_INT_MODE, data);

	return ring;
}

static void xgene_enet_clear_ring(struct xgene_enet_desc_ring *ring)
{
	u32 data;
	bool is_bufpool;

	is_bufpool = xgene_enet_is_bufpool(ring->id);
	if (is_bufpool || xgene_enet_ring_owner(ring->id) != RING_OWNER_CPU)
		goto out;

	xgene_enet_ring_rd32(ring, CSR_RING_NE_INT_MODE, &data);
	data &= ~BIT(31 - xgene_enet_ring_bufnum(ring->id));
	xgene_enet_ring_wr32(ring, CSR_RING_NE_INT_MODE, data);

out:
	xgene_enet_clr_desc_ring_id(ring);
	xgene_enet_clr_ring_state(ring);
}

static void xgene_enet_wr_cmd(struct xgene_enet_desc_ring *ring, int count)
{
	iowrite32(count, ring->cmd);
}

static u32 xgene_enet_ring_len(struct xgene_enet_desc_ring *ring)
{
	u32 __iomem *cmd_base = ring->cmd_base;
	u32 ring_state, num_msgs;

	ring_state = ioread32(&cmd_base[1]);
	num_msgs = GET_VAL(NUMMSGSINQ, ring_state);

	return num_msgs;
}

void xgene_enet_parse_error(struct xgene_enet_desc_ring *ring,
			    struct xgene_enet_pdata *pdata,
			    enum xgene_enet_err_code status)
{
	switch (status) {
	case INGRESS_CRC:
		ring->rx_crc_errors++;
		ring->rx_dropped++;
		break;
	case INGRESS_CHECKSUM:
	case INGRESS_CHECKSUM_COMPUTE:
		ring->rx_errors++;
		ring->rx_dropped++;
		break;
	case INGRESS_TRUNC_FRAME:
		ring->rx_frame_errors++;
		ring->rx_dropped++;
		break;
	case INGRESS_PKT_LEN:
		ring->rx_length_errors++;
		ring->rx_dropped++;
		break;
	case INGRESS_PKT_UNDER:
		ring->rx_frame_errors++;
		ring->rx_dropped++;
		break;
	case INGRESS_FIFO_OVERRUN:
		ring->rx_fifo_errors++;
		break;
	default:
		break;
	}
}

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

static void xgene_enet_wr_mcx_csr(struct xgene_enet_pdata *pdata,
				  u32 offset, u32 val)
{
	void __iomem *addr = pdata->mcx_mac_csr_addr + offset;

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

static void xgene_enet_wr_mcx_mac(struct xgene_enet_pdata *pdata,
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

static void xgene_enet_rd_mcx_csr(struct xgene_enet_pdata *pdata,
				  u32 offset, u32 *val)
{
	void __iomem *addr = pdata->mcx_mac_csr_addr + offset;

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

static void xgene_enet_rd_mcx_mac(struct xgene_enet_pdata *pdata,
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

static void xgene_gmac_set_mac_addr(struct xgene_enet_pdata *pdata)
{
	u32 addr0, addr1;
	u8 *dev_addr = pdata->ndev->dev_addr;

	addr0 = (dev_addr[3] << 24) | (dev_addr[2] << 16) |
		(dev_addr[1] << 8) | dev_addr[0];
	addr1 = (dev_addr[5] << 24) | (dev_addr[4] << 16);

	xgene_enet_wr_mcx_mac(pdata, STATION_ADDR0_ADDR, addr0);
	xgene_enet_wr_mcx_mac(pdata, STATION_ADDR1_ADDR, addr1);
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

static void xgene_gmac_reset(struct xgene_enet_pdata *pdata)
{
	xgene_enet_wr_mcx_mac(pdata, MAC_CONFIG_1_ADDR, SOFT_RESET1);
	xgene_enet_wr_mcx_mac(pdata, MAC_CONFIG_1_ADDR, 0);
}

static void xgene_enet_configure_clock(struct xgene_enet_pdata *pdata)
{
	struct device *dev = &pdata->pdev->dev;

	if (dev->of_node) {
		struct clk *parent = clk_get_parent(pdata->clk);

		switch (pdata->phy_speed) {
		case SPEED_10:
			clk_set_rate(parent, 2500000);
			break;
		case SPEED_100:
			clk_set_rate(parent, 25000000);
			break;
		default:
			clk_set_rate(parent, 125000000);
			break;
		}
	}
#ifdef CONFIG_ACPI
	else {
		switch (pdata->phy_speed) {
		case SPEED_10:
			acpi_evaluate_object(ACPI_HANDLE(dev),
					     "S10", NULL, NULL);
			break;
		case SPEED_100:
			acpi_evaluate_object(ACPI_HANDLE(dev),
					     "S100", NULL, NULL);
			break;
		default:
			acpi_evaluate_object(ACPI_HANDLE(dev),
					     "S1G", NULL, NULL);
			break;
		}
	}
#endif
}

static void xgene_gmac_set_speed(struct xgene_enet_pdata *pdata)
{
	struct device *dev = &pdata->pdev->dev;
	u32 icm0, icm2, mc2;
	u32 intf_ctl, rgmii, value;

	xgene_enet_rd_mcx_csr(pdata, ICM_CONFIG0_REG_0_ADDR, &icm0);
	xgene_enet_rd_mcx_csr(pdata, ICM_CONFIG2_REG_0_ADDR, &icm2);
	xgene_enet_rd_mcx_mac(pdata, MAC_CONFIG_2_ADDR, &mc2);
	xgene_enet_rd_mcx_mac(pdata, INTERFACE_CONTROL_ADDR, &intf_ctl);
	xgene_enet_rd_csr(pdata, RGMII_REG_0_ADDR, &rgmii);

	switch (pdata->phy_speed) {
	case SPEED_10:
		ENET_INTERFACE_MODE2_SET(&mc2, 1);
		intf_ctl &= ~(ENET_LHD_MODE | ENET_GHD_MODE);
		CFG_MACMODE_SET(&icm0, 0);
		CFG_WAITASYNCRD_SET(&icm2, 500);
		rgmii &= ~CFG_SPEED_1250;
		break;
	case SPEED_100:
		ENET_INTERFACE_MODE2_SET(&mc2, 1);
		intf_ctl &= ~ENET_GHD_MODE;
		intf_ctl |= ENET_LHD_MODE;
		CFG_MACMODE_SET(&icm0, 1);
		CFG_WAITASYNCRD_SET(&icm2, 80);
		rgmii &= ~CFG_SPEED_1250;
		break;
	default:
		ENET_INTERFACE_MODE2_SET(&mc2, 2);
		intf_ctl &= ~ENET_LHD_MODE;
		intf_ctl |= ENET_GHD_MODE;
		CFG_MACMODE_SET(&icm0, 2);
		CFG_WAITASYNCRD_SET(&icm2, 0);
		if (dev->of_node) {
			CFG_TXCLK_MUXSEL0_SET(&rgmii, pdata->tx_delay);
			CFG_RXCLK_MUXSEL0_SET(&rgmii, pdata->rx_delay);
		}
		rgmii |= CFG_SPEED_1250;

		xgene_enet_rd_csr(pdata, DEBUG_REG_ADDR, &value);
		value |= CFG_BYPASS_UNISEC_TX | CFG_BYPASS_UNISEC_RX;
		xgene_enet_wr_csr(pdata, DEBUG_REG_ADDR, value);
		break;
	}

	mc2 |= FULL_DUPLEX2 | PAD_CRC | LENGTH_CHK;
	xgene_enet_wr_mcx_mac(pdata, MAC_CONFIG_2_ADDR, mc2);
	xgene_enet_wr_mcx_mac(pdata, INTERFACE_CONTROL_ADDR, intf_ctl);
	xgene_enet_wr_csr(pdata, RGMII_REG_0_ADDR, rgmii);
	xgene_enet_configure_clock(pdata);

	xgene_enet_wr_mcx_csr(pdata, ICM_CONFIG0_REG_0_ADDR, icm0);
	xgene_enet_wr_mcx_csr(pdata, ICM_CONFIG2_REG_0_ADDR, icm2);
}

static void xgene_enet_set_frame_size(struct xgene_enet_pdata *pdata, int size)
{
	xgene_enet_wr_mcx_mac(pdata, MAX_FRAME_LEN_ADDR, size);
}

static void xgene_gmac_enable_tx_pause(struct xgene_enet_pdata *pdata,
				       bool enable)
{
	u32 data;

	xgene_enet_rd_mcx_csr(pdata, CSR_ECM_CFG_0_ADDR, &data);

	if (enable)
		data |= MULTI_DPF_AUTOCTRL | PAUSE_XON_EN;
	else
		data &= ~(MULTI_DPF_AUTOCTRL | PAUSE_XON_EN);

	xgene_enet_wr_mcx_csr(pdata, CSR_ECM_CFG_0_ADDR, data);
}

static void xgene_gmac_flowctl_tx(struct xgene_enet_pdata *pdata, bool enable)
{
	u32 data;

	xgene_enet_rd_mcx_mac(pdata, MAC_CONFIG_1_ADDR, &data);

	if (enable)
		data |= TX_FLOW_EN;
	else
		data &= ~TX_FLOW_EN;

	xgene_enet_wr_mcx_mac(pdata, MAC_CONFIG_1_ADDR, data);

	pdata->mac_ops->enable_tx_pause(pdata, enable);
}

static void xgene_gmac_flowctl_rx(struct xgene_enet_pdata *pdata, bool enable)
{
	u32 data;

	xgene_enet_rd_mcx_mac(pdata, MAC_CONFIG_1_ADDR, &data);

	if (enable)
		data |= RX_FLOW_EN;
	else
		data &= ~RX_FLOW_EN;

	xgene_enet_wr_mcx_mac(pdata, MAC_CONFIG_1_ADDR, data);
}

static void xgene_gmac_init(struct xgene_enet_pdata *pdata)
{
	u32 value;

	if (!pdata->mdio_driver)
		xgene_gmac_reset(pdata);

	xgene_gmac_set_speed(pdata);
	xgene_gmac_set_mac_addr(pdata);

	/* Adjust MDC clock frequency */
	xgene_enet_rd_mcx_mac(pdata, MII_MGMT_CONFIG_ADDR, &value);
	MGMT_CLOCK_SEL_SET(&value, 7);
	xgene_enet_wr_mcx_mac(pdata, MII_MGMT_CONFIG_ADDR, value);

	/* Enable drop if bufpool not available */
	xgene_enet_rd_csr(pdata, RSIF_CONFIG_REG_ADDR, &value);
	value |= CFG_RSIF_FPBUFF_TIMEOUT_EN;
	xgene_enet_wr_csr(pdata, RSIF_CONFIG_REG_ADDR, value);

	/* Rtype should be copied from FP */
	xgene_enet_wr_csr(pdata, RSIF_RAM_DBG_REG0_ADDR, 0);

	/* Configure HW pause frame generation */
	xgene_enet_rd_mcx_csr(pdata, CSR_MULTI_DPF0_ADDR, &value);
	value = (DEF_QUANTA << 16) | (value & 0xFFFF);
	xgene_enet_wr_mcx_csr(pdata, CSR_MULTI_DPF0_ADDR, value);

	xgene_enet_wr_csr(pdata, RXBUF_PAUSE_THRESH, DEF_PAUSE_THRES);
	xgene_enet_wr_csr(pdata, RXBUF_PAUSE_OFF_THRESH, DEF_PAUSE_OFF_THRES);

	xgene_gmac_flowctl_tx(pdata, pdata->tx_pause);
	xgene_gmac_flowctl_rx(pdata, pdata->rx_pause);

	/* Rx-Tx traffic resume */
	xgene_enet_wr_csr(pdata, CFG_LINK_AGGR_RESUME_0_ADDR, TX_PORT0);

	xgene_enet_rd_mcx_csr(pdata, RX_DV_GATE_REG_0_ADDR, &value);
	value &= ~TX_DV_GATE_EN0;
	value &= ~RX_DV_GATE_EN0;
	value |= RESUME_RX0;
	xgene_enet_wr_mcx_csr(pdata, RX_DV_GATE_REG_0_ADDR, value);

	xgene_enet_wr_csr(pdata, CFG_BYPASS_ADDR, RESUME_TX);
}

static void xgene_enet_config_ring_if_assoc(struct xgene_enet_pdata *pdata)
{
	u32 val = 0xffffffff;

	xgene_enet_wr_ring_if(pdata, ENET_CFGSSQMIWQASSOC_ADDR, val);
	xgene_enet_wr_ring_if(pdata, ENET_CFGSSQMIFPQASSOC_ADDR, val);
	xgene_enet_wr_ring_if(pdata, ENET_CFGSSQMIQMLITEWQASSOC_ADDR, val);
	xgene_enet_wr_ring_if(pdata, ENET_CFGSSQMIQMLITEFPQASSOC_ADDR, val);
}

static void xgene_enet_cle_bypass(struct xgene_enet_pdata *pdata,
				  u32 dst_ring_num, u16 bufpool_id,
				  u16 nxtbufpool_id)
{
	u32 cb;
	u32 fpsel, nxtfpsel;

	fpsel = xgene_enet_get_fpsel(bufpool_id);
	nxtfpsel = xgene_enet_get_fpsel(nxtbufpool_id);

	xgene_enet_rd_csr(pdata, CLE_BYPASS_REG0_0_ADDR, &cb);
	cb |= CFG_CLE_BYPASS_EN0;
	CFG_CLE_IP_PROTOCOL0_SET(&cb, 3);
	CFG_CLE_IP_HDR_LEN_SET(&cb, 0);
	xgene_enet_wr_csr(pdata, CLE_BYPASS_REG0_0_ADDR, cb);

	xgene_enet_rd_csr(pdata, CLE_BYPASS_REG1_0_ADDR, &cb);
	CFG_CLE_DSTQID0_SET(&cb, dst_ring_num);
	CFG_CLE_FPSEL0_SET(&cb, fpsel);
	CFG_CLE_NXTFPSEL0_SET(&cb, nxtfpsel);
	xgene_enet_wr_csr(pdata, CLE_BYPASS_REG1_0_ADDR, cb);
}

static void xgene_gmac_rx_enable(struct xgene_enet_pdata *pdata)
{
	u32 data;

	xgene_enet_rd_mcx_mac(pdata, MAC_CONFIG_1_ADDR, &data);
	xgene_enet_wr_mcx_mac(pdata, MAC_CONFIG_1_ADDR, data | RX_EN);
}

static void xgene_gmac_tx_enable(struct xgene_enet_pdata *pdata)
{
	u32 data;

	xgene_enet_rd_mcx_mac(pdata, MAC_CONFIG_1_ADDR, &data);
	xgene_enet_wr_mcx_mac(pdata, MAC_CONFIG_1_ADDR, data | TX_EN);
}

static void xgene_gmac_rx_disable(struct xgene_enet_pdata *pdata)
{
	u32 data;

	xgene_enet_rd_mcx_mac(pdata, MAC_CONFIG_1_ADDR, &data);
	xgene_enet_wr_mcx_mac(pdata, MAC_CONFIG_1_ADDR, data & ~RX_EN);
}

static void xgene_gmac_tx_disable(struct xgene_enet_pdata *pdata)
{
	u32 data;

	xgene_enet_rd_mcx_mac(pdata, MAC_CONFIG_1_ADDR, &data);
	xgene_enet_wr_mcx_mac(pdata, MAC_CONFIG_1_ADDR, data & ~TX_EN);
}

bool xgene_ring_mgr_init(struct xgene_enet_pdata *p)
{
	if (!ioread32(p->ring_csr_addr + CLKEN_ADDR))
		return false;

	if (ioread32(p->ring_csr_addr + SRST_ADDR))
		return false;

	return true;
}

static int xgene_enet_reset(struct xgene_enet_pdata *pdata)
{
	struct device *dev = &pdata->pdev->dev;

	if (!xgene_ring_mgr_init(pdata))
		return -ENODEV;

	if (pdata->mdio_driver) {
		xgene_enet_config_ring_if_assoc(pdata);
		return 0;
	}

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

static void xgene_gport_shutdown(struct xgene_enet_pdata *pdata)
{
	struct device *dev = &pdata->pdev->dev;
	struct xgene_enet_desc_ring *ring;
	u32 pb;
	int i;

	pb = 0;
	for (i = 0; i < pdata->rxq_cnt; i++) {
		ring = pdata->rx_ring[i]->buf_pool;
		pb |= BIT(xgene_enet_get_fpsel(ring->id));
		ring = pdata->rx_ring[i]->page_pool;
		if (ring)
			pb |= BIT(xgene_enet_get_fpsel(ring->id));

	}
	xgene_enet_wr_ring_if(pdata, ENET_CFGSSQMIFPRESET_ADDR, pb);

	pb = 0;
	for (i = 0; i < pdata->txq_cnt; i++) {
		ring = pdata->tx_ring[i];
		pb |= BIT(xgene_enet_ring_bufnum(ring->id));
	}
	xgene_enet_wr_ring_if(pdata, ENET_CFGSSQMIWQRESET_ADDR, pb);

	if (dev->of_node) {
		if (!IS_ERR(pdata->clk))
			clk_disable_unprepare(pdata->clk);
	}
}

static u32 xgene_enet_flowctrl_cfg(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	u16 lcladv, rmtadv = 0;
	u32 rx_pause, tx_pause;
	u8 flowctl = 0;

	if (!phydev->duplex || !pdata->pause_autoneg)
		return 0;

	if (pdata->tx_pause)
		flowctl |= FLOW_CTRL_TX;

	if (pdata->rx_pause)
		flowctl |= FLOW_CTRL_RX;

	lcladv = mii_advertise_flowctrl(flowctl);

	if (phydev->pause)
		rmtadv = LPA_PAUSE_CAP;

	if (phydev->asym_pause)
		rmtadv |= LPA_PAUSE_ASYM;

	flowctl = mii_resolve_flowctrl_fdx(lcladv, rmtadv);
	tx_pause = !!(flowctl & FLOW_CTRL_TX);
	rx_pause = !!(flowctl & FLOW_CTRL_RX);

	if (tx_pause != pdata->tx_pause) {
		pdata->tx_pause = tx_pause;
		pdata->mac_ops->flowctl_tx(pdata, pdata->tx_pause);
	}

	if (rx_pause != pdata->rx_pause) {
		pdata->rx_pause = rx_pause;
		pdata->mac_ops->flowctl_rx(pdata, pdata->rx_pause);
	}

	return 0;
}

static void xgene_enet_adjust_link(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	const struct xgene_mac_ops *mac_ops = pdata->mac_ops;
	struct phy_device *phydev = ndev->phydev;

	if (phydev->link) {
		if (pdata->phy_speed != phydev->speed) {
			pdata->phy_speed = phydev->speed;
			mac_ops->set_speed(pdata);
			mac_ops->rx_enable(pdata);
			mac_ops->tx_enable(pdata);
			phy_print_status(phydev);
		}

		xgene_enet_flowctrl_cfg(ndev);
	} else {
		mac_ops->rx_disable(pdata);
		mac_ops->tx_disable(pdata);
		pdata->phy_speed = SPEED_UNKNOWN;
		phy_print_status(phydev);
	}
}

#ifdef CONFIG_ACPI
static struct acpi_device *acpi_phy_find_device(struct device *dev)
{
	struct acpi_reference_args args;
	struct fwnode_handle *fw_node;
	int status;

	fw_node = acpi_fwnode_handle(ACPI_COMPANION(dev));
	status = acpi_node_get_property_reference(fw_node, "phy-handle", 0,
						  &args);
	if (ACPI_FAILURE(status)) {
		dev_dbg(dev, "No matching phy in ACPI table\n");
		return NULL;
	}

	return args.adev;
}
#endif

int xgene_enet_phy_connect(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct device_node *np;
	struct phy_device *phy_dev;
	struct device *dev = &pdata->pdev->dev;
	int i;

	if (dev->of_node) {
		for (i = 0 ; i < 2; i++) {
			np = of_parse_phandle(dev->of_node, "phy-handle", i);
			phy_dev = of_phy_connect(ndev, np,
						 &xgene_enet_adjust_link,
						 0, pdata->phy_mode);
			of_node_put(np);
			if (phy_dev)
				break;
		}

		if (!phy_dev) {
			netdev_err(ndev, "Could not connect to PHY\n");
			return -ENODEV;
		}
	} else {
#ifdef CONFIG_ACPI
		struct acpi_device *adev = acpi_phy_find_device(dev);
		if (adev)
			phy_dev = adev->driver_data;
		else
			phy_dev = NULL;

		if (!phy_dev ||
		    phy_connect_direct(ndev, phy_dev, &xgene_enet_adjust_link,
				       pdata->phy_mode)) {
			netdev_err(ndev, "Could not connect to PHY\n");
			return  -ENODEV;
		}
#else
		return -ENODEV;
#endif
	}

	pdata->phy_speed = SPEED_UNKNOWN;
	phy_dev->supported &= ~SUPPORTED_10baseT_Half &
			      ~SUPPORTED_100baseT_Half &
			      ~SUPPORTED_1000baseT_Half;
	phy_dev->supported |= SUPPORTED_Pause |
			      SUPPORTED_Asym_Pause;
	phy_dev->advertising = phy_dev->supported;

	return 0;
}

static int xgene_mdiobus_register(struct xgene_enet_pdata *pdata,
				  struct mii_bus *mdio)
{
	struct device *dev = &pdata->pdev->dev;
	struct net_device *ndev = pdata->ndev;
	struct phy_device *phy;
	struct device_node *child_np;
	struct device_node *mdio_np = NULL;
	u32 phy_addr;
	int ret;

	if (dev->of_node) {
		for_each_child_of_node(dev->of_node, child_np) {
			if (of_device_is_compatible(child_np,
						    "apm,xgene-mdio")) {
				mdio_np = child_np;
				break;
			}
		}

		if (!mdio_np) {
			netdev_dbg(ndev, "No mdio node in the dts\n");
			return -ENXIO;
		}

		return of_mdiobus_register(mdio, mdio_np);
	}

	/* Mask out all PHYs from auto probing. */
	mdio->phy_mask = ~0;

	/* Register the MDIO bus */
	ret = mdiobus_register(mdio);
	if (ret)
		return ret;

	ret = device_property_read_u32(dev, "phy-channel", &phy_addr);
	if (ret)
		ret = device_property_read_u32(dev, "phy-addr", &phy_addr);
	if (ret)
		return -EINVAL;

	phy = xgene_enet_phy_register(mdio, phy_addr);
	if (!phy)
		return -EIO;

	return ret;
}

int xgene_enet_mdio_config(struct xgene_enet_pdata *pdata)
{
	struct net_device *ndev = pdata->ndev;
	struct mii_bus *mdio_bus;
	int ret;

	mdio_bus = mdiobus_alloc();
	if (!mdio_bus)
		return -ENOMEM;

	mdio_bus->name = "APM X-Gene MDIO bus";
	mdio_bus->read = xgene_mdio_rgmii_read;
	mdio_bus->write = xgene_mdio_rgmii_write;
	snprintf(mdio_bus->id, MII_BUS_ID_SIZE, "%s-%s", "xgene-mii",
		 ndev->name);

	mdio_bus->priv = (void __force *)pdata->mcx_mac_addr;
	mdio_bus->parent = &pdata->pdev->dev;

	ret = xgene_mdiobus_register(pdata, mdio_bus);
	if (ret) {
		netdev_err(ndev, "Failed to register MDIO bus\n");
		mdiobus_free(mdio_bus);
		return ret;
	}
	pdata->mdio_bus = mdio_bus;

	ret = xgene_enet_phy_connect(ndev);
	if (ret)
		xgene_enet_mdio_remove(pdata);

	return ret;
}

void xgene_enet_phy_disconnect(struct xgene_enet_pdata *pdata)
{
	struct net_device *ndev = pdata->ndev;

	if (ndev->phydev)
		phy_disconnect(ndev->phydev);
}

void xgene_enet_mdio_remove(struct xgene_enet_pdata *pdata)
{
	struct net_device *ndev = pdata->ndev;

	if (ndev->phydev)
		phy_disconnect(ndev->phydev);

	mdiobus_unregister(pdata->mdio_bus);
	mdiobus_free(pdata->mdio_bus);
	pdata->mdio_bus = NULL;
}

const struct xgene_mac_ops xgene_gmac_ops = {
	.init = xgene_gmac_init,
	.reset = xgene_gmac_reset,
	.rx_enable = xgene_gmac_rx_enable,
	.tx_enable = xgene_gmac_tx_enable,
	.rx_disable = xgene_gmac_rx_disable,
	.tx_disable = xgene_gmac_tx_disable,
	.set_speed = xgene_gmac_set_speed,
	.set_mac_addr = xgene_gmac_set_mac_addr,
	.set_framesize = xgene_enet_set_frame_size,
	.enable_tx_pause = xgene_gmac_enable_tx_pause,
	.flowctl_tx     = xgene_gmac_flowctl_tx,
	.flowctl_rx     = xgene_gmac_flowctl_rx,
};

const struct xgene_port_ops xgene_gport_ops = {
	.reset = xgene_enet_reset,
	.clear = xgene_enet_clear,
	.cle_bypass = xgene_enet_cle_bypass,
	.shutdown = xgene_gport_shutdown,
};

struct xgene_ring_ops xgene_ring1_ops = {
	.num_ring_config = NUM_RING_CONFIG,
	.num_ring_id_shift = 6,
	.setup = xgene_enet_setup_ring,
	.clear = xgene_enet_clear_ring,
	.wr_cmd = xgene_enet_wr_cmd,
	.len = xgene_enet_ring_len,
};
