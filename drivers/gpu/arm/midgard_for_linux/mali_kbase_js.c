/*
 *
 * (C) COPYRIGHT 2011-2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */

/* #define ENABLE_DEBUG_LOG */
#include "./platform/rk/custom_log.h"

/*
 * Job Scheduler Implementation
 */
#include <mali_kbase.h>
#include <mali_kbase_js.h>
#if defined(CONFIG_MALI_GATOR_SUPPORT)
#include <mali_kbase_gator.h>
#endif
#include <mali_kbase_tlstream.h>
#include <mali_kbase_hw.h>

#include <mali_kbase_defs.h>
#include <mali_kbase_config_defaults.h>

#include "mali_kbase_jm.h"
#include "mali_kbase_hwaccess_jm.h"

/*
 * Private types
 */

/* Bitpattern indicating the result of releasing a context */
enum {
	/* The context was descheduled - caller should try scheduling in a new
	 * one to keep the runpool full */
	KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED = (1u << 0),
	/* Ctx attributes were changed - caller should try scheduling all
	 * contexts */
	KBASEP_JS_RELEASE_RESULT_SCHED_ALL = (1u << 1)
};

typedef u32 kbasep_js_release_result;

const int kbasep_js_atom_priority_to_relative[BASE_JD_NR_PRIO_LEVELS] = {
	KBASE_JS_ATOM_SCHED_PRIO_MED, /* BASE_JD_PRIO_MEDIUM */
	KBASE_JS_ATOM_SCHED_PRIO_HIGH, /* BASE_JD_PRIO_HIGH */
	KBASE_JS_ATOM_SCHED_PRIO_LOW  /* BASE_JD_PRIO_LOW */
};

const base_jd_prio
kbasep_js_relative_priority_to_atom[KBASE_JS_ATOM_SCHED_PRIO_COUNT] = {
	BASE_JD_PRIO_HIGH,   /* KBASE_JS_ATOM_SCHED_PRIO_HIGH */
	BASE_JD_PRIO_MEDIUM, /* KBASE_JS_ATOM_SCHED_PRIO_MED */
	BASE_JD_PRIO_LOW     /* KBASE_JS_ATOM_SCHED_PRIO_LOW */
};


/*
 * Private function prototypes
 */
static kbasep_js_release_result kbasep_js_runpool_release_ctx_internal(
		struct kbase_device *kbdev, struct kbase_context *kctx,
		struct kbasep_js_atom_retained_state *katom_retained_state);

static int kbase_js_get_slot(struct kbase_device *kbdev,
				struct kbase_jd_atom *katom);

static void kbase_js_foreach_ctx_job(struct kbase_context *kctx,
		kbasep_js_policy_ctx_job_cb callback);

/* Helper for trace subcodes */
#if KBASE_TRACE_ENABLE
static int kbasep_js_trace_get_refcnt(struct kbase_device *kbdev,
		struct kbase_context *kctx)
{
	unsigned long flags;
	struct kbasep_js_device_data *js_devdata;
	int as_nr;
	int refcnt = 0;

	js_devdata = &kbdev->js_data;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	as_nr = kctx->as_nr;
	if (as_nr != KBASEP_AS_NR_INVALID) {
		struct kbasep_js_per_as_data *js_per_as_data;

		js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

		refcnt = js_per_as_data->as_busy_refcount;
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return refcnt;
}

static int kbasep_js_trace_get_refcnt_nolock(struct kbase_device *kbdev,
						struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	int as_nr;
	int refcnt = 0;

	js_devdata = &kbdev->js_data;

	as_nr = kctx->as_nr;
	if (as_nr != KBASEP_AS_NR_INVALID) {
		struct kbasep_js_per_as_data *js_per_as_data;

		js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

		refcnt = js_per_as_data->as_busy_refcount;
	}

	return refcnt;
}
#else				/* KBASE_TRACE_ENABLE  */
static int kbasep_js_trace_get_refcnt(struct kbase_device *kbdev,
		struct kbase_context *kctx)
{
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(kctx);
	return 0;
}
static int kbasep_js_trace_get_refcnt_nolock(struct kbase_device *kbdev,
						struct kbase_context *kctx)
{
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(kctx);
	return 0;
}
#endif				/* KBASE_TRACE_ENABLE  */

/*
 * Private types
 */
enum {
	JS_DEVDATA_INIT_NONE = 0,
	JS_DEVDATA_INIT_CONSTANTS = (1 << 0),
	JS_DEVDATA_INIT_POLICY = (1 << 1),
	JS_DEVDATA_INIT_ALL = ((1 << 2) - 1)
};

enum {
	JS_KCTX_INIT_NONE = 0,
	JS_KCTX_INIT_CONSTANTS = (1 << 0),
	JS_KCTX_INIT_POLICY = (1 << 1),
	JS_KCTX_INIT_ALL = ((1 << 2) - 1)
};

/*
 * Private functions
 */

/**
 * core_reqs_from_jsn_features - Convert JSn_FEATURES to core requirements
 * @features: JSn_FEATURE register value
 *
 * Given a JSn_FEATURE register value returns the core requirements that match
 *
 * Return: Core requirement bit mask
 */
static base_jd_core_req core_reqs_from_jsn_features(u16 features)
{
	base_jd_core_req core_req = 0u;

	if ((features & JS_FEATURE_SET_VALUE_JOB) != 0)
		core_req |= BASE_JD_REQ_V;

	if ((features & JS_FEATURE_CACHE_FLUSH_JOB) != 0)
		core_req |= BASE_JD_REQ_CF;

	if ((features & JS_FEATURE_COMPUTE_JOB) != 0)
		core_req |= BASE_JD_REQ_CS;

	if ((features & JS_FEATURE_TILER_JOB) != 0)
		core_req |= BASE_JD_REQ_T;

	if ((features & JS_FEATURE_FRAGMENT_JOB) != 0)
		core_req |= BASE_JD_REQ_FS;

	return core_req;
}

static void kbase_js_sync_timers(struct kbase_device *kbdev)
{
	mutex_lock(&kbdev->js_data.runpool_mutex);
	kbase_backend_ctx_count_changed(kbdev);
	mutex_unlock(&kbdev->js_data.runpool_mutex);
}

/* Hold the hwaccess_lock for this */
bool kbasep_js_runpool_retain_ctx_nolock(struct kbase_device *kbdev,
		struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_per_as_data *js_per_as_data;
	bool result = false;
	int as_nr;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_devdata = &kbdev->js_data;

	as_nr = kctx->as_nr;
	if (as_nr != KBASEP_AS_NR_INVALID) {
		int new_refcnt;

		KBASE_DEBUG_ASSERT(as_nr >= 0);
		js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

		KBASE_DEBUG_ASSERT(js_per_as_data->kctx != NULL);

		new_refcnt = ++(js_per_as_data->as_busy_refcount);

		KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_RETAIN_CTX_NOLOCK, kctx,
				NULL, 0u, new_refcnt);
		result = true;
	}

	return result;
}

/**
 * jsctx_rb_none_to_pull_prio(): - Check if there are no pullable atoms
 * @kctx: Pointer to kbase context with ring buffer.
 * @js:   Job slot id to check.
 * @prio: Priority to check.
 *
 * Return true if there are no atoms to pull. There may be running atoms in the
 * ring buffer even if there are no atoms to pull. It is also possible for the
 * ring buffer to be full (with running atoms) when this functions returns
 * true.
 *
 * Return: true if there are no atoms to pull, false otherwise.
 */
static inline bool
jsctx_rb_none_to_pull_prio(struct kbase_context *kctx, int js, int prio)
{
	struct jsctx_queue *rb = &kctx->jsctx_queue[prio][js];

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	return RB_EMPTY_ROOT(&rb->runnable_tree);
}

/**
 * jsctx_rb_none_to_pull(): - Check if all priority ring buffers have no
 * pullable atoms
 * @kctx: Pointer to kbase context with ring buffer.
 * @js:   Job slot id to check.
 *
 * Caller must hold hwaccess_lock
 *
 * Return: true if the ring buffers for all priorities have no pullable atoms,
 *	   false otherwise.
 */
static inline bool
jsctx_rb_none_to_pull(struct kbase_context *kctx, int js)
{
	int prio;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	for (prio = 0; prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT; prio++) {
		if (!jsctx_rb_none_to_pull_prio(kctx, js, prio))
			return false;
	}

	return true;
}

/**
 * jsctx_queue_foreach_prio(): - Execute callback for each entry in the queue.
 * @kctx:     Pointer to kbase context with the queue.
 * @js:       Job slot id to iterate.
 * @prio:     Priority id to iterate.
 * @callback: Function pointer to callback.
 *
 * Iterate over a queue and invoke @callback for each entry in the queue, and
 * remove the entry from the queue.
 *
 * If entries are added to the queue while this is running those entries may, or
 * may not be covered. To ensure that all entries in the buffer have been
 * enumerated when this function returns jsctx->lock must be held when calling
 * this function.
 *
 * The HW access lock must always be held when calling this function.
 */
static void
jsctx_queue_foreach_prio(struct kbase_context *kctx, int js, int prio,
		kbasep_js_policy_ctx_job_cb callback)
{
	struct jsctx_queue *queue = &kctx->jsctx_queue[prio][js];

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	while (!RB_EMPTY_ROOT(&queue->runnable_tree)) {
		struct rb_node *node = rb_first(&queue->runnable_tree);
		struct kbase_jd_atom *entry = rb_entry(node,
				struct kbase_jd_atom, runnable_tree_node);

		rb_erase(node, &queue->runnable_tree);
		callback(kctx->kbdev, entry);
	}

	while (!list_empty(&queue->x_dep_head)) {
		struct kbase_jd_atom *entry = list_entry(queue->x_dep_head.next,
				struct kbase_jd_atom, queue);

		list_del(queue->x_dep_head.next);

		callback(kctx->kbdev, entry);
	}
}

/**
 * jsctx_queue_foreach(): - Execute callback for each entry in every queue
 * @kctx:     Pointer to kbase context with queue.
 * @js:       Job slot id to iterate.
 * @callback: Function pointer to callback.
 *
 * Iterate over all the different priorities, and for each call
 * jsctx_queue_foreach_prio() to iterate over the queue and invoke @callback
 * for each entry, and remove the entry from the queue.
 */
static inline void
jsctx_queue_foreach(struct kbase_context *kctx, int js,
		kbasep_js_policy_ctx_job_cb callback)
{
	int prio;

	for (prio = 0; prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT; prio++)
		jsctx_queue_foreach_prio(kctx, js, prio, callback);
}

/**
 * jsctx_rb_peek_prio(): - Check buffer and get next atom
 * @kctx: Pointer to kbase context with ring buffer.
 * @js:   Job slot id to check.
 * @prio: Priority id to check.
 *
 * Check the ring buffer for the specified @js and @prio and return a pointer to
 * the next atom, unless the ring buffer is empty.
 *
 * Return: Pointer to next atom in buffer, or NULL if there is no atom.
 */
static inline struct kbase_jd_atom *
jsctx_rb_peek_prio(struct kbase_context *kctx, int js, int prio)
{
	struct jsctx_queue *rb = &kctx->jsctx_queue[prio][js];
	struct rb_node *node;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	node = rb_first(&rb->runnable_tree);
	if (!node)
		return NULL;

	return rb_entry(node, struct kbase_jd_atom, runnable_tree_node);
}

/**
 * jsctx_rb_peek(): - Check all priority buffers and get next atom
 * @kctx: Pointer to kbase context with ring buffer.
 * @js:   Job slot id to check.
 *
 * Check the ring buffers for all priorities, starting from
 * KBASE_JS_ATOM_SCHED_PRIO_HIGH, for the specified @js and @prio and return a
 * pointer to the next atom, unless all the priority's ring buffers are empty.
 *
 * Caller must hold the hwaccess_lock.
 *
 * Return: Pointer to next atom in buffer, or NULL if there is no atom.
 */
static inline struct kbase_jd_atom *
jsctx_rb_peek(struct kbase_context *kctx, int js)
{
	int prio;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	for (prio = 0; prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT; prio++) {
		struct kbase_jd_atom *katom;

		katom = jsctx_rb_peek_prio(kctx, js, prio);
		if (katom)
			return katom;
	}

	return NULL;
}

/**
 * jsctx_rb_pull(): - Mark atom in list as running
 * @kctx:  Pointer to kbase context with ring buffer.
 * @katom: Pointer to katom to pull.
 *
 * Mark an atom previously obtained from jsctx_rb_peek() as running.
 *
 * @katom must currently be at the head of the ring buffer.
 */
static inline void
jsctx_rb_pull(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	int prio = katom->sched_priority;
	int js = katom->slot_nr;
	struct jsctx_queue *rb = &kctx->jsctx_queue[prio][js];

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	/* Atoms must be pulled in the correct order. */
	WARN_ON(katom != jsctx_rb_peek_prio(kctx, js, prio));

	rb_erase(&katom->runnable_tree_node, &rb->runnable_tree);
}

#define LESS_THAN_WRAP(a, b) ((s32)(a - b) < 0)

static void
jsctx_tree_add(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	int prio = katom->sched_priority;
	int js = katom->slot_nr;
	struct jsctx_queue *queue = &kctx->jsctx_queue[prio][js];
	struct rb_node **new = &(queue->runnable_tree.rb_node), *parent = NULL;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	while (*new) {
		struct kbase_jd_atom *entry = container_of(*new,
				struct kbase_jd_atom, runnable_tree_node);

		parent = *new;
		if (LESS_THAN_WRAP(katom->age, entry->age))
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&katom->runnable_tree_node, parent, new);
	rb_insert_color(&katom->runnable_tree_node, &queue->runnable_tree);
}

/**
 * jsctx_rb_unpull(): - Undo marking of atom in list as running
 * @kctx:  Pointer to kbase context with ring buffer.
 * @katom: Pointer to katom to unpull.
 *
 * Undo jsctx_rb_pull() and put @katom back in the queue.
 *
 * jsctx_rb_unpull() must be called on atoms in the same order the atoms were
 * pulled.
 */
static inline void
jsctx_rb_unpull(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	jsctx_tree_add(kctx, katom);
}

static bool kbase_js_ctx_pullable(struct kbase_context *kctx,
					int js,
					bool is_scheduled);
static bool kbase_js_ctx_list_add_pullable_nolock(struct kbase_device *kbdev,
						struct kbase_context *kctx,
						int js);
static bool kbase_js_ctx_list_add_unpullable_nolock(struct kbase_device *kbdev,
						struct kbase_context *kctx,
						int js);

/*
 * Functions private to KBase ('Protected' functions)
 */
int kbasep_js_devdata_init(struct kbase_device * const kbdev)
{
	struct kbasep_js_device_data *jsdd;
	int err;
	int i;
	u16 as_present;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	jsdd = &kbdev->js_data;

	KBASE_DEBUG_ASSERT(jsdd->init_status == JS_DEVDATA_INIT_NONE);

	/* These two must be recalculated if nr_hw_address_spaces changes
	 * (e.g. for HW workarounds) */
	as_present = (1U << kbdev->nr_hw_address_spaces) - 1;
	kbdev->nr_user_address_spaces = kbdev->nr_hw_address_spaces;
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8987)) {
		bool use_workaround;

		use_workaround = DEFAULT_SECURE_BUT_LOSS_OF_PERFORMANCE;
		if (use_workaround) {
			dev_dbg(kbdev->dev, "GPU has HW ISSUE 8987, and driver configured for security workaround: 1 address space only");
			kbdev->nr_user_address_spaces = 1;
		}
	}
#ifdef CONFIG_MALI_DEBUG
	/* Soft-stop will be disabled on a single context by default unless
	 * softstop_always is set */
	jsdd->softstop_always = false;
#endif				/* CONFIG_MALI_DEBUG */
	jsdd->nr_all_contexts_running = 0;
	jsdd->nr_user_contexts_running = 0;
	jsdd->nr_contexts_pullable = 0;
	atomic_set(&jsdd->nr_contexts_runnable, 0);
	/* All ASs initially free */
	jsdd->as_free = as_present;
	/* No ctx allowed to submit */
	jsdd->runpool_irq.submit_allowed = 0u;
	memset(jsdd->runpool_irq.ctx_attr_ref_count, 0,
			sizeof(jsdd->runpool_irq.ctx_attr_ref_count));
	memset(jsdd->runpool_irq.slot_affinities, 0,
			sizeof(jsdd->runpool_irq.slot_affinities));
	memset(jsdd->runpool_irq.slot_affinity_refcount, 0,
			sizeof(jsdd->runpool_irq.slot_affinity_refcount));
	INIT_LIST_HEAD(&jsdd->suspended_soft_jobs_list);

	/* Config attributes */
	jsdd->scheduling_period_ns = DEFAULT_JS_SCHEDULING_PERIOD_NS;
	jsdd->soft_stop_ticks = DEFAULT_JS_SOFT_STOP_TICKS;
	jsdd->soft_stop_ticks_cl = DEFAULT_JS_SOFT_STOP_TICKS_CL;
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8408))
		jsdd->hard_stop_ticks_ss = DEFAULT_JS_HARD_STOP_TICKS_SS_8408;
	else
		jsdd->hard_stop_ticks_ss = DEFAULT_JS_HARD_STOP_TICKS_SS;
	jsdd->hard_stop_ticks_cl = DEFAULT_JS_HARD_STOP_TICKS_CL;
	jsdd->hard_stop_ticks_dumping = DEFAULT_JS_HARD_STOP_TICKS_DUMPING;
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8408))
		jsdd->gpu_reset_ticks_ss = DEFAULT_JS_RESET_TICKS_SS_8408;
	else
		jsdd->gpu_reset_ticks_ss = DEFAULT_JS_RESET_TICKS_SS;
	jsdd->gpu_reset_ticks_cl = DEFAULT_JS_RESET_TICKS_CL;
	jsdd->gpu_reset_ticks_dumping = DEFAULT_JS_RESET_TICKS_DUMPING;
	jsdd->ctx_timeslice_ns = DEFAULT_JS_CTX_TIMESLICE_NS;
	jsdd->cfs_ctx_runtime_init_slices =
		DEFAULT_JS_CFS_CTX_RUNTIME_INIT_SLICES;
	jsdd->cfs_ctx_runtime_min_slices =
		DEFAULT_JS_CFS_CTX_RUNTIME_MIN_SLICES;
	atomic_set(&jsdd->soft_job_timeout_ms, DEFAULT_JS_SOFT_JOB_TIMEOUT);

	dev_dbg(kbdev->dev, "JS Config Attribs: ");
	dev_dbg(kbdev->dev, "\tscheduling_period_ns:%u",
			jsdd->scheduling_period_ns);
	dev_dbg(kbdev->dev, "\tsoft_stop_ticks:%u",
			jsdd->soft_stop_ticks);
	dev_dbg(kbdev->dev, "\tsoft_stop_ticks_cl:%u",
			jsdd->soft_stop_ticks_cl);
	dev_dbg(kbdev->dev, "\thard_stop_ticks_ss:%u",
			jsdd->hard_stop_ticks_ss);
	dev_dbg(kbdev->dev, "\thard_stop_ticks_cl:%u",
			jsdd->hard_stop_ticks_cl);
	dev_dbg(kbdev->dev, "\thard_stop_ticks_dumping:%u",
			jsdd->hard_stop_ticks_dumping);
	dev_dbg(kbdev->dev, "\tgpu_reset_ticks_ss:%u",
			jsdd->gpu_reset_ticks_ss);
	dev_dbg(kbdev->dev, "\tgpu_reset_ticks_cl:%u",
			jsdd->gpu_reset_ticks_cl);
	dev_dbg(kbdev->dev, "\tgpu_reset_ticks_dumping:%u",
			jsdd->gpu_reset_ticks_dumping);
	dev_dbg(kbdev->dev, "\tctx_timeslice_ns:%u",
			jsdd->ctx_timeslice_ns);
	dev_dbg(kbdev->dev, "\tcfs_ctx_runtime_init_slices:%u",
			jsdd->cfs_ctx_runtime_init_slices);
	dev_dbg(kbdev->dev, "\tcfs_ctx_runtime_min_slices:%u",
			jsdd->cfs_ctx_runtime_min_slices);
	dev_dbg(kbdev->dev, "\tsoft_job_timeout:%i",
		atomic_read(&jsdd->soft_job_timeout_ms));

	if (!(jsdd->soft_stop_ticks < jsdd->hard_stop_ticks_ss &&
			jsdd->hard_stop_ticks_ss < jsdd->gpu_reset_ticks_ss &&
			jsdd->soft_stop_ticks < jsdd->hard_stop_ticks_dumping &&
			jsdd->hard_stop_ticks_dumping <
			jsdd->gpu_reset_ticks_dumping)) {
		dev_err(kbdev->dev, "Job scheduler timeouts invalid; soft/hard/reset tick counts should be in increasing order\n");
		return -EINVAL;
	}

#if KBASE_DISABLE_SCHEDULING_SOFT_STOPS
	dev_dbg(kbdev->dev, "Job Scheduling Policy Soft-stops disabled, ignoring value for soft_stop_ticks==%u at %uns per tick. Other soft-stops may still occur.",
			jsdd->soft_stop_ticks,
			jsdd->scheduling_period_ns);
#endif
#if KBASE_DISABLE_SCHEDULING_HARD_STOPS
	dev_dbg(kbdev->dev, "Job Scheduling Policy Hard-stops disabled, ignoring values for hard_stop_ticks_ss==%d and hard_stop_ticks_dumping==%u at %uns per tick. Other hard-stops may still occur.",
			jsdd->hard_stop_ticks_ss,
			jsdd->hard_stop_ticks_dumping,
			jsdd->scheduling_period_ns);
#endif
#if KBASE_DISABLE_SCHEDULING_SOFT_STOPS && KBASE_DISABLE_SCHEDULING_HARD_STOPS
	dev_dbg(kbdev->dev, "Note: The JS policy's tick timer (if coded) will still be run, but do nothing.");
#endif

	/* setup the number of irq throttle cycles base on given time */
	{
		int time_us = kbdev->gpu_props.irq_throttle_time_us;
		int cycles = kbasep_js_convert_us_to_gpu_ticks_max_freq(kbdev,
				time_us);

		atomic_set(&kbdev->irq_throttle_cycles, cycles);
	}

	/* Clear the AS data, including setting NULL pointers */
	memset(&jsdd->runpool_irq.per_as_data[0], 0,
			sizeof(jsdd->runpool_irq.per_as_data));

	for (i = 0; i < kbdev->gpu_props.num_job_slots; ++i)
		jsdd->js_reqs[i] = core_reqs_from_jsn_features(
			kbdev->gpu_props.props.raw_props.js_features[i]);

	jsdd->init_status |= JS_DEVDATA_INIT_CONSTANTS;

	/* On error, we could continue on: providing none of the below resources
	 * rely on the ones above */

	mutex_init(&jsdd->runpool_mutex);
	mutex_init(&jsdd->queue_mutex);
	spin_lock_init(&kbdev->hwaccess_lock);
	sema_init(&jsdd->schedule_sem, 1);

	err = kbasep_js_policy_init(kbdev);
	if (!err)
		jsdd->init_status |= JS_DEVDATA_INIT_POLICY;

	for (i = 0; i < kbdev->gpu_props.num_job_slots; ++i) {
		INIT_LIST_HEAD(&jsdd->ctx_list_pullable[i]);
		INIT_LIST_HEAD(&jsdd->ctx_list_unpullable[i]);
	}

	/* On error, do no cleanup; this will be handled by the caller(s), since
	 * we've designed this resource to be safe to terminate on init-fail */
	if (jsdd->init_status != JS_DEVDATA_INIT_ALL)
		return -EINVAL;

	return 0;
}

void kbasep_js_devdata_halt(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbasep_js_devdata_term(struct kbase_device *kbdev)
{
	struct kbasep_js_device_data *js_devdata;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_devdata = &kbdev->js_data;

	if ((js_devdata->init_status & JS_DEVDATA_INIT_CONSTANTS)) {
		s8 zero_ctx_attr_ref_count[KBASEP_JS_CTX_ATTR_COUNT] = { 0, };
		/* The caller must de-register all contexts before calling this
		 */
		KBASE_DEBUG_ASSERT(js_devdata->nr_all_contexts_running == 0);
		KBASE_DEBUG_ASSERT(memcmp(
				js_devdata->runpool_irq.ctx_attr_ref_count,
				zero_ctx_attr_ref_count,
				sizeof(zero_ctx_attr_ref_count)) == 0);
		CSTD_UNUSED(zero_ctx_attr_ref_count);
	}
	if ((js_devdata->init_status & JS_DEVDATA_INIT_POLICY))
		kbasep_js_policy_term(&js_devdata->policy);

	js_devdata->init_status = JS_DEVDATA_INIT_NONE;
}

int kbasep_js_kctx_init(struct kbase_context * const kctx)
{
	struct kbase_device *kbdev;
	struct kbasep_js_kctx_info *js_kctx_info;
	int err;
	int i, j;

	KBASE_DEBUG_ASSERT(kctx != NULL);

	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	for (i = 0; i < BASE_JM_MAX_NR_SLOTS; ++i)
		INIT_LIST_HEAD(&kctx->jctx.sched_info.ctx.ctx_list_entry[i]);

	js_kctx_info = &kctx->jctx.sched_info;
	KBASE_DEBUG_ASSERT(js_kctx_info->init_status == JS_KCTX_INIT_NONE);

	js_kctx_info->ctx.nr_jobs = 0;
	kbase_ctx_flag_clear(kctx, KCTX_SCHEDULED);
	kbase_ctx_flag_clear(kctx, KCTX_DYING);
	memset(js_kctx_info->ctx.ctx_attr_ref_count, 0,
			sizeof(js_kctx_info->ctx.ctx_attr_ref_count));

	/* Initially, the context is disabled from submission until the create
	 * flags are set */
	kbase_ctx_flag_set(kctx, KCTX_SUBMIT_DISABLED);

	js_kctx_info->init_status |= JS_KCTX_INIT_CONSTANTS;

	/* On error, we could continue on: providing none of the below resources
	 * rely on the ones above */
	mutex_init(&js_kctx_info->ctx.jsctx_mutex);

	init_waitqueue_head(&js_kctx_info->ctx.is_scheduled_wait);

	err = kbasep_js_policy_init_ctx(kbdev, kctx);
	if (!err)
		js_kctx_info->init_status |= JS_KCTX_INIT_POLICY;

	/* On error, do no cleanup; this will be handled by the caller(s), since
	 * we've designed this resource to be safe to terminate on init-fail */
	if (js_kctx_info->init_status != JS_KCTX_INIT_ALL)
		return -EINVAL;

	for (i = 0; i < KBASE_JS_ATOM_SCHED_PRIO_COUNT; i++) {
		for (j = 0; j < BASE_JM_MAX_NR_SLOTS; j++) {
			INIT_LIST_HEAD(&kctx->jsctx_queue[i][j].x_dep_head);
			kctx->jsctx_queue[i][j].runnable_tree = RB_ROOT;
		}
	}

	return 0;
}

void kbasep_js_kctx_term(struct kbase_context *kctx)
{
	struct kbase_device *kbdev;
	struct kbasep_js_kctx_info *js_kctx_info;
	union kbasep_js_policy *js_policy;
	int js;
	bool update_ctx_count = false;

	KBASE_DEBUG_ASSERT(kctx != NULL);

	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_policy = &kbdev->js_data.policy;
	js_kctx_info = &kctx->jctx.sched_info;

	if ((js_kctx_info->init_status & JS_KCTX_INIT_CONSTANTS)) {
		/* The caller must de-register all jobs before calling this */
		KBASE_DEBUG_ASSERT(!kbase_ctx_flag(kctx, KCTX_SCHEDULED));
		KBASE_DEBUG_ASSERT(js_kctx_info->ctx.nr_jobs == 0);
	}

	mutex_lock(&kbdev->js_data.queue_mutex);
	mutex_lock(&kctx->jctx.sched_info.ctx.jsctx_mutex);

	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++)
		list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);

	if (kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF)) {
		WARN_ON(atomic_read(&kbdev->js_data.nr_contexts_runnable) <= 0);
		atomic_dec(&kbdev->js_data.nr_contexts_runnable);
		update_ctx_count = true;
		kbase_ctx_flag_clear(kctx, KCTX_RUNNABLE_REF);
	}

	mutex_unlock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	mutex_unlock(&kbdev->js_data.queue_mutex);

	if ((js_kctx_info->init_status & JS_KCTX_INIT_POLICY))
		kbasep_js_policy_term_ctx(js_policy, kctx);

	js_kctx_info->init_status = JS_KCTX_INIT_NONE;

	if (update_ctx_count) {
		mutex_lock(&kbdev->js_data.runpool_mutex);
		kbase_backend_ctx_count_changed(kbdev);
		mutex_unlock(&kbdev->js_data.runpool_mutex);
	}
}

/**
 * kbase_js_ctx_list_add_pullable_nolock - Variant of
 *                                         kbase_jd_ctx_list_add_pullable()
 *                                         where the caller must hold
 *                                         hwaccess_lock
 * @kbdev:  Device pointer
 * @kctx:   Context to add to queue
 * @js:     Job slot to use
 *
 * Caller must hold hwaccess_lock
 *
 * Return: true if caller should call kbase_backend_ctx_count_changed()
 */
static bool kbase_js_ctx_list_add_pullable_nolock(struct kbase_device *kbdev,
						struct kbase_context *kctx,
						int js)
{
	bool ret = false;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (!list_empty(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]))
		list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);

	list_add_tail(&kctx->jctx.sched_info.ctx.ctx_list_entry[js],
					&kbdev->js_data.ctx_list_pullable[js]);

	if (!kctx->slots_pullable) {
		kbdev->js_data.nr_contexts_pullable++;
		ret = true;
		if (!atomic_read(&kctx->atoms_pulled)) {
			WARN_ON(kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
			kbase_ctx_flag_set(kctx, KCTX_RUNNABLE_REF);
			atomic_inc(&kbdev->js_data.nr_contexts_runnable);
		}
	}
	kctx->slots_pullable |= (1 << js);

	return ret;
}

/**
 * kbase_js_ctx_list_add_pullable_head_nolock - Variant of
 *                                              kbase_js_ctx_list_add_pullable_head()
 *                                              where the caller must hold
 *                                              hwaccess_lock
 * @kbdev:  Device pointer
 * @kctx:   Context to add to queue
 * @js:     Job slot to use
 *
 * Caller must hold hwaccess_lock
 *
 * Return:  true if caller should call kbase_backend_ctx_count_changed()
 */
static bool kbase_js_ctx_list_add_pullable_head_nolock(
		struct kbase_device *kbdev, struct kbase_context *kctx, int js)
{
	bool ret = false;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (!list_empty(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]))
		list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);

	list_add(&kctx->jctx.sched_info.ctx.ctx_list_entry[js],
					&kbdev->js_data.ctx_list_pullable[js]);

	if (!kctx->slots_pullable) {
		kbdev->js_data.nr_contexts_pullable++;
		ret = true;
		if (!atomic_read(&kctx->atoms_pulled)) {
			WARN_ON(kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
			kbase_ctx_flag_set(kctx, KCTX_RUNNABLE_REF);
			atomic_inc(&kbdev->js_data.nr_contexts_runnable);
		}
	}
	kctx->slots_pullable |= (1 << js);

	return ret;
}

/**
 * kbase_js_ctx_list_add_pullable_head - Add context to the head of the
 *                                       per-slot pullable context queue
 * @kbdev:  Device pointer
 * @kctx:   Context to add to queue
 * @js:     Job slot to use
 *
 * If the context is on either the pullable or unpullable queues, then it is
 * removed before being added to the head.
 *
 * This function should be used when a context has been scheduled, but no jobs
 * can currently be pulled from it.
 *
 * Return:  true if caller should call kbase_backend_ctx_count_changed()
 */
static bool kbase_js_ctx_list_add_pullable_head(struct kbase_device *kbdev,
						struct kbase_context *kctx,
						int js)
{
	bool ret;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	ret = kbase_js_ctx_list_add_pullable_head_nolock(kbdev, kctx, js);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return ret;
}

/**
 * kbase_js_ctx_list_add_unpullable_nolock - Add context to the tail of the
 *                                           per-slot unpullable context queue
 * @kbdev:  Device pointer
 * @kctx:   Context to add to queue
 * @js:     Job slot to use
 *
 * The context must already be on the per-slot pullable queue. It will be
 * removed from the pullable queue before being added to the unpullable queue.
 *
 * This function should be used when a context has been pulled from, and there
 * are no jobs remaining on the specified slot.
 *
 * Caller must hold hwaccess_lock
 *
 * Return:  true if caller should call kbase_backend_ctx_count_changed()
 */
static bool kbase_js_ctx_list_add_unpullable_nolock(struct kbase_device *kbdev,
						struct kbase_context *kctx,
						int js)
{
	bool ret = false;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	list_move_tail(&kctx->jctx.sched_info.ctx.ctx_list_entry[js],
				&kbdev->js_data.ctx_list_unpullable[js]);

	if (kctx->slots_pullable == (1 << js)) {
		kbdev->js_data.nr_contexts_pullable--;
		ret = true;
		if (!atomic_read(&kctx->atoms_pulled)) {
			WARN_ON(!kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
			kbase_ctx_flag_clear(kctx, KCTX_RUNNABLE_REF);
			atomic_dec(&kbdev->js_data.nr_contexts_runnable);
		}
	}
	kctx->slots_pullable &= ~(1 << js);

	return ret;
}

/**
 * kbase_js_ctx_list_remove_nolock - Remove context from the per-slot pullable
 *                                   or unpullable context queues
 * @kbdev:  Device pointer
 * @kctx:   Context to remove from queue
 * @js:     Job slot to use
 *
 * The context must already be on one of the queues.
 *
 * This function should be used when a context has no jobs on the GPU, and no
 * jobs remaining for the specified slot.
 *
 * Caller must hold hwaccess_lock
 *
 * Return:  true if caller should call kbase_backend_ctx_count_changed()
 */
static bool kbase_js_ctx_list_remove_nolock(struct kbase_device *kbdev,
					struct kbase_context *kctx,
					int js)
{
	bool ret = false;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ON(list_empty(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]));

	list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);

	if (kctx->slots_pullable == (1 << js)) {
		kbdev->js_data.nr_contexts_pullable--;
		ret = true;
		if (!atomic_read(&kctx->atoms_pulled)) {
			WARN_ON(!kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
			kbase_ctx_flag_clear(kctx, KCTX_RUNNABLE_REF);
			atomic_dec(&kbdev->js_data.nr_contexts_runnable);
		}
	}
	kctx->slots_pullable &= ~(1 << js);

	return ret;
}

/**
 * kbase_js_ctx_list_pop_head_nolock - Variant of kbase_js_ctx_list_pop_head()
 *                                     where the caller must hold
 *                                     hwaccess_lock
 * @kbdev:  Device pointer
 * @js:     Job slot to use
 *
 * Caller must hold hwaccess_lock
 *
 * Return:  Context to use for specified slot.
 *          NULL if no contexts present for specified slot
 */
static struct kbase_context *kbase_js_ctx_list_pop_head_nolock(
						struct kbase_device *kbdev,
						int js)
{
	struct kbase_context *kctx;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (list_empty(&kbdev->js_data.ctx_list_pullable[js]))
		return NULL;

	kctx = list_entry(kbdev->js_data.ctx_list_pullable[js].next,
					struct kbase_context,
					jctx.sched_info.ctx.ctx_list_entry[js]);

	list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);

	return kctx;
}

/**
 * kbase_js_ctx_list_pop_head - Pop the head context off the per-slot pullable
 *                              queue.
 * @kbdev:  Device pointer
 * @js:     Job slot to use
 *
 * Return:  Context to use for specified slot.
 *          NULL if no contexts present for specified slot
 */
static struct kbase_context *kbase_js_ctx_list_pop_head(
		struct kbase_device *kbdev, int js)
{
	struct kbase_context *kctx;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kctx = kbase_js_ctx_list_pop_head_nolock(kbdev, js);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return kctx;
}

/**
 * kbase_js_ctx_pullable - Return if a context can be pulled from on the
 *                         specified slot
 * @kctx:          Context pointer
 * @js:            Job slot to use
 * @is_scheduled:  true if the context is currently scheduled
 *
 * Caller must hold hwaccess_lock
 *
 * Return:         true if context can be pulled from on specified slot
 *                 false otherwise
 */
static bool kbase_js_ctx_pullable(struct kbase_context *kctx, int js,
					bool is_scheduled)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbase_jd_atom *katom;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	js_devdata = &kctx->kbdev->js_data;

	if (is_scheduled) {
		if (!kbasep_js_is_submit_allowed(js_devdata, kctx))
			return false;
	}
	katom = jsctx_rb_peek(kctx, js);
	if (!katom)
		return false; /* No pullable atoms */
	if (atomic_read(&katom->blocked))
		return false; /* next atom blocked */
	if (katom->atom_flags & KBASE_KATOM_FLAG_X_DEP_BLOCKED) {
		if (katom->x_pre_dep->gpu_rb_state ==
					KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB ||
					katom->x_pre_dep->will_fail_event_code)
			return false;
		if ((katom->atom_flags & KBASE_KATOM_FLAG_FAIL_BLOCKER) &&
				kbase_backend_nr_atoms_on_slot(kctx->kbdev, js))
			return false;
	}

	return true;
}

static bool kbase_js_dep_validate(struct kbase_context *kctx,
				struct kbase_jd_atom *katom)
{
	struct kbase_device *kbdev = kctx->kbdev;
	bool ret = true;
	bool has_dep = false, has_x_dep = false;
	int js = kbase_js_get_slot(kbdev, katom);
	int prio = katom->sched_priority;
	int i;

	for (i = 0; i < 2; i++) {
		struct kbase_jd_atom *dep_atom = katom->dep[i].atom;

		if (dep_atom) {
			int dep_js = kbase_js_get_slot(kbdev, dep_atom);
			int dep_prio = dep_atom->sched_priority;

			/* Dependent atom must already have been submitted */
			if (!(dep_atom->atom_flags &
					KBASE_KATOM_FLAG_JSCTX_IN_TREE)) {
				ret = false;
				break;
			}

			/* Dependencies with different priorities can't
			  be represented in the ringbuffer */
			if (prio != dep_prio) {
				ret = false;
				break;
			}

			if (js == dep_js) {
				/* Only one same-slot dependency can be
				 * represented in the ringbuffer */
				if (has_dep) {
					ret = false;
					break;
				}
				/* Each dependee atom can only have one
				 * same-slot dependency */
				if (dep_atom->post_dep) {
					ret = false;
					break;
				}
				has_dep = true;
			} else {
				/* Only one cross-slot dependency can be
				 * represented in the ringbuffer */
				if (has_x_dep) {
					ret = false;
					break;
				}
				/* Each dependee atom can only have one
				 * cross-slot dependency */
				if (dep_atom->x_post_dep) {
					ret = false;
					break;
				}
				/* The dependee atom can not already be in the
				 * HW access ringbuffer */
				if (dep_atom->gpu_rb_state !=
					KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
					ret = false;
					break;
				}
				/* The dependee atom can not already have
				 * completed */
				if (dep_atom->status !=
						KBASE_JD_ATOM_STATE_IN_JS) {
					ret = false;
					break;
				}
				/* Cross-slot dependencies must not violate
				 * PRLAM-8987 affinity restrictions */
				if (kbase_hw_has_issue(kbdev,
							BASE_HW_ISSUE_8987) &&
						(js == 2 || dep_js == 2)) {
					ret = false;
					break;
				}
				has_x_dep = true;
			}

			/* Dependency can be represented in ringbuffers */
		}
	}

	/* If dependencies can be represented by ringbuffer then clear them from
	 * atom structure */
	if (ret) {
		for (i = 0; i < 2; i++) {
			struct kbase_jd_atom *dep_atom = katom->dep[i].atom;

			if (dep_atom) {
				int dep_js = kbase_js_get_slot(kbdev, dep_atom);

				if ((js != dep_js) &&
					(dep_atom->status !=
						KBASE_JD_ATOM_STATE_COMPLETED)
					&& (dep_atom->status !=
					KBASE_JD_ATOM_STATE_HW_COMPLETED)
					&& (dep_atom->status !=
						KBASE_JD_ATOM_STATE_UNUSED)) {

					katom->atom_flags |=
						KBASE_KATOM_FLAG_X_DEP_BLOCKED;
					katom->x_pre_dep = dep_atom;
					dep_atom->x_post_dep = katom;
					if (kbase_jd_katom_dep_type(
							&katom->dep[i]) ==
							BASE_JD_DEP_TYPE_DATA)
						katom->atom_flags |=
						KBASE_KATOM_FLAG_FAIL_BLOCKER;
				}
				if ((kbase_jd_katom_dep_type(&katom->dep[i])
						== BASE_JD_DEP_TYPE_DATA) &&
						(js == dep_js)) {
					katom->pre_dep = dep_atom;
					dep_atom->post_dep = katom;
				}

				list_del(&katom->dep_item[i]);
				kbase_jd_katom_dep_clear(&katom->dep[i]);
			}
		}
	}

	return ret;
}

bool kbasep_js_add_job(struct kbase_context *kctx,
		struct kbase_jd_atom *atom)
{
	unsigned long flags;
	struct kbasep_js_kctx_info *js_kctx_info;
	struct kbase_device *kbdev;
	struct kbasep_js_device_data *js_devdata;
	union kbasep_js_policy *js_policy;

	bool enqueue_required = false;
	bool timer_sync = false;

	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(atom != NULL);
	lockdep_assert_held(&kctx->jctx.lock);

	kbdev = kctx->kbdev;
	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;
	js_kctx_info = &kctx->jctx.sched_info;

	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);

	/*
	 * Begin Runpool transaction
	 */
	mutex_lock(&js_devdata->runpool_mutex);

	/* Refcount ctx.nr_jobs */
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.nr_jobs < U32_MAX);
	++(js_kctx_info->ctx.nr_jobs);

	/* Setup any scheduling information */
	kbasep_js_clear_job_retry_submit(atom);

	/* Lock for state available during IRQ */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (!kbase_js_dep_validate(kctx, atom)) {
		/* Dependencies could not be represented */
		--(js_kctx_info->ctx.nr_jobs);

		/* Setting atom status back to queued as it still has unresolved
		 * dependencies */
		atom->status = KBASE_JD_ATOM_STATE_QUEUED;

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&js_devdata->runpool_mutex);

		goto out_unlock;
	}

	kbase_tlstream_tl_attrib_atom_state(atom, TL_ATOM_STATE_READY);
	KBASE_TIMELINE_ATOM_READY(kctx, kbase_jd_atom_id(kctx, atom));

	enqueue_required = kbase_js_dep_resolved_submit(kctx, atom);

	KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_ADD_JOB, kctx, atom, atom->jc,
				kbasep_js_trace_get_refcnt_nolock(kbdev, kctx));

	/* Context Attribute Refcounting */
	kbasep_js_ctx_attr_ctx_retain_atom(kbdev, kctx, atom);

	if (enqueue_required) {
		if (kbase_js_ctx_pullable(kctx, atom->slot_nr, false))
			timer_sync = kbase_js_ctx_list_add_pullable_nolock(
					kbdev, kctx, atom->slot_nr);
		else
			timer_sync = kbase_js_ctx_list_add_unpullable_nolock(
					kbdev, kctx, atom->slot_nr);
	}
	/* If this context is active and the atom is the first on its slot,
	 * kick the job manager to attempt to fast-start the atom */
	if (enqueue_required && kctx == kbdev->hwaccess.active_kctx)
		kbase_jm_try_kick(kbdev, 1 << atom->slot_nr);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	if (timer_sync)
		kbase_backend_ctx_count_changed(kbdev);
	mutex_unlock(&js_devdata->runpool_mutex);
	/* End runpool transaction */

	if (!kbase_ctx_flag(kctx, KCTX_SCHEDULED)) {
		if (kbase_ctx_flag(kctx, KCTX_DYING)) {
			/* A job got added while/after kbase_job_zap_context()
			 * was called on a non-scheduled context (e.g. KDS
			 * dependency resolved). Kill that job by killing the
			 * context. */
			kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx,
					false);
		} else if (js_kctx_info->ctx.nr_jobs == 1) {
			/* Handle Refcount going from 0 to 1: schedule the
			 * context on the Policy Queue */
			KBASE_DEBUG_ASSERT(!kbase_ctx_flag(kctx, KCTX_SCHEDULED));
			dev_dbg(kbdev->dev, "JS: Enqueue Context %p", kctx);

			/* Policy Queue was updated - caller must try to
			 * schedule the head context */
			WARN_ON(!enqueue_required);
		}
	}
out_unlock:
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	mutex_unlock(&js_devdata->queue_mutex);

	return enqueue_required;
}

void kbasep_js_remove_job(struct kbase_device *kbdev,
		struct kbase_context *kctx, struct kbase_jd_atom *atom)
{
	struct kbasep_js_kctx_info *js_kctx_info;
	struct kbasep_js_device_data *js_devdata;
	union kbasep_js_policy *js_policy;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(atom != NULL);

	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;
	js_kctx_info = &kctx->jctx.sched_info;

	KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_REMOVE_JOB, kctx, atom, atom->jc,
			kbasep_js_trace_get_refcnt(kbdev, kctx));

	/* De-refcount ctx.nr_jobs */
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.nr_jobs > 0);
	--(js_kctx_info->ctx.nr_jobs);
}

bool kbasep_js_remove_cancelled_job(struct kbase_device *kbdev,
		struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	unsigned long flags;
	struct kbasep_js_atom_retained_state katom_retained_state;
	struct kbasep_js_device_data *js_devdata;
	bool attr_state_changed;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);

	js_devdata = &kbdev->js_data;

	kbasep_js_atom_retained_state_copy(&katom_retained_state, katom);
	kbasep_js_remove_job(kbdev, kctx, katom);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	/* The atom has 'finished' (will not be re-run), so no need to call
	 * kbasep_js_has_atom_finished().
	 *
	 * This is because it returns false for soft-stopped atoms, but we
	 * want to override that, because we're cancelling an atom regardless of
	 * whether it was soft-stopped or not */
	attr_state_changed = kbasep_js_ctx_attr_ctx_release_atom(kbdev, kctx,
			&katom_retained_state);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return attr_state_changed;
}

bool kbasep_js_runpool_retain_ctx(struct kbase_device *kbdev,
		struct kbase_context *kctx)
{
	unsigned long flags;
	struct kbasep_js_device_data *js_devdata;
	bool result;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	js_devdata = &kbdev->js_data;

	/* KBASE_TRACE_ADD_REFCOUNT( kbdev, JS_RETAIN_CTX, kctx, NULL, 0,
	   kbasep_js_trace_get_refcnt(kbdev, kctx)); */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	result = kbasep_js_runpool_retain_ctx_nolock(kbdev, kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return result;
}

struct kbase_context *kbasep_js_runpool_lookup_ctx(struct kbase_device *kbdev,
		int as_nr)
{
	unsigned long flags;
	struct kbasep_js_device_data *js_devdata;
	struct kbase_context *found_kctx = NULL;
	struct kbasep_js_per_as_data *js_per_as_data;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(0 <= as_nr && as_nr < BASE_MAX_NR_AS);
	js_devdata = &kbdev->js_data;
	js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	found_kctx = js_per_as_data->kctx;

	if (found_kctx != NULL)
		++(js_per_as_data->as_busy_refcount);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return found_kctx;
}

struct kbase_context *kbasep_js_runpool_lookup_ctx_nolock(
		struct kbase_device *kbdev, int as_nr)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbase_context *found_kctx = NULL;
	struct kbasep_js_per_as_data *js_per_as_data;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(0 <= as_nr && as_nr < BASE_MAX_NR_AS);

	lockdep_assert_held(&kbdev->hwaccess_lock);

	js_devdata = &kbdev->js_data;
	js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

	found_kctx = js_per_as_data->kctx;

	if (found_kctx != NULL)
		++(js_per_as_data->as_busy_refcount);

	return found_kctx;
}

/**
 * kbasep_js_release_result - Try running more jobs after releasing a context
 *                            and/or atom
 *
 * @kbdev:                   The kbase_device to operate on
 * @kctx:                    The kbase_context to operate on
 * @katom_retained_state:    Retained state from the atom
 * @runpool_ctx_attr_change: True if the runpool context attributes have changed
 *
 * This collates a set of actions that must happen whilst hwaccess_lock is held.
 *
 * This includes running more jobs when:
 * - The previously released kctx caused a ctx attribute change,
 * - The released atom caused a ctx attribute change,
 * - Slots were previously blocked due to affinity restrictions,
 * - Submission during IRQ handling failed.
 *
 * Return: %KBASEP_JS_RELEASE_RESULT_SCHED_ALL if context attributes were
 *         changed. The caller should try scheduling all contexts
 */
static kbasep_js_release_result kbasep_js_run_jobs_after_ctx_and_atom_release(
		struct kbase_device *kbdev,
		struct kbase_context *kctx,
		struct kbasep_js_atom_retained_state *katom_retained_state,
		bool runpool_ctx_attr_change)
{
	struct kbasep_js_device_data *js_devdata;
	kbasep_js_release_result result = 0;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(katom_retained_state != NULL);
	js_devdata = &kbdev->js_data;

	lockdep_assert_held(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	lockdep_assert_held(&js_devdata->runpool_mutex);
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (js_devdata->nr_user_contexts_running != 0) {
		bool retry_submit = false;
		int retry_jobslot = 0;

		if (katom_retained_state)
			retry_submit = kbasep_js_get_atom_retry_submit_slot(
					katom_retained_state, &retry_jobslot);

		if (runpool_ctx_attr_change || retry_submit) {
			/* A change in runpool ctx attributes might mean we can
			 * run more jobs than before  */
			result = KBASEP_JS_RELEASE_RESULT_SCHED_ALL;

			KBASE_TRACE_ADD_SLOT(kbdev, JD_DONE_TRY_RUN_NEXT_JOB,
						kctx, NULL, 0u, retry_jobslot);
		}
	}
	return result;
}

/*
 * Internal function to release the reference on a ctx and an atom's "retained
 * state", only taking the runpool and as transaction mutexes
 *
 * This also starts more jobs running in the case of an ctx-attribute state
 * change
 *
 * This does none of the followup actions for scheduling:
 * - It does not schedule in a new context
 * - It does not requeue or handle dying contexts
 *
 * For those tasks, just call kbasep_js_runpool_release_ctx() instead
 *
 * Requires:
 * - Context is scheduled in, and kctx->as_nr matches kctx_as_nr
 * - Context has a non-zero refcount
 * - Caller holds js_kctx_info->ctx.jsctx_mutex
 * - Caller holds js_devdata->runpool_mutex
 */
static kbasep_js_release_result kbasep_js_runpool_release_ctx_internal(
		struct kbase_device *kbdev,
		struct kbase_context *kctx,
		struct kbasep_js_atom_retained_state *katom_retained_state)
{
	unsigned long flags;
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	union kbasep_js_policy *js_policy;
	struct kbasep_js_per_as_data *js_per_as_data;

	kbasep_js_release_result release_result = 0u;
	bool runpool_ctx_attr_change = false;
	int kctx_as_nr;
	struct kbase_as *current_as;
	int new_ref_count;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;

	/* Ensure context really is scheduled in */
	KBASE_DEBUG_ASSERT(kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	/* kctx->as_nr and js_per_as_data are only read from here. The caller's
	 * js_ctx_mutex provides a barrier that ensures they are up-to-date.
	 *
	 * They will not change whilst we're reading them, because the refcount
	 * is non-zero (and we ASSERT on that last fact).
	 */
	kctx_as_nr = kctx->as_nr;
	KBASE_DEBUG_ASSERT(kctx_as_nr != KBASEP_AS_NR_INVALID);
	js_per_as_data = &js_devdata->runpool_irq.per_as_data[kctx_as_nr];
	KBASE_DEBUG_ASSERT(js_per_as_data->as_busy_refcount > 0);

	/*
	 * Transaction begins on AS and runpool_irq
	 *
	 * Assert about out calling contract
	 */
	current_as = &kbdev->as[kctx_as_nr];
	mutex_lock(&kbdev->pm.lock);
	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	KBASE_DEBUG_ASSERT(kctx_as_nr == kctx->as_nr);
	KBASE_DEBUG_ASSERT(js_per_as_data->as_busy_refcount > 0);

	/* Update refcount */
	new_ref_count = --(js_per_as_data->as_busy_refcount);

	/* Release the atom if it finished (i.e. wasn't soft-stopped) */
	if (kbasep_js_has_atom_finished(katom_retained_state))
		runpool_ctx_attr_change |= kbasep_js_ctx_attr_ctx_release_atom(
				kbdev, kctx, katom_retained_state);

	KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_RELEASE_CTX, kctx, NULL, 0u,
			new_ref_count);

	if (new_ref_count == 1 && kbase_ctx_flag(kctx, KCTX_PRIVILEGED) &&
			!kbase_pm_is_suspending(kbdev)) {
		/* Context is kept scheduled into an address space even when
		 * there are no jobs, in this case we have to handle the
		 * situation where all jobs have been evicted from the GPU and
		 * submission is disabled.
		 *
		 * At this point we re-enable submission to allow further jobs
		 * to be executed
		 */
		kbasep_js_set_submit_allowed(js_devdata, kctx);
	}

	/* Make a set of checks to see if the context should be scheduled out */
	if (new_ref_count == 0 &&
		(!kbasep_js_is_submit_allowed(js_devdata, kctx) ||
							kbdev->pm.suspending)) {
		int num_slots = kbdev->gpu_props.num_job_slots;
		int slot;

		/* Last reference, and we've been told to remove this context
		 * from the Run Pool */
		dev_dbg(kbdev->dev, "JS: RunPool Remove Context %p because as_busy_refcount=%d, jobs=%d, allowed=%d",
				kctx, new_ref_count, js_kctx_info->ctx.nr_jobs,
				kbasep_js_is_submit_allowed(js_devdata, kctx));

#if defined(CONFIG_MALI_GATOR_SUPPORT)
		kbase_trace_mali_mmu_as_released(kctx->as_nr);
#endif
		kbase_tlstream_tl_nret_as_ctx(&kbdev->as[kctx->as_nr], kctx);

		kbase_backend_release_ctx_irq(kbdev, kctx);

		if (kbdev->hwaccess.active_kctx == kctx)
			kbdev->hwaccess.active_kctx = NULL;

		/* Ctx Attribute handling
		 *
		 * Releasing atoms attributes must either happen before this, or
		 * after the KCTX_SHEDULED flag is changed, otherwise we
		 * double-decount the attributes
		 */
		runpool_ctx_attr_change |=
			kbasep_js_ctx_attr_runpool_release_ctx(kbdev, kctx);

		/* Releasing the context and katom retained state can allow
		 * more jobs to run */
		release_result |=
			kbasep_js_run_jobs_after_ctx_and_atom_release(kbdev,
						kctx, katom_retained_state,
						runpool_ctx_attr_change);

		/*
		 * Transaction ends on AS and runpool_irq:
		 *
		 * By this point, the AS-related data is now clear and ready
		 * for re-use.
		 *
		 * Since releases only occur once for each previous successful
		 * retain, and no more retains are allowed on this context, no
		 * other thread will be operating in this
		 * code whilst we are
		 */

		/* Recalculate pullable status for all slots */
		for (slot = 0; slot < num_slots; slot++) {
			if (kbase_js_ctx_pullable(kctx, slot, false))
				kbase_js_ctx_list_add_pullable_nolock(kbdev,
						kctx, slot);
		}

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		kbase_backend_release_ctx_noirq(kbdev, kctx);

		mutex_unlock(&kbdev->mmu_hw_mutex);
		mutex_unlock(&kbdev->pm.lock);

		/* Note: Don't reuse kctx_as_nr now */

		/* Synchronize with any policy timers */
		kbase_backend_ctx_count_changed(kbdev);

		/* update book-keeping info */
		kbase_ctx_flag_clear(kctx, KCTX_SCHEDULED);
		/* Signal any waiter that the context is not scheduled, so is
		 * safe for termination - once the jsctx_mutex is also dropped,
		 * and jobs have finished. */
		wake_up(&js_kctx_info->ctx.is_scheduled_wait);

		/* Queue an action to occur after we've dropped the lock */
		release_result |= KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED |
			KBASEP_JS_RELEASE_RESULT_SCHED_ALL;
	} else {
		kbasep_js_run_jobs_after_ctx_and_atom_release(kbdev, kctx,
				katom_retained_state, runpool_ctx_attr_change);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&kbdev->mmu_hw_mutex);
		mutex_unlock(&kbdev->pm.lock);
	}

	return release_result;
}

void kbasep_js_runpool_release_ctx_nolock(struct kbase_device *kbdev,
						struct kbase_context *kctx)
{
	struct kbasep_js_atom_retained_state katom_retained_state;

	/* Setup a dummy katom_retained_state */
	kbasep_js_atom_retained_state_init_invalid(&katom_retained_state);

	kbasep_js_runpool_release_ctx_internal(kbdev, kctx,
							&katom_retained_state);
}

void kbasep_js_runpool_requeue_or_kill_ctx(struct kbase_device *kbdev,
		struct kbase_context *kctx, bool has_pm_ref)
{
	struct kbasep_js_device_data *js_devdata;
	union kbasep_js_policy *js_policy;
	struct kbasep_js_kctx_info *js_kctx_info;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;
	js_policy = &kbdev->js_data.policy;
	js_devdata = &kbdev->js_data;

	/* This is called if and only if you've you've detached the context from
	 * the Runpool or the Policy Queue, and not added it back to the Runpool
	 */
	KBASE_DEBUG_ASSERT(!kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	if (kbase_ctx_flag(kctx, KCTX_DYING)) {
		/* Dying: don't requeue, but kill all jobs on the context. This
		 * happens asynchronously */
		dev_dbg(kbdev->dev,
			"JS: ** Killing Context %p on RunPool Remove **", kctx);
		kbase_js_foreach_ctx_job(kctx, &kbase_jd_cancel);
	}
}

void kbasep_js_runpool_release_ctx_and_katom_retained_state(
		struct kbase_device *kbdev, struct kbase_context *kctx,
		struct kbasep_js_atom_retained_state *katom_retained_state)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	kbasep_js_release_result release_result;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;

	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_lock(&js_devdata->runpool_mutex);

	release_result = kbasep_js_runpool_release_ctx_internal(kbdev, kctx,
			katom_retained_state);

	/* Drop the runpool mutex to allow requeing kctx */
	mutex_unlock(&js_devdata->runpool_mutex);

	if ((release_result & KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED) != 0u)
		kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx, true);

	/* Drop the jsctx_mutex to allow scheduling in a new context */

	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_unlock(&js_devdata->queue_mutex);

	if (release_result & KBASEP_JS_RELEASE_RESULT_SCHED_ALL)
		kbase_js_sched_all(kbdev);
}

void kbasep_js_runpool_release_ctx(struct kbase_device *kbdev,
		struct kbase_context *kctx)
{
	struct kbasep_js_atom_retained_state katom_retained_state;

	kbasep_js_atom_retained_state_init_invalid(&katom_retained_state);

	kbasep_js_runpool_release_ctx_and_katom_retained_state(kbdev, kctx,
			&katom_retained_state);
}

/* Variant of kbasep_js_runpool_release_ctx() that doesn't call into
 * kbase_js_sched_all() */
static void kbasep_js_runpool_release_ctx_no_schedule(
		struct kbase_device *kbdev, struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	kbasep_js_release_result release_result;
	struct kbasep_js_atom_retained_state katom_retained_state_struct;
	struct kbasep_js_atom_retained_state *katom_retained_state =
		&katom_retained_state_struct;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;
	kbasep_js_atom_retained_state_init_invalid(katom_retained_state);

	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_lock(&js_devdata->runpool_mutex);

	release_result = kbasep_js_runpool_release_ctx_internal(kbdev, kctx,
			katom_retained_state);

	/* Drop the runpool mutex to allow requeing kctx */
	mutex_unlock(&js_devdata->runpool_mutex);
	if ((release_result & KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED) != 0u)
		kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx, true);

	/* Drop the jsctx_mutex to allow scheduling in a new context */
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	/* NOTE: could return release_result if the caller would like to know
	 * whether it should schedule a new context, but currently no callers do
	 */
}

void kbase_js_set_timeouts(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbase_backend_timeouts_changed(kbdev);
}

static bool kbasep_js_schedule_ctx(struct kbase_device *kbdev,
					struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	union kbasep_js_policy *js_policy;
	struct kbase_as *new_address_space = NULL;
	unsigned long flags;
	bool kctx_suspended = false;
	int as_nr;

	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;
	js_kctx_info = &kctx->jctx.sched_info;

	/* Pick available address space for this context */
	as_nr = kbase_backend_find_free_address_space(kbdev, kctx);

	if (as_nr == KBASEP_AS_NR_INVALID)
		return false; /* No address spaces currently available */

	new_address_space = &kbdev->as[as_nr];

	/*
	 * Atomic transaction on the Context and Run Pool begins
	 */
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_lock(&js_devdata->runpool_mutex);

	/* Check to see if context is dying due to kbase_job_zap_context() */
	if (kbase_ctx_flag(kctx, KCTX_DYING)) {
		/* Roll back the transaction so far and return */
		kbase_backend_release_free_address_space(kbdev, as_nr);

		mutex_unlock(&js_devdata->runpool_mutex);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

		return false;
	}

	KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_TRY_SCHEDULE_HEAD_CTX, kctx, NULL,
				0u,
				kbasep_js_trace_get_refcnt(kbdev, kctx));

	kbase_ctx_flag_set(kctx, KCTX_SCHEDULED);

	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	/* Assign context to previously chosen address space */
	if (!kbase_backend_use_ctx(kbdev, kctx, as_nr)) {
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&kbdev->mmu_hw_mutex);
		/* Roll back the transaction so far and return */
		kbase_ctx_flag_clear(kctx, KCTX_SCHEDULED);

		kbase_backend_release_free_address_space(kbdev, as_nr);

		mutex_unlock(&js_devdata->runpool_mutex);

		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		return false;
	}

	kbdev->hwaccess.active_kctx = kctx;

#if defined(CONFIG_MALI_GATOR_SUPPORT)
	kbase_trace_mali_mmu_as_in_use(kctx->as_nr);
#endif
	kbase_tlstream_tl_ret_as_ctx(&kbdev->as[kctx->as_nr], kctx);

	/* Cause any future waiter-on-termination to wait until the context is
	 * descheduled */
	wake_up(&js_kctx_info->ctx.is_scheduled_wait);

	/* Re-check for suspending: a suspend could've occurred, and all the
	 * contexts could've been removed from the runpool before we took this
	 * lock. In this case, we don't want to allow this context to run jobs,
	 * we just want it out immediately.
	 *
	 * The DMB required to read the suspend flag was issued recently as part
	 * of the hwaccess_lock locking. If a suspend occurs *after* that lock
	 * was taken (i.e. this condition doesn't execute), then the
	 * kbasep_js_suspend() code will cleanup this context instead (by virtue
	 * of it being called strictly after the suspend flag is set, and will
	 * wait for this lock to drop) */
	if (kbase_pm_is_suspending(kbdev)) {
		/* Cause it to leave at some later point */
		bool retained;

		retained = kbasep_js_runpool_retain_ctx_nolock(kbdev, kctx);
		KBASE_DEBUG_ASSERT(retained);

		kbasep_js_clear_submit_allowed(js_devdata, kctx);
		kctx_suspended = true;
	}

	/* Transaction complete */
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);

	/* Synchronize with any policy timers */
	kbase_backend_ctx_count_changed(kbdev);

	mutex_unlock(&js_devdata->runpool_mutex);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
	/* Note: after this point, the context could potentially get scheduled
	 * out immediately */

	if (kctx_suspended) {
		/* Finishing forcing out the context due to a suspend. Use a
		 * variant of kbasep_js_runpool_release_ctx() that doesn't
		 * schedule a new context, to prevent a risk of recursion back
		 * into this function */
		kbasep_js_runpool_release_ctx_no_schedule(kbdev, kctx);
		return false;
	}
	return true;
}

static bool kbase_js_use_ctx(struct kbase_device *kbdev,
				struct kbase_context *kctx)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbase_backend_use_ctx_sched(kbdev, kctx)) {
		/* Context already has ASID - mark as active */
		kbdev->hwaccess.active_kctx = kctx;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		return true; /* Context already scheduled */
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return kbasep_js_schedule_ctx(kbdev, kctx);
}

void kbasep_js_schedule_privileged_ctx(struct kbase_device *kbdev,
		struct kbase_context *kctx)
{
	struct kbasep_js_kctx_info *js_kctx_info;
	struct kbasep_js_device_data *js_devdata;
	bool is_scheduled;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	/* This must never be attempted whilst suspending - i.e. it should only
	 * happen in response to a syscall from a user-space thread */
	BUG_ON(kbase_pm_is_suspending(kbdev));

	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);

	/* Mark the context as privileged */
	kbase_ctx_flag_set(kctx, KCTX_PRIVILEGED);

	is_scheduled = kbase_ctx_flag(kctx, KCTX_SCHEDULED);
	if (!is_scheduled) {
		/* Add the context to the pullable list */
		if (kbase_js_ctx_list_add_pullable_head(kbdev, kctx, 0))
			kbase_js_sync_timers(kbdev);

		/* Fast-starting requires the jsctx_mutex to be dropped,
		 * because it works on multiple ctxs */
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);

		/* Try to schedule the context in */
		kbase_js_sched_all(kbdev);

		/* Wait for the context to be scheduled in */
		wait_event(kctx->jctx.sched_info.ctx.is_scheduled_wait,
			   kbase_ctx_flag(kctx, KCTX_SCHEDULED));
	} else {
		/* Already scheduled in - We need to retain it to keep the
		 * corresponding address space */
		kbasep_js_runpool_retain_ctx(kbdev, kctx);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);
	}
}
KBASE_EXPORT_TEST_API(kbasep_js_schedule_privileged_ctx);

void kbasep_js_release_privileged_ctx(struct kbase_device *kbdev,
		struct kbase_context *kctx)
{
	struct kbasep_js_kctx_info *js_kctx_info;

	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;

	/* We don't need to use the address space anymore */
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	kbase_ctx_flag_clear(kctx, KCTX_PRIVILEGED);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	/* Release the context - it will be scheduled out */
	kbasep_js_runpool_release_ctx(kbdev, kctx);

	kbase_js_sched_all(kbdev);
}
KBASE_EXPORT_TEST_API(kbasep_js_release_privileged_ctx);

void kbasep_js_suspend(struct kbase_device *kbdev)
{
	unsigned long flags;
	struct kbasep_js_device_data *js_devdata;
	int i;
	u16 retained = 0u;
	int nr_privileged_ctx = 0;

	KBASE_DEBUG_ASSERT(kbdev);
	KBASE_DEBUG_ASSERT(kbase_pm_is_suspending(kbdev));
	js_devdata = &kbdev->js_data;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	/* Prevent all contexts from submitting */
	js_devdata->runpool_irq.submit_allowed = 0;

	/* Retain each of the contexts, so we can cause it to leave even if it
	 * had no refcount to begin with */
	for (i = BASE_MAX_NR_AS - 1; i >= 0; --i) {
		struct kbasep_js_per_as_data *js_per_as_data =
			&js_devdata->runpool_irq.per_as_data[i];
		struct kbase_context *kctx = js_per_as_data->kctx;

		retained = retained << 1;

		if (kctx) {
			++(js_per_as_data->as_busy_refcount);
			retained |= 1u;
			/* We can only cope with up to 1 privileged context -
			 * the instrumented context. It'll be suspended by
			 * disabling instrumentation */
			if (kbase_ctx_flag(kctx, KCTX_PRIVILEGED)) {
				++nr_privileged_ctx;
				WARN_ON(nr_privileged_ctx != 1);
			}
		}
	}
	CSTD_UNUSED(nr_privileged_ctx);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* De-ref the previous retain to ensure each context gets pulled out
	 * sometime later. */
	for (i = 0;
		 i < BASE_MAX_NR_AS;
		 ++i, retained = retained >> 1) {
		struct kbasep_js_per_as_data *js_per_as_data =
			&js_devdata->runpool_irq.per_as_data[i];
		struct kbase_context *kctx = js_per_as_data->kctx;

		if (retained & 1u)
			kbasep_js_runpool_release_ctx(kbdev, kctx);
	}

	/* Caller must wait for all Power Manager active references to be
	 * dropped */
}

void kbasep_js_resume(struct kbase_device *kbdev)
{
	struct kbasep_js_device_data *js_devdata;
	int js;

	KBASE_DEBUG_ASSERT(kbdev);
	js_devdata = &kbdev->js_data;
	KBASE_DEBUG_ASSERT(!kbase_pm_is_suspending(kbdev));

	mutex_lock(&js_devdata->queue_mutex);
	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		struct kbase_context *kctx, *n;

		list_for_each_entry_safe(kctx, n,
				&kbdev->js_data.ctx_list_unpullable[js],
				jctx.sched_info.ctx.ctx_list_entry[js]) {
			struct kbasep_js_kctx_info *js_kctx_info;
			unsigned long flags;
			bool timer_sync = false;

			js_kctx_info = &kctx->jctx.sched_info;

			mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
			mutex_lock(&js_devdata->runpool_mutex);
			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

			if (!kbase_ctx_flag(kctx, KCTX_SCHEDULED) &&
				kbase_js_ctx_pullable(kctx, js, false))
				timer_sync =
					kbase_js_ctx_list_add_pullable_nolock(
							kbdev, kctx, js);
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			if (timer_sync)
				kbase_backend_ctx_count_changed(kbdev);
			mutex_unlock(&js_devdata->runpool_mutex);
			mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		}
	}
	mutex_unlock(&js_devdata->queue_mutex);

	/* Restart atom processing */
	kbase_js_sched_all(kbdev);

	/* JS Resume complete */
}

bool kbase_js_is_atom_valid(struct kbase_device *kbdev,
				struct kbase_jd_atom *katom)
{
	if ((katom->core_req & BASE_JD_REQ_FS) &&
	    (katom->core_req & (BASE_JD_REQ_CS | BASE_JD_REQ_ONLY_COMPUTE |
								BASE_JD_REQ_T)))
		return false;

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8987) &&
	    (katom->core_req & BASE_JD_REQ_ONLY_COMPUTE) &&
	    (katom->core_req & (BASE_JD_REQ_CS | BASE_JD_REQ_T)))
		return false;

	return true;
}

static int kbase_js_get_slot(struct kbase_device *kbdev,
				struct kbase_jd_atom *katom)
{
	if (katom->core_req & BASE_JD_REQ_FS)
		return 0;

	if (katom->core_req & BASE_JD_REQ_ONLY_COMPUTE) {
		if (katom->device_nr == 1 &&
				kbdev->gpu_props.num_core_groups == 2)
			return 2;
		if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8987))
			return 2;
	}

	return 1;
}

bool kbase_js_dep_resolved_submit(struct kbase_context *kctx,
					struct kbase_jd_atom *katom)
{
	bool enqueue_required;

	katom->slot_nr = kbase_js_get_slot(kctx->kbdev, katom);

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);
	lockdep_assert_held(&kctx->jctx.lock);

	/* If slot will transition from unpullable to pullable then add to
	 * pullable list */
	if (jsctx_rb_none_to_pull(kctx, katom->slot_nr)) {
		enqueue_required = true;
	} else {
		enqueue_required = false;
	}
	if ((katom->atom_flags & KBASE_KATOM_FLAG_X_DEP_BLOCKED) ||
			(katom->pre_dep && (katom->pre_dep->atom_flags &
			KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST))) {
		int prio = katom->sched_priority;
		int js = katom->slot_nr;
		struct jsctx_queue *queue = &kctx->jsctx_queue[prio][js];

		list_add_tail(&katom->queue, &queue->x_dep_head);
		katom->atom_flags |= KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST;
		enqueue_required = false;
	} else {
		/* Check if there are lower priority jobs to soft stop */
		kbase_job_slot_ctx_priority_check_locked(kctx, katom);

		/* Add atom to ring buffer. */
		jsctx_tree_add(kctx, katom);
		katom->atom_flags |= KBASE_KATOM_FLAG_JSCTX_IN_TREE;
	}

	return enqueue_required;
}

/**
 * kbase_js_move_to_tree - Move atom (and any dependent atoms) to the
 *                         runnable_tree, ready for execution
 * @katom: Atom to submit
 *
 * It is assumed that @katom does not have KBASE_KATOM_FLAG_X_DEP_BLOCKED set,
 * but is still present in the x_dep list. If @katom has a same-slot dependent
 * atom then that atom (and any dependents) will also be moved.
 */
static void kbase_js_move_to_tree(struct kbase_jd_atom *katom)
{
	lockdep_assert_held(&katom->kctx->kbdev->hwaccess_lock);

	while (katom) {
		WARN_ON(!(katom->atom_flags &
				KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST));

		if (!(katom->atom_flags & KBASE_KATOM_FLAG_X_DEP_BLOCKED)) {
			list_del(&katom->queue);
			katom->atom_flags &=
					~KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST;
			jsctx_tree_add(katom->kctx, katom);
			katom->atom_flags |= KBASE_KATOM_FLAG_JSCTX_IN_TREE;
		} else {
			break;
		}

		katom = katom->post_dep;
	}
}


/**
 * kbase_js_evict_deps - Evict dependencies of a failed atom.
 * @kctx:       Context pointer
 * @katom:      Pointer to the atom that has failed.
 * @js:         The job slot the katom was run on.
 * @prio:       Priority of the katom.
 *
 * Remove all post dependencies of an atom from the context ringbuffers.
 *
 * The original atom's event_code will be propogated to all dependent atoms.
 *
 * Context: Caller must hold the HW access lock
 */
static void kbase_js_evict_deps(struct kbase_context *kctx,
				struct kbase_jd_atom *katom, int js, int prio)
{
	struct kbase_jd_atom *x_dep = katom->x_post_dep;
	struct kbase_jd_atom *next_katom = katom->post_dep;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	if (next_katom) {
		KBASE_DEBUG_ASSERT(next_katom->status !=
				KBASE_JD_ATOM_STATE_HW_COMPLETED);
		next_katom->will_fail_event_code = katom->event_code;

	}

	/* Has cross slot depenency. */
	if (x_dep && (x_dep->atom_flags & (KBASE_KATOM_FLAG_JSCTX_IN_TREE |
				KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST))) {
		/* Remove dependency.*/
		x_dep->atom_flags &= ~KBASE_KATOM_FLAG_X_DEP_BLOCKED;

		/* Fail if it had a data dependency. */
		if (x_dep->atom_flags & KBASE_KATOM_FLAG_FAIL_BLOCKER) {
			x_dep->will_fail_event_code = katom->event_code;
		}
		if (x_dep->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST)
			kbase_js_move_to_tree(x_dep);
	}
}

struct kbase_jd_atom *kbase_js_pull(struct kbase_context *kctx, int js)
{
	struct kbase_jd_atom *katom;
	struct kbasep_js_device_data *js_devdata;
	int pulled;

	KBASE_DEBUG_ASSERT(kctx);

	js_devdata = &kctx->kbdev->js_data;
	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	if (!kbasep_js_is_submit_allowed(js_devdata, kctx))
		return NULL;
	if (kbase_pm_is_suspending(kctx->kbdev))
		return NULL;

	katom = jsctx_rb_peek(kctx, js);
	if (!katom)
		return NULL;

	if (atomic_read(&katom->blocked))
		return NULL;

	/* Due to ordering restrictions when unpulling atoms on failure, we do
	 * not allow multiple runs of fail-dep atoms from the same context to be
	 * present on the same slot */
	if (katom->pre_dep && atomic_read(&kctx->atoms_pulled_slot[js])) {
		struct kbase_jd_atom *prev_atom =
				kbase_backend_inspect_tail(kctx->kbdev, js);

		if (prev_atom && prev_atom->kctx != kctx)
			return NULL;
	}

	if (katom->atom_flags & KBASE_KATOM_FLAG_X_DEP_BLOCKED) {
		if (katom->x_pre_dep->gpu_rb_state ==
					KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB ||
					katom->x_pre_dep->will_fail_event_code)
			return NULL;
		if ((katom->atom_flags & KBASE_KATOM_FLAG_FAIL_BLOCKER) &&
				kbase_backend_nr_atoms_on_slot(kctx->kbdev, js))
			return NULL;
	}

	kbase_ctx_flag_set(kctx, KCTX_PULLED);

	pulled = atomic_inc_return(&kctx->atoms_pulled);
	if (pulled == 1 && !kctx->slots_pullable) {
		WARN_ON(kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
		kbase_ctx_flag_set(kctx, KCTX_RUNNABLE_REF);
		atomic_inc(&kctx->kbdev->js_data.nr_contexts_runnable);
	}
	atomic_inc(&kctx->atoms_pulled_slot[katom->slot_nr]);
	jsctx_rb_pull(kctx, katom);

	kbasep_js_runpool_retain_ctx_nolock(kctx->kbdev, kctx);
	katom->atom_flags |= KBASE_KATOM_FLAG_HOLDING_CTX_REF;

	katom->sched_info.cfs.ticks = 0;

	return katom;
}


static void js_return_worker(struct work_struct *data)
{
	struct kbase_jd_atom *katom = container_of(data, struct kbase_jd_atom,
									work);
	struct kbase_context *kctx = katom->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
	struct kbasep_js_kctx_info *js_kctx_info = &kctx->jctx.sched_info;
	struct kbasep_js_atom_retained_state retained_state;
	int js = katom->slot_nr;
	bool timer_sync = false;
	bool context_idle = false;
	unsigned long flags;
	base_jd_core_req core_req = katom->core_req;
	u64 affinity = katom->affinity;
	enum kbase_atom_coreref_state coreref_state = katom->coreref_state;

	kbase_tlstream_tl_event_atom_softstop_ex(katom);

	kbase_backend_complete_wq(kbdev, katom);

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316))
		kbase_as_poking_timer_release_atom(kbdev, kctx, katom);

	kbasep_js_atom_retained_state_copy(&retained_state, katom);

	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);

	atomic_dec(&kctx->atoms_pulled);
	atomic_dec(&kctx->atoms_pulled_slot[js]);

	atomic_dec(&katom->blocked);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (!atomic_read(&kctx->atoms_pulled_slot[js]) &&
			jsctx_rb_none_to_pull(kctx, js))
		timer_sync |= kbase_js_ctx_list_remove_nolock(kbdev, kctx, js);

	if (!atomic_read(&kctx->atoms_pulled)) {
		if (!kctx->slots_pullable) {
			WARN_ON(!kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
			kbase_ctx_flag_clear(kctx, KCTX_RUNNABLE_REF);
			atomic_dec(&kbdev->js_data.nr_contexts_runnable);
			timer_sync = true;
		}

		if (kctx->as_nr != KBASEP_AS_NR_INVALID &&
				!kbase_ctx_flag(kctx, KCTX_DYING)) {
			int num_slots = kbdev->gpu_props.num_job_slots;
			int slot;

			if (!kbasep_js_is_submit_allowed(js_devdata, kctx))
				kbasep_js_set_submit_allowed(js_devdata, kctx);

			for (slot = 0; slot < num_slots; slot++) {
				if (kbase_js_ctx_pullable(kctx, slot, true))
					timer_sync |=
					kbase_js_ctx_list_add_pullable_nolock(
							kbdev, kctx, slot);
			}
		}

		kbase_jm_idle_ctx(kbdev, kctx);

		context_idle = true;
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (context_idle) {
		WARN_ON(!kbase_ctx_flag(kctx, KCTX_ACTIVE));
		kbase_ctx_flag_clear(kctx, KCTX_ACTIVE);
		kbase_pm_context_idle(kbdev);
	}

	if (timer_sync)
		kbase_js_sync_timers(kbdev);

	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_unlock(&js_devdata->queue_mutex);

	katom->atom_flags &= ~KBASE_KATOM_FLAG_HOLDING_CTX_REF;
	kbasep_js_runpool_release_ctx_and_katom_retained_state(kbdev, kctx,
							&retained_state);

	kbase_js_sched_all(kbdev);

	kbase_backend_complete_wq_post_sched(kbdev, core_req, affinity,
			coreref_state);
}

void kbase_js_unpull(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	jsctx_rb_unpull(kctx, katom);

	WARN_ON(work_pending(&katom->work));

	/* Block re-submission until workqueue has run */
	atomic_inc(&katom->blocked);

	kbase_job_check_leave_disjoint(kctx->kbdev, katom);

	KBASE_DEBUG_ASSERT(0 == object_is_on_stack(&katom->work));
	INIT_WORK(&katom->work, js_return_worker);
	queue_work(kctx->jctx.job_done_wq, &katom->work);
}

bool kbase_js_complete_atom_wq(struct kbase_context *kctx,
						struct kbase_jd_atom *katom)
{
	struct kbasep_js_kctx_info *js_kctx_info;
	struct kbasep_js_device_data *js_devdata;
	struct kbase_device *kbdev;
	unsigned long flags;
	bool timer_sync = false;
	int atom_slot;
	bool context_idle = false;

	kbdev = kctx->kbdev;
	atom_slot = katom->slot_nr;

	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;

	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);

	mutex_lock(&js_devdata->runpool_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (katom->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_TREE) {
		context_idle = !atomic_dec_return(&kctx->atoms_pulled);
		atomic_dec(&kctx->atoms_pulled_slot[atom_slot]);

		if (!atomic_read(&kctx->atoms_pulled) &&
				!kctx->slots_pullable) {
			WARN_ON(!kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
			kbase_ctx_flag_clear(kctx, KCTX_RUNNABLE_REF);
			atomic_dec(&kbdev->js_data.nr_contexts_runnable);
			timer_sync = true;
		}
	}
	WARN_ON(!(katom->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_TREE));

	if (!atomic_read(&kctx->atoms_pulled_slot[atom_slot]) &&
			jsctx_rb_none_to_pull(kctx, atom_slot)) {
		if (!list_empty(
			&kctx->jctx.sched_info.ctx.ctx_list_entry[atom_slot]))
			timer_sync |= kbase_js_ctx_list_remove_nolock(
					kctx->kbdev, kctx, atom_slot);
	}

	/*
	 * If submission is disabled on this context (most likely due to an
	 * atom failure) and there are now no atoms left in the system then
	 * re-enable submission so that context can be scheduled again.
	 */
	if (!kbasep_js_is_submit_allowed(js_devdata, kctx) &&
					!atomic_read(&kctx->atoms_pulled) &&
					!kbase_ctx_flag(kctx, KCTX_DYING)) {
		int js;

		kbasep_js_set_submit_allowed(js_devdata, kctx);

		for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
			if (kbase_js_ctx_pullable(kctx, js, true))
				timer_sync |=
					kbase_js_ctx_list_add_pullable_nolock(
							kbdev, kctx, js);
		}
	} else if (katom->x_post_dep &&
			kbasep_js_is_submit_allowed(js_devdata, kctx)) {
		int js;

		for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
			if (kbase_js_ctx_pullable(kctx, js, true))
				timer_sync |=
					kbase_js_ctx_list_add_pullable_nolock(
							kbdev, kctx, js);
		}
	}

	/* Mark context as inactive. The pm reference will be dropped later in
	 * jd_done_worker().
	 */
	if (context_idle)
		kbase_ctx_flag_clear(kctx, KCTX_ACTIVE);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	if (timer_sync)
		kbase_backend_ctx_count_changed(kbdev);
	mutex_unlock(&js_devdata->runpool_mutex);

	return context_idle;
}

struct kbase_jd_atom *kbase_js_complete_atom(struct kbase_jd_atom *katom,
		ktime_t *end_timestamp)
{
	u64 microseconds_spent = 0;
	struct kbase_device *kbdev;
	struct kbase_context *kctx = katom->kctx;
	union kbasep_js_policy *js_policy;
	struct kbase_jd_atom *x_dep = katom->x_post_dep;

	kbdev = kctx->kbdev;

	js_policy = &kbdev->js_data.policy;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	if (katom->will_fail_event_code)
		katom->event_code = katom->will_fail_event_code;

	katom->status = KBASE_JD_ATOM_STATE_HW_COMPLETED;

	if (katom->event_code != BASE_JD_EVENT_DONE) {
		kbase_js_evict_deps(kctx, katom, katom->slot_nr,
				katom->sched_priority);
	}

#if defined(CONFIG_MALI_GATOR_SUPPORT)
	kbase_trace_mali_job_slots_event(GATOR_MAKE_EVENT(GATOR_JOB_SLOT_STOP,
				katom->slot_nr), NULL, 0);
#endif

	/* Calculate the job's time used */
	if (end_timestamp != NULL) {
		/* Only calculating it for jobs that really run on the HW (e.g.
		 * removed from next jobs never actually ran, so really did take
		 * zero time) */
		ktime_t tick_diff = ktime_sub(*end_timestamp,
							katom->start_timestamp);

		microseconds_spent = ktime_to_ns(tick_diff);

		do_div(microseconds_spent, 1000);

		/* Round up time spent to the minimum timer resolution */
		if (microseconds_spent < KBASEP_JS_TICK_RESOLUTION_US)
			microseconds_spent = KBASEP_JS_TICK_RESOLUTION_US;
	}

	/* Log the result of the job (completion status, and time spent). */
	kbasep_js_policy_log_job_result(js_policy, katom, microseconds_spent);

	kbase_jd_done(katom, katom->slot_nr, end_timestamp, 0);

	/* Unblock cross dependency if present */
	if (x_dep && (katom->event_code == BASE_JD_EVENT_DONE ||
			!(x_dep->atom_flags & KBASE_KATOM_FLAG_FAIL_BLOCKER)) &&
			(x_dep->atom_flags & KBASE_KATOM_FLAG_X_DEP_BLOCKED)) {
		bool was_pullable = kbase_js_ctx_pullable(kctx, x_dep->slot_nr,
				false);
		x_dep->atom_flags &= ~KBASE_KATOM_FLAG_X_DEP_BLOCKED;
		kbase_js_move_to_tree(x_dep);
		if (!was_pullable && kbase_js_ctx_pullable(kctx, x_dep->slot_nr,
				false))
			kbase_js_ctx_list_add_pullable_nolock(kbdev, kctx,
					x_dep->slot_nr);

		if (x_dep->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_TREE)
			return x_dep;
	}

	return NULL;
}

void kbase_js_sched(struct kbase_device *kbdev, int js_mask)
{
	struct kbasep_js_device_data *js_devdata;
	bool timer_sync = false;

	js_devdata = &kbdev->js_data;

	down(&js_devdata->schedule_sem);
	mutex_lock(&js_devdata->queue_mutex);

	while (js_mask) {
		int js;

		js = ffs(js_mask) - 1;

		while (1) {
			struct kbase_context *kctx;
			unsigned long flags;
			bool context_idle = false;

			kctx = kbase_js_ctx_list_pop_head(kbdev, js);

			if (!kctx) {
				js_mask &= ~(1 << js);
				break; /* No contexts on pullable list */
			}

			if (!kbase_ctx_flag(kctx, KCTX_ACTIVE)) {
				context_idle = true;

				if (kbase_pm_context_active_handle_suspend(
									kbdev,
				      KBASE_PM_SUSPEND_HANDLER_DONT_INCREASE)) {
					/* Suspend pending - return context to
					 * queue and stop scheduling */
					mutex_lock(
					&kctx->jctx.sched_info.ctx.jsctx_mutex);
					if (kbase_js_ctx_list_add_pullable_head(
						kctx->kbdev, kctx, js))
						kbase_js_sync_timers(kbdev);
					mutex_unlock(
					&kctx->jctx.sched_info.ctx.jsctx_mutex);
					mutex_unlock(&js_devdata->queue_mutex);
					up(&js_devdata->schedule_sem);
					return;
				}
				kbase_ctx_flag_set(kctx, KCTX_ACTIVE);
			}

			if (!kbase_js_use_ctx(kbdev, kctx)) {
				mutex_lock(
					&kctx->jctx.sched_info.ctx.jsctx_mutex);
				/* Context can not be used at this time */
				spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
				if (kbase_js_ctx_pullable(kctx, js, false)
				    || kbase_ctx_flag(kctx, KCTX_PRIVILEGED))
					timer_sync |=
					kbase_js_ctx_list_add_pullable_head_nolock(
							kctx->kbdev, kctx, js);
				else
					timer_sync |=
					kbase_js_ctx_list_add_unpullable_nolock(
							kctx->kbdev, kctx, js);
				spin_unlock_irqrestore(&kbdev->hwaccess_lock,
						flags);
				mutex_unlock(
					&kctx->jctx.sched_info.ctx.jsctx_mutex);
				if (context_idle) {
					WARN_ON(!kbase_ctx_flag(kctx, KCTX_ACTIVE));
					kbase_ctx_flag_clear(kctx, KCTX_ACTIVE);
					kbase_pm_context_idle(kbdev);
				}

				/* No more jobs can be submitted on this slot */
				js_mask &= ~(1 << js);
				break;
			}
			mutex_lock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

			kbase_ctx_flag_clear(kctx, KCTX_PULLED);

			if (!kbase_jm_kick(kbdev, 1 << js))
				/* No more jobs can be submitted on this slot */
				js_mask &= ~(1 << js);

			if (!kbase_ctx_flag(kctx, KCTX_PULLED)) {
				/* Failed to pull jobs - push to head of list */
				if (kbase_js_ctx_pullable(kctx, js, true))
					timer_sync |=
					kbase_js_ctx_list_add_pullable_head_nolock(
								kctx->kbdev,
								kctx, js);
				else
					timer_sync |=
					kbase_js_ctx_list_add_unpullable_nolock(
								kctx->kbdev,
								kctx, js);

				if (context_idle) {
					kbase_jm_idle_ctx(kbdev, kctx);
					spin_unlock_irqrestore(
							&kbdev->hwaccess_lock,
							flags);
					WARN_ON(!kbase_ctx_flag(kctx, KCTX_ACTIVE));
					kbase_ctx_flag_clear(kctx, KCTX_ACTIVE);
					kbase_pm_context_idle(kbdev);
				} else {
					spin_unlock_irqrestore(
							&kbdev->hwaccess_lock,
							flags);
				}
				mutex_unlock(
					&kctx->jctx.sched_info.ctx.jsctx_mutex);

				js_mask &= ~(1 << js);
				break; /* Could not run atoms on this slot */
			}

			/* Push to back of list */
			if (kbase_js_ctx_pullable(kctx, js, true))
				timer_sync |=
					kbase_js_ctx_list_add_pullable_nolock(
							kctx->kbdev, kctx, js);
			else
				timer_sync |=
					kbase_js_ctx_list_add_unpullable_nolock(
							kctx->kbdev, kctx, js);

			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			mutex_unlock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
		}
	}

	if (timer_sync)
		kbase_js_sync_timers(kbdev);

	mutex_unlock(&js_devdata->queue_mutex);
	up(&js_devdata->schedule_sem);
}

void kbase_js_zap_context(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
	struct kbasep_js_kctx_info *js_kctx_info = &kctx->jctx.sched_info;
	int js;

	/*
	 * Critical assumption: No more submission is possible outside of the
	 * workqueue. This is because the OS *must* prevent U/K calls (IOCTLs)
	 * whilst the struct kbase_context is terminating.
	 */

	/* First, atomically do the following:
	 * - mark the context as dying
	 * - try to evict it from the policy queue */
	mutex_lock(&kctx->jctx.lock);
	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	kbase_ctx_flag_set(kctx, KCTX_DYING);

	dev_dbg(kbdev->dev, "Zap: Try Evict Ctx %p", kctx);

	/*
	 * At this point we know:
	 * - If eviction succeeded, it was in the policy queue, but now no
	 *   longer is
	 *  - We must cancel the jobs here. No Power Manager active reference to
	 *    release.
	 *  - This happens asynchronously - kbase_jd_zap_context() will wait for
	 *    those jobs to be killed.
	 * - If eviction failed, then it wasn't in the policy queue. It is one
	 *   of the following:
	 *  - a. it didn't have any jobs, and so is not in the Policy Queue or
	 *       the Run Pool (not scheduled)
	 *   - Hence, no more work required to cancel jobs. No Power Manager
	 *     active reference to release.
	 *  - b. it was in the middle of a scheduling transaction (and thus must
	 *       have at least 1 job). This can happen from a syscall or a
	 *       kernel thread. We still hold the jsctx_mutex, and so the thread
	 *       must be waiting inside kbasep_js_try_schedule_head_ctx(),
	 *       before checking whether the runpool is full. That thread will
	 *       continue after we drop the mutex, and will notice the context
	 *       is dying. It will rollback the transaction, killing all jobs at
	 *       the same time. kbase_jd_zap_context() will wait for those jobs
	 *       to be killed.
	 *   - Hence, no more work required to cancel jobs, or to release the
	 *     Power Manager active reference.
	 *  - c. it is scheduled, and may or may not be running jobs
	 * - We must cause it to leave the runpool by stopping it from
	 * submitting any more jobs. When it finally does leave,
	 * kbasep_js_runpool_requeue_or_kill_ctx() will kill all remaining jobs
	 * (because it is dying), release the Power Manager active reference,
	 * and will not requeue the context in the policy queue.
	 * kbase_jd_zap_context() will wait for those jobs to be killed.
	 *  - Hence, work required just to make it leave the runpool. Cancelling
	 *    jobs and releasing the Power manager active reference will be
	 *    handled when it leaves the runpool.
	 */
	if (!kbase_ctx_flag(kctx, KCTX_SCHEDULED)) {
		for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
			if (!list_empty(
				&kctx->jctx.sched_info.ctx.ctx_list_entry[js]))
				list_del_init(
				&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);
		}

		/* The following events require us to kill off remaining jobs
		 * and update PM book-keeping:
		 * - we evicted it correctly (it must have jobs to be in the
		 *   Policy Queue)
		 *
		 * These events need no action, but take this path anyway:
		 * - Case a: it didn't have any jobs, and was never in the Queue
		 * - Case b: scheduling transaction will be partially rolled-
		 *           back (this already cancels the jobs)
		 */

		KBASE_TRACE_ADD(kbdev, JM_ZAP_NON_SCHEDULED, kctx, NULL, 0u,
						kbase_ctx_flag(kctx, KCTX_SCHEDULED));

		dev_dbg(kbdev->dev, "Zap: Ctx %p scheduled=0", kctx);

		/* Only cancel jobs when we evicted from the policy
		 * queue. No Power Manager active reference was held.
		 *
		 * Having is_dying set ensures that this kills, and
		 * doesn't requeue */
		kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx, false);

		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);
		mutex_unlock(&kctx->jctx.lock);
	} else {
		unsigned long flags;
		bool was_retained;

		/* Case c: didn't evict, but it is scheduled - it's in the Run
		 * Pool */
		KBASE_TRACE_ADD(kbdev, JM_ZAP_SCHEDULED, kctx, NULL, 0u,
						kbase_ctx_flag(kctx, KCTX_SCHEDULED));
		dev_dbg(kbdev->dev, "Zap: Ctx %p is in RunPool", kctx);

		/* Disable the ctx from submitting any more jobs */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

		kbasep_js_clear_submit_allowed(js_devdata, kctx);

		/* Retain and (later) release the context whilst it is is now
		 * disallowed from submitting jobs - ensures that someone
		 * somewhere will be removing the context later on */
		was_retained = kbasep_js_runpool_retain_ctx_nolock(kbdev, kctx);

		/* Since it's scheduled and we have the jsctx_mutex, it must be
		 * retained successfully */
		KBASE_DEBUG_ASSERT(was_retained);

		dev_dbg(kbdev->dev, "Zap: Ctx %p Kill Any Running jobs", kctx);

		/* Cancel any remaining running jobs for this kctx - if any.
		 * Submit is disallowed which takes effect immediately, so no
		 * more new jobs will appear after we do this. */
		for (js = 0; js < kbdev->gpu_props.num_job_slots; js++)
			kbase_job_slot_hardstop(kctx, js, NULL);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);
		mutex_unlock(&kctx->jctx.lock);

		dev_dbg(kbdev->dev, "Zap: Ctx %p Release (may or may not schedule out immediately)",
									kctx);

		kbasep_js_runpool_release_ctx(kbdev, kctx);
	}

	KBASE_TRACE_ADD(kbdev, JM_ZAP_DONE, kctx, NULL, 0u, 0u);

	/* After this, you must wait on both the
	 * kbase_jd_context::zero_jobs_wait and the
	 * kbasep_js_kctx_info::ctx::is_scheduled_waitq - to wait for the jobs
	 * to be destroyed, and the context to be de-scheduled (if it was on the
	 * runpool).
	 *
	 * kbase_jd_zap_context() will do this. */
}

static inline int trace_get_refcnt(struct kbase_device *kbdev,
					struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	int as_nr;
	int refcnt = 0;

	js_devdata = &kbdev->js_data;

	as_nr = kctx->as_nr;
	if (as_nr != KBASEP_AS_NR_INVALID) {
		struct kbasep_js_per_as_data *js_per_as_data;

		js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

		refcnt = js_per_as_data->as_busy_refcount;
	}

	return refcnt;
}

/**
 * kbase_js_foreach_ctx_job(): - Call a function on all jobs in context
 * @kctx:     Pointer to context.
 * @callback: Pointer to function to call for each job.
 *
 * Call a function on all jobs belonging to a non-queued, non-running
 * context, and detach the jobs from the context as it goes.
 *
 * Due to the locks that might be held at the time of the call, the callback
 * may need to defer work on a workqueue to complete its actions (e.g. when
 * cancelling jobs)
 *
 * Atoms will be removed from the queue, so this must only be called when
 * cancelling jobs (which occurs as part of context destruction).
 *
 * The locking conditions on the caller are as follows:
 * - it will be holding kbasep_js_kctx_info::ctx::jsctx_mutex.
 */
static void kbase_js_foreach_ctx_job(struct kbase_context *kctx,
		kbasep_js_policy_ctx_job_cb callback)
{
	struct kbase_device *kbdev;
	struct kbasep_js_device_data *js_devdata;
	unsigned long flags;
	u32 js;

	kbdev = kctx->kbdev;
	js_devdata = &kbdev->js_data;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_POLICY_FOREACH_CTX_JOBS, kctx, NULL,
					0u, trace_get_refcnt(kbdev, kctx));

	/* Invoke callback on jobs on each slot in turn */
	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++)
		jsctx_queue_foreach(kctx, js, callback);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}
