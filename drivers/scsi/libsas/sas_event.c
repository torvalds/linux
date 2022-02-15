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

void sas_queue_deferred_work(struct sas_ha_struct *ha)
{
	struct sas_work *sw, *_sw;
	int ret;

	spin_lock_irq(&ha->lock);
	list_for_each_entry_safe(sw, _sw, &ha->defer_q, drain_node) {
		list_del_init(&sw->drain_node);
		ret = sas_queue_work(ha, sw);
		if (ret != 1) {
			pm_runtime_put(ha->dev);
			sas_free_event(to_asd_sas_event(&sw->work));
		}
	}
	spin_unlock_irq(&ha->lock);
}

void __sas_drain_work(struct sas_ha_struct *ha)
{
	set_bit(SAS_HA_DRAINING, &ha->state);
	/* flush submitters */
	spin_lock_irq(&ha->lock);
	spin_unlock_irq(&ha->lock);

	drain_workqueue(ha->event_q);
	drain_workqueue(ha->disco_q);

	clear_bit(SAS_HA_DRAINING, &ha->state);
	sas_queue_deferred_work(ha);
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

		spin_lock(&port->phy_list_lock);
		if (list_empty(&port->phy_list)) {
			spin_unlock(&port->phy_list_lock);
			continue;
		}

		sas_phy = container_of(port->phy_list.next, struct asd_sas_phy,
				port_phy_el);
		spin_unlock(&port->phy_list_lock);
		sas_notify_port_event(sas_phy,
				PORTE_BROADCAST_RCVD, GFP_KERNEL);
	}
	mutex_unlock(&ha->disco_mutex);
}


static void sas_port_event_worker(struct work_struct *work)
{
	struct asd_sas_event *ev = to_asd_sas_event(work);
	struct asd_sas_phy *phy = ev->phy;
	struct sas_ha_struct *ha = phy->ha;

	sas_port_event_fns[ev->event](work);
	pm_runtime_put(ha->dev);
	sas_free_event(ev);
}

static void sas_phy_event_worker(struct work_struct *work)
{
	struct asd_sas_event *ev = to_asd_sas_event(work);
	struct asd_sas_phy *phy = ev->phy;
	struct sas_ha_struct *ha = phy->ha;

	sas_phy_event_fns[ev->event](work);
	pm_runtime_put(ha->dev);
	sas_free_event(ev);
}

/* defer works of new phys during suspend */
static bool sas_defer_event(struct asd_sas_phy *phy, struct asd_sas_event *ev)
{
	struct sas_ha_struct *ha = phy->ha;
	unsigned long flags;
	bool deferred = false;

	spin_lock_irqsave(&ha->lock, flags);
	if (test_bit(SAS_HA_RESUMING, &ha->state) && !phy->suspended) {
		struct sas_work *sw = &ev->work;

		list_add_tail(&sw->drain_node, &ha->defer_q);
		deferred = true;
	}
	spin_unlock_irqrestore(&ha->lock, flags);
	return deferred;
}

int sas_notify_port_event(struct asd_sas_phy *phy, enum port_event event,
			  gfp_t gfp_flags)
{
	struct sas_ha_struct *ha = phy->ha;
	struct asd_sas_event *ev;
	int ret;

	BUG_ON(event >= PORT_NUM_EVENTS);

	ev = sas_alloc_event(phy, gfp_flags);
	if (!ev)
		return -ENOMEM;

	/* Call pm_runtime_put() with pairs in sas_port_event_worker() */
	pm_runtime_get_noresume(ha->dev);

	INIT_SAS_EVENT(ev, sas_port_event_worker, phy, event);

	if (sas_defer_event(phy, ev))
		return 0;

	ret = sas_queue_event(event, &ev->work, ha);
	if (ret != 1) {
		pm_runtime_put(ha->dev);
		sas_free_event(ev);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sas_notify_port_event);

int sas_notify_phy_event(struct asd_sas_phy *phy, enum phy_event event,
			 gfp_t gfp_flags)
{
	struct sas_ha_struct *ha = phy->ha;
	struct asd_sas_event *ev;
	int ret;

	BUG_ON(event >= PHY_NUM_EVENTS);

	ev = sas_alloc_event(phy, gfp_flags);
	if (!ev)
		return -ENOMEM;

	/* Call pm_runtime_put() with pairs in sas_phy_event_worker() */
	pm_runtime_get_noresume(ha->dev);

	INIT_SAS_EVENT(ev, sas_phy_event_worker, phy, event);

	if (sas_defer_event(phy, ev))
		return 0;

	ret = sas_queue_event(event, &ev->work, ha);
	if (ret != 1) {
		pm_runtime_put(ha->dev);
		sas_free_event(ev);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sas_notify_phy_event);
