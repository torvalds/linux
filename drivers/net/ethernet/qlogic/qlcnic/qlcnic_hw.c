// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 */

#include <linux/slab.h>
#include <net/ip.h>
#include <linux/bitops.h>

#include "qlcnic.h"
#include "qlcnic_hdr.h"

#define MASK(n) ((1ULL<<(n))-1)
#define OCM_WIN_P3P(addr) (addr & 0xffc0000)

#define GET_MEM_OFFS_2M(addr) (addr & MASK(18))

#define CRB_BLK(off)	((off >> 20) & 0x3f)
#define CRB_SUBBLK(off)	((off >> 16) & 0xf)
#define CRB_WINDOW_2M	(0x130060)
#define CRB_HI(off)	((crb_hub_agt[CRB_BLK(off)] << 20) | ((off) & 0xf0000))
#define CRB_INDIRECT_2M	(0x1e0000UL)

struct qlcnic_ms_reg_ctrl {
	u32 ocm_window;
	u32 control;
	u32 hi;
	u32 low;
	u32 rd[4];
	u32 wd[4];
	u64 off;
};

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

static struct crb_128M_2M_block_map
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

static void qlcnic_read_window_reg(u32 addr, void __iomem *bar0, u32 *data)
{
	u32 dest;
	void __iomem *val;

	dest = addr & 0xFFFF0000;
	val = bar0 + QLCNIC_FW_DUMP_REG1;
	writel(dest, val);
	readl(val);
	val = bar0 + QLCNIC_FW_DUMP_REG2 + LSW(addr);
	*data = readl(val);
}

static void qlcnic_write_window_reg(u32 addr, void __iomem *bar0, u32 data)
{
	u32 dest;
	void __iomem *val;

	dest = addr & 0xFFFF0000;
	val = bar0 + QLCNIC_FW_DUMP_REG1;
	writel(dest, val);
	readl(val);
	val = bar0 + QLCNIC_FW_DUMP_REG2 + LSW(addr);
	writel(data, val);
	readl(val);
}

int
qlcnic_pcie_sem_lock(struct qlcnic_adapter *adapter, int sem, u32 id_reg)
{
	int timeout = 0, err = 0, done = 0;

	while (!done) {
		done = QLCRD32(adapter, QLCNIC_PCIE_REG(PCIE_SEM_LOCK(sem)),
			       &err);
		if (done == 1)
			break;
		if (++timeout >= QLCNIC_PCIE_SEM_TIMEOUT) {
			if (id_reg) {
				done = QLCRD32(adapter, id_reg, &err);
				if (done != -1)
					dev_err(&adapter->pdev->dev,
						"Failed to acquire sem=%d lock held by=%d\n",
						sem, done);
				else
					dev_err(&adapter->pdev->dev,
						"Failed to acquire sem=%d lock",
						sem);
			} else {
				dev_err(&adapter->pdev->dev,
					"Failed to acquire sem=%d lock", sem);
			}
			return -EIO;
		}
		udelay(1200);
	}

	if (id_reg)
		QLCWR32(adapter, id_reg, adapter->portnum);

	return 0;
}

void
qlcnic_pcie_sem_unlock(struct qlcnic_adapter *adapter, int sem)
{
	int err = 0;

	QLCRD32(adapter, QLCNIC_PCIE_REG(PCIE_SEM_UNLOCK(sem)), &err);
}

int qlcnic_ind_rd(struct qlcnic_adapter *adapter, u32 addr)
{
	int err = 0;
	u32 data;

	if (qlcnic_82xx_check(adapter))
		qlcnic_read_window_reg(addr, adapter->ahw->pci_base0, &data);
	else {
		data = QLCRD32(adapter, addr, &err);
		if (err == -EIO)
			return err;
	}
	return data;
}

int qlcnic_ind_wr(struct qlcnic_adapter *adapter, u32 addr, u32 data)
{
	int ret = 0;

	if (qlcnic_82xx_check(adapter))
		qlcnic_write_window_reg(addr, adapter->ahw->pci_base0, data);
	else
		ret = qlcnic_83xx_wrt_reg_indirect(adapter, addr, data);

	return ret;
}

static int
qlcnic_send_cmd_descs(struct qlcnic_adapter *adapter,
		struct cmd_desc_type0 *cmd_desc_arr, int nr_desc)
{
	u32 i, producer;
	struct qlcnic_cmd_buffer *pbuf;
	struct cmd_desc_type0 *cmd_desc;
	struct qlcnic_host_tx_ring *tx_ring;

	i = 0;

	if (!test_bit(__QLCNIC_FW_ATTACHED, &adapter->state))
		return -EIO;

	tx_ring = &adapter->tx_ring[0];
	__netif_tx_lock_bh(tx_ring->txq);

	producer = tx_ring->producer;

	if (nr_desc >= qlcnic_tx_avail(tx_ring)) {
		netif_tx_stop_queue(tx_ring->txq);
		smp_mb();
		if (qlcnic_tx_avail(tx_ring) > nr_desc) {
			if (qlcnic_tx_avail(tx_ring) > TX_STOP_THRESH)
				netif_tx_wake_queue(tx_ring->txq);
		} else {
			adapter->stats.xmit_off++;
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
		       cmd_desc, sizeof(struct cmd_desc_type0));

		producer = get_next_index(producer, tx_ring->num_desc);
		i++;

	} while (i != nr_desc);

	tx_ring->producer = producer;

	qlcnic_update_cmd_producer(tx_ring);

	__netif_tx_unlock_bh(tx_ring->txq);

	return 0;
}

int qlcnic_82xx_sre_macaddr_change(struct qlcnic_adapter *adapter, u8 *addr,
				   u16 vlan_id, u8 op)
{
	struct qlcnic_nic_req req;
	struct qlcnic_mac_req *mac_req;
	struct qlcnic_vlan_req *vlan_req;
	u64 word;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));
	req.qhdr = cpu_to_le64(QLCNIC_REQUEST << 23);

	word = QLCNIC_MAC_EVENT | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	mac_req = (struct qlcnic_mac_req *)&req.words[0];
	mac_req->op = op;
	memcpy(mac_req->mac_addr, addr, ETH_ALEN);

	vlan_req = (struct qlcnic_vlan_req *)&req.words[1];
	vlan_req->vlan_id = cpu_to_le16(vlan_id);

	return qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
}

int qlcnic_nic_del_mac(struct qlcnic_adapter *adapter, const u8 *addr)
{
	struct qlcnic_mac_vlan_list *cur;
	int err = -EINVAL;

	/* Delete MAC from the existing list */
	list_for_each_entry(cur, &adapter->mac_list, list) {
		if (ether_addr_equal(addr, cur->mac_addr)) {
			err = qlcnic_sre_macaddr_change(adapter, cur->mac_addr,
							0, QLCNIC_MAC_DEL);
			if (err)
				return err;
			list_del(&cur->list);
			kfree(cur);
			return err;
		}
	}
	return err;
}

int qlcnic_nic_add_mac(struct qlcnic_adapter *adapter, const u8 *addr, u16 vlan,
		       enum qlcnic_mac_type mac_type)
{
	struct qlcnic_mac_vlan_list *cur;

	/* look up if already exists */
	list_for_each_entry(cur, &adapter->mac_list, list) {
		if (ether_addr_equal(addr, cur->mac_addr) &&
		    cur->vlan_id == vlan)
			return 0;
	}

	cur = kzalloc(sizeof(*cur), GFP_ATOMIC);
	if (cur == NULL)
		return -ENOMEM;

	memcpy(cur->mac_addr, addr, ETH_ALEN);

	if (qlcnic_sre_macaddr_change(adapter,
				cur->mac_addr, vlan, QLCNIC_MAC_ADD)) {
		kfree(cur);
		return -EIO;
	}

	cur->vlan_id = vlan;
	cur->mac_type = mac_type;

	list_add_tail(&cur->list, &adapter->mac_list);
	return 0;
}

void qlcnic_flush_mcast_mac(struct qlcnic_adapter *adapter)
{
	struct qlcnic_mac_vlan_list *cur;
	struct list_head *head, *tmp;

	list_for_each_safe(head, tmp, &adapter->mac_list) {
		cur = list_entry(head, struct qlcnic_mac_vlan_list, list);
		if (cur->mac_type != QLCNIC_MULTICAST_MAC)
			continue;

		qlcnic_sre_macaddr_change(adapter, cur->mac_addr,
					  cur->vlan_id, QLCNIC_MAC_DEL);
		list_del(&cur->list);
		kfree(cur);
	}
}

static void __qlcnic_set_multi(struct net_device *netdev, u16 vlan)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct netdev_hw_addr *ha;
	static const u8 bcast_addr[ETH_ALEN] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};
	u32 mode = VPORT_MISS_MODE_DROP;

	if (!test_bit(__QLCNIC_FW_ATTACHED, &adapter->state))
		return;

	qlcnic_nic_add_mac(adapter, adapter->mac_addr, vlan,
			   QLCNIC_UNICAST_MAC);
	qlcnic_nic_add_mac(adapter, bcast_addr, vlan, QLCNIC_BROADCAST_MAC);

	if (netdev->flags & IFF_PROMISC) {
		if (!(adapter->flags & QLCNIC_PROMISC_DISABLED))
			mode = VPORT_MISS_MODE_ACCEPT_ALL;
	} else if ((netdev->flags & IFF_ALLMULTI) ||
		   (netdev_mc_count(netdev) > ahw->max_mc_count)) {
		mode = VPORT_MISS_MODE_ACCEPT_MULTI;
	} else if (!netdev_mc_empty(netdev)) {
		qlcnic_flush_mcast_mac(adapter);
		netdev_for_each_mc_addr(ha, netdev)
			qlcnic_nic_add_mac(adapter, ha->addr, vlan,
					   QLCNIC_MULTICAST_MAC);
	}

	/* configure unicast MAC address, if there is not sufficient space
	 * to store all the unicast addresses then enable promiscuous mode
	 */
	if (netdev_uc_count(netdev) > ahw->max_uc_count) {
		mode = VPORT_MISS_MODE_ACCEPT_ALL;
	} else if (!netdev_uc_empty(netdev)) {
		netdev_for_each_uc_addr(ha, netdev)
			qlcnic_nic_add_mac(adapter, ha->addr, vlan,
					   QLCNIC_UNICAST_MAC);
	}

	if (mode == VPORT_MISS_MODE_ACCEPT_ALL &&
	    !adapter->fdb_mac_learn) {
		qlcnic_alloc_lb_filters_mem(adapter);
		adapter->drv_mac_learn = 1;
		if (adapter->flags & QLCNIC_ESWITCH_ENABLED)
			adapter->rx_mac_learn = true;
	} else {
		adapter->drv_mac_learn = 0;
		adapter->rx_mac_learn = false;
	}

	qlcnic_nic_set_promisc(adapter, mode);
}

void qlcnic_set_multi(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);

	if (!test_bit(__QLCNIC_FW_ATTACHED, &adapter->state))
		return;

	if (qlcnic_sriov_vf_check(adapter))
		qlcnic_sriov_vf_set_multi(netdev);
	else
		__qlcnic_set_multi(netdev, 0);
}

int qlcnic_82xx_nic_set_promisc(struct qlcnic_adapter *adapter, u32 mode)
{
	struct qlcnic_nic_req req;
	u64 word;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));

	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_SET_MAC_RECEIVE_MODE |
			((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	req.words[0] = cpu_to_le64(mode);

	return qlcnic_send_cmd_descs(adapter,
				(struct cmd_desc_type0 *)&req, 1);
}

void qlcnic_82xx_free_mac_list(struct qlcnic_adapter *adapter)
{
	struct list_head *head = &adapter->mac_list;
	struct qlcnic_mac_vlan_list *cur;

	while (!list_empty(head)) {
		cur = list_entry(head->next, struct qlcnic_mac_vlan_list, list);
		qlcnic_sre_macaddr_change(adapter,
				cur->mac_addr, 0, QLCNIC_MAC_DEL);
		list_del(&cur->list);
		kfree(cur);
	}
}

void qlcnic_prune_lb_filters(struct qlcnic_adapter *adapter)
{
	struct qlcnic_filter *tmp_fil;
	struct hlist_node *n;
	struct hlist_head *head;
	int i;
	unsigned long expires;
	u8 cmd;

	for (i = 0; i < adapter->fhash.fbucket_size; i++) {
		head = &(adapter->fhash.fhead[i]);
		hlist_for_each_entry_safe(tmp_fil, n, head, fnode) {
			cmd =  tmp_fil->vlan_id ? QLCNIC_MAC_VLAN_DEL :
						  QLCNIC_MAC_DEL;
			expires = tmp_fil->ftime + QLCNIC_FILTER_AGE * HZ;
			if (time_before(expires, jiffies)) {
				qlcnic_sre_macaddr_change(adapter,
							  tmp_fil->faddr,
							  tmp_fil->vlan_id,
							  cmd);
				spin_lock_bh(&adapter->mac_learn_lock);
				adapter->fhash.fnum--;
				hlist_del(&tmp_fil->fnode);
				spin_unlock_bh(&adapter->mac_learn_lock);
				kfree(tmp_fil);
			}
		}
	}
	for (i = 0; i < adapter->rx_fhash.fbucket_size; i++) {
		head = &(adapter->rx_fhash.fhead[i]);

		hlist_for_each_entry_safe(tmp_fil, n, head, fnode)
		{
			expires = tmp_fil->ftime + QLCNIC_FILTER_AGE * HZ;
			if (time_before(expires, jiffies)) {
				spin_lock_bh(&adapter->rx_mac_learn_lock);
				adapter->rx_fhash.fnum--;
				hlist_del(&tmp_fil->fnode);
				spin_unlock_bh(&adapter->rx_mac_learn_lock);
				kfree(tmp_fil);
			}
		}
	}
}

void qlcnic_delete_lb_filters(struct qlcnic_adapter *adapter)
{
	struct qlcnic_filter *tmp_fil;
	struct hlist_node *n;
	struct hlist_head *head;
	int i;
	u8 cmd;

	for (i = 0; i < adapter->fhash.fbucket_size; i++) {
		head = &(adapter->fhash.fhead[i]);
		hlist_for_each_entry_safe(tmp_fil, n, head, fnode) {
			cmd =  tmp_fil->vlan_id ? QLCNIC_MAC_VLAN_DEL :
						  QLCNIC_MAC_DEL;
			qlcnic_sre_macaddr_change(adapter,
						  tmp_fil->faddr,
						  tmp_fil->vlan_id,
						  cmd);
			spin_lock_bh(&adapter->mac_learn_lock);
			adapter->fhash.fnum--;
			hlist_del(&tmp_fil->fnode);
			spin_unlock_bh(&adapter->mac_learn_lock);
			kfree(tmp_fil);
		}
	}
}

static int qlcnic_set_fw_loopback(struct qlcnic_adapter *adapter, u8 flag)
{
	struct qlcnic_nic_req req;
	int rv;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));

	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);
	req.req_hdr = cpu_to_le64(QLCNIC_H2C_OPCODE_CONFIG_LOOPBACK |
		((u64) adapter->portnum << 16) | ((u64) 0x1 << 32));

	req.words[0] = cpu_to_le64(flag);

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0)
		dev_err(&adapter->pdev->dev, "%sting loopback mode failed\n",
				flag ? "Set" : "Reset");
	return rv;
}

int qlcnic_82xx_set_lb_mode(struct qlcnic_adapter *adapter, u8 mode)
{
	if (qlcnic_set_fw_loopback(adapter, mode))
		return -EIO;

	if (qlcnic_nic_set_promisc(adapter,
				   VPORT_MISS_MODE_ACCEPT_ALL)) {
		qlcnic_set_fw_loopback(adapter, 0);
		return -EIO;
	}

	msleep(1000);
	return 0;
}

int qlcnic_82xx_clear_lb_mode(struct qlcnic_adapter *adapter, u8 mode)
{
	struct net_device *netdev = adapter->netdev;

	mode = VPORT_MISS_MODE_DROP;
	qlcnic_set_fw_loopback(adapter, 0);

	if (netdev->flags & IFF_PROMISC)
		mode = VPORT_MISS_MODE_ACCEPT_ALL;
	else if (netdev->flags & IFF_ALLMULTI)
		mode = VPORT_MISS_MODE_ACCEPT_MULTI;

	qlcnic_nic_set_promisc(adapter, mode);
	msleep(1000);
	return 0;
}

int qlcnic_82xx_read_phys_port_id(struct qlcnic_adapter *adapter)
{
	u8 mac[ETH_ALEN];
	int ret;

	ret = qlcnic_get_mac_address(adapter, mac,
				     adapter->ahw->physical_port);
	if (ret)
		return ret;

	memcpy(adapter->ahw->phys_port_id, mac, ETH_ALEN);
	adapter->flags |= QLCNIC_HAS_PHYS_PORT_ID;

	return 0;
}

int qlcnic_82xx_set_rx_coalesce(struct qlcnic_adapter *adapter)
{
	struct qlcnic_nic_req req;
	int rv;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));

	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	req.req_hdr = cpu_to_le64(QLCNIC_CONFIG_INTR_COALESCE |
		((u64) adapter->portnum << 16));

	req.words[0] = cpu_to_le64(((u64) adapter->ahw->coal.flag) << 32);
	req.words[2] = cpu_to_le64(adapter->ahw->coal.rx_packets |
			((u64) adapter->ahw->coal.rx_time_us) << 16);
	req.words[5] = cpu_to_le64(adapter->ahw->coal.timer_out |
			((u64) adapter->ahw->coal.type) << 32 |
			((u64) adapter->ahw->coal.sts_ring_mask) << 40);
	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0)
		dev_err(&adapter->netdev->dev,
			"Could not send interrupt coalescing parameters\n");

	return rv;
}

/* Send the interrupt coalescing parameter set by ethtool to the card. */
int qlcnic_82xx_config_intr_coalesce(struct qlcnic_adapter *adapter,
				     struct ethtool_coalesce *ethcoal)
{
	struct qlcnic_nic_intr_coalesce *coal = &adapter->ahw->coal;
	int rv;

	coal->flag = QLCNIC_INTR_DEFAULT;
	coal->rx_time_us = ethcoal->rx_coalesce_usecs;
	coal->rx_packets = ethcoal->rx_max_coalesced_frames;

	rv = qlcnic_82xx_set_rx_coalesce(adapter);

	if (rv)
		netdev_err(adapter->netdev,
			   "Failed to set Rx coalescing parameters\n");

	return rv;
}

#define QLCNIC_ENABLE_IPV4_LRO		BIT_0
#define QLCNIC_ENABLE_IPV6_LRO		(BIT_1 | BIT_9)

int qlcnic_82xx_config_hw_lro(struct qlcnic_adapter *adapter, int enable)
{
	struct qlcnic_nic_req req;
	u64 word;
	int rv;

	if (!test_bit(__QLCNIC_FW_ATTACHED, &adapter->state))
		return 0;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));

	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_HW_LRO | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	word = 0;
	if (enable) {
		word = QLCNIC_ENABLE_IPV4_LRO;
		if (adapter->ahw->extra_capability[0] &
		    QLCNIC_FW_CAP2_HW_LRO_IPV6)
			word |= QLCNIC_ENABLE_IPV6_LRO;
	}

	req.words[0] = cpu_to_le64(word);

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0)
		dev_err(&adapter->netdev->dev,
			"Could not send configure hw lro request\n");

	return rv;
}

int qlcnic_config_bridged_mode(struct qlcnic_adapter *adapter, u32 enable)
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


#define QLCNIC_RSS_HASHTYPE_IP_TCP	0x3
#define QLCNIC_ENABLE_TYPE_C_RSS	BIT_10
#define QLCNIC_RSS_FEATURE_FLAG	(1ULL << 63)
#define QLCNIC_RSS_IND_TABLE_MASK	0x7ULL

int qlcnic_82xx_config_rss(struct qlcnic_adapter *adapter, int enable)
{
	struct qlcnic_nic_req req;
	u64 word;
	int i, rv;

	static const u64 key[] = {
		0xbeac01fa6a42b73bULL, 0x8030f20c77cb2da3ULL,
		0xae7b30b4d0ca2bcbULL, 0x43a38fb04167253dULL,
		0x255b0ec26d5a56daULL
	};

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
	 *       10: type-c rss
	 *	 11: udp rss
	 *    47-12: reserved
	 *    62-48: indirection table mask
	 *	 63: feature flag
	 */
	word =  ((u64)(QLCNIC_RSS_HASHTYPE_IP_TCP & 0x3) << 4) |
		((u64)(QLCNIC_RSS_HASHTYPE_IP_TCP & 0x3) << 6) |
		((u64)(enable & 0x1) << 8) |
		((u64)QLCNIC_RSS_IND_TABLE_MASK << 48) |
		(u64)QLCNIC_ENABLE_TYPE_C_RSS |
		(u64)QLCNIC_RSS_FEATURE_FLAG;

	req.words[0] = cpu_to_le64(word);
	for (i = 0; i < 5; i++)
		req.words[i+1] = cpu_to_le64(key[i]);

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0)
		dev_err(&adapter->netdev->dev, "could not configure RSS\n");

	return rv;
}

void qlcnic_82xx_config_ipaddr(struct qlcnic_adapter *adapter,
			       __be32 ip, int cmd)
{
	struct qlcnic_nic_req req;
	struct qlcnic_ipaddr *ipa;
	u64 word;
	int rv;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));
	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_IPADDR | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	req.words[0] = cpu_to_le64(cmd);
	ipa = (struct qlcnic_ipaddr *)&req.words[1];
	ipa->ipv4 = ip;

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv != 0)
		dev_err(&adapter->netdev->dev,
				"could not notify %s IP 0x%x request\n",
				(cmd == QLCNIC_IP_UP) ? "Add" : "Remove", ip);
}

int qlcnic_82xx_linkevent_request(struct qlcnic_adapter *adapter, int enable)
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

static int qlcnic_send_lro_cleanup(struct qlcnic_adapter *adapter)
{
	struct qlcnic_nic_req req;
	u64 word;
	int rv;

	if (!test_bit(__QLCNIC_FW_ATTACHED, &adapter->state))
		return 0;

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

	rc = qlcnic_fw_cmd_set_mtu(adapter, mtu);

	if (!rc)
		netdev->mtu = mtu;

	return rc;
}

static netdev_features_t qlcnic_process_flags(struct qlcnic_adapter *adapter,
					      netdev_features_t features)
{
	u32 offload_flags = adapter->offload_flags;

	if (offload_flags & BIT_0) {
		features |= NETIF_F_RXCSUM | NETIF_F_IP_CSUM |
			    NETIF_F_IPV6_CSUM;
		adapter->rx_csum = 1;
		if (QLCNIC_IS_TSO_CAPABLE(adapter)) {
			if (!(offload_flags & BIT_1))
				features &= ~NETIF_F_TSO;
			else
				features |= NETIF_F_TSO;

			if (!(offload_flags & BIT_2))
				features &= ~NETIF_F_TSO6;
			else
				features |= NETIF_F_TSO6;
		}
	} else {
		features &= ~(NETIF_F_RXCSUM |
			      NETIF_F_IP_CSUM |
			      NETIF_F_IPV6_CSUM);

		if (QLCNIC_IS_TSO_CAPABLE(adapter))
			features &= ~(NETIF_F_TSO | NETIF_F_TSO6);
		adapter->rx_csum = 0;
	}

	return features;
}

netdev_features_t qlcnic_fix_features(struct net_device *netdev,
	netdev_features_t features)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	netdev_features_t changed;

	if (qlcnic_82xx_check(adapter) &&
	    (adapter->flags & QLCNIC_ESWITCH_ENABLED)) {
		if (adapter->flags & QLCNIC_APP_CHANGED_FLAGS) {
			features = qlcnic_process_flags(adapter, features);
		} else {
			changed = features ^ netdev->features;
			features ^= changed & (NETIF_F_RXCSUM |
					       NETIF_F_IP_CSUM |
					       NETIF_F_IPV6_CSUM |
					       NETIF_F_TSO |
					       NETIF_F_TSO6);
		}
	}

	if (!(features & NETIF_F_RXCSUM))
		features &= ~NETIF_F_LRO;

	return features;
}


int qlcnic_set_features(struct net_device *netdev, netdev_features_t features)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	netdev_features_t changed = netdev->features ^ features;
	int hw_lro = (features & NETIF_F_LRO) ? QLCNIC_LRO_ENABLED : 0;

	if (!(changed & NETIF_F_LRO))
		return 0;

	netdev->features ^= NETIF_F_LRO;

	if (qlcnic_config_hw_lro(adapter, hw_lro))
		return -EIO;

	if (!hw_lro && qlcnic_82xx_check(adapter)) {
		if (qlcnic_send_lro_cleanup(adapter))
			return -EIO;
	}

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
static int qlcnic_pci_get_crb_addr_2M(struct qlcnic_hardware_context *ahw,
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
		*addr = ahw->pci_base0 + m->start_2M +
			(off - m->start_128M);
		return 0;
	}

	/*
	 * Not in direct map, use crb window
	 */
	*addr = ahw->pci_base0 + CRB_INDIRECT_2M + (off & MASK(16));
	return 1;
}

/*
 * In: 'off' is offset from CRB space in 128M pci map
 * Out: 'off' is 2M pci map addr
 * side effect: lock crb window
 */
static int
qlcnic_pci_set_crbwindow_2M(struct qlcnic_adapter *adapter, ulong off)
{
	u32 window;
	void __iomem *addr = adapter->ahw->pci_base0 + CRB_WINDOW_2M;

	off -= QLCNIC_PCI_CRBSPACE;

	window = CRB_HI(off);
	if (window == 0) {
		dev_err(&adapter->pdev->dev, "Invalid offset 0x%lx\n", off);
		return -EIO;
	}

	writel(window, addr);
	if (readl(addr) != window) {
		if (printk_ratelimit())
			dev_warn(&adapter->pdev->dev,
				"failed to set CRB window to %d off 0x%lx\n",
				window, off);
		return -EIO;
	}
	return 0;
}

int qlcnic_82xx_hw_write_wx_2M(struct qlcnic_adapter *adapter, ulong off,
			       u32 data)
{
	unsigned long flags;
	int rv;
	void __iomem *addr = NULL;

	rv = qlcnic_pci_get_crb_addr_2M(adapter->ahw, off, &addr);

	if (rv == 0) {
		writel(data, addr);
		return 0;
	}

	if (rv > 0) {
		/* indirect access */
		write_lock_irqsave(&adapter->ahw->crb_lock, flags);
		crb_win_lock(adapter);
		rv = qlcnic_pci_set_crbwindow_2M(adapter, off);
		if (!rv)
			writel(data, addr);
		crb_win_unlock(adapter);
		write_unlock_irqrestore(&adapter->ahw->crb_lock, flags);
		return rv;
	}

	dev_err(&adapter->pdev->dev,
			"%s: invalid offset: 0x%016lx\n", __func__, off);
	dump_stack();
	return -EIO;
}

int qlcnic_82xx_hw_read_wx_2M(struct qlcnic_adapter *adapter, ulong off,
			      int *err)
{
	unsigned long flags;
	int rv;
	u32 data = -1;
	void __iomem *addr = NULL;

	rv = qlcnic_pci_get_crb_addr_2M(adapter->ahw, off, &addr);

	if (rv == 0)
		return readl(addr);

	if (rv > 0) {
		/* indirect access */
		write_lock_irqsave(&adapter->ahw->crb_lock, flags);
		crb_win_lock(adapter);
		if (!qlcnic_pci_set_crbwindow_2M(adapter, off))
			data = readl(addr);
		crb_win_unlock(adapter);
		write_unlock_irqrestore(&adapter->ahw->crb_lock, flags);
		return data;
	}

	dev_err(&adapter->pdev->dev,
			"%s: invalid offset: 0x%016lx\n", __func__, off);
	dump_stack();
	return -1;
}

void __iomem *qlcnic_get_ioaddr(struct qlcnic_hardware_context *ahw,
				u32 offset)
{
	void __iomem *addr = NULL;

	WARN_ON(qlcnic_pci_get_crb_addr_2M(ahw, offset, &addr));

	return addr;
}

static int qlcnic_pci_mem_access_direct(struct qlcnic_adapter *adapter,
					u32 window, u64 off, u64 *data, int op)
{
	void __iomem *addr;
	u32 start;

	mutex_lock(&adapter->ahw->mem_lock);

	writel(window, adapter->ahw->ocm_win_crb);
	/* read back to flush */
	readl(adapter->ahw->ocm_win_crb);
	start = QLCNIC_PCI_OCM0_2M + off;

	addr = adapter->ahw->pci_base0 + start;

	if (op == 0)	/* read */
		*data = readq(addr);
	else		/* write */
		writeq(*data, addr);

	/* Set window to 0 */
	writel(0, adapter->ahw->ocm_win_crb);
	readl(adapter->ahw->ocm_win_crb);

	mutex_unlock(&adapter->ahw->mem_lock);
	return 0;
}

static void
qlcnic_pci_camqm_read_2M(struct qlcnic_adapter *adapter, u64 off, u64 *data)
{
	void __iomem *addr = adapter->ahw->pci_base0 +
		QLCNIC_PCI_CAMQM_2M_BASE + (off - QLCNIC_PCI_CAMQM);

	mutex_lock(&adapter->ahw->mem_lock);
	*data = readq(addr);
	mutex_unlock(&adapter->ahw->mem_lock);
}

static void
qlcnic_pci_camqm_write_2M(struct qlcnic_adapter *adapter, u64 off, u64 data)
{
	void __iomem *addr = adapter->ahw->pci_base0 +
		QLCNIC_PCI_CAMQM_2M_BASE + (off - QLCNIC_PCI_CAMQM);

	mutex_lock(&adapter->ahw->mem_lock);
	writeq(data, addr);
	mutex_unlock(&adapter->ahw->mem_lock);
}



/* Set MS memory control data for different adapters */
static void qlcnic_set_ms_controls(struct qlcnic_adapter *adapter, u64 off,
				   struct qlcnic_ms_reg_ctrl *ms)
{
	ms->control = QLCNIC_MS_CTRL;
	ms->low = QLCNIC_MS_ADDR_LO;
	ms->hi = QLCNIC_MS_ADDR_HI;
	if (off & 0xf) {
		ms->wd[0] = QLCNIC_MS_WRTDATA_LO;
		ms->rd[0] = QLCNIC_MS_RDDATA_LO;
		ms->wd[1] = QLCNIC_MS_WRTDATA_HI;
		ms->rd[1] = QLCNIC_MS_RDDATA_HI;
		ms->wd[2] = QLCNIC_MS_WRTDATA_ULO;
		ms->wd[3] = QLCNIC_MS_WRTDATA_UHI;
		ms->rd[2] = QLCNIC_MS_RDDATA_ULO;
		ms->rd[3] = QLCNIC_MS_RDDATA_UHI;
	} else {
		ms->wd[0] = QLCNIC_MS_WRTDATA_ULO;
		ms->rd[0] = QLCNIC_MS_RDDATA_ULO;
		ms->wd[1] = QLCNIC_MS_WRTDATA_UHI;
		ms->rd[1] = QLCNIC_MS_RDDATA_UHI;
		ms->wd[2] = QLCNIC_MS_WRTDATA_LO;
		ms->wd[3] = QLCNIC_MS_WRTDATA_HI;
		ms->rd[2] = QLCNIC_MS_RDDATA_LO;
		ms->rd[3] = QLCNIC_MS_RDDATA_HI;
	}

	ms->ocm_window = OCM_WIN_P3P(off);
	ms->off = GET_MEM_OFFS_2M(off);
}

int qlcnic_pci_mem_write_2M(struct qlcnic_adapter *adapter, u64 off, u64 data)
{
	int j, ret = 0;
	u32 temp, off8;
	struct qlcnic_ms_reg_ctrl ms;

	/* Only 64-bit aligned access */
	if (off & 7)
		return -EIO;

	memset(&ms, 0, sizeof(struct qlcnic_ms_reg_ctrl));
	if (!(ADDR_IN_RANGE(off, QLCNIC_ADDR_QDR_NET,
			    QLCNIC_ADDR_QDR_NET_MAX) ||
	      ADDR_IN_RANGE(off, QLCNIC_ADDR_DDR_NET,
			    QLCNIC_ADDR_DDR_NET_MAX)))
		return -EIO;

	qlcnic_set_ms_controls(adapter, off, &ms);

	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_OCM0, QLCNIC_ADDR_OCM0_MAX))
		return qlcnic_pci_mem_access_direct(adapter, ms.ocm_window,
						    ms.off, &data, 1);

	off8 = off & ~0xf;

	mutex_lock(&adapter->ahw->mem_lock);

	qlcnic_ind_wr(adapter, ms.low, off8);
	qlcnic_ind_wr(adapter, ms.hi, 0);

	qlcnic_ind_wr(adapter, ms.control, TA_CTL_ENABLE);
	qlcnic_ind_wr(adapter, ms.control, QLCNIC_TA_START_ENABLE);

	for (j = 0; j < MAX_CTL_CHECK; j++) {
		temp = qlcnic_ind_rd(adapter, ms.control);
		if ((temp & TA_CTL_BUSY) == 0)
			break;
	}

	if (j >= MAX_CTL_CHECK) {
		ret = -EIO;
		goto done;
	}

	/* This is the modify part of read-modify-write */
	qlcnic_ind_wr(adapter, ms.wd[0], qlcnic_ind_rd(adapter, ms.rd[0]));
	qlcnic_ind_wr(adapter, ms.wd[1], qlcnic_ind_rd(adapter, ms.rd[1]));
	/* This is the write part of read-modify-write */
	qlcnic_ind_wr(adapter, ms.wd[2], data & 0xffffffff);
	qlcnic_ind_wr(adapter, ms.wd[3], (data >> 32) & 0xffffffff);

	qlcnic_ind_wr(adapter, ms.control, QLCNIC_TA_WRITE_ENABLE);
	qlcnic_ind_wr(adapter, ms.control, QLCNIC_TA_WRITE_START);

	for (j = 0; j < MAX_CTL_CHECK; j++) {
		temp = qlcnic_ind_rd(adapter, ms.control);
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
	mutex_unlock(&adapter->ahw->mem_lock);

	return ret;
}

int qlcnic_pci_mem_read_2M(struct qlcnic_adapter *adapter, u64 off, u64 *data)
{
	int j, ret;
	u32 temp, off8;
	u64 val;
	struct qlcnic_ms_reg_ctrl ms;

	/* Only 64-bit aligned access */
	if (off & 7)
		return -EIO;
	if (!(ADDR_IN_RANGE(off, QLCNIC_ADDR_QDR_NET,
			    QLCNIC_ADDR_QDR_NET_MAX) ||
	      ADDR_IN_RANGE(off, QLCNIC_ADDR_DDR_NET,
			    QLCNIC_ADDR_DDR_NET_MAX)))
		return -EIO;

	memset(&ms, 0, sizeof(struct qlcnic_ms_reg_ctrl));
	qlcnic_set_ms_controls(adapter, off, &ms);

	if (ADDR_IN_RANGE(off, QLCNIC_ADDR_OCM0, QLCNIC_ADDR_OCM0_MAX))
		return qlcnic_pci_mem_access_direct(adapter, ms.ocm_window,
						    ms.off, data, 0);

	mutex_lock(&adapter->ahw->mem_lock);

	off8 = off & ~0xf;

	qlcnic_ind_wr(adapter, ms.low, off8);
	qlcnic_ind_wr(adapter, ms.hi, 0);

	qlcnic_ind_wr(adapter, ms.control, TA_CTL_ENABLE);
	qlcnic_ind_wr(adapter, ms.control, QLCNIC_TA_START_ENABLE);

	for (j = 0; j < MAX_CTL_CHECK; j++) {
		temp = qlcnic_ind_rd(adapter, ms.control);
		if ((temp & TA_CTL_BUSY) == 0)
			break;
	}

	if (j >= MAX_CTL_CHECK) {
		if (printk_ratelimit())
			dev_err(&adapter->pdev->dev,
					"failed to read through agent\n");
		ret = -EIO;
	} else {

		temp = qlcnic_ind_rd(adapter, ms.rd[3]);
		val = (u64)temp << 32;
		val |= qlcnic_ind_rd(adapter, ms.rd[2]);
		*data = val;
		ret = 0;
	}

	mutex_unlock(&adapter->ahw->mem_lock);

	return ret;
}

int qlcnic_82xx_get_board_info(struct qlcnic_adapter *adapter)
{
	int offset, board_type, magic, err = 0;
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

	adapter->ahw->board_type = board_type;

	if (board_type == QLCNIC_BRDTYPE_P3P_4_GB_MM) {
		u32 gpio = QLCRD32(adapter, QLCNIC_ROMUSB_GLB_PAD_GPIO_I, &err);
		if (err == -EIO)
			return err;
		if ((gpio & 0x8000) == 0)
			board_type = QLCNIC_BRDTYPE_P3P_10G_TP;
	}

	switch (board_type) {
	case QLCNIC_BRDTYPE_P3P_HMEZ:
	case QLCNIC_BRDTYPE_P3P_XG_LOM:
	case QLCNIC_BRDTYPE_P3P_10G_CX4:
	case QLCNIC_BRDTYPE_P3P_10G_CX4_LP:
	case QLCNIC_BRDTYPE_P3P_IMEZ:
	case QLCNIC_BRDTYPE_P3P_10G_SFP_PLUS:
	case QLCNIC_BRDTYPE_P3P_10G_SFP_CT:
	case QLCNIC_BRDTYPE_P3P_10G_SFP_QT:
	case QLCNIC_BRDTYPE_P3P_10G_XFP:
	case QLCNIC_BRDTYPE_P3P_10000_BASE_T:
		adapter->ahw->port_type = QLCNIC_XGBE;
		break;
	case QLCNIC_BRDTYPE_P3P_REF_QG:
	case QLCNIC_BRDTYPE_P3P_4_GB:
	case QLCNIC_BRDTYPE_P3P_4_GB_MM:
		adapter->ahw->port_type = QLCNIC_GBE;
		break;
	case QLCNIC_BRDTYPE_P3P_10G_TP:
		adapter->ahw->port_type = (adapter->portnum < 2) ?
			QLCNIC_XGBE : QLCNIC_GBE;
		break;
	default:
		dev_err(&pdev->dev, "unknown board type %x\n", board_type);
		adapter->ahw->port_type = QLCNIC_XGBE;
		break;
	}

	return 0;
}

static int
qlcnic_wol_supported(struct qlcnic_adapter *adapter)
{
	u32 wol_cfg;
	int err = 0;

	wol_cfg = QLCRD32(adapter, QLCNIC_WOL_CONFIG_NV, &err);
	if (wol_cfg & (1UL << adapter->portnum)) {
		wol_cfg = QLCRD32(adapter, QLCNIC_WOL_CONFIG, &err);
		if (err == -EIO)
			return err;
		if (wol_cfg & (1 << adapter->portnum))
			return 1;
	}

	return 0;
}

int qlcnic_82xx_config_led(struct qlcnic_adapter *adapter, u32 state, u32 rate)
{
	struct qlcnic_nic_req   req;
	int rv;
	u64 word;

	memset(&req, 0, sizeof(struct qlcnic_nic_req));
	req.qhdr = cpu_to_le64(QLCNIC_HOST_REQUEST << 23);

	word = QLCNIC_H2C_OPCODE_CONFIG_LED | ((u64)adapter->portnum << 16);
	req.req_hdr = cpu_to_le64(word);

	req.words[0] = cpu_to_le64(((u64)rate << 32) | adapter->portnum);
	req.words[1] = cpu_to_le64(state);

	rv = qlcnic_send_cmd_descs(adapter, (struct cmd_desc_type0 *)&req, 1);
	if (rv)
		dev_err(&adapter->pdev->dev, "LED configuration failed.\n");

	return rv;
}

void qlcnic_82xx_get_beacon_state(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_cmd_args cmd;
	u8 beacon_state;
	int err = 0;

	if (ahw->extra_capability[0] & QLCNIC_FW_CAPABILITY_2_BEACON) {
		err = qlcnic_alloc_mbx_args(&cmd, adapter,
					    QLCNIC_CMD_GET_LED_STATUS);
		if (!err) {
			err = qlcnic_issue_cmd(adapter, &cmd);
			if (err) {
				netdev_err(adapter->netdev,
					   "Failed to get current beacon state, err=%d\n",
					   err);
			} else {
				beacon_state = cmd.rsp.arg[1];
				if (beacon_state == QLCNIC_BEACON_DISABLE)
					ahw->beacon_state = QLCNIC_BEACON_OFF;
				else if (beacon_state == QLCNIC_BEACON_EANBLE)
					ahw->beacon_state = QLCNIC_BEACON_ON;
			}
		}
		qlcnic_free_mbx_args(&cmd);
	}

	return;
}

void qlcnic_82xx_get_func_no(struct qlcnic_adapter *adapter)
{
	void __iomem *msix_base_addr;
	u32 func;
	u32 msix_base;

	pci_read_config_dword(adapter->pdev, QLCNIC_MSIX_TABLE_OFFSET, &func);
	msix_base_addr = adapter->ahw->pci_base0 + QLCNIC_MSIX_BASE;
	msix_base = readl(msix_base_addr);
	func = (func - msix_base) / QLCNIC_MSIX_TBL_PGSIZE;
	adapter->ahw->pci_func = func;
}

void qlcnic_82xx_read_crb(struct qlcnic_adapter *adapter, char *buf,
			  loff_t offset, size_t size)
{
	int err = 0;
	u32 data;
	u64 qmdata;

	if (ADDR_IN_RANGE(offset, QLCNIC_PCI_CAMQM, QLCNIC_PCI_CAMQM_END)) {
		qlcnic_pci_camqm_read_2M(adapter, offset, &qmdata);
		memcpy(buf, &qmdata, size);
	} else {
		data = QLCRD32(adapter, offset, &err);
		memcpy(buf, &data, size);
	}
}

void qlcnic_82xx_write_crb(struct qlcnic_adapter *adapter, char *buf,
			   loff_t offset, size_t size)
{
	u32 data;
	u64 qmdata;

	if (ADDR_IN_RANGE(offset, QLCNIC_PCI_CAMQM, QLCNIC_PCI_CAMQM_END)) {
		memcpy(&qmdata, buf, size);
		qlcnic_pci_camqm_write_2M(adapter, offset, qmdata);
	} else {
		memcpy(&data, buf, size);
		QLCWR32(adapter, offset, data);
	}
}

int qlcnic_82xx_api_lock(struct qlcnic_adapter *adapter)
{
	return qlcnic_pcie_sem_lock(adapter, 5, 0);
}

void qlcnic_82xx_api_unlock(struct qlcnic_adapter *adapter)
{
	qlcnic_pcie_sem_unlock(adapter, 5);
}

int qlcnic_82xx_shutdown(struct pci_dev *pdev)
{
	struct qlcnic_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;

	netif_device_detach(netdev);

	qlcnic_cancel_idc_work(adapter);

	if (netif_running(netdev))
		qlcnic_down(adapter, netdev);

	qlcnic_clr_all_drv_state(adapter, 0);

	clear_bit(__QLCNIC_RESETTING, &adapter->state);

	if (qlcnic_wol_supported(adapter))
		device_wakeup_enable(&pdev->dev);

	return 0;
}

int qlcnic_82xx_resume(struct qlcnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int err;

	err = qlcnic_start_firmware(adapter);
	if (err) {
		dev_err(&adapter->pdev->dev, "failed to start firmware\n");
		return err;
	}

	if (netif_running(netdev)) {
		err = qlcnic_up(adapter, netdev);
		if (!err)
			qlcnic_restore_indev_addr(netdev, NETDEV_UP);
	}

	netif_device_attach(netdev);
	qlcnic_schedule_work(adapter, qlcnic_fw_poll_work, FW_POLL_DELAY);
	return err;
}
