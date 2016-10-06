/*
 * Copyright (C) 2003 - 2009 NetXen, Inc.
 * Copyright (C) 2009 - QLogic Corporation.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called "COPYING".
 *
 */

#include <linux/slab.h>
#include "netxen_nic.h"
#include "netxen_nic_hw.h"

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

static void netxen_nic_io_write_128M(struct netxen_adapter *adapter,
		void __iomem *addr, u32 data);
static u32 netxen_nic_io_read_128M(struct netxen_adapter *adapter,
		void __iomem *addr);
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

#define PCI_OFFSET_FIRST_RANGE(adapter, off)    \
	((adapter)->ahw.pci_base0 + (off))
#define PCI_OFFSET_SECOND_RANGE(adapter, off)   \
	((adapter)->ahw.pci_base1 + (off) - SECOND_PAGE_GROUP_START)
#define PCI_OFFSET_THIRD_RANGE(adapter, off)    \
	((adapter)->ahw.pci_base2 + (off) - THIRD_PAGE_GROUP_START)

static void __iomem *pci_base_offset(struct netxen_adapter *adapter,
					    unsigned long off)
{
	if (ADDR_IN_RANGE(off, FIRST_PAGE_GROUP_START, FIRST_PAGE_GROUP_END))
		return PCI_OFFSET_FIRST_RANGE(adapter, off);

	if (ADDR_IN_RANGE(off, SECOND_PAGE_GROUP_START, SECOND_PAGE_GROUP_END))
		return PCI_OFFSET_SECOND_RANGE(adapter, off);

	if (ADDR_IN_RANGE(off, THIRD_PAGE_GROUP_START, THIRD_PAGE_GROUP_END))
		return PCI_OFFSET_THIRD_RANGE(adapter, off);

	return NULL;
}

static crb_128M_2M_block_map_t
crb_128M_2M_map[64] __cacheline_aligned_in_smp = {
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

#define NETXEN_WINDOW_ONE 	0x2000000 /*CRB Window: bit 25 of CRB address */

#define NETXEN_PCIE_SEM_TIMEOUT	10000

static int netxen_nic_set_mtu_xgb(struct netxen_adapter *adapter, int new_mtu);

int
netxen_pcie_sem_lock(struct netxen_adapter *adapter, int sem, u32 id_reg)
{
	int done = 0, timeout = 0;

	while (!done) {
		done = NXRD32(adapter, NETXEN_PCIE_REG(PCIE_SEM_LOCK(sem)));
		if (done == 1)
			break;
		if (++timeout >= NETXEN_PCIE_SEM_TIMEOUT)
			return -EIO;
		msleep(1);
	}

	if (id_reg)
		NXWR32(adapter, id_reg, adapter->portnum);

	return 0;
}

void
netxen_pcie_sem_unlock(struct netxen_adapter *adapter, int sem)
{
	NXRD32(adapter, NETXEN_PCIE_REG(PCIE_SEM_UNLOCK(sem)));
}

static int netxen_niu_xg_init_port(struct netxen_adapter *adapter, int port)
{
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		NXWR32(adapter, NETXEN_NIU_XGE_CONFIG_1+(0x10000*port), 0x1447);
		NXWR32(adapter, NETXEN_NIU_XGE_CONFIG_0+(0x10000*port), 0x5);
	}

	return 0;
}

/* Disable an XG interface */
static int netxen_niu_disable_xg_port(struct netxen_adapter *adapter)
{
	__u32 mac_cfg;
	u32 port = adapter->physical_port;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		return 0;

	if (port >= NETXEN_NIU_MAX_XG_PORTS)
		return -EINVAL;

	mac_cfg = 0;
	if (NXWR32(adapter,
			NETXEN_NIU_XGE_CONFIG_0 + (0x10000 * port), mac_cfg))
		return -EIO;
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

static int netxen_p2_nic_set_promisc(struct netxen_adapter *adapter, u32 mode)
{
	u32 mac_cfg;
	u32 cnt = 0;
	__u32 reg = 0x0200;
	u32 port = adapter->physical_port;
	u16 board_type = adapter->ahw.board_type;

	if (port >= NETXEN_NIU_MAX_XG_PORTS)
		return -EINVAL;

	mac_cfg = NXRD32(adapter, NETXEN_NIU_XGE_CONFIG_0 + (0x10000 * port));
	mac_cfg &= ~0x4;
	NXWR32(adapter, NETXEN_NIU_XGE_CONFIG_0 + (0x10000 * port), mac_cfg);

	if ((board_type == NETXEN_BRDTYPE_P2_SB31_10G_IMEZ) ||
			(board_type == NETXEN_BRDTYPE_P2_SB31_10G_HMEZ))
		reg = (0x20 << port);

	NXWR32(adapter, NETXEN_NIU_FRAME_COUNT_SELECT, reg);

	mdelay(10);

	while (NXRD32(adapter, NETXEN_NIU_FRAME_COUNT) && ++cnt < 20)
		mdelay(10);

	if (cnt < 20) {

		reg = NXRD32(adapter,
			NETXEN_NIU_XGE_CONFIG_1 + (0x10000 * port));

		if (mode == NETXEN_NIU_PROMISC_MODE)
			reg = (reg | 0x2000UL);
		else
			reg = (reg & ~0x2000UL);

		if (mode == NETXEN_NIU_ALLMULTI_MODE)
			reg = (reg | 0x1000UL);
		else
			reg = (reg & ~0x1000UL);

		NXWR32(adapter,
			NETXEN_NIU_XGE_CONFIG_1 + (0x10000 * port), reg);
	}

	mac_cfg |= 0x4;
	NXWR32(adapter, NETXEN_NIU_XGE_CONFIG_0 + (0x10000 * port), mac_cfg);

	return 0;
}

static int netxen_p2_nic_set_mac_addr(struct netxen_adapter *adapter, u8 *addr)
{
	u32 mac_hi, mac_lo;
	u32 reg_hi, reg_lo;

	u8 phy = adapter->physical_port;

	if (phy >= NETXEN_NIU_MAX_XG_PORTS)
		return -EINVAL;

	mac_lo = ((u32)addr[0] << 16) | ((u32)addr[1] << 24);
	mac_hi = addr[2] | ((u32)addr[3] << 8) |
		((u32)addr[4] << 16) | ((u32)addr[5] << 24);

	reg_lo = NETXEN_NIU_XGE_STATION_ADDR_0_1 + (0x10000 * phy);
	reg_hi = NETXEN_NIU_XGE_STATION_ADDR_0_HI + (0x10000 * phy);

	/* write twice to flush */
	if (NXWR32(adapter, reg_lo, mac_lo) || NXWR32(adapter, reg_hi, mac_hi))
		return -EIO;
	if (NXWR32(adapter, reg_lo, mac_lo) || NXWR32(adapter, reg_hi, mac_hi))
		return -EIO;

	return 0;
}

static int
netxen_nic_enable_mcast_filter(struct netxen_adapter *adapter)
{
	u32	val = 0;
	u16 port = adapter->physical_port;
	u8 *addr = adapter->mac_addr;

	if (adapter->mc_enabled)
		return 0;

	val = NXRD32(adapter, NETXEN_MAC_ADDR_CNTL_REG);
	val |= (1UL << (28+port));
	NXWR32(adapter, NETXEN_MAC_ADDR_CNTL_REG, val);

	/* add broadcast addr to filter */
	val = 0xffffff;
	NXWR32(adapter, NETXEN_UNICAST_ADDR(port, 0), val);
	NXWR32(adapter, NETXEN_UNICAST_ADDR(port, 0)+4, val);

	/* add station addr to filter */
	val = MAC_HI(addr);
	NXWR32(adapter, NETXEN_UNICAST_ADDR(port, 1), val);
	val = MAC_LO(addr);
	NXWR32(adapter, NETXEN_UNICAST_ADDR(port, 1)+4, val);

	adapter->mc_enabled = 1;
	return 0;
}

static int
netxen_nic_disable_mcast_filter(struct netxen_adapter *adapter)
{
	u32	val = 0;
	u16 port = adapter->physical_port;
	u8 *addr = adapter->mac_addr;

	if (!adapter->mc_enabled)
		return 0;

	val = NXRD32(adapter, NETXEN_MAC_ADDR_CNTL_REG);
	val &= ~(1UL << (28+port));
	NXWR32(adapter, NETXEN_MAC_ADDR_CNTL_REG, val);

	val = MAC_HI(addr);
	NXWR32(adapter, NETXEN_UNICAST_ADDR(port, 0), val);
	val = MAC_LO(addr);
	NXWR32(adapter, NETXEN_UNICAST_ADDR(port, 0)+4, val);

	NXWR32(adapter, NETXEN_UNICAST_ADDR(port, 1), 0);
	NXWR32(adapter, NETXEN_UNICAST_ADDR(port, 1)+4, 0);

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

	NXWR32(adapter, NETXEN_MCAST_ADDR(port, index), hi);
	NXWR32(adapter, NETXEN_MCAST_ADDR(port, index)+4, lo);

	return 0;
}

static void netxen_p2_nic_set_multi(struct net_device *netdev)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct netdev_hw_addr *ha;
	u8 null_addr[ETH_ALEN];
	int i;

	eth_zero_addr(null_addr);

	if (netdev->flags & IFF_PROMISC) {

		adapter->set_promisc(adapter,
				NETXEN_NIU_PROMISC_MODE);

		/* Full promiscuous mode */
		netxen_nic_disable_mcast_filter(adapter);

		return;
	}

	if (netdev_mc_empty(netdev)) {
		adapter->set_promisc(adapter,
				NETXEN_NIU_NON_PROMISC_MODE);
		netxen_nic_disable_mcast_filter(adapter);
		return;
	}

	adapter->set_promisc(adapter, NETXEN_NIU_ALLMULTI_MODE);
	if (netdev->flags & IFF_ALLMULTI ||
			netdev_mc_count(netdev) > adapter->max_mc_count) {
		netxen_nic_disable_mcast_filter(adapter);
		return;
	}

	netxen_nic_enable_mcast_filter(adapter);

	i = 0;
	netdev_for_each_mc_addr(ha, netdev)
		netxen_nic_set_mcast_addr(adapter, i++, ha->addr);

	/* Clear out remaining addresses */
	while (i < adapter->max_mc_count)
		netxen_nic_set_mcast_addr(adapter, i++, null_addr);
}

static int
netxen_send_cmd_descs(struct netxen_adapter *adapter,
		struct cmd_desc_type0 *cmd_desc_arr, int nr_desc)
{
	u32 i, producer, consumer;
	struct netxen_cmd_buffer *pbuf;
	struct cmd_desc_type0 *cmd_desc;
	struct nx_host_tx_ring *tx_ring;

	i = 0;

	if (adapter->is_up != NETXEN_ADAPTER_UP_MAGIC)
		return -EIO;

	tx_ring = adapter->tx_ring;
	__netif_tx_lock_bh(tx_ring->txq);

	producer = tx_ring->producer;
	consumer = tx_ring->sw_consumer;

	if (nr_desc >= netxen_tx_avail(tx_ring)) {
		netif_tx_stop_queue(tx_ring->txq);
		smp_mb();
		if (netxen_tx_avail(tx_ring) > nr_desc) {
			if (netxen_tx_avail(tx_ring) > TX_STOP_THRESH)
				netif_tx_wake_queue(tx_ring->txq);
		} else {
			__netif_tx_unlock_bh(tx_ring->txq);
			return -EBUSY;
		}
	}

	do {
		cmd_desc = &cmd_desc_arr[i];

		pbuf = &tx_ring->cmd_buf_arr[producer];
		pbuf->skb = NULL;
		pbuf->frag_count = 0;

		memcpy(&tx_ring->desc_head[producer],
			&cmd_desc_arr[i], sizeof(struct cmd_desc_type0));

		producer = get_next_index(producer, tx_ring->num_desc);
		i++;

	} while (i != nr_desc);

	tx_ring->producer = producer;

	netxen_nic_update_cmd_producer(adapter, tx_ring);

	__netif_tx_unlock_bh(tx_ring->txq);

	return 0;
}

static int
nx_p3_sre_macaddr_change(struct netxen_adapter *adapter, u8 *addr, unsigned op)
{
	nx_nic_req_t req;
	nx_mac_req_t *mac_req;
	u64 word;

	memset(&req, 0, sizeof(nx_nic_req_t));
	req.qhdr = cpu_to_le64(NX_NIC_REQUEST << 23);

	word = NX_MAC_EVENT | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	mac_req = (nx_mac_req_t *)&req.words[0];
	mac_req->op = op;
	memcpy(mac_req->mac_addr, addr, ETH_ALEN);

	return netxen_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
}

static int nx_p3_nic_add_mac(struct netxen_adapter *adapter,
		const u8 *addr, struct list_head *del_list)
{
	struct list_head *head;
	nx_mac_list_t *cur;

	/* look up if already exists */
	list_for_each(head, del_list) {
		cur = list_entry(head, nx_mac_list_t, list);

		if (ether_addr_equal(addr, cur->mac_addr)) {
			list_move_tail(head, &adapter->mac_list);
			return 0;
		}
	}

	cur = kzalloc(sizeof(nx_mac_list_t), GFP_ATOMIC);
	if (cur == NULL)
		return -ENOMEM;

	memcpy(cur->mac_addr, addr, ETH_ALEN);
	list_add_tail(&cur->list, &adapter->mac_list);
	return nx_p3_sre_macaddr_change(adapter,
				cur->mac_addr, NETXEN_MAC_ADD);
}

static void netxen_p3_nic_set_multi(struct net_device *netdev)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct netdev_hw_addr *ha;
	static const u8 bcast_addr[ETH_ALEN] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};
	u32 mode = VPORT_MISS_MODE_DROP;
	LIST_HEAD(del_list);
	struct list_head *head;
	nx_mac_list_t *cur;

	if (adapter->is_up != NETXEN_ADAPTER_UP_MAGIC)
		return;

	list_splice_tail_init(&adapter->mac_list, &del_list);

	nx_p3_nic_add_mac(adapter, adapter->mac_addr, &del_list);
	nx_p3_nic_add_mac(adapter, bcast_addr, &del_list);

	if (netdev->flags & IFF_PROMISC) {
		mode = VPORT_MISS_MODE_ACCEPT_ALL;
		goto send_fw_cmd;
	}

	if ((netdev->flags & IFF_ALLMULTI) ||
			(netdev_mc_count(netdev) > adapter->max_mc_count)) {
		mode = VPORT_MISS_MODE_ACCEPT_MULTI;
		goto send_fw_cmd;
	}

	if (!netdev_mc_empty(netdev)) {
		netdev_for_each_mc_addr(ha, netdev)
			nx_p3_nic_add_mac(adapter, ha->addr, &del_list);
	}

send_fw_cmd:
	adapter->set_promisc(adapter, mode);
	head = &del_list;
	while (!list_empty(head)) {
		cur = list_entry(head->next, nx_mac_list_t, list);

		nx_p3_sre_macaddr_change(adapter,
				cur->mac_addr, NETXEN_MAC_DEL);
		list_del(&cur->list);
		kfree(cur);
	}
}

static int netxen_p3_nic_set_promisc(struct netxen_adapter *adapter, u32 mode)
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
	nx_mac_list_t *cur;
	struct list_head *head = &adapter->mac_list;

	while (!list_empty(head)) {
		cur = list_entry(head->next, nx_mac_list_t, list);
		nx_p3_sre_macaddr_change(adapter,
				cur->mac_addr, NETXEN_MAC_DEL);
		list_del(&cur->list);
		kfree(cur);
	}
}

static int netxen_p3_nic_set_mac_addr(struct netxen_adapter *adapter, u8 *addr)
{
	/* assuming caller has already copied new addr to netdev */
	netxen_p3_nic_set_multi(adapter->netdev);
	return 0;
}

#define	NETXEN_CONFIG_INTR_COALESCE	3

/*
 * Send the interrupt coalescing parameter set by ethtool to the card.
 */
int netxen_config_intr_coalesce(struct netxen_adapter *adapter)
{
	nx_nic_req_t req;
	u64 word[6];
	int rv, i;

	memset(&req, 0, sizeof(nx_nic_req_t));
	memset(word, 0, sizeof(word));

	req.qhdr = cpu_to_le64(NX_HOST_REQUEST << 23);

	word[0] = NETXEN_CONFIG_INTR_COALESCE | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word[0]);

	memcpy(&word[0], &adapter->coal, sizeof(adapter->coal));
	for (i = 0; i < 6; i++)
		req.words[i] = cpu_to_le64(word[i]);

	rv = netxen_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0) {
		printk(KERN_ERR "ERROR. Could not send "
			"interrupt coalescing parameters\n");
	}

	return rv;
}

int netxen_config_hw_lro(struct netxen_adapter *adapter, int enable)
{
	nx_nic_req_t req;
	u64 word;
	int rv = 0;

	if (!test_bit(__NX_FW_ATTACHED, &adapter->state))
		return 0;

	memset(&req, 0, sizeof(nx_nic_req_t));

	req.qhdr = cpu_to_le64(NX_HOST_REQUEST << 23);

	word = NX_NIC_H2C_OPCODE_CONFIG_HW_LRO | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	req.words[0] = cpu_to_le64(enable);

	rv = netxen_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0) {
		printk(KERN_ERR "ERROR. Could not send "
			"configure hw lro request\n");
	}

	return rv;
}

int netxen_config_bridged_mode(struct netxen_adapter *adapter, int enable)
{
	nx_nic_req_t req;
	u64 word;
	int rv = 0;

	if (!!(adapter->flags & NETXEN_NIC_BRIDGE_ENABLED) == enable)
		return rv;

	memset(&req, 0, sizeof(nx_nic_req_t));

	req.qhdr = cpu_to_le64(NX_HOST_REQUEST << 23);

	word = NX_NIC_H2C_OPCODE_CONFIG_BRIDGING |
		((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	req.words[0] = cpu_to_le64(enable);

	rv = netxen_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0) {
		printk(KERN_ERR "ERROR. Could not send "
				"configure bridge mode request\n");
	}

	adapter->flags ^= NETXEN_NIC_BRIDGE_ENABLED;

	return rv;
}


#define RSS_HASHTYPE_IP_TCP	0x3

int netxen_config_rss(struct netxen_adapter *adapter, int enable)
{
	nx_nic_req_t req;
	u64 word;
	int i, rv;

	static const u64 key[] = {
		0xbeac01fa6a42b73bULL, 0x8030f20c77cb2da3ULL,
		0xae7b30b4d0ca2bcbULL, 0x43a38fb04167253dULL,
		0x255b0ec26d5a56daULL
	};


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
	for (i = 0; i < ARRAY_SIZE(key); i++)
		req.words[i+1] = cpu_to_le64(key[i]);


	rv = netxen_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0) {
		printk(KERN_ERR "%s: could not configure RSS\n",
				adapter->netdev->name);
	}

	return rv;
}

int netxen_config_ipaddr(struct netxen_adapter *adapter, __be32 ip, int cmd)
{
	nx_nic_req_t req;
	u64 word;
	int rv;

	memset(&req, 0, sizeof(nx_nic_req_t));
	req.qhdr = cpu_to_le64(NX_HOST_REQUEST << 23);

	word = NX_NIC_H2C_OPCODE_CONFIG_IPADDR | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	req.words[0] = cpu_to_le64(cmd);
	memcpy(&req.words[1], &ip, sizeof(u32));

	rv = netxen_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0) {
		printk(KERN_ERR "%s: could not notify %s IP 0x%x request\n",
				adapter->netdev->name,
				(cmd == NX_IP_UP) ? "Add" : "Remove", ip);
	}
	return rv;
}

int netxen_linkevent_request(struct netxen_adapter *adapter, int enable)
{
	nx_nic_req_t req;
	u64 word;
	int rv;

	memset(&req, 0, sizeof(nx_nic_req_t));
	req.qhdr = cpu_to_le64(NX_HOST_REQUEST << 23);

	word = NX_NIC_H2C_OPCODE_GET_LINKEVENT | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);
	req.words[0] = cpu_to_le64(enable | (enable << 8));

	rv = netxen_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0) {
		printk(KERN_ERR "%s: could not configure link notification\n",
				adapter->netdev->name);
	}

	return rv;
}

int netxen_send_lro_cleanup(struct netxen_adapter *adapter)
{
	nx_nic_req_t req;
	u64 word;
	int rv;

	if (!test_bit(__NX_FW_ATTACHED, &adapter->state))
		return 0;

	memset(&req, 0, sizeof(nx_nic_req_t));
	req.qhdr = cpu_to_le64(NX_HOST_REQUEST << 23);

	word = NX_NIC_H2C_OPCODE_LRO_REQUEST |
		((u64)adapter->portnum << 16) |
		((u64)NX_NIC_LRO_REQUEST_CLEANUP << 56) ;

	req.req_hdr = cpu_to_le64(word);

	rv = netxen_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0) {
		printk(KERN_ERR "%s: could not cleanup lro flows\n",
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
	int ret;

	addr = base;
	ptr32 = buf;
	for (i = 0; i < size / sizeof(u32); i++) {
		ret = netxen_rom_fast_read(adapter, addr, &v);
		if (ret)
			return ret;

		*ptr32 = cpu_to_le32(v);
		ptr32++;
		addr += sizeof(u32);
	}
	if ((char *)buf + size > (char *)ptr32) {
		__le32 local;
		ret = netxen_rom_fast_read(adapter, addr, &v);
		if (ret)
			return ret;
		local = cpu_to_le32(v);
		memcpy(ptr32, &local, (char *)buf + size - (char *)ptr32);
	}

	return 0;
}

int netxen_get_flash_mac_addr(struct netxen_adapter *adapter, u64 *mac)
{
	__le32 *pmac = (__le32 *) mac;
	u32 offset;

	offset = NX_FW_MAC_ADDR_OFFSET + (adapter->portnum * sizeof(u64));

	if (netxen_get_flash_block(adapter, offset, sizeof(u64), pmac) == -1)
		return -1;

	if (*mac == ~0ULL) {

		offset = NX_OLD_MAC_ADDR_OFFSET +
			(adapter->portnum * sizeof(u64));

		if (netxen_get_flash_block(adapter,
					offset, sizeof(u64), pmac) == -1)
			return -1;

		if (*mac == ~0ULL)
			return -1;
	}
	return 0;
}

int netxen_p3_get_mac_addr(struct netxen_adapter *adapter, u64 *mac)
{
	uint32_t crbaddr, mac_hi, mac_lo;
	int pci_func = adapter->ahw.pci_func;

	crbaddr = CRB_MAC_BLOCK_START +
		(4 * ((pci_func/2) * 3)) + (4 * (pci_func & 1));

	mac_lo = NXRD32(adapter, crbaddr);
	mac_hi = NXRD32(adapter, crbaddr+4);

	if (pci_func & 1)
		*mac = le64_to_cpu((mac_lo >> 16) | ((u64)mac_hi << 16));
	else
		*mac = le64_to_cpu((u64)mac_lo | ((u64)mac_hi << 32));

	return 0;
}

/*
 * Changes the CRB window to the specified window.
 */
static void
netxen_nic_pci_set_crbwindow_128M(struct netxen_adapter *adapter,
		u32 window)
{
	void __iomem *offset;
	int count = 10;
	u8 func = adapter->ahw.pci_func;

	if (adapter->ahw.crb_win == window)
		return;

	offset = PCI_OFFSET_SECOND_RANGE(adapter,
			NETXEN_PCIX_PH_REG(PCIE_CRB_WINDOW_REG(func)));

	writel(window, offset);
	do {
		if (window == readl(offset))
			break;

		if (printk_ratelimit())
			dev_warn(&adapter->pdev->dev,
					"failed to set CRB window to %d\n",
					(window == NETXEN_WINDOW_ONE));
		udelay(1);

	} while (--count > 0);

	if (count > 0)
		adapter->ahw.crb_win = window;
}

/*
 * Returns < 0 if off is not valid,
 *	 1 if window access is needed. 'off' is set to offset from
 *	   CRB space in 128M pci map
 *	 0 if no window access is needed. 'off' is set to 2M addr
 * In: 'off' is offset from base in 128M pci map
 */
static int
netxen_nic_pci_get_crb_addr_2M(struct netxen_adapter *adapter,
		ulong off, void __iomem **addr)
{
	crb_128M_2M_sub_block_map_t *m;


	if ((off >= NETXEN_CRB_MAX) || (off < NETXEN_PCI_CRBSPACE))
		return -EINVAL;

	off -= NETXEN_PCI_CRBSPACE;

	/*
	 * Try direct map
	 */
	m = &crb_128M_2M_map[CRB_BLK(off)].sub_block[CRB_SUBBLK(off)];

	if (m->valid && (m->start_128M <= off) && (m->end_128M > off)) {
		*addr = adapter->ahw.pci_base0 + m->start_2M +
			(off - m->start_128M);
		return 0;
	}

	/*
	 * Not in direct map, use crb window
	 */
	*addr = adapter->ahw.pci_base0 + CRB_INDIRECT_2M +
		(off & MASK(16));
	return 1;
}

/*
 * In: 'off' is offset from CRB space in 128M pci map
 * Out: 'off' is 2M pci map addr
 * side effect: lock crb window
 */
static void
netxen_nic_pci_set_crbwindow_2M(struct netxen_adapter *adapter, ulong off)
{
	u32 window;
	void __iomem *addr = adapter->ahw.pci_base0 + CRB_WINDOW_2M;

	off -= NETXEN_PCI_CRBSPACE;

	window = CRB_HI(off);

	writel(window, addr);
	if (readl(addr) != window) {
		if (printk_ratelimit())
			dev_warn(&adapter->pdev->dev,
				"failed to set CRB window to %d off 0x%lx\n",
				window, off);
	}
}

static void __iomem *
netxen_nic_map_indirect_address_128M(struct netxen_adapter *adapter,
		ulong win_off, void __iomem **mem_ptr)
{
	ulong off = win_off;
	void __iomem *addr;
	resource_size_t mem_base;

	if (ADDR_IN_WINDOW1(win_off))
		off = NETXEN_CRB_NORMAL(win_off);

	addr = pci_base_offset(adapter, off);
	if (addr)
		return addr;

	if (adapter->ahw.pci_len0 == 0)
		off -= NETXEN_PCI_CRBSPACE;

	mem_base = pci_resource_start(adapter->pdev, 0);
	*mem_ptr = ioremap(mem_base + (off & PAGE_MASK), PAGE_SIZE);
	if (*mem_ptr)
		addr = *mem_ptr + (off & (PAGE_SIZE - 1));

	return addr;
}

static int
netxen_nic_hw_write_wx_128M(struct netxen_adapter *adapter, ulong off, u32 data)
{
	unsigned long flags;
	void __iomem *addr, *mem_ptr = NULL;

	addr = netxen_nic_map_indirect_address_128M(adapter, off, &mem_ptr);
	if (!addr)
		return -EIO;

	if (ADDR_IN_WINDOW1(off)) { /* Window 1 */
		netxen_nic_io_write_128M(adapter, addr, data);
	} else {        /* Window 0 */
		write_lock_irqsave(&adapter->ahw.crb_lock, flags);
		netxen_nic_pci_set_crbwindow_128M(adapter, 0);
		writel(data, addr);
		netxen_nic_pci_set_crbwindow_128M(adapter,
				NETXEN_WINDOW_ONE);
		write_unlock_irqrestore(&adapter->ahw.crb_lock, flags);
	}

	if (mem_ptr)
		iounmap(mem_ptr);

	return 0;
}

static u32
netxen_nic_hw_read_wx_128M(struct netxen_adapter *adapter, ulong off)
{
	unsigned long flags;
	void __iomem *addr, *mem_ptr = NULL;
	u32 data;

	addr = netxen_nic_map_indirect_address_128M(adapter, off, &mem_ptr);
	if (!addr)
		return -EIO;

	if (ADDR_IN_WINDOW1(off)) { /* Window 1 */
		data = netxen_nic_io_read_128M(adapter, addr);
	} else {        /* Window 0 */
		write_lock_irqsave(&adapter->ahw.crb_lock, flags);
		netxen_nic_pci_set_crbwindow_128M(adapter, 0);
		data = readl(addr);
		netxen_nic_pci_set_crbwindow_128M(adapter,
				NETXEN_WINDOW_ONE);
		write_unlock_irqrestore(&adapter->ahw.crb_lock, flags);
	}

	if (mem_ptr)
		iounmap(mem_ptr);

	return data;
}

static int
netxen_nic_hw_write_wx_2M(struct netxen_adapter *adapter, ulong off, u32 data)
{
	unsigned long flags;
	int rv;
	void __iomem *addr = NULL;

	rv = netxen_nic_pci_get_crb_addr_2M(adapter, off, &addr);

	if (rv == 0) {
		writel(data, addr);
		return 0;
	}

	if (rv > 0) {
		/* indirect access */
		write_lock_irqsave(&adapter->ahw.crb_lock, flags);
		crb_win_lock(adapter);
		netxen_nic_pci_set_crbwindow_2M(adapter, off);
		writel(data, addr);
		crb_win_unlock(adapter);
		write_unlock_irqrestore(&adapter->ahw.crb_lock, flags);
		return 0;
	}

	dev_err(&adapter->pdev->dev,
			"%s: invalid offset: 0x%016lx\n", __func__, off);
	dump_stack();
	return -EIO;
}

static u32
netxen_nic_hw_read_wx_2M(struct netxen_adapter *adapter, ulong off)
{
	unsigned long flags;
	int rv;
	u32 data;
	void __iomem *addr = NULL;

	rv = netxen_nic_pci_get_crb_addr_2M(adapter, off, &addr);

	if (rv == 0)
		return readl(addr);

	if (rv > 0) {
		/* indirect access */
		write_lock_irqsave(&adapter->ahw.crb_lock, flags);
		crb_win_lock(adapter);
		netxen_nic_pci_set_crbwindow_2M(adapter, off);
		data = readl(addr);
		crb_win_unlock(adapter);
		write_unlock_irqrestore(&adapter->ahw.crb_lock, flags);
		return data;
	}

	dev_err(&adapter->pdev->dev,
			"%s: invalid offset: 0x%016lx\n", __func__, off);
	dump_stack();
	return -1;
}

/* window 1 registers only */
static void netxen_nic_io_write_128M(struct netxen_adapter *adapter,
		void __iomem *addr, u32 data)
{
	read_lock(&adapter->ahw.crb_lock);
	writel(data, addr);
	read_unlock(&adapter->ahw.crb_lock);
}

static u32 netxen_nic_io_read_128M(struct netxen_adapter *adapter,
		void __iomem *addr)
{
	u32 val;

	read_lock(&adapter->ahw.crb_lock);
	val = readl(addr);
	read_unlock(&adapter->ahw.crb_lock);

	return val;
}

static void netxen_nic_io_write_2M(struct netxen_adapter *adapter,
		void __iomem *addr, u32 data)
{
	writel(data, addr);
}

static u32 netxen_nic_io_read_2M(struct netxen_adapter *adapter,
		void __iomem *addr)
{
	return readl(addr);
}

void __iomem *
netxen_get_ioaddr(struct netxen_adapter *adapter, u32 offset)
{
	void __iomem *addr = NULL;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		if ((offset < NETXEN_CRB_PCIX_HOST2) &&
				(offset > NETXEN_CRB_PCIX_HOST))
			addr = PCI_OFFSET_SECOND_RANGE(adapter, offset);
		else
			addr = NETXEN_CRB_NORMALIZE(adapter, offset);
	} else {
		WARN_ON(netxen_nic_pci_get_crb_addr_2M(adapter,
					offset, &addr));
	}

	return addr;
}

static int
netxen_nic_pci_set_window_128M(struct netxen_adapter *adapter,
		u64 addr, u32 *start)
{
	if (ADDR_IN_RANGE(addr, NETXEN_ADDR_OCM0, NETXEN_ADDR_OCM0_MAX)) {
		*start = (addr - NETXEN_ADDR_OCM0  + NETXEN_PCI_OCM0);
		return 0;
	} else if (ADDR_IN_RANGE(addr,
				NETXEN_ADDR_OCM1, NETXEN_ADDR_OCM1_MAX)) {
		*start = (addr - NETXEN_ADDR_OCM1 + NETXEN_PCI_OCM1);
		return 0;
	}

	return -EIO;
}

static int
netxen_nic_pci_set_window_2M(struct netxen_adapter *adapter,
		u64 addr, u32 *start)
{
	u32 window;

	window = OCM_WIN(addr);

	writel(window, adapter->ahw.ocm_win_crb);
	/* read back to flush */
	readl(adapter->ahw.ocm_win_crb);

	adapter->ahw.ocm_win = window;
	*start = NETXEN_PCI_OCM0_2M + GET_MEM_OFFS_2M(addr);
	return 0;
}

static int
netxen_nic_pci_mem_access_direct(struct netxen_adapter *adapter, u64 off,
		u64 *data, int op)
{
	void __iomem *addr, *mem_ptr = NULL;
	resource_size_t mem_base;
	int ret;
	u32 start;

	spin_lock(&adapter->ahw.mem_lock);

	ret = adapter->pci_set_window(adapter, off, &start);
	if (ret != 0)
		goto unlock;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		addr = adapter->ahw.pci_base0 + start;
	} else {
		addr = pci_base_offset(adapter, start);
		if (addr)
			goto noremap;

		mem_base = pci_resource_start(adapter->pdev, 0) +
					(start & PAGE_MASK);
		mem_ptr = ioremap(mem_base, PAGE_SIZE);
		if (mem_ptr == NULL) {
			ret = -EIO;
			goto unlock;
		}

		addr = mem_ptr + (start & (PAGE_SIZE-1));
	}
noremap:
	if (op == 0)	/* read */
		*data = readq(addr);
	else		/* write */
		writeq(*data, addr);

unlock:
	spin_unlock(&adapter->ahw.mem_lock);

	if (mem_ptr)
		iounmap(mem_ptr);
	return ret;
}

void
netxen_pci_camqm_read_2M(struct netxen_adapter *adapter, u64 off, u64 *data)
{
	void __iomem *addr = adapter->ahw.pci_base0 +
		NETXEN_PCI_CAMQM_2M_BASE + (off - NETXEN_PCI_CAMQM);

	spin_lock(&adapter->ahw.mem_lock);
	*data = readq(addr);
	spin_unlock(&adapter->ahw.mem_lock);
}

void
netxen_pci_camqm_write_2M(struct netxen_adapter *adapter, u64 off, u64 data)
{
	void __iomem *addr = adapter->ahw.pci_base0 +
		NETXEN_PCI_CAMQM_2M_BASE + (off - NETXEN_PCI_CAMQM);

	spin_lock(&adapter->ahw.mem_lock);
	writeq(data, addr);
	spin_unlock(&adapter->ahw.mem_lock);
}

#define MAX_CTL_CHECK   1000

static int
netxen_nic_pci_mem_write_128M(struct netxen_adapter *adapter,
		u64 off, u64 data)
{
	int j, ret;
	u32 temp, off_lo, off_hi, addr_hi, data_hi, data_lo;
	void __iomem *mem_crb;

	/* Only 64-bit aligned access */
	if (off & 7)
		return -EIO;

	/* P2 has different SIU and MIU test agent base addr */
	if (ADDR_IN_RANGE(off, NETXEN_ADDR_QDR_NET,
				NETXEN_ADDR_QDR_NET_MAX_P2)) {
		mem_crb = pci_base_offset(adapter,
				NETXEN_CRB_QDR_NET+SIU_TEST_AGT_BASE);
		addr_hi = SIU_TEST_AGT_ADDR_HI;
		data_lo = SIU_TEST_AGT_WRDATA_LO;
		data_hi = SIU_TEST_AGT_WRDATA_HI;
		off_lo = off & SIU_TEST_AGT_ADDR_MASK;
		off_hi = SIU_TEST_AGT_UPPER_ADDR(off);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX)) {
		mem_crb = pci_base_offset(adapter,
				NETXEN_CRB_DDR_NET+MIU_TEST_AGT_BASE);
		addr_hi = MIU_TEST_AGT_ADDR_HI;
		data_lo = MIU_TEST_AGT_WRDATA_LO;
		data_hi = MIU_TEST_AGT_WRDATA_HI;
		off_lo = off & MIU_TEST_AGT_ADDR_MASK;
		off_hi = 0;
		goto correct;
	}

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_OCM0, NETXEN_ADDR_OCM0_MAX) ||
		ADDR_IN_RANGE(off, NETXEN_ADDR_OCM1, NETXEN_ADDR_OCM1_MAX)) {
		if (adapter->ahw.pci_len0 != 0) {
			return netxen_nic_pci_mem_access_direct(adapter,
					off, &data, 1);
		}
	}

	return -EIO;

correct:
	spin_lock(&adapter->ahw.mem_lock);
	netxen_nic_pci_set_crbwindow_128M(adapter, 0);

	writel(off_lo, (mem_crb + MIU_TEST_AGT_ADDR_LO));
	writel(off_hi, (mem_crb + addr_hi));
	writel(data & 0xffffffff, (mem_crb + data_lo));
	writel((data >> 32) & 0xffffffff, (mem_crb + data_hi));
	writel((TA_CTL_ENABLE | TA_CTL_WRITE), (mem_crb + TEST_AGT_CTRL));
	writel((TA_CTL_START | TA_CTL_ENABLE | TA_CTL_WRITE),
			(mem_crb + TEST_AGT_CTRL));

	for (j = 0; j < MAX_CTL_CHECK; j++) {
		temp = readl((mem_crb + TEST_AGT_CTRL));
		if ((temp & TA_CTL_BUSY) == 0)
			break;
	}

	if (j >= MAX_CTL_CHECK) {
		if (printk_ratelimit())
			dev_err(&adapter->pdev->dev,
					"failed to write through agent\n");
		ret = -EIO;
	} else
		ret = 0;

	netxen_nic_pci_set_crbwindow_128M(adapter, NETXEN_WINDOW_ONE);
	spin_unlock(&adapter->ahw.mem_lock);
	return ret;
}

static int
netxen_nic_pci_mem_read_128M(struct netxen_adapter *adapter,
		u64 off, u64 *data)
{
	int j, ret;
	u32 temp, off_lo, off_hi, addr_hi, data_hi, data_lo;
	u64 val;
	void __iomem *mem_crb;

	/* Only 64-bit aligned access */
	if (off & 7)
		return -EIO;

	/* P2 has different SIU and MIU test agent base addr */
	if (ADDR_IN_RANGE(off, NETXEN_ADDR_QDR_NET,
				NETXEN_ADDR_QDR_NET_MAX_P2)) {
		mem_crb = pci_base_offset(adapter,
				NETXEN_CRB_QDR_NET+SIU_TEST_AGT_BASE);
		addr_hi = SIU_TEST_AGT_ADDR_HI;
		data_lo = SIU_TEST_AGT_RDDATA_LO;
		data_hi = SIU_TEST_AGT_RDDATA_HI;
		off_lo = off & SIU_TEST_AGT_ADDR_MASK;
		off_hi = SIU_TEST_AGT_UPPER_ADDR(off);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX)) {
		mem_crb = pci_base_offset(adapter,
				NETXEN_CRB_DDR_NET+MIU_TEST_AGT_BASE);
		addr_hi = MIU_TEST_AGT_ADDR_HI;
		data_lo = MIU_TEST_AGT_RDDATA_LO;
		data_hi = MIU_TEST_AGT_RDDATA_HI;
		off_lo = off & MIU_TEST_AGT_ADDR_MASK;
		off_hi = 0;
		goto correct;
	}

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_OCM0, NETXEN_ADDR_OCM0_MAX) ||
		ADDR_IN_RANGE(off, NETXEN_ADDR_OCM1, NETXEN_ADDR_OCM1_MAX)) {
		if (adapter->ahw.pci_len0 != 0) {
			return netxen_nic_pci_mem_access_direct(adapter,
					off, data, 0);
		}
	}

	return -EIO;

correct:
	spin_lock(&adapter->ahw.mem_lock);
	netxen_nic_pci_set_crbwindow_128M(adapter, 0);

	writel(off_lo, (mem_crb + MIU_TEST_AGT_ADDR_LO));
	writel(off_hi, (mem_crb + addr_hi));
	writel(TA_CTL_ENABLE, (mem_crb + TEST_AGT_CTRL));
	writel((TA_CTL_START|TA_CTL_ENABLE), (mem_crb + TEST_AGT_CTRL));

	for (j = 0; j < MAX_CTL_CHECK; j++) {
		temp = readl(mem_crb + TEST_AGT_CTRL);
		if ((temp & TA_CTL_BUSY) == 0)
			break;
	}

	if (j >= MAX_CTL_CHECK) {
		if (printk_ratelimit())
			dev_err(&adapter->pdev->dev,
					"failed to read through agent\n");
		ret = -EIO;
	} else {

		temp = readl(mem_crb + data_hi);
		val = ((u64)temp << 32);
		val |= readl(mem_crb + data_lo);
		*data = val;
		ret = 0;
	}

	netxen_nic_pci_set_crbwindow_128M(adapter, NETXEN_WINDOW_ONE);
	spin_unlock(&adapter->ahw.mem_lock);

	return ret;
}

static int
netxen_nic_pci_mem_write_2M(struct netxen_adapter *adapter,
		u64 off, u64 data)
{
	int j, ret;
	u32 temp, off8;
	void __iomem *mem_crb;

	/* Only 64-bit aligned access */
	if (off & 7)
		return -EIO;

	/* P3 onward, test agent base for MIU and SIU is same */
	if (ADDR_IN_RANGE(off, NETXEN_ADDR_QDR_NET,
				NETXEN_ADDR_QDR_NET_MAX_P3)) {
		mem_crb = netxen_get_ioaddr(adapter,
				NETXEN_CRB_QDR_NET+MIU_TEST_AGT_BASE);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX)) {
		mem_crb = netxen_get_ioaddr(adapter,
				NETXEN_CRB_DDR_NET+MIU_TEST_AGT_BASE);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_OCM0, NETXEN_ADDR_OCM0_MAX))
		return netxen_nic_pci_mem_access_direct(adapter, off, &data, 1);

	return -EIO;

correct:
	off8 = off & 0xfffffff8;

	spin_lock(&adapter->ahw.mem_lock);

	writel(off8, (mem_crb + MIU_TEST_AGT_ADDR_LO));
	writel(0, (mem_crb + MIU_TEST_AGT_ADDR_HI));

	writel(data & 0xffffffff,
			mem_crb + MIU_TEST_AGT_WRDATA_LO);
	writel((data >> 32) & 0xffffffff,
			mem_crb + MIU_TEST_AGT_WRDATA_HI);

	writel((TA_CTL_ENABLE | TA_CTL_WRITE), (mem_crb + TEST_AGT_CTRL));
	writel((TA_CTL_START | TA_CTL_ENABLE | TA_CTL_WRITE),
			(mem_crb + TEST_AGT_CTRL));

	for (j = 0; j < MAX_CTL_CHECK; j++) {
		temp = readl(mem_crb + TEST_AGT_CTRL);
		if ((temp & TA_CTL_BUSY) == 0)
			break;
	}

	if (j >= MAX_CTL_CHECK) {
		if (printk_ratelimit())
			dev_err(&adapter->pdev->dev,
					"failed to write through agent\n");
		ret = -EIO;
	} else
		ret = 0;

	spin_unlock(&adapter->ahw.mem_lock);

	return ret;
}

static int
netxen_nic_pci_mem_read_2M(struct netxen_adapter *adapter,
		u64 off, u64 *data)
{
	int j, ret;
	u32 temp, off8;
	u64 val;
	void __iomem *mem_crb;

	/* Only 64-bit aligned access */
	if (off & 7)
		return -EIO;

	/* P3 onward, test agent base for MIU and SIU is same */
	if (ADDR_IN_RANGE(off, NETXEN_ADDR_QDR_NET,
				NETXEN_ADDR_QDR_NET_MAX_P3)) {
		mem_crb = netxen_get_ioaddr(adapter,
				NETXEN_CRB_QDR_NET+MIU_TEST_AGT_BASE);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX)) {
		mem_crb = netxen_get_ioaddr(adapter,
				NETXEN_CRB_DDR_NET+MIU_TEST_AGT_BASE);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_OCM0, NETXEN_ADDR_OCM0_MAX)) {
		return netxen_nic_pci_mem_access_direct(adapter,
				off, data, 0);
	}

	return -EIO;

correct:
	off8 = off & 0xfffffff8;

	spin_lock(&adapter->ahw.mem_lock);

	writel(off8, (mem_crb + MIU_TEST_AGT_ADDR_LO));
	writel(0, (mem_crb + MIU_TEST_AGT_ADDR_HI));
	writel(TA_CTL_ENABLE, (mem_crb + TEST_AGT_CTRL));
	writel((TA_CTL_START | TA_CTL_ENABLE), (mem_crb + TEST_AGT_CTRL));

	for (j = 0; j < MAX_CTL_CHECK; j++) {
		temp = readl(mem_crb + TEST_AGT_CTRL);
		if ((temp & TA_CTL_BUSY) == 0)
			break;
	}

	if (j >= MAX_CTL_CHECK) {
		if (printk_ratelimit())
			dev_err(&adapter->pdev->dev,
					"failed to read through agent\n");
		ret = -EIO;
	} else {
		val = (u64)(readl(mem_crb + MIU_TEST_AGT_RDDATA_HI)) << 32;
		val |= readl(mem_crb + MIU_TEST_AGT_RDDATA_LO);
		*data = val;
		ret = 0;
	}

	spin_unlock(&adapter->ahw.mem_lock);

	return ret;
}

void
netxen_setup_hwops(struct netxen_adapter *adapter)
{
	adapter->init_port = netxen_niu_xg_init_port;
	adapter->stop_port = netxen_niu_disable_xg_port;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		adapter->crb_read = netxen_nic_hw_read_wx_128M,
		adapter->crb_write = netxen_nic_hw_write_wx_128M,
		adapter->pci_set_window = netxen_nic_pci_set_window_128M,
		adapter->pci_mem_read = netxen_nic_pci_mem_read_128M,
		adapter->pci_mem_write = netxen_nic_pci_mem_write_128M,
		adapter->io_read = netxen_nic_io_read_128M,
		adapter->io_write = netxen_nic_io_write_128M,

		adapter->macaddr_set = netxen_p2_nic_set_mac_addr;
		adapter->set_multi = netxen_p2_nic_set_multi;
		adapter->set_mtu = netxen_nic_set_mtu_xgb;
		adapter->set_promisc = netxen_p2_nic_set_promisc;

	} else {
		adapter->crb_read = netxen_nic_hw_read_wx_2M,
		adapter->crb_write = netxen_nic_hw_write_wx_2M,
		adapter->pci_set_window = netxen_nic_pci_set_window_2M,
		adapter->pci_mem_read = netxen_nic_pci_mem_read_2M,
		adapter->pci_mem_write = netxen_nic_pci_mem_write_2M,
		adapter->io_read = netxen_nic_io_read_2M,
		adapter->io_write = netxen_nic_io_write_2M,

		adapter->set_mtu = nx_fw_cmd_set_mtu;
		adapter->set_promisc = netxen_p3_nic_set_promisc;
		adapter->macaddr_set = netxen_p3_nic_set_mac_addr;
		adapter->set_multi = netxen_p3_nic_set_multi;

		adapter->phy_read = nx_fw_cmd_query_phy;
		adapter->phy_write = nx_fw_cmd_set_phy;
	}
}

int netxen_nic_get_board_info(struct netxen_adapter *adapter)
{
	int offset, board_type, magic;
	struct pci_dev *pdev = adapter->pdev;

	offset = NX_FW_MAGIC_OFFSET;
	if (netxen_rom_fast_read(adapter, offset, &magic))
		return -EIO;

	if (magic != NETXEN_BDINFO_MAGIC) {
		dev_err(&pdev->dev, "invalid board config, magic=%08x\n",
			magic);
		return -EIO;
	}

	offset = NX_BRDTYPE_OFFSET;
	if (netxen_rom_fast_read(adapter, offset, &board_type))
		return -EIO;

	if (board_type == NETXEN_BRDTYPE_P3_4_GB_MM) {
		u32 gpio = NXRD32(adapter, NETXEN_ROMUSB_GLB_PAD_GPIO_I);
		if ((gpio & 0x8000) == 0)
			board_type = NETXEN_BRDTYPE_P3_10G_TP;
	}

	adapter->ahw.board_type = board_type;

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
static int netxen_nic_set_mtu_xgb(struct netxen_adapter *adapter, int new_mtu)
{
	new_mtu += MTU_FUDGE_FACTOR;
	if (adapter->physical_port == 0)
		NXWR32(adapter, NETXEN_NIU_XGE_MAX_FRAME_SIZE, new_mtu);
	else
		NXWR32(adapter, NETXEN_NIU_XG1_MAX_FRAME_SIZE, new_mtu);
	return 0;
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
		port_mode = NXRD32(adapter, NETXEN_PORT_MODE_ADDR);
		if (port_mode == NETXEN_PORT_MODE_802_3_AP) {
			adapter->link_speed   = SPEED_1000;
			adapter->link_duplex  = DUPLEX_FULL;
			adapter->link_autoneg = AUTONEG_DISABLE;
			return;
		}

		if (adapter->phy_read &&
		    adapter->phy_read(adapter,
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
				if (adapter->phy_read &&
				    adapter->phy_read(adapter,
						      NETXEN_NIU_GB_MII_MGMT_ADDR_AUTONEG,
						      &autoneg) == 0)
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

int
netxen_nic_wol_supported(struct netxen_adapter *adapter)
{
	u32 wol_cfg;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return 0;

	wol_cfg = NXRD32(adapter, NETXEN_WOL_CONFIG_NV);
	if (wol_cfg & (1UL << adapter->portnum)) {
		wol_cfg = NXRD32(adapter, NETXEN_WOL_CONFIG);
		if (wol_cfg & (1 << adapter->portnum))
			return 1;
	}

	return 0;
}

static u32 netxen_md_cntrl(struct netxen_adapter *adapter,
			struct netxen_minidump_template_hdr *template_hdr,
			struct netxen_minidump_entry_crb *crtEntry)
{
	int loop_cnt, i, rv = 0, timeout_flag;
	u32 op_count, stride;
	u32 opcode, read_value, addr;
	unsigned long timeout, timeout_jiffies;
	addr = crtEntry->addr;
	op_count = crtEntry->op_count;
	stride = crtEntry->addr_stride;

	for (loop_cnt = 0; loop_cnt < op_count; loop_cnt++) {
		for (i = 0; i < sizeof(crtEntry->opcode) * 8; i++) {
			opcode = (crtEntry->opcode & (0x1 << i));
			if (opcode) {
				switch (opcode) {
				case NX_DUMP_WCRB:
					NX_WR_DUMP_REG(addr,
						adapter->ahw.pci_base0,
							crtEntry->value_1);
					break;
				case NX_DUMP_RWCRB:
					NX_RD_DUMP_REG(addr,
						adapter->ahw.pci_base0,
								&read_value);
					NX_WR_DUMP_REG(addr,
						adapter->ahw.pci_base0,
								read_value);
					break;
				case NX_DUMP_ANDCRB:
					NX_RD_DUMP_REG(addr,
						adapter->ahw.pci_base0,
								&read_value);
					read_value &= crtEntry->value_2;
					NX_WR_DUMP_REG(addr,
						adapter->ahw.pci_base0,
								read_value);
					break;
				case NX_DUMP_ORCRB:
					NX_RD_DUMP_REG(addr,
						adapter->ahw.pci_base0,
								&read_value);
					read_value |= crtEntry->value_3;
					NX_WR_DUMP_REG(addr,
						adapter->ahw.pci_base0,
								read_value);
					break;
				case NX_DUMP_POLLCRB:
					timeout = crtEntry->poll_timeout;
					NX_RD_DUMP_REG(addr,
						adapter->ahw.pci_base0,
								&read_value);
					timeout_jiffies =
					msecs_to_jiffies(timeout) + jiffies;
					for (timeout_flag = 0;
						!timeout_flag
					&& ((read_value & crtEntry->value_2)
					!= crtEntry->value_1);) {
						if (time_after(jiffies,
							timeout_jiffies))
							timeout_flag = 1;
					NX_RD_DUMP_REG(addr,
							adapter->ahw.pci_base0,
								&read_value);
					}

					if (timeout_flag) {
						dev_err(&adapter->pdev->dev, "%s : "
							"Timeout in poll_crb control operation.\n"
								, __func__);
						return -1;
					}
					break;
				case NX_DUMP_RD_SAVE:
					/* Decide which address to use */
					if (crtEntry->state_index_a)
						addr =
						template_hdr->saved_state_array
						[crtEntry->state_index_a];
					NX_RD_DUMP_REG(addr,
						adapter->ahw.pci_base0,
								&read_value);
					template_hdr->saved_state_array
					[crtEntry->state_index_v]
						= read_value;
					break;
				case NX_DUMP_WRT_SAVED:
					/* Decide which value to use */
					if (crtEntry->state_index_v)
						read_value =
						template_hdr->saved_state_array
						[crtEntry->state_index_v];
					else
						read_value = crtEntry->value_1;

					/* Decide which address to use */
					if (crtEntry->state_index_a)
						addr =
						template_hdr->saved_state_array
						[crtEntry->state_index_a];

					NX_WR_DUMP_REG(addr,
						adapter->ahw.pci_base0,
								read_value);
					break;
				case NX_DUMP_MOD_SAVE_ST:
					read_value =
					template_hdr->saved_state_array
						[crtEntry->state_index_v];
					read_value <<= crtEntry->shl;
					read_value >>= crtEntry->shr;
					if (crtEntry->value_2)
						read_value &=
						crtEntry->value_2;
					read_value |= crtEntry->value_3;
					read_value += crtEntry->value_1;
					/* Write value back to state area.*/
					template_hdr->saved_state_array
						[crtEntry->state_index_v]
							= read_value;
					break;
				default:
					rv = 1;
					break;
				}
			}
		}
		addr = addr + stride;
	}
	return rv;
}

/* Read memory or MN */
static u32
netxen_md_rdmem(struct netxen_adapter *adapter,
		struct netxen_minidump_entry_rdmem
			*memEntry, u64 *data_buff)
{
	u64 addr, value = 0;
	int i = 0, loop_cnt;

	addr = (u64)memEntry->read_addr;
	loop_cnt = memEntry->read_data_size;    /* This is size in bytes */
	loop_cnt /= sizeof(value);

	for (i = 0; i < loop_cnt; i++) {
		if (netxen_nic_pci_mem_read_2M(adapter, addr, &value))
			goto out;
		*data_buff++ = value;
		addr += sizeof(value);
	}
out:
	return i * sizeof(value);
}

/* Read CRB operation */
static u32 netxen_md_rd_crb(struct netxen_adapter *adapter,
			struct netxen_minidump_entry_crb
				*crbEntry, u32 *data_buff)
{
	int loop_cnt;
	u32 op_count, addr, stride, value;

	addr = crbEntry->addr;
	op_count = crbEntry->op_count;
	stride = crbEntry->addr_stride;

	for (loop_cnt = 0; loop_cnt < op_count; loop_cnt++) {
		NX_RD_DUMP_REG(addr, adapter->ahw.pci_base0, &value);
		*data_buff++ = addr;
		*data_buff++ = value;
		addr = addr + stride;
	}
	return loop_cnt * (2 * sizeof(u32));
}

/* Read ROM */
static u32
netxen_md_rdrom(struct netxen_adapter *adapter,
			struct netxen_minidump_entry_rdrom
				*romEntry, __le32 *data_buff)
{
	int i, count = 0;
	u32 size, lck_val;
	u32 val;
	u32 fl_addr, waddr, raddr;
	fl_addr = romEntry->read_addr;
	size = romEntry->read_data_size/4;
lock_try:
	lck_val = readl((void __iomem *)(adapter->ahw.pci_base0 +
							NX_FLASH_SEM2_LK));
	if (!lck_val && count < MAX_CTL_CHECK) {
		msleep(20);
		count++;
		goto lock_try;
	}
	writel(adapter->ahw.pci_func, (void __iomem *)(adapter->ahw.pci_base0 +
							NX_FLASH_LOCK_ID));
	for (i = 0; i < size; i++) {
		waddr = fl_addr & 0xFFFF0000;
		NX_WR_DUMP_REG(FLASH_ROM_WINDOW, adapter->ahw.pci_base0, waddr);
		raddr = FLASH_ROM_DATA + (fl_addr & 0x0000FFFF);
		NX_RD_DUMP_REG(raddr, adapter->ahw.pci_base0, &val);
		*data_buff++ = cpu_to_le32(val);
		fl_addr += sizeof(val);
	}
	readl((void __iomem *)(adapter->ahw.pci_base0 + NX_FLASH_SEM2_ULK));
	return romEntry->read_data_size;
}

/* Handle L2 Cache */
static u32
netxen_md_L2Cache(struct netxen_adapter *adapter,
				struct netxen_minidump_entry_cache
					*cacheEntry, u32 *data_buff)
{
	int loop_cnt, i, k, timeout_flag = 0;
	u32 addr, read_addr, read_value, cntrl_addr, tag_reg_addr;
	u32 tag_value, read_cnt;
	u8 cntl_value_w, cntl_value_r;
	unsigned long timeout, timeout_jiffies;

	loop_cnt = cacheEntry->op_count;
	read_addr = cacheEntry->read_addr;
	cntrl_addr = cacheEntry->control_addr;
	cntl_value_w = (u32) cacheEntry->write_value;
	tag_reg_addr = cacheEntry->tag_reg_addr;
	tag_value = cacheEntry->init_tag_value;
	read_cnt = cacheEntry->read_addr_cnt;

	for (i = 0; i < loop_cnt; i++) {
		NX_WR_DUMP_REG(tag_reg_addr, adapter->ahw.pci_base0, tag_value);
		if (cntl_value_w)
			NX_WR_DUMP_REG(cntrl_addr, adapter->ahw.pci_base0,
					(u32)cntl_value_w);
		if (cacheEntry->poll_mask) {
			timeout = cacheEntry->poll_wait;
			NX_RD_DUMP_REG(cntrl_addr, adapter->ahw.pci_base0,
							&cntl_value_r);
			timeout_jiffies = msecs_to_jiffies(timeout) + jiffies;
			for (timeout_flag = 0; !timeout_flag &&
			((cntl_value_r & cacheEntry->poll_mask) != 0);) {
				if (time_after(jiffies, timeout_jiffies))
					timeout_flag = 1;
				NX_RD_DUMP_REG(cntrl_addr,
					adapter->ahw.pci_base0,
							&cntl_value_r);
			}
			if (timeout_flag) {
				dev_err(&adapter->pdev->dev,
						"Timeout in processing L2 Tag poll.\n");
				return -1;
			}
		}
		addr = read_addr;
		for (k = 0; k < read_cnt; k++) {
			NX_RD_DUMP_REG(addr, adapter->ahw.pci_base0,
					&read_value);
			*data_buff++ = read_value;
			addr += cacheEntry->read_addr_stride;
		}
		tag_value += cacheEntry->tag_value_stride;
	}
	return read_cnt * loop_cnt * sizeof(read_value);
}


/* Handle L1 Cache */
static u32 netxen_md_L1Cache(struct netxen_adapter *adapter,
				struct netxen_minidump_entry_cache
					*cacheEntry, u32 *data_buff)
{
	int i, k, loop_cnt;
	u32 addr, read_addr, read_value, cntrl_addr, tag_reg_addr;
	u32 tag_value, read_cnt;
	u8 cntl_value_w;

	loop_cnt = cacheEntry->op_count;
	read_addr = cacheEntry->read_addr;
	cntrl_addr = cacheEntry->control_addr;
	cntl_value_w = (u32) cacheEntry->write_value;
	tag_reg_addr = cacheEntry->tag_reg_addr;
	tag_value = cacheEntry->init_tag_value;
	read_cnt = cacheEntry->read_addr_cnt;

	for (i = 0; i < loop_cnt; i++) {
		NX_WR_DUMP_REG(tag_reg_addr, adapter->ahw.pci_base0, tag_value);
		NX_WR_DUMP_REG(cntrl_addr, adapter->ahw.pci_base0,
						(u32) cntl_value_w);
		addr = read_addr;
		for (k = 0; k < read_cnt; k++) {
			NX_RD_DUMP_REG(addr,
				adapter->ahw.pci_base0,
						&read_value);
			*data_buff++ = read_value;
			addr += cacheEntry->read_addr_stride;
		}
		tag_value += cacheEntry->tag_value_stride;
	}
	return read_cnt * loop_cnt * sizeof(read_value);
}

/* Reading OCM memory */
static u32
netxen_md_rdocm(struct netxen_adapter *adapter,
				struct netxen_minidump_entry_rdocm
					*ocmEntry, u32 *data_buff)
{
	int i, loop_cnt;
	u32 value;
	void __iomem *addr;
	addr = (ocmEntry->read_addr + adapter->ahw.pci_base0);
	loop_cnt = ocmEntry->op_count;

	for (i = 0; i < loop_cnt; i++) {
		value = readl(addr);
		*data_buff++ = value;
		addr += ocmEntry->read_addr_stride;
	}
	return i * sizeof(u32);
}

/* Read MUX data */
static u32
netxen_md_rdmux(struct netxen_adapter *adapter, struct netxen_minidump_entry_mux
					*muxEntry, u32 *data_buff)
{
	int loop_cnt = 0;
	u32 read_addr, read_value, select_addr, sel_value;

	read_addr = muxEntry->read_addr;
	sel_value = muxEntry->select_value;
	select_addr = muxEntry->select_addr;

	for (loop_cnt = 0; loop_cnt < muxEntry->op_count; loop_cnt++) {
		NX_WR_DUMP_REG(select_addr, adapter->ahw.pci_base0, sel_value);
		NX_RD_DUMP_REG(read_addr, adapter->ahw.pci_base0, &read_value);
		*data_buff++ = sel_value;
		*data_buff++ = read_value;
		sel_value += muxEntry->select_value_stride;
	}
	return loop_cnt * (2 * sizeof(u32));
}

/* Handling Queue State Reads */
static u32
netxen_md_rdqueue(struct netxen_adapter *adapter,
				struct netxen_minidump_entry_queue
					*queueEntry, u32 *data_buff)
{
	int loop_cnt, k;
	u32 queue_id, read_addr, read_value, read_stride, select_addr, read_cnt;

	read_cnt = queueEntry->read_addr_cnt;
	read_stride = queueEntry->read_addr_stride;
	select_addr = queueEntry->select_addr;

	for (loop_cnt = 0, queue_id = 0; loop_cnt < queueEntry->op_count;
				 loop_cnt++) {
		NX_WR_DUMP_REG(select_addr, adapter->ahw.pci_base0, queue_id);
		read_addr = queueEntry->read_addr;
		for (k = 0; k < read_cnt; k--) {
			NX_RD_DUMP_REG(read_addr, adapter->ahw.pci_base0,
							&read_value);
			*data_buff++ = read_value;
			read_addr += read_stride;
		}
		queue_id += queueEntry->queue_id_stride;
	}
	return loop_cnt * (read_cnt * sizeof(read_value));
}


/*
* We catch an error where driver does not read
* as much data as we expect from the entry.
*/

static int netxen_md_entry_err_chk(struct netxen_adapter *adapter,
				struct netxen_minidump_entry *entry, int esize)
{
	if (esize < 0) {
		entry->hdr.driver_flags |= NX_DUMP_SKIP;
		return esize;
	}
	if (esize != entry->hdr.entry_capture_size) {
		entry->hdr.entry_capture_size = esize;
		entry->hdr.driver_flags |= NX_DUMP_SIZE_ERR;
		dev_info(&adapter->pdev->dev,
			"Invalidate dump, Type:%d\tMask:%d\tSize:%dCap_size:%d\n",
			entry->hdr.entry_type, entry->hdr.entry_capture_mask,
			esize, entry->hdr.entry_capture_size);
		dev_info(&adapter->pdev->dev, "Aborting further dump capture\n");
	}
	return 0;
}

static int netxen_parse_md_template(struct netxen_adapter *adapter)
{
	int num_of_entries, buff_level, e_cnt, esize;
	int end_cnt = 0, rv = 0, sane_start = 0, sane_end = 0;
	char *dbuff;
	void *template_buff = adapter->mdump.md_template;
	char *dump_buff = adapter->mdump.md_capture_buff;
	int capture_mask = adapter->mdump.md_capture_mask;
	struct netxen_minidump_template_hdr *template_hdr;
	struct netxen_minidump_entry *entry;

	if ((capture_mask & 0x3) != 0x3) {
		dev_err(&adapter->pdev->dev, "Capture mask %02x below minimum needed "
			"for valid firmware dump\n", capture_mask);
		return -EINVAL;
	}
	template_hdr = (struct netxen_minidump_template_hdr *) template_buff;
	num_of_entries = template_hdr->num_of_entries;
	entry = (struct netxen_minidump_entry *) ((char *) template_buff +
				template_hdr->first_entry_offset);
	memcpy(dump_buff, template_buff, adapter->mdump.md_template_size);
	dump_buff = dump_buff + adapter->mdump.md_template_size;

	if (template_hdr->entry_type == TLHDR)
		sane_start = 1;

	for (e_cnt = 0, buff_level = 0; e_cnt < num_of_entries; e_cnt++) {
		if (!(entry->hdr.entry_capture_mask & capture_mask)) {
			entry->hdr.driver_flags |= NX_DUMP_SKIP;
			entry = (struct netxen_minidump_entry *)
				((char *) entry + entry->hdr.entry_size);
			continue;
		}
		switch (entry->hdr.entry_type) {
		case RDNOP:
			entry->hdr.driver_flags |= NX_DUMP_SKIP;
			break;
		case RDEND:
			entry->hdr.driver_flags |= NX_DUMP_SKIP;
			if (!sane_end)
				end_cnt = e_cnt;
			sane_end += 1;
			break;
		case CNTRL:
			rv = netxen_md_cntrl(adapter,
				template_hdr, (void *)entry);
			if (rv)
				entry->hdr.driver_flags |= NX_DUMP_SKIP;
			break;
		case RDCRB:
			dbuff = dump_buff + buff_level;
			esize = netxen_md_rd_crb(adapter,
					(void *) entry, (void *) dbuff);
			rv = netxen_md_entry_err_chk
				(adapter, entry, esize);
			if (rv < 0)
				break;
			buff_level += esize;
			break;
		case RDMN:
		case RDMEM:
			dbuff = dump_buff + buff_level;
			esize = netxen_md_rdmem(adapter,
				(void *) entry, (void *) dbuff);
			rv = netxen_md_entry_err_chk
				(adapter, entry, esize);
			if (rv < 0)
				break;
			buff_level += esize;
			break;
		case BOARD:
		case RDROM:
			dbuff = dump_buff + buff_level;
			esize = netxen_md_rdrom(adapter,
				(void *) entry, (void *) dbuff);
			rv = netxen_md_entry_err_chk
				(adapter, entry, esize);
			if (rv < 0)
				break;
			buff_level += esize;
			break;
		case L2ITG:
		case L2DTG:
		case L2DAT:
		case L2INS:
			dbuff = dump_buff + buff_level;
			esize = netxen_md_L2Cache(adapter,
				(void *) entry, (void *) dbuff);
			rv = netxen_md_entry_err_chk
				(adapter, entry, esize);
			if (rv < 0)
				break;
			buff_level += esize;
			break;
		case L1DAT:
		case L1INS:
			dbuff = dump_buff + buff_level;
			esize = netxen_md_L1Cache(adapter,
				(void *) entry, (void *) dbuff);
			rv = netxen_md_entry_err_chk
				(adapter, entry, esize);
			if (rv < 0)
				break;
			buff_level += esize;
			break;
		case RDOCM:
			dbuff = dump_buff + buff_level;
			esize = netxen_md_rdocm(adapter,
				(void *) entry, (void *) dbuff);
			rv = netxen_md_entry_err_chk
				(adapter, entry, esize);
			if (rv < 0)
				break;
			buff_level += esize;
			break;
		case RDMUX:
			dbuff = dump_buff + buff_level;
			esize = netxen_md_rdmux(adapter,
				(void *) entry, (void *) dbuff);
			rv = netxen_md_entry_err_chk
				(adapter, entry, esize);
			if (rv < 0)
				break;
			buff_level += esize;
			break;
		case QUEUE:
			dbuff = dump_buff + buff_level;
			esize = netxen_md_rdqueue(adapter,
				(void *) entry, (void *) dbuff);
			rv = netxen_md_entry_err_chk
				(adapter, entry, esize);
			if (rv  < 0)
				break;
			buff_level += esize;
			break;
		default:
			entry->hdr.driver_flags |= NX_DUMP_SKIP;
			break;
		}
		/* Next entry in the template */
		entry = (struct netxen_minidump_entry *)
			((char *) entry + entry->hdr.entry_size);
	}
	if (!sane_start || sane_end > 1) {
		dev_err(&adapter->pdev->dev,
				"Firmware minidump template configuration error.\n");
	}
	return 0;
}

static int
netxen_collect_minidump(struct netxen_adapter *adapter)
{
	int ret = 0;
	struct netxen_minidump_template_hdr *hdr;
	struct timespec val;
	hdr = (struct netxen_minidump_template_hdr *)
				adapter->mdump.md_template;
	hdr->driver_capture_mask = adapter->mdump.md_capture_mask;
	jiffies_to_timespec(jiffies, &val);
	hdr->driver_timestamp = (u32) val.tv_sec;
	hdr->driver_info_word2 = adapter->fw_version;
	hdr->driver_info_word3 = NXRD32(adapter, CRB_DRIVER_VERSION);
	ret = netxen_parse_md_template(adapter);
	if (ret)
		return ret;

	return ret;
}


void
netxen_dump_fw(struct netxen_adapter *adapter)
{
	struct netxen_minidump_template_hdr *hdr;
	int i, k, data_size = 0;
	u32 capture_mask;
	hdr = (struct netxen_minidump_template_hdr *)
				adapter->mdump.md_template;
	capture_mask = adapter->mdump.md_capture_mask;

	for (i = 0x2, k = 1; (i & NX_DUMP_MASK_MAX); i <<= 1, k++) {
		if (i & capture_mask)
			data_size += hdr->capture_size_array[k];
	}
	if (!data_size) {
		dev_err(&adapter->pdev->dev,
				"Invalid cap sizes for capture_mask=0x%x\n",
			adapter->mdump.md_capture_mask);
		return;
	}
	adapter->mdump.md_capture_size = data_size;
	adapter->mdump.md_dump_size = adapter->mdump.md_template_size +
					adapter->mdump.md_capture_size;
	if (!adapter->mdump.md_capture_buff) {
		adapter->mdump.md_capture_buff =
				vzalloc(adapter->mdump.md_dump_size);
		if (!adapter->mdump.md_capture_buff)
			return;

		if (netxen_collect_minidump(adapter)) {
			adapter->mdump.has_valid_dump = 0;
			adapter->mdump.md_dump_size = 0;
			vfree(adapter->mdump.md_capture_buff);
			adapter->mdump.md_capture_buff = NULL;
			dev_err(&adapter->pdev->dev,
				"Error in collecting firmware minidump.\n");
		} else {
			adapter->mdump.md_timestamp = jiffies;
			adapter->mdump.has_valid_dump = 1;
			adapter->fw_mdump_rdy = 1;
			dev_info(&adapter->pdev->dev, "%s Successfully "
				"collected fw dump.\n", adapter->netdev->name);
		}

	} else {
		dev_info(&adapter->pdev->dev,
					"Cannot overwrite previously collected "
							"firmware minidump.\n");
		adapter->fw_mdump_rdy = 1;
		return;
	}
}
