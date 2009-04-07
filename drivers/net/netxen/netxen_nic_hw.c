/*
 * Copyright (C) 2003 - 2009 NetXen, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 *
 * Contact Information:
 *    info@netxen.com
 * NetXen Inc,
 * 18922 Forge Drive
 * Cupertino, CA 95014-0701
 *
 */

#include "netxen_nic.h"
#include "netxen_nic_hw.h"
#include "netxen_nic_phan_reg.h"

#include <linux/firmware.h>
#include <net/ip.h>

#define MASK(n) ((1ULL<<(n))-1)
#define MN_WIN(addr) (((addr & 0x1fc0000) >> 1) | ((addr >> 25) & 0x3ff))
#define OCM_WIN(addr) (((addr & 0x1ff0000) >> 1) | ((addr >> 25) & 0x3ff))
#define MS_WIN(addr) (addr & 0x0ffc0000)

#define GET_MEM_OFFS_2M(addr) (addr & MASK(18))

#define CRB_BLK(off)	((off >> 20) & 0x3f)
#define CRB_SUBBLK(off)	((off >> 16) & 0xf)
#define CRB_WINDOW_2M	(0x130060)
#define CRB_HI(off)	((crb_hub_agt[CRB_BLK(off)] << 20) | ((off) & 0xf0000))
#define CRB_INDIRECT_2M	(0x1e0000UL)

#ifndef readq
static inline u64 readq(void __iomem *addr)
{
	return readl(addr) | (((u64) readl(addr + 4)) << 32LL);
}
#endif

#ifndef writeq
static inline void writeq(u64 val, void __iomem *addr)
{
	writel(((u32) (val)), (addr));
	writel(((u32) (val >> 32)), (addr + 4));
}
#endif

#define CRB_WIN_LOCK_TIMEOUT 100000000
static crb_128M_2M_block_map_t crb_128M_2M_map[64] = {
    {{{0, 0,         0,         0} } },		/* 0: PCI */
    {{{1, 0x0100000, 0x0102000, 0x120000},	/* 1: PCIE */
	  {1, 0x0110000, 0x0120000, 0x130000},
	  {1, 0x0120000, 0x0122000, 0x124000},
	  {1, 0x0130000, 0x0132000, 0x126000},
	  {1, 0x0140000, 0x0142000, 0x128000},
	  {1, 0x0150000, 0x0152000, 0x12a000},
	  {1, 0x0160000, 0x0170000, 0x110000},
	  {1, 0x0170000, 0x0172000, 0x12e000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {1, 0x01e0000, 0x01e0800, 0x122000},
	  {0, 0x0000000, 0x0000000, 0x000000} } },
	{{{1, 0x0200000, 0x0210000, 0x180000} } },/* 2: MN */
    {{{0, 0,         0,         0} } },	    /* 3: */
    {{{1, 0x0400000, 0x0401000, 0x169000} } },/* 4: P2NR1 */
    {{{1, 0x0500000, 0x0510000, 0x140000} } },/* 5: SRE   */
    {{{1, 0x0600000, 0x0610000, 0x1c0000} } },/* 6: NIU   */
    {{{1, 0x0700000, 0x0704000, 0x1b8000} } },/* 7: QM    */
    {{{1, 0x0800000, 0x0802000, 0x170000},  /* 8: SQM0  */
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {1, 0x08f0000, 0x08f2000, 0x172000} } },
    {{{1, 0x0900000, 0x0902000, 0x174000},	/* 9: SQM1*/
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {1, 0x09f0000, 0x09f2000, 0x176000} } },
    {{{0, 0x0a00000, 0x0a02000, 0x178000},	/* 10: SQM2*/
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {1, 0x0af0000, 0x0af2000, 0x17a000} } },
    {{{0, 0x0b00000, 0x0b02000, 0x17c000},	/* 11: SQM3*/
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {1, 0x0bf0000, 0x0bf2000, 0x17e000} } },
	{{{1, 0x0c00000, 0x0c04000, 0x1d4000} } },/* 12: I2Q */
	{{{1, 0x0d00000, 0x0d04000, 0x1a4000} } },/* 13: TMR */
	{{{1, 0x0e00000, 0x0e04000, 0x1a0000} } },/* 14: ROMUSB */
	{{{1, 0x0f00000, 0x0f01000, 0x164000} } },/* 15: PEG4 */
	{{{0, 0x1000000, 0x1004000, 0x1a8000} } },/* 16: XDMA */
	{{{1, 0x1100000, 0x1101000, 0x160000} } },/* 17: PEG0 */
	{{{1, 0x1200000, 0x1201000, 0x161000} } },/* 18: PEG1 */
	{{{1, 0x1300000, 0x1301000, 0x162000} } },/* 19: PEG2 */
	{{{1, 0x1400000, 0x1401000, 0x163000} } },/* 20: PEG3 */
	{{{1, 0x1500000, 0x1501000, 0x165000} } },/* 21: P2ND */
	{{{1, 0x1600000, 0x1601000, 0x166000} } },/* 22: P2NI */
	{{{0, 0,         0,         0} } },	/* 23: */
	{{{0, 0,         0,         0} } },	/* 24: */
	{{{0, 0,         0,         0} } },	/* 25: */
	{{{0, 0,         0,         0} } },	/* 26: */
	{{{0, 0,         0,         0} } },	/* 27: */
	{{{0, 0,         0,         0} } },	/* 28: */
	{{{1, 0x1d00000, 0x1d10000, 0x190000} } },/* 29: MS */
    {{{1, 0x1e00000, 0x1e01000, 0x16a000} } },/* 30: P2NR2 */
    {{{1, 0x1f00000, 0x1f10000, 0x150000} } },/* 31: EPG */
	{{{0} } },				/* 32: PCI */
	{{{1, 0x2100000, 0x2102000, 0x120000},	/* 33: PCIE */
	  {1, 0x2110000, 0x2120000, 0x130000},
	  {1, 0x2120000, 0x2122000, 0x124000},
	  {1, 0x2130000, 0x2132000, 0x126000},
	  {1, 0x2140000, 0x2142000, 0x128000},
	  {1, 0x2150000, 0x2152000, 0x12a000},
	  {1, 0x2160000, 0x2170000, 0x110000},
	  {1, 0x2170000, 0x2172000, 0x12e000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000} } },
	{{{1, 0x2200000, 0x2204000, 0x1b0000} } },/* 34: CAM */
	{{{0} } },				/* 35: */
	{{{0} } },				/* 36: */
	{{{0} } },				/* 37: */
	{{{0} } },				/* 38: */
	{{{0} } },				/* 39: */
	{{{1, 0x2800000, 0x2804000, 0x1a4000} } },/* 40: TMR */
	{{{1, 0x2900000, 0x2901000, 0x16b000} } },/* 41: P2NR3 */
	{{{1, 0x2a00000, 0x2a00400, 0x1ac400} } },/* 42: RPMX1 */
	{{{1, 0x2b00000, 0x2b00400, 0x1ac800} } },/* 43: RPMX2 */
	{{{1, 0x2c00000, 0x2c00400, 0x1acc00} } },/* 44: RPMX3 */
	{{{1, 0x2d00000, 0x2d00400, 0x1ad000} } },/* 45: RPMX4 */
	{{{1, 0x2e00000, 0x2e00400, 0x1ad400} } },/* 46: RPMX5 */
	{{{1, 0x2f00000, 0x2f00400, 0x1ad800} } },/* 47: RPMX6 */
	{{{1, 0x3000000, 0x3000400, 0x1adc00} } },/* 48: RPMX7 */
	{{{0, 0x3100000, 0x3104000, 0x1a8000} } },/* 49: XDMA */
	{{{1, 0x3200000, 0x3204000, 0x1d4000} } },/* 50: I2Q */
	{{{1, 0x3300000, 0x3304000, 0x1a0000} } },/* 51: ROMUSB */
	{{{0} } },				/* 52: */
	{{{1, 0x3500000, 0x3500400, 0x1ac000} } },/* 53: RPMX0 */
	{{{1, 0x3600000, 0x3600400, 0x1ae000} } },/* 54: RPMX8 */
	{{{1, 0x3700000, 0x3700400, 0x1ae400} } },/* 55: RPMX9 */
	{{{1, 0x3800000, 0x3804000, 0x1d0000} } },/* 56: OCM0 */
	{{{1, 0x3900000, 0x3904000, 0x1b4000} } },/* 57: CRYPTO */
	{{{1, 0x3a00000, 0x3a04000, 0x1d8000} } },/* 58: SMB */
	{{{0} } },				/* 59: I2C0 */
	{{{0} } },				/* 60: I2C1 */
	{{{1, 0x3d00000, 0x3d04000, 0x1d8000} } },/* 61: LPC */
	{{{1, 0x3e00000, 0x3e01000, 0x167000} } },/* 62: P2NC */
	{{{1, 0x3f00000, 0x3f01000, 0x168000} } }	/* 63: P2NR0 */
};

/*
 * top 12 bits of crb internal address (hub, agent)
 */
static unsigned crb_hub_agt[64] =
{
	0,
	NETXEN_HW_CRB_HUB_AGT_ADR_PS,
	NETXEN_HW_CRB_HUB_AGT_ADR_MN,
	NETXEN_HW_CRB_HUB_AGT_ADR_MS,
	0,
	NETXEN_HW_CRB_HUB_AGT_ADR_SRE,
	NETXEN_HW_CRB_HUB_AGT_ADR_NIU,
	NETXEN_HW_CRB_HUB_AGT_ADR_QMN,
	NETXEN_HW_CRB_HUB_AGT_ADR_SQN0,
	NETXEN_HW_CRB_HUB_AGT_ADR_SQN1,
	NETXEN_HW_CRB_HUB_AGT_ADR_SQN2,
	NETXEN_HW_CRB_HUB_AGT_ADR_SQN3,
	NETXEN_HW_CRB_HUB_AGT_ADR_I2Q,
	NETXEN_HW_CRB_HUB_AGT_ADR_TIMR,
	NETXEN_HW_CRB_HUB_AGT_ADR_ROMUSB,
	NETXEN_HW_CRB_HUB_AGT_ADR_PGN4,
	NETXEN_HW_CRB_HUB_AGT_ADR_XDMA,
	NETXEN_HW_CRB_HUB_AGT_ADR_PGN0,
	NETXEN_HW_CRB_HUB_AGT_ADR_PGN1,
	NETXEN_HW_CRB_HUB_AGT_ADR_PGN2,
	NETXEN_HW_CRB_HUB_AGT_ADR_PGN3,
	NETXEN_HW_CRB_HUB_AGT_ADR_PGND,
	NETXEN_HW_CRB_HUB_AGT_ADR_PGNI,
	NETXEN_HW_CRB_HUB_AGT_ADR_PGS0,
	NETXEN_HW_CRB_HUB_AGT_ADR_PGS1,
	NETXEN_HW_CRB_HUB_AGT_ADR_PGS2,
	NETXEN_HW_CRB_HUB_AGT_ADR_PGS3,
	0,
	NETXEN_HW_CRB_HUB_AGT_ADR_PGSI,
	NETXEN_HW_CRB_HUB_AGT_ADR_SN,
	0,
	NETXEN_HW_CRB_HUB_AGT_ADR_EG,
	0,
	NETXEN_HW_CRB_HUB_AGT_ADR_PS,
	NETXEN_HW_CRB_HUB_AGT_ADR_CAM,
	0,
	0,
	0,
	0,
	0,
	NETXEN_HW_CRB_HUB_AGT_ADR_TIMR,
	0,
	NETXEN_HW_CRB_HUB_AGT_ADR_RPMX1,
	NETXEN_HW_CRB_HUB_AGT_ADR_RPMX2,
	NETXEN_HW_CRB_HUB_AGT_ADR_RPMX3,
	NETXEN_HW_CRB_HUB_AGT_ADR_RPMX4,
	NETXEN_HW_CRB_HUB_AGT_ADR_RPMX5,
	NETXEN_HW_CRB_HUB_AGT_ADR_RPMX6,
	NETXEN_HW_CRB_HUB_AGT_ADR_RPMX7,
	NETXEN_HW_CRB_HUB_AGT_ADR_XDMA,
	NETXEN_HW_CRB_HUB_AGT_ADR_I2Q,
	NETXEN_HW_CRB_HUB_AGT_ADR_ROMUSB,
	0,
	NETXEN_HW_CRB_HUB_AGT_ADR_RPMX0,
	NETXEN_HW_CRB_HUB_AGT_ADR_RPMX8,
	NETXEN_HW_CRB_HUB_AGT_ADR_RPMX9,
	NETXEN_HW_CRB_HUB_AGT_ADR_OCM0,
	0,
	NETXEN_HW_CRB_HUB_AGT_ADR_SMB,
	NETXEN_HW_CRB_HUB_AGT_ADR_I2C0,
	NETXEN_HW_CRB_HUB_AGT_ADR_I2C1,
	0,
	NETXEN_HW_CRB_HUB_AGT_ADR_PGNC,
	0,
};

/*  PCI Windowing for DDR regions.  */

#define ADDR_IN_RANGE(addr, low, high)	\
	(((addr) <= (high)) && ((addr) >= (low)))

#define NETXEN_WINDOW_ONE 	0x2000000 /*CRB Window: bit 25 of CRB address */

#define NETXEN_NIC_ZERO_PAUSE_ADDR     0ULL
#define NETXEN_NIC_UNIT_PAUSE_ADDR     0x200ULL
#define NETXEN_NIC_EPG_PAUSE_ADDR1     0x2200010000c28001ULL
#define NETXEN_NIC_EPG_PAUSE_ADDR2     0x0100088866554433ULL

#define NETXEN_NIC_WINDOW_MARGIN 0x100000

int netxen_nic_set_mac(struct net_device *netdev, void *p)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (netif_running(netdev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);

	/* For P3, MAC addr is not set in NIU */
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		if (adapter->macaddr_set)
			adapter->macaddr_set(adapter, addr->sa_data);

	return 0;
}

#define NETXEN_UNICAST_ADDR(port, index) \
	(NETXEN_UNICAST_ADDR_BASE+(port*32)+(index*8))
#define NETXEN_MCAST_ADDR(port, index) \
	(NETXEN_MULTICAST_ADDR_BASE+(port*0x80)+(index*8))
#define MAC_HI(addr) \
	((addr[2] << 16) | (addr[1] << 8) | (addr[0]))
#define MAC_LO(addr) \
	((addr[5] << 16) | (addr[4] << 8) | (addr[3]))

static int
netxen_nic_enable_mcast_filter(struct netxen_adapter *adapter)
{
	u32	val = 0;
	u16 port = adapter->physical_port;
	u8 *addr = adapter->netdev->dev_addr;

	if (adapter->mc_enabled)
		return 0;

	adapter->hw_read_wx(adapter, NETXEN_MAC_ADDR_CNTL_REG, &val, 4);
	val |= (1UL << (28+port));
	adapter->hw_write_wx(adapter, NETXEN_MAC_ADDR_CNTL_REG, &val, 4);

	/* add broadcast addr to filter */
	val = 0xffffff;
	netxen_crb_writelit_adapter(adapter, NETXEN_UNICAST_ADDR(port, 0), val);
	netxen_crb_writelit_adapter(adapter,
			NETXEN_UNICAST_ADDR(port, 0)+4, val);

	/* add station addr to filter */
	val = MAC_HI(addr);
	netxen_crb_writelit_adapter(adapter, NETXEN_UNICAST_ADDR(port, 1), val);
	val = MAC_LO(addr);
	netxen_crb_writelit_adapter(adapter,
			NETXEN_UNICAST_ADDR(port, 1)+4, val);

	adapter->mc_enabled = 1;
	return 0;
}

static int
netxen_nic_disable_mcast_filter(struct netxen_adapter *adapter)
{
	u32	val = 0;
	u16 port = adapter->physical_port;
	u8 *addr = adapter->netdev->dev_addr;

	if (!adapter->mc_enabled)
		return 0;

	adapter->hw_read_wx(adapter, NETXEN_MAC_ADDR_CNTL_REG, &val, 4);
	val &= ~(1UL << (28+port));
	adapter->hw_write_wx(adapter, NETXEN_MAC_ADDR_CNTL_REG, &val, 4);

	val = MAC_HI(addr);
	netxen_crb_writelit_adapter(adapter, NETXEN_UNICAST_ADDR(port, 0), val);
	val = MAC_LO(addr);
	netxen_crb_writelit_adapter(adapter,
			NETXEN_UNICAST_ADDR(port, 0)+4, val);

	netxen_crb_writelit_adapter(adapter, NETXEN_UNICAST_ADDR(port, 1), 0);
	netxen_crb_writelit_adapter(adapter, NETXEN_UNICAST_ADDR(port, 1)+4, 0);

	adapter->mc_enabled = 0;
	return 0;
}

static int
netxen_nic_set_mcast_addr(struct netxen_adapter *adapter,
		int index, u8 *addr)
{
	u32 hi = 0, lo = 0;
	u16 port = adapter->physical_port;

	lo = MAC_LO(addr);
	hi = MAC_HI(addr);

	netxen_crb_writelit_adapter(adapter,
			NETXEN_MCAST_ADDR(port, index), hi);
	netxen_crb_writelit_adapter(adapter,
			NETXEN_MCAST_ADDR(port, index)+4, lo);

	return 0;
}

void netxen_p2_nic_set_multi(struct net_device *netdev)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct dev_mc_list *mc_ptr;
	u8 null_addr[6];
	int index = 0;

	memset(null_addr, 0, 6);

	if (netdev->flags & IFF_PROMISC) {

		adapter->set_promisc(adapter,
				NETXEN_NIU_PROMISC_MODE);

		/* Full promiscuous mode */
		netxen_nic_disable_mcast_filter(adapter);

		return;
	}

	if (netdev->mc_count == 0) {
		adapter->set_promisc(adapter,
				NETXEN_NIU_NON_PROMISC_MODE);
		netxen_nic_disable_mcast_filter(adapter);
		return;
	}

	adapter->set_promisc(adapter, NETXEN_NIU_ALLMULTI_MODE);
	if (netdev->flags & IFF_ALLMULTI ||
			netdev->mc_count > adapter->max_mc_count) {
		netxen_nic_disable_mcast_filter(adapter);
		return;
	}

	netxen_nic_enable_mcast_filter(adapter);

	for (mc_ptr = netdev->mc_list; mc_ptr; mc_ptr = mc_ptr->next, index++)
		netxen_nic_set_mcast_addr(adapter, index, mc_ptr->dmi_addr);

	if (index != netdev->mc_count)
		printk(KERN_WARNING "%s: %s multicast address count mismatch\n",
			netxen_nic_driver_name, netdev->name);

	/* Clear out remaining addresses */
	for (; index < adapter->max_mc_count; index++)
		netxen_nic_set_mcast_addr(adapter, index, null_addr);
}

static int nx_p3_nic_add_mac(struct netxen_adapter *adapter,
		u8 *addr, nx_mac_list_t **add_list, nx_mac_list_t **del_list)
{
	nx_mac_list_t *cur, *prev;

	/* if in del_list, move it to adapter->mac_list */
	for (cur = *del_list, prev = NULL; cur;) {
		if (memcmp(addr, cur->mac_addr, ETH_ALEN) == 0) {
			if (prev == NULL)
				*del_list = cur->next;
			else
				prev->next = cur->next;
			cur->next = adapter->mac_list;
			adapter->mac_list = cur;
			return 0;
		}
		prev = cur;
		cur = cur->next;
	}

	/* make sure to add each mac address only once */
	for (cur = adapter->mac_list; cur; cur = cur->next) {
		if (memcmp(addr, cur->mac_addr, ETH_ALEN) == 0)
			return 0;
	}
	/* not in del_list, create new entry and add to add_list */
	cur = kmalloc(sizeof(*cur), in_atomic()? GFP_ATOMIC : GFP_KERNEL);
	if (cur == NULL) {
		printk(KERN_ERR "%s: cannot allocate memory. MAC filtering may"
				"not work properly from now.\n", __func__);
		return -1;
	}

	memcpy(cur->mac_addr, addr, ETH_ALEN);
	cur->next = *add_list;
	*add_list = cur;
	return 0;
}

static int
netxen_send_cmd_descs(struct netxen_adapter *adapter,
		struct cmd_desc_type0 *cmd_desc_arr, int nr_elements)
{
	uint32_t i, producer;
	struct netxen_cmd_buffer *pbuf;
	struct cmd_desc_type0 *cmd_desc;

	if (nr_elements > MAX_PENDING_DESC_BLOCK_SIZE || nr_elements == 0) {
		printk(KERN_WARNING "%s: Too many command descriptors in a "
			      "request\n", __func__);
		return -EINVAL;
	}

	i = 0;

	netif_tx_lock_bh(adapter->netdev);

	producer = adapter->cmd_producer;
	do {
		cmd_desc = &cmd_desc_arr[i];

		pbuf = &adapter->cmd_buf_arr[producer];
		pbuf->skb = NULL;
		pbuf->frag_count = 0;

		/* adapter->ahw.cmd_desc_head[producer] = *cmd_desc; */
		memcpy(&adapter->ahw.cmd_desc_head[producer],
			&cmd_desc_arr[i], sizeof(struct cmd_desc_type0));

		producer = get_next_index(producer,
				adapter->num_txd);
		i++;

	} while (i != nr_elements);

	adapter->cmd_producer = producer;

	/* write producer index to start the xmit */

	netxen_nic_update_cmd_producer(adapter, adapter->cmd_producer);

	netif_tx_unlock_bh(adapter->netdev);

	return 0;
}

static int nx_p3_sre_macaddr_change(struct net_device *dev,
		u8 *addr, unsigned op)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	nx_nic_req_t req;
	nx_mac_req_t *mac_req;
	u64 word;
	int rv;

	memset(&req, 0, sizeof(nx_nic_req_t));
	req.qhdr = cpu_to_le64(NX_NIC_REQUEST << 23);

	word = NX_MAC_EVENT | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	mac_req = (nx_mac_req_t *)&req.words[0];
	mac_req->op = op;
	memcpy(mac_req->mac_addr, addr, 6);

	rv = netxen_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0) {
		printk(KERN_ERR "ERROR. Could not send mac update\n");
		return rv;
	}

	return 0;
}

void netxen_p3_nic_set_multi(struct net_device *netdev)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	nx_mac_list_t *cur, *next, *del_list, *add_list = NULL;
	struct dev_mc_list *mc_ptr;
	u8 bcast_addr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	u32 mode = VPORT_MISS_MODE_DROP;

	del_list = adapter->mac_list;
	adapter->mac_list = NULL;

	nx_p3_nic_add_mac(adapter, netdev->dev_addr, &add_list, &del_list);
	nx_p3_nic_add_mac(adapter, bcast_addr, &add_list, &del_list);

	if (netdev->flags & IFF_PROMISC) {
		mode = VPORT_MISS_MODE_ACCEPT_ALL;
		goto send_fw_cmd;
	}

	if ((netdev->flags & IFF_ALLMULTI) ||
			(netdev->mc_count > adapter->max_mc_count)) {
		mode = VPORT_MISS_MODE_ACCEPT_MULTI;
		goto send_fw_cmd;
	}

	if (netdev->mc_count > 0) {
		for (mc_ptr = netdev->mc_list; mc_ptr;
		     mc_ptr = mc_ptr->next) {
			nx_p3_nic_add_mac(adapter, mc_ptr->dmi_addr,
					  &add_list, &del_list);
		}
	}

send_fw_cmd:
	adapter->set_promisc(adapter, mode);
	for (cur = del_list; cur;) {
		nx_p3_sre_macaddr_change(netdev, cur->mac_addr, NETXEN_MAC_DEL);
		next = cur->next;
		kfree(cur);
		cur = next;
	}
	for (cur = add_list; cur;) {
		nx_p3_sre_macaddr_change(netdev, cur->mac_addr, NETXEN_MAC_ADD);
		next = cur->next;
		cur->next = adapter->mac_list;
		adapter->mac_list = cur;
		cur = next;
	}
}

int netxen_p3_nic_set_promisc(struct netxen_adapter *adapter, u32 mode)
{
	nx_nic_req_t req;
	u64 word;

	memset(&req, 0, sizeof(nx_nic_req_t));

	req.qhdr = cpu_to_le64(NX_HOST_REQUEST << 23);

	word = NX_NIC_H2C_OPCODE_PROXY_SET_VPORT_MISS_MODE |
			((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	req.words[0] = cpu_to_le64(mode);

	return netxen_send_cmd_descs(adapter,
				(struct cmd_desc_type0 *)&req, 1);
}

void netxen_p3_free_mac_list(struct netxen_adapter *adapter)
{
	nx_mac_list_t *cur, *next;

	cur = adapter->mac_list;

	while (cur) {
		next = cur->next;
		kfree(cur);
		cur = next;
	}
}

#define	NETXEN_CONFIG_INTR_COALESCE	3

/*
 * Send the interrupt coalescing parameter set by ethtool to the card.
 */
int netxen_config_intr_coalesce(struct netxen_adapter *adapter)
{
	nx_nic_req_t req;
	u64 word;
	int rv;

	memset(&req, 0, sizeof(nx_nic_req_t));

	req.qhdr = cpu_to_le64(NX_NIC_REQUEST << 23);

	word = NETXEN_CONFIG_INTR_COALESCE | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	memcpy(&req.words[0], &adapter->coal, sizeof(adapter->coal));

	rv = netxen_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0) {
		printk(KERN_ERR "ERROR. Could not send "
			"interrupt coalescing parameters\n");
	}

	return rv;
}

#define RSS_HASHTYPE_IP_TCP	0x3

int netxen_config_rss(struct netxen_adapter *adapter, int enable)
{
	nx_nic_req_t req;
	u64 word;
	int i, rv;

	u64 key[] = { 0xbeac01fa6a42b73bULL, 0x8030f20c77cb2da3ULL,
			0xae7b30b4d0ca2bcbULL, 0x43a38fb04167253dULL,
			0x255b0ec26d5a56daULL };


	memset(&req, 0, sizeof(nx_nic_req_t));
	req.qhdr = cpu_to_le64(NX_HOST_REQUEST << 23);

	word = NX_NIC_H2C_OPCODE_CONFIG_RSS | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	/*
	 * RSS request:
	 * bits 3-0: hash_method
	 *      5-4: hash_type_ipv4
	 *	7-6: hash_type_ipv6
	 *	  8: enable
	 *        9: use indirection table
	 *    47-10: reserved
	 *    63-48: indirection table mask
	 */
	word =  ((u64)(RSS_HASHTYPE_IP_TCP & 0x3) << 4) |
		((u64)(RSS_HASHTYPE_IP_TCP & 0x3) << 6) |
		((u64)(enable & 0x1) << 8) |
		((0x7ULL) << 48);
	req.words[0] = cpu_to_le64(word);
	for (i = 0; i < 5; i++)
		req.words[i+1] = cpu_to_le64(key[i]);


	rv = netxen_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0) {
		printk(KERN_ERR "%s: could not configure RSS\n",
				adapter->netdev->name);
	}

	return rv;
}

/*
 * netxen_nic_change_mtu - Change the Maximum Transfer Unit
 * @returns 0 on success, negative on failure
 */

#define MTU_FUDGE_FACTOR	100

int netxen_nic_change_mtu(struct net_device *netdev, int mtu)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	int max_mtu;
	int rc = 0;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		max_mtu = P3_MAX_MTU;
	else
		max_mtu = P2_MAX_MTU;

	if (mtu > max_mtu) {
		printk(KERN_ERR "%s: mtu > %d bytes unsupported\n",
				netdev->name, max_mtu);
		return -EINVAL;
	}

	if (adapter->set_mtu)
		rc = adapter->set_mtu(adapter, mtu);

	if (!rc)
		netdev->mtu = mtu;

	return rc;
}

static int netxen_get_flash_block(struct netxen_adapter *adapter, int base,
				  int size, __le32 * buf)
{
	int i, v, addr;
	__le32 *ptr32;

	addr = base;
	ptr32 = buf;
	for (i = 0; i < size / sizeof(u32); i++) {
		if (netxen_rom_fast_read(adapter, addr, &v) == -1)
			return -1;
		*ptr32 = cpu_to_le32(v);
		ptr32++;
		addr += sizeof(u32);
	}
	if ((char *)buf + size > (char *)ptr32) {
		__le32 local;
		if (netxen_rom_fast_read(adapter, addr, &v) == -1)
			return -1;
		local = cpu_to_le32(v);
		memcpy(ptr32, &local, (char *)buf + size - (char *)ptr32);
	}

	return 0;
}

int netxen_get_flash_mac_addr(struct netxen_adapter *adapter, __le64 *mac)
{
	__le32 *pmac = (__le32 *) mac;
	u32 offset;

	offset = NETXEN_USER_START +
		offsetof(struct netxen_new_user_info, mac_addr) +
		adapter->portnum * sizeof(u64);

	if (netxen_get_flash_block(adapter, offset, sizeof(u64), pmac) == -1)
		return -1;

	if (*mac == cpu_to_le64(~0ULL)) {

		offset = NETXEN_USER_START_OLD +
			offsetof(struct netxen_user_old_info, mac_addr) +
			adapter->portnum * sizeof(u64);

		if (netxen_get_flash_block(adapter,
					offset, sizeof(u64), pmac) == -1)
			return -1;

		if (*mac == cpu_to_le64(~0ULL))
			return -1;
	}
	return 0;
}

int netxen_p3_get_mac_addr(struct netxen_adapter *adapter, __le64 *mac)
{
	uint32_t crbaddr, mac_hi, mac_lo;
	int pci_func = adapter->ahw.pci_func;

	crbaddr = CRB_MAC_BLOCK_START +
		(4 * ((pci_func/2) * 3)) + (4 * (pci_func & 1));

	adapter->hw_read_wx(adapter, crbaddr, &mac_lo, 4);
	adapter->hw_read_wx(adapter, crbaddr+4, &mac_hi, 4);

	if (pci_func & 1)
		*mac = le64_to_cpu((mac_lo >> 16) | ((u64)mac_hi << 16));
	else
		*mac = le64_to_cpu((u64)mac_lo | ((u64)mac_hi << 32));

	return 0;
}

#define CRB_WIN_LOCK_TIMEOUT 100000000

static int crb_win_lock(struct netxen_adapter *adapter)
{
	int done = 0, timeout = 0;

	while (!done) {
		/* acquire semaphore3 from PCI HW block */
		adapter->hw_read_wx(adapter,
				NETXEN_PCIE_REG(PCIE_SEM7_LOCK), &done, 4);
		if (done == 1)
			break;
		if (timeout >= CRB_WIN_LOCK_TIMEOUT)
			return -1;
		timeout++;
		udelay(1);
	}
	netxen_crb_writelit_adapter(adapter,
			NETXEN_CRB_WIN_LOCK_ID, adapter->portnum);
	return 0;
}

static void crb_win_unlock(struct netxen_adapter *adapter)
{
	int val;

	adapter->hw_read_wx(adapter,
			NETXEN_PCIE_REG(PCIE_SEM7_UNLOCK), &val, 4);
}

/*
 * Changes the CRB window to the specified window.
 */
void
netxen_nic_pci_change_crbwindow_128M(struct netxen_adapter *adapter, u32 wndw)
{
	void __iomem *offset;
	u32 tmp;
	int count = 0;
	uint8_t func = adapter->ahw.pci_func;

	if (adapter->curr_window == wndw)
		return;
	/*
	 * Move the CRB window.
	 * We need to write to the "direct access" region of PCI
	 * to avoid a race condition where the window register has
	 * not been successfully written across CRB before the target
	 * register address is received by PCI. The direct region bypasses
	 * the CRB bus.
	 */
	offset = PCI_OFFSET_SECOND_RANGE(adapter,
			NETXEN_PCIX_PH_REG(PCIE_CRB_WINDOW_REG(func)));

	if (wndw & 0x1)
		wndw = NETXEN_WINDOW_ONE;

	writel(wndw, offset);

	/* MUST make sure window is set before we forge on... */
	while ((tmp = readl(offset)) != wndw) {
		printk(KERN_WARNING "%s: %s WARNING: CRB window value not "
		       "registered properly: 0x%08x.\n",
		       netxen_nic_driver_name, __func__, tmp);
		mdelay(1);
		if (count >= 10)
			break;
		count++;
	}

	if (wndw == NETXEN_WINDOW_ONE)
		adapter->curr_window = 1;
	else
		adapter->curr_window = 0;
}

/*
 * Return -1 if off is not valid,
 *	 1 if window access is needed. 'off' is set to offset from
 *	   CRB space in 128M pci map
 *	 0 if no window access is needed. 'off' is set to 2M addr
 * In: 'off' is offset from base in 128M pci map
 */
static int
netxen_nic_pci_get_crb_addr_2M(struct netxen_adapter *adapter,
		ulong *off, int len)
{
	unsigned long end = *off + len;
	crb_128M_2M_sub_block_map_t *m;


	if (*off >= NETXEN_CRB_MAX)
		return -1;

	if (*off >= NETXEN_PCI_CAMQM && (end <= NETXEN_PCI_CAMQM_2M_END)) {
		*off = (*off - NETXEN_PCI_CAMQM) + NETXEN_PCI_CAMQM_2M_BASE +
			(ulong)adapter->ahw.pci_base0;
		return 0;
	}

	if (*off < NETXEN_PCI_CRBSPACE)
		return -1;

	*off -= NETXEN_PCI_CRBSPACE;
	end = *off + len;

	/*
	 * Try direct map
	 */
	m = &crb_128M_2M_map[CRB_BLK(*off)].sub_block[CRB_SUBBLK(*off)];

	if (m->valid && (m->start_128M <= *off) && (m->end_128M >= end)) {
		*off = *off + m->start_2M - m->start_128M +
			(ulong)adapter->ahw.pci_base0;
		return 0;
	}

	/*
	 * Not in direct map, use crb window
	 */
	return 1;
}

/*
 * In: 'off' is offset from CRB space in 128M pci map
 * Out: 'off' is 2M pci map addr
 * side effect: lock crb window
 */
static void
netxen_nic_pci_set_crbwindow_2M(struct netxen_adapter *adapter, ulong *off)
{
	u32 win_read;

	adapter->crb_win = CRB_HI(*off);
	writel(adapter->crb_win, (adapter->ahw.pci_base0 + CRB_WINDOW_2M));
	/*
	 * Read back value to make sure write has gone through before trying
	 * to use it.
	 */
	win_read = readl(adapter->ahw.pci_base0 + CRB_WINDOW_2M);
	if (win_read != adapter->crb_win) {
		printk(KERN_ERR "%s: Written crbwin (0x%x) != "
				"Read crbwin (0x%x), off=0x%lx\n",
				__func__, adapter->crb_win, win_read, *off);
	}
	*off = (*off & MASK(16)) + CRB_INDIRECT_2M +
		(ulong)adapter->ahw.pci_base0;
}

static int
netxen_do_load_firmware(struct netxen_adapter *adapter, const char *fwname,
		const struct firmware *fw)
{
	u64 *ptr64;
	u32 i, flashaddr, size;
	struct pci_dev *pdev = adapter->pdev;

	if (fw)
		dev_info(&pdev->dev, "loading firmware from file %s\n", fwname);
	else
		dev_info(&pdev->dev, "loading firmware from flash\n");

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		adapter->pci_write_normalize(adapter,
				NETXEN_ROMUSB_GLB_CAS_RST, 1);

	if (fw) {
		__le64 data;

		size = (NETXEN_IMAGE_START - NETXEN_BOOTLD_START) / 8;

		ptr64 = (u64 *)&fw->data[NETXEN_BOOTLD_START];
		flashaddr = NETXEN_BOOTLD_START;

		for (i = 0; i < size; i++) {
			data = cpu_to_le64(ptr64[i]);
			adapter->pci_mem_write(adapter, flashaddr, &data, 8);
			flashaddr += 8;
		}

		size = *(u32 *)&fw->data[NX_FW_SIZE_OFFSET];
		size = (__force u32)cpu_to_le32(size) / 8;

		ptr64 = (u64 *)&fw->data[NETXEN_IMAGE_START];
		flashaddr = NETXEN_IMAGE_START;

		for (i = 0; i < size; i++) {
			data = cpu_to_le64(ptr64[i]);

			if (adapter->pci_mem_write(adapter,
						flashaddr, &data, 8))
				return -EIO;

			flashaddr += 8;
		}
	} else {
		u32 data;

		size = (NETXEN_IMAGE_START - NETXEN_BOOTLD_START) / 4;
		flashaddr = NETXEN_BOOTLD_START;

		for (i = 0; i < size; i++) {
			if (netxen_rom_fast_read(adapter,
					flashaddr, (int *)&data) != 0)
				return -EIO;

			if (adapter->pci_mem_write(adapter,
						flashaddr, &data, 4))
				return -EIO;

			flashaddr += 4;
		}
	}
	msleep(1);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		adapter->pci_write_normalize(adapter,
				NETXEN_ROMUSB_GLB_SW_RESET, 0x80001d);
	else {
		adapter->pci_write_normalize(adapter,
				NETXEN_ROMUSB_GLB_CHIP_CLK_CTRL, 0x3fff);
		adapter->pci_write_normalize(adapter,
				NETXEN_ROMUSB_GLB_CAS_RST, 0);
	}

	return 0;
}

static int
netxen_validate_firmware(struct netxen_adapter *adapter, const char *fwname,
		const struct firmware *fw)
{
	__le32 val;
	u32 major, minor, build, ver, min_ver, bios;
	struct pci_dev *pdev = adapter->pdev;

	if (fw->size < NX_FW_MIN_SIZE)
		return -EINVAL;

	val = cpu_to_le32(*(u32 *)&fw->data[NX_FW_MAGIC_OFFSET]);
	if ((__force u32)val != NETXEN_BDINFO_MAGIC)
		return -EINVAL;

	val = cpu_to_le32(*(u32 *)&fw->data[NX_FW_VERSION_OFFSET]);
	major = (__force u32)val & 0xff;
	minor = ((__force u32)val >> 8) & 0xff;
	build = (__force u32)val >> 16;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		min_ver = NETXEN_VERSION_CODE(4, 0, 216);
	else
		min_ver = NETXEN_VERSION_CODE(3, 4, 216);

	ver = NETXEN_VERSION_CODE(major, minor, build);

	if ((major > _NETXEN_NIC_LINUX_MAJOR) || (ver < min_ver)) {
		dev_err(&pdev->dev,
				"%s: firmware version %d.%d.%d unsupported\n",
				fwname, major, minor, build);
		return -EINVAL;
	}

	val = cpu_to_le32(*(u32 *)&fw->data[NX_BIOS_VERSION_OFFSET]);
	netxen_rom_fast_read(adapter, NX_BIOS_VERSION_OFFSET, (int *)&bios);
	if ((__force u32)val != bios) {
		dev_err(&pdev->dev, "%s: firmware bios is incompatible\n",
				fwname);
		return -EINVAL;
	}

	/* check if flashed firmware is newer */
	if (netxen_rom_fast_read(adapter,
			NX_FW_VERSION_OFFSET, (int *)&val))
		return -EIO;
	major = (__force u32)val & 0xff;
	minor = ((__force u32)val >> 8) & 0xff;
	build = (__force u32)val >> 16;
	if (NETXEN_VERSION_CODE(major, minor, build) > ver)
		return -EINVAL;

	netxen_nic_reg_write(adapter, NETXEN_CAM_RAM(0x1fc),
			NETXEN_BDINFO_MAGIC);
	return 0;
}

static char *fw_name[] = { "nxromimg.bin", "nx3fwct.bin", "nx3fwmn.bin" };

int netxen_load_firmware(struct netxen_adapter *adapter)
{
	u32 capability, flashed_ver;
	const struct firmware *fw;
	int fw_type;
	struct pci_dev *pdev = adapter->pdev;
	int rc = 0;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		fw_type = NX_P2_MN_ROMIMAGE;
		goto request_fw;
	} else {
		fw_type = NX_P3_CT_ROMIMAGE;
		goto request_fw;
	}

request_mn:
	capability = 0;

	netxen_rom_fast_read(adapter,
			NX_FW_VERSION_OFFSET, (int *)&flashed_ver);
	if (flashed_ver >= NETXEN_VERSION_CODE(4, 0, 220)) {
		adapter->hw_read_wx(adapter,
				NX_PEG_TUNE_CAPABILITY, &capability, 4);
		if (capability & NX_PEG_TUNE_MN_PRESENT) {
			fw_type = NX_P3_MN_ROMIMAGE;
			goto request_fw;
		}
	}

request_fw:
	rc = request_firmware(&fw, fw_name[fw_type], &pdev->dev);
	if (rc != 0) {
		if (fw_type == NX_P3_CT_ROMIMAGE) {
			msleep(1);
			goto request_mn;
		}

		fw = NULL;
		goto load_fw;
	}

	rc = netxen_validate_firmware(adapter, fw_name[fw_type], fw);
	if (rc != 0) {
		release_firmware(fw);

		if (fw_type == NX_P3_CT_ROMIMAGE) {
			msleep(1);
			goto request_mn;
		}

		fw = NULL;
	}

load_fw:
	rc = netxen_do_load_firmware(adapter, fw_name[fw_type], fw);

	if (fw)
		release_firmware(fw);
	return rc;
}

int
netxen_nic_hw_write_wx_128M(struct netxen_adapter *adapter,
		ulong off, void *data, int len)
{
	void __iomem *addr;

	BUG_ON(len != 4);

	if (ADDR_IN_WINDOW1(off)) {
		addr = NETXEN_CRB_NORMALIZE(adapter, off);
	} else {		/* Window 0 */
		addr = pci_base_offset(adapter, off);
		netxen_nic_pci_change_crbwindow_128M(adapter, 0);
	}

	if (!addr) {
		netxen_nic_pci_change_crbwindow_128M(adapter, 1);
		return 1;
	}

	writel(*(u32 *) data, addr);

	if (!ADDR_IN_WINDOW1(off))
		netxen_nic_pci_change_crbwindow_128M(adapter, 1);

	return 0;
}

int
netxen_nic_hw_read_wx_128M(struct netxen_adapter *adapter,
		ulong off, void *data, int len)
{
	void __iomem *addr;

	BUG_ON(len != 4);

	if (ADDR_IN_WINDOW1(off)) {	/* Window 1 */
		addr = NETXEN_CRB_NORMALIZE(adapter, off);
	} else {		/* Window 0 */
		addr = pci_base_offset(adapter, off);
		netxen_nic_pci_change_crbwindow_128M(adapter, 0);
	}

	if (!addr) {
		netxen_nic_pci_change_crbwindow_128M(adapter, 1);
		return 1;
	}

	*(u32 *)data = readl(addr);

	if (!ADDR_IN_WINDOW1(off))
		netxen_nic_pci_change_crbwindow_128M(adapter, 1);

	return 0;
}

int
netxen_nic_hw_write_wx_2M(struct netxen_adapter *adapter,
		ulong off, void *data, int len)
{
	unsigned long flags = 0;
	int rv;

	BUG_ON(len != 4);

	rv = netxen_nic_pci_get_crb_addr_2M(adapter, &off, len);

	if (rv == -1) {
		printk(KERN_ERR "%s: invalid offset: 0x%016lx\n",
				__func__, off);
		dump_stack();
		return -1;
	}

	if (rv == 1) {
		write_lock_irqsave(&adapter->adapter_lock, flags);
		crb_win_lock(adapter);
		netxen_nic_pci_set_crbwindow_2M(adapter, &off);
		writel(*(uint32_t *)data, (void __iomem *)off);
		crb_win_unlock(adapter);
		write_unlock_irqrestore(&adapter->adapter_lock, flags);
	} else
		writel(*(uint32_t *)data, (void __iomem *)off);


	return 0;
}

int
netxen_nic_hw_read_wx_2M(struct netxen_adapter *adapter,
		ulong off, void *data, int len)
{
	unsigned long flags = 0;
	int rv;

	BUG_ON(len != 4);

	rv = netxen_nic_pci_get_crb_addr_2M(adapter, &off, len);

	if (rv == -1) {
		printk(KERN_ERR "%s: invalid offset: 0x%016lx\n",
				__func__, off);
		dump_stack();
		return -1;
	}

	if (rv == 1) {
		write_lock_irqsave(&adapter->adapter_lock, flags);
		crb_win_lock(adapter);
		netxen_nic_pci_set_crbwindow_2M(adapter, &off);
		*(uint32_t *)data = readl((void __iomem *)off);
		crb_win_unlock(adapter);
		write_unlock_irqrestore(&adapter->adapter_lock, flags);
	} else
		*(uint32_t *)data = readl((void __iomem *)off);

	return 0;
}

void netxen_nic_reg_write(struct netxen_adapter *adapter, u64 off, u32 val)
{
	adapter->hw_write_wx(adapter, off, &val, 4);
}

int netxen_nic_reg_read(struct netxen_adapter *adapter, u64 off)
{
	int val;
	adapter->hw_read_wx(adapter, off, &val, 4);
	return val;
}

/* Change the window to 0, write and change back to window 1. */
void netxen_nic_write_w0(struct netxen_adapter *adapter, u32 index, u32 value)
{
	adapter->hw_write_wx(adapter, index, &value, 4);
}

/* Change the window to 0, read and change back to window 1. */
void netxen_nic_read_w0(struct netxen_adapter *adapter, u32 index, u32 *value)
{
	adapter->hw_read_wx(adapter, index, value, 4);
}

void netxen_nic_write_w1(struct netxen_adapter *adapter, u32 index, u32 value)
{
	adapter->hw_write_wx(adapter, index, &value, 4);
}

void netxen_nic_read_w1(struct netxen_adapter *adapter, u32 index, u32 *value)
{
	adapter->hw_read_wx(adapter, index, value, 4);
}

/*
 * check memory access boundary.
 * used by test agent. support ddr access only for now
 */
static unsigned long
netxen_nic_pci_mem_bound_check(struct netxen_adapter *adapter,
		unsigned long long addr, int size)
{
	if (!ADDR_IN_RANGE(addr,
			NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX) ||
		!ADDR_IN_RANGE(addr+size-1,
			NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX) ||
		((size != 1) && (size != 2) && (size != 4) && (size != 8))) {
		return 0;
	}

	return 1;
}

static int netxen_pci_set_window_warning_count;

unsigned long
netxen_nic_pci_set_window_128M(struct netxen_adapter *adapter,
		unsigned long long addr)
{
	void __iomem *offset;
	int window;
	unsigned long long	qdr_max;
	uint8_t func = adapter->ahw.pci_func;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		qdr_max = NETXEN_ADDR_QDR_NET_MAX_P2;
	} else {
		qdr_max = NETXEN_ADDR_QDR_NET_MAX_P3;
	}

	if (ADDR_IN_RANGE(addr, NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX)) {
		/* DDR network side */
		addr -= NETXEN_ADDR_DDR_NET;
		window = (addr >> 25) & 0x3ff;
		if (adapter->ahw.ddr_mn_window != window) {
			adapter->ahw.ddr_mn_window = window;
			offset = PCI_OFFSET_SECOND_RANGE(adapter,
				NETXEN_PCIX_PH_REG(PCIE_MN_WINDOW_REG(func)));
			writel(window, offset);
			/* MUST make sure window is set before we forge on... */
			readl(offset);
		}
		addr -= (window * NETXEN_WINDOW_ONE);
		addr += NETXEN_PCI_DDR_NET;
	} else if (ADDR_IN_RANGE(addr, NETXEN_ADDR_OCM0, NETXEN_ADDR_OCM0_MAX)) {
		addr -= NETXEN_ADDR_OCM0;
		addr += NETXEN_PCI_OCM0;
	} else if (ADDR_IN_RANGE(addr, NETXEN_ADDR_OCM1, NETXEN_ADDR_OCM1_MAX)) {
		addr -= NETXEN_ADDR_OCM1;
		addr += NETXEN_PCI_OCM1;
	} else if (ADDR_IN_RANGE(addr, NETXEN_ADDR_QDR_NET, qdr_max)) {
		/* QDR network side */
		addr -= NETXEN_ADDR_QDR_NET;
		window = (addr >> 22) & 0x3f;
		if (adapter->ahw.qdr_sn_window != window) {
			adapter->ahw.qdr_sn_window = window;
			offset = PCI_OFFSET_SECOND_RANGE(adapter,
				NETXEN_PCIX_PH_REG(PCIE_SN_WINDOW_REG(func)));
			writel((window << 22), offset);
			/* MUST make sure window is set before we forge on... */
			readl(offset);
		}
		addr -= (window * 0x400000);
		addr += NETXEN_PCI_QDR_NET;
	} else {
		/*
		 * peg gdb frequently accesses memory that doesn't exist,
		 * this limits the chit chat so debugging isn't slowed down.
		 */
		if ((netxen_pci_set_window_warning_count++ < 8)
		    || (netxen_pci_set_window_warning_count % 64 == 0))
			printk("%s: Warning:netxen_nic_pci_set_window()"
			       " Unknown address range!\n",
			       netxen_nic_driver_name);
		addr = -1UL;
	}
	return addr;
}

/*
 * Note : only 32-bit writes!
 */
int netxen_nic_pci_write_immediate_128M(struct netxen_adapter *adapter,
		u64 off, u32 data)
{
	writel(data, (void __iomem *)(PCI_OFFSET_SECOND_RANGE(adapter, off)));
	return 0;
}

u32 netxen_nic_pci_read_immediate_128M(struct netxen_adapter *adapter, u64 off)
{
	return readl((void __iomem *)(pci_base_offset(adapter, off)));
}

void netxen_nic_pci_write_normalize_128M(struct netxen_adapter *adapter,
		u64 off, u32 data)
{
	writel(data, NETXEN_CRB_NORMALIZE(adapter, off));
}

u32 netxen_nic_pci_read_normalize_128M(struct netxen_adapter *adapter, u64 off)
{
	return readl(NETXEN_CRB_NORMALIZE(adapter, off));
}

unsigned long
netxen_nic_pci_set_window_2M(struct netxen_adapter *adapter,
		unsigned long long addr)
{
	int window;
	u32 win_read;

	if (ADDR_IN_RANGE(addr, NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX)) {
		/* DDR network side */
		window = MN_WIN(addr);
		adapter->ahw.ddr_mn_window = window;
		adapter->hw_write_wx(adapter,
				adapter->ahw.mn_win_crb | NETXEN_PCI_CRBSPACE,
				&window, 4);
		adapter->hw_read_wx(adapter,
				adapter->ahw.mn_win_crb | NETXEN_PCI_CRBSPACE,
				&win_read, 4);
		if ((win_read << 17) != window) {
			printk(KERN_INFO "Written MNwin (0x%x) != "
				"Read MNwin (0x%x)\n", window, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + NETXEN_PCI_DDR_NET;
	} else if (ADDR_IN_RANGE(addr,
				NETXEN_ADDR_OCM0, NETXEN_ADDR_OCM0_MAX)) {
		if ((addr & 0x00ff800) == 0xff800) {
			printk("%s: QM access not handled.\n", __func__);
			addr = -1UL;
		}

		window = OCM_WIN(addr);
		adapter->ahw.ddr_mn_window = window;
		adapter->hw_write_wx(adapter,
				adapter->ahw.mn_win_crb | NETXEN_PCI_CRBSPACE,
				&window, 4);
		adapter->hw_read_wx(adapter,
				adapter->ahw.mn_win_crb | NETXEN_PCI_CRBSPACE,
				&win_read, 4);
		if ((win_read >> 7) != window) {
			printk(KERN_INFO "%s: Written OCMwin (0x%x) != "
					"Read OCMwin (0x%x)\n",
					__func__, window, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + NETXEN_PCI_OCM0_2M;

	} else if (ADDR_IN_RANGE(addr,
			NETXEN_ADDR_QDR_NET, NETXEN_ADDR_QDR_NET_MAX_P3)) {
		/* QDR network side */
		window = MS_WIN(addr);
		adapter->ahw.qdr_sn_window = window;
		adapter->hw_write_wx(adapter,
				adapter->ahw.ms_win_crb | NETXEN_PCI_CRBSPACE,
				&window, 4);
		adapter->hw_read_wx(adapter,
				adapter->ahw.ms_win_crb | NETXEN_PCI_CRBSPACE,
				&win_read, 4);
		if (win_read != window) {
			printk(KERN_INFO "%s: Written MSwin (0x%x) != "
					"Read MSwin (0x%x)\n",
					__func__, window, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + NETXEN_PCI_QDR_NET;

	} else {
		/*
		 * peg gdb frequently accesses memory that doesn't exist,
		 * this limits the chit chat so debugging isn't slowed down.
		 */
		if ((netxen_pci_set_window_warning_count++ < 8)
			|| (netxen_pci_set_window_warning_count%64 == 0)) {
			printk("%s: Warning:%s Unknown address range!\n",
					__func__, netxen_nic_driver_name);
}
		addr = -1UL;
	}
	return addr;
}

static int netxen_nic_pci_is_same_window(struct netxen_adapter *adapter,
				      unsigned long long addr)
{
	int window;
	unsigned long long qdr_max;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		qdr_max = NETXEN_ADDR_QDR_NET_MAX_P2;
	else
		qdr_max = NETXEN_ADDR_QDR_NET_MAX_P3;

	if (ADDR_IN_RANGE(addr,
			NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX)) {
		/* DDR network side */
		BUG();	/* MN access can not come here */
	} else if (ADDR_IN_RANGE(addr,
			NETXEN_ADDR_OCM0, NETXEN_ADDR_OCM0_MAX)) {
		return 1;
	} else if (ADDR_IN_RANGE(addr,
				NETXEN_ADDR_OCM1, NETXEN_ADDR_OCM1_MAX)) {
		return 1;
	} else if (ADDR_IN_RANGE(addr, NETXEN_ADDR_QDR_NET, qdr_max)) {
		/* QDR network side */
		window = ((addr - NETXEN_ADDR_QDR_NET) >> 22) & 0x3f;
		if (adapter->ahw.qdr_sn_window == window)
			return 1;
	}

	return 0;
}

static int netxen_nic_pci_mem_read_direct(struct netxen_adapter *adapter,
			u64 off, void *data, int size)
{
	unsigned long flags;
	void __iomem *addr, *mem_ptr = NULL;
	int ret = 0;
	u64 start;
	unsigned long mem_base;
	unsigned long mem_page;

	write_lock_irqsave(&adapter->adapter_lock, flags);

	/*
	 * If attempting to access unknown address or straddle hw windows,
	 * do not access.
	 */
	start = adapter->pci_set_window(adapter, off);
	if ((start == -1UL) ||
		(netxen_nic_pci_is_same_window(adapter, off+size-1) == 0)) {
		write_unlock_irqrestore(&adapter->adapter_lock, flags);
		printk(KERN_ERR "%s out of bound pci memory access. "
			"offset is 0x%llx\n", netxen_nic_driver_name,
			(unsigned long long)off);
		return -1;
	}

	addr = pci_base_offset(adapter, start);
	if (!addr) {
		write_unlock_irqrestore(&adapter->adapter_lock, flags);
		mem_base = pci_resource_start(adapter->pdev, 0);
		mem_page = start & PAGE_MASK;
		/* Map two pages whenever user tries to access addresses in two
		consecutive pages.
		*/
		if (mem_page != ((start + size - 1) & PAGE_MASK))
			mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE * 2);
		else
			mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE);
		if (mem_ptr == NULL) {
			*(uint8_t  *)data = 0;
			return -1;
		}
		addr = mem_ptr;
		addr += start & (PAGE_SIZE - 1);
		write_lock_irqsave(&adapter->adapter_lock, flags);
	}

	switch (size) {
	case 1:
		*(uint8_t  *)data = readb(addr);
		break;
	case 2:
		*(uint16_t *)data = readw(addr);
		break;
	case 4:
		*(uint32_t *)data = readl(addr);
		break;
	case 8:
		*(uint64_t *)data = readq(addr);
		break;
	default:
		ret = -1;
		break;
	}
	write_unlock_irqrestore(&adapter->adapter_lock, flags);

	if (mem_ptr)
		iounmap(mem_ptr);
	return ret;
}

static int
netxen_nic_pci_mem_write_direct(struct netxen_adapter *adapter, u64 off,
		void *data, int size)
{
	unsigned long flags;
	void __iomem *addr, *mem_ptr = NULL;
	int ret = 0;
	u64 start;
	unsigned long mem_base;
	unsigned long mem_page;

	write_lock_irqsave(&adapter->adapter_lock, flags);

	/*
	 * If attempting to access unknown address or straddle hw windows,
	 * do not access.
	 */
	start = adapter->pci_set_window(adapter, off);
	if ((start == -1UL) ||
		(netxen_nic_pci_is_same_window(adapter, off+size-1) == 0)) {
		write_unlock_irqrestore(&adapter->adapter_lock, flags);
		printk(KERN_ERR "%s out of bound pci memory access. "
			"offset is 0x%llx\n", netxen_nic_driver_name,
			(unsigned long long)off);
		return -1;
	}

	addr = pci_base_offset(adapter, start);
	if (!addr) {
		write_unlock_irqrestore(&adapter->adapter_lock, flags);
		mem_base = pci_resource_start(adapter->pdev, 0);
		mem_page = start & PAGE_MASK;
		/* Map two pages whenever user tries to access addresses in two
		 * consecutive pages.
		 */
		if (mem_page != ((start + size - 1) & PAGE_MASK))
			mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE*2);
		else
			mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE);
		if (mem_ptr == NULL)
			return -1;
		addr = mem_ptr;
		addr += start & (PAGE_SIZE - 1);
		write_lock_irqsave(&adapter->adapter_lock, flags);
	}

	switch (size) {
	case 1:
		writeb(*(uint8_t *)data, addr);
		break;
	case 2:
		writew(*(uint16_t *)data, addr);
		break;
	case 4:
		writel(*(uint32_t *)data, addr);
		break;
	case 8:
		writeq(*(uint64_t *)data, addr);
		break;
	default:
		ret = -1;
		break;
	}
	write_unlock_irqrestore(&adapter->adapter_lock, flags);
	if (mem_ptr)
		iounmap(mem_ptr);
	return ret;
}

#define MAX_CTL_CHECK   1000

int
netxen_nic_pci_mem_write_128M(struct netxen_adapter *adapter,
		u64 off, void *data, int size)
{
	unsigned long   flags;
	int	     i, j, ret = 0, loop, sz[2], off0;
	uint32_t      temp;
	uint64_t      off8, tmpw, word[2] = {0, 0};
	void __iomem *mem_crb;

	/*
	 * If not MN, go check for MS or invalid.
	 */
	if (netxen_nic_pci_mem_bound_check(adapter, off, size) == 0)
		return netxen_nic_pci_mem_write_direct(adapter,
				off, data, size);

	off8 = off & 0xfffffff8;
	off0 = off & 0x7;
	sz[0] = (size < (8 - off0)) ? size : (8 - off0);
	sz[1] = size - sz[0];
	loop = ((off0 + size - 1) >> 3) + 1;
	mem_crb = pci_base_offset(adapter, NETXEN_CRB_DDR_NET);

	if ((size != 8) || (off0 != 0))  {
		for (i = 0; i < loop; i++) {
			if (adapter->pci_mem_read(adapter,
				off8 + (i << 3), &word[i], 8))
				return -1;
		}
	}

	switch (size) {
	case 1:
		tmpw = *((uint8_t *)data);
		break;
	case 2:
		tmpw = *((uint16_t *)data);
		break;
	case 4:
		tmpw = *((uint32_t *)data);
		break;
	case 8:
	default:
		tmpw = *((uint64_t *)data);
		break;
	}
	word[0] &= ~((~(~0ULL << (sz[0] * 8))) << (off0 * 8));
	word[0] |= tmpw << (off0 * 8);

	if (loop == 2) {
		word[1] &= ~(~0ULL << (sz[1] * 8));
		word[1] |= tmpw >> (sz[0] * 8);
	}

	write_lock_irqsave(&adapter->adapter_lock, flags);
	netxen_nic_pci_change_crbwindow_128M(adapter, 0);

	for (i = 0; i < loop; i++) {
		writel((uint32_t)(off8 + (i << 3)),
			(mem_crb+MIU_TEST_AGT_ADDR_LO));
		writel(0,
			(mem_crb+MIU_TEST_AGT_ADDR_HI));
		writel(word[i] & 0xffffffff,
			(mem_crb+MIU_TEST_AGT_WRDATA_LO));
		writel((word[i] >> 32) & 0xffffffff,
			(mem_crb+MIU_TEST_AGT_WRDATA_HI));
		writel(MIU_TA_CTL_ENABLE|MIU_TA_CTL_WRITE,
			(mem_crb+MIU_TEST_AGT_CTRL));
		writel(MIU_TA_CTL_START|MIU_TA_CTL_ENABLE|MIU_TA_CTL_WRITE,
			(mem_crb+MIU_TEST_AGT_CTRL));

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			temp = readl(
			     (mem_crb+MIU_TEST_AGT_CTRL));
			if ((temp & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			if (printk_ratelimit())
				dev_err(&adapter->pdev->dev,
					"failed to write through agent\n");
			ret = -1;
			break;
		}
	}

	netxen_nic_pci_change_crbwindow_128M(adapter, 1);
	write_unlock_irqrestore(&adapter->adapter_lock, flags);
	return ret;
}

int
netxen_nic_pci_mem_read_128M(struct netxen_adapter *adapter,
		u64 off, void *data, int size)
{
	unsigned long   flags;
	int	     i, j = 0, k, start, end, loop, sz[2], off0[2];
	uint32_t      temp;
	uint64_t      off8, val, word[2] = {0, 0};
	void __iomem *mem_crb;


	/*
	 * If not MN, go check for MS or invalid.
	 */
	if (netxen_nic_pci_mem_bound_check(adapter, off, size) == 0)
		return netxen_nic_pci_mem_read_direct(adapter, off, data, size);

	off8 = off & 0xfffffff8;
	off0[0] = off & 0x7;
	off0[1] = 0;
	sz[0] = (size < (8 - off0[0])) ? size : (8 - off0[0]);
	sz[1] = size - sz[0];
	loop = ((off0[0] + size - 1) >> 3) + 1;
	mem_crb = pci_base_offset(adapter, NETXEN_CRB_DDR_NET);

	write_lock_irqsave(&adapter->adapter_lock, flags);
	netxen_nic_pci_change_crbwindow_128M(adapter, 0);

	for (i = 0; i < loop; i++) {
		writel((uint32_t)(off8 + (i << 3)),
			(mem_crb+MIU_TEST_AGT_ADDR_LO));
		writel(0,
			(mem_crb+MIU_TEST_AGT_ADDR_HI));
		writel(MIU_TA_CTL_ENABLE,
			(mem_crb+MIU_TEST_AGT_CTRL));
		writel(MIU_TA_CTL_START|MIU_TA_CTL_ENABLE,
			(mem_crb+MIU_TEST_AGT_CTRL));

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			temp = readl(
			      (mem_crb+MIU_TEST_AGT_CTRL));
			if ((temp & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			if (printk_ratelimit())
				dev_err(&adapter->pdev->dev,
					"failed to read through agent\n");
			break;
		}

		start = off0[i] >> 2;
		end   = (off0[i] + sz[i] - 1) >> 2;
		for (k = start; k <= end; k++) {
			word[i] |= ((uint64_t) readl(
				    (mem_crb +
				    MIU_TEST_AGT_RDDATA(k))) << (32*k));
		}
	}

	netxen_nic_pci_change_crbwindow_128M(adapter, 1);
	write_unlock_irqrestore(&adapter->adapter_lock, flags);

	if (j >= MAX_CTL_CHECK)
		return -1;

	if (sz[0] == 8) {
		val = word[0];
	} else {
		val = ((word[0] >> (off0[0] * 8)) & (~(~0ULL << (sz[0] * 8)))) |
			((word[1] & (~(~0ULL << (sz[1] * 8)))) << (sz[0] * 8));
	}

	switch (size) {
	case 1:
		*(uint8_t  *)data = val;
		break;
	case 2:
		*(uint16_t *)data = val;
		break;
	case 4:
		*(uint32_t *)data = val;
		break;
	case 8:
		*(uint64_t *)data = val;
		break;
	}
	return 0;
}

int
netxen_nic_pci_mem_write_2M(struct netxen_adapter *adapter,
		u64 off, void *data, int size)
{
	int i, j, ret = 0, loop, sz[2], off0;
	uint32_t temp;
	uint64_t off8, mem_crb, tmpw, word[2] = {0, 0};

	/*
	 * If not MN, go check for MS or invalid.
	 */
	if (off >= NETXEN_ADDR_QDR_NET && off <= NETXEN_ADDR_QDR_NET_MAX_P3)
		mem_crb = NETXEN_CRB_QDR_NET;
	else {
		mem_crb = NETXEN_CRB_DDR_NET;
		if (netxen_nic_pci_mem_bound_check(adapter, off, size) == 0)
			return netxen_nic_pci_mem_write_direct(adapter,
					off, data, size);
	}

	off8 = off & 0xfffffff8;
	off0 = off & 0x7;
	sz[0] = (size < (8 - off0)) ? size : (8 - off0);
	sz[1] = size - sz[0];
	loop = ((off0 + size - 1) >> 3) + 1;

	if ((size != 8) || (off0 != 0)) {
		for (i = 0; i < loop; i++) {
			if (adapter->pci_mem_read(adapter, off8 + (i << 3),
						&word[i], 8))
				return -1;
		}
	}

	switch (size) {
	case 1:
		tmpw = *((uint8_t *)data);
		break;
	case 2:
		tmpw = *((uint16_t *)data);
		break;
	case 4:
		tmpw = *((uint32_t *)data);
		break;
	case 8:
	default:
		tmpw = *((uint64_t *)data);
	break;
	}

	word[0] &= ~((~(~0ULL << (sz[0] * 8))) << (off0 * 8));
	word[0] |= tmpw << (off0 * 8);

	if (loop == 2) {
		word[1] &= ~(~0ULL << (sz[1] * 8));
		word[1] |= tmpw >> (sz[0] * 8);
	}

	/*
	 * don't lock here - write_wx gets the lock if each time
	 * write_lock_irqsave(&adapter->adapter_lock, flags);
	 * netxen_nic_pci_change_crbwindow_128M(adapter, 0);
	 */

	for (i = 0; i < loop; i++) {
		temp = off8 + (i << 3);
		adapter->hw_write_wx(adapter,
				mem_crb+MIU_TEST_AGT_ADDR_LO, &temp, 4);
		temp = 0;
		adapter->hw_write_wx(adapter,
				mem_crb+MIU_TEST_AGT_ADDR_HI, &temp, 4);
		temp = word[i] & 0xffffffff;
		adapter->hw_write_wx(adapter,
				mem_crb+MIU_TEST_AGT_WRDATA_LO, &temp, 4);
		temp = (word[i] >> 32) & 0xffffffff;
		adapter->hw_write_wx(adapter,
				mem_crb+MIU_TEST_AGT_WRDATA_HI, &temp, 4);
		temp = MIU_TA_CTL_ENABLE | MIU_TA_CTL_WRITE;
		adapter->hw_write_wx(adapter,
				mem_crb+MIU_TEST_AGT_CTRL, &temp, 4);
		temp = MIU_TA_CTL_START | MIU_TA_CTL_ENABLE | MIU_TA_CTL_WRITE;
		adapter->hw_write_wx(adapter,
				mem_crb+MIU_TEST_AGT_CTRL, &temp, 4);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			adapter->hw_read_wx(adapter,
					mem_crb + MIU_TEST_AGT_CTRL, &temp, 4);
			if ((temp & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			if (printk_ratelimit())
				dev_err(&adapter->pdev->dev,
					"failed to write through agent\n");
			ret = -1;
			break;
		}
	}

	/*
	 * netxen_nic_pci_change_crbwindow_128M(adapter, 1);
	 * write_unlock_irqrestore(&adapter->adapter_lock, flags);
	 */
	return ret;
}

int
netxen_nic_pci_mem_read_2M(struct netxen_adapter *adapter,
		u64 off, void *data, int size)
{
	int i, j = 0, k, start, end, loop, sz[2], off0[2];
	uint32_t      temp;
	uint64_t      off8, val, mem_crb, word[2] = {0, 0};

	/*
	 * If not MN, go check for MS or invalid.
	 */

	if (off >= NETXEN_ADDR_QDR_NET && off <= NETXEN_ADDR_QDR_NET_MAX_P3)
		mem_crb = NETXEN_CRB_QDR_NET;
	else {
		mem_crb = NETXEN_CRB_DDR_NET;
		if (netxen_nic_pci_mem_bound_check(adapter, off, size) == 0)
			return netxen_nic_pci_mem_read_direct(adapter,
					off, data, size);
	}

	off8 = off & 0xfffffff8;
	off0[0] = off & 0x7;
	off0[1] = 0;
	sz[0] = (size < (8 - off0[0])) ? size : (8 - off0[0]);
	sz[1] = size - sz[0];
	loop = ((off0[0] + size - 1) >> 3) + 1;

	/*
	 * don't lock here - write_wx gets the lock if each time
	 * write_lock_irqsave(&adapter->adapter_lock, flags);
	 * netxen_nic_pci_change_crbwindow_128M(adapter, 0);
	 */

	for (i = 0; i < loop; i++) {
		temp = off8 + (i << 3);
		adapter->hw_write_wx(adapter,
				mem_crb + MIU_TEST_AGT_ADDR_LO, &temp, 4);
		temp = 0;
		adapter->hw_write_wx(adapter,
				mem_crb + MIU_TEST_AGT_ADDR_HI, &temp, 4);
		temp = MIU_TA_CTL_ENABLE;
		adapter->hw_write_wx(adapter,
				mem_crb + MIU_TEST_AGT_CTRL, &temp, 4);
		temp = MIU_TA_CTL_START | MIU_TA_CTL_ENABLE;
		adapter->hw_write_wx(adapter,
				mem_crb + MIU_TEST_AGT_CTRL, &temp, 4);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			adapter->hw_read_wx(adapter,
					mem_crb + MIU_TEST_AGT_CTRL, &temp, 4);
			if ((temp & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			if (printk_ratelimit())
				dev_err(&adapter->pdev->dev,
					"failed to read through agent\n");
			break;
		}

		start = off0[i] >> 2;
		end   = (off0[i] + sz[i] - 1) >> 2;
		for (k = start; k <= end; k++) {
			adapter->hw_read_wx(adapter,
				mem_crb + MIU_TEST_AGT_RDDATA(k), &temp, 4);
			word[i] |= ((uint64_t)temp << (32 * k));
		}
	}

	/*
	 * netxen_nic_pci_change_crbwindow_128M(adapter, 1);
	 * write_unlock_irqrestore(&adapter->adapter_lock, flags);
	 */

	if (j >= MAX_CTL_CHECK)
		return -1;

	if (sz[0] == 8) {
		val = word[0];
	} else {
		val = ((word[0] >> (off0[0] * 8)) & (~(~0ULL << (sz[0] * 8)))) |
		((word[1] & (~(~0ULL << (sz[1] * 8)))) << (sz[0] * 8));
	}

	switch (size) {
	case 1:
		*(uint8_t  *)data = val;
		break;
	case 2:
		*(uint16_t *)data = val;
		break;
	case 4:
		*(uint32_t *)data = val;
		break;
	case 8:
		*(uint64_t *)data = val;
		break;
	}
	return 0;
}

/*
 * Note : only 32-bit writes!
 */
int netxen_nic_pci_write_immediate_2M(struct netxen_adapter *adapter,
		u64 off, u32 data)
{
	adapter->hw_write_wx(adapter, off, &data, 4);

	return 0;
}

u32 netxen_nic_pci_read_immediate_2M(struct netxen_adapter *adapter, u64 off)
{
	u32 temp;
	adapter->hw_read_wx(adapter, off, &temp, 4);
	return temp;
}

void netxen_nic_pci_write_normalize_2M(struct netxen_adapter *adapter,
		u64 off, u32 data)
{
	adapter->hw_write_wx(adapter, off, &data, 4);
}

u32 netxen_nic_pci_read_normalize_2M(struct netxen_adapter *adapter, u64 off)
{
	u32 temp;
	adapter->hw_read_wx(adapter, off, &temp, 4);
	return temp;
}

int netxen_nic_get_board_info(struct netxen_adapter *adapter)
{
	int offset, board_type, magic, header_version;
	struct pci_dev *pdev = adapter->pdev;

	offset = NETXEN_BRDCFG_START +
		offsetof(struct netxen_board_info, magic);
	if (netxen_rom_fast_read(adapter, offset, &magic))
		return -EIO;

	offset = NETXEN_BRDCFG_START +
		offsetof(struct netxen_board_info, header_version);
	if (netxen_rom_fast_read(adapter, offset, &header_version))
		return -EIO;

	if (magic != NETXEN_BDINFO_MAGIC ||
			header_version != NETXEN_BDINFO_VERSION) {
		dev_err(&pdev->dev,
			"invalid board config, magic=%08x, version=%08x\n",
			magic, header_version);
		return -EIO;
	}

	offset = NETXEN_BRDCFG_START +
		offsetof(struct netxen_board_info, board_type);
	if (netxen_rom_fast_read(adapter, offset, &board_type))
		return -EIO;

	adapter->ahw.board_type = board_type;

	if (board_type == NETXEN_BRDTYPE_P3_4_GB_MM) {
		u32 gpio = netxen_nic_reg_read(adapter,
				NETXEN_ROMUSB_GLB_PAD_GPIO_I);
		if ((gpio & 0x8000) == 0)
			board_type = NETXEN_BRDTYPE_P3_10G_TP;
	}

	switch (board_type) {
	case NETXEN_BRDTYPE_P2_SB35_4G:
		adapter->ahw.port_type = NETXEN_NIC_GBE;
		break;
	case NETXEN_BRDTYPE_P2_SB31_10G:
	case NETXEN_BRDTYPE_P2_SB31_10G_IMEZ:
	case NETXEN_BRDTYPE_P2_SB31_10G_HMEZ:
	case NETXEN_BRDTYPE_P2_SB31_10G_CX4:
	case NETXEN_BRDTYPE_P3_HMEZ:
	case NETXEN_BRDTYPE_P3_XG_LOM:
	case NETXEN_BRDTYPE_P3_10G_CX4:
	case NETXEN_BRDTYPE_P3_10G_CX4_LP:
	case NETXEN_BRDTYPE_P3_IMEZ:
	case NETXEN_BRDTYPE_P3_10G_SFP_PLUS:
	case NETXEN_BRDTYPE_P3_10G_SFP_CT:
	case NETXEN_BRDTYPE_P3_10G_SFP_QT:
	case NETXEN_BRDTYPE_P3_10G_XFP:
	case NETXEN_BRDTYPE_P3_10000_BASE_T:
		adapter->ahw.port_type = NETXEN_NIC_XGBE;
		break;
	case NETXEN_BRDTYPE_P1_BD:
	case NETXEN_BRDTYPE_P1_SB:
	case NETXEN_BRDTYPE_P1_SMAX:
	case NETXEN_BRDTYPE_P1_SOCK:
	case NETXEN_BRDTYPE_P3_REF_QG:
	case NETXEN_BRDTYPE_P3_4_GB:
	case NETXEN_BRDTYPE_P3_4_GB_MM:
		adapter->ahw.port_type = NETXEN_NIC_GBE;
		break;
	case NETXEN_BRDTYPE_P3_10G_TP:
		adapter->ahw.port_type = (adapter->portnum < 2) ?
			NETXEN_NIC_XGBE : NETXEN_NIC_GBE;
		break;
	default:
		dev_err(&pdev->dev, "unknown board type %x\n", board_type);
		adapter->ahw.port_type = NETXEN_NIC_XGBE;
		break;
	}

	return 0;
}

/* NIU access sections */

int netxen_nic_set_mtu_gb(struct netxen_adapter *adapter, int new_mtu)
{
	new_mtu += MTU_FUDGE_FACTOR;
	netxen_nic_write_w0(adapter,
		NETXEN_NIU_GB_MAX_FRAME_SIZE(adapter->physical_port),
		new_mtu);
	return 0;
}

int netxen_nic_set_mtu_xgb(struct netxen_adapter *adapter, int new_mtu)
{
	new_mtu += MTU_FUDGE_FACTOR;
	if (adapter->physical_port == 0)
		netxen_nic_write_w0(adapter, NETXEN_NIU_XGE_MAX_FRAME_SIZE,
				new_mtu);
	else
		netxen_nic_write_w0(adapter, NETXEN_NIU_XG1_MAX_FRAME_SIZE,
				new_mtu);
	return 0;
}

void
netxen_crb_writelit_adapter(struct netxen_adapter *adapter,
		unsigned long off, int data)
{
	adapter->hw_write_wx(adapter, off, &data, 4);
}

void netxen_nic_set_link_parameters(struct netxen_adapter *adapter)
{
	__u32 status;
	__u32 autoneg;
	__u32 port_mode;

	if (!netif_carrier_ok(adapter->netdev)) {
		adapter->link_speed   = 0;
		adapter->link_duplex  = -1;
		adapter->link_autoneg = AUTONEG_ENABLE;
		return;
	}

	if (adapter->ahw.port_type == NETXEN_NIC_GBE) {
		adapter->hw_read_wx(adapter,
				NETXEN_PORT_MODE_ADDR, &port_mode, 4);
		if (port_mode == NETXEN_PORT_MODE_802_3_AP) {
			adapter->link_speed   = SPEED_1000;
			adapter->link_duplex  = DUPLEX_FULL;
			adapter->link_autoneg = AUTONEG_DISABLE;
			return;
		}

		if (adapter->phy_read
		    && adapter->phy_read(adapter,
			     NETXEN_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
			     &status) == 0) {
			if (netxen_get_phy_link(status)) {
				switch (netxen_get_phy_speed(status)) {
				case 0:
					adapter->link_speed = SPEED_10;
					break;
				case 1:
					adapter->link_speed = SPEED_100;
					break;
				case 2:
					adapter->link_speed = SPEED_1000;
					break;
				default:
					adapter->link_speed = 0;
					break;
				}
				switch (netxen_get_phy_duplex(status)) {
				case 0:
					adapter->link_duplex = DUPLEX_HALF;
					break;
				case 1:
					adapter->link_duplex = DUPLEX_FULL;
					break;
				default:
					adapter->link_duplex = -1;
					break;
				}
				if (adapter->phy_read
				    && adapter->phy_read(adapter,
					     NETXEN_NIU_GB_MII_MGMT_ADDR_AUTONEG,
					     &autoneg) != 0)
					adapter->link_autoneg = autoneg;
			} else
				goto link_down;
		} else {
		      link_down:
			adapter->link_speed = 0;
			adapter->link_duplex = -1;
		}
	}
}

void netxen_nic_get_firmware_info(struct netxen_adapter *adapter)
{
	u32 fw_major, fw_minor, fw_build;
	char brd_name[NETXEN_MAX_SHORT_NAME];
	char serial_num[32];
	int i, addr, val;
	int *ptr32;
	struct pci_dev *pdev = adapter->pdev;

	adapter->driver_mismatch = 0;

	ptr32 = (int *)&serial_num;
	addr = NETXEN_USER_START +
	       offsetof(struct netxen_new_user_info, serial_num);
	for (i = 0; i < 8; i++) {
		if (netxen_rom_fast_read(adapter, addr, &val) == -1) {
			dev_err(&pdev->dev, "error reading board info\n");
			adapter->driver_mismatch = 1;
			return;
		}
		ptr32[i] = cpu_to_le32(val);
		addr += sizeof(u32);
	}

	adapter->hw_read_wx(adapter, NETXEN_FW_VERSION_MAJOR, &fw_major, 4);
	adapter->hw_read_wx(adapter, NETXEN_FW_VERSION_MINOR, &fw_minor, 4);
	adapter->hw_read_wx(adapter, NETXEN_FW_VERSION_SUB, &fw_build, 4);

	adapter->fw_major = fw_major;
	adapter->fw_version = NETXEN_VERSION_CODE(fw_major, fw_minor, fw_build);

	if (adapter->portnum == 0) {
		get_brd_name_by_type(adapter->ahw.board_type, brd_name);

		printk(KERN_INFO "NetXen %s Board S/N %s  Chip rev 0x%x\n",
				brd_name, serial_num, adapter->ahw.revision_id);
	}

	if (adapter->fw_version < NETXEN_VERSION_CODE(3, 4, 216)) {
		adapter->driver_mismatch = 1;
		dev_warn(&pdev->dev, "firmware version %d.%d.%d unsupported\n",
				fw_major, fw_minor, fw_build);
		return;
	}

	dev_info(&pdev->dev, "firmware version %d.%d.%d\n",
			fw_major, fw_minor, fw_build);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		adapter->hw_read_wx(adapter,
				NETXEN_MIU_MN_CONTROL, &i, 4);
		adapter->ahw.cut_through = (i & 0x4) ? 1 : 0;
		dev_info(&pdev->dev, "firmware running in %s mode\n",
		adapter->ahw.cut_through ? "cut-through" : "legacy");
	}
}

int
netxen_nic_wol_supported(struct netxen_adapter *adapter)
{
	u32 wol_cfg;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return 0;

	wol_cfg = netxen_nic_reg_read(adapter, NETXEN_WOL_CONFIG_NV);
	if (wol_cfg & (1UL << adapter->portnum)) {
		wol_cfg = netxen_nic_reg_read(adapter, NETXEN_WOL_CONFIG);
		if (wol_cfg & (1 << adapter->portnum))
			return 1;
	}

	return 0;
}
