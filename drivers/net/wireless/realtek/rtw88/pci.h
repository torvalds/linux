/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTK_PCI_H_
#define __RTK_PCI_H_

#define RTK_PCI_DEVICE(vend, dev, hw_config)	\
	PCI_DEVICE(vend, dev),			\
	.driver_data = (kernel_ulong_t)&(hw_config),

#define RTK_DEFAULT_TX_DESC_NUM 128
#define RTK_BEQ_TX_DESC_NUM	256

#define RTK_MAX_RX_DESC_NUM	512
/* 8K + rx desc size */
#define RTK_PCI_RX_BUF_SIZE	(8192 + 24)

#define RTK_PCI_CTRL		0x300
#define BIT_RST_TRXDMA_INTF	BIT(20)
#define BIT_RX_TAG_EN		BIT(15)
#define REG_DBI_WDATA_V1	0x03E8
#define REG_DBI_FLAG_V1		0x03F0
#define REG_MDIO_V1		0x03F4
#define REG_PCIE_MIX_CFG	0x03F8
#define BIT_MDIO_WFLAG_V1	BIT(5)

#define BIT_PCI_BCNQ_FLAG	BIT(4)
#define RTK_PCI_TXBD_DESA_BCNQ	0x308
#define RTK_PCI_TXBD_DESA_H2CQ	0x1320
#define RTK_PCI_TXBD_DESA_MGMTQ	0x310
#define RTK_PCI_TXBD_DESA_BKQ	0x330
#define RTK_PCI_TXBD_DESA_BEQ	0x328
#define RTK_PCI_TXBD_DESA_VIQ	0x320
#define RTK_PCI_TXBD_DESA_VOQ	0x318
#define RTK_PCI_TXBD_DESA_HI0Q	0x340
#define RTK_PCI_RXBD_DESA_MPDUQ	0x338

/* BCNQ is specialized for rsvd page, does not need to specify a number */
#define RTK_PCI_TXBD_NUM_H2CQ	0x1328
#define RTK_PCI_TXBD_NUM_MGMTQ	0x380
#define RTK_PCI_TXBD_NUM_BKQ	0x38A
#define RTK_PCI_TXBD_NUM_BEQ	0x388
#define RTK_PCI_TXBD_NUM_VIQ	0x386
#define RTK_PCI_TXBD_NUM_VOQ	0x384
#define RTK_PCI_TXBD_NUM_HI0Q	0x38C
#define RTK_PCI_RXBD_NUM_MPDUQ	0x382
#define RTK_PCI_TXBD_IDX_H2CQ	0x132C
#define RTK_PCI_TXBD_IDX_MGMTQ	0x3B0
#define RTK_PCI_TXBD_IDX_BKQ	0x3AC
#define RTK_PCI_TXBD_IDX_BEQ	0x3A8
#define RTK_PCI_TXBD_IDX_VIQ	0x3A4
#define RTK_PCI_TXBD_IDX_VOQ	0x3A0
#define RTK_PCI_TXBD_IDX_HI0Q	0x3B8
#define RTK_PCI_RXBD_IDX_MPDUQ	0x3B4

#define RTK_PCI_TXBD_RWPTR_CLR	0x39C
#define RTK_PCI_TXBD_H2CQ_CSR	0x1330

#define BIT_CLR_H2CQ_HOST_IDX	BIT(16)
#define BIT_CLR_H2CQ_HW_IDX	BIT(8)

#define RTK_PCI_HIMR0		0x0B0
#define RTK_PCI_HISR0		0x0B4
#define RTK_PCI_HIMR1		0x0B8
#define RTK_PCI_HISR1		0x0BC
#define RTK_PCI_HIMR2		0x10B0
#define RTK_PCI_HISR2		0x10B4
#define RTK_PCI_HIMR3		0x10B8
#define RTK_PCI_HISR3		0x10BC
/* IMR 0 */
#define IMR_TIMER2		BIT(31)
#define IMR_TIMER1		BIT(30)
#define IMR_PSTIMEOUT		BIT(29)
#define IMR_GTINT4		BIT(28)
#define IMR_GTINT3		BIT(27)
#define IMR_TBDER		BIT(26)
#define IMR_TBDOK		BIT(25)
#define IMR_TSF_BIT32_TOGGLE	BIT(24)
#define IMR_BCNDMAINT0		BIT(20)
#define IMR_BCNDOK0		BIT(16)
#define IMR_HSISR_IND_ON_INT	BIT(15)
#define IMR_BCNDMAINT_E		BIT(14)
#define IMR_ATIMEND		BIT(12)
#define IMR_HISR1_IND_INT	BIT(11)
#define IMR_C2HCMD		BIT(10)
#define IMR_CPWM2		BIT(9)
#define IMR_CPWM		BIT(8)
#define IMR_HIGHDOK		BIT(7)
#define IMR_MGNTDOK		BIT(6)
#define IMR_BKDOK		BIT(5)
#define IMR_BEDOK		BIT(4)
#define IMR_VIDOK		BIT(3)
#define IMR_VODOK		BIT(2)
#define IMR_RDU			BIT(1)
#define IMR_ROK			BIT(0)
/* IMR 1 */
#define IMR_TXFIFO_TH_INT	BIT(30)
#define IMR_BTON_STS_UPDATE	BIT(29)
#define IMR_MCUERR		BIT(28)
#define IMR_BCNDMAINT7		BIT(27)
#define IMR_BCNDMAINT6		BIT(26)
#define IMR_BCNDMAINT5		BIT(25)
#define IMR_BCNDMAINT4		BIT(24)
#define IMR_BCNDMAINT3		BIT(23)
#define IMR_BCNDMAINT2		BIT(22)
#define IMR_BCNDMAINT1		BIT(21)
#define IMR_BCNDOK7		BIT(20)
#define IMR_BCNDOK6		BIT(19)
#define IMR_BCNDOK5		BIT(18)
#define IMR_BCNDOK4		BIT(17)
#define IMR_BCNDOK3		BIT(16)
#define IMR_BCNDOK2		BIT(15)
#define IMR_BCNDOK1		BIT(14)
#define IMR_ATIMEND_E		BIT(13)
#define IMR_ATIMEND		BIT(12)
#define IMR_TXERR		BIT(11)
#define IMR_RXERR		BIT(10)
#define IMR_TXFOVW		BIT(9)
#define IMR_RXFOVW		BIT(8)
#define IMR_CPU_MGQ_TXDONE	BIT(5)
#define IMR_PS_TIMER_C		BIT(4)
#define IMR_PS_TIMER_B		BIT(3)
#define IMR_PS_TIMER_A		BIT(2)
#define IMR_CPUMGQ_TX_TIMER	BIT(1)
/* IMR 3 */
#define IMR_H2CDOK		BIT(16)

/* one element is reserved to know if the ring is closed */
static inline int avail_desc(u32 wp, u32 rp, u32 len)
{
	if (rp > wp)
		return rp - wp - 1;
	else
		return len - wp + rp - 1;
}

#define RTK_PCI_TXBD_OWN_OFFSET 15
#define RTK_PCI_TXBD_BCN_WORK	0x383

struct rtw_pci_tx_buffer_desc {
	__le16 buf_size;
	__le16 psb_len;
	__le32 dma;
};

struct rtw_pci_tx_data {
	dma_addr_t dma;
	u8 sn;
};

struct rtw_pci_ring {
	u8 *head;
	dma_addr_t dma;

	u8 desc_size;

	u32 len;
	u32 wp;
	u32 rp;
};

struct rtw_pci_tx_ring {
	struct rtw_pci_ring r;
	struct sk_buff_head queue;
	bool queue_stopped;
};

struct rtw_pci_rx_buffer_desc {
	__le16 buf_size;
	__le16 total_pkt_size;
	__le32 dma;
};

struct rtw_pci_rx_ring {
	struct rtw_pci_ring r;
	struct sk_buff *buf[RTK_MAX_RX_DESC_NUM];
};

#define RX_TAG_MAX	8192

struct rtw_pci {
	struct pci_dev *pdev;

	/* used for pci interrupt */
	spinlock_t irq_lock;
	u32 irq_mask[4];
	bool irq_enabled;

	u16 rx_tag;
	struct rtw_pci_tx_ring tx_rings[RTK_MAX_TX_QUEUE_NUM];
	struct rtw_pci_rx_ring rx_rings[RTK_MAX_RX_QUEUE_NUM];

	void __iomem *mmap;
};

static u32 max_num_of_tx_queue(u8 queue)
{
	u32 max_num;

	switch (queue) {
	case RTW_TX_QUEUE_BE:
		max_num = RTK_BEQ_TX_DESC_NUM;
		break;
	case RTW_TX_QUEUE_BCN:
		max_num = 1;
		break;
	default:
		max_num = RTK_DEFAULT_TX_DESC_NUM;
		break;
	}

	return max_num;
}

static inline struct
rtw_pci_tx_data *rtw_pci_get_tx_data(struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	BUILD_BUG_ON(sizeof(struct rtw_pci_tx_data) >
		     sizeof(info->status.status_driver_data));

	return (struct rtw_pci_tx_data *)info->status.status_driver_data;
}

static inline
struct rtw_pci_tx_buffer_desc *get_tx_buffer_desc(struct rtw_pci_tx_ring *ring,
						  u32 size)
{
	u8 *buf_desc;

	buf_desc = ring->r.head + ring->r.wp * size;
	return (struct rtw_pci_tx_buffer_desc *)buf_desc;
}

#endif
