/*******************************************************************************
 *
 * Thunderbolt(TM) driver
 * Copyright(c) 2014 - 2017 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#include <linux/etherdevice.h>
#include <linux/crc32.h>
#include <linux/prefetch.h>
#include <linux/highmem.h>
#include <linux/if_vlan.h>
#include <linux/jhash.h>
#include <linux/vmalloc.h>
#include <net/ip6_checksum.h>
#include "icm_nhi.h"
#include "net.h"

#define DEFAULT_MSG_ENABLE (NETIF_MSG_PROBE | NETIF_MSG_LINK | NETIF_MSG_IFUP)
static int debug = -1;
module_param(debug, int, 0000);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

#define TBT_NET_RX_HDR_SIZE 256

#define NUM_TX_LOGIN_RETRIES 60

#define APPLE_THUNDERBOLT_IP_PROTOCOL_REVISION 1

#define LOGIN_TX_PATH 0xf

#define TBT_NET_MTU (64 * 1024)

#define TBT_RX_DMA_ATTR (DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

/* Number of Rx buffers we bundle into one write to the hardware */
#define TBT_NET_RX_BUFFER_WRITE	16

#define TBT_NET_MULTICAST_HASH_TABLE_SIZE 1024
#define TBT_NET_ETHER_ADDR_HASH(addr) (((addr[4] >> 4) | (addr[5] << 4)) % \
				       TBT_NET_MULTICAST_HASH_TABLE_SIZE)

#define BITS_PER_U32 (sizeof(u32) * BITS_PER_BYTE)

#define TBT_NET_NUM_TX_BUFS 256
#define TBT_NET_NUM_RX_BUFS 256
#define TBT_NET_SIZE_TOTAL_DESCS ((TBT_NET_NUM_TX_BUFS + TBT_NET_NUM_RX_BUFS) \
				  * sizeof(struct tbt_buf_desc))


#define TBT_NUM_FRAMES_PER_PAGE (PAGE_SIZE / TBT_RING_MAX_FRAME_SIZE)

#define TBT_NUM_BUFS_BETWEEN(idx1, idx2, num_bufs) \
	(((num_bufs) - 1) - \
	 ((((idx1) - (idx2)) + (num_bufs)) & ((num_bufs) - 1)))

#define TX_WAKE_THRESHOLD (2 * DIV_ROUND_UP(TBT_NET_MTU, \
			   TBT_RING_MAX_FRM_DATA_SZ))

#define TBT_NET_DESC_ATTR_SOF_EOF (((PDF_TBT_NET_START_OF_FRAME << \
				     DESC_ATTR_SOF_SHIFT) & \
				    DESC_ATTR_SOF_MASK) | \
				   ((PDF_TBT_NET_END_OF_FRAME << \
				     DESC_ATTR_EOF_SHIFT) & \
				    DESC_ATTR_EOF_MASK))

/* E2E workaround */
#define TBT_EXIST_BUT_UNUSED_HOPID 2

enum tbt_net_frame_pdf {
	PDF_TBT_NET_MIDDLE_FRAME,
	PDF_TBT_NET_START_OF_FRAME,
	PDF_TBT_NET_END_OF_FRAME,
};

struct thunderbolt_ip_login {
	struct thunderbolt_ip_header header;
	__be32 protocol_revision;
	__be32 transmit_path;
	__be32 reserved[4];
	__be32 crc;
};

struct thunderbolt_ip_login_response {
	struct thunderbolt_ip_header header;
	__be32 status;
	__be32 receiver_mac_address[2];
	__be32 receiver_mac_address_length;
	__be32 reserved[4];
	__be32 crc;
};

struct thunderbolt_ip_logout {
	struct thunderbolt_ip_header header;
	__be32 crc;
};

struct thunderbolt_ip_status {
	struct thunderbolt_ip_header header;
	__be32 status;
	__be32 crc;
};

struct approve_inter_domain_connection_cmd {
	__be32 req_code;
	__be32 attributes;
#define AIDC_ATTR_LINK_SHIFT	16
#define AIDC_ATTR_LINK_MASK	GENMASK(18, AIDC_ATTR_LINK_SHIFT)
#define AIDC_ATTR_DEPTH_SHIFT	20
#define AIDC_ATTR_DEPTH_MASK	GENMASK(23, AIDC_ATTR_DEPTH_SHIFT)
	uuid_be remote_uuid;
	__be16 transmit_ring_number;
	__be16 transmit_path;
	__be16 receive_ring_number;
	__be16 receive_path;
	__be32 crc;
};

struct tbt_frame_header {
	/* size of the data with the frame */
	__le32 frame_size;
	/* running index on the frames */
	__le16 frame_index;
	/* ID of the frame to match frames to specific packet */
	__le16 frame_id;
	/* how many frames assembles a full packet */
	__le32 frame_count;
};

enum neg_event {
	RECEIVE_LOGOUT = NUM_MEDIUM_STATUSES,
	RECEIVE_LOGIN_RESPONSE,
	RECEIVE_LOGIN,
	NUM_NEG_EVENTS
};

enum frame_status {
	GOOD_MID_FRAME,
	GOOD_AS_FIRST_FRAME,
	GOOD_AS_FIRST_MULTICAST_FRAME,
	FRAME_NOT_READY,
	FRAME_ERROR,
};

enum packet_filter {
	/* all multicast MAC addresses */
	PACKET_TYPE_ALL_MULTICAST,
	/* all types of MAC addresses: multicast, unicast and broadcast */
	PACKET_TYPE_PROMISCUOUS,
	/* all unicast MAC addresses */
	PACKET_TYPE_UNICAST_PROMISCUOUS,
};

enum disconnect_path_stage {
	STAGE_1 = BIT(0),
	STAGE_2 = BIT(1)
};

struct tbt_net_stats {
	u64 tx_packets;
	u64 tx_bytes;
	u64 tx_errors;
	u64 rx_packets;
	u64 rx_bytes;
	u64 rx_length_errors;
	u64 rx_over_errors;
	u64 rx_crc_errors;
	u64 rx_missed_errors;
	u64 multicast;
};

static const char tbt_net_gstrings_stats[][ETH_GSTRING_LEN] = {
	"tx_packets",
	"tx_bytes",
	"tx_errors",
	"rx_packets",
	"rx_bytes",
	"rx_length_errors",
	"rx_over_errors",
	"rx_crc_errors",
	"rx_missed_errors",
	"multicast",
};

struct tbt_buffer {
	dma_addr_t dma;
	union {
		struct tbt_frame_header *hdr;
		struct page *page;
	};
	u32 page_offset;
};

struct tbt_desc_ring {
	/* pointer to the descriptor ring memory */
	struct tbt_buf_desc *desc;
	/* physical address of the descriptor ring */
	dma_addr_t dma;
	/* array of buffer structs */
	struct tbt_buffer *buffers;
	/* last descriptor that was associated with a buffer */
	u16 last_allocated;
	/* next descriptor to check for DD status bit */
	u16 next_to_clean;
};

/**
 *  struct tbt_port - the basic tbt_port structure
 *  @tbt_nhi_ctxt:		context of the nhi controller.
 *  @net_dev:			networking device object.
 *  @napi:			network API
 *  @login_retry_work:		work queue for sending login requests.
 *  @login_response_work:	work queue for sending login responses.
 *  @work_struct logout_work:	work queue for sending logout requests.
 *  @status_reply_work:		work queue for sending logout replies.
 *  @approve_inter_domain_work:	work queue for sending interdomain to icm.
 *  @route_str:			allows to route the messages to destination.
 *  @interdomain_local_uuid:	allows to route the messages from local source.
 *  @interdomain_remote_uuid:	allows to route the messages to destination.
 *  @command_id			a number that identifies the command.
 *  @negotiation_status:	holds the network negotiation state.
 *  @msg_enable:		used for debugging filters.
 *  @seq_num:			a number that identifies the session.
 *  @login_retry_count:		counts number of login retries sent.
 *  @local_depth:		depth of the remote peer in the chain.
 *  @transmit_path:		routing parameter for the icm.
 *  @tx_ring:			transmit ring from where the packets are sent.
 *  @rx_ring:			receive ring  where the packets are received.
 *  @stats:			network statistics of the rx/tx packets.
 *  @packet_filters:		defines filters for the received packets.
 *  @multicast_hash_table:	hash table of multicast addresses.
 *  @frame_id:			counting ID of frames.
 *  @num:			port number.
 *  @local_path:		routing parameter for the icm.
 *  @enable_full_e2e:		whether to enable full E2E.
 *  @match_frame_id:		whether to match frame id on incoming packets.
 */
struct tbt_port {
	struct tbt_nhi_ctxt *nhi_ctxt;
	struct net_device *net_dev;
	struct napi_struct napi;
	struct delayed_work login_retry_work;
	struct work_struct login_response_work;
	struct work_struct logout_work;
	struct work_struct status_reply_work;
	struct work_struct approve_inter_domain_work;
	struct route_string route_str;
	uuid_be interdomain_local_uuid;
	uuid_be interdomain_remote_uuid;
	u32 command_id;
	u16 negotiation_status;
	u16 msg_enable;
	u8 seq_num;
	u8 login_retry_count;
	u8 local_depth;
	u8 transmit_path;
	struct tbt_desc_ring tx_ring ____cacheline_aligned_in_smp;
	struct tbt_desc_ring rx_ring;
	struct tbt_net_stats stats;
	u32 packet_filters;
	/*
	 * hash table of 1024 boolean entries with hashing of
	 * the multicast address
	 */
	u32 multicast_hash_table[DIV_ROUND_UP(
					TBT_NET_MULTICAST_HASH_TABLE_SIZE,
					BITS_PER_U32)];
	u16 frame_id;
	u8 num;
	u8 local_path;
	bool enable_full_e2e : 1;
	bool match_frame_id : 1;
};

static void disconnect_path(struct tbt_port *port,
			    enum disconnect_path_stage stage)
{
	u32 cmd = (DISCONNECT_PORT_A_INTER_DOMAIN_PATH + port->num);

	cmd <<= REG_INMAIL_CMD_CMD_SHIFT;
	cmd &= REG_INMAIL_CMD_CMD_MASK;
	cmd |= REG_INMAIL_CMD_REQUEST;

	mutex_lock(&port->nhi_ctxt->mailbox_mutex);
	if (!mutex_trylock(&port->nhi_ctxt->d0_exit_mailbox_mutex)) {
		netif_notice(port, link, port->net_dev, "controller id %#x is existing D0\n",
			     port->nhi_ctxt->id);
	} else {
		nhi_mailbox(port->nhi_ctxt, cmd, stage, false);

		port->nhi_ctxt->net_devices[port->num].medium_sts =
					MEDIUM_READY_FOR_CONNECTION;

		mutex_unlock(&port->nhi_ctxt->d0_exit_mailbox_mutex);
	}
	mutex_unlock(&port->nhi_ctxt->mailbox_mutex);
}

static void tbt_net_tear_down(struct net_device *net_dev, bool send_logout)
{
	struct tbt_port *port = netdev_priv(net_dev);
	void __iomem *iobase = port->nhi_ctxt->iobase;
	void __iomem *tx_reg = NULL;
	u32 tx_reg_val = 0;

	netif_carrier_off(net_dev);
	netif_stop_queue(net_dev);

	if (port->negotiation_status & BIT(MEDIUM_CONNECTED)) {
		void __iomem *rx_reg = iobase + REG_RX_OPTIONS_BASE +
		      (port->local_path * REG_OPTS_STEP);
		u32 rx_reg_val = ioread32(rx_reg) & ~REG_OPTS_E2E_EN;

		napi_disable(&port->napi);

		tx_reg = iobase + REG_TX_OPTIONS_BASE +
			 (port->local_path * REG_OPTS_STEP);
		tx_reg_val = ioread32(tx_reg) & ~REG_OPTS_E2E_EN;

		disconnect_path(port, STAGE_1);

		/* disable RX flow control  */
		iowrite32(rx_reg_val, rx_reg);
		/* disable TX flow control  */
		iowrite32(tx_reg_val, tx_reg);
		/* disable RX ring  */
		iowrite32(rx_reg_val & ~REG_OPTS_VALID, rx_reg);

		rx_reg = iobase + REG_RX_RING_BASE +
			 (port->local_path * REG_RING_STEP);
		iowrite32(0, rx_reg + REG_RING_PHYS_LO_OFFSET);
		iowrite32(0, rx_reg + REG_RING_PHYS_HI_OFFSET);
	}

	/* Stop login messages */
	cancel_delayed_work_sync(&port->login_retry_work);

	if (send_logout)
		queue_work(port->nhi_ctxt->net_workqueue, &port->logout_work);

	if (port->negotiation_status & BIT(MEDIUM_CONNECTED)) {
		unsigned long flags;

		/* wait for TX to finish */
		usleep_range(5 * USEC_PER_MSEC, 7 * USEC_PER_MSEC);
		/* disable TX ring  */
		iowrite32(tx_reg_val & ~REG_OPTS_VALID, tx_reg);

		disconnect_path(port, STAGE_2);

		spin_lock_irqsave(&port->nhi_ctxt->lock, flags);
		/* disable RX and TX interrupts */
		RING_INT_DISABLE_TX_RX(iobase, port->local_path,
				       port->nhi_ctxt->num_paths);
		spin_unlock_irqrestore(&port->nhi_ctxt->lock, flags);
	}

	port->rx_ring.next_to_clean = 0;
	port->rx_ring.last_allocated = TBT_NET_NUM_RX_BUFS - 1;

}

void tbt_net_tx_msi(struct net_device *net_dev)
{
	struct tbt_port *port = netdev_priv(net_dev);
	void __iomem *iobase = port->nhi_ctxt->iobase;
	u32 prod_cons, prod, cons;

	prod_cons = ioread32(TBT_RING_CONS_PROD_REG(iobase, REG_TX_RING_BASE,
						    port->local_path));
	prod = TBT_REG_RING_PROD_EXTRACT(prod_cons);
	cons = TBT_REG_RING_CONS_EXTRACT(prod_cons);
	if (prod >= TBT_NET_NUM_TX_BUFS || cons >= TBT_NET_NUM_TX_BUFS)
		return;

	if (TBT_NUM_BUFS_BETWEEN(prod, cons, TBT_NET_NUM_TX_BUFS) >=
							TX_WAKE_THRESHOLD) {
		netif_wake_queue(port->net_dev);
	} else {
		spin_lock(&port->nhi_ctxt->lock);
		/* enable TX interrupt */
		RING_INT_ENABLE_TX(iobase, port->local_path);
		spin_unlock(&port->nhi_ctxt->lock);
	}
}

static irqreturn_t tbt_net_tx_msix(int __always_unused irq, void *data)
{
	struct tbt_port *port = data;
	void __iomem *iobase = port->nhi_ctxt->iobase;
	u32 prod_cons, prod, cons;

	prod_cons = ioread32(TBT_RING_CONS_PROD_REG(iobase,
						    REG_TX_RING_BASE,
						    port->local_path));
	prod = TBT_REG_RING_PROD_EXTRACT(prod_cons);
	cons = TBT_REG_RING_CONS_EXTRACT(prod_cons);
	if (prod < TBT_NET_NUM_TX_BUFS && cons < TBT_NET_NUM_TX_BUFS &&
	    TBT_NUM_BUFS_BETWEEN(prod, cons, TBT_NET_NUM_TX_BUFS) >=
							TX_WAKE_THRESHOLD) {
		spin_lock(&port->nhi_ctxt->lock);
		/* disable TX interrupt */
		RING_INT_DISABLE_TX(iobase, port->local_path);
		spin_unlock(&port->nhi_ctxt->lock);

		netif_wake_queue(port->net_dev);
	}

	return IRQ_HANDLED;
}

void tbt_net_rx_msi(struct net_device *net_dev)
{
	struct tbt_port *port = netdev_priv(net_dev);

	napi_schedule_irqoff(&port->napi);
}

static irqreturn_t tbt_net_rx_msix(int __always_unused irq, void *data)
{
	struct tbt_port *port = data;

	if (likely(napi_schedule_prep(&port->napi))) {
		struct tbt_nhi_ctxt *nhi_ctx = port->nhi_ctxt;

		spin_lock(&nhi_ctx->lock);
		/* disable RX interrupt */
		RING_INT_DISABLE_RX(nhi_ctx->iobase, port->local_path,
				    nhi_ctx->num_paths);
		spin_unlock(&nhi_ctx->lock);

		__napi_schedule_irqoff(&port->napi);
	}

	return IRQ_HANDLED;
}

static void tbt_net_pull_tail(struct sk_buff *skb)
{
	skb_frag_t *frag = &skb_shinfo(skb)->frags[0];
	unsigned int pull_len;
	unsigned char *va;

	/*
	 * it is valid to use page_address instead of kmap since we are
	 * working with pages allocated out of the lomem pool
	 */
	va = skb_frag_address(frag);

	pull_len = eth_get_headlen(va, TBT_NET_RX_HDR_SIZE);

	/* align pull length to size of long to optimize memcpy performance */
	skb_copy_to_linear_data(skb, va, ALIGN(pull_len, sizeof(long)));

	/* update all of the pointers */
	skb_frag_size_sub(frag, pull_len);
	frag->page_offset += pull_len;
	skb->data_len -= pull_len;
	skb->tail += pull_len;
}

static inline bool tbt_net_alloc_mapped_page(struct device *dev,
					     struct tbt_buffer *buf, gfp_t gfp)
{
	if (!buf->page) {
		buf->page = alloc_page(gfp | __GFP_COLD);
		if (unlikely(!buf->page))
			return false;

		buf->dma = dma_map_page_attrs(dev, buf->page, 0, PAGE_SIZE,
					      DMA_FROM_DEVICE,
					      TBT_RX_DMA_ATTR);
		if (dma_mapping_error(dev, buf->dma)) {
			__free_page(buf->page);
			buf->page = NULL;
			return false;
		}
		buf->page_offset = 0;
	}
	return true;
}

static bool tbt_net_alloc_rx_buffers(struct device *dev,
				     struct tbt_desc_ring *rx_ring,
				     u16 cleaned_count, void __iomem *reg,
				     gfp_t gfp)
{
	u16 i = (rx_ring->last_allocated + 1) & (TBT_NET_NUM_RX_BUFS - 1);
	bool res = false;

	while (cleaned_count--) {
		struct tbt_buf_desc *desc = &rx_ring->desc[i];
		struct tbt_buffer *buf = &rx_ring->buffers[i];

		/* making sure next_to_clean won't get old buffer */
		desc->attributes = cpu_to_le32(DESC_ATTR_REQ_STS |
					       DESC_ATTR_INT_EN);
		if (!tbt_net_alloc_mapped_page(dev, buf, gfp))
			break;

		/* sync the buffer for use by the device */
		dma_sync_single_range_for_device(dev, buf->dma,
						 buf->page_offset,
						 TBT_RING_MAX_FRAME_SIZE,
						 DMA_FROM_DEVICE);
		res = true;
		rx_ring->last_allocated = i;
		i = (i + 1) & (TBT_NET_NUM_RX_BUFS - 1);
		desc->phys = cpu_to_le64(buf->dma + buf->page_offset);
	}

	if (res) {
		iowrite32((rx_ring->last_allocated << REG_RING_CONS_SHIFT) &
			  REG_RING_CONS_MASK, reg);
	}

	return res;
}

static inline bool tbt_net_multicast_mac_set(const u32 *multicast_hash_table,
					     const u8 *ether_addr)
{
	u16 hash_val = TBT_NET_ETHER_ADDR_HASH(ether_addr);

	return !!(multicast_hash_table[hash_val / BITS_PER_U32] &
		  BIT(hash_val % BITS_PER_U32));
}

static enum frame_status tbt_net_check_frame(struct tbt_port *port,
					     u16 frame_num, u32 *count,
					     u16 index, u16 *id, u32 *size)
{
	struct tbt_desc_ring *rx_ring = &port->rx_ring;
	__le32 desc_attr = rx_ring->desc[frame_num].attributes;
	enum frame_status res = GOOD_AS_FIRST_FRAME;
	u32 len, frame_count, frame_size;
	struct tbt_frame_header *hdr;

	if (!(desc_attr & cpu_to_le32(DESC_ATTR_DESC_DONE)))
		return FRAME_NOT_READY;

	rmb(); /* read other fields from desc after checking DD */

	if (unlikely(desc_attr & cpu_to_le32(DESC_ATTR_RX_CRC_ERR))) {
		++port->stats.rx_crc_errors;
		goto err;
	} else if (unlikely(desc_attr &
				cpu_to_le32(DESC_ATTR_RX_BUF_OVRN_ERR))) {
		++port->stats.rx_over_errors;
		goto err;
	}

	len = (le32_to_cpu(desc_attr) & DESC_ATTR_LEN_MASK)
	      >> DESC_ATTR_LEN_SHIFT;
	if (len == 0)
		len = TBT_RING_MAX_FRAME_SIZE;
	/* should be greater than just header i.e. contains data */
	if (unlikely(len <= sizeof(struct tbt_frame_header))) {
		++port->stats.rx_length_errors;
		goto err;
	}

	prefetchw(rx_ring->buffers[frame_num].page);
	hdr = page_address(rx_ring->buffers[frame_num].page) +
				rx_ring->buffers[frame_num].page_offset;
	/* prefetch first cache line of first page */
	prefetch(hdr);

	/* we are reusing so sync this buffer for CPU use */
	dma_sync_single_range_for_cpu(&port->nhi_ctxt->pdev->dev,
				      rx_ring->buffers[frame_num].dma,
				      rx_ring->buffers[frame_num].page_offset,
				      len,
				      DMA_FROM_DEVICE);

	frame_count = le32_to_cpu(hdr->frame_count);
	frame_size = le32_to_cpu(hdr->frame_size);

	if (unlikely((frame_size > len - sizeof(struct tbt_frame_header)) ||
		     (frame_size == 0))) {
		++port->stats.rx_length_errors;
		goto err;
	}
	/*
	 * In case we're in the middle of packet, validate the frame header
	 * based on first fragment of the packet
	 */
	if (*count) {
		/* check the frame count fits the count field */
		if (frame_count != *count) {
			++port->stats.rx_length_errors;
			goto check_as_first;
		}

		/*
		 * check the frame identifiers are incremented correctly,
		 * and id is matching
		 */
		if ((le16_to_cpu(hdr->frame_index) != index) ||
		    (le16_to_cpu(hdr->frame_id) != *id)) {
			++port->stats.rx_missed_errors;
			goto check_as_first;
		}

		*size += frame_size;
		if (*size > TBT_NET_MTU) {
			++port->stats.rx_length_errors;
			goto err;
		}
		res = GOOD_MID_FRAME;
	} else { /* start of packet, validate the frame header */
		const u8 *addr;

check_as_first:
		rx_ring->next_to_clean = frame_num;

		/* validate the first packet has a valid frame count */
		if (unlikely(frame_count == 0 ||
			     frame_count > (TBT_NET_NUM_RX_BUFS / 4))) {
			++port->stats.rx_length_errors;
			goto err;
		}

		/* validate the first packet has a valid frame index */
		if (hdr->frame_index != 0) {
			++port->stats.rx_missed_errors;
			goto err;
		}

		BUILD_BUG_ON(TBT_NET_RX_HDR_SIZE > TBT_RING_MAX_FRM_DATA_SZ);
		if ((frame_count > 1) && (frame_size < TBT_NET_RX_HDR_SIZE)) {
			++port->stats.rx_length_errors;
			goto err;
		}

		addr = (u8 *)(hdr + 1);

		/* check the packet can go through the filter */
		if (is_multicast_ether_addr(addr)) {
			if (!is_broadcast_ether_addr(addr)) {
				if ((port->packet_filters &
				     (BIT(PACKET_TYPE_PROMISCUOUS) |
				      BIT(PACKET_TYPE_ALL_MULTICAST))) ||
				    tbt_net_multicast_mac_set(
					port->multicast_hash_table, addr))
					res = GOOD_AS_FIRST_MULTICAST_FRAME;
				else
					goto err;
			}
		} else if (!(port->packet_filters &
			     (BIT(PACKET_TYPE_PROMISCUOUS) |
			      BIT(PACKET_TYPE_UNICAST_PROMISCUOUS))) &&
			   !ether_addr_equal(port->net_dev->dev_addr, addr)) {
			goto err;
		}

		*size = frame_size;
		*count = frame_count;
		*id = le16_to_cpu(hdr->frame_id);
	}

#if (PREFETCH_STRIDE < 128)
	prefetch((u8 *)hdr + PREFETCH_STRIDE);
#endif

	return res;

err:
	rx_ring->next_to_clean = (frame_num + 1) & (TBT_NET_NUM_RX_BUFS - 1);
	return FRAME_ERROR;
}

static inline unsigned int tbt_net_max_frm_data_size(
						__maybe_unused u32 frame_size)
{
#if (TBT_NUM_FRAMES_PER_PAGE > 1)
	return ALIGN(frame_size + sizeof(struct tbt_frame_header),
		     L1_CACHE_BYTES) -
	       sizeof(struct tbt_frame_header);
#else
	return TBT_RING_MAX_FRM_DATA_SZ;
#endif
}

static int tbt_net_poll(struct napi_struct *napi, int budget)
{
	struct tbt_port *port = container_of(napi, struct tbt_port, napi);
	void __iomem *reg = TBT_RING_CONS_PROD_REG(port->nhi_ctxt->iobase,
						   REG_RX_RING_BASE,
						   port->local_path);
	struct tbt_desc_ring *rx_ring = &port->rx_ring;
	u16 cleaned_count = TBT_NUM_BUFS_BETWEEN(rx_ring->last_allocated,
						 rx_ring->next_to_clean,
						 TBT_NET_NUM_RX_BUFS);
	unsigned long flags;
	int rx_packets = 0;

loop:
	while (likely(rx_packets < budget)) {
		struct sk_buff *skb;
		enum frame_status status;
		bool multicast = false;
		u32 frame_count = 0, size;
		u16 j, frame_id;
		int i;

		/*
		 * return some buffers to hardware, one at a time is too slow
		 * so allocate  TBT_NET_RX_BUFFER_WRITE buffers at the same time
		 */
		if (cleaned_count >= TBT_NET_RX_BUFFER_WRITE) {
			tbt_net_alloc_rx_buffers(&port->nhi_ctxt->pdev->dev,
						 rx_ring, cleaned_count, reg,
						 GFP_ATOMIC);
			cleaned_count = 0;
		}

		status = tbt_net_check_frame(port, rx_ring->next_to_clean,
					     &frame_count, 0, &frame_id,
					     &size);
		if (status == FRAME_NOT_READY)
			break;

		if (status == FRAME_ERROR) {
			++cleaned_count;
			continue;
		}

		multicast = (status == GOOD_AS_FIRST_MULTICAST_FRAME);

		/*
		 *  i is incremented up to the frame_count frames received,
		 *  j cyclicly goes over the location from the next frame
		 *  to clean in the ring
		 */
		j = (rx_ring->next_to_clean + 1);
		j &= (TBT_NET_NUM_RX_BUFS - 1);
		for (i = 1; i < frame_count; ++i) {
			status = tbt_net_check_frame(port, j, &frame_count, i,
						     &frame_id, &size);
			if (status == FRAME_NOT_READY)
				goto out;

			j = (j + 1) & (TBT_NET_NUM_RX_BUFS - 1);

			/* if a new frame is found, start over */
			if (status == GOOD_AS_FIRST_FRAME ||
			    status == GOOD_AS_FIRST_MULTICAST_FRAME) {
				multicast = (status ==
					     GOOD_AS_FIRST_MULTICAST_FRAME);
				cleaned_count += i;
				i = 0;
				continue;
			}

			if (status == FRAME_ERROR) {
				cleaned_count += (i + 1);
				goto loop;
			}
		}

		/* allocate a skb to store the frags */
		skb = netdev_alloc_skb_ip_align(port->net_dev,
						TBT_NET_RX_HDR_SIZE);
		if (unlikely(!skb))
			break;

		/*
		 * we will be copying header into skb->data in
		 * tbt_net_pull_tail so it is in our interest to prefetch
		 * it now to avoid a possible cache miss
		 */
		prefetchw(skb->data);

		/*
		 * if overall size of packet smaller than TBT_NET_RX_HDR_SIZE
		 * which is a small buffer size we decided to allocate
		 * as the base to RX
		 */
		if (size <= TBT_NET_RX_HDR_SIZE) {
			struct tbt_buffer *buf =
				&(rx_ring->buffers[rx_ring->next_to_clean]);
			u8 *va = page_address(buf->page) + buf->page_offset +
				 sizeof(struct tbt_frame_header);

			memcpy(__skb_put(skb, size), va,
			       ALIGN(size, sizeof(long)));

			/*
			 * Reuse buffer as-is,
			 * just make sure it is local
			 * Access to local memory is faster than non-local
			 * memory so let's reuse.
			 * If not local, let's free it and reallocate later.
			 */
			if (unlikely(page_to_nid(buf->page) !=
				     numa_node_id())) {
				/*
				 * We are not reusing the buffer so unmap it and
				 * free any references we are holding to it.
				 */
				dma_unmap_page_attrs(&port->nhi_ctxt->pdev->dev,
						     buf->dma,
						     PAGE_SIZE,
						     DMA_FROM_DEVICE,
						     TBT_RX_DMA_ATTR);

				put_page(buf->page);
				buf->page = NULL;
			}
			rx_ring->next_to_clean = (rx_ring->next_to_clean + 1) &
						 (TBT_NET_NUM_RX_BUFS - 1);
		} else {
			for (i = 0; i < frame_count;  ++i) {
				struct tbt_buffer *buf = &(rx_ring->buffers[
						rx_ring->next_to_clean]);
				struct tbt_frame_header *hdr =
						page_address(buf->page) +
						buf->page_offset;
				u32 frm_size = le32_to_cpu(hdr->frame_size);

				unsigned int truesize =
					tbt_net_max_frm_data_size(frm_size);

				/* add frame to skb struct */
				skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
						buf->page,
						sizeof(struct tbt_frame_header)
							+ buf->page_offset,
						frm_size, truesize);

#if (TBT_NUM_FRAMES_PER_PAGE > 1)
				/* move offset up to the next cache line */
				buf->page_offset += (truesize +
					sizeof(struct tbt_frame_header));

				/*
				 * we can reuse buffer if there is space
				 * available and it is local
				 */
				if (page_to_nid(buf->page) == numa_node_id()
				    && buf->page_offset <=
					PAGE_SIZE - TBT_RING_MAX_FRAME_SIZE) {
					/*
					 * bump ref count on page before
					 * it is given to the stack
					 */
					get_page(buf->page);
					/*
					 * sync the buffer for use by the
					 * device
					 */
					dma_sync_single_range_for_device(
						&port->nhi_ctxt->pdev->dev,
						buf->dma, buf->page_offset,
						TBT_RING_MAX_FRAME_SIZE,
						DMA_FROM_DEVICE);
				} else
#endif
				{
					buf->page = NULL;
					dma_unmap_page_attrs(
						&port->nhi_ctxt->pdev->dev,
						buf->dma, PAGE_SIZE,
						DMA_FROM_DEVICE,
						TBT_RX_DMA_ATTR);
				}

				rx_ring->next_to_clean =
						(rx_ring->next_to_clean + 1) &
						(TBT_NET_NUM_RX_BUFS - 1);
			}
			/*
			 * place header from the first
			 * fragment in linear portion of buffer
			 */
			tbt_net_pull_tail(skb);
		}

		/*
		 * The Thunderbolt medium doesn't have any restriction on
		 * minimum frame size, thus doesn't need any padding in
		 * transmit.
		 * The network stack accepts Runt Ethernet frames,
		 * therefore there is neither padding in receive.
		 */

		skb->protocol = eth_type_trans(skb, port->net_dev);
		napi_gro_receive(&port->napi, skb);

		++rx_packets;
		port->stats.rx_bytes += size;
		if (multicast)
			++port->stats.multicast;
		cleaned_count += frame_count;
	}

out:
	port->stats.rx_packets += rx_packets;

	if (cleaned_count)
		tbt_net_alloc_rx_buffers(&port->nhi_ctxt->pdev->dev,
					 rx_ring, cleaned_count, reg,
					 GFP_ATOMIC);

	/* If all work not completed, return budget and keep polling */
	if (rx_packets >= budget)
		return budget;

	/* Work is done so exit the polling mode and re-enable the interrupt */
	napi_complete_done(napi, rx_packets);

	spin_lock_irqsave(&port->nhi_ctxt->lock, flags);
	/* enable RX interrupt */
	RING_INT_ENABLE_RX(port->nhi_ctxt->iobase, port->local_path,
			   port->nhi_ctxt->num_paths);

	spin_unlock_irqrestore(&port->nhi_ctxt->lock, flags);

	return rx_packets;
}

static int tbt_net_open(struct net_device *net_dev)
{
	struct tbt_port *port = netdev_priv(net_dev);
	int res = 0;
	int i, j;

	/* change link state to off until path establishment finishes */
	netif_carrier_off(net_dev);

	/*
	 * if we previously succeeded to allocate msix entries,
	 * now request IRQ for them:
	 *  2=tx data port 0,
	 *  3=rx data port 0,
	 *  4=tx data port 1,
	 *  5=rx data port 1,
	 *  ...
	 *  if not, if msi is used, nhi_msi will handle icm & data paths
	 */
	if (port->nhi_ctxt->msix_entries) {
		char name[] = "tbt-net-xx-xx";

		scnprintf(name, sizeof(name), "tbt-net-rx-%02u", port->num);
		res = devm_request_irq(&port->nhi_ctxt->pdev->dev,
			port->nhi_ctxt->msix_entries[3+(port->num*2)].vector,
			tbt_net_rx_msix, 0, name, port);
		if (res) {
			netif_err(port, ifup, net_dev, "request_irq %s failed %d\n",
				  name, res);
			goto out;
		}
		name[8] = 't';
		res = devm_request_irq(&port->nhi_ctxt->pdev->dev,
			port->nhi_ctxt->msix_entries[2+(port->num*2)].vector,
			tbt_net_tx_msix, 0, name, port);
		if (res) {
			netif_err(port, ifup, net_dev, "request_irq %s failed %d\n",
				  name, res);
			goto request_irq_failure;
		}
	}
	/*
	 * Verifying that all buffer sizes are well defined.
	 * Starting with frame(s) will not tip over the
	 * page boundary
	 */
	BUILD_BUG_ON(TBT_NUM_FRAMES_PER_PAGE < 1);
	/*
	 * Just to make sure we have enough place for containing
	 * 3 max MTU packets for TX
	 */
	BUILD_BUG_ON((TBT_NET_NUM_TX_BUFS * TBT_RING_MAX_FRAME_SIZE) <
		     (TBT_NET_MTU * 3));
	/* make sure the number of TX Buffers is power of 2 */
	BUILD_BUG_ON_NOT_POWER_OF_2(TBT_NET_NUM_TX_BUFS);
	/*
	 * Just to make sure we have enough place for containing
	 * 3 max MTU packets for RX
	 */
	BUILD_BUG_ON((TBT_NET_NUM_RX_BUFS * TBT_RING_MAX_FRAME_SIZE) <
		     (TBT_NET_MTU * 3));
	/* make sure the number of RX Buffers is power of 2 */
	BUILD_BUG_ON_NOT_POWER_OF_2(TBT_NET_NUM_RX_BUFS);

	port->rx_ring.last_allocated = TBT_NET_NUM_RX_BUFS - 1;

	port->tx_ring.buffers = vzalloc(TBT_NET_NUM_TX_BUFS *
					sizeof(struct tbt_buffer));
	if (!port->tx_ring.buffers)
		goto ring_alloc_failure;
	port->rx_ring.buffers = vzalloc(TBT_NET_NUM_RX_BUFS *
					sizeof(struct tbt_buffer));
	if (!port->rx_ring.buffers)
		goto ring_alloc_failure;

	/*
	 * Allocate TX and RX descriptors
	 * if the total size is less than a page, do a central allocation
	 * Otherwise, split TX and RX
	 */
	if (TBT_NET_SIZE_TOTAL_DESCS <= PAGE_SIZE) {
		port->tx_ring.desc = dmam_alloc_coherent(
				&port->nhi_ctxt->pdev->dev,
				TBT_NET_SIZE_TOTAL_DESCS,
				&port->tx_ring.dma,
				GFP_KERNEL | __GFP_ZERO);
		if (!port->tx_ring.desc)
			goto ring_alloc_failure;
		/* RX starts where TX finishes */
		port->rx_ring.desc = &port->tx_ring.desc[TBT_NET_NUM_TX_BUFS];
		port->rx_ring.dma = port->tx_ring.dma +
			(TBT_NET_NUM_TX_BUFS * sizeof(struct tbt_buf_desc));
	} else {
		port->tx_ring.desc = dmam_alloc_coherent(
				&port->nhi_ctxt->pdev->dev,
				TBT_NET_NUM_TX_BUFS *
						sizeof(struct tbt_buf_desc),
				&port->tx_ring.dma,
				GFP_KERNEL | __GFP_ZERO);
		if (!port->tx_ring.desc)
			goto ring_alloc_failure;
		port->rx_ring.desc = dmam_alloc_coherent(
				&port->nhi_ctxt->pdev->dev,
				TBT_NET_NUM_RX_BUFS *
						sizeof(struct tbt_buf_desc),
				&port->rx_ring.dma,
				GFP_KERNEL | __GFP_ZERO);
		if (!port->rx_ring.desc)
			goto rx_desc_alloc_failure;
	}

	/* allocate TX buffers and configure the descriptors */
	for (i = 0; i < TBT_NET_NUM_TX_BUFS; i++) {
		port->tx_ring.buffers[i].hdr = dma_alloc_coherent(
			&port->nhi_ctxt->pdev->dev,
			TBT_NUM_FRAMES_PER_PAGE * TBT_RING_MAX_FRAME_SIZE,
			&port->tx_ring.buffers[i].dma,
			GFP_KERNEL);
		if (!port->tx_ring.buffers[i].hdr)
			goto buffers_alloc_failure;

		port->tx_ring.desc[i].phys =
				cpu_to_le64(port->tx_ring.buffers[i].dma);
		port->tx_ring.desc[i].attributes =
				cpu_to_le32(DESC_ATTR_REQ_STS |
					    TBT_NET_DESC_ATTR_SOF_EOF);

		/*
		 * In case the page is bigger than the frame size,
		 * make the next buffer descriptor points
		 * on the next frame memory address within the page
		 */
		for (i++, j = 1; (i < TBT_NET_NUM_TX_BUFS) &&
				 (j < TBT_NUM_FRAMES_PER_PAGE); i++, j++) {
			port->tx_ring.buffers[i].dma =
				port->tx_ring.buffers[i - 1].dma +
				TBT_RING_MAX_FRAME_SIZE;
			port->tx_ring.buffers[i].hdr =
				(void *)(port->tx_ring.buffers[i - 1].hdr) +
				TBT_RING_MAX_FRAME_SIZE;
			/* move the next offset i.e. TBT_RING_MAX_FRAME_SIZE */
			port->tx_ring.buffers[i].page_offset =
				port->tx_ring.buffers[i - 1].page_offset +
				TBT_RING_MAX_FRAME_SIZE;
			port->tx_ring.desc[i].phys =
				cpu_to_le64(port->tx_ring.buffers[i].dma);
			port->tx_ring.desc[i].attributes =
				cpu_to_le32(DESC_ATTR_REQ_STS |
					    TBT_NET_DESC_ATTR_SOF_EOF);
		}
		i--;
	}

	port->negotiation_status =
			BIT(port->nhi_ctxt->net_devices[port->num].medium_sts);
	if (port->negotiation_status == BIT(MEDIUM_READY_FOR_CONNECTION)) {
		port->login_retry_count = 0;
		queue_delayed_work(port->nhi_ctxt->net_workqueue,
				   &port->login_retry_work, 0);
	}

	netif_info(port, ifup, net_dev, "Thunderbolt(TM) Networking port %u - ready for ThunderboltIP negotiation\n",
		   port->num);
	return 0;

buffers_alloc_failure:
	/*
	 * Rollback the Tx buffers that were already allocated
	 * until the failure
	 */
	for (i--; i >= 0; i--) {
		/* free only for first buffer allocation */
		if (port->tx_ring.buffers[i].page_offset == 0)
			dma_free_coherent(&port->nhi_ctxt->pdev->dev,
					  TBT_NUM_FRAMES_PER_PAGE *
						TBT_RING_MAX_FRAME_SIZE,
					  port->tx_ring.buffers[i].hdr,
					  port->tx_ring.buffers[i].dma);
		port->tx_ring.buffers[i].hdr = NULL;
	}
	/*
	 * For central allocation, free all
	 * otherwise free RX and then TX separately
	 */
	if (TBT_NET_SIZE_TOTAL_DESCS <= PAGE_SIZE) {
		dmam_free_coherent(&port->nhi_ctxt->pdev->dev,
				   TBT_NET_SIZE_TOTAL_DESCS,
				   port->tx_ring.desc,
				   port->tx_ring.dma);
		port->rx_ring.desc = NULL;
	} else {
		dmam_free_coherent(&port->nhi_ctxt->pdev->dev,
				   TBT_NET_NUM_RX_BUFS *
						sizeof(struct tbt_buf_desc),
				   port->rx_ring.desc,
				   port->rx_ring.dma);
		port->rx_ring.desc = NULL;
rx_desc_alloc_failure:
		dmam_free_coherent(&port->nhi_ctxt->pdev->dev,
				   TBT_NET_NUM_TX_BUFS *
						sizeof(struct tbt_buf_desc),
				   port->tx_ring.desc,
				   port->tx_ring.dma);
	}
	port->tx_ring.desc = NULL;
ring_alloc_failure:
	vfree(port->tx_ring.buffers);
	port->tx_ring.buffers = NULL;
	vfree(port->rx_ring.buffers);
	port->rx_ring.buffers = NULL;
	res = -ENOMEM;
	netif_err(port, ifup, net_dev, "Thunderbolt(TM) Networking port %u - unable to allocate memory\n",
		  port->num);

	if (!port->nhi_ctxt->msix_entries)
		goto out;

	devm_free_irq(&port->nhi_ctxt->pdev->dev,
		      port->nhi_ctxt->msix_entries[2 + (port->num * 2)].vector,
		      port);
request_irq_failure:
	devm_free_irq(&port->nhi_ctxt->pdev->dev,
		      port->nhi_ctxt->msix_entries[3 + (port->num * 2)].vector,
		      port);
out:
	return res;
}

static int tbt_net_close(struct net_device *net_dev)
{
	struct tbt_port *port = netdev_priv(net_dev);
	int i;

	/*
	 * Close connection, disable rings, flow controls
	 * and interrupts
	 */
	tbt_net_tear_down(net_dev, !(port->negotiation_status &
				     BIT(RECEIVE_LOGOUT)));

	cancel_work_sync(&port->login_response_work);
	cancel_work_sync(&port->logout_work);
	cancel_work_sync(&port->status_reply_work);
	cancel_work_sync(&port->approve_inter_domain_work);

	/* Rollback the Tx buffers that were allocated */
	for (i = 0; i < TBT_NET_NUM_TX_BUFS; i++) {
		if (port->tx_ring.buffers[i].page_offset == 0)
			dma_free_coherent(&port->nhi_ctxt->pdev->dev,
					  TBT_NUM_FRAMES_PER_PAGE *
						TBT_RING_MAX_FRAME_SIZE,
					  port->tx_ring.buffers[i].hdr,
					  port->tx_ring.buffers[i].dma);
		port->tx_ring.buffers[i].hdr = NULL;
	}
	/* Unmap the Rx buffers that were allocated */
	for (i = 0; i < TBT_NET_NUM_RX_BUFS; i++) {
		struct tbt_buffer *buf = &port->rx_ring.buffers[i];

		if (!buf->page)
			continue;

		/*
		 * Invalidate cache lines that may have been written to by
		 * device so that we avoid corrupting memory.
		 */
		dma_sync_single_range_for_cpu(&port->nhi_ctxt->pdev->dev,
					      buf->dma,
					      buf->page_offset,
					      TBT_RING_MAX_FRAME_SIZE,
					      DMA_FROM_DEVICE);

		/* free resources associated with mapping */
		dma_unmap_page_attrs(&port->nhi_ctxt->pdev->dev,
				     buf->dma,
				     PAGE_SIZE,
				     DMA_FROM_DEVICE,
				     TBT_RX_DMA_ATTR);

		put_page(buf->page);

		buf->page = NULL;
	}

	/*
	 * For central allocation, free all
	 * otherwise free RX and then TX separately
	 */
	if (TBT_NET_SIZE_TOTAL_DESCS <= PAGE_SIZE) {
		dmam_free_coherent(&port->nhi_ctxt->pdev->dev,
				   TBT_NET_SIZE_TOTAL_DESCS,
				   port->tx_ring.desc,
				   port->tx_ring.dma);
		port->rx_ring.desc = NULL;
	} else {
		dmam_free_coherent(&port->nhi_ctxt->pdev->dev,
				   TBT_NET_NUM_RX_BUFS *
						sizeof(struct tbt_buf_desc),
				   port->rx_ring.desc,
				   port->rx_ring.dma);
		port->rx_ring.desc = NULL;
		dmam_free_coherent(&port->nhi_ctxt->pdev->dev,
				   TBT_NET_NUM_TX_BUFS *
						sizeof(struct tbt_buf_desc),
				   port->tx_ring.desc,
				   port->tx_ring.dma);
	}
	port->tx_ring.desc = NULL;

	vfree(port->tx_ring.buffers);
	port->tx_ring.buffers = NULL;
	vfree(port->rx_ring.buffers);
	port->rx_ring.buffers = NULL;

	if (!port->nhi_ctxt->msix_entries)
		goto out;

	devm_free_irq(&port->nhi_ctxt->pdev->dev,
		      port->nhi_ctxt->msix_entries[3 + (port->num * 2)].vector,
		      port);
	devm_free_irq(&port->nhi_ctxt->pdev->dev,
		      port->nhi_ctxt->msix_entries[2 + (port->num * 2)].vector,
		      port);

out:
	netif_info(port, ifdown, net_dev, "Thunderbolt(TM) Networking port %u - is down\n",
		   port->num);

	return 0;
}

static bool tbt_net_xmit_csum(struct sk_buff *skb,
			      struct tbt_desc_ring *tx_ring, u32 first,
			      u32 last, u32 frame_count)
{

	struct tbt_frame_header *hdr = tx_ring->buffers[first].hdr;
	__wsum wsum = (__force __wsum)htonl(skb->len -
					    skb_transport_offset(skb));
	int offset = skb_transport_offset(skb);
	__sum16 *tucso;  /* TCP UDP Checksum Segment Offset */
	__be16 protocol = skb->protocol;
	u8 *dest = (u8 *)(hdr + 1);
	int len;

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		for (; first != last;
			first = (first + 1) & (TBT_NET_NUM_TX_BUFS - 1)) {
			hdr = tx_ring->buffers[first].hdr;
			hdr->frame_count = cpu_to_le32(frame_count);
		}
		return true;
	}

	if (protocol == htons(ETH_P_8021Q)) {
		struct vlan_hdr *vhdr, vh;

		vhdr = skb_header_pointer(skb, ETH_HLEN, sizeof(vh), &vh);
		if (!vhdr)
			return false;

		protocol = vhdr->h_vlan_encapsulated_proto;
	}

	/*
	 * Data points on the beginning of packet.
	 * Check is the checksum absolute place in the
	 * packet.
	 * ipcso will update IP checksum.
	 * tucso will update TCP/UPD checksum.
	 */
	if (protocol == htons(ETH_P_IP)) {
		__sum16 *ipcso = (__sum16 *)(dest +
			((u8 *)&(ip_hdr(skb)->check) - skb->data));

		*ipcso = 0;
		*ipcso = ip_fast_csum(dest + skb_network_offset(skb),
				      ip_hdr(skb)->ihl);
		if (ip_hdr(skb)->protocol == IPPROTO_TCP)
			tucso = (__sum16 *)(dest +
				((u8 *)&(tcp_hdr(skb)->check) - skb->data));
		else if (ip_hdr(skb)->protocol == IPPROTO_UDP)
			tucso = (__sum16 *)(dest +
				((u8 *)&(udp_hdr(skb)->check) - skb->data));
		else
			return false;

		*tucso = ~csum_tcpudp_magic(ip_hdr(skb)->saddr,
					    ip_hdr(skb)->daddr, 0,
					    ip_hdr(skb)->protocol, 0);
	} else if (skb_is_gso(skb)) {
		if (skb_is_gso_v6(skb)) {
			tucso = (__sum16 *)(dest +
				((u8 *)&(tcp_hdr(skb)->check) - skb->data));
			*tucso = ~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
						  &ipv6_hdr(skb)->daddr,
						  0, IPPROTO_TCP, 0);
		} else if ((protocol == htons(ETH_P_IPV6)) &&
			   (skb_shinfo(skb)->gso_type & SKB_GSO_UDP)) {
			tucso = (__sum16 *)(dest +
				((u8 *)&(udp_hdr(skb)->check) - skb->data));
			*tucso = ~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
						  &ipv6_hdr(skb)->daddr,
						  0, IPPROTO_UDP, 0);
		} else {
			return false;
		}
	} else if (protocol == htons(ETH_P_IPV6)) {
		tucso = (__sum16 *)(dest + skb_checksum_start_offset(skb) +
				    skb->csum_offset);
		*tucso = ~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
					  &ipv6_hdr(skb)->daddr,
					  0, ipv6_hdr(skb)->nexthdr, 0);
	} else {
		return false;
	}

	/* First frame was headers, rest of the frames is data */
	for (; first != last; first = (first + 1) & (TBT_NET_NUM_TX_BUFS - 1),
								offset = 0) {
		hdr = tx_ring->buffers[first].hdr;
		dest = (u8 *)(hdr + 1) + offset;
		len = le32_to_cpu(hdr->frame_size) - offset;
		wsum = csum_partial(dest, len, wsum);
		hdr->frame_count = cpu_to_le32(frame_count);
	}
	*tucso = csum_fold(wsum);

	return true;
}

static netdev_tx_t tbt_net_xmit_frame(struct sk_buff *skb,
				      struct net_device *net_dev)
{
	struct tbt_port *port = netdev_priv(net_dev);
	void __iomem *iobase = port->nhi_ctxt->iobase;
	void __iomem *reg = TBT_RING_CONS_PROD_REG(iobase,
						   REG_TX_RING_BASE,
						   port->local_path);
	struct tbt_desc_ring *tx_ring = &port->tx_ring;
	struct tbt_frame_header *hdr;
	u32 prod_cons, prod, cons, first;
	/* len equivalent to the fragment length */
	unsigned int len = skb_headlen(skb);
	/* data_len is overall packet length */
	unsigned int data_len = skb->len;
	u32 frm_idx, frag_num = 0;
	const u8 *src = skb->data;
	bool unmap = false;
	__le32 *attr;
	u8 *dest;

	if (unlikely(data_len == 0 || data_len > TBT_NET_MTU))
		goto invalid_packet;

	prod_cons = ioread32(reg);
	prod = TBT_REG_RING_PROD_EXTRACT(prod_cons);
	cons = TBT_REG_RING_CONS_EXTRACT(prod_cons);
	if (prod >= TBT_NET_NUM_TX_BUFS || cons >= TBT_NET_NUM_TX_BUFS)
		goto tx_error;

	if (data_len > (TBT_NUM_BUFS_BETWEEN(prod, cons, TBT_NET_NUM_TX_BUFS) *
			TBT_RING_MAX_FRM_DATA_SZ)) {
		unsigned long flags;

		netif_stop_queue(net_dev);

		spin_lock_irqsave(&port->nhi_ctxt->lock, flags);
		/*
		 * Enable TX interrupt to be notified about available buffers
		 * and restart transmission upon this.
		 */
		RING_INT_ENABLE_TX(iobase, port->local_path);
		spin_unlock_irqrestore(&port->nhi_ctxt->lock, flags);

		return NETDEV_TX_BUSY;
	}

	first = prod;
	attr = &tx_ring->desc[prod].attributes;
	hdr = tx_ring->buffers[prod].hdr;
	dest = (u8 *)(hdr + 1);
	/* if overall packet is bigger than the frame data size */
	for (frm_idx = 0; data_len > TBT_RING_MAX_FRM_DATA_SZ; ++frm_idx) {
		u32 size_left = TBT_RING_MAX_FRM_DATA_SZ;

		*attr &= cpu_to_le32(~(DESC_ATTR_LEN_MASK |
				      DESC_ATTR_INT_EN |
				      DESC_ATTR_DESC_DONE));
		hdr->frame_size = cpu_to_le32(TBT_RING_MAX_FRM_DATA_SZ);
		hdr->frame_index = cpu_to_le16(frm_idx);
		hdr->frame_id = cpu_to_le16(port->frame_id);

		do {
			if (len > size_left) {
				/*
				 * Copy data onto tx buffer data with full
				 * frame size then break
				 * and go to next frame
				 */
				memcpy(dest, src, size_left);
				len -= size_left;
				dest += size_left;
				src += size_left;
				break;
			}

			memcpy(dest, src, len);
			size_left -= len;
			dest += len;

			if (unmap) {
				kunmap_atomic((void *)src);
				unmap = false;
			}
			/*
			 * Ensure all fragments have been processed
			 */
			if (frag_num < skb_shinfo(skb)->nr_frags) {
				const skb_frag_t *frag =
					&(skb_shinfo(skb)->frags[frag_num]);
				len = skb_frag_size(frag);
				/* map and then unmap quickly */
				src = kmap_atomic(skb_frag_page(frag)) +
							frag->page_offset;
				unmap = true;
				++frag_num;
			} else if (unlikely(size_left > 0)) {
				goto invalid_packet;
			}
		} while (size_left > 0);

		data_len -= TBT_RING_MAX_FRM_DATA_SZ;
		prod = (prod + 1) & (TBT_NET_NUM_TX_BUFS - 1);
		attr = &tx_ring->desc[prod].attributes;
		hdr = tx_ring->buffers[prod].hdr;
		dest = (u8 *)(hdr + 1);
	}

	*attr &= cpu_to_le32(~(DESC_ATTR_LEN_MASK | DESC_ATTR_DESC_DONE));
	/* Enable the interrupts, for resuming from stop queue later (if so) */
	*attr |= cpu_to_le32(DESC_ATTR_INT_EN |
		(((sizeof(struct tbt_frame_header) + data_len) <<
		  DESC_ATTR_LEN_SHIFT) & DESC_ATTR_LEN_MASK));
	hdr->frame_size = cpu_to_le32(data_len);
	hdr->frame_index = cpu_to_le16(frm_idx);
	hdr->frame_id = cpu_to_le16(port->frame_id);

	/* In case  the remaining data_len is smaller than a frame */
	while (len < data_len) {
		memcpy(dest, src, len);
		data_len -= len;
		dest += len;

		if (unmap) {
			kunmap_atomic((void *)src);
			unmap = false;
		}

		if (frag_num < skb_shinfo(skb)->nr_frags) {
			const skb_frag_t *frag =
					&(skb_shinfo(skb)->frags[frag_num]);
			len = skb_frag_size(frag);
			src = kmap_atomic(skb_frag_page(frag)) +
							frag->page_offset;
			unmap = true;
			++frag_num;
		} else if (unlikely(data_len > 0)) {
			goto invalid_packet;
		}
	}
	memcpy(dest, src, data_len);
	if (unmap) {
		kunmap_atomic((void *)src);
		unmap = false;
	}

	++frm_idx;
	prod = (prod + 1) & (TBT_NET_NUM_TX_BUFS - 1);

	if (!tbt_net_xmit_csum(skb, tx_ring, first, prod, frm_idx))
		goto invalid_packet;

	if (port->match_frame_id)
		++port->frame_id;

	prod_cons &= ~REG_RING_PROD_MASK;
	prod_cons |= (prod << REG_RING_PROD_SHIFT) & REG_RING_PROD_MASK;
	wmb(); /* make sure producer update is done after buffers are ready */
	iowrite32(prod_cons, reg);

	++port->stats.tx_packets;
	port->stats.tx_bytes += skb->len;

	dev_consume_skb_any(skb);
	return NETDEV_TX_OK;

invalid_packet:
	netif_err(port, tx_err, net_dev, "port %u invalid transmit packet\n",
		  port->num);
tx_error:
	++port->stats.tx_errors;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static void tbt_net_set_rx_mode(struct net_device *net_dev)
{
	struct tbt_port *port = netdev_priv(net_dev);
	struct netdev_hw_addr *ha;

	if (net_dev->flags & IFF_PROMISC)
		port->packet_filters |= BIT(PACKET_TYPE_PROMISCUOUS);
	else
		port->packet_filters &= ~BIT(PACKET_TYPE_PROMISCUOUS);
	if (net_dev->flags & IFF_ALLMULTI)
		port->packet_filters |= BIT(PACKET_TYPE_ALL_MULTICAST);
	else
		port->packet_filters &= ~BIT(PACKET_TYPE_ALL_MULTICAST);

	/* if you have more than a single MAC address */
	if (netdev_uc_count(net_dev) > 1)
		port->packet_filters |= BIT(PACKET_TYPE_UNICAST_PROMISCUOUS);
	/* if have a single MAC address */
	else if (netdev_uc_count(net_dev) == 1) {
		netdev_for_each_uc_addr(ha, net_dev)
			/* checks whether the MAC is what we set */
			if (ether_addr_equal(ha->addr, net_dev->dev_addr))
				port->packet_filters &=
					~BIT(PACKET_TYPE_UNICAST_PROMISCUOUS);
			else
				port->packet_filters |=
					BIT(PACKET_TYPE_UNICAST_PROMISCUOUS);
	} else {
		port->packet_filters &= ~BIT(PACKET_TYPE_UNICAST_PROMISCUOUS);
	}

	/* Populate the multicast hash table with received MAC addresses */
	memset(port->multicast_hash_table, 0,
	       sizeof(port->multicast_hash_table));
	netdev_for_each_mc_addr(ha, net_dev) {
		u16 hash_val = TBT_NET_ETHER_ADDR_HASH(ha->addr);

		port->multicast_hash_table[hash_val / BITS_PER_U32] |=
						BIT(hash_val % BITS_PER_U32);
	}

}

static void tbt_net_get_stats64(struct net_device *net_dev,
				struct rtnl_link_stats64 *stats)
{
	struct tbt_port *port = netdev_priv(net_dev);

	stats->tx_packets = port->stats.tx_packets;
	stats->tx_bytes = port->stats.tx_bytes;
	stats->tx_errors = port->stats.tx_errors;
	stats->rx_packets = port->stats.rx_packets;
	stats->rx_bytes = port->stats.rx_bytes;
	stats->rx_length_errors = port->stats.rx_length_errors;
	stats->rx_over_errors = port->stats.rx_over_errors;
	stats->rx_crc_errors = port->stats.rx_crc_errors;
	stats->rx_missed_errors = port->stats.rx_missed_errors;
	stats->rx_errors = stats->rx_length_errors + stats->rx_over_errors +
			   stats->rx_crc_errors + stats->rx_missed_errors;
	stats->multicast = port->stats.multicast;
}

static int tbt_net_set_mac_address(struct net_device *net_dev, void *addr)
{
	struct sockaddr *saddr = addr;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(net_dev->dev_addr, saddr->sa_data, net_dev->addr_len);

	return 0;
}

static const struct net_device_ops tbt_netdev_ops = {
	/* called when the network is up'ed */
	.ndo_open		= tbt_net_open,
	/* called when the network is down'ed */
	.ndo_stop		= tbt_net_close,
	.ndo_start_xmit		= tbt_net_xmit_frame,
	.ndo_set_rx_mode	= tbt_net_set_rx_mode,
	.ndo_get_stats64	= tbt_net_get_stats64,
	.ndo_set_mac_address	= tbt_net_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
};

static int tbt_net_get_link_ksettings(__maybe_unused struct net_device *net_dev,
				      struct ethtool_link_ksettings *cmd)
{
	cmd->base.autoneg = AUTONEG_DISABLE;
	cmd->base.port = PORT_FIBRE;
	cmd->base.speed = SPEED_20000;
	cmd->base.duplex = DUPLEX_FULL;
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						SUPPORTED_20000baseKR2_Full |
						SUPPORTED_FIBRE);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						ADVERTISED_20000baseKR2_Full |
						ADVERTISED_FIBRE);

	return 0;
}


static u32 tbt_net_get_msglevel(struct net_device *net_dev)
{
	struct tbt_port *port = netdev_priv(net_dev);

	return port->msg_enable;
}

static void tbt_net_set_msglevel(struct net_device *net_dev, u32 data)
{
	struct tbt_port *port = netdev_priv(net_dev);

	port->msg_enable = data;
}

static void tbt_net_get_strings(__maybe_unused struct net_device *net_dev,
				u32 stringset, u8 *data)
{
	if (stringset == ETH_SS_STATS)
		memcpy(data, tbt_net_gstrings_stats,
		       sizeof(tbt_net_gstrings_stats));
}

static void tbt_net_get_ethtool_stats(struct net_device *net_dev,
				      __maybe_unused struct ethtool_stats *sts,
				      u64 *data)
{
	struct tbt_port *port = netdev_priv(net_dev);

	memcpy(data, &port->stats, sizeof(port->stats));
}

static int tbt_net_get_sset_count(__maybe_unused struct net_device *net_dev,
				  int sset)
{
	if (sset == ETH_SS_STATS)
		return sizeof(tbt_net_gstrings_stats) / ETH_GSTRING_LEN;
	return -EOPNOTSUPP;
}

static void tbt_net_get_drvinfo(struct net_device *net_dev,
				struct ethtool_drvinfo *drvinfo)
{
	struct tbt_port *port = netdev_priv(net_dev);

	strlcpy(drvinfo->driver, "Thunderbolt(TM) Networking",
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, DRV_VERSION, sizeof(drvinfo->version));

	strlcpy(drvinfo->bus_info, pci_name(port->nhi_ctxt->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->n_stats = tbt_net_get_sset_count(net_dev, ETH_SS_STATS);
}

static const struct ethtool_ops tbt_net_ethtool_ops = {
	.get_drvinfo		= tbt_net_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_msglevel		= tbt_net_get_msglevel,
	.set_msglevel		= tbt_net_set_msglevel,
	.get_strings		= tbt_net_get_strings,
	.get_ethtool_stats	= tbt_net_get_ethtool_stats,
	.get_sset_count		= tbt_net_get_sset_count,
	.get_link_ksettings	= tbt_net_get_link_ksettings,
};

static inline int send_message(struct tbt_port *port, const char *func,
				enum pdf_value pdf, u32 msg_len,
				const void *msg)
{
	u32 crc_offset = msg_len - sizeof(__be32);
	__be32 *crc = (__be32 *)((u8 *)msg + crc_offset);
	bool is_intdom = (pdf == PDF_INTER_DOMAIN_RESPONSE);
	int res;

	*crc = cpu_to_be32(~__crc32c_le(~0, msg, crc_offset));
	res = down_timeout(&port->nhi_ctxt->send_sem,
			   msecs_to_jiffies(3 * MSEC_PER_SEC));
	if (res) {
		netif_err(port, link, port->net_dev, "%s: controller id %#x timeout on send semaphore\n",
			  func, port->nhi_ctxt->id);
		return res;
	}

	if (!mutex_trylock(&port->nhi_ctxt->d0_exit_send_mutex)) {
		up(&port->nhi_ctxt->send_sem);
		netif_notice(port, link, port->net_dev, "%s: controller id %#x is existing D0\n",
			     func, port->nhi_ctxt->id);
		return -ENODEV;
	}

	res = nhi_send_message(port->nhi_ctxt, pdf, msg_len, msg, is_intdom);

	mutex_unlock(&port->nhi_ctxt->d0_exit_send_mutex);
	if (res)
		up(&port->nhi_ctxt->send_sem);

	return res;
}

static void approve_inter_domain(struct work_struct *work)
{
	struct tbt_port *port = container_of(work, typeof(*port),
					     approve_inter_domain_work);
	struct approve_inter_domain_connection_cmd approve_msg = {
		.req_code = cpu_to_be32(CC_APPROVE_INTER_DOMAIN_CONNECTION),
		.transmit_path = cpu_to_be16(LOGIN_TX_PATH),
	};
	u32 aidc = (L0_PORT_NUM(port->route_str.lo) << AIDC_ATTR_LINK_SHIFT) &
		    AIDC_ATTR_LINK_MASK;

	aidc |= (port->local_depth << AIDC_ATTR_DEPTH_SHIFT) &
		 AIDC_ATTR_DEPTH_MASK;

	approve_msg.attributes = cpu_to_be32(aidc);

	memcpy(&approve_msg.remote_uuid, &port->interdomain_remote_uuid,
	       sizeof(approve_msg.remote_uuid));
	approve_msg.transmit_ring_number = cpu_to_be16(port->local_path);
	approve_msg.receive_ring_number = cpu_to_be16(port->local_path);
	approve_msg.receive_path = cpu_to_be16(port->transmit_path);

	send_message(port, __func__, PDF_SW_TO_FW_COMMAND, sizeof(approve_msg),
		     &approve_msg);
}

static inline void prepare_header(struct thunderbolt_ip_header *header,
				  struct tbt_port *port,
				  enum thunderbolt_ip_packet_type packet_type,
				  u8 len_dwords)
{
	const uuid_be proto_uuid = APPLE_THUNDERBOLT_IP_PROTOCOL_UUID;

	header->packet_type = cpu_to_be32(packet_type);
	header->route_str.hi = cpu_to_be32(port->route_str.hi);
	header->route_str.lo = cpu_to_be32(port->route_str.lo);
	header->attributes = cpu_to_be32(
		((port->seq_num << HDR_ATTR_SEQ_NUM_SHIFT) &
		 HDR_ATTR_SEQ_NUM_MASK) |
		((len_dwords << HDR_ATTR_LEN_SHIFT) & HDR_ATTR_LEN_MASK));
	memcpy(&header->apple_tbt_ip_proto_uuid, &proto_uuid,
	       sizeof(header->apple_tbt_ip_proto_uuid));
	memcpy(&header->initiator_uuid, &port->interdomain_local_uuid,
	       sizeof(header->initiator_uuid));
	memcpy(&header->target_uuid, &port->interdomain_remote_uuid,
	       sizeof(header->target_uuid));
	header->command_id = cpu_to_be32(port->command_id);

	port->command_id++;
}

static void status_reply(struct work_struct *work)
{
	struct tbt_port *port = container_of(work, typeof(*port),
					     status_reply_work);
	struct thunderbolt_ip_status status_msg = {
		.status = 0,
	};

	prepare_header(&status_msg.header, port,
		       THUNDERBOLT_IP_STATUS_TYPE,
		       (offsetof(struct thunderbolt_ip_status, crc) -
			offsetof(struct thunderbolt_ip_status,
				 header.apple_tbt_ip_proto_uuid)) /
		       sizeof(u32));

	send_message(port, __func__, PDF_INTER_DOMAIN_RESPONSE,
		     sizeof(status_msg), &status_msg);

}

static void logout(struct work_struct *work)
{
	struct tbt_port *port = container_of(work, typeof(*port),
					     logout_work);
	struct thunderbolt_ip_logout logout_msg;

	prepare_header(&logout_msg.header, port,
		       THUNDERBOLT_IP_LOGOUT_TYPE,
		       (offsetof(struct thunderbolt_ip_logout, crc) -
			offsetof(struct thunderbolt_ip_logout,
			       header.apple_tbt_ip_proto_uuid)) / sizeof(u32));

	send_message(port, __func__, PDF_INTER_DOMAIN_RESPONSE,
		     sizeof(logout_msg), &logout_msg);

}

static void login_response(struct work_struct *work)
{
	struct tbt_port *port = container_of(work, typeof(*port),
					     login_response_work);
	struct thunderbolt_ip_login_response login_res_msg = {
		.receiver_mac_address_length = cpu_to_be32(ETH_ALEN),
	};

	prepare_header(&login_res_msg.header, port,
		       THUNDERBOLT_IP_LOGIN_RESPONSE_TYPE,
		       (offsetof(struct thunderbolt_ip_login_response, crc) -
			offsetof(struct thunderbolt_ip_login_response,
			       header.apple_tbt_ip_proto_uuid)) / sizeof(u32));

	ether_addr_copy((u8 *)login_res_msg.receiver_mac_address,
			port->net_dev->dev_addr);

	send_message(port, __func__, PDF_INTER_DOMAIN_RESPONSE,
		     sizeof(login_res_msg), &login_res_msg);

}

static void login_retry(struct work_struct *work)
{
	struct tbt_port *port = container_of(work, typeof(*port),
					     login_retry_work.work);
	struct thunderbolt_ip_login login_msg = {
		.protocol_revision = cpu_to_be32(
				APPLE_THUNDERBOLT_IP_PROTOCOL_REVISION),
		.transmit_path = cpu_to_be32(LOGIN_TX_PATH),
	};


	if (port->nhi_ctxt->d0_exit)
		return;

	port->login_retry_count++;

	prepare_header(&login_msg.header, port,
		       THUNDERBOLT_IP_LOGIN_TYPE,
		       (offsetof(struct thunderbolt_ip_login, crc) -
		       offsetof(struct thunderbolt_ip_login,
		       header.apple_tbt_ip_proto_uuid)) / sizeof(u32));

	if (send_message(port, __func__, PDF_INTER_DOMAIN_RESPONSE,
			 sizeof(login_msg), &login_msg) == -ENODEV)
		return;

	if (likely(port->login_retry_count < NUM_TX_LOGIN_RETRIES))
		queue_delayed_work(port->nhi_ctxt->net_workqueue,
				   &port->login_retry_work,
				   msecs_to_jiffies(5 * MSEC_PER_SEC));
	else
		netif_notice(port, link, port->net_dev, "port %u (%#x) login timeout after %u retries\n",
			     port->num, port->negotiation_status,
			     port->login_retry_count);
}

void negotiation_events(struct net_device *net_dev,
			enum medium_status medium_sts)
{
	struct tbt_port *port = netdev_priv(net_dev);
	void __iomem *iobase = port->nhi_ctxt->iobase;
	u32 sof_eof_en, tx_ring_conf, rx_ring_conf, e2e_en;
	void __iomem *reg;
	unsigned long flags;
	u16 hop_id;
	bool send_logout;

	if (!netif_running(net_dev)) {
		netif_dbg(port, link, net_dev, "port %u (%#x) is down\n",
			  port->num, port->negotiation_status);
		return;
	}

	netif_dbg(port, link, net_dev, "port %u (%#x) receive event %u\n",
		  port->num, port->negotiation_status, medium_sts);

	switch (medium_sts) {
	case MEDIUM_DISCONNECTED:
		send_logout = (port->negotiation_status
				& (BIT(MEDIUM_CONNECTED)
				   |  BIT(MEDIUM_READY_FOR_CONNECTION)));
		send_logout = send_logout && !(port->negotiation_status &
					       BIT(RECEIVE_LOGOUT));

		tbt_net_tear_down(net_dev, send_logout);
		port->negotiation_status = BIT(MEDIUM_DISCONNECTED);
		break;

	case MEDIUM_CONNECTED:
		/*
		 * check if meanwhile other side sent logout
		 * if yes, just don't allow connection to take place
		 * and disconnect path
		 */
		if (port->negotiation_status & BIT(RECEIVE_LOGOUT)) {
			disconnect_path(port, STAGE_1 | STAGE_2);
			break;
		}

		port->negotiation_status = BIT(MEDIUM_CONNECTED);

		/* configure TX ring */
		reg = iobase + REG_TX_RING_BASE +
		      (port->local_path * REG_RING_STEP);
		iowrite32(lower_32_bits(port->tx_ring.dma),
			  reg + REG_RING_PHYS_LO_OFFSET);
		iowrite32(upper_32_bits(port->tx_ring.dma),
			  reg + REG_RING_PHYS_HI_OFFSET);

		tx_ring_conf = (TBT_NET_NUM_TX_BUFS << REG_RING_SIZE_SHIFT) &
				REG_RING_SIZE_MASK;

		iowrite32(tx_ring_conf, reg + REG_RING_SIZE_OFFSET);

		/* enable the rings */
		reg = iobase + REG_TX_OPTIONS_BASE +
		      (port->local_path * REG_OPTS_STEP);
		if (port->enable_full_e2e) {
			iowrite32(REG_OPTS_VALID | REG_OPTS_E2E_EN, reg);
			hop_id = port->local_path;
		} else {
			iowrite32(REG_OPTS_VALID, reg);
			hop_id = TBT_EXIST_BUT_UNUSED_HOPID;
		}

		reg = iobase + REG_RX_OPTIONS_BASE +
		      (port->local_path * REG_OPTS_STEP);

		sof_eof_en = (BIT(PDF_TBT_NET_START_OF_FRAME) <<
			      REG_RX_OPTS_MASK_SOF_SHIFT) &
			     REG_RX_OPTS_MASK_SOF_MASK;

		sof_eof_en |= (BIT(PDF_TBT_NET_END_OF_FRAME) <<
			       REG_RX_OPTS_MASK_EOF_SHIFT) &
			      REG_RX_OPTS_MASK_EOF_MASK;

		iowrite32(sof_eof_en, reg + REG_RX_OPTS_MASK_OFFSET);

		e2e_en = REG_OPTS_VALID | REG_OPTS_E2E_EN;
		e2e_en |= (hop_id << REG_RX_OPTS_TX_E2E_HOP_ID_SHIFT) &
			  REG_RX_OPTS_TX_E2E_HOP_ID_MASK;

		iowrite32(e2e_en, reg);

		/*
		 * Configure RX ring
		 * must be after enable ring for E2E to work
		 */
		reg = iobase + REG_RX_RING_BASE +
		      (port->local_path * REG_RING_STEP);
		iowrite32(lower_32_bits(port->rx_ring.dma),
			  reg + REG_RING_PHYS_LO_OFFSET);
		iowrite32(upper_32_bits(port->rx_ring.dma),
			  reg + REG_RING_PHYS_HI_OFFSET);

		rx_ring_conf = (TBT_NET_NUM_RX_BUFS << REG_RING_SIZE_SHIFT) &
				REG_RING_SIZE_MASK;

		rx_ring_conf |= (TBT_RING_MAX_FRAME_SIZE <<
				 REG_RING_BUF_SIZE_SHIFT) &
				REG_RING_BUF_SIZE_MASK;

		iowrite32(rx_ring_conf, reg + REG_RING_SIZE_OFFSET);
		/* allocate RX buffers and configure the descriptors */
		if (!tbt_net_alloc_rx_buffers(&port->nhi_ctxt->pdev->dev,
					      &port->rx_ring,
					      TBT_NET_NUM_RX_BUFS,
					      reg + REG_RING_CONS_PROD_OFFSET,
					      GFP_KERNEL)) {
			netif_err(port, link, net_dev, "Thunderbolt(TM) Networking port %u - no memory for receive buffers\n",
				  port->num);
			tbt_net_tear_down(net_dev, true);
			break;
		}

		spin_lock_irqsave(&port->nhi_ctxt->lock, flags);
		/* enable RX interrupt */
		iowrite32(ioread32(iobase + REG_RING_INTERRUPT_BASE) |
			  REG_RING_INT_RX_PROCESSED(port->local_path,
						    port->nhi_ctxt->num_paths),
			  iobase + REG_RING_INTERRUPT_BASE);
		spin_unlock_irqrestore(&port->nhi_ctxt->lock, flags);

		netif_info(port, link, net_dev, "Thunderbolt(TM) Networking port %u - ready\n",
			   port->num);

		napi_enable(&port->napi);
		netif_carrier_on(net_dev);
		netif_start_queue(net_dev);
		break;

	case MEDIUM_READY_FOR_CONNECTION:
		/*
		 * If medium is connected, no reason to go back,
		 * keep it 'connected'.
		 * If received login response, don't need to trigger login
		 * retries again.
		 */
		if (unlikely(port->negotiation_status &
			     (BIT(MEDIUM_CONNECTED) |
			      BIT(RECEIVE_LOGIN_RESPONSE))))
			break;

		port->negotiation_status = BIT(MEDIUM_READY_FOR_CONNECTION);
		port->login_retry_count = 0;
		queue_delayed_work(port->nhi_ctxt->net_workqueue,
				   &port->login_retry_work, 0);
		break;

	default:
		break;
	}
}

void negotiation_messages(struct net_device *net_dev,
			  struct thunderbolt_ip_header *hdr)
{
	struct tbt_port *port = netdev_priv(net_dev);
	__be32 status;

	if (!netif_running(net_dev)) {
		netif_dbg(port, link, net_dev, "port %u (%#x) is down\n",
			  port->num, port->negotiation_status);
		return;
	}

	switch (hdr->packet_type) {
	case cpu_to_be32(THUNDERBOLT_IP_LOGIN_TYPE):
		port->transmit_path = be32_to_cpu(
			((struct thunderbolt_ip_login *)hdr)->transmit_path);
		netif_dbg(port, link, net_dev, "port %u (%#x) receive ThunderboltIP login message with transmit path %u\n",
			  port->num, port->negotiation_status,
			  port->transmit_path);

		if (unlikely(port->negotiation_status &
			     BIT(MEDIUM_DISCONNECTED)))
			break;

		queue_work(port->nhi_ctxt->net_workqueue,
			   &port->login_response_work);

		if (unlikely(port->negotiation_status & BIT(MEDIUM_CONNECTED)))
			break;

		/*
		 *  In case a login response received from other peer
		 * on my login and acked their login for the first time,
		 * so just approve the inter-domain now
		 */
		if (port->negotiation_status & BIT(RECEIVE_LOGIN_RESPONSE)) {
			if (!(port->negotiation_status & BIT(RECEIVE_LOGIN)))
				queue_work(port->nhi_ctxt->net_workqueue,
					   &port->approve_inter_domain_work);
		/*
		 * if we reached the number of max retries or previous
		 * logout, schedule another round of login retries
		 */
		} else if ((port->login_retry_count >= NUM_TX_LOGIN_RETRIES) ||
			   (port->negotiation_status & BIT(RECEIVE_LOGOUT))) {
			port->negotiation_status &= ~(BIT(RECEIVE_LOGOUT));
			port->login_retry_count = 0;
			queue_delayed_work(port->nhi_ctxt->net_workqueue,
					   &port->login_retry_work, 0);
		}

		port->negotiation_status |= BIT(RECEIVE_LOGIN);

		break;

	case cpu_to_be32(THUNDERBOLT_IP_LOGIN_RESPONSE_TYPE):
		status = ((struct thunderbolt_ip_login_response *)hdr)->status;
		if (likely(status == 0)) {
			netif_dbg(port, link, net_dev, "port %u (%#x) receive ThunderboltIP login response message\n",
				  port->num,
				  port->negotiation_status);

			if (unlikely(port->negotiation_status &
				     (BIT(MEDIUM_DISCONNECTED) |
				      BIT(MEDIUM_CONNECTED) |
				      BIT(RECEIVE_LOGIN_RESPONSE))))
				break;

			port->negotiation_status |=
						BIT(RECEIVE_LOGIN_RESPONSE);
			cancel_delayed_work_sync(&port->login_retry_work);
			/*
			 * login was received from other peer and now response
			 * on our login so approve the inter-domain
			 */
			if (port->negotiation_status & BIT(RECEIVE_LOGIN))
				queue_work(port->nhi_ctxt->net_workqueue,
					   &port->approve_inter_domain_work);
			else
				port->negotiation_status &=
							~BIT(RECEIVE_LOGOUT);
		} else {
			netif_notice(port, link, net_dev, "port %u (%#x) receive ThunderboltIP login response message with status %u\n",
				     port->num,
				     port->negotiation_status,
				     be32_to_cpu(status));
		}
		break;

	case cpu_to_be32(THUNDERBOLT_IP_LOGOUT_TYPE):
		netif_dbg(port, link, net_dev, "port %u (%#x) receive ThunderboltIP logout message\n",
			  port->num, port->negotiation_status);

		queue_work(port->nhi_ctxt->net_workqueue,
			   &port->status_reply_work);
		port->negotiation_status &= ~(BIT(RECEIVE_LOGIN) |
					      BIT(RECEIVE_LOGIN_RESPONSE));
		port->negotiation_status |= BIT(RECEIVE_LOGOUT);

		if (!(port->negotiation_status & BIT(MEDIUM_CONNECTED))) {
			tbt_net_tear_down(net_dev, false);
			break;
		}

		tbt_net_tear_down(net_dev, true);

		port->negotiation_status |= BIT(MEDIUM_READY_FOR_CONNECTION);
		port->negotiation_status &= ~(BIT(MEDIUM_CONNECTED));
		break;

	case cpu_to_be32(THUNDERBOLT_IP_STATUS_TYPE):
		netif_dbg(port, link, net_dev, "port %u (%#x) receive ThunderboltIP status message with status %u\n",
			  port->num, port->negotiation_status,
			  be32_to_cpu(
			  ((struct thunderbolt_ip_status *)hdr)->status));
		break;
	}
}

void nhi_dealloc_etherdev(struct net_device *net_dev)
{
	unregister_netdev(net_dev);
	free_netdev(net_dev);
}

void nhi_update_etherdev(struct tbt_nhi_ctxt *nhi_ctxt,
			 struct net_device *net_dev, struct genl_info *info)
{
	struct tbt_port *port = netdev_priv(net_dev);

	nla_memcpy(&(port->route_str),
		   info->attrs[NHI_ATTR_LOCAL_ROUTE_STRING],
		   sizeof(port->route_str));
	nla_memcpy(&port->interdomain_remote_uuid,
		   info->attrs[NHI_ATTR_REMOTE_UUID],
		   sizeof(port->interdomain_remote_uuid));
	port->local_depth = nla_get_u8(info->attrs[NHI_ATTR_LOCAL_DEPTH]);
	port->enable_full_e2e = nhi_ctxt->support_full_e2e ?
		nla_get_flag(info->attrs[NHI_ATTR_ENABLE_FULL_E2E]) : false;
	port->match_frame_id =
		nla_get_flag(info->attrs[NHI_ATTR_MATCH_FRAME_ID]);
	port->frame_id = 0;
}

struct net_device *nhi_alloc_etherdev(struct tbt_nhi_ctxt *nhi_ctxt,
				      u8 port_num, struct genl_info *info)
{
	struct tbt_port *port;
	struct net_device *net_dev = alloc_etherdev(sizeof(struct tbt_port));
	u32 hash;

	if (!net_dev)
		return NULL;

	SET_NETDEV_DEV(net_dev, &nhi_ctxt->pdev->dev);

	port = netdev_priv(net_dev);
	port->nhi_ctxt = nhi_ctxt;
	port->net_dev = net_dev;
	nla_memcpy(&port->interdomain_local_uuid,
		   info->attrs[NHI_ATTR_LOCAL_UUID],
		   sizeof(port->interdomain_local_uuid));
	nhi_update_etherdev(nhi_ctxt, net_dev, info);
	port->num = port_num;
	port->local_path = PATH_FROM_PORT(nhi_ctxt->num_paths, port_num);

	port->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);

	net_dev->addr_assign_type = NET_ADDR_PERM;
	/* unicast and locally administred MAC */
	net_dev->dev_addr[0] = (port_num << 4) | 0x02;
	hash = jhash2((u32 *)&port->interdomain_local_uuid,
		      sizeof(port->interdomain_local_uuid)/sizeof(u32), 0);

	memcpy(net_dev->dev_addr + 1, &hash, sizeof(hash));
	hash = jhash2((u32 *)&port->interdomain_local_uuid,
		      sizeof(port->interdomain_local_uuid)/sizeof(u32), hash);

	net_dev->dev_addr[5] = hash & 0xff;

	scnprintf(net_dev->name, sizeof(net_dev->name), "tbtnet%%dp%hhu",
		  port_num);

	net_dev->netdev_ops = &tbt_netdev_ops;

	netif_napi_add(net_dev, &port->napi, tbt_net_poll, NAPI_POLL_WEIGHT);

	net_dev->hw_features = NETIF_F_SG |
			       NETIF_F_ALL_TSO |
			       NETIF_F_UFO |
			       NETIF_F_GRO |
			       NETIF_F_IP_CSUM |
			       NETIF_F_IPV6_CSUM;
	net_dev->features = net_dev->hw_features;
	if (nhi_ctxt->pci_using_dac)
		net_dev->features |= NETIF_F_HIGHDMA;

	INIT_DELAYED_WORK(&port->login_retry_work, login_retry);
	INIT_WORK(&port->login_response_work, login_response);
	INIT_WORK(&port->logout_work, logout);
	INIT_WORK(&port->status_reply_work, status_reply);
	INIT_WORK(&port->approve_inter_domain_work, approve_inter_domain);

	net_dev->ethtool_ops = &tbt_net_ethtool_ops;

	/* MTU range: 68 - 65522 */
	net_dev->min_mtu = ETH_MIN_MTU;
	net_dev->max_mtu = TBT_NET_MTU - ETH_HLEN;

	if (register_netdev(net_dev))
		goto err_register;

	netif_carrier_off(net_dev);

	netif_info(port, probe, net_dev,
		   "Thunderbolt(TM) Networking port %u - MAC Address: %pM\n",
		   port_num, net_dev->dev_addr);

	return net_dev;

err_register:
	free_netdev(net_dev);
	return NULL;
}
