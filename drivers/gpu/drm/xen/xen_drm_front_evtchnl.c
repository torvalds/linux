// SPDX-License-Identifier: GPL-2.0 OR MIT

/*
 *  Xen para-virtual DRM device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#include <linux/errno.h>
#include <linux/irq.h>

#include <drm/drm_print.h>

#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/grant_table.h>

#include "xen_drm_front.h"
#include "xen_drm_front_evtchnl.h"

static irqreturn_t evtchnl_interrupt_ctrl(int irq, void *dev_id)
{
	struct xen_drm_front_evtchnl *evtchnl = dev_id;
	struct xen_drm_front_info *front_info = evtchnl->front_info;
	struct xendispl_resp *resp;
	RING_IDX i, rp;
	unsigned long flags;

	if (unlikely(evtchnl->state != EVTCHNL_STATE_CONNECTED))
		return IRQ_HANDLED;

	spin_lock_irqsave(&front_info->io_lock, flags);

again:
	rp = evtchnl->u.req.ring.sring->rsp_prod;
	/* ensure we see queued responses up to rp */
	virt_rmb();

	for (i = evtchnl->u.req.ring.rsp_cons; i != rp; i++) {
		resp = RING_GET_RESPONSE(&evtchnl->u.req.ring, i);
		if (unlikely(resp->id != evtchnl->evt_id))
			continue;

		switch (resp->operation) {
		case XENDISPL_OP_PG_FLIP:
		case XENDISPL_OP_FB_ATTACH:
		case XENDISPL_OP_FB_DETACH:
		case XENDISPL_OP_DBUF_CREATE:
		case XENDISPL_OP_DBUF_DESTROY:
		case XENDISPL_OP_SET_CONFIG:
			evtchnl->u.req.resp_status = resp->status;
			complete(&evtchnl->u.req.completion);
			break;

		default:
			DRM_ERROR("Operation %d is not supported\n",
				  resp->operation);
			break;
		}
	}

	evtchnl->u.req.ring.rsp_cons = i;

	if (i != evtchnl->u.req.ring.req_prod_pvt) {
		int more_to_do;

		RING_FINAL_CHECK_FOR_RESPONSES(&evtchnl->u.req.ring,
					       more_to_do);
		if (more_to_do)
			goto again;
	} else {
		evtchnl->u.req.ring.sring->rsp_event = i + 1;
	}

	spin_unlock_irqrestore(&front_info->io_lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t evtchnl_interrupt_evt(int irq, void *dev_id)
{
	struct xen_drm_front_evtchnl *evtchnl = dev_id;
	struct xen_drm_front_info *front_info = evtchnl->front_info;
	struct xendispl_event_page *page = evtchnl->u.evt.page;
	u32 cons, prod;
	unsigned long flags;

	if (unlikely(evtchnl->state != EVTCHNL_STATE_CONNECTED))
		return IRQ_HANDLED;

	spin_lock_irqsave(&front_info->io_lock, flags);

	prod = page->in_prod;
	/* ensure we see ring contents up to prod */
	virt_rmb();
	if (prod == page->in_cons)
		goto out;

	for (cons = page->in_cons; cons != prod; cons++) {
		struct xendispl_evt *event;

		event = &XENDISPL_IN_RING_REF(page, cons);
		if (unlikely(event->id != evtchnl->evt_id++))
			continue;

		switch (event->type) {
		case XENDISPL_EVT_PG_FLIP:
			xen_drm_front_on_frame_done(front_info, evtchnl->index,
						    event->op.pg_flip.fb_cookie);
			break;
		}
	}
	page->in_cons = cons;
	/* ensure ring contents */
	virt_wmb();

out:
	spin_unlock_irqrestore(&front_info->io_lock, flags);
	return IRQ_HANDLED;
}

static void evtchnl_free(struct xen_drm_front_info *front_info,
			 struct xen_drm_front_evtchnl *evtchnl)
{
	unsigned long page = 0;

	if (evtchnl->type == EVTCHNL_TYPE_REQ)
		page = (unsigned long)evtchnl->u.req.ring.sring;
	else if (evtchnl->type == EVTCHNL_TYPE_EVT)
		page = (unsigned long)evtchnl->u.evt.page;
	if (!page)
		return;

	evtchnl->state = EVTCHNL_STATE_DISCONNECTED;

	if (evtchnl->type == EVTCHNL_TYPE_REQ) {
		/* release all who still waits for response if any */
		evtchnl->u.req.resp_status = -EIO;
		complete_all(&evtchnl->u.req.completion);
	}

	if (evtchnl->irq)
		unbind_from_irqhandler(evtchnl->irq, evtchnl);

	if (evtchnl->port)
		xenbus_free_evtchn(front_info->xb_dev, evtchnl->port);

	/* end access and free the page */
	if (evtchnl->gref != GRANT_INVALID_REF)
		gnttab_end_foreign_access(evtchnl->gref, 0, page);

	memset(evtchnl, 0, sizeof(*evtchnl));
}

static int evtchnl_alloc(struct xen_drm_front_info *front_info, int index,
			 struct xen_drm_front_evtchnl *evtchnl,
			 enum xen_drm_front_evtchnl_type type)
{
	struct xenbus_device *xb_dev = front_info->xb_dev;
	unsigned long page;
	grant_ref_t gref;
	irq_handler_t handler;
	int ret;

	memset(evtchnl, 0, sizeof(*evtchnl));
	evtchnl->type = type;
	evtchnl->index = index;
	evtchnl->front_info = front_info;
	evtchnl->state = EVTCHNL_STATE_DISCONNECTED;
	evtchnl->gref = GRANT_INVALID_REF;

	page = get_zeroed_page(GFP_NOIO | __GFP_HIGH);
	if (!page) {
		ret = -ENOMEM;
		goto fail;
	}

	if (type == EVTCHNL_TYPE_REQ) {
		struct xen_displif_sring *sring;

		init_completion(&evtchnl->u.req.completion);
		mutex_init(&evtchnl->u.req.req_io_lock);
		sring = (struct xen_displif_sring *)page;
		SHARED_RING_INIT(sring);
		FRONT_RING_INIT(&evtchnl->u.req.ring, sring, XEN_PAGE_SIZE);

		ret = xenbus_grant_ring(xb_dev, sring, 1, &gref);
		if (ret < 0) {
			evtchnl->u.req.ring.sring = NULL;
			free_page(page);
			goto fail;
		}

		handler = evtchnl_interrupt_ctrl;
	} else {
		ret = gnttab_grant_foreign_access(xb_dev->otherend_id,
						  virt_to_gfn((void *)page), 0);
		if (ret < 0) {
			free_page(page);
			goto fail;
		}

		evtchnl->u.evt.page = (struct xendispl_event_page *)page;
		gref = ret;
		handler = evtchnl_interrupt_evt;
	}
	evtchnl->gref = gref;

	ret = xenbus_alloc_evtchn(xb_dev, &evtchnl->port);
	if (ret < 0)
		goto fail;

	ret = bind_evtchn_to_irqhandler(evtchnl->port,
					handler, 0, xb_dev->devicetype,
					evtchnl);
	if (ret < 0)
		goto fail;

	evtchnl->irq = ret;
	return 0;

fail:
	DRM_ERROR("Failed to allocate ring: %d\n", ret);
	return ret;
}

int xen_drm_front_evtchnl_create_all(struct xen_drm_front_info *front_info)
{
	struct xen_drm_front_cfg *cfg;
	int ret, conn;

	cfg = &front_info->cfg;

	front_info->evt_pairs =
			kcalloc(cfg->num_connectors,
				sizeof(struct xen_drm_front_evtchnl_pair),
				GFP_KERNEL);
	if (!front_info->evt_pairs) {
		ret = -ENOMEM;
		goto fail;
	}

	for (conn = 0; conn < cfg->num_connectors; conn++) {
		ret = evtchnl_alloc(front_info, conn,
				    &front_info->evt_pairs[conn].req,
				    EVTCHNL_TYPE_REQ);
		if (ret < 0) {
			DRM_ERROR("Error allocating control channel\n");
			goto fail;
		}

		ret = evtchnl_alloc(front_info, conn,
				    &front_info->evt_pairs[conn].evt,
				    EVTCHNL_TYPE_EVT);
		if (ret < 0) {
			DRM_ERROR("Error allocating in-event channel\n");
			goto fail;
		}
	}
	front_info->num_evt_pairs = cfg->num_connectors;
	return 0;

fail:
	xen_drm_front_evtchnl_free_all(front_info);
	return ret;
}

static int evtchnl_publish(struct xenbus_transaction xbt,
			   struct xen_drm_front_evtchnl *evtchnl,
			   const char *path, const char *node_ring,
			   const char *node_chnl)
{
	struct xenbus_device *xb_dev = evtchnl->front_info->xb_dev;
	int ret;

	/* write control channel ring reference */
	ret = xenbus_printf(xbt, path, node_ring, "%u", evtchnl->gref);
	if (ret < 0) {
		xenbus_dev_error(xb_dev, ret, "writing ring-ref");
		return ret;
	}

	/* write event channel ring reference */
	ret = xenbus_printf(xbt, path, node_chnl, "%u", evtchnl->port);
	if (ret < 0) {
		xenbus_dev_error(xb_dev, ret, "writing event channel");
		return ret;
	}

	return 0;
}

int xen_drm_front_evtchnl_publish_all(struct xen_drm_front_info *front_info)
{
	struct xenbus_transaction xbt;
	struct xen_drm_front_cfg *plat_data;
	int ret, conn;

	plat_data = &front_info->cfg;

again:
	ret = xenbus_transaction_start(&xbt);
	if (ret < 0) {
		xenbus_dev_fatal(front_info->xb_dev, ret,
				 "starting transaction");
		return ret;
	}

	for (conn = 0; conn < plat_data->num_connectors; conn++) {
		ret = evtchnl_publish(xbt, &front_info->evt_pairs[conn].req,
				      plat_data->connectors[conn].xenstore_path,
				      XENDISPL_FIELD_REQ_RING_REF,
				      XENDISPL_FIELD_REQ_CHANNEL);
		if (ret < 0)
			goto fail;

		ret = evtchnl_publish(xbt, &front_info->evt_pairs[conn].evt,
				      plat_data->connectors[conn].xenstore_path,
				      XENDISPL_FIELD_EVT_RING_REF,
				      XENDISPL_FIELD_EVT_CHANNEL);
		if (ret < 0)
			goto fail;
	}

	ret = xenbus_transaction_end(xbt, 0);
	if (ret < 0) {
		if (ret == -EAGAIN)
			goto again;

		xenbus_dev_fatal(front_info->xb_dev, ret,
				 "completing transaction");
		goto fail_to_end;
	}

	return 0;

fail:
	xenbus_transaction_end(xbt, 1);

fail_to_end:
	xenbus_dev_fatal(front_info->xb_dev, ret, "writing Xen store");
	return ret;
}

void xen_drm_front_evtchnl_flush(struct xen_drm_front_evtchnl *evtchnl)
{
	int notify;

	evtchnl->u.req.ring.req_prod_pvt++;
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&evtchnl->u.req.ring, notify);
	if (notify)
		notify_remote_via_irq(evtchnl->irq);
}

void xen_drm_front_evtchnl_set_state(struct xen_drm_front_info *front_info,
				     enum xen_drm_front_evtchnl_state state)
{
	unsigned long flags;
	int i;

	if (!front_info->evt_pairs)
		return;

	spin_lock_irqsave(&front_info->io_lock, flags);
	for (i = 0; i < front_info->num_evt_pairs; i++) {
		front_info->evt_pairs[i].req.state = state;
		front_info->evt_pairs[i].evt.state = state;
	}
	spin_unlock_irqrestore(&front_info->io_lock, flags);
}

void xen_drm_front_evtchnl_free_all(struct xen_drm_front_info *front_info)
{
	int i;

	if (!front_info->evt_pairs)
		return;

	for (i = 0; i < front_info->num_evt_pairs; i++) {
		evtchnl_free(front_info, &front_info->evt_pairs[i].req);
		evtchnl_free(front_info, &front_info->evt_pairs[i].evt);
	}

	kfree(front_info->evt_pairs);
	front_info->evt_pairs = NULL;
}
