/*
 * CXL Flash Device Driver
 *
 * Written by: Manoj N. Kumar <manoj@linux.vnet.ibm.com>, IBM Corporation
 *             Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2015 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <misc/cxl.h>
#include <asm/unaligned.h>

#include <scsi/scsi_host.h>
#include <uapi/scsi/cxlflash_ioctl.h>

#include "sislite.h"
#include "common.h"
#include "vlun.h"
#include "superpipe.h"

/**
 * create_local() - allocate and initialize a local LUN information structure
 * @sdev:	SCSI device associated with LUN.
 * @wwid:	World Wide Node Name for LUN.
 *
 * Return: Allocated local llun_info structure on success, NULL on failure
 */
static struct llun_info *create_local(struct scsi_device *sdev, u8 *wwid)
{
	struct cxlflash_cfg *cfg = shost_priv(sdev->host);
	struct device *dev = &cfg->dev->dev;
	struct llun_info *lli = NULL;

	lli = kzalloc(sizeof(*lli), GFP_KERNEL);
	if (unlikely(!lli)) {
		dev_err(dev, "%s: could not allocate lli\n", __func__);
		goto out;
	}

	lli->sdev = sdev;
	lli->host_no = sdev->host->host_no;
	lli->in_table = false;

	memcpy(lli->wwid, wwid, DK_CXLFLASH_MANAGE_LUN_WWID_LEN);
out:
	return lli;
}

/**
 * create_global() - allocate and initialize a global LUN information structure
 * @sdev:	SCSI device associated with LUN.
 * @wwid:	World Wide Node Name for LUN.
 *
 * Return: Allocated global glun_info structure on success, NULL on failure
 */
static struct glun_info *create_global(struct scsi_device *sdev, u8 *wwid)
{
	struct cxlflash_cfg *cfg = shost_priv(sdev->host);
	struct device *dev = &cfg->dev->dev;
	struct glun_info *gli = NULL;

	gli = kzalloc(sizeof(*gli), GFP_KERNEL);
	if (unlikely(!gli)) {
		dev_err(dev, "%s: could not allocate gli\n", __func__);
		goto out;
	}

	mutex_init(&gli->mutex);
	memcpy(gli->wwid, wwid, DK_CXLFLASH_MANAGE_LUN_WWID_LEN);
out:
	return gli;
}

/**
 * lookup_local() - find a local LUN information structure by WWID
 * @cfg:	Internal structure associated with the host.
 * @wwid:	WWID associated with LUN.
 *
 * Return: Found local lun_info structure on success, NULL on failure
 */
static struct llun_info *lookup_local(struct cxlflash_cfg *cfg, u8 *wwid)
{
	struct llun_info *lli, *temp;

	list_for_each_entry_safe(lli, temp, &cfg->lluns, list)
		if (!memcmp(lli->wwid, wwid, DK_CXLFLASH_MANAGE_LUN_WWID_LEN))
			return lli;

	return NULL;
}

/**
 * lookup_global() - find a global LUN information structure by WWID
 * @wwid:	WWID associated with LUN.
 *
 * Return: Found global lun_info structure on success, NULL on failure
 */
static struct glun_info *lookup_global(u8 *wwid)
{
	struct glun_info *gli, *temp;

	list_for_each_entry_safe(gli, temp, &global.gluns, list)
		if (!memcmp(gli->wwid, wwid, DK_CXLFLASH_MANAGE_LUN_WWID_LEN))
			return gli;

	return NULL;
}

/**
 * find_and_create_lun() - find or create a local LUN information structure
 * @sdev:	SCSI device associated with LUN.
 * @wwid:	WWID associated with LUN.
 *
 * The LUN is kept both in a local list (per adapter) and in a global list
 * (across all adapters). Certain attributes of the LUN are local to the
 * adapter (such as index, port selection mask, etc.).
 *
 * The block allocation map is shared across all adapters (i.e. associated
 * wih the global list). Since different attributes are associated with
 * the per adapter and global entries, allocate two separate structures for each
 * LUN (one local, one global).
 *
 * Keep a pointer back from the local to the global entry.
 *
 * This routine assumes the caller holds the global mutex.
 *
 * Return: Found/Allocated local lun_info structure on success, NULL on failure
 */
static struct llun_info *find_and_create_lun(struct scsi_device *sdev, u8 *wwid)
{
	struct cxlflash_cfg *cfg = shost_priv(sdev->host);
	struct device *dev = &cfg->dev->dev;
	struct llun_info *lli = NULL;
	struct glun_info *gli = NULL;

	if (unlikely(!wwid))
		goto out;

	lli = lookup_local(cfg, wwid);
	if (lli)
		goto out;

	lli = create_local(sdev, wwid);
	if (unlikely(!lli))
		goto out;

	gli = lookup_global(wwid);
	if (gli) {
		lli->parent = gli;
		list_add(&lli->list, &cfg->lluns);
		goto out;
	}

	gli = create_global(sdev, wwid);
	if (unlikely(!gli)) {
		kfree(lli);
		lli = NULL;
		goto out;
	}

	lli->parent = gli;
	list_add(&lli->list, &cfg->lluns);

	list_add(&gli->list, &global.gluns);

out:
	dev_dbg(dev, "%s: returning lli=%p, gli=%p\n", __func__, lli, gli);
	return lli;
}

/**
 * cxlflash_term_local_luns() - Delete all entries from local LUN list, free.
 * @cfg:	Internal structure associated with the host.
 */
void cxlflash_term_local_luns(struct cxlflash_cfg *cfg)
{
	struct llun_info *lli, *temp;

	mutex_lock(&global.mutex);
	list_for_each_entry_safe(lli, temp, &cfg->lluns, list) {
		list_del(&lli->list);
		kfree(lli);
	}
	mutex_unlock(&global.mutex);
}

/**
 * cxlflash_list_init() - initializes the global LUN list
 */
void cxlflash_list_init(void)
{
	INIT_LIST_HEAD(&global.gluns);
	mutex_init(&global.mutex);
	global.err_page = NULL;
}

/**
 * cxlflash_term_global_luns() - frees resources associated with global LUN list
 */
void cxlflash_term_global_luns(void)
{
	struct glun_info *gli, *temp;

	mutex_lock(&global.mutex);
	list_for_each_entry_safe(gli, temp, &global.gluns, list) {
		list_del(&gli->list);
		cxlflash_ba_terminate(&gli->blka.ba_lun);
		kfree(gli);
	}
	mutex_unlock(&global.mutex);
}

/**
 * cxlflash_manage_lun() - handles LUN management activities
 * @sdev:	SCSI device associated with LUN.
 * @manage:	Manage ioctl data structure.
 *
 * This routine is used to notify the driver about a LUN's WWID and associate
 * SCSI devices (sdev) with a global LUN instance. Additionally it serves to
 * change a LUN's operating mode: legacy or superpipe.
 *
 * Return: 0 on success, -errno on failure
 */
int cxlflash_manage_lun(struct scsi_device *sdev,
			struct dk_cxlflash_manage_lun *manage)
{
	struct cxlflash_cfg *cfg = shost_priv(sdev->host);
	struct device *dev = &cfg->dev->dev;
	struct llun_info *lli = NULL;
	int rc = 0;
	u64 flags = manage->hdr.flags;
	u32 chan = sdev->channel;

	mutex_lock(&global.mutex);
	lli = find_and_create_lun(sdev, manage->wwid);
	dev_dbg(dev, "%s: WWID=%016llx%016llx, flags=%016llx lli=%p\n",
		__func__, get_unaligned_be64(&manage->wwid[0]),
		get_unaligned_be64(&manage->wwid[8]), manage->hdr.flags, lli);
	if (unlikely(!lli)) {
		rc = -ENOMEM;
		goto out;
	}

	if (flags & DK_CXLFLASH_MANAGE_LUN_ENABLE_SUPERPIPE) {
		/*
		 * Update port selection mask based upon channel, store off LUN
		 * in unpacked, AFU-friendly format, and hang LUN reference in
		 * the sdev.
		 */
		lli->port_sel |= CHAN2PORTMASK(chan);
		lli->lun_id[chan] = lun_to_lunid(sdev->lun);
		sdev->hostdata = lli;
	} else if (flags & DK_CXLFLASH_MANAGE_LUN_DISABLE_SUPERPIPE) {
		if (lli->parent->mode != MODE_NONE)
			rc = -EBUSY;
		else {
			/*
			 * Clean up local LUN for this port and reset table
			 * tracking when no more references exist.
			 */
			sdev->hostdata = NULL;
			lli->port_sel &= ~CHAN2PORTMASK(chan);
			if (lli->port_sel == 0U)
				lli->in_table = false;
		}
	}

	dev_dbg(dev, "%s: port_sel=%08x chan=%u lun_id=%016llx\n",
		__func__, lli->port_sel, chan, lli->lun_id[chan]);

out:
	mutex_unlock(&global.mutex);
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}
