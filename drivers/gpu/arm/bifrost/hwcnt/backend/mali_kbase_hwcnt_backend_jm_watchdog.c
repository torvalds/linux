// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2021-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

#include <mali_kbase.h>

#include <hwcnt/mali_kbase_hwcnt_gpu.h>
#include <hwcnt/mali_kbase_hwcnt_types.h>

#include <hwcnt/backend/mali_kbase_hwcnt_backend.h>
#include <hwcnt/backend/mali_kbase_hwcnt_backend_jm_watchdog.h>
#include <hwcnt/mali_kbase_hwcnt_watchdog_if.h>

#if IS_ENABLED(CONFIG_MALI_IS_FPGA) && !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
/* Backend watch dog timer interval in milliseconds: 18 seconds. */
static const u32 hwcnt_backend_watchdog_timer_interval_ms = 18000;
#else
/* Backend watch dog timer interval in milliseconds: 1 second. */
static const u32 hwcnt_backend_watchdog_timer_interval_ms = 1000;
#endif /* IS_FPGA && !NO_MALI */

/*
 * IDLE_BUFFER_EMPTY -> USER_DUMPING_BUFFER_EMPTY     on dump_request.
 * IDLE_BUFFER_EMPTY -> TIMER_DUMPING                 after
 *                                                    hwcnt_backend_watchdog_timer_interval_ms
 *                                                    milliseconds, if no dump_request has been
 *                                                    called in the meantime.
 * IDLE_BUFFER_FULL  -> USER_DUMPING_BUFFER_FULL      on dump_request.
 * IDLE_BUFFER_FULL  -> TIMER_DUMPING                 after
 *                                                    hwcnt_backend_watchdog_timer_interval_ms
 *                                                    milliseconds, if no dump_request has been
 *                                                    called in the meantime.
 * IDLE_BUFFER_FULL -> IDLE_BUFFER_EMPTY              on dump_disable, upon discarding undumped
 *                                                    counter values since the last dump_get.
 * IDLE_BUFFER_EMPTY -> BUFFER_CLEARING               on dump_clear, before calling job manager
 *                                                    backend dump_clear.
 * IDLE_BUFFER_FULL  -> BUFFER_CLEARING               on dump_clear, before calling job manager
 *                                                    backend dump_clear.
 * USER_DUMPING_BUFFER_EMPTY -> BUFFER_CLEARING       on dump_clear, before calling job manager
 *                                                    backend dump_clear.
 * USER_DUMPING_BUFFER_FULL  -> BUFFER_CLEARING       on dump_clear, before calling job manager
 *                                                    backend dump_clear.
 * BUFFER_CLEARING -> IDLE_BUFFER_EMPTY               on dump_clear, upon job manager backend
 *                                                    dump_clear completion.
 * TIMER_DUMPING -> IDLE_BUFFER_FULL                  on timer's callback completion.
 * TIMER_DUMPING -> TIMER_DUMPING_USER_CLEAR          on dump_clear, notifies the callback thread
 *                                                    that there is no need for dumping the buffer
 *                                                    anymore, and that the client will proceed
 *                                                    clearing the buffer.
 * TIMER_DUMPING_USER_CLEAR -> IDLE_BUFFER_EMPTY      on timer's callback completion, when a user
 *                                                    requested a dump_clear.
 * TIMER_DUMPING -> TIMER_DUMPING_USER_REQUESTED      on dump_request, when a client performs a
 *                                                    dump request while the timer is dumping (the
 *                                                    timer will perform the dump and (once
 *                                                    completed) the client will retrieve the value
 *                                                    from the buffer).
 * TIMER_DUMPING_USER_REQUESTED -> IDLE_BUFFER_EMPTY  on dump_get, when a timer completed and the
 *                                                    user reads the periodic dump buffer.
 * Any -> ERROR                                       if the job manager backend returns an error
 *                                                    (of any kind).
 * USER_DUMPING_BUFFER_EMPTY -> IDLE_BUFFER_EMPTY     on dump_get (performs get, ignores the
 *                                                    periodic dump buffer and returns).
 * USER_DUMPING_BUFFER_FULL  -> IDLE_BUFFER_EMPTY     on dump_get (performs get, accumulates with
 *                                                    periodic dump buffer and returns).
 */

/** enum backend_watchdog_state State used to synchronize timer callbacks with the main thread.
 * @HWCNT_JM_WD_ERROR: Received an error from the job manager backend calls.
 * @HWCNT_JM_WD_IDLE_BUFFER_EMPTY: Initial state. Watchdog timer enabled, periodic dump buffer is
 *                                 empty.
 * @HWCNT_JM_WD_IDLE_BUFFER_FULL: Watchdog timer enabled, periodic dump buffer is full.
 * @HWCNT_JM_WD_BUFFER_CLEARING: The client is performing a dump clear. A concurrent timer callback
 *                               thread should just ignore and reschedule another callback in
 *                               hwcnt_backend_watchdog_timer_interval_ms milliseconds.
 * @HWCNT_JM_WD_TIMER_DUMPING: The timer ran out. The callback is performing a periodic dump.
 * @HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED: While the timer is performing a periodic dump, user
 *                                            requested a dump.
 * @HWCNT_JM_WD_TIMER_DUMPING_USER_CLEAR: While the timer is performing a dump, user requested a
 *                                        dump_clear. The timer has to complete the periodic dump
 *                                        and clear buffer (internal and job manager backend).
 * @HWCNT_JM_WD_USER_DUMPING_BUFFER_EMPTY: From IDLE state, user requested a dump. The periodic
 *                                         dump buffer is empty.
 * @HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL: From IDLE state, user requested a dump. The periodic dump
 *                                        buffer is full.
 *
 * While the state machine is in HWCNT_JM_WD_TIMER_DUMPING*, only the timer callback thread is
 * allowed to call the job manager backend layer.
 */
enum backend_watchdog_state {
	HWCNT_JM_WD_ERROR,
	HWCNT_JM_WD_IDLE_BUFFER_EMPTY,
	HWCNT_JM_WD_IDLE_BUFFER_FULL,
	HWCNT_JM_WD_BUFFER_CLEARING,
	HWCNT_JM_WD_TIMER_DUMPING,
	HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED,
	HWCNT_JM_WD_TIMER_DUMPING_USER_CLEAR,
	HWCNT_JM_WD_USER_DUMPING_BUFFER_EMPTY,
	HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL,
};

/** enum wd_init_state - State machine for initialization / termination of the backend resources
 */
enum wd_init_state {
	HWCNT_JM_WD_INIT_START,
	HWCNT_JM_WD_INIT_BACKEND = HWCNT_JM_WD_INIT_START,
	HWCNT_JM_WD_INIT_ENABLE_MAP,
	HWCNT_JM_WD_INIT_DUMP_BUFFER,
	HWCNT_JM_WD_INIT_END
};

/**
 * struct kbase_hwcnt_backend_jm_watchdog_info - Immutable information used to initialize an
 *                                               instance of the job manager watchdog backend.
 * @jm_backend_iface: Hardware counter backend interface. This module extends
 *                    this interface with a watchdog that performs regular
 *                    dumps. The new interface this module provides complies
 *                    with the old backend interface.
 * @dump_watchdog_iface: Dump watchdog interface, used to periodically dump the
 *                       hardware counter in case no reads are requested within
 *                       a certain time, used to avoid hardware counter's buffer
 *                       saturation.
 */
struct kbase_hwcnt_backend_jm_watchdog_info {
	struct kbase_hwcnt_backend_interface *jm_backend_iface;
	struct kbase_hwcnt_watchdog_interface *dump_watchdog_iface;
};

/**
 * struct kbase_hwcnt_backend_jm_watchdog - An instance of the job manager watchdog backend.
 * @info: Immutable information used to create the job manager watchdog backend.
 * @jm_backend: Job manager's backend internal state. To be passed as argument during parent calls.
 * @timeout_ms: Time period in milliseconds for hardware counters dumping.
 * @wd_dump_buffer: Used to store periodic dumps done by a timer callback function. Contents are
 *                  valid in state %HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED,
 *                  %HWCNT_JM_WD_IDLE_BUFFER_FULL or %HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL.
 * @wd_enable_map: Watchdog backend internal buffer mask, initialized during dump_enable copying
 *                 the enable_map passed as argument.
 * @wd_dump_timestamp: Holds the dumping timestamp for potential future client dump_request, filled
 *                     during watchdog timer dumps.
 * @watchdog_complete: Used for synchronization between watchdog dumper thread and client calls.
 * @locked: Members protected from concurrent access by different threads.
 * @locked.watchdog_lock: Lock used to access fields within this struct (that require mutual
 *                        exclusion).
 * @locked.is_enabled: If true then the wrapped job manager hardware counter backend and the
 *                     watchdog timer are both enabled. If false then both are disabled (or soon
 *                     will be). Races between enable and disable have undefined behavior.
 * @locked.state: State used to synchronize timer callbacks with the main thread.
 */
struct kbase_hwcnt_backend_jm_watchdog {
	const struct kbase_hwcnt_backend_jm_watchdog_info *info;
	struct kbase_hwcnt_backend *jm_backend;
	u32 timeout_ms;
	struct kbase_hwcnt_dump_buffer wd_dump_buffer;
	struct kbase_hwcnt_enable_map wd_enable_map;
	u64 wd_dump_timestamp;
	struct completion watchdog_complete;
	struct {
		spinlock_t watchdog_lock;
		bool is_enabled;
		enum backend_watchdog_state state;
	} locked;
};

/* timer's callback function */
static void kbasep_hwcnt_backend_jm_watchdog_timer_callback(void *backend)
{
	struct kbase_hwcnt_backend_jm_watchdog *wd_backend = backend;
	unsigned long flags;
	bool wd_accumulate;

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);

	if (!wd_backend->locked.is_enabled || wd_backend->locked.state == HWCNT_JM_WD_ERROR) {
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return;
	}

	if (!(wd_backend->locked.state == HWCNT_JM_WD_IDLE_BUFFER_EMPTY ||
	      wd_backend->locked.state == HWCNT_JM_WD_IDLE_BUFFER_FULL)) {
		/*resetting the timer. Calling modify on a disabled timer enables it.*/
		wd_backend->info->dump_watchdog_iface->modify(
			wd_backend->info->dump_watchdog_iface->timer, wd_backend->timeout_ms);
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return;
	}
	/*start performing the dump*/

	/* if there has been a previous timeout use accumulating dump_get()
	 * otherwise use non-accumulating to overwrite buffer
	 */
	wd_accumulate = (wd_backend->locked.state == HWCNT_JM_WD_IDLE_BUFFER_FULL);

	wd_backend->locked.state = HWCNT_JM_WD_TIMER_DUMPING;

	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	if (wd_backend->info->jm_backend_iface->dump_request(wd_backend->jm_backend,
							     &wd_backend->wd_dump_timestamp) ||
	    wd_backend->info->jm_backend_iface->dump_wait(wd_backend->jm_backend) ||
	    wd_backend->info->jm_backend_iface->dump_get(
		    wd_backend->jm_backend, &wd_backend->wd_dump_buffer, &wd_backend->wd_enable_map,
		    wd_accumulate)) {
		spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
		WARN_ON(wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING &&
			wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING_USER_CLEAR &&
			wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED);
		wd_backend->locked.state = HWCNT_JM_WD_ERROR;
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		/* Unblock user if it's waiting. */
		complete_all(&wd_backend->watchdog_complete);
		return;
	}

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
	WARN_ON(wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING &&
		wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING_USER_CLEAR &&
		wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED);

	if (wd_backend->locked.state == HWCNT_JM_WD_TIMER_DUMPING) {
		/* If there is no user request/clear, transit to HWCNT_JM_WD_IDLE_BUFFER_FULL
		 * to indicate timer dump is done and the buffer is full. If state changed to
		 * HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED or
		 * HWCNT_JM_WD_TIMER_DUMPING_USER_CLEAR then user will transit the state
		 * machine to next state.
		 */
		wd_backend->locked.state = HWCNT_JM_WD_IDLE_BUFFER_FULL;
	}
	if (wd_backend->locked.state != HWCNT_JM_WD_ERROR && wd_backend->locked.is_enabled) {
		/* reset the timer to schedule another callback. Calling modify on a
		 * disabled timer enables it.
		 */
		/*The spin lock needs to be held in case the client calls dump_enable*/
		wd_backend->info->dump_watchdog_iface->modify(
			wd_backend->info->dump_watchdog_iface->timer, wd_backend->timeout_ms);
	}
	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	/* Unblock user if it's waiting. */
	complete_all(&wd_backend->watchdog_complete);
}

/* helper methods, info structure creation and destruction*/

static struct kbase_hwcnt_backend_jm_watchdog_info *
kbasep_hwcnt_backend_jm_watchdog_info_create(struct kbase_hwcnt_backend_interface *backend_iface,
					     struct kbase_hwcnt_watchdog_interface *watchdog_iface)
{
	struct kbase_hwcnt_backend_jm_watchdog_info *const info =
		kmalloc(sizeof(*info), GFP_KERNEL);

	if (!info)
		return NULL;

	*info = (struct kbase_hwcnt_backend_jm_watchdog_info){ .jm_backend_iface = backend_iface,
							       .dump_watchdog_iface =
								       watchdog_iface };

	return info;
}

/****** kbase_hwcnt_backend_interface implementation *******/

/* Job manager watchdog backend, implementation of kbase_hwcnt_backend_metadata_fn */
static const struct kbase_hwcnt_metadata *
kbasep_hwcnt_backend_jm_watchdog_metadata(const struct kbase_hwcnt_backend_info *info)
{
	const struct kbase_hwcnt_backend_jm_watchdog_info *wd_info = (void *)info;

	if (WARN_ON(!info))
		return NULL;

	return wd_info->jm_backend_iface->metadata(wd_info->jm_backend_iface->info);
}

static void
kbasep_hwcnt_backend_jm_watchdog_term_partial(struct kbase_hwcnt_backend_jm_watchdog *wd_backend,
					      enum wd_init_state state)
{
	if (!wd_backend)
		return;

	WARN_ON(state > HWCNT_JM_WD_INIT_END);

	while (state-- > HWCNT_JM_WD_INIT_START) {
		switch (state) {
		case HWCNT_JM_WD_INIT_BACKEND:
			wd_backend->info->jm_backend_iface->term(wd_backend->jm_backend);
			break;
		case HWCNT_JM_WD_INIT_ENABLE_MAP:
			kbase_hwcnt_enable_map_free(&wd_backend->wd_enable_map);
			break;
		case HWCNT_JM_WD_INIT_DUMP_BUFFER:
			kbase_hwcnt_dump_buffer_free(&wd_backend->wd_dump_buffer);
			break;
		case HWCNT_JM_WD_INIT_END:
			break;
		}
	}

	kfree(wd_backend);
}

/* Job manager watchdog backend, implementation of kbase_hwcnt_backend_term_fn
 * Calling term does *not* destroy the interface
 */
static void kbasep_hwcnt_backend_jm_watchdog_term(struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_jm_watchdog *wd_backend =
		(struct kbase_hwcnt_backend_jm_watchdog *)backend;

	if (!backend)
		return;

	/* disable timer thread to avoid concurrent access to shared resources */
	wd_backend->info->dump_watchdog_iface->disable(
		wd_backend->info->dump_watchdog_iface->timer);

	kbasep_hwcnt_backend_jm_watchdog_term_partial(wd_backend, HWCNT_JM_WD_INIT_END);
}

/* Job manager watchdog backend, implementation of kbase_hwcnt_backend_init_fn */
static int kbasep_hwcnt_backend_jm_watchdog_init(const struct kbase_hwcnt_backend_info *info,
						 struct kbase_hwcnt_backend **out_backend)
{
	int errcode = 0;
	struct kbase_hwcnt_backend_jm_watchdog *wd_backend = NULL;
	struct kbase_hwcnt_backend_jm_watchdog_info *const wd_info = (void *)info;
	const struct kbase_hwcnt_backend_info *jm_info;
	const struct kbase_hwcnt_metadata *metadata;
	enum wd_init_state state = HWCNT_JM_WD_INIT_START;

	if (WARN_ON(!info) || WARN_ON(!out_backend))
		return -EINVAL;

	jm_info = wd_info->jm_backend_iface->info;
	metadata = wd_info->jm_backend_iface->metadata(wd_info->jm_backend_iface->info);

	wd_backend = kmalloc(sizeof(*wd_backend), GFP_KERNEL);
	if (!wd_backend) {
		*out_backend = NULL;
		return -ENOMEM;
	}

	*wd_backend = (struct kbase_hwcnt_backend_jm_watchdog){
		.info = wd_info,
		.timeout_ms = hwcnt_backend_watchdog_timer_interval_ms,
		.locked = { .state = HWCNT_JM_WD_IDLE_BUFFER_EMPTY, .is_enabled = false }
	};

	while (state < HWCNT_JM_WD_INIT_END && !errcode) {
		switch (state) {
		case HWCNT_JM_WD_INIT_BACKEND:
			errcode = wd_info->jm_backend_iface->init(jm_info, &wd_backend->jm_backend);
			break;
		case HWCNT_JM_WD_INIT_ENABLE_MAP:
			errcode =
				kbase_hwcnt_enable_map_alloc(metadata, &wd_backend->wd_enable_map);
			break;
		case HWCNT_JM_WD_INIT_DUMP_BUFFER:
			errcode = kbase_hwcnt_dump_buffer_alloc(metadata,
								&wd_backend->wd_dump_buffer);
			break;
		case HWCNT_JM_WD_INIT_END:
			break;
		}
		if (!errcode)
			state++;
	}

	if (errcode) {
		kbasep_hwcnt_backend_jm_watchdog_term_partial(wd_backend, state);
		*out_backend = NULL;
		return errcode;
	}

	WARN_ON(state != HWCNT_JM_WD_INIT_END);

	spin_lock_init(&wd_backend->locked.watchdog_lock);
	init_completion(&wd_backend->watchdog_complete);

	*out_backend = (struct kbase_hwcnt_backend *)wd_backend;
	return 0;
}

/* Job manager watchdog backend, implementation of timestamp_ns */
static u64 kbasep_hwcnt_backend_jm_watchdog_timestamp_ns(struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;

	return wd_backend->info->jm_backend_iface->timestamp_ns(wd_backend->jm_backend);
}

static int kbasep_hwcnt_backend_jm_watchdog_dump_enable_common(
	struct kbase_hwcnt_backend_jm_watchdog *wd_backend,
	const struct kbase_hwcnt_enable_map *enable_map, kbase_hwcnt_backend_dump_enable_fn enabler)
{
	int errcode = -EPERM;
	unsigned long flags;

	if (WARN_ON(!wd_backend) || WARN_ON(!enable_map))
		return -EINVAL;

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);

	/* If the backend is already enabled return an error */
	if (wd_backend->locked.is_enabled) {
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return -EPERM;
	}

	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	/*We copy the enable map into our watchdog backend copy, for future usage*/
	kbase_hwcnt_enable_map_copy(&wd_backend->wd_enable_map, enable_map);

	errcode = enabler(wd_backend->jm_backend, enable_map);
	if (!errcode) {
		/*Enable dump watchdog*/
		errcode = wd_backend->info->dump_watchdog_iface->enable(
			wd_backend->info->dump_watchdog_iface->timer, wd_backend->timeout_ms,
			kbasep_hwcnt_backend_jm_watchdog_timer_callback, wd_backend);
		if (!errcode) {
			spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
			WARN_ON(wd_backend->locked.is_enabled);
			wd_backend->locked.is_enabled = true;
			spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		} else
			/*Reverting the job manager backend back to disabled*/
			wd_backend->info->jm_backend_iface->dump_disable(wd_backend->jm_backend);
	}

	return errcode;
}

/* Job manager watchdog backend, implementation of dump_enable */
static int
kbasep_hwcnt_backend_jm_watchdog_dump_enable(struct kbase_hwcnt_backend *backend,
					     const struct kbase_hwcnt_enable_map *enable_map)
{
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;

	return kbasep_hwcnt_backend_jm_watchdog_dump_enable_common(
		wd_backend, enable_map, wd_backend->info->jm_backend_iface->dump_enable);
}

/* Job manager watchdog backend, implementation of dump_enable_nolock */
static int
kbasep_hwcnt_backend_jm_watchdog_dump_enable_nolock(struct kbase_hwcnt_backend *backend,
						    const struct kbase_hwcnt_enable_map *enable_map)
{
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;

	return kbasep_hwcnt_backend_jm_watchdog_dump_enable_common(
		wd_backend, enable_map, wd_backend->info->jm_backend_iface->dump_enable_nolock);
}

/* Job manager watchdog backend, implementation of dump_disable */
static void kbasep_hwcnt_backend_jm_watchdog_dump_disable(struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;
	unsigned long flags;

	if (WARN_ON(!backend))
		return;

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
	if (!wd_backend->locked.is_enabled) {
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return;
	}

	wd_backend->locked.is_enabled = false;

	/* Discard undumped counter values since the last dump_get. */
	if (wd_backend->locked.state == HWCNT_JM_WD_IDLE_BUFFER_FULL)
		wd_backend->locked.state = HWCNT_JM_WD_IDLE_BUFFER_EMPTY;

	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	wd_backend->info->dump_watchdog_iface->disable(
		wd_backend->info->dump_watchdog_iface->timer);

	wd_backend->info->jm_backend_iface->dump_disable(wd_backend->jm_backend);
}

/* Job manager watchdog backend, implementation of dump_clear */
static int kbasep_hwcnt_backend_jm_watchdog_dump_clear(struct kbase_hwcnt_backend *backend)
{
	int errcode = -EPERM;
	bool clear_wd_wait_completion = false;
	unsigned long flags;
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;

	if (WARN_ON(!backend))
		return -EINVAL;

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
	if (!wd_backend->locked.is_enabled) {
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return -EPERM;
	}

	switch (wd_backend->locked.state) {
	case HWCNT_JM_WD_IDLE_BUFFER_FULL:
	case HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL:
	case HWCNT_JM_WD_IDLE_BUFFER_EMPTY:
	case HWCNT_JM_WD_USER_DUMPING_BUFFER_EMPTY:
		wd_backend->locked.state = HWCNT_JM_WD_BUFFER_CLEARING;
		errcode = 0;
		break;
	case HWCNT_JM_WD_TIMER_DUMPING:
		/* The timer asked for a dump request, when complete, the job manager backend
		 * buffer will be zero
		 */
		clear_wd_wait_completion = true;
		/* This thread will have to wait for the callback to terminate and then call a
		 * dump_clear on the job manager backend. We change the state to
		 * HWCNT_JM_WD_TIMER_DUMPING_USER_CLEAR to notify the callback thread there is
		 * no more need to dump the buffer (since we will clear it right after anyway).
		 * We set up a wait queue to synchronize with the callback.
		 */
		reinit_completion(&wd_backend->watchdog_complete);
		wd_backend->locked.state = HWCNT_JM_WD_TIMER_DUMPING_USER_CLEAR;
		errcode = 0;
		break;
	default:
		errcode = -EPERM;
		break;
	}
	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	if (!errcode) {
		if (clear_wd_wait_completion) {
			/* Waiting for the callback to finish */
			wait_for_completion(&wd_backend->watchdog_complete);
		}

		/* Clearing job manager backend buffer */
		errcode = wd_backend->info->jm_backend_iface->dump_clear(wd_backend->jm_backend);

		spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);

		WARN_ON(wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING_USER_CLEAR &&
			wd_backend->locked.state != HWCNT_JM_WD_BUFFER_CLEARING &&
			wd_backend->locked.state != HWCNT_JM_WD_ERROR);

		WARN_ON(!wd_backend->locked.is_enabled);

		if (!errcode && wd_backend->locked.state != HWCNT_JM_WD_ERROR) {
			/* Setting the internal buffer state to EMPTY */
			wd_backend->locked.state = HWCNT_JM_WD_IDLE_BUFFER_EMPTY;
			/* Resetting the timer. Calling modify on a disabled timer
			 * enables it.
			 */
			wd_backend->info->dump_watchdog_iface->modify(
				wd_backend->info->dump_watchdog_iface->timer,
				wd_backend->timeout_ms);
		} else {
			wd_backend->locked.state = HWCNT_JM_WD_ERROR;
			errcode = -EPERM;
		}

		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
	}

	return errcode;
}

/* Job manager watchdog backend, implementation of dump_request */
static int kbasep_hwcnt_backend_jm_watchdog_dump_request(struct kbase_hwcnt_backend *backend,
							 u64 *dump_time_ns)
{
	bool call_dump_request = false;
	int errcode = 0;
	unsigned long flags;
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;

	if (WARN_ON(!backend) || WARN_ON(!dump_time_ns))
		return -EINVAL;

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);

	if (!wd_backend->locked.is_enabled) {
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return -EPERM;
	}

	switch (wd_backend->locked.state) {
	case HWCNT_JM_WD_IDLE_BUFFER_EMPTY:
		/* progressing the state to avoid callbacks running while calling the job manager
		 * backend
		 */
		wd_backend->locked.state = HWCNT_JM_WD_USER_DUMPING_BUFFER_EMPTY;
		call_dump_request = true;
		break;
	case HWCNT_JM_WD_IDLE_BUFFER_FULL:
		wd_backend->locked.state = HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL;
		call_dump_request = true;
		break;
	case HWCNT_JM_WD_TIMER_DUMPING:
		/* Retrieve timing information from previous dump_request */
		*dump_time_ns = wd_backend->wd_dump_timestamp;
		/* On the next client call (dump_wait) the thread will have to wait for the
		 * callback to finish the dumping.
		 * We set up a wait queue to synchronize with the callback.
		 */
		reinit_completion(&wd_backend->watchdog_complete);
		wd_backend->locked.state = HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED;
		break;
	default:
		errcode = -EPERM;
		break;
	}
	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	if (call_dump_request) {
		errcode = wd_backend->info->jm_backend_iface->dump_request(wd_backend->jm_backend,
									   dump_time_ns);
		if (!errcode) {
			/*resetting the timer. Calling modify on a disabled timer enables it*/
			wd_backend->info->dump_watchdog_iface->modify(
				wd_backend->info->dump_watchdog_iface->timer,
				wd_backend->timeout_ms);
		} else {
			spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
			WARN_ON(!wd_backend->locked.is_enabled);
			wd_backend->locked.state = HWCNT_JM_WD_ERROR;
			spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		}
	}

	return errcode;
}

/* Job manager watchdog backend, implementation of dump_wait */
static int kbasep_hwcnt_backend_jm_watchdog_dump_wait(struct kbase_hwcnt_backend *backend)
{
	int errcode = -EPERM;
	bool wait_for_auto_dump = false, wait_for_user_dump = false;
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;
	unsigned long flags;

	if (WARN_ON(!backend))
		return -EINVAL;

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
	if (!wd_backend->locked.is_enabled) {
		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		return -EPERM;
	}

	switch (wd_backend->locked.state) {
	case HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED:
		wait_for_auto_dump = true;
		errcode = 0;
		break;
	case HWCNT_JM_WD_USER_DUMPING_BUFFER_EMPTY:
	case HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL:
		wait_for_user_dump = true;
		errcode = 0;
		break;
	default:
		errcode = -EPERM;
		break;
	}
	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	if (wait_for_auto_dump)
		wait_for_completion(&wd_backend->watchdog_complete);
	else if (wait_for_user_dump) {
		errcode = wd_backend->info->jm_backend_iface->dump_wait(wd_backend->jm_backend);
		if (errcode) {
			spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);
			WARN_ON(!wd_backend->locked.is_enabled);
			wd_backend->locked.state = HWCNT_JM_WD_ERROR;
			spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
		}
	}

	return errcode;
}

/* Job manager watchdog backend, implementation of dump_get */
static int kbasep_hwcnt_backend_jm_watchdog_dump_get(
	struct kbase_hwcnt_backend *backend, struct kbase_hwcnt_dump_buffer *dump_buffer,
	const struct kbase_hwcnt_enable_map *enable_map, bool accumulate)
{
	bool call_dump_get = false;
	struct kbase_hwcnt_backend_jm_watchdog *const wd_backend = (void *)backend;
	unsigned long flags;
	int errcode = 0;

	if (WARN_ON(!backend) || WARN_ON(!dump_buffer) || WARN_ON(!enable_map))
		return -EINVAL;

	/* The resultant contents of the dump buffer are only well defined if a prior
	 * call to dump_wait returned successfully, and a new dump has not yet been
	 * requested by a call to dump_request.
	 */

	spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);

	switch (wd_backend->locked.state) {
	case HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED:
		/*we assume dump_wait has been called and completed successfully*/
		if (accumulate)
			kbase_hwcnt_dump_buffer_accumulate(dump_buffer, &wd_backend->wd_dump_buffer,
							   enable_map);
		else
			kbase_hwcnt_dump_buffer_copy(dump_buffer, &wd_backend->wd_dump_buffer,
						     enable_map);

		/*use state to indicate the the buffer is now empty*/
		wd_backend->locked.state = HWCNT_JM_WD_IDLE_BUFFER_EMPTY;
		break;
	case HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL:
		/*accumulate or copy watchdog data to user buffer first so that dump_get can set
		 * the header correctly
		 */
		if (accumulate)
			kbase_hwcnt_dump_buffer_accumulate(dump_buffer, &wd_backend->wd_dump_buffer,
							   enable_map);
		else
			kbase_hwcnt_dump_buffer_copy(dump_buffer, &wd_backend->wd_dump_buffer,
						     enable_map);

		/*accumulate backend data into user buffer on top of watchdog data*/
		accumulate = true;
		call_dump_get = true;
		break;
	case HWCNT_JM_WD_USER_DUMPING_BUFFER_EMPTY:
		call_dump_get = true;
		break;
	default:
		errcode = -EPERM;
		break;
	}

	spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);

	if (call_dump_get && !errcode) {
		/*we just dump the job manager backend into the user buffer, following
		 *accumulate flag
		 */
		errcode = wd_backend->info->jm_backend_iface->dump_get(
			wd_backend->jm_backend, dump_buffer, enable_map, accumulate);

		spin_lock_irqsave(&wd_backend->locked.watchdog_lock, flags);

		WARN_ON(wd_backend->locked.state != HWCNT_JM_WD_USER_DUMPING_BUFFER_EMPTY &&
			wd_backend->locked.state != HWCNT_JM_WD_USER_DUMPING_BUFFER_FULL &&
			wd_backend->locked.state != HWCNT_JM_WD_TIMER_DUMPING_USER_REQUESTED);

		if (!errcode)
			wd_backend->locked.state = HWCNT_JM_WD_IDLE_BUFFER_EMPTY;
		else
			wd_backend->locked.state = HWCNT_JM_WD_ERROR;

		spin_unlock_irqrestore(&wd_backend->locked.watchdog_lock, flags);
	}

	return errcode;
}

/* exposed methods */

int kbase_hwcnt_backend_jm_watchdog_create(struct kbase_hwcnt_backend_interface *backend_iface,
					   struct kbase_hwcnt_watchdog_interface *watchdog_iface,
					   struct kbase_hwcnt_backend_interface *out_iface)
{
	struct kbase_hwcnt_backend_jm_watchdog_info *info = NULL;

	if (WARN_ON(!backend_iface) || WARN_ON(!watchdog_iface) || WARN_ON(!out_iface))
		return -EINVAL;

	info = kbasep_hwcnt_backend_jm_watchdog_info_create(backend_iface, watchdog_iface);
	if (!info)
		return -ENOMEM;

	/*linking the info table with the output iface, to allow the callbacks below to access the
	 *info object later on
	 */
	*out_iface = (struct kbase_hwcnt_backend_interface){
		.info = (void *)info,
		.metadata = kbasep_hwcnt_backend_jm_watchdog_metadata,
		.init = kbasep_hwcnt_backend_jm_watchdog_init,
		.term = kbasep_hwcnt_backend_jm_watchdog_term,
		.timestamp_ns = kbasep_hwcnt_backend_jm_watchdog_timestamp_ns,
		.dump_enable = kbasep_hwcnt_backend_jm_watchdog_dump_enable,
		.dump_enable_nolock = kbasep_hwcnt_backend_jm_watchdog_dump_enable_nolock,
		.dump_disable = kbasep_hwcnt_backend_jm_watchdog_dump_disable,
		.dump_clear = kbasep_hwcnt_backend_jm_watchdog_dump_clear,
		.dump_request = kbasep_hwcnt_backend_jm_watchdog_dump_request,
		.dump_wait = kbasep_hwcnt_backend_jm_watchdog_dump_wait,
		.dump_get = kbasep_hwcnt_backend_jm_watchdog_dump_get
	};

	/*registering watchdog backend module methods on the output interface*/

	return 0;
}

void kbase_hwcnt_backend_jm_watchdog_destroy(struct kbase_hwcnt_backend_interface *iface)
{
	if (!iface || !iface->info)
		return;

	kfree((struct kbase_hwcnt_backend_jm_watchdog_info *)iface->info);

	/*blanking the watchdog backend interface*/
	memset(iface, 0, sizeof(*iface));
}
