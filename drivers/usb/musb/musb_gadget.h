// SPDX-License-Identifier: GPL-2.0
/*
 * MUSB OTG driver peripheral defines
 *
 * Copyright 2005 Mentor Graphics Corporation
 * Copyright (C) 2005-2006 by Texas Instruments
 * Copyright (C) 2006-2007 Nokia Corporation
 */

#ifndef __MUSB_GADGET_H
#define __MUSB_GADGET_H

#include <linux/list.h>

#if IS_ENABLED(CONFIG_USB_MUSB_GADGET) || IS_ENABLED(CONFIG_USB_MUSB_DUAL_ROLE)
extern irqreturn_t musb_g_ep0_irq(struct musb *);
extern void musb_g_tx(struct musb *, u8);
extern void musb_g_rx(struct musb *, u8);
extern void musb_g_reset(struct musb *);
extern void musb_g_suspend(struct musb *);
extern void musb_g_resume(struct musb *);
extern void musb_g_wakeup(struct musb *);
extern void musb_g_disconnect(struct musb *);
extern void musb_gadget_cleanup(struct musb *);
extern int musb_gadget_setup(struct musb *);

#else
static inline irqreturn_t musb_g_ep0_irq(struct musb *musb)
{
	return 0;
}

static inline void musb_g_tx(struct musb *musb, u8 epnum)	{}
static inline void musb_g_rx(struct musb *musb, u8 epnum)	{}
static inline void musb_g_reset(struct musb *musb)		{}
static inline void musb_g_suspend(struct musb *musb)		{}
static inline void musb_g_resume(struct musb *musb)		{}
static inline void musb_g_wakeup(struct musb *musb)		{}
static inline void musb_g_disconnect(struct musb *musb)		{}
static inline void musb_gadget_cleanup(struct musb *musb)	{}
static inline int musb_gadget_setup(struct musb *musb)
{
	return 0;
}
#endif

enum buffer_map_state {
	UN_MAPPED = 0,
	PRE_MAPPED,
	MUSB_MAPPED
};

struct musb_request {
	struct usb_request	request;
	struct list_head	list;
	struct musb_ep		*ep;
	struct musb		*musb;
	u8 tx;			/* endpoint direction */
	u8 epnum;
	enum buffer_map_state map_state;
};

static inline struct musb_request *to_musb_request(struct usb_request *req)
{
	return req ? container_of(req, struct musb_request, request) : NULL;
}

extern struct usb_request *
musb_alloc_request(struct usb_ep *ep, gfp_t gfp_flags);
extern void musb_free_request(struct usb_ep *ep, struct usb_request *req);


/*
 * struct musb_ep - peripheral side view of endpoint rx or tx side
 */
struct musb_ep {
	/* stuff towards the head is basically write-once. */
	struct usb_ep			end_point;
	char				name[12];
	struct musb_hw_ep		*hw_ep;
	struct musb			*musb;
	u8				current_epnum;

	/* ... when enabled/disabled ... */
	u8				type;
	u8				is_in;
	u16				packet_sz;
	const struct usb_endpoint_descriptor	*desc;
	struct dma_channel		*dma;

	/* later things are modified based on usage */
	struct list_head		req_list;

	u8				wedged;

	/* true if lock must be dropped but req_list may not be advanced */
	u8				busy;

	u8				hb_mult;
};

static inline struct musb_ep *to_musb_ep(struct usb_ep *ep)
{
	return ep ? container_of(ep, struct musb_ep, end_point) : NULL;
}

static inline struct musb_request *next_request(struct musb_ep *ep)
{
	struct list_head	*queue = &ep->req_list;

	if (list_empty(queue))
		return NULL;
	return container_of(queue->next, struct musb_request, list);
}

extern const struct usb_ep_ops musb_g_ep0_ops;

extern void musb_g_giveback(struct musb_ep *, struct usb_request *, int);

extern void musb_ep_restart(struct musb *, struct musb_request *);

#endif		/* __MUSB_GADGET_H */
