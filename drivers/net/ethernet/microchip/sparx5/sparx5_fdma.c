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

#define FDMA_XTR_CHANNEL		6
#define FDMA_INJ_CHANNEL		0

#define FDMA_DCB_INFO_DATAL(x)		((x) & GENMASK(15, 0))
#define FDMA_DCB_INFO_TOKEN		BIT(17)
#define FDMA_DCB_INFO_INTR		BIT(18)
#define FDMA_DCB_INFO_SW(x)		(((x) << 24) & GENMASK(31, 24))

#define FDMA_DCB_STATUS_BLOCKL(x)	((x) & GENMASK(15, 0))
#define FDMA_DCB_STATUS_SOF		BIT(16)
#define FDMA_DCB_STATUS_EOF		BIT(17)
#define FDMA_DCB_STATUS_INTR		BIT(18)
#define FDMA_DCB_STATUS_DONE		BIT(19)
#define FDMA_DCB_STATUS_BLOCKO(x)	(((x) << 20) & GENMASK(31, 20))
#define FDMA_DCB_INVALID_DATA		0x1

#define FDMA_XTR_BUFFER_SIZE		2048
#define FDMA_WEIGHT			4

/* Frame DMA DCB format
 *
 * +---------------------------+
 * |         Next Ptr          |
 * +---------------------------+
 * |   Reserved  |    Info     |
 * +---------------------------+
 * |         Data0 Ptr         |
 * +---------------------------+
 * |   Reserved  |    Status0  |
 * +---------------------------+
 * |         Data1 Ptr         |
 * +---------------------------+
 * |   Reserved  |    Status1  |
 * +---------------------------+
 * |         Data2 Ptr         |
 * +---------------------------+
 * |   Reserved  |    Status2  |
 * |-------------|-------------|
 * |                           |
 * |                           |
 * |                           |
 * |                           |
 * |                           |
 * |---------------------------|
 * |         Data14 Ptr        |
 * +-------------|-------------+
 * |   Reserved  |    Status14 |
 * +-------------|-------------+
 */

/* For each hardware DB there is an entry in this list and when the HW DB
 * entry is used, this SW DB entry is moved to the back of the list
 */
struct sparx5_db {
	struct list_head list;
	void *cpu_addr;
};

static void sparx5_fdma_rx_add_dcb(struct sparx5_rx *rx,
				   struct sparx5_rx_dcb_hw *dcb,
				   u64 nextptr)
{
	int idx = 0;

	/* Reset the status of the DB */
	for (idx = 0; idx < FDMA_RX_DCB_MAX_DBS; ++idx) {
		struct sparx5_db_hw *db = &dcb->db[idx];

		db->status = FDMA_DCB_STATUS_INTR;
	}
	dcb->nextptr = FDMA_DCB_INVALID_DATA;
	dcb->info = FDMA_DCB_INFO_DATAL(FDMA_XTR_BUFFER_SIZE);
	rx->last_entry->nextptr = nextptr;
	rx->last_entry = dcb;
}

static void sparx5_fdma_tx_add_dcb(struct sparx5_tx *tx,
				   struct sparx5_tx_dcb_hw *dcb,
				   u64 nextptr)
{
	int idx = 0;

	/* Reset the status of the DB */
	for (idx = 0; idx < FDMA_TX_DCB_MAX_DBS; ++idx) {
		struct sparx5_db_hw *db = &dcb->db[idx];

		db->status = FDMA_DCB_STATUS_DONE;
	}
	dcb->nextptr = FDMA_DCB_INVALID_DATA;
	dcb->info = FDMA_DCB_INFO_DATAL(FDMA_XTR_BUFFER_SIZE);
}

static void sparx5_fdma_rx_activate(struct sparx5 *sparx5, struct sparx5_rx *rx)
{
	/* Write the buffer address in the LLP and LLP1 regs */
	spx5_wr(((u64)rx->dma) & GENMASK(31, 0), sparx5,
		FDMA_DCB_LLP(rx->channel_id));
	spx5_wr(((u64)rx->dma) >> 32, sparx5, FDMA_DCB_LLP1(rx->channel_id));

	/* Set the number of RX DBs to be used, and DB end-of-frame interrupt */
	spx5_wr(FDMA_CH_CFG_CH_DCB_DB_CNT_SET(FDMA_RX_DCB_MAX_DBS) |
		FDMA_CH_CFG_CH_INTR_DB_EOF_ONLY_SET(1) |
		FDMA_CH_CFG_CH_INJ_PORT_SET(XTR_QUEUE),
		sparx5, FDMA_CH_CFG(rx->channel_id));

	/* Set the RX Watermark to max */
	spx5_rmw(FDMA_XTR_CFG_XTR_FIFO_WM_SET(31), FDMA_XTR_CFG_XTR_FIFO_WM,
		 sparx5,
		 FDMA_XTR_CFG);

	/* Start RX fdma */
	spx5_rmw(FDMA_PORT_CTRL_XTR_STOP_SET(0), FDMA_PORT_CTRL_XTR_STOP,
		 sparx5, FDMA_PORT_CTRL(0));

	/* Enable RX channel DB interrupt */
	spx5_rmw(BIT(rx->channel_id),
		 BIT(rx->channel_id) & FDMA_INTR_DB_ENA_INTR_DB_ENA,
		 sparx5, FDMA_INTR_DB_ENA);

	/* Activate the RX channel */
	spx5_wr(BIT(rx->channel_id), sparx5, FDMA_CH_ACTIVATE);
}

static void sparx5_fdma_rx_deactivate(struct sparx5 *sparx5, struct sparx5_rx *rx)
{
	/* Deactivate the RX channel */
	spx5_rmw(0, BIT(rx->channel_id) & FDMA_CH_ACTIVATE_CH_ACTIVATE,
		 sparx5, FDMA_CH_ACTIVATE);

	/* Disable RX channel DB interrupt */
	spx5_rmw(0, BIT(rx->channel_id) & FDMA_INTR_DB_ENA_INTR_DB_ENA,
		 sparx5, FDMA_INTR_DB_ENA);

	/* Stop RX fdma */
	spx5_rmw(FDMA_PORT_CTRL_XTR_STOP_SET(1), FDMA_PORT_CTRL_XTR_STOP,
		 sparx5, FDMA_PORT_CTRL(0));
}

static void sparx5_fdma_tx_activate(struct sparx5 *sparx5, struct sparx5_tx *tx)
{
	/* Write the buffer address in the LLP and LLP1 regs */
	spx5_wr(((u64)tx->dma) & GENMASK(31, 0), sparx5,
		FDMA_DCB_LLP(tx->channel_id));
	spx5_wr(((u64)tx->dma) >> 32, sparx5, FDMA_DCB_LLP1(tx->channel_id));

	/* Set the number of TX DBs to be used, and DB end-of-frame interrupt */
	spx5_wr(FDMA_CH_CFG_CH_DCB_DB_CNT_SET(FDMA_TX_DCB_MAX_DBS) |
		FDMA_CH_CFG_CH_INTR_DB_EOF_ONLY_SET(1) |
		FDMA_CH_CFG_CH_INJ_PORT_SET(INJ_QUEUE),
		sparx5, FDMA_CH_CFG(tx->channel_id));

	/* Start TX fdma */
	spx5_rmw(FDMA_PORT_CTRL_INJ_STOP_SET(0), FDMA_PORT_CTRL_INJ_STOP,
		 sparx5, FDMA_PORT_CTRL(0));

	/* Activate the channel */
	spx5_wr(BIT(tx->channel_id), sparx5, FDMA_CH_ACTIVATE);
}

static void sparx5_fdma_tx_deactivate(struct sparx5 *sparx5, struct sparx5_tx *tx)
{
	/* Disable the channel */
	spx5_rmw(0, BIT(tx->channel_id) & FDMA_CH_ACTIVATE_CH_ACTIVATE,
		 sparx5, FDMA_CH_ACTIVATE);
}

static void sparx5_fdma_rx_reload(struct sparx5 *sparx5, struct sparx5_rx *rx)
{
	/* Reload the RX channel */
	spx5_wr(BIT(rx->channel_id), sparx5, FDMA_CH_RELOAD);
}

static void sparx5_fdma_tx_reload(struct sparx5 *sparx5, struct sparx5_tx *tx)
{
	/* Reload the TX channel */
	spx5_wr(BIT(tx->channel_id), sparx5, FDMA_CH_RELOAD);
}

static struct sk_buff *sparx5_fdma_rx_alloc_skb(struct sparx5_rx *rx)
{
	return __netdev_alloc_skb(rx->ndev, FDMA_XTR_BUFFER_SIZE,
				  GFP_ATOMIC);
}

static bool sparx5_fdma_rx_get_frame(struct sparx5 *sparx5, struct sparx5_rx *rx)
{
	struct sparx5_db_hw *db_hw;
	unsigned int packet_size;
	struct sparx5_port *port;
	struct sk_buff *new_skb;
	struct frame_info fi;
	struct sk_buff *skb;
	dma_addr_t dma_addr;

	/* Check if the DCB is done */
	db_hw = &rx->dcb_entries[rx->dcb_index].db[rx->db_index];
	if (unlikely(!(db_hw->status & FDMA_DCB_STATUS_DONE)))
		return false;
	skb = rx->skb[rx->dcb_index][rx->db_index];
	/* Replace the DB entry with a new SKB */
	new_skb = sparx5_fdma_rx_alloc_skb(rx);
	if (unlikely(!new_skb))
		return false;
	/* Map the new skb data and set the new skb */
	dma_addr = virt_to_phys(new_skb->data);
	rx->skb[rx->dcb_index][rx->db_index] = new_skb;
	db_hw->dataptr = dma_addr;
	packet_size = FDMA_DCB_STATUS_BLOCKL(db_hw->status);
	skb_put(skb, packet_size);
	/* Now do the normal processing of the skb */
	sparx5_ifh_parse((u32 *)skb->data, &fi);
	/* Map to port netdev */
	port = fi.src_port < SPX5_PORTS ?  sparx5->ports[fi.src_port] : NULL;
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

static int sparx5_fdma_napi_callback(struct napi_struct *napi, int weight)
{
	struct sparx5_rx *rx = container_of(napi, struct sparx5_rx, napi);
	struct sparx5 *sparx5 = container_of(rx, struct sparx5, rx);
	int counter = 0;

	while (counter < weight && sparx5_fdma_rx_get_frame(sparx5, rx)) {
		struct sparx5_rx_dcb_hw *old_dcb;

		rx->db_index++;
		counter++;
		/* Check if the DCB can be reused */
		if (rx->db_index != FDMA_RX_DCB_MAX_DBS)
			continue;
		/* As the DCB  can be reused, just advance the dcb_index
		 * pointer and set the nextptr in the DCB
		 */
		rx->db_index = 0;
		old_dcb = &rx->dcb_entries[rx->dcb_index];
		rx->dcb_index++;
		rx->dcb_index &= FDMA_DCB_MAX - 1;
		sparx5_fdma_rx_add_dcb(rx, old_dcb,
				       rx->dma +
				       ((unsigned long)old_dcb -
					(unsigned long)rx->dcb_entries));
	}
	if (counter < weight) {
		napi_complete_done(&rx->napi, counter);
		spx5_rmw(BIT(rx->channel_id),
			 BIT(rx->channel_id) & FDMA_INTR_DB_ENA_INTR_DB_ENA,
			 sparx5, FDMA_INTR_DB_ENA);
	}
	if (counter)
		sparx5_fdma_rx_reload(sparx5, rx);
	return counter;
}

static struct sparx5_tx_dcb_hw *sparx5_fdma_next_dcb(struct sparx5_tx *tx,
						     struct sparx5_tx_dcb_hw *dcb)
{
	struct sparx5_tx_dcb_hw *next_dcb;

	next_dcb = dcb;
	next_dcb++;
	/* Handle wrap-around */
	if ((unsigned long)next_dcb >=
	    ((unsigned long)tx->first_entry + FDMA_DCB_MAX * sizeof(*dcb)))
		next_dcb = tx->first_entry;
	return next_dcb;
}

int sparx5_fdma_xmit(struct sparx5 *sparx5, u32 *ifh, struct sk_buff *skb)
{
	struct sparx5_tx_dcb_hw *next_dcb_hw;
	struct sparx5_tx *tx = &sparx5->tx;
	static bool first_time = true;
	struct sparx5_db_hw *db_hw;
	struct sparx5_db *db;

	next_dcb_hw = sparx5_fdma_next_dcb(tx, tx->curr_entry);
	db_hw = &next_dcb_hw->db[0];
	if (!(db_hw->status & FDMA_DCB_STATUS_DONE))
		return -EINVAL;
	db = list_first_entry(&tx->db_list, struct sparx5_db, list);
	list_move_tail(&db->list, &tx->db_list);
	next_dcb_hw->nextptr = FDMA_DCB_INVALID_DATA;
	tx->curr_entry->nextptr = tx->dma +
		((unsigned long)next_dcb_hw -
		 (unsigned long)tx->first_entry);
	tx->curr_entry = next_dcb_hw;
	memset(db->cpu_addr, 0, FDMA_XTR_BUFFER_SIZE);
	memcpy(db->cpu_addr, ifh, IFH_LEN * 4);
	memcpy(db->cpu_addr + IFH_LEN * 4, skb->data, skb->len);
	db_hw->status = FDMA_DCB_STATUS_SOF |
			FDMA_DCB_STATUS_EOF |
			FDMA_DCB_STATUS_BLOCKO(0) |
			FDMA_DCB_STATUS_BLOCKL(skb->len + IFH_LEN * 4 + 4);
	if (first_time) {
		sparx5_fdma_tx_activate(sparx5, tx);
		first_time = false;
	} else {
		sparx5_fdma_tx_reload(sparx5, tx);
	}
	return NETDEV_TX_OK;
}

static int sparx5_fdma_rx_alloc(struct sparx5 *sparx5)
{
	struct sparx5_rx *rx = &sparx5->rx;
	struct sparx5_rx_dcb_hw *dcb;
	int idx, jdx;
	int size;

	size = sizeof(struct sparx5_rx_dcb_hw) * FDMA_DCB_MAX;
	size = ALIGN(size, PAGE_SIZE);
	rx->dcb_entries = devm_kzalloc(sparx5->dev, size, GFP_KERNEL);
	if (!rx->dcb_entries)
		return -ENOMEM;
	rx->dma = virt_to_phys(rx->dcb_entries);
	rx->last_entry = rx->dcb_entries;
	rx->db_index = 0;
	rx->dcb_index = 0;
	/* Now for each dcb allocate the db */
	for (idx = 0; idx < FDMA_DCB_MAX; ++idx) {
		dcb = &rx->dcb_entries[idx];
		dcb->info = 0;
		/* For each db allocate an skb and map skb data pointer to the DB
		 * dataptr. In this way when the frame is received the skb->data
		 * will contain the frame, so no memcpy is needed
		 */
		for (jdx = 0; jdx < FDMA_RX_DCB_MAX_DBS; ++jdx) {
			struct sparx5_db_hw *db_hw = &dcb->db[jdx];
			dma_addr_t dma_addr;
			struct sk_buff *skb;

			skb = sparx5_fdma_rx_alloc_skb(rx);
			if (!skb)
				return -ENOMEM;

			dma_addr = virt_to_phys(skb->data);
			db_hw->dataptr = dma_addr;
			db_hw->status = 0;
			rx->skb[idx][jdx] = skb;
		}
		sparx5_fdma_rx_add_dcb(rx, dcb, rx->dma + sizeof(*dcb) * idx);
	}
	netif_napi_add_weight(rx->ndev, &rx->napi, sparx5_fdma_napi_callback,
			      FDMA_WEIGHT);
	napi_enable(&rx->napi);
	sparx5_fdma_rx_activate(sparx5, rx);
	return 0;
}

static int sparx5_fdma_tx_alloc(struct sparx5 *sparx5)
{
	struct sparx5_tx *tx = &sparx5->tx;
	struct sparx5_tx_dcb_hw *dcb;
	int idx, jdx;
	int size;

	size = sizeof(struct sparx5_tx_dcb_hw) * FDMA_DCB_MAX;
	size = ALIGN(size, PAGE_SIZE);
	tx->curr_entry = devm_kzalloc(sparx5->dev, size, GFP_KERNEL);
	if (!tx->curr_entry)
		return -ENOMEM;
	tx->dma = virt_to_phys(tx->curr_entry);
	tx->first_entry = tx->curr_entry;
	INIT_LIST_HEAD(&tx->db_list);
	/* Now for each dcb allocate the db */
	for (idx = 0; idx < FDMA_DCB_MAX; ++idx) {
		dcb = &tx->curr_entry[idx];
		dcb->info = 0;
		/* TX databuffers must be 16byte aligned */
		for (jdx = 0; jdx < FDMA_TX_DCB_MAX_DBS; ++jdx) {
			struct sparx5_db_hw *db_hw = &dcb->db[jdx];
			struct sparx5_db *db;
			dma_addr_t phys;
			void *cpu_addr;

			cpu_addr = devm_kzalloc(sparx5->dev,
						FDMA_XTR_BUFFER_SIZE,
						GFP_KERNEL);
			if (!cpu_addr)
				return -ENOMEM;
			phys = virt_to_phys(cpu_addr);
			db_hw->dataptr = phys;
			db_hw->status = 0;
			db = devm_kzalloc(sparx5->dev, sizeof(*db), GFP_KERNEL);
			if (!db)
				return -ENOMEM;
			db->cpu_addr = cpu_addr;
			list_add_tail(&db->list, &tx->db_list);
		}
		sparx5_fdma_tx_add_dcb(tx, dcb, tx->dma + sizeof(*dcb) * idx);
		/* Let the curr_entry to point to the last allocated entry */
		if (idx == FDMA_DCB_MAX - 1)
			tx->curr_entry = dcb;
	}
	return 0;
}

static void sparx5_fdma_rx_init(struct sparx5 *sparx5,
				struct sparx5_rx *rx, int channel)
{
	int idx;

	rx->channel_id = channel;
	/* Fetch a netdev for SKB and NAPI use, any will do */
	for (idx = 0; idx < SPX5_PORTS; ++idx) {
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
	tx->channel_id = channel;
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

static void sparx5_fdma_injection_mode(struct sparx5 *sparx5)
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
	for (portno = SPX5_PORT_CPU_0; portno <= SPX5_PORT_CPU_1; portno++) {
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

int sparx5_fdma_start(struct sparx5 *sparx5)
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

static u32 sparx5_fdma_port_ctrl(struct sparx5 *sparx5)
{
	return spx5_rd(sparx5, FDMA_PORT_CTRL(0));
}

int sparx5_fdma_stop(struct sparx5 *sparx5)
{
	u32 val;

	napi_disable(&sparx5->rx.napi);
	/* Stop the fdma and channel interrupts */
	sparx5_fdma_rx_deactivate(sparx5, &sparx5->rx);
	sparx5_fdma_tx_deactivate(sparx5, &sparx5->tx);
	/* Wait for the RX channel to stop */
	read_poll_timeout(sparx5_fdma_port_ctrl, val,
			  FDMA_PORT_CTRL_XTR_BUF_IS_EMPTY_GET(val) == 0,
			  500, 10000, 0, sparx5);
	return 0;
}
