/*
 * IEEE 1394 for Linux
 *
 * Core support: hpsb_packet management, packet handling and forwarding to
 *               highlevel or lowlevel code
 *
 * Copyright (C) 1999, 2000 Andreas E. Bombe
 *                     2002 Manfred Weihs <weihs@ict.tuwien.ac.at>
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 *
 *
 * Contributions:
 *
 * Manfred Weihs <weihs@ict.tuwien.ac.at>
 *        loopback functionality in hpsb_send_packet
 *        allow highlevel drivers to disable automatic response generation
 *              and to generate responses themselves (deferred)
 *
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/bitops.h>
#include <linux/kdev_t.h>
#include <linux/skbuff.h>
#include <linux/suspend.h>
#include <linux/kthread.h>

#include <asm/byteorder.h>

#include "ieee1394_types.h"
#include "ieee1394.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"
#include "ieee1394_transactions.h"
#include "csr.h"
#include "nodemgr.h"
#include "dma.h"
#include "iso.h"
#include "config_roms.h"

/*
 * Disable the nodemgr detection and config rom reading functionality.
 */
static int disable_nodemgr;
module_param(disable_nodemgr, int, 0444);
MODULE_PARM_DESC(disable_nodemgr, "Disable nodemgr functionality.");

/* Disable Isochronous Resource Manager functionality */
int hpsb_disable_irm = 0;
module_param_named(disable_irm, hpsb_disable_irm, bool, 0444);
MODULE_PARM_DESC(disable_irm,
		 "Disable Isochronous Resource Manager functionality.");

/* We are GPL, so treat us special */
MODULE_LICENSE("GPL");

/* Some globals used */
const char *hpsb_speedto_str[] = { "S100", "S200", "S400", "S800", "S1600", "S3200" };
struct class *hpsb_protocol_class;

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
static void dump_packet(const char *text, quadlet_t *data, int size, int speed)
{
	int i;

	size /= 4;
	size = (size > 4 ? 4 : size);

	printk(KERN_DEBUG "ieee1394: %s", text);
	if (speed > -1 && speed < 6)
		printk(" at %s", hpsb_speedto_str[speed]);
	printk(":");
	for (i = 0; i < size; i++)
		printk(" %08x", data[i]);
	printk("\n");
}
#else
#define dump_packet(a,b,c,d) do {} while (0)
#endif

static void abort_requests(struct hpsb_host *host);
static void queue_packet_complete(struct hpsb_packet *packet);


/**
 * hpsb_set_packet_complete_task - set the task that runs when a packet
 * completes. You cannot call this more than once on a single packet
 * before it is sent.
 *
 * @packet: the packet whose completion we want the task added to
 * @routine: function to call
 * @data: data (if any) to pass to the above function
 */
void hpsb_set_packet_complete_task(struct hpsb_packet *packet,
				   void (*routine)(void *), void *data)
{
	WARN_ON(packet->complete_routine != NULL);
	packet->complete_routine = routine;
	packet->complete_data = data;
	return;
}

/**
 * hpsb_alloc_packet - allocate new packet structure
 * @data_size: size of the data block to be allocated
 *
 * This function allocates, initializes and returns a new &struct hpsb_packet.
 * It can be used in interrupt context.  A header block is always included, its
 * size is big enough to contain all possible 1394 headers.  The data block is
 * only allocated when @data_size is not zero.
 *
 * For packets for which responses will be received the @data_size has to be big
 * enough to contain the response's data block since no further allocation
 * occurs at response matching time.
 *
 * The packet's generation value will be set to the current generation number
 * for ease of use.  Remember to overwrite it with your own recorded generation
 * number if you can not be sure that your code will not race with a bus reset.
 *
 * Return value: A pointer to a &struct hpsb_packet or NULL on allocation
 * failure.
 */
struct hpsb_packet *hpsb_alloc_packet(size_t data_size)
{
	struct hpsb_packet *packet = NULL;
	struct sk_buff *skb;

	data_size = ((data_size + 3) & ~3);

	skb = alloc_skb(data_size + sizeof(*packet), GFP_ATOMIC);
	if (skb == NULL)
		return NULL;

	memset(skb->data, 0, data_size + sizeof(*packet));

	packet = (struct hpsb_packet *)skb->data;
	packet->skb = skb;

	packet->header = packet->embedded_header;
	packet->state = hpsb_unused;
	packet->generation = -1;
	INIT_LIST_HEAD(&packet->driver_list);
	atomic_set(&packet->refcnt, 1);

	if (data_size) {
		packet->data = (quadlet_t *)(skb->data + sizeof(*packet));
		packet->data_size = data_size;
	}

	return packet;
}


/**
 * hpsb_free_packet - free packet and data associated with it
 * @packet: packet to free (is NULL safe)
 *
 * This function will free packet->data and finally the packet itself.
 */
void hpsb_free_packet(struct hpsb_packet *packet)
{
	if (packet && atomic_dec_and_test(&packet->refcnt)) {
		BUG_ON(!list_empty(&packet->driver_list));
		kfree_skb(packet->skb);
	}
}


int hpsb_reset_bus(struct hpsb_host *host, int type)
{
	if (!host->in_bus_reset) {
		host->driver->devctl(host, RESET_BUS, type);
		return 0;
	} else {
		return 1;
	}
}


int hpsb_bus_reset(struct hpsb_host *host)
{
	if (host->in_bus_reset) {
		HPSB_NOTICE("%s called while bus reset already in progress",
			    __FUNCTION__);
		return 1;
	}

	abort_requests(host);
	host->in_bus_reset = 1;
	host->irm_id = -1;
	host->is_irm = 0;
	host->busmgr_id = -1;
	host->is_busmgr = 0;
	host->is_cycmst = 0;
	host->node_count = 0;
	host->selfid_count = 0;

	return 0;
}


/*
 * Verify num_of_selfids SelfIDs and return number of nodes.  Return zero in
 * case verification failed.
 */
static int check_selfids(struct hpsb_host *host)
{
	int nodeid = -1;
	int rest_of_selfids = host->selfid_count;
	struct selfid *sid = (struct selfid *)host->topology_map;
	struct ext_selfid *esid;
	int esid_seq = 23;

	host->nodes_active = 0;

	while (rest_of_selfids--) {
		if (!sid->extended) {
			nodeid++;
			esid_seq = 0;

			if (sid->phy_id != nodeid) {
				HPSB_INFO("SelfIDs failed monotony check with "
					  "%d", sid->phy_id);
				return 0;
			}

			if (sid->link_active) {
				host->nodes_active++;
				if (sid->contender)
					host->irm_id = LOCAL_BUS | sid->phy_id;
			}
		} else {
			esid = (struct ext_selfid *)sid;

			if ((esid->phy_id != nodeid)
			    || (esid->seq_nr != esid_seq)) {
				HPSB_INFO("SelfIDs failed monotony check with "
					  "%d/%d", esid->phy_id, esid->seq_nr);
				return 0;
			}
			esid_seq++;
		}
		sid++;
	}

	esid = (struct ext_selfid *)(sid - 1);
	while (esid->extended) {
		if ((esid->porta == SELFID_PORT_PARENT) ||
		    (esid->portb == SELFID_PORT_PARENT) ||
		    (esid->portc == SELFID_PORT_PARENT) ||
		    (esid->portd == SELFID_PORT_PARENT) ||
		    (esid->porte == SELFID_PORT_PARENT) ||
		    (esid->portf == SELFID_PORT_PARENT) ||
		    (esid->portg == SELFID_PORT_PARENT) ||
		    (esid->porth == SELFID_PORT_PARENT)) {
			HPSB_INFO("SelfIDs failed root check on "
				  "extended SelfID");
			return 0;
		}
		esid--;
	}

	sid = (struct selfid *)esid;
	if ((sid->port0 == SELFID_PORT_PARENT) ||
	    (sid->port1 == SELFID_PORT_PARENT) ||
	    (sid->port2 == SELFID_PORT_PARENT)) {
		HPSB_INFO("SelfIDs failed root check");
		return 0;
	}

	host->node_count = nodeid + 1;
	return 1;
}

static void build_speed_map(struct hpsb_host *host, int nodecount)
{
	u8 cldcnt[nodecount];
	u8 *map = host->speed_map;
	u8 *speedcap = host->speed;
	struct selfid *sid;
	struct ext_selfid *esid;
	int i, j, n;

	for (i = 0; i < (nodecount * 64); i += 64) {
		for (j = 0; j < nodecount; j++) {
			map[i+j] = IEEE1394_SPEED_MAX;
		}
	}

	for (i = 0; i < nodecount; i++) {
		cldcnt[i] = 0;
	}

	/* find direct children count and speed */
	for (sid = (struct selfid *)&host->topology_map[host->selfid_count-1],
		     n = nodecount - 1;
	     (void *)sid >= (void *)host->topology_map; sid--) {
		if (sid->extended) {
			esid = (struct ext_selfid *)sid;

			if (esid->porta == SELFID_PORT_CHILD) cldcnt[n]++;
			if (esid->portb == SELFID_PORT_CHILD) cldcnt[n]++;
			if (esid->portc == SELFID_PORT_CHILD) cldcnt[n]++;
			if (esid->portd == SELFID_PORT_CHILD) cldcnt[n]++;
			if (esid->porte == SELFID_PORT_CHILD) cldcnt[n]++;
			if (esid->portf == SELFID_PORT_CHILD) cldcnt[n]++;
			if (esid->portg == SELFID_PORT_CHILD) cldcnt[n]++;
			if (esid->porth == SELFID_PORT_CHILD) cldcnt[n]++;
                } else {
			if (sid->port0 == SELFID_PORT_CHILD) cldcnt[n]++;
			if (sid->port1 == SELFID_PORT_CHILD) cldcnt[n]++;
			if (sid->port2 == SELFID_PORT_CHILD) cldcnt[n]++;

			speedcap[n] = sid->speed;
			n--;
		}
	}

	/* set self mapping */
	for (i = 0; i < nodecount; i++) {
		map[64*i + i] = speedcap[i];
	}

	/* fix up direct children count to total children count;
	 * also fix up speedcaps for sibling and parent communication */
	for (i = 1; i < nodecount; i++) {
		for (j = cldcnt[i], n = i - 1; j > 0; j--) {
			cldcnt[i] += cldcnt[n];
			speedcap[n] = min(speedcap[n], speedcap[i]);
			n -= cldcnt[n] + 1;
		}
	}

	for (n = 0; n < nodecount; n++) {
		for (i = n - cldcnt[n]; i <= n; i++) {
			for (j = 0; j < (n - cldcnt[n]); j++) {
				map[j*64 + i] = map[i*64 + j] =
					min(map[i*64 + j], speedcap[n]);
			}
			for (j = n + 1; j < nodecount; j++) {
				map[j*64 + i] = map[i*64 + j] =
					min(map[i*64 + j], speedcap[n]);
			}
		}
	}

#if SELFID_SPEED_UNKNOWN != IEEE1394_SPEED_MAX
	/* assume maximum speed for 1394b PHYs, nodemgr will correct it */
	for (n = 0; n < nodecount; n++)
		if (speedcap[n] == SELFID_SPEED_UNKNOWN)
			speedcap[n] = IEEE1394_SPEED_MAX;
#endif
}


void hpsb_selfid_received(struct hpsb_host *host, quadlet_t sid)
{
	if (host->in_bus_reset) {
		HPSB_VERBOSE("Including SelfID 0x%x", sid);
		host->topology_map[host->selfid_count++] = sid;
	} else {
		HPSB_NOTICE("Spurious SelfID packet (0x%08x) received from bus %d",
			    sid, NODEID_TO_BUS(host->node_id));
	}
}

void hpsb_selfid_complete(struct hpsb_host *host, int phyid, int isroot)
{
	if (!host->in_bus_reset)
		HPSB_NOTICE("SelfID completion called outside of bus reset!");

	host->node_id = LOCAL_BUS | phyid;
	host->is_root = isroot;

	if (!check_selfids(host)) {
		if (host->reset_retries++ < 20) {
			/* selfid stage did not complete without error */
			HPSB_NOTICE("Error in SelfID stage, resetting");
			host->in_bus_reset = 0;
			/* this should work from ohci1394 now... */
			hpsb_reset_bus(host, LONG_RESET);
			return;
		} else {
			HPSB_NOTICE("Stopping out-of-control reset loop");
			HPSB_NOTICE("Warning - topology map and speed map will not be valid");
			host->reset_retries = 0;
		}
	} else {
		host->reset_retries = 0;
		build_speed_map(host, host->node_count);
	}

	HPSB_VERBOSE("selfid_complete called with successful SelfID stage "
		     "... irm_id: 0x%X node_id: 0x%X",host->irm_id,host->node_id);

	/* irm_id is kept up to date by check_selfids() */
	if (host->irm_id == host->node_id) {
		host->is_irm = 1;
	} else {
		host->is_busmgr = 0;
		host->is_irm = 0;
	}

	if (isroot) {
		host->driver->devctl(host, ACT_CYCLE_MASTER, 1);
		host->is_cycmst = 1;
	}
	atomic_inc(&host->generation);
	host->in_bus_reset = 0;
	highlevel_host_reset(host);
}


void hpsb_packet_sent(struct hpsb_host *host, struct hpsb_packet *packet,
		      int ackcode)
{
	unsigned long flags;

	spin_lock_irqsave(&host->pending_packet_queue.lock, flags);

	packet->ack_code = ackcode;

	if (packet->no_waiter || packet->state == hpsb_complete) {
		/* if packet->no_waiter, must not have a tlabel allocated */
		spin_unlock_irqrestore(&host->pending_packet_queue.lock, flags);
		hpsb_free_packet(packet);
		return;
	}

	atomic_dec(&packet->refcnt);	/* drop HC's reference */
	/* here the packet must be on the host->pending_packet_queue */

	if (ackcode != ACK_PENDING || !packet->expect_response) {
		packet->state = hpsb_complete;
		__skb_unlink(packet->skb, &host->pending_packet_queue);
		spin_unlock_irqrestore(&host->pending_packet_queue.lock, flags);
		queue_packet_complete(packet);
		return;
	}

	packet->state = hpsb_pending;
	packet->sendtime = jiffies;

	spin_unlock_irqrestore(&host->pending_packet_queue.lock, flags);

	mod_timer(&host->timeout, jiffies + host->timeout_interval);
}

/**
 * hpsb_send_phy_config - transmit a PHY configuration packet on the bus
 * @host: host that PHY config packet gets sent through
 * @rootid: root whose force_root bit should get set (-1 = don't set force_root)
 * @gapcnt: gap count value to set (-1 = don't set gap count)
 *
 * This function sends a PHY config packet on the bus through the specified host.
 *
 * Return value: 0 for success or error number otherwise.
 */
int hpsb_send_phy_config(struct hpsb_host *host, int rootid, int gapcnt)
{
	struct hpsb_packet *packet;
	quadlet_t d = 0;
	int retval = 0;

	if (rootid >= ALL_NODES || rootid < -1 || gapcnt > 0x3f || gapcnt < -1 ||
	   (rootid == -1 && gapcnt == -1)) {
		HPSB_DEBUG("Invalid Parameter: rootid = %d   gapcnt = %d",
			   rootid, gapcnt);
		return -EINVAL;
	}

	if (rootid != -1)
		d |= PHYPACKET_PHYCONFIG_R | rootid << PHYPACKET_PORT_SHIFT;
	if (gapcnt != -1)
		d |= PHYPACKET_PHYCONFIG_T | gapcnt << PHYPACKET_GAPCOUNT_SHIFT;

	packet = hpsb_make_phypacket(host, d);
	if (!packet)
		return -ENOMEM;

	packet->generation = get_hpsb_generation(host);
	retval = hpsb_send_packet_and_wait(packet);
	hpsb_free_packet(packet);

	return retval;
}

/**
 * hpsb_send_packet - transmit a packet on the bus
 * @packet: packet to send
 *
 * The packet is sent through the host specified in the packet->host field.
 * Before sending, the packet's transmit speed is automatically determined
 * using the local speed map when it is an async, non-broadcast packet.
 *
 * Possibilities for failure are that host is either not initialized, in bus
 * reset, the packet's generation number doesn't match the current generation
 * number or the host reports a transmit error.
 *
 * Return value: 0 on success, negative errno on failure.
 */
int hpsb_send_packet(struct hpsb_packet *packet)
{
	struct hpsb_host *host = packet->host;

	if (host->is_shutdown)
		return -EINVAL;
	if (host->in_bus_reset ||
	    (packet->generation != get_hpsb_generation(host)))
		return -EAGAIN;

	packet->state = hpsb_queued;

	/* This just seems silly to me */
	WARN_ON(packet->no_waiter && packet->expect_response);

	if (!packet->no_waiter || packet->expect_response) {
		atomic_inc(&packet->refcnt);
		/* Set the initial "sendtime" to 10 seconds from now, to
		   prevent premature expiry.  If a packet takes more than
		   10 seconds to hit the wire, we have bigger problems :) */
		packet->sendtime = jiffies + 10 * HZ;
		skb_queue_tail(&host->pending_packet_queue, packet->skb);
	}

	if (packet->node_id == host->node_id) {
		/* it is a local request, so handle it locally */

		quadlet_t *data;
		size_t size = packet->data_size + packet->header_size;

		data = kmalloc(size, GFP_ATOMIC);
		if (!data) {
			HPSB_ERR("unable to allocate memory for concatenating header and data");
			return -ENOMEM;
		}

		memcpy(data, packet->header, packet->header_size);

		if (packet->data_size)
			memcpy(((u8*)data) + packet->header_size, packet->data, packet->data_size);

		dump_packet("send packet local", packet->header, packet->header_size, -1);

		hpsb_packet_sent(host, packet, packet->expect_response ? ACK_PENDING : ACK_COMPLETE);
		hpsb_packet_received(host, data, size, 0);

		kfree(data);

		return 0;
	}

	if (packet->type == hpsb_async &&
	    NODEID_TO_NODE(packet->node_id) != ALL_NODES)
		packet->speed_code =
			host->speed[NODEID_TO_NODE(packet->node_id)];

	dump_packet("send packet", packet->header, packet->header_size, packet->speed_code);

	return host->driver->transmit_packet(host, packet);
}

/* We could just use complete() directly as the packet complete
 * callback, but this is more typesafe, in the sense that we get a
 * compiler error if the prototype for complete() changes. */

static void complete_packet(void *data)
{
	complete((struct completion *) data);
}

int hpsb_send_packet_and_wait(struct hpsb_packet *packet)
{
	struct completion done;
	int retval;

	init_completion(&done);
	hpsb_set_packet_complete_task(packet, complete_packet, &done);
	retval = hpsb_send_packet(packet);
	if (retval == 0)
		wait_for_completion(&done);

	return retval;
}

static void send_packet_nocare(struct hpsb_packet *packet)
{
	if (hpsb_send_packet(packet) < 0) {
		hpsb_free_packet(packet);
	}
}


static void handle_packet_response(struct hpsb_host *host, int tcode,
				   quadlet_t *data, size_t size)
{
	struct hpsb_packet *packet = NULL;
	struct sk_buff *skb;
	int tcode_match = 0;
	int tlabel;
	unsigned long flags;

	tlabel = (data[0] >> 10) & 0x3f;

	spin_lock_irqsave(&host->pending_packet_queue.lock, flags);

	skb_queue_walk(&host->pending_packet_queue, skb) {
		packet = (struct hpsb_packet *)skb->data;
		if ((packet->tlabel == tlabel)
		    && (packet->node_id == (data[1] >> 16))){
			break;
		}

		packet = NULL;
	}

	if (packet == NULL) {
		HPSB_DEBUG("unsolicited response packet received - no tlabel match");
		dump_packet("contents", data, 16, -1);
		spin_unlock_irqrestore(&host->pending_packet_queue.lock, flags);
		return;
	}

	switch (packet->tcode) {
	case TCODE_WRITEQ:
	case TCODE_WRITEB:
		if (tcode != TCODE_WRITE_RESPONSE)
			break;
		tcode_match = 1;
		memcpy(packet->header, data, 12);
		break;
	case TCODE_READQ:
		if (tcode != TCODE_READQ_RESPONSE)
			break;
		tcode_match = 1;
		memcpy(packet->header, data, 16);
		break;
	case TCODE_READB:
		if (tcode != TCODE_READB_RESPONSE)
			break;
		tcode_match = 1;
		BUG_ON(packet->skb->len - sizeof(*packet) < size - 16);
		memcpy(packet->header, data, 16);
		memcpy(packet->data, data + 4, size - 16);
		break;
	case TCODE_LOCK_REQUEST:
		if (tcode != TCODE_LOCK_RESPONSE)
			break;
		tcode_match = 1;
		size = min((size - 16), (size_t)8);
		BUG_ON(packet->skb->len - sizeof(*packet) < size);
		memcpy(packet->header, data, 16);
		memcpy(packet->data, data + 4, size);
		break;
	}

	if (!tcode_match) {
		spin_unlock_irqrestore(&host->pending_packet_queue.lock, flags);
		HPSB_INFO("unsolicited response packet received - tcode mismatch");
		dump_packet("contents", data, 16, -1);
		return;
	}

	__skb_unlink(skb, &host->pending_packet_queue);

	if (packet->state == hpsb_queued) {
		packet->sendtime = jiffies;
		packet->ack_code = ACK_PENDING;
	}

	packet->state = hpsb_complete;
	spin_unlock_irqrestore(&host->pending_packet_queue.lock, flags);

	queue_packet_complete(packet);
}


static struct hpsb_packet *create_reply_packet(struct hpsb_host *host,
					       quadlet_t *data, size_t dsize)
{
	struct hpsb_packet *p;

	p = hpsb_alloc_packet(dsize);
	if (unlikely(p == NULL)) {
		/* FIXME - send data_error response */
		return NULL;
	}

	p->type = hpsb_async;
	p->state = hpsb_unused;
	p->host = host;
	p->node_id = data[1] >> 16;
	p->tlabel = (data[0] >> 10) & 0x3f;
	p->no_waiter = 1;

	p->generation = get_hpsb_generation(host);

	if (dsize % 4)
		p->data[dsize / 4] = 0;

	return p;
}

#define PREP_ASYNC_HEAD_RCODE(tc) \
	packet->tcode = tc; \
	packet->header[0] = (packet->node_id << 16) | (packet->tlabel << 10) \
		| (1 << 8) | (tc << 4); \
	packet->header[1] = (packet->host->node_id << 16) | (rcode << 12); \
	packet->header[2] = 0

static void fill_async_readquad_resp(struct hpsb_packet *packet, int rcode,
			      quadlet_t data)
{
	PREP_ASYNC_HEAD_RCODE(TCODE_READQ_RESPONSE);
	packet->header[3] = data;
	packet->header_size = 16;
	packet->data_size = 0;
}

static void fill_async_readblock_resp(struct hpsb_packet *packet, int rcode,
			       int length)
{
	if (rcode != RCODE_COMPLETE)
		length = 0;

	PREP_ASYNC_HEAD_RCODE(TCODE_READB_RESPONSE);
	packet->header[3] = length << 16;
	packet->header_size = 16;
	packet->data_size = length + (length % 4 ? 4 - (length % 4) : 0);
}

static void fill_async_write_resp(struct hpsb_packet *packet, int rcode)
{
	PREP_ASYNC_HEAD_RCODE(TCODE_WRITE_RESPONSE);
	packet->header[2] = 0;
	packet->header_size = 12;
	packet->data_size = 0;
}

static void fill_async_lock_resp(struct hpsb_packet *packet, int rcode, int extcode,
			  int length)
{
	if (rcode != RCODE_COMPLETE)
		length = 0;

	PREP_ASYNC_HEAD_RCODE(TCODE_LOCK_RESPONSE);
	packet->header[3] = (length << 16) | extcode;
	packet->header_size = 16;
	packet->data_size = length;
}

#define PREP_REPLY_PACKET(length) \
		packet = create_reply_packet(host, data, length); \
		if (packet == NULL) break

static void handle_incoming_packet(struct hpsb_host *host, int tcode,
				   quadlet_t *data, size_t size, int write_acked)
{
	struct hpsb_packet *packet;
	int length, rcode, extcode;
	quadlet_t buffer;
	nodeid_t source = data[1] >> 16;
	nodeid_t dest = data[0] >> 16;
	u16 flags = (u16) data[0];
	u64 addr;

	/* big FIXME - no error checking is done for an out of bounds length */

	switch (tcode) {
	case TCODE_WRITEQ:
		addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];
		rcode = highlevel_write(host, source, dest, data+3,
					addr, 4, flags);

		if (!write_acked
		    && (NODEID_TO_NODE(data[0] >> 16) != NODE_MASK)
		    && (rcode >= 0)) {
			/* not a broadcast write, reply */
			PREP_REPLY_PACKET(0);
			fill_async_write_resp(packet, rcode);
			send_packet_nocare(packet);
		}
		break;

	case TCODE_WRITEB:
		addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];
		rcode = highlevel_write(host, source, dest, data+4,
					addr, data[3]>>16, flags);

		if (!write_acked
		    && (NODEID_TO_NODE(data[0] >> 16) != NODE_MASK)
		    && (rcode >= 0)) {
			/* not a broadcast write, reply */
			PREP_REPLY_PACKET(0);
			fill_async_write_resp(packet, rcode);
			send_packet_nocare(packet);
		}
		break;

	case TCODE_READQ:
		addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];
		rcode = highlevel_read(host, source, &buffer, addr, 4, flags);

		if (rcode >= 0) {
			PREP_REPLY_PACKET(0);
			fill_async_readquad_resp(packet, rcode, buffer);
			send_packet_nocare(packet);
		}
		break;

	case TCODE_READB:
		length = data[3] >> 16;
		PREP_REPLY_PACKET(length);

		addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];
		rcode = highlevel_read(host, source, packet->data, addr,
				       length, flags);

		if (rcode >= 0) {
			fill_async_readblock_resp(packet, rcode, length);
			send_packet_nocare(packet);
		} else {
			hpsb_free_packet(packet);
		}
		break;

	case TCODE_LOCK_REQUEST:
		length = data[3] >> 16;
		extcode = data[3] & 0xffff;
		addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];

		PREP_REPLY_PACKET(8);

		if ((extcode == 0) || (extcode >= 7)) {
			/* let switch default handle error */
			length = 0;
		}

		switch (length) {
		case 4:
			rcode = highlevel_lock(host, source, packet->data, addr,
					       data[4], 0, extcode,flags);
			fill_async_lock_resp(packet, rcode, extcode, 4);
			break;
		case 8:
			if ((extcode != EXTCODE_FETCH_ADD)
			    && (extcode != EXTCODE_LITTLE_ADD)) {
				rcode = highlevel_lock(host, source,
						       packet->data, addr,
						       data[5], data[4],
						       extcode, flags);
				fill_async_lock_resp(packet, rcode, extcode, 4);
			} else {
				rcode = highlevel_lock64(host, source,
					     (octlet_t *)packet->data, addr,
					     *(octlet_t *)(data + 4), 0ULL,
					     extcode, flags);
				fill_async_lock_resp(packet, rcode, extcode, 8);
			}
			break;
		case 16:
			rcode = highlevel_lock64(host, source,
						 (octlet_t *)packet->data, addr,
						 *(octlet_t *)(data + 6),
						 *(octlet_t *)(data + 4),
						 extcode, flags);
			fill_async_lock_resp(packet, rcode, extcode, 8);
			break;
		default:
			rcode = RCODE_TYPE_ERROR;
			fill_async_lock_resp(packet, rcode,
					     extcode, 0);
		}

		if (rcode >= 0) {
			send_packet_nocare(packet);
		} else {
			hpsb_free_packet(packet);
		}
		break;
	}

}
#undef PREP_REPLY_PACKET


void hpsb_packet_received(struct hpsb_host *host, quadlet_t *data, size_t size,
			  int write_acked)
{
	int tcode;

	if (host->in_bus_reset) {
		HPSB_INFO("received packet during reset; ignoring");
		return;
	}

	dump_packet("received packet", data, size, -1);

	tcode = (data[0] >> 4) & 0xf;

	switch (tcode) {
	case TCODE_WRITE_RESPONSE:
	case TCODE_READQ_RESPONSE:
	case TCODE_READB_RESPONSE:
	case TCODE_LOCK_RESPONSE:
		handle_packet_response(host, tcode, data, size);
		break;

	case TCODE_WRITEQ:
	case TCODE_WRITEB:
	case TCODE_READQ:
	case TCODE_READB:
	case TCODE_LOCK_REQUEST:
		handle_incoming_packet(host, tcode, data, size, write_acked);
		break;


	case TCODE_ISO_DATA:
		highlevel_iso_receive(host, data, size);
		break;

	case TCODE_CYCLE_START:
		/* simply ignore this packet if it is passed on */
		break;

	default:
		HPSB_NOTICE("received packet with bogus transaction code %d",
			    tcode);
		break;
	}
}


static void abort_requests(struct hpsb_host *host)
{
	struct hpsb_packet *packet;
	struct sk_buff *skb;

	host->driver->devctl(host, CANCEL_REQUESTS, 0);

	while ((skb = skb_dequeue(&host->pending_packet_queue)) != NULL) {
		packet = (struct hpsb_packet *)skb->data;

		packet->state = hpsb_complete;
		packet->ack_code = ACKX_ABORTED;
		queue_packet_complete(packet);
	}
}

void abort_timedouts(unsigned long __opaque)
{
	struct hpsb_host *host = (struct hpsb_host *)__opaque;
	unsigned long flags;
	struct hpsb_packet *packet;
	struct sk_buff *skb;
	unsigned long expire;

	spin_lock_irqsave(&host->csr.lock, flags);
	expire = host->csr.expire;
	spin_unlock_irqrestore(&host->csr.lock, flags);

	/* Hold the lock around this, since we aren't dequeuing all
	 * packets, just ones we need. */
	spin_lock_irqsave(&host->pending_packet_queue.lock, flags);

	while (!skb_queue_empty(&host->pending_packet_queue)) {
		skb = skb_peek(&host->pending_packet_queue);

		packet = (struct hpsb_packet *)skb->data;

		if (time_before(packet->sendtime + expire, jiffies)) {
			__skb_unlink(skb, &host->pending_packet_queue);
			packet->state = hpsb_complete;
			packet->ack_code = ACKX_TIMEOUT;
			queue_packet_complete(packet);
		} else {
			/* Since packets are added to the tail, the oldest
			 * ones are first, always. When we get to one that
			 * isn't timed out, the rest aren't either. */
			break;
		}
	}

	if (!skb_queue_empty(&host->pending_packet_queue))
		mod_timer(&host->timeout, jiffies + host->timeout_interval);

	spin_unlock_irqrestore(&host->pending_packet_queue.lock, flags);
}


/* Kernel thread and vars, which handles packets that are completed. Only
 * packets that have a "complete" function are sent here. This way, the
 * completion is run out of kernel context, and doesn't block the rest of
 * the stack. */
static struct task_struct *khpsbpkt_thread;
static struct sk_buff_head hpsbpkt_queue;

static void queue_packet_complete(struct hpsb_packet *packet)
{
	if (packet->no_waiter) {
		hpsb_free_packet(packet);
		return;
	}
	if (packet->complete_routine != NULL) {
		skb_queue_tail(&hpsbpkt_queue, packet->skb);
		wake_up_process(khpsbpkt_thread);
	}
	return;
}

static int hpsbpkt_thread(void *__hi)
{
	struct sk_buff *skb;
	struct hpsb_packet *packet;
	void (*complete_routine)(void*);
	void *complete_data;

	current->flags |= PF_NOFREEZE;

	while (!kthread_should_stop()) {
		while ((skb = skb_dequeue(&hpsbpkt_queue)) != NULL) {
			packet = (struct hpsb_packet *)skb->data;

			complete_routine = packet->complete_routine;
			complete_data = packet->complete_data;

			packet->complete_routine = packet->complete_data = NULL;

			complete_routine(complete_data);
		}

		set_current_state(TASK_INTERRUPTIBLE);
		if (!skb_peek(&hpsbpkt_queue))
			schedule();
		__set_current_state(TASK_RUNNING);
	}
	return 0;
}

static int __init ieee1394_init(void)
{
	int i, ret;

	skb_queue_head_init(&hpsbpkt_queue);

	/* non-fatal error */
	if (hpsb_init_config_roms()) {
		HPSB_ERR("Failed to initialize some config rom entries.\n");
		HPSB_ERR("Some features may not be available\n");
	}

	khpsbpkt_thread = kthread_run(hpsbpkt_thread, NULL, "khpsbpkt");
	if (IS_ERR(khpsbpkt_thread)) {
		HPSB_ERR("Failed to start hpsbpkt thread!\n");
		ret = PTR_ERR(khpsbpkt_thread);
		goto exit_cleanup_config_roms;
	}

	if (register_chrdev_region(IEEE1394_CORE_DEV, 256, "ieee1394")) {
		HPSB_ERR("unable to register character device major %d!\n", IEEE1394_MAJOR);
		ret = -ENODEV;
		goto exit_release_kernel_thread;
	}

	ret = bus_register(&ieee1394_bus_type);
	if (ret < 0) {
		HPSB_INFO("bus register failed");
		goto release_chrdev;
	}

	for (i = 0; fw_bus_attrs[i]; i++) {
		ret = bus_create_file(&ieee1394_bus_type, fw_bus_attrs[i]);
		if (ret < 0) {
			while (i >= 0) {
				bus_remove_file(&ieee1394_bus_type,
						fw_bus_attrs[i--]);
			}
			bus_unregister(&ieee1394_bus_type);
			goto release_chrdev;
		}
	}

	ret = class_register(&hpsb_host_class);
	if (ret < 0)
		goto release_all_bus;

	hpsb_protocol_class = class_create(THIS_MODULE, "ieee1394_protocol");
	if (IS_ERR(hpsb_protocol_class)) {
		ret = PTR_ERR(hpsb_protocol_class);
		goto release_class_host;
	}

	ret = init_csr();
	if (ret) {
		HPSB_INFO("init csr failed");
		ret = -ENOMEM;
		goto release_class_protocol;
	}

	if (disable_nodemgr) {
		HPSB_INFO("nodemgr and IRM functionality disabled");
		/* We shouldn't contend for IRM with nodemgr disabled, since
		   nodemgr implements functionality required of ieee1394a-2000
		   IRMs */
		hpsb_disable_irm = 1;

		return 0;
	}

	if (hpsb_disable_irm) {
		HPSB_INFO("IRM functionality disabled");
	}

	ret = init_ieee1394_nodemgr();
	if (ret < 0) {
		HPSB_INFO("init nodemgr failed");
		goto cleanup_csr;
	}

	return 0;

cleanup_csr:
	cleanup_csr();
release_class_protocol:
	class_destroy(hpsb_protocol_class);
release_class_host:
	class_unregister(&hpsb_host_class);
release_all_bus:
	for (i = 0; fw_bus_attrs[i]; i++)
		bus_remove_file(&ieee1394_bus_type, fw_bus_attrs[i]);
	bus_unregister(&ieee1394_bus_type);
release_chrdev:
	unregister_chrdev_region(IEEE1394_CORE_DEV, 256);
exit_release_kernel_thread:
	kthread_stop(khpsbpkt_thread);
exit_cleanup_config_roms:
	hpsb_cleanup_config_roms();
	return ret;
}

static void __exit ieee1394_cleanup(void)
{
	int i;

	if (!disable_nodemgr)
		cleanup_ieee1394_nodemgr();

	cleanup_csr();

	class_destroy(hpsb_protocol_class);
	class_unregister(&hpsb_host_class);
	for (i = 0; fw_bus_attrs[i]; i++)
		bus_remove_file(&ieee1394_bus_type, fw_bus_attrs[i]);
	bus_unregister(&ieee1394_bus_type);

	kthread_stop(khpsbpkt_thread);

	hpsb_cleanup_config_roms();

	unregister_chrdev_region(IEEE1394_CORE_DEV, 256);
}

fs_initcall(ieee1394_init); /* same as ohci1394 */
module_exit(ieee1394_cleanup);

/* Exported symbols */

/** hosts.c **/
EXPORT_SYMBOL(hpsb_alloc_host);
EXPORT_SYMBOL(hpsb_add_host);
EXPORT_SYMBOL(hpsb_remove_host);
EXPORT_SYMBOL(hpsb_update_config_rom_image);

/** ieee1394_core.c **/
EXPORT_SYMBOL(hpsb_speedto_str);
EXPORT_SYMBOL(hpsb_protocol_class);
EXPORT_SYMBOL(hpsb_set_packet_complete_task);
EXPORT_SYMBOL(hpsb_alloc_packet);
EXPORT_SYMBOL(hpsb_free_packet);
EXPORT_SYMBOL(hpsb_send_packet);
EXPORT_SYMBOL(hpsb_reset_bus);
EXPORT_SYMBOL(hpsb_bus_reset);
EXPORT_SYMBOL(hpsb_selfid_received);
EXPORT_SYMBOL(hpsb_selfid_complete);
EXPORT_SYMBOL(hpsb_packet_sent);
EXPORT_SYMBOL(hpsb_packet_received);
EXPORT_SYMBOL_GPL(hpsb_disable_irm);
#ifdef CONFIG_IEEE1394_EXPORT_FULL_API
EXPORT_SYMBOL(hpsb_send_phy_config);
EXPORT_SYMBOL(hpsb_send_packet_and_wait);
#endif

/** ieee1394_transactions.c **/
EXPORT_SYMBOL(hpsb_get_tlabel);
EXPORT_SYMBOL(hpsb_free_tlabel);
EXPORT_SYMBOL(hpsb_make_readpacket);
EXPORT_SYMBOL(hpsb_make_writepacket);
EXPORT_SYMBOL(hpsb_make_streampacket);
EXPORT_SYMBOL(hpsb_make_lockpacket);
EXPORT_SYMBOL(hpsb_make_lock64packet);
EXPORT_SYMBOL(hpsb_make_phypacket);
EXPORT_SYMBOL(hpsb_make_isopacket);
EXPORT_SYMBOL(hpsb_read);
EXPORT_SYMBOL(hpsb_write);
EXPORT_SYMBOL(hpsb_packet_success);

/** highlevel.c **/
EXPORT_SYMBOL(hpsb_register_highlevel);
EXPORT_SYMBOL(hpsb_unregister_highlevel);
EXPORT_SYMBOL(hpsb_register_addrspace);
EXPORT_SYMBOL(hpsb_unregister_addrspace);
EXPORT_SYMBOL(hpsb_allocate_and_register_addrspace);
EXPORT_SYMBOL(hpsb_listen_channel);
EXPORT_SYMBOL(hpsb_unlisten_channel);
EXPORT_SYMBOL(hpsb_get_hostinfo);
EXPORT_SYMBOL(hpsb_create_hostinfo);
EXPORT_SYMBOL(hpsb_destroy_hostinfo);
EXPORT_SYMBOL(hpsb_set_hostinfo_key);
EXPORT_SYMBOL(hpsb_get_hostinfo_bykey);
EXPORT_SYMBOL(hpsb_set_hostinfo);
EXPORT_SYMBOL(highlevel_host_reset);
#ifdef CONFIG_IEEE1394_EXPORT_FULL_API
EXPORT_SYMBOL(highlevel_add_host);
EXPORT_SYMBOL(highlevel_remove_host);
#endif

/** nodemgr.c **/
EXPORT_SYMBOL(hpsb_node_fill_packet);
EXPORT_SYMBOL(hpsb_node_write);
EXPORT_SYMBOL(__hpsb_register_protocol);
EXPORT_SYMBOL(hpsb_unregister_protocol);
#ifdef CONFIG_IEEE1394_EXPORT_FULL_API
EXPORT_SYMBOL(ieee1394_bus_type);
EXPORT_SYMBOL(nodemgr_for_each_host);
#endif

/** csr.c **/
EXPORT_SYMBOL(hpsb_update_config_rom);

/** dma.c **/
EXPORT_SYMBOL(dma_prog_region_init);
EXPORT_SYMBOL(dma_prog_region_alloc);
EXPORT_SYMBOL(dma_prog_region_free);
EXPORT_SYMBOL(dma_region_init);
EXPORT_SYMBOL(dma_region_alloc);
EXPORT_SYMBOL(dma_region_free);
EXPORT_SYMBOL(dma_region_sync_for_cpu);
EXPORT_SYMBOL(dma_region_sync_for_device);
EXPORT_SYMBOL(dma_region_mmap);
EXPORT_SYMBOL(dma_region_offset_to_bus);

/** iso.c **/
EXPORT_SYMBOL(hpsb_iso_xmit_init);
EXPORT_SYMBOL(hpsb_iso_recv_init);
EXPORT_SYMBOL(hpsb_iso_xmit_start);
EXPORT_SYMBOL(hpsb_iso_recv_start);
EXPORT_SYMBOL(hpsb_iso_recv_listen_channel);
EXPORT_SYMBOL(hpsb_iso_recv_unlisten_channel);
EXPORT_SYMBOL(hpsb_iso_recv_set_channel_mask);
EXPORT_SYMBOL(hpsb_iso_stop);
EXPORT_SYMBOL(hpsb_iso_shutdown);
EXPORT_SYMBOL(hpsb_iso_xmit_queue_packet);
EXPORT_SYMBOL(hpsb_iso_xmit_sync);
EXPORT_SYMBOL(hpsb_iso_recv_release_packets);
EXPORT_SYMBOL(hpsb_iso_n_ready);
EXPORT_SYMBOL(hpsb_iso_packet_sent);
EXPORT_SYMBOL(hpsb_iso_packet_received);
EXPORT_SYMBOL(hpsb_iso_wake);
EXPORT_SYMBOL(hpsb_iso_recv_flush);

/** csr1212.c **/
EXPORT_SYMBOL(csr1212_new_directory);
EXPORT_SYMBOL(csr1212_attach_keyval_to_directory);
EXPORT_SYMBOL(csr1212_detach_keyval_from_directory);
EXPORT_SYMBOL(csr1212_release_keyval);
EXPORT_SYMBOL(csr1212_read);
EXPORT_SYMBOL(csr1212_parse_keyval);
EXPORT_SYMBOL(_csr1212_read_keyval);
EXPORT_SYMBOL(_csr1212_destroy_keyval);
#ifdef CONFIG_IEEE1394_EXPORT_FULL_API
EXPORT_SYMBOL(csr1212_create_csr);
EXPORT_SYMBOL(csr1212_init_local_csr);
EXPORT_SYMBOL(csr1212_new_immediate);
EXPORT_SYMBOL(csr1212_associate_keyval);
EXPORT_SYMBOL(csr1212_new_string_descriptor_leaf);
EXPORT_SYMBOL(csr1212_destroy_csr);
EXPORT_SYMBOL(csr1212_generate_csr_image);
EXPORT_SYMBOL(csr1212_parse_csr);
#endif
