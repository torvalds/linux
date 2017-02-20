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
#include "xgene_enet_xgmac.h"

static void xgene_enet_wr_csr(struct xgene_enet_pdata *p, u32 offset, u32 val)
{
	iowrite32(val, p->eth_csr_addr + offset);
}

static void xgene_enet_wr_clkrst_csr(struct xgene_enet_pdata *p, u32 offset,
				     u32 val)
{
	iowrite32(val, p->base_addr + offset);
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

static void xgene_enet_wr_mcx_csr(struct xgene_enet_pdata *pdata,
				  u32 offset, u32 val)
{
	void __iomem *addr = pdata->mcx_mac_csr_addr + offset;

	iowrite32(val, addr);
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

static u32 xgene_enet_rd_mcx_csr(struct xgene_enet_pdata *p, u32 offset)
{
	return ioread32(p->mcx_mac_csr_addr + offset);
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
	u32 data, shutdown;
	int i = 0;

	shutdown = xgene_enet_rd_diag_csr(p, ENET_CFG_MEM_RAM_SHUTDOWN_ADDR);
	data = xgene_enet_rd_diag_csr(p, ENET_BLOCK_MEM_RDY_ADDR);

	if (!shutdown && data == ~0U) {
		netdev_dbg(ndev, "+ ecc_init done, skipping\n");
		return 0;
	}

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
	u32 val;

	val = (p->enet_id == XGENE_ENET1) ? 0xffffffff : 0;
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

	if (LINK_SPEED(data) == PHY_SPEED_1000)
		p->phy_speed = SPEED_1000;
	else if (LINK_SPEED(data) == PHY_SPEED_100)
		p->phy_speed = SPEED_100;
	else
		p->phy_speed = SPEED_10;

	return data & LINK_UP;
}

static void xgene_sgmii_configure(struct xgene_enet_pdata *p)
{
	xgene_mii_phy_write(p, INT_PHY_ADDR, SGMII_TBI_CONTROL_ADDR >> 2,
			    0x8000);
	xgene_mii_phy_write(p, INT_PHY_ADDR, SGMII_CONTROL_ADDR >> 2, 0x9000);
	xgene_mii_phy_write(p, INT_PHY_ADDR, SGMII_TBI_CONTROL_ADDR >> 2, 0);
}

static void xgene_sgmii_tbi_control_reset(struct xgene_enet_pdata *p)
{
	xgene_mii_phy_write(p, INT_PHY_ADDR, SGMII_TBI_CONTROL_ADDR >> 2,
			    0x8000);
	xgene_mii_phy_write(p, INT_PHY_ADDR, SGMII_TBI_CONTROL_ADDR >> 2, 0);
}

static void xgene_sgmii_reset(struct xgene_enet_pdata *p)
{
	u32 value;

	if (p->phy_speed == SPEED_UNKNOWN)
		return;

	value = xgene_mii_phy_read(p, INT_PHY_ADDR,
				   SGMII_BASE_PAGE_ABILITY_ADDR >> 2);
	if (!(value & LINK_UP))
		xgene_sgmii_tbi_control_reset(p);
}

static void xgene_sgmac_set_speed(struct xgene_enet_pdata *p)
{
	u32 icm0_addr, icm2_addr, debug_addr;
	u32 icm0, icm2, intf_ctl;
	u32 mc2, value;

	xgene_sgmii_reset(p);

	if (p->enet_id == XGENE_ENET1) {
		icm0_addr = ICM_CONFIG0_REG_0_ADDR + p->port_id * OFFSET_8;
		icm2_addr = ICM_CONFIG2_REG_0_ADDR + p->port_id * OFFSET_4;
		debug_addr = DEBUG_REG_ADDR;
	} else {
		icm0_addr = XG_MCX_ICM_CONFIG0_REG_0_ADDR;
		icm2_addr = XG_MCX_ICM_CONFIG2_REG_0_ADDR;
		debug_addr = XG_DEBUG_REG_ADDR;
	}

	icm0 = xgene_enet_rd_mcx_csr(p, icm0_addr);
	icm2 = xgene_enet_rd_mcx_csr(p, icm2_addr);
	mc2 = xgene_enet_rd_mac(p, MAC_CONFIG_2_ADDR);
	intf_ctl = xgene_enet_rd_mac(p, INTERFACE_CONTROL_ADDR);

	switch (p->phy_speed) {
	case SPEED_10:
		ENET_INTERFACE_MODE2_SET(&mc2, 1);
		intf_ctl &= ~(ENET_LHD_MODE | ENET_GHD_MODE);
		CFG_MACMODE_SET(&icm0, 0);
		CFG_WAITASYNCRD_SET(&icm2, 500);
		break;
	case SPEED_100:
		ENET_INTERFACE_MODE2_SET(&mc2, 1);
		intf_ctl &= ~ENET_GHD_MODE;
		intf_ctl |= ENET_LHD_MODE;
		CFG_MACMODE_SET(&icm0, 1);
		CFG_WAITASYNCRD_SET(&icm2, 80);
		break;
	default:
		ENET_INTERFACE_MODE2_SET(&mc2, 2);
		intf_ctl &= ~ENET_LHD_MODE;
		intf_ctl |= ENET_GHD_MODE;
		CFG_MACMODE_SET(&icm0, 2);
		CFG_WAITASYNCRD_SET(&icm2, 16);
		value = xgene_enet_rd_csr(p, debug_addr);
		value |= CFG_BYPASS_UNISEC_TX | CFG_BYPASS_UNISEC_RX;
		xgene_enet_wr_csr(p, debug_addr, value);
		break;
	}

	mc2 |= FULL_DUPLEX2 | PAD_CRC;
	xgene_enet_wr_mac(p, MAC_CONFIG_2_ADDR, mc2);
	xgene_enet_wr_mac(p, INTERFACE_CONTROL_ADDR, intf_ctl);
	xgene_enet_wr_mcx_csr(p, icm0_addr, icm0);
	xgene_enet_wr_mcx_csr(p, icm2_addr, icm2);
}

static void xgene_sgmac_set_frame_size(struct xgene_enet_pdata *pdata, int size)
{
	xgene_enet_wr_mac(pdata, MAX_FRAME_LEN_ADDR, size);
}

static void xgene_sgmii_enable_autoneg(struct xgene_enet_pdata *p)
{
	u32 data, loop = 10;

	xgene_sgmii_configure(p);

	while (loop--) {
		data = xgene_mii_phy_read(p, INT_PHY_ADDR,
					  SGMII_STATUS_ADDR >> 2);
		if ((data & AUTO_NEG_COMPLETE) && (data & LINK_STATUS))
			break;
		usleep_range(1000, 2000);
	}
	if (!(data & AUTO_NEG_COMPLETE) || !(data & LINK_STATUS))
		netdev_err(p->ndev, "Auto-negotiation failed\n");
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

static void xgene_sgmac_flowctl_tx(struct xgene_enet_pdata *p, bool enable)
{
	xgene_sgmac_rxtx(p, TX_FLOW_EN, enable);

	p->mac_ops->enable_tx_pause(p, enable);
}

static void xgene_sgmac_flowctl_rx(struct xgene_enet_pdata *pdata, bool enable)
{
	xgene_sgmac_rxtx(pdata, RX_FLOW_EN, enable);
}

static void xgene_sgmac_init(struct xgene_enet_pdata *p)
{
	u32 pause_thres_reg, pause_off_thres_reg;
	u32 enet_spare_cfg_reg, rsif_config_reg;
	u32 cfg_bypass_reg, rx_dv_gate_reg;
	u32 data, data1, data2, offset;
	u32 multi_dpf_reg;

	if (!(p->enet_id == XGENE_ENET2 && p->mdio_driver))
		xgene_sgmac_reset(p);

	xgene_sgmii_enable_autoneg(p);
	xgene_sgmac_set_speed(p);
	xgene_sgmac_set_mac_addr(p);

	if (p->enet_id == XGENE_ENET1) {
		enet_spare_cfg_reg = ENET_SPARE_CFG_REG_ADDR;
		rsif_config_reg = RSIF_CONFIG_REG_ADDR;
		cfg_bypass_reg = CFG_BYPASS_ADDR;
		offset = p->port_id * OFFSET_4;
		rx_dv_gate_reg = SG_RX_DV_GATE_REG_0_ADDR + offset;
	} else {
		enet_spare_cfg_reg = XG_ENET_SPARE_CFG_REG_ADDR;
		rsif_config_reg = XG_RSIF_CONFIG_REG_ADDR;
		cfg_bypass_reg = XG_CFG_BYPASS_ADDR;
		rx_dv_gate_reg = XG_MCX_RX_DV_GATE_REG_0_ADDR;
	}

	data = xgene_enet_rd_csr(p, enet_spare_cfg_reg);
	data |= MPA_IDLE_WITH_QMI_EMPTY;
	xgene_enet_wr_csr(p, enet_spare_cfg_reg, data);

	/* Adjust MDC clock frequency */
	data = xgene_enet_rd_mac(p, MII_MGMT_CONFIG_ADDR);
	MGMT_CLOCK_SEL_SET(&data, 7);
	xgene_enet_wr_mac(p, MII_MGMT_CONFIG_ADDR, data);

	/* Enable drop if bufpool not available */
	data = xgene_enet_rd_csr(p, rsif_config_reg);
	data |= CFG_RSIF_FPBUFF_TIMEOUT_EN;
	xgene_enet_wr_csr(p, rsif_config_reg, data);

	/* Configure HW pause frame generation */
	multi_dpf_reg = (p->enet_id == XGENE_ENET1) ? CSR_MULTI_DPF0_ADDR :
			 XG_MCX_MULTI_DPF0_ADDR;
	data = xgene_enet_rd_mcx_csr(p, multi_dpf_reg);
	data = (DEF_QUANTA << 16) | (data & 0xffff);
	xgene_enet_wr_mcx_csr(p, multi_dpf_reg, data);

	if (p->enet_id != XGENE_ENET1) {
		data = xgene_enet_rd_mcx_csr(p, XG_MCX_MULTI_DPF1_ADDR);
		data =  (NORM_PAUSE_OPCODE << 16) | (data & 0xFFFF);
		xgene_enet_wr_mcx_csr(p, XG_MCX_MULTI_DPF1_ADDR, data);
	}

	pause_thres_reg = (p->enet_id == XGENE_ENET1) ? RXBUF_PAUSE_THRESH :
			   XG_RXBUF_PAUSE_THRESH;
	pause_off_thres_reg = (p->enet_id == XGENE_ENET1) ?
			       RXBUF_PAUSE_OFF_THRESH : 0;

	if (p->enet_id == XGENE_ENET1) {
		data1 = xgene_enet_rd_csr(p, pause_thres_reg);
		data2 = xgene_enet_rd_csr(p, pause_off_thres_reg);

		if (!(p->port_id % 2)) {
			data1 = (data1 & 0xffff0000) | DEF_PAUSE_THRES;
			data2 = (data2 & 0xffff0000) | DEF_PAUSE_OFF_THRES;
		} else {
			data1 = (data1 & 0xffff) | (DEF_PAUSE_THRES << 16);
			data2 = (data2 & 0xffff) | (DEF_PAUSE_OFF_THRES << 16);
		}

		xgene_enet_wr_csr(p, pause_thres_reg, data1);
		xgene_enet_wr_csr(p, pause_off_thres_reg, data2);
	} else {
		data = (DEF_PAUSE_OFF_THRES << 16) | DEF_PAUSE_THRES;
		xgene_enet_wr_csr(p, pause_thres_reg, data);
	}

	xgene_sgmac_flowctl_tx(p, p->tx_pause);
	xgene_sgmac_flowctl_rx(p, p->rx_pause);

	/* Bypass traffic gating */
	xgene_enet_wr_csr(p, XG_ENET_SPARE_CFG_REG_1_ADDR, 0x84);
	xgene_enet_wr_csr(p, cfg_bypass_reg, RESUME_TX);
	xgene_enet_wr_mcx_csr(p, rx_dv_gate_reg, RESUME_RX0);
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
	struct device *dev = &p->pdev->dev;

	if (!xgene_ring_mgr_init(p))
		return -ENODEV;

	if (p->mdio_driver && p->enet_id == XGENE_ENET2) {
		xgene_enet_config_ring_if_assoc(p);
		return 0;
	}

	if (p->enet_id == XGENE_ENET2)
		xgene_enet_wr_clkrst_csr(p, XGENET_CONFIG_REG_ADDR, SGMII_EN);

	if (dev->of_node) {
		if (!IS_ERR(p->clk)) {
			clk_prepare_enable(p->clk);
			udelay(5);
			clk_disable_unprepare(p->clk);
			udelay(5);
			clk_prepare_enable(p->clk);
			udelay(5);
		}
	} else {
#ifdef CONFIG_ACPI
		if (acpi_has_method(ACPI_HANDLE(&p->pdev->dev), "_RST"))
			acpi_evaluate_object(ACPI_HANDLE(&p->pdev->dev),
					     "_RST", NULL, NULL);
		else if (acpi_has_method(ACPI_HANDLE(&p->pdev->dev), "_INI"))
			acpi_evaluate_object(ACPI_HANDLE(&p->pdev->dev),
					     "_INI", NULL, NULL);
#endif
	}

	if (!p->port_id) {
		xgene_enet_ecc_init(p);
		xgene_enet_config_ring_if_assoc(p);
	}

	return 0;
}

static void xgene_enet_cle_bypass(struct xgene_enet_pdata *p,
				  u32 dst_ring_num, u16 bufpool_id,
				  u16 nxtbufpool_id)
{
	u32 cle_bypass_reg0, cle_bypass_reg1;
	u32 offset = p->port_id * MAC_OFFSET;
	u32 data, fpsel, nxtfpsel;

	if (p->enet_id == XGENE_ENET1) {
		cle_bypass_reg0 = CLE_BYPASS_REG0_0_ADDR;
		cle_bypass_reg1 = CLE_BYPASS_REG1_0_ADDR;
	} else {
		cle_bypass_reg0 = XCLE_BYPASS_REG0_ADDR;
		cle_bypass_reg1 = XCLE_BYPASS_REG1_ADDR;
	}

	data = CFG_CLE_BYPASS_EN0;
	xgene_enet_wr_csr(p, cle_bypass_reg0 + offset, data);

	fpsel = xgene_enet_get_fpsel(bufpool_id);
	nxtfpsel = xgene_enet_get_fpsel(nxtbufpool_id);
	data = CFG_CLE_DSTQID0(dst_ring_num) | CFG_CLE_FPSEL0(fpsel) |
	       CFG_CLE_NXTFPSEL0(nxtfpsel);
	xgene_enet_wr_csr(p, cle_bypass_reg1 + offset, data);
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

static void xgene_enet_shutdown(struct xgene_enet_pdata *p)
{
	struct device *dev = &p->pdev->dev;
	struct xgene_enet_desc_ring *ring;
	u32 pb;
	int i;

	pb = 0;
	for (i = 0; i < p->rxq_cnt; i++) {
		ring = p->rx_ring[i]->buf_pool;
		pb |= BIT(xgene_enet_get_fpsel(ring->id));
		ring = p->rx_ring[i]->page_pool;
		if (ring)
			pb |= BIT(xgene_enet_get_fpsel(ring->id));
	}
	xgene_enet_wr_ring_if(p, ENET_CFGSSQMIFPRESET_ADDR, pb);

	pb = 0;
	for (i = 0; i < p->txq_cnt; i++) {
		ring = p->tx_ring[i];
		pb |= BIT(xgene_enet_ring_bufnum(ring->id));
	}
	xgene_enet_wr_ring_if(p, ENET_CFGSSQMIWQRESET_ADDR, pb);

	if (dev->of_node) {
		if (!IS_ERR(p->clk))
			clk_disable_unprepare(p->clk);
	}
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
			xgene_sgmac_set_speed(p);
			xgene_sgmac_rx_enable(p);
			xgene_sgmac_tx_enable(p);
			netdev_info(ndev, "Link is Up - %dMbps\n",
				    p->phy_speed);
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

static void xgene_sgmac_enable_tx_pause(struct xgene_enet_pdata *p, bool enable)
{
	u32 data, ecm_cfg_addr;

	if (p->enet_id == XGENE_ENET1) {
		ecm_cfg_addr = (!(p->port_id % 2)) ? CSR_ECM_CFG_0_ADDR :
				CSR_ECM_CFG_1_ADDR;
	} else {
		ecm_cfg_addr = XG_MCX_ECM_CFG_0_ADDR;
	}

	data = xgene_enet_rd_mcx_csr(p, ecm_cfg_addr);
	if (enable)
		data |= MULTI_DPF_AUTOCTRL | PAUSE_XON_EN;
	else
		data &= ~(MULTI_DPF_AUTOCTRL | PAUSE_XON_EN);
	xgene_enet_wr_mcx_csr(p, ecm_cfg_addr, data);
}

const struct xgene_mac_ops xgene_sgmac_ops = {
	.init		= xgene_sgmac_init,
	.reset		= xgene_sgmac_reset,
	.rx_enable	= xgene_sgmac_rx_enable,
	.tx_enable	= xgene_sgmac_tx_enable,
	.rx_disable	= xgene_sgmac_rx_disable,
	.tx_disable	= xgene_sgmac_tx_disable,
	.set_speed	= xgene_sgmac_set_speed,
	.set_mac_addr	= xgene_sgmac_set_mac_addr,
	.set_framesize  = xgene_sgmac_set_frame_size,
	.link_state	= xgene_enet_link_state,
	.enable_tx_pause = xgene_sgmac_enable_tx_pause,
	.flowctl_tx     = xgene_sgmac_flowctl_tx,
	.flowctl_rx     = xgene_sgmac_flowctl_rx
};

const struct xgene_port_ops xgene_sgport_ops = {
	.reset		= xgene_enet_reset,
	.clear		= xgene_enet_clear,
	.cle_bypass	= xgene_enet_cle_bypass,
	.shutdown	= xgene_enet_shutdown
};
