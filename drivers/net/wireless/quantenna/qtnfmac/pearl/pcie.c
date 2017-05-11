/*
 * Copyright (c) 2015-2016 Quantenna Communications, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/crc32.h>
#include <linux/spinlock.h>

#include "qtn_hw_ids.h"
#include "pcie_bus_priv.h"
#include "core.h"
#include "bus.h"
#include "debug.h"

static bool use_msi = true;
module_param(use_msi, bool, 0644);
MODULE_PARM_DESC(use_msi, "set 0 to use legacy interrupt");

static unsigned int tx_bd_size_param = 256;
module_param(tx_bd_size_param, uint, 0644);
MODULE_PARM_DESC(tx_bd_size_param, "Tx descriptors queue size");

static unsigned int rx_bd_size_param = 256;
module_param(rx_bd_size_param, uint, 0644);
MODULE_PARM_DESC(rx_bd_size_param, "Rx descriptors queue size");

static unsigned int rx_bd_reserved_param = 16;
module_param(rx_bd_reserved_param, uint, 0644);
MODULE_PARM_DESC(rx_bd_reserved_param, "Reserved RX descriptors");

static u8 flashboot = 1;
module_param(flashboot, byte, 0644);
MODULE_PARM_DESC(flashboot, "set to 0 to use FW binary file on FS");

#define DRV_NAME	"qtnfmac_pearl_pcie"

static inline void qtnf_non_posted_write(u32 val, void __iomem *basereg)
{
	writel(val, basereg);

	/* flush posted write */
	readl(basereg);
}

static inline void qtnf_init_hdp_irqs(struct qtnf_pcie_bus_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->irq_lock, flags);
	priv->pcie_irq_mask = (PCIE_HDP_INT_RX_BITS | PCIE_HDP_INT_TX_BITS);
	spin_unlock_irqrestore(&priv->irq_lock, flags);
}

static inline void qtnf_enable_hdp_irqs(struct qtnf_pcie_bus_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->irq_lock, flags);
	writel(priv->pcie_irq_mask, PCIE_HDP_INT_EN(priv->pcie_reg_base));
	spin_unlock_irqrestore(&priv->irq_lock, flags);
}

static inline void qtnf_disable_hdp_irqs(struct qtnf_pcie_bus_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->irq_lock, flags);
	writel(0x0, PCIE_HDP_INT_EN(priv->pcie_reg_base));
	spin_unlock_irqrestore(&priv->irq_lock, flags);
}

static inline void qtnf_en_rxdone_irq(struct qtnf_pcie_bus_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->irq_lock, flags);
	priv->pcie_irq_mask |= PCIE_HDP_INT_RX_BITS;
	writel(priv->pcie_irq_mask, PCIE_HDP_INT_EN(priv->pcie_reg_base));
	spin_unlock_irqrestore(&priv->irq_lock, flags);
}

static inline void qtnf_dis_rxdone_irq(struct qtnf_pcie_bus_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->irq_lock, flags);
	priv->pcie_irq_mask &= ~PCIE_HDP_INT_RX_BITS;
	writel(priv->pcie_irq_mask, PCIE_HDP_INT_EN(priv->pcie_reg_base));
	spin_unlock_irqrestore(&priv->irq_lock, flags);
}

static inline void qtnf_en_txdone_irq(struct qtnf_pcie_bus_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->irq_lock, flags);
	priv->pcie_irq_mask |= PCIE_HDP_INT_TX_BITS;
	writel(priv->pcie_irq_mask, PCIE_HDP_INT_EN(priv->pcie_reg_base));
	spin_unlock_irqrestore(&priv->irq_lock, flags);
}

static inline void qtnf_dis_txdone_irq(struct qtnf_pcie_bus_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->irq_lock, flags);
	priv->pcie_irq_mask &= ~PCIE_HDP_INT_TX_BITS;
	writel(priv->pcie_irq_mask, PCIE_HDP_INT_EN(priv->pcie_reg_base));
	spin_unlock_irqrestore(&priv->irq_lock, flags);
}

static int qtnf_pcie_init_irq(struct qtnf_pcie_bus_priv *priv)
{
	struct pci_dev *pdev = priv->pdev;

	/* fall back to legacy INTx interrupts by default */
	priv->msi_enabled = 0;

	/* check if MSI capability is available */
	if (use_msi) {
		if (!pci_enable_msi(pdev)) {
			pr_debug("MSI interrupt enabled\n");
			priv->msi_enabled = 1;
		} else {
			pr_warn("failed to enable MSI interrupts");
		}
	}

	if (!priv->msi_enabled) {
		pr_warn("legacy PCIE interrupts enabled\n");
		pci_intx(pdev, 1);
	}

	return 0;
}

static void qtnf_deassert_intx(struct qtnf_pcie_bus_priv *priv)
{
	void __iomem *reg = priv->sysctl_bar + PEARL_PCIE_CFG0_OFFSET;
	u32 cfg;

	cfg = readl(reg);
	cfg &= ~PEARL_ASSERT_INTX;
	qtnf_non_posted_write(cfg, reg);
}

static void qtnf_ipc_gen_ep_int(void *arg)
{
	const struct qtnf_pcie_bus_priv *priv = arg;
	const u32 data = QTN_PEARL_IPC_IRQ_WORD(QTN_PEARL_LHOST_IPC_IRQ);
	void __iomem *reg = priv->sysctl_bar +
			    QTN_PEARL_SYSCTL_LHOST_IRQ_OFFSET;

	qtnf_non_posted_write(data, reg);
}

static void __iomem *qtnf_map_bar(struct qtnf_pcie_bus_priv *priv, u8 index)
{
	void __iomem *vaddr;
	dma_addr_t busaddr;
	size_t len;
	int ret;

	ret = pcim_iomap_regions(priv->pdev, 1 << index, DRV_NAME);
	if (ret)
		return IOMEM_ERR_PTR(ret);

	busaddr = pci_resource_start(priv->pdev, index);
	vaddr = pcim_iomap_table(priv->pdev)[index];
	len = pci_resource_len(priv->pdev, index);

	pr_debug("BAR%u vaddr=0x%p busaddr=%pad len=%u\n",
		 index, vaddr, &busaddr, (int)len);

	return vaddr;
}

static void qtnf_pcie_control_rx_callback(void *arg, const u8 *buf, size_t len)
{
	struct qtnf_pcie_bus_priv *priv = arg;
	struct qtnf_bus *bus = pci_get_drvdata(priv->pdev);
	struct sk_buff *skb;

	if (unlikely(len == 0)) {
		pr_warn("zero length packet received\n");
		return;
	}

	skb = __dev_alloc_skb(len, GFP_KERNEL);

	if (unlikely(!skb)) {
		pr_err("failed to allocate skb\n");
		return;
	}

	memcpy(skb_put(skb, len), buf, len);

	qtnf_trans_handle_rx_ctl_packet(bus, skb);
}

static int qtnf_pcie_init_shm_ipc(struct qtnf_pcie_bus_priv *priv)
{
	struct qtnf_shm_ipc_region __iomem *ipc_tx_reg;
	struct qtnf_shm_ipc_region __iomem *ipc_rx_reg;
	const struct qtnf_shm_ipc_int ipc_int = { qtnf_ipc_gen_ep_int, priv };
	const struct qtnf_shm_ipc_rx_callback rx_callback = {
					qtnf_pcie_control_rx_callback, priv };

	ipc_tx_reg = &priv->bda->bda_shm_reg1;
	ipc_rx_reg = &priv->bda->bda_shm_reg2;

	qtnf_shm_ipc_init(&priv->shm_ipc_ep_in, QTNF_SHM_IPC_OUTBOUND,
			  ipc_tx_reg, priv->workqueue,
			  &ipc_int, &rx_callback);
	qtnf_shm_ipc_init(&priv->shm_ipc_ep_out, QTNF_SHM_IPC_INBOUND,
			  ipc_rx_reg, priv->workqueue,
			  &ipc_int, &rx_callback);

	return 0;
}

static void qtnf_pcie_free_shm_ipc(struct qtnf_pcie_bus_priv *priv)
{
	qtnf_shm_ipc_free(&priv->shm_ipc_ep_in);
	qtnf_shm_ipc_free(&priv->shm_ipc_ep_out);
}

static int qtnf_pcie_init_memory(struct qtnf_pcie_bus_priv *priv)
{
	int ret;

	priv->sysctl_bar = qtnf_map_bar(priv, QTN_SYSCTL_BAR);
	if (IS_ERR_OR_NULL(priv->sysctl_bar)) {
		pr_err("failed to map BAR%u\n", QTN_SYSCTL_BAR);
		return ret;
	}

	priv->dmareg_bar = qtnf_map_bar(priv, QTN_DMA_BAR);
	if (IS_ERR_OR_NULL(priv->dmareg_bar)) {
		pr_err("failed to map BAR%u\n", QTN_DMA_BAR);
		return ret;
	}

	priv->epmem_bar = qtnf_map_bar(priv, QTN_SHMEM_BAR);
	if (IS_ERR_OR_NULL(priv->epmem_bar)) {
		pr_err("failed to map BAR%u\n", QTN_SHMEM_BAR);
		return ret;
	}

	priv->pcie_reg_base = priv->dmareg_bar;
	priv->bda = priv->epmem_bar;
	writel(priv->msi_enabled, &priv->bda->bda_rc_msi_enabled);

	return 0;
}

static int
qtnf_pcie_init_dma_mask(struct qtnf_pcie_bus_priv *priv, u64 dma_mask)
{
	int ret;

	ret = dma_supported(&priv->pdev->dev, dma_mask);
	if (!ret) {
		pr_err("DMA mask %llu not supported\n", dma_mask);
		return ret;
	}

	ret = pci_set_dma_mask(priv->pdev, dma_mask);
	if (ret) {
		pr_err("failed to set DMA mask %llu\n", dma_mask);
		return ret;
	}

	ret = pci_set_consistent_dma_mask(priv->pdev, dma_mask);
	if (ret) {
		pr_err("failed to set consistent DMA mask %llu\n", dma_mask);
		return ret;
	}

	return ret;
}

static void qtnf_tune_pcie_mps(struct qtnf_pcie_bus_priv *priv)
{
	struct pci_dev *pdev = priv->pdev;
	struct pci_dev *parent;
	int mps_p, mps_o, mps_m, mps;
	int ret;

	/* current mps */
	mps_o = pcie_get_mps(pdev);

	/* maximum supported mps */
	mps_m = 128 << pdev->pcie_mpss;

	/* suggested new mps value */
	mps = mps_m;

	if (pdev->bus && pdev->bus->self) {
		/* parent (bus) mps */
		parent = pdev->bus->self;

		if (pci_is_pcie(parent)) {
			mps_p = pcie_get_mps(parent);
			mps = min(mps_m, mps_p);
		}
	}

	ret = pcie_set_mps(pdev, mps);
	if (ret) {
		pr_err("failed to set mps to %d, keep using current %d\n",
		       mps, mps_o);
		priv->mps = mps_o;
		return;
	}

	pr_debug("set mps to %d (was %d, max %d)\n", mps, mps_o, mps_m);
	priv->mps = mps;
}

static int qtnf_is_state(__le32 __iomem *reg, u32 state)
{
	u32 s = readl(reg);

	return s & state;
}

static void qtnf_set_state(__le32 __iomem *reg, u32 state)
{
	u32 s = readl(reg);

	qtnf_non_posted_write(state | s, reg);
}

static void qtnf_clear_state(__le32 __iomem *reg, u32 state)
{
	u32 s = readl(reg);

	qtnf_non_posted_write(s & ~state, reg);
}

static int qtnf_poll_state(__le32 __iomem *reg, u32 state, u32 delay_in_ms)
{
	u32 timeout = 0;

	while ((qtnf_is_state(reg, state) == 0)) {
		usleep_range(1000, 1200);
		if (++timeout > delay_in_ms)
			return -1;
	}

	return 0;
}

static int alloc_skb_array(struct qtnf_pcie_bus_priv *priv)
{
	struct sk_buff **vaddr;
	int len;

	len = priv->tx_bd_num * sizeof(*priv->tx_skb) +
		priv->rx_bd_num * sizeof(*priv->rx_skb);
	vaddr = devm_kzalloc(&priv->pdev->dev, len, GFP_KERNEL);

	if (!vaddr)
		return -ENOMEM;

	priv->tx_skb = vaddr;

	vaddr += priv->tx_bd_num;
	priv->rx_skb = vaddr;

	return 0;
}

static int alloc_bd_table(struct qtnf_pcie_bus_priv *priv)
{
	dma_addr_t paddr;
	void *vaddr;
	int len;

	len = priv->tx_bd_num * sizeof(struct qtnf_tx_bd) +
		priv->rx_bd_num * sizeof(struct qtnf_rx_bd);

	vaddr = dmam_alloc_coherent(&priv->pdev->dev, len, &paddr, GFP_KERNEL);
	if (!vaddr)
		return -ENOMEM;

	/* tx bd */

	memset(vaddr, 0, len);

	priv->bd_table_vaddr = vaddr;
	priv->bd_table_paddr = paddr;
	priv->bd_table_len = len;

	priv->tx_bd_vbase = vaddr;
	priv->tx_bd_pbase = paddr;

	pr_debug("TX descriptor table: vaddr=0x%p paddr=%pad\n", vaddr, &paddr);

	priv->tx_bd_reclaim_start = 0;
	priv->tx_bd_index = 0;
	priv->tx_queue_len = 0;

	/* rx bd */

	vaddr = ((struct qtnf_tx_bd *)vaddr) + priv->tx_bd_num;
	paddr += priv->tx_bd_num * sizeof(struct qtnf_tx_bd);

	priv->rx_bd_vbase = vaddr;
	priv->rx_bd_pbase = paddr;

	writel(QTN_HOST_LO32(paddr),
	       PCIE_HDP_TX_HOST_Q_BASE_L(priv->pcie_reg_base));
	writel(QTN_HOST_HI32(paddr),
	       PCIE_HDP_TX_HOST_Q_BASE_H(priv->pcie_reg_base));
	writel(priv->rx_bd_num | (sizeof(struct qtnf_rx_bd)) << 16,
	       PCIE_HDP_TX_HOST_Q_SZ_CTRL(priv->pcie_reg_base));

	priv->hw_txproc_wr_ptr = priv->rx_bd_num - rx_bd_reserved_param;

	writel(priv->hw_txproc_wr_ptr,
	       PCIE_HDP_TX_HOST_Q_WR_PTR(priv->pcie_reg_base));

	pr_debug("RX descriptor table: vaddr=0x%p paddr=%pad\n", vaddr, &paddr);

	priv->rx_bd_index = 0;

	return 0;
}

static int skb2rbd_attach(struct qtnf_pcie_bus_priv *priv, u16 rx_bd_index)
{
	struct qtnf_rx_bd *rxbd;
	struct sk_buff *skb;
	dma_addr_t paddr;

	skb = __dev_alloc_skb(SKB_BUF_SIZE + NET_IP_ALIGN,
			      GFP_ATOMIC);
	if (!skb) {
		priv->rx_skb[rx_bd_index] = NULL;
		return -ENOMEM;
	}

	priv->rx_skb[rx_bd_index] = skb;

	skb_reserve(skb, NET_IP_ALIGN);

	rxbd = &priv->rx_bd_vbase[rx_bd_index];

	paddr = pci_map_single(priv->pdev, skb->data,
			       SKB_BUF_SIZE, PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(priv->pdev, paddr)) {
		pr_err("skb DMA mapping error: %pad\n", &paddr);
		return -ENOMEM;
	}

	writel(QTN_HOST_LO32(paddr),
	       PCIE_HDP_HHBM_BUF_PTR(priv->pcie_reg_base));
	writel(QTN_HOST_HI32(paddr),
	       PCIE_HDP_HHBM_BUF_PTR_H(priv->pcie_reg_base));

	/* keep rx skb paddrs in rx buffer descriptors for cleanup purposes */
	rxbd->addr = cpu_to_le32(QTN_HOST_LO32(paddr));
	rxbd->addr_h = cpu_to_le32(QTN_HOST_HI32(paddr));

	rxbd->info = 0x0;

	return 0;
}

static int alloc_rx_buffers(struct qtnf_pcie_bus_priv *priv)
{
	u16 i;
	int ret = 0;

	memset(priv->rx_bd_vbase, 0x0,
	       priv->rx_bd_num * sizeof(struct qtnf_rx_bd));

	for (i = 0; i < priv->rx_bd_num; i++) {
		ret = skb2rbd_attach(priv, i);
		if (ret)
			break;
	}

	return ret;
}

/* all rx/tx activity should have ceased before calling this function */
static void free_xfer_buffers(void *data)
{
	struct qtnf_pcie_bus_priv *priv = (struct qtnf_pcie_bus_priv *)data;
	struct qtnf_rx_bd *rxbd;
	dma_addr_t paddr;
	int i;

	/* free rx buffers */
	for (i = 0; i < priv->rx_bd_num; i++) {
		if (priv->rx_skb[i]) {
			rxbd = &priv->rx_bd_vbase[i];
			paddr = QTN_HOST_ADDR(le32_to_cpu(rxbd->addr_h),
					      le32_to_cpu(rxbd->addr));
			pci_unmap_single(priv->pdev, paddr, SKB_BUF_SIZE,
					 PCI_DMA_FROMDEVICE);

			dev_kfree_skb_any(priv->rx_skb[i]);
		}
	}

	/* free tx buffers */
	for (i = 0; i < priv->tx_bd_num; i++) {
		if (priv->tx_skb[i]) {
			dev_kfree_skb_any(priv->tx_skb[i]);
			priv->tx_skb[i] = NULL;
		}
	}
}

static int qtnf_pcie_init_xfer(struct qtnf_pcie_bus_priv *priv)
{
	int ret;

	priv->tx_bd_num = tx_bd_size_param;
	priv->rx_bd_num = rx_bd_size_param;

	ret = alloc_skb_array(priv);
	if (ret) {
		pr_err("failed to allocate skb array\n");
		return ret;
	}

	ret = alloc_bd_table(priv);
	if (ret) {
		pr_err("failed to allocate bd table\n");
		return ret;
	}

	ret = alloc_rx_buffers(priv);
	if (ret) {
		pr_err("failed to allocate rx buffers\n");
		return ret;
	}

	return ret;
}

static int qtnf_pcie_data_tx_reclaim(struct qtnf_pcie_bus_priv *priv)
{
	struct qtnf_tx_bd *txbd;
	struct sk_buff *skb;
	dma_addr_t paddr;
	int last_sent;
	int count;
	int i;

	last_sent = readl(PCIE_HDP_RX0DMA_CNT(priv->pcie_reg_base))
			% priv->tx_bd_num;
	i = priv->tx_bd_reclaim_start;
	count = 0;

	while (i != last_sent) {
		skb = priv->tx_skb[i];
		if (!skb)
			break;

		txbd = &priv->tx_bd_vbase[i];
		paddr = QTN_HOST_ADDR(le32_to_cpu(txbd->addr_h),
				      le32_to_cpu(txbd->addr));
		pci_unmap_single(priv->pdev, paddr, skb->len, PCI_DMA_TODEVICE);

		if (skb->dev) {
			skb->dev->stats.tx_packets++;
			skb->dev->stats.tx_bytes += skb->len;

			if (netif_queue_stopped(skb->dev))
				netif_wake_queue(skb->dev);
		}

		dev_kfree_skb_any(skb);
		priv->tx_skb[i] = NULL;
		priv->tx_queue_len--;
		count++;

		if (++i >= priv->tx_bd_num)
			i = 0;
	}

	priv->tx_bd_reclaim_start = i;
	priv->tx_reclaim_done += count;
	priv->tx_reclaim_req++;

	return count;
}

static bool qtnf_tx_queue_ready(struct qtnf_pcie_bus_priv *priv)
{
	if (priv->tx_queue_len >= priv->tx_bd_num - 1) {
		pr_err_ratelimited("reclaim full Tx queue\n");
		qtnf_pcie_data_tx_reclaim(priv);

		if (priv->tx_queue_len >= priv->tx_bd_num - 1) {
			priv->tx_full_count++;
			return false;
		}
	}

	return true;
}

static int qtnf_pcie_data_tx(struct qtnf_bus *bus, struct sk_buff *skb)
{
	struct qtnf_pcie_bus_priv *priv = (void *)get_bus_priv(bus);
	dma_addr_t txbd_paddr, skb_paddr;
	struct qtnf_tx_bd *txbd;
	unsigned long flags;
	int len, i;
	u32 info;
	int ret = 0;

	spin_lock_irqsave(&priv->tx_lock, flags);

	priv->tx_done_count++;

	if (!qtnf_tx_queue_ready(priv)) {
		if (skb->dev)
			netif_stop_queue(skb->dev);

		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return NETDEV_TX_BUSY;
	}

	i = priv->tx_bd_index;
	priv->tx_skb[i] = skb;
	len = skb->len;

	skb_paddr = pci_map_single(priv->pdev, skb->data,
				   skb->len, PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(priv->pdev, skb_paddr)) {
		pr_err("skb DMA mapping error: %pad\n", &skb_paddr);
		ret = -ENOMEM;
		goto tx_done;
	}

	txbd = &priv->tx_bd_vbase[i];
	txbd->addr = cpu_to_le32(QTN_HOST_LO32(skb_paddr));
	txbd->addr_h = cpu_to_le32(QTN_HOST_HI32(skb_paddr));

	info = (len & QTN_PCIE_TX_DESC_LEN_MASK) << QTN_PCIE_TX_DESC_LEN_SHIFT;
	txbd->info = cpu_to_le32(info);

	/* sync up all descriptor updates before passing them to EP */
	dma_wmb();

	/* write new TX descriptor to PCIE_RX_FIFO on EP */
	txbd_paddr = priv->tx_bd_pbase + i * sizeof(struct qtnf_tx_bd);
	writel(QTN_HOST_LO32(txbd_paddr),
	       PCIE_HDP_HOST_WR_DESC0(priv->pcie_reg_base));
	writel(QTN_HOST_HI32(txbd_paddr),
	       PCIE_HDP_HOST_WR_DESC0_H(priv->pcie_reg_base));

	if (++i >= priv->tx_bd_num)
		i = 0;

	priv->tx_bd_index = i;
	priv->tx_queue_len++;

tx_done:
	if (ret && skb) {
		pr_err_ratelimited("drop skb\n");
		if (skb->dev)
			skb->dev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
	}

	spin_unlock_irqrestore(&priv->tx_lock, flags);

	return NETDEV_TX_OK;
}

static int qtnf_pcie_control_tx(struct qtnf_bus *bus, struct sk_buff *skb)
{
	struct qtnf_pcie_bus_priv *priv = (void *)get_bus_priv(bus);

	return qtnf_shm_ipc_send(&priv->shm_ipc_ep_in, skb->data, skb->len);
}

static irqreturn_t qtnf_interrupt(int irq, void *data)
{
	struct qtnf_bus *bus = (struct qtnf_bus *)data;
	struct qtnf_pcie_bus_priv *priv = (void *)get_bus_priv(bus);
	u32 status;

	priv->pcie_irq_count++;
	status = readl(PCIE_HDP_INT_STATUS(priv->pcie_reg_base));

	qtnf_shm_ipc_irq_handler(&priv->shm_ipc_ep_in);
	qtnf_shm_ipc_irq_handler(&priv->shm_ipc_ep_out);

	if (!(status & priv->pcie_irq_mask))
		goto irq_done;

	if (status & PCIE_HDP_INT_RX_BITS) {
		priv->pcie_irq_rx_count++;
		qtnf_dis_rxdone_irq(priv);
		napi_schedule(&bus->mux_napi);
	}

	if (status & PCIE_HDP_INT_TX_BITS) {
		priv->pcie_irq_tx_count++;
		qtnf_dis_txdone_irq(priv);
		tasklet_hi_schedule(&priv->reclaim_tq);
	}

irq_done:
	/* H/W workaround: clean all bits, not only enabled */
	qtnf_non_posted_write(~0U, PCIE_HDP_INT_STATUS(priv->pcie_reg_base));

	if (!priv->msi_enabled)
		qtnf_deassert_intx(priv);

	return IRQ_HANDLED;
}

static inline void hw_txproc_wr_ptr_inc(struct qtnf_pcie_bus_priv *priv)
{
	u32 index;

	index = priv->hw_txproc_wr_ptr;

	if (++index >= priv->rx_bd_num)
		index = 0;

	priv->hw_txproc_wr_ptr = index;
}

static int qtnf_rx_poll(struct napi_struct *napi, int budget)
{
	struct qtnf_bus *bus = container_of(napi, struct qtnf_bus, mux_napi);
	struct qtnf_pcie_bus_priv *priv = (void *)get_bus_priv(bus);
	struct net_device *ndev = NULL;
	struct sk_buff *skb = NULL;
	int processed = 0;
	struct qtnf_rx_bd *rxbd;
	dma_addr_t skb_paddr;
	u32 descw;
	u16 index;
	int ret;

	index = priv->rx_bd_index;
	rxbd = &priv->rx_bd_vbase[index];

	descw = le32_to_cpu(rxbd->info);

	while ((descw & QTN_TXDONE_MASK) && (processed < budget)) {
		skb = priv->rx_skb[index];

		if (likely(skb)) {
			skb_put(skb, QTN_GET_LEN(descw));

			skb_paddr = QTN_HOST_ADDR(le32_to_cpu(rxbd->addr_h),
						  le32_to_cpu(rxbd->addr));
			pci_unmap_single(priv->pdev, skb_paddr, SKB_BUF_SIZE,
					 PCI_DMA_FROMDEVICE);

			ndev = qtnf_classify_skb(bus, skb);
			if (likely(ndev)) {
				ndev->stats.rx_packets++;
				ndev->stats.rx_bytes += skb->len;

				skb->protocol = eth_type_trans(skb, ndev);
				netif_receive_skb(skb);
			} else {
				pr_debug("drop untagged skb\n");
				bus->mux_dev.stats.rx_dropped++;
				dev_kfree_skb_any(skb);
			}

			processed++;
		} else {
			pr_err("missing rx_skb[%d]\n", index);
		}

		/* attached rx buffer is passed upstream: map a new one */
		ret = skb2rbd_attach(priv, index);
		if (likely(!ret)) {
			if (++index >= priv->rx_bd_num)
				index = 0;

			priv->rx_bd_index = index;
			hw_txproc_wr_ptr_inc(priv);

			rxbd = &priv->rx_bd_vbase[index];
			descw = le32_to_cpu(rxbd->info);
		} else {
			pr_err("failed to allocate new rx_skb[%d]\n", index);
			break;
		}

		writel(priv->hw_txproc_wr_ptr,
		       PCIE_HDP_TX_HOST_Q_WR_PTR(priv->pcie_reg_base));
	}

	if (processed < budget) {
		napi_complete(napi);
		qtnf_en_rxdone_irq(priv);
	}

	return processed;
}

static void
qtnf_pcie_data_tx_timeout(struct qtnf_bus *bus, struct net_device *ndev)
{
	struct qtnf_pcie_bus_priv *priv = (void *)get_bus_priv(bus);

	tasklet_hi_schedule(&priv->reclaim_tq);
}

static void qtnf_pcie_data_rx_start(struct qtnf_bus *bus)
{
	struct qtnf_pcie_bus_priv *priv = (void *)get_bus_priv(bus);

	qtnf_enable_hdp_irqs(priv);
	napi_enable(&bus->mux_napi);
}

static void qtnf_pcie_data_rx_stop(struct qtnf_bus *bus)
{
	struct qtnf_pcie_bus_priv *priv = (void *)get_bus_priv(bus);

	napi_disable(&bus->mux_napi);
	qtnf_disable_hdp_irqs(priv);
}

static const struct qtnf_bus_ops qtnf_pcie_bus_ops = {
	/* control path methods */
	.control_tx	= qtnf_pcie_control_tx,

	/* data path methods */
	.data_tx		= qtnf_pcie_data_tx,
	.data_tx_timeout	= qtnf_pcie_data_tx_timeout,
	.data_rx_start		= qtnf_pcie_data_rx_start,
	.data_rx_stop		= qtnf_pcie_data_rx_stop,
};

static int qtnf_ep_fw_send(struct qtnf_pcie_bus_priv *priv, uint32_t size,
			   int blk, const u8 *pblk, const u8 *fw)
{
	struct pci_dev *pdev = priv->pdev;
	struct qtnf_bus *bus = pci_get_drvdata(pdev);

	struct qtnf_pcie_fw_hdr *hdr;
	u8 *pdata;

	int hds = sizeof(*hdr);
	struct sk_buff *skb = NULL;
	int len = 0;
	int ret;

	skb = __dev_alloc_skb(QTN_PCIE_FW_BUFSZ, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	skb->len = QTN_PCIE_FW_BUFSZ;
	skb->dev = NULL;

	hdr = (struct qtnf_pcie_fw_hdr *)skb->data;
	memcpy(hdr->boardflg, QTN_PCIE_BOARDFLG, strlen(QTN_PCIE_BOARDFLG));
	hdr->fwsize = cpu_to_le32(size);
	hdr->seqnum = cpu_to_le32(blk);

	if (blk)
		hdr->type = cpu_to_le32(QTN_FW_DSUB);
	else
		hdr->type = cpu_to_le32(QTN_FW_DBEGIN);

	pdata = skb->data + hds;

	len = QTN_PCIE_FW_BUFSZ - hds;
	if (pblk >= (fw + size - len)) {
		len = fw + size - pblk;
		hdr->type = cpu_to_le32(QTN_FW_DEND);
	}

	hdr->pktlen = cpu_to_le32(len);
	memcpy(pdata, pblk, len);
	hdr->crc = cpu_to_le32(~crc32(0, pdata, len));

	ret = qtnf_pcie_data_tx(bus, skb);

	return (ret == NETDEV_TX_OK) ? len : 0;
}

static int
qtnf_ep_fw_load(struct qtnf_pcie_bus_priv *priv, const u8 *fw, u32 fw_size)
{
	int blk_size = QTN_PCIE_FW_BUFSZ - sizeof(struct qtnf_pcie_fw_hdr);
	int blk_count = fw_size / blk_size + ((fw_size % blk_size) ? 1 : 0);
	const u8 *pblk = fw;
	int threshold = 0;
	int blk = 0;
	int len;

	pr_debug("FW upload started: fw_addr=0x%p size=%d\n", fw, fw_size);

	while (blk < blk_count) {
		if (++threshold > 10000) {
			pr_err("FW upload failed: too many retries\n");
			return -ETIMEDOUT;
		}

		len = qtnf_ep_fw_send(priv, fw_size, blk, pblk, fw);
		if (len <= 0)
			continue;

		if (!((blk + 1) & QTN_PCIE_FW_DLMASK) ||
		    (blk == (blk_count - 1))) {
			qtnf_set_state(&priv->bda->bda_rc_state,
				       QTN_RC_FW_SYNC);
			if (qtnf_poll_state(&priv->bda->bda_ep_state,
					    QTN_EP_FW_SYNC,
					    QTN_FW_DL_TIMEOUT_MS)) {
				pr_err("FW upload failed: SYNC timed out\n");
				return -ETIMEDOUT;
			}

			qtnf_clear_state(&priv->bda->bda_ep_state,
					 QTN_EP_FW_SYNC);

			if (qtnf_is_state(&priv->bda->bda_ep_state,
					  QTN_EP_FW_RETRY)) {
				if (blk == (blk_count - 1)) {
					int last_round =
						blk_count & QTN_PCIE_FW_DLMASK;
					blk -= last_round;
					pblk -= ((last_round - 1) *
						blk_size + len);
				} else {
					blk -= QTN_PCIE_FW_DLMASK;
					pblk -= QTN_PCIE_FW_DLMASK * blk_size;
				}

				qtnf_clear_state(&priv->bda->bda_ep_state,
						 QTN_EP_FW_RETRY);

				pr_warn("FW upload retry: block #%d\n", blk);
				continue;
			}

			qtnf_pcie_data_tx_reclaim(priv);
		}

		pblk += len;
		blk++;
	}

	pr_debug("FW upload completed: totally sent %d blocks\n", blk);
	return 0;
}

static void qtnf_firmware_load(const struct firmware *fw, void *context)
{
	struct qtnf_pcie_bus_priv *priv = (void *)context;
	struct pci_dev *pdev = priv->pdev;
	struct qtnf_bus *bus = pci_get_drvdata(pdev);
	int ret;

	if (!fw) {
		pr_err("failed to get firmware %s\n", bus->fwname);
		goto fw_load_err;
	}

	ret = qtnf_ep_fw_load(priv, fw->data, fw->size);
	if (ret) {
		pr_err("FW upload error\n");
		goto fw_load_err;
	}

	if (qtnf_poll_state(&priv->bda->bda_ep_state, QTN_EP_FW_DONE,
			    QTN_FW_DL_TIMEOUT_MS)) {
		pr_err("FW bringup timed out\n");
		goto fw_load_err;
	}

	bus->fw_state = QTNF_FW_STATE_FW_DNLD_DONE;
	pr_info("firmware is up and running\n");

fw_load_err:

	if (fw)
		release_firmware(fw);

	complete(&bus->request_firmware_complete);
}

static int qtnf_bringup_fw(struct qtnf_bus *bus)
{
	struct qtnf_pcie_bus_priv *priv = (void *)get_bus_priv(bus);
	struct pci_dev *pdev = priv->pdev;
	int ret;
	u32 state = QTN_RC_FW_LOADRDY | QTN_RC_FW_QLINK;

	if (flashboot)
		state |= QTN_RC_FW_FLASHBOOT;

	qtnf_set_state(&priv->bda->bda_rc_state, state);

	if (qtnf_poll_state(&priv->bda->bda_ep_state, QTN_EP_FW_LOADRDY,
			    QTN_FW_DL_TIMEOUT_MS)) {
		pr_err("card is not ready\n");
		return -ETIMEDOUT;
	}

	qtnf_clear_state(&priv->bda->bda_ep_state, QTN_EP_FW_LOADRDY);

	if (flashboot) {
		pr_info("Booting FW from flash\n");

		if (!qtnf_poll_state(&priv->bda->bda_ep_state, QTN_EP_FW_DONE,
				     QTN_FW_DL_TIMEOUT_MS))
			bus->fw_state = QTNF_FW_STATE_FW_DNLD_DONE;

		return 0;
	}

	pr_info("starting firmware upload: %s\n", bus->fwname);

	ret = request_firmware_nowait(THIS_MODULE, 1, bus->fwname, &pdev->dev,
				      GFP_KERNEL, priv, qtnf_firmware_load);
	if (ret < 0)
		pr_err("request_firmware_nowait error %d\n", ret);
	else
		ret = 1;

	return ret;
}

static void qtnf_reclaim_tasklet_fn(unsigned long data)
{
	struct qtnf_pcie_bus_priv *priv = (void *)data;
	unsigned long flags;

	spin_lock_irqsave(&priv->tx_lock, flags);
	qtnf_pcie_data_tx_reclaim(priv);
	spin_unlock_irqrestore(&priv->tx_lock, flags);
	qtnf_en_txdone_irq(priv);
}

static int qtnf_dbg_mps_show(struct seq_file *s, void *data)
{
	struct qtnf_bus *bus = dev_get_drvdata(s->private);
	struct qtnf_pcie_bus_priv *priv = get_bus_priv(bus);

	seq_printf(s, "%d\n", priv->mps);

	return 0;
}

static int qtnf_dbg_msi_show(struct seq_file *s, void *data)
{
	struct qtnf_bus *bus = dev_get_drvdata(s->private);
	struct qtnf_pcie_bus_priv *priv = get_bus_priv(bus);

	seq_printf(s, "%u\n", priv->msi_enabled);

	return 0;
}

static int qtnf_dbg_irq_stats(struct seq_file *s, void *data)
{
	struct qtnf_bus *bus = dev_get_drvdata(s->private);
	struct qtnf_pcie_bus_priv *priv = get_bus_priv(bus);

	seq_printf(s, "pcie_irq_count(%u)\n", priv->pcie_irq_count);
	seq_printf(s, "pcie_irq_tx_count(%u)\n", priv->pcie_irq_tx_count);
	seq_printf(s, "pcie_irq_rx_count(%u)\n", priv->pcie_irq_rx_count);

	return 0;
}

static int qtnf_dbg_hdp_stats(struct seq_file *s, void *data)
{
	struct qtnf_bus *bus = dev_get_drvdata(s->private);
	struct qtnf_pcie_bus_priv *priv = get_bus_priv(bus);

	seq_printf(s, "tx_full_count(%u)\n", priv->tx_full_count);
	seq_printf(s, "tx_done_count(%u)\n", priv->tx_done_count);
	seq_printf(s, "tx_reclaim_done(%u)\n", priv->tx_reclaim_done);
	seq_printf(s, "tx_reclaim_req(%u)\n", priv->tx_reclaim_req);
	seq_printf(s, "tx_bd_reclaim_start(%u)\n", priv->tx_bd_reclaim_start);
	seq_printf(s, "tx_bd_index(%u)\n", priv->tx_bd_index);
	seq_printf(s, "rx_bd_index(%u)\n", priv->rx_bd_index);
	seq_printf(s, "tx_queue_len(%u)\n", priv->tx_queue_len);

	return 0;
}

static int qtnf_dbg_shm_stats(struct seq_file *s, void *data)
{
	struct qtnf_bus *bus = dev_get_drvdata(s->private);
	struct qtnf_pcie_bus_priv *priv = get_bus_priv(bus);

	seq_printf(s, "shm_ipc_ep_in.tx_packet_count(%zu)\n",
		   priv->shm_ipc_ep_in.tx_packet_count);
	seq_printf(s, "shm_ipc_ep_in.rx_packet_count(%zu)\n",
		   priv->shm_ipc_ep_in.rx_packet_count);
	seq_printf(s, "shm_ipc_ep_out.tx_packet_count(%zu)\n",
		   priv->shm_ipc_ep_out.tx_timeout_count);
	seq_printf(s, "shm_ipc_ep_out.rx_packet_count(%zu)\n",
		   priv->shm_ipc_ep_out.rx_packet_count);

	return 0;
}

static int qtnf_pcie_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct qtnf_pcie_bus_priv *pcie_priv;
	struct qtnf_bus *bus;
	int ret;

	bus = devm_kzalloc(&pdev->dev,
			   sizeof(*bus) + sizeof(*pcie_priv), GFP_KERNEL);
	if (!bus) {
		ret = -ENOMEM;
		goto err_init;
	}

	pcie_priv = get_bus_priv(bus);

	pci_set_drvdata(pdev, bus);
	bus->bus_ops = &qtnf_pcie_bus_ops;
	bus->dev = &pdev->dev;
	bus->fw_state = QTNF_FW_STATE_RESET;
	pcie_priv->pdev = pdev;

	strcpy(bus->fwname, QTN_PCI_PEARL_FW_NAME);
	init_completion(&bus->request_firmware_complete);
	mutex_init(&bus->bus_lock);
	spin_lock_init(&pcie_priv->irq_lock);
	spin_lock_init(&pcie_priv->tx_lock);

	/* init stats */
	pcie_priv->tx_full_count = 0;
	pcie_priv->tx_done_count = 0;
	pcie_priv->pcie_irq_count = 0;
	pcie_priv->pcie_irq_rx_count = 0;
	pcie_priv->pcie_irq_tx_count = 0;
	pcie_priv->tx_reclaim_done = 0;
	pcie_priv->tx_reclaim_req = 0;

	pcie_priv->workqueue = create_singlethread_workqueue("QTNF_PEARL_PCIE");
	if (!pcie_priv->workqueue) {
		pr_err("failed to alloc bus workqueue\n");
		ret = -ENODEV;
		goto err_priv;
	}

	if (!pci_is_pcie(pdev)) {
		pr_err("device %s is not PCI Express\n", pci_name(pdev));
		ret = -EIO;
		goto err_base;
	}

	qtnf_tune_pcie_mps(pcie_priv);

	ret = pcim_enable_device(pdev);
	if (ret) {
		pr_err("failed to init PCI device %x\n", pdev->device);
		goto err_base;
	} else {
		pr_debug("successful init of PCI device %x\n", pdev->device);
	}

	pcim_pin_device(pdev);
	pci_set_master(pdev);

	ret = qtnf_pcie_init_irq(pcie_priv);
	if (ret < 0) {
		pr_err("irq init failed\n");
		goto err_base;
	}

	ret = qtnf_pcie_init_memory(pcie_priv);
	if (ret < 0) {
		pr_err("PCIE memory init failed\n");
		goto err_base;
	}

	ret = qtnf_pcie_init_shm_ipc(pcie_priv);
	if (ret < 0) {
		pr_err("PCIE SHM IPC init failed\n");
		goto err_base;
	}

	ret = qtnf_pcie_init_dma_mask(pcie_priv, DMA_BIT_MASK(32));
	if (ret) {
		pr_err("PCIE DMA mask init failed\n");
		goto err_base;
	}

	ret = devm_add_action(&pdev->dev, free_xfer_buffers, (void *)pcie_priv);
	if (ret) {
		pr_err("custom release callback init failed\n");
		goto err_base;
	}

	ret = qtnf_pcie_init_xfer(pcie_priv);
	if (ret) {
		pr_err("PCIE xfer init failed\n");
		goto err_base;
	}

	/* init default irq settings */
	qtnf_init_hdp_irqs(pcie_priv);

	/* start with disabled irqs */
	qtnf_disable_hdp_irqs(pcie_priv);

	ret = devm_request_irq(&pdev->dev, pdev->irq, &qtnf_interrupt, 0,
			       "qtnf_pcie_irq", (void *)bus);
	if (ret) {
		pr_err("failed to request pcie irq %d\n", pdev->irq);
		goto err_base;
	}

	tasklet_init(&pcie_priv->reclaim_tq, qtnf_reclaim_tasklet_fn,
		     (unsigned long)pcie_priv);
	init_dummy_netdev(&bus->mux_dev);
	netif_napi_add(&bus->mux_dev, &bus->mux_napi,
		       qtnf_rx_poll, 10);

	ret = qtnf_bringup_fw(bus);
	if (ret < 0)
		goto err_bringup_fw;
	else if (ret)
		wait_for_completion(&bus->request_firmware_complete);

	if (bus->fw_state != QTNF_FW_STATE_FW_DNLD_DONE) {
		pr_err("failed to start FW\n");
		goto err_bringup_fw;
	}

	if (qtnf_poll_state(&pcie_priv->bda->bda_ep_state, QTN_EP_FW_QLINK_DONE,
			    QTN_FW_QLINK_TIMEOUT_MS)) {
		pr_err("FW runtime failure\n");
		goto err_bringup_fw;
	}

	ret = qtnf_core_attach(bus);
	if (ret) {
		pr_err("failed to attach core\n");
		goto err_bringup_fw;
	}

	qtnf_debugfs_init(bus, DRV_NAME);
	qtnf_debugfs_add_entry(bus, "mps", qtnf_dbg_mps_show);
	qtnf_debugfs_add_entry(bus, "msi_enabled", qtnf_dbg_msi_show);
	qtnf_debugfs_add_entry(bus, "hdp_stats", qtnf_dbg_hdp_stats);
	qtnf_debugfs_add_entry(bus, "irq_stats", qtnf_dbg_irq_stats);
	qtnf_debugfs_add_entry(bus, "shm_stats", qtnf_dbg_shm_stats);

	return 0;

err_bringup_fw:
	netif_napi_del(&bus->mux_napi);

err_base:
	flush_workqueue(pcie_priv->workqueue);
	destroy_workqueue(pcie_priv->workqueue);

err_priv:
	pci_set_drvdata(pdev, NULL);

err_init:
	return ret;
}

static void qtnf_pcie_remove(struct pci_dev *pdev)
{
	struct qtnf_pcie_bus_priv *priv;
	struct qtnf_bus *bus;

	bus = pci_get_drvdata(pdev);
	if (!bus)
		return;

	priv = get_bus_priv(bus);

	qtnf_core_detach(bus);
	netif_napi_del(&bus->mux_napi);

	flush_workqueue(priv->workqueue);
	destroy_workqueue(priv->workqueue);
	tasklet_kill(&priv->reclaim_tq);

	qtnf_debugfs_remove(bus);

	qtnf_pcie_free_shm_ipc(priv);
}

#ifdef CONFIG_PM_SLEEP
static int qtnf_pcie_suspend(struct device *dev)
{
	return -EOPNOTSUPP;
}

static int qtnf_pcie_resume(struct device *dev)
{
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM_SLEEP
/* Power Management Hooks */
static SIMPLE_DEV_PM_OPS(qtnf_pcie_pm_ops, qtnf_pcie_suspend,
			 qtnf_pcie_resume);
#endif

static struct pci_device_id qtnf_pcie_devid_table[] = {
	{
		PCIE_VENDOR_ID_QUANTENNA, PCIE_DEVICE_ID_QTN_PEARL,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	},
	{ },
};

MODULE_DEVICE_TABLE(pci, qtnf_pcie_devid_table);

static struct pci_driver qtnf_pcie_drv_data = {
	.name = DRV_NAME,
	.id_table = qtnf_pcie_devid_table,
	.probe = qtnf_pcie_probe,
	.remove = qtnf_pcie_remove,
#ifdef CONFIG_PM_SLEEP
	.driver = {
		.pm = &qtnf_pcie_pm_ops,
	},
#endif
};

static int __init qtnf_pcie_register(void)
{
	pr_info("register Quantenna QSR10g FullMAC PCIE driver\n");
	return pci_register_driver(&qtnf_pcie_drv_data);
}

static void __exit qtnf_pcie_exit(void)
{
	pr_info("unregister Quantenna QSR10g FullMAC PCIE driver\n");
	pci_unregister_driver(&qtnf_pcie_drv_data);
}

module_init(qtnf_pcie_register);
module_exit(qtnf_pcie_exit);

MODULE_AUTHOR("Quantenna Communications");
MODULE_DESCRIPTION("Quantenna QSR10g PCIe bus driver for 802.11 wireless LAN.");
MODULE_LICENSE("GPL");
