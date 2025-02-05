// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_pcode.h"

#include <linux/delay.h>
#include <linux/errno.h>

#include <drm/drm_managed.h>

#include "xe_assert.h"
#include "xe_device.h"
#include "xe_mmio.h"
#include "xe_pcode_api.h"

/**
 * DOC: PCODE
 *
 * Xe PCODE is the component responsible for interfacing with the PCODE
 * firmware.
 * It shall provide a very simple ABI to other Xe components, but be the
 * single and consolidated place that will communicate with PCODE. All read
 * and write operations to PCODE will be internal and private to this component.
 *
 * What's next:
 * - PCODE hw metrics
 * - PCODE for display operations
 */

static int pcode_mailbox_status(struct xe_tile *tile)
{
	u32 err;
	static const struct pcode_err_decode err_decode[] = {
		[PCODE_ILLEGAL_CMD] = {-ENXIO, "Illegal Command"},
		[PCODE_TIMEOUT] = {-ETIMEDOUT, "Timed out"},
		[PCODE_ILLEGAL_DATA] = {-EINVAL, "Illegal Data"},
		[PCODE_ILLEGAL_SUBCOMMAND] = {-ENXIO, "Illegal Subcommand"},
		[PCODE_LOCKED] = {-EBUSY, "PCODE Locked"},
		[PCODE_GT_RATIO_OUT_OF_RANGE] = {-EOVERFLOW,
			"GT ratio out of range"},
		[PCODE_REJECTED] = {-EACCES, "PCODE Rejected"},
		[PCODE_ERROR_MASK] = {-EPROTO, "Unknown"},
	};

	err = xe_mmio_read32(&tile->mmio, PCODE_MAILBOX) & PCODE_ERROR_MASK;
	if (err) {
		drm_err(&tile_to_xe(tile)->drm, "PCODE Mailbox failed: %d %s", err,
			err_decode[err].str ?: "Unknown");
		return err_decode[err].errno ?: -EPROTO;
	}

	return 0;
}

static int __pcode_mailbox_rw(struct xe_tile *tile, u32 mbox, u32 *data0, u32 *data1,
			      unsigned int timeout_ms, bool return_data,
			      bool atomic)
{
	struct xe_mmio *mmio = &tile->mmio;
	int err;

	if (tile_to_xe(tile)->info.skip_pcode)
		return 0;

	if ((xe_mmio_read32(mmio, PCODE_MAILBOX) & PCODE_READY) != 0)
		return -EAGAIN;

	xe_mmio_write32(mmio, PCODE_DATA0, *data0);
	xe_mmio_write32(mmio, PCODE_DATA1, data1 ? *data1 : 0);
	xe_mmio_write32(mmio, PCODE_MAILBOX, PCODE_READY | mbox);

	err = xe_mmio_wait32(mmio, PCODE_MAILBOX, PCODE_READY, 0,
			     timeout_ms * USEC_PER_MSEC, NULL, atomic);
	if (err)
		return err;

	if (return_data) {
		*data0 = xe_mmio_read32(mmio, PCODE_DATA0);
		if (data1)
			*data1 = xe_mmio_read32(mmio, PCODE_DATA1);
	}

	return pcode_mailbox_status(tile);
}

static int pcode_mailbox_rw(struct xe_tile *tile, u32 mbox, u32 *data0, u32 *data1,
			    unsigned int timeout_ms, bool return_data,
			    bool atomic)
{
	if (tile_to_xe(tile)->info.skip_pcode)
		return 0;

	lockdep_assert_held(&tile->pcode.lock);

	return __pcode_mailbox_rw(tile, mbox, data0, data1, timeout_ms, return_data, atomic);
}

int xe_pcode_write_timeout(struct xe_tile *tile, u32 mbox, u32 data, int timeout)
{
	int err;

	mutex_lock(&tile->pcode.lock);
	err = pcode_mailbox_rw(tile, mbox, &data, NULL, timeout, false, false);
	mutex_unlock(&tile->pcode.lock);

	return err;
}

int xe_pcode_read(struct xe_tile *tile, u32 mbox, u32 *val, u32 *val1)
{
	int err;

	mutex_lock(&tile->pcode.lock);
	err = pcode_mailbox_rw(tile, mbox, val, val1, 1, true, false);
	mutex_unlock(&tile->pcode.lock);

	return err;
}

static int pcode_try_request(struct xe_tile *tile, u32 mbox,
			     u32 request, u32 reply_mask, u32 reply,
			     u32 *status, bool atomic, int timeout_us, bool locked)
{
	int slept, wait = 10;

	xe_tile_assert(tile, timeout_us > 0);

	for (slept = 0; slept < timeout_us; slept += wait) {
		if (locked)
			*status = pcode_mailbox_rw(tile, mbox, &request, NULL, 1, true,
						   atomic);
		else
			*status = __pcode_mailbox_rw(tile, mbox, &request, NULL, 1, true,
						     atomic);
		if ((*status == 0) && ((request & reply_mask) == reply))
			return 0;

		if (atomic)
			udelay(wait);
		else
			usleep_range(wait, wait << 1);
		wait <<= 1;
	}

	return -ETIMEDOUT;
}

/**
 * xe_pcode_request - send PCODE request until acknowledgment
 * @tile: tile
 * @mbox: PCODE mailbox ID the request is targeted for
 * @request: request ID
 * @reply_mask: mask used to check for request acknowledgment
 * @reply: value used to check for request acknowledgment
 * @timeout_base_ms: timeout for polling with preemption enabled
 *
 * Keep resending the @request to @mbox until PCODE acknowledges it, PCODE
 * reports an error or an overall timeout of @timeout_base_ms+50 ms expires.
 * The request is acknowledged once the PCODE reply dword equals @reply after
 * applying @reply_mask. Polling is first attempted with preemption enabled
 * for @timeout_base_ms and if this times out for another 50 ms with
 * preemption disabled.
 *
 * Returns 0 on success, %-ETIMEDOUT in case of a timeout, <0 in case of some
 * other error as reported by PCODE.
 */
int xe_pcode_request(struct xe_tile *tile, u32 mbox, u32 request,
		     u32 reply_mask, u32 reply, int timeout_base_ms)
{
	u32 status;
	int ret;

	xe_tile_assert(tile, timeout_base_ms <= 3);

	mutex_lock(&tile->pcode.lock);

	ret = pcode_try_request(tile, mbox, request, reply_mask, reply, &status,
				false, timeout_base_ms * 1000, true);
	if (!ret)
		goto out;

	/*
	 * The above can time out if the number of requests was low (2 in the
	 * worst case) _and_ PCODE was busy for some reason even after a
	 * (queued) request and @timeout_base_ms delay. As a workaround retry
	 * the poll with preemption disabled to maximize the number of
	 * requests. Increase the timeout from @timeout_base_ms to 50ms to
	 * account for interrupts that could reduce the number of these
	 * requests, and for any quirks of the PCODE firmware that delays
	 * the request completion.
	 */
	drm_err(&tile_to_xe(tile)->drm,
		"PCODE timeout, retrying with preemption disabled\n");
	preempt_disable();
	ret = pcode_try_request(tile, mbox, request, reply_mask, reply, &status,
				true, 50 * 1000, true);
	preempt_enable();

out:
	mutex_unlock(&tile->pcode.lock);
	return status ? status : ret;
}
/**
 * xe_pcode_init_min_freq_table - Initialize PCODE's QOS frequency table
 * @tile: tile instance
 * @min_gt_freq: Minimal (RPn) GT frequency in units of 50MHz.
 * @max_gt_freq: Maximal (RP0) GT frequency in units of 50MHz.
 *
 * This function initialize PCODE's QOS frequency table for a proper minimal
 * frequency/power steering decision, depending on the current requested GT
 * frequency. For older platforms this was a more complete table including
 * the IA freq. However for the latest platforms this table become a simple
 * 1-1 Ring vs GT frequency. Even though, without setting it, PCODE might
 * not take the right decisions for some memory frequencies and affect latency.
 *
 * It returns 0 on success, and -ERROR number on failure, -EINVAL if max
 * frequency is higher then the minimal, and other errors directly translated
 * from the PCODE Error returns:
 * - -ENXIO: "Illegal Command"
 * - -ETIMEDOUT: "Timed out"
 * - -EINVAL: "Illegal Data"
 * - -ENXIO, "Illegal Subcommand"
 * - -EBUSY: "PCODE Locked"
 * - -EOVERFLOW, "GT ratio out of range"
 * - -EACCES, "PCODE Rejected"
 * - -EPROTO, "Unknown"
 */
int xe_pcode_init_min_freq_table(struct xe_tile *tile, u32 min_gt_freq,
				 u32 max_gt_freq)
{
	int ret;
	u32 freq;

	if (!tile_to_xe(tile)->info.has_llc)
		return 0;

	if (max_gt_freq <= min_gt_freq)
		return -EINVAL;

	mutex_lock(&tile->pcode.lock);
	for (freq = min_gt_freq; freq <= max_gt_freq; freq++) {
		u32 data = freq << PCODE_FREQ_RING_RATIO_SHIFT | freq;

		ret = pcode_mailbox_rw(tile, PCODE_WRITE_MIN_FREQ_TABLE,
				       &data, NULL, 1, false, false);
		if (ret)
			goto unlock;
	}

unlock:
	mutex_unlock(&tile->pcode.lock);
	return ret;
}

/**
 * xe_pcode_ready - Ensure PCODE is initialized
 * @xe: xe instance
 * @locked: true if lock held, false otherwise
 *
 * PCODE init mailbox is polled only on root gt of root tile
 * as the root tile provides the initialization is complete only
 * after all the tiles have completed the initialization.
 * Called only on early probe without locks and with locks in
 * resume path.
 *
 * Returns 0 on success, and -error number on failure.
 */
int xe_pcode_ready(struct xe_device *xe, bool locked)
{
	u32 status, request = DGFX_GET_INIT_STATUS;
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	int timeout_us = 180000000; /* 3 min */
	int ret;

	if (xe->info.skip_pcode)
		return 0;

	if (!IS_DGFX(xe))
		return 0;

	if (locked)
		mutex_lock(&tile->pcode.lock);

	ret = pcode_try_request(tile, DGFX_PCODE_STATUS, request,
				DGFX_INIT_STATUS_COMPLETE,
				DGFX_INIT_STATUS_COMPLETE,
				&status, false, timeout_us, locked);

	if (locked)
		mutex_unlock(&tile->pcode.lock);

	if (ret)
		drm_err(&xe->drm,
			"PCODE initialization timedout after: 3 min\n");

	return ret;
}

/**
 * xe_pcode_init: initialize components of PCODE
 * @tile: tile instance
 *
 * This function initializes the xe_pcode component.
 * To be called once only during probe.
 */
void xe_pcode_init(struct xe_tile *tile)
{
	drmm_mutex_init(&tile_to_xe(tile)->drm, &tile->pcode.lock);
}

/**
 * xe_pcode_probe_early: initializes PCODE
 * @xe: xe instance
 *
 * This function checks the initialization status of PCODE
 * To be called once only during early probe without locks.
 *
 * Returns 0 on success, error code otherwise
 */
int xe_pcode_probe_early(struct xe_device *xe)
{
	return xe_pcode_ready(xe, false);
}
