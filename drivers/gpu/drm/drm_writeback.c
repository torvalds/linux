// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2016 ARM Limited. All rights reserved.
 * Author: Brian Starkey <brian.starkey@arm.com>
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 */

#include <drm/drm_crtc.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_property.h>
#include <drm/drm_writeback.h>
#include <drm/drmP.h>

/**
 * DOC: overview
 *
 * Writeback connectors are used to expose hardware which can write the output
 * from a CRTC to a memory buffer. They are used and act similarly to other
 * types of connectors, with some important differences:
 *  - Writeback connectors don't provide a way to output visually to the user.
 *  - Writeback connectors should always report as "disconnected" (so that
 *    clients which don't understand them will ignore them).
 *  - Writeback connectors don't have EDID.
 *
 * A framebuffer may only be attached to a writeback connector when the
 * connector is attached to a CRTC. The WRITEBACK_FB_ID property which sets the
 * framebuffer applies only to a single commit (see below). A framebuffer may
 * not be attached while the CRTC is off.
 *
 * Writeback connectors have some additional properties, which userspace
 * can use to query and control them:
 *
 *  "WRITEBACK_FB_ID":
 *	Write-only object property storing a DRM_MODE_OBJECT_FB: it stores the
 *	framebuffer to be written by the writeback connector. This property is
 *	similar to the FB_ID property on planes, but will always read as zero
 *	and is not preserved across commits.
 *	Userspace must set this property to an output buffer every time it
 *	wishes the buffer to get filled.
 *
 *  "WRITEBACK_PIXEL_FORMATS":
 *	Immutable blob property to store the supported pixel formats table. The
 *	data is an array of u32 DRM_FORMAT_* fourcc values.
 *	Userspace can use this blob to find out what pixel formats are supported
 *	by the connector's writeback engine.
 */

static int create_writeback_properties(struct drm_device *dev)
{
	struct drm_property *prop;

	if (!dev->mode_config.writeback_fb_id_property) {
		prop = drm_property_create_object(dev, DRM_MODE_PROP_ATOMIC,
						  "WRITEBACK_FB_ID",
						  DRM_MODE_OBJECT_FB);
		if (!prop)
			return -ENOMEM;
		dev->mode_config.writeback_fb_id_property = prop;
	}

	if (!dev->mode_config.writeback_pixel_formats_property) {
		prop = drm_property_create(dev, DRM_MODE_PROP_BLOB |
					   DRM_MODE_PROP_ATOMIC |
					   DRM_MODE_PROP_IMMUTABLE,
					   "WRITEBACK_PIXEL_FORMATS", 0);
		if (!prop)
			return -ENOMEM;
		dev->mode_config.writeback_pixel_formats_property = prop;
	}

	return 0;
}

static const struct drm_encoder_funcs drm_writeback_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

/**
 * drm_writeback_connector_init - Initialize a writeback connector and its properties
 * @dev: DRM device
 * @wb_connector: Writeback connector to initialize
 * @con_funcs: Connector funcs vtable
 * @enc_helper_funcs: Encoder helper funcs vtable to be used by the internal encoder
 * @formats: Array of supported pixel formats for the writeback engine
 * @n_formats: Length of the formats array
 *
 * This function creates the writeback-connector-specific properties if they
 * have not been already created, initializes the connector as
 * type DRM_MODE_CONNECTOR_WRITEBACK, and correctly initializes the property
 * values. It will also create an internal encoder associated with the
 * drm_writeback_connector and set it to use the @enc_helper_funcs vtable for
 * the encoder helper.
 *
 * Drivers should always use this function instead of drm_connector_init() to
 * set up writeback connectors.
 *
 * Returns: 0 on success, or a negative error code
 */
int drm_writeback_connector_init(struct drm_device *dev,
				 struct drm_writeback_connector *wb_connector,
				 const struct drm_connector_funcs *con_funcs,
				 const struct drm_encoder_helper_funcs *enc_helper_funcs,
				 const u32 *formats, int n_formats)
{
	struct drm_property_blob *blob;
	struct drm_connector *connector = &wb_connector->base;
	struct drm_mode_config *config = &dev->mode_config;
	int ret = create_writeback_properties(dev);

	if (ret != 0)
		return ret;

	blob = drm_property_create_blob(dev, n_formats * sizeof(*formats),
					formats);
	if (IS_ERR(blob))
		return PTR_ERR(blob);

	drm_encoder_helper_add(&wb_connector->encoder, enc_helper_funcs);
	ret = drm_encoder_init(dev, &wb_connector->encoder,
			       &drm_writeback_encoder_funcs,
			       DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret)
		goto fail;

	connector->interlace_allowed = 0;

	ret = drm_connector_init(dev, connector, con_funcs,
				 DRM_MODE_CONNECTOR_WRITEBACK);
	if (ret)
		goto connector_fail;

	ret = drm_mode_connector_attach_encoder(connector,
						&wb_connector->encoder);
	if (ret)
		goto attach_fail;

	INIT_LIST_HEAD(&wb_connector->job_queue);
	spin_lock_init(&wb_connector->job_lock);

	drm_object_attach_property(&connector->base,
				   config->writeback_fb_id_property, 0);

	drm_object_attach_property(&connector->base,
				   config->writeback_pixel_formats_property,
				   blob->base.id);
	wb_connector->pixel_formats_blob_ptr = blob;

	return 0;

attach_fail:
	drm_connector_cleanup(connector);
connector_fail:
	drm_encoder_cleanup(&wb_connector->encoder);
fail:
	drm_property_blob_put(blob);
	return ret;
}
EXPORT_SYMBOL(drm_writeback_connector_init);

/**
 * drm_writeback_queue_job - Queue a writeback job for later signalling
 * @wb_connector: The writeback connector to queue a job on
 * @job: The job to queue
 *
 * This function adds a job to the job_queue for a writeback connector. It
 * should be considered to take ownership of the writeback job, and so any other
 * references to the job must be cleared after calling this function.
 *
 * Drivers must ensure that for a given writeback connector, jobs are queued in
 * exactly the same order as they will be completed by the hardware (and
 * signaled via drm_writeback_signal_completion).
 *
 * For every call to drm_writeback_queue_job() there must be exactly one call to
 * drm_writeback_signal_completion()
 *
 * See also: drm_writeback_signal_completion()
 */
void drm_writeback_queue_job(struct drm_writeback_connector *wb_connector,
			     struct drm_writeback_job *job)
{
	unsigned long flags;

	spin_lock_irqsave(&wb_connector->job_lock, flags);
	list_add_tail(&job->list_entry, &wb_connector->job_queue);
	spin_unlock_irqrestore(&wb_connector->job_lock, flags);
}
EXPORT_SYMBOL(drm_writeback_queue_job);

/*
 * @cleanup_work: deferred cleanup of a writeback job
 *
 * The job cannot be cleaned up directly in drm_writeback_signal_completion,
 * because it may be called in interrupt context. Dropping the framebuffer
 * reference can sleep, and so the cleanup is deferred to a workqueue.
 */
static void cleanup_work(struct work_struct *work)
{
	struct drm_writeback_job *job = container_of(work,
						     struct drm_writeback_job,
						     cleanup_work);
	drm_framebuffer_put(job->fb);
	kfree(job);
}


/**
 * drm_writeback_signal_completion - Signal the completion of a writeback job
 * @wb_connector: The writeback connector whose job is complete
 *
 * Drivers should call this to signal the completion of a previously queued
 * writeback job. It should be called as soon as possible after the hardware
 * has finished writing, and may be called from interrupt context.
 * It is the driver's responsibility to ensure that for a given connector, the
 * hardware completes writeback jobs in the same order as they are queued.
 *
 * Unless the driver is holding its own reference to the framebuffer, it must
 * not be accessed after calling this function.
 *
 * See also: drm_writeback_queue_job()
 */
void
drm_writeback_signal_completion(struct drm_writeback_connector *wb_connector)
{
	unsigned long flags;
	struct drm_writeback_job *job;

	spin_lock_irqsave(&wb_connector->job_lock, flags);
	job = list_first_entry_or_null(&wb_connector->job_queue,
				       struct drm_writeback_job,
				       list_entry);
	if (job)
		list_del(&job->list_entry);
	spin_unlock_irqrestore(&wb_connector->job_lock, flags);

	if (WARN_ON(!job))
		return;

	INIT_WORK(&job->cleanup_work, cleanup_work);
	queue_work(system_long_wq, &job->cleanup_work);
}
EXPORT_SYMBOL(drm_writeback_signal_completion);
