// SPDX-License-Identifier: GPL-2.0
/*
 * zfcp device driver
 *
 * Functions to handle diagnostics.
 *
 * Copyright IBM Corp. 2018
 */

#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/kernfs.h>
#include <linux/sysfs.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "zfcp_diag.h"
#include "zfcp_ext.h"
#include "zfcp_def.h"

/* Max age of data in a diagnostics buffer before it needs a refresh (in ms). */
#define ZFCP_DIAG_MAX_AGE (5 * 1000)

/**
 * zfcp_diag_adapter_setup() - Setup storage for adapter diagnostics.
 * @adapter: the adapter to setup diagnostics for.
 *
 * Creates the data-structures to store the diagnostics for an adapter. This
 * overwrites whatever was stored before at &zfcp_adapter->diagnostics!
 *
 * Return:
 * * 0	     - Everyting is OK
 * * -ENOMEM - Could not allocate all/parts of the data-structures;
 *	       &zfcp_adapter->diagnostics remains unchanged
 */
int zfcp_diag_adapter_setup(struct zfcp_adapter *const adapter)
{
	/* set the timestamp so that the first test on age will always fail */
	const unsigned long initial_timestamp =
		jiffies - msecs_to_jiffies(ZFCP_DIAG_MAX_AGE);
	struct zfcp_diag_adapter *diag;
	struct zfcp_diag_header *hdr;

	diag = kzalloc(sizeof(*diag), GFP_KERNEL);
	if (diag == NULL)
		return -ENOMEM;

	/* setup header for port_data */
	hdr = &diag->port_data.header;

	spin_lock_init(&hdr->access_lock);
	hdr->buffer = &diag->port_data.data;
	hdr->buffer_size = sizeof(diag->port_data.data);
	hdr->timestamp = initial_timestamp;

	/* setup header for config_data */
	hdr = &diag->config_data.header;

	spin_lock_init(&hdr->access_lock);
	hdr->buffer = &diag->config_data.data;
	hdr->buffer_size = sizeof(diag->config_data.data);
	hdr->timestamp = initial_timestamp;

	adapter->diagnostics = diag;
	return 0;
}

/**
 * zfcp_diag_adapter_free() - Frees all adapter diagnostics allocations.
 * @adapter: the adapter whose diagnostic structures should be freed.
 *
 * Frees all data-structures in the given adapter that store diagnostics
 * information. Can savely be called with partially setup diagnostics.
 */
void zfcp_diag_adapter_free(struct zfcp_adapter *const adapter)
{
	kfree(adapter->diagnostics);
	adapter->diagnostics = NULL;
}

/**
 * zfcp_diag_sysfs_setup() - Setup the sysfs-group for adapter-diagnostics.
 * @adapter: target adapter to which the group should be added.
 *
 * Return: 0 on success; Something else otherwise (see sysfs_create_group()).
 */
int zfcp_diag_sysfs_setup(struct zfcp_adapter *const adapter)
{
	int rc = sysfs_create_group(&adapter->ccw_device->dev.kobj,
				    &zfcp_sysfs_diag_attr_group);
	if (rc == 0)
		adapter->diagnostics->sysfs_established = 1;

	return rc;
}

/**
 * zfcp_diag_sysfs_destroy() - Remove the sysfs-group for adapter-diagnostics.
 * @adapter: target adapter from which the group should be removed.
 */
void zfcp_diag_sysfs_destroy(struct zfcp_adapter *const adapter)
{
	if (adapter->diagnostics == NULL ||
	    !adapter->diagnostics->sysfs_established)
		return;

	/*
	 * We need this state-handling so we can prevent warnings being printed
	 * on the kernel-console in case we have to abort a halfway done
	 * zfcp_adapter_enqueue(), in which the sysfs-group was not yet
	 * established. sysfs_remove_group() does this checking as well, but
	 * still prints a warning in case we try to remove a group that has not
	 * been established before
	 */
	adapter->diagnostics->sysfs_established = 0;
	sysfs_remove_group(&adapter->ccw_device->dev.kobj,
			   &zfcp_sysfs_diag_attr_group);
}


/**
 * zfcp_diag_update_xdata() - Update a diagnostics buffer.
 * @hdr: the meta data to update.
 * @data: data to use for the update.
 * @incomplete: flag stating whether the data in @data is incomplete.
 */
void zfcp_diag_update_xdata(struct zfcp_diag_header *const hdr,
			    const void *const data, const bool incomplete)
{
	const unsigned long capture_timestamp = jiffies;
	unsigned long flags;

	spin_lock_irqsave(&hdr->access_lock, flags);

	/* make sure we never go into the past with an update */
	if (!time_after_eq(capture_timestamp, hdr->timestamp))
		goto out;

	hdr->timestamp = capture_timestamp;
	hdr->incomplete = incomplete;
	memcpy(hdr->buffer, data, hdr->buffer_size);
out:
	spin_unlock_irqrestore(&hdr->access_lock, flags);
}
