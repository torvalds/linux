/*********************************************************************
 *
 * Filename:      irlan_client_event.c
 * Version:       0.9
 * Description:   IrLAN client state machine
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Sun Dec 26 21:52:24 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1998-1999 Dag Brattli <dagb@cs.uit.no>,
 *     All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#include <linux/skbuff.h>

#include <net/irda/irda.h>
#include <net/irda/timer.h>
#include <net/irda/irmod.h>
#include <net/irda/iriap.h>
#include <net/irda/irlmp.h>
#include <net/irda/irttp.h>

#include <net/irda/irlan_common.h>
#include <net/irda/irlan_client.h>
#include <net/irda/irlan_event.h>

static int irlan_client_state_idle (struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb);
static int irlan_client_state_query(struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb);
static int irlan_client_state_conn (struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb);
static int irlan_client_state_info (struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb);
static int irlan_client_state_media(struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb);
static int irlan_client_state_open (struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb);
static int irlan_client_state_wait (struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb);
static int irlan_client_state_arb  (struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb);
static int irlan_client_state_data (struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb);
static int irlan_client_state_close(struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb);
static int irlan_client_state_sync (struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb);

static int (*state[])(struct irlan_cb *, IRLAN_EVENT event, struct sk_buff *) =
{
	irlan_client_state_idle,
	irlan_client_state_query,
	irlan_client_state_conn,
	irlan_client_state_info,
	irlan_client_state_media,
	irlan_client_state_open,
	irlan_client_state_wait,
	irlan_client_state_arb,
	irlan_client_state_data,
	irlan_client_state_close,
	irlan_client_state_sync
};

void irlan_do_client_event(struct irlan_cb *self, IRLAN_EVENT event,
			   struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	(*state[ self->client.state]) (self, event, skb);
}

/*
 * Function irlan_client_state_idle (event, skb, info)
 *
 *    IDLE, We are waiting for an indication that there is a provider
 *    available.
 */
static int irlan_client_state_idle(struct irlan_cb *self, IRLAN_EVENT event,
				   struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return -1;);

	switch (event) {
	case IRLAN_DISCOVERY_INDICATION:
		if (self->client.iriap) {
			net_warn_ratelimited("%s(), busy with a previous query\n",
					     __func__);
			return -EBUSY;
		}

		self->client.iriap = iriap_open(LSAP_ANY, IAS_CLIENT, self,
						irlan_client_get_value_confirm);
		/* Get some values from peer IAS */
		irlan_next_client_state(self, IRLAN_QUERY);
		iriap_getvaluebyclass_request(self->client.iriap,
					      self->saddr, self->daddr,
					      "IrLAN", "IrDA:TinyTP:LsapSel");
		break;
	case IRLAN_WATCHDOG_TIMEOUT:
		pr_debug("%s(), IRLAN_WATCHDOG_TIMEOUT\n", __func__);
		break;
	default:
		pr_debug("%s(), Unknown event %d\n", __func__ , event);
		break;
	}
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

/*
 * Function irlan_client_state_query (event, skb, info)
 *
 *    QUERY, We have queryed the remote IAS and is ready to connect
 *    to provider, just waiting for the confirm.
 *
 */
static int irlan_client_state_query(struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return -1;);

	switch(event) {
	case IRLAN_IAS_PROVIDER_AVAIL:
		IRDA_ASSERT(self->dtsap_sel_ctrl != 0, return -1;);

		self->client.open_retries = 0;

		irttp_connect_request(self->client.tsap_ctrl,
				      self->dtsap_sel_ctrl,
				      self->saddr, self->daddr, NULL,
				      IRLAN_MTU, NULL);
		irlan_next_client_state(self, IRLAN_CONN);
		break;
	case IRLAN_IAS_PROVIDER_NOT_AVAIL:
		pr_debug("%s(), IAS_PROVIDER_NOT_AVAIL\n", __func__);
		irlan_next_client_state(self, IRLAN_IDLE);

		/* Give the client a kick! */
		if ((self->provider.access_type == ACCESS_PEER) &&
		    (self->provider.state != IRLAN_IDLE))
			irlan_client_wakeup(self, self->saddr, self->daddr);
		break;
	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_client_state(self, IRLAN_IDLE);
		break;
	case IRLAN_WATCHDOG_TIMEOUT:
		pr_debug("%s(), IRLAN_WATCHDOG_TIMEOUT\n", __func__);
		break;
	default:
		pr_debug("%s(), Unknown event %d\n", __func__ , event);
		break;
	}
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

/*
 * Function irlan_client_state_conn (event, skb, info)
 *
 *    CONN, We have connected to a provider but has not issued any
 *    commands yet.
 *
 */
static int irlan_client_state_conn(struct irlan_cb *self, IRLAN_EVENT event,
				   struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return -1;);

	switch (event) {
	case IRLAN_CONNECT_COMPLETE:
		/* Send getinfo cmd */
		irlan_get_provider_info(self);
		irlan_next_client_state(self, IRLAN_INFO);
		break;
	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_client_state(self, IRLAN_IDLE);
		break;
	case IRLAN_WATCHDOG_TIMEOUT:
		pr_debug("%s(), IRLAN_WATCHDOG_TIMEOUT\n", __func__);
		break;
	default:
		pr_debug("%s(), Unknown event %d\n", __func__ , event);
		break;
	}
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

/*
 * Function irlan_client_state_info (self, event, skb, info)
 *
 *    INFO, We have issued a GetInfo command and is awaiting a reply.
 */
static int irlan_client_state_info(struct irlan_cb *self, IRLAN_EVENT event,
				   struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return -1;);

	switch (event) {
	case IRLAN_DATA_INDICATION:
		IRDA_ASSERT(skb != NULL, return -1;);

		irlan_client_parse_response(self, skb);

		irlan_next_client_state(self, IRLAN_MEDIA);

		irlan_get_media_char(self);
		break;

	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_client_state(self, IRLAN_IDLE);
		break;
	case IRLAN_WATCHDOG_TIMEOUT:
		pr_debug("%s(), IRLAN_WATCHDOG_TIMEOUT\n", __func__);
		break;
	default:
		pr_debug("%s(), Unknown event %d\n", __func__ , event);
		break;
	}
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

/*
 * Function irlan_client_state_media (self, event, skb, info)
 *
 *    MEDIA, The irlan_client has issued a GetMedia command and is awaiting a
 *    reply.
 *
 */
static int irlan_client_state_media(struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return -1;);

	switch(event) {
	case IRLAN_DATA_INDICATION:
		irlan_client_parse_response(self, skb);
		irlan_open_data_channel(self);
		irlan_next_client_state(self, IRLAN_OPEN);
		break;
	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_client_state(self, IRLAN_IDLE);
		break;
	case IRLAN_WATCHDOG_TIMEOUT:
		pr_debug("%s(), IRLAN_WATCHDOG_TIMEOUT\n", __func__);
		break;
	default:
		pr_debug("%s(), Unknown event %d\n", __func__ , event);
		break;
	}
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

/*
 * Function irlan_client_state_open (self, event, skb, info)
 *
 *    OPEN, The irlan_client has issued a OpenData command and is awaiting a
 *    reply
 *
 */
static int irlan_client_state_open(struct irlan_cb *self, IRLAN_EVENT event,
				   struct sk_buff *skb)
{
	struct qos_info qos;

	IRDA_ASSERT(self != NULL, return -1;);

	switch(event) {
	case IRLAN_DATA_INDICATION:
		irlan_client_parse_response(self, skb);

		/*
		 *  Check if we have got the remote TSAP for data
		 *  communications
		 */
		IRDA_ASSERT(self->dtsap_sel_data != 0, return -1;);

		/* Check which access type we are dealing with */
		switch (self->client.access_type) {
		case ACCESS_PEER:
		    if (self->provider.state == IRLAN_OPEN) {

			    irlan_next_client_state(self, IRLAN_ARB);
			    irlan_do_client_event(self, IRLAN_CHECK_CON_ARB,
						  NULL);
		    } else {

			    irlan_next_client_state(self, IRLAN_WAIT);
		    }
		    break;
		case ACCESS_DIRECT:
		case ACCESS_HOSTED:
			qos.link_disc_time.bits = 0x01; /* 3 secs */

			irttp_connect_request(self->tsap_data,
					      self->dtsap_sel_data,
					      self->saddr, self->daddr, &qos,
					      IRLAN_MTU, NULL);

			irlan_next_client_state(self, IRLAN_DATA);
			break;
		default:
			pr_debug("%s(), unknown access type!\n", __func__);
			break;
		}
		break;
	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_client_state(self, IRLAN_IDLE);
		break;
	case IRLAN_WATCHDOG_TIMEOUT:
		pr_debug("%s(), IRLAN_WATCHDOG_TIMEOUT\n", __func__);
		break;
	default:
		pr_debug("%s(), Unknown event %d\n", __func__ , event);
		break;
	}

	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

/*
 * Function irlan_client_state_wait (self, event, skb, info)
 *
 *    WAIT, The irlan_client is waiting for the local provider to enter the
 *    provider OPEN state.
 *
 */
static int irlan_client_state_wait(struct irlan_cb *self, IRLAN_EVENT event,
				   struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return -1;);

	switch(event) {
	case IRLAN_PROVIDER_SIGNAL:
		irlan_next_client_state(self, IRLAN_ARB);
		irlan_do_client_event(self, IRLAN_CHECK_CON_ARB, NULL);
		break;
	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_client_state(self, IRLAN_IDLE);
		break;
	case IRLAN_WATCHDOG_TIMEOUT:
		pr_debug("%s(), IRLAN_WATCHDOG_TIMEOUT\n", __func__);
		break;
	default:
		pr_debug("%s(), Unknown event %d\n", __func__ , event);
		break;
	}
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

static int irlan_client_state_arb(struct irlan_cb *self, IRLAN_EVENT event,
				  struct sk_buff *skb)
{
	struct qos_info qos;

	IRDA_ASSERT(self != NULL, return -1;);

	switch(event) {
	case IRLAN_CHECK_CON_ARB:
		if (self->client.recv_arb_val == self->provider.send_arb_val) {
			irlan_next_client_state(self, IRLAN_CLOSE);
			irlan_close_data_channel(self);
		} else if (self->client.recv_arb_val <
			   self->provider.send_arb_val)
		{
			qos.link_disc_time.bits = 0x01; /* 3 secs */

			irlan_next_client_state(self, IRLAN_DATA);
			irttp_connect_request(self->tsap_data,
					      self->dtsap_sel_data,
					      self->saddr, self->daddr, &qos,
					      IRLAN_MTU, NULL);
		} else if (self->client.recv_arb_val >
			   self->provider.send_arb_val)
		{
			pr_debug("%s(), lost the battle :-(\n", __func__);
		}
		break;
	case IRLAN_DATA_CONNECT_INDICATION:
		irlan_next_client_state(self, IRLAN_DATA);
		break;
	case IRLAN_LMP_DISCONNECT:
	case IRLAN_LAP_DISCONNECT:
		irlan_next_client_state(self, IRLAN_IDLE);
		break;
	case IRLAN_WATCHDOG_TIMEOUT:
		pr_debug("%s(), IRLAN_WATCHDOG_TIMEOUT\n", __func__);
		break;
	default:
		pr_debug("%s(), Unknown event %d\n", __func__ , event);
		break;
	}
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

/*
 * Function irlan_client_state_data (self, event, skb, info)
 *
 *    DATA, The data channel is connected, allowing data transfers between
 *    the local and remote machines.
 *
 */
static int irlan_client_state_data(struct irlan_cb *self, IRLAN_EVENT event,
				   struct sk_buff *skb)
{
	IRDA_ASSERT(self != NULL, return -1;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return -1;);

	switch(event) {
	case IRLAN_DATA_INDICATION:
		irlan_client_parse_response(self, skb);
		break;
	case IRLAN_LMP_DISCONNECT: /* FALLTHROUGH */
	case IRLAN_LAP_DISCONNECT:
		irlan_next_client_state(self, IRLAN_IDLE);
		break;
	default:
		pr_debug("%s(), Unknown event %d\n", __func__ , event);
		break;
	}
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

/*
 * Function irlan_client_state_close (self, event, skb, info)
 *
 *
 *
 */
static int irlan_client_state_close(struct irlan_cb *self, IRLAN_EVENT event,
				    struct sk_buff *skb)
{
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}

/*
 * Function irlan_client_state_sync (self, event, skb, info)
 *
 *
 *
 */
static int irlan_client_state_sync(struct irlan_cb *self, IRLAN_EVENT event,
				   struct sk_buff *skb)
{
	if (skb)
		dev_kfree_skb(skb);

	return 0;
}













