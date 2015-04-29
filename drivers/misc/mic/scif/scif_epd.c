/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 *
 */
#include "scif_main.h"
#include "scif_map.h"

void scif_cleanup_ep_qp(struct scif_endpt *ep)
{
	struct scif_qp *qp = ep->qp_info.qp;

	if (qp->outbound_q.rb_base) {
		scif_iounmap((void *)qp->outbound_q.rb_base,
			     qp->outbound_q.size, ep->remote_dev);
		qp->outbound_q.rb_base = NULL;
	}
	if (qp->remote_qp) {
		scif_iounmap((void *)qp->remote_qp,
			     sizeof(struct scif_qp), ep->remote_dev);
		qp->remote_qp = NULL;
	}
	if (qp->local_qp) {
		scif_unmap_single(qp->local_qp, ep->remote_dev,
				  sizeof(struct scif_qp));
		qp->local_qp = 0x0;
	}
	if (qp->local_buf) {
		scif_unmap_single(qp->local_buf, ep->remote_dev,
				  SCIF_ENDPT_QP_SIZE);
		qp->local_buf = 0;
	}
}

void scif_teardown_ep(void *endpt)
{
	struct scif_endpt *ep = endpt;
	struct scif_qp *qp = ep->qp_info.qp;

	if (qp) {
		spin_lock(&ep->lock);
		scif_cleanup_ep_qp(ep);
		spin_unlock(&ep->lock);
		kfree(qp->inbound_q.rb_base);
		kfree(qp);
	}
}

/*
 * Enqueue the endpoint to the zombie list for cleanup.
 * The endpoint should not be accessed once this API returns.
 */
void scif_add_epd_to_zombie_list(struct scif_endpt *ep, bool eplock_held)
{
	if (!eplock_held)
		spin_lock(&scif_info.eplock);
	spin_lock(&ep->lock);
	ep->state = SCIFEP_ZOMBIE;
	spin_unlock(&ep->lock);
	list_add_tail(&ep->list, &scif_info.zombie);
	scif_info.nr_zombies++;
	if (!eplock_held)
		spin_unlock(&scif_info.eplock);
	schedule_work(&scif_info.misc_work);
}

void scif_cleanup_zombie_epd(void)
{
	struct list_head *pos, *tmpq;
	struct scif_endpt *ep;

	spin_lock(&scif_info.eplock);
	list_for_each_safe(pos, tmpq, &scif_info.zombie) {
		ep = list_entry(pos, struct scif_endpt, list);
		list_del(pos);
		scif_info.nr_zombies--;
		kfree(ep);
	}
	spin_unlock(&scif_info.eplock);
}
