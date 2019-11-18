// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/pci.h>
#include "main.h"
#include "pci.h"
#include "tx.h"
#include "rx.h"
#include "fw.h"
#include "ps.h"
#include "debug.h"

static bool rtw_disable_msi;
module_param_named(disable_msi, rtw_disable_msi, bool, 0644);
MODULE_PARM_DESC(disable_msi, "Set Y to disable MSI interrupt support");

static u32 rtw_pci_tx_queue_idx_addr[] = {
	[RTW_TX_QUEUE_BK]	= RTK_PCI_TXBD_IDX_BKQ,
	[RTW_TX_QUEUE_BE]	= RTK_PCI_TXBD_IDX_BEQ,
	[RTW_TX_QUEUE_VI]	= RTK_PCI_TXBD_IDX_VIQ,
	[RTW_TX_QUEUE_VO]	= RTK_PCI_TXBD_IDX_VOQ,
	[RTW_TX_QUEUE_MGMT]	= RTK_PCI_TXBD_IDX_MGMTQ,
	[RTW_TX_QUEUE_HI0]	= RTK_PCI_TXBD_IDX_HI0Q,
	[RTW_TX_QUEUE_H2C]	= RTK_PCI_TXBD_IDX_H2CQ,
};

static u8 rtw_pci_get_tx_qsel(struct sk_buff *skb, u8 queue)
{
	switch (queue) {
	case RTW_TX_QUEUE_BCN:
		return TX_DESC_QSEL_BEACON;
	case RTW_TX_QUEUE_H2C:
		return TX_DESC_QSEL_H2C;
	case RTW_TX_QUEUE_MGMT:
		return TX_DESC_QSEL_MGMT;
	case RTW_TX_QUEUE_HI0:
		return TX_DESC_QSEL_HIGH;
	default:
		return skb->priority;
	}
};

static u8 rtw_pci_read8(struct rtw_dev *rtwdev, u32 addr)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;

	return readb(rtwpci->mmap + addr);
}

static u16 rtw_pci_read16(struct rtw_dev *rtwdev, u32 addr)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;

	return readw(rtwpci->mmap + addr);
}

static u32 rtw_pci_read32(struct rtw_dev *rtwdev, u32 addr)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;

	return readl(rtwpci->mmap + addr);
}

static void rtw_pci_write8(struct rtw_dev *rtwdev, u32 addr, u8 val)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;

	writeb(val, rtwpci->mmap + addr);
}

static void rtw_pci_write16(struct rtw_dev *rtwdev, u32 addr, u16 val)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;

	writew(val, rtwpci->mmap + addr);
}

static void rtw_pci_write32(struct rtw_dev *rtwdev, u32 addr, u32 val)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;

	writel(val, rtwpci->mmap + addr);
}

static inline void *rtw_pci_get_tx_desc(struct rtw_pci_tx_ring *tx_ring, u8 idx)
{
	int offset = tx_ring->r.desc_size * idx;

	return tx_ring->r.head + offset;
}

static void rtw_pci_free_tx_ring_skbs(struct rtw_dev *rtwdev,
				      struct rtw_pci_tx_ring *tx_ring)
{
	struct pci_dev *pdev = to_pci_dev(rtwdev->dev);
	struct rtw_pci_tx_data *tx_data;
	struct sk_buff *skb, *tmp;
	dma_addr_t dma;

	/* free every skb remained in tx list */
	skb_queue_walk_safe(&tx_ring->queue, skb, tmp) {
		__skb_unlink(skb, &tx_ring->queue);
		tx_data = rtw_pci_get_tx_data(skb);
		dma = tx_data->dma;

		pci_unmap_single(pdev, dma, skb->len, PCI_DMA_TODEVICE);
		dev_kfree_skb_any(skb);
	}
}

static void rtw_pci_free_tx_ring(struct rtw_dev *rtwdev,
				 struct rtw_pci_tx_ring *tx_ring)
{
	struct pci_dev *pdev = to_pci_dev(rtwdev->dev);
	u8 *head = tx_ring->r.head;
	u32 len = tx_ring->r.len;
	int ring_sz = len * tx_ring->r.desc_size;

	rtw_pci_free_tx_ring_skbs(rtwdev, tx_ring);

	/* free the ring itself */
	pci_free_consistent(pdev, ring_sz, head, tx_ring->r.dma);
	tx_ring->r.head = NULL;
}

static void rtw_pci_free_rx_ring_skbs(struct rtw_dev *rtwdev,
				      struct rtw_pci_rx_ring *rx_ring)
{
	struct pci_dev *pdev = to_pci_dev(rtwdev->dev);
	struct sk_buff *skb;
	int buf_sz = RTK_PCI_RX_BUF_SIZE;
	dma_addr_t dma;
	int i;

	for (i = 0; i < rx_ring->r.len; i++) {
		skb = rx_ring->buf[i];
		if (!skb)
			continue;

		dma = *((dma_addr_t *)skb->cb);
		pci_unmap_single(pdev, dma, buf_sz, PCI_DMA_FROMDEVICE);
		dev_kfree_skb(skb);
		rx_ring->buf[i] = NULL;
	}
}

static void rtw_pci_free_rx_ring(struct rtw_dev *rtwdev,
				 struct rtw_pci_rx_ring *rx_ring)
{
	struct pci_dev *pdev = to_pci_dev(rtwdev->dev);
	u8 *head = rx_ring->r.head;
	int ring_sz = rx_ring->r.desc_size * rx_ring->r.len;

	rtw_pci_free_rx_ring_skbs(rtwdev, rx_ring);

	pci_free_consistent(pdev, ring_sz, head, rx_ring->r.dma);
}

static void rtw_pci_free_trx_ring(struct rtw_dev *rtwdev)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	struct rtw_pci_tx_ring *tx_ring;
	struct rtw_pci_rx_ring *rx_ring;
	int i;

	for (i = 0; i < RTK_MAX_TX_QUEUE_NUM; i++) {
		tx_ring = &rtwpci->tx_rings[i];
		rtw_pci_free_tx_ring(rtwdev, tx_ring);
	}

	for (i = 0; i < RTK_MAX_RX_QUEUE_NUM; i++) {
		rx_ring = &rtwpci->rx_rings[i];
		rtw_pci_free_rx_ring(rtwdev, rx_ring);
	}
}

static int rtw_pci_init_tx_ring(struct rtw_dev *rtwdev,
				struct rtw_pci_tx_ring *tx_ring,
				u8 desc_size, u32 len)
{
	struct pci_dev *pdev = to_pci_dev(rtwdev->dev);
	int ring_sz = desc_size * len;
	dma_addr_t dma;
	u8 *head;

	head = pci_zalloc_consistent(pdev, ring_sz, &dma);
	if (!head) {
		rtw_err(rtwdev, "failed to allocate tx ring\n");
		return -ENOMEM;
	}

	skb_queue_head_init(&tx_ring->queue);
	tx_ring->r.head = head;
	tx_ring->r.dma = dma;
	tx_ring->r.len = len;
	tx_ring->r.desc_size = desc_size;
	tx_ring->r.wp = 0;
	tx_ring->r.rp = 0;

	return 0;
}

static int rtw_pci_reset_rx_desc(struct rtw_dev *rtwdev, struct sk_buff *skb,
				 struct rtw_pci_rx_ring *rx_ring,
				 u32 idx, u32 desc_sz)
{
	struct pci_dev *pdev = to_pci_dev(rtwdev->dev);
	struct rtw_pci_rx_buffer_desc *buf_desc;
	int buf_sz = RTK_PCI_RX_BUF_SIZE;
	dma_addr_t dma;

	if (!skb)
		return -EINVAL;

	dma = pci_map_single(pdev, skb->data, buf_sz, PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(pdev, dma))
		return -EBUSY;

	*((dma_addr_t *)skb->cb) = dma;
	buf_desc = (struct rtw_pci_rx_buffer_desc *)(rx_ring->r.head +
						     idx * desc_sz);
	memset(buf_desc, 0, sizeof(*buf_desc));
	buf_desc->buf_size = cpu_to_le16(RTK_PCI_RX_BUF_SIZE);
	buf_desc->dma = cpu_to_le32(dma);

	return 0;
}

static void rtw_pci_sync_rx_desc_device(struct rtw_dev *rtwdev, dma_addr_t dma,
					struct rtw_pci_rx_ring *rx_ring,
					u32 idx, u32 desc_sz)
{
	struct device *dev = rtwdev->dev;
	struct rtw_pci_rx_buffer_desc *buf_desc;
	int buf_sz = RTK_PCI_RX_BUF_SIZE;

	dma_sync_single_for_device(dev, dma, buf_sz, DMA_FROM_DEVICE);

	buf_desc = (struct rtw_pci_rx_buffer_desc *)(rx_ring->r.head +
						     idx * desc_sz);
	memset(buf_desc, 0, sizeof(*buf_desc));
	buf_desc->buf_size = cpu_to_le16(RTK_PCI_RX_BUF_SIZE);
	buf_desc->dma = cpu_to_le32(dma);
}

static int rtw_pci_init_rx_ring(struct rtw_dev *rtwdev,
				struct rtw_pci_rx_ring *rx_ring,
				u8 desc_size, u32 len)
{
	struct pci_dev *pdev = to_pci_dev(rtwdev->dev);
	struct sk_buff *skb = NULL;
	dma_addr_t dma;
	u8 *head;
	int ring_sz = desc_size * len;
	int buf_sz = RTK_PCI_RX_BUF_SIZE;
	int i, allocated;
	int ret = 0;

	head = pci_zalloc_consistent(pdev, ring_sz, &dma);
	if (!head) {
		rtw_err(rtwdev, "failed to allocate rx ring\n");
		return -ENOMEM;
	}
	rx_ring->r.head = head;

	for (i = 0; i < len; i++) {
		skb = dev_alloc_skb(buf_sz);
		if (!skb) {
			allocated = i;
			ret = -ENOMEM;
			goto err_out;
		}

		memset(skb->data, 0, buf_sz);
		rx_ring->buf[i] = skb;
		ret = rtw_pci_reset_rx_desc(rtwdev, skb, rx_ring, i, desc_size);
		if (ret) {
			allocated = i;
			dev_kfree_skb_any(skb);
			goto err_out;
		}
	}

	rx_ring->r.dma = dma;
	rx_ring->r.len = len;
	rx_ring->r.desc_size = desc_size;
	rx_ring->r.wp = 0;
	rx_ring->r.rp = 0;

	return 0;

err_out:
	for (i = 0; i < allocated; i++) {
		skb = rx_ring->buf[i];
		if (!skb)
			continue;
		dma = *((dma_addr_t *)skb->cb);
		pci_unmap_single(pdev, dma, buf_sz, PCI_DMA_FROMDEVICE);
		dev_kfree_skb_any(skb);
		rx_ring->buf[i] = NULL;
	}
	pci_free_consistent(pdev, ring_sz, head, dma);

	rtw_err(rtwdev, "failed to init rx buffer\n");

	return ret;
}

static int rtw_pci_init_trx_ring(struct rtw_dev *rtwdev)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	struct rtw_pci_tx_ring *tx_ring;
	struct rtw_pci_rx_ring *rx_ring;
	struct rtw_chip_info *chip = rtwdev->chip;
	int i = 0, j = 0, tx_alloced = 0, rx_alloced = 0;
	int tx_desc_size, rx_desc_size;
	u32 len;
	int ret;

	tx_desc_size = chip->tx_buf_desc_sz;

	for (i = 0; i < RTK_MAX_TX_QUEUE_NUM; i++) {
		tx_ring = &rtwpci->tx_rings[i];
		len = max_num_of_tx_queue(i);
		ret = rtw_pci_init_tx_ring(rtwdev, tx_ring, tx_desc_size, len);
		if (ret)
			goto out;
	}

	rx_desc_size = chip->rx_buf_desc_sz;

	for (j = 0; j < RTK_MAX_RX_QUEUE_NUM; j++) {
		rx_ring = &rtwpci->rx_rings[j];
		ret = rtw_pci_init_rx_ring(rtwdev, rx_ring, rx_desc_size,
					   RTK_MAX_RX_DESC_NUM);
		if (ret)
			goto out;
	}

	return 0;

out:
	tx_alloced = i;
	for (i = 0; i < tx_alloced; i++) {
		tx_ring = &rtwpci->tx_rings[i];
		rtw_pci_free_tx_ring(rtwdev, tx_ring);
	}

	rx_alloced = j;
	for (j = 0; j < rx_alloced; j++) {
		rx_ring = &rtwpci->rx_rings[j];
		rtw_pci_free_rx_ring(rtwdev, rx_ring);
	}

	return ret;
}

static void rtw_pci_deinit(struct rtw_dev *rtwdev)
{
	rtw_pci_free_trx_ring(rtwdev);
}

static int rtw_pci_init(struct rtw_dev *rtwdev)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	int ret = 0;

	rtwpci->irq_mask[0] = IMR_HIGHDOK |
			      IMR_MGNTDOK |
			      IMR_BKDOK |
			      IMR_BEDOK |
			      IMR_VIDOK |
			      IMR_VODOK |
			      IMR_ROK |
			      IMR_BCNDMAINT_E |
			      0;
	rtwpci->irq_mask[1] = IMR_TXFOVW |
			      0;
	rtwpci->irq_mask[3] = IMR_H2CDOK |
			      0;
	spin_lock_init(&rtwpci->irq_lock);
	ret = rtw_pci_init_trx_ring(rtwdev);

	return ret;
}

static void rtw_pci_reset_buf_desc(struct rtw_dev *rtwdev)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	u32 len;
	u8 tmp;
	dma_addr_t dma;

	tmp = rtw_read8(rtwdev, RTK_PCI_CTRL + 3);
	rtw_write8(rtwdev, RTK_PCI_CTRL + 3, tmp | 0xf7);

	dma = rtwpci->tx_rings[RTW_TX_QUEUE_BCN].r.dma;
	rtw_write32(rtwdev, RTK_PCI_TXBD_DESA_BCNQ, dma);

	len = rtwpci->tx_rings[RTW_TX_QUEUE_H2C].r.len;
	dma = rtwpci->tx_rings[RTW_TX_QUEUE_H2C].r.dma;
	rtwpci->tx_rings[RTW_TX_QUEUE_H2C].r.rp = 0;
	rtwpci->tx_rings[RTW_TX_QUEUE_H2C].r.wp = 0;
	rtw_write16(rtwdev, RTK_PCI_TXBD_NUM_H2CQ, len);
	rtw_write32(rtwdev, RTK_PCI_TXBD_DESA_H2CQ, dma);

	len = rtwpci->tx_rings[RTW_TX_QUEUE_BK].r.len;
	dma = rtwpci->tx_rings[RTW_TX_QUEUE_BK].r.dma;
	rtwpci->tx_rings[RTW_TX_QUEUE_BK].r.rp = 0;
	rtwpci->tx_rings[RTW_TX_QUEUE_BK].r.wp = 0;
	rtw_write16(rtwdev, RTK_PCI_TXBD_NUM_BKQ, len);
	rtw_write32(rtwdev, RTK_PCI_TXBD_DESA_BKQ, dma);

	len = rtwpci->tx_rings[RTW_TX_QUEUE_BE].r.len;
	dma = rtwpci->tx_rings[RTW_TX_QUEUE_BE].r.dma;
	rtwpci->tx_rings[RTW_TX_QUEUE_BE].r.rp = 0;
	rtwpci->tx_rings[RTW_TX_QUEUE_BE].r.wp = 0;
	rtw_write16(rtwdev, RTK_PCI_TXBD_NUM_BEQ, len);
	rtw_write32(rtwdev, RTK_PCI_TXBD_DESA_BEQ, dma);

	len = rtwpci->tx_rings[RTW_TX_QUEUE_VO].r.len;
	dma = rtwpci->tx_rings[RTW_TX_QUEUE_VO].r.dma;
	rtwpci->tx_rings[RTW_TX_QUEUE_VO].r.rp = 0;
	rtwpci->tx_rings[RTW_TX_QUEUE_VO].r.wp = 0;
	rtw_write16(rtwdev, RTK_PCI_TXBD_NUM_VOQ, len);
	rtw_write32(rtwdev, RTK_PCI_TXBD_DESA_VOQ, dma);

	len = rtwpci->tx_rings[RTW_TX_QUEUE_VI].r.len;
	dma = rtwpci->tx_rings[RTW_TX_QUEUE_VI].r.dma;
	rtwpci->tx_rings[RTW_TX_QUEUE_VI].r.rp = 0;
	rtwpci->tx_rings[RTW_TX_QUEUE_VI].r.wp = 0;
	rtw_write16(rtwdev, RTK_PCI_TXBD_NUM_VIQ, len);
	rtw_write32(rtwdev, RTK_PCI_TXBD_DESA_VIQ, dma);

	len = rtwpci->tx_rings[RTW_TX_QUEUE_MGMT].r.len;
	dma = rtwpci->tx_rings[RTW_TX_QUEUE_MGMT].r.dma;
	rtwpci->tx_rings[RTW_TX_QUEUE_MGMT].r.rp = 0;
	rtwpci->tx_rings[RTW_TX_QUEUE_MGMT].r.wp = 0;
	rtw_write16(rtwdev, RTK_PCI_TXBD_NUM_MGMTQ, len);
	rtw_write32(rtwdev, RTK_PCI_TXBD_DESA_MGMTQ, dma);

	len = rtwpci->tx_rings[RTW_TX_QUEUE_HI0].r.len;
	dma = rtwpci->tx_rings[RTW_TX_QUEUE_HI0].r.dma;
	rtwpci->tx_rings[RTW_TX_QUEUE_HI0].r.rp = 0;
	rtwpci->tx_rings[RTW_TX_QUEUE_HI0].r.wp = 0;
	rtw_write16(rtwdev, RTK_PCI_TXBD_NUM_HI0Q, len);
	rtw_write32(rtwdev, RTK_PCI_TXBD_DESA_HI0Q, dma);

	len = rtwpci->rx_rings[RTW_RX_QUEUE_MPDU].r.len;
	dma = rtwpci->rx_rings[RTW_RX_QUEUE_MPDU].r.dma;
	rtwpci->rx_rings[RTW_RX_QUEUE_MPDU].r.rp = 0;
	rtwpci->rx_rings[RTW_RX_QUEUE_MPDU].r.wp = 0;
	rtw_write16(rtwdev, RTK_PCI_RXBD_NUM_MPDUQ, len & 0xfff);
	rtw_write32(rtwdev, RTK_PCI_RXBD_DESA_MPDUQ, dma);

	/* reset read/write point */
	rtw_write32(rtwdev, RTK_PCI_TXBD_RWPTR_CLR, 0xffffffff);

	/* reset H2C Queue index in a single write */
	rtw_write32_set(rtwdev, RTK_PCI_TXBD_H2CQ_CSR,
			BIT_CLR_H2CQ_HOST_IDX | BIT_CLR_H2CQ_HW_IDX);
}

static void rtw_pci_reset_trx_ring(struct rtw_dev *rtwdev)
{
	rtw_pci_reset_buf_desc(rtwdev);
}

static void rtw_pci_enable_interrupt(struct rtw_dev *rtwdev,
				     struct rtw_pci *rtwpci)
{
	rtw_write32(rtwdev, RTK_PCI_HIMR0, rtwpci->irq_mask[0]);
	rtw_write32(rtwdev, RTK_PCI_HIMR1, rtwpci->irq_mask[1]);
	rtw_write32(rtwdev, RTK_PCI_HIMR3, rtwpci->irq_mask[3]);
	rtwpci->irq_enabled = true;
}

static void rtw_pci_disable_interrupt(struct rtw_dev *rtwdev,
				      struct rtw_pci *rtwpci)
{
	rtw_write32(rtwdev, RTK_PCI_HIMR0, 0);
	rtw_write32(rtwdev, RTK_PCI_HIMR1, 0);
	rtw_write32(rtwdev, RTK_PCI_HIMR3, 0);
	rtwpci->irq_enabled = false;
}

static int rtw_pci_setup(struct rtw_dev *rtwdev)
{
	rtw_pci_reset_trx_ring(rtwdev);

	return 0;
}

static void rtw_pci_dma_reset(struct rtw_dev *rtwdev, struct rtw_pci *rtwpci)
{
	/* reset dma and rx tag */
	rtw_write32_set(rtwdev, RTK_PCI_CTRL,
			BIT_RST_TRXDMA_INTF | BIT_RX_TAG_EN);
	rtwpci->rx_tag = 0;
}

static void rtw_pci_dma_release(struct rtw_dev *rtwdev, struct rtw_pci *rtwpci)
{
	struct rtw_pci_tx_ring *tx_ring;
	u8 queue;

	for (queue = 0; queue < RTK_MAX_TX_QUEUE_NUM; queue++) {
		tx_ring = &rtwpci->tx_rings[queue];
		rtw_pci_free_tx_ring_skbs(rtwdev, tx_ring);
	}
}

static int rtw_pci_start(struct rtw_dev *rtwdev)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	unsigned long flags;

	rtw_pci_dma_reset(rtwdev, rtwpci);

	spin_lock_irqsave(&rtwpci->irq_lock, flags);
	rtw_pci_enable_interrupt(rtwdev, rtwpci);
	spin_unlock_irqrestore(&rtwpci->irq_lock, flags);

	return 0;
}

static void rtw_pci_stop(struct rtw_dev *rtwdev)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	unsigned long flags;

	spin_lock_irqsave(&rtwpci->irq_lock, flags);
	rtw_pci_disable_interrupt(rtwdev, rtwpci);
	rtw_pci_dma_release(rtwdev, rtwpci);
	spin_unlock_irqrestore(&rtwpci->irq_lock, flags);
}

static void rtw_pci_deep_ps_enter(struct rtw_dev *rtwdev)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	struct rtw_pci_tx_ring *tx_ring;
	bool tx_empty = true;
	u8 queue;

	lockdep_assert_held(&rtwpci->irq_lock);

	/* Deep PS state is not allowed to TX-DMA */
	for (queue = 0; queue < RTK_MAX_TX_QUEUE_NUM; queue++) {
		/* BCN queue is rsvd page, does not have DMA interrupt
		 * H2C queue is managed by firmware
		 */
		if (queue == RTW_TX_QUEUE_BCN ||
		    queue == RTW_TX_QUEUE_H2C)
			continue;

		tx_ring = &rtwpci->tx_rings[queue];

		/* check if there is any skb DMAing */
		if (skb_queue_len(&tx_ring->queue)) {
			tx_empty = false;
			break;
		}
	}

	if (!tx_empty) {
		rtw_dbg(rtwdev, RTW_DBG_PS,
			"TX path not empty, cannot enter deep power save state\n");
		return;
	}

	set_bit(RTW_FLAG_LEISURE_PS_DEEP, rtwdev->flags);
	rtw_power_mode_change(rtwdev, true);
}

static void rtw_pci_deep_ps_leave(struct rtw_dev *rtwdev)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;

	lockdep_assert_held(&rtwpci->irq_lock);

	if (test_and_clear_bit(RTW_FLAG_LEISURE_PS_DEEP, rtwdev->flags))
		rtw_power_mode_change(rtwdev, false);
}

static void rtw_pci_deep_ps(struct rtw_dev *rtwdev, bool enter)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	unsigned long flags;

	spin_lock_irqsave(&rtwpci->irq_lock, flags);

	if (enter && !test_bit(RTW_FLAG_LEISURE_PS_DEEP, rtwdev->flags))
		rtw_pci_deep_ps_enter(rtwdev);

	if (!enter && test_bit(RTW_FLAG_LEISURE_PS_DEEP, rtwdev->flags))
		rtw_pci_deep_ps_leave(rtwdev);

	spin_unlock_irqrestore(&rtwpci->irq_lock, flags);
}

static u8 ac_to_hwq[] = {
	[IEEE80211_AC_VO] = RTW_TX_QUEUE_VO,
	[IEEE80211_AC_VI] = RTW_TX_QUEUE_VI,
	[IEEE80211_AC_BE] = RTW_TX_QUEUE_BE,
	[IEEE80211_AC_BK] = RTW_TX_QUEUE_BK,
};

static u8 rtw_hw_queue_mapping(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	__le16 fc = hdr->frame_control;
	u8 q_mapping = skb_get_queue_mapping(skb);
	u8 queue;

	if (unlikely(ieee80211_is_beacon(fc)))
		queue = RTW_TX_QUEUE_BCN;
	else if (unlikely(ieee80211_is_mgmt(fc) || ieee80211_is_ctl(fc)))
		queue = RTW_TX_QUEUE_MGMT;
	else if (WARN_ON_ONCE(q_mapping >= ARRAY_SIZE(ac_to_hwq)))
		queue = ac_to_hwq[IEEE80211_AC_BE];
	else
		queue = ac_to_hwq[q_mapping];

	return queue;
}

static void rtw_pci_release_rsvd_page(struct rtw_pci *rtwpci,
				      struct rtw_pci_tx_ring *ring)
{
	struct sk_buff *prev = skb_dequeue(&ring->queue);
	struct rtw_pci_tx_data *tx_data;
	dma_addr_t dma;

	if (!prev)
		return;

	tx_data = rtw_pci_get_tx_data(prev);
	dma = tx_data->dma;
	pci_unmap_single(rtwpci->pdev, dma, prev->len,
			 PCI_DMA_TODEVICE);
	dev_kfree_skb_any(prev);
}

static void rtw_pci_dma_check(struct rtw_dev *rtwdev,
			      struct rtw_pci_rx_ring *rx_ring,
			      u32 idx)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_pci_rx_buffer_desc *buf_desc;
	u32 desc_sz = chip->rx_buf_desc_sz;
	u16 total_pkt_size;

	buf_desc = (struct rtw_pci_rx_buffer_desc *)(rx_ring->r.head +
						     idx * desc_sz);
	total_pkt_size = le16_to_cpu(buf_desc->total_pkt_size);

	/* rx tag mismatch, throw a warning */
	if (total_pkt_size != rtwpci->rx_tag)
		rtw_warn(rtwdev, "pci bus timeout, check dma status\n");

	rtwpci->rx_tag = (rtwpci->rx_tag + 1) % RX_TAG_MAX;
}

static int rtw_pci_xmit(struct rtw_dev *rtwdev,
			struct rtw_tx_pkt_info *pkt_info,
			struct sk_buff *skb, u8 queue)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_pci_tx_ring *ring;
	struct rtw_pci_tx_data *tx_data;
	dma_addr_t dma;
	u32 tx_pkt_desc_sz = chip->tx_pkt_desc_sz;
	u32 tx_buf_desc_sz = chip->tx_buf_desc_sz;
	u32 size;
	u32 psb_len;
	u8 *pkt_desc;
	struct rtw_pci_tx_buffer_desc *buf_desc;
	u32 bd_idx;
	unsigned long flags;

	ring = &rtwpci->tx_rings[queue];

	size = skb->len;

	if (queue == RTW_TX_QUEUE_BCN)
		rtw_pci_release_rsvd_page(rtwpci, ring);
	else if (!avail_desc(ring->r.wp, ring->r.rp, ring->r.len))
		return -ENOSPC;

	pkt_desc = skb_push(skb, chip->tx_pkt_desc_sz);
	memset(pkt_desc, 0, tx_pkt_desc_sz);
	pkt_info->qsel = rtw_pci_get_tx_qsel(skb, queue);
	rtw_tx_fill_tx_desc(pkt_info, skb);
	dma = pci_map_single(rtwpci->pdev, skb->data, skb->len,
			     PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(rtwpci->pdev, dma))
		return -EBUSY;

	/* after this we got dma mapped, there is no way back */
	buf_desc = get_tx_buffer_desc(ring, tx_buf_desc_sz);
	memset(buf_desc, 0, tx_buf_desc_sz);
	psb_len = (skb->len - 1) / 128 + 1;
	if (queue == RTW_TX_QUEUE_BCN)
		psb_len |= 1 << RTK_PCI_TXBD_OWN_OFFSET;

	buf_desc[0].psb_len = cpu_to_le16(psb_len);
	buf_desc[0].buf_size = cpu_to_le16(tx_pkt_desc_sz);
	buf_desc[0].dma = cpu_to_le32(dma);
	buf_desc[1].buf_size = cpu_to_le16(size);
	buf_desc[1].dma = cpu_to_le32(dma + tx_pkt_desc_sz);

	tx_data = rtw_pci_get_tx_data(skb);
	tx_data->dma = dma;
	tx_data->sn = pkt_info->sn;

	spin_lock_irqsave(&rtwpci->irq_lock, flags);

	rtw_pci_deep_ps_leave(rtwdev);
	skb_queue_tail(&ring->queue, skb);

	/* kick off tx queue */
	if (queue != RTW_TX_QUEUE_BCN) {
		if (++ring->r.wp >= ring->r.len)
			ring->r.wp = 0;
		bd_idx = rtw_pci_tx_queue_idx_addr[queue];
		rtw_write16(rtwdev, bd_idx, ring->r.wp & 0xfff);
	} else {
		u32 reg_bcn_work;

		reg_bcn_work = rtw_read8(rtwdev, RTK_PCI_TXBD_BCN_WORK);
		reg_bcn_work |= BIT_PCI_BCNQ_FLAG;
		rtw_write8(rtwdev, RTK_PCI_TXBD_BCN_WORK, reg_bcn_work);
	}
	spin_unlock_irqrestore(&rtwpci->irq_lock, flags);

	return 0;
}

static int rtw_pci_write_data_rsvd_page(struct rtw_dev *rtwdev, u8 *buf,
					u32 size)
{
	struct sk_buff *skb;
	struct rtw_tx_pkt_info pkt_info;
	u32 tx_pkt_desc_sz;
	u32 length;

	tx_pkt_desc_sz = rtwdev->chip->tx_pkt_desc_sz;
	length = size + tx_pkt_desc_sz;
	skb = dev_alloc_skb(length);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, tx_pkt_desc_sz);
	memcpy((u8 *)skb_put(skb, size), buf, size);
	memset(&pkt_info, 0, sizeof(pkt_info));
	pkt_info.tx_pkt_size = size;
	pkt_info.offset = tx_pkt_desc_sz;

	return rtw_pci_xmit(rtwdev, &pkt_info, skb, RTW_TX_QUEUE_BCN);
}

static int rtw_pci_write_data_h2c(struct rtw_dev *rtwdev, u8 *buf, u32 size)
{
	struct sk_buff *skb;
	struct rtw_tx_pkt_info pkt_info;
	u32 tx_pkt_desc_sz;
	u32 length;

	tx_pkt_desc_sz = rtwdev->chip->tx_pkt_desc_sz;
	length = size + tx_pkt_desc_sz;
	skb = dev_alloc_skb(length);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, tx_pkt_desc_sz);
	memcpy((u8 *)skb_put(skb, size), buf, size);
	memset(&pkt_info, 0, sizeof(pkt_info));
	pkt_info.tx_pkt_size = size;

	return rtw_pci_xmit(rtwdev, &pkt_info, skb, RTW_TX_QUEUE_H2C);
}

static int rtw_pci_tx(struct rtw_dev *rtwdev,
		      struct rtw_tx_pkt_info *pkt_info,
		      struct sk_buff *skb)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	struct rtw_pci_tx_ring *ring;
	u8 queue = rtw_hw_queue_mapping(skb);
	int ret;

	ret = rtw_pci_xmit(rtwdev, pkt_info, skb, queue);
	if (ret)
		return ret;

	ring = &rtwpci->tx_rings[queue];
	if (avail_desc(ring->r.wp, ring->r.rp, ring->r.len) < 2) {
		ieee80211_stop_queue(rtwdev->hw, skb_get_queue_mapping(skb));
		ring->queue_stopped = true;
	}

	return 0;
}

static void rtw_pci_tx_isr(struct rtw_dev *rtwdev, struct rtw_pci *rtwpci,
			   u8 hw_queue)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct ieee80211_tx_info *info;
	struct rtw_pci_tx_ring *ring;
	struct rtw_pci_tx_data *tx_data;
	struct sk_buff *skb;
	u32 count;
	u32 bd_idx_addr;
	u32 bd_idx, cur_rp;
	u16 q_map;

	ring = &rtwpci->tx_rings[hw_queue];

	bd_idx_addr = rtw_pci_tx_queue_idx_addr[hw_queue];
	bd_idx = rtw_read32(rtwdev, bd_idx_addr);
	cur_rp = bd_idx >> 16;
	cur_rp &= 0xfff;
	if (cur_rp >= ring->r.rp)
		count = cur_rp - ring->r.rp;
	else
		count = ring->r.len - (ring->r.rp - cur_rp);

	while (count--) {
		skb = skb_dequeue(&ring->queue);
		tx_data = rtw_pci_get_tx_data(skb);
		pci_unmap_single(rtwpci->pdev, tx_data->dma, skb->len,
				 PCI_DMA_TODEVICE);

		/* just free command packets from host to card */
		if (hw_queue == RTW_TX_QUEUE_H2C) {
			dev_kfree_skb_irq(skb);
			continue;
		}

		if (ring->queue_stopped &&
		    avail_desc(ring->r.wp, ring->r.rp, ring->r.len) > 4) {
			q_map = skb_get_queue_mapping(skb);
			ieee80211_wake_queue(hw, q_map);
			ring->queue_stopped = false;
		}

		skb_pull(skb, rtwdev->chip->tx_pkt_desc_sz);

		info = IEEE80211_SKB_CB(skb);

		/* enqueue to wait for tx report */
		if (info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS) {
			rtw_tx_report_enqueue(rtwdev, skb, tx_data->sn);
			continue;
		}

		/* always ACK for others, then they won't be marked as drop */
		if (info->flags & IEEE80211_TX_CTL_NO_ACK)
			info->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;
		else
			info->flags |= IEEE80211_TX_STAT_ACK;

		ieee80211_tx_info_clear_status(info);
		ieee80211_tx_status_irqsafe(hw, skb);
	}

	ring->r.rp = cur_rp;
}

static void rtw_pci_rx_isr(struct rtw_dev *rtwdev, struct rtw_pci *rtwpci,
			   u8 hw_queue)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_pci_rx_ring *ring;
	struct rtw_rx_pkt_stat pkt_stat;
	struct ieee80211_rx_status rx_status;
	struct sk_buff *skb, *new;
	u32 cur_wp, cur_rp, tmp;
	u32 count;
	u32 pkt_offset;
	u32 pkt_desc_sz = chip->rx_pkt_desc_sz;
	u32 buf_desc_sz = chip->rx_buf_desc_sz;
	u32 new_len;
	u8 *rx_desc;
	dma_addr_t dma;

	ring = &rtwpci->rx_rings[RTW_RX_QUEUE_MPDU];

	tmp = rtw_read32(rtwdev, RTK_PCI_RXBD_IDX_MPDUQ);
	cur_wp = tmp >> 16;
	cur_wp &= 0xfff;
	if (cur_wp >= ring->r.wp)
		count = cur_wp - ring->r.wp;
	else
		count = ring->r.len - (ring->r.wp - cur_wp);

	cur_rp = ring->r.rp;
	while (count--) {
		rtw_pci_dma_check(rtwdev, ring, cur_rp);
		skb = ring->buf[cur_rp];
		dma = *((dma_addr_t *)skb->cb);
		dma_sync_single_for_cpu(rtwdev->dev, dma, RTK_PCI_RX_BUF_SIZE,
					DMA_FROM_DEVICE);
		rx_desc = skb->data;
		chip->ops->query_rx_desc(rtwdev, rx_desc, &pkt_stat, &rx_status);

		/* offset from rx_desc to payload */
		pkt_offset = pkt_desc_sz + pkt_stat.drv_info_sz +
			     pkt_stat.shift;

		/* allocate a new skb for this frame,
		 * discard the frame if none available
		 */
		new_len = pkt_stat.pkt_len + pkt_offset;
		new = dev_alloc_skb(new_len);
		if (WARN_ONCE(!new, "rx routine starvation\n"))
			goto next_rp;

		/* put the DMA data including rx_desc from phy to new skb */
		skb_put_data(new, skb->data, new_len);

		if (pkt_stat.is_c2h) {
			rtw_fw_c2h_cmd_rx_irqsafe(rtwdev, pkt_offset, new);
		} else {
			/* remove rx_desc */
			skb_pull(new, pkt_offset);

			rtw_rx_stats(rtwdev, pkt_stat.vif, new);
			memcpy(new->cb, &rx_status, sizeof(rx_status));
			ieee80211_rx_irqsafe(rtwdev->hw, new);
		}

next_rp:
		/* new skb delivered to mac80211, re-enable original skb DMA */
		rtw_pci_sync_rx_desc_device(rtwdev, dma, ring, cur_rp,
					    buf_desc_sz);

		/* host read next element in ring */
		if (++cur_rp >= ring->r.len)
			cur_rp = 0;
	}

	ring->r.rp = cur_rp;
	ring->r.wp = cur_wp;
	rtw_write16(rtwdev, RTK_PCI_RXBD_IDX_MPDUQ, ring->r.rp);
}

static void rtw_pci_irq_recognized(struct rtw_dev *rtwdev,
				   struct rtw_pci *rtwpci, u32 *irq_status)
{
	irq_status[0] = rtw_read32(rtwdev, RTK_PCI_HISR0);
	irq_status[1] = rtw_read32(rtwdev, RTK_PCI_HISR1);
	irq_status[3] = rtw_read32(rtwdev, RTK_PCI_HISR3);
	irq_status[0] &= rtwpci->irq_mask[0];
	irq_status[1] &= rtwpci->irq_mask[1];
	irq_status[3] &= rtwpci->irq_mask[3];
	rtw_write32(rtwdev, RTK_PCI_HISR0, irq_status[0]);
	rtw_write32(rtwdev, RTK_PCI_HISR1, irq_status[1]);
	rtw_write32(rtwdev, RTK_PCI_HISR3, irq_status[3]);
}

static irqreturn_t rtw_pci_interrupt_handler(int irq, void *dev)
{
	struct rtw_dev *rtwdev = dev;
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;

	spin_lock(&rtwpci->irq_lock);
	if (!rtwpci->irq_enabled)
		goto out;

	/* disable RTW PCI interrupt to avoid more interrupts before the end of
	 * thread function
	 *
	 * disable HIMR here to also avoid new HISR flag being raised before
	 * the HISRs have been Write-1-cleared for MSI. If not all of the HISRs
	 * are cleared, the edge-triggered interrupt will not be generated when
	 * a new HISR flag is set.
	 */
	rtw_pci_disable_interrupt(rtwdev, rtwpci);
out:
	spin_unlock(&rtwpci->irq_lock);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t rtw_pci_interrupt_threadfn(int irq, void *dev)
{
	struct rtw_dev *rtwdev = dev;
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	unsigned long flags;
	u32 irq_status[4];

	spin_lock_irqsave(&rtwpci->irq_lock, flags);
	rtw_pci_irq_recognized(rtwdev, rtwpci, irq_status);

	if (irq_status[0] & IMR_MGNTDOK)
		rtw_pci_tx_isr(rtwdev, rtwpci, RTW_TX_QUEUE_MGMT);
	if (irq_status[0] & IMR_HIGHDOK)
		rtw_pci_tx_isr(rtwdev, rtwpci, RTW_TX_QUEUE_HI0);
	if (irq_status[0] & IMR_BEDOK)
		rtw_pci_tx_isr(rtwdev, rtwpci, RTW_TX_QUEUE_BE);
	if (irq_status[0] & IMR_BKDOK)
		rtw_pci_tx_isr(rtwdev, rtwpci, RTW_TX_QUEUE_BK);
	if (irq_status[0] & IMR_VODOK)
		rtw_pci_tx_isr(rtwdev, rtwpci, RTW_TX_QUEUE_VO);
	if (irq_status[0] & IMR_VIDOK)
		rtw_pci_tx_isr(rtwdev, rtwpci, RTW_TX_QUEUE_VI);
	if (irq_status[3] & IMR_H2CDOK)
		rtw_pci_tx_isr(rtwdev, rtwpci, RTW_TX_QUEUE_H2C);
	if (irq_status[0] & IMR_ROK)
		rtw_pci_rx_isr(rtwdev, rtwpci, RTW_RX_QUEUE_MPDU);

	/* all of the jobs for this interrupt have been done */
	rtw_pci_enable_interrupt(rtwdev, rtwpci);
	spin_unlock_irqrestore(&rtwpci->irq_lock, flags);

	return IRQ_HANDLED;
}

static int rtw_pci_io_mapping(struct rtw_dev *rtwdev,
			      struct pci_dev *pdev)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	unsigned long len;
	u8 bar_id = 2;
	int ret;

	ret = pci_request_regions(pdev, KBUILD_MODNAME);
	if (ret) {
		rtw_err(rtwdev, "failed to request pci regions\n");
		return ret;
	}

	len = pci_resource_len(pdev, bar_id);
	rtwpci->mmap = pci_iomap(pdev, bar_id, len);
	if (!rtwpci->mmap) {
		rtw_err(rtwdev, "failed to map pci memory\n");
		return -ENOMEM;
	}

	return 0;
}

static void rtw_pci_io_unmapping(struct rtw_dev *rtwdev,
				 struct pci_dev *pdev)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;

	if (rtwpci->mmap) {
		pci_iounmap(pdev, rtwpci->mmap);
		pci_release_regions(pdev);
	}
}

static void rtw_dbi_write8(struct rtw_dev *rtwdev, u16 addr, u8 data)
{
	u16 write_addr;
	u16 remainder = addr & ~(BITS_DBI_WREN | BITS_DBI_ADDR_MASK);
	u8 flag;
	u8 cnt;

	write_addr = addr & BITS_DBI_ADDR_MASK;
	write_addr |= u16_encode_bits(BIT(remainder), BITS_DBI_WREN);
	rtw_write8(rtwdev, REG_DBI_WDATA_V1 + remainder, data);
	rtw_write16(rtwdev, REG_DBI_FLAG_V1, write_addr);
	rtw_write8(rtwdev, REG_DBI_FLAG_V1 + 2, BIT_DBI_WFLAG >> 16);

	for (cnt = 0; cnt < RTW_PCI_WR_RETRY_CNT; cnt++) {
		flag = rtw_read8(rtwdev, REG_DBI_FLAG_V1 + 2);
		if (flag == 0)
			return;

		udelay(10);
	}

	WARN(flag, "failed to write to DBI register, addr=0x%04x\n", addr);
}

static int rtw_dbi_read8(struct rtw_dev *rtwdev, u16 addr, u8 *value)
{
	u16 read_addr = addr & BITS_DBI_ADDR_MASK;
	u8 flag;
	u8 cnt;

	rtw_write16(rtwdev, REG_DBI_FLAG_V1, read_addr);
	rtw_write8(rtwdev, REG_DBI_FLAG_V1 + 2, BIT_DBI_RFLAG >> 16);

	for (cnt = 0; cnt < RTW_PCI_WR_RETRY_CNT; cnt++) {
		flag = rtw_read8(rtwdev, REG_DBI_FLAG_V1 + 2);
		if (flag == 0) {
			read_addr = REG_DBI_RDATA_V1 + (addr & 3);
			*value = rtw_read8(rtwdev, read_addr);
			return 0;
		}

		udelay(10);
	}

	WARN(1, "failed to read DBI register, addr=0x%04x\n", addr);
	return -EIO;
}

static void rtw_mdio_write(struct rtw_dev *rtwdev, u8 addr, u16 data, bool g1)
{
	u8 page;
	u8 wflag;
	u8 cnt;

	rtw_write16(rtwdev, REG_MDIO_V1, data);

	page = addr < RTW_PCI_MDIO_PG_SZ ? 0 : 1;
	page += g1 ? RTW_PCI_MDIO_PG_OFFS_G1 : RTW_PCI_MDIO_PG_OFFS_G2;
	rtw_write8(rtwdev, REG_PCIE_MIX_CFG, addr & BITS_MDIO_ADDR_MASK);
	rtw_write8(rtwdev, REG_PCIE_MIX_CFG + 3, page);
	rtw_write32_mask(rtwdev, REG_PCIE_MIX_CFG, BIT_MDIO_WFLAG_V1, 1);

	for (cnt = 0; cnt < RTW_PCI_WR_RETRY_CNT; cnt++) {
		wflag = rtw_read32_mask(rtwdev, REG_PCIE_MIX_CFG,
					BIT_MDIO_WFLAG_V1);
		if (wflag == 0)
			return;

		udelay(10);
	}

	WARN(wflag, "failed to write to MDIO register, addr=0x%02x\n", addr);
}

static void rtw_pci_clkreq_set(struct rtw_dev *rtwdev, bool enable)
{
	u8 value;
	int ret;

	ret = rtw_dbi_read8(rtwdev, RTK_PCIE_LINK_CFG, &value);
	if (ret) {
		rtw_err(rtwdev, "failed to read CLKREQ_L1, ret=%d", ret);
		return;
	}

	if (enable)
		value |= BIT_CLKREQ_SW_EN;
	else
		value &= ~BIT_CLKREQ_SW_EN;

	rtw_dbi_write8(rtwdev, RTK_PCIE_LINK_CFG, value);
}

static void rtw_pci_aspm_set(struct rtw_dev *rtwdev, bool enable)
{
	u8 value;
	int ret;

	ret = rtw_dbi_read8(rtwdev, RTK_PCIE_LINK_CFG, &value);
	if (ret) {
		rtw_err(rtwdev, "failed to read ASPM, ret=%d", ret);
		return;
	}

	if (enable)
		value |= BIT_L1_SW_EN;
	else
		value &= ~BIT_L1_SW_EN;

	rtw_dbi_write8(rtwdev, RTK_PCIE_LINK_CFG, value);
}

static void rtw_pci_link_ps(struct rtw_dev *rtwdev, bool enter)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;

	/* Like CLKREQ, ASPM is also implemented by two HW modules, and can
	 * only be enabled when host supports it.
	 *
	 * And ASPM mechanism should be enabled when driver/firmware enters
	 * power save mode, without having heavy traffic. Because we've
	 * experienced some inter-operability issues that the link tends
	 * to enter L1 state on the fly even when driver is having high
	 * throughput. This is probably because the ASPM behavior slightly
	 * varies from different SOC.
	 */
	if (rtwpci->link_ctrl & PCI_EXP_LNKCTL_ASPM_L1)
		rtw_pci_aspm_set(rtwdev, enter);
}

static void rtw_pci_link_cfg(struct rtw_dev *rtwdev)
{
	struct rtw_pci *rtwpci = (struct rtw_pci *)rtwdev->priv;
	struct pci_dev *pdev = rtwpci->pdev;
	u16 link_ctrl;
	int ret;

	/* Though there is standard PCIE configuration space to set the
	 * link control register, but by Realtek's design, driver should
	 * check if host supports CLKREQ/ASPM to enable the HW module.
	 *
	 * These functions are implemented by two HW modules associated,
	 * one is responsible to access PCIE configuration space to
	 * follow the host settings, and another is in charge of doing
	 * CLKREQ/ASPM mechanisms, it is default disabled. Because sometimes
	 * the host does not support it, and due to some reasons or wrong
	 * settings (ex. CLKREQ# not Bi-Direction), it could lead to device
	 * loss if HW misbehaves on the link.
	 *
	 * Hence it's designed that driver should first check the PCIE
	 * configuration space is sync'ed and enabled, then driver can turn
	 * on the other module that is actually working on the mechanism.
	 */
	ret = pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &link_ctrl);
	if (ret) {
		rtw_err(rtwdev, "failed to read PCI cap, ret=%d\n", ret);
		return;
	}

	if (link_ctrl & PCI_EXP_LNKCTL_CLKREQ_EN)
		rtw_pci_clkreq_set(rtwdev, true);

	rtwpci->link_ctrl = link_ctrl;
}

static void rtw_pci_phy_cfg(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_intf_phy_para *para;
	u16 cut;
	u16 value;
	u16 offset;
	int i;

	cut = BIT(0) << rtwdev->hal.cut_version;

	for (i = 0; i < chip->intf_table->n_gen1_para; i++) {
		para = &chip->intf_table->gen1_para[i];
		if (!(para->cut_mask & cut))
			continue;
		if (para->offset == 0xffff)
			break;
		offset = para->offset;
		value = para->value;
		if (para->ip_sel == RTW_IP_SEL_PHY)
			rtw_mdio_write(rtwdev, offset, value, true);
		else
			rtw_dbi_write8(rtwdev, offset, value);
	}

	for (i = 0; i < chip->intf_table->n_gen2_para; i++) {
		para = &chip->intf_table->gen2_para[i];
		if (!(para->cut_mask & cut))
			continue;
		if (para->offset == 0xffff)
			break;
		offset = para->offset;
		value = para->value;
		if (para->ip_sel == RTW_IP_SEL_PHY)
			rtw_mdio_write(rtwdev, offset, value, false);
		else
			rtw_dbi_write8(rtwdev, offset, value);
	}

	rtw_pci_link_cfg(rtwdev);
}

static int rtw_pci_claim(struct rtw_dev *rtwdev, struct pci_dev *pdev)
{
	int ret;

	ret = pci_enable_device(pdev);
	if (ret) {
		rtw_err(rtwdev, "failed to enable pci device\n");
		return ret;
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, rtwdev->hw);
	SET_IEEE80211_DEV(rtwdev->hw, &pdev->dev);

	return 0;
}

static void rtw_pci_declaim(struct rtw_dev *rtwdev, struct pci_dev *pdev)
{
	pci_clear_master(pdev);
	pci_disable_device(pdev);
}

static int rtw_pci_setup_resource(struct rtw_dev *rtwdev, struct pci_dev *pdev)
{
	struct rtw_pci *rtwpci;
	int ret;

	rtwpci = (struct rtw_pci *)rtwdev->priv;
	rtwpci->pdev = pdev;

	/* after this driver can access to hw registers */
	ret = rtw_pci_io_mapping(rtwdev, pdev);
	if (ret) {
		rtw_err(rtwdev, "failed to request pci io region\n");
		goto err_out;
	}

	ret = rtw_pci_init(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to allocate pci resources\n");
		goto err_io_unmap;
	}

	return 0;

err_io_unmap:
	rtw_pci_io_unmapping(rtwdev, pdev);

err_out:
	return ret;
}

static void rtw_pci_destroy(struct rtw_dev *rtwdev, struct pci_dev *pdev)
{
	rtw_pci_deinit(rtwdev);
	rtw_pci_io_unmapping(rtwdev, pdev);
}

static struct rtw_hci_ops rtw_pci_ops = {
	.tx = rtw_pci_tx,
	.setup = rtw_pci_setup,
	.start = rtw_pci_start,
	.stop = rtw_pci_stop,
	.deep_ps = rtw_pci_deep_ps,
	.link_ps = rtw_pci_link_ps,

	.read8 = rtw_pci_read8,
	.read16 = rtw_pci_read16,
	.read32 = rtw_pci_read32,
	.write8 = rtw_pci_write8,
	.write16 = rtw_pci_write16,
	.write32 = rtw_pci_write32,
	.write_data_rsvd_page = rtw_pci_write_data_rsvd_page,
	.write_data_h2c = rtw_pci_write_data_h2c,
};

static int rtw_pci_request_irq(struct rtw_dev *rtwdev, struct pci_dev *pdev)
{
	unsigned int flags = PCI_IRQ_LEGACY;
	int ret;

	if (!rtw_disable_msi)
		flags |= PCI_IRQ_MSI;

	ret = pci_alloc_irq_vectors(pdev, 1, 1, flags);
	if (ret < 0) {
		rtw_err(rtwdev, "failed to alloc PCI irq vectors\n");
		return ret;
	}

	ret = devm_request_threaded_irq(rtwdev->dev, pdev->irq,
					rtw_pci_interrupt_handler,
					rtw_pci_interrupt_threadfn,
					IRQF_SHARED, KBUILD_MODNAME, rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to request irq %d\n", ret);
		pci_free_irq_vectors(pdev);
	}

	return ret;
}

static void rtw_pci_free_irq(struct rtw_dev *rtwdev, struct pci_dev *pdev)
{
	devm_free_irq(rtwdev->dev, pdev->irq, rtwdev);
	pci_free_irq_vectors(pdev);
}

static int rtw_pci_probe(struct pci_dev *pdev,
			 const struct pci_device_id *id)
{
	struct ieee80211_hw *hw;
	struct rtw_dev *rtwdev;
	int drv_data_size;
	int ret;

	drv_data_size = sizeof(struct rtw_dev) + sizeof(struct rtw_pci);
	hw = ieee80211_alloc_hw(drv_data_size, &rtw_ops);
	if (!hw) {
		dev_err(&pdev->dev, "failed to allocate hw\n");
		return -ENOMEM;
	}

	rtwdev = hw->priv;
	rtwdev->hw = hw;
	rtwdev->dev = &pdev->dev;
	rtwdev->chip = (struct rtw_chip_info *)id->driver_data;
	rtwdev->hci.ops = &rtw_pci_ops;
	rtwdev->hci.type = RTW_HCI_TYPE_PCIE;

	ret = rtw_core_init(rtwdev);
	if (ret)
		goto err_release_hw;

	rtw_dbg(rtwdev, RTW_DBG_PCI,
		"rtw88 pci probe: vendor=0x%4.04X device=0x%4.04X rev=%d\n",
		pdev->vendor, pdev->device, pdev->revision);

	ret = rtw_pci_claim(rtwdev, pdev);
	if (ret) {
		rtw_err(rtwdev, "failed to claim pci device\n");
		goto err_deinit_core;
	}

	ret = rtw_pci_setup_resource(rtwdev, pdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup pci resources\n");
		goto err_pci_declaim;
	}

	ret = rtw_chip_info_setup(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to setup chip information\n");
		goto err_destroy_pci;
	}

	rtw_pci_phy_cfg(rtwdev);

	ret = rtw_register_hw(rtwdev, hw);
	if (ret) {
		rtw_err(rtwdev, "failed to register hw\n");
		goto err_destroy_pci;
	}

	ret = rtw_pci_request_irq(rtwdev, pdev);
	if (ret) {
		ieee80211_unregister_hw(hw);
		goto err_destroy_pci;
	}

	return 0;

err_destroy_pci:
	rtw_pci_destroy(rtwdev, pdev);

err_pci_declaim:
	rtw_pci_declaim(rtwdev, pdev);

err_deinit_core:
	rtw_core_deinit(rtwdev);

err_release_hw:
	ieee80211_free_hw(hw);

	return ret;
}

static void rtw_pci_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct rtw_dev *rtwdev;
	struct rtw_pci *rtwpci;

	if (!hw)
		return;

	rtwdev = hw->priv;
	rtwpci = (struct rtw_pci *)rtwdev->priv;

	rtw_unregister_hw(rtwdev, hw);
	rtw_pci_disable_interrupt(rtwdev, rtwpci);
	rtw_pci_destroy(rtwdev, pdev);
	rtw_pci_declaim(rtwdev, pdev);
	rtw_pci_free_irq(rtwdev, pdev);
	rtw_core_deinit(rtwdev);
	ieee80211_free_hw(hw);
}

static const struct pci_device_id rtw_pci_id_table[] = {
#ifdef CONFIG_RTW88_8822BE
	{ RTK_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0xB822, rtw8822b_hw_spec) },
#endif
#ifdef CONFIG_RTW88_8822CE
	{ RTK_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0xC822, rtw8822c_hw_spec) },
#endif
	{},
};
MODULE_DEVICE_TABLE(pci, rtw_pci_id_table);

static struct pci_driver rtw_pci_driver = {
	.name = "rtw_pci",
	.id_table = rtw_pci_id_table,
	.probe = rtw_pci_probe,
	.remove = rtw_pci_remove,
};
module_pci_driver(rtw_pci_driver);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ac wireless PCI driver");
MODULE_LICENSE("Dual BSD/GPL");
