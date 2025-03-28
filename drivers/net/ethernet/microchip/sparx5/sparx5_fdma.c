// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2021 Microchip Technology Inc. and its subsidiaries.
 *
 * The Sparx5 Chip Register Model can be browsed at this location:
 * https://github.com/microchip-ung/sparx-5_reginfo
 */

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/dma-mapping.h>

#include "sparx5_main_regs.h"
#include "sparx5_main.h"
#include "sparx5_port.h"

#define FDMA_XTR_BUFFER_SIZE		2048
#define FDMA_WEIGHT			4

static int sparx5_fdma_tx_dataptr_cb(struct fdma *fdma, int dcb, int db,
				     u64 *dataptr)
{
	*dataptr = fdma->dma + (sizeof(struct fdma_dcb) * fdma->n_dcbs) +
		   ((dcb * fdma->n_dbs + db) * fdma->db_size);

	return 0;
}

static int sparx5_fdma_rx_dataptr_cb(struct fdma *fdma, int dcb, int db,
				     u64 *dataptr)
{
	struct sparx5 *sparx5 = fdma->priv;
	struct sparx5_rx *rx = &sparx5->rx;
	struct sk_buff *skb;

	skb = __netdev_alloc_skb(rx->ndev, fdma->db_size, GFP_ATOMIC);
	if (unlikely(!skb))
		return -ENOMEM;

	*dataptr = virt_to_phys(skb->data);

	rx->skb[dcb][db] = skb;

	return 0;
}

static void sparx5_fdma_rx_activate(struct sparx5 *sparx5, struct sparx5_rx *rx)
{
	struct fdma *fdma = &rx->fdma;

	/* Write the buffer address in the LLP and LLP1 regs */
	spx5_wr(((u64)fdma->dma) & GENMASK(31, 0), sparx5,
		FDMA_DCB_LLP(fdma->channel_id));
	spx5_wr(((u64)fdma->dma) >> 32, sparx5,
		FDMA_DCB_LLP1(fdma->channel_id));

	/* Set the number of RX DBs to be used, and DB end-of-frame interrupt */
	spx5_wr(FDMA_CH_CFG_CH_DCB_DB_CNT_SET(fdma->n_dbs) |
		FDMA_CH_CFG_CH_INTR_DB_EOF_ONLY_SET(1) |
		FDMA_CH_CFG_CH_INJ_PORT_SET(XTR_QUEUE),
		sparx5, FDMA_CH_CFG(fdma->channel_id));

	/* Set the RX Watermark to max */
	spx5_rmw(FDMA_XTR_CFG_XTR_FIFO_WM_SET(31), FDMA_XTR_CFG_XTR_FIFO_WM,
		 sparx5,
		 FDMA_XTR_CFG);

	/* Start RX fdma */
	spx5_rmw(FDMA_PORT_CTRL_XTR_STOP_SET(0), FDMA_PORT_CTRL_XTR_STOP,
		 sparx5, FDMA_PORT_CTRL(0));

	/* Enable RX channel DB interrupt */
	spx5_rmw(BIT(fdma->channel_id),
		 BIT(fdma->channel_id) & FDMA_INTR_DB_ENA_INTR_DB_ENA,
		 sparx5, FDMA_INTR_DB_ENA);

	/* Activate the RX channel */
	spx5_wr(BIT(fdma->channel_id), sparx5, FDMA_CH_ACTIVATE);
}

static void sparx5_fdma_rx_deactivate(struct sparx5 *sparx5, struct sparx5_rx *rx)
{
	struct fdma *fdma = &rx->fdma;

	/* Deactivate the RX channel */
	spx5_rmw(0, BIT(fdma->channel_id) & FDMA_CH_ACTIVATE_CH_ACTIVATE,
		 sparx5, FDMA_CH_ACTIVATE);

	/* Disable RX channel DB interrupt */
	spx5_rmw(0, BIT(fdma->channel_id) & FDMA_INTR_DB_ENA_INTR_DB_ENA,
		 sparx5, FDMA_INTR_DB_ENA);

	/* Stop RX fdma */
	spx5_rmw(FDMA_PORT_CTRL_XTR_STOP_SET(1), FDMA_PORT_CTRL_XTR_STOP,
		 sparx5, FDMA_PORT_CTRL(0));
}

static void sparx5_fdma_tx_activate(struct sparx5 *sparx5, struct sparx5_tx *tx)
{
	struct fdma *fdma = &tx->fdma;

	/* Write the buffer address in the LLP and LLP1 regs */
	spx5_wr(((u64)fdma->dma) & GENMASK(31, 0), sparx5,
		FDMA_DCB_LLP(fdma->channel_id));
	spx5_wr(((u64)fdma->dma) >> 32, sparx5,
		FDMA_DCB_LLP1(fdma->channel_id));

	/* Set the number of TX DBs to be used, and DB end-of-frame interrupt */
	spx5_wr(FDMA_CH_CFG_CH_DCB_DB_CNT_SET(fdma->n_dbs) |
		FDMA_CH_CFG_CH_INTR_DB_EOF_ONLY_SET(1) |
		FDMA_CH_CFG_CH_INJ_PORT_SET(INJ_QUEUE),
		sparx5, FDMA_CH_CFG(fdma->channel_id));

	/* Start TX fdma */
	spx5_rmw(FDMA_PORT_CTRL_INJ_STOP_SET(0), FDMA_PORT_CTRL_INJ_STOP,
		 sparx5, FDMA_PORT_CTRL(0));

	/* Activate the channel */
	spx5_wr(BIT(fdma->channel_id), sparx5, FDMA_CH_ACTIVATE);
}

static void sparx5_fdma_tx_deactivate(struct sparx5 *sparx5, struct sparx5_tx *tx)
{
	/* Disable the channel */
	spx5_rmw(0, BIT(tx->fdma.channel_id) & FDMA_CH_ACTIVATE_CH_ACTIVATE,
		 sparx5, FDMA_CH_ACTIVATE);
}

void sparx5_fdma_reload(struct sparx5 *sparx5, struct fdma *fdma)
{
	/* Reload the RX channel */
	spx5_wr(BIT(fdma->channel_id), sparx5, FDMA_CH_RELOAD);
}

static bool sparx5_fdma_rx_get_frame(struct sparx5 *sparx5, struct sparx5_rx *rx)
{
	struct fdma *fdma = &rx->fdma;
	struct sparx5_port *port;
	struct fdma_db *db_hw;
	struct frame_info fi;
	struct sk_buff *skb;

	/* Check if the DCB is done */
	db_hw = fdma_db_next_get(fdma);
	if (unlikely(!fdma_db_is_done(db_hw)))
		return false;
	skb = rx->skb[fdma->dcb_index][fdma->db_index];
	skb_put(skb, fdma_db_len_get(db_hw));
	/* Now do the normal processing of the skb */
	sparx5_ifh_parse(sparx5, (u32 *)skb->data, &fi);
	/* Map to port netdev */
	port = fi.src_port < sparx5->data->consts->n_ports ?
		       sparx5->ports[fi.src_port] :
		       NULL;
	if (!port || !port->ndev) {
		dev_err(sparx5->dev, "Data on inactive port %d\n", fi.src_port);
		sparx5_xtr_flush(sparx5, XTR_QUEUE);
		return false;
	}
	skb->dev = port->ndev;
	skb_pull(skb, IFH_LEN * sizeof(u32));
	if (likely(!(skb->dev->features & NETIF_F_RXFCS)))
		skb_trim(skb, skb->len - ETH_FCS_LEN);

	sparx5_ptp_rxtstamp(sparx5, skb, fi.timestamp);
	skb->protocol = eth_type_trans(skb, skb->dev);
	/* Everything we see on an interface that is in the HW bridge
	 * has already been forwarded
	 */
	if (test_bit(port->portno, sparx5->bridge_mask))
		skb->offload_fwd_mark = 1;
	skb->dev->stats.rx_bytes += skb->len;
	skb->dev->stats.rx_packets++;
	rx->packets++;
	netif_receive_skb(skb);
	return true;
}

int sparx5_fdma_napi_callback(struct napi_struct *napi, int weight)
{
	struct sparx5_rx *rx = container_of(napi, struct sparx5_rx, napi);
	struct sparx5 *sparx5 = container_of(rx, struct sparx5, rx);
	struct fdma *fdma = &rx->fdma;
	int counter = 0;

	while (counter < weight && sparx5_fdma_rx_get_frame(sparx5, rx)) {
		fdma_db_advance(fdma);
		counter++;
		/* Check if the DCB can be reused */
		if (fdma_dcb_is_reusable(fdma))
			continue;
		fdma_dcb_add(fdma, fdma->dcb_index,
			     FDMA_DCB_INFO_DATAL(fdma->db_size),
			     FDMA_DCB_STATUS_INTR);
		fdma_db_reset(fdma);
		fdma_dcb_advance(fdma);
	}
	if (counter < weight) {
		napi_complete_done(&rx->napi, counter);
		spx5_rmw(BIT(fdma->channel_id),
			 BIT(fdma->channel_id) & FDMA_INTR_DB_ENA_INTR_DB_ENA,
			 sparx5, FDMA_INTR_DB_ENA);
	}
	if (counter)
		sparx5_fdma_reload(sparx5, fdma);
	return counter;
}

int sparx5_fdma_xmit(struct sparx5 *sparx5, u32 *ifh, struct sk_buff *skb,
		     struct net_device *dev)
{
	struct sparx5_tx *tx = &sparx5->tx;
	struct fdma *fdma = &tx->fdma;
	void *virt_addr;

	fdma_dcb_advance(fdma);
	if (!fdma_db_is_done(fdma_db_get(fdma, fdma->dcb_index, 0)))
		return -EINVAL;

	/* Get the virtual address of the dataptr for the next DB */
	virt_addr = ((u8 *)fdma->dcbs +
		     (sizeof(struct fdma_dcb) * fdma->n_dcbs) +
		     ((fdma->dcb_index * fdma->n_dbs) * fdma->db_size));

	memcpy(virt_addr, ifh, IFH_LEN * 4);
	memcpy(virt_addr + IFH_LEN * 4, skb->data, skb->len);

	fdma_dcb_add(fdma, fdma->dcb_index, 0,
		     FDMA_DCB_STATUS_SOF |
		     FDMA_DCB_STATUS_EOF |
		     FDMA_DCB_STATUS_BLOCKO(0) |
		     FDMA_DCB_STATUS_BLOCKL(skb->len + IFH_LEN * 4 + 4));

	sparx5_fdma_reload(sparx5, fdma);

	return NETDEV_TX_OK;
}

static int sparx5_fdma_rx_alloc(struct sparx5 *sparx5)
{
	struct sparx5_rx *rx = &sparx5->rx;
	struct fdma *fdma = &rx->fdma;
	int err;

	err = fdma_alloc_phys(fdma);
	if (err)
		return err;

	fdma_dcbs_init(fdma, FDMA_DCB_INFO_DATAL(fdma->db_size),
		       FDMA_DCB_STATUS_INTR);

	return 0;
}

static int sparx5_fdma_tx_alloc(struct sparx5 *sparx5)
{
	struct sparx5_tx *tx = &sparx5->tx;
	struct fdma *fdma = &tx->fdma;
	int err;

	err = fdma_alloc_phys(fdma);
	if (err)
		return err;

	fdma_dcbs_init(fdma, FDMA_DCB_INFO_DATAL(fdma->db_size),
		       FDMA_DCB_STATUS_DONE);

	return 0;
}

static void sparx5_fdma_rx_init(struct sparx5 *sparx5,
				struct sparx5_rx *rx, int channel)
{
	struct fdma *fdma = &rx->fdma;
	int idx;

	fdma->channel_id = channel;
	fdma->n_dcbs = FDMA_DCB_MAX;
	fdma->n_dbs = FDMA_RX_DCB_MAX_DBS;
	fdma->priv = sparx5;
	fdma->db_size = ALIGN(FDMA_XTR_BUFFER_SIZE, PAGE_SIZE);
	fdma->size = fdma_get_size(&sparx5->rx.fdma);
	fdma->ops.dataptr_cb = &sparx5_fdma_rx_dataptr_cb;
	fdma->ops.nextptr_cb = &fdma_nextptr_cb;
	/* Fetch a netdev for SKB and NAPI use, any will do */
	for (idx = 0; idx < sparx5->data->consts->n_ports; ++idx) {
		struct sparx5_port *port = sparx5->ports[idx];

		if (port && port->ndev) {
			rx->ndev = port->ndev;
			break;
		}
	}
}

static void sparx5_fdma_tx_init(struct sparx5 *sparx5,
				struct sparx5_tx *tx, int channel)
{
	struct fdma *fdma = &tx->fdma;

	fdma->channel_id = channel;
	fdma->n_dcbs = FDMA_DCB_MAX;
	fdma->n_dbs = FDMA_TX_DCB_MAX_DBS;
	fdma->priv = sparx5;
	fdma->db_size = ALIGN(FDMA_XTR_BUFFER_SIZE, PAGE_SIZE);
	fdma->size = fdma_get_size_contiguous(&sparx5->tx.fdma);
	fdma->ops.dataptr_cb = &sparx5_fdma_tx_dataptr_cb;
	fdma->ops.nextptr_cb = &fdma_nextptr_cb;
}

irqreturn_t sparx5_fdma_handler(int irq, void *args)
{
	struct sparx5 *sparx5 = args;
	u32 db = 0, err = 0;

	db = spx5_rd(sparx5, FDMA_INTR_DB);
	err = spx5_rd(sparx5, FDMA_INTR_ERR);
	/* Clear interrupt */
	if (db) {
		spx5_wr(0, sparx5, FDMA_INTR_DB_ENA);
		spx5_wr(db, sparx5, FDMA_INTR_DB);
		napi_schedule(&sparx5->rx.napi);
	}
	if (err) {
		u32 err_type = spx5_rd(sparx5, FDMA_ERRORS);

		dev_err_ratelimited(sparx5->dev,
				    "ERR: int: %#x, type: %#x\n",
				    err, err_type);
		spx5_wr(err, sparx5, FDMA_INTR_ERR);
		spx5_wr(err_type, sparx5, FDMA_ERRORS);
	}
	return IRQ_HANDLED;
}

void sparx5_fdma_injection_mode(struct sparx5 *sparx5)
{
	const int byte_swap = 1;
	int portno;
	int urgency;

	/* Change mode to fdma extraction and injection */
	spx5_wr(QS_XTR_GRP_CFG_MODE_SET(2) |
		QS_XTR_GRP_CFG_STATUS_WORD_POS_SET(1) |
		QS_XTR_GRP_CFG_BYTE_SWAP_SET(byte_swap),
		sparx5, QS_XTR_GRP_CFG(XTR_QUEUE));
	spx5_wr(QS_INJ_GRP_CFG_MODE_SET(2) |
		QS_INJ_GRP_CFG_BYTE_SWAP_SET(byte_swap),
		sparx5, QS_INJ_GRP_CFG(INJ_QUEUE));

	/* CPU ports capture setup */
	for (portno = sparx5_get_internal_port(sparx5, SPX5_PORT_CPU_0);
	     portno <= sparx5_get_internal_port(sparx5, SPX5_PORT_CPU_1);
	     portno++) {
		/* ASM CPU port: No preamble, IFH, enable padding */
		spx5_wr(ASM_PORT_CFG_PAD_ENA_SET(1) |
			ASM_PORT_CFG_NO_PREAMBLE_ENA_SET(1) |
			ASM_PORT_CFG_INJ_FORMAT_CFG_SET(1), /* 1 = IFH */
			sparx5, ASM_PORT_CFG(portno));

		/* Reset WM cnt to unclog queued frames */
		spx5_rmw(DSM_DEV_TX_STOP_WM_CFG_DEV_TX_CNT_CLR_SET(1),
			 DSM_DEV_TX_STOP_WM_CFG_DEV_TX_CNT_CLR,
			 sparx5,
			 DSM_DEV_TX_STOP_WM_CFG(portno));

		/* Set Disassembler Stop Watermark level */
		spx5_rmw(DSM_DEV_TX_STOP_WM_CFG_DEV_TX_STOP_WM_SET(100),
			 DSM_DEV_TX_STOP_WM_CFG_DEV_TX_STOP_WM,
			 sparx5,
			 DSM_DEV_TX_STOP_WM_CFG(portno));

		/* Enable port in queue system */
		urgency = sparx5_port_fwd_urg(sparx5, SPEED_2500);
		spx5_rmw(QFWD_SWITCH_PORT_MODE_PORT_ENA_SET(1) |
			 QFWD_SWITCH_PORT_MODE_FWD_URGENCY_SET(urgency),
			 QFWD_SWITCH_PORT_MODE_PORT_ENA |
			 QFWD_SWITCH_PORT_MODE_FWD_URGENCY,
			 sparx5,
			 QFWD_SWITCH_PORT_MODE(portno));

		/* Disable Disassembler buffer underrun watchdog
		 * to avoid truncated packets in XTR
		 */
		spx5_rmw(DSM_BUF_CFG_UNDERFLOW_WATCHDOG_DIS_SET(1),
			 DSM_BUF_CFG_UNDERFLOW_WATCHDOG_DIS,
			 sparx5,
			 DSM_BUF_CFG(portno));

		/* Disabling frame aging */
		spx5_rmw(HSCH_PORT_MODE_AGE_DIS_SET(1),
			 HSCH_PORT_MODE_AGE_DIS,
			 sparx5,
			 HSCH_PORT_MODE(portno));
	}
}

int sparx5_fdma_init(struct sparx5 *sparx5)
{
	int err;

	/* Reset FDMA state */
	spx5_wr(FDMA_CTRL_NRESET_SET(0), sparx5, FDMA_CTRL);
	spx5_wr(FDMA_CTRL_NRESET_SET(1), sparx5, FDMA_CTRL);

	/* Force ACP caching but disable read/write allocation */
	spx5_rmw(CPU_PROC_CTRL_ACP_CACHE_FORCE_ENA_SET(1) |
		 CPU_PROC_CTRL_ACP_AWCACHE_SET(0) |
		 CPU_PROC_CTRL_ACP_ARCACHE_SET(0),
		 CPU_PROC_CTRL_ACP_CACHE_FORCE_ENA |
		 CPU_PROC_CTRL_ACP_AWCACHE |
		 CPU_PROC_CTRL_ACP_ARCACHE,
		 sparx5, CPU_PROC_CTRL);

	sparx5_fdma_injection_mode(sparx5);
	sparx5_fdma_rx_init(sparx5, &sparx5->rx, FDMA_XTR_CHANNEL);
	sparx5_fdma_tx_init(sparx5, &sparx5->tx, FDMA_INJ_CHANNEL);
	err = sparx5_fdma_rx_alloc(sparx5);
	if (err) {
		dev_err(sparx5->dev, "Could not allocate RX buffers: %d\n", err);
		return err;
	}
	err = sparx5_fdma_tx_alloc(sparx5);
	if (err) {
		dev_err(sparx5->dev, "Could not allocate TX buffers: %d\n", err);
		return err;
	}
	return err;
}

int sparx5_fdma_deinit(struct sparx5 *sparx5)
{
	sparx5_fdma_stop(sparx5);
	fdma_free_phys(&sparx5->rx.fdma);
	fdma_free_phys(&sparx5->tx.fdma);

	return 0;
}

static u32 sparx5_fdma_port_ctrl(struct sparx5 *sparx5)
{
	return spx5_rd(sparx5, FDMA_PORT_CTRL(0));
}

int sparx5_fdma_start(struct sparx5 *sparx5)
{
	const struct sparx5_ops *ops = sparx5->data->ops;
	struct sparx5_rx *rx = &sparx5->rx;
	struct sparx5_tx *tx = &sparx5->tx;

	netif_napi_add_weight(rx->ndev,
			      &rx->napi,
			      ops->fdma_poll,
			      FDMA_WEIGHT);

	napi_enable(&rx->napi);

	sparx5_fdma_rx_activate(sparx5, rx);
	sparx5_fdma_tx_activate(sparx5, tx);

	return 0;
}

int sparx5_fdma_stop(struct sparx5 *sparx5)
{
	struct sparx5_rx *rx = &sparx5->rx;
	struct sparx5_tx *tx = &sparx5->tx;
	u32 val;

	napi_disable(&rx->napi);

	/* Stop the fdma and channel interrupts */
	sparx5_fdma_rx_deactivate(sparx5, rx);
	sparx5_fdma_tx_deactivate(sparx5, tx);

	/* Wait for the RX channel to stop */
	read_poll_timeout(sparx5_fdma_port_ctrl, val,
			  FDMA_PORT_CTRL_XTR_BUF_IS_EMPTY_GET(val) == 0,
			  500, 10000, 0, sparx5);

	return 0;
}
