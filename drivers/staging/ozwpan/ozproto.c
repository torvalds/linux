/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */

#include <linux/module.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/errno.h>
#include <linux/ieee80211.h>
#include <linux/slab.h>
#include "ozdbg.h"
#include "ozprotocol.h"
#include "ozeltbuf.h"
#include "ozpd.h"
#include "ozproto.h"
#include "ozusbsvc.h"

#include "ozappif.h"
#include <asm/unaligned.h>
#include <linux/uaccess.h>
#include <net/psnap.h>

#define OZ_CF_CONN_SUCCESS	1
#define OZ_CF_CONN_FAILURE	2

#define OZ_DO_STOP		1
#define OZ_DO_SLEEP		2

struct oz_binding {
	struct packet_type ptype;
	char name[OZ_MAX_BINDING_LEN];
	struct list_head link;
};

/*
 * External variable
 */

DEFINE_SPINLOCK(g_polling_lock);
/*
 * Static external variables.
 */
static LIST_HEAD(g_pd_list);
static LIST_HEAD(g_binding);
static DEFINE_SPINLOCK(g_binding_lock);
static struct sk_buff_head g_rx_queue;
static u8 g_session_id;
static u16 g_apps = 0x1;
static int g_processing_rx;

struct kmem_cache *oz_elt_info_cache;
struct kmem_cache *oz_tx_frame_cache;

/*
 * Context: softirq-serialized
 */
static u8 oz_get_new_session_id(u8 exclude)
{
	if (++g_session_id == 0)
		g_session_id = 1;
	if (g_session_id == exclude) {
		if (++g_session_id == 0)
			g_session_id = 1;
	}
	return g_session_id;
}

/*
 * Context: softirq-serialized
 */
static void oz_send_conn_rsp(struct oz_pd *pd, u8 status)
{
	struct sk_buff *skb;
	struct net_device *dev = pd->net_dev;
	struct oz_hdr *oz_hdr;
	struct oz_elt *elt;
	struct oz_elt_connect_rsp *body;

	int sz = sizeof(struct oz_hdr) + sizeof(struct oz_elt) +
			sizeof(struct oz_elt_connect_rsp);
	skb = alloc_skb(sz + OZ_ALLOCATED_SPACE(dev), GFP_ATOMIC);
	if (skb == NULL)
		return;
	skb_reserve(skb, LL_RESERVED_SPACE(dev));
	skb_reset_network_header(skb);
	oz_hdr = (struct oz_hdr *)skb_put(skb, sz);
	elt = (struct oz_elt *)(oz_hdr+1);
	body = (struct oz_elt_connect_rsp *)(elt+1);
	skb->dev = dev;
	skb->protocol = htons(OZ_ETHERTYPE);
	/* Fill in device header */
	if (dev_hard_header(skb, dev, OZ_ETHERTYPE, pd->mac_addr,
			dev->dev_addr, skb->len) < 0) {
		kfree_skb(skb);
		return;
	}
	oz_hdr->control = OZ_PROTOCOL_VERSION<<OZ_VERSION_SHIFT;
	oz_hdr->last_pkt_num = 0;
	put_unaligned(0, &oz_hdr->pkt_num);
	elt->type = OZ_ELT_CONNECT_RSP;
	elt->length = sizeof(struct oz_elt_connect_rsp);
	memset(body, 0, sizeof(struct oz_elt_connect_rsp));
	body->status = status;
	if (status == 0) {
		body->mode = pd->mode;
		body->session_id = pd->session_id;
		put_unaligned(cpu_to_le16(pd->total_apps), &body->apps);
	}
	oz_dbg(ON, "TX: OZ_ELT_CONNECT_RSP %d", status);
	dev_queue_xmit(skb);
}

/*
 * Context: softirq-serialized
 */
static void pd_set_keepalive(struct oz_pd *pd, u8 kalive)
{
	unsigned long keep_alive = kalive & OZ_KALIVE_VALUE_MASK;

	switch (kalive & OZ_KALIVE_TYPE_MASK) {
	case OZ_KALIVE_SPECIAL:
		pd->keep_alive = keep_alive * 1000*60*60*24*20;
		break;
	case OZ_KALIVE_SECS:
		pd->keep_alive = keep_alive*1000;
		break;
	case OZ_KALIVE_MINS:
		pd->keep_alive = keep_alive*1000*60;
		break;
	case OZ_KALIVE_HOURS:
		pd->keep_alive = keep_alive*1000*60*60;
		break;
	default:
		pd->keep_alive = 0;
	}
	oz_dbg(ON, "Keepalive = %lu mSec\n", pd->keep_alive);
}

/*
 * Context: softirq-serialized
 */
static void pd_set_presleep(struct oz_pd *pd, u8 presleep, u8 start_timer)
{
	if (presleep)
		pd->presleep = presleep*100;
	else
		pd->presleep = OZ_PRESLEEP_TOUT;
	if (start_timer) {
		spin_unlock(&g_polling_lock);
		oz_timer_add(pd, OZ_TIMER_TOUT, pd->presleep);
		spin_lock(&g_polling_lock);
	}
	oz_dbg(ON, "Presleep time = %lu mSec\n", pd->presleep);
}

/*
 * Context: softirq-serialized
 */
static struct oz_pd *oz_connect_req(struct oz_pd *cur_pd, struct oz_elt *elt,
			const u8 *pd_addr, struct net_device *net_dev)
{
	struct oz_pd *pd;
	struct oz_elt_connect_req *body =
			(struct oz_elt_connect_req *)(elt+1);
	u8 rsp_status = OZ_STATUS_SUCCESS;
	u8 stop_needed = 0;
	u16 new_apps = g_apps;
	struct net_device *old_net_dev = NULL;
	struct oz_pd *free_pd = NULL;

	if (cur_pd) {
		pd = cur_pd;
		spin_lock_bh(&g_polling_lock);
	} else {
		struct oz_pd *pd2 = NULL;
		struct list_head *e;

		pd = oz_pd_alloc(pd_addr);
		if (pd == NULL)
			return NULL;
		getnstimeofday(&pd->last_rx_timestamp);
		spin_lock_bh(&g_polling_lock);
		list_for_each(e, &g_pd_list) {
			pd2 = list_entry(e, struct oz_pd, link);
			if (ether_addr_equal(pd2->mac_addr, pd_addr)) {
				free_pd = pd;
				pd = pd2;
				break;
			}
		}
		if (pd != pd2)
			list_add_tail(&pd->link, &g_pd_list);
	}
	if (pd == NULL) {
		spin_unlock_bh(&g_polling_lock);
		return NULL;
	}
	if (pd->net_dev != net_dev) {
		old_net_dev = pd->net_dev;
		dev_hold(net_dev);
		pd->net_dev = net_dev;
	}
	oz_dbg(ON, "Host vendor: %d\n", body->host_vendor);
	pd->max_tx_size = OZ_MAX_TX_SIZE;
	pd->mode = body->mode;
	pd->pd_info = body->pd_info;
	if (pd->mode & OZ_F_ISOC_NO_ELTS) {
		pd->ms_per_isoc = body->ms_per_isoc;
		if (!pd->ms_per_isoc)
			pd->ms_per_isoc = 4;

		switch (body->ms_isoc_latency & OZ_LATENCY_MASK) {
		case OZ_ONE_MS_LATENCY:
			pd->isoc_latency = (body->ms_isoc_latency &
					~OZ_LATENCY_MASK) / pd->ms_per_isoc;
			break;
		case OZ_TEN_MS_LATENCY:
			pd->isoc_latency = ((body->ms_isoc_latency &
				~OZ_LATENCY_MASK) * 10) / pd->ms_per_isoc;
			break;
		default:
			pd->isoc_latency = OZ_MAX_TX_QUEUE_ISOC;
		}
	}
	if (body->max_len_div16)
		pd->max_tx_size = ((u16)body->max_len_div16)<<4;
	oz_dbg(ON, "Max frame:%u Ms per isoc:%u\n",
	       pd->max_tx_size, pd->ms_per_isoc);
	pd->max_stream_buffering = 3*1024;
	pd->pulse_period = OZ_QUANTUM;
	pd_set_presleep(pd, body->presleep, 0);
	pd_set_keepalive(pd, body->keep_alive);

	new_apps &= le16_to_cpu(get_unaligned(&body->apps));
	if ((new_apps & 0x1) && (body->session_id)) {
		if (pd->session_id) {
			if (pd->session_id != body->session_id) {
				rsp_status = OZ_STATUS_SESSION_MISMATCH;
				goto done;
			}
		} else {
			new_apps &= ~0x1;  /* Resume not permitted */
			pd->session_id =
				oz_get_new_session_id(body->session_id);
		}
	} else {
		if (pd->session_id && !body->session_id) {
			rsp_status = OZ_STATUS_SESSION_TEARDOWN;
			stop_needed = 1;
		} else {
			new_apps &= ~0x1;  /* Resume not permitted */
			pd->session_id =
				oz_get_new_session_id(body->session_id);
		}
	}
done:
	if (rsp_status == OZ_STATUS_SUCCESS) {
		u16 start_apps = new_apps & ~pd->total_apps & ~0x1;
		u16 stop_apps = pd->total_apps & ~new_apps & ~0x1;
		u16 resume_apps = new_apps & pd->paused_apps  & ~0x1;

		spin_unlock_bh(&g_polling_lock);
		oz_pd_set_state(pd, OZ_PD_S_CONNECTED);
		oz_dbg(ON, "new_apps=0x%x total_apps=0x%x paused_apps=0x%x\n",
		       new_apps, pd->total_apps, pd->paused_apps);
		if (start_apps) {
			if (oz_services_start(pd, start_apps, 0))
				rsp_status = OZ_STATUS_TOO_MANY_PDS;
		}
		if (resume_apps)
			if (oz_services_start(pd, resume_apps, 1))
				rsp_status = OZ_STATUS_TOO_MANY_PDS;
		if (stop_apps)
			oz_services_stop(pd, stop_apps, 0);
		oz_pd_request_heartbeat(pd);
	} else {
		spin_unlock_bh(&g_polling_lock);
	}
	oz_send_conn_rsp(pd, rsp_status);
	if (rsp_status != OZ_STATUS_SUCCESS) {
		if (stop_needed)
			oz_pd_stop(pd);
		oz_pd_put(pd);
		pd = NULL;
	}
	if (old_net_dev)
		dev_put(old_net_dev);
	if (free_pd)
		oz_pd_destroy(free_pd);
	return pd;
}

/*
 * Context: softirq-serialized
 */
static void oz_add_farewell(struct oz_pd *pd, u8 ep_num, u8 index,
			const u8 *report, u8 len)
{
	struct oz_farewell *f;
	struct oz_farewell *f2;
	int found = 0;

	f = kmalloc(sizeof(struct oz_farewell) + len, GFP_ATOMIC);
	if (!f)
		return;
	f->ep_num = ep_num;
	f->index = index;
	f->len = len;
	memcpy(f->report, report, len);
	oz_dbg(ON, "RX: Adding farewell report\n");
	spin_lock(&g_polling_lock);
	list_for_each_entry(f2, &pd->farewell_list, link) {
		if ((f2->ep_num == ep_num) && (f2->index == index)) {
			found = 1;
			list_del(&f2->link);
			break;
		}
	}
	list_add_tail(&f->link, &pd->farewell_list);
	spin_unlock(&g_polling_lock);
	if (found)
		kfree(f2);
}

/*
 * Context: softirq-serialized
 */
static void oz_rx_frame(struct sk_buff *skb)
{
	u8 *mac_hdr;
	u8 *src_addr;
	struct oz_elt *elt;
	int length;
	struct oz_pd *pd = NULL;
	struct oz_hdr *oz_hdr = (struct oz_hdr *)skb_network_header(skb);
	struct timespec current_time;
	int dup = 0;
	u32 pkt_num;

	oz_dbg(RX_FRAMES, "RX frame PN=0x%x LPN=0x%x control=0x%x\n",
	       oz_hdr->pkt_num, oz_hdr->last_pkt_num, oz_hdr->control);
	mac_hdr = skb_mac_header(skb);
	src_addr = &mac_hdr[ETH_ALEN];
	length = skb->len;

	/* Check the version field */
	if (oz_get_prot_ver(oz_hdr->control) != OZ_PROTOCOL_VERSION) {
		oz_dbg(ON, "Incorrect protocol version: %d\n",
		       oz_get_prot_ver(oz_hdr->control));
		goto done;
	}

	pkt_num = le32_to_cpu(get_unaligned(&oz_hdr->pkt_num));

	pd = oz_pd_find(src_addr);
	if (pd) {
		if (!(pd->state & OZ_PD_S_CONNECTED))
			oz_pd_set_state(pd, OZ_PD_S_CONNECTED);
		getnstimeofday(&current_time);
		if ((current_time.tv_sec != pd->last_rx_timestamp.tv_sec) ||
			(pd->presleep < MSEC_PER_SEC))  {
			oz_timer_add(pd, OZ_TIMER_TOUT,	pd->presleep);
			pd->last_rx_timestamp = current_time;
		}
		if (pkt_num != pd->last_rx_pkt_num) {
			pd->last_rx_pkt_num = pkt_num;
		} else {
			dup = 1;
			oz_dbg(ON, "Duplicate frame\n");
		}
	}

	if (pd && !dup && ((pd->mode & OZ_MODE_MASK) == OZ_MODE_TRIGGERED)) {
		oz_dbg(RX_FRAMES, "Received TRIGGER Frame\n");
		pd->last_sent_frame = &pd->tx_queue;
		if (oz_hdr->control & OZ_F_ACK) {
			/* Retire completed frames */
			oz_retire_tx_frames(pd, oz_hdr->last_pkt_num);
		}
		if ((oz_hdr->control & OZ_F_ACK_REQUESTED) &&
				(pd->state == OZ_PD_S_CONNECTED)) {
			int backlog = pd->nb_queued_frames;

			pd->trigger_pkt_num = pkt_num;
			/* Send queued frames */
			oz_send_queued_frames(pd, backlog);
		}
	}

	length -= sizeof(struct oz_hdr);
	elt = (struct oz_elt *)((u8 *)oz_hdr + sizeof(struct oz_hdr));

	while (length >= sizeof(struct oz_elt)) {
		length -= sizeof(struct oz_elt) + elt->length;
		if (length < 0)
			break;
		switch (elt->type) {
		case OZ_ELT_CONNECT_REQ:
			oz_dbg(ON, "RX: OZ_ELT_CONNECT_REQ\n");
			pd = oz_connect_req(pd, elt, src_addr, skb->dev);
			break;
		case OZ_ELT_DISCONNECT:
			oz_dbg(ON, "RX: OZ_ELT_DISCONNECT\n");
			if (pd)
				oz_pd_sleep(pd);
			break;
		case OZ_ELT_UPDATE_PARAM_REQ: {
				struct oz_elt_update_param *body =
					(struct oz_elt_update_param *)(elt + 1);
				oz_dbg(ON, "RX: OZ_ELT_UPDATE_PARAM_REQ\n");
				if (pd && (pd->state & OZ_PD_S_CONNECTED)) {
					spin_lock(&g_polling_lock);
					pd_set_keepalive(pd, body->keepalive);
					pd_set_presleep(pd, body->presleep, 1);
					spin_unlock(&g_polling_lock);
				}
			}
			break;
		case OZ_ELT_FAREWELL_REQ: {
				struct oz_elt_farewell *body =
					(struct oz_elt_farewell *)(elt + 1);
				oz_dbg(ON, "RX: OZ_ELT_FAREWELL_REQ\n");
				oz_add_farewell(pd, body->ep_num,
					body->index, body->report,
					elt->length + 1 - sizeof(*body));
			}
			break;
		case OZ_ELT_APP_DATA:
			if (pd && (pd->state & OZ_PD_S_CONNECTED)) {
				struct oz_app_hdr *app_hdr =
					(struct oz_app_hdr *)(elt+1);
				if (dup)
					break;
				oz_handle_app_elt(pd, app_hdr->app_id, elt);
			}
			break;
		default:
			oz_dbg(ON, "RX: Unknown elt %02x\n", elt->type);
		}
		elt = oz_next_elt(elt);
	}
done:
	if (pd)
		oz_pd_put(pd);
	consume_skb(skb);
}

/*
 * Context: process
 */
void oz_protocol_term(void)
{
	struct oz_binding *b, *t;

	/* Walk the list of bindings and remove each one.
	 */
	spin_lock_bh(&g_binding_lock);
	list_for_each_entry_safe(b, t, &g_binding, link) {
		list_del(&b->link);
		spin_unlock_bh(&g_binding_lock);
		dev_remove_pack(&b->ptype);
		if (b->ptype.dev)
			dev_put(b->ptype.dev);
		kfree(b);
		spin_lock_bh(&g_binding_lock);
	}
	spin_unlock_bh(&g_binding_lock);
	/* Walk the list of PDs and stop each one. This causes the PD to be
	 * removed from the list so we can just pull each one from the head
	 * of the list.
	 */
	spin_lock_bh(&g_polling_lock);
	while (!list_empty(&g_pd_list)) {
		struct oz_pd *pd =
			list_first_entry(&g_pd_list, struct oz_pd, link);
		oz_pd_get(pd);
		spin_unlock_bh(&g_polling_lock);
		oz_pd_stop(pd);
		oz_pd_put(pd);
		spin_lock_bh(&g_polling_lock);
	}
	spin_unlock_bh(&g_polling_lock);
	oz_dbg(ON, "Protocol stopped\n");

	kmem_cache_destroy(oz_tx_frame_cache);
	kmem_cache_destroy(oz_elt_info_cache);
}

/*
 * Context: softirq
 */
void oz_pd_heartbeat_handler(unsigned long data)
{
	struct oz_pd *pd = (struct oz_pd *)data;
	u16 apps = 0;

	spin_lock_bh(&g_polling_lock);
	if (pd->state & OZ_PD_S_CONNECTED)
		apps = pd->total_apps;
	spin_unlock_bh(&g_polling_lock);
	if (apps)
		oz_pd_heartbeat(pd, apps);
	oz_pd_put(pd);
}

/*
 * Context: softirq
 */
void oz_pd_timeout_handler(unsigned long data)
{
	int type;
	struct oz_pd *pd = (struct oz_pd *)data;

	spin_lock_bh(&g_polling_lock);
	type = pd->timeout_type;
	spin_unlock_bh(&g_polling_lock);
	switch (type) {
	case OZ_TIMER_TOUT:
		oz_pd_sleep(pd);
		break;
	case OZ_TIMER_STOP:
		oz_pd_stop(pd);
		break;
	}
	oz_pd_put(pd);
}

/*
 * Context: Interrupt
 */
enum hrtimer_restart oz_pd_heartbeat_event(struct hrtimer *timer)
{
	struct oz_pd *pd;

	pd = container_of(timer, struct oz_pd, heartbeat);
	hrtimer_forward_now(timer, ktime_set(pd->pulse_period /
	MSEC_PER_SEC, (pd->pulse_period % MSEC_PER_SEC) * NSEC_PER_MSEC));
	oz_pd_get(pd);
	tasklet_schedule(&pd->heartbeat_tasklet);
	return HRTIMER_RESTART;
}

/*
 * Context: Interrupt
 */
enum hrtimer_restart oz_pd_timeout_event(struct hrtimer *timer)
{
	struct oz_pd *pd;

	pd = container_of(timer, struct oz_pd, timeout);
	oz_pd_get(pd);
	tasklet_schedule(&pd->timeout_tasklet);
	return HRTIMER_NORESTART;
}

/*
 * Context: softirq or process
 */
void oz_timer_add(struct oz_pd *pd, int type, unsigned long due_time)
{
	spin_lock_bh(&g_polling_lock);
	switch (type) {
	case OZ_TIMER_TOUT:
	case OZ_TIMER_STOP:
		if (hrtimer_active(&pd->timeout)) {
			hrtimer_set_expires(&pd->timeout, ktime_set(due_time /
			MSEC_PER_SEC, (due_time % MSEC_PER_SEC) *
							NSEC_PER_MSEC));
			hrtimer_start_expires(&pd->timeout, HRTIMER_MODE_REL);
		} else {
			hrtimer_start(&pd->timeout, ktime_set(due_time /
			MSEC_PER_SEC, (due_time % MSEC_PER_SEC) *
					NSEC_PER_MSEC), HRTIMER_MODE_REL);
		}
		pd->timeout_type = type;
		break;
	case OZ_TIMER_HEARTBEAT:
		if (!hrtimer_active(&pd->heartbeat))
			hrtimer_start(&pd->heartbeat, ktime_set(due_time /
			MSEC_PER_SEC, (due_time % MSEC_PER_SEC) *
					NSEC_PER_MSEC), HRTIMER_MODE_REL);
		break;
	}
	spin_unlock_bh(&g_polling_lock);
}

/*
 * Context: softirq or process
 */
void oz_pd_request_heartbeat(struct oz_pd *pd)
{
	oz_timer_add(pd, OZ_TIMER_HEARTBEAT, pd->pulse_period > 0 ?
					pd->pulse_period : OZ_QUANTUM);
}

/*
 * Context: softirq or process
 */
struct oz_pd *oz_pd_find(const u8 *mac_addr)
{
	struct oz_pd *pd;

	spin_lock_bh(&g_polling_lock);
	list_for_each_entry(pd, &g_pd_list, link) {
		if (ether_addr_equal(pd->mac_addr, mac_addr)) {
			oz_pd_get(pd);
			spin_unlock_bh(&g_polling_lock);
			return pd;
		}
	}
	spin_unlock_bh(&g_polling_lock);
	return NULL;
}

/*
 * Context: process
 */
void oz_app_enable(int app_id, int enable)
{
	if (app_id < OZ_NB_APPS) {
		spin_lock_bh(&g_polling_lock);
		if (enable)
			g_apps |= (1<<app_id);
		else
			g_apps &= ~(1<<app_id);
		spin_unlock_bh(&g_polling_lock);
	}
}

/*
 * Context: softirq
 */
static int oz_pkt_recv(struct sk_buff *skb, struct net_device *dev,
		struct packet_type *pt, struct net_device *orig_dev)
{
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (skb == NULL)
		return 0;
	spin_lock_bh(&g_rx_queue.lock);
	if (g_processing_rx) {
		/* We already hold the lock so use __ variant.
		 */
		__skb_queue_head(&g_rx_queue, skb);
		spin_unlock_bh(&g_rx_queue.lock);
	} else {
		g_processing_rx = 1;
		do {

			spin_unlock_bh(&g_rx_queue.lock);
			oz_rx_frame(skb);
			spin_lock_bh(&g_rx_queue.lock);
			if (skb_queue_empty(&g_rx_queue)) {
				g_processing_rx = 0;
				spin_unlock_bh(&g_rx_queue.lock);
				break;
			}
			/* We already hold the lock so use __ variant.
			 */
			skb = __skb_dequeue(&g_rx_queue);
		} while (1);
	}
	return 0;
}

/*
 * Context: process
 */
void oz_binding_add(const char *net_dev)
{
	struct oz_binding *binding;

	binding = kzalloc(sizeof(struct oz_binding), GFP_KERNEL);
	if (!binding)
		return;

	binding->ptype.type = htons(OZ_ETHERTYPE);
	binding->ptype.func = oz_pkt_recv;
	if (net_dev && *net_dev) {
		memcpy(binding->name, net_dev, OZ_MAX_BINDING_LEN);
		oz_dbg(ON, "Adding binding: %s\n", net_dev);
		binding->ptype.dev = dev_get_by_name(&init_net, net_dev);
		if (binding->ptype.dev == NULL) {
			oz_dbg(ON, "Netdev %s not found\n", net_dev);
			kfree(binding);
			return;
		}
	}
	dev_add_pack(&binding->ptype);
	spin_lock_bh(&g_binding_lock);
	list_add_tail(&binding->link, &g_binding);
	spin_unlock_bh(&g_binding_lock);
}

/*
 * Context: process
 */
static void pd_stop_all_for_device(struct net_device *net_dev)
{
	LIST_HEAD(h);
	struct oz_pd *pd;
	struct oz_pd *n;

	spin_lock_bh(&g_polling_lock);
	list_for_each_entry_safe(pd, n, &g_pd_list, link) {
		if (pd->net_dev == net_dev) {
			list_move(&pd->link, &h);
			oz_pd_get(pd);
		}
	}
	spin_unlock_bh(&g_polling_lock);
	while (!list_empty(&h)) {
		pd = list_first_entry(&h, struct oz_pd, link);
		oz_pd_stop(pd);
		oz_pd_put(pd);
	}
}

/*
 * Context: process
 */
void oz_binding_remove(const char *net_dev)
{
	struct oz_binding *binding;
	int found = 0;

	oz_dbg(ON, "Removing binding: %s\n", net_dev);
	spin_lock_bh(&g_binding_lock);
	list_for_each_entry(binding, &g_binding, link) {
		if (strncmp(binding->name, net_dev, OZ_MAX_BINDING_LEN) == 0) {
			oz_dbg(ON, "Binding '%s' found\n", net_dev);
			found = 1;
			break;
		}
	}
	spin_unlock_bh(&g_binding_lock);
	if (found) {
		dev_remove_pack(&binding->ptype);
		if (binding->ptype.dev) {
			dev_put(binding->ptype.dev);
			pd_stop_all_for_device(binding->ptype.dev);
		}
		list_del(&binding->link);
		kfree(binding);
	}
}

/*
 * Context: process
 */
static char *oz_get_next_device_name(char *s, char *dname, int max_size)
{
	while (*s == ',')
		s++;
	while (*s && (*s != ',') && max_size > 1) {
		*dname++ = *s++;
		max_size--;
	}
	*dname = 0;
	return s;
}

/*
 * Context: process
 */
int oz_protocol_init(char *devs)
{
	oz_elt_info_cache = KMEM_CACHE(oz_elt_info, 0);
	if (!oz_elt_info_cache)
		return -ENOMEM;

	oz_tx_frame_cache = KMEM_CACHE(oz_tx_frame, 0);
	if (!oz_tx_frame_cache) {
		kmem_cache_destroy(oz_elt_info_cache);
		return -ENOMEM;
	}

	skb_queue_head_init(&g_rx_queue);
	if (devs[0] == '*') {
		oz_binding_add(NULL);
	} else {
		char d[32];

		while (*devs) {
			devs = oz_get_next_device_name(devs, d, sizeof(d));
			if (d[0])
				oz_binding_add(d);
		}
	}
	return 0;
}

/*
 * Context: process
 */
int oz_get_pd_list(struct oz_mac_addr *addr, int max_count)
{
	struct oz_pd *pd;
	int count = 0;

	spin_lock_bh(&g_polling_lock);
	list_for_each_entry(pd, &g_pd_list, link) {
		if (count >= max_count)
			break;
		ether_addr_copy((u8 *)&addr[count++], pd->mac_addr);
	}
	spin_unlock_bh(&g_polling_lock);
	return count;
}

