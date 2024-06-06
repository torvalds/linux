// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Core IEEE1394 transaction logic
 *
 * Copyright (C) 2004-2006 Kristian Hoegsberg <krh@bitplanet.net>
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
#include "packet-header-definitions.h"
#include "phy-packet-definitions.h"
#include <trace/events/firewire.h>

#define HEADER_DESTINATION_IS_BROADCAST(header) \
	((async_header_get_destination(header) & 0x3f) == 0x3f)

/* returns 0 if the split timeout handler is already running */
static int try_cancel_split_timeout(struct fw_transaction *t)
{
	if (t->is_split_transaction)
		return del_timer(&t->split_timeout_timer);
	else
		return 1;
}

static int close_transaction(struct fw_transaction *transaction, struct fw_card *card, int rcode,
			     u32 response_tstamp)
{
	struct fw_transaction *t = NULL, *iter;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	list_for_each_entry(iter, &card->transaction_list, link) {
		if (iter == transaction) {
			if (!try_cancel_split_timeout(iter)) {
				spin_unlock_irqrestore(&card->lock, flags);
				goto timed_out;
			}
			list_del_init(&iter->link);
			card->tlabel_mask &= ~(1ULL << iter->tlabel);
			t = iter;
			break;
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);

	if (t) {
		if (!t->with_tstamp) {
			t->callback.without_tstamp(card, rcode, NULL, 0, t->callback_data);
		} else {
			t->callback.with_tstamp(card, rcode, t->packet.timestamp, response_tstamp,
						NULL, 0, t->callback_data);
		}
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
	u32 tstamp;

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

	if (transaction->packet.ack == 0) {
		// The timestamp is reused since it was just read now.
		tstamp = transaction->packet.timestamp;
	} else {
		u32 curr_cycle_time = 0;

		(void)fw_card_read_cycle_time(card, &curr_cycle_time);
		tstamp = cycle_time_to_ohci_tstamp(curr_cycle_time);
	}

	return close_transaction(transaction, card, RCODE_CANCELLED, tstamp);
}
EXPORT_SYMBOL(fw_cancel_transaction);

static void split_transaction_timeout_callback(struct timer_list *timer)
{
	struct fw_transaction *t = from_timer(t, timer, split_timeout_timer);
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

	if (!t->with_tstamp) {
		t->callback.without_tstamp(card, RCODE_CANCELLED, NULL, 0, t->callback_data);
	} else {
		t->callback.with_tstamp(card, RCODE_CANCELLED, t->packet.timestamp,
					t->split_timeout_cycle, NULL, 0, t->callback_data);
	}
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

static u32 compute_split_timeout_timestamp(struct fw_card *card, u32 request_timestamp);

static void transmit_complete_callback(struct fw_packet *packet,
				       struct fw_card *card, int status)
{
	struct fw_transaction *t =
	    container_of(packet, struct fw_transaction, packet);

	trace_async_request_outbound_complete((uintptr_t)t, card->index, packet->generation,
					      packet->speed, status, packet->timestamp);

	switch (status) {
	case ACK_COMPLETE:
		close_transaction(t, card, RCODE_COMPLETE, packet->timestamp);
		break;
	case ACK_PENDING:
	{
		t->split_timeout_cycle =
			compute_split_timeout_timestamp(card, packet->timestamp) & 0xffff;
		start_split_transaction_timeout(t, card);
		break;
	}
	case ACK_BUSY_X:
	case ACK_BUSY_A:
	case ACK_BUSY_B:
		close_transaction(t, card, RCODE_BUSY, packet->timestamp);
		break;
	case ACK_DATA_ERROR:
		close_transaction(t, card, RCODE_DATA_ERROR, packet->timestamp);
		break;
	case ACK_TYPE_ERROR:
		close_transaction(t, card, RCODE_TYPE_ERROR, packet->timestamp);
		break;
	default:
		/*
		 * In this case the ack is really a juju specific
		 * rcode, so just forward that to the callback.
		 */
		close_transaction(t, card, status, packet->timestamp);
		break;
	}
}

static void fw_fill_request(struct fw_packet *packet, int tcode, int tlabel,
		int destination_id, int source_id, int generation, int speed,
		unsigned long long offset, void *payload, size_t length)
{
	int ext_tcode;

	if (tcode == TCODE_STREAM_DATA) {
		// The value of destination_id argument should include tag, channel, and sy fields
		// as isochronous packet header has.
		packet->header[0] = destination_id;
		isoc_header_set_data_length(packet->header, length);
		isoc_header_set_tcode(packet->header, TCODE_STREAM_DATA);
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

	async_header_set_retry(packet->header, RETRY_X);
	async_header_set_tlabel(packet->header, tlabel);
	async_header_set_tcode(packet->header, tcode);
	async_header_set_destination(packet->header, destination_id);
	async_header_set_source(packet->header, source_id);
	async_header_set_offset(packet->header, offset);

	switch (tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
		async_header_set_quadlet_data(packet->header, *(u32 *)payload);
		packet->header_length = 16;
		packet->payload_length = 0;
		break;

	case TCODE_LOCK_REQUEST:
	case TCODE_WRITE_BLOCK_REQUEST:
		async_header_set_data_length(packet->header, length);
		async_header_set_extended_tcode(packet->header, ext_tcode);
		packet->header_length = 16;
		packet->payload = payload;
		packet->payload_length = length;
		break;

	case TCODE_READ_QUADLET_REQUEST:
		packet->header_length = 12;
		packet->payload_length = 0;
		break;

	case TCODE_READ_BLOCK_REQUEST:
		async_header_set_data_length(packet->header, length);
		async_header_set_extended_tcode(packet->header, ext_tcode);
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
 * __fw_send_request() - submit a request packet for transmission to generate callback for response
 *			 subaction with or without time stamp.
 * @card:		interface to send the request at
 * @t:			transaction instance to which the request belongs
 * @tcode:		transaction code
 * @destination_id:	destination node ID, consisting of bus_ID and phy_ID
 * @generation:		bus generation in which request and response are valid
 * @speed:		transmission speed
 * @offset:		48bit wide offset into destination's address space
 * @payload:		data payload for the request subaction
 * @length:		length of the payload, in bytes
 * @callback:		union of two functions whether to receive time stamp or not for response
 *			subaction.
 * @with_tstamp:	Whether to receive time stamp or not for response subaction.
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
void __fw_send_request(struct fw_card *card, struct fw_transaction *t, int tcode,
		int destination_id, int generation, int speed, unsigned long long offset,
		void *payload, size_t length, union fw_transaction_callback callback,
		bool with_tstamp, void *callback_data)
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
		if (!with_tstamp) {
			callback.without_tstamp(card, RCODE_SEND_ERROR, NULL, 0, callback_data);
		} else {
			// Timestamping on behalf of hardware.
			u32 curr_cycle_time = 0;
			u32 tstamp;

			(void)fw_card_read_cycle_time(card, &curr_cycle_time);
			tstamp = cycle_time_to_ohci_tstamp(curr_cycle_time);

			callback.with_tstamp(card, RCODE_SEND_ERROR, tstamp, tstamp, NULL, 0,
					     callback_data);
		}
		return;
	}

	t->node_id = destination_id;
	t->tlabel = tlabel;
	t->card = card;
	t->is_split_transaction = false;
	timer_setup(&t->split_timeout_timer, split_transaction_timeout_callback, 0);
	t->callback = callback;
	t->with_tstamp = with_tstamp;
	t->callback_data = callback_data;

	fw_fill_request(&t->packet, tcode, t->tlabel, destination_id, card->node_id, generation,
			speed, offset, payload, length);
	t->packet.callback = transmit_complete_callback;

	list_add_tail(&t->link, &card->transaction_list);

	spin_unlock_irqrestore(&card->lock, flags);

	trace_async_request_outbound_initiate((uintptr_t)t, card->index, generation, speed,
					      t->packet.header, payload,
					      tcode_is_read_request(tcode) ? 0 : length / 4);

	card->driver->send_request(card, &t->packet);
}
EXPORT_SYMBOL_GPL(__fw_send_request);

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
 * @card:		card interface for this request
 * @tcode:		transaction code
 * @destination_id:	destination node ID, consisting of bus_ID and phy_ID
 * @generation:		bus generation in which request and response are valid
 * @speed:		transmission speed
 * @offset:		48bit wide offset into destination's address space
 * @payload:		data payload for the request subaction
 * @length:		length of the payload, in bytes
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

	timer_setup_on_stack(&t.split_timeout_timer, NULL, 0);
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
	trace_async_phy_outbound_complete((uintptr_t)packet, card->index, packet->generation, status,
					  packet->timestamp);
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
	u32 data = 0;

	phy_packet_set_packet_identifier(&data, PHY_PACKET_PACKET_IDENTIFIER_PHY_CONFIG);

	if (node_id != FW_PHY_CONFIG_NO_NODE_ID) {
		phy_packet_phy_config_set_root_id(&data, node_id);
		phy_packet_phy_config_set_force_root_node(&data, true);
	}

	if (gap_count == FW_PHY_CONFIG_CURRENT_GAP_COUNT) {
		gap_count = card->driver->read_phy_reg(card, 1);
		if (gap_count < 0)
			return;

		gap_count &= 63;
		if (gap_count == 63)
			return;
	}
	phy_packet_phy_config_set_gap_count(&data, gap_count);
	phy_packet_phy_config_set_gap_count_optimization(&data, true);

	mutex_lock(&phy_config_mutex);

	phy_config_packet.header[1] = data;
	phy_config_packet.header[2] = ~data;
	phy_config_packet.generation = generation;
	reinit_completion(&phy_config_done);

	trace_async_phy_outbound_initiate((uintptr_t)&phy_config_packet, card->index,
					  phy_config_packet.generation, phy_config_packet.header[1],
					  phy_config_packet.header[2]);

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
	{ .start = FW_MAX_PHYSICAL_RANGE, .end = 0xffffe0000000ULL, };
EXPORT_SYMBOL(fw_high_memory_region);

static const struct fw_address_region low_memory_region =
	{ .start = 0x000000000000ULL, .end = FW_MAX_PHYSICAL_RANGE, };

#if 0
const struct fw_address_region fw_private_region =
	{ .start = 0xffffe0000000ULL, .end = 0xfffff0000000ULL,  };
const struct fw_address_region fw_csr_region =
	{ .start = CSR_REGISTER_BASE,
	  .end   = CSR_REGISTER_BASE | CSR_CONFIG_ROM_END,  };
const struct fw_address_region fw_unit_space_region =
	{ .start = 0xfffff0000900ULL, .end = 0x1000000000000ULL, };
#endif  /*  0  */

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
 * @handler: callback
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
	struct kref kref;
	struct fw_packet response;
	u32 request_header[ASYNC_HEADER_QUADLET_COUNT];
	int ack;
	u32 timestamp;
	u32 length;
	u32 data[];
};

void fw_request_get(struct fw_request *request)
{
	kref_get(&request->kref);
}

static void release_request(struct kref *kref)
{
	struct fw_request *request = container_of(kref, struct fw_request, kref);

	kfree(request);
}

void fw_request_put(struct fw_request *request)
{
	kref_put(&request->kref, release_request);
}

static void free_response_callback(struct fw_packet *packet,
				   struct fw_card *card, int status)
{
	struct fw_request *request = container_of(packet, struct fw_request, response);

	trace_async_response_outbound_complete((uintptr_t)request, card->index, packet->generation,
					       packet->speed, status, packet->timestamp);

	// Decrease the reference count since not at in-flight.
	fw_request_put(request);

	// Decrease the reference count to release the object.
	fw_request_put(request);
}

int fw_get_response_length(struct fw_request *r)
{
	int tcode, ext_tcode, data_length;

	tcode = async_header_get_tcode(r->request_header);

	switch (tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
	case TCODE_WRITE_BLOCK_REQUEST:
		return 0;

	case TCODE_READ_QUADLET_REQUEST:
		return 4;

	case TCODE_READ_BLOCK_REQUEST:
		data_length = async_header_get_data_length(r->request_header);
		return data_length;

	case TCODE_LOCK_REQUEST:
		ext_tcode = async_header_get_extended_tcode(r->request_header);
		data_length = async_header_get_data_length(r->request_header);
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

	tcode = async_header_get_tcode(request_header);
	tlabel = async_header_get_tlabel(request_header);
	source = async_header_get_destination(request_header); // Exchange.
	destination = async_header_get_source(request_header); // Exchange.
	extended_tcode = async_header_get_extended_tcode(request_header);

	async_header_set_retry(response->header, RETRY_1);
	async_header_set_tlabel(response->header, tlabel);
	async_header_set_destination(response->header, destination);
	async_header_set_source(response->header, source);
	async_header_set_rcode(response->header, rcode);
	response->header[2] = 0;	// The field is reserved.

	switch (tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
	case TCODE_WRITE_BLOCK_REQUEST:
		async_header_set_tcode(response->header, TCODE_WRITE_RESPONSE);
		response->header_length = 12;
		response->payload_length = 0;
		break;

	case TCODE_READ_QUADLET_REQUEST:
		async_header_set_tcode(response->header, TCODE_READ_QUADLET_RESPONSE);
		if (payload != NULL)
			async_header_set_quadlet_data(response->header, *(u32 *)payload);
		else
			async_header_set_quadlet_data(response->header, 0);
		response->header_length = 16;
		response->payload_length = 0;
		break;

	case TCODE_READ_BLOCK_REQUEST:
	case TCODE_LOCK_REQUEST:
		async_header_set_tcode(response->header, tcode + 2);
		async_header_set_data_length(response->header, length);
		async_header_set_extended_tcode(response->header, extended_tcode);
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

	request_tcode = async_header_get_tcode(p->header);
	switch (request_tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
		data = &p->header[3];
		length = 4;
		break;

	case TCODE_WRITE_BLOCK_REQUEST:
	case TCODE_LOCK_REQUEST:
		data = p->payload;
		length = async_header_get_data_length(p->header);
		break;

	case TCODE_READ_QUADLET_REQUEST:
		data = NULL;
		length = 4;
		break;

	case TCODE_READ_BLOCK_REQUEST:
		data = NULL;
		length = async_header_get_data_length(p->header);
		break;

	default:
		fw_notice(card, "ERROR - corrupt request received - %08x %08x %08x\n",
			 p->header[0], p->header[1], p->header[2]);
		return NULL;
	}

	request = kmalloc(sizeof(*request) + length, GFP_ATOMIC);
	if (request == NULL)
		return NULL;
	kref_init(&request->kref);

	request->response.speed = p->speed;
	request->response.timestamp =
			compute_split_timeout_timestamp(card, p->timestamp);
	request->response.generation = p->generation;
	request->response.ack = 0;
	request->response.callback = free_response_callback;
	request->ack = p->ack;
	request->timestamp = p->timestamp;
	request->length = length;
	if (data)
		memcpy(request->data, data, length);

	memcpy(request->request_header, p->header, sizeof(p->header));

	return request;
}

/**
 * fw_send_response: - send response packet for asynchronous transaction.
 * @card:	interface to send the response at.
 * @request:	firewire request data for the transaction.
 * @rcode:	response code to send.
 *
 * Submit a response packet into the asynchronous response transmission queue. The @request
 * is going to be released when the transmission successfully finishes later.
 */
void fw_send_response(struct fw_card *card,
		      struct fw_request *request, int rcode)
{
	u32 *data = NULL;
	unsigned int data_length = 0;

	/* unified transaction or broadcast transaction: don't respond */
	if (request->ack != ACK_PENDING ||
	    HEADER_DESTINATION_IS_BROADCAST(request->request_header)) {
		fw_request_put(request);
		return;
	}

	if (rcode == RCODE_COMPLETE) {
		data = request->data;
		data_length = fw_get_response_length(request);
	}

	fw_fill_response(&request->response, request->request_header, rcode, data, data_length);

	// Increase the reference count so that the object is kept during in-flight.
	fw_request_get(request);

	trace_async_response_outbound_initiate((uintptr_t)request, card->index,
					       request->response.generation, request->response.speed,
					       request->response.header, data,
					       data ? data_length / 4 : 0);

	card->driver->send_response(card, &request->response);
}
EXPORT_SYMBOL(fw_send_response);

/**
 * fw_get_request_speed() - returns speed at which the @request was received
 * @request: firewire request data
 */
int fw_get_request_speed(struct fw_request *request)
{
	return request->response.speed;
}
EXPORT_SYMBOL(fw_get_request_speed);

/**
 * fw_request_get_timestamp: Get timestamp of the request.
 * @request: The opaque pointer to request structure.
 *
 * Get timestamp when 1394 OHCI controller receives the asynchronous request subaction. The
 * timestamp consists of the low order 3 bits of second field and the full 13 bits of count
 * field of isochronous cycle time register.
 *
 * Returns: timestamp of the request.
 */
u32 fw_request_get_timestamp(const struct fw_request *request)
{
	return request->timestamp;
}
EXPORT_SYMBOL_GPL(fw_request_get_timestamp);

static void handle_exclusive_region_request(struct fw_card *card,
					    struct fw_packet *p,
					    struct fw_request *request,
					    unsigned long long offset)
{
	struct fw_address_handler *handler;
	int tcode, destination, source;

	destination = async_header_get_destination(p->header);
	source = async_header_get_source(p->header);
	tcode = async_header_get_tcode(p->header);
	if (tcode == TCODE_LOCK_REQUEST)
		tcode = 0x10 + async_header_get_extended_tcode(p->header);

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

	tcode = async_header_get_tcode(p->header);
	destination = async_header_get_destination(p->header);
	source = async_header_get_source(p->header);

	if (tcode != TCODE_WRITE_QUADLET_REQUEST &&
	    tcode != TCODE_WRITE_BLOCK_REQUEST) {
		fw_send_response(card, request, RCODE_TYPE_ERROR);

		return;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(handler, &address_handler_list, link) {
		if (is_enclosing_handler(handler, offset, request->length))
			handler->address_callback(card, request, tcode,
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
	unsigned int tcode;

	if (p->ack != ACK_PENDING && p->ack != ACK_COMPLETE)
		return;

	tcode = async_header_get_tcode(p->header);
	if (tcode_is_link_internal(tcode)) {
		trace_async_phy_inbound((uintptr_t)p, card->index, p->generation, p->ack, p->timestamp,
					 p->header[1], p->header[2]);
		fw_cdev_handle_phy_packet(card, p);
		return;
	}

	request = allocate_request(card, p);
	if (request == NULL) {
		/* FIXME: send statically allocated busy packet. */
		return;
	}

	trace_async_request_inbound((uintptr_t)request, card->index, p->generation, p->speed,
				    p->ack, p->timestamp, p->header, request->data,
				    tcode_is_read_request(tcode) ? 0 : request->length / 4);

	offset = async_header_get_offset(p->header);

	if (!is_in_fcp_region(offset, request->length))
		handle_exclusive_region_request(card, p, request, offset);
	else
		handle_fcp_region_request(card, p, request, offset);

}
EXPORT_SYMBOL(fw_core_handle_request);

void fw_core_handle_response(struct fw_card *card, struct fw_packet *p)
{
	struct fw_transaction *t = NULL, *iter;
	unsigned long flags;
	u32 *data;
	size_t data_length;
	int tcode, tlabel, source, rcode;

	tcode = async_header_get_tcode(p->header);
	tlabel = async_header_get_tlabel(p->header);
	source = async_header_get_source(p->header);
	rcode = async_header_get_rcode(p->header);

	// FIXME: sanity check packet, is length correct, does tcodes
	// and addresses match to the transaction request queried later.
	//
	// For the tracepoints event, let us decode the header here against the concern.

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
		data_length = async_header_get_data_length(p->header);
		break;

	default:
		/* Should never happen, this is just to shut up gcc. */
		data = NULL;
		data_length = 0;
		break;
	}

	spin_lock_irqsave(&card->lock, flags);
	list_for_each_entry(iter, &card->transaction_list, link) {
		if (iter->node_id == source && iter->tlabel == tlabel) {
			if (!try_cancel_split_timeout(iter)) {
				spin_unlock_irqrestore(&card->lock, flags);
				goto timed_out;
			}
			list_del_init(&iter->link);
			card->tlabel_mask &= ~(1ULL << iter->tlabel);
			t = iter;
			break;
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);

	trace_async_response_inbound((uintptr_t)t, card->index, p->generation, p->speed, p->ack,
				     p->timestamp, p->header, data, data_length / 4);

	if (!t) {
 timed_out:
		fw_notice(card, "unsolicited response (source %x, tlabel %x)\n",
			  source, tlabel);
		return;
	}

	/*
	 * The response handler may be executed while the request handler
	 * is still pending.  Cancel the request handler.
	 */
	card->driver->cancel_packet(card, &t->packet);

	if (!t->with_tstamp) {
		t->callback.without_tstamp(card, rcode, data, data_length, t->callback_data);
	} else {
		t->callback.with_tstamp(card, rcode, t->packet.timestamp, p->timestamp, data,
					data_length, t->callback_data);
	}
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

	if (!tcode_is_read_request(tcode)) {
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
		fallthrough;

	case CSR_NODE_IDS:
		/*
		 * per IEEE 1394-2008 8.3.22.3, not IEEE 1394.1-2004 3.2.8
		 * and 9.6, but interoperable with IEEE 1394.1-2004 bridges
		 */
		fallthrough;

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
	.length			= FW_MAX_PHYSICAL_RANGE,
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
	.immediate = 0x03001f11,
	.key = 0x81000000,
	.data = vendor_textual_descriptor,
};

static struct fw_descriptor model_id_descriptor = {
	.length = ARRAY_SIZE(model_textual_descriptor),
	.immediate = 0x17023901,
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
