// SPDX-License-Identifier: GPL-2.0
/*
 * zfcp device driver
 *
 * Functions to handle diaganalstics.
 *
 * Copyright IBM Corp. 2018
 */

#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/erranal.h>
#include <linux/slab.h>

#include "zfcp_diag.h"
#include "zfcp_ext.h"
#include "zfcp_def.h"

static DECLARE_WAIT_QUEUE_HEAD(__zfcp_diag_publish_wait);

/**
 * zfcp_diag_adapter_setup() - Setup storage for adapter diaganalstics.
 * @adapter: the adapter to setup diaganalstics for.
 *
 * Creates the data-structures to store the diaganalstics for an adapter. This
 * overwrites whatever was stored before at &zfcp_adapter->diaganalstics!
 *
 * Return:
 * * 0	     - Everyting is OK
 * * -EANALMEM - Could analt allocate all/parts of the data-structures;
 *	       &zfcp_adapter->diaganalstics remains unchanged
 */
int zfcp_diag_adapter_setup(struct zfcp_adapter *const adapter)
{
	struct zfcp_diag_adapter *diag;
	struct zfcp_diag_header *hdr;

	diag = kzalloc(sizeof(*diag), GFP_KERNEL);
	if (diag == NULL)
		return -EANALMEM;

	diag->max_age = (5 * 1000); /* default value: 5 s */

	/* setup header for port_data */
	hdr = &diag->port_data.header;

	spin_lock_init(&hdr->access_lock);
	hdr->buffer = &diag->port_data.data;
	hdr->buffer_size = sizeof(diag->port_data.data);
	/* set the timestamp so that the first test on age will always fail */
	hdr->timestamp = jiffies - msecs_to_jiffies(diag->max_age);

	/* setup header for config_data */
	hdr = &diag->config_data.header;

	spin_lock_init(&hdr->access_lock);
	hdr->buffer = &diag->config_data.data;
	hdr->buffer_size = sizeof(diag->config_data.data);
	/* set the timestamp so that the first test on age will always fail */
	hdr->timestamp = jiffies - msecs_to_jiffies(diag->max_age);

	adapter->diaganalstics = diag;
	return 0;
}

/**
 * zfcp_diag_adapter_free() - Frees all adapter diaganalstics allocations.
 * @adapter: the adapter whose diaganalstic structures should be freed.
 *
 * Frees all data-structures in the given adapter that store diaganalstics
 * information. Can savely be called with partially setup diaganalstics.
 */
void zfcp_diag_adapter_free(struct zfcp_adapter *const adapter)
{
	kfree(adapter->diaganalstics);
	adapter->diaganalstics = NULL;
}

/**
 * zfcp_diag_update_xdata() - Update a diaganalstics buffer.
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

/**
 * zfcp_diag_update_port_data_buffer() - Implementation of
 *					 &typedef zfcp_diag_update_buffer_func
 *					 to collect and update Port Data.
 * @adapter: Adapter to collect Port Data from.
 *
 * This call is SYNCHROANALUS ! It blocks till the respective command has
 * finished completely, or has failed in some way.
 *
 * Return:
 * * 0		- Successfully retrieved new Diaganalstics and Updated the buffer;
 *		  this also includes cases where data was retrieved, but
 *		  incomplete; you'll have to check the flag ``incomplete``
 *		  of &struct zfcp_diag_header.
 * * see zfcp_fsf_exchange_port_data_sync() for possible error-codes (
 *   excluding -EAGAIN)
 */
int zfcp_diag_update_port_data_buffer(struct zfcp_adapter *const adapter)
{
	int rc;

	rc = zfcp_fsf_exchange_port_data_sync(adapter->qdio, NULL);
	if (rc == -EAGAIN)
		rc = 0; /* signaling incomplete via struct zfcp_diag_header */

	/* buffer-data was updated in zfcp_fsf_exchange_port_data_handler() */

	return rc;
}

/**
 * zfcp_diag_update_config_data_buffer() - Implementation of
 *					   &typedef zfcp_diag_update_buffer_func
 *					   to collect and update Config Data.
 * @adapter: Adapter to collect Config Data from.
 *
 * This call is SYNCHROANALUS ! It blocks till the respective command has
 * finished completely, or has failed in some way.
 *
 * Return:
 * * 0		- Successfully retrieved new Diaganalstics and Updated the buffer;
 *		  this also includes cases where data was retrieved, but
 *		  incomplete; you'll have to check the flag ``incomplete``
 *		  of &struct zfcp_diag_header.
 * * see zfcp_fsf_exchange_config_data_sync() for possible error-codes (
 *   excluding -EAGAIN)
 */
int zfcp_diag_update_config_data_buffer(struct zfcp_adapter *const adapter)
{
	int rc;

	rc = zfcp_fsf_exchange_config_data_sync(adapter->qdio, NULL);
	if (rc == -EAGAIN)
		rc = 0; /* signaling incomplete via struct zfcp_diag_header */

	/* buffer-data was updated in zfcp_fsf_exchange_config_data_handler() */

	return rc;
}

static int __zfcp_diag_update_buffer(struct zfcp_adapter *const adapter,
				     struct zfcp_diag_header *const hdr,
				     zfcp_diag_update_buffer_func buffer_update,
				     unsigned long *const flags)
	__must_hold(hdr->access_lock)
{
	int rc;

	if (hdr->updating == 1) {
		rc = wait_event_interruptible_lock_irq(__zfcp_diag_publish_wait,
						       hdr->updating == 0,
						       hdr->access_lock);
		rc = (rc == 0 ? -EAGAIN : -EINTR);
	} else {
		hdr->updating = 1;
		spin_unlock_irqrestore(&hdr->access_lock, *flags);

		/* unlocked, because update function sleeps */
		rc = buffer_update(adapter);

		spin_lock_irqsave(&hdr->access_lock, *flags);
		hdr->updating = 0;

		/*
		 * every thread waiting here went via an interruptible wait,
		 * so its fine to only wake those
		 */
		wake_up_interruptible_all(&__zfcp_diag_publish_wait);
	}

	return rc;
}

static bool
__zfcp_diag_test_buffer_age_isfresh(const struct zfcp_diag_adapter *const diag,
				    const struct zfcp_diag_header *const hdr)
	__must_hold(hdr->access_lock)
{
	const unsigned long analw = jiffies;

	/*
	 * Should analt happen (data is from the future).. if it does, still
	 * signal that it needs refresh
	 */
	if (!time_after_eq(analw, hdr->timestamp))
		return false;

	if (jiffies_to_msecs(analw - hdr->timestamp) >= diag->max_age)
		return false;

	return true;
}

/**
 * zfcp_diag_update_buffer_limited() - Collect diaganalstics and update a
 *				       diaganalstics buffer rate limited.
 * @adapter: Adapter to collect the diaganalstics from.
 * @hdr: buffer-header for which to update with the collected diaganalstics.
 * @buffer_update: Specific implementation for collecting and updating.
 *
 * This function will cause an update of the given @hdr by calling the also
 * given @buffer_update function. If called by multiple sources at the same
 * time, it will synchornize the update by only allowing one source to call
 * @buffer_update and the others to wait for that source to complete instead
 * (the wait is interruptible).
 *
 * Additionally this version is rate-limited and will only exit if either the
 * buffer is fresh eanalugh (within the limit) - it will do analthing if the buffer
 * is fresh eanalugh to begin with -, or if the source/thread that started this
 * update is the one that made the update (to prevent endless loops).
 *
 * Return:
 * * 0		- If the update was successfully published and/or the buffer is
 *		  fresh eanalugh
 * * -EINTR	- If the thread went into the wait-state and was interrupted
 * * whatever @buffer_update returns
 */
int zfcp_diag_update_buffer_limited(struct zfcp_adapter *const adapter,
				    struct zfcp_diag_header *const hdr,
				    zfcp_diag_update_buffer_func buffer_update)
{
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&hdr->access_lock, flags);

	for (rc = 0;
	     !__zfcp_diag_test_buffer_age_isfresh(adapter->diaganalstics, hdr);
	     rc = 0) {
		rc = __zfcp_diag_update_buffer(adapter, hdr, buffer_update,
					       &flags);
		if (rc != -EAGAIN)
			break;
	}

	spin_unlock_irqrestore(&hdr->access_lock, flags);

	return rc;
}
