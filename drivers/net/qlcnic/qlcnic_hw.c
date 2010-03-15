/*
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
 * in the file called "COPYING".
 *
 */

#include "qlcnic.h"

#include <net/ip.h>

#define MASK(n) ((1ULL<<(n))-1)
#define OCM_WIN_P3P(addr) (addr & 0xffc0000)

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

static void __iomem *pci_base_offset(struct qlcnic_adapter *adapter,
					    unsigned long off)
{
	if (ADDR_IN_RANGE(off, FIRST_PAGE_GROUP_START, FIRST_PAGE_GROUP_END))
		return PCI_OFFSET_FIRST_RANGE(adapter, off);

	return NULL;
}

static const struct crb_128M_2M_block_map
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
static const unsigned crb_hub_agt[64] = {
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PS,
	QLCNIC_HW_CRB_HUB_AGT_ADR_MN,
	QLCNIC_HW_CRB_HUB_AGT_ADR_MS,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SRE,
	QLCNIC_HW_CRB_HUB_AGT_ADR_NIU,
	QLCNIC_HW_CRB_HUB_AGT_ADR_QMN,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SQN0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SQN1,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SQN2,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SQN3,
	QLCNIC_HW_CRB_HUB_AGT_ADR_I2Q,
	QLCNIC_HW_CRB_HUB_AGT_ADR_TIMR,
	QLCNIC_HW_CRB_HUB_AGT_ADR_ROMUSB,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGN4,
	QLCNIC_HW_CRB_HUB_AGT_ADR_XDMA,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGN0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGN1,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGN2,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGN3,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGND,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGNI,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGS0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGS1,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGS2,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGS3,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGSI,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SN,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_EG,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PS,
	QLCNIC_HW_CRB_HUB_AGT_ADR_CAM,
	0,
	0,
	0,
	0,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_TIMR,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX1,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX2,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX3,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX4,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX5,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX6,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX7,
	QLCNIC_HW_CRB_HUB_AGT_ADR_XDMA,
	QLCNIC_HW_CRB_HUB_AGT_ADR_I2Q,
	QLCNIC_HW_CRB_HUB_AGT_ADR_ROMUSB,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX8,
	QLCNIC_HW_CRB_HUB_AGT_ADR_RPMX9,
	QLCNIC_HW_CRB_HUB_AGT_ADR_OCM0,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_SMB,
	QLCNIC_HW_CRB_HUB_AGT_ADR_I2C0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_I2C1,
	0,
	QLCNIC_HW_CRB_HUB_AGT_ADR_PGNC,
	0,
};

/*  PCI Windowing for DDR regions.  */

#define QLCNIC_PCIE_SEM_TIMEOUT	10000

int
qlcnic_pcie_sem_lock(struct qlcnic_adapter *adapter, int sem, u32 id_reg)
{
	int done = 0, timeout = 0;

	while (!done) {
		done = QLCRD32(adapter, QLCNIC_PCIE_REG(PCIE_SEM_LOCK(sem)));
		if (done == 1)
			break;
		if (++timeout >= QLCNIC_PCIE_SEM_TIMEOUT)
			return -EIO;
		msleep(1);
	}

	if (id_reg)
		QLCWR32(adapter, id_reg, adapter->portnum);

	return 0;
}

void
qlcnic_pcie_sem_unlock(struct qlcnic_adapter *adapter, int sem)
{
	QLCRD32(adapter, QLCNIC_PCIE_REG(PCIE_SEM_UNLOCK(sem)));
}

static int
qlcnic_send_cmd_descs(struct qlcnic_adapter *adapter,
		struct cmd_desc_type0 *cmd_desc_arr, int nr_desc)
{
	u32 i, producer, consumer;
	struct qlcnic_cmd_buffer *pbuf;
	struct cmd_desc_type0 *cmd_desc;
	struct qlcnic_host_tx_ring *tx_ring;

	i = 0;

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		return -EIO;

	tx_ring = adapter->tx_ring;
	__netif_tx_lock_bh(tx_ring->txq);

	producer = tx_ring->producer;
	consumer = tx_ring->sw_consumer;

	if (nr_desc >= qlcnic_tx_avail(tx_ring)) {
		netif_tx_stop_queue(tx_ring->txq);
		__netif_tx_unlock_bh(tx_ring->txq);
		adapter->stats.xmit_off++;
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

	qlcnic_update_cmd_producer(adapter, tx_ring);

	__netif_tx_unlock_bh(tx_ring->txq);

	return 0;
}

static int
qlcnic_sre_macaddr_change(struct qlcnic_adapter *adapter, u8 *addr,
				unsigned op)
{
	struct qlcnic_nic_req req;
	struct qlcnic_mac_req *mac_req;
	u64 word;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));
	req.qhdr = cpu_to_le64(QLCNIC_REQUEST << 23);

	word = QLCNIC_MAC_EVENT | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	mac_req = (struct qlcnic_mac_req *)&req.words[0];
	mac_req->op = op;
	memcpy(mac_req->mac_addr, addr, 6);

	return qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
}

static int qlcnic_nic_add_mac(struct qlcnic_adapter *adapter, u8 *addr)
{
	struct list_head *head;
	struct qlcnic_mac_list_s *cur;

	/* look up if already exists */
	list_for_each(head, &adapter->mac_list) {
		cur = list_entry(head, struct qlcnic_mac_list_s, list);
		if (memcmp(addr, cur->mac_addr, ETH_ALEN) == 0)
			return 0;
	}

	cur = kzalloc(sizeof(struct qlcnic_mac_list_s), GFP_ATOMIC);
	if (cur == NULL) {
		dev_err(&adapter->netdev->dev,
			"failed to add mac address filter\n");
		return -ENOMEM;
	}
	memcpy(cur->mac_addr, addr, ETH_ALEN);
	list_add_tail(&cur->list, &adapter->mac_list);

	return qlcnic_sre_macaddr_change(adapter,
				cur->mac_addr, QLCNIC_MAC_ADD);
}

void qlcnic_set_multi(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct dev_mc_list *mc_ptr;
	u8 bcast_addr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	u32 mode = VPORT_MISS_MODE_DROP;

	qlcnic_nic_add_mac(adapter, adapter->mac_addr);
	qlcnic_nic_add_mac(adapter, bcast_addr);

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
		netdev_for_each_mc_addr(mc_ptr, netdev) {
			qlcnic_nic_add_mac(adapter, mc_ptr->dmi_addr);
		}
	}

send_fw_cmd:
	qlcnic_nic_set_promisc(adapter, mode);
}

int qlcnic_nic_set_promisc(struct qlcnic_adapter *adapter, u32 mode)
{
	struct qlcnic_nic_req req;
	u64 word;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));

	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_PROXY_SET_VPORT_MISS_MODE |
			((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	req.words[0] = cpu_to_le64(mode);

	return qlcnic_send_cmd_descs(adapter,
				(struct cmd_desc_type0 *)&req, 1);
}

void qlcnic_free_mac_list(struct qlcnic_adapter *adapter)
{
	struct qlcnic_mac_list_s *cur;
	struct list_head *head = &adapter->mac_list;

	while (!list_empty(head)) {
		cur = list_entry(head->next, struct qlcnic_mac_list_s, list);
		qlcnic_sre_macaddr_change(adapter,
				cur->mac_addr, QLCNIC_MAC_DEL);
		list_del(&cur->list);
		kfree(cur);
	}
}

#define	QLCNIC_CONFIG_INTR_COALESCE	3

/*
 * Send the interrupt coalescing parameter set by ethtool to the card.
 */
int qlcnic_config_intr_coalesce(struct qlcnic_adapter *adapter)
{
	struct qlcnic_nic_req req;
	u64 word[6];
	int rv, i;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));

	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word[0] = QLCNIC_CONFIG_INTR_COALESCE | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word[0]);

	memcpy(&word[0], &adapter->coal, sizeof(adapter->coal));
	for (i = 0; i < 6; i++)
		req.words[i] = cpu_to_le64(word[i]);

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0)
		dev_err(&adapter->netdev->dev,
			"Could not send interrupt coalescing parameters\n");

	return rv;
}

int qlcnic_config_hw_lro(struct qlcnic_adapter *adapter, int enable)
{
	struct qlcnic_nic_req req;
	u64 word;
	int rv;

	if ((adapter->flags & QLCNIC_LRO_ENABLED) == enable)
		return 0;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));

	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_HW_LRO | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	req.words[0] = cpu_to_le64(enable);

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0)
		dev_err(&adapter->netdev->dev,
			"Could not send configure hw lro request\n");

	adapter->flags ^= QLCNIC_LRO_ENABLED;

	return rv;
}

int qlcnic_config_bridged_mode(struct qlcnic_adapter *adapter, int enable)
{
	struct qlcnic_nic_req req;
	u64 word;
	int rv;

	if (!!(adapter->flags & QLCNIC_BRIDGE_ENABLED) == enable)
		return 0;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));

	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_BRIDGING |
		((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	req.words[0] = cpu_to_le64(enable);

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0)
		dev_err(&adapter->netdev->dev,
			"Could not send configure bridge mode request\n");

	adapter->flags ^= QLCNIC_BRIDGE_ENABLED;

	return rv;
}


#define RSS_HASHTYPE_IP_TCP	0x3

int qlcnic_config_rss(struct qlcnic_adapter *adapter, int enable)
{
	struct qlcnic_nic_req req;
	u64 word;
	int i, rv;

	const u64 key[] = { 0xbeac01fa6a42b73bULL, 0x8030f20c77cb2da3ULL,
			0xae7b30b4d0ca2bcbULL, 0x43a38fb04167253dULL,
			0x255b0ec26d5a56daULL };


	memset(&req, 0, sizeof(struct qlcnic_nic_req));
	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_RSS | ((u64)adapter->portnum << 16);
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

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0)
		dev_err(&adapter->netdev->dev, "could not configure RSS\n");

	return rv;
}

int qlcnic_config_ipaddr(struct qlcnic_adapter *adapter, u32 ip, int cmd)
{
	struct qlcnic_nic_req req;
	u64 word;
	int rv;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));
	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_IPADDR | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	req.words[0] = cpu_to_le64(cmd);
	req.words[1] = cpu_to_le64(ip);

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0)
		dev_err(&adapter->netdev->dev,
				"could not notify %s IP 0x%x reuqest\n",
				(cmd == QLCNIC_IP_UP) ? "Add" : "Remove", ip);

	return rv;
}

int qlcnic_linkevent_request(struct qlcnic_adapter *adapter, int enable)
{
	struct qlcnic_nic_req req;
	u64 word;
	int rv;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));
	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_GET_LINKEVENT | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);
	req.words[0] = cpu_to_le64(enable | (enable << 8));

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0)
		dev_err(&adapter->netdev->dev,
				"could not configure link notification\n");

	return rv;
}

int qlcnic_send_lro_cleanup(struct qlcnic_adapter *adapter)
{
	struct qlcnic_nic_req req;
	u64 word;
	int rv;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));
	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_LRO_REQUEST |
		((u64)adapter->portnum << 16) |
		((u64)QLCNIC_LRO_REQUEST_CLEANUP << 56) ;

	req.req_hdr = cpu_to_le64(word);

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0)
		dev_err(&adapter->netdev->dev,
				 "could not cleanup lro flows\n");

	return rv;
}

/*
 * qlcnic_change_mtu - Change the Maximum Transfer Unit
 * @returns 0 on success, negative on failure
 */

int qlcnic_change_mtu(struct net_device *netdev, int mtu)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	int rc = 0;

	if (mtu > P3_MAX_MTU) {
		dev_err(&adapter->netdev->dev, "mtu > %d bytes unsupported\n",
						P3_MAX_MTU);
		return -EINVAL;
	}

	rc = qlcnic_fw_cmd_set_mtu(adapter, mtu);

	if (!rc)
		netdev->mtu = mtu;

	return rc;
}

int qlcnic_get_mac_addr(struct qlcnic_adapter *adapter, u64 *mac)
{
	u32 crbaddr, mac_hi, mac_lo;
	int pci_func = adapter->ahw.pci_func;

	crbaddr = CRB_MAC_BLOCK_START +
		(4 * ((pci_func/2) * 3)) + (4 * (pci_func & 1));

	mac_lo = QLCRD32(adapter, crbaddr);
	mac_hi = QLCRD32(adapter, crbaddr+4);

	if (pci_func & 1)
		*mac = le64_to_cpu((mac_lo >> 16) | ((u64)mac_hi << 16));
	else
		*mac = le64_to_cpu((u64)mac_lo | ((u64)mac_hi << 32));

	return 0;
}

/*
 * Changes the CRB window to the specified window.
 */
 /* Returns < 0 if off is not valid,
 *	 1 if window access is needed. 'off' is set to offset from
 *	   CRB space in 128M pci map
 *	 0 if no window access is needed. 'off' is set to 2M addr
 * In: 'off' is offset from base in 128M pci map
 */
static int
qlcnic_pci_get_crb_addr_2M(struct qlcnic_adapter *adapter,
		ulong off, void __iomem **addr)
{
	const struct crb_128M_2M_sub_block_map *m;

	if ((off >= QLCNIC_CRB_MAX) || (off < QLCNIC_PCI_CRBSPACE))
		return -EINVAL;

	off -= QLCNIC_PCI_CRBSPACE;

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
	*addr = adapter->ahw.pci_base0 + CRB_INDIRECT_2M + (off & MASK(16));
	return 1;
}

/*
 * In: 'off' is offset from CRB space in 128M pci map
 * Out: 'off' is 2M pci map addr
 * side effect: lock crb window
 */
static void
qlcnic_pci_set_crbwindow_2M(struct qlcnic_adapter *adapter, ulong off)
{
	u32 window;
	void __iomem *addr = adapter->ahw.pci_base0 + CRB_WINDOW_2M;

	off -= QLCNIC_PCI_CRBSPACE;

	window = CRB_HI(off);

	if (adapter->ahw.crb_win == window)
		return;

	writel(window, addr);
	if (readl(addr) != window) {
		if (printk_ratelimit())
			dev_warn(&adapter->pdev->dev,
				"failed to set CRB window to %d off 0x%lx\n",
				window, off);
	}
	adapter->ahw.crb_win = window;
}

int
qlcnic_hw_write_wx_2M(struct qlcnic_adapter *adapter, ulong off, u32 data)
{
	unsigned long flags;
	int rv;
	void __iomem *addr = NULL;

	rv = qlcnic_pci_get_crb_addr_2M(adapter, off, &addr);

	if (rv == 0) {
		writel(data, addr);
		return 0;
	}

	if (rv > 0) {
		/* indirect access */
		write_lock_irqsave(&adapter->ahw.crb_lock, flags);
		crb_win_lock(adapter);
		qlcnic_pci_set_crbwindow_2M(adapter, off);
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

u32
qlcnic_hw_read_wx_2M(struct qlcnic_adapter *adapter, ulong off)
{
	unsigned long flags;
	int rv;
	u32 data;
	void __iomem *addr = NULL;

	rv = qlcnic_pci_get_crb_addr_2M(adapter, off, &addr);

	if (rv == 0)
		return readl(addr);

	if (rv > 0) {
		/* indirect access */
		write_lock_irqsave(&adapter->ahw.crb_lock, flags);
		crb_win_lock(adapter);
		qlcnic_pci_set_crbwindow_2M(adapter, off);
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


void __iomem *
qlcnic_get_ioaddr(struct qlcnic_adapter *adapter, u32 offset)
{
	void __iomem *addr = NULL;

	WARN_ON(qlcnic_pci_get_crb_addr_2M(adapter, offset, &addr));

	return addr;
}


static int
qlcnic_pci_set_window_2M(struct qlcnic_adapter *adapter,
		u64 addr, u32 *start)
{
	u32 window;
	struct pci_dev *pdev = adapter->pdev;

	if ((addr & 0x00ff800) == 0xff800) {
		if (printk_ratelimit())
			dev_warn(&pdev->dev, "QM access not handled\n");
		return -EIO;
	}

	window = OCM_WIN_P3P(addr);

	writel(window, adapter->ahw.ocm_win_crb);
	/* read back to flush */
	readl(adapter->ahw.ocm_win_crb);

	adapter->ahw.ocm_win = window;
	*start = QLCNIC_PCI_OCM0_2M + GET_MEM_OFFS_2M(addr);
	return 0;
}

static int
qlcnic_pci_mem_access_direct(struct qlcnic_adapter *adapter, u64 off,
		u64 *data, int op)
{
	void __iomem *addr, *mem_ptr = NULL;
	resource_size_t mem_base;
	int ret;
	u32 start;

	mutex_lock(&adapter->ahw.mem_lock);

	ret = qlcnic_pci_set_window_2M(adapter, off, &start);
	if (ret != 0)
		goto unlock;

	addr = pci_base_offset(adapter, start);
	if (addr)
		goto noremap;

	mem_base = pci_resource_start(adapter->pdev, 0) + (start & PAGE_MASK);

	mem_ptr = ioremap(mem_base, PAGE_SIZE);
	if (mem_ptr == NULL) {
		ret = -EIO;
		goto unlock;
	}

	addr = mem_ptr + (start & (PAGE_SIZE - 1));

noremap:
	if (op == 0)	/* read */
		*data = readq(addr);
	else		/* write */
		writeq(*data, addr);

unlock:
	mutex_unlock(&adapter->ahw.mem_lock);

	if (mem_ptr)
		iounmap(mem_ptr);
	return ret;
}

#define MAX_CTL_CHECK   1000

int
qlcnic_pci_mem_write_2M(struct qlcnic_adapter *adapter,
		u64 off, u64 data)
{
	int i, j, ret;
	u32 temp, off8;
	u64 stride;
	void __iomem *mem_crb;

	/* Only 64-bit aligned access */
	if (off & 7)
		return -EIO;

	/* P3 onward, test agent base for MIU and SIU is same */
	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_QDR_NET,
				QLCNIC_ADDR_QDR_NET_MAX_P3)) {
		mem_crb = qlcnic_get_ioaddr(adapter,
				QLCNIC_CRB_QDR_NET+MIU_TEST_AGT_BASE);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_DDR_NET, QLCNIC_ADDR_DDR_NET_MAX)) {
		mem_crb = qlcnic_get_ioaddr(adapter,
				QLCNIC_CRB_DDR_NET+MIU_TEST_AGT_BASE);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_OCM0, QLCNIC_ADDR_OCM0_MAX))
		return qlcnic_pci_mem_access_direct(adapter, off, &data, 1);

	return -EIO;

correct:
	stride = QLCNIC_IS_REVISION_P3P(adapter->ahw.revision_id) ? 16 : 8;

	off8 = off & ~(stride-1);

	mutex_lock(&adapter->ahw.mem_lock);

	writel(off8, (mem_crb + MIU_TEST_AGT_ADDR_LO));
	writel(0, (mem_crb + MIU_TEST_AGT_ADDR_HI));

	i = 0;
	if (stride == 16) {
		writel(TA_CTL_ENABLE, (mem_crb + TEST_AGT_CTRL));
		writel((TA_CTL_START | TA_CTL_ENABLE),
				(mem_crb + TEST_AGT_CTRL));

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			temp = readl(mem_crb + TEST_AGT_CTRL);
			if ((temp & TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			ret = -EIO;
			goto done;
		}

		i = (off & 0xf) ? 0 : 2;
		writel(readl(mem_crb + MIU_TEST_AGT_RDDATA(i)),
				mem_crb + MIU_TEST_AGT_WRDATA(i));
		writel(readl(mem_crb + MIU_TEST_AGT_RDDATA(i+1)),
				mem_crb + MIU_TEST_AGT_WRDATA(i+1));
		i = (off & 0xf) ? 2 : 0;
	}

	writel(data & 0xffffffff,
			mem_crb + MIU_TEST_AGT_WRDATA(i));
	writel((data >> 32) & 0xffffffff,
			mem_crb + MIU_TEST_AGT_WRDATA(i+1));

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

done:
	mutex_unlock(&adapter->ahw.mem_lock);

	return ret;
}

int
qlcnic_pci_mem_read_2M(struct qlcnic_adapter *adapter,
		u64 off, u64 *data)
{
	int j, ret;
	u32 temp, off8;
	u64 val, stride;
	void __iomem *mem_crb;

	/* Only 64-bit aligned access */
	if (off & 7)
		return -EIO;

	/* P3 onward, test agent base for MIU and SIU is same */
	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_QDR_NET,
				QLCNIC_ADDR_QDR_NET_MAX_P3)) {
		mem_crb = qlcnic_get_ioaddr(adapter,
				QLCNIC_CRB_QDR_NET+MIU_TEST_AGT_BASE);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_DDR_NET, QLCNIC_ADDR_DDR_NET_MAX)) {
		mem_crb = qlcnic_get_ioaddr(adapter,
				QLCNIC_CRB_DDR_NET+MIU_TEST_AGT_BASE);
		goto correct;
	}

	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_OCM0, QLCNIC_ADDR_OCM0_MAX)) {
		return qlcnic_pci_mem_access_direct(adapter,
				off, data, 0);
	}

	return -EIO;

correct:
	stride = QLCNIC_IS_REVISION_P3P(adapter->ahw.revision_id) ? 16 : 8;

	off8 = off & ~(stride-1);

	mutex_lock(&adapter->ahw.mem_lock);

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
		off8 = MIU_TEST_AGT_RDDATA_LO;
		if ((stride == 16) && (off & 0xf))
			off8 = MIU_TEST_AGT_RDDATA_UPPER_LO;

		temp = readl(mem_crb + off8 + 4);
		val = (u64)temp << 32;
		val |= readl(mem_crb + off8);
		*data = val;
		ret = 0;
	}

	mutex_unlock(&adapter->ahw.mem_lock);

	return ret;
}

int qlcnic_get_board_info(struct qlcnic_adapter *adapter)
{
	int offset, board_type, magic;
	struct pci_dev *pdev = adapter->pdev;

	offset = QLCNIC_FW_MAGIC_OFFSET;
	if (qlcnic_rom_fast_read(adapter, offset, &magic))
		return -EIO;

	if (magic != QLCNIC_BDINFO_MAGIC) {
		dev_err(&pdev->dev, "invalid board config, magic=%08x\n",
			magic);
		return -EIO;
	}

	offset = QLCNIC_BRDTYPE_OFFSET;
	if (qlcnic_rom_fast_read(adapter, offset, &board_type))
		return -EIO;

	adapter->ahw.board_type = board_type;

	if (board_type == QLCNIC_BRDTYPE_P3_4_GB_MM) {
		u32 gpio = QLCRD32(adapter, QLCNIC_ROMUSB_GLB_PAD_GPIO_I);
		if ((gpio & 0x8000) == 0)
			board_type = QLCNIC_BRDTYPE_P3_10G_TP;
	}

	switch (board_type) {
	case QLCNIC_BRDTYPE_P3_HMEZ:
	case QLCNIC_BRDTYPE_P3_XG_LOM:
	case QLCNIC_BRDTYPE_P3_10G_CX4:
	case QLCNIC_BRDTYPE_P3_10G_CX4_LP:
	case QLCNIC_BRDTYPE_P3_IMEZ:
	case QLCNIC_BRDTYPE_P3_10G_SFP_PLUS:
	case QLCNIC_BRDTYPE_P3_10G_SFP_CT:
	case QLCNIC_BRDTYPE_P3_10G_SFP_QT:
	case QLCNIC_BRDTYPE_P3_10G_XFP:
	case QLCNIC_BRDTYPE_P3_10000_BASE_T:
		adapter->ahw.port_type = QLCNIC_XGBE;
		break;
	case QLCNIC_BRDTYPE_P3_REF_QG:
	case QLCNIC_BRDTYPE_P3_4_GB:
	case QLCNIC_BRDTYPE_P3_4_GB_MM:
		adapter->ahw.port_type = QLCNIC_GBE;
		break;
	case QLCNIC_BRDTYPE_P3_10G_TP:
		adapter->ahw.port_type = (adapter->portnum < 2) ?
			QLCNIC_XGBE : QLCNIC_GBE;
		break;
	default:
		dev_err(&pdev->dev, "unknown board type %x\n", board_type);
		adapter->ahw.port_type = QLCNIC_XGBE;
		break;
	}

	return 0;
}

int
qlcnic_wol_supported(struct qlcnic_adapter *adapter)
{
	u32 wol_cfg;

	wol_cfg = QLCRD32(adapter, QLCNIC_WOL_CONFIG_NV);
	if (wol_cfg & (1UL << adapter->portnum)) {
		wol_cfg = QLCRD32(adapter, QLCNIC_WOL_CONFIG);
		if (wol_cfg & (1 << adapter->portnum))
			return 1;
	}

	return 0;
}

int qlcnic_config_led(struct qlcnic_adapter *adapter, u32 state, u32 rate)
{
	struct qlcnic_nic_req   req;
	int rv;
	u64 word;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));
	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_LED | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	req.words[0] = cpu_to_le64((u64)rate << 32);
	req.words[1] = cpu_to_le64(state);

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv)
		dev_err(&adapter->pdev->dev, "LED configuration failed.\n");

	return rv;
}

static int qlcnic_set_fw_loopback(struct qlcnic_adapter *adapter, u32 flag)
{
	struct qlcnic_nic_req	req;
	int			rv;
	u64			word;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));
	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_LOOPBACK |
			((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);
	req.words[0] = cpu_to_le64(flag);

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv)
		dev_err(&adapter->pdev->dev,
			"%sting loopback mode failed.\n",
					flag ? "Set" : "Reset");
	return rv;
}

int qlcnic_set_ilb_mode(struct qlcnic_adapter *adapter)
{
	if (qlcnic_set_fw_loopback(adapter, 1))
		return -EIO;

	if (qlcnic_nic_set_promisc(adapter,
				VPORT_MISS_MODE_ACCEPT_ALL)) {
		qlcnic_set_fw_loopback(adapter, 0);
		return -EIO;
	}

	msleep(1000);
	return 0;
}

void qlcnic_clear_ilb_mode(struct qlcnic_adapter *adapter)
{
	int mode = VPORT_MISS_MODE_DROP;
	struct net_device *netdev = adapter->netdev;

	qlcnic_set_fw_loopback(adapter, 0);

	if (netdev->flags & IFF_PROMISC)
		mode = VPORT_MISS_MODE_ACCEPT_ALL;
	else if (netdev->flags & IFF_ALLMULTI)
		mode = VPORT_MISS_MODE_ACCEPT_MULTI;

	qlcnic_nic_set_promisc(adapter, mode);
}
