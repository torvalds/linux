/*
 * IEEE 1394 for Linux
 *
 * Transaction support.
 *
 * Copyright (C) 1999 Andreas E. Bombe
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/hardirq.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/sched.h>  /* because linux/wait.h is broken if CONFIG_SMP=n */
#include <linux/wait.h>

#include <asm/bug.h>
#include <asm/errno.h>
#include <asm/system.h>

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "ieee1394_transactions.h"

#define PREP_ASYNC_HEAD_ADDRESS(tc) \
        packet->tcode = tc; \
        packet->header[0] = (packet->node_id << 16) | (packet->tlabel << 10) \
                | (1 << 8) | (tc << 4); \
        packet->header[1] = (packet->host->node_id << 16) | (addr >> 32); \
        packet->header[2] = addr & 0xffffffff

#ifndef HPSB_DEBUG_TLABELS
static
#endif
DEFINE_SPINLOCK(hpsb_tlabel_lock);

static DECLARE_WAIT_QUEUE_HEAD(tlabel_wq);

static void fill_async_readquad(struct hpsb_packet *packet, u64 addr)
{
	PREP_ASYNC_HEAD_ADDRESS(TCODE_READQ);
	packet->header_size = 12;
	packet->data_size = 0;
	packet->expect_response = 1;
}

static void fill_async_readblock(struct hpsb_packet *packet, u64 addr,
				 int length)
{
	PREP_ASYNC_HEAD_ADDRESS(TCODE_READB);
	packet->header[3] = length << 16;
	packet->header_size = 16;
	packet->data_size = 0;
	packet->expect_response = 1;
}

static void fill_async_writequad(struct hpsb_packet *packet, u64 addr,
				 quadlet_t data)
{
	PREP_ASYNC_HEAD_ADDRESS(TCODE_WRITEQ);
	packet->header[3] = data;
	packet->header_size = 16;
	packet->data_size = 0;
	packet->expect_response = 1;
}

static void fill_async_writeblock(struct hpsb_packet *packet, u64 addr,
				  int length)
{
	PREP_ASYNC_HEAD_ADDRESS(TCODE_WRITEB);
	packet->header[3] = length << 16;
	packet->header_size = 16;
	packet->expect_response = 1;
	packet->data_size = length + (length % 4 ? 4 - (length % 4) : 0);
}

static void fill_async_lock(struct hpsb_packet *packet, u64 addr, int extcode,
			    int length)
{
	PREP_ASYNC_HEAD_ADDRESS(TCODE_LOCK_REQUEST);
	packet->header[3] = (length << 16) | extcode;
	packet->header_size = 16;
	packet->data_size = length;
	packet->expect_response = 1;
}

static void fill_iso_packet(struct hpsb_packet *packet, int length, int channel,
			    int tag, int sync)
{
	packet->header[0] = (length << 16) | (tag << 14) | (channel << 8)
	    | (TCODE_ISO_DATA << 4) | sync;

	packet->header_size = 4;
	packet->data_size = length;
	packet->type = hpsb_iso;
	packet->tcode = TCODE_ISO_DATA;
}

static void fill_phy_packet(struct hpsb_packet *packet, quadlet_t data)
{
	packet->header[0] = data;
	packet->header[1] = ~data;
	packet->header_size = 8;
	packet->data_size = 0;
	packet->expect_response = 0;
	packet->type = hpsb_raw;	/* No CRC added */
	packet->speed_code = IEEE1394_SPEED_100;	/* Force speed to be 100Mbps */
}

static void fill_async_stream_packet(struct hpsb_packet *packet, int length,
				     int channel, int tag, int sync)
{
	packet->header[0] = (length << 16) | (tag << 14) | (channel << 8)
	    | (TCODE_STREAM_DATA << 4) | sync;

	packet->header_size = 4;
	packet->data_size = length;
	packet->type = hpsb_async;
	packet->tcode = TCODE_ISO_DATA;
}

/* same as hpsb_get_tlabel, except that it returns immediately */
static int hpsb_get_tlabel_atomic(struct hpsb_packet *packet)
{
	unsigned long flags, *tp;
	u8 *next;
	int tlabel, n = NODEID_TO_NODE(packet->node_id);

	/* Broadcast transactions are complete once the request has been sent.
	 * Use the same transaction label for all broadcast transactions. */
	if (unlikely(n == ALL_NODES)) {
		packet->tlabel = 0;
		return 0;
	}
	tp = packet->host->tl_pool[n].map;
	next = &packet->host->next_tl[n];

	spin_lock_irqsave(&hpsb_tlabel_lock, flags);
	tlabel = find_next_zero_bit(tp, 64, *next);
	if (tlabel > 63)
		tlabel = find_first_zero_bit(tp, 64);
	if (tlabel > 63) {
		spin_unlock_irqrestore(&hpsb_tlabel_lock, flags);
		return -EAGAIN;
	}
	__set_bit(tlabel, tp);
	*next = (tlabel + 1) & 63;
	spin_unlock_irqrestore(&hpsb_tlabel_lock, flags);

	packet->tlabel = tlabel;
	return 0;
}

/**
 * hpsb_get_tlabel - allocate a transaction label
 * @packet: the packet whose tlabel and tl_pool we set
 *
 * Every asynchronous transaction on the 1394 bus needs a transaction
 * label to match the response to the request.  This label has to be
 * different from any other transaction label in an outstanding request to
 * the same node to make matching possible without ambiguity.
 *
 * There are 64 different tlabels, so an allocated tlabel has to be freed
 * with hpsb_free_tlabel() after the transaction is complete (unless it's
 * reused again for the same target node).
 *
 * Return value: Zero on success, otherwise non-zero. A non-zero return
 * generally means there are no available tlabels. If this is called out
 * of interrupt or atomic context, then it will sleep until can return a
 * tlabel or a signal is received.
 */
int hpsb_get_tlabel(struct hpsb_packet *packet)
{
	if (irqs_disabled() || in_atomic())
		return hpsb_get_tlabel_atomic(packet);

	/* NB: The macro wait_event_interruptible() is called with a condition
	 * argument with side effect.  This is only possible because the side
	 * effect does not occur until the condition became true, and
	 * wait_event_interruptible() won't evaluate the condition again after
	 * that. */
	return wait_event_interruptible(tlabel_wq,
					!hpsb_get_tlabel_atomic(packet));
}

/**
 * hpsb_free_tlabel - free an allocated transaction label
 * @packet: packet whose tlabel and tl_pool needs to be cleared
 *
 * Frees the transaction label allocated with hpsb_get_tlabel().  The
 * tlabel has to be freed after the transaction is complete (i.e. response
 * was received for a split transaction or packet was sent for a unified
 * transaction).
 *
 * A tlabel must not be freed twice.
 */
void hpsb_free_tlabel(struct hpsb_packet *packet)
{
	unsigned long flags, *tp;
	int tlabel, n = NODEID_TO_NODE(packet->node_id);

	if (unlikely(n == ALL_NODES))
		return;
	tp = packet->host->tl_pool[n].map;
	tlabel = packet->tlabel;
	BUG_ON(tlabel > 63 || tlabel < 0);

	spin_lock_irqsave(&hpsb_tlabel_lock, flags);
	BUG_ON(!__test_and_clear_bit(tlabel, tp));
	spin_unlock_irqrestore(&hpsb_tlabel_lock, flags);

	wake_up_interruptible(&tlabel_wq);
}

/**
 * hpsb_packet_success - Make sense of the ack and reply codes
 *
 * Make sense of the ack and reply codes and return more convenient error codes:
 * 0 = success.  -%EBUSY = node is busy, try again.  -%EAGAIN = error which can
 * probably resolved by retry.  -%EREMOTEIO = node suffers from an internal
 * error.  -%EACCES = this transaction is not allowed on requested address.
 * -%EINVAL = invalid address at node.
 */
int hpsb_packet_success(struct hpsb_packet *packet)
{
	switch (packet->ack_code) {
	case ACK_PENDING:
		switch ((packet->header[1] >> 12) & 0xf) {
		case RCODE_COMPLETE:
			return 0;
		case RCODE_CONFLICT_ERROR:
			return -EAGAIN;
		case RCODE_DATA_ERROR:
			return -EREMOTEIO;
		case RCODE_TYPE_ERROR:
			return -EACCES;
		case RCODE_ADDRESS_ERROR:
			return -EINVAL;
		default:
			HPSB_ERR("received reserved rcode %d from node %d",
				 (packet->header[1] >> 12) & 0xf,
				 packet->node_id);
			return -EAGAIN;
		}
		BUG();

	case ACK_BUSY_X:
	case ACK_BUSY_A:
	case ACK_BUSY_B:
		return -EBUSY;

	case ACK_TYPE_ERROR:
		return -EACCES;

	case ACK_COMPLETE:
		if (packet->tcode == TCODE_WRITEQ
		    || packet->tcode == TCODE_WRITEB) {
			return 0;
		} else {
			HPSB_ERR("impossible ack_complete from node %d "
				 "(tcode %d)", packet->node_id, packet->tcode);
			return -EAGAIN;
		}

	case ACK_DATA_ERROR:
		if (packet->tcode == TCODE_WRITEB
		    || packet->tcode == TCODE_LOCK_REQUEST) {
			return -EAGAIN;
		} else {
			HPSB_ERR("impossible ack_data_error from node %d "
				 "(tcode %d)", packet->node_id, packet->tcode);
			return -EAGAIN;
		}

	case ACK_ADDRESS_ERROR:
		return -EINVAL;

	case ACK_TARDY:
	case ACK_CONFLICT_ERROR:
	case ACKX_NONE:
	case ACKX_SEND_ERROR:
	case ACKX_ABORTED:
	case ACKX_TIMEOUT:
		/* error while sending */
		return -EAGAIN;

	default:
		HPSB_ERR("got invalid ack %d from node %d (tcode %d)",
			 packet->ack_code, packet->node_id, packet->tcode);
		return -EAGAIN;
	}
	BUG();
}

struct hpsb_packet *hpsb_make_readpacket(struct hpsb_host *host, nodeid_t node,
					 u64 addr, size_t length)
{
	struct hpsb_packet *packet;

	if (length == 0)
		return NULL;

	packet = hpsb_alloc_packet(length);
	if (!packet)
		return NULL;

	packet->host = host;
	packet->node_id = node;

	if (hpsb_get_tlabel(packet)) {
		hpsb_free_packet(packet);
		return NULL;
	}

	if (length == 4)
		fill_async_readquad(packet, addr);
	else
		fill_async_readblock(packet, addr, length);

	return packet;
}

struct hpsb_packet *hpsb_make_writepacket(struct hpsb_host *host, nodeid_t node,
					  u64 addr, quadlet_t * buffer,
					  size_t length)
{
	struct hpsb_packet *packet;

	if (length == 0)
		return NULL;

	packet = hpsb_alloc_packet(length);
	if (!packet)
		return NULL;

	if (length % 4) {	/* zero padding bytes */
		packet->data[length >> 2] = 0;
	}
	packet->host = host;
	packet->node_id = node;

	if (hpsb_get_tlabel(packet)) {
		hpsb_free_packet(packet);
		return NULL;
	}

	if (length == 4) {
		fill_async_writequad(packet, addr, buffer ? *buffer : 0);
	} else {
		fill_async_writeblock(packet, addr, length);
		if (buffer)
			memcpy(packet->data, buffer, length);
	}

	return packet;
}

struct hpsb_packet *hpsb_make_streampacket(struct hpsb_host *host, u8 * buffer,
					   int length, int channel, int tag,
					   int sync)
{
	struct hpsb_packet *packet;

	if (length == 0)
		return NULL;

	packet = hpsb_alloc_packet(length);
	if (!packet)
		return NULL;

	if (length % 4) {	/* zero padding bytes */
		packet->data[length >> 2] = 0;
	}
	packet->host = host;

	/* Because it is too difficult to determine all PHY speeds and link
	 * speeds here, we use S100... */
	packet->speed_code = IEEE1394_SPEED_100;

	/* ...and prevent hpsb_send_packet() from overriding it. */
	packet->node_id = LOCAL_BUS | ALL_NODES;

	if (hpsb_get_tlabel(packet)) {
		hpsb_free_packet(packet);
		return NULL;
	}

	fill_async_stream_packet(packet, length, channel, tag, sync);
	if (buffer)
		memcpy(packet->data, buffer, length);

	return packet;
}

struct hpsb_packet *hpsb_make_lockpacket(struct hpsb_host *host, nodeid_t node,
					 u64 addr, int extcode,
					 quadlet_t * data, quadlet_t arg)
{
	struct hpsb_packet *p;
	u32 length;

	p = hpsb_alloc_packet(8);
	if (!p)
		return NULL;

	p->host = host;
	p->node_id = node;
	if (hpsb_get_tlabel(p)) {
		hpsb_free_packet(p);
		return NULL;
	}

	switch (extcode) {
	case EXTCODE_FETCH_ADD:
	case EXTCODE_LITTLE_ADD:
		length = 4;
		if (data)
			p->data[0] = *data;
		break;
	default:
		length = 8;
		if (data) {
			p->data[0] = arg;
			p->data[1] = *data;
		}
		break;
	}
	fill_async_lock(p, addr, extcode, length);

	return p;
}

struct hpsb_packet *hpsb_make_lock64packet(struct hpsb_host *host,
					   nodeid_t node, u64 addr, int extcode,
					   octlet_t * data, octlet_t arg)
{
	struct hpsb_packet *p;
	u32 length;

	p = hpsb_alloc_packet(16);
	if (!p)
		return NULL;

	p->host = host;
	p->node_id = node;
	if (hpsb_get_tlabel(p)) {
		hpsb_free_packet(p);
		return NULL;
	}

	switch (extcode) {
	case EXTCODE_FETCH_ADD:
	case EXTCODE_LITTLE_ADD:
		length = 8;
		if (data) {
			p->data[0] = *data >> 32;
			p->data[1] = *data & 0xffffffff;
		}
		break;
	default:
		length = 16;
		if (data) {
			p->data[0] = arg >> 32;
			p->data[1] = arg & 0xffffffff;
			p->data[2] = *data >> 32;
			p->data[3] = *data & 0xffffffff;
		}
		break;
	}
	fill_async_lock(p, addr, extcode, length);

	return p;
}

struct hpsb_packet *hpsb_make_phypacket(struct hpsb_host *host, quadlet_t data)
{
	struct hpsb_packet *p;

	p = hpsb_alloc_packet(0);
	if (!p)
		return NULL;

	p->host = host;
	fill_phy_packet(p, data);

	return p;
}

struct hpsb_packet *hpsb_make_isopacket(struct hpsb_host *host,
					int length, int channel,
					int tag, int sync)
{
	struct hpsb_packet *p;

	p = hpsb_alloc_packet(length);
	if (!p)
		return NULL;

	p->host = host;
	fill_iso_packet(p, length, channel, tag, sync);

	p->generation = get_hpsb_generation(host);

	return p;
}

/*
 * FIXME - these functions should probably read from / write to user space to
 * avoid in kernel buffers for user space callers
 */

/**
 * hpsb_read - generic read function
 *
 * Recognizes the local node ID and act accordingly.  Automatically uses a
 * quadlet read request if @length == 4 and and a block read request otherwise.
 * It does not yet support lengths that are not a multiple of 4.
 *
 * You must explicitly specifiy the @generation for which the node ID is valid,
 * to avoid sending packets to the wrong nodes when we race with a bus reset.
 */
int hpsb_read(struct hpsb_host *host, nodeid_t node, unsigned int generation,
	      u64 addr, quadlet_t * buffer, size_t length)
{
	struct hpsb_packet *packet;
	int retval = 0;

	if (length == 0)
		return -EINVAL;

	BUG_ON(in_interrupt());	// We can't be called in an interrupt, yet

	packet = hpsb_make_readpacket(host, node, addr, length);

	if (!packet) {
		return -ENOMEM;
	}

	packet->generation = generation;
	retval = hpsb_send_packet_and_wait(packet);
	if (retval < 0)
		goto hpsb_read_fail;

	retval = hpsb_packet_success(packet);

	if (retval == 0) {
		if (length == 4) {
			*buffer = packet->header[3];
		} else {
			memcpy(buffer, packet->data, length);
		}
	}

      hpsb_read_fail:
	hpsb_free_tlabel(packet);
	hpsb_free_packet(packet);

	return retval;
}

/**
 * hpsb_write - generic write function
 *
 * Recognizes the local node ID and act accordingly.  Automatically uses a
 * quadlet write request if @length == 4 and and a block write request
 * otherwise.  It does not yet support lengths that are not a multiple of 4.
 *
 * You must explicitly specifiy the @generation for which the node ID is valid,
 * to avoid sending packets to the wrong nodes when we race with a bus reset.
 */
int hpsb_write(struct hpsb_host *host, nodeid_t node, unsigned int generation,
	       u64 addr, quadlet_t * buffer, size_t length)
{
	struct hpsb_packet *packet;
	int retval;

	if (length == 0)
		return -EINVAL;

	BUG_ON(in_interrupt());	// We can't be called in an interrupt, yet

	packet = hpsb_make_writepacket(host, node, addr, buffer, length);

	if (!packet)
		return -ENOMEM;

	packet->generation = generation;
	retval = hpsb_send_packet_and_wait(packet);
	if (retval < 0)
		goto hpsb_write_fail;

	retval = hpsb_packet_success(packet);

      hpsb_write_fail:
	hpsb_free_tlabel(packet);
	hpsb_free_packet(packet);

	return retval;
}

#if 0

int hpsb_lock(struct hpsb_host *host, nodeid_t node, unsigned int generation,
	      u64 addr, int extcode, quadlet_t * data, quadlet_t arg)
{
	struct hpsb_packet *packet;
	int retval = 0;

	BUG_ON(in_interrupt());	// We can't be called in an interrupt, yet

	packet = hpsb_make_lockpacket(host, node, addr, extcode, data, arg);
	if (!packet)
		return -ENOMEM;

	packet->generation = generation;
	retval = hpsb_send_packet_and_wait(packet);
	if (retval < 0)
		goto hpsb_lock_fail;

	retval = hpsb_packet_success(packet);

	if (retval == 0) {
		*data = packet->data[0];
	}

      hpsb_lock_fail:
	hpsb_free_tlabel(packet);
	hpsb_free_packet(packet);

	return retval;
}

int hpsb_send_gasp(struct hpsb_host *host, int channel, unsigned int generation,
		   quadlet_t * buffer, size_t length, u32 specifier_id,
		   unsigned int version)
{
	struct hpsb_packet *packet;
	int retval = 0;
	u16 specifier_id_hi = (specifier_id & 0x00ffff00) >> 8;
	u8 specifier_id_lo = specifier_id & 0xff;

	HPSB_VERBOSE("Send GASP: channel = %d, length = %Zd", channel, length);

	length += 8;

	packet = hpsb_make_streampacket(host, NULL, length, channel, 3, 0);
	if (!packet)
		return -ENOMEM;

	packet->data[0] = cpu_to_be32((host->node_id << 16) | specifier_id_hi);
	packet->data[1] =
	    cpu_to_be32((specifier_id_lo << 24) | (version & 0x00ffffff));

	memcpy(&(packet->data[2]), buffer, length - 8);

	packet->generation = generation;

	packet->no_waiter = 1;

	retval = hpsb_send_packet(packet);
	if (retval < 0)
		hpsb_free_packet(packet);

	return retval;
}

#endif				/*  0  */
