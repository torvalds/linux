/*
 * Networking over Thunderbolt cable using Apple ThunderboltIP protocol
 *
 * Copyright (C) 2017, Intel Corporation
 * Authors: Amir Levy <amir.jer.levy@intel.com>
 *          Michael Jamet <michael.jamet@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/atomic.h>
#include <linux/highmem.h>
#include <linux/if_vlan.h>
#include <linux/jhash.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/sizes.h>
#include <linux/thunderbolt.h>
#include <linux/uuid.h>
#include <linux/workqueue.h>

#include <net/ip6_checksum.h>

/* Protocol timeouts in ms */
#define TBNET_LOGIN_DELAY	4500
#define TBNET_LOGIN_TIMEOUT	500
#define TBNET_LOGOUT_TIMEOUT	100

#define TBNET_RING_SIZE		256
#define TBNET_LOCAL_PATH	0xf
#define TBNET_LOGIN_RETRIES	60
#define TBNET_LOGOUT_RETRIES	5
#define TBNET_MATCH_FRAGS_ID	BIT(1)
#define TBNET_MAX_MTU		SZ_64K
#define TBNET_FRAME_SIZE	SZ_4K
#define TBNET_MAX_PAYLOAD_SIZE	\
	(TBNET_FRAME_SIZE - sizeof(struct thunderbolt_ip_frame_header))
/* Rx packets need to hold space for skb_shared_info */
#define TBNET_RX_MAX_SIZE	\
	(TBNET_FRAME_SIZE + SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
#define TBNET_RX_PAGE_ORDER	get_order(TBNET_RX_MAX_SIZE)
#define TBNET_RX_PAGE_SIZE	(PAGE_SIZE << TBNET_RX_PAGE_ORDER)

#define TBNET_L0_PORT_NUM(route) ((route) & GENMASK(5, 0))

/**
 * struct thunderbolt_ip_frame_header - Header for each Thunderbolt frame
 * @frame_size: size of the data with the frame
 * @frame_index: running index on the frames
 * @frame_id: ID of the frame to match frames to specific packet
 * @frame_count: how many frames assembles a full packet
 *
 * Each data frame passed to the high-speed DMA ring has this header. If
 * the XDomain network directory announces that %TBNET_MATCH_FRAGS_ID is
 * supported then @frame_id is filled, otherwise it stays %0.
 */
struct thunderbolt_ip_frame_header {
	u32 frame_size;
	u16 frame_index;
	u16 frame_id;
	u32 frame_count;
};

enum thunderbolt_ip_frame_pdf {
	TBIP_PDF_FRAME_START = 1,
	TBIP_PDF_FRAME_END,
};

enum thunderbolt_ip_type {
	TBIP_LOGIN,
	TBIP_LOGIN_RESPONSE,
	TBIP_LOGOUT,
	TBIP_STATUS,
};

struct thunderbolt_ip_header {
	u32 route_hi;
	u32 route_lo;
	u32 length_sn;
	uuid_t uuid;
	uuid_t initiator_uuid;
	uuid_t target_uuid;
	u32 type;
	u32 command_id;
};

#define TBIP_HDR_LENGTH_MASK		GENMASK(5, 0)
#define TBIP_HDR_SN_MASK		GENMASK(28, 27)
#define TBIP_HDR_SN_SHIFT		27

struct thunderbolt_ip_login {
	struct thunderbolt_ip_header hdr;
	u32 proto_version;
	u32 transmit_path;
	u32 reserved[4];
};

#define TBIP_LOGIN_PROTO_VERSION	1

struct thunderbolt_ip_login_response {
	struct thunderbolt_ip_header hdr;
	u32 status;
	u32 receiver_mac[2];
	u32 receiver_mac_len;
	u32 reserved[4];
};

struct thunderbolt_ip_logout {
	struct thunderbolt_ip_header hdr;
};

struct thunderbolt_ip_status {
	struct thunderbolt_ip_header hdr;
	u32 status;
};

struct tbnet_stats {
	u64 tx_packets;
	u64 rx_packets;
	u64 tx_bytes;
	u64 rx_bytes;
	u64 rx_errors;
	u64 tx_errors;
	u64 rx_length_errors;
	u64 rx_over_errors;
	u64 rx_crc_errors;
	u64 rx_missed_errors;
};

struct tbnet_frame {
	struct net_device *dev;
	struct page *page;
	struct ring_frame frame;
};

struct tbnet_ring {
	struct tbnet_frame frames[TBNET_RING_SIZE];
	unsigned int cons;
	unsigned int prod;
	struct tb_ring *ring;
};

/**
 * struct tbnet - ThunderboltIP network driver private data
 * @svc: XDomain service the driver is bound to
 * @xd: XDomain the service blongs to
 * @handler: ThunderboltIP configuration protocol handler
 * @dev: Networking device
 * @napi: NAPI structure for Rx polling
 * @stats: Network statistics
 * @skb: Network packet that is currently processed on Rx path
 * @command_id: ID used for next configuration protocol packet
 * @login_sent: ThunderboltIP login message successfully sent
 * @login_received: ThunderboltIP login message received from the remote
 *		    host
 * @transmit_path: HopID the other end needs to use building the
 *		   opposite side path.
 * @connection_lock: Lock serializing access to @login_sent,
 *		     @login_received and @transmit_path.
 * @login_retries: Number of login retries currently done
 * @login_work: Worker to send ThunderboltIP login packets
 * @connected_work: Worker that finalizes the ThunderboltIP connection
 *		    setup and enables DMA paths for high speed data
 *		    transfers
 * @rx_hdr: Copy of the currently processed Rx frame. Used when a
 *	    network packet consists of multiple Thunderbolt frames.
 *	    In host byte order.
 * @rx_ring: Software ring holding Rx frames
 * @frame_id: Frame ID use for next Tx packet
 *            (if %TBNET_MATCH_FRAGS_ID is supported in both ends)
 * @tx_ring: Software ring holding Tx frames
 */
struct tbnet {
	const struct tb_service *svc;
	struct tb_xdomain *xd;
	struct tb_protocol_handler handler;
	struct net_device *dev;
	struct napi_struct napi;
	struct tbnet_stats stats;
	struct sk_buff *skb;
	atomic_t command_id;
	bool login_sent;
	bool login_received;
	u32 transmit_path;
	struct mutex connection_lock;
	int login_retries;
	struct delayed_work login_work;
	struct work_struct connected_work;
	struct thunderbolt_ip_frame_header rx_hdr;
	struct tbnet_ring rx_ring;
	atomic_t frame_id;
	struct tbnet_ring tx_ring;
};

/* Network property directory UUID: c66189ca-1cce-4195-bdb8-49592e5f5a4f */
static const uuid_t tbnet_dir_uuid =
	UUID_INIT(0xc66189ca, 0x1cce, 0x4195,
		  0xbd, 0xb8, 0x49, 0x59, 0x2e, 0x5f, 0x5a, 0x4f);

/* ThunderboltIP protocol UUID: 798f589e-3616-8a47-97c6-5664a920c8dd */
static const uuid_t tbnet_svc_uuid =
	UUID_INIT(0x798f589e, 0x3616, 0x8a47,
		  0x97, 0xc6, 0x56, 0x64, 0xa9, 0x20, 0xc8, 0xdd);

static struct tb_property_dir *tbnet_dir;

static void tbnet_fill_header(struct thunderbolt_ip_header *hdr, u64 route,
	u8 sequence, const uuid_t *initiator_uuid, const uuid_t *target_uuid,
	enum thunderbolt_ip_type type, size_t size, u32 command_id)
{
	u32 length_sn;

	/* Length does not include route_hi/lo and length_sn fields */
	length_sn = (size - 3 * 4) / 4;
	length_sn |= (sequence << TBIP_HDR_SN_SHIFT) & TBIP_HDR_SN_MASK;

	hdr->route_hi = upper_32_bits(route);
	hdr->route_lo = lower_32_bits(route);
	hdr->length_sn = length_sn;
	uuid_copy(&hdr->uuid, &tbnet_svc_uuid);
	uuid_copy(&hdr->initiator_uuid, initiator_uuid);
	uuid_copy(&hdr->target_uuid, target_uuid);
	hdr->type = type;
	hdr->command_id = command_id;
}

static int tbnet_login_response(struct tbnet *net, u64 route, u8 sequence,
				u32 command_id)
{
	struct thunderbolt_ip_login_response reply;
	struct tb_xdomain *xd = net->xd;

	memset(&reply, 0, sizeof(reply));
	tbnet_fill_header(&reply.hdr, route, sequence, xd->local_uuid,
			  xd->remote_uuid, TBIP_LOGIN_RESPONSE, sizeof(reply),
			  command_id);
	memcpy(reply.receiver_mac, net->dev->dev_addr, ETH_ALEN);
	reply.receiver_mac_len = ETH_ALEN;

	return tb_xdomain_response(xd, &reply, sizeof(reply),
				   TB_CFG_PKG_XDOMAIN_RESP);
}

static int tbnet_login_request(struct tbnet *net, u8 sequence)
{
	struct thunderbolt_ip_login_response reply;
	struct thunderbolt_ip_login request;
	struct tb_xdomain *xd = net->xd;

	memset(&request, 0, sizeof(request));
	tbnet_fill_header(&request.hdr, xd->route, sequence, xd->local_uuid,
			  xd->remote_uuid, TBIP_LOGIN, sizeof(request),
			  atomic_inc_return(&net->command_id));

	request.proto_version = TBIP_LOGIN_PROTO_VERSION;
	request.transmit_path = TBNET_LOCAL_PATH;

	return tb_xdomain_request(xd, &request, sizeof(request),
				  TB_CFG_PKG_XDOMAIN_RESP, &reply,
				  sizeof(reply), TB_CFG_PKG_XDOMAIN_RESP,
				  TBNET_LOGIN_TIMEOUT);
}

static int tbnet_logout_response(struct tbnet *net, u64 route, u8 sequence,
				 u32 command_id)
{
	struct thunderbolt_ip_status reply;
	struct tb_xdomain *xd = net->xd;

	memset(&reply, 0, sizeof(reply));
	tbnet_fill_header(&reply.hdr, route, sequence, xd->local_uuid,
			  xd->remote_uuid, TBIP_STATUS, sizeof(reply),
			  atomic_inc_return(&net->command_id));
	return tb_xdomain_response(xd, &reply, sizeof(reply),
				   TB_CFG_PKG_XDOMAIN_RESP);
}

static int tbnet_logout_request(struct tbnet *net)
{
	struct thunderbolt_ip_logout request;
	struct thunderbolt_ip_status reply;
	struct tb_xdomain *xd = net->xd;

	memset(&request, 0, sizeof(request));
	tbnet_fill_header(&request.hdr, xd->route, 0, xd->local_uuid,
			  xd->remote_uuid, TBIP_LOGOUT, sizeof(request),
			  atomic_inc_return(&net->command_id));

	return tb_xdomain_request(xd, &request, sizeof(request),
				  TB_CFG_PKG_XDOMAIN_RESP, &reply,
				  sizeof(reply), TB_CFG_PKG_XDOMAIN_RESP,
				  TBNET_LOGOUT_TIMEOUT);
}

static void start_login(struct tbnet *net)
{
	mutex_lock(&net->connection_lock);
	net->login_sent = false;
	net->login_received = false;
	mutex_unlock(&net->connection_lock);

	queue_delayed_work(system_long_wq, &net->login_work,
			   msecs_to_jiffies(1000));
}

static void stop_login(struct tbnet *net)
{
	cancel_delayed_work_sync(&net->login_work);
	cancel_work_sync(&net->connected_work);
}

static inline unsigned int tbnet_frame_size(const struct tbnet_frame *tf)
{
	return tf->frame.size ? : TBNET_FRAME_SIZE;
}

static void tbnet_free_buffers(struct tbnet_ring *ring)
{
	unsigned int i;

	for (i = 0; i < TBNET_RING_SIZE; i++) {
		struct device *dma_dev = tb_ring_dma_device(ring->ring);
		struct tbnet_frame *tf = &ring->frames[i];
		enum dma_data_direction dir;
		unsigned int order;
		size_t size;

		if (!tf->page)
			continue;

		if (ring->ring->is_tx) {
			dir = DMA_TO_DEVICE;
			order = 0;
			size = tbnet_frame_size(tf);
		} else {
			dir = DMA_FROM_DEVICE;
			order = TBNET_RX_PAGE_ORDER;
			size = TBNET_RX_PAGE_SIZE;
		}

		if (tf->frame.buffer_phy)
			dma_unmap_page(dma_dev, tf->frame.buffer_phy, size,
				       dir);

		__free_pages(tf->page, order);
		tf->page = NULL;
	}

	ring->cons = 0;
	ring->prod = 0;
}

static void tbnet_tear_down(struct tbnet *net, bool send_logout)
{
	netif_carrier_off(net->dev);
	netif_stop_queue(net->dev);

	stop_login(net);

	mutex_lock(&net->connection_lock);

	if (net->login_sent && net->login_received) {
		int retries = TBNET_LOGOUT_RETRIES;

		while (send_logout && retries-- > 0) {
			int ret = tbnet_logout_request(net);
			if (ret != -ETIMEDOUT)
				break;
		}

		tb_ring_stop(net->rx_ring.ring);
		tb_ring_stop(net->tx_ring.ring);
		tbnet_free_buffers(&net->rx_ring);
		tbnet_free_buffers(&net->tx_ring);

		if (tb_xdomain_disable_paths(net->xd))
			netdev_warn(net->dev, "failed to disable DMA paths\n");
	}

	net->login_retries = 0;
	net->login_sent = false;
	net->login_received = false;

	mutex_unlock(&net->connection_lock);
}

static int tbnet_handle_packet(const void *buf, size_t size, void *data)
{
	const struct thunderbolt_ip_login *pkg = buf;
	struct tbnet *net = data;
	u32 command_id;
	int ret = 0;
	u8 sequence;
	u64 route;

	/* Make sure the packet is for us */
	if (size < sizeof(struct thunderbolt_ip_header))
		return 0;
	if (!uuid_equal(&pkg->hdr.initiator_uuid, net->xd->remote_uuid))
		return 0;
	if (!uuid_equal(&pkg->hdr.target_uuid, net->xd->local_uuid))
		return 0;

	route = ((u64)pkg->hdr.route_hi << 32) | pkg->hdr.route_lo;
	route &= ~BIT_ULL(63);
	if (route != net->xd->route)
		return 0;

	sequence = pkg->hdr.length_sn & TBIP_HDR_SN_MASK;
	sequence >>= TBIP_HDR_SN_SHIFT;
	command_id = pkg->hdr.command_id;

	switch (pkg->hdr.type) {
	case TBIP_LOGIN:
		if (!netif_running(net->dev))
			break;

		ret = tbnet_login_response(net, route, sequence,
					   pkg->hdr.command_id);
		if (!ret) {
			mutex_lock(&net->connection_lock);
			net->login_received = true;
			net->transmit_path = pkg->transmit_path;

			/* If we reached the number of max retries or
			 * previous logout, schedule another round of
			 * login retries
			 */
			if (net->login_retries >= TBNET_LOGIN_RETRIES ||
			    !net->login_sent) {
				net->login_retries = 0;
				queue_delayed_work(system_long_wq,
						   &net->login_work, 0);
			}
			mutex_unlock(&net->connection_lock);

			queue_work(system_long_wq, &net->connected_work);
		}
		break;

	case TBIP_LOGOUT:
		ret = tbnet_logout_response(net, route, sequence, command_id);
		if (!ret)
			tbnet_tear_down(net, false);
		break;

	default:
		return 0;
	}

	if (ret)
		netdev_warn(net->dev, "failed to send ThunderboltIP response\n");

	return 1;
}

static unsigned int tbnet_available_buffers(const struct tbnet_ring *ring)
{
	return ring->prod - ring->cons;
}

static int tbnet_alloc_rx_buffers(struct tbnet *net, unsigned int nbuffers)
{
	struct tbnet_ring *ring = &net->rx_ring;
	int ret;

	while (nbuffers--) {
		struct device *dma_dev = tb_ring_dma_device(ring->ring);
		unsigned int index = ring->prod & (TBNET_RING_SIZE - 1);
		struct tbnet_frame *tf = &ring->frames[index];
		dma_addr_t dma_addr;

		if (tf->page)
			break;

		/* Allocate page (order > 0) so that it can hold maximum
		 * ThunderboltIP frame (4kB) and the additional room for
		 * SKB shared info required by build_skb().
		 */
		tf->page = dev_alloc_pages(TBNET_RX_PAGE_ORDER);
		if (!tf->page) {
			ret = -ENOMEM;
			goto err_free;
		}

		dma_addr = dma_map_page(dma_dev, tf->page, 0,
					TBNET_RX_PAGE_SIZE, DMA_FROM_DEVICE);
		if (dma_mapping_error(dma_dev, dma_addr)) {
			ret = -ENOMEM;
			goto err_free;
		}

		tf->frame.buffer_phy = dma_addr;
		tf->dev = net->dev;

		tb_ring_rx(ring->ring, &tf->frame);

		ring->prod++;
	}

	return 0;

err_free:
	tbnet_free_buffers(ring);
	return ret;
}

static struct tbnet_frame *tbnet_get_tx_buffer(struct tbnet *net)
{
	struct tbnet_ring *ring = &net->tx_ring;
	struct tbnet_frame *tf;
	unsigned int index;

	if (!tbnet_available_buffers(ring))
		return NULL;

	index = ring->cons++ & (TBNET_RING_SIZE - 1);

	tf = &ring->frames[index];
	tf->frame.size = 0;
	tf->frame.buffer_phy = 0;

	return tf;
}

static void tbnet_tx_callback(struct tb_ring *ring, struct ring_frame *frame,
			      bool canceled)
{
	struct tbnet_frame *tf = container_of(frame, typeof(*tf), frame);
	struct device *dma_dev = tb_ring_dma_device(ring);
	struct tbnet *net = netdev_priv(tf->dev);

	dma_unmap_page(dma_dev, tf->frame.buffer_phy, tbnet_frame_size(tf),
		       DMA_TO_DEVICE);

	/* Return buffer to the ring */
	net->tx_ring.prod++;

	if (tbnet_available_buffers(&net->tx_ring) >= TBNET_RING_SIZE / 2)
		netif_wake_queue(net->dev);
}

static int tbnet_alloc_tx_buffers(struct tbnet *net)
{
	struct tbnet_ring *ring = &net->tx_ring;
	unsigned int i;

	for (i = 0; i < TBNET_RING_SIZE; i++) {
		struct tbnet_frame *tf = &ring->frames[i];

		tf->page = alloc_page(GFP_KERNEL);
		if (!tf->page) {
			tbnet_free_buffers(ring);
			return -ENOMEM;
		}

		tf->dev = net->dev;
		tf->frame.callback = tbnet_tx_callback;
		tf->frame.sof = TBIP_PDF_FRAME_START;
		tf->frame.eof = TBIP_PDF_FRAME_END;
	}

	ring->cons = 0;
	ring->prod = TBNET_RING_SIZE - 1;

	return 0;
}

static void tbnet_connected_work(struct work_struct *work)
{
	struct tbnet *net = container_of(work, typeof(*net), connected_work);
	bool connected;
	int ret;

	if (netif_carrier_ok(net->dev))
		return;

	mutex_lock(&net->connection_lock);
	connected = net->login_sent && net->login_received;
	mutex_unlock(&net->connection_lock);

	if (!connected)
		return;

	/* Both logins successful so enable the high-speed DMA paths and
	 * start the network device queue.
	 */
	ret = tb_xdomain_enable_paths(net->xd, TBNET_LOCAL_PATH,
				      net->rx_ring.ring->hop,
				      net->transmit_path,
				      net->tx_ring.ring->hop);
	if (ret) {
		netdev_err(net->dev, "failed to enable DMA paths\n");
		return;
	}

	tb_ring_start(net->tx_ring.ring);
	tb_ring_start(net->rx_ring.ring);

	ret = tbnet_alloc_rx_buffers(net, TBNET_RING_SIZE);
	if (ret)
		goto err_stop_rings;

	ret = tbnet_alloc_tx_buffers(net);
	if (ret)
		goto err_free_rx_buffers;

	netif_carrier_on(net->dev);
	netif_start_queue(net->dev);
	return;

err_free_rx_buffers:
	tbnet_free_buffers(&net->rx_ring);
err_stop_rings:
	tb_ring_stop(net->rx_ring.ring);
	tb_ring_stop(net->tx_ring.ring);
}

static void tbnet_login_work(struct work_struct *work)
{
	struct tbnet *net = container_of(work, typeof(*net), login_work.work);
	unsigned long delay = msecs_to_jiffies(TBNET_LOGIN_DELAY);
	int ret;

	if (netif_carrier_ok(net->dev))
		return;

	ret = tbnet_login_request(net, net->login_retries % 4);
	if (ret) {
		if (net->login_retries++ < TBNET_LOGIN_RETRIES) {
			queue_delayed_work(system_long_wq, &net->login_work,
					   delay);
		} else {
			netdev_info(net->dev, "ThunderboltIP login timed out\n");
		}
	} else {
		net->login_retries = 0;

		mutex_lock(&net->connection_lock);
		net->login_sent = true;
		mutex_unlock(&net->connection_lock);

		queue_work(system_long_wq, &net->connected_work);
	}
}

static bool tbnet_check_frame(struct tbnet *net, const struct tbnet_frame *tf,
			      const struct thunderbolt_ip_frame_header *hdr)
{
	u32 frame_id, frame_count, frame_size, frame_index;
	unsigned int size;

	if (tf->frame.flags & RING_DESC_CRC_ERROR) {
		net->stats.rx_crc_errors++;
		return false;
	} else if (tf->frame.flags & RING_DESC_BUFFER_OVERRUN) {
		net->stats.rx_over_errors++;
		return false;
	}

	/* Should be greater than just header i.e. contains data */
	size = tbnet_frame_size(tf);
	if (size <= sizeof(*hdr)) {
		net->stats.rx_length_errors++;
		return false;
	}

	frame_count = le32_to_cpu(hdr->frame_count);
	frame_size = le32_to_cpu(hdr->frame_size);
	frame_index = le16_to_cpu(hdr->frame_index);
	frame_id = le16_to_cpu(hdr->frame_id);

	if ((frame_size > size - sizeof(*hdr)) || !frame_size) {
		net->stats.rx_length_errors++;
		return false;
	}

	/* In case we're in the middle of packet, validate the frame
	 * header based on first fragment of the packet.
	 */
	if (net->skb && net->rx_hdr.frame_count) {
		/* Check the frame count fits the count field */
		if (frame_count != net->rx_hdr.frame_count) {
			net->stats.rx_length_errors++;
			return false;
		}

		/* Check the frame identifiers are incremented correctly,
		 * and id is matching.
		 */
		if (frame_index != net->rx_hdr.frame_index + 1 ||
		    frame_id != net->rx_hdr.frame_id) {
			net->stats.rx_missed_errors++;
			return false;
		}

		if (net->skb->len + frame_size > TBNET_MAX_MTU) {
			net->stats.rx_length_errors++;
			return false;
		}

		return true;
	}

	/* Start of packet, validate the frame header */
	if (frame_count == 0 || frame_count > TBNET_RING_SIZE / 4) {
		net->stats.rx_length_errors++;
		return false;
	}
	if (frame_index != 0) {
		net->stats.rx_missed_errors++;
		return false;
	}

	return true;
}

static int tbnet_poll(struct napi_struct *napi, int budget)
{
	struct tbnet *net = container_of(napi, struct tbnet, napi);
	unsigned int cleaned_count = tbnet_available_buffers(&net->rx_ring);
	struct device *dma_dev = tb_ring_dma_device(net->rx_ring.ring);
	unsigned int rx_packets = 0;

	while (rx_packets < budget) {
		const struct thunderbolt_ip_frame_header *hdr;
		unsigned int hdr_size = sizeof(*hdr);
		struct sk_buff *skb = NULL;
		struct ring_frame *frame;
		struct tbnet_frame *tf;
		struct page *page;
		bool last = true;
		u32 frame_size;

		/* Return some buffers to hardware, one at a time is too
		 * slow so allocate MAX_SKB_FRAGS buffers at the same
		 * time.
		 */
		if (cleaned_count >= MAX_SKB_FRAGS) {
			tbnet_alloc_rx_buffers(net, cleaned_count);
			cleaned_count = 0;
		}

		frame = tb_ring_poll(net->rx_ring.ring);
		if (!frame)
			break;

		dma_unmap_page(dma_dev, frame->buffer_phy,
			       TBNET_RX_PAGE_SIZE, DMA_FROM_DEVICE);

		tf = container_of(frame, typeof(*tf), frame);

		page = tf->page;
		tf->page = NULL;
		net->rx_ring.cons++;
		cleaned_count++;

		hdr = page_address(page);
		if (!tbnet_check_frame(net, tf, hdr)) {
			__free_pages(page, TBNET_RX_PAGE_ORDER);
			dev_kfree_skb_any(net->skb);
			net->skb = NULL;
			continue;
		}

		frame_size = le32_to_cpu(hdr->frame_size);

		skb = net->skb;
		if (!skb) {
			skb = build_skb(page_address(page),
					TBNET_RX_PAGE_SIZE);
			if (!skb) {
				__free_pages(page, TBNET_RX_PAGE_ORDER);
				net->stats.rx_errors++;
				break;
			}

			skb_reserve(skb, hdr_size);
			skb_put(skb, frame_size);

			net->skb = skb;
		} else {
			skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
					page, hdr_size, frame_size,
					TBNET_RX_PAGE_SIZE - hdr_size);
		}

		net->rx_hdr.frame_size = frame_size;
		net->rx_hdr.frame_count = le32_to_cpu(hdr->frame_count);
		net->rx_hdr.frame_index = le16_to_cpu(hdr->frame_index);
		net->rx_hdr.frame_id = le16_to_cpu(hdr->frame_id);
		last = net->rx_hdr.frame_index == net->rx_hdr.frame_count - 1;

		rx_packets++;
		net->stats.rx_bytes += frame_size;

		if (last) {
			skb->protocol = eth_type_trans(skb, net->dev);
			napi_gro_receive(&net->napi, skb);
			net->skb = NULL;
		}
	}

	net->stats.rx_packets += rx_packets;

	if (cleaned_count)
		tbnet_alloc_rx_buffers(net, cleaned_count);

	if (rx_packets >= budget)
		return budget;

	napi_complete_done(napi, rx_packets);
	/* Re-enable the ring interrupt */
	tb_ring_poll_complete(net->rx_ring.ring);

	return rx_packets;
}

static void tbnet_start_poll(void *data)
{
	struct tbnet *net = data;

	napi_schedule(&net->napi);
}

static int tbnet_open(struct net_device *dev)
{
	struct tbnet *net = netdev_priv(dev);
	struct tb_xdomain *xd = net->xd;
	u16 sof_mask, eof_mask;
	struct tb_ring *ring;

	netif_carrier_off(dev);

	ring = tb_ring_alloc_tx(xd->tb->nhi, -1, TBNET_RING_SIZE,
				RING_FLAG_FRAME);
	if (!ring) {
		netdev_err(dev, "failed to allocate Tx ring\n");
		return -ENOMEM;
	}
	net->tx_ring.ring = ring;

	sof_mask = BIT(TBIP_PDF_FRAME_START);
	eof_mask = BIT(TBIP_PDF_FRAME_END);

	ring = tb_ring_alloc_rx(xd->tb->nhi, -1, TBNET_RING_SIZE,
				RING_FLAG_FRAME | RING_FLAG_E2E, sof_mask,
				eof_mask, tbnet_start_poll, net);
	if (!ring) {
		netdev_err(dev, "failed to allocate Rx ring\n");
		tb_ring_free(net->tx_ring.ring);
		net->tx_ring.ring = NULL;
		return -ENOMEM;
	}
	net->rx_ring.ring = ring;

	napi_enable(&net->napi);
	start_login(net);

	return 0;
}

static int tbnet_stop(struct net_device *dev)
{
	struct tbnet *net = netdev_priv(dev);

	napi_disable(&net->napi);

	tbnet_tear_down(net, true);

	tb_ring_free(net->rx_ring.ring);
	net->rx_ring.ring = NULL;
	tb_ring_free(net->tx_ring.ring);
	net->tx_ring.ring = NULL;

	return 0;
}

static bool tbnet_xmit_map(struct device *dma_dev, struct tbnet_frame *tf)
{
	dma_addr_t dma_addr;

	dma_addr = dma_map_page(dma_dev, tf->page, 0, tbnet_frame_size(tf),
				DMA_TO_DEVICE);
	if (dma_mapping_error(dma_dev, dma_addr))
		return false;

	tf->frame.buffer_phy = dma_addr;
	return true;
}

static bool tbnet_xmit_csum_and_map(struct tbnet *net, struct sk_buff *skb,
	struct tbnet_frame **frames, u32 frame_count)
{
	struct thunderbolt_ip_frame_header *hdr = page_address(frames[0]->page);
	struct device *dma_dev = tb_ring_dma_device(net->tx_ring.ring);
	__wsum wsum = htonl(skb->len - skb_transport_offset(skb));
	unsigned int i, len, offset = skb_transport_offset(skb);
	__be16 protocol = skb->protocol;
	void *data = skb->data;
	void *dest = hdr + 1;
	__sum16 *tucso;

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		/* No need to calculate checksum so we just update the
		 * total frame count and map the frames for DMA.
		 */
		for (i = 0; i < frame_count; i++) {
			hdr = page_address(frames[i]->page);
			hdr->frame_count = cpu_to_le32(frame_count);
			if (!tbnet_xmit_map(dma_dev, frames[i]))
				goto err_unmap;
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

	/* Data points on the beginning of packet.
	 * Check is the checksum absolute place in the packet.
	 * ipcso will update IP checksum.
	 * tucso will update TCP/UPD checksum.
	 */
	if (protocol == htons(ETH_P_IP)) {
		__sum16 *ipcso = dest + ((void *)&(ip_hdr(skb)->check) - data);

		*ipcso = 0;
		*ipcso = ip_fast_csum(dest + skb_network_offset(skb),
				      ip_hdr(skb)->ihl);

		if (ip_hdr(skb)->protocol == IPPROTO_TCP)
			tucso = dest + ((void *)&(tcp_hdr(skb)->check) - data);
		else if (ip_hdr(skb)->protocol == IPPROTO_UDP)
			tucso = dest + ((void *)&(udp_hdr(skb)->check) - data);
		else
			return false;

		*tucso = ~csum_tcpudp_magic(ip_hdr(skb)->saddr,
					    ip_hdr(skb)->daddr, 0,
					    ip_hdr(skb)->protocol, 0);
	} else if (skb_is_gso_v6(skb)) {
		tucso = dest + ((void *)&(tcp_hdr(skb)->check) - data);
		*tucso = ~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
					  &ipv6_hdr(skb)->daddr, 0,
					  IPPROTO_TCP, 0);
		return false;
	} else if (protocol == htons(ETH_P_IPV6)) {
		tucso = dest + skb_checksum_start_offset(skb) + skb->csum_offset;
		*tucso = ~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
					  &ipv6_hdr(skb)->daddr, 0,
					  ipv6_hdr(skb)->nexthdr, 0);
	} else {
		return false;
	}

	/* First frame was headers, rest of the frames contain data.
	 * Calculate checksum over each frame.
	 */
	for (i = 0; i < frame_count; i++) {
		hdr = page_address(frames[i]->page);
		dest = (void *)(hdr + 1) + offset;
		len = le32_to_cpu(hdr->frame_size) - offset;
		wsum = csum_partial(dest, len, wsum);
		hdr->frame_count = cpu_to_le32(frame_count);

		offset = 0;
	}

	*tucso = csum_fold(wsum);

	/* Checksum is finally calculated and we don't touch the memory
	 * anymore, so DMA map the frames now.
	 */
	for (i = 0; i < frame_count; i++) {
		if (!tbnet_xmit_map(dma_dev, frames[i]))
			goto err_unmap;
	}

	return true;

err_unmap:
	while (i--)
		dma_unmap_page(dma_dev, frames[i]->frame.buffer_phy,
			       tbnet_frame_size(frames[i]), DMA_TO_DEVICE);

	return false;
}

static void *tbnet_kmap_frag(struct sk_buff *skb, unsigned int frag_num,
			     unsigned int *len)
{
	const skb_frag_t *frag = &skb_shinfo(skb)->frags[frag_num];

	*len = skb_frag_size(frag);
	return kmap_atomic(skb_frag_page(frag)) + frag->page_offset;
}

static netdev_tx_t tbnet_start_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct tbnet *net = netdev_priv(dev);
	struct tbnet_frame *frames[MAX_SKB_FRAGS];
	u16 frame_id = atomic_read(&net->frame_id);
	struct thunderbolt_ip_frame_header *hdr;
	unsigned int len = skb_headlen(skb);
	unsigned int data_len = skb->len;
	unsigned int nframes, i;
	unsigned int frag = 0;
	void *src = skb->data;
	u32 frame_index = 0;
	bool unmap = false;
	void *dest;

	nframes = DIV_ROUND_UP(data_len, TBNET_MAX_PAYLOAD_SIZE);
	if (tbnet_available_buffers(&net->tx_ring) < nframes) {
		netif_stop_queue(net->dev);
		return NETDEV_TX_BUSY;
	}

	frames[frame_index] = tbnet_get_tx_buffer(net);
	if (!frames[frame_index])
		goto err_drop;

	hdr = page_address(frames[frame_index]->page);
	dest = hdr + 1;

	/* If overall packet is bigger than the frame data size */
	while (data_len > TBNET_MAX_PAYLOAD_SIZE) {
		unsigned int size_left = TBNET_MAX_PAYLOAD_SIZE;

		hdr->frame_size = cpu_to_le32(TBNET_MAX_PAYLOAD_SIZE);
		hdr->frame_index = cpu_to_le16(frame_index);
		hdr->frame_id = cpu_to_le16(frame_id);

		do {
			if (len > size_left) {
				/* Copy data onto Tx buffer data with
				 * full frame size then break and go to
				 * next frame
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
				kunmap_atomic(src);
				unmap = false;
			}

			/* Ensure all fragments have been processed */
			if (frag < skb_shinfo(skb)->nr_frags) {
				/* Map and then unmap quickly */
				src = tbnet_kmap_frag(skb, frag++, &len);
				unmap = true;
			} else if (unlikely(size_left > 0)) {
				goto err_drop;
			}
		} while (size_left > 0);

		data_len -= TBNET_MAX_PAYLOAD_SIZE;
		frame_index++;

		frames[frame_index] = tbnet_get_tx_buffer(net);
		if (!frames[frame_index])
			goto err_drop;

		hdr = page_address(frames[frame_index]->page);
		dest = hdr + 1;
	}

	hdr->frame_size = cpu_to_le32(data_len);
	hdr->frame_index = cpu_to_le16(frame_index);
	hdr->frame_id = cpu_to_le16(frame_id);

	frames[frame_index]->frame.size = data_len + sizeof(*hdr);

	/* In case the remaining data_len is smaller than a frame */
	while (len < data_len) {
		memcpy(dest, src, len);
		data_len -= len;
		dest += len;

		if (unmap) {
			kunmap_atomic(src);
			unmap = false;
		}

		if (frag < skb_shinfo(skb)->nr_frags) {
			src = tbnet_kmap_frag(skb, frag++, &len);
			unmap = true;
		} else if (unlikely(data_len > 0)) {
			goto err_drop;
		}
	}

	memcpy(dest, src, data_len);

	if (unmap)
		kunmap_atomic(src);

	if (!tbnet_xmit_csum_and_map(net, skb, frames, frame_index + 1))
		goto err_drop;

	for (i = 0; i < frame_index + 1; i++)
		tb_ring_tx(net->tx_ring.ring, &frames[i]->frame);

	if (net->svc->prtcstns & TBNET_MATCH_FRAGS_ID)
		atomic_inc(&net->frame_id);

	net->stats.tx_packets++;
	net->stats.tx_bytes += skb->len;

	dev_consume_skb_any(skb);

	return NETDEV_TX_OK;

err_drop:
	/* We can re-use the buffers */
	net->tx_ring.cons -= frame_index;

	dev_kfree_skb_any(skb);
	net->stats.tx_errors++;

	return NETDEV_TX_OK;
}

static void tbnet_get_stats64(struct net_device *dev,
			      struct rtnl_link_stats64 *stats)
{
	struct tbnet *net = netdev_priv(dev);

	stats->tx_packets = net->stats.tx_packets;
	stats->rx_packets = net->stats.rx_packets;
	stats->tx_bytes = net->stats.tx_bytes;
	stats->rx_bytes = net->stats.rx_bytes;
	stats->rx_errors = net->stats.rx_errors + net->stats.rx_length_errors +
		net->stats.rx_over_errors + net->stats.rx_crc_errors +
		net->stats.rx_missed_errors;
	stats->tx_errors = net->stats.tx_errors;
	stats->rx_length_errors = net->stats.rx_length_errors;
	stats->rx_over_errors = net->stats.rx_over_errors;
	stats->rx_crc_errors = net->stats.rx_crc_errors;
	stats->rx_missed_errors = net->stats.rx_missed_errors;
}

static const struct net_device_ops tbnet_netdev_ops = {
	.ndo_open = tbnet_open,
	.ndo_stop = tbnet_stop,
	.ndo_start_xmit = tbnet_start_xmit,
	.ndo_get_stats64 = tbnet_get_stats64,
};

static void tbnet_generate_mac(struct net_device *dev)
{
	const struct tbnet *net = netdev_priv(dev);
	const struct tb_xdomain *xd = net->xd;
	u8 phy_port;
	u32 hash;

	phy_port = tb_phy_port_from_link(TBNET_L0_PORT_NUM(xd->route));

	/* Unicast and locally administered MAC */
	dev->dev_addr[0] = phy_port << 4 | 0x02;
	hash = jhash2((u32 *)xd->local_uuid, 4, 0);
	memcpy(dev->dev_addr + 1, &hash, sizeof(hash));
	hash = jhash2((u32 *)xd->local_uuid, 4, hash);
	dev->dev_addr[5] = hash & 0xff;
}

static int tbnet_probe(struct tb_service *svc, const struct tb_service_id *id)
{
	struct tb_xdomain *xd = tb_service_parent(svc);
	struct net_device *dev;
	struct tbnet *net;
	int ret;

	dev = alloc_etherdev(sizeof(*net));
	if (!dev)
		return -ENOMEM;

	SET_NETDEV_DEV(dev, &svc->dev);

	net = netdev_priv(dev);
	INIT_DELAYED_WORK(&net->login_work, tbnet_login_work);
	INIT_WORK(&net->connected_work, tbnet_connected_work);
	mutex_init(&net->connection_lock);
	atomic_set(&net->command_id, 0);
	atomic_set(&net->frame_id, 0);
	net->svc = svc;
	net->dev = dev;
	net->xd = xd;

	tbnet_generate_mac(dev);

	strcpy(dev->name, "thunderbolt%d");
	dev->netdev_ops = &tbnet_netdev_ops;

	/* ThunderboltIP takes advantage of TSO packets but instead of
	 * segmenting them we just split the packet into Thunderbolt
	 * frames (maximum payload size of each frame is 4084 bytes) and
	 * calculate checksum over the whole packet here.
	 *
	 * The receiving side does the opposite if the host OS supports
	 * LRO, otherwise it needs to split the large packet into MTU
	 * sized smaller packets.
	 *
	 * In order to receive large packets from the networking stack,
	 * we need to announce support for most of the offloading
	 * features here.
	 */
	dev->hw_features = NETIF_F_SG | NETIF_F_ALL_TSO | NETIF_F_GRO |
			   NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	dev->features = dev->hw_features | NETIF_F_HIGHDMA;
	dev->hard_header_len += sizeof(struct thunderbolt_ip_frame_header);

	netif_napi_add(dev, &net->napi, tbnet_poll, NAPI_POLL_WEIGHT);

	/* MTU range: 68 - 65522 */
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = TBNET_MAX_MTU - ETH_HLEN;

	net->handler.uuid = &tbnet_svc_uuid;
	net->handler.callback = tbnet_handle_packet,
	net->handler.data = net;
	tb_register_protocol_handler(&net->handler);

	tb_service_set_drvdata(svc, net);

	ret = register_netdev(dev);
	if (ret) {
		tb_unregister_protocol_handler(&net->handler);
		free_netdev(dev);
		return ret;
	}

	return 0;
}

static void tbnet_remove(struct tb_service *svc)
{
	struct tbnet *net = tb_service_get_drvdata(svc);

	unregister_netdev(net->dev);
	tb_unregister_protocol_handler(&net->handler);
	free_netdev(net->dev);
}

static void tbnet_shutdown(struct tb_service *svc)
{
	tbnet_tear_down(tb_service_get_drvdata(svc), true);
}

static int __maybe_unused tbnet_suspend(struct device *dev)
{
	struct tb_service *svc = tb_to_service(dev);
	struct tbnet *net = tb_service_get_drvdata(svc);

	stop_login(net);
	if (netif_running(net->dev)) {
		netif_device_detach(net->dev);
		tb_ring_stop(net->rx_ring.ring);
		tb_ring_stop(net->tx_ring.ring);
		tbnet_free_buffers(&net->rx_ring);
		tbnet_free_buffers(&net->tx_ring);
	}

	return 0;
}

static int __maybe_unused tbnet_resume(struct device *dev)
{
	struct tb_service *svc = tb_to_service(dev);
	struct tbnet *net = tb_service_get_drvdata(svc);

	netif_carrier_off(net->dev);
	if (netif_running(net->dev)) {
		netif_device_attach(net->dev);
		start_login(net);
	}

	return 0;
}

static const struct dev_pm_ops tbnet_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tbnet_suspend, tbnet_resume)
};

static const struct tb_service_id tbnet_ids[] = {
	{ TB_SERVICE("network", 1) },
	{ },
};
MODULE_DEVICE_TABLE(tbsvc, tbnet_ids);

static struct tb_service_driver tbnet_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "thunderbolt-net",
		.pm = &tbnet_pm_ops,
	},
	.probe = tbnet_probe,
	.remove = tbnet_remove,
	.shutdown = tbnet_shutdown,
	.id_table = tbnet_ids,
};

static int __init tbnet_init(void)
{
	int ret;

	tbnet_dir = tb_property_create_dir(&tbnet_dir_uuid);
	if (!tbnet_dir)
		return -ENOMEM;

	tb_property_add_immediate(tbnet_dir, "prtcid", 1);
	tb_property_add_immediate(tbnet_dir, "prtcvers", 1);
	tb_property_add_immediate(tbnet_dir, "prtcrevs", 1);
	tb_property_add_immediate(tbnet_dir, "prtcstns",
				  TBNET_MATCH_FRAGS_ID);

	ret = tb_register_property_dir("network", tbnet_dir);
	if (ret) {
		tb_property_free_dir(tbnet_dir);
		return ret;
	}

	return tb_register_service_driver(&tbnet_driver);
}
module_init(tbnet_init);

static void __exit tbnet_exit(void)
{
	tb_unregister_service_driver(&tbnet_driver);
	tb_unregister_property_dir("network", tbnet_dir);
	tb_property_free_dir(tbnet_dir);
}
module_exit(tbnet_exit);

MODULE_AUTHOR("Amir Levy <amir.jer.levy@intel.com>");
MODULE_AUTHOR("Michael Jamet <michael.jamet@intel.com>");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_DESCRIPTION("Thunderbolt network driver");
MODULE_LICENSE("GPL v2");
