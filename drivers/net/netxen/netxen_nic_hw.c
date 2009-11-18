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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 *
 */

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

#define ADDR_IN_RANGE(addr, low, high)	\
	(((addr) < (high)) && ((addr) >= (low)))

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

int
netxen_pcie_sem_lock(struct netxen_adapter *adapter, int sem, u32 id_reg)
{
	int done = 0, timeout = 0;

	while (!done) {
		done = NXRD32(adapter, NETXEN_PCIE_REG(PCIE_SEM_LOCK(sem)));
		if (done == 1)
			break;
		if (++timeout >= NETXEN_PCIE_SEM_TIMEOUT)
			return -1;
		msleep(1);
	}

	if (id_reg)
		NXWR32(adapter, id_reg, adapter->portnum);

	return 0;
}

void
netxen_pcie_sem_unlock(struct netxen_adapter *adapter, int sem)
{
	int val;
	val = NXRD32(adapter, NETXEN_PCIE_REG(PCIE_SEM_UNLOCK(sem)));
}

int netxen_niu_xg_init_port(struct netxen_adapter *adapter, int port)
{
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		NXWR32(adapter, NETXEN_NIU_XGE_CONFIG_1+(0x10000*port), 0x1447);
		NXWR32(adapter, NETXEN_NIU_XGE_CONFIG_0+(0x10000*port), 0x5);
	}

	return 0;
}

/* Disable an XG interface */
int netxen_niu_disable_xg_port(struct netxen_adapter *adapter)
{
	__u32 mac_cfg;
	u32 port = adapter->physical_port;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		return 0;

	if (port > NETXEN_NIU_MAX_XG_PORTS)
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

int netxen_p2_nic_set_promisc(struct netxen_adapter *adapter, u32 mode)
{
	__u32 reg;
	u32 port = adapter->physical_port;

	if (port > NETXEN_NIU_MAX_XG_PORTS)
		return -EINVAL;

	reg = NXRD32(adapter, NETXEN_NIU_XGE_CONFIG_1 + (0x10000 * port));
	if (mode == NETXEN_NIU_PROMISC_MODE)
		reg = (reg | 0x2000UL);
	else
		reg = (reg & ~0x2000UL);

	if (mode == NETXEN_NIU_ALLMULTI_MODE)
		reg = (reg | 0x1000UL);
	else
		reg = (reg & ~0x1000UL);

	NXWR32(adapter, NETXEN_NIU_XGE_CONFIG_1 + (0x10000 * port), reg);

	return 0;
}

int netxen_p2_nic_set_mac_addr(struct netxen_adapter *adapter, u8 *addr)
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
	u8 *addr = adapter->netdev->dev_addr;

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
	u8 *addr = adapter->netdev->dev_addr;

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
		__netif_tx_unlock_bh(tx_ring->txq);
		return -EBUSY;
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
	memcpy(mac_req->mac_addr, addr, 6);

	return netxen_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
}

static int nx_p3_nic_add_mac(struct netxen_adapter *adapter,
		u8 *addr, struct list_head *del_list)
{
	struct list_head *head;
	nx_mac_list_t *cur;

	/* look up if already exists */
	list_for_each(head, del_list) {
		cur = list_entry(head, nx_mac_list_t, list);

		if (memcmp(addr, cur->mac_addr, ETH_ALEN) == 0) {
			list_move_tail(head, &adapter->mac_list);
			return 0;
		}
	}

	cur = kzalloc(sizeof(nx_mac_list_t), GFP_ATOMIC);
	if (cur == NULL) {
		printk(KERN_ERR "%s: failed to add mac address filter\n",
				adapter->netdev->name);
		return -ENOMEM;
	}
	memcpy(cur->mac_addr, addr, ETH_ALEN);
	list_add_tail(&cur->list, &adapter->mac_list);
	return nx_p3_sre_macaddr_change(adapter,
				cur->mac_addr, NETXEN_MAC_ADD);
}

void netxen_p3_nic_set_multi(struct net_device *netdev)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct dev_mc_list *mc_ptr;
	u8 bcast_addr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	u32 mode = VPORT_MISS_MODE_DROP;
	LIST_HEAD(del_list);
	struct list_head *head;
	nx_mac_list_t *cur;

	list_splice_tail_init(&adapter->mac_list, &del_list);

	nx_p3_nic_add_mac(adapter, netdev->dev_addr, &del_list);
	nx_p3_nic_add_mac(adapter, bcast_addr, &del_list);

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
			nx_p3_nic_add_mac(adapter, mc_ptr->dmi_addr, &del_list);
		}
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

int netxen_p3_nic_set_mac_addr(struct netxen_adapter *adapter, u8 *addr)
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
	u64 word;
	int rv;

	memset(&req, 0, sizeof(nx_nic_req_t));

	req.qhdr = cpu_to_le64(NX_HOST_REQUEST << 23);

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

int netxen_config_hw_lro(struct netxen_adapter *adapter, int enable)
{
	nx_nic_req_t req;
	u64 word;
	int rv = 0;

	if ((adapter->flags & NETXEN_NIC_LRO_ENABLED) == enable)
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

	adapter->flags ^= NETXEN_NIC_LRO_ENABLED;

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

int netxen_config_ipaddr(struct netxen_adapter *adapter, u32 ip, int cmd)
{
	nx_nic_req_t req;
	u64 word;
	int rv;

	memset(&req, 0, sizeof(nx_nic_req_t));
	req.qhdr = cpu_to_le64(NX_HOST_REQUEST << 23);

	word = NX_NIC_H2C_OPCODE_CONFIG_IPADDR | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	req.words[0] = cpu_to_le64(cmd);
	req.words[1] = cpu_to_le64(ip);

	rv = netxen_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0) {
		printk(KERN_ERR "%s: could not notify %s IP 0x%x reuqest\n",
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

	offset = NX_FW_MAC_ADDR_OFFSET + (adapter->portnum * sizeof(u64));

	if (netxen_get_flash_block(adapter, offset, sizeof(u64), pmac) == -1)
		return -1;

	if (*mac == cpu_to_le64(~0ULL)) {

		offset = NX_OLD_MAC_ADDR_OFFSET +
			(adapter->portnum * sizeof(u64));

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
netxen_nic_pci_get_crb_addr_2M(struct netxen_adapter *adapter, ulong *off)
{
	crb_128M_2M_sub_block_map_t *m;


	if (*off >= NETXEN_CRB_MAX)
		return -1;

	if (*off >= NETXEN_PCI_CAMQM && (*off < NETXEN_PCI_CAMQM_2M_END)) {
		*off = (*off - NETXEN_PCI_CAMQM) + NETXEN_PCI_CAMQM_2M_BASE +
			(ulong)adapter->ahw.pci_base0;
		return 0;
	}

	if (*off < NETXEN_PCI_CRBSPACE)
		return -1;

	*off -= NETXEN_PCI_CRBSPACE;

	/*
	 * Try direct map
	 */
	m = &crb_128M_2M_map[CRB_BLK(*off)].sub_block[CRB_SUBBLK(*off)];

	if (m->valid && (m->start_128M <= *off) && (m->end_128M > *off)) {
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
netxen_nic_hw_write_wx_128M(struct netxen_adapter *adapter, ulong off, u32 data)
{
	unsigned long flags;
	void __iomem *addr;

	if (ADDR_IN_WINDOW1(off))
		addr = NETXEN_CRB_NORMALIZE(adapter, off);
	else
		addr = pci_base_offset(adapter, off);

	BUG_ON(!addr);

	if (ADDR_IN_WINDOW1(off)) {	/* Window 1 */
		read_lock(&adapter->adapter_lock);
		writel(data, addr);
		read_unlock(&adapter->adapter_lock);
	} else {		/* Window 0 */
		write_lock_irqsave(&adapter->adapter_lock, flags);
		addr = pci_base_offset(adapter, off);
		netxen_nic_pci_change_crbwindow_128M(adapter, 0);
		writel(data, addr);
		netxen_nic_pci_change_crbwindow_128M(adapter, 1);
		write_unlock_irqrestore(&adapter->adapter_lock, flags);
	}

	return 0;
}

static u32
netxen_nic_hw_read_wx_128M(struct netxen_adapter *adapter, ulong off)
{
	unsigned long flags;
	void __iomem *addr;
	u32 data;

	if (ADDR_IN_WINDOW1(off))
		addr = NETXEN_CRB_NORMALIZE(adapter, off);
	else
		addr = pci_base_offset(adapter, off);

	BUG_ON(!addr);

	if (ADDR_IN_WINDOW1(off)) {	/* Window 1 */
		read_lock(&adapter->adapter_lock);
		data = readl(addr);
		read_unlock(&adapter->adapter_lock);
	} else {		/* Window 0 */
		write_lock_irqsave(&adapter->adapter_lock, flags);
		netxen_nic_pci_change_crbwindow_128M(adapter, 0);
		data = readl(addr);
		netxen_nic_pci_change_crbwindow_128M(adapter, 1);
		write_unlock_irqrestore(&adapter->adapter_lock, flags);
	}

	return data;
}

static int
netxen_nic_hw_write_wx_2M(struct netxen_adapter *adapter, ulong off, u32 data)
{
	unsigned long flags;
	int rv;

	rv = netxen_nic_pci_get_crb_addr_2M(adapter, &off);

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
		writel(data, (void __iomem *)off);
		crb_win_unlock(adapter);
		write_unlock_irqrestore(&adapter->adapter_lock, flags);
	} else
		writel(data, (void __iomem *)off);


	return 0;
}

static u32
netxen_nic_hw_read_wx_2M(struct netxen_adapter *adapter, ulong off)
{
	unsigned long flags;
	int rv;
	u32 data;

	rv = netxen_nic_pci_get_crb_addr_2M(adapter, &off);

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
		data = readl((void __iomem *)off);
		crb_win_unlock(adapter);
		write_unlock_irqrestore(&adapter->adapter_lock, flags);
	} else
		data = readl((void __iomem *)off);

	return data;
}

static int netxen_pci_set_window_warning_count;

static unsigned long
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

/* window 1 registers only */
static void netxen_nic_io_write_128M(struct netxen_adapter *adapter,
		void __iomem *addr, u32 data)
{
	read_lock(&adapter->adapter_lock);
	writel(data, addr);
	read_unlock(&adapter->adapter_lock);
}

static u32 netxen_nic_io_read_128M(struct netxen_adapter *adapter,
		void __iomem *addr)
{
	u32 val;

	read_lock(&adapter->adapter_lock);
	val = readl(addr);
	read_unlock(&adapter->adapter_lock);

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
	ulong off = offset;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		if (offset < NETXEN_CRB_PCIX_HOST2 &&
				offset > NETXEN_CRB_PCIX_HOST)
			return PCI_OFFSET_SECOND_RANGE(adapter, offset);
		return NETXEN_CRB_NORMALIZE(adapter, offset);
	}

	BUG_ON(netxen_nic_pci_get_crb_addr_2M(adapter, &off));
	return (void __iomem *)off;
}

static unsigned long
netxen_nic_pci_set_window_2M(struct netxen_adapter *adapter,
		unsigned long long addr)
{
	int window;
	u32 win_read;

	if (ADDR_IN_RANGE(addr, NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX)) {
		/* DDR network side */
		window = MN_WIN(addr);
		adapter->ahw.ddr_mn_window = window;
		NXWR32(adapter, adapter->ahw.mn_win_crb, window);
		win_read = NXRD32(adapter, adapter->ahw.mn_win_crb);
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
		NXWR32(adapter, adapter->ahw.mn_win_crb, window);
		win_read = NXRD32(adapter, adapter->ahw.mn_win_crb);
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
		NXWR32(adapter, adapter->ahw.ms_win_crb, window);
		win_read = NXRD32(adapter, adapter->ahw.ms_win_crb);
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

#define MAX_CTL_CHECK   1000

static int
netxen_nic_pci_mem_write_128M(struct netxen_adapter *adapter,
		u64 off, void *data, int size)
{
	unsigned long   flags;
	int	     i, j, ret = 0, loop, sz[2], off0;
	uint32_t      temp;
	uint64_t      off8, tmpw, word[2] = {0, 0};
	void __iomem *mem_crb;

	if (size != 8)
		return -EIO;

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_QDR_NET,
				NETXEN_ADDR_QDR_NET_MAX_P2)) {
		mem_crb = pci_base_offset(adapter, NETXEN_CRB_QDR_NET);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX)) {
		mem_crb = pci_base_offset(adapter, NETXEN_CRB_DDR_NET);
		goto correct;
	}

	return -EIO;

correct:
	off8 = off & 0xfffffff8;
	off0 = off & 0x7;
	sz[0] = (size < (8 - off0)) ? size : (8 - off0);
	sz[1] = size - sz[0];
	loop = ((off0 + size - 1) >> 3) + 1;

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

static int
netxen_nic_pci_mem_read_128M(struct netxen_adapter *adapter,
		u64 off, void *data, int size)
{
	unsigned long   flags;
	int	     i, j = 0, k, start, end, loop, sz[2], off0[2];
	uint32_t      temp;
	uint64_t      off8, val, word[2] = {0, 0};
	void __iomem *mem_crb;

	if (size != 8)
		return -EIO;

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_QDR_NET,
				NETXEN_ADDR_QDR_NET_MAX_P2)) {
		mem_crb = pci_base_offset(adapter, NETXEN_CRB_QDR_NET);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX)) {
		mem_crb = pci_base_offset(adapter, NETXEN_CRB_DDR_NET);
		goto correct;
	}

	return -EIO;

correct:
	off8 = off & 0xfffffff8;
	off0[0] = off & 0x7;
	off0[1] = 0;
	sz[0] = (size < (8 - off0[0])) ? size : (8 - off0[0]);
	sz[1] = size - sz[0];
	loop = ((off0[0] + size - 1) >> 3) + 1;

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

static int
netxen_nic_pci_mem_write_2M(struct netxen_adapter *adapter,
		u64 off, void *data, int size)
{
	int i, j, ret = 0, loop, sz[2], off0;
	uint32_t temp;
	uint64_t off8, tmpw, word[2] = {0, 0};
	void __iomem *mem_crb;

	if (size != 8)
		return -EIO;

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_QDR_NET,
				NETXEN_ADDR_QDR_NET_MAX_P3)) {
		mem_crb = netxen_get_ioaddr(adapter, NETXEN_CRB_QDR_NET);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX)) {
		mem_crb = netxen_get_ioaddr(adapter, NETXEN_CRB_DDR_NET);
		goto correct;
	}

	return -EIO;

correct:
	off8 = off & 0xfffffff8;
	off0 = off & 0x7;
	sz[0] = (size < (8 - off0)) ? size : (8 - off0);
	sz[1] = size - sz[0];
	loop = ((off0 + size - 1) >> 3) + 1;

	if ((size != 8) || (off0 != 0)) {
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

	/*
	 * don't lock here - write_wx gets the lock if each time
	 * write_lock_irqsave(&adapter->adapter_lock, flags);
	 * netxen_nic_pci_change_crbwindow_128M(adapter, 0);
	 */

	for (i = 0; i < loop; i++) {
		writel(off8 + (i << 3), mem_crb+MIU_TEST_AGT_ADDR_LO);
		writel(0, mem_crb+MIU_TEST_AGT_ADDR_HI);
		writel(word[i] & 0xffffffff, mem_crb+MIU_TEST_AGT_WRDATA_LO);
		writel((word[i] >> 32) & 0xffffffff,
				mem_crb+MIU_TEST_AGT_WRDATA_HI);
		writel((MIU_TA_CTL_ENABLE | MIU_TA_CTL_WRITE),
				mem_crb+MIU_TEST_AGT_CTRL);
		writel(MIU_TA_CTL_START | MIU_TA_CTL_ENABLE | MIU_TA_CTL_WRITE,
				mem_crb+MIU_TEST_AGT_CTRL);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			temp = readl(mem_crb + MIU_TEST_AGT_CTRL);
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

static int
netxen_nic_pci_mem_read_2M(struct netxen_adapter *adapter,
		u64 off, void *data, int size)
{
	int i, j = 0, k, start, end, loop, sz[2], off0[2];
	uint32_t      temp;
	uint64_t      off8, val, word[2] = {0, 0};
	void __iomem *mem_crb;

	if (size != 8)
		return -EIO;

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_QDR_NET,
				NETXEN_ADDR_QDR_NET_MAX_P3)) {
		mem_crb = netxen_get_ioaddr(adapter, NETXEN_CRB_QDR_NET);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, NETXEN_ADDR_DDR_NET, NETXEN_ADDR_DDR_NET_MAX)) {
		mem_crb = netxen_get_ioaddr(adapter, NETXEN_CRB_DDR_NET);
		goto correct;
	}

	return -EIO;

correct:
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
		writel(off8 + (i << 3), mem_crb + MIU_TEST_AGT_ADDR_LO);
		writel(0, mem_crb + MIU_TEST_AGT_ADDR_HI);
		writel(MIU_TA_CTL_ENABLE, mem_crb + MIU_TEST_AGT_CTRL);
		writel(MIU_TA_CTL_START | MIU_TA_CTL_ENABLE,
				mem_crb + MIU_TEST_AGT_CTRL);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			temp = readl(mem_crb + MIU_TEST_AGT_CTRL);
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
			temp = readl(mem_crb + MIU_TEST_AGT_RDDATA(k));
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

	adapter->ahw.board_type = board_type;

	if (board_type == NETXEN_BRDTYPE_P3_4_GB_MM) {
		u32 gpio = NXRD32(adapter, NETXEN_ROMUSB_GLB_PAD_GPIO_I);
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
	NXWR32(adapter, NETXEN_NIU_GB_MAX_FRAME_SIZE(adapter->physical_port),
		new_mtu);
	return 0;
}

int netxen_nic_set_mtu_xgb(struct netxen_adapter *adapter, int new_mtu)
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
