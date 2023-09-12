// SPDX-License-Identifier: GPL-2.0-only
/*
 * Serial Attached SCSI (SAS) Transport Layer initialization
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <scsi/sas_ata.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_sas.h>

#include "sas_internal.h"

#include "scsi_sas_internal.h"

static struct kmem_cache *sas_task_cache;
static struct kmem_cache *sas_event_cache;

struct sas_task *sas_alloc_task(gfp_t flags)
{
	struct sas_task *task = kmem_cache_zalloc(sas_task_cache, flags);

	if (task) {
		spin_lock_init(&task->task_state_lock);
		task->task_state_flags = SAS_TASK_STATE_PENDING;
	}

	return task;
}

struct sas_task *sas_alloc_slow_task(gfp_t flags)
{
	struct sas_task *task = sas_alloc_task(flags);
	struct sas_task_slow *slow = kmalloc(sizeof(*slow), flags);

	if (!task || !slow) {
		if (task)
			kmem_cache_free(sas_task_cache, task);
		kfree(slow);
		return NULL;
	}

	task->slow_task = slow;
	slow->task = task;
	timer_setup(&slow->timer, NULL, 0);
	init_completion(&slow->completion);

	return task;
}

void sas_free_task(struct sas_task *task)
{
	if (task) {
		kfree(task->slow_task);
		kmem_cache_free(sas_task_cache, task);
	}
}

/*------------ SAS addr hash -----------*/
void sas_hash_addr(u8 *hashed, const u8 *sas_addr)
{
	const u32 poly = 0x00DB2777;
	u32 r = 0;
	int i;

	for (i = 0; i < SAS_ADDR_SIZE; i++) {
		int b;

		for (b = (SAS_ADDR_SIZE - 1); b >= 0; b--) {
			r <<= 1;
			if ((1 << b) & sas_addr[i]) {
				if (!(r & 0x01000000))
					r ^= poly;
			} else if (r & 0x01000000) {
				r ^= poly;
			}
		}
	}

	hashed[0] = (r >> 16) & 0xFF;
	hashed[1] = (r >> 8) & 0xFF;
	hashed[2] = r & 0xFF;
}

int sas_register_ha(struct sas_ha_struct *sas_ha)
{
	char name[64];
	int error = 0;

	mutex_init(&sas_ha->disco_mutex);
	spin_lock_init(&sas_ha->phy_port_lock);
	sas_hash_addr(sas_ha->hashed_sas_addr, sas_ha->sas_addr);

	set_bit(SAS_HA_REGISTERED, &sas_ha->state);
	spin_lock_init(&sas_ha->lock);
	mutex_init(&sas_ha->drain_mutex);
	init_waitqueue_head(&sas_ha->eh_wait_q);
	INIT_LIST_HEAD(&sas_ha->defer_q);
	INIT_LIST_HEAD(&sas_ha->eh_dev_q);

	sas_ha->event_thres = SAS_PHY_SHUTDOWN_THRES;

	error = sas_register_phys(sas_ha);
	if (error) {
		pr_notice("couldn't register sas phys:%d\n", error);
		return error;
	}

	error = sas_register_ports(sas_ha);
	if (error) {
		pr_notice("couldn't register sas ports:%d\n", error);
		goto Undo_phys;
	}

	error = -ENOMEM;
	snprintf(name, sizeof(name), "%s_event_q", dev_name(sas_ha->dev));
	sas_ha->event_q = create_singlethread_workqueue(name);
	if (!sas_ha->event_q)
		goto Undo_ports;

	snprintf(name, sizeof(name), "%s_disco_q", dev_name(sas_ha->dev));
	sas_ha->disco_q = create_singlethread_workqueue(name);
	if (!sas_ha->disco_q)
		goto Undo_event_q;

	INIT_LIST_HEAD(&sas_ha->eh_done_q);
	INIT_LIST_HEAD(&sas_ha->eh_ata_q);

	return 0;

Undo_event_q:
	destroy_workqueue(sas_ha->event_q);
Undo_ports:
	sas_unregister_ports(sas_ha);
Undo_phys:

	return error;
}
EXPORT_SYMBOL_GPL(sas_register_ha);

static void sas_disable_events(struct sas_ha_struct *sas_ha)
{
	/* Set the state to unregistered to avoid further unchained
	 * events to be queued, and flush any in-progress drainers
	 */
	mutex_lock(&sas_ha->drain_mutex);
	spin_lock_irq(&sas_ha->lock);
	clear_bit(SAS_HA_REGISTERED, &sas_ha->state);
	spin_unlock_irq(&sas_ha->lock);
	__sas_drain_work(sas_ha);
	mutex_unlock(&sas_ha->drain_mutex);
}

int sas_unregister_ha(struct sas_ha_struct *sas_ha)
{
	sas_disable_events(sas_ha);
	sas_unregister_ports(sas_ha);

	/* flush unregistration work */
	mutex_lock(&sas_ha->drain_mutex);
	__sas_drain_work(sas_ha);
	mutex_unlock(&sas_ha->drain_mutex);

	destroy_workqueue(sas_ha->disco_q);
	destroy_workqueue(sas_ha->event_q);

	return 0;
}
EXPORT_SYMBOL_GPL(sas_unregister_ha);

static int sas_get_linkerrors(struct sas_phy *phy)
{
	if (scsi_is_sas_phy_local(phy)) {
		struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
		struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
		struct asd_sas_phy *asd_phy = sas_ha->sas_phy[phy->number];
		struct sas_internal *i =
			to_sas_internal(sas_ha->shost->transportt);

		return i->dft->lldd_control_phy(asd_phy, PHY_FUNC_GET_EVENTS, NULL);
	}

	return sas_smp_get_phy_events(phy);
}

int sas_try_ata_reset(struct asd_sas_phy *asd_phy)
{
	struct domain_device *dev = NULL;

	/* try to route user requested link resets through libata */
	if (asd_phy->port)
		dev = asd_phy->port->port_dev;

	/* validate that dev has been probed */
	if (dev)
		dev = sas_find_dev_by_rphy(dev->rphy);

	if (dev && dev_is_sata(dev)) {
		sas_ata_schedule_reset(dev);
		sas_ata_wait_eh(dev);
		return 0;
	}

	return -ENODEV;
}

/*
 * transport_sas_phy_reset - reset a phy and permit libata to manage the link
 *
 * phy reset request via sysfs in host workqueue context so we know we
 * can block on eh and safely traverse the domain_device topology
 */
static int transport_sas_phy_reset(struct sas_phy *phy, int hard_reset)
{
	enum phy_func reset_type;

	if (hard_reset)
		reset_type = PHY_FUNC_HARD_RESET;
	else
		reset_type = PHY_FUNC_LINK_RESET;

	if (scsi_is_sas_phy_local(phy)) {
		struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
		struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
		struct asd_sas_phy *asd_phy = sas_ha->sas_phy[phy->number];
		struct sas_internal *i =
			to_sas_internal(sas_ha->shost->transportt);

		if (!hard_reset && sas_try_ata_reset(asd_phy) == 0)
			return 0;
		return i->dft->lldd_control_phy(asd_phy, reset_type, NULL);
	} else {
		struct sas_rphy *rphy = dev_to_rphy(phy->dev.parent);
		struct domain_device *ddev = sas_find_dev_by_rphy(rphy);
		struct domain_device *ata_dev = sas_ex_to_ata(ddev, phy->number);

		if (ata_dev && !hard_reset) {
			sas_ata_schedule_reset(ata_dev);
			sas_ata_wait_eh(ata_dev);
			return 0;
		} else
			return sas_smp_phy_control(ddev, phy->number, reset_type, NULL);
	}
}

int sas_phy_enable(struct sas_phy *phy, int enable)
{
	int ret;
	enum phy_func cmd;

	if (enable)
		cmd = PHY_FUNC_LINK_RESET;
	else
		cmd = PHY_FUNC_DISABLE;

	if (scsi_is_sas_phy_local(phy)) {
		struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
		struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
		struct asd_sas_phy *asd_phy = sas_ha->sas_phy[phy->number];
		struct sas_internal *i =
			to_sas_internal(sas_ha->shost->transportt);

		if (enable)
			ret = transport_sas_phy_reset(phy, 0);
		else
			ret = i->dft->lldd_control_phy(asd_phy, cmd, NULL);
	} else {
		struct sas_rphy *rphy = dev_to_rphy(phy->dev.parent);
		struct domain_device *ddev = sas_find_dev_by_rphy(rphy);

		if (enable)
			ret = transport_sas_phy_reset(phy, 0);
		else
			ret = sas_smp_phy_control(ddev, phy->number, cmd, NULL);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(sas_phy_enable);

int sas_phy_reset(struct sas_phy *phy, int hard_reset)
{
	int ret;
	enum phy_func reset_type;

	if (!phy->enabled)
		return -ENODEV;

	if (hard_reset)
		reset_type = PHY_FUNC_HARD_RESET;
	else
		reset_type = PHY_FUNC_LINK_RESET;

	if (scsi_is_sas_phy_local(phy)) {
		struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
		struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
		struct asd_sas_phy *asd_phy = sas_ha->sas_phy[phy->number];
		struct sas_internal *i =
			to_sas_internal(sas_ha->shost->transportt);

		ret = i->dft->lldd_control_phy(asd_phy, reset_type, NULL);
	} else {
		struct sas_rphy *rphy = dev_to_rphy(phy->dev.parent);
		struct domain_device *ddev = sas_find_dev_by_rphy(rphy);
		ret = sas_smp_phy_control(ddev, phy->number, reset_type, NULL);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(sas_phy_reset);

int sas_set_phy_speed(struct sas_phy *phy,
		      struct sas_phy_linkrates *rates)
{
	int ret;

	if ((rates->minimum_linkrate &&
	     rates->minimum_linkrate > phy->maximum_linkrate) ||
	    (rates->maximum_linkrate &&
	     rates->maximum_linkrate < phy->minimum_linkrate))
		return -EINVAL;

	if (rates->minimum_linkrate &&
	    rates->minimum_linkrate < phy->minimum_linkrate_hw)
		rates->minimum_linkrate = phy->minimum_linkrate_hw;

	if (rates->maximum_linkrate &&
	    rates->maximum_linkrate > phy->maximum_linkrate_hw)
		rates->maximum_linkrate = phy->maximum_linkrate_hw;

	if (scsi_is_sas_phy_local(phy)) {
		struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
		struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
		struct asd_sas_phy *asd_phy = sas_ha->sas_phy[phy->number];
		struct sas_internal *i =
			to_sas_internal(sas_ha->shost->transportt);

		ret = i->dft->lldd_control_phy(asd_phy, PHY_FUNC_SET_LINK_RATE,
					       rates);
	} else {
		struct sas_rphy *rphy = dev_to_rphy(phy->dev.parent);
		struct domain_device *ddev = sas_find_dev_by_rphy(rphy);
		ret = sas_smp_phy_control(ddev, phy->number,
					  PHY_FUNC_LINK_RESET, rates);

	}

	return ret;
}

void sas_prep_resume_ha(struct sas_ha_struct *ha)
{
	int i;

	set_bit(SAS_HA_REGISTERED, &ha->state);
	set_bit(SAS_HA_RESUMING, &ha->state);

	/* clear out any stale link events/data from the suspension path */
	for (i = 0; i < ha->num_phys; i++) {
		struct asd_sas_phy *phy = ha->sas_phy[i];

		memset(phy->attached_sas_addr, 0, SAS_ADDR_SIZE);
		phy->frame_rcvd_size = 0;
	}
}
EXPORT_SYMBOL(sas_prep_resume_ha);

static int phys_suspended(struct sas_ha_struct *ha)
{
	int i, rc = 0;

	for (i = 0; i < ha->num_phys; i++) {
		struct asd_sas_phy *phy = ha->sas_phy[i];

		if (phy->suspended)
			rc++;
	}

	return rc;
}

static void sas_resume_insert_broadcast_ha(struct sas_ha_struct *ha)
{
	int i;

	for (i = 0; i < ha->num_phys; i++) {
		struct asd_sas_port *port = ha->sas_port[i];
		struct domain_device *dev = port->port_dev;

		if (dev && dev_is_expander(dev->dev_type)) {
			struct asd_sas_phy *first_phy;

			spin_lock(&port->phy_list_lock);
			first_phy = list_first_entry_or_null(
				&port->phy_list, struct asd_sas_phy,
				port_phy_el);
			spin_unlock(&port->phy_list_lock);

			if (first_phy)
				sas_notify_port_event(first_phy,
					PORTE_BROADCAST_RCVD, GFP_KERNEL);
		}
	}
}

static void _sas_resume_ha(struct sas_ha_struct *ha, bool drain)
{
	const unsigned long tmo = msecs_to_jiffies(25000);
	int i;

	/* deform ports on phys that did not resume
	 * at this point we may be racing the phy coming back (as posted
	 * by the lldd).  So we post the event and once we are in the
	 * libsas context check that the phy remains suspended before
	 * tearing it down.
	 */
	i = phys_suspended(ha);
	if (i)
		dev_info(ha->dev, "waiting up to 25 seconds for %d phy%s to resume\n",
			 i, i > 1 ? "s" : "");
	wait_event_timeout(ha->eh_wait_q, phys_suspended(ha) == 0, tmo);
	for (i = 0; i < ha->num_phys; i++) {
		struct asd_sas_phy *phy = ha->sas_phy[i];

		if (phy->suspended) {
			dev_warn(&phy->phy->dev, "resume timeout\n");
			sas_notify_phy_event(phy, PHYE_RESUME_TIMEOUT,
					     GFP_KERNEL);
		}
	}

	/* all phys are back up or timed out, turn on i/o so we can
	 * flush out disks that did not return
	 */
	scsi_unblock_requests(ha->shost);
	if (drain)
		sas_drain_work(ha);
	clear_bit(SAS_HA_RESUMING, &ha->state);

	sas_queue_deferred_work(ha);
	/* send event PORTE_BROADCAST_RCVD to identify some new inserted
	 * disks for expander
	 */
	sas_resume_insert_broadcast_ha(ha);
}

void sas_resume_ha(struct sas_ha_struct *ha)
{
	_sas_resume_ha(ha, true);
}
EXPORT_SYMBOL(sas_resume_ha);

/* A no-sync variant, which does not call sas_drain_ha(). */
void sas_resume_ha_no_sync(struct sas_ha_struct *ha)
{
	_sas_resume_ha(ha, false);
}
EXPORT_SYMBOL(sas_resume_ha_no_sync);

void sas_suspend_ha(struct sas_ha_struct *ha)
{
	int i;

	sas_disable_events(ha);
	scsi_block_requests(ha->shost);
	for (i = 0; i < ha->num_phys; i++) {
		struct asd_sas_port *port = ha->sas_port[i];

		sas_discover_event(port, DISCE_SUSPEND);
	}

	/* flush suspend events while unregistered */
	mutex_lock(&ha->drain_mutex);
	__sas_drain_work(ha);
	mutex_unlock(&ha->drain_mutex);
}
EXPORT_SYMBOL(sas_suspend_ha);

static void sas_phy_release(struct sas_phy *phy)
{
	kfree(phy->hostdata);
	phy->hostdata = NULL;
}

static void phy_reset_work(struct work_struct *work)
{
	struct sas_phy_data *d = container_of(work, typeof(*d), reset_work.work);

	d->reset_result = transport_sas_phy_reset(d->phy, d->hard_reset);
}

static void phy_enable_work(struct work_struct *work)
{
	struct sas_phy_data *d = container_of(work, typeof(*d), enable_work.work);

	d->enable_result = sas_phy_enable(d->phy, d->enable);
}

static int sas_phy_setup(struct sas_phy *phy)
{
	struct sas_phy_data *d = kzalloc(sizeof(*d), GFP_KERNEL);

	if (!d)
		return -ENOMEM;

	mutex_init(&d->event_lock);
	INIT_SAS_WORK(&d->reset_work, phy_reset_work);
	INIT_SAS_WORK(&d->enable_work, phy_enable_work);
	d->phy = phy;
	phy->hostdata = d;

	return 0;
}

static int queue_phy_reset(struct sas_phy *phy, int hard_reset)
{
	struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
	struct sas_ha_struct *ha = SHOST_TO_SAS_HA(shost);
	struct sas_phy_data *d = phy->hostdata;
	int rc;

	if (!d)
		return -ENOMEM;

	pm_runtime_get_sync(ha->dev);
	/* libsas workqueue coordinates ata-eh reset with discovery */
	mutex_lock(&d->event_lock);
	d->reset_result = 0;
	d->hard_reset = hard_reset;

	spin_lock_irq(&ha->lock);
	sas_queue_work(ha, &d->reset_work);
	spin_unlock_irq(&ha->lock);

	rc = sas_drain_work(ha);
	if (rc == 0)
		rc = d->reset_result;
	mutex_unlock(&d->event_lock);
	pm_runtime_put_sync(ha->dev);

	return rc;
}

static int queue_phy_enable(struct sas_phy *phy, int enable)
{
	struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
	struct sas_ha_struct *ha = SHOST_TO_SAS_HA(shost);
	struct sas_phy_data *d = phy->hostdata;
	int rc;

	if (!d)
		return -ENOMEM;

	pm_runtime_get_sync(ha->dev);
	/* libsas workqueue coordinates ata-eh reset with discovery */
	mutex_lock(&d->event_lock);
	d->enable_result = 0;
	d->enable = enable;

	spin_lock_irq(&ha->lock);
	sas_queue_work(ha, &d->enable_work);
	spin_unlock_irq(&ha->lock);

	rc = sas_drain_work(ha);
	if (rc == 0)
		rc = d->enable_result;
	mutex_unlock(&d->event_lock);
	pm_runtime_put_sync(ha->dev);

	return rc;
}

static struct sas_function_template sft = {
	.phy_enable = queue_phy_enable,
	.phy_reset = queue_phy_reset,
	.phy_setup = sas_phy_setup,
	.phy_release = sas_phy_release,
	.set_phy_speed = sas_set_phy_speed,
	.get_linkerrors = sas_get_linkerrors,
	.smp_handler = sas_smp_handler,
};

static inline ssize_t phy_event_threshold_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);

	return scnprintf(buf, PAGE_SIZE, "%u\n", sha->event_thres);
}

static inline ssize_t phy_event_threshold_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);

	sha->event_thres = simple_strtol(buf, NULL, 10);

	/* threshold cannot be set too small */
	if (sha->event_thres < 32)
		sha->event_thres = 32;

	return count;
}

DEVICE_ATTR(phy_event_threshold,
	S_IRUGO|S_IWUSR,
	phy_event_threshold_show,
	phy_event_threshold_store);
EXPORT_SYMBOL_GPL(dev_attr_phy_event_threshold);

struct scsi_transport_template *
sas_domain_attach_transport(struct sas_domain_function_template *dft)
{
	struct scsi_transport_template *stt = sas_attach_transport(&sft);
	struct sas_internal *i;

	if (!stt)
		return stt;

	i = to_sas_internal(stt);
	i->dft = dft;
	stt->create_work_queue = 1;
	stt->eh_strategy_handler = sas_scsi_recover_host;

	return stt;
}
EXPORT_SYMBOL_GPL(sas_domain_attach_transport);

struct asd_sas_event *sas_alloc_event(struct asd_sas_phy *phy,
				      gfp_t gfp_flags)
{
	struct asd_sas_event *event;
	struct sas_ha_struct *sas_ha = phy->ha;
	struct sas_internal *i =
		to_sas_internal(sas_ha->shost->transportt);

	event = kmem_cache_zalloc(sas_event_cache, gfp_flags);
	if (!event)
		return NULL;

	atomic_inc(&phy->event_nr);

	if (atomic_read(&phy->event_nr) > phy->ha->event_thres) {
		if (i->dft->lldd_control_phy) {
			if (cmpxchg(&phy->in_shutdown, 0, 1) == 0) {
				pr_notice("The phy%d bursting events, shut it down.\n",
					  phy->id);
				sas_notify_phy_event(phy, PHYE_SHUTDOWN,
						     gfp_flags);
			}
		} else {
			/* Do not support PHY control, stop allocating events */
			WARN_ONCE(1, "PHY control not supported.\n");
			kmem_cache_free(sas_event_cache, event);
			atomic_dec(&phy->event_nr);
			event = NULL;
		}
	}

	return event;
}

void sas_free_event(struct asd_sas_event *event)
{
	struct asd_sas_phy *phy = event->phy;

	kmem_cache_free(sas_event_cache, event);
	atomic_dec(&phy->event_nr);
}

/* ---------- SAS Class register/unregister ---------- */

static int __init sas_class_init(void)
{
	sas_task_cache = KMEM_CACHE(sas_task, SLAB_HWCACHE_ALIGN);
	if (!sas_task_cache)
		goto out;

	sas_event_cache = KMEM_CACHE(asd_sas_event, SLAB_HWCACHE_ALIGN);
	if (!sas_event_cache)
		goto free_task_kmem;

	return 0;
free_task_kmem:
	kmem_cache_destroy(sas_task_cache);
out:
	return -ENOMEM;
}

static void __exit sas_class_exit(void)
{
	kmem_cache_destroy(sas_task_cache);
	kmem_cache_destroy(sas_event_cache);
}

MODULE_AUTHOR("Luben Tuikov <luben_tuikov@adaptec.com>");
MODULE_DESCRIPTION("SAS Transport Layer");
MODULE_LICENSE("GPL v2");

module_init(sas_class_init);
module_exit(sas_class_exit);

