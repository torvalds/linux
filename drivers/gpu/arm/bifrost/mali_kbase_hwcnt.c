/*
 *
 * (C) COPYRIGHT 2018, 2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

/*
 * Implementation of hardware counter context and accumulator APIs.
 */

#include "mali_kbase_hwcnt_context.h"
#include "mali_kbase_hwcnt_accumulator.h"
#include "mali_kbase_hwcnt_backend.h"
#include "mali_kbase_hwcnt_types.h"
#include "mali_malisw.h"
#include "mali_kbase_debug.h"
#include "mali_kbase_linux.h"

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

/**
 * enum kbase_hwcnt_accum_state - Hardware counter accumulator states.
 * @ACCUM_STATE_ERROR:    Error state, where all accumulator operations fail.
 * @ACCUM_STATE_DISABLED: Disabled state, where dumping is always disabled.
 * @ACCUM_STATE_ENABLED:  Enabled state, where dumping is enabled if there are
 *                        any enabled counters.
 */
enum kbase_hwcnt_accum_state {
	ACCUM_STATE_ERROR,
	ACCUM_STATE_DISABLED,
	ACCUM_STATE_ENABLED
};

/**
 * struct kbase_hwcnt_accumulator - Hardware counter accumulator structure.
 * @backend:                Pointer to created counter backend.
 * @state:                  The current state of the accumulator.
 *                           - State transition from disabled->enabled or
 *                             disabled->error requires state_lock.
 *                           - State transition from enabled->disabled or
 *                             enabled->error requires both accum_lock and
 *                             state_lock.
 *                           - Error state persists until next disable.
 * @enable_map:             The current set of enabled counters.
 *                           - Must only be modified while holding both
 *                             accum_lock and state_lock.
 *                           - Can be read while holding either lock.
 *                           - Must stay in sync with enable_map_any_enabled.
 * @enable_map_any_enabled: True if any counters in the map are enabled, else
 *                          false. If true, and state is ACCUM_STATE_ENABLED,
 *                          then the counter backend will be enabled.
 *                           - Must only be modified while holding both
 *                             accum_lock and state_lock.
 *                           - Can be read while holding either lock.
 *                           - Must stay in sync with enable_map.
 * @scratch_map:            Scratch enable map, used as temporary enable map
 *                          storage during dumps.
 *                           - Must only be read or modified while holding
 *                             accum_lock.
 * @accum_buf:              Accumulation buffer, where dumps will be accumulated
 *                          into on transition to a disable state.
 *                           - Must only be read or modified while holding
 *                             accum_lock.
 * @accumulated:            True if the accumulation buffer has been accumulated
 *                          into and not subsequently read from yet, else false.
 *                           - Must only be read or modified while holding
 *                             accum_lock.
 * @ts_last_dump_ns:        Timestamp (ns) of the end time of the most recent
 *                          dump that was requested by the user.
 *                           - Must only be read or modified while holding
 *                             accum_lock.
 */
struct kbase_hwcnt_accumulator {
	struct kbase_hwcnt_backend *backend;
	enum kbase_hwcnt_accum_state state;
	struct kbase_hwcnt_enable_map enable_map;
	bool enable_map_any_enabled;
	struct kbase_hwcnt_enable_map scratch_map;
	struct kbase_hwcnt_dump_buffer accum_buf;
	bool accumulated;
	u64 ts_last_dump_ns;
};

/**
 * struct kbase_hwcnt_context - Hardware counter context structure.
 * @iface:         Pointer to hardware counter backend interface.
 * @state_lock:    Spinlock protecting state.
 * @disable_count: Disable count of the context. Initialised to 1.
 *                 Decremented when the accumulator is acquired, and incremented
 *                 on release. Incremented on calls to
 *                 kbase_hwcnt_context_disable[_atomic], and decremented on
 *                 calls to kbase_hwcnt_context_enable.
 *                  - Must only be read or modified while holding state_lock.
 * @accum_lock:    Mutex protecting accumulator.
 * @accum_inited:  Flag to prevent concurrent accumulator initialisation and/or
 *                 termination. Set to true before accumulator initialisation,
 *                 and false after accumulator termination.
 *                  - Must only be modified while holding both accum_lock and
 *                    state_lock.
 *                  - Can be read while holding either lock.
 * @accum:         Hardware counter accumulator structure.
 */
struct kbase_hwcnt_context {
	const struct kbase_hwcnt_backend_interface *iface;
	spinlock_t state_lock;
	size_t disable_count;
	struct mutex accum_lock;
	bool accum_inited;
	struct kbase_hwcnt_accumulator accum;
};

int kbase_hwcnt_context_init(
	const struct kbase_hwcnt_backend_interface *iface,
	struct kbase_hwcnt_context **out_hctx)
{
	struct kbase_hwcnt_context *hctx = NULL;

	if (!iface || !out_hctx)
		return -EINVAL;

	hctx = kzalloc(sizeof(*hctx), GFP_KERNEL);
	if (!hctx)
		return -ENOMEM;

	hctx->iface = iface;
	spin_lock_init(&hctx->state_lock);
	hctx->disable_count = 1;
	mutex_init(&hctx->accum_lock);
	hctx->accum_inited = false;

	*out_hctx = hctx;

	return 0;
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_context_init);

void kbase_hwcnt_context_term(struct kbase_hwcnt_context *hctx)
{
	if (!hctx)
		return;

	/* Make sure we didn't leak the accumulator */
	WARN_ON(hctx->accum_inited);
	kfree(hctx);
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_context_term);

/**
 * kbasep_hwcnt_accumulator_term() - Terminate the accumulator for the context.
 * @hctx: Non-NULL pointer to hardware counter context.
 */
static void kbasep_hwcnt_accumulator_term(struct kbase_hwcnt_context *hctx)
{
	WARN_ON(!hctx);
	WARN_ON(!hctx->accum_inited);

	kbase_hwcnt_enable_map_free(&hctx->accum.scratch_map);
	kbase_hwcnt_dump_buffer_free(&hctx->accum.accum_buf);
	kbase_hwcnt_enable_map_free(&hctx->accum.enable_map);
	hctx->iface->term(hctx->accum.backend);
	memset(&hctx->accum, 0, sizeof(hctx->accum));
}

/**
 * kbasep_hwcnt_accumulator_init() - Initialise the accumulator for the context.
 * @hctx: Non-NULL pointer to hardware counter context.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_hwcnt_accumulator_init(struct kbase_hwcnt_context *hctx)
{
	int errcode;

	WARN_ON(!hctx);
	WARN_ON(!hctx->accum_inited);

	errcode = hctx->iface->init(
		hctx->iface->info, &hctx->accum.backend);
	if (errcode)
		goto error;

	hctx->accum.state = ACCUM_STATE_ERROR;

	errcode = kbase_hwcnt_enable_map_alloc(
		hctx->iface->metadata, &hctx->accum.enable_map);
	if (errcode)
		goto error;

	hctx->accum.enable_map_any_enabled = false;

	errcode = kbase_hwcnt_dump_buffer_alloc(
		hctx->iface->metadata, &hctx->accum.accum_buf);
	if (errcode)
		goto error;

	errcode = kbase_hwcnt_enable_map_alloc(
		hctx->iface->metadata, &hctx->accum.scratch_map);
	if (errcode)
		goto error;

	hctx->accum.accumulated = false;

	hctx->accum.ts_last_dump_ns =
		hctx->iface->timestamp_ns(hctx->accum.backend);

	return 0;

error:
	kbasep_hwcnt_accumulator_term(hctx);
	return errcode;
}

/**
 * kbasep_hwcnt_accumulator_disable() - Transition the accumulator into the
 *                                      disabled state, from the enabled or
 *                                      error states.
 * @hctx:       Non-NULL pointer to hardware counter context.
 * @accumulate: True if we should accumulate before disabling, else false.
 */
static void kbasep_hwcnt_accumulator_disable(
	struct kbase_hwcnt_context *hctx, bool accumulate)
{
	int errcode = 0;
	bool backend_enabled = false;
	struct kbase_hwcnt_accumulator *accum;
	unsigned long flags;
	u64 dump_time_ns;

	WARN_ON(!hctx);
	lockdep_assert_held(&hctx->accum_lock);
	WARN_ON(!hctx->accum_inited);

	accum = &hctx->accum;

	spin_lock_irqsave(&hctx->state_lock, flags);

	WARN_ON(hctx->disable_count != 0);
	WARN_ON(hctx->accum.state == ACCUM_STATE_DISABLED);

	if ((hctx->accum.state == ACCUM_STATE_ENABLED) &&
	    (accum->enable_map_any_enabled))
		backend_enabled = true;

	if (!backend_enabled)
		hctx->accum.state = ACCUM_STATE_DISABLED;

	spin_unlock_irqrestore(&hctx->state_lock, flags);

	/* Early out if the backend is not already enabled */
	if (!backend_enabled)
		return;

	if (!accumulate)
		goto disable;

	/* Try and accumulate before disabling */
	errcode = hctx->iface->dump_request(accum->backend, &dump_time_ns);
	if (errcode)
		goto disable;

	errcode = hctx->iface->dump_wait(accum->backend);
	if (errcode)
		goto disable;

	errcode = hctx->iface->dump_get(accum->backend,
		&accum->accum_buf, &accum->enable_map, accum->accumulated);
	if (errcode)
		goto disable;

	accum->accumulated = true;

disable:
	hctx->iface->dump_disable(accum->backend);

	/* Regardless of any errors during the accumulate, put the accumulator
	 * in the disabled state.
	 */
	spin_lock_irqsave(&hctx->state_lock, flags);

	hctx->accum.state = ACCUM_STATE_DISABLED;

	spin_unlock_irqrestore(&hctx->state_lock, flags);
}

/**
 * kbasep_hwcnt_accumulator_enable() - Transition the accumulator into the
 *                                     enabled state, from the disabled state.
 * @hctx: Non-NULL pointer to hardware counter context.
 */
static void kbasep_hwcnt_accumulator_enable(struct kbase_hwcnt_context *hctx)
{
	int errcode = 0;
	struct kbase_hwcnt_accumulator *accum;

	WARN_ON(!hctx);
	lockdep_assert_held(&hctx->state_lock);
	WARN_ON(!hctx->accum_inited);
	WARN_ON(hctx->accum.state != ACCUM_STATE_DISABLED);

	accum = &hctx->accum;

	/* The backend only needs enabling if any counters are enabled */
	if (accum->enable_map_any_enabled)
		errcode = hctx->iface->dump_enable_nolock(
			accum->backend, &accum->enable_map);

	if (!errcode)
		accum->state = ACCUM_STATE_ENABLED;
	else
		accum->state = ACCUM_STATE_ERROR;
}

/**
 * kbasep_hwcnt_accumulator_dump() - Perform a dump with the most up-to-date
 *                                   values of enabled counters possible, and
 *                                   optionally update the set of enabled
 *                                   counters.
 * @hctx :       Non-NULL pointer to the hardware counter context
 * @ts_start_ns: Non-NULL pointer where the start timestamp of the dump will
 *               be written out to on success
 * @ts_end_ns:   Non-NULL pointer where the end timestamp of the dump will
 *               be written out to on success
 * @dump_buf:    Pointer to the buffer where the dump will be written out to on
 *               success. If non-NULL, must have the same metadata as the
 *               accumulator. If NULL, the dump will be discarded.
 * @new_map:     Pointer to the new counter enable map. If non-NULL, must have
 *               the same metadata as the accumulator. If NULL, the set of
 *               enabled counters will be unchanged.
 */
static int kbasep_hwcnt_accumulator_dump(
	struct kbase_hwcnt_context *hctx,
	u64 *ts_start_ns,
	u64 *ts_end_ns,
	struct kbase_hwcnt_dump_buffer *dump_buf,
	const struct kbase_hwcnt_enable_map *new_map)
{
	int errcode = 0;
	unsigned long flags;
	enum kbase_hwcnt_accum_state state;
	bool dump_requested = false;
	bool dump_written = false;
	bool cur_map_any_enabled;
	struct kbase_hwcnt_enable_map *cur_map;
	bool new_map_any_enabled = false;
	u64 dump_time_ns;
	struct kbase_hwcnt_accumulator *accum;

	WARN_ON(!hctx);
	WARN_ON(!ts_start_ns);
	WARN_ON(!ts_end_ns);
	WARN_ON(dump_buf && (dump_buf->metadata != hctx->iface->metadata));
	WARN_ON(new_map && (new_map->metadata != hctx->iface->metadata));
	WARN_ON(!hctx->accum_inited);
	lockdep_assert_held(&hctx->accum_lock);

	accum = &hctx->accum;
	cur_map = &accum->scratch_map;

	/* Save out info about the current enable map */
	cur_map_any_enabled = accum->enable_map_any_enabled;
	kbase_hwcnt_enable_map_copy(cur_map, &accum->enable_map);

	if (new_map)
		new_map_any_enabled =
			kbase_hwcnt_enable_map_any_enabled(new_map);

	/*
	 * We're holding accum_lock, so the accumulator state might transition
	 * from disabled to enabled during this function (as enabling is lock
	 * free), but it will never disable (as disabling needs to hold the
	 * accum_lock), nor will it ever transition from enabled to error (as
	 * an enable while we're already enabled is impossible).
	 *
	 * If we're already disabled, we'll only look at the accumulation buffer
	 * rather than do a real dump, so a concurrent enable does not affect
	 * us.
	 *
	 * If a concurrent enable fails, we might transition to the error
	 * state, but again, as we're only looking at the accumulation buffer,
	 * it's not an issue.
	 */
	spin_lock_irqsave(&hctx->state_lock, flags);

	state = accum->state;

	/*
	 * Update the new map now, such that if an enable occurs during this
	 * dump then that enable will set the new map. If we're already enabled,
	 * then we'll do it ourselves after the dump.
	 */
	if (new_map) {
		kbase_hwcnt_enable_map_copy(
			&accum->enable_map, new_map);
		accum->enable_map_any_enabled = new_map_any_enabled;
	}

	spin_unlock_irqrestore(&hctx->state_lock, flags);

	/* Error state, so early out. No need to roll back any map updates */
	if (state == ACCUM_STATE_ERROR)
		return -EIO;

	/* Initiate the dump if the backend is enabled. */
	if ((state == ACCUM_STATE_ENABLED) && cur_map_any_enabled) {
		if (dump_buf) {
			errcode = hctx->iface->dump_request(
					accum->backend, &dump_time_ns);
			dump_requested = true;
		} else {
			dump_time_ns = hctx->iface->timestamp_ns(
					accum->backend);
			errcode = hctx->iface->dump_clear(accum->backend);
		}

		if (errcode)
			goto error;
	} else {
		dump_time_ns = hctx->iface->timestamp_ns(accum->backend);
	}

	/* Copy any accumulation into the dest buffer */
	if (accum->accumulated && dump_buf) {
		kbase_hwcnt_dump_buffer_copy(
			dump_buf, &accum->accum_buf, cur_map);
		dump_written = true;
	}

	/* Wait for any requested dumps to complete */
	if (dump_requested) {
		WARN_ON(state != ACCUM_STATE_ENABLED);
		errcode = hctx->iface->dump_wait(accum->backend);
		if (errcode)
			goto error;
	}

	/* If we're enabled and there's a new enable map, change the enabled set
	 * as soon after the dump has completed as possible.
	 */
	if ((state == ACCUM_STATE_ENABLED) && new_map) {
		/* Backend is only enabled if there were any enabled counters */
		if (cur_map_any_enabled)
			hctx->iface->dump_disable(accum->backend);

		/* (Re-)enable the backend if the new map has enabled counters.
		 * No need to acquire the spinlock, as concurrent enable while
		 * we're already enabled and holding accum_lock is impossible.
		 */
		if (new_map_any_enabled) {
			errcode = hctx->iface->dump_enable(
				accum->backend, new_map);
			if (errcode)
				goto error;
		}
	}

	/* Copy, accumulate, or zero into the dest buffer to finish */
	if (dump_buf) {
		/* If we dumped, copy or accumulate it into the destination */
		if (dump_requested) {
			WARN_ON(state != ACCUM_STATE_ENABLED);
			errcode = hctx->iface->dump_get(
				accum->backend,
				dump_buf,
				cur_map,
				dump_written);
			if (errcode)
				goto error;
			dump_written = true;
		}

		/* If we've not written anything into the dump buffer so far, it
		 * means there was nothing to write. Zero any enabled counters.
		 */
		if (!dump_written)
			kbase_hwcnt_dump_buffer_zero(dump_buf, cur_map);
	}

	/* Write out timestamps */
	*ts_start_ns = accum->ts_last_dump_ns;
	*ts_end_ns = dump_time_ns;

	accum->accumulated = false;
	accum->ts_last_dump_ns = dump_time_ns;

	return 0;
error:
	/* An error was only physically possible if the backend was enabled */
	WARN_ON(state != ACCUM_STATE_ENABLED);

	/* Disable the backend, and transition to the error state */
	hctx->iface->dump_disable(accum->backend);
	spin_lock_irqsave(&hctx->state_lock, flags);

	accum->state = ACCUM_STATE_ERROR;

	spin_unlock_irqrestore(&hctx->state_lock, flags);

	return errcode;
}

/**
 * kbasep_hwcnt_context_disable() - Increment the disable count of the context.
 * @hctx:       Non-NULL pointer to hardware counter context.
 * @accumulate: True if we should accumulate before disabling, else false.
 */
static void kbasep_hwcnt_context_disable(
	struct kbase_hwcnt_context *hctx, bool accumulate)
{
	unsigned long flags;

	WARN_ON(!hctx);
	lockdep_assert_held(&hctx->accum_lock);

	if (!kbase_hwcnt_context_disable_atomic(hctx)) {
		kbasep_hwcnt_accumulator_disable(hctx, accumulate);

		spin_lock_irqsave(&hctx->state_lock, flags);

		/* Atomic disable failed and we're holding the mutex, so current
		 * disable count must be 0.
		 */
		WARN_ON(hctx->disable_count != 0);
		hctx->disable_count++;

		spin_unlock_irqrestore(&hctx->state_lock, flags);
	}
}

int kbase_hwcnt_accumulator_acquire(
	struct kbase_hwcnt_context *hctx,
	struct kbase_hwcnt_accumulator **accum)
{
	int errcode = 0;
	unsigned long flags;

	if (!hctx || !accum)
		return -EINVAL;

	mutex_lock(&hctx->accum_lock);
	spin_lock_irqsave(&hctx->state_lock, flags);

	if (!hctx->accum_inited)
		/* Set accum initing now to prevent concurrent init */
		hctx->accum_inited = true;
	else
		/* Already have an accum, or already being inited */
		errcode = -EBUSY;

	spin_unlock_irqrestore(&hctx->state_lock, flags);
	mutex_unlock(&hctx->accum_lock);

	if (errcode)
		return errcode;

	errcode = kbasep_hwcnt_accumulator_init(hctx);

	if (errcode) {
		mutex_lock(&hctx->accum_lock);
		spin_lock_irqsave(&hctx->state_lock, flags);

		hctx->accum_inited = false;

		spin_unlock_irqrestore(&hctx->state_lock, flags);
		mutex_unlock(&hctx->accum_lock);

		return errcode;
	}

	spin_lock_irqsave(&hctx->state_lock, flags);

	WARN_ON(hctx->disable_count == 0);
	WARN_ON(hctx->accum.enable_map_any_enabled);

	/* Decrement the disable count to allow the accumulator to be accessible
	 * now that it's fully constructed.
	 */
	hctx->disable_count--;

	/*
	 * Make sure the accumulator is initialised to the correct state.
	 * Regardless of initial state, counters don't need to be enabled via
	 * the backend, as the initial enable map has no enabled counters.
	 */
	hctx->accum.state = (hctx->disable_count == 0) ?
		ACCUM_STATE_ENABLED :
		ACCUM_STATE_DISABLED;

	spin_unlock_irqrestore(&hctx->state_lock, flags);

	*accum = &hctx->accum;

	return 0;
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_accumulator_acquire);

void kbase_hwcnt_accumulator_release(struct kbase_hwcnt_accumulator *accum)
{
	unsigned long flags;
	struct kbase_hwcnt_context *hctx;

	if (!accum)
		return;

	hctx = container_of(accum, struct kbase_hwcnt_context, accum);

	mutex_lock(&hctx->accum_lock);

	/* Double release is a programming error */
	WARN_ON(!hctx->accum_inited);

	/* Disable the context to ensure the accumulator is inaccesible while
	 * we're destroying it. This performs the corresponding disable count
	 * increment to the decrement done during acquisition.
	 */
	kbasep_hwcnt_context_disable(hctx, false);

	mutex_unlock(&hctx->accum_lock);

	kbasep_hwcnt_accumulator_term(hctx);

	mutex_lock(&hctx->accum_lock);
	spin_lock_irqsave(&hctx->state_lock, flags);

	hctx->accum_inited = false;

	spin_unlock_irqrestore(&hctx->state_lock, flags);
	mutex_unlock(&hctx->accum_lock);
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_accumulator_release);

void kbase_hwcnt_context_disable(struct kbase_hwcnt_context *hctx)
{
	if (WARN_ON(!hctx))
		return;

	/* Try and atomically disable first, so we can avoid locking the mutex
	 * if we don't need to.
	 */
	if (kbase_hwcnt_context_disable_atomic(hctx))
		return;

	mutex_lock(&hctx->accum_lock);

	kbasep_hwcnt_context_disable(hctx, true);

	mutex_unlock(&hctx->accum_lock);
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_context_disable);

bool kbase_hwcnt_context_disable_atomic(struct kbase_hwcnt_context *hctx)
{
	unsigned long flags;
	bool atomic_disabled = false;

	if (WARN_ON(!hctx))
		return false;

	spin_lock_irqsave(&hctx->state_lock, flags);

	if (!WARN_ON(hctx->disable_count == SIZE_MAX)) {
		/*
		 * If disable count is non-zero, we can just bump the disable
		 * count.
		 *
		 * Otherwise, we can't disable in an atomic context.
		 */
		if (hctx->disable_count != 0) {
			hctx->disable_count++;
			atomic_disabled = true;
		}
	}

	spin_unlock_irqrestore(&hctx->state_lock, flags);

	return atomic_disabled;
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_context_disable_atomic);

void kbase_hwcnt_context_enable(struct kbase_hwcnt_context *hctx)
{
	unsigned long flags;

	if (WARN_ON(!hctx))
		return;

	spin_lock_irqsave(&hctx->state_lock, flags);

	if (!WARN_ON(hctx->disable_count == 0)) {
		if (hctx->disable_count == 1)
			kbasep_hwcnt_accumulator_enable(hctx);

		hctx->disable_count--;
	}

	spin_unlock_irqrestore(&hctx->state_lock, flags);
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_context_enable);

const struct kbase_hwcnt_metadata *kbase_hwcnt_context_metadata(
	struct kbase_hwcnt_context *hctx)
{
	if (!hctx)
		return NULL;

	return hctx->iface->metadata;
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_context_metadata);

int kbase_hwcnt_accumulator_set_counters(
	struct kbase_hwcnt_accumulator *accum,
	const struct kbase_hwcnt_enable_map *new_map,
	u64 *ts_start_ns,
	u64 *ts_end_ns,
	struct kbase_hwcnt_dump_buffer *dump_buf)
{
	int errcode;
	struct kbase_hwcnt_context *hctx;

	if (!accum || !new_map || !ts_start_ns || !ts_end_ns)
		return -EINVAL;

	hctx = container_of(accum, struct kbase_hwcnt_context, accum);

	if ((new_map->metadata != hctx->iface->metadata) ||
	    (dump_buf && (dump_buf->metadata != hctx->iface->metadata)))
		return -EINVAL;

	mutex_lock(&hctx->accum_lock);

	errcode = kbasep_hwcnt_accumulator_dump(
		hctx, ts_start_ns, ts_end_ns, dump_buf, new_map);

	mutex_unlock(&hctx->accum_lock);

	return errcode;
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_accumulator_set_counters);

int kbase_hwcnt_accumulator_dump(
	struct kbase_hwcnt_accumulator *accum,
	u64 *ts_start_ns,
	u64 *ts_end_ns,
	struct kbase_hwcnt_dump_buffer *dump_buf)
{
	int errcode;
	struct kbase_hwcnt_context *hctx;

	if (!accum || !ts_start_ns || !ts_end_ns)
		return -EINVAL;

	hctx = container_of(accum, struct kbase_hwcnt_context, accum);

	if (dump_buf && (dump_buf->metadata != hctx->iface->metadata))
		return -EINVAL;

	mutex_lock(&hctx->accum_lock);

	errcode = kbasep_hwcnt_accumulator_dump(
		hctx, ts_start_ns, ts_end_ns, dump_buf, NULL);

	mutex_unlock(&hctx->accum_lock);

	return errcode;
}
KBASE_EXPORT_TEST_API(kbase_hwcnt_accumulator_dump);

u64 kbase_hwcnt_accumulator_timestamp_ns(struct kbase_hwcnt_accumulator *accum)
{
	struct kbase_hwcnt_context *hctx;

	if (WARN_ON(!accum))
		return 0;

	hctx = container_of(accum, struct kbase_hwcnt_context, accum);
	return hctx->iface->timestamp_ns(accum->backend);
}
