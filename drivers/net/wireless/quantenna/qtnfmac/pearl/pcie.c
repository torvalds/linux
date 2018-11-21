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
#include <linux/circ_buf.h>
#include <linux/log2.h>

#include "qtn_hw_ids.h"
#include "pcie_bus_priv.h"
#include "core.h"
#include "bus.h"
#include "debug.h"

static bool use_msi = true;
module_param(use_msi, bool, 0644);
MODULE_PARM_DESC(use_msi, "set 0 to use legacy interrupt");

static unsigned int tx_bd_size_param = 32;
module_param(tx_bd_size_param, uint, 0644);
MODULE_PARM_DESC(tx_bd_size_param, "Tx descriptors queue size, power of two");

static unsigned int rx_bd_size_param = 256;
module_param(rx_bd_size_param, uint, 0644);
MODULE_PARM_DESC(rx_bd_size_param, "Rx descriptors queue size, power of two");

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

static void qtnf_pcie_init_irq(struct qtnf_pcie_bus_priv *priv)
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
}

static void qtnf_deassert_intx(struct qtnf_pcie_bus_priv *priv)
{
	void __iomem *reg = priv->sysctl_bar + PEARL_PCIE_CFG0_OFFSET;
	u32 cfg;

	cfg = readl(reg);
	cfg &= ~PEARL_ASSERT_INTX;
	qtnf_non_posted_write(cfg, reg);
}

static void qtnf_reset_card(struct qtnf_pcie_bus_priv *priv)
{
	const u32 data = QTN_PEARL_IPC_IRQ_WORD(QTN_PEARL_LHOST_EP_RESET);
	void __iomem *reg = priv->sysctl_bar +
			    QTN_PEARL_SYSCTL_LHOST_IRQ_OFFSET;

	qtnf_non_posted_write(data, reg);
	msleep(QTN_EP_RESET_WAIT_MS);
	pci_restore_state(priv->pdev);
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
	len = pci_resource_len(priv->pdev, index);
	vaddr = pcim_iomap_table(priv->pdev)[index];
	if (!vaddr)
		return IOMEM_ERR_PTR(-ENOMEM);

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

	skb_put_data(skb, buf, len);

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
	int ret = -ENOMEM;

	priv->sysctl_bar = qtnf_map_bar(priv, QTN_SYSCTL_BAR);
	if (IS_ERR(priv->sysctl_bar)) {
		pr_err("failed to map BAR%u\n", QTN_SYSCTL_BAR);
		return ret;
	}

	priv->dmareg_bar = qtnf_map_bar(priv, QTN_DMA_BAR);
	if (IS_ERR(priv->dmareg_bar)) {
		pr_err("failed to map BAR%u\n", QTN_DMA_BAR);
		return ret;
	}

	priv->epmem_bar = qtnf_map_bar(priv, QTN_SHMEM_BAR);
	if (IS_ERR(priv->epmem_bar)) {
		pr_err("failed to map BAR%u\n", QTN_SHMEM_BAR);
		return ret;
	}

	priv->pcie_reg_base = priv->dmareg_bar;
	priv->bda = priv->epmem_bar;
	writel(priv->msi_enabled, &priv->bda->bda_rc_msi_enabled);

	return 0;
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

	priv->tx_bd_r_index = 0;
	priv->tx_bd_w_index = 0;

	/* rx bd */

	vaddr = ((struct qtnf_tx_bd *)vaddr) + priv->tx_bd_num;
	paddr += priv->tx_bd_num * sizeof(struct qtnf_tx_bd);

	priv->rx_bd_vbase = vaddr;
	priv->rx_bd_pbase = paddr;

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	writel(QTN_HOST_HI32(paddr),
	       PCIE_HDP_TX_HOST_Q_BASE_H(priv->pcie_reg_base));
#endif
	writel(QTN_HOST_LO32(paddr),
	       PCIE_HDP_TX_HOST_Q_BASE_L(priv->pcie_reg_base));
	writel(priv->rx_bd_num | (sizeof(struct qtnf_rx_bd)) << 16,
	       PCIE_HDP_TX_HOST_Q_SZ_CTRL(priv->pcie_reg_base));

	pr_debug("RX descriptor table: vaddr=0x%p paddr=%pad\n", vaddr, &paddr);

	return 0;
}

static int skb2rbd_attach(struct qtnf_pcie_bus_priv *priv, u16 index)
{
	struct qtnf_rx_bd *rxbd;
	struct sk_buff *skb;
	dma_addr_t paddr;

	skb = __netdev_alloc_skb_ip_align(NULL, SKB_BUF_SIZE, GFP_ATOMIC);
	if (!skb) {
		priv->rx_skb[index] = NULL;
		return -ENOMEM;
	}

	priv->rx_skb[index] = skb;
	rxbd = &priv->rx_bd_vbase[index];

	paddr = pci_map_single(priv->pdev, skb->data,
			       SKB_BUF_SIZE, PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(priv->pdev, paddr)) {
		pr_err("skb DMA mapping error: %pad\n", &paddr);
		return -ENOMEM;
	}

	/* keep rx skb paddrs in rx buffer descriptors for cleanup purposes */
	rxbd->addr = cpu_to_le32(QTN_HOST_LO32(paddr));
	rxbd->addr_h = cpu_to_le32(QTN_HOST_HI32(paddr));
	rxbd->info = 0x0;

	priv->rx_bd_w_index = index;

	/* sync up all descriptor updates */
	wmb();

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	writel(QTN_HOST_HI32(paddr),
	       PCIE_HDP_HHBM_BUF_PTR_H(priv->pcie_reg_base));
#endif
	writel(QTN_HOST_LO32(paddr),
	       PCIE_HDP_HHBM_BUF_PTR(priv->pcie_reg_base));

	writel(index, PCIE_HDP_TX_HOST_Q_WR_PTR(priv->pcie_reg_base));
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
static void qtnf_free_xfer_buffers(struct qtnf_pcie_bus_priv *priv)
{
	struct qtnf_tx_bd *txbd;
	struct qtnf_rx_bd *rxbd;
	struct sk_buff *skb;
	dma_addr_t paddr;
	int i;

	/* free rx buffers */
	for (i = 0; i < priv->rx_bd_num; i++) {
		if (priv->rx_skb && priv->rx_skb[i]) {
			rxbd = &priv->rx_bd_vbase[i];
			skb = priv->rx_skb[i];
			paddr = QTN_HOST_ADDR(le32_to_cpu(rxbd->addr_h),
					      le32_to_cpu(rxbd->addr));
			pci_unmap_single(priv->pdev, paddr, SKB_BUF_SIZE,
					 PCI_DMA_FROMDEVICE);
			dev_kfree_skb_any(skb);
			priv->rx_skb[i] = NULL;
		}
	}

	/* free tx buffers */
	for (i = 0; i < priv->tx_bd_num; i++) {
		if (priv->tx_skb && priv->tx_skb[i]) {
			txbd = &priv->tx_bd_vbase[i];
			skb = priv->tx_skb[i];
			paddr = QTN_HOST_ADDR(le32_to_cpu(txbd->addr_h),
					      le32_to_cpu(txbd->addr));
			pci_unmap_single(priv->pdev, paddr, skb->len,
					 PCI_DMA_TODEVICE);
			dev_kfree_skb_any(skb);
			priv->tx_skb[i] = NULL;
		}
	}
}

static int qtnf_hhbm_init(struct qtnf_pcie_bus_priv *priv)
{
	u32 val;

	val = readl(PCIE_HHBM_CONFIG(priv->pcie_reg_base));
	val |= HHBM_CONFIG_SOFT_RESET;
	writel(val, PCIE_HHBM_CONFIG(priv->pcie_reg_base));
	usleep_range(50, 100);
	val &= ~HHBM_CONFIG_SOFT_RESET;
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	val |= HHBM_64BIT;
#endif
	writel(val, PCIE_HHBM_CONFIG(priv->pcie_reg_base));
	writel(priv->rx_bd_num, PCIE_HHBM_Q_LIMIT_REG(priv->pcie_reg_base));

	return 0;
}

static int qtnf_pcie_init_xfer(struct qtnf_pcie_bus_priv *priv)
{
	int ret;
	u32 val;

	priv->tx_bd_num = tx_bd_size_param;
	priv->rx_bd_num = rx_bd_size_param;
	priv->rx_bd_w_index = 0;
	priv->rx_bd_r_index = 0;

	if (!priv->tx_bd_num || !is_power_of_2(priv->tx_bd_num)) {
		pr_err("tx_bd_size_param %u is not power of two\n",
		       priv->tx_bd_num);
		return -EINVAL;
	}

	val = priv->tx_bd_num * sizeof(struct qtnf_tx_bd);
	if (val > PCIE_HHBM_MAX_SIZE) {
		pr_err("tx_bd_size_param %u is too large\n",
		       priv->tx_bd_num);
		return -EINVAL;
	}

	if (!priv->rx_bd_num || !is_power_of_2(priv->rx_bd_num)) {
		pr_err("rx_bd_size_param %u is not power of two\n",
		       priv->rx_bd_num);
		return -EINVAL;
	}

	val = priv->rx_bd_num * sizeof(dma_addr_t);
	if (val > PCIE_HHBM_MAX_SIZE) {
		pr_err("rx_bd_size_param %u is too large\n",
		       priv->rx_bd_num);
		return -EINVAL;
	}

	ret = qtnf_hhbm_init(priv);
	if (ret) {
		pr_err("failed to init h/w queues\n");
		return ret;
	}

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

static void qtnf_pcie_data_tx_reclaim(struct qtnf_pcie_bus_priv *priv)
{
	struct qtnf_tx_bd *txbd;
	struct sk_buff *skb;
	unsigned long flags;
	dma_addr_t paddr;
	u32 tx_done_index;
	int count = 0;
	int i;

	spin_lock_irqsave(&priv->tx_reclaim_lock, flags);

	tx_done_index = readl(PCIE_HDP_RX0DMA_CNT(priv->pcie_reg_base))
			& (priv->tx_bd_num - 1);

	i = priv->tx_bd_r_index;

	while (CIRC_CNT(tx_done_index, i, priv->tx_bd_num)) {
		skb = priv->tx_skb[i];
		if (likely(skb)) {
			txbd = &priv->tx_bd_vbase[i];
			paddr = QTN_HOST_ADDR(le32_to_cpu(txbd->addr_h),
					      le32_to_cpu(txbd->addr));
			pci_unmap_single(priv->pdev, paddr, skb->len,
					 PCI_DMA_TODEVICE);

			if (skb->dev) {
				qtnf_update_tx_stats(skb->dev, skb);
				if (unlikely(priv->tx_stopped)) {
					qtnf_wake_all_queues(skb->dev);
					priv->tx_stopped = 0;
				}
			}

			dev_kfree_skb_any(skb);
		}

		priv->tx_skb[i] = NULL;
		count++;

		if (++i >= priv->tx_bd_num)
			i = 0;
	}

	priv->tx_reclaim_done += count;
	priv->tx_reclaim_req++;
	priv->tx_bd_r_index = i;

	spin_unlock_irqrestore(&priv->tx_reclaim_lock, flags);
}

static int qtnf_tx_queue_ready(struct qtnf_pcie_bus_priv *priv)
{
	if (!CIRC_SPACE(priv->tx_bd_w_index, priv->tx_bd_r_index,
			priv->tx_bd_num)) {
		qtnf_pcie_data_tx_reclaim(priv);

		if (!CIRC_SPACE(priv->tx_bd_w_index, priv->tx_bd_r_index,
				priv->tx_bd_num)) {
			pr_warn_ratelimited("reclaim full Tx queue\n");
			priv->tx_full_count++;
			return 0;
		}
	}

	return 1;
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

	spin_lock_irqsave(&priv->tx0_lock, flags);

	if (!qtnf_tx_queue_ready(priv)) {
		if (skb->dev) {
			netif_tx_stop_all_queues(skb->dev);
			priv->tx_stopped = 1;
		}

		spin_unlock_irqrestore(&priv->tx0_lock, flags);
		return NETDEV_TX_BUSY;
	}

	i = priv->tx_bd_w_index;
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

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	writel(QTN_HOST_HI32(txbd_paddr),
	       PCIE_HDP_HOST_WR_DESC0_H(priv->pcie_reg_base));
#endif
	writel(QTN_HOST_LO32(txbd_paddr),
	       PCIE_HDP_HOST_WR_DESC0(priv->pcie_reg_base));

	if (++i >= priv->tx_bd_num)
		i = 0;

	priv->tx_bd_w_index = i;

tx_done:
	if (ret && skb) {
		pr_err_ratelimited("drop skb\n");
		if (skb->dev)
			skb->dev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
	}

	priv->tx_done_count++;
	spin_unlock_irqrestore(&priv->tx0_lock, flags);

	qtnf_pcie_data_tx_reclaim(priv);

	return NETDEV_TX_OK;
}

static int qtnf_pcie_control_tx(struct qtnf_bus *bus, struct sk_buff *skb)
{
	struct qtnf_pcie_bus_priv *priv = (void *)get_bus_priv(bus);
	int ret;

	ret = qtnf_shm_ipc_send(&priv->shm_ipc_ep_in, skb->data, skb->len);

	if (ret == -ETIMEDOUT) {
		pr_err("EP firmware is dead\n");
		bus->fw_state = QTNF_FW_STATE_EP_DEAD;
	}

	return ret;
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

	if (status & PCIE_HDP_INT_RX_BITS)
		priv->pcie_irq_rx_count++;

	if (status & PCIE_HDP_INT_TX_BITS)
		priv->pcie_irq_tx_count++;

	if (status & PCIE_HDP_INT_HHBM_UF)
		priv->pcie_irq_uf_count++;

	if (status & PCIE_HDP_INT_RX_BITS) {
		qtnf_dis_rxdone_irq(priv);
		napi_schedule(&bus->mux_napi);
	}

	if (status & PCIE_HDP_INT_TX_BITS) {
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

static int qtnf_rx_data_ready(struct qtnf_pcie_bus_priv *priv)
{
	u16 index = priv->rx_bd_r_index;
	struct qtnf_rx_bd *rxbd;
	u32 descw;

	rxbd = &priv->rx_bd_vbase[index];
	descw = le32_to_cpu(rxbd->info);

	if (descw & QTN_TXDONE_MASK)
		return 1;

	return 0;
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
	int consume;
	u32 descw;
	u32 psize;
	u16 r_idx;
	u16 w_idx;
	int ret;

	while (processed < budget) {


		if (!qtnf_rx_data_ready(priv))
			goto rx_out;

		r_idx = priv->rx_bd_r_index;
		rxbd = &priv->rx_bd_vbase[r_idx];
		descw = le32_to_cpu(rxbd->info);

		skb = priv->rx_skb[r_idx];
		psize = QTN_GET_LEN(descw);
		consume = 1;

		if (!(descw & QTN_TXDONE_MASK)) {
			pr_warn("skip invalid rxbd[%d]\n", r_idx);
			consume = 0;
		}

		if (!skb) {
			pr_warn("skip missing rx_skb[%d]\n", r_idx);
			consume = 0;
		}

		if (skb && (skb_tailroom(skb) <  psize)) {
			pr_err("skip packet with invalid length: %u > %u\n",
			       psize, skb_tailroom(skb));
			consume = 0;
		}

		if (skb) {
			skb_paddr = QTN_HOST_ADDR(le32_to_cpu(rxbd->addr_h),
						  le32_to_cpu(rxbd->addr));
			pci_unmap_single(priv->pdev, skb_paddr, SKB_BUF_SIZE,
					 PCI_DMA_FROMDEVICE);
		}

		if (consume) {
			skb_put(skb, psize);
			ndev = qtnf_classify_skb(bus, skb);
			if (likely(ndev)) {
				qtnf_update_rx_stats(ndev, skb);
				skb->protocol = eth_type_trans(skb, ndev);
				napi_gro_receive(napi, skb);
			} else {
				pr_debug("drop untagged skb\n");
				bus->mux_dev.stats.rx_dropped++;
				dev_kfree_skb_any(skb);
			}
		} else {
			if (skb) {
				bus->mux_dev.stats.rx_dropped++;
				dev_kfree_skb_any(skb);
			}
		}

		priv->rx_skb[r_idx] = NULL;
		if (++r_idx >= priv->rx_bd_num)
			r_idx = 0;

		priv->rx_bd_r_index = r_idx;

		/* repalce processed buffer by a new one */
		w_idx = priv->rx_bd_w_index;
		while (CIRC_SPACE(priv->rx_bd_w_index, priv->rx_bd_r_index,
				  priv->rx_bd_num) > 0) {
			if (++w_idx >= priv->rx_bd_num)
				w_idx = 0;

			ret = skb2rbd_attach(priv, w_idx);
			if (ret) {
				pr_err("failed to allocate new rx_skb[%d]\n",
				       w_idx);
				break;
			}
		}

		processed++;
	}

rx_out:
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
	u32 reg = readl(PCIE_HDP_INT_EN(priv->pcie_reg_base));
	u32 status;

	seq_printf(s, "pcie_irq_count(%u)\n", priv->pcie_irq_count);
	seq_printf(s, "pcie_irq_tx_count(%u)\n", priv->pcie_irq_tx_count);
	status = reg &  PCIE_HDP_INT_TX_BITS;
	seq_printf(s, "pcie_irq_tx_status(%s)\n",
		   (status == PCIE_HDP_INT_TX_BITS) ? "EN" : "DIS");
	seq_printf(s, "pcie_irq_rx_count(%u)\n", priv->pcie_irq_rx_count);
	status = reg &  PCIE_HDP_INT_RX_BITS;
	seq_printf(s, "pcie_irq_rx_status(%s)\n",
		   (status == PCIE_HDP_INT_RX_BITS) ? "EN" : "DIS");
	seq_printf(s, "pcie_irq_uf_count(%u)\n", priv->pcie_irq_uf_count);
	status = reg &  PCIE_HDP_INT_HHBM_UF;
	seq_printf(s, "pcie_irq_hhbm_uf_status(%s)\n",
		   (status == PCIE_HDP_INT_HHBM_UF) ? "EN" : "DIS");

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

	seq_printf(s, "tx_bd_r_index(%u)\n", priv->tx_bd_r_index);
	seq_printf(s, "tx_bd_p_index(%u)\n",
		   readl(PCIE_HDP_RX0DMA_CNT(priv->pcie_reg_base))
			& (priv->tx_bd_num - 1));
	seq_printf(s, "tx_bd_w_index(%u)\n", priv->tx_bd_w_index);
	seq_printf(s, "tx queue len(%u)\n",
		   CIRC_CNT(priv->tx_bd_w_index, priv->tx_bd_r_index,
			    priv->tx_bd_num));

	seq_printf(s, "rx_bd_r_index(%u)\n", priv->rx_bd_r_index);
	seq_printf(s, "rx_bd_p_index(%u)\n",
		   readl(PCIE_HDP_TX0DMA_CNT(priv->pcie_reg_base))
			& (priv->rx_bd_num - 1));
	seq_printf(s, "rx_bd_w_index(%u)\n", priv->rx_bd_w_index);
	seq_printf(s, "rx alloc queue len(%u)\n",
		   CIRC_SPACE(priv->rx_bd_w_index, priv->rx_bd_r_index,
			      priv->rx_bd_num));

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

static void qtnf_fw_work_handler(struct work_struct *work)
{
	struct qtnf_bus *bus = container_of(work, struct qtnf_bus, fw_work);
	struct qtnf_pcie_bus_priv *priv = (void *)get_bus_priv(bus);
	struct pci_dev *pdev = priv->pdev;
	const struct firmware *fw;
	int ret;
	u32 state = QTN_RC_FW_LOADRDY | QTN_RC_FW_QLINK;

	if (flashboot) {
		state |= QTN_RC_FW_FLASHBOOT;
	} else {
		ret = request_firmware(&fw, bus->fwname, &pdev->dev);
		if (ret < 0) {
			pr_err("failed to get firmware %s\n", bus->fwname);
			goto fw_load_fail;
		}
	}

	qtnf_set_state(&priv->bda->bda_rc_state, state);

	if (qtnf_poll_state(&priv->bda->bda_ep_state, QTN_EP_FW_LOADRDY,
			    QTN_FW_DL_TIMEOUT_MS)) {
		pr_err("card is not ready\n");

		if (!flashboot)
			release_firmware(fw);

		goto fw_load_fail;
	}

	qtnf_clear_state(&priv->bda->bda_ep_state, QTN_EP_FW_LOADRDY);

	if (flashboot) {
		pr_info("booting firmware from flash\n");
	} else {
		pr_info("starting firmware upload: %s\n", bus->fwname);

		ret = qtnf_ep_fw_load(priv, fw->data, fw->size);
		release_firmware(fw);
		if (ret) {
			pr_err("firmware upload error\n");
			goto fw_load_fail;
		}
	}

	if (qtnf_poll_state(&priv->bda->bda_ep_state, QTN_EP_FW_DONE,
			    QTN_FW_DL_TIMEOUT_MS)) {
		pr_err("firmware bringup timed out\n");
		goto fw_load_fail;
	}

	bus->fw_state = QTNF_FW_STATE_FW_DNLD_DONE;
	pr_info("firmware is up and running\n");

	if (qtnf_poll_state(&priv->bda->bda_ep_state,
			    QTN_EP_FW_QLINK_DONE, QTN_FW_QLINK_TIMEOUT_MS)) {
		pr_err("firmware runtime failure\n");
		goto fw_load_fail;
	}

	ret = qtnf_core_attach(bus);
	if (ret) {
		pr_err("failed to attach core\n");
		goto fw_load_fail;
	}

	qtnf_debugfs_init(bus, DRV_NAME);
	qtnf_debugfs_add_entry(bus, "mps", qtnf_dbg_mps_show);
	qtnf_debugfs_add_entry(bus, "msi_enabled", qtnf_dbg_msi_show);
	qtnf_debugfs_add_entry(bus, "hdp_stats", qtnf_dbg_hdp_stats);
	qtnf_debugfs_add_entry(bus, "irq_stats", qtnf_dbg_irq_stats);
	qtnf_debugfs_add_entry(bus, "shm_stats", qtnf_dbg_shm_stats);

	goto fw_load_exit;

fw_load_fail:
	bus->fw_state = QTNF_FW_STATE_DETACHED;

fw_load_exit:
	complete(&bus->firmware_init_complete);
	put_device(&pdev->dev);
}

static void qtnf_bringup_fw_async(struct qtnf_bus *bus)
{
	struct qtnf_pcie_bus_priv *priv = (void *)get_bus_priv(bus);
	struct pci_dev *pdev = priv->pdev;

	get_device(&pdev->dev);
	INIT_WORK(&bus->fw_work, qtnf_fw_work_handler);
	schedule_work(&bus->fw_work);
}

static void qtnf_reclaim_tasklet_fn(unsigned long data)
{
	struct qtnf_pcie_bus_priv *priv = (void *)data;

	qtnf_pcie_data_tx_reclaim(priv);
	qtnf_en_txdone_irq(priv);
}

static int qtnf_pcie_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct qtnf_pcie_bus_priv *pcie_priv;
	struct qtnf_bus *bus;
	int ret;

	bus = devm_kzalloc(&pdev->dev,
			   sizeof(*bus) + sizeof(*pcie_priv), GFP_KERNEL);
	if (!bus)
		return -ENOMEM;

	pcie_priv = get_bus_priv(bus);

	pci_set_drvdata(pdev, bus);
	bus->bus_ops = &qtnf_pcie_bus_ops;
	bus->dev = &pdev->dev;
	bus->fw_state = QTNF_FW_STATE_RESET;
	pcie_priv->pdev = pdev;

	strcpy(bus->fwname, QTN_PCI_PEARL_FW_NAME);
	init_completion(&bus->firmware_init_complete);
	mutex_init(&bus->bus_lock);
	spin_lock_init(&pcie_priv->tx0_lock);
	spin_lock_init(&pcie_priv->irq_lock);
	spin_lock_init(&pcie_priv->tx_reclaim_lock);

	/* init stats */
	pcie_priv->tx_full_count = 0;
	pcie_priv->tx_done_count = 0;
	pcie_priv->pcie_irq_count = 0;
	pcie_priv->pcie_irq_rx_count = 0;
	pcie_priv->pcie_irq_tx_count = 0;
	pcie_priv->pcie_irq_uf_count = 0;
	pcie_priv->tx_reclaim_done = 0;
	pcie_priv->tx_reclaim_req = 0;

	tasklet_init(&pcie_priv->reclaim_tq, qtnf_reclaim_tasklet_fn,
		     (unsigned long)pcie_priv);

	init_dummy_netdev(&bus->mux_dev);
	netif_napi_add(&bus->mux_dev, &bus->mux_napi,
		       qtnf_rx_poll, 10);

	pcie_priv->workqueue = create_singlethread_workqueue("QTNF_PEARL_PCIE");
	if (!pcie_priv->workqueue) {
		pr_err("failed to alloc bus workqueue\n");
		ret = -ENODEV;
		goto err_init;
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

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
#else
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
#endif
	if (ret) {
		pr_err("PCIE DMA coherent mask init failed\n");
		goto err_base;
	}

	pci_set_master(pdev);
	qtnf_pcie_init_irq(pcie_priv);

	ret = qtnf_pcie_init_memory(pcie_priv);
	if (ret < 0) {
		pr_err("PCIE memory init failed\n");
		goto err_base;
	}

	pci_save_state(pdev);

	ret = qtnf_pcie_init_shm_ipc(pcie_priv);
	if (ret < 0) {
		pr_err("PCIE SHM IPC init failed\n");
		goto err_base;
	}

	ret = qtnf_pcie_init_xfer(pcie_priv);
	if (ret) {
		pr_err("PCIE xfer init failed\n");
		goto err_ipc;
	}

	/* init default irq settings */
	qtnf_init_hdp_irqs(pcie_priv);

	/* start with disabled irqs */
	qtnf_disable_hdp_irqs(pcie_priv);

	ret = devm_request_irq(&pdev->dev, pdev->irq, &qtnf_interrupt, 0,
			       "qtnf_pcie_irq", (void *)bus);
	if (ret) {
		pr_err("failed to request pcie irq %d\n", pdev->irq);
		goto err_xfer;
	}

	qtnf_bringup_fw_async(bus);

	return 0;

err_xfer:
	qtnf_free_xfer_buffers(pcie_priv);

err_ipc:
	qtnf_pcie_free_shm_ipc(pcie_priv);

err_base:
	flush_workqueue(pcie_priv->workqueue);
	destroy_workqueue(pcie_priv->workqueue);
	netif_napi_del(&bus->mux_napi);

err_init:
	tasklet_kill(&pcie_priv->reclaim_tq);
	pci_set_drvdata(pdev, NULL);

	return ret;
}

static void qtnf_pcie_remove(struct pci_dev *pdev)
{
	struct qtnf_pcie_bus_priv *priv;
	struct qtnf_bus *bus;

	bus = pci_get_drvdata(pdev);
	if (!bus)
		return;

	wait_for_completion(&bus->firmware_init_complete);

	if (bus->fw_state == QTNF_FW_STATE_ACTIVE ||
	    bus->fw_state == QTNF_FW_STATE_EP_DEAD)
		qtnf_core_detach(bus);

	priv = get_bus_priv(bus);

	netif_napi_del(&bus->mux_napi);
	flush_workqueue(priv->workqueue);
	destroy_workqueue(priv->workqueue);
	tasklet_kill(&priv->reclaim_tq);

	qtnf_free_xfer_buffers(priv);
	qtnf_debugfs_remove(bus);

	qtnf_pcie_free_shm_ipc(priv);
	qtnf_reset_card(priv);
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

static const struct pci_device_id qtnf_pcie_devid_table[] = {
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
