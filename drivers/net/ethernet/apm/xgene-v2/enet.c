/*
 * Applied Micro X-Gene SoC Ethernet v2 Driver
 *
 * Copyright (c) 2017, Applied Micro Circuits Corporation
 * Author(s): Iyappan Subramanian <isubramanian@apm.com>
 *	      Keyur Chudgar <kchudgar@apm.com>
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

#include "main.h"

void xge_wr_csr(struct xge_pdata *pdata, u32 offset, u32 val)
{
	void __iomem *addr = pdata->resources.base_addr + offset;

	iowrite32(val, addr);
}

u32 xge_rd_csr(struct xge_pdata *pdata, u32 offset)
{
	void __iomem *addr = pdata->resources.base_addr + offset;

	return ioread32(addr);
}

int xge_port_reset(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct device *dev = &pdata->pdev->dev;
	u32 data, wait = 10;

	xge_wr_csr(pdata, ENET_CLKEN, 0x3);
	xge_wr_csr(pdata, ENET_SRST, 0xf);
	xge_wr_csr(pdata, ENET_SRST, 0);
	xge_wr_csr(pdata, CFG_MEM_RAM_SHUTDOWN, 1);
	xge_wr_csr(pdata, CFG_MEM_RAM_SHUTDOWN, 0);

	do {
		usleep_range(100, 110);
		data = xge_rd_csr(pdata, BLOCK_MEM_RDY);
	} while (data != MEM_RDY && wait--);

	if (data != MEM_RDY) {
		dev_err(dev, "ECC init failed: %x\n", data);
		return -ETIMEDOUT;
	}

	xge_wr_csr(pdata, ENET_SHIM, DEVM_ARAUX_COH | DEVM_AWAUX_COH);

	return 0;
}

static void xge_traffic_resume(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);

	xge_wr_csr(pdata, CFG_FORCE_LINK_STATUS_EN, 1);
	xge_wr_csr(pdata, FORCE_LINK_STATUS, 1);

	xge_wr_csr(pdata, CFG_LINK_AGGR_RESUME, 1);
	xge_wr_csr(pdata, RX_DV_GATE_REG, 1);
}

void xge_port_init(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);

	pdata->phy_speed = SPEED_1000;
	xge_mac_init(pdata);
	xge_traffic_resume(ndev);
}
