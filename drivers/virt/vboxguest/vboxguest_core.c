/* SPDX-License-Identifier: (GPL-2.0 OR CDDL-1.0) */
/*
 * vboxguest core guest-device handling code, VBoxGuest.cpp in upstream svn.
 *
 * Copyright (C) 2007-2016 Oracle Corporation
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/vbox_err.h>
#include <linux/vbox_utils.h>
#include <linux/vmalloc.h>
#include "vboxguest_core.h"
#include "vboxguest_version.h"

/* Get the pointer to the first HGCM parameter. */
#define VBG_IOCTL_HGCM_CALL_PARMS(a) \
	((struct vmmdev_hgcm_function_parameter *)( \
		(u8 *)(a) + sizeof(struct vbg_ioctl_hgcm_call)))
/* Get the pointer to the first HGCM parameter in a 32-bit request. */
#define VBG_IOCTL_HGCM_CALL_PARMS32(a) \
	((struct vmmdev_hgcm_function_parameter32 *)( \
		(u8 *)(a) + sizeof(struct vbg_ioctl_hgcm_call)))

#define GUEST_MAPPINGS_TRIES	5

#define VBG_KERNEL_REQUEST \
	(VMMDEV_REQUESTOR_KERNEL | VMMDEV_REQUESTOR_USR_DRV | \
	 VMMDEV_REQUESTOR_CON_DONT_KNOW | VMMDEV_REQUESTOR_TRUST_NOT_GIVEN)

/**
 * Reserves memory in which the VMM can relocate any guest mappings
 * that are floating around.
 *
 * This operation is a little bit tricky since the VMM might not accept
 * just any address because of address clashes between the three contexts
 * it operates in, so we try several times.
 *
 * Failure to reserve the guest mappings is ignored.
 *
 * @gdev:		The Guest extension device.
 */
static void vbg_guest_mappings_init(struct vbg_dev *gdev)
{
	struct vmmdev_hypervisorinfo *req;
	void *guest_mappings[GUEST_MAPPINGS_TRIES];
	struct page **pages = NULL;
	u32 size, hypervisor_size;
	int i, rc;

	/* Query the required space. */
	req = vbg_req_alloc(sizeof(*req), VMMDEVREQ_GET_HYPERVISOR_INFO,
			    VBG_KERNEL_REQUEST);
	if (!req)
		return;

	req->hypervisor_start = 0;
	req->hypervisor_size = 0;
	rc = vbg_req_perform(gdev, req);
	if (rc < 0)
		goto out;

	/*
	 * The VMM will report back if there is nothing it wants to map, like
	 * for instance in VT-x and AMD-V mode.
	 */
	if (req->hypervisor_size == 0)
		goto out;

	hypervisor_size = req->hypervisor_size;
	/* Add 4M so that we can align the vmap to 4MiB as the host requires. */
	size = PAGE_ALIGN(req->hypervisor_size) + SZ_4M;

	pages = kmalloc_array(size >> PAGE_SHIFT, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		goto out;

	gdev->guest_mappings_dummy_page = alloc_page(GFP_HIGHUSER);
	if (!gdev->guest_mappings_dummy_page)
		goto out;

	for (i = 0; i < (size >> PAGE_SHIFT); i++)
		pages[i] = gdev->guest_mappings_dummy_page;

	/*
	 * Try several times, the VMM might not accept some addresses because
	 * of address clashes between the three contexts.
	 */
	for (i = 0; i < GUEST_MAPPINGS_TRIES; i++) {
		guest_mappings[i] = vmap(pages, (size >> PAGE_SHIFT),
					 VM_MAP, PAGE_KERNEL_RO);
		if (!guest_mappings[i])
			break;

		req->header.request_type = VMMDEVREQ_SET_HYPERVISOR_INFO;
		req->header.rc = VERR_INTERNAL_ERROR;
		req->hypervisor_size = hypervisor_size;
		req->hypervisor_start =
			(unsigned long)PTR_ALIGN(guest_mappings[i], SZ_4M);

		rc = vbg_req_perform(gdev, req);
		if (rc >= 0) {
			gdev->guest_mappings = guest_mappings[i];
			break;
		}
	}

	/* Free vmap's from failed attempts. */
	while (--i >= 0)
		vunmap(guest_mappings[i]);

	/* On failure free the dummy-page backing the vmap */
	if (!gdev->guest_mappings) {
		__free_page(gdev->guest_mappings_dummy_page);
		gdev->guest_mappings_dummy_page = NULL;
	}

out:
	vbg_req_free(req, sizeof(*req));
	kfree(pages);
}

/**
 * Undo what vbg_guest_mappings_init did.
 *
 * @gdev:		The Guest extension device.
 */
static void vbg_guest_mappings_exit(struct vbg_dev *gdev)
{
	struct vmmdev_hypervisorinfo *req;
	int rc;

	if (!gdev->guest_mappings)
		return;

	/*
	 * Tell the host that we're going to free the memory we reserved for
	 * it, the free it up. (Leak the memory if anything goes wrong here.)
	 */
	req = vbg_req_alloc(sizeof(*req), VMMDEVREQ_SET_HYPERVISOR_INFO,
			    VBG_KERNEL_REQUEST);
	if (!req)
		return;

	req->hypervisor_start = 0;
	req->hypervisor_size = 0;

	rc = vbg_req_perform(gdev, req);

	vbg_req_free(req, sizeof(*req));

	if (rc < 0) {
		vbg_err("%s error: %d\n", __func__, rc);
		return;
	}

	vunmap(gdev->guest_mappings);
	gdev->guest_mappings = NULL;

	__free_page(gdev->guest_mappings_dummy_page);
	gdev->guest_mappings_dummy_page = NULL;
}

/**
 * Report the guest information to the host.
 * Return: 0 or negative errno value.
 * @gdev:		The Guest extension device.
 */
static int vbg_report_guest_info(struct vbg_dev *gdev)
{
	/*
	 * Allocate and fill in the two guest info reports.
	 */
	struct vmmdev_guest_info *req1 = NULL;
	struct vmmdev_guest_info2 *req2 = NULL;
	int rc, ret = -ENOMEM;

	req1 = vbg_req_alloc(sizeof(*req1), VMMDEVREQ_REPORT_GUEST_INFO,
			     VBG_KERNEL_REQUEST);
	req2 = vbg_req_alloc(sizeof(*req2), VMMDEVREQ_REPORT_GUEST_INFO2,
			     VBG_KERNEL_REQUEST);
	if (!req1 || !req2)
		goto out_free;

	req1->interface_version = VMMDEV_VERSION;
	req1->os_type = VMMDEV_OSTYPE_LINUX26;
#if __BITS_PER_LONG == 64
	req1->os_type |= VMMDEV_OSTYPE_X64;
#endif

	req2->additions_major = VBG_VERSION_MAJOR;
	req2->additions_minor = VBG_VERSION_MINOR;
	req2->additions_build = VBG_VERSION_BUILD;
	req2->additions_revision = VBG_SVN_REV;
	req2->additions_features =
		VMMDEV_GUEST_INFO2_ADDITIONS_FEATURES_REQUESTOR_INFO;
	strlcpy(req2->name, VBG_VERSION_STRING,
		sizeof(req2->name));

	/*
	 * There are two protocols here:
	 *      1. INFO2 + INFO1. Supported by >=3.2.51.
	 *      2. INFO1 and optionally INFO2. The old protocol.
	 *
	 * We try protocol 2 first.  It will fail with VERR_NOT_SUPPORTED
	 * if not supported by the VMMDev (message ordering requirement).
	 */
	rc = vbg_req_perform(gdev, req2);
	if (rc >= 0) {
		rc = vbg_req_perform(gdev, req1);
	} else if (rc == VERR_NOT_SUPPORTED || rc == VERR_NOT_IMPLEMENTED) {
		rc = vbg_req_perform(gdev, req1);
		if (rc >= 0) {
			rc = vbg_req_perform(gdev, req2);
			if (rc == VERR_NOT_IMPLEMENTED)
				rc = VINF_SUCCESS;
		}
	}
	ret = vbg_status_code_to_errno(rc);

out_free:
	vbg_req_free(req2, sizeof(*req2));
	vbg_req_free(req1, sizeof(*req1));
	return ret;
}

/**
 * Report the guest driver status to the host.
 * Return: 0 or negative errno value.
 * @gdev:		The Guest extension device.
 * @active:		Flag whether the driver is now active or not.
 */
static int vbg_report_driver_status(struct vbg_dev *gdev, bool active)
{
	struct vmmdev_guest_status *req;
	int rc;

	req = vbg_req_alloc(sizeof(*req), VMMDEVREQ_REPORT_GUEST_STATUS,
			    VBG_KERNEL_REQUEST);
	if (!req)
		return -ENOMEM;

	req->facility = VBOXGUEST_FACILITY_TYPE_VBOXGUEST_DRIVER;
	if (active)
		req->status = VBOXGUEST_FACILITY_STATUS_ACTIVE;
	else
		req->status = VBOXGUEST_FACILITY_STATUS_INACTIVE;
	req->flags = 0;

	rc = vbg_req_perform(gdev, req);
	if (rc == VERR_NOT_IMPLEMENTED)	/* Compatibility with older hosts. */
		rc = VINF_SUCCESS;

	vbg_req_free(req, sizeof(*req));

	return vbg_status_code_to_errno(rc);
}

/**
 * Inflate the balloon by one chunk. The caller owns the balloon mutex.
 * Return: 0 or negative errno value.
 * @gdev:		The Guest extension device.
 * @chunk_idx:		Index of the chunk.
 */
static int vbg_balloon_inflate(struct vbg_dev *gdev, u32 chunk_idx)
{
	struct vmmdev_memballoon_change *req = gdev->mem_balloon.change_req;
	struct page **pages;
	int i, rc, ret;

	pages = kmalloc_array(VMMDEV_MEMORY_BALLOON_CHUNK_PAGES,
			      sizeof(*pages),
			      GFP_KERNEL | __GFP_NOWARN);
	if (!pages)
		return -ENOMEM;

	req->header.size = sizeof(*req);
	req->inflate = true;
	req->pages = VMMDEV_MEMORY_BALLOON_CHUNK_PAGES;

	for (i = 0; i < VMMDEV_MEMORY_BALLOON_CHUNK_PAGES; i++) {
		pages[i] = alloc_page(GFP_KERNEL | __GFP_NOWARN);
		if (!pages[i]) {
			ret = -ENOMEM;
			goto out_error;
		}

		req->phys_page[i] = page_to_phys(pages[i]);
	}

	rc = vbg_req_perform(gdev, req);
	if (rc < 0) {
		vbg_err("%s error, rc: %d\n", __func__, rc);
		ret = vbg_status_code_to_errno(rc);
		goto out_error;
	}

	gdev->mem_balloon.pages[chunk_idx] = pages;

	return 0;

out_error:
	while (--i >= 0)
		__free_page(pages[i]);
	kfree(pages);

	return ret;
}

/**
 * Deflate the balloon by one chunk. The caller owns the balloon mutex.
 * Return: 0 or negative errno value.
 * @gdev:		The Guest extension device.
 * @chunk_idx:		Index of the chunk.
 */
static int vbg_balloon_deflate(struct vbg_dev *gdev, u32 chunk_idx)
{
	struct vmmdev_memballoon_change *req = gdev->mem_balloon.change_req;
	struct page **pages = gdev->mem_balloon.pages[chunk_idx];
	int i, rc;

	req->header.size = sizeof(*req);
	req->inflate = false;
	req->pages = VMMDEV_MEMORY_BALLOON_CHUNK_PAGES;

	for (i = 0; i < VMMDEV_MEMORY_BALLOON_CHUNK_PAGES; i++)
		req->phys_page[i] = page_to_phys(pages[i]);

	rc = vbg_req_perform(gdev, req);
	if (rc < 0) {
		vbg_err("%s error, rc: %d\n", __func__, rc);
		return vbg_status_code_to_errno(rc);
	}

	for (i = 0; i < VMMDEV_MEMORY_BALLOON_CHUNK_PAGES; i++)
		__free_page(pages[i]);
	kfree(pages);
	gdev->mem_balloon.pages[chunk_idx] = NULL;

	return 0;
}

/**
 * Respond to VMMDEV_EVENT_BALLOON_CHANGE_REQUEST events, query the size
 * the host wants the balloon to be and adjust accordingly.
 */
static void vbg_balloon_work(struct work_struct *work)
{
	struct vbg_dev *gdev =
		container_of(work, struct vbg_dev, mem_balloon.work);
	struct vmmdev_memballoon_info *req = gdev->mem_balloon.get_req;
	u32 i, chunks;
	int rc, ret;

	/*
	 * Setting this bit means that we request the value from the host and
	 * change the guest memory balloon according to the returned value.
	 */
	req->event_ack = VMMDEV_EVENT_BALLOON_CHANGE_REQUEST;
	rc = vbg_req_perform(gdev, req);
	if (rc < 0) {
		vbg_err("%s error, rc: %d)\n", __func__, rc);
		return;
	}

	/*
	 * The host always returns the same maximum amount of chunks, so
	 * we do this once.
	 */
	if (!gdev->mem_balloon.max_chunks) {
		gdev->mem_balloon.pages =
			devm_kcalloc(gdev->dev, req->phys_mem_chunks,
				     sizeof(struct page **), GFP_KERNEL);
		if (!gdev->mem_balloon.pages)
			return;

		gdev->mem_balloon.max_chunks = req->phys_mem_chunks;
	}

	chunks = req->balloon_chunks;
	if (chunks > gdev->mem_balloon.max_chunks) {
		vbg_err("%s: illegal balloon size %u (max=%u)\n",
			__func__, chunks, gdev->mem_balloon.max_chunks);
		return;
	}

	if (chunks > gdev->mem_balloon.chunks) {
		/* inflate */
		for (i = gdev->mem_balloon.chunks; i < chunks; i++) {
			ret = vbg_balloon_inflate(gdev, i);
			if (ret < 0)
				return;

			gdev->mem_balloon.chunks++;
		}
	} else {
		/* deflate */
		for (i = gdev->mem_balloon.chunks; i-- > chunks;) {
			ret = vbg_balloon_deflate(gdev, i);
			if (ret < 0)
				return;

			gdev->mem_balloon.chunks--;
		}
	}
}

/**
 * Callback for heartbeat timer.
 */
static void vbg_heartbeat_timer(struct timer_list *t)
{
	struct vbg_dev *gdev = from_timer(gdev, t, heartbeat_timer);

	vbg_req_perform(gdev, gdev->guest_heartbeat_req);
	mod_timer(&gdev->heartbeat_timer,
		  msecs_to_jiffies(gdev->heartbeat_interval_ms));
}

/**
 * Configure the host to check guest's heartbeat
 * and get heartbeat interval from the host.
 * Return: 0 or negative errno value.
 * @gdev:		The Guest extension device.
 * @enabled:		Set true to enable guest heartbeat checks on host.
 */
static int vbg_heartbeat_host_config(struct vbg_dev *gdev, bool enabled)
{
	struct vmmdev_heartbeat *req;
	int rc;

	req = vbg_req_alloc(sizeof(*req), VMMDEVREQ_HEARTBEAT_CONFIGURE,
			    VBG_KERNEL_REQUEST);
	if (!req)
		return -ENOMEM;

	req->enabled = enabled;
	req->interval_ns = 0;
	rc = vbg_req_perform(gdev, req);
	do_div(req->interval_ns, 1000000); /* ns -> ms */
	gdev->heartbeat_interval_ms = req->interval_ns;
	vbg_req_free(req, sizeof(*req));

	return vbg_status_code_to_errno(rc);
}

/**
 * Initializes the heartbeat timer. This feature may be disabled by the host.
 * Return: 0 or negative errno value.
 * @gdev:		The Guest extension device.
 */
static int vbg_heartbeat_init(struct vbg_dev *gdev)
{
	int ret;

	/* Make sure that heartbeat checking is disabled if we fail. */
	ret = vbg_heartbeat_host_config(gdev, false);
	if (ret < 0)
		return ret;

	ret = vbg_heartbeat_host_config(gdev, true);
	if (ret < 0)
		return ret;

	gdev->guest_heartbeat_req = vbg_req_alloc(
					sizeof(*gdev->guest_heartbeat_req),
					VMMDEVREQ_GUEST_HEARTBEAT,
					VBG_KERNEL_REQUEST);
	if (!gdev->guest_heartbeat_req)
		return -ENOMEM;

	vbg_info("%s: Setting up heartbeat to trigger every %d milliseconds\n",
		 __func__, gdev->heartbeat_interval_ms);
	mod_timer(&gdev->heartbeat_timer, 0);

	return 0;
}

/**
 * Cleanup hearbeat code, stop HB timer and disable host heartbeat checking.
 * @gdev:		The Guest extension device.
 */
static void vbg_heartbeat_exit(struct vbg_dev *gdev)
{
	del_timer_sync(&gdev->heartbeat_timer);
	vbg_heartbeat_host_config(gdev, false);
	vbg_req_free(gdev->guest_heartbeat_req,
		     sizeof(*gdev->guest_heartbeat_req));
}

/**
 * Applies a change to the bit usage tracker.
 * Return: true if the mask changed, false if not.
 * @tracker:		The bit usage tracker.
 * @changed:		The bits to change.
 * @previous:		The previous value of the bits.
 */
static bool vbg_track_bit_usage(struct vbg_bit_usage_tracker *tracker,
				u32 changed, u32 previous)
{
	bool global_change = false;

	while (changed) {
		u32 bit = ffs(changed) - 1;
		u32 bitmask = BIT(bit);

		if (bitmask & previous) {
			tracker->per_bit_usage[bit] -= 1;
			if (tracker->per_bit_usage[bit] == 0) {
				global_change = true;
				tracker->mask &= ~bitmask;
			}
		} else {
			tracker->per_bit_usage[bit] += 1;
			if (tracker->per_bit_usage[bit] == 1) {
				global_change = true;
				tracker->mask |= bitmask;
			}
		}

		changed &= ~bitmask;
	}

	return global_change;
}

/**
 * Init and termination worker for resetting the (host) event filter on the host
 * Return: 0 or negative errno value.
 * @gdev:		   The Guest extension device.
 * @fixed_events:	   Fixed events (init time).
 */
static int vbg_reset_host_event_filter(struct vbg_dev *gdev,
				       u32 fixed_events)
{
	struct vmmdev_mask *req;
	int rc;

	req = vbg_req_alloc(sizeof(*req), VMMDEVREQ_CTL_GUEST_FILTER_MASK,
			    VBG_KERNEL_REQUEST);
	if (!req)
		return -ENOMEM;

	req->not_mask = U32_MAX & ~fixed_events;
	req->or_mask = fixed_events;
	rc = vbg_req_perform(gdev, req);
	if (rc < 0)
		vbg_err("%s error, rc: %d\n", __func__, rc);

	vbg_req_free(req, sizeof(*req));
	return vbg_status_code_to_errno(rc);
}

/**
 * Changes the event filter mask for the given session.
 *
 * This is called in response to VBG_IOCTL_CHANGE_FILTER_MASK as well as to
 * do session cleanup. Takes the session spinlock.
 *
 * Return: 0 or negative errno value.
 * @gdev:			The Guest extension device.
 * @session:			The session.
 * @or_mask:			The events to add.
 * @not_mask:			The events to remove.
 * @session_termination:	Set if we're called by the session cleanup code.
 *				This tweaks the error handling so we perform
 *				proper session cleanup even if the host
 *				misbehaves.
 */
static int vbg_set_session_event_filter(struct vbg_dev *gdev,
					struct vbg_session *session,
					u32 or_mask, u32 not_mask,
					bool session_termination)
{
	struct vmmdev_mask *req;
	u32 changed, previous;
	int rc, ret = 0;

	/*
	 * Allocate a request buffer before taking the spinlock, when
	 * the session is being terminated the requestor is the kernel,
	 * as we're cleaning up.
	 */
	req = vbg_req_alloc(sizeof(*req), VMMDEVREQ_CTL_GUEST_FILTER_MASK,
			    session_termination ? VBG_KERNEL_REQUEST :
						  session->requestor);
	if (!req) {
		if (!session_termination)
			return -ENOMEM;
		/* Ignore allocation failure, we must do session cleanup. */
	}

	mutex_lock(&gdev->session_mutex);

	/* Apply the changes to the session mask. */
	previous = session->event_filter;
	session->event_filter |= or_mask;
	session->event_filter &= ~not_mask;

	/* If anything actually changed, update the global usage counters. */
	changed = previous ^ session->event_filter;
	if (!changed)
		goto out;

	vbg_track_bit_usage(&gdev->event_filter_tracker, changed, previous);
	or_mask = gdev->fixed_events | gdev->event_filter_tracker.mask;

	if (gdev->event_filter_host == or_mask || !req)
		goto out;

	gdev->event_filter_host = or_mask;
	req->or_mask = or_mask;
	req->not_mask = ~or_mask;
	rc = vbg_req_perform(gdev, req);
	if (rc < 0) {
		ret = vbg_status_code_to_errno(rc);

		/* Failed, roll back (unless it's session termination time). */
		gdev->event_filter_host = U32_MAX;
		if (session_termination)
			goto out;

		vbg_track_bit_usage(&gdev->event_filter_tracker, changed,
				    session->event_filter);
		session->event_filter = previous;
	}

out:
	mutex_unlock(&gdev->session_mutex);
	vbg_req_free(req, sizeof(*req));

	return ret;
}

/**
 * Init and termination worker for set guest capabilities to zero on the host.
 * Return: 0 or negative errno value.
 * @gdev:		The Guest extension device.
 */
static int vbg_reset_host_capabilities(struct vbg_dev *gdev)
{
	struct vmmdev_mask *req;
	int rc;

	req = vbg_req_alloc(sizeof(*req), VMMDEVREQ_SET_GUEST_CAPABILITIES,
			    VBG_KERNEL_REQUEST);
	if (!req)
		return -ENOMEM;

	req->not_mask = U32_MAX;
	req->or_mask = 0;
	rc = vbg_req_perform(gdev, req);
	if (rc < 0)
		vbg_err("%s error, rc: %d\n", __func__, rc);

	vbg_req_free(req, sizeof(*req));
	return vbg_status_code_to_errno(rc);
}

/**
 * Set guest capabilities on the host.
 * Must be called with gdev->session_mutex hold.
 * Return: 0 or negative errno value.
 * @gdev:			The Guest extension device.
 * @session:			The session.
 * @session_termination:	Set if we're called by the session cleanup code.
 */
static int vbg_set_host_capabilities(struct vbg_dev *gdev,
				     struct vbg_session *session,
				     bool session_termination)
{
	struct vmmdev_mask *req;
	u32 caps;
	int rc;

	WARN_ON(!mutex_is_locked(&gdev->session_mutex));

	caps = gdev->set_guest_caps_tracker.mask;

	if (gdev->guest_caps_host == caps)
		return 0;

	/* On termination the requestor is the kernel, as we're cleaning up. */
	req = vbg_req_alloc(sizeof(*req), VMMDEVREQ_SET_GUEST_CAPABILITIES,
			    session_termination ? VBG_KERNEL_REQUEST :
						  session->requestor);
	if (!req) {
		gdev->guest_caps_host = U32_MAX;
		return -ENOMEM;
	}

	req->or_mask = caps;
	req->not_mask = ~caps;
	rc = vbg_req_perform(gdev, req);
	vbg_req_free(req, sizeof(*req));

	gdev->guest_caps_host = (rc >= 0) ? caps : U32_MAX;

	return vbg_status_code_to_errno(rc);
}

/**
 * Sets the guest capabilities for a session. Takes the session spinlock.
 * Return: 0 or negative errno value.
 * @gdev:			The Guest extension device.
 * @session:			The session.
 * @or_mask:			The capabilities to add.
 * @not_mask:			The capabilities to remove.
 * @session_termination:	Set if we're called by the session cleanup code.
 *				This tweaks the error handling so we perform
 *				proper session cleanup even if the host
 *				misbehaves.
 */
static int vbg_set_session_capabilities(struct vbg_dev *gdev,
					struct vbg_session *session,
					u32 or_mask, u32 not_mask,
					bool session_termination)
{
	u32 changed, previous;
	int ret = 0;

	mutex_lock(&gdev->session_mutex);

	/* Apply the changes to the session mask. */
	previous = session->set_guest_caps;
	session->set_guest_caps |= or_mask;
	session->set_guest_caps &= ~not_mask;

	/* If anything actually changed, update the global usage counters. */
	changed = previous ^ session->set_guest_caps;
	if (!changed)
		goto out;

	vbg_track_bit_usage(&gdev->set_guest_caps_tracker, changed, previous);

	ret = vbg_set_host_capabilities(gdev, session, session_termination);
	/* Roll back on failure, unless it's session termination time. */
	if (ret < 0 && !session_termination) {
		vbg_track_bit_usage(&gdev->set_guest_caps_tracker, changed,
				    session->set_guest_caps);
		session->set_guest_caps = previous;
	}

out:
	mutex_unlock(&gdev->session_mutex);

	return ret;
}

/**
 * vbg_query_host_version get the host feature mask and version information.
 * Return: 0 or negative errno value.
 * @gdev:		The Guest extension device.
 */
static int vbg_query_host_version(struct vbg_dev *gdev)
{
	struct vmmdev_host_version *req;
	int rc, ret;

	req = vbg_req_alloc(sizeof(*req), VMMDEVREQ_GET_HOST_VERSION,
			    VBG_KERNEL_REQUEST);
	if (!req)
		return -ENOMEM;

	rc = vbg_req_perform(gdev, req);
	ret = vbg_status_code_to_errno(rc);
	if (ret) {
		vbg_err("%s error: %d\n", __func__, rc);
		goto out;
	}

	snprintf(gdev->host_version, sizeof(gdev->host_version), "%u.%u.%ur%u",
		 req->major, req->minor, req->build, req->revision);
	gdev->host_features = req->features;

	vbg_info("vboxguest: host-version: %s %#x\n", gdev->host_version,
		 gdev->host_features);

	if (!(req->features & VMMDEV_HVF_HGCM_PHYS_PAGE_LIST)) {
		vbg_err("vboxguest: Error host too old (does not support page-lists)\n");
		ret = -ENODEV;
	}

out:
	vbg_req_free(req, sizeof(*req));
	return ret;
}

/**
 * Initializes the VBoxGuest device extension when the
 * device driver is loaded.
 *
 * The native code locates the VMMDev on the PCI bus and retrieve
 * the MMIO and I/O port ranges, this function will take care of
 * mapping the MMIO memory (if present). Upon successful return
 * the native code should set up the interrupt handler.
 *
 * Return: 0 or negative errno value.
 *
 * @gdev:		The Guest extension device.
 * @fixed_events:	Events that will be enabled upon init and no client
 *			will ever be allowed to mask.
 */
int vbg_core_init(struct vbg_dev *gdev, u32 fixed_events)
{
	int ret = -ENOMEM;

	gdev->fixed_events = fixed_events | VMMDEV_EVENT_HGCM;
	gdev->event_filter_host = U32_MAX;	/* forces a report */
	gdev->guest_caps_host = U32_MAX;	/* forces a report */

	init_waitqueue_head(&gdev->event_wq);
	init_waitqueue_head(&gdev->hgcm_wq);
	spin_lock_init(&gdev->event_spinlock);
	mutex_init(&gdev->session_mutex);
	mutex_init(&gdev->cancel_req_mutex);
	timer_setup(&gdev->heartbeat_timer, vbg_heartbeat_timer, 0);
	INIT_WORK(&gdev->mem_balloon.work, vbg_balloon_work);

	gdev->mem_balloon.get_req =
		vbg_req_alloc(sizeof(*gdev->mem_balloon.get_req),
			      VMMDEVREQ_GET_MEMBALLOON_CHANGE_REQ,
			      VBG_KERNEL_REQUEST);
	gdev->mem_balloon.change_req =
		vbg_req_alloc(sizeof(*gdev->mem_balloon.change_req),
			      VMMDEVREQ_CHANGE_MEMBALLOON,
			      VBG_KERNEL_REQUEST);
	gdev->cancel_req =
		vbg_req_alloc(sizeof(*(gdev->cancel_req)),
			      VMMDEVREQ_HGCM_CANCEL2,
			      VBG_KERNEL_REQUEST);
	gdev->ack_events_req =
		vbg_req_alloc(sizeof(*gdev->ack_events_req),
			      VMMDEVREQ_ACKNOWLEDGE_EVENTS,
			      VBG_KERNEL_REQUEST);
	gdev->mouse_status_req =
		vbg_req_alloc(sizeof(*gdev->mouse_status_req),
			      VMMDEVREQ_GET_MOUSE_STATUS,
			      VBG_KERNEL_REQUEST);

	if (!gdev->mem_balloon.get_req || !gdev->mem_balloon.change_req ||
	    !gdev->cancel_req || !gdev->ack_events_req ||
	    !gdev->mouse_status_req)
		goto err_free_reqs;

	ret = vbg_query_host_version(gdev);
	if (ret)
		goto err_free_reqs;

	ret = vbg_report_guest_info(gdev);
	if (ret) {
		vbg_err("vboxguest: vbg_report_guest_info error: %d\n", ret);
		goto err_free_reqs;
	}

	ret = vbg_reset_host_event_filter(gdev, gdev->fixed_events);
	if (ret) {
		vbg_err("vboxguest: Error setting fixed event filter: %d\n",
			ret);
		goto err_free_reqs;
	}

	ret = vbg_reset_host_capabilities(gdev);
	if (ret) {
		vbg_err("vboxguest: Error clearing guest capabilities: %d\n",
			ret);
		goto err_free_reqs;
	}

	ret = vbg_core_set_mouse_status(gdev, 0);
	if (ret) {
		vbg_err("vboxguest: Error clearing mouse status: %d\n", ret);
		goto err_free_reqs;
	}

	/* These may fail without requiring the driver init to fail. */
	vbg_guest_mappings_init(gdev);
	vbg_heartbeat_init(gdev);

	/* All Done! */
	ret = vbg_report_driver_status(gdev, true);
	if (ret < 0)
		vbg_err("vboxguest: Error reporting driver status: %d\n", ret);

	return 0;

err_free_reqs:
	vbg_req_free(gdev->mouse_status_req,
		     sizeof(*gdev->mouse_status_req));
	vbg_req_free(gdev->ack_events_req,
		     sizeof(*gdev->ack_events_req));
	vbg_req_free(gdev->cancel_req,
		     sizeof(*gdev->cancel_req));
	vbg_req_free(gdev->mem_balloon.change_req,
		     sizeof(*gdev->mem_balloon.change_req));
	vbg_req_free(gdev->mem_balloon.get_req,
		     sizeof(*gdev->mem_balloon.get_req));
	return ret;
}

/**
 * Call this on exit to clean-up vboxguest-core managed resources.
 *
 * The native code should call this before the driver is loaded,
 * but don't call this on shutdown.
 * @gdev:		The Guest extension device.
 */
void vbg_core_exit(struct vbg_dev *gdev)
{
	vbg_heartbeat_exit(gdev);
	vbg_guest_mappings_exit(gdev);

	/* Clear the host flags (mouse status etc). */
	vbg_reset_host_event_filter(gdev, 0);
	vbg_reset_host_capabilities(gdev);
	vbg_core_set_mouse_status(gdev, 0);

	vbg_req_free(gdev->mouse_status_req,
		     sizeof(*gdev->mouse_status_req));
	vbg_req_free(gdev->ack_events_req,
		     sizeof(*gdev->ack_events_req));
	vbg_req_free(gdev->cancel_req,
		     sizeof(*gdev->cancel_req));
	vbg_req_free(gdev->mem_balloon.change_req,
		     sizeof(*gdev->mem_balloon.change_req));
	vbg_req_free(gdev->mem_balloon.get_req,
		     sizeof(*gdev->mem_balloon.get_req));
}

/**
 * Creates a VBoxGuest user session.
 *
 * vboxguest_linux.c calls this when userspace opens the char-device.
 * Return: A pointer to the new session or an ERR_PTR on error.
 * @gdev:		The Guest extension device.
 * @requestor:		VMMDEV_REQUESTOR_* flags
 */
struct vbg_session *vbg_core_open_session(struct vbg_dev *gdev, u32 requestor)
{
	struct vbg_session *session;

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session)
		return ERR_PTR(-ENOMEM);

	session->gdev = gdev;
	session->requestor = requestor;

	return session;
}

/**
 * Closes a VBoxGuest session.
 * @session:		The session to close (and free).
 */
void vbg_core_close_session(struct vbg_session *session)
{
	struct vbg_dev *gdev = session->gdev;
	int i, rc;

	vbg_set_session_capabilities(gdev, session, 0, U32_MAX, true);
	vbg_set_session_event_filter(gdev, session, 0, U32_MAX, true);

	for (i = 0; i < ARRAY_SIZE(session->hgcm_client_ids); i++) {
		if (!session->hgcm_client_ids[i])
			continue;

		/* requestor is kernel here, as we're cleaning up. */
		vbg_hgcm_disconnect(gdev, VBG_KERNEL_REQUEST,
				    session->hgcm_client_ids[i], &rc);
	}

	kfree(session);
}

static int vbg_ioctl_chk(struct vbg_ioctl_hdr *hdr, size_t in_size,
			 size_t out_size)
{
	if (hdr->size_in  != (sizeof(*hdr) + in_size) ||
	    hdr->size_out != (sizeof(*hdr) + out_size))
		return -EINVAL;

	return 0;
}

static int vbg_ioctl_driver_version_info(
	struct vbg_ioctl_driver_version_info *info)
{
	const u16 vbg_maj_version = VBG_IOC_VERSION >> 16;
	u16 min_maj_version, req_maj_version;

	if (vbg_ioctl_chk(&info->hdr, sizeof(info->u.in), sizeof(info->u.out)))
		return -EINVAL;

	req_maj_version = info->u.in.req_version >> 16;
	min_maj_version = info->u.in.min_version >> 16;

	if (info->u.in.min_version > info->u.in.req_version ||
	    min_maj_version != req_maj_version)
		return -EINVAL;

	if (info->u.in.min_version <= VBG_IOC_VERSION &&
	    min_maj_version == vbg_maj_version) {
		info->u.out.session_version = VBG_IOC_VERSION;
	} else {
		info->u.out.session_version = U32_MAX;
		info->hdr.rc = VERR_VERSION_MISMATCH;
	}

	info->u.out.driver_version  = VBG_IOC_VERSION;
	info->u.out.driver_revision = 0;
	info->u.out.reserved1      = 0;
	info->u.out.reserved2      = 0;

	return 0;
}

static bool vbg_wait_event_cond(struct vbg_dev *gdev,
				struct vbg_session *session,
				u32 event_mask)
{
	unsigned long flags;
	bool wakeup;
	u32 events;

	spin_lock_irqsave(&gdev->event_spinlock, flags);

	events = gdev->pending_events & event_mask;
	wakeup = events || session->cancel_waiters;

	spin_unlock_irqrestore(&gdev->event_spinlock, flags);

	return wakeup;
}

/* Must be called with the event_lock held */
static u32 vbg_consume_events_locked(struct vbg_dev *gdev,
				     struct vbg_session *session,
				     u32 event_mask)
{
	u32 events = gdev->pending_events & event_mask;

	gdev->pending_events &= ~events;
	return events;
}

static int vbg_ioctl_wait_for_events(struct vbg_dev *gdev,
				     struct vbg_session *session,
				     struct vbg_ioctl_wait_for_events *wait)
{
	u32 timeout_ms = wait->u.in.timeout_ms;
	u32 event_mask = wait->u.in.events;
	unsigned long flags;
	long timeout;
	int ret = 0;

	if (vbg_ioctl_chk(&wait->hdr, sizeof(wait->u.in), sizeof(wait->u.out)))
		return -EINVAL;

	if (timeout_ms == U32_MAX)
		timeout = MAX_SCHEDULE_TIMEOUT;
	else
		timeout = msecs_to_jiffies(timeout_ms);

	wait->u.out.events = 0;
	do {
		timeout = wait_event_interruptible_timeout(
				gdev->event_wq,
				vbg_wait_event_cond(gdev, session, event_mask),
				timeout);

		spin_lock_irqsave(&gdev->event_spinlock, flags);

		if (timeout < 0 || session->cancel_waiters) {
			ret = -EINTR;
		} else if (timeout == 0) {
			ret = -ETIMEDOUT;
		} else {
			wait->u.out.events =
			   vbg_consume_events_locked(gdev, session, event_mask);
		}

		spin_unlock_irqrestore(&gdev->event_spinlock, flags);

		/*
		 * Someone else may have consumed the event(s) first, in
		 * which case we go back to waiting.
		 */
	} while (ret == 0 && wait->u.out.events == 0);

	return ret;
}

static int vbg_ioctl_interrupt_all_wait_events(struct vbg_dev *gdev,
					       struct vbg_session *session,
					       struct vbg_ioctl_hdr *hdr)
{
	unsigned long flags;

	if (hdr->size_in != sizeof(*hdr) || hdr->size_out != sizeof(*hdr))
		return -EINVAL;

	spin_lock_irqsave(&gdev->event_spinlock, flags);
	session->cancel_waiters = true;
	spin_unlock_irqrestore(&gdev->event_spinlock, flags);

	wake_up(&gdev->event_wq);

	return 0;
}

/**
 * Checks if the VMM request is allowed in the context of the given session.
 * Return: 0 or negative errno value.
 * @gdev:		The Guest extension device.
 * @session:		The calling session.
 * @req:		The request.
 */
static int vbg_req_allowed(struct vbg_dev *gdev, struct vbg_session *session,
			   const struct vmmdev_request_header *req)
{
	const struct vmmdev_guest_status *guest_status;
	bool trusted_apps_only;

	switch (req->request_type) {
	/* Trusted users apps only. */
	case VMMDEVREQ_QUERY_CREDENTIALS:
	case VMMDEVREQ_REPORT_CREDENTIALS_JUDGEMENT:
	case VMMDEVREQ_REGISTER_SHARED_MODULE:
	case VMMDEVREQ_UNREGISTER_SHARED_MODULE:
	case VMMDEVREQ_WRITE_COREDUMP:
	case VMMDEVREQ_GET_CPU_HOTPLUG_REQ:
	case VMMDEVREQ_SET_CPU_HOTPLUG_STATUS:
	case VMMDEVREQ_CHECK_SHARED_MODULES:
	case VMMDEVREQ_GET_PAGE_SHARING_STATUS:
	case VMMDEVREQ_DEBUG_IS_PAGE_SHARED:
	case VMMDEVREQ_REPORT_GUEST_STATS:
	case VMMDEVREQ_REPORT_GUEST_USER_STATE:
	case VMMDEVREQ_GET_STATISTICS_CHANGE_REQ:
		trusted_apps_only = true;
		break;

	/* Anyone. */
	case VMMDEVREQ_GET_MOUSE_STATUS:
	case VMMDEVREQ_SET_MOUSE_STATUS:
	case VMMDEVREQ_SET_POINTER_SHAPE:
	case VMMDEVREQ_GET_HOST_VERSION:
	case VMMDEVREQ_IDLE:
	case VMMDEVREQ_GET_HOST_TIME:
	case VMMDEVREQ_SET_POWER_STATUS:
	case VMMDEVREQ_ACKNOWLEDGE_EVENTS:
	case VMMDEVREQ_CTL_GUEST_FILTER_MASK:
	case VMMDEVREQ_REPORT_GUEST_STATUS:
	case VMMDEVREQ_GET_DISPLAY_CHANGE_REQ:
	case VMMDEVREQ_VIDEMODE_SUPPORTED:
	case VMMDEVREQ_GET_HEIGHT_REDUCTION:
	case VMMDEVREQ_GET_DISPLAY_CHANGE_REQ2:
	case VMMDEVREQ_VIDEMODE_SUPPORTED2:
	case VMMDEVREQ_VIDEO_ACCEL_ENABLE:
	case VMMDEVREQ_VIDEO_ACCEL_FLUSH:
	case VMMDEVREQ_VIDEO_SET_VISIBLE_REGION:
	case VMMDEVREQ_GET_DISPLAY_CHANGE_REQEX:
	case VMMDEVREQ_GET_SEAMLESS_CHANGE_REQ:
	case VMMDEVREQ_GET_VRDPCHANGE_REQ:
	case VMMDEVREQ_LOG_STRING:
	case VMMDEVREQ_GET_SESSION_ID:
		trusted_apps_only = false;
		break;

	/* Depends on the request parameters... */
	case VMMDEVREQ_REPORT_GUEST_CAPABILITIES:
		guest_status = (const struct vmmdev_guest_status *)req;
		switch (guest_status->facility) {
		case VBOXGUEST_FACILITY_TYPE_ALL:
		case VBOXGUEST_FACILITY_TYPE_VBOXGUEST_DRIVER:
			vbg_err("Denying userspace vmm report guest cap. call facility %#08x\n",
				guest_status->facility);
			return -EPERM;
		case VBOXGUEST_FACILITY_TYPE_VBOX_SERVICE:
			trusted_apps_only = true;
			break;
		case VBOXGUEST_FACILITY_TYPE_VBOX_TRAY_CLIENT:
		case VBOXGUEST_FACILITY_TYPE_SEAMLESS:
		case VBOXGUEST_FACILITY_TYPE_GRAPHICS:
		default:
			trusted_apps_only = false;
			break;
		}
		break;

	/* Anything else is not allowed. */
	default:
		vbg_err("Denying userspace vmm call type %#08x\n",
			req->request_type);
		return -EPERM;
	}

	if (trusted_apps_only &&
	    (session->requestor & VMMDEV_REQUESTOR_USER_DEVICE)) {
		vbg_err("Denying userspace vmm call type %#08x through vboxuser device node\n",
			req->request_type);
		return -EPERM;
	}

	return 0;
}

static int vbg_ioctl_vmmrequest(struct vbg_dev *gdev,
				struct vbg_session *session, void *data)
{
	struct vbg_ioctl_hdr *hdr = data;
	int ret;

	if (hdr->size_in != hdr->size_out)
		return -EINVAL;

	if (hdr->size_in > VMMDEV_MAX_VMMDEVREQ_SIZE)
		return -E2BIG;

	if (hdr->type == VBG_IOCTL_HDR_TYPE_DEFAULT)
		return -EINVAL;

	ret = vbg_req_allowed(gdev, session, data);
	if (ret < 0)
		return ret;

	vbg_req_perform(gdev, data);
	WARN_ON(hdr->rc == VINF_HGCM_ASYNC_EXECUTE);

	return 0;
}

static int vbg_ioctl_hgcm_connect(struct vbg_dev *gdev,
				  struct vbg_session *session,
				  struct vbg_ioctl_hgcm_connect *conn)
{
	u32 client_id;
	int i, ret;

	if (vbg_ioctl_chk(&conn->hdr, sizeof(conn->u.in), sizeof(conn->u.out)))
		return -EINVAL;

	/* Find a free place in the sessions clients array and claim it */
	mutex_lock(&gdev->session_mutex);
	for (i = 0; i < ARRAY_SIZE(session->hgcm_client_ids); i++) {
		if (!session->hgcm_client_ids[i]) {
			session->hgcm_client_ids[i] = U32_MAX;
			break;
		}
	}
	mutex_unlock(&gdev->session_mutex);

	if (i >= ARRAY_SIZE(session->hgcm_client_ids))
		return -EMFILE;

	ret = vbg_hgcm_connect(gdev, session->requestor, &conn->u.in.loc,
			       &client_id, &conn->hdr.rc);

	mutex_lock(&gdev->session_mutex);
	if (ret == 0 && conn->hdr.rc >= 0) {
		conn->u.out.client_id = client_id;
		session->hgcm_client_ids[i] = client_id;
	} else {
		conn->u.out.client_id = 0;
		session->hgcm_client_ids[i] = 0;
	}
	mutex_unlock(&gdev->session_mutex);

	return ret;
}

static int vbg_ioctl_hgcm_disconnect(struct vbg_dev *gdev,
				     struct vbg_session *session,
				     struct vbg_ioctl_hgcm_disconnect *disconn)
{
	u32 client_id;
	int i, ret;

	if (vbg_ioctl_chk(&disconn->hdr, sizeof(disconn->u.in), 0))
		return -EINVAL;

	client_id = disconn->u.in.client_id;
	if (client_id == 0 || client_id == U32_MAX)
		return -EINVAL;

	mutex_lock(&gdev->session_mutex);
	for (i = 0; i < ARRAY_SIZE(session->hgcm_client_ids); i++) {
		if (session->hgcm_client_ids[i] == client_id) {
			session->hgcm_client_ids[i] = U32_MAX;
			break;
		}
	}
	mutex_unlock(&gdev->session_mutex);

	if (i >= ARRAY_SIZE(session->hgcm_client_ids))
		return -EINVAL;

	ret = vbg_hgcm_disconnect(gdev, session->requestor, client_id,
				  &disconn->hdr.rc);

	mutex_lock(&gdev->session_mutex);
	if (ret == 0 && disconn->hdr.rc >= 0)
		session->hgcm_client_ids[i] = 0;
	else
		session->hgcm_client_ids[i] = client_id;
	mutex_unlock(&gdev->session_mutex);

	return ret;
}

static bool vbg_param_valid(enum vmmdev_hgcm_function_parameter_type type)
{
	switch (type) {
	case VMMDEV_HGCM_PARM_TYPE_32BIT:
	case VMMDEV_HGCM_PARM_TYPE_64BIT:
	case VMMDEV_HGCM_PARM_TYPE_LINADDR:
	case VMMDEV_HGCM_PARM_TYPE_LINADDR_IN:
	case VMMDEV_HGCM_PARM_TYPE_LINADDR_OUT:
		return true;
	default:
		return false;
	}
}

static int vbg_ioctl_hgcm_call(struct vbg_dev *gdev,
			       struct vbg_session *session, bool f32bit,
			       struct vbg_ioctl_hgcm_call *call)
{
	size_t actual_size;
	u32 client_id;
	int i, ret;

	if (call->hdr.size_in < sizeof(*call))
		return -EINVAL;

	if (call->hdr.size_in != call->hdr.size_out)
		return -EINVAL;

	if (call->parm_count > VMMDEV_HGCM_MAX_PARMS)
		return -E2BIG;

	client_id = call->client_id;
	if (client_id == 0 || client_id == U32_MAX)
		return -EINVAL;

	actual_size = sizeof(*call);
	if (f32bit)
		actual_size += call->parm_count *
			       sizeof(struct vmmdev_hgcm_function_parameter32);
	else
		actual_size += call->parm_count *
			       sizeof(struct vmmdev_hgcm_function_parameter);
	if (call->hdr.size_in < actual_size) {
		vbg_debug("VBG_IOCTL_HGCM_CALL: hdr.size_in %d required size is %zd\n",
			  call->hdr.size_in, actual_size);
		return -EINVAL;
	}
	call->hdr.size_out = actual_size;

	/* Validate parameter types */
	if (f32bit) {
		struct vmmdev_hgcm_function_parameter32 *parm =
			VBG_IOCTL_HGCM_CALL_PARMS32(call);

		for (i = 0; i < call->parm_count; i++)
			if (!vbg_param_valid(parm[i].type))
				return -EINVAL;
	} else {
		struct vmmdev_hgcm_function_parameter *parm =
			VBG_IOCTL_HGCM_CALL_PARMS(call);

		for (i = 0; i < call->parm_count; i++)
			if (!vbg_param_valid(parm[i].type))
				return -EINVAL;
	}

	/*
	 * Validate the client id.
	 */
	mutex_lock(&gdev->session_mutex);
	for (i = 0; i < ARRAY_SIZE(session->hgcm_client_ids); i++)
		if (session->hgcm_client_ids[i] == client_id)
			break;
	mutex_unlock(&gdev->session_mutex);
	if (i >= ARRAY_SIZE(session->hgcm_client_ids)) {
		vbg_debug("VBG_IOCTL_HGCM_CALL: INVALID handle. u32Client=%#08x\n",
			  client_id);
		return -EINVAL;
	}

	if (IS_ENABLED(CONFIG_COMPAT) && f32bit)
		ret = vbg_hgcm_call32(gdev, session->requestor, client_id,
				      call->function, call->timeout_ms,
				      VBG_IOCTL_HGCM_CALL_PARMS32(call),
				      call->parm_count, &call->hdr.rc);
	else
		ret = vbg_hgcm_call(gdev, session->requestor, client_id,
				    call->function, call->timeout_ms,
				    VBG_IOCTL_HGCM_CALL_PARMS(call),
				    call->parm_count, &call->hdr.rc);

	if (ret == -E2BIG) {
		/* E2BIG needs to be reported through the hdr.rc field. */
		call->hdr.rc = VERR_OUT_OF_RANGE;
		ret = 0;
	}

	if (ret && ret != -EINTR && ret != -ETIMEDOUT)
		vbg_err("VBG_IOCTL_HGCM_CALL error: %d\n", ret);

	return ret;
}

static int vbg_ioctl_log(struct vbg_ioctl_log *log)
{
	if (log->hdr.size_out != sizeof(log->hdr))
		return -EINVAL;

	vbg_info("%.*s", (int)(log->hdr.size_in - sizeof(log->hdr)),
		 log->u.in.msg);

	return 0;
}

static int vbg_ioctl_change_filter_mask(struct vbg_dev *gdev,
					struct vbg_session *session,
					struct vbg_ioctl_change_filter *filter)
{
	u32 or_mask, not_mask;

	if (vbg_ioctl_chk(&filter->hdr, sizeof(filter->u.in), 0))
		return -EINVAL;

	or_mask = filter->u.in.or_mask;
	not_mask = filter->u.in.not_mask;

	if ((or_mask | not_mask) & ~VMMDEV_EVENT_VALID_EVENT_MASK)
		return -EINVAL;

	return vbg_set_session_event_filter(gdev, session, or_mask, not_mask,
					    false);
}

static int vbg_ioctl_change_guest_capabilities(struct vbg_dev *gdev,
	     struct vbg_session *session, struct vbg_ioctl_set_guest_caps *caps)
{
	u32 or_mask, not_mask;
	int ret;

	if (vbg_ioctl_chk(&caps->hdr, sizeof(caps->u.in), sizeof(caps->u.out)))
		return -EINVAL;

	or_mask = caps->u.in.or_mask;
	not_mask = caps->u.in.not_mask;

	if ((or_mask | not_mask) & ~VMMDEV_GUEST_CAPABILITIES_MASK)
		return -EINVAL;

	ret = vbg_set_session_capabilities(gdev, session, or_mask, not_mask,
					   false);
	if (ret)
		return ret;

	caps->u.out.session_caps = session->set_guest_caps;
	caps->u.out.global_caps = gdev->guest_caps_host;

	return 0;
}

static int vbg_ioctl_check_balloon(struct vbg_dev *gdev,
				   struct vbg_ioctl_check_balloon *balloon_info)
{
	if (vbg_ioctl_chk(&balloon_info->hdr, 0, sizeof(balloon_info->u.out)))
		return -EINVAL;

	balloon_info->u.out.balloon_chunks = gdev->mem_balloon.chunks;
	/*
	 * Under Linux we handle VMMDEV_EVENT_BALLOON_CHANGE_REQUEST
	 * events entirely in the kernel, see vbg_core_isr().
	 */
	balloon_info->u.out.handle_in_r3 = false;

	return 0;
}

static int vbg_ioctl_write_core_dump(struct vbg_dev *gdev,
				     struct vbg_session *session,
				     struct vbg_ioctl_write_coredump *dump)
{
	struct vmmdev_write_core_dump *req;

	if (vbg_ioctl_chk(&dump->hdr, sizeof(dump->u.in), 0))
		return -EINVAL;

	req = vbg_req_alloc(sizeof(*req), VMMDEVREQ_WRITE_COREDUMP,
			    session->requestor);
	if (!req)
		return -ENOMEM;

	req->flags = dump->u.in.flags;
	dump->hdr.rc = vbg_req_perform(gdev, req);

	vbg_req_free(req, sizeof(*req));
	return 0;
}

/**
 * Common IOCtl for user to kernel communication.
 * Return: 0 or negative errno value.
 * @session:	The client session.
 * @req:	The requested function.
 * @data:	The i/o data buffer, minimum size sizeof(struct vbg_ioctl_hdr).
 */
int vbg_core_ioctl(struct vbg_session *session, unsigned int req, void *data)
{
	unsigned int req_no_size = req & ~IOCSIZE_MASK;
	struct vbg_dev *gdev = session->gdev;
	struct vbg_ioctl_hdr *hdr = data;
	bool f32bit = false;

	hdr->rc = VINF_SUCCESS;
	if (!hdr->size_out)
		hdr->size_out = hdr->size_in;

	/*
	 * hdr->version and hdr->size_in / hdr->size_out minimum size are
	 * already checked by vbg_misc_device_ioctl().
	 */

	/* For VMMDEV_REQUEST hdr->type != VBG_IOCTL_HDR_TYPE_DEFAULT */
	if (req_no_size == VBG_IOCTL_VMMDEV_REQUEST(0) ||
	    req == VBG_IOCTL_VMMDEV_REQUEST_BIG ||
	    req == VBG_IOCTL_VMMDEV_REQUEST_BIG_ALT)
		return vbg_ioctl_vmmrequest(gdev, session, data);

	if (hdr->type != VBG_IOCTL_HDR_TYPE_DEFAULT)
		return -EINVAL;

	/* Fixed size requests. */
	switch (req) {
	case VBG_IOCTL_DRIVER_VERSION_INFO:
		return vbg_ioctl_driver_version_info(data);
	case VBG_IOCTL_HGCM_CONNECT:
		return vbg_ioctl_hgcm_connect(gdev, session, data);
	case VBG_IOCTL_HGCM_DISCONNECT:
		return vbg_ioctl_hgcm_disconnect(gdev, session, data);
	case VBG_IOCTL_WAIT_FOR_EVENTS:
		return vbg_ioctl_wait_for_events(gdev, session, data);
	case VBG_IOCTL_INTERRUPT_ALL_WAIT_FOR_EVENTS:
		return vbg_ioctl_interrupt_all_wait_events(gdev, session, data);
	case VBG_IOCTL_CHANGE_FILTER_MASK:
		return vbg_ioctl_change_filter_mask(gdev, session, data);
	case VBG_IOCTL_CHANGE_GUEST_CAPABILITIES:
		return vbg_ioctl_change_guest_capabilities(gdev, session, data);
	case VBG_IOCTL_CHECK_BALLOON:
		return vbg_ioctl_check_balloon(gdev, data);
	case VBG_IOCTL_WRITE_CORE_DUMP:
		return vbg_ioctl_write_core_dump(gdev, session, data);
	}

	/* Variable sized requests. */
	switch (req_no_size) {
#ifdef CONFIG_COMPAT
	case VBG_IOCTL_HGCM_CALL_32(0):
		f32bit = true;
		fallthrough;
#endif
	case VBG_IOCTL_HGCM_CALL(0):
		return vbg_ioctl_hgcm_call(gdev, session, f32bit, data);
	case VBG_IOCTL_LOG(0):
	case VBG_IOCTL_LOG_ALT(0):
		return vbg_ioctl_log(data);
	}

	vbg_debug("VGDrvCommonIoCtl: Unknown req %#08x\n", req);
	return -ENOTTY;
}

/**
 * Report guest supported mouse-features to the host.
 *
 * Return: 0 or negative errno value.
 * @gdev:		The Guest extension device.
 * @features:		The set of features to report to the host.
 */
int vbg_core_set_mouse_status(struct vbg_dev *gdev, u32 features)
{
	struct vmmdev_mouse_status *req;
	int rc;

	req = vbg_req_alloc(sizeof(*req), VMMDEVREQ_SET_MOUSE_STATUS,
			    VBG_KERNEL_REQUEST);
	if (!req)
		return -ENOMEM;

	req->mouse_features = features;
	req->pointer_pos_x = 0;
	req->pointer_pos_y = 0;

	rc = vbg_req_perform(gdev, req);
	if (rc < 0)
		vbg_err("%s error, rc: %d\n", __func__, rc);

	vbg_req_free(req, sizeof(*req));
	return vbg_status_code_to_errno(rc);
}

/** Core interrupt service routine. */
irqreturn_t vbg_core_isr(int irq, void *dev_id)
{
	struct vbg_dev *gdev = dev_id;
	struct vmmdev_events *req = gdev->ack_events_req;
	bool mouse_position_changed = false;
	unsigned long flags;
	u32 events = 0;
	int rc;

	if (!gdev->mmio->V.V1_04.have_events)
		return IRQ_NONE;

	/* Get and acknowlegde events. */
	req->header.rc = VERR_INTERNAL_ERROR;
	req->events = 0;
	rc = vbg_req_perform(gdev, req);
	if (rc < 0) {
		vbg_err("Error performing events req, rc: %d\n", rc);
		return IRQ_NONE;
	}

	events = req->events;

	if (events & VMMDEV_EVENT_MOUSE_POSITION_CHANGED) {
		mouse_position_changed = true;
		events &= ~VMMDEV_EVENT_MOUSE_POSITION_CHANGED;
	}

	if (events & VMMDEV_EVENT_HGCM) {
		wake_up(&gdev->hgcm_wq);
		events &= ~VMMDEV_EVENT_HGCM;
	}

	if (events & VMMDEV_EVENT_BALLOON_CHANGE_REQUEST) {
		schedule_work(&gdev->mem_balloon.work);
		events &= ~VMMDEV_EVENT_BALLOON_CHANGE_REQUEST;
	}

	if (events) {
		spin_lock_irqsave(&gdev->event_spinlock, flags);
		gdev->pending_events |= events;
		spin_unlock_irqrestore(&gdev->event_spinlock, flags);

		wake_up(&gdev->event_wq);
	}

	if (mouse_position_changed)
		vbg_linux_mouse_event(gdev);

	return IRQ_HANDLED;
}
