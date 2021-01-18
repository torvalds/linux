// SPDX-License-Identifier: GPL-2.0
/*
 * Serial Attached SCSI (SAS) Event processing
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 */

#include <linux/export.h>
#include <scsi/scsi_host.h>
#include "sas_internal.h"

int sas_queue_work(struct sas_ha_struct *ha, struct sas_work *sw)
{
	/* it's added to the defer_q when draining so return succeed */
	int rc = 1;

	if (!test_bit(SAS_HA_REGISTERED, &ha->state))
		return 0;

	if (test_bit(SAS_HA_DRAINING, &ha->state)) {
		/* add it to the defer list, if not already pending */
		if (list_empty(&sw->drain_node))
			list_add_tail(&sw->drain_node, &ha->defer_q);
	} else
		rc = queue_work(ha->event_q, &sw->work);

	return rc;
}

static int sas_queue_event(int event, struct sas_work *work,
			    struct sas_ha_struct *ha)
{
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&ha->lock, flags);
	rc = sas_queue_work(ha, work);
	spin_unlock_irqrestore(&ha->lock, flags);

	return rc;
}


void __sas_drain_work(struct sas_ha_struct *ha)
{
	struct sas_work *sw, *_sw;
	int ret;

	set_bit(SAS_HA_DRAINING, &ha->state);
	/* flush submitters */
	spin_lock_irq(&ha->lock);
	spin_unlock_irq(&ha->lock);

	drain_workqueue(ha->event_q);
	drain_workqueue(ha->disco_q);

	spin_lock_irq(&ha->lock);
	clear_bit(SAS_HA_DRAINING, &ha->state);
	list_for_each_entry_safe(sw, _sw, &ha->defer_q, drain_node) {
		list_del_init(&sw->drain_node);
		ret = sas_queue_work(ha, sw);
		if (ret != 1)
			sas_free_event(to_asd_sas_event(&sw->work));

	}
	spin_unlock_irq(&ha->lock);
}

int sas_drain_work(struct sas_ha_struct *ha)
{
	int err;

	err = mutex_lock_interruptible(&ha->drain_mutex);
	if (err)
		return err;
	if (test_bit(SAS_HA_REGISTERED, &ha->state))
		__sas_drain_work(ha);
	mutex_unlock(&ha->drain_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(sas_drain_work);

void sas_disable_revalidation(struct sas_ha_struct *ha)
{
	mutex_lock(&ha->disco_mutex);
	set_bit(SAS_HA_ATA_EH_ACTIVE, &ha->state);
	mutex_unlock(&ha->disco_mutex);
}

void sas_enable_revalidation(struct sas_ha_struct *ha)
{
	int i;

	mutex_lock(&ha->disco_mutex);
	clear_bit(SAS_HA_ATA_EH_ACTIVE, &ha->state);
	for (i = 0; i < ha->num_phys; i++) {
		struct asd_sas_port *port = ha->sas_port[i];
		const int ev = DISCE_REVALIDATE_DOMAIN;
		struct sas_discovery *d = &port->disc;
		struct asd_sas_phy *sas_phy;

		if (!test_and_clear_bit(ev, &d->pending))
			continue;

		if (list_empty(&port->phy_list))
			continue;

		sas_phy = container_of(port->phy_list.next, struct asd_sas_phy,
				port_phy_el);
		sas_notify_port_event(sas_phy, PORTE_BROADCAST_RCVD);
	}
	mutex_unlock(&ha->disco_mutex);
}


static void sas_port_event_worker(struct work_struct *work)
{
	struct asd_sas_event *ev = to_asd_sas_event(work);

	sas_port_event_fns[ev->event](work);
	sas_free_event(ev);
}

static void sas_phy_event_worker(struct work_struct *work)
{
	struct asd_sas_event *ev = to_asd_sas_event(work);

	sas_phy_event_fns[ev->event](work);
	sas_free_event(ev);
}

static int __sas_notify_port_event(struct asd_sas_phy *phy,
				   enum port_event event,
				   struct asd_sas_event *ev)
{
	struct sas_ha_struct *ha = phy->ha;
	int ret;

	BUG_ON(event >= PORT_NUM_EVENTS);

	INIT_SAS_EVENT(ev, sas_port_event_worker, phy, event);

	ret = sas_queue_event(event, &ev->work, ha);
	if (ret != 1)
		sas_free_event(ev);

	return ret;
}

int sas_notify_port_event_gfp(struct asd_sas_phy *phy, enum port_event event,
			      gfp_t gfp_flags)
{
	struct asd_sas_event *ev;

	ev = sas_alloc_event_gfp(phy, gfp_flags);
	if (!ev)
		return -ENOMEM;

	return __sas_notify_port_event(phy, event, ev);
}
EXPORT_SYMBOL_GPL(sas_notify_port_event_gfp);

int sas_notify_port_event(struct asd_sas_phy *phy, enum port_event event)
{
	struct asd_sas_event *ev;

	ev = sas_alloc_event(phy);
	if (!ev)
		return -ENOMEM;

	return __sas_notify_port_event(phy, event, ev);
}
EXPORT_SYMBOL_GPL(sas_notify_port_event);

static inline int __sas_notify_phy_event(struct asd_sas_phy *phy,
					 enum phy_event event,
					 struct asd_sas_event *ev)
{
	struct sas_ha_struct *ha = phy->ha;
	int ret;

	BUG_ON(event >= PHY_NUM_EVENTS);

	INIT_SAS_EVENT(ev, sas_phy_event_worker, phy, event);

	ret = sas_queue_event(event, &ev->work, ha);
	if (ret != 1)
		sas_free_event(ev);

	return ret;
}

int sas_notify_phy_event_gfp(struct asd_sas_phy *phy, enum phy_event event,
			     gfp_t gfp_flags)
{
	struct asd_sas_event *ev;

	ev = sas_alloc_event_gfp(phy, gfp_flags);
	if (!ev)
		return -ENOMEM;

	return __sas_notify_phy_event(phy, event, ev);
}
EXPORT_SYMBOL_GPL(sas_notify_phy_event_gfp);

int sas_notify_phy_event(struct asd_sas_phy *phy, enum phy_event event)
{
	struct asd_sas_event *ev;

	ev = sas_alloc_event(phy);
	if (!ev)
		return -ENOMEM;

	return __sas_notify_phy_event(phy, event, ev);
}
EXPORT_SYMBOL_GPL(sas_notify_phy_event);
