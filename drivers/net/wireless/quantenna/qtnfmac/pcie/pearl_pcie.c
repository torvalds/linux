// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2018 Quantenna Communications */

#include <linux/kernel.h>
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

#include "pcie_priv.h"
#include "pearl_pcie_regs.h"
#include "pearl_pcie_ipc.h"
#include "qtn_hw_ids.h"
#include "core.h"
#include "bus.h"
#include "shm_ipc.h"
#include "debug.h"

#define PEARL_TX_BD_SIZE_DEFAULT	32

struct qtnf_pearl_bda {
	__le16 bda_len;
	__le16 bda_version;
	__le32 bda_pci_endian;
	__le32 bda_ep_state;
	__le32 bda_rc_state;
	__le32 bda_dma_mask;
	__le32 bda_msi_addr;
	__le32 bda_flashsz;
	u8 bda_boardname[PCIE_BDA_NAMELEN];
	__le32 bda_rc_msi_enabled;
	u8 bda_hhbm_list[PCIE_HHBM_MAX_SIZE];
	__le32 bda_dsbw_start_index;
	__le32 bda_dsbw_end_index;
	__le32 bda_dsbw_total_bytes;
	__le32 bda_rc_tx_bd_base;
	__le32 bda_rc_tx_bd_num;
	u8 bda_pcie_mac[QTN_ENET_ADDR_LENGTH];
	struct qtnf_shm_ipc_region bda_shm_reg1 __aligned(4096); /* host TX */
	struct qtnf_shm_ipc_region bda_shm_reg2 __aligned(4096); /* host RX */
} __packed;

struct qtnf_pearl_tx_bd {
	__le32 addr;
	__le32 addr_h;
	__le32 info;
	__le32 info_h;
} __packed;

struct qtnf_pearl_rx_bd {
	__le32 addr;
	__le32 addr_h;
	__le32 info;
	__le32 info_h;
	__le32 next_ptr;
	__le32 next_ptr_h;
} __packed;

struct qtnf_pearl_fw_hdr {
	u8 boardflg[8];
	__le32 fwsize;
	__le32 seqnum;
	__le32 type;
	__le32 pktlen;
	__le32 crc;
} __packed;

struct qtnf_pcie_pearl_state {
	struct qtnf_pcie_bus_priv base;

	/* lock for irq configuration changes */
	spinlock_t irq_lock;

	struct qtnf_pearl_bda __iomem *bda;
	void __iomem *pcie_reg_base;

	struct qtnf_pearl_tx_bd *tx_bd_vbase;
	dma_addr_t tx_bd_pbase;

	struct qtnf_pearl_rx_bd *rx_bd_vbase;
	dma_addr_t rx_bd_pbase;

	dma_addr_t bd_table_paddr;
	void *bd_table_vaddr;
	u32 bd_table_len;
	u32 pcie_irq_mask;
	u32 pcie_irq_rx_count;
	u32 pcie_irq_tx_count;
	u32 pcie_irq_uf_count;
};

static inline void qtnf_init_hdp_irqs(struct qtnf_pcie_pearl_state *ps)
{
	unsigned long flags;

	spin_lock_irqsave(&ps->irq_lock, flags);
	ps->pcie_irq_mask = (PCIE_HDP_INT_RX_BITS | PCIE_HDP_INT_TX_BITS);
	spin_unlock_irqrestore(&ps->irq_lock, flags);
}

static inline void qtnf_enable_hdp_irqs(struct qtnf_pcie_pearl_state *ps)
{
	unsigned long flags;

	spin_lock_irqsave(&ps->irq_lock, flags);
	writel(ps->pcie_irq_mask, PCIE_HDP_INT_EN(ps->pcie_reg_base));
	spin_unlock_irqrestore(&ps->irq_lock, flags);
}

static inline void qtnf_disable_hdp_irqs(struct qtnf_pcie_pearl_state *ps)
{
	unsigned long flags;

	spin_lock_irqsave(&ps->irq_lock, flags);
	writel(0x0, PCIE_HDP_INT_EN(ps->pcie_reg_base));
	spin_unlock_irqrestore(&ps->irq_lock, flags);
}

static inline void qtnf_en_rxdone_irq(struct qtnf_pcie_pearl_state *ps)
{
	unsigned long flags;

	spin_lock_irqsave(&ps->irq_lock, flags);
	ps->pcie_irq_mask |= PCIE_HDP_INT_RX_BITS;
	writel(ps->pcie_irq_mask, PCIE_HDP_INT_EN(ps->pcie_reg_base));
	spin_unlock_irqrestore(&ps->irq_lock, flags);
}

static inline void qtnf_dis_rxdone_irq(struct qtnf_pcie_pearl_state *ps)
{
	unsigned long flags;

	spin_lock_irqsave(&ps->irq_lock, flags);
	ps->pcie_irq_mask &= ~PCIE_HDP_INT_RX_BITS;
	writel(ps->pcie_irq_mask, PCIE_HDP_INT_EN(ps->pcie_reg_base));
	spin_unlock_irqrestore(&ps->irq_lock, flags);
}

static inline void qtnf_en_txdone_irq(struct qtnf_pcie_pearl_state *ps)
{
	unsigned long flags;

	spin_lock_irqsave(&ps->irq_lock, flags);
	ps->pcie_irq_mask |= PCIE_HDP_INT_TX_BITS;
	writel(ps->pcie_irq_mask, PCIE_HDP_INT_EN(ps->pcie_reg_base));
	spin_unlock_irqrestore(&ps->irq_lock, flags);
}

static inline void qtnf_dis_txdone_irq(struct qtnf_pcie_pearl_state *ps)
{
	unsigned long flags;

	spin_lock_irqsave(&ps->irq_lock, flags);
	ps->pcie_irq_mask &= ~PCIE_HDP_INT_TX_BITS;
	writel(ps->pcie_irq_mask, PCIE_HDP_INT_EN(ps->pcie_reg_base));
	spin_unlock_irqrestore(&ps->irq_lock, flags);
}

static void qtnf_deassert_intx(struct qtnf_pcie_pearl_state *ps)
{
	void __iomem *reg = ps->base.sysctl_bar + PEARL_PCIE_CFG0_OFFSET;
	u32 cfg;

	cfg = readl(reg);
	cfg &= ~PEARL_ASSERT_INTX;
	qtnf_non_posted_write(cfg, reg);
}

static void qtnf_pearl_reset_ep(struct qtnf_pcie_pearl_state *ps)
{
	const u32 data = QTN_PEARL_IPC_IRQ_WORD(QTN_PEARL_LHOST_EP_RESET);
	void __iomem *reg = ps->base.sysctl_bar +
			    QTN_PEARL_SYSCTL_LHOST_IRQ_OFFSET;

	qtnf_non_posted_write(data, reg);
	msleep(QTN_EP_RESET_WAIT_MS);
	pci_restore_state(ps->base.pdev);
}

static void qtnf_pcie_pearl_ipc_gen_ep_int(void *arg)
{
	const struct qtnf_pcie_pearl_state *ps = arg;
	const u32 data = QTN_PEARL_IPC_IRQ_WORD(QTN_PEARL_LHOST_IPC_IRQ);
	void __iomem *reg = ps->base.sysctl_bar +
			    QTN_PEARL_SYSCTL_LHOST_IRQ_OFFSET;

	qtnf_non_posted_write(data, reg);
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

static int pearl_alloc_bd_table(struct qtnf_pcie_pearl_state *ps)
{
	struct qtnf_pcie_bus_priv *priv = &ps->base;
	dma_addr_t paddr;
	void *vaddr;
	int len;

	len = priv->tx_bd_num * sizeof(struct qtnf_pearl_tx_bd) +
		priv->rx_bd_num * sizeof(struct qtnf_pearl_rx_bd);

	vaddr = dmam_alloc_coherent(&priv->pdev->dev, len, &paddr, GFP_KERNEL);
	if (!vaddr)
		return -ENOMEM;

	/* tx bd */

	memset(vaddr, 0, len);

	ps->bd_table_vaddr = vaddr;
	ps->bd_table_paddr = paddr;
	ps->bd_table_len = len;

	ps->tx_bd_vbase = vaddr;
	ps->tx_bd_pbase = paddr;

	pr_debug("TX descriptor table: vaddr=0x%p paddr=%pad\n", vaddr, &paddr);

	priv->tx_bd_r_index = 0;
	priv->tx_bd_w_index = 0;

	/* rx bd */

	vaddr = ((struct qtnf_pearl_tx_bd *)vaddr) + priv->tx_bd_num;
	paddr += priv->tx_bd_num * sizeof(struct qtnf_pearl_tx_bd);

	ps->rx_bd_vbase = vaddr;
	ps->rx_bd_pbase = paddr;

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	writel(QTN_HOST_HI32(paddr),
	       PCIE_HDP_TX_HOST_Q_BASE_H(ps->pcie_reg_base));
#endif
	writel(QTN_HOST_LO32(paddr),
	       PCIE_HDP_TX_HOST_Q_BASE_L(ps->pcie_reg_base));
	writel(priv->rx_bd_num | (sizeof(struct qtnf_pearl_rx_bd)) << 16,
	       PCIE_HDP_TX_HOST_Q_SZ_CTRL(ps->pcie_reg_base));

	pr_debug("RX descriptor table: vaddr=0x%p paddr=%pad\n", vaddr, &paddr);

	return 0;
}

static int pearl_skb2rbd_attach(struct qtnf_pcie_pearl_state *ps, u16 index)
{
	struct qtnf_pcie_bus_priv *priv = &ps->base;
	struct qtnf_pearl_rx_bd *rxbd;
	struct sk_buff *skb;
	dma_addr_t paddr;

	skb = __netdev_alloc_skb_ip_align(NULL, SKB_BUF_SIZE, GFP_ATOMIC);
	if (!skb) {
		priv->rx_skb[index] = NULL;
		return -ENOMEM;
	}

	priv->rx_skb[index] = skb;
	rxbd = &ps->rx_bd_vbase[index];

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
	       PCIE_HDP_HHBM_BUF_PTR_H(ps->pcie_reg_base));
#endif
	writel(QTN_HOST_LO32(paddr),
	       PCIE_HDP_HHBM_BUF_PTR(ps->pcie_reg_base));

	writel(index, PCIE_HDP_TX_HOST_Q_WR_PTR(ps->pcie_reg_base));
	return 0;
}

static int pearl_alloc_rx_buffers(struct qtnf_pcie_pearl_state *ps)
{
	u16 i;
	int ret = 0;

	memset(ps->rx_bd_vbase, 0x0,
	       ps->base.rx_bd_num * sizeof(struct qtnf_pearl_rx_bd));

	for (i = 0; i < ps->base.rx_bd_num; i++) {
		ret = pearl_skb2rbd_attach(ps, i);
		if (ret)
			break;
	}

	return ret;
}

/* all rx/tx activity should have ceased before calling this function */
static void qtnf_pearl_free_xfer_buffers(struct qtnf_pcie_pearl_state *ps)
{
	struct qtnf_pcie_bus_priv *priv = &ps->base;
	struct qtnf_pearl_tx_bd *txbd;
	struct qtnf_pearl_rx_bd *rxbd;
	struct sk_buff *skb;
	dma_addr_t paddr;
	int i;

	/* free rx buffers */
	for (i = 0; i < priv->rx_bd_num; i++) {
		if (priv->rx_skb && priv->rx_skb[i]) {
			rxbd = &ps->rx_bd_vbase[i];
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
			txbd = &ps->tx_bd_vbase[i];
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

static int pearl_hhbm_init(struct qtnf_pcie_pearl_state *ps)
{
	u32 val;

	val = readl(PCIE_HHBM_CONFIG(ps->pcie_reg_base));
	val |= HHBM_CONFIG_SOFT_RESET;
	writel(val, PCIE_HHBM_CONFIG(ps->pcie_reg_base));
	usleep_range(50, 100);
	val &= ~HHBM_CONFIG_SOFT_RESET;
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	val |= HHBM_64BIT;
#endif
	writel(val, PCIE_HHBM_CONFIG(ps->pcie_reg_base));
	writel(ps->base.rx_bd_num, PCIE_HHBM_Q_LIMIT_REG(ps->pcie_reg_base));

	return 0;
}

static int qtnf_pcie_pearl_init_xfer(struct qtnf_pcie_pearl_state *ps,
				     unsigned int tx_bd_size)
{
	struct qtnf_pcie_bus_priv *priv = &ps->base;
	int ret;
	u32 val;

	if (tx_bd_size == 0)
		tx_bd_size = PEARL_TX_BD_SIZE_DEFAULT;

	val = tx_bd_size * sizeof(struct qtnf_pearl_tx_bd);

	if (!is_power_of_2(tx_bd_size) || val > PCIE_HHBM_MAX_SIZE) {
		pr_warn("bad tx_bd_size value %u\n", tx_bd_size);
		priv->tx_bd_num = PEARL_TX_BD_SIZE_DEFAULT;
	} else {
		priv->tx_bd_num = tx_bd_size;
	}

	priv->rx_bd_w_index = 0;
	priv->rx_bd_r_index = 0;

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

	ret = pearl_hhbm_init(ps);
	if (ret) {
		pr_err("failed to init h/w queues\n");
		return ret;
	}

	ret = qtnf_pcie_alloc_skb_array(priv);
	if (ret) {
		pr_err("failed to allocate skb array\n");
		return ret;
	}

	ret = pearl_alloc_bd_table(ps);
	if (ret) {
		pr_err("failed to allocate bd table\n");
		return ret;
	}

	ret = pearl_alloc_rx_buffers(ps);
	if (ret) {
		pr_err("failed to allocate rx buffers\n");
		return ret;
	}

	return ret;
}

static void qtnf_pearl_data_tx_reclaim(struct qtnf_pcie_pearl_state *ps)
{
	struct qtnf_pcie_bus_priv *priv = &ps->base;
	struct qtnf_pearl_tx_bd *txbd;
	struct sk_buff *skb;
	unsigned long flags;
	dma_addr_t paddr;
	u32 tx_done_index;
	int count = 0;
	int i;

	spin_lock_irqsave(&priv->tx_reclaim_lock, flags);

	tx_done_index = readl(PCIE_HDP_RX0DMA_CNT(ps->pcie_reg_base))
			& (priv->tx_bd_num - 1);

	i = priv->tx_bd_r_index;

	while (CIRC_CNT(tx_done_index, i, priv->tx_bd_num)) {
		skb = priv->tx_skb[i];
		if (likely(skb)) {
			txbd = &ps->tx_bd_vbase[i];
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

static int qtnf_tx_queue_ready(struct qtnf_pcie_pearl_state *ps)
{
	struct qtnf_pcie_bus_priv *priv = &ps->base;

	if (!CIRC_SPACE(priv->tx_bd_w_index, priv->tx_bd_r_index,
			priv->tx_bd_num)) {
		qtnf_pearl_data_tx_reclaim(ps);

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
	struct qtnf_pcie_pearl_state *ps = get_bus_priv(bus);
	struct qtnf_pcie_bus_priv *priv = &ps->base;
	dma_addr_t txbd_paddr, skb_paddr;
	struct qtnf_pearl_tx_bd *txbd;
	unsigned long flags;
	int len, i;
	u32 info;
	int ret = 0;

	spin_lock_irqsave(&priv->tx_lock, flags);

	if (!qtnf_tx_queue_ready(ps)) {
		if (skb->dev) {
			netif_tx_stop_all_queues(skb->dev);
			priv->tx_stopped = 1;
		}

		spin_unlock_irqrestore(&priv->tx_lock, flags);
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

	txbd = &ps->tx_bd_vbase[i];
	txbd->addr = cpu_to_le32(QTN_HOST_LO32(skb_paddr));
	txbd->addr_h = cpu_to_le32(QTN_HOST_HI32(skb_paddr));

	info = (len & QTN_PCIE_TX_DESC_LEN_MASK) << QTN_PCIE_TX_DESC_LEN_SHIFT;
	txbd->info = cpu_to_le32(info);

	/* sync up all descriptor updates before passing them to EP */
	dma_wmb();

	/* write new TX descriptor to PCIE_RX_FIFO on EP */
	txbd_paddr = ps->tx_bd_pbase + i * sizeof(struct qtnf_pearl_tx_bd);

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	writel(QTN_HOST_HI32(txbd_paddr),
	       PCIE_HDP_HOST_WR_DESC0_H(ps->pcie_reg_base));
#endif
	writel(QTN_HOST_LO32(txbd_paddr),
	       PCIE_HDP_HOST_WR_DESC0(ps->pcie_reg_base));

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
	spin_unlock_irqrestore(&priv->tx_lock, flags);

	qtnf_pearl_data_tx_reclaim(ps);

	return NETDEV_TX_OK;
}

static irqreturn_t qtnf_pcie_pearl_interrupt(int irq, void *data)
{
	struct qtnf_bus *bus = (struct qtnf_bus *)data;
	struct qtnf_pcie_pearl_state *ps = get_bus_priv(bus);
	struct qtnf_pcie_bus_priv *priv = &ps->base;
	u32 status;

	priv->pcie_irq_count++;
	status = readl(PCIE_HDP_INT_STATUS(ps->pcie_reg_base));

	qtnf_shm_ipc_irq_handler(&priv->shm_ipc_ep_in);
	qtnf_shm_ipc_irq_handler(&priv->shm_ipc_ep_out);

	if (!(status & ps->pcie_irq_mask))
		goto irq_done;

	if (status & PCIE_HDP_INT_RX_BITS)
		ps->pcie_irq_rx_count++;

	if (status & PCIE_HDP_INT_TX_BITS)
		ps->pcie_irq_tx_count++;

	if (status & PCIE_HDP_INT_HHBM_UF)
		ps->pcie_irq_uf_count++;

	if (status & PCIE_HDP_INT_RX_BITS) {
		qtnf_dis_rxdone_irq(ps);
		napi_schedule(&bus->mux_napi);
	}

	if (status & PCIE_HDP_INT_TX_BITS) {
		qtnf_dis_txdone_irq(ps);
		tasklet_hi_schedule(&priv->reclaim_tq);
	}

irq_done:
	/* H/W workaround: clean all bits, not only enabled */
	qtnf_non_posted_write(~0U, PCIE_HDP_INT_STATUS(ps->pcie_reg_base));

	if (!priv->msi_enabled)
		qtnf_deassert_intx(ps);

	return IRQ_HANDLED;
}

static int qtnf_rx_data_ready(struct qtnf_pcie_pearl_state *ps)
{
	u16 index = ps->base.rx_bd_r_index;
	struct qtnf_pearl_rx_bd *rxbd;
	u32 descw;

	rxbd = &ps->rx_bd_vbase[index];
	descw = le32_to_cpu(rxbd->info);

	if (descw & QTN_TXDONE_MASK)
		return 1;

	return 0;
}

static int qtnf_pcie_pearl_rx_poll(struct napi_struct *napi, int budget)
{
	struct qtnf_bus *bus = container_of(napi, struct qtnf_bus, mux_napi);
	struct qtnf_pcie_pearl_state *ps = get_bus_priv(bus);
	struct qtnf_pcie_bus_priv *priv = &ps->base;
	struct net_device *ndev = NULL;
	struct sk_buff *skb = NULL;
	int processed = 0;
	struct qtnf_pearl_rx_bd *rxbd;
	dma_addr_t skb_paddr;
	int consume;
	u32 descw;
	u32 psize;
	u16 r_idx;
	u16 w_idx;
	int ret;

	while (processed < budget) {
		if (!qtnf_rx_data_ready(ps))
			goto rx_out;

		r_idx = priv->rx_bd_r_index;
		rxbd = &ps->rx_bd_vbase[r_idx];
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

			ret = pearl_skb2rbd_attach(ps, w_idx);
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
		qtnf_en_rxdone_irq(ps);
	}

	return processed;
}

static void
qtnf_pcie_data_tx_timeout(struct qtnf_bus *bus, struct net_device *ndev)
{
	struct qtnf_pcie_pearl_state *ps = (void *)get_bus_priv(bus);

	tasklet_hi_schedule(&ps->base.reclaim_tq);
}

static void qtnf_pcie_data_rx_start(struct qtnf_bus *bus)
{
	struct qtnf_pcie_pearl_state *ps = (void *)get_bus_priv(bus);

	qtnf_enable_hdp_irqs(ps);
	napi_enable(&bus->mux_napi);
}

static void qtnf_pcie_data_rx_stop(struct qtnf_bus *bus)
{
	struct qtnf_pcie_pearl_state *ps = (void *)get_bus_priv(bus);

	napi_disable(&bus->mux_napi);
	qtnf_disable_hdp_irqs(ps);
}

static const struct qtnf_bus_ops qtnf_pcie_pearl_bus_ops = {
	/* control path methods */
	.control_tx	= qtnf_pcie_control_tx,

	/* data path methods */
	.data_tx		= qtnf_pcie_data_tx,
	.data_tx_timeout	= qtnf_pcie_data_tx_timeout,
	.data_rx_start		= qtnf_pcie_data_rx_start,
	.data_rx_stop		= qtnf_pcie_data_rx_stop,
};

static int qtnf_dbg_irq_stats(struct seq_file *s, void *data)
{
	struct qtnf_bus *bus = dev_get_drvdata(s->private);
	struct qtnf_pcie_pearl_state *ps = get_bus_priv(bus);
	u32 reg = readl(PCIE_HDP_INT_EN(ps->pcie_reg_base));
	u32 status;

	seq_printf(s, "pcie_irq_count(%u)\n", ps->base.pcie_irq_count);
	seq_printf(s, "pcie_irq_tx_count(%u)\n", ps->pcie_irq_tx_count);
	status = reg &  PCIE_HDP_INT_TX_BITS;
	seq_printf(s, "pcie_irq_tx_status(%s)\n",
		   (status == PCIE_HDP_INT_TX_BITS) ? "EN" : "DIS");
	seq_printf(s, "pcie_irq_rx_count(%u)\n", ps->pcie_irq_rx_count);
	status = reg &  PCIE_HDP_INT_RX_BITS;
	seq_printf(s, "pcie_irq_rx_status(%s)\n",
		   (status == PCIE_HDP_INT_RX_BITS) ? "EN" : "DIS");
	seq_printf(s, "pcie_irq_uf_count(%u)\n", ps->pcie_irq_uf_count);
	status = reg &  PCIE_HDP_INT_HHBM_UF;
	seq_printf(s, "pcie_irq_hhbm_uf_status(%s)\n",
		   (status == PCIE_HDP_INT_HHBM_UF) ? "EN" : "DIS");

	return 0;
}

static int qtnf_dbg_hdp_stats(struct seq_file *s, void *data)
{
	struct qtnf_bus *bus = dev_get_drvdata(s->private);
	struct qtnf_pcie_pearl_state *ps = get_bus_priv(bus);
	struct qtnf_pcie_bus_priv *priv = &ps->base;

	seq_printf(s, "tx_full_count(%u)\n", priv->tx_full_count);
	seq_printf(s, "tx_done_count(%u)\n", priv->tx_done_count);
	seq_printf(s, "tx_reclaim_done(%u)\n", priv->tx_reclaim_done);
	seq_printf(s, "tx_reclaim_req(%u)\n", priv->tx_reclaim_req);

	seq_printf(s, "tx_bd_r_index(%u)\n", priv->tx_bd_r_index);
	seq_printf(s, "tx_bd_p_index(%u)\n",
		   readl(PCIE_HDP_RX0DMA_CNT(ps->pcie_reg_base))
			& (priv->tx_bd_num - 1));
	seq_printf(s, "tx_bd_w_index(%u)\n", priv->tx_bd_w_index);
	seq_printf(s, "tx queue len(%u)\n",
		   CIRC_CNT(priv->tx_bd_w_index, priv->tx_bd_r_index,
			    priv->tx_bd_num));

	seq_printf(s, "rx_bd_r_index(%u)\n", priv->rx_bd_r_index);
	seq_printf(s, "rx_bd_p_index(%u)\n",
		   readl(PCIE_HDP_TX0DMA_CNT(ps->pcie_reg_base))
			& (priv->rx_bd_num - 1));
	seq_printf(s, "rx_bd_w_index(%u)\n", priv->rx_bd_w_index);
	seq_printf(s, "rx alloc queue len(%u)\n",
		   CIRC_SPACE(priv->rx_bd_w_index, priv->rx_bd_r_index,
			      priv->rx_bd_num));

	return 0;
}

static int qtnf_ep_fw_send(struct pci_dev *pdev, uint32_t size,
			   int blk, const u8 *pblk, const u8 *fw)
{
	struct qtnf_bus *bus = pci_get_drvdata(pdev);

	struct qtnf_pearl_fw_hdr *hdr;
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

	hdr = (struct qtnf_pearl_fw_hdr *)skb->data;
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
qtnf_ep_fw_load(struct qtnf_pcie_pearl_state *ps, const u8 *fw, u32 fw_size)
{
	int blk_size = QTN_PCIE_FW_BUFSZ - sizeof(struct qtnf_pearl_fw_hdr);
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

		len = qtnf_ep_fw_send(ps->base.pdev, fw_size, blk, pblk, fw);
		if (len <= 0)
			continue;

		if (!((blk + 1) & QTN_PCIE_FW_DLMASK) ||
		    (blk == (blk_count - 1))) {
			qtnf_set_state(&ps->bda->bda_rc_state,
				       QTN_RC_FW_SYNC);
			if (qtnf_poll_state(&ps->bda->bda_ep_state,
					    QTN_EP_FW_SYNC,
					    QTN_FW_DL_TIMEOUT_MS)) {
				pr_err("FW upload failed: SYNC timed out\n");
				return -ETIMEDOUT;
			}

			qtnf_clear_state(&ps->bda->bda_ep_state,
					 QTN_EP_FW_SYNC);

			if (qtnf_is_state(&ps->bda->bda_ep_state,
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

				qtnf_clear_state(&ps->bda->bda_ep_state,
						 QTN_EP_FW_RETRY);

				pr_warn("FW upload retry: block #%d\n", blk);
				continue;
			}

			qtnf_pearl_data_tx_reclaim(ps);
		}

		pblk += len;
		blk++;
	}

	pr_debug("FW upload completed: totally sent %d blocks\n", blk);
	return 0;
}

static void qtnf_pearl_fw_work_handler(struct work_struct *work)
{
	struct qtnf_bus *bus = container_of(work, struct qtnf_bus, fw_work);
	struct qtnf_pcie_pearl_state *ps = (void *)get_bus_priv(bus);
	u32 state = QTN_RC_FW_LOADRDY | QTN_RC_FW_QLINK;
	const char *fwname = QTN_PCI_PEARL_FW_NAME;
	struct pci_dev *pdev = ps->base.pdev;
	const struct firmware *fw;
	int ret;

	if (ps->base.flashboot) {
		state |= QTN_RC_FW_FLASHBOOT;
	} else {
		ret = request_firmware(&fw, fwname, &pdev->dev);
		if (ret < 0) {
			pr_err("failed to get firmware %s\n", fwname);
			goto fw_load_exit;
		}
	}

	qtnf_set_state(&ps->bda->bda_rc_state, state);

	if (qtnf_poll_state(&ps->bda->bda_ep_state, QTN_EP_FW_LOADRDY,
			    QTN_FW_DL_TIMEOUT_MS)) {
		pr_err("card is not ready\n");

		if (!ps->base.flashboot)
			release_firmware(fw);

		goto fw_load_exit;
	}

	qtnf_clear_state(&ps->bda->bda_ep_state, QTN_EP_FW_LOADRDY);

	if (ps->base.flashboot) {
		pr_info("booting firmware from flash\n");

	} else {
		pr_info("starting firmware upload: %s\n", fwname);

		ret = qtnf_ep_fw_load(ps, fw->data, fw->size);
		release_firmware(fw);
		if (ret) {
			pr_err("firmware upload error\n");
			goto fw_load_exit;
		}
	}

	if (qtnf_poll_state(&ps->bda->bda_ep_state, QTN_EP_FW_DONE,
			    QTN_FW_DL_TIMEOUT_MS)) {
		pr_err("firmware bringup timed out\n");
		goto fw_load_exit;
	}

	if (qtnf_poll_state(&ps->bda->bda_ep_state,
			    QTN_EP_FW_QLINK_DONE, QTN_FW_QLINK_TIMEOUT_MS)) {
		pr_err("firmware runtime failure\n");
		goto fw_load_exit;
	}

	pr_info("firmware is up and running\n");

	ret = qtnf_pcie_fw_boot_done(bus);
	if (ret)
		goto fw_load_exit;

	qtnf_debugfs_add_entry(bus, "hdp_stats", qtnf_dbg_hdp_stats);
	qtnf_debugfs_add_entry(bus, "irq_stats", qtnf_dbg_irq_stats);

fw_load_exit:
	put_device(&pdev->dev);
}

static void qtnf_pearl_reclaim_tasklet_fn(unsigned long data)
{
	struct qtnf_pcie_pearl_state *ps = (void *)data;

	qtnf_pearl_data_tx_reclaim(ps);
	qtnf_en_txdone_irq(ps);
}

static u64 qtnf_pearl_dma_mask_get(void)
{
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	return DMA_BIT_MASK(64);
#else
	return DMA_BIT_MASK(32);
#endif
}

static int qtnf_pcie_pearl_probe(struct qtnf_bus *bus, unsigned int tx_bd_size)
{
	struct qtnf_shm_ipc_int ipc_int;
	struct qtnf_pcie_pearl_state *ps = get_bus_priv(bus);
	struct pci_dev *pdev = ps->base.pdev;
	int ret;

	bus->bus_ops = &qtnf_pcie_pearl_bus_ops;
	spin_lock_init(&ps->irq_lock);
	INIT_WORK(&bus->fw_work, qtnf_pearl_fw_work_handler);

	ps->pcie_reg_base = ps->base.dmareg_bar;
	ps->bda = ps->base.epmem_bar;
	writel(ps->base.msi_enabled, &ps->bda->bda_rc_msi_enabled);

	ret = qtnf_pcie_pearl_init_xfer(ps, tx_bd_size);
	if (ret) {
		pr_err("PCIE xfer init failed\n");
		return ret;
	}

	/* init default irq settings */
	qtnf_init_hdp_irqs(ps);

	/* start with disabled irqs */
	qtnf_disable_hdp_irqs(ps);

	ret = devm_request_irq(&pdev->dev, pdev->irq,
			       &qtnf_pcie_pearl_interrupt, 0,
			       "qtnf_pearl_irq", (void *)bus);
	if (ret) {
		pr_err("failed to request pcie irq %d\n", pdev->irq);
		qtnf_pearl_free_xfer_buffers(ps);
		return ret;
	}

	tasklet_init(&ps->base.reclaim_tq, qtnf_pearl_reclaim_tasklet_fn,
		     (unsigned long)ps);
	netif_napi_add(&bus->mux_dev, &bus->mux_napi,
		       qtnf_pcie_pearl_rx_poll, 10);

	ipc_int.fn = qtnf_pcie_pearl_ipc_gen_ep_int;
	ipc_int.arg = ps;
	qtnf_pcie_init_shm_ipc(&ps->base, &ps->bda->bda_shm_reg1,
			       &ps->bda->bda_shm_reg2, &ipc_int);

	return 0;
}

static void qtnf_pcie_pearl_remove(struct qtnf_bus *bus)
{
	struct qtnf_pcie_pearl_state *ps = get_bus_priv(bus);

	qtnf_pearl_reset_ep(ps);
	qtnf_pearl_free_xfer_buffers(ps);
}

#ifdef CONFIG_PM_SLEEP
static int qtnf_pcie_pearl_suspend(struct qtnf_bus *bus)
{
	return -EOPNOTSUPP;
}

static int qtnf_pcie_pearl_resume(struct qtnf_bus *bus)
{
	return 0;
}
#endif

struct qtnf_bus *qtnf_pcie_pearl_alloc(struct pci_dev *pdev)
{
	struct qtnf_bus *bus;
	struct qtnf_pcie_pearl_state *ps;

	bus = devm_kzalloc(&pdev->dev, sizeof(*bus) + sizeof(*ps), GFP_KERNEL);
	if (!bus)
		return NULL;

	ps = get_bus_priv(bus);
	ps->base.probe_cb = qtnf_pcie_pearl_probe;
	ps->base.remove_cb = qtnf_pcie_pearl_remove;
	ps->base.dma_mask_get_cb = qtnf_pearl_dma_mask_get;
#ifdef CONFIG_PM_SLEEP
	ps->base.resume_cb = qtnf_pcie_pearl_resume;
	ps->base.suspend_cb = qtnf_pcie_pearl_suspend;
#endif

	return bus;
}
