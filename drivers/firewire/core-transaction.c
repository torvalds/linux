/*
 * Core IEEE1394 transaction logic
 *
 * Copyright (C) 2004-2006 Kristian Hoegsberg <krh@bitplanet.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/bug.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/idr.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/rculist.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <asm/byteorder.h>

#include "core.h"

#define HEADER_PRI(pri)			((pri) << 0)
#define HEADER_TCODE(tcode)		((tcode) << 4)
#define HEADER_RETRY(retry)		((retry) << 8)
#define HEADER_TLABEL(tlabel)		((tlabel) << 10)
#define HEADER_DESTINATION(destination)	((destination) << 16)
#define HEADER_SOURCE(source)		((source) << 16)
#define HEADER_RCODE(rcode)		((rcode) << 12)
#define HEADER_OFFSET_HIGH(offset_high)	((offset_high) << 0)
#define HEADER_DATA_LENGTH(length)	((length) << 16)
#define HEADER_EXTENDED_TCODE(tcode)	((tcode) << 0)

#define HEADER_GET_TCODE(q)		(((q) >> 4) & 0x0f)
#define HEADER_GET_TLABEL(q)		(((q) >> 10) & 0x3f)
#define HEADER_GET_RCODE(q)		(((q) >> 12) & 0x0f)
#define HEADER_GET_DESTINATION(q)	(((q) >> 16) & 0xffff)
#define HEADER_GET_SOURCE(q)		(((q) >> 16) & 0xffff)
#define HEADER_GET_OFFSET_HIGH(q)	(((q) >> 0) & 0xffff)
#define HEADER_GET_DATA_LENGTH(q)	(((q) >> 16) & 0xffff)
#define HEADER_GET_EXTENDED_TCODE(q)	(((q) >> 0) & 0xffff)

#define HEADER_DESTINATION_IS_BROADCAST(q) \
	(((q) & HEADER_DESTINATION(0x3f)) == HEADER_DESTINATION(0x3f))

#define PHY_PACKET_CONFIG	0x0
#define PHY_PACKET_LINK_ON	0x1
#define PHY_PACKET_SELF_ID	0x2

#define PHY_CONFIG_GAP_COUNT(gap_count)	(((gap_count) << 16) | (1 << 22))
#define PHY_CONFIG_ROOT_ID(node_id)	((((node_id) & 0x3f) << 24) | (1 << 23))
#define PHY_IDENTIFIER(id)		((id) << 30)

/* returns 0 if the split timeout handler is already running */
static int try_cancel_split_timeout(struct fw_transaction *t)
{
	if (t->is_split_transaction)
		return del_timer(&t->split_timeout_timer);
	else
		return 1;
}

static int close_transaction(struct fw_transaction *transaction,
			     struct fw_card *card, int rcode)
{
	struct fw_transaction *t;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	list_for_each_entry(t, &card->transaction_list, link) {
		if (t == transaction) {
			if (!try_cancel_split_timeout(t)) {
				spin_unlock_irqrestore(&card->lock, flags);
				goto timed_out;
			}
			list_del_init(&t->link);
			card->tlabel_mask &= ~(1ULL << t->tlabel);
			break;
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);

	if (&t->link != &card->transaction_list) {
		t->callback(card, rcode, NULL, 0, t->callback_data);
		return 0;
	}

 timed_out:
	return -ENOENT;
}

/*
 * Only valid for transactions that are potentially pending (ie have
 * been sent).
 */
int fw_cancel_transaction(struct fw_card *card,
			  struct fw_transaction *transaction)
{
	/*
	 * Cancel the packet transmission if it's still queued.  That
	 * will call the packet transmission callback which cancels
	 * the transaction.
	 */

	if (card->driver->cancel_packet(card, &transaction->packet) == 0)
		return 0;

	/*
	 * If the request packet has already been sent, we need to see
	 * if the transaction is still pending and remove it in that case.
	 */

	return close_transaction(transaction, card, RCODE_CANCELLED);
}
EXPORT_SYMBOL(fw_cancel_transaction);

static void split_transaction_timeout_callback(unsigned long data)
{
	struct fw_transaction *t = (struct fw_transaction *)data;
	struct fw_card *card = t->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	if (list_empty(&t->link)) {
		spin_unlock_irqrestore(&card->lock, flags);
		return;
	}
	list_del(&t->link);
	card->tlabel_mask &= ~(1ULL << t->tlabel);
	spin_unlock_irqrestore(&card->lock, flags);

	t->callback(card, RCODE_CANCELLED, NULL, 0, t->callback_data);
}

static void start_split_transaction_timeout(struct fw_transaction *t,
					    struct fw_card *card)
{
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	if (list_empty(&t->link) || WARN_ON(t->is_split_transaction)) {
		spin_unlock_irqrestore(&card->lock, flags);
		return;
	}

	t->is_split_transaction = true;
	mod_timer(&t->split_timeout_timer,
		  jiffies + card->split_timeout_jiffies);

	spin_unlock_irqrestore(&card->lock, flags);
}

static void transmit_complete_callback(struct fw_packet *packet,
				       struct fw_card *card, int status)
{
	struct fw_transaction *t =
	    container_of(packet, struct fw_transaction, packet);

	switch (status) {
	case ACK_COMPLETE:
		close_transaction(t, card, RCODE_COMPLETE);
		break;
	case ACK_PENDING:
		start_split_transaction_timeout(t, card);
		break;
	case ACK_BUSY_X:
	case ACK_BUSY_A:
	case ACK_BUSY_B:
		close_transaction(t, card, RCODE_BUSY);
		break;
	case ACK_DATA_ERROR:
		close_transaction(t, card, RCODE_DATA_ERROR);
		break;
	case ACK_TYPE_ERROR:
		close_transaction(t, card, RCODE_TYPE_ERROR);
		break;
	default:
		/*
		 * In this case the ack is really a juju specific
		 * rcode, so just forward that to the callback.
		 */
		close_transaction(t, card, status);
		break;
	}
}

static void fw_fill_request(struct fw_packet *packet, int tcode, int tlabel,
		int destination_id, int source_id, int generation, int speed,
		unsigned long long offset, void *payload, size_t length)
{
	int ext_tcode;

	if (tcode == TCODE_STREAM_DATA) {
		packet->header[0] =
			HEADER_DATA_LENGTH(length) |
			destination_id |
			HEADER_TCODE(TCODE_STREAM_DATA);
		packet->header_length = 4;
		packet->payload = payload;
		packet->payload_length = length;

		goto common;
	}

	if (tcode > 0x10) {
		ext_tcode = tcode & ~0x10;
		tcode = TCODE_LOCK_REQUEST;
	} else
		ext_tcode = 0;

	packet->header[0] =
		HEADER_RETRY(RETRY_X) |
		HEADER_TLABEL(tlabel) |
		HEADER_TCODE(tcode) |
		HEADER_DESTINATION(destination_id);
	packet->header[1] =
		HEADER_OFFSET_HIGH(offset >> 32) | HEADER_SOURCE(source_id);
	packet->header[2] =
		offset;

	switch (tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
		packet->header[3] = *(u32 *)payload;
		packet->header_length = 16;
		packet->payload_length = 0;
		break;

	case TCODE_LOCK_REQUEST:
	case TCODE_WRITE_BLOCK_REQUEST:
		packet->header[3] =
			HEADER_DATA_LENGTH(length) |
			HEADER_EXTENDED_TCODE(ext_tcode);
		packet->header_length = 16;
		packet->payload = payload;
		packet->payload_length = length;
		break;

	case TCODE_READ_QUADLET_REQUEST:
		packet->header_length = 12;
		packet->payload_length = 0;
		break;

	case TCODE_READ_BLOCK_REQUEST:
		packet->header[3] =
			HEADER_DATA_LENGTH(length) |
			HEADER_EXTENDED_TCODE(ext_tcode);
		packet->header_length = 16;
		packet->payload_length = 0;
		break;

	default:
		WARN(1, "wrong tcode %d\n", tcode);
	}
 common:
	packet->speed = speed;
	packet->generation = generation;
	packet->ack = 0;
	packet->payload_mapped = false;
}

static int allocate_tlabel(struct fw_card *card)
{
	int tlabel;

	tlabel = card->current_tlabel;
	while (card->tlabel_mask & (1ULL << tlabel)) {
		tlabel = (tlabel + 1) & 0x3f;
		if (tlabel == card->current_tlabel)
			return -EBUSY;
	}

	card->current_tlabel = (tlabel + 1) & 0x3f;
	card->tlabel_mask |= 1ULL << tlabel;

	return tlabel;
}

/**
 * fw_send_request() - submit a request packet for transmission
 * @card:		interface to send the request at
 * @t:			transaction instance to which the request belongs
 * @tcode:		transaction code
 * @destination_id:	destination node ID, consisting of bus_ID and phy_ID
 * @generation:		bus generation in which request and response are valid
 * @speed:		transmission speed
 * @offset:		48bit wide offset into destination's address space
 * @payload:		data payload for the request subaction
 * @length:		length of the payload, in bytes
 * @callback:		function to be called when the transaction is completed
 * @callback_data:	data to be passed to the transaction completion callback
 *
 * Submit a request packet into the asynchronous request transmission queue.
 * Can be called from atomic context.  If you prefer a blocking API, use
 * fw_run_transaction() in a context that can sleep.
 *
 * In case of lock requests, specify one of the firewire-core specific %TCODE_
 * constants instead of %TCODE_LOCK_REQUEST in @tcode.
 *
 * Make sure that the value in @destination_id is not older than the one in
 * @generation.  Otherwise the request is in danger to be sent to a wrong node.
 *
 * In case of asynchronous stream packets i.e. %TCODE_STREAM_DATA, the caller
 * needs to synthesize @destination_id with fw_stream_packet_destination_id().
 * It will contain tag, channel, and sy data instead of a node ID then.
 *
 * The payload buffer at @data is going to be DMA-mapped except in case of
 * @length <= 8 or of local (loopback) requests.  Hence make sure that the
 * buffer complies with the restrictions of the streaming DMA mapping API.
 * @payload must not be freed before the @callback is called.
 *
 * In case of request types without payload, @data is NULL and @length is 0.
 *
 * After the transaction is completed successfully or unsuccessfully, the
 * @callback will be called.  Among its parameters is the response code which
 * is either one of the rcodes per IEEE 1394 or, in case of internal errors,
 * the firewire-core specific %RCODE_SEND_ERROR.  The other firewire-core
 * specific rcodes (%RCODE_CANCELLED, %RCODE_BUSY, %RCODE_GENERATION,
 * %RCODE_NO_ACK) denote transaction timeout, busy responder, stale request
 * generation, or missing ACK respectively.
 *
 * Note some timing corner cases:  fw_send_request() may complete much earlier
 * than when the request packet actually hits the wire.  On the other hand,
 * transaction completion and hence execution of @callback may happen even
 * before fw_send_request() returns.
 */
void fw_send_request(struct fw_card *card, struct fw_transaction *t, int tcode,
		     int destination_id, int generation, int speed,
		     unsigned long long offset, void *payload, size_t length,
		     fw_transaction_callback_t callback, void *callback_data)
{
	unsigned long flags;
	int tlabel;

	/*
	 * Allocate tlabel from the bitmap and put the transaction on
	 * the list while holding the card spinlock.
	 */

	spin_lock_irqsave(&card->lock, flags);

	tlabel = allocate_tlabel(card);
	if (tlabel < 0) {
		spin_unlock_irqrestore(&card->lock, flags);
		callback(card, RCODE_SEND_ERROR, NULL, 0, callback_data);
		return;
	}

	t->node_id = destination_id;
	t->tlabel = tlabel;
	t->card = card;
	t->is_split_transaction = false;
	setup_timer(&t->split_timeout_timer,
		    split_transaction_timeout_callback, (unsigned long)t);
	t->callback = callback;
	t->callback_data = callback_data;

	fw_fill_request(&t->packet, tcode, t->tlabel,
			destination_id, card->node_id, generation,
			speed, offset, payload, length);
	t->packet.callback = transmit_complete_callback;

	list_add_tail(&t->link, &card->transaction_list);

	spin_unlock_irqrestore(&card->lock, flags);

	card->driver->send_request(card, &t->packet);
}
EXPORT_SYMBOL(fw_send_request);

struct transaction_callback_data {
	struct completion done;
	void *payload;
	int rcode;
};

static void transaction_callback(struct fw_card *card, int rcode,
				 void *payload, size_t length, void *data)
{
	struct transaction_callback_data *d = data;

	if (rcode == RCODE_COMPLETE)
		memcpy(d->payload, payload, length);
	d->rcode = rcode;
	complete(&d->done);
}

/**
 * fw_run_transaction() - send request and sleep until transaction is completed
 *
 * Returns the RCODE.  See fw_send_request() for parameter documentation.
 * Unlike fw_send_request(), @data points to the payload of the request or/and
 * to the payload of the response.  DMA mapping restrictions apply to outbound
 * request payloads of >= 8 bytes but not to inbound response payloads.
 */
int fw_run_transaction(struct fw_card *card, int tcode, int destination_id,
		       int generation, int speed, unsigned long long offset,
		       void *payload, size_t length)
{
	struct transaction_callback_data d;
	struct fw_transaction t;

	init_timer_on_stack(&t.split_timeout_timer);
	init_completion(&d.done);
	d.payload = payload;
	fw_send_request(card, &t, tcode, destination_id, generation, speed,
			offset, payload, length, transaction_callback, &d);
	wait_for_completion(&d.done);
	destroy_timer_on_stack(&t.split_timeout_timer);

	return d.rcode;
}
EXPORT_SYMBOL(fw_run_transaction);

static DEFINE_MUTEX(phy_config_mutex);
static DECLARE_COMPLETION(phy_config_done);

static void transmit_phy_packet_callback(struct fw_packet *packet,
					 struct fw_card *card, int status)
{
	complete(&phy_config_done);
}

static struct fw_packet phy_config_packet = {
	.header_length	= 12,
	.header[0]	= TCODE_LINK_INTERNAL << 4,
	.payload_length	= 0,
	.speed		= SCODE_100,
	.callback	= transmit_phy_packet_callback,
};

void fw_send_phy_config(struct fw_card *card,
			int node_id, int generation, int gap_count)
{
	long timeout = DIV_ROUND_UP(HZ, 10);
	u32 data = PHY_IDENTIFIER(PHY_PACKET_CONFIG);

	if (node_id != FW_PHY_CONFIG_NO_NODE_ID)
		data |= PHY_CONFIG_ROOT_ID(node_id);

	if (gap_count == FW_PHY_CONFIG_CURRENT_GAP_COUNT) {
		gap_count = card->driver->read_phy_reg(card, 1);
		if (gap_count < 0)
			return;

		gap_count &= 63;
		if (gap_count == 63)
			return;
	}
	data |= PHY_CONFIG_GAP_COUNT(gap_count);

	mutex_lock(&phy_config_mutex);

	phy_config_packet.header[1] = data;
	phy_config_packet.header[2] = ~data;
	phy_config_packet.generation = generation;
	reinit_completion(&phy_config_done);

	card->driver->send_request(card, &phy_config_packet);
	wait_for_completion_timeout(&phy_config_done, timeout);

	mutex_unlock(&phy_config_mutex);
}

static struct fw_address_handler *lookup_overlapping_address_handler(
	struct list_head *list, unsigned long long offset, size_t length)
{
	struct fw_address_handler *handler;

	list_for_each_entry_rcu(handler, list, link) {
		if (handler->offset < offset + length &&
		    offset < handler->offset + handler->length)
			return handler;
	}

	return NULL;
}

static bool is_enclosing_handler(struct fw_address_handler *handler,
				 unsigned long long offset, size_t length)
{
	return handler->offset <= offset &&
		offset + length <= handler->offset + handler->length;
}

static struct fw_address_handler *lookup_enclosing_address_handler(
	struct list_head *list, unsigned long long offset, size_t length)
{
	struct fw_address_handler *handler;

	list_for_each_entry_rcu(handler, list, link) {
		if (is_enclosing_handler(handler, offset, length))
			return handler;
	}

	return NULL;
}

static DEFINE_SPINLOCK(address_handler_list_lock);
static LIST_HEAD(address_handler_list);

const struct fw_address_region fw_high_memory_region =
	{ .start = 0x000100000000ULL, .end = 0xffffe0000000ULL,  };
EXPORT_SYMBOL(fw_high_memory_region);

static const struct fw_address_region low_memory_region =
	{ .start = 0x000000000000ULL, .end = 0x000100000000ULL,  };

#if 0
const struct fw_address_region fw_private_region =
	{ .start = 0xffffe0000000ULL, .end = 0xfffff0000000ULL,  };
const struct fw_address_region fw_csr_region =
	{ .start = CSR_REGISTER_BASE,
	  .end   = CSR_REGISTER_BASE | CSR_CONFIG_ROM_END,  };
const struct fw_address_region fw_unit_space_region =
	{ .start = 0xfffff0000900ULL, .end = 0x1000000000000ULL, };
#endif  /*  0  */

static bool is_in_fcp_region(u64 offset, size_t length)
{
	return offset >= (CSR_REGISTER_BASE | CSR_FCP_COMMAND) &&
		offset + length <= (CSR_REGISTER_BASE | CSR_FCP_END);
}

/**
 * fw_core_add_address_handler() - register for incoming requests
 * @handler:	callback
 * @region:	region in the IEEE 1212 node space address range
 *
 * region->start, ->end, and handler->length have to be quadlet-aligned.
 *
 * When a request is received that falls within the specified address range,
 * the specified callback is invoked.  The parameters passed to the callback
 * give the details of the particular request.
 *
 * To be called in process context.
 * Return value:  0 on success, non-zero otherwise.
 *
 * The start offset of the handler's address region is determined by
 * fw_core_add_address_handler() and is returned in handler->offset.
 *
 * Address allocations are exclusive, except for the FCP registers.
 */
int fw_core_add_address_handler(struct fw_address_handler *handler,
				const struct fw_address_region *region)
{
	struct fw_address_handler *other;
	int ret = -EBUSY;

	if (region->start & 0xffff000000000003ULL ||
	    region->start >= region->end ||
	    region->end   > 0x0001000000000000ULL ||
	    handler->length & 3 ||
	    handler->length == 0)
		return -EINVAL;

	spin_lock(&address_handler_list_lock);

	handler->offset = region->start;
	while (handler->offset + handler->length <= region->end) {
		if (is_in_fcp_region(handler->offset, handler->length))
			other = NULL;
		else
			other = lookup_overlapping_address_handler
					(&address_handler_list,
					 handler->offset, handler->length);
		if (other != NULL) {
			handler->offset += other->length;
		} else {
			list_add_tail_rcu(&handler->link, &address_handler_list);
			ret = 0;
			break;
		}
	}

	spin_unlock(&address_handler_list_lock);

	return ret;
}
EXPORT_SYMBOL(fw_core_add_address_handler);

/**
 * fw_core_remove_address_handler() - unregister an address handler
 *
 * To be called in process context.
 *
 * When fw_core_remove_address_handler() returns, @handler->callback() is
 * guaranteed to not run on any CPU anymore.
 */
void fw_core_remove_address_handler(struct fw_address_handler *handler)
{
	spin_lock(&address_handler_list_lock);
	list_del_rcu(&handler->link);
	spin_unlock(&address_handler_list_lock);
	synchronize_rcu();
}
EXPORT_SYMBOL(fw_core_remove_address_handler);

struct fw_request {
	struct fw_packet response;
	u32 request_header[4];
	int ack;
	u32 length;
	u32 data[0];
};

static void free_response_callback(struct fw_packet *packet,
				   struct fw_card *card, int status)
{
	struct fw_request *request;

	request = container_of(packet, struct fw_request, response);
	kfree(request);
}

int fw_get_response_length(struct fw_request *r)
{
	int tcode, ext_tcode, data_length;

	tcode = HEADER_GET_TCODE(r->request_header[0]);

	switch (tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
	case TCODE_WRITE_BLOCK_REQUEST:
		return 0;

	case TCODE_READ_QUADLET_REQUEST:
		return 4;

	case TCODE_READ_BLOCK_REQUEST:
		data_length = HEADER_GET_DATA_LENGTH(r->request_header[3]);
		return data_length;

	case TCODE_LOCK_REQUEST:
		ext_tcode = HEADER_GET_EXTENDED_TCODE(r->request_header[3]);
		data_length = HEADER_GET_DATA_LENGTH(r->request_header[3]);
		switch (ext_tcode) {
		case EXTCODE_FETCH_ADD:
		case EXTCODE_LITTLE_ADD:
			return data_length;
		default:
			return data_length / 2;
		}

	default:
		WARN(1, "wrong tcode %d\n", tcode);
		return 0;
	}
}

void fw_fill_response(struct fw_packet *response, u32 *request_header,
		      int rcode, void *payload, size_t length)
{
	int tcode, tlabel, extended_tcode, source, destination;

	tcode          = HEADER_GET_TCODE(request_header[0]);
	tlabel         = HEADER_GET_TLABEL(request_header[0]);
	source         = HEADER_GET_DESTINATION(request_header[0]);
	destination    = HEADER_GET_SOURCE(request_header[1]);
	extended_tcode = HEADER_GET_EXTENDED_TCODE(request_header[3]);

	response->header[0] =
		HEADER_RETRY(RETRY_1) |
		HEADER_TLABEL(tlabel) |
		HEADER_DESTINATION(destination);
	response->header[1] =
		HEADER_SOURCE(source) |
		HEADER_RCODE(rcode);
	response->header[2] = 0;

	switch (tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
	case TCODE_WRITE_BLOCK_REQUEST:
		response->header[0] |= HEADER_TCODE(TCODE_WRITE_RESPONSE);
		response->header_length = 12;
		response->payload_length = 0;
		break;

	case TCODE_READ_QUADLET_REQUEST:
		response->header[0] |=
			HEADER_TCODE(TCODE_READ_QUADLET_RESPONSE);
		if (payload != NULL)
			response->header[3] = *(u32 *)payload;
		else
			response->header[3] = 0;
		response->header_length = 16;
		response->payload_length = 0;
		break;

	case TCODE_READ_BLOCK_REQUEST:
	case TCODE_LOCK_REQUEST:
		response->header[0] |= HEADER_TCODE(tcode + 2);
		response->header[3] =
			HEADER_DATA_LENGTH(length) |
			HEADER_EXTENDED_TCODE(extended_tcode);
		response->header_length = 16;
		response->payload = payload;
		response->payload_length = length;
		break;

	default:
		WARN(1, "wrong tcode %d\n", tcode);
	}

	response->payload_mapped = false;
}
EXPORT_SYMBOL(fw_fill_response);

static u32 compute_split_timeout_timestamp(struct fw_card *card,
					   u32 request_timestamp)
{
	unsigned int cycles;
	u32 timestamp;

	cycles = card->split_timeout_cycles;
	cycles += request_timestamp & 0x1fff;

	timestamp = request_timestamp & ~0x1fff;
	timestamp += (cycles / 8000) << 13;
	timestamp |= cycles % 8000;

	return timestamp;
}

static struct fw_request *allocate_request(struct fw_card *card,
					   struct fw_packet *p)
{
	struct fw_request *request;
	u32 *data, length;
	int request_tcode;

	request_tcode = HEADER_GET_TCODE(p->header[0]);
	switch (request_tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
		data = &p->header[3];
		length = 4;
		break;

	case TCODE_WRITE_BLOCK_REQUEST:
	case TCODE_LOCK_REQUEST:
		data = p->payload;
		length = HEADER_GET_DATA_LENGTH(p->header[3]);
		break;

	case TCODE_READ_QUADLET_REQUEST:
		data = NULL;
		length = 4;
		break;

	case TCODE_READ_BLOCK_REQUEST:
		data = NULL;
		length = HEADER_GET_DATA_LENGTH(p->header[3]);
		break;

	default:
		fw_notice(card, "ERROR - corrupt request received - %08x %08x %08x\n",
			 p->header[0], p->header[1], p->header[2]);
		return NULL;
	}

	request = kmalloc(sizeof(*request) + length, GFP_ATOMIC);
	if (request == NULL)
		return NULL;

	request->response.speed = p->speed;
	request->response.timestamp =
			compute_split_timeout_timestamp(card, p->timestamp);
	request->response.generation = p->generation;
	request->response.ack = 0;
	request->response.callback = free_response_callback;
	request->ack = p->ack;
	request->length = length;
	if (data)
		memcpy(request->data, data, length);

	memcpy(request->request_header, p->header, sizeof(p->header));

	return request;
}

void fw_send_response(struct fw_card *card,
		      struct fw_request *request, int rcode)
{
	if (WARN_ONCE(!request, "invalid for FCP address handlers"))
		return;

	/* unified transaction or broadcast transaction: don't respond */
	if (request->ack != ACK_PENDING ||
	    HEADER_DESTINATION_IS_BROADCAST(request->request_header[0])) {
		kfree(request);
		return;
	}

	if (rcode == RCODE_COMPLETE)
		fw_fill_response(&request->response, request->request_header,
				 rcode, request->data,
				 fw_get_response_length(request));
	else
		fw_fill_response(&request->response, request->request_header,
				 rcode, NULL, 0);

	card->driver->send_response(card, &request->response);
}
EXPORT_SYMBOL(fw_send_response);

/**
 * fw_get_request_speed() - returns speed at which the @request was received
 */
int fw_get_request_speed(struct fw_request *request)
{
	return request->response.speed;
}
EXPORT_SYMBOL(fw_get_request_speed);

static void handle_exclusive_region_request(struct fw_card *card,
					    struct fw_packet *p,
					    struct fw_request *request,
					    unsigned long long offset)
{
	struct fw_address_handler *handler;
	int tcode, destination, source;

	destination = HEADER_GET_DESTINATION(p->header[0]);
	source      = HEADER_GET_SOURCE(p->header[1]);
	tcode       = HEADER_GET_TCODE(p->header[0]);
	if (tcode == TCODE_LOCK_REQUEST)
		tcode = 0x10 + HEADER_GET_EXTENDED_TCODE(p->header[3]);

	rcu_read_lock();
	handler = lookup_enclosing_address_handler(&address_handler_list,
						   offset, request->length);
	if (handler)
		handler->address_callback(card, request,
					  tcode, destination, source,
					  p->generation, offset,
					  request->data, request->length,
					  handler->callback_data);
	rcu_read_unlock();

	if (!handler)
		fw_send_response(card, request, RCODE_ADDRESS_ERROR);
}

static void handle_fcp_region_request(struct fw_card *card,
				      struct fw_packet *p,
				      struct fw_request *request,
				      unsigned long long offset)
{
	struct fw_address_handler *handler;
	int tcode, destination, source;

	if ((offset != (CSR_REGISTER_BASE | CSR_FCP_COMMAND) &&
	     offset != (CSR_REGISTER_BASE | CSR_FCP_RESPONSE)) ||
	    request->length > 0x200) {
		fw_send_response(card, request, RCODE_ADDRESS_ERROR);

		return;
	}

	tcode       = HEADER_GET_TCODE(p->header[0]);
	destination = HEADER_GET_DESTINATION(p->header[0]);
	source      = HEADER_GET_SOURCE(p->header[1]);

	if (tcode != TCODE_WRITE_QUADLET_REQUEST &&
	    tcode != TCODE_WRITE_BLOCK_REQUEST) {
		fw_send_response(card, request, RCODE_TYPE_ERROR);

		return;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(handler, &address_handler_list, link) {
		if (is_enclosing_handler(handler, offset, request->length))
			handler->address_callback(card, NULL, tcode,
						  destination, source,
						  p->generation, offset,
						  request->data,
						  request->length,
						  handler->callback_data);
	}
	rcu_read_unlock();

	fw_send_response(card, request, RCODE_COMPLETE);
}

void fw_core_handle_request(struct fw_card *card, struct fw_packet *p)
{
	struct fw_request *request;
	unsigned long long offset;

	if (p->ack != ACK_PENDING && p->ack != ACK_COMPLETE)
		return;

	if (TCODE_IS_LINK_INTERNAL(HEADER_GET_TCODE(p->header[0]))) {
		fw_cdev_handle_phy_packet(card, p);
		return;
	}

	request = allocate_request(card, p);
	if (request == NULL) {
		/* FIXME: send statically allocated busy packet. */
		return;
	}

	offset = ((u64)HEADER_GET_OFFSET_HIGH(p->header[1]) << 32) |
		p->header[2];

	if (!is_in_fcp_region(offset, request->length))
		handle_exclusive_region_request(card, p, request, offset);
	else
		handle_fcp_region_request(card, p, request, offset);

}
EXPORT_SYMBOL(fw_core_handle_request);

void fw_core_handle_response(struct fw_card *card, struct fw_packet *p)
{
	struct fw_transaction *t;
	unsigned long flags;
	u32 *data;
	size_t data_length;
	int tcode, tlabel, source, rcode;

	tcode	= HEADER_GET_TCODE(p->header[0]);
	tlabel	= HEADER_GET_TLABEL(p->header[0]);
	source	= HEADER_GET_SOURCE(p->header[1]);
	rcode	= HEADER_GET_RCODE(p->header[1]);

	spin_lock_irqsave(&card->lock, flags);
	list_for_each_entry(t, &card->transaction_list, link) {
		if (t->node_id == source && t->tlabel == tlabel) {
			if (!try_cancel_split_timeout(t)) {
				spin_unlock_irqrestore(&card->lock, flags);
				goto timed_out;
			}
			list_del_init(&t->link);
			card->tlabel_mask &= ~(1ULL << t->tlabel);
			break;
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);

	if (&t->link == &card->transaction_list) {
 timed_out:
		fw_notice(card, "unsolicited response (source %x, tlabel %x)\n",
			  source, tlabel);
		return;
	}

	/*
	 * FIXME: sanity check packet, is length correct, does tcodes
	 * and addresses match.
	 */

	switch (tcode) {
	case TCODE_READ_QUADLET_RESPONSE:
		data = (u32 *) &p->header[3];
		data_length = 4;
		break;

	case TCODE_WRITE_RESPONSE:
		data = NULL;
		data_length = 0;
		break;

	case TCODE_READ_BLOCK_RESPONSE:
	case TCODE_LOCK_RESPONSE:
		data = p->payload;
		data_length = HEADER_GET_DATA_LENGTH(p->header[3]);
		break;

	default:
		/* Should never happen, this is just to shut up gcc. */
		data = NULL;
		data_length = 0;
		break;
	}

	/*
	 * The response handler may be executed while the request handler
	 * is still pending.  Cancel the request handler.
	 */
	card->driver->cancel_packet(card, &t->packet);

	t->callback(card, rcode, data, data_length, t->callback_data);
}
EXPORT_SYMBOL(fw_core_handle_response);

/**
 * fw_rcode_string - convert a firewire result code to an error description
 * @rcode: the result code
 */
const char *fw_rcode_string(int rcode)
{
	static const char *const names[] = {
		[RCODE_COMPLETE]       = "no error",
		[RCODE_CONFLICT_ERROR] = "conflict error",
		[RCODE_DATA_ERROR]     = "data error",
		[RCODE_TYPE_ERROR]     = "type error",
		[RCODE_ADDRESS_ERROR]  = "address error",
		[RCODE_SEND_ERROR]     = "send error",
		[RCODE_CANCELLED]      = "timeout",
		[RCODE_BUSY]           = "busy",
		[RCODE_GENERATION]     = "bus reset",
		[RCODE_NO_ACK]         = "no ack",
	};

	if ((unsigned int)rcode < ARRAY_SIZE(names) && names[rcode])
		return names[rcode];
	else
		return "unknown";
}
EXPORT_SYMBOL(fw_rcode_string);

static const struct fw_address_region topology_map_region =
	{ .start = CSR_REGISTER_BASE | CSR_TOPOLOGY_MAP,
	  .end   = CSR_REGISTER_BASE | CSR_TOPOLOGY_MAP_END, };

static void handle_topology_map(struct fw_card *card, struct fw_request *request,
		int tcode, int destination, int source, int generation,
		unsigned long long offset, void *payload, size_t length,
		void *callback_data)
{
	int start;

	if (!TCODE_IS_READ_REQUEST(tcode)) {
		fw_send_response(card, request, RCODE_TYPE_ERROR);
		return;
	}

	if ((offset & 3) > 0 || (length & 3) > 0) {
		fw_send_response(card, request, RCODE_ADDRESS_ERROR);
		return;
	}

	start = (offset - topology_map_region.start) / 4;
	memcpy(payload, &card->topology_map[start], length);

	fw_send_response(card, request, RCODE_COMPLETE);
}

static struct fw_address_handler topology_map = {
	.length			= 0x400,
	.address_callback	= handle_topology_map,
};

static const struct fw_address_region registers_region =
	{ .start = CSR_REGISTER_BASE,
	  .end   = CSR_REGISTER_BASE | CSR_CONFIG_ROM, };

static void update_split_timeout(struct fw_card *card)
{
	unsigned int cycles;

	cycles = card->split_timeout_hi * 8000 + (card->split_timeout_lo >> 19);

	/* minimum per IEEE 1394, maximum which doesn't overflow OHCI */
	cycles = clamp(cycles, 800u, 3u * 8000u);

	card->split_timeout_cycles = cycles;
	card->split_timeout_jiffies = DIV_ROUND_UP(cycles * HZ, 8000);
}

static void handle_registers(struct fw_card *card, struct fw_request *request,
		int tcode, int destination, int source, int generation,
		unsigned long long offset, void *payload, size_t length,
		void *callback_data)
{
	int reg = offset & ~CSR_REGISTER_BASE;
	__be32 *data = payload;
	int rcode = RCODE_COMPLETE;
	unsigned long flags;

	switch (reg) {
	case CSR_PRIORITY_BUDGET:
		if (!card->priority_budget_implemented) {
			rcode = RCODE_ADDRESS_ERROR;
			break;
		}
		/* else fall through */

	case CSR_NODE_IDS:
		/*
		 * per IEEE 1394-2008 8.3.22.3, not IEEE 1394.1-2004 3.2.8
		 * and 9.6, but interoperable with IEEE 1394.1-2004 bridges
		 */
		/* fall through */

	case CSR_STATE_CLEAR:
	case CSR_STATE_SET:
	case CSR_CYCLE_TIME:
	case CSR_BUS_TIME:
	case CSR_BUSY_TIMEOUT:
		if (tcode == TCODE_READ_QUADLET_REQUEST)
			*data = cpu_to_be32(card->driver->read_csr(card, reg));
		else if (tcode == TCODE_WRITE_QUADLET_REQUEST)
			card->driver->write_csr(card, reg, be32_to_cpu(*data));
		else
			rcode = RCODE_TYPE_ERROR;
		break;

	case CSR_RESET_START:
		if (tcode == TCODE_WRITE_QUADLET_REQUEST)
			card->driver->write_csr(card, CSR_STATE_CLEAR,
						CSR_STATE_BIT_ABDICATE);
		else
			rcode = RCODE_TYPE_ERROR;
		break;

	case CSR_SPLIT_TIMEOUT_HI:
		if (tcode == TCODE_READ_QUADLET_REQUEST) {
			*data = cpu_to_be32(card->split_timeout_hi);
		} else if (tcode == TCODE_WRITE_QUADLET_REQUEST) {
			spin_lock_irqsave(&card->lock, flags);
			card->split_timeout_hi = be32_to_cpu(*data) & 7;
			update_split_timeout(card);
			spin_unlock_irqrestore(&card->lock, flags);
		} else {
			rcode = RCODE_TYPE_ERROR;
		}
		break;

	case CSR_SPLIT_TIMEOUT_LO:
		if (tcode == TCODE_READ_QUADLET_REQUEST) {
			*data = cpu_to_be32(card->split_timeout_lo);
		} else if (tcode == TCODE_WRITE_QUADLET_REQUEST) {
			spin_lock_irqsave(&card->lock, flags);
			card->split_timeout_lo =
					be32_to_cpu(*data) & 0xfff80000;
			update_split_timeout(card);
			spin_unlock_irqrestore(&card->lock, flags);
		} else {
			rcode = RCODE_TYPE_ERROR;
		}
		break;

	case CSR_MAINT_UTILITY:
		if (tcode == TCODE_READ_QUADLET_REQUEST)
			*data = card->maint_utility_register;
		else if (tcode == TCODE_WRITE_QUADLET_REQUEST)
			card->maint_utility_register = *data;
		else
			rcode = RCODE_TYPE_ERROR;
		break;

	case CSR_BROADCAST_CHANNEL:
		if (tcode == TCODE_READ_QUADLET_REQUEST)
			*data = cpu_to_be32(card->broadcast_channel);
		else if (tcode == TCODE_WRITE_QUADLET_REQUEST)
			card->broadcast_channel =
			    (be32_to_cpu(*data) & BROADCAST_CHANNEL_VALID) |
			    BROADCAST_CHANNEL_INITIAL;
		else
			rcode = RCODE_TYPE_ERROR;
		break;

	case CSR_BUS_MANAGER_ID:
	case CSR_BANDWIDTH_AVAILABLE:
	case CSR_CHANNELS_AVAILABLE_HI:
	case CSR_CHANNELS_AVAILABLE_LO:
		/*
		 * FIXME: these are handled by the OHCI hardware and
		 * the stack never sees these request. If we add
		 * support for a new type of controller that doesn't
		 * handle this in hardware we need to deal with these
		 * transactions.
		 */
		BUG();
		break;

	default:
		rcode = RCODE_ADDRESS_ERROR;
		break;
	}

	fw_send_response(card, request, rcode);
}

static struct fw_address_handler registers = {
	.length			= 0x400,
	.address_callback	= handle_registers,
};

static void handle_low_memory(struct fw_card *card, struct fw_request *request,
		int tcode, int destination, int source, int generation,
		unsigned long long offset, void *payload, size_t length,
		void *callback_data)
{
	/*
	 * This catches requests not handled by the physical DMA unit,
	 * i.e., wrong transaction types or unauthorized source nodes.
	 */
	fw_send_response(card, request, RCODE_TYPE_ERROR);
}

static struct fw_address_handler low_memory = {
	.length			= 0x000100000000ULL,
	.address_callback	= handle_low_memory,
};

MODULE_AUTHOR("Kristian Hoegsberg <krh@bitplanet.net>");
MODULE_DESCRIPTION("Core IEEE1394 transaction logic");
MODULE_LICENSE("GPL");

static const u32 vendor_textual_descriptor[] = {
	/* textual descriptor leaf () */
	0x00060000,
	0x00000000,
	0x00000000,
	0x4c696e75,		/* L i n u */
	0x78204669,		/* x   F i */
	0x72657769,		/* r e w i */
	0x72650000,		/* r e     */
};

static const u32 model_textual_descriptor[] = {
	/* model descriptor leaf () */
	0x00030000,
	0x00000000,
	0x00000000,
	0x4a756a75,		/* J u j u */
};

static struct fw_descriptor vendor_id_descriptor = {
	.length = ARRAY_SIZE(vendor_textual_descriptor),
	.immediate = 0x03d00d1e,
	.key = 0x81000000,
	.data = vendor_textual_descriptor,
};

static struct fw_descriptor model_id_descriptor = {
	.length = ARRAY_SIZE(model_textual_descriptor),
	.immediate = 0x17000001,
	.key = 0x81000000,
	.data = model_textual_descriptor,
};

static int __init fw_core_init(void)
{
	int ret;

	fw_workqueue = alloc_workqueue("firewire", WQ_MEM_RECLAIM, 0);
	if (!fw_workqueue)
		return -ENOMEM;

	ret = bus_register(&fw_bus_type);
	if (ret < 0) {
		destroy_workqueue(fw_workqueue);
		return ret;
	}

	fw_cdev_major = register_chrdev(0, "firewire", &fw_device_ops);
	if (fw_cdev_major < 0) {
		bus_unregister(&fw_bus_type);
		destroy_workqueue(fw_workqueue);
		return fw_cdev_major;
	}

	fw_core_add_address_handler(&topology_map, &topology_map_region);
	fw_core_add_address_handler(&registers, &registers_region);
	fw_core_add_address_handler(&low_memory, &low_memory_region);
	fw_core_add_descriptor(&vendor_id_descriptor);
	fw_core_add_descriptor(&model_id_descriptor);

	return 0;
}

static void __exit fw_core_cleanup(void)
{
	unregister_chrdev(fw_cdev_major, "firewire");
	bus_unregister(&fw_bus_type);
	destroy_workqueue(fw_workqueue);
	idr_destroy(&fw_device_idr);
}

module_init(fw_core_init);
module_exit(fw_core_cleanup);
