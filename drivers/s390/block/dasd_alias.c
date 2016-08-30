/*
 * PAV alias management for the DASD ECKD discipline
 *
 * Copyright IBM Corp. 2007
 * Author(s): Stefan Weinhuber <wein@de.ibm.com>
 */

#define KMSG_COMPONENT "dasd-eckd"

#include <linux/list.h>
#include <linux/slab.h>
#include <asm/ebcdic.h>
#include "dasd_int.h"
#include "dasd_eckd.h"

#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#endif				/* PRINTK_HEADER */
#define PRINTK_HEADER "dasd(eckd):"


/*
 * General concept of alias management:
 * - PAV and DASD alias management is specific to the eckd discipline.
 * - A device is connected to an lcu as long as the device exists.
 *   dasd_alias_make_device_known_to_lcu will be called wenn the
 *   device is checked by the eckd discipline and
 *   dasd_alias_disconnect_device_from_lcu will be called
 *   before the device is deleted.
 * - The dasd_alias_add_device / dasd_alias_remove_device
 *   functions mark the point when a device is 'ready for service'.
 * - A summary unit check is a rare occasion, but it is mandatory to
 *   support it. It requires some complex recovery actions before the
 *   devices can be used again (see dasd_alias_handle_summary_unit_check).
 * - dasd_alias_get_start_dev will find an alias device that can be used
 *   instead of the base device and does some (very simple) load balancing.
 *   This is the function that gets called for each I/O, so when improving
 *   something, this function should get faster or better, the rest has just
 *   to be correct.
 */


static void summary_unit_check_handling_work(struct work_struct *);
static void lcu_update_work(struct work_struct *);
static int _schedule_lcu_update(struct alias_lcu *, struct dasd_device *);

static struct alias_root aliastree = {
	.serverlist = LIST_HEAD_INIT(aliastree.serverlist),
	.lock = __SPIN_LOCK_UNLOCKED(aliastree.lock),
};

static struct alias_server *_find_server(struct dasd_uid *uid)
{
	struct alias_server *pos;
	list_for_each_entry(pos, &aliastree.serverlist, server) {
		if (!strncmp(pos->uid.vendor, uid->vendor,
			     sizeof(uid->vendor))
		    && !strncmp(pos->uid.serial, uid->serial,
				sizeof(uid->serial)))
			return pos;
	};
	return NULL;
}

static struct alias_lcu *_find_lcu(struct alias_server *server,
				   struct dasd_uid *uid)
{
	struct alias_lcu *pos;
	list_for_each_entry(pos, &server->lculist, lcu) {
		if (pos->uid.ssid == uid->ssid)
			return pos;
	};
	return NULL;
}

static struct alias_pav_group *_find_group(struct alias_lcu *lcu,
					   struct dasd_uid *uid)
{
	struct alias_pav_group *pos;
	__u8 search_unit_addr;

	/* for hyper pav there is only one group */
	if (lcu->pav == HYPER_PAV) {
		if (list_empty(&lcu->grouplist))
			return NULL;
		else
			return list_first_entry(&lcu->grouplist,
						struct alias_pav_group, group);
	}

	/* for base pav we have to find the group that matches the base */
	if (uid->type == UA_BASE_DEVICE)
		search_unit_addr = uid->real_unit_addr;
	else
		search_unit_addr = uid->base_unit_addr;
	list_for_each_entry(pos, &lcu->grouplist, group) {
		if (pos->uid.base_unit_addr == search_unit_addr &&
		    !strncmp(pos->uid.vduit, uid->vduit, sizeof(uid->vduit)))
			return pos;
	};
	return NULL;
}

static struct alias_server *_allocate_server(struct dasd_uid *uid)
{
	struct alias_server *server;

	server = kzalloc(sizeof(*server), GFP_KERNEL);
	if (!server)
		return ERR_PTR(-ENOMEM);
	memcpy(server->uid.vendor, uid->vendor, sizeof(uid->vendor));
	memcpy(server->uid.serial, uid->serial, sizeof(uid->serial));
	INIT_LIST_HEAD(&server->server);
	INIT_LIST_HEAD(&server->lculist);
	return server;
}

static void _free_server(struct alias_server *server)
{
	kfree(server);
}

static struct alias_lcu *_allocate_lcu(struct dasd_uid *uid)
{
	struct alias_lcu *lcu;

	lcu = kzalloc(sizeof(*lcu), GFP_KERNEL);
	if (!lcu)
		return ERR_PTR(-ENOMEM);
	lcu->uac = kzalloc(sizeof(*(lcu->uac)), GFP_KERNEL | GFP_DMA);
	if (!lcu->uac)
		goto out_err1;
	lcu->rsu_cqr = kzalloc(sizeof(*lcu->rsu_cqr), GFP_KERNEL | GFP_DMA);
	if (!lcu->rsu_cqr)
		goto out_err2;
	lcu->rsu_cqr->cpaddr = kzalloc(sizeof(struct ccw1),
				       GFP_KERNEL | GFP_DMA);
	if (!lcu->rsu_cqr->cpaddr)
		goto out_err3;
	lcu->rsu_cqr->data = kzalloc(16, GFP_KERNEL | GFP_DMA);
	if (!lcu->rsu_cqr->data)
		goto out_err4;

	memcpy(lcu->uid.vendor, uid->vendor, sizeof(uid->vendor));
	memcpy(lcu->uid.serial, uid->serial, sizeof(uid->serial));
	lcu->uid.ssid = uid->ssid;
	lcu->pav = NO_PAV;
	lcu->flags = NEED_UAC_UPDATE | UPDATE_PENDING;
	INIT_LIST_HEAD(&lcu->lcu);
	INIT_LIST_HEAD(&lcu->inactive_devices);
	INIT_LIST_HEAD(&lcu->active_devices);
	INIT_LIST_HEAD(&lcu->grouplist);
	INIT_WORK(&lcu->suc_data.worker, summary_unit_check_handling_work);
	INIT_DELAYED_WORK(&lcu->ruac_data.dwork, lcu_update_work);
	spin_lock_init(&lcu->lock);
	init_completion(&lcu->lcu_setup);
	return lcu;

out_err4:
	kfree(lcu->rsu_cqr->cpaddr);
out_err3:
	kfree(lcu->rsu_cqr);
out_err2:
	kfree(lcu->uac);
out_err1:
	kfree(lcu);
	return ERR_PTR(-ENOMEM);
}

static void _free_lcu(struct alias_lcu *lcu)
{
	kfree(lcu->rsu_cqr->data);
	kfree(lcu->rsu_cqr->cpaddr);
	kfree(lcu->rsu_cqr);
	kfree(lcu->uac);
	kfree(lcu);
}

/*
 * This is the function that will allocate all the server and lcu data,
 * so this function must be called first for a new device.
 * If the return value is 1, the lcu was already known before, if it
 * is 0, this is a new lcu.
 * Negative return code indicates that something went wrong (e.g. -ENOMEM)
 */
int dasd_alias_make_device_known_to_lcu(struct dasd_device *device)
{
	struct dasd_eckd_private *private;
	unsigned long flags;
	struct alias_server *server, *newserver;
	struct alias_lcu *lcu, *newlcu;
	struct dasd_uid uid;

	private = (struct dasd_eckd_private *) device->private;

	device->discipline->get_uid(device, &uid);
	spin_lock_irqsave(&aliastree.lock, flags);
	server = _find_server(&uid);
	if (!server) {
		spin_unlock_irqrestore(&aliastree.lock, flags);
		newserver = _allocate_server(&uid);
		if (IS_ERR(newserver))
			return PTR_ERR(newserver);
		spin_lock_irqsave(&aliastree.lock, flags);
		server = _find_server(&uid);
		if (!server) {
			list_add(&newserver->server, &aliastree.serverlist);
			server = newserver;
		} else {
			/* someone was faster */
			_free_server(newserver);
		}
	}

	lcu = _find_lcu(server, &uid);
	if (!lcu) {
		spin_unlock_irqrestore(&aliastree.lock, flags);
		newlcu = _allocate_lcu(&uid);
		if (IS_ERR(newlcu))
			return PTR_ERR(newlcu);
		spin_lock_irqsave(&aliastree.lock, flags);
		lcu = _find_lcu(server, &uid);
		if (!lcu) {
			list_add(&newlcu->lcu, &server->lculist);
			lcu = newlcu;
		} else {
			/* someone was faster */
			_free_lcu(newlcu);
		}
	}
	spin_lock(&lcu->lock);
	list_add(&device->alias_list, &lcu->inactive_devices);
	private->lcu = lcu;
	spin_unlock(&lcu->lock);
	spin_unlock_irqrestore(&aliastree.lock, flags);

	return 0;
}

/*
 * This function removes a device from the scope of alias management.
 * The complicated part is to make sure that it is not in use by
 * any of the workers. If necessary cancel the work.
 */
void dasd_alias_disconnect_device_from_lcu(struct dasd_device *device)
{
	struct dasd_eckd_private *private;
	unsigned long flags;
	struct alias_lcu *lcu;
	struct alias_server *server;
	int was_pending;
	struct dasd_uid uid;

	private = (struct dasd_eckd_private *) device->private;
	lcu = private->lcu;
	/* nothing to do if already disconnected */
	if (!lcu)
		return;
	device->discipline->get_uid(device, &uid);
	spin_lock_irqsave(&lcu->lock, flags);
	list_del_init(&device->alias_list);
	/* make sure that the workers don't use this device */
	if (device == lcu->suc_data.device) {
		spin_unlock_irqrestore(&lcu->lock, flags);
		cancel_work_sync(&lcu->suc_data.worker);
		spin_lock_irqsave(&lcu->lock, flags);
		if (device == lcu->suc_data.device) {
			dasd_put_device(device);
			lcu->suc_data.device = NULL;
		}
	}
	was_pending = 0;
	if (device == lcu->ruac_data.device) {
		spin_unlock_irqrestore(&lcu->lock, flags);
		was_pending = 1;
		cancel_delayed_work_sync(&lcu->ruac_data.dwork);
		spin_lock_irqsave(&lcu->lock, flags);
		if (device == lcu->ruac_data.device) {
			dasd_put_device(device);
			lcu->ruac_data.device = NULL;
		}
	}
	private->lcu = NULL;
	spin_unlock_irqrestore(&lcu->lock, flags);

	spin_lock_irqsave(&aliastree.lock, flags);
	spin_lock(&lcu->lock);
	if (list_empty(&lcu->grouplist) &&
	    list_empty(&lcu->active_devices) &&
	    list_empty(&lcu->inactive_devices)) {
		list_del(&lcu->lcu);
		spin_unlock(&lcu->lock);
		_free_lcu(lcu);
		lcu = NULL;
	} else {
		if (was_pending)
			_schedule_lcu_update(lcu, NULL);
		spin_unlock(&lcu->lock);
	}
	server = _find_server(&uid);
	if (server && list_empty(&server->lculist)) {
		list_del(&server->server);
		_free_server(server);
	}
	spin_unlock_irqrestore(&aliastree.lock, flags);
}

/*
 * This function assumes that the unit address configuration stored
 * in the lcu is up to date and will update the device uid before
 * adding it to a pav group.
 */

static int _add_device_to_lcu(struct alias_lcu *lcu,
			      struct dasd_device *device,
			      struct dasd_device *pos)
{

	struct dasd_eckd_private *private;
	struct alias_pav_group *group;
	struct dasd_uid uid;
	unsigned long flags;

	private = (struct dasd_eckd_private *) device->private;

	/* only lock if not already locked */
	if (device != pos)
		spin_lock_irqsave_nested(get_ccwdev_lock(device->cdev), flags,
					 CDEV_NESTED_SECOND);
	private->uid.type = lcu->uac->unit[private->uid.real_unit_addr].ua_type;
	private->uid.base_unit_addr =
		lcu->uac->unit[private->uid.real_unit_addr].base_ua;
	uid = private->uid;

	if (device != pos)
		spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);

	/* if we have no PAV anyway, we don't need to bother with PAV groups */
	if (lcu->pav == NO_PAV) {
		list_move(&device->alias_list, &lcu->active_devices);
		return 0;
	}

	group = _find_group(lcu, &uid);
	if (!group) {
		group = kzalloc(sizeof(*group), GFP_ATOMIC);
		if (!group)
			return -ENOMEM;
		memcpy(group->uid.vendor, uid.vendor, sizeof(uid.vendor));
		memcpy(group->uid.serial, uid.serial, sizeof(uid.serial));
		group->uid.ssid = uid.ssid;
		if (uid.type == UA_BASE_DEVICE)
			group->uid.base_unit_addr = uid.real_unit_addr;
		else
			group->uid.base_unit_addr = uid.base_unit_addr;
		memcpy(group->uid.vduit, uid.vduit, sizeof(uid.vduit));
		INIT_LIST_HEAD(&group->group);
		INIT_LIST_HEAD(&group->baselist);
		INIT_LIST_HEAD(&group->aliaslist);
		list_add(&group->group, &lcu->grouplist);
	}
	if (uid.type == UA_BASE_DEVICE)
		list_move(&device->alias_list, &group->baselist);
	else
		list_move(&device->alias_list, &group->aliaslist);
	private->pavgroup = group;
	return 0;
};

static void _remove_device_from_lcu(struct alias_lcu *lcu,
				    struct dasd_device *device)
{
	struct dasd_eckd_private *private;
	struct alias_pav_group *group;

	private = (struct dasd_eckd_private *) device->private;
	list_move(&device->alias_list, &lcu->inactive_devices);
	group = private->pavgroup;
	if (!group)
		return;
	private->pavgroup = NULL;
	if (list_empty(&group->baselist) && list_empty(&group->aliaslist)) {
		list_del(&group->group);
		kfree(group);
		return;
	}
	if (group->next == device)
		group->next = NULL;
};

static int
suborder_not_supported(struct dasd_ccw_req *cqr)
{
	char *sense;
	char reason;
	char msg_format;
	char msg_no;

	sense = dasd_get_sense(&cqr->irb);
	if (!sense)
		return 0;

	reason = sense[0];
	msg_format = (sense[7] & 0xF0);
	msg_no = (sense[7] & 0x0F);

	/* command reject, Format 0 MSG 4 - invalid parameter */
	if ((reason == 0x80) && (msg_format == 0x00) && (msg_no == 0x04))
		return 1;

	return 0;
}

static int read_unit_address_configuration(struct dasd_device *device,
					   struct alias_lcu *lcu)
{
	struct dasd_psf_prssd_data *prssdp;
	struct dasd_ccw_req *cqr;
	struct ccw1 *ccw;
	int rc;
	unsigned long flags;

	cqr = dasd_kmalloc_request(DASD_ECKD_MAGIC, 1 /* PSF */	+ 1 /* RSSD */,
				   (sizeof(struct dasd_psf_prssd_data)),
				   device);
	if (IS_ERR(cqr))
		return PTR_ERR(cqr);
	cqr->startdev = device;
	cqr->memdev = device;
	clear_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);
	cqr->retries = 10;
	cqr->expires = 20 * HZ;

	/* Prepare for Read Subsystem Data */
	prssdp = (struct dasd_psf_prssd_data *) cqr->data;
	memset(prssdp, 0, sizeof(struct dasd_psf_prssd_data));
	prssdp->order = PSF_ORDER_PRSSD;
	prssdp->suborder = 0x0e;	/* Read unit address configuration */
	/* all other bytes of prssdp must be zero */

	ccw = cqr->cpaddr;
	ccw->cmd_code = DASD_ECKD_CCW_PSF;
	ccw->count = sizeof(struct dasd_psf_prssd_data);
	ccw->flags |= CCW_FLAG_CC;
	ccw->cda = (__u32)(addr_t) prssdp;

	/* Read Subsystem Data - feature codes */
	memset(lcu->uac, 0, sizeof(*(lcu->uac)));

	ccw++;
	ccw->cmd_code = DASD_ECKD_CCW_RSSD;
	ccw->count = sizeof(*(lcu->uac));
	ccw->cda = (__u32)(addr_t) lcu->uac;

	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;

	/* need to unset flag here to detect race with summary unit check */
	spin_lock_irqsave(&lcu->lock, flags);
	lcu->flags &= ~NEED_UAC_UPDATE;
	spin_unlock_irqrestore(&lcu->lock, flags);

	do {
		rc = dasd_sleep_on(cqr);
		if (rc && suborder_not_supported(cqr))
			return -EOPNOTSUPP;
	} while (rc && (cqr->retries > 0));
	if (rc) {
		spin_lock_irqsave(&lcu->lock, flags);
		lcu->flags |= NEED_UAC_UPDATE;
		spin_unlock_irqrestore(&lcu->lock, flags);
	}
	dasd_kfree_request(cqr, cqr->memdev);
	return rc;
}

static int _lcu_update(struct dasd_device *refdev, struct alias_lcu *lcu)
{
	unsigned long flags;
	struct alias_pav_group *pavgroup, *tempgroup;
	struct dasd_device *device, *tempdev;
	int i, rc;
	struct dasd_eckd_private *private;

	spin_lock_irqsave(&lcu->lock, flags);
	list_for_each_entry_safe(pavgroup, tempgroup, &lcu->grouplist, group) {
		list_for_each_entry_safe(device, tempdev, &pavgroup->baselist,
					 alias_list) {
			list_move(&device->alias_list, &lcu->active_devices);
			private = (struct dasd_eckd_private *) device->private;
			private->pavgroup = NULL;
		}
		list_for_each_entry_safe(device, tempdev, &pavgroup->aliaslist,
					 alias_list) {
			list_move(&device->alias_list, &lcu->active_devices);
			private = (struct dasd_eckd_private *) device->private;
			private->pavgroup = NULL;
		}
		list_del(&pavgroup->group);
		kfree(pavgroup);
	}
	spin_unlock_irqrestore(&lcu->lock, flags);

	rc = read_unit_address_configuration(refdev, lcu);
	if (rc)
		return rc;

	/* need to take cdev lock before lcu lock */
	spin_lock_irqsave_nested(get_ccwdev_lock(refdev->cdev), flags,
				 CDEV_NESTED_FIRST);
	spin_lock(&lcu->lock);
	lcu->pav = NO_PAV;
	for (i = 0; i < MAX_DEVICES_PER_LCU; ++i) {
		switch (lcu->uac->unit[i].ua_type) {
		case UA_BASE_PAV_ALIAS:
			lcu->pav = BASE_PAV;
			break;
		case UA_HYPER_PAV_ALIAS:
			lcu->pav = HYPER_PAV;
			break;
		}
		if (lcu->pav != NO_PAV)
			break;
	}

	list_for_each_entry_safe(device, tempdev, &lcu->active_devices,
				 alias_list) {
		_add_device_to_lcu(lcu, device, refdev);
	}
	spin_unlock(&lcu->lock);
	spin_unlock_irqrestore(get_ccwdev_lock(refdev->cdev), flags);
	return 0;
}

static void lcu_update_work(struct work_struct *work)
{
	struct alias_lcu *lcu;
	struct read_uac_work_data *ruac_data;
	struct dasd_device *device;
	unsigned long flags;
	int rc;

	ruac_data = container_of(work, struct read_uac_work_data, dwork.work);
	lcu = container_of(ruac_data, struct alias_lcu, ruac_data);
	device = ruac_data->device;
	rc = _lcu_update(device, lcu);
	/*
	 * Need to check flags again, as there could have been another
	 * prepare_update or a new device a new device while we were still
	 * processing the data
	 */
	spin_lock_irqsave(&lcu->lock, flags);
	if ((rc && (rc != -EOPNOTSUPP)) || (lcu->flags & NEED_UAC_UPDATE)) {
		DBF_DEV_EVENT(DBF_WARNING, device, "could not update"
			    " alias data in lcu (rc = %d), retry later", rc);
		if (!schedule_delayed_work(&lcu->ruac_data.dwork, 30*HZ))
			dasd_put_device(device);
	} else {
		dasd_put_device(device);
		lcu->ruac_data.device = NULL;
		lcu->flags &= ~UPDATE_PENDING;
	}
	spin_unlock_irqrestore(&lcu->lock, flags);
}

static int _schedule_lcu_update(struct alias_lcu *lcu,
				struct dasd_device *device)
{
	struct dasd_device *usedev = NULL;
	struct alias_pav_group *group;

	lcu->flags |= NEED_UAC_UPDATE;
	if (lcu->ruac_data.device) {
		/* already scheduled or running */
		return 0;
	}
	if (device && !list_empty(&device->alias_list))
		usedev = device;

	if (!usedev && !list_empty(&lcu->grouplist)) {
		group = list_first_entry(&lcu->grouplist,
					 struct alias_pav_group, group);
		if (!list_empty(&group->baselist))
			usedev = list_first_entry(&group->baselist,
						  struct dasd_device,
						  alias_list);
		else if (!list_empty(&group->aliaslist))
			usedev = list_first_entry(&group->aliaslist,
						  struct dasd_device,
						  alias_list);
	}
	if (!usedev && !list_empty(&lcu->active_devices)) {
		usedev = list_first_entry(&lcu->active_devices,
					  struct dasd_device, alias_list);
	}
	/*
	 * if we haven't found a proper device yet, give up for now, the next
	 * device that will be set active will trigger an lcu update
	 */
	if (!usedev)
		return -EINVAL;
	dasd_get_device(usedev);
	lcu->ruac_data.device = usedev;
	if (!schedule_delayed_work(&lcu->ruac_data.dwork, 0))
		dasd_put_device(usedev);
	return 0;
}

int dasd_alias_add_device(struct dasd_device *device)
{
	struct dasd_eckd_private *private;
	struct alias_lcu *lcu;
	unsigned long flags;
	int rc;

	private = (struct dasd_eckd_private *) device->private;
	lcu = private->lcu;
	rc = 0;

	/* need to take cdev lock before lcu lock */
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	spin_lock(&lcu->lock);
	if (!(lcu->flags & UPDATE_PENDING)) {
		rc = _add_device_to_lcu(lcu, device, device);
		if (rc)
			lcu->flags |= UPDATE_PENDING;
	}
	if (lcu->flags & UPDATE_PENDING) {
		list_move(&device->alias_list, &lcu->active_devices);
		_schedule_lcu_update(lcu, device);
	}
	spin_unlock(&lcu->lock);
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	return rc;
}

int dasd_alias_update_add_device(struct dasd_device *device)
{
	struct dasd_eckd_private *private;
	private = (struct dasd_eckd_private *) device->private;
	private->lcu->flags |= UPDATE_PENDING;
	return dasd_alias_add_device(device);
}

int dasd_alias_remove_device(struct dasd_device *device)
{
	struct dasd_eckd_private *private;
	struct alias_lcu *lcu;
	unsigned long flags;

	private = (struct dasd_eckd_private *) device->private;
	lcu = private->lcu;
	/* nothing to do if already removed */
	if (!lcu)
		return 0;
	spin_lock_irqsave(&lcu->lock, flags);
	_remove_device_from_lcu(lcu, device);
	spin_unlock_irqrestore(&lcu->lock, flags);
	return 0;
}

struct dasd_device *dasd_alias_get_start_dev(struct dasd_device *base_device)
{

	struct dasd_device *alias_device;
	struct alias_pav_group *group;
	struct alias_lcu *lcu;
	struct dasd_eckd_private *private, *alias_priv;
	unsigned long flags;

	private = (struct dasd_eckd_private *) base_device->private;
	group = private->pavgroup;
	lcu = private->lcu;
	if (!group || !lcu)
		return NULL;
	if (lcu->pav == NO_PAV ||
	    lcu->flags & (NEED_UAC_UPDATE | UPDATE_PENDING))
		return NULL;
	if (unlikely(!(private->features.feature[8] & 0x01))) {
		/*
		 * PAV enabled but prefix not, very unlikely
		 * seems to be a lost pathgroup
		 * use base device to do IO
		 */
		DBF_DEV_EVENT(DBF_ERR, base_device, "%s",
			      "Prefix not enabled with PAV enabled\n");
		return NULL;
	}

	spin_lock_irqsave(&lcu->lock, flags);
	alias_device = group->next;
	if (!alias_device) {
		if (list_empty(&group->aliaslist)) {
			spin_unlock_irqrestore(&lcu->lock, flags);
			return NULL;
		} else {
			alias_device = list_first_entry(&group->aliaslist,
							struct dasd_device,
							alias_list);
		}
	}
	if (list_is_last(&alias_device->alias_list, &group->aliaslist))
		group->next = list_first_entry(&group->aliaslist,
					       struct dasd_device, alias_list);
	else
		group->next = list_first_entry(&alias_device->alias_list,
					       struct dasd_device, alias_list);
	spin_unlock_irqrestore(&lcu->lock, flags);
	alias_priv = (struct dasd_eckd_private *) alias_device->private;
	if ((alias_priv->count < private->count) && !alias_device->stopped)
		return alias_device;
	else
		return NULL;
}

/*
 * Summary unit check handling depends on the way alias devices
 * are handled so it is done here rather then in dasd_eckd.c
 */
static int reset_summary_unit_check(struct alias_lcu *lcu,
				    struct dasd_device *device,
				    char reason)
{
	struct dasd_ccw_req *cqr;
	int rc = 0;
	struct ccw1 *ccw;

	cqr = lcu->rsu_cqr;
	strncpy((char *) &cqr->magic, "ECKD", 4);
	ASCEBC((char *) &cqr->magic, 4);
	ccw = cqr->cpaddr;
	ccw->cmd_code = DASD_ECKD_CCW_RSCK;
	ccw->flags = CCW_FLAG_SLI;
	ccw->count = 16;
	ccw->cda = (__u32)(addr_t) cqr->data;
	((char *)cqr->data)[0] = reason;

	clear_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);
	cqr->retries = 255;	/* set retry counter to enable basic ERP */
	cqr->startdev = device;
	cqr->memdev = device;
	cqr->block = NULL;
	cqr->expires = 5 * HZ;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;

	rc = dasd_sleep_on_immediatly(cqr);
	return rc;
}

static void _restart_all_base_devices_on_lcu(struct alias_lcu *lcu)
{
	struct alias_pav_group *pavgroup;
	struct dasd_device *device;
	struct dasd_eckd_private *private;
	unsigned long flags;

	/* active and inactive list can contain alias as well as base devices */
	list_for_each_entry(device, &lcu->active_devices, alias_list) {
		private = (struct dasd_eckd_private *) device->private;
		spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
		if (private->uid.type != UA_BASE_DEVICE) {
			spin_unlock_irqrestore(get_ccwdev_lock(device->cdev),
					       flags);
			continue;
		}
		spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
		dasd_schedule_block_bh(device->block);
		dasd_schedule_device_bh(device);
	}
	list_for_each_entry(device, &lcu->inactive_devices, alias_list) {
		private = (struct dasd_eckd_private *) device->private;
		spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
		if (private->uid.type != UA_BASE_DEVICE) {
			spin_unlock_irqrestore(get_ccwdev_lock(device->cdev),
					       flags);
			continue;
		}
		spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
		dasd_schedule_block_bh(device->block);
		dasd_schedule_device_bh(device);
	}
	list_for_each_entry(pavgroup, &lcu->grouplist, group) {
		list_for_each_entry(device, &pavgroup->baselist, alias_list) {
			dasd_schedule_block_bh(device->block);
			dasd_schedule_device_bh(device);
		}
	}
}

static void flush_all_alias_devices_on_lcu(struct alias_lcu *lcu)
{
	struct alias_pav_group *pavgroup;
	struct dasd_device *device, *temp;
	struct dasd_eckd_private *private;
	int rc;
	unsigned long flags;
	LIST_HEAD(active);

	/*
	 * Problem here ist that dasd_flush_device_queue may wait
	 * for termination of a request to complete. We can't keep
	 * the lcu lock during that time, so we must assume that
	 * the lists may have changed.
	 * Idea: first gather all active alias devices in a separate list,
	 * then flush the first element of this list unlocked, and afterwards
	 * check if it is still on the list before moving it to the
	 * active_devices list.
	 */

	spin_lock_irqsave(&lcu->lock, flags);
	list_for_each_entry_safe(device, temp, &lcu->active_devices,
				 alias_list) {
		private = (struct dasd_eckd_private *) device->private;
		if (private->uid.type == UA_BASE_DEVICE)
			continue;
		list_move(&device->alias_list, &active);
	}

	list_for_each_entry(pavgroup, &lcu->grouplist, group) {
		list_splice_init(&pavgroup->aliaslist, &active);
	}
	while (!list_empty(&active)) {
		device = list_first_entry(&active, struct dasd_device,
					  alias_list);
		spin_unlock_irqrestore(&lcu->lock, flags);
		rc = dasd_flush_device_queue(device);
		spin_lock_irqsave(&lcu->lock, flags);
		/*
		 * only move device around if it wasn't moved away while we
		 * were waiting for the flush
		 */
		if (device == list_first_entry(&active,
					       struct dasd_device, alias_list))
			list_move(&device->alias_list, &lcu->active_devices);
	}
	spin_unlock_irqrestore(&lcu->lock, flags);
}

static void __stop_device_on_lcu(struct dasd_device *device,
				 struct dasd_device *pos)
{
	/* If pos == device then device is already locked! */
	if (pos == device) {
		dasd_device_set_stop_bits(pos, DASD_STOPPED_SU);
		return;
	}
	spin_lock(get_ccwdev_lock(pos->cdev));
	dasd_device_set_stop_bits(pos, DASD_STOPPED_SU);
	spin_unlock(get_ccwdev_lock(pos->cdev));
}

/*
 * This function is called in interrupt context, so the
 * cdev lock for device is already locked!
 */
static void _stop_all_devices_on_lcu(struct alias_lcu *lcu,
				     struct dasd_device *device)
{
	struct alias_pav_group *pavgroup;
	struct dasd_device *pos;

	list_for_each_entry(pos, &lcu->active_devices, alias_list)
		__stop_device_on_lcu(device, pos);
	list_for_each_entry(pos, &lcu->inactive_devices, alias_list)
		__stop_device_on_lcu(device, pos);
	list_for_each_entry(pavgroup, &lcu->grouplist, group) {
		list_for_each_entry(pos, &pavgroup->baselist, alias_list)
			__stop_device_on_lcu(device, pos);
		list_for_each_entry(pos, &pavgroup->aliaslist, alias_list)
			__stop_device_on_lcu(device, pos);
	}
}

static void _unstop_all_devices_on_lcu(struct alias_lcu *lcu)
{
	struct alias_pav_group *pavgroup;
	struct dasd_device *device;
	unsigned long flags;

	list_for_each_entry(device, &lcu->active_devices, alias_list) {
		spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
		dasd_device_remove_stop_bits(device, DASD_STOPPED_SU);
		spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	}

	list_for_each_entry(device, &lcu->inactive_devices, alias_list) {
		spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
		dasd_device_remove_stop_bits(device, DASD_STOPPED_SU);
		spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	}

	list_for_each_entry(pavgroup, &lcu->grouplist, group) {
		list_for_each_entry(device, &pavgroup->baselist, alias_list) {
			spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
			dasd_device_remove_stop_bits(device, DASD_STOPPED_SU);
			spin_unlock_irqrestore(get_ccwdev_lock(device->cdev),
					       flags);
		}
		list_for_each_entry(device, &pavgroup->aliaslist, alias_list) {
			spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
			dasd_device_remove_stop_bits(device, DASD_STOPPED_SU);
			spin_unlock_irqrestore(get_ccwdev_lock(device->cdev),
					       flags);
		}
	}
}

static void summary_unit_check_handling_work(struct work_struct *work)
{
	struct alias_lcu *lcu;
	struct summary_unit_check_work_data *suc_data;
	unsigned long flags;
	struct dasd_device *device;

	suc_data = container_of(work, struct summary_unit_check_work_data,
				worker);
	lcu = container_of(suc_data, struct alias_lcu, suc_data);
	device = suc_data->device;

	/* 1. flush alias devices */
	flush_all_alias_devices_on_lcu(lcu);

	/* 2. reset summary unit check */
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	dasd_device_remove_stop_bits(device,
				     (DASD_STOPPED_SU | DASD_STOPPED_PENDING));
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	reset_summary_unit_check(lcu, device, suc_data->reason);

	spin_lock_irqsave(&lcu->lock, flags);
	_unstop_all_devices_on_lcu(lcu);
	_restart_all_base_devices_on_lcu(lcu);
	/* 3. read new alias configuration */
	_schedule_lcu_update(lcu, device);
	lcu->suc_data.device = NULL;
	dasd_put_device(device);
	spin_unlock_irqrestore(&lcu->lock, flags);
}

/*
 * note: this will be called from int handler context (cdev locked)
 */
void dasd_alias_handle_summary_unit_check(struct dasd_device *device,
					  struct irb *irb)
{
	struct alias_lcu *lcu;
	char reason;
	struct dasd_eckd_private *private;
	char *sense;

	private = (struct dasd_eckd_private *) device->private;

	sense = dasd_get_sense(irb);
	if (sense) {
		reason = sense[8];
		DBF_DEV_EVENT(DBF_NOTICE, device, "%s %x",
			    "eckd handle summary unit check: reason", reason);
	} else {
		DBF_DEV_EVENT(DBF_WARNING, device, "%s",
			    "eckd handle summary unit check:"
			    " no reason code available");
		return;
	}

	lcu = private->lcu;
	if (!lcu) {
		DBF_DEV_EVENT(DBF_WARNING, device, "%s",
			    "device not ready to handle summary"
			    " unit check (no lcu structure)");
		return;
	}
	spin_lock(&lcu->lock);
	_stop_all_devices_on_lcu(lcu, device);
	/* prepare for lcu_update */
	private->lcu->flags |= NEED_UAC_UPDATE | UPDATE_PENDING;
	/* If this device is about to be removed just return and wait for
	 * the next interrupt on a different device
	 */
	if (list_empty(&device->alias_list)) {
		DBF_DEV_EVENT(DBF_WARNING, device, "%s",
			    "device is in offline processing,"
			    " don't do summary unit check handling");
		spin_unlock(&lcu->lock);
		return;
	}
	if (lcu->suc_data.device) {
		/* already scheduled or running */
		DBF_DEV_EVENT(DBF_WARNING, device, "%s",
			    "previous instance of summary unit check worker"
			    " still pending");
		spin_unlock(&lcu->lock);
		return ;
	}
	lcu->suc_data.reason = reason;
	lcu->suc_data.device = device;
	dasd_get_device(device);
	spin_unlock(&lcu->lock);
	if (!schedule_work(&lcu->suc_data.worker))
		dasd_put_device(device);
};
