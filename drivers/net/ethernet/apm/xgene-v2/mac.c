// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Applied Micro X-Gene SoC Ethernet v2 Driver
 *
 * Copyright (c) 2017, Applied Micro Circuits Corporation
 * Author(s): Iyappan Subramanian <isubramanian@apm.com>
 *	      Keyur Chudgar <kchudgar@apm.com>
 */

#include "main.h"

void xge_mac_reset(struct xge_pdata *pdata)
{
	xge_wr_csr(pdata, MAC_CONFIG_1, SOFT_RESET);
	xge_wr_csr(pdata, MAC_CONFIG_1, 0);
}

void xge_mac_set_speed(struct xge_pdata *pdata)
{
	u32 icm0, icm2, ecm0, mc2;
	u32 intf_ctrl, rgmii;

	icm0 = xge_rd_csr(pdata, ICM_CONFIG0_REG_0);
	icm2 = xge_rd_csr(pdata, ICM_CONFIG2_REG_0);
	ecm0 = xge_rd_csr(pdata, ECM_CONFIG0_REG_0);
	rgmii = xge_rd_csr(pdata, RGMII_REG_0);
	mc2 = xge_rd_csr(pdata, MAC_CONFIG_2);
	intf_ctrl = xge_rd_csr(pdata, INTERFACE_CONTROL);
	icm2 |= CFG_WAITASYNCRD_EN;

	switch (pdata->phy_speed) {
	case SPEED_10:
		SET_REG_BITS(&mc2, INTF_MODE, 1);
		SET_REG_BITS(&intf_ctrl, HD_MODE, 0);
		SET_REG_BITS(&icm0, CFG_MACMODE, 0);
		SET_REG_BITS(&icm2, CFG_WAITASYNCRD, 500);
		SET_REG_BIT(&rgmii, CFG_SPEED_125, 0);
		break;
	case SPEED_100:
		SET_REG_BITS(&mc2, INTF_MODE, 1);
		SET_REG_BITS(&intf_ctrl, HD_MODE, 1);
		SET_REG_BITS(&icm0, CFG_MACMODE, 1);
		SET_REG_BITS(&icm2, CFG_WAITASYNCRD, 80);
		SET_REG_BIT(&rgmii, CFG_SPEED_125, 0);
		break;
	default:
		SET_REG_BITS(&mc2, INTF_MODE, 2);
		SET_REG_BITS(&intf_ctrl, HD_MODE, 2);
		SET_REG_BITS(&icm0, CFG_MACMODE, 2);
		SET_REG_BITS(&icm2, CFG_WAITASYNCRD, 16);
		SET_REG_BIT(&rgmii, CFG_SPEED_125, 1);
		break;
	}

	mc2 |= FULL_DUPLEX | CRC_EN | PAD_CRC;
	SET_REG_BITS(&ecm0, CFG_WFIFOFULLTHR, 0x32);

	xge_wr_csr(pdata, MAC_CONFIG_2, mc2);
	xge_wr_csr(pdata, INTERFACE_CONTROL, intf_ctrl);
	xge_wr_csr(pdata, RGMII_REG_0, rgmii);
	xge_wr_csr(pdata, ICM_CONFIG0_REG_0, icm0);
	xge_wr_csr(pdata, ICM_CONFIG2_REG_0, icm2);
	xge_wr_csr(pdata, ECM_CONFIG0_REG_0, ecm0);
}

void xge_mac_set_station_addr(struct xge_pdata *pdata)
{
	u8 *dev_addr = pdata->ndev->dev_addr;
	u32 addr0, addr1;

	addr0 = (dev_addr[3] << 24) | (dev_addr[2] << 16) |
		(dev_addr[1] << 8) | dev_addr[0];
	addr1 = (dev_addr[5] << 24) | (dev_addr[4] << 16);

	xge_wr_csr(pdata, STATION_ADDR0, addr0);
	xge_wr_csr(pdata, STATION_ADDR1, addr1);
}

void xge_mac_init(struct xge_pdata *pdata)
{
	xge_mac_reset(pdata);
	xge_mac_set_speed(pdata);
	xge_mac_set_station_addr(pdata);
}

void xge_mac_enable(struct xge_pdata *pdata)
{
	u32 data;

	data = xge_rd_csr(pdata, MAC_CONFIG_1);
	data |= TX_EN | RX_EN;
	xge_wr_csr(pdata, MAC_CONFIG_1, data);

	data = xge_rd_csr(pdata, MAC_CONFIG_1);
}

void xge_mac_disable(struct xge_pdata *pdata)
{
	u32 data;

	data = xge_rd_csr(pdata, MAC_CONFIG_1);
	data &= ~(TX_EN | RX_EN);
	xge_wr_csr(pdata, MAC_CONFIG_1, data);
}
