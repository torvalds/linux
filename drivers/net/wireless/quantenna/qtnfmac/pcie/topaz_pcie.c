// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2018 Quantenna Communications */

#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/crc32.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/circ_buf.h>

#include "pcie_priv.h"
#include "topaz_pcie_regs.h"
#include "topaz_pcie_ipc.h"
#include "qtn_hw_ids.h"
#include "core.h"
#include "bus.h"
#include "shm_ipc.h"
#include "debug.h"

#define TOPAZ_TX_BD_SIZE_DEFAULT	128

struct qtnf_topaz_tx_bd {
	__le32 addr;
	__le32 info;
} __packed;

struct qtnf_topaz_rx_bd {
	__le32 addr;
	__le32 info;
} __packed;

struct qtnf_extra_bd_params {
	__le32 param1;
	__le32 param2;
	__le32 param3;
	__le32 param4;
} __packed;

#define QTNF_BD_PARAM_OFFSET(n)	offsetof(struct qtnf_extra_bd_params, param##n)

struct vmac_pkt_info {
	__le32 addr;
	__le32 info;
};

struct qtnf_topaz_bda {
	__le16	bda_len;
	__le16	bda_version;
	__le32	bda_bootstate;
	__le32	bda_dma_mask;
	__le32	bda_dma_offset;
	__le32	bda_flags;
	__le32	bda_img;
	__le32	bda_img_size;
	__le32	bda_ep2h_irqstatus;
	__le32	bda_h2ep_irqstatus;
	__le32	bda_msi_addr;
	u8	reserved1[56];
	__le32	bda_flashsz;
	u8	bda_boardname[PCIE_BDA_NAMELEN];
	__le32	bda_pci_pre_status;
	__le32	bda_pci_endian;
	__le32	bda_pci_post_status;
	__le32	bda_h2ep_txd_budget;
	__le32	bda_ep2h_txd_budget;
	__le32	bda_rc_rx_bd_base;
	__le32	bda_rc_rx_bd_num;
	__le32	bda_rc_tx_bd_base;
	__le32	bda_rc_tx_bd_num;
	u8	bda_ep_link_state;
	u8	bda_rc_link_state;
	u8	bda_rc_msi_enabled;
	u8	reserved2;
	__le32	bda_ep_next_pkt;
	struct vmac_pkt_info request[QTN_PCIE_RC_TX_QUEUE_LEN];
	struct qtnf_shm_ipc_region bda_shm_reg1 __aligned(4096);
	struct qtnf_shm_ipc_region bda_shm_reg2 __aligned(4096);
} __packed;

struct qtnf_pcie_topaz_state {
	struct qtnf_pcie_bus_priv base;
	struct qtnf_topaz_bda __iomem *bda;

	dma_addr_t dma_msi_dummy;
	u32 dma_msi_imwr;

	struct qtnf_topaz_tx_bd *tx_bd_vbase;
	struct qtnf_topaz_rx_bd *rx_bd_vbase;

	__le32 __iomem *ep_next_rx_pkt;
	__le32 __iomem *txqueue_wake;
	__le32 __iomem *ep_pmstate;

	unsigned long rx_pkt_count;
};

static void qtnf_deassert_intx(struct qtnf_pcie_topaz_state *ts)
{
	void __iomem *reg = ts->base.sysctl_bar + TOPAZ_PCIE_CFG0_OFFSET;
	u32 cfg;

	cfg = readl(reg);
	cfg &= ~TOPAZ_ASSERT_INTX;
	qtnf_non_posted_write(cfg, reg);
}

static inline int qtnf_topaz_intx_asserted(struct qtnf_pcie_topaz_state *ts)
{
	void __iomem *reg = ts->base.sysctl_bar + TOPAZ_PCIE_CFG0_OFFSET;
	u32 cfg = readl(reg);

	return !!(cfg & TOPAZ_ASSERT_INTX);
}

static void qtnf_topaz_reset_ep(struct qtnf_pcie_topaz_state *ts)
{
	writel(TOPAZ_IPC_IRQ_WORD(TOPAZ_RC_RST_EP_IRQ),
	       TOPAZ_LH_IPC4_INT(ts->base.sysctl_bar));
	msleep(QTN_EP_RESET_WAIT_MS);
	pci_restore_state(ts->base.pdev);
}

static void setup_rx_irqs(struct qtnf_pcie_topaz_state *ts)
{
	void __iomem *reg = PCIE_DMA_WR_DONE_IMWR_ADDR_LOW(ts->base.dmareg_bar);

	ts->dma_msi_imwr = readl(reg);
}

static void enable_rx_irqs(struct qtnf_pcie_topaz_state *ts)
{
	void __iomem *reg = PCIE_DMA_WR_DONE_IMWR_ADDR_LOW(ts->base.dmareg_bar);

	qtnf_non_posted_write(ts->dma_msi_imwr, reg);
}

static void disable_rx_irqs(struct qtnf_pcie_topaz_state *ts)
{
	void __iomem *reg = PCIE_DMA_WR_DONE_IMWR_ADDR_LOW(ts->base.dmareg_bar);

	qtnf_non_posted_write(QTN_HOST_LO32(ts->dma_msi_dummy), reg);
}

static void qtnf_topaz_ipc_gen_ep_int(void *arg)
{
	struct qtnf_pcie_topaz_state *ts = arg;

	writel(TOPAZ_IPC_IRQ_WORD(TOPAZ_RC_CTRL_IRQ),
	       TOPAZ_CTL_M2L_INT(ts->base.sysctl_bar));
}

static int qtnf_is_state(__le32 __iomem *reg, u32 state)
{
	u32 s = readl(reg);

	return (s == state);
}

static void qtnf_set_state(__le32 __iomem *reg, u32 state)
{
	qtnf_non_posted_write(state, reg);
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

static int topaz_alloc_bd_table(struct qtnf_pcie_topaz_state *ts,
				struct qtnf_topaz_bda __iomem *bda)
{
	struct qtnf_extra_bd_params __iomem *extra_params;
	struct qtnf_pcie_bus_priv *priv = &ts->base;
	dma_addr_t paddr;
	void *vaddr;
	int len;
	int i;

	/* bd table */

	len = priv->tx_bd_num * sizeof(struct qtnf_topaz_tx_bd) +
		priv->rx_bd_num * sizeof(struct qtnf_topaz_rx_bd) +
			sizeof(struct qtnf_extra_bd_params);

	vaddr = dmam_alloc_coherent(&priv->pdev->dev, len, &paddr, GFP_KERNEL);
	if (!vaddr)
		return -ENOMEM;

	memset(vaddr, 0, len);

	/* tx bd */

	ts->tx_bd_vbase = vaddr;
	qtnf_non_posted_write(paddr, &bda->bda_rc_tx_bd_base);

	for (i = 0; i < priv->tx_bd_num; i++)
		ts->tx_bd_vbase[i].info |= cpu_to_le32(QTN_BD_EMPTY);

	pr_debug("TX descriptor table: vaddr=0x%p paddr=%pad\n", vaddr, &paddr);

	priv->tx_bd_r_index = 0;
	priv->tx_bd_w_index = 0;

	/* rx bd */

	vaddr = ((struct qtnf_topaz_tx_bd *)vaddr) + priv->tx_bd_num;
	paddr += priv->tx_bd_num * sizeof(struct qtnf_topaz_tx_bd);

	ts->rx_bd_vbase = vaddr;
	qtnf_non_posted_write(paddr, &bda->bda_rc_rx_bd_base);

	pr_debug("RX descriptor table: vaddr=0x%p paddr=%pad\n", vaddr, &paddr);

	/* extra shared params */

	vaddr = ((struct qtnf_topaz_rx_bd *)vaddr) + priv->rx_bd_num;
	paddr += priv->rx_bd_num * sizeof(struct qtnf_topaz_rx_bd);

	extra_params = (struct qtnf_extra_bd_params __iomem *)vaddr;

	ts->ep_next_rx_pkt = &extra_params->param1;
	qtnf_non_posted_write(paddr + QTNF_BD_PARAM_OFFSET(1),
			      &bda->bda_ep_next_pkt);
	ts->txqueue_wake = &extra_params->param2;
	ts->ep_pmstate = &extra_params->param3;
	ts->dma_msi_dummy = paddr + QTNF_BD_PARAM_OFFSET(4);

	return 0;
}

static int
topaz_skb2rbd_attach(struct qtnf_pcie_topaz_state *ts, u16 index, u32 wrap)
{
	struct qtnf_topaz_rx_bd *rxbd = &ts->rx_bd_vbase[index];
	struct sk_buff *skb;
	dma_addr_t paddr;

	skb = __netdev_alloc_skb_ip_align(NULL, SKB_BUF_SIZE, GFP_ATOMIC);
	if (!skb) {
		ts->base.rx_skb[index] = NULL;
		return -ENOMEM;
	}

	ts->base.rx_skb[index] = skb;

	paddr = pci_map_single(ts->base.pdev, skb->data,
			       SKB_BUF_SIZE, PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(ts->base.pdev, paddr)) {
		pr_err("skb mapping error: %pad\n", &paddr);
		return -ENOMEM;
	}

	rxbd->addr = cpu_to_le32(QTN_HOST_LO32(paddr));
	rxbd->info = cpu_to_le32(QTN_BD_EMPTY | wrap);

	ts->base.rx_bd_w_index = index;

	return 0;
}

static int topaz_alloc_rx_buffers(struct qtnf_pcie_topaz_state *ts)
{
	u16 i;
	int ret = 0;

	memset(ts->rx_bd_vbase, 0x0,
	       ts->base.rx_bd_num * sizeof(struct qtnf_topaz_rx_bd));

	for (i = 0; i < ts->base.rx_bd_num; i++) {
		ret = topaz_skb2rbd_attach(ts, i, 0);
		if (ret)
			break;
	}

	ts->rx_bd_vbase[ts->base.rx_bd_num - 1].info |=
						cpu_to_le32(QTN_BD_WRAP);

	return ret;
}

/* all rx/tx activity should have ceased before calling this function */
static void qtnf_topaz_free_xfer_buffers(struct qtnf_pcie_topaz_state *ts)
{
	struct qtnf_pcie_bus_priv *priv = &ts->base;
	struct qtnf_topaz_rx_bd *rxbd;
	struct qtnf_topaz_tx_bd *txbd;
	struct sk_buff *skb;
	dma_addr_t paddr;
	int i;

	/* free rx buffers */
	for (i = 0; i < priv->rx_bd_num; i++) {
		if (priv->rx_skb && priv->rx_skb[i]) {
			rxbd = &ts->rx_bd_vbase[i];
			skb = priv->rx_skb[i];
			paddr = QTN_HOST_ADDR(0x0, le32_to_cpu(rxbd->addr));
			pci_unmap_single(priv->pdev, paddr, SKB_BUF_SIZE,
					 PCI_DMA_FROMDEVICE);
			dev_kfree_skb_any(skb);
			priv->rx_skb[i] = NULL;
			rxbd->addr = 0;
			rxbd->info = 0;
		}
	}

	/* free tx buffers */
	for (i = 0; i < priv->tx_bd_num; i++) {
		if (priv->tx_skb && priv->tx_skb[i]) {
			txbd = &ts->tx_bd_vbase[i];
			skb = priv->tx_skb[i];
			paddr = QTN_HOST_ADDR(0x0, le32_to_cpu(txbd->addr));
			pci_unmap_single(priv->pdev, paddr, SKB_BUF_SIZE,
					 PCI_DMA_TODEVICE);
			dev_kfree_skb_any(skb);
			priv->tx_skb[i] = NULL;
			txbd->addr = 0;
			txbd->info = 0;
		}
	}
}

static int qtnf_pcie_topaz_init_xfer(struct qtnf_pcie_topaz_state *ts,
				     unsigned int tx_bd_size)
{
	struct qtnf_topaz_bda __iomem *bda = ts->bda;
	struct qtnf_pcie_bus_priv *priv = &ts->base;
	int ret;

	if (tx_bd_size == 0)
		tx_bd_size = TOPAZ_TX_BD_SIZE_DEFAULT;

	/* check TX BD queue max length according to struct qtnf_topaz_bda */
	if (tx_bd_size > QTN_PCIE_RC_TX_QUEUE_LEN) {
		pr_warn("TX BD queue cannot exceed %d\n",
			QTN_PCIE_RC_TX_QUEUE_LEN);
		tx_bd_size = QTN_PCIE_RC_TX_QUEUE_LEN;
	}

	priv->tx_bd_num = tx_bd_size;
	qtnf_non_posted_write(priv->tx_bd_num, &bda->bda_rc_tx_bd_num);
	qtnf_non_posted_write(priv->rx_bd_num, &bda->bda_rc_rx_bd_num);

	priv->rx_bd_w_index = 0;
	priv->rx_bd_r_index = 0;

	ret = qtnf_pcie_alloc_skb_array(priv);
	if (ret) {
		pr_err("failed to allocate skb array\n");
		return ret;
	}

	ret = topaz_alloc_bd_table(ts, bda);
	if (ret) {
		pr_err("failed to allocate bd table\n");
		return ret;
	}

	ret = topaz_alloc_rx_buffers(ts);
	if (ret) {
		pr_err("failed to allocate rx buffers\n");
		return ret;
	}

	return ret;
}

static void qtnf_topaz_data_tx_reclaim(struct qtnf_pcie_topaz_state *ts)
{
	struct qtnf_pcie_bus_priv *priv = &ts->base;
	struct qtnf_topaz_tx_bd *txbd;
	struct sk_buff *skb;
	unsigned long flags;
	dma_addr_t paddr;
	u32 tx_done_index;
	int count = 0;
	int i;

	spin_lock_irqsave(&priv->tx_reclaim_lock, flags);

	tx_done_index = readl(ts->ep_next_rx_pkt);
	i = priv->tx_bd_r_index;

	if (CIRC_CNT(priv->tx_bd_w_index, tx_done_index, priv->tx_bd_num))
		writel(TOPAZ_IPC_IRQ_WORD(TOPAZ_RC_TX_DONE_IRQ),
		       TOPAZ_LH_IPC4_INT(priv->sysctl_bar));

	while (CIRC_CNT(tx_done_index, i, priv->tx_bd_num)) {
		skb = priv->tx_skb[i];

		if (likely(skb)) {
			txbd = &ts->tx_bd_vbase[i];
			paddr = QTN_HOST_ADDR(0x0, le32_to_cpu(txbd->addr));
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

static void qtnf_try_stop_xmit(struct qtnf_bus *bus, struct net_device *ndev)
{
	struct qtnf_pcie_topaz_state *ts = (void *)get_bus_priv(bus);

	if (ndev) {
		netif_tx_stop_all_queues(ndev);
		ts->base.tx_stopped = 1;
	}

	writel(0x0, ts->txqueue_wake);

	/* sync up tx queue status before generating interrupt */
	dma_wmb();

	/* send irq to card: tx stopped */
	writel(TOPAZ_IPC_IRQ_WORD(TOPAZ_RC_TX_STOP_IRQ),
	       TOPAZ_LH_IPC4_INT(ts->base.sysctl_bar));

	/* schedule reclaim attempt */
	tasklet_hi_schedule(&ts->base.reclaim_tq);
}

static void qtnf_try_wake_xmit(struct qtnf_bus *bus, struct net_device *ndev)
{
	struct qtnf_pcie_topaz_state *ts = get_bus_priv(bus);
	int ready;

	ready = readl(ts->txqueue_wake);
	if (ready) {
		netif_wake_queue(ndev);
	} else {
		/* re-send irq to card: tx stopped */
		writel(TOPAZ_IPC_IRQ_WORD(TOPAZ_RC_TX_STOP_IRQ),
		       TOPAZ_LH_IPC4_INT(ts->base.sysctl_bar));
	}
}

static int qtnf_tx_queue_ready(struct qtnf_pcie_topaz_state *ts)
{
	struct qtnf_pcie_bus_priv *priv = &ts->base;

	if (!CIRC_SPACE(priv->tx_bd_w_index, priv->tx_bd_r_index,
			priv->tx_bd_num)) {
		qtnf_topaz_data_tx_reclaim(ts);

		if (!CIRC_SPACE(priv->tx_bd_w_index, priv->tx_bd_r_index,
				priv->tx_bd_num)) {
			priv->tx_full_count++;
			return 0;
		}
	}

	return 1;
}

static int qtnf_pcie_data_tx(struct qtnf_bus *bus, struct sk_buff *skb)
{
	struct qtnf_pcie_topaz_state *ts = (void *)get_bus_priv(bus);
	struct qtnf_pcie_bus_priv *priv = &ts->base;
	struct qtnf_topaz_bda __iomem *bda = ts->bda;
	struct qtnf_topaz_tx_bd *txbd;
	dma_addr_t skb_paddr;
	unsigned long flags;
	int ret = 0;
	int len;
	int i;

	if (unlikely(skb->protocol == htons(ETH_P_PAE))) {
		qtnf_packet_send_hi_pri(skb);
		qtnf_update_tx_stats(skb->dev, skb);
		priv->tx_eapol++;
		return NETDEV_TX_OK;
	}

	spin_lock_irqsave(&priv->tx_lock, flags);

	if (!qtnf_tx_queue_ready(ts)) {
		qtnf_try_stop_xmit(bus, skb->dev);
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return NETDEV_TX_BUSY;
	}

	i = priv->tx_bd_w_index;
	priv->tx_skb[i] = skb;
	len = skb->len;

	skb_paddr = pci_map_single(priv->pdev, skb->data,
				   skb->len, PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(priv->pdev, skb_paddr)) {
		ret = -ENOMEM;
		goto tx_done;
	}

	txbd = &ts->tx_bd_vbase[i];
	txbd->addr = cpu_to_le32(QTN_HOST_LO32(skb_paddr));

	writel(QTN_HOST_LO32(skb_paddr), &bda->request[i].addr);
	writel(len | QTN_PCIE_TX_VALID_PKT, &bda->request[i].info);

	/* sync up descriptor updates before generating interrupt */
	dma_wmb();

	/* generate irq to card: tx done */
	writel(TOPAZ_IPC_IRQ_WORD(TOPAZ_RC_TX_DONE_IRQ),
	       TOPAZ_LH_IPC4_INT(priv->sysctl_bar));

	if (++i >= priv->tx_bd_num)
		i = 0;

	priv->tx_bd_w_index = i;

tx_done:
	if (ret) {
		if (skb->dev)
			skb->dev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
	}

	priv->tx_done_count++;
	spin_unlock_irqrestore(&priv->tx_lock, flags);

	qtnf_topaz_data_tx_reclaim(ts);

	return NETDEV_TX_OK;
}

static irqreturn_t qtnf_pcie_topaz_interrupt(int irq, void *data)
{
	struct qtnf_bus *bus = (struct qtnf_bus *)data;
	struct qtnf_pcie_topaz_state *ts = (void *)get_bus_priv(bus);
	struct qtnf_pcie_bus_priv *priv = &ts->base;

	if (!priv->msi_enabled && !qtnf_topaz_intx_asserted(ts))
		return IRQ_NONE;

	if (!priv->msi_enabled)
		qtnf_deassert_intx(ts);

	priv->pcie_irq_count++;

	qtnf_shm_ipc_irq_handler(&priv->shm_ipc_ep_in);
	qtnf_shm_ipc_irq_handler(&priv->shm_ipc_ep_out);

	if (napi_schedule_prep(&bus->mux_napi)) {
		disable_rx_irqs(ts);
		__napi_schedule(&bus->mux_napi);
	}

	tasklet_hi_schedule(&priv->reclaim_tq);

	return IRQ_HANDLED;
}

static int qtnf_rx_data_ready(struct qtnf_pcie_topaz_state *ts)
{
	u16 index = ts->base.rx_bd_r_index;
	struct qtnf_topaz_rx_bd *rxbd;
	u32 descw;

	rxbd = &ts->rx_bd_vbase[index];
	descw = le32_to_cpu(rxbd->info);

	if (descw & QTN_BD_EMPTY)
		return 0;

	return 1;
}

static int qtnf_topaz_rx_poll(struct napi_struct *napi, int budget)
{
	struct qtnf_bus *bus = container_of(napi, struct qtnf_bus, mux_napi);
	struct qtnf_pcie_topaz_state *ts = (void *)get_bus_priv(bus);
	struct qtnf_pcie_bus_priv *priv = &ts->base;
	struct net_device *ndev = NULL;
	struct sk_buff *skb = NULL;
	int processed = 0;
	struct qtnf_topaz_rx_bd *rxbd;
	dma_addr_t skb_paddr;
	int consume;
	u32 descw;
	u32 poffset;
	u32 psize;
	u16 r_idx;
	u16 w_idx;
	int ret;

	while (processed < budget) {
		if (!qtnf_rx_data_ready(ts))
			goto rx_out;

		r_idx = priv->rx_bd_r_index;
		rxbd = &ts->rx_bd_vbase[r_idx];
		descw = le32_to_cpu(rxbd->info);

		skb = priv->rx_skb[r_idx];
		poffset = QTN_GET_OFFSET(descw);
		psize = QTN_GET_LEN(descw);
		consume = 1;

		if (descw & QTN_BD_EMPTY) {
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
			skb_paddr = QTN_HOST_ADDR(0x0, le32_to_cpu(rxbd->addr));
			pci_unmap_single(priv->pdev, skb_paddr, SKB_BUF_SIZE,
					 PCI_DMA_FROMDEVICE);
		}

		if (consume) {
			skb_reserve(skb, poffset);
			skb_put(skb, psize);
			ndev = qtnf_classify_skb(bus, skb);
			if (likely(ndev)) {
				qtnf_update_rx_stats(ndev, skb);
				skb->protocol = eth_type_trans(skb, ndev);
				netif_receive_skb(skb);
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

		/* notify card about recv packets once per several packets */
		if (((++ts->rx_pkt_count) & RX_DONE_INTR_MSK) == 0)
			writel(TOPAZ_IPC_IRQ_WORD(TOPAZ_RC_RX_DONE_IRQ),
			       TOPAZ_LH_IPC4_INT(priv->sysctl_bar));

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

			ret = topaz_skb2rbd_attach(ts, w_idx,
						   descw & QTN_BD_WRAP);
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
		enable_rx_irqs(ts);
	}

	return processed;
}

static void
qtnf_pcie_data_tx_timeout(struct qtnf_bus *bus, struct net_device *ndev)
{
	struct qtnf_pcie_topaz_state *ts = get_bus_priv(bus);

	qtnf_try_wake_xmit(bus, ndev);
	tasklet_hi_schedule(&ts->base.reclaim_tq);
}

static void qtnf_pcie_data_rx_start(struct qtnf_bus *bus)
{
	struct qtnf_pcie_topaz_state *ts = get_bus_priv(bus);

	napi_enable(&bus->mux_napi);
	enable_rx_irqs(ts);
}

static void qtnf_pcie_data_rx_stop(struct qtnf_bus *bus)
{
	struct qtnf_pcie_topaz_state *ts = get_bus_priv(bus);

	disable_rx_irqs(ts);
	napi_disable(&bus->mux_napi);
}

static const struct qtnf_bus_ops qtnf_pcie_topaz_bus_ops = {
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
	struct qtnf_pcie_topaz_state *ts = get_bus_priv(bus);

	seq_printf(s, "pcie_irq_count(%u)\n", ts->base.pcie_irq_count);

	return 0;
}

static int qtnf_dbg_pkt_stats(struct seq_file *s, void *data)
{
	struct qtnf_bus *bus = dev_get_drvdata(s->private);
	struct qtnf_pcie_topaz_state *ts = get_bus_priv(bus);
	struct qtnf_pcie_bus_priv *priv = &ts->base;
	u32 tx_done_index = readl(ts->ep_next_rx_pkt);

	seq_printf(s, "tx_full_count(%u)\n", priv->tx_full_count);
	seq_printf(s, "tx_done_count(%u)\n", priv->tx_done_count);
	seq_printf(s, "tx_reclaim_done(%u)\n", priv->tx_reclaim_done);
	seq_printf(s, "tx_reclaim_req(%u)\n", priv->tx_reclaim_req);
	seq_printf(s, "tx_eapol(%u)\n", priv->tx_eapol);

	seq_printf(s, "tx_bd_r_index(%u)\n", priv->tx_bd_r_index);
	seq_printf(s, "tx_done_index(%u)\n", tx_done_index);
	seq_printf(s, "tx_bd_w_index(%u)\n", priv->tx_bd_w_index);

	seq_printf(s, "tx host queue len(%u)\n",
		   CIRC_CNT(priv->tx_bd_w_index, priv->tx_bd_r_index,
			    priv->tx_bd_num));
	seq_printf(s, "tx reclaim queue len(%u)\n",
		   CIRC_CNT(tx_done_index, priv->tx_bd_r_index,
			    priv->tx_bd_num));
	seq_printf(s, "tx card queue len(%u)\n",
		   CIRC_CNT(priv->tx_bd_w_index, tx_done_index,
			    priv->tx_bd_num));

	seq_printf(s, "rx_bd_r_index(%u)\n", priv->rx_bd_r_index);
	seq_printf(s, "rx_bd_w_index(%u)\n", priv->rx_bd_w_index);
	seq_printf(s, "rx alloc queue len(%u)\n",
		   CIRC_SPACE(priv->rx_bd_w_index, priv->rx_bd_r_index,
			      priv->rx_bd_num));

	return 0;
}

static void qtnf_reset_dma_offset(struct qtnf_pcie_topaz_state *ts)
{
	struct qtnf_topaz_bda __iomem *bda = ts->bda;
	u32 offset = readl(&bda->bda_dma_offset);

	if ((offset & PCIE_DMA_OFFSET_ERROR_MASK) != PCIE_DMA_OFFSET_ERROR)
		return;

	writel(0x0, &bda->bda_dma_offset);
}

static int qtnf_pcie_endian_detect(struct qtnf_pcie_topaz_state *ts)
{
	struct qtnf_topaz_bda __iomem *bda = ts->bda;
	u32 timeout = 0;
	u32 endian;
	int ret = 0;

	writel(QTN_PCI_ENDIAN_DETECT_DATA, &bda->bda_pci_endian);

	/* flush endian modifications before status update */
	dma_wmb();

	writel(QTN_PCI_ENDIAN_VALID_STATUS, &bda->bda_pci_pre_status);

	while (readl(&bda->bda_pci_post_status) !=
	       QTN_PCI_ENDIAN_VALID_STATUS) {
		usleep_range(1000, 1200);
		if (++timeout > QTN_FW_DL_TIMEOUT_MS) {
			pr_err("card endianness detection timed out\n");
			ret = -ETIMEDOUT;
			goto endian_out;
		}
	}

	/* do not read before status is updated */
	dma_rmb();

	endian = readl(&bda->bda_pci_endian);
	WARN(endian != QTN_PCI_LITTLE_ENDIAN,
	     "%s: unexpected card endianness", __func__);

endian_out:
	writel(0, &bda->bda_pci_pre_status);
	writel(0, &bda->bda_pci_post_status);
	writel(0, &bda->bda_pci_endian);

	return ret;
}

static int qtnf_pre_init_ep(struct qtnf_bus *bus)
{
	struct qtnf_pcie_topaz_state *ts = (void *)get_bus_priv(bus);
	struct qtnf_topaz_bda __iomem *bda = ts->bda;
	u32 flags;
	int ret;

	ret = qtnf_pcie_endian_detect(ts);
	if (ret < 0) {
		pr_err("failed to detect card endianness\n");
		return ret;
	}

	writeb(ts->base.msi_enabled, &ts->bda->bda_rc_msi_enabled);
	qtnf_reset_dma_offset(ts);

	/* notify card about driver type and boot mode */
	flags = readl(&bda->bda_flags) | QTN_BDA_HOST_QLINK_DRV;

	if (ts->base.flashboot)
		flags |= QTN_BDA_FLASH_BOOT;
	else
		flags &= ~QTN_BDA_FLASH_BOOT;

	writel(flags, &bda->bda_flags);

	qtnf_set_state(&ts->bda->bda_bootstate, QTN_BDA_FW_HOST_RDY);
	if (qtnf_poll_state(&ts->bda->bda_bootstate, QTN_BDA_FW_TARGET_RDY,
			    QTN_FW_DL_TIMEOUT_MS)) {
		pr_err("card is not ready to boot...\n");
		return -ETIMEDOUT;
	}

	return ret;
}

static int qtnf_post_init_ep(struct qtnf_pcie_topaz_state *ts)
{
	struct pci_dev *pdev = ts->base.pdev;

	setup_rx_irqs(ts);
	disable_rx_irqs(ts);

	if (qtnf_poll_state(&ts->bda->bda_bootstate, QTN_BDA_FW_QLINK_DONE,
			    QTN_FW_QLINK_TIMEOUT_MS))
		return -ETIMEDOUT;

	enable_irq(pdev->irq);
	return 0;
}

static int
qtnf_ep_fw_load(struct qtnf_pcie_topaz_state *ts, const u8 *fw, u32 fw_size)
{
	struct qtnf_topaz_bda __iomem *bda = ts->bda;
	struct pci_dev *pdev = ts->base.pdev;
	u32 remaining = fw_size;
	u8 *curr = (u8 *)fw;
	u32 blksize;
	u32 nblocks;
	u32 offset;
	u32 count;
	u32 size;
	dma_addr_t paddr;
	void *data;
	int ret = 0;

	pr_debug("FW upload started: fw_addr = 0x%p, size=%d\n", fw, fw_size);

	blksize = ts->base.fw_blksize;

	if (blksize < PAGE_SIZE)
		blksize = PAGE_SIZE;

	while (blksize >= PAGE_SIZE) {
		pr_debug("allocating %u bytes to upload FW\n", blksize);
		data = dma_alloc_coherent(&pdev->dev, blksize,
					  &paddr, GFP_KERNEL);
		if (data)
			break;
		blksize /= 2;
	}

	if (!data) {
		pr_err("failed to allocate DMA buffer for FW upload\n");
		ret = -ENOMEM;
		goto fw_load_out;
	}

	nblocks = NBLOCKS(fw_size, blksize);
	offset = readl(&bda->bda_dma_offset);

	qtnf_set_state(&ts->bda->bda_bootstate, QTN_BDA_FW_HOST_LOAD);
	if (qtnf_poll_state(&ts->bda->bda_bootstate, QTN_BDA_FW_EP_RDY,
			    QTN_FW_DL_TIMEOUT_MS)) {
		pr_err("card is not ready to download FW\n");
		ret = -ETIMEDOUT;
		goto fw_load_map;
	}

	for (count = 0 ; count < nblocks; count++) {
		size = (remaining > blksize) ? blksize : remaining;

		memcpy(data, curr, size);
		qtnf_non_posted_write(paddr + offset, &bda->bda_img);
		qtnf_non_posted_write(size, &bda->bda_img_size);

		pr_debug("chunk[%u] VA[0x%p] PA[%pad] sz[%u]\n",
			 count, (void *)curr, &paddr, size);

		qtnf_set_state(&ts->bda->bda_bootstate, QTN_BDA_FW_BLOCK_RDY);
		if (qtnf_poll_state(&ts->bda->bda_bootstate,
				    QTN_BDA_FW_BLOCK_DONE,
				    QTN_FW_DL_TIMEOUT_MS)) {
			pr_err("confirmation for block #%d timed out\n", count);
			ret = -ETIMEDOUT;
			goto fw_load_map;
		}

		remaining = (remaining < size) ? remaining : (remaining - size);
		curr += size;
	}

	/* upload completion mark: zero-sized block */
	qtnf_non_posted_write(0, &bda->bda_img);
	qtnf_non_posted_write(0, &bda->bda_img_size);

	qtnf_set_state(&ts->bda->bda_bootstate, QTN_BDA_FW_BLOCK_RDY);
	if (qtnf_poll_state(&ts->bda->bda_bootstate, QTN_BDA_FW_BLOCK_DONE,
			    QTN_FW_DL_TIMEOUT_MS)) {
		pr_err("confirmation for the last block timed out\n");
		ret = -ETIMEDOUT;
		goto fw_load_map;
	}

	/* RC is done */
	qtnf_set_state(&ts->bda->bda_bootstate, QTN_BDA_FW_BLOCK_END);
	if (qtnf_poll_state(&ts->bda->bda_bootstate, QTN_BDA_FW_LOAD_DONE,
			    QTN_FW_DL_TIMEOUT_MS)) {
		pr_err("confirmation for FW upload completion timed out\n");
		ret = -ETIMEDOUT;
		goto fw_load_map;
	}

	pr_debug("FW upload completed: totally sent %d blocks\n", count);

fw_load_map:
	dma_free_coherent(&pdev->dev, blksize, data, paddr);

fw_load_out:
	return ret;
}

static int qtnf_topaz_fw_upload(struct qtnf_pcie_topaz_state *ts,
				const char *fwname)
{
	const struct firmware *fw;
	struct pci_dev *pdev = ts->base.pdev;
	int ret;

	if (qtnf_poll_state(&ts->bda->bda_bootstate,
			    QTN_BDA_FW_LOAD_RDY,
			    QTN_FW_DL_TIMEOUT_MS)) {
		pr_err("%s: card is not ready\n", fwname);
		return -1;
	}

	pr_info("starting firmware upload: %s\n", fwname);

	ret = request_firmware(&fw, fwname, &pdev->dev);
	if (ret < 0) {
		pr_err("%s: request_firmware error %d\n", fwname, ret);
		return -1;
	}

	ret = qtnf_ep_fw_load(ts, fw->data, fw->size);
	release_firmware(fw);

	if (ret)
		pr_err("%s: FW upload error\n", fwname);

	return ret;
}

static void qtnf_topaz_fw_work_handler(struct work_struct *work)
{
	struct qtnf_bus *bus = container_of(work, struct qtnf_bus, fw_work);
	struct qtnf_pcie_topaz_state *ts = (void *)get_bus_priv(bus);
	int bootloader_needed = readl(&ts->bda->bda_flags) & QTN_BDA_XMIT_UBOOT;
	struct pci_dev *pdev = ts->base.pdev;
	int ret;

	qtnf_set_state(&ts->bda->bda_bootstate, QTN_BDA_FW_TARGET_BOOT);

	if (bootloader_needed) {
		ret = qtnf_topaz_fw_upload(ts, QTN_PCI_TOPAZ_BOOTLD_NAME);
		if (ret)
			goto fw_load_exit;

		ret = qtnf_pre_init_ep(bus);
		if (ret)
			goto fw_load_exit;

		qtnf_set_state(&ts->bda->bda_bootstate,
			       QTN_BDA_FW_TARGET_BOOT);
	}

	if (ts->base.flashboot) {
		pr_info("booting firmware from flash\n");

		ret = qtnf_poll_state(&ts->bda->bda_bootstate,
				      QTN_BDA_FW_FLASH_BOOT,
				      QTN_FW_DL_TIMEOUT_MS);
		if (ret)
			goto fw_load_exit;
	} else {
		ret = qtnf_topaz_fw_upload(ts, QTN_PCI_TOPAZ_FW_NAME);
		if (ret)
			goto fw_load_exit;

		qtnf_set_state(&ts->bda->bda_bootstate, QTN_BDA_FW_START);
		ret = qtnf_poll_state(&ts->bda->bda_bootstate,
				      QTN_BDA_FW_CONFIG,
				      QTN_FW_QLINK_TIMEOUT_MS);
		if (ret) {
			pr_err("FW bringup timed out\n");
			goto fw_load_exit;
		}

		qtnf_set_state(&ts->bda->bda_bootstate, QTN_BDA_FW_RUN);
		ret = qtnf_poll_state(&ts->bda->bda_bootstate,
				      QTN_BDA_FW_RUNNING,
				      QTN_FW_QLINK_TIMEOUT_MS);
		if (ret) {
			pr_err("card bringup timed out\n");
			goto fw_load_exit;
		}
	}

	ret = qtnf_post_init_ep(ts);
	if (ret) {
		pr_err("FW runtime failure\n");
		goto fw_load_exit;
	}

	pr_info("firmware is up and running\n");

	ret = qtnf_pcie_fw_boot_done(bus);
	if (ret)
		goto fw_load_exit;

	qtnf_debugfs_add_entry(bus, "pkt_stats", qtnf_dbg_pkt_stats);
	qtnf_debugfs_add_entry(bus, "irq_stats", qtnf_dbg_irq_stats);

fw_load_exit:
	put_device(&pdev->dev);
}

static void qtnf_reclaim_tasklet_fn(unsigned long data)
{
	struct qtnf_pcie_topaz_state *ts = (void *)data;

	qtnf_topaz_data_tx_reclaim(ts);
}

static u64 qtnf_topaz_dma_mask_get(void)
{
	return DMA_BIT_MASK(32);
}

static int qtnf_pcie_topaz_probe(struct qtnf_bus *bus, unsigned int tx_bd_num)
{
	struct qtnf_pcie_topaz_state *ts = get_bus_priv(bus);
	struct pci_dev *pdev = ts->base.pdev;
	struct qtnf_shm_ipc_int ipc_int;
	unsigned long irqflags;
	int ret;

	bus->bus_ops = &qtnf_pcie_topaz_bus_ops;
	INIT_WORK(&bus->fw_work, qtnf_topaz_fw_work_handler);
	ts->bda = ts->base.epmem_bar;

	/* assign host msi irq before card init */
	if (ts->base.msi_enabled)
		irqflags = IRQF_NOBALANCING;
	else
		irqflags = IRQF_NOBALANCING | IRQF_SHARED;

	ret = devm_request_irq(&pdev->dev, pdev->irq,
			       &qtnf_pcie_topaz_interrupt,
			       irqflags, "qtnf_topaz_irq", (void *)bus);
	if (ret) {
		pr_err("failed to request pcie irq %d\n", pdev->irq);
		return ret;
	}

	disable_irq(pdev->irq);

	ret = qtnf_pre_init_ep(bus);
	if (ret) {
		pr_err("failed to init card\n");
		return ret;
	}

	ret = qtnf_pcie_topaz_init_xfer(ts, tx_bd_num);
	if (ret) {
		pr_err("PCIE xfer init failed\n");
		return ret;
	}

	tasklet_init(&ts->base.reclaim_tq, qtnf_reclaim_tasklet_fn,
		     (unsigned long)ts);
	netif_napi_add(&bus->mux_dev, &bus->mux_napi,
		       qtnf_topaz_rx_poll, 10);

	ipc_int.fn = qtnf_topaz_ipc_gen_ep_int;
	ipc_int.arg = ts;
	qtnf_pcie_init_shm_ipc(&ts->base, &ts->bda->bda_shm_reg1,
			       &ts->bda->bda_shm_reg2, &ipc_int);

	return 0;
}

static void qtnf_pcie_topaz_remove(struct qtnf_bus *bus)
{
	struct qtnf_pcie_topaz_state *ts = get_bus_priv(bus);

	qtnf_topaz_reset_ep(ts);
	qtnf_topaz_free_xfer_buffers(ts);
}

#ifdef CONFIG_PM_SLEEP
static int qtnf_pcie_topaz_suspend(struct qtnf_bus *bus)
{
	struct qtnf_pcie_topaz_state *ts = get_bus_priv(bus);
	struct pci_dev *pdev = ts->base.pdev;

	writel((u32 __force)PCI_D3hot, ts->ep_pmstate);
	dma_wmb();
	writel(TOPAZ_IPC_IRQ_WORD(TOPAZ_RC_PM_EP_IRQ),
	       TOPAZ_LH_IPC4_INT(ts->base.sysctl_bar));

	pci_save_state(pdev);
	pci_enable_wake(pdev, PCI_D3hot, 1);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int qtnf_pcie_topaz_resume(struct qtnf_bus *bus)
{
	struct qtnf_pcie_topaz_state *ts = get_bus_priv(bus);
	struct pci_dev *pdev = ts->base.pdev;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_enable_wake(pdev, PCI_D0, 0);

	writel((u32 __force)PCI_D0, ts->ep_pmstate);
	dma_wmb();
	writel(TOPAZ_IPC_IRQ_WORD(TOPAZ_RC_PM_EP_IRQ),
	       TOPAZ_LH_IPC4_INT(ts->base.sysctl_bar));

	return 0;
}
#endif

struct qtnf_bus *qtnf_pcie_topaz_alloc(struct pci_dev *pdev)
{
	struct qtnf_bus *bus;
	struct qtnf_pcie_topaz_state *ts;

	bus = devm_kzalloc(&pdev->dev, sizeof(*bus) + sizeof(*ts), GFP_KERNEL);
	if (!bus)
		return NULL;

	ts = get_bus_priv(bus);
	ts->base.probe_cb = qtnf_pcie_topaz_probe;
	ts->base.remove_cb = qtnf_pcie_topaz_remove;
	ts->base.dma_mask_get_cb = qtnf_topaz_dma_mask_get;
#ifdef CONFIG_PM_SLEEP
	ts->base.resume_cb = qtnf_pcie_topaz_resume;
	ts->base.suspend_cb = qtnf_pcie_topaz_suspend;
#endif

	return bus;
}
