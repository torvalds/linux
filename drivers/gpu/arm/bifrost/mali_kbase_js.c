// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2011-2022 ARM Limited. All rights reserved.
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

/*
 * Job Scheduler Implementation
 */
#include <mali_kbase.h>
#include <mali_kbase_js.h>
#include <tl/mali_kbase_tracepoints.h>
#include <mali_linux_trace.h>
#include <mali_kbase_hw.h>
#include <mali_kbase_ctx_sched.h>

#include <mali_kbase_defs.h>
#include <mali_kbase_config_defaults.h>

#include "mali_kbase_jm.h"
#include "mali_kbase_hwaccess_jm.h"
#include <linux/priority_control_manager.h>

/*
 * Private types
 */

/* Bitpattern indicating the result of releasing a context */
enum {
	/* The context was descheduled - caller should try scheduling in a new
	 * one to keep the runpool full
	 */
	KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED = (1u << 0),
	/* Ctx attributes were changed - caller should try scheduling all
	 * contexts
	 */
	KBASEP_JS_RELEASE_RESULT_SCHED_ALL = (1u << 1)
};

typedef u32 kbasep_js_release_result;

const int kbasep_js_atom_priority_to_relative[BASE_JD_NR_PRIO_LEVELS] = {
	KBASE_JS_ATOM_SCHED_PRIO_MED,      /* BASE_JD_PRIO_MEDIUM */
	KBASE_JS_ATOM_SCHED_PRIO_HIGH,     /* BASE_JD_PRIO_HIGH */
	KBASE_JS_ATOM_SCHED_PRIO_LOW,      /* BASE_JD_PRIO_LOW */
	KBASE_JS_ATOM_SCHED_PRIO_REALTIME  /* BASE_JD_PRIO_REALTIME */
};

const base_jd_prio
kbasep_js_relative_priority_to_atom[KBASE_JS_ATOM_SCHED_PRIO_COUNT] = {
	BASE_JD_PRIO_REALTIME,   /* KBASE_JS_ATOM_SCHED_PRIO_REALTIME */
	BASE_JD_PRIO_HIGH,       /* KBASE_JS_ATOM_SCHED_PRIO_HIGH */
	BASE_JD_PRIO_MEDIUM,     /* KBASE_JS_ATOM_SCHED_PRIO_MED */
	BASE_JD_PRIO_LOW         /* KBASE_JS_ATOM_SCHED_PRIO_LOW */
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
				     kbasep_js_ctx_job_cb *callback);

/* Helper for ktrace */
#if KBASE_KTRACE_ENABLE
static int kbase_ktrace_get_ctx_refcnt(struct kbase_context *kctx)
{
	return atomic_read(&kctx->refcount);
}
#else /* KBASE_KTRACE_ENABLE  */
static int kbase_ktrace_get_ctx_refcnt(struct kbase_context *kctx)
{
	CSTD_UNUSED(kctx);
	return 0;
}
#endif /* KBASE_KTRACE_ENABLE  */

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
	bool none_to_pull;
	struct jsctx_queue *rb = &kctx->jsctx_queue[prio][js];

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	none_to_pull = RB_EMPTY_ROOT(&rb->runnable_tree);

	dev_dbg(kctx->kbdev->dev,
		"Slot %d (prio %d) is %spullable in kctx %pK\n",
		js, prio, none_to_pull ? "not " : "", kctx);

	return none_to_pull;
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

	for (prio = KBASE_JS_ATOM_SCHED_PRIO_FIRST;
		prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT; prio++) {
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
static void jsctx_queue_foreach_prio(struct kbase_context *kctx, int js,
				     int prio, kbasep_js_ctx_job_cb *callback)
{
	struct jsctx_queue *queue = &kctx->jsctx_queue[prio][js];

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	while (!RB_EMPTY_ROOT(&queue->runnable_tree)) {
		struct rb_node *node = rb_first(&queue->runnable_tree);
		struct kbase_jd_atom *entry = rb_entry(node,
				struct kbase_jd_atom, runnable_tree_node);

		rb_erase(node, &queue->runnable_tree);
		callback(kctx->kbdev, entry);

		/* Runnable end-of-renderpass atoms can also be in the linked
		 * list of atoms blocked on cross-slot dependencies. Remove them
		 * to avoid calling the callback twice.
		 */
		if (entry->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST) {
			WARN_ON(!(entry->core_req &
				BASE_JD_REQ_END_RENDERPASS));
			dev_dbg(kctx->kbdev->dev,
				"Del runnable atom %pK from X_DEP list\n",
				(void *)entry);

			list_del(&entry->queue);
			entry->atom_flags &=
					~KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST;
		}
	}

	while (!list_empty(&queue->x_dep_head)) {
		struct kbase_jd_atom *entry = list_entry(queue->x_dep_head.next,
				struct kbase_jd_atom, queue);

		WARN_ON(!(entry->atom_flags &
			KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST));
		dev_dbg(kctx->kbdev->dev,
			"Del blocked atom %pK from X_DEP list\n",
			(void *)entry);

		list_del(queue->x_dep_head.next);
		entry->atom_flags &=
				~KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST;

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
static inline void jsctx_queue_foreach(struct kbase_context *kctx, int js,
				       kbasep_js_ctx_job_cb *callback)
{
	int prio;

	for (prio = KBASE_JS_ATOM_SCHED_PRIO_FIRST;
		prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT; prio++)
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
	dev_dbg(kctx->kbdev->dev,
		"Peeking runnable tree of kctx %pK for prio %d (s:%d)\n",
		(void *)kctx, prio, js);

	node = rb_first(&rb->runnable_tree);
	if (!node) {
		dev_dbg(kctx->kbdev->dev, "Tree is empty\n");
		return NULL;
	}

	return rb_entry(node, struct kbase_jd_atom, runnable_tree_node);
}

/**
 * jsctx_rb_peek(): - Check all priority buffers and get next atom
 * @kctx: Pointer to kbase context with ring buffer.
 * @js:   Job slot id to check.
 *
 * Check the ring buffers for all priorities, starting from
 * KBASE_JS_ATOM_SCHED_PRIO_REALTIME, for the specified @js and @prio and return a
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

	for (prio = KBASE_JS_ATOM_SCHED_PRIO_FIRST;
		prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT; prio++) {
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

	dev_dbg(kctx->kbdev->dev, "Erasing atom %pK from runnable tree of kctx %pK\n",
		(void *)katom, (void *)kctx);

	/* Atoms must be pulled in the correct order. */
	WARN_ON(katom != jsctx_rb_peek_prio(kctx, js, prio));

	rb_erase(&katom->runnable_tree_node, &rb->runnable_tree);
}

static void
jsctx_tree_add(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	struct kbase_device *kbdev = kctx->kbdev;
	int prio = katom->sched_priority;
	int js = katom->slot_nr;
	struct jsctx_queue *queue = &kctx->jsctx_queue[prio][js];
	struct rb_node **new = &(queue->runnable_tree.rb_node), *parent = NULL;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	dev_dbg(kbdev->dev, "Adding atom %pK to runnable tree of kctx %pK (s:%d)\n",
		(void *)katom, (void *)kctx, js);

	while (*new) {
		struct kbase_jd_atom *entry = container_of(*new,
				struct kbase_jd_atom, runnable_tree_node);

		parent = *new;
		if (kbase_jd_atom_is_younger(katom, entry))
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&katom->runnable_tree_node, parent, new);
	rb_insert_color(&katom->runnable_tree_node, &queue->runnable_tree);

	KBASE_TLSTREAM_TL_ATTRIB_ATOM_STATE(kbdev, katom, TL_ATOM_STATE_READY);
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

	KBASE_KTRACE_ADD_JM(kctx->kbdev, JS_UNPULL_JOB, kctx, katom, katom->jc,
			    0u);

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

typedef bool(katom_ordering_func)(const struct kbase_jd_atom *,
				  const struct kbase_jd_atom *);

bool kbase_js_atom_runs_before(struct kbase_device *kbdev,
			       const struct kbase_jd_atom *katom_a,
			       const struct kbase_jd_atom *katom_b,
			       const kbase_atom_ordering_flag_t order_flags)
{
	struct kbase_context *kctx_a = katom_a->kctx;
	struct kbase_context *kctx_b = katom_b->kctx;
	katom_ordering_func *samectxatomprio_ordering_func =
		kbase_jd_atom_is_younger;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (order_flags & KBASE_ATOM_ORDERING_FLAG_SEQNR)
		samectxatomprio_ordering_func = kbase_jd_atom_is_earlier;

	/* It only makes sense to make this test for atoms on the same slot */
	WARN_ON(katom_a->slot_nr != katom_b->slot_nr);

	if (kbdev->js_ctx_scheduling_mode ==
	    KBASE_JS_PROCESS_LOCAL_PRIORITY_MODE) {
		/* In local priority mode, querying either way around for "a
		 * should run before b" and "b should run before a" should
		 * always be false when they're from different contexts
		 */
		if (kctx_a != kctx_b)
			return false;
	} else {
		/* In system priority mode, ordering is done first strictly by
		 * context priority, even when katom_b might be lower priority
		 * than katom_a. This is due to scheduling of contexts in order
		 * of highest priority first, regardless of whether the atoms
		 * for a particular slot from such contexts have the highest
		 * priority or not.
		 */
		if (kctx_a != kctx_b) {
			if (kctx_a->priority < kctx_b->priority)
				return true;
			if (kctx_a->priority > kctx_b->priority)
				return false;
		}
	}

	/* For same contexts/contexts with the same context priority (in system
	 * priority mode), ordering is next done by atom priority
	 */
	if (katom_a->sched_priority < katom_b->sched_priority)
		return true;
	if (katom_a->sched_priority > katom_b->sched_priority)
		return false;
	/* For atoms of same priority on the same kctx, they are
	 * ordered by seq_nr/age (dependent on caller)
	 */
	if (kctx_a == kctx_b && samectxatomprio_ordering_func(katom_a, katom_b))
		return true;

	return false;
}

/*
 * Functions private to KBase ('Protected' functions)
 */
int kbasep_js_devdata_init(struct kbase_device * const kbdev)
{
	struct kbasep_js_device_data *jsdd;
	int i, j;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	jsdd = &kbdev->js_data;

#ifdef CONFIG_MALI_BIFROST_DEBUG
	/* Soft-stop will be disabled on a single context by default unless
	 * softstop_always is set
	 */
	jsdd->softstop_always = false;
#endif				/* CONFIG_MALI_BIFROST_DEBUG */
	jsdd->nr_all_contexts_running = 0;
	jsdd->nr_user_contexts_running = 0;
	jsdd->nr_contexts_pullable = 0;
	atomic_set(&jsdd->nr_contexts_runnable, 0);
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
	jsdd->hard_stop_ticks_ss = DEFAULT_JS_HARD_STOP_TICKS_SS;
	jsdd->hard_stop_ticks_cl = DEFAULT_JS_HARD_STOP_TICKS_CL;
	jsdd->hard_stop_ticks_dumping = DEFAULT_JS_HARD_STOP_TICKS_DUMPING;
	jsdd->gpu_reset_ticks_ss = DEFAULT_JS_RESET_TICKS_SS;
	jsdd->gpu_reset_ticks_cl = DEFAULT_JS_RESET_TICKS_CL;

	jsdd->gpu_reset_ticks_dumping = DEFAULT_JS_RESET_TICKS_DUMPING;
	jsdd->ctx_timeslice_ns = DEFAULT_JS_CTX_TIMESLICE_NS;
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
	dev_dbg(kbdev->dev, "Job Scheduling Soft-stops disabled, ignoring value for soft_stop_ticks==%u at %uns per tick. Other soft-stops may still occur.",
			jsdd->soft_stop_ticks,
			jsdd->scheduling_period_ns);
#endif
#if KBASE_DISABLE_SCHEDULING_HARD_STOPS
	dev_dbg(kbdev->dev, "Job Scheduling Hard-stops disabled, ignoring values for hard_stop_ticks_ss==%d and hard_stop_ticks_dumping==%u at %uns per tick. Other hard-stops may still occur.",
			jsdd->hard_stop_ticks_ss,
			jsdd->hard_stop_ticks_dumping,
			jsdd->scheduling_period_ns);
#endif
#if KBASE_DISABLE_SCHEDULING_SOFT_STOPS && KBASE_DISABLE_SCHEDULING_HARD_STOPS
	dev_dbg(kbdev->dev, "Note: The JS tick timer (if coded) will still be run, but do nothing.");
#endif

	for (i = 0; i < kbdev->gpu_props.num_job_slots; ++i)
		jsdd->js_reqs[i] = core_reqs_from_jsn_features(
			kbdev->gpu_props.props.raw_props.js_features[i]);

	/* On error, we could continue on: providing none of the below resources
	 * rely on the ones above
	 */

	mutex_init(&jsdd->runpool_mutex);
	mutex_init(&jsdd->queue_mutex);
	sema_init(&jsdd->schedule_sem, 1);

	for (i = 0; i < kbdev->gpu_props.num_job_slots; ++i) {
		for (j = KBASE_JS_ATOM_SCHED_PRIO_FIRST; j < KBASE_JS_ATOM_SCHED_PRIO_COUNT; ++j) {
			INIT_LIST_HEAD(&jsdd->ctx_list_pullable[i][j]);
			INIT_LIST_HEAD(&jsdd->ctx_list_unpullable[i][j]);
		}
	}

	return 0;
}

void kbasep_js_devdata_halt(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbasep_js_devdata_term(struct kbase_device *kbdev)
{
	struct kbasep_js_device_data *js_devdata;
	s8 zero_ctx_attr_ref_count[KBASEP_JS_CTX_ATTR_COUNT] = { 0, };
	CSTD_UNUSED(js_devdata);

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_devdata = &kbdev->js_data;

	/* The caller must de-register all contexts before calling this
	 */
	KBASE_DEBUG_ASSERT(js_devdata->nr_all_contexts_running == 0);
	KBASE_DEBUG_ASSERT(memcmp(
				  js_devdata->runpool_irq.ctx_attr_ref_count,
				  zero_ctx_attr_ref_count,
				  sizeof(zero_ctx_attr_ref_count)) == 0);
	CSTD_UNUSED(zero_ctx_attr_ref_count);
}

int kbasep_js_kctx_init(struct kbase_context *const kctx)
{
	struct kbasep_js_kctx_info *js_kctx_info;
	int i, j;
	CSTD_UNUSED(js_kctx_info);

	KBASE_DEBUG_ASSERT(kctx != NULL);

	for (i = 0; i < BASE_JM_MAX_NR_SLOTS; ++i)
		INIT_LIST_HEAD(&kctx->jctx.sched_info.ctx.ctx_list_entry[i]);

	js_kctx_info = &kctx->jctx.sched_info;

	kctx->slots_pullable = 0;
	js_kctx_info->ctx.nr_jobs = 0;
	kbase_ctx_flag_clear(kctx, KCTX_SCHEDULED);
	kbase_ctx_flag_clear(kctx, KCTX_DYING);
	memset(js_kctx_info->ctx.ctx_attr_ref_count, 0,
			sizeof(js_kctx_info->ctx.ctx_attr_ref_count));

	/* Initially, the context is disabled from submission until the create
	 * flags are set
	 */
	kbase_ctx_flag_set(kctx, KCTX_SUBMIT_DISABLED);

	/* On error, we could continue on: providing none of the below resources
	 * rely on the ones above
	 */
	mutex_init(&js_kctx_info->ctx.jsctx_mutex);

	init_waitqueue_head(&js_kctx_info->ctx.is_scheduled_wait);

	for (i = KBASE_JS_ATOM_SCHED_PRIO_FIRST; i < KBASE_JS_ATOM_SCHED_PRIO_COUNT; i++) {
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
	int js;
	bool update_ctx_count = false;
	unsigned long flags;
	CSTD_UNUSED(js_kctx_info);

	KBASE_DEBUG_ASSERT(kctx != NULL);

	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_kctx_info = &kctx->jctx.sched_info;

	/* The caller must de-register all jobs before calling this */
	KBASE_DEBUG_ASSERT(!kbase_ctx_flag(kctx, KCTX_SCHEDULED));
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.nr_jobs == 0);

	mutex_lock(&kbdev->js_data.queue_mutex);
	mutex_lock(&kctx->jctx.sched_info.ctx.jsctx_mutex);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++)
		list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF)) {
		WARN_ON(atomic_read(&kbdev->js_data.nr_contexts_runnable) <= 0);
		atomic_dec(&kbdev->js_data.nr_contexts_runnable);
		update_ctx_count = true;
		kbase_ctx_flag_clear(kctx, KCTX_RUNNABLE_REF);
	}

	mutex_unlock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	mutex_unlock(&kbdev->js_data.queue_mutex);

	if (update_ctx_count) {
		mutex_lock(&kbdev->js_data.runpool_mutex);
		kbase_backend_ctx_count_changed(kbdev);
		mutex_unlock(&kbdev->js_data.runpool_mutex);
	}
}

/*
 * Priority blocking management functions
 */

/* Should not normally use directly - use kbase_jsctx_slot_atom_pulled_dec() instead */
static void kbase_jsctx_slot_prio_blocked_clear(struct kbase_context *kctx,
						int js, int sched_prio)
{
	struct kbase_jsctx_slot_tracking *slot_tracking =
		&kctx->slot_tracking[js];

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	slot_tracking->blocked &= ~(((kbase_js_prio_bitmap_t)1) << sched_prio);
	KBASE_KTRACE_ADD_JM_SLOT_INFO(kctx->kbdev, JS_SLOT_PRIO_UNBLOCKED, kctx,
				      NULL, 0, js, (unsigned int)sched_prio);
}

static int kbase_jsctx_slot_atoms_pulled(struct kbase_context *kctx, int js)
{
	return atomic_read(&kctx->slot_tracking[js].atoms_pulled);
}

/*
 * A priority level on a slot is blocked when:
 * - that priority level is blocked
 * - or, any higher priority level is blocked
 */
static bool kbase_jsctx_slot_prio_is_blocked(struct kbase_context *kctx, int js,
					     int sched_prio)
{
	struct kbase_jsctx_slot_tracking *slot_tracking =
		&kctx->slot_tracking[js];
	kbase_js_prio_bitmap_t prio_bit, higher_prios_mask;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	/* done in two separate shifts to prevent future undefined behavior
	 * should the number of priority levels == (bit width of the type)
	 */
	prio_bit = (((kbase_js_prio_bitmap_t)1) << sched_prio);
	/* all bits of sched_prio or higher, with sched_prio = 0 being the
	 * highest priority
	 */
	higher_prios_mask = (prio_bit << 1) - 1u;
	return (slot_tracking->blocked & higher_prios_mask) != 0u;
}

/**
 * kbase_jsctx_slot_atom_pulled_inc - Increase counts of atoms that have being
 *                                    pulled for a slot from a ctx, based on
 *                                    this atom
 * @kctx: kbase context
 * @katom: atom pulled
 *
 * Manages counts of atoms pulled (including per-priority-level counts), for
 * later determining when a ctx can become unblocked on a slot.
 *
 * Once a slot has been blocked at @katom's priority level, it should not be
 * pulled from, hence this function should not be called in that case.
 *
 * The return value is to aid tracking of when @kctx becomes runnable.
 *
 * Return: new total count of atoms pulled from all slots on @kctx
 */
static int kbase_jsctx_slot_atom_pulled_inc(struct kbase_context *kctx,
					    const struct kbase_jd_atom *katom)
{
	int js = katom->slot_nr;
	int sched_prio = katom->sched_priority;
	struct kbase_jsctx_slot_tracking *slot_tracking =
		&kctx->slot_tracking[js];
	int nr_atoms_pulled;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	WARN(kbase_jsctx_slot_prio_is_blocked(kctx, js, sched_prio),
	     "Should not have pulled atoms for slot %d from a context that is blocked at priority %d or higher",
	     js, sched_prio);

	nr_atoms_pulled = atomic_inc_return(&kctx->atoms_pulled_all_slots);
	atomic_inc(&slot_tracking->atoms_pulled);
	slot_tracking->atoms_pulled_pri[sched_prio]++;

	return nr_atoms_pulled;
}

/**
 * kbase_jsctx_slot_atom_pulled_dec- Decrease counts of atoms that have being
 *                                   pulled for a slot from a ctx, and
 *                                   re-evaluate whether a context is blocked
 *                                   on this slot
 * @kctx: kbase context
 * @katom: atom that has just been removed from a job slot
 *
 * @kctx can become unblocked on a slot for a priority level when it no longer
 * has any pulled atoms at that priority level on that slot, and all higher
 * (numerically lower) priority levels are also unblocked @kctx on that
 * slot. The latter condition is to retain priority ordering within @kctx.
 *
 * Return: true if the slot was previously blocked but has now become unblocked
 * at @katom's priority level, false otherwise.
 */
static bool kbase_jsctx_slot_atom_pulled_dec(struct kbase_context *kctx,
					     const struct kbase_jd_atom *katom)
{
	int js = katom->slot_nr;
	int sched_prio = katom->sched_priority;
	int atoms_pulled_pri;
	struct kbase_jsctx_slot_tracking *slot_tracking =
		&kctx->slot_tracking[js];
	bool slot_prio_became_unblocked = false;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	atomic_dec(&kctx->atoms_pulled_all_slots);
	atomic_dec(&slot_tracking->atoms_pulled);

	atoms_pulled_pri = --(slot_tracking->atoms_pulled_pri[sched_prio]);

	/* We can safely clear this priority level's blocked status even if
	 * higher priority levels are still blocked: a subsequent query to
	 * kbase_jsctx_slot_prio_is_blocked() will still return true
	 */
	if (!atoms_pulled_pri &&
	    kbase_jsctx_slot_prio_is_blocked(kctx, js, sched_prio)) {
		kbase_jsctx_slot_prio_blocked_clear(kctx, js, sched_prio);

		if (!kbase_jsctx_slot_prio_is_blocked(kctx, js, sched_prio))
			slot_prio_became_unblocked = true;
	}

	if (slot_prio_became_unblocked)
		KBASE_KTRACE_ADD_JM_SLOT_INFO(kctx->kbdev,
					      JS_SLOT_PRIO_AND_HIGHER_UNBLOCKED,
					      kctx, katom, katom->jc, js,
					      (unsigned int)sched_prio);

	return slot_prio_became_unblocked;
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
	dev_dbg(kbdev->dev, "Add pullable tail kctx %pK (s:%d)\n",
		(void *)kctx, js);

	if (!list_empty(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]))
		list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);

	list_add_tail(&kctx->jctx.sched_info.ctx.ctx_list_entry[js],
			&kbdev->js_data.ctx_list_pullable[js][kctx->priority]);

	if (!kctx->slots_pullable) {
		kbdev->js_data.nr_contexts_pullable++;
		ret = true;
		if (!kbase_jsctx_atoms_pulled(kctx)) {
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
	dev_dbg(kbdev->dev, "Add pullable head kctx %pK (s:%d)\n",
		(void *)kctx, js);

	if (!list_empty(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]))
		list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);

	list_add(&kctx->jctx.sched_info.ctx.ctx_list_entry[js],
			&kbdev->js_data.ctx_list_pullable[js][kctx->priority]);

	if (!kctx->slots_pullable) {
		kbdev->js_data.nr_contexts_pullable++;
		ret = true;
		if (!kbase_jsctx_atoms_pulled(kctx)) {
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
	dev_dbg(kbdev->dev, "Add unpullable tail kctx %pK (s:%d)\n",
		(void *)kctx, js);

	list_move_tail(&kctx->jctx.sched_info.ctx.ctx_list_entry[js],
		&kbdev->js_data.ctx_list_unpullable[js][kctx->priority]);

	if (kctx->slots_pullable == (1 << js)) {
		kbdev->js_data.nr_contexts_pullable--;
		ret = true;
		if (!kbase_jsctx_atoms_pulled(kctx)) {
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
		if (!kbase_jsctx_atoms_pulled(kctx)) {
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
	int i;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (i = KBASE_JS_ATOM_SCHED_PRIO_FIRST; i < KBASE_JS_ATOM_SCHED_PRIO_COUNT; i++) {
		if (list_empty(&kbdev->js_data.ctx_list_pullable[js][i]))
			continue;

		kctx = list_entry(kbdev->js_data.ctx_list_pullable[js][i].next,
				struct kbase_context,
				jctx.sched_info.ctx.ctx_list_entry[js]);

		list_del_init(&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);
		dev_dbg(kbdev->dev,
			"Popped %pK from the pullable queue (s:%d)\n",
			(void *)kctx, js);
		return kctx;
	}
	return NULL;
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
	struct kbase_device *kbdev = kctx->kbdev;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	js_devdata = &kbdev->js_data;

	if (is_scheduled) {
		if (!kbasep_js_is_submit_allowed(js_devdata, kctx)) {
			dev_dbg(kbdev->dev, "JS: No submit allowed for kctx %pK\n",
				(void *)kctx);
			return false;
		}
	}
	katom = jsctx_rb_peek(kctx, js);
	if (!katom) {
		dev_dbg(kbdev->dev, "JS: No pullable atom in kctx %pK (s:%d)\n",
			(void *)kctx, js);
		return false; /* No pullable atoms */
	}
	if (kbase_jsctx_slot_prio_is_blocked(kctx, js, katom->sched_priority)) {
		KBASE_KTRACE_ADD_JM_SLOT_INFO(
			kctx->kbdev, JS_SLOT_PRIO_IS_BLOCKED, kctx, katom,
			katom->jc, js, (unsigned int)katom->sched_priority);
		dev_dbg(kbdev->dev,
			"JS: kctx %pK is blocked from submitting atoms at priority %d and lower (s:%d)\n",
			(void *)kctx, katom->sched_priority, js);
		return false;
	}
	if (atomic_read(&katom->blocked)) {
		dev_dbg(kbdev->dev, "JS: Atom %pK is blocked in js_ctx_pullable\n",
			(void *)katom);
		return false; /* next atom blocked */
	}
	if (kbase_js_atom_blocked_on_x_dep(katom)) {
		if (katom->x_pre_dep->gpu_rb_state ==
				KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB ||
				katom->x_pre_dep->will_fail_event_code) {
			dev_dbg(kbdev->dev,
				"JS: X pre-dep %pK is not present in slot FIFO or will fail\n",
				(void *)katom->x_pre_dep);
			return false;
		}
		if ((katom->atom_flags & KBASE_KATOM_FLAG_FAIL_BLOCKER) &&
			kbase_backend_nr_atoms_on_slot(kctx->kbdev, js)) {
			dev_dbg(kbdev->dev,
				"JS: Atom %pK has cross-slot fail dependency and atoms on slot (s:%d)\n",
				(void *)katom, js);
			return false;
		}
	}

	dev_dbg(kbdev->dev, "JS: Atom %pK is pullable in kctx %pK (s:%d)\n",
		(void *)katom, (void *)kctx, js);

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

			dev_dbg(kbdev->dev,
				"Checking dep %d of atom %pK (s:%d) on %pK (s:%d)\n",
				i, (void *)katom, js, (void *)dep_atom, dep_js);

			/* Dependent atom must already have been submitted */
			if (!(dep_atom->atom_flags &
					KBASE_KATOM_FLAG_JSCTX_IN_TREE)) {
				dev_dbg(kbdev->dev,
					"Blocker not submitted yet\n");
				ret = false;
				break;
			}

			/* Dependencies with different priorities can't
			 * be represented in the ringbuffer
			 */
			if (prio != dep_prio) {
				dev_dbg(kbdev->dev,
					"Different atom priorities\n");
				ret = false;
				break;
			}

			if (js == dep_js) {
				/* Only one same-slot dependency can be
				 * represented in the ringbuffer
				 */
				if (has_dep) {
					dev_dbg(kbdev->dev,
						"Too many same-slot deps\n");
					ret = false;
					break;
				}
				/* Each dependee atom can only have one
				 * same-slot dependency
				 */
				if (dep_atom->post_dep) {
					dev_dbg(kbdev->dev,
						"Too many same-slot successors\n");
					ret = false;
					break;
				}
				has_dep = true;
			} else {
				/* Only one cross-slot dependency can be
				 * represented in the ringbuffer
				 */
				if (has_x_dep) {
					dev_dbg(kbdev->dev,
						"Too many cross-slot deps\n");
					ret = false;
					break;
				}
				/* Each dependee atom can only have one
				 * cross-slot dependency
				 */
				if (dep_atom->x_post_dep) {
					dev_dbg(kbdev->dev,
						"Too many cross-slot successors\n");
					ret = false;
					break;
				}
				/* The dependee atom can not already be in the
				 * HW access ringbuffer
				 */
				if (dep_atom->gpu_rb_state !=
					KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB) {
					dev_dbg(kbdev->dev,
						"Blocker already in ringbuffer (state:%d)\n",
						dep_atom->gpu_rb_state);
					ret = false;
					break;
				}
				/* The dependee atom can not already have
				 * completed
				 */
				if (dep_atom->status !=
						KBASE_JD_ATOM_STATE_IN_JS) {
					dev_dbg(kbdev->dev,
						"Blocker already completed (status:%d)\n",
						dep_atom->status);
					ret = false;
					break;
				}

				has_x_dep = true;
			}

			/* Dependency can be represented in ringbuffers */
		}
	}

	/* If dependencies can be represented by ringbuffer then clear them from
	 * atom structure
	 */
	if (ret) {
		for (i = 0; i < 2; i++) {
			struct kbase_jd_atom *dep_atom = katom->dep[i].atom;

			if (dep_atom) {
				int dep_js = kbase_js_get_slot(kbdev, dep_atom);

				dev_dbg(kbdev->dev,
					"Clearing dep %d of atom %pK (s:%d) on %pK (s:%d)\n",
					i, (void *)katom, js, (void *)dep_atom,
					dep_js);

				if ((js != dep_js) &&
					(dep_atom->status !=
						KBASE_JD_ATOM_STATE_COMPLETED)
					&& (dep_atom->status !=
					KBASE_JD_ATOM_STATE_HW_COMPLETED)
					&& (dep_atom->status !=
						KBASE_JD_ATOM_STATE_UNUSED)) {

					katom->atom_flags |=
						KBASE_KATOM_FLAG_X_DEP_BLOCKED;

					dev_dbg(kbdev->dev, "Set X_DEP flag on atom %pK\n",
						(void *)katom);

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
	} else {
		dev_dbg(kbdev->dev,
			"Deps of atom %pK (s:%d) could not be represented\n",
			(void *)katom, js);
	}

	return ret;
}

void kbase_js_set_ctx_priority(struct kbase_context *kctx, int new_priority)
{
	struct kbase_device *kbdev = kctx->kbdev;
	int js;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* Move kctx to the pullable/upullable list as per the new priority */
	if (new_priority != kctx->priority) {
		for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
			if (kctx->slots_pullable & (1 << js))
				list_move_tail(&kctx->jctx.sched_info.ctx.ctx_list_entry[js],
					&kbdev->js_data.ctx_list_pullable[js][new_priority]);
			else
				list_move_tail(&kctx->jctx.sched_info.ctx.ctx_list_entry[js],
					&kbdev->js_data.ctx_list_unpullable[js][new_priority]);
		}

		kctx->priority = new_priority;
	}
}

void kbase_js_update_ctx_priority(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	int new_priority = KBASE_JS_ATOM_SCHED_PRIO_LOW;
	int prio;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->js_ctx_scheduling_mode == KBASE_JS_SYSTEM_PRIORITY_MODE) {
		/* Determine the new priority for context, as per the priority
		 * of currently in-use atoms.
		 */
		for (prio = KBASE_JS_ATOM_SCHED_PRIO_FIRST;
			prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT; prio++) {
			if (kctx->atoms_count[prio]) {
				new_priority = prio;
				break;
			}
		}
	}

	kbase_js_set_ctx_priority(kctx, new_priority);
}
KBASE_EXPORT_TEST_API(kbase_js_update_ctx_priority);

/**
 * js_add_start_rp() - Add an atom that starts a renderpass to the job scheduler
 * @start_katom: Pointer to the atom to be added.
 * Return: 0 if successful or a negative value on failure.
 */
static int js_add_start_rp(struct kbase_jd_atom *const start_katom)
{
	struct kbase_context *const kctx = start_katom->kctx;
	struct kbase_jd_renderpass *rp;
	struct kbase_device *const kbdev = kctx->kbdev;
	unsigned long flags;

	lockdep_assert_held(&kctx->jctx.lock);

	if (WARN_ON(!(start_katom->core_req & BASE_JD_REQ_START_RENDERPASS)))
		return -EINVAL;

	if (start_katom->core_req & BASE_JD_REQ_END_RENDERPASS)
		return -EINVAL;

	compiletime_assert((1ull << (sizeof(start_katom->renderpass_id) * 8)) <=
			ARRAY_SIZE(kctx->jctx.renderpasses),
			"Should check invalid access to renderpasses");

	rp = &kctx->jctx.renderpasses[start_katom->renderpass_id];

	if (rp->state != KBASE_JD_RP_COMPLETE)
		return -EINVAL;

	dev_dbg(kctx->kbdev->dev, "JS add start atom %pK of RP %d\n",
		(void *)start_katom, start_katom->renderpass_id);

	/* The following members are read when updating the job slot
	 * ringbuffer/fifo therefore they require additional locking.
	 */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	rp->state = KBASE_JD_RP_START;
	rp->start_katom = start_katom;
	rp->end_katom = NULL;
	INIT_LIST_HEAD(&rp->oom_reg_list);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return 0;
}

/**
 * js_add_end_rp() - Add an atom that ends a renderpass to the job scheduler
 * @end_katom: Pointer to the atom to be added.
 * Return: 0 if successful or a negative value on failure.
 */
static int js_add_end_rp(struct kbase_jd_atom *const end_katom)
{
	struct kbase_context *const kctx = end_katom->kctx;
	struct kbase_jd_renderpass *rp;
	struct kbase_device *const kbdev = kctx->kbdev;

	lockdep_assert_held(&kctx->jctx.lock);

	if (WARN_ON(!(end_katom->core_req & BASE_JD_REQ_END_RENDERPASS)))
		return -EINVAL;

	if (end_katom->core_req & BASE_JD_REQ_START_RENDERPASS)
		return -EINVAL;

	compiletime_assert((1ull << (sizeof(end_katom->renderpass_id) * 8)) <=
			ARRAY_SIZE(kctx->jctx.renderpasses),
			"Should check invalid access to renderpasses");

	rp = &kctx->jctx.renderpasses[end_katom->renderpass_id];

	dev_dbg(kbdev->dev, "JS add end atom %pK in state %d of RP %d\n",
		(void *)end_katom, (int)rp->state, end_katom->renderpass_id);

	if (rp->state == KBASE_JD_RP_COMPLETE)
		return -EINVAL;

	if (rp->end_katom == NULL) {
		/* We can't be in a retry state until the fragment job chain
		 * has completed.
		 */
		unsigned long flags;

		WARN_ON(rp->state == KBASE_JD_RP_RETRY);
		WARN_ON(rp->state == KBASE_JD_RP_RETRY_PEND_OOM);
		WARN_ON(rp->state == KBASE_JD_RP_RETRY_OOM);

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		rp->end_katom = end_katom;
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	} else
		WARN_ON(rp->end_katom != end_katom);

	return 0;
}

bool kbasep_js_add_job(struct kbase_context *kctx,
		struct kbase_jd_atom *atom)
{
	unsigned long flags;
	struct kbasep_js_kctx_info *js_kctx_info;
	struct kbase_device *kbdev;
	struct kbasep_js_device_data *js_devdata;
	int err = 0;

	bool enqueue_required = false;
	bool timer_sync = false;

	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(atom != NULL);
	lockdep_assert_held(&kctx->jctx.lock);

	kbdev = kctx->kbdev;
	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);

	if (atom->core_req & BASE_JD_REQ_START_RENDERPASS)
		err = js_add_start_rp(atom);
	else if (atom->core_req & BASE_JD_REQ_END_RENDERPASS)
		err = js_add_end_rp(atom);

	if (err < 0) {
		atom->event_code = BASE_JD_EVENT_JOB_INVALID;
		atom->status = KBASE_JD_ATOM_STATE_COMPLETED;
		goto out_unlock;
	}

	/*
	 * Begin Runpool transaction
	 */
	mutex_lock(&js_devdata->runpool_mutex);

	/* Refcount ctx.nr_jobs */
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.nr_jobs < U32_MAX);
	++(js_kctx_info->ctx.nr_jobs);
	dev_dbg(kbdev->dev, "Add atom %pK to kctx %pK; now %d in ctx\n",
		(void *)atom, (void *)kctx, js_kctx_info->ctx.nr_jobs);

	/* Lock for state available during IRQ */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (++kctx->atoms_count[atom->sched_priority] == 1)
		kbase_js_update_ctx_priority(kctx);

	if (!kbase_js_dep_validate(kctx, atom)) {
		/* Dependencies could not be represented */
		--(js_kctx_info->ctx.nr_jobs);
		dev_dbg(kbdev->dev,
			"Remove atom %pK from kctx %pK; now %d in ctx\n",
			(void *)atom, (void *)kctx, js_kctx_info->ctx.nr_jobs);

		/* Setting atom status back to queued as it still has unresolved
		 * dependencies
		 */
		atom->status = KBASE_JD_ATOM_STATE_QUEUED;
		dev_dbg(kbdev->dev, "Atom %pK status to queued\n", (void *)atom);

		/* Undo the count, as the atom will get added again later but
		 * leave the context priority adjusted or boosted, in case if
		 * this was the first higher priority atom received for this
		 * context.
		 * This will prevent the scenario of priority inversion, where
		 * another context having medium priority atoms keeps getting
		 * scheduled over this context, which is having both lower and
		 * higher priority atoms, but higher priority atoms are blocked
		 * due to dependency on lower priority atoms. With priority
		 * boost the high priority atom will get to run at earliest.
		 */
		kctx->atoms_count[atom->sched_priority]--;

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&js_devdata->runpool_mutex);

		goto out_unlock;
	}

	enqueue_required = kbase_js_dep_resolved_submit(kctx, atom);

	KBASE_KTRACE_ADD_JM_REFCOUNT(kbdev, JS_ADD_JOB, kctx, atom, atom->jc,
				kbase_ktrace_get_ctx_refcnt(kctx));

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
	 * kick the job manager to attempt to fast-start the atom
	 */
	if (enqueue_required && kctx ==
			kbdev->hwaccess.active_kctx[atom->slot_nr])
		kbase_jm_try_kick(kbdev, 1 << atom->slot_nr);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	if (timer_sync)
		kbase_backend_ctx_count_changed(kbdev);
	mutex_unlock(&js_devdata->runpool_mutex);
	/* End runpool transaction */

	if (!kbase_ctx_flag(kctx, KCTX_SCHEDULED)) {
		if (kbase_ctx_flag(kctx, KCTX_DYING)) {
			/* A job got added while/after kbase_job_zap_context()
			 * was called on a non-scheduled context. Kill that job
			 * by killing the context.
			 */
			kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx,
					false);
		} else if (js_kctx_info->ctx.nr_jobs == 1) {
			/* Handle Refcount going from 0 to 1: schedule the
			 * context on the Queue
			 */
			KBASE_DEBUG_ASSERT(!kbase_ctx_flag(kctx, KCTX_SCHEDULED));
			dev_dbg(kbdev->dev, "JS: Enqueue Context %pK", kctx);

			/* Queue was updated - caller must try to schedule the
			 * head context
			 */
			WARN_ON(!enqueue_required);
		}
	}
out_unlock:
	dev_dbg(kbdev->dev, "Enqueue of kctx %pK is %srequired\n",
		kctx, enqueue_required ? "" : "not ");

	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	mutex_unlock(&js_devdata->queue_mutex);

	return enqueue_required;
}

void kbasep_js_remove_job(struct kbase_device *kbdev,
		struct kbase_context *kctx, struct kbase_jd_atom *atom)
{
	struct kbasep_js_kctx_info *js_kctx_info;
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(atom != NULL);

	js_kctx_info = &kctx->jctx.sched_info;

	KBASE_KTRACE_ADD_JM_REFCOUNT(kbdev, JS_REMOVE_JOB, kctx, atom, atom->jc,
			kbase_ktrace_get_ctx_refcnt(kctx));

	/* De-refcount ctx.nr_jobs */
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.nr_jobs > 0);
	--(js_kctx_info->ctx.nr_jobs);
	dev_dbg(kbdev->dev,
		"Remove atom %pK from kctx %pK; now %d in ctx\n",
		(void *)atom, (void *)kctx, js_kctx_info->ctx.nr_jobs);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	if (--kctx->atoms_count[atom->sched_priority] == 0)
		kbase_js_update_ctx_priority(kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

bool kbasep_js_remove_cancelled_job(struct kbase_device *kbdev,
		struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	unsigned long flags;
	struct kbasep_js_atom_retained_state katom_retained_state;
	bool attr_state_changed;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);

	kbasep_js_atom_retained_state_copy(&katom_retained_state, katom);
	kbasep_js_remove_job(kbdev, kctx, katom);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	/* The atom has 'finished' (will not be re-run), so no need to call
	 * kbasep_js_has_atom_finished().
	 *
	 * This is because it returns false for soft-stopped atoms, but we
	 * want to override that, because we're cancelling an atom regardless of
	 * whether it was soft-stopped or not
	 */
	attr_state_changed = kbasep_js_ctx_attr_ctx_release_atom(kbdev, kctx,
			&katom_retained_state);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return attr_state_changed;
}

/**
 * kbasep_js_run_jobs_after_ctx_and_atom_release - Try running more jobs after
 *                           releasing a context and/or atom
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

	if (js_devdata->nr_user_contexts_running != 0 && runpool_ctx_attr_change) {
		/* A change in runpool ctx attributes might mean we can
		 * run more jobs than before
		 */
		result = KBASEP_JS_RELEASE_RESULT_SCHED_ALL;

		KBASE_KTRACE_ADD_JM_SLOT(kbdev, JD_DONE_TRY_RUN_NEXT_JOB,
					kctx, NULL, 0u, 0);
	}
	return result;
}

/**
 * kbasep_js_runpool_release_ctx_internal - Internal function to release the reference
 *                                          on a ctx and an atom's "retained state", only
 *                                          taking the runpool and as transaction mutexes
 * @kbdev:                   The kbase_device to operate on
 * @kctx:                    The kbase_context to operate on
 * @katom_retained_state:    Retained state from the atom
 *
 * This also starts more jobs running in the case of an ctx-attribute state change
 *
 * This does none of the followup actions for scheduling:
 * - It does not schedule in a new context
 * - It does not requeue or handle dying contexts
 *
 * For those tasks, just call kbasep_js_runpool_release_ctx() instead
 *
 * Has following requirements
 * - Context is scheduled in, and kctx->as_nr matches kctx_as_nr
 * - Context has a non-zero refcount
 * - Caller holds js_kctx_info->ctx.jsctx_mutex
 * - Caller holds js_devdata->runpool_mutex
 *
 * Return: A bitpattern, containing KBASEP_JS_RELEASE_RESULT_* flags, indicating
 *         the result of releasing a context that whether the caller should try
 *         scheduling a new context or should try scheduling all contexts.
 */
static kbasep_js_release_result kbasep_js_runpool_release_ctx_internal(
		struct kbase_device *kbdev,
		struct kbase_context *kctx,
		struct kbasep_js_atom_retained_state *katom_retained_state)
{
	unsigned long flags;
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;

	kbasep_js_release_result release_result = 0u;
	bool runpool_ctx_attr_change = false;
	int kctx_as_nr;
	int new_ref_count;
	CSTD_UNUSED(kctx_as_nr);

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;

	/* Ensure context really is scheduled in */
	KBASE_DEBUG_ASSERT(kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	kctx_as_nr = kctx->as_nr;
	KBASE_DEBUG_ASSERT(kctx_as_nr != KBASEP_AS_NR_INVALID);
	KBASE_DEBUG_ASSERT(atomic_read(&kctx->refcount) > 0);

	/*
	 * Transaction begins on AS and runpool_irq
	 *
	 * Assert about out calling contract
	 */
	mutex_lock(&kbdev->pm.lock);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	KBASE_DEBUG_ASSERT(kctx_as_nr == kctx->as_nr);
	KBASE_DEBUG_ASSERT(atomic_read(&kctx->refcount) > 0);

	/* Update refcount */
	kbase_ctx_sched_release_ctx(kctx);
	new_ref_count = atomic_read(&kctx->refcount);

	/* Release the atom if it finished (i.e. wasn't soft-stopped) */
	if (kbasep_js_has_atom_finished(katom_retained_state))
		runpool_ctx_attr_change |= kbasep_js_ctx_attr_ctx_release_atom(
				kbdev, kctx, katom_retained_state);

	if (new_ref_count == 2 && kbase_ctx_flag(kctx, KCTX_PRIVILEGED) &&
#ifdef CONFIG_MALI_ARBITER_SUPPORT
			!kbase_pm_is_gpu_lost(kbdev) &&
#endif
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

	/* Make a set of checks to see if the context should be scheduled out.
	 * Note that there'll always be at least 1 reference to the context
	 * which was previously acquired by kbasep_js_schedule_ctx().
	 */
	if (new_ref_count == 1 &&
		(!kbasep_js_is_submit_allowed(js_devdata, kctx) ||
#ifdef CONFIG_MALI_ARBITER_SUPPORT
			kbase_pm_is_gpu_lost(kbdev) ||
#endif
			kbase_pm_is_suspending(kbdev))) {
		int num_slots = kbdev->gpu_props.num_job_slots;
		int slot;

		/* Last reference, and we've been told to remove this context
		 * from the Run Pool
		 */
		dev_dbg(kbdev->dev, "JS: RunPool Remove Context %pK because refcount=%d, jobs=%d, allowed=%d",
				kctx, new_ref_count, js_kctx_info->ctx.nr_jobs,
				kbasep_js_is_submit_allowed(js_devdata, kctx));

		KBASE_TLSTREAM_TL_NRET_AS_CTX(kbdev, &kbdev->as[kctx->as_nr], kctx);

		kbase_backend_release_ctx_irq(kbdev, kctx);

		for (slot = 0; slot < num_slots; slot++) {
			if (kbdev->hwaccess.active_kctx[slot] == kctx) {
				dev_dbg(kbdev->dev, "Marking kctx %pK as inactive (s:%d)\n",
					(void *)kctx, slot);
				kbdev->hwaccess.active_kctx[slot] = NULL;
			}
		}

		/* Ctx Attribute handling
		 *
		 * Releasing atoms attributes must either happen before this, or
		 * after the KCTX_SHEDULED flag is changed, otherwise we
		 * double-decount the attributes
		 */
		runpool_ctx_attr_change |=
			kbasep_js_ctx_attr_runpool_release_ctx(kbdev, kctx);

		/* Releasing the context and katom retained state can allow
		 * more jobs to run
		 */
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

		mutex_unlock(&kbdev->pm.lock);

		/* Note: Don't reuse kctx_as_nr now */

		/* Synchronize with any timers */
		kbase_backend_ctx_count_changed(kbdev);

		/* update book-keeping info */
		kbase_ctx_flag_clear(kctx, KCTX_SCHEDULED);
		/* Signal any waiter that the context is not scheduled, so is
		 * safe for termination - once the jsctx_mutex is also dropped,
		 * and jobs have finished.
		 */
		wake_up(&js_kctx_info->ctx.is_scheduled_wait);

		/* Queue an action to occur after we've dropped the lock */
		release_result |= KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED |
			KBASEP_JS_RELEASE_RESULT_SCHED_ALL;
	} else {
		kbasep_js_run_jobs_after_ctx_and_atom_release(kbdev, kctx,
				katom_retained_state, runpool_ctx_attr_change);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
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
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	/* This is called if and only if you've you've detached the context from
	 * the Runpool Queue, and not added it back to the Runpool
	 */
	KBASE_DEBUG_ASSERT(!kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	if (kbase_ctx_flag(kctx, KCTX_DYING)) {
		/* Dying: don't requeue, but kill all jobs on the context. This
		 * happens asynchronously
		 */
		dev_dbg(kbdev->dev,
			"JS: ** Killing Context %pK on RunPool Remove **", kctx);
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
 * kbase_js_sched_all()
 */
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
					struct kbase_context *kctx,
					int js)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	unsigned long flags;
	bool kctx_suspended = false;
	int as_nr;

	dev_dbg(kbdev->dev, "Scheduling kctx %pK (s:%d)\n", kctx, js);

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	/* Pick available address space for this context */
	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	as_nr = kbase_ctx_sched_retain_ctx(kctx);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);
	if (as_nr == KBASEP_AS_NR_INVALID) {
		as_nr = kbase_backend_find_and_release_free_address_space(
				kbdev, kctx);
		if (as_nr != KBASEP_AS_NR_INVALID) {
			/* Attempt to retain the context again, this should
			 * succeed
			 */
			mutex_lock(&kbdev->mmu_hw_mutex);
			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
			as_nr = kbase_ctx_sched_retain_ctx(kctx);
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			mutex_unlock(&kbdev->mmu_hw_mutex);

			WARN_ON(as_nr == KBASEP_AS_NR_INVALID);
		}
	}
	if (as_nr == KBASEP_AS_NR_INVALID)
		return false; /* No address spaces currently available */

	/*
	 * Atomic transaction on the Context and Run Pool begins
	 */
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_lock(&js_devdata->runpool_mutex);
	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	/* Check to see if context is dying due to kbase_job_zap_context() */
	if (kbase_ctx_flag(kctx, KCTX_DYING)) {
		/* Roll back the transaction so far and return */
		kbase_ctx_sched_release_ctx(kctx);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&kbdev->mmu_hw_mutex);
		mutex_unlock(&js_devdata->runpool_mutex);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

		return false;
	}

	KBASE_KTRACE_ADD_JM_REFCOUNT(kbdev, JS_TRY_SCHEDULE_HEAD_CTX, kctx, NULL,
				0u,
				kbase_ktrace_get_ctx_refcnt(kctx));

	kbase_ctx_flag_set(kctx, KCTX_SCHEDULED);

	/* Assign context to previously chosen address space */
	if (!kbase_backend_use_ctx(kbdev, kctx, as_nr)) {
		/* Roll back the transaction so far and return */
		kbase_ctx_sched_release_ctx(kctx);
		kbase_ctx_flag_clear(kctx, KCTX_SCHEDULED);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&kbdev->mmu_hw_mutex);
		mutex_unlock(&js_devdata->runpool_mutex);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

		return false;
	}

	kbdev->hwaccess.active_kctx[js] = kctx;

	KBASE_TLSTREAM_TL_RET_AS_CTX(kbdev, &kbdev->as[kctx->as_nr], kctx);

	/* Cause any future waiter-on-termination to wait until the context is
	 * descheduled
	 */
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
	 * wait for this lock to drop)
	 */
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbase_pm_is_suspending(kbdev) || kbase_pm_is_gpu_lost(kbdev)) {
#else
	if (kbase_pm_is_suspending(kbdev)) {
#endif
		/* Cause it to leave at some later point */
		bool retained;
		CSTD_UNUSED(retained);

		retained = kbase_ctx_sched_inc_refcount_nolock(kctx);
		KBASE_DEBUG_ASSERT(retained);

		kbasep_js_clear_submit_allowed(js_devdata, kctx);
		kctx_suspended = true;
	}

	kbase_ctx_flag_clear(kctx, KCTX_PULLED_SINCE_ACTIVE_JS0 << js);

	/* Transaction complete */
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);

	/* Synchronize with any timers */
	kbase_backend_ctx_count_changed(kbdev);

	mutex_unlock(&js_devdata->runpool_mutex);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
	/* Note: after this point, the context could potentially get scheduled
	 * out immediately
	 */

	if (kctx_suspended) {
		/* Finishing forcing out the context due to a suspend. Use a
		 * variant of kbasep_js_runpool_release_ctx() that doesn't
		 * schedule a new context, to prevent a risk of recursion back
		 * into this function
		 */
		kbasep_js_runpool_release_ctx_no_schedule(kbdev, kctx);
		return false;
	}
	return true;
}

static bool kbase_js_use_ctx(struct kbase_device *kbdev,
				struct kbase_context *kctx,
				int js)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbase_ctx_flag(kctx, KCTX_SCHEDULED) &&
			kbase_backend_use_ctx_sched(kbdev, kctx, js)) {

		dev_dbg(kbdev->dev,
			"kctx %pK already has ASID - mark as active (s:%d)\n",
			(void *)kctx, js);

		if (kbdev->hwaccess.active_kctx[js] != kctx) {
			kbdev->hwaccess.active_kctx[js] = kctx;
			kbase_ctx_flag_clear(kctx,
					KCTX_PULLED_SINCE_ACTIVE_JS0 << js);
		}
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		return true; /* Context already scheduled */
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	return kbasep_js_schedule_ctx(kbdev, kctx, js);
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

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	/* This should only happen in response to a system call
	 * from a user-space thread.
	 * In a non-arbitrated environment this can never happen
	 * whilst suspending.
	 *
	 * In an arbitrated environment, user-space threads can run
	 * while we are suspended (for example GPU not available
	 * to this VM), however in that case we will block on
	 * the wait event for KCTX_SCHEDULED, since no context
	 * can be scheduled until we have the GPU again.
	 */
	if (kbdev->arb.arb_if == NULL)
		if (WARN_ON(kbase_pm_is_suspending(kbdev)))
			return;
#else
	/* This should only happen in response to a system call
	 * from a user-space thread.
	 * In a non-arbitrated environment this can never happen
	 * whilst suspending.
	 */
	if (WARN_ON(kbase_pm_is_suspending(kbdev)))
		return;
#endif

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
		 * because it works on multiple ctxs
		 */
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);

		/* Try to schedule the context in */
		kbase_js_sched_all(kbdev);

		/* Wait for the context to be scheduled in */
		wait_event(kctx->jctx.sched_info.ctx.is_scheduled_wait,
			   kbase_ctx_flag(kctx, KCTX_SCHEDULED));
	} else {
		/* Already scheduled in - We need to retain it to keep the
		 * corresponding address space
		 */
		WARN_ON(!kbase_ctx_sched_inc_refcount(kctx));
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

	KBASE_DEBUG_ASSERT(kbdev);
	KBASE_DEBUG_ASSERT(kbase_pm_is_suspending(kbdev));
	js_devdata = &kbdev->js_data;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	/* Prevent all contexts from submitting */
	js_devdata->runpool_irq.submit_allowed = 0;

	/* Retain each of the contexts, so we can cause it to leave even if it
	 * had no refcount to begin with
	 */
	for (i = BASE_MAX_NR_AS - 1; i >= 0; --i) {
		struct kbase_context *kctx = kbdev->as_to_kctx[i];

		retained = retained << 1;

		if (kctx && !(kbdev->as_free & (1u << i))) {
			kbase_ctx_sched_retain_ctx_refcount(kctx);
			retained |= 1u;
			/* This loop will not have an effect on the privileged
			 * contexts as they would have an extra ref count
			 * compared to the normal contexts, so they will hold
			 * on to their address spaces. MMU will re-enabled for
			 * them on resume.
			 */
		}
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* De-ref the previous retain to ensure each context gets pulled out
	 * sometime later.
	 */
	for (i = 0;
		 i < BASE_MAX_NR_AS;
		 ++i, retained = retained >> 1) {
		struct kbase_context *kctx = kbdev->as_to_kctx[i];

		if (retained & 1u)
			kbasep_js_runpool_release_ctx(kbdev, kctx);
	}

	/* Caller must wait for all Power Manager active references to be
	 * dropped
	 */
}

void kbasep_js_resume(struct kbase_device *kbdev)
{
	struct kbasep_js_device_data *js_devdata;
	int js, prio;

	KBASE_DEBUG_ASSERT(kbdev);
	js_devdata = &kbdev->js_data;
	KBASE_DEBUG_ASSERT(!kbase_pm_is_suspending(kbdev));

	mutex_lock(&js_devdata->queue_mutex);
	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		for (prio = KBASE_JS_ATOM_SCHED_PRIO_FIRST;
			prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT; prio++) {
			struct kbase_context *kctx, *n;
			unsigned long flags;

#ifndef CONFIG_MALI_ARBITER_SUPPORT
			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

			list_for_each_entry_safe(kctx, n,
				 &kbdev->js_data.ctx_list_unpullable[js][prio],
				 jctx.sched_info.ctx.ctx_list_entry[js]) {
				struct kbasep_js_kctx_info *js_kctx_info;
				bool timer_sync = false;

				/* Drop lock so we can take kctx mutexes */
				spin_unlock_irqrestore(&kbdev->hwaccess_lock,
						flags);

				js_kctx_info = &kctx->jctx.sched_info;

				mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
				mutex_lock(&js_devdata->runpool_mutex);
				spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

				if (!kbase_ctx_flag(kctx, KCTX_SCHEDULED) &&
					kbase_js_ctx_pullable(kctx, js, false))
					timer_sync =
						kbase_js_ctx_list_add_pullable_nolock(
								kbdev, kctx, js);

				spin_unlock_irqrestore(&kbdev->hwaccess_lock,
						flags);

				if (timer_sync)
					kbase_backend_ctx_count_changed(kbdev);

				mutex_unlock(&js_devdata->runpool_mutex);
				mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

				/* Take lock before accessing list again */
				spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
			}
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
#else
			bool timer_sync = false;

			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

			list_for_each_entry_safe(kctx, n,
				 &kbdev->js_data.ctx_list_unpullable[js][prio],
				 jctx.sched_info.ctx.ctx_list_entry[js]) {

				if (!kbase_ctx_flag(kctx, KCTX_SCHEDULED) &&
					kbase_js_ctx_pullable(kctx, js, false))
					timer_sync |=
						kbase_js_ctx_list_add_pullable_nolock(
							kbdev, kctx, js);
			}

			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

			if (timer_sync) {
				mutex_lock(&js_devdata->runpool_mutex);
				kbase_backend_ctx_count_changed(kbdev);
				mutex_unlock(&js_devdata->runpool_mutex);
			}
#endif
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

	if ((katom->core_req & BASE_JD_REQ_JOB_SLOT) &&
			(katom->jobslot >= BASE_JM_MAX_NR_SLOTS))
		return false;

	return true;
}

static int kbase_js_get_slot(struct kbase_device *kbdev,
				struct kbase_jd_atom *katom)
{
	if (katom->core_req & BASE_JD_REQ_JOB_SLOT)
		return katom->jobslot;

	if (katom->core_req & BASE_JD_REQ_FS)
		return 0;

	if (katom->core_req & BASE_JD_REQ_ONLY_COMPUTE) {
		if (katom->device_nr == 1 &&
				kbdev->gpu_props.num_core_groups == 2)
			return 2;
	}

	return 1;
}

bool kbase_js_dep_resolved_submit(struct kbase_context *kctx,
					struct kbase_jd_atom *katom)
{
	bool enqueue_required, add_required = true;

	katom->slot_nr = kbase_js_get_slot(kctx->kbdev, katom);

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);
	lockdep_assert_held(&kctx->jctx.lock);

	/* If slot will transition from unpullable to pullable then add to
	 * pullable list
	 */
	if (jsctx_rb_none_to_pull(kctx, katom->slot_nr))
		enqueue_required = true;
	else
		enqueue_required = false;

	if ((katom->atom_flags & KBASE_KATOM_FLAG_X_DEP_BLOCKED) ||
			(katom->pre_dep && (katom->pre_dep->atom_flags &
			KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST))) {
		int prio = katom->sched_priority;
		int js = katom->slot_nr;
		struct jsctx_queue *queue = &kctx->jsctx_queue[prio][js];

		dev_dbg(kctx->kbdev->dev, "Add atom %pK to X_DEP list (s:%d)\n",
			(void *)katom, js);

		list_add_tail(&katom->queue, &queue->x_dep_head);
		katom->atom_flags |= KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST;
		if (kbase_js_atom_blocked_on_x_dep(katom)) {
			enqueue_required = false;
			add_required = false;
		}
	} else {
		dev_dbg(kctx->kbdev->dev, "Atom %pK not added to X_DEP list\n",
			(void *)katom);
	}

	if (add_required) {
		/* Check if there are lower priority jobs to soft stop */
		kbase_job_slot_ctx_priority_check_locked(kctx, katom);

		/* Add atom to ring buffer. */
		jsctx_tree_add(kctx, katom);
		katom->atom_flags |= KBASE_KATOM_FLAG_JSCTX_IN_TREE;
	}

	dev_dbg(kctx->kbdev->dev,
		"Enqueue of kctx %pK is %srequired to submit atom %pK\n",
		kctx, enqueue_required ? "" : "not ", katom);

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
	struct kbase_context *const kctx = katom->kctx;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	while (katom) {
		WARN_ON(!(katom->atom_flags &
				KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST));

		if (!kbase_js_atom_blocked_on_x_dep(katom)) {
			dev_dbg(kctx->kbdev->dev,
				"Del atom %pK from X_DEP list in js_move_to_tree\n",
				(void *)katom);

			list_del(&katom->queue);
			katom->atom_flags &=
					~KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST;
			/* For incremental rendering, an end-of-renderpass atom
			 * may have had its dependency on start-of-renderpass
			 * ignored and may therefore already be in the tree.
			 */
			if (!(katom->atom_flags &
				KBASE_KATOM_FLAG_JSCTX_IN_TREE)) {
				jsctx_tree_add(kctx, katom);
				katom->atom_flags |=
					KBASE_KATOM_FLAG_JSCTX_IN_TREE;
			}
		} else {
			dev_dbg(kctx->kbdev->dev,
				"Atom %pK blocked on x-dep in js_move_to_tree\n",
				(void *)katom);
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

		dev_dbg(kctx->kbdev->dev, "Cleared X_DEP flag on atom %pK\n",
			(void *)x_dep);

		/* Fail if it had a data dependency. */
		if (x_dep->atom_flags & KBASE_KATOM_FLAG_FAIL_BLOCKER)
			x_dep->will_fail_event_code = katom->event_code;

		if (x_dep->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST)
			kbase_js_move_to_tree(x_dep);
	}
}

struct kbase_jd_atom *kbase_js_pull(struct kbase_context *kctx, int js)
{
	struct kbase_jd_atom *katom;
	struct kbasep_js_device_data *js_devdata;
	struct kbase_device *kbdev;
	int pulled;

	KBASE_DEBUG_ASSERT(kctx);

	kbdev = kctx->kbdev;
	dev_dbg(kbdev->dev, "JS: pulling an atom from kctx %pK (s:%d)\n",
		(void *)kctx, js);

	js_devdata = &kbdev->js_data;
	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (!kbasep_js_is_submit_allowed(js_devdata, kctx)) {
		dev_dbg(kbdev->dev, "JS: No submit allowed for kctx %pK\n",
			(void *)kctx);
		return NULL;
	}
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbase_pm_is_suspending(kbdev) || kbase_pm_is_gpu_lost(kbdev))
#else
	if (kbase_pm_is_suspending(kbdev))
#endif
		return NULL;

	katom = jsctx_rb_peek(kctx, js);
	if (!katom) {
		dev_dbg(kbdev->dev, "JS: No pullable atom in kctx %pK (s:%d)\n",
			(void *)kctx, js);
		return NULL;
	}
	if (kbase_jsctx_slot_prio_is_blocked(kctx, js, katom->sched_priority)) {
		dev_dbg(kbdev->dev,
			"JS: kctx %pK is blocked from submitting atoms at priority %d and lower (s:%d)\n",
			(void *)kctx, katom->sched_priority, js);
		return NULL;
	}
	if (atomic_read(&katom->blocked)) {
		dev_dbg(kbdev->dev, "JS: Atom %pK is blocked in js_pull\n",
			(void *)katom);
		return NULL;
	}

	/* Due to ordering restrictions when unpulling atoms on failure, we do
	 * not allow multiple runs of fail-dep atoms from the same context to be
	 * present on the same slot
	 */
	if (katom->pre_dep && kbase_jsctx_slot_atoms_pulled(kctx, js)) {
		struct kbase_jd_atom *prev_atom =
				kbase_backend_inspect_tail(kbdev, js);

		if (prev_atom && prev_atom->kctx != kctx)
			return NULL;
	}

	if (kbase_js_atom_blocked_on_x_dep(katom)) {
		if (katom->x_pre_dep->gpu_rb_state ==
				KBASE_ATOM_GPU_RB_NOT_IN_SLOT_RB ||
				katom->x_pre_dep->will_fail_event_code)	{
			dev_dbg(kbdev->dev,
				"JS: X pre-dep %pK is not present in slot FIFO or will fail\n",
				(void *)katom->x_pre_dep);
			return NULL;
		}
		if ((katom->atom_flags & KBASE_KATOM_FLAG_FAIL_BLOCKER) &&
				kbase_backend_nr_atoms_on_slot(kbdev, js)) {
			dev_dbg(kbdev->dev,
				"JS: Atom %pK has cross-slot fail dependency and atoms on slot (s:%d)\n",
				(void *)katom, js);
			return NULL;
		}
	}

	KBASE_KTRACE_ADD_JM_SLOT_INFO(kbdev, JS_PULL_JOB, kctx, katom,
				      katom->jc, js, katom->sched_priority);
	kbase_ctx_flag_set(kctx, KCTX_PULLED);
	kbase_ctx_flag_set(kctx, (KCTX_PULLED_SINCE_ACTIVE_JS0 << js));

	pulled = kbase_jsctx_slot_atom_pulled_inc(kctx, katom);
	if (pulled == 1 && !kctx->slots_pullable) {
		WARN_ON(kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
		kbase_ctx_flag_set(kctx, KCTX_RUNNABLE_REF);
		atomic_inc(&kbdev->js_data.nr_contexts_runnable);
	}
	jsctx_rb_pull(kctx, katom);

	kbase_ctx_sched_retain_ctx_refcount(kctx);

	katom->ticks = 0;

	dev_dbg(kbdev->dev, "JS: successfully pulled atom %pK from kctx %pK (s:%d)\n",
		(void *)katom, (void *)kctx, js);

	return katom;
}

/**
 * js_return_of_start_rp() - Handle soft-stop of an atom that starts a
 *                           renderpass
 * @start_katom: Pointer to the start-of-renderpass atom that was soft-stopped
 *
 * This function is called to switch to incremental rendering if the tiler job
 * chain at the start of a renderpass has used too much memory. It prevents the
 * tiler job being pulled for execution in the job scheduler again until the
 * next phase of incremental rendering is complete.
 *
 * If the end-of-renderpass atom is already in the job scheduler (because a
 * previous attempt at tiling used too much memory during the same renderpass)
 * then it is unblocked; otherwise, it is run by handing it to the scheduler.
 */
static void js_return_of_start_rp(struct kbase_jd_atom *const start_katom)
{
	struct kbase_context *const kctx = start_katom->kctx;
	struct kbase_device *const kbdev = kctx->kbdev;
	struct kbase_jd_renderpass *rp;
	struct kbase_jd_atom *end_katom;
	unsigned long flags;

	lockdep_assert_held(&kctx->jctx.lock);

	if (WARN_ON(!(start_katom->core_req & BASE_JD_REQ_START_RENDERPASS)))
		return;

	compiletime_assert((1ull << (sizeof(start_katom->renderpass_id) * 8)) <=
			ARRAY_SIZE(kctx->jctx.renderpasses),
			"Should check invalid access to renderpasses");

	rp = &kctx->jctx.renderpasses[start_katom->renderpass_id];

	if (WARN_ON(rp->start_katom != start_katom))
		return;

	dev_dbg(kctx->kbdev->dev,
		"JS return start atom %pK in state %d of RP %d\n",
		(void *)start_katom, (int)rp->state,
		start_katom->renderpass_id);

	if (WARN_ON(rp->state == KBASE_JD_RP_COMPLETE))
		return;

	/* The tiler job might have been soft-stopped for some reason other
	 * than running out of memory.
	 */
	if (rp->state == KBASE_JD_RP_START || rp->state == KBASE_JD_RP_RETRY) {
		dev_dbg(kctx->kbdev->dev,
			"JS return isn't OOM in state %d of RP %d\n",
			(int)rp->state, start_katom->renderpass_id);
		return;
	}

	dev_dbg(kctx->kbdev->dev,
		"JS return confirm OOM in state %d of RP %d\n",
		(int)rp->state, start_katom->renderpass_id);

	if (WARN_ON(rp->state != KBASE_JD_RP_PEND_OOM &&
		rp->state != KBASE_JD_RP_RETRY_PEND_OOM))
		return;

	/* Prevent the tiler job being pulled for execution in the
	 * job scheduler again.
	 */
	dev_dbg(kbdev->dev, "Blocking start atom %pK\n",
		(void *)start_katom);
	atomic_inc(&start_katom->blocked);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	rp->state = (rp->state == KBASE_JD_RP_PEND_OOM) ?
		KBASE_JD_RP_OOM : KBASE_JD_RP_RETRY_OOM;

	/* Was the fragment job chain submitted to kbase yet? */
	end_katom = rp->end_katom;
	if (end_katom) {
		dev_dbg(kctx->kbdev->dev, "JS return add end atom %pK\n",
			(void *)end_katom);

		if (rp->state == KBASE_JD_RP_RETRY_OOM) {
			/* Allow the end of the renderpass to be pulled for
			 * execution again to continue incremental rendering.
			 */
			dev_dbg(kbdev->dev, "Unblocking end atom %pK\n",
				(void *)end_katom);
			atomic_dec(&end_katom->blocked);
			WARN_ON(!(end_katom->atom_flags &
				KBASE_KATOM_FLAG_JSCTX_IN_TREE));
			WARN_ON(end_katom->status != KBASE_JD_ATOM_STATE_IN_JS);

			kbase_js_ctx_list_add_pullable_nolock(kbdev, kctx,
				end_katom->slot_nr);

			/* Expect the fragment job chain to be scheduled without
			 * further action because this function is called when
			 * returning an atom to the job scheduler ringbuffer.
			 */
			end_katom = NULL;
		} else {
			WARN_ON(end_katom->status !=
				KBASE_JD_ATOM_STATE_QUEUED &&
				end_katom->status != KBASE_JD_ATOM_STATE_IN_JS);
		}
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (end_katom)
		kbase_jd_dep_clear_locked(end_katom);
}

/**
 * js_return_of_end_rp() - Handle completion of an atom that ends a renderpass
 * @end_katom: Pointer to the end-of-renderpass atom that was completed
 *
 * This function is called to continue incremental rendering if the tiler job
 * chain at the start of a renderpass used too much memory. It resets the
 * mechanism for detecting excessive memory usage then allows the soft-stopped
 * tiler job chain to be pulled for execution again.
 *
 * The start-of-renderpass atom must already been submitted to kbase.
 */
static void js_return_of_end_rp(struct kbase_jd_atom *const end_katom)
{
	struct kbase_context *const kctx = end_katom->kctx;
	struct kbase_device *const kbdev = kctx->kbdev;
	struct kbase_jd_renderpass *rp;
	struct kbase_jd_atom *start_katom;
	unsigned long flags;

	lockdep_assert_held(&kctx->jctx.lock);

	if (WARN_ON(!(end_katom->core_req & BASE_JD_REQ_END_RENDERPASS)))
		return;

	compiletime_assert((1ull << (sizeof(end_katom->renderpass_id) * 8)) <=
			ARRAY_SIZE(kctx->jctx.renderpasses),
			"Should check invalid access to renderpasses");

	rp = &kctx->jctx.renderpasses[end_katom->renderpass_id];

	if (WARN_ON(rp->end_katom != end_katom))
		return;

	dev_dbg(kctx->kbdev->dev,
		"JS return end atom %pK in state %d of RP %d\n",
		(void *)end_katom, (int)rp->state, end_katom->renderpass_id);

	if (WARN_ON(rp->state != KBASE_JD_RP_OOM &&
		rp->state != KBASE_JD_RP_RETRY_OOM))
		return;

	/* Reduce the number of mapped pages in the memory regions that
	 * triggered out-of-memory last time so that we can detect excessive
	 * memory usage again.
	 */
	kbase_gpu_vm_lock(kctx);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	while (!list_empty(&rp->oom_reg_list)) {
		struct kbase_va_region *reg =
			list_first_entry(&rp->oom_reg_list,
					 struct kbase_va_region, link);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		dev_dbg(kbdev->dev,
			"Reset backing to %zu pages for region %pK\n",
			reg->threshold_pages, (void *)reg);

		if (!WARN_ON(reg->flags & KBASE_REG_VA_FREED))
			kbase_mem_shrink(kctx, reg, reg->threshold_pages);

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		dev_dbg(kbdev->dev, "Deleting region %pK from list\n",
			(void *)reg);
		list_del_init(&reg->link);
		kbase_va_region_alloc_put(kctx, reg);
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	kbase_gpu_vm_unlock(kctx);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	rp->state = KBASE_JD_RP_RETRY;
	dev_dbg(kbdev->dev, "Changed state to %d for retry\n", rp->state);

	/* Allow the start of the renderpass to be pulled for execution again
	 * to begin/continue incremental rendering.
	 */
	start_katom = rp->start_katom;
	if (!WARN_ON(!start_katom)) {
		dev_dbg(kbdev->dev, "Unblocking start atom %pK\n",
			(void *)start_katom);
		atomic_dec(&start_katom->blocked);
		(void)kbase_js_ctx_list_add_pullable_head_nolock(kbdev, kctx,
			start_katom->slot_nr);
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
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
	bool slot_became_unblocked;
	bool timer_sync = false;
	bool context_idle = false;
	unsigned long flags;
	base_jd_core_req core_req = katom->core_req;
	u64 cache_jc = katom->jc;

	dev_dbg(kbdev->dev, "%s for atom %pK with event code 0x%x\n",
		__func__, (void *)katom, katom->event_code);

	KBASE_KTRACE_ADD_JM(kbdev, JS_RETURN_WORKER, kctx, katom, katom->jc, 0);

	if (katom->event_code != BASE_JD_EVENT_END_RP_DONE)
		KBASE_TLSTREAM_TL_EVENT_ATOM_SOFTSTOP_EX(kbdev, katom);

	kbase_backend_complete_wq(kbdev, katom);

	kbasep_js_atom_retained_state_copy(&retained_state, katom);

	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);

	if (katom->event_code != BASE_JD_EVENT_END_RP_DONE)
		atomic_dec(&katom->blocked);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	slot_became_unblocked = kbase_jsctx_slot_atom_pulled_dec(kctx, katom);

	if (!kbase_jsctx_slot_atoms_pulled(kctx, js) &&
	    jsctx_rb_none_to_pull(kctx, js))
		timer_sync |= kbase_js_ctx_list_remove_nolock(kbdev, kctx, js);

	/* If the context is now unblocked on this slot after soft-stopped
	 * atoms, then only mark it as pullable on this slot if it is not
	 * idle
	 */
	if (slot_became_unblocked && kbase_jsctx_atoms_pulled(kctx) &&
	    kbase_js_ctx_pullable(kctx, js, true))
		timer_sync |=
			kbase_js_ctx_list_add_pullable_nolock(kbdev, kctx, js);

	if (!kbase_jsctx_atoms_pulled(kctx)) {
		dev_dbg(kbdev->dev,
			"No atoms currently pulled from context %pK\n",
			(void *)kctx);

		if (!kctx->slots_pullable) {
			dev_dbg(kbdev->dev,
				"Context %pK %s counted as runnable\n",
				(void *)kctx,
				kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF) ?
					"is" : "isn't");

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
		dev_dbg(kbdev->dev,
			"Context %pK %s counted as active\n",
			(void *)kctx,
			kbase_ctx_flag(kctx, KCTX_ACTIVE) ?
				"is" : "isn't");
		WARN_ON(!kbase_ctx_flag(kctx, KCTX_ACTIVE));
		kbase_ctx_flag_clear(kctx, KCTX_ACTIVE);
		kbase_pm_context_idle(kbdev);
	}

	if (timer_sync)
		kbase_js_sync_timers(kbdev);

	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_unlock(&js_devdata->queue_mutex);

	if (katom->core_req & BASE_JD_REQ_START_RENDERPASS) {
		mutex_lock(&kctx->jctx.lock);
		js_return_of_start_rp(katom);
		mutex_unlock(&kctx->jctx.lock);
	} else if (katom->event_code == BASE_JD_EVENT_END_RP_DONE) {
		mutex_lock(&kctx->jctx.lock);
		js_return_of_end_rp(katom);
		mutex_unlock(&kctx->jctx.lock);
	}

	dev_dbg(kbdev->dev, "JS: retained state %s finished",
		kbasep_js_has_atom_finished(&retained_state) ?
		"has" : "hasn't");

	WARN_ON(kbasep_js_has_atom_finished(&retained_state));

	kbasep_js_runpool_release_ctx_and_katom_retained_state(kbdev, kctx,
							&retained_state);

	kbase_js_sched_all(kbdev);

	kbase_backend_complete_wq_post_sched(kbdev, core_req);

	KBASE_KTRACE_ADD_JM(kbdev, JS_RETURN_WORKER_END, kctx, NULL, cache_jc,
			    0);

	dev_dbg(kbdev->dev, "Leaving %s for atom %pK\n",
		__func__, (void *)katom);
}

void kbase_js_unpull(struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	dev_dbg(kctx->kbdev->dev, "Unpulling atom %pK in kctx %pK\n",
		(void *)katom, (void *)kctx);

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	jsctx_rb_unpull(kctx, katom);

	WARN_ON(work_pending(&katom->work));

	/* Block re-submission until workqueue has run */
	atomic_inc(&katom->blocked);

	kbase_job_check_leave_disjoint(kctx->kbdev, katom);

	INIT_WORK(&katom->work, js_return_worker);
	queue_work(kctx->jctx.job_done_wq, &katom->work);
}

/**
 * js_complete_start_rp() - Handle completion of atom that starts a renderpass
 * @kctx:        Context pointer
 * @start_katom: Pointer to the atom that completed
 *
 * Put any references to virtual memory regions that might have been added by
 * kbase_job_slot_softstop_start_rp() because the tiler job chain completed
 * despite any pending soft-stop request.
 *
 * If the atom that just completed was soft-stopped during a previous attempt to
 * run it then there should be a blocked end-of-renderpass atom waiting for it,
 * which we must unblock to process the output of the tiler job chain.
 *
 * Return: true if caller should call kbase_backend_ctx_count_changed()
 */
static bool js_complete_start_rp(struct kbase_context *kctx,
	struct kbase_jd_atom *const start_katom)
{
	struct kbase_device *const kbdev = kctx->kbdev;
	struct kbase_jd_renderpass *rp;
	bool timer_sync = false;

	lockdep_assert_held(&kctx->jctx.lock);

	if (WARN_ON(!(start_katom->core_req & BASE_JD_REQ_START_RENDERPASS)))
		return false;

	compiletime_assert((1ull << (sizeof(start_katom->renderpass_id) * 8)) <=
			ARRAY_SIZE(kctx->jctx.renderpasses),
			"Should check invalid access to renderpasses");

	rp = &kctx->jctx.renderpasses[start_katom->renderpass_id];

	if (WARN_ON(rp->start_katom != start_katom))
		return false;

	dev_dbg(kctx->kbdev->dev,
		"Start atom %pK is done in state %d of RP %d\n",
		(void *)start_katom, (int)rp->state,
		start_katom->renderpass_id);

	if (WARN_ON(rp->state == KBASE_JD_RP_COMPLETE))
		return false;

	if (rp->state == KBASE_JD_RP_PEND_OOM ||
		rp->state == KBASE_JD_RP_RETRY_PEND_OOM) {
		unsigned long flags;

		dev_dbg(kctx->kbdev->dev,
			"Start atom %pK completed before soft-stop\n",
			(void *)start_katom);

		kbase_gpu_vm_lock(kctx);
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

		while (!list_empty(&rp->oom_reg_list)) {
			struct kbase_va_region *reg =
				list_first_entry(&rp->oom_reg_list,
						 struct kbase_va_region, link);

			WARN_ON(reg->flags & KBASE_REG_VA_FREED);
			dev_dbg(kctx->kbdev->dev, "Deleting region %pK from list\n",
				(void *)reg);
			list_del_init(&reg->link);
			kbase_va_region_alloc_put(kctx, reg);
		}

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		kbase_gpu_vm_unlock(kctx);
	} else {
		dev_dbg(kctx->kbdev->dev,
			"Start atom %pK did not exceed memory threshold\n",
			(void *)start_katom);

		WARN_ON(rp->state != KBASE_JD_RP_START &&
			rp->state != KBASE_JD_RP_RETRY);
	}

	if (rp->state == KBASE_JD_RP_RETRY ||
		rp->state == KBASE_JD_RP_RETRY_PEND_OOM) {
		struct kbase_jd_atom *const end_katom = rp->end_katom;

		if (!WARN_ON(!end_katom)) {
			unsigned long flags;

			/* Allow the end of the renderpass to be pulled for
			 * execution again to continue incremental rendering.
			 */
			dev_dbg(kbdev->dev, "Unblocking end atom %pK!\n",
				(void *)end_katom);
			atomic_dec(&end_katom->blocked);

			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
			timer_sync = kbase_js_ctx_list_add_pullable_nolock(
					kbdev, kctx, end_katom->slot_nr);
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		}
	}

	return timer_sync;
}

/**
 * js_complete_end_rp() - Handle final completion of atom that ends a renderpass
 * @kctx:      Context pointer
 * @end_katom: Pointer to the atom that completed for the last time
 *
 * This function must only be called if the renderpass actually completed
 * without the tiler job chain at the start using too much memory; otherwise
 * completion of the end-of-renderpass atom is handled similarly to a soft-stop.
 */
static void js_complete_end_rp(struct kbase_context *kctx,
	struct kbase_jd_atom *const end_katom)
{
	struct kbase_device *const kbdev = kctx->kbdev;
	unsigned long flags;
	struct kbase_jd_renderpass *rp;

	lockdep_assert_held(&kctx->jctx.lock);

	if (WARN_ON(!(end_katom->core_req & BASE_JD_REQ_END_RENDERPASS)))
		return;

	compiletime_assert((1ull << (sizeof(end_katom->renderpass_id) * 8)) <=
			ARRAY_SIZE(kctx->jctx.renderpasses),
			"Should check invalid access to renderpasses");

	rp = &kctx->jctx.renderpasses[end_katom->renderpass_id];

	if (WARN_ON(rp->end_katom != end_katom))
		return;

	dev_dbg(kbdev->dev, "End atom %pK is done in state %d of RP %d\n",
		(void *)end_katom, (int)rp->state, end_katom->renderpass_id);

	if (WARN_ON(rp->state == KBASE_JD_RP_COMPLETE) ||
		WARN_ON(rp->state == KBASE_JD_RP_OOM) ||
		WARN_ON(rp->state == KBASE_JD_RP_RETRY_OOM))
		return;

	/* Rendering completed without running out of memory.
	 */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	WARN_ON(!list_empty(&rp->oom_reg_list));
	rp->state = KBASE_JD_RP_COMPLETE;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	dev_dbg(kbdev->dev, "Renderpass %d is complete\n",
		end_katom->renderpass_id);
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
	int prio = katom->sched_priority;

	kbdev = kctx->kbdev;
	atom_slot = katom->slot_nr;

	dev_dbg(kbdev->dev, "%s for atom %pK (s:%d)\n",
		__func__, (void *)katom, atom_slot);

	/* Update the incremental rendering state machine.
	 */
	if (katom->core_req & BASE_JD_REQ_START_RENDERPASS)
		timer_sync |= js_complete_start_rp(kctx, katom);
	else if (katom->core_req & BASE_JD_REQ_END_RENDERPASS)
		js_complete_end_rp(kctx, katom);

	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;

	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);

	mutex_lock(&js_devdata->runpool_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (katom->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_TREE) {
		bool slot_became_unblocked;

		dev_dbg(kbdev->dev, "Atom %pK is in runnable_tree\n",
			(void *)katom);

		slot_became_unblocked =
			kbase_jsctx_slot_atom_pulled_dec(kctx, katom);
		context_idle = !kbase_jsctx_atoms_pulled(kctx);

		if (!kbase_jsctx_atoms_pulled(kctx) && !kctx->slots_pullable) {
			WARN_ON(!kbase_ctx_flag(kctx, KCTX_RUNNABLE_REF));
			kbase_ctx_flag_clear(kctx, KCTX_RUNNABLE_REF);
			atomic_dec(&kbdev->js_data.nr_contexts_runnable);
			timer_sync = true;
		}

		/* If this slot has been blocked due to soft-stopped atoms, and
		 * all atoms have now been processed at this priority level and
		 * higher, then unblock the slot
		 */
		if (slot_became_unblocked) {
			dev_dbg(kbdev->dev,
				"kctx %pK is no longer blocked from submitting on slot %d at priority %d or higher\n",
				(void *)kctx, atom_slot, prio);

			if (kbase_js_ctx_pullable(kctx, atom_slot, true))
				timer_sync |=
					kbase_js_ctx_list_add_pullable_nolock(
						kbdev, kctx, atom_slot);
		}
	}
	WARN_ON(!(katom->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_TREE));

	if (!kbase_jsctx_slot_atoms_pulled(kctx, atom_slot) &&
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
	    !kbase_jsctx_atoms_pulled(kctx) &&
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
	if (context_idle) {
		dev_dbg(kbdev->dev, "kctx %pK is no longer active\n",
			(void *)kctx);
		kbase_ctx_flag_clear(kctx, KCTX_ACTIVE);
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	if (timer_sync)
		kbase_backend_ctx_count_changed(kbdev);
	mutex_unlock(&js_devdata->runpool_mutex);

	dev_dbg(kbdev->dev, "Leaving %s\n", __func__);
	return context_idle;
}

/**
 * js_end_rp_is_complete() - Check whether an atom that ends a renderpass has
 *                           completed for the last time.
 *
 * @end_katom: Pointer to the atom that completed on the hardware.
 *
 * An atom that ends a renderpass may be run on the hardware several times
 * before notifying userspace or allowing dependent atoms to be executed.
 *
 * This function is used to decide whether or not to allow end-of-renderpass
 * atom completion. It only returns false if the atom at the start of the
 * renderpass was soft-stopped because it used too much memory during the most
 * recent attempt at tiling.
 *
 * Return: True if the atom completed for the last time.
 */
static bool js_end_rp_is_complete(struct kbase_jd_atom *const end_katom)
{
	struct kbase_context *const kctx = end_katom->kctx;
	struct kbase_device *const kbdev = kctx->kbdev;
	struct kbase_jd_renderpass *rp;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	if (WARN_ON(!(end_katom->core_req & BASE_JD_REQ_END_RENDERPASS)))
		return true;

	compiletime_assert((1ull << (sizeof(end_katom->renderpass_id) * 8)) <=
			ARRAY_SIZE(kctx->jctx.renderpasses),
			"Should check invalid access to renderpasses");

	rp = &kctx->jctx.renderpasses[end_katom->renderpass_id];

	if (WARN_ON(rp->end_katom != end_katom))
		return true;

	dev_dbg(kbdev->dev,
		"JS complete end atom %pK in state %d of RP %d\n",
		(void *)end_katom, (int)rp->state,
		end_katom->renderpass_id);

	if (WARN_ON(rp->state == KBASE_JD_RP_COMPLETE))
		return true;

	/* Failure of end-of-renderpass atoms must not return to the
	 * start of the renderpass.
	 */
	if (end_katom->event_code != BASE_JD_EVENT_DONE)
		return true;

	if (rp->state != KBASE_JD_RP_OOM &&
		rp->state != KBASE_JD_RP_RETRY_OOM)
		return true;

	dev_dbg(kbdev->dev, "Suppressing end atom completion\n");
	return false;
}

struct kbase_jd_atom *kbase_js_complete_atom(struct kbase_jd_atom *katom,
		ktime_t *end_timestamp)
{
	struct kbase_device *kbdev;
	struct kbase_context *kctx = katom->kctx;
	struct kbase_jd_atom *x_dep = katom->x_post_dep;

	kbdev = kctx->kbdev;
	dev_dbg(kbdev->dev, "Atom %pK complete in kctx %pK (post-dep %pK)\n",
		(void *)katom, (void *)kctx, (void *)x_dep);

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	if ((katom->core_req & BASE_JD_REQ_END_RENDERPASS) &&
		!js_end_rp_is_complete(katom)) {
		katom->event_code = BASE_JD_EVENT_END_RP_DONE;
		kbase_js_unpull(kctx, katom);
		return NULL;
	}

	if (katom->will_fail_event_code)
		katom->event_code = katom->will_fail_event_code;

	katom->status = KBASE_JD_ATOM_STATE_HW_COMPLETED;
	dev_dbg(kbdev->dev, "Atom %pK status to HW completed\n", (void *)katom);
	if (kbase_is_quick_reset_enabled(kbdev)) {
		kbdev->num_of_atoms_hw_completed++;
		if (kbdev->num_of_atoms_hw_completed >= 20)
			kbase_disable_quick_reset(kbdev);
	}

	if (katom->event_code != BASE_JD_EVENT_DONE) {
		kbase_js_evict_deps(kctx, katom, katom->slot_nr,
				katom->sched_priority);
	}

	KBASE_TLSTREAM_AUX_EVENT_JOB_SLOT(kbdev, NULL,
		katom->slot_nr, 0, TL_JS_EVENT_STOP);

	trace_sysgraph_gpu(SGR_COMPLETE, kctx->id,
			kbase_jd_atom_id(katom->kctx, katom), katom->slot_nr);

	KBASE_TLSTREAM_TL_JD_DONE_START(kbdev, katom);
	kbase_jd_done(katom, katom->slot_nr, end_timestamp, 0);
	KBASE_TLSTREAM_TL_JD_DONE_END(kbdev, katom);

	/* Unblock cross dependency if present */
	if (x_dep && (katom->event_code == BASE_JD_EVENT_DONE ||
		!(x_dep->atom_flags & KBASE_KATOM_FLAG_FAIL_BLOCKER)) &&
		(x_dep->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_X_DEP_LIST)) {
		bool was_pullable = kbase_js_ctx_pullable(kctx, x_dep->slot_nr,
				false);
		x_dep->atom_flags &= ~KBASE_KATOM_FLAG_X_DEP_BLOCKED;
		dev_dbg(kbdev->dev, "Cleared X_DEP flag on atom %pK\n",
			(void *)x_dep);

		kbase_js_move_to_tree(x_dep);

		if (!was_pullable && kbase_js_ctx_pullable(kctx, x_dep->slot_nr,
				false))
			kbase_js_ctx_list_add_pullable_nolock(kbdev, kctx,
					x_dep->slot_nr);

		if (x_dep->atom_flags & KBASE_KATOM_FLAG_JSCTX_IN_TREE) {
			dev_dbg(kbdev->dev, "Atom %pK is in runnable tree\n",
				(void *)x_dep);
			return x_dep;
		}
	} else {
		dev_dbg(kbdev->dev,
			"No cross-slot dep to unblock for atom %pK\n",
			(void *)katom);
	}

	return NULL;
}

/**
 * kbase_js_atom_blocked_on_x_dep - Decide whether to ignore a cross-slot
 *                                  dependency
 * @katom:	Pointer to an atom in the slot ringbuffer
 *
 * A cross-slot dependency is ignored if necessary to unblock incremental
 * rendering. If the atom at the start of a renderpass used too much memory
 * and was soft-stopped then the atom at the end of a renderpass is submitted
 * to hardware regardless of its dependency on the start-of-renderpass atom.
 * This can happen multiple times for the same pair of atoms.
 *
 * Return: true to block the atom or false to allow it to be submitted to
 *         hardware
 */
bool kbase_js_atom_blocked_on_x_dep(struct kbase_jd_atom *const katom)
{
	struct kbase_context *const kctx = katom->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbase_jd_renderpass *rp;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (!(katom->atom_flags &
			KBASE_KATOM_FLAG_X_DEP_BLOCKED)) {
		dev_dbg(kbdev->dev, "Atom %pK is not blocked on a cross-slot dependency",
			(void *)katom);
		return false;
	}

	if (!(katom->core_req & BASE_JD_REQ_END_RENDERPASS)) {
		dev_dbg(kbdev->dev, "Atom %pK is blocked on a cross-slot dependency",
			(void *)katom);
		return true;
	}

	compiletime_assert((1ull << (sizeof(katom->renderpass_id) * 8)) <=
			ARRAY_SIZE(kctx->jctx.renderpasses),
			"Should check invalid access to renderpasses");

	rp = &kctx->jctx.renderpasses[katom->renderpass_id];
	/* We can read a subset of renderpass state without holding
	 * higher-level locks (but not end_katom, for example).
	 */

	WARN_ON(rp->state == KBASE_JD_RP_COMPLETE);

	dev_dbg(kbdev->dev, "End atom has cross-slot dep in state %d\n",
		(int)rp->state);

	if (rp->state != KBASE_JD_RP_OOM && rp->state != KBASE_JD_RP_RETRY_OOM)
		return true;

	/* Tiler ran out of memory so allow the fragment job chain to run
	 * if it only depends on the tiler job chain.
	 */
	if (katom->x_pre_dep != rp->start_katom) {
		dev_dbg(kbdev->dev, "Dependency is on %pK not start atom %pK\n",
			(void *)katom->x_pre_dep, (void *)rp->start_katom);
		return true;
	}

	dev_dbg(kbdev->dev, "Ignoring cross-slot dep on atom %pK\n",
		(void *)katom->x_pre_dep);

	return false;
}

void kbase_js_sched(struct kbase_device *kbdev, int js_mask)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbase_context *last_active[BASE_JM_MAX_NR_SLOTS];
	bool timer_sync = false;
	bool ctx_waiting[BASE_JM_MAX_NR_SLOTS];
	int js;

	KBASE_TLSTREAM_TL_JS_SCHED_START(kbdev, 0);

	dev_dbg(kbdev->dev, "%s kbdev %pK mask 0x%x\n",
		__func__, (void *)kbdev, (unsigned int)js_mask);

	js_devdata = &kbdev->js_data;

	down(&js_devdata->schedule_sem);
	mutex_lock(&js_devdata->queue_mutex);

	for (js = 0; js < BASE_JM_MAX_NR_SLOTS; js++) {
		last_active[js] = kbdev->hwaccess.active_kctx[js];
		ctx_waiting[js] = false;
	}

	while (js_mask) {
		js = ffs(js_mask) - 1;

		while (1) {
			struct kbase_context *kctx;
			unsigned long flags;
			bool context_idle = false;

			kctx = kbase_js_ctx_list_pop_head(kbdev, js);

			if (!kctx) {
				js_mask &= ~(1 << js);
				dev_dbg(kbdev->dev,
					"No kctx on pullable list (s:%d)\n",
					js);
				break;
			}

			if (!kbase_ctx_flag(kctx, KCTX_ACTIVE)) {
				context_idle = true;

				dev_dbg(kbdev->dev,
					"kctx %pK is not active (s:%d)\n",
					(void *)kctx, js);

				if (kbase_pm_context_active_handle_suspend(
									kbdev,
				      KBASE_PM_SUSPEND_HANDLER_DONT_INCREASE)) {
					dev_dbg(kbdev->dev,
						"Suspend pending (s:%d)\n", js);
					/* Suspend pending - return context to
					 * queue and stop scheduling
					 */
					mutex_lock(
					&kctx->jctx.sched_info.ctx.jsctx_mutex);
					if (kbase_js_ctx_list_add_pullable_head(
						kctx->kbdev, kctx, js))
						kbase_js_sync_timers(kbdev);
					mutex_unlock(
					&kctx->jctx.sched_info.ctx.jsctx_mutex);
					mutex_unlock(&js_devdata->queue_mutex);
					up(&js_devdata->schedule_sem);
					KBASE_TLSTREAM_TL_JS_SCHED_END(kbdev,
									  0);
					return;
				}
				kbase_ctx_flag_set(kctx, KCTX_ACTIVE);
			}

			if (!kbase_js_use_ctx(kbdev, kctx, js)) {
				mutex_lock(
					&kctx->jctx.sched_info.ctx.jsctx_mutex);

				dev_dbg(kbdev->dev,
					"kctx %pK cannot be used at this time\n",
					kctx);

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

			if (!kbase_jm_kick(kbdev, 1 << js)) {
				dev_dbg(kbdev->dev,
					"No more jobs can be submitted (s:%d)\n",
					js);
				js_mask &= ~(1 << js);
			}
			if (!kbase_ctx_flag(kctx, KCTX_PULLED)) {
				bool pullable;

				dev_dbg(kbdev->dev,
					"No atoms pulled from kctx %pK (s:%d)\n",
					(void *)kctx, js);

				pullable = kbase_js_ctx_pullable(kctx, js,
						true);

				/* Failed to pull jobs - push to head of list.
				 * Unless this context is already 'active', in
				 * which case it's effectively already scheduled
				 * so push it to the back of the list.
				 */
				if (pullable && kctx == last_active[js] &&
						kbase_ctx_flag(kctx,
						(KCTX_PULLED_SINCE_ACTIVE_JS0 <<
						js)))
					timer_sync |=
					kbase_js_ctx_list_add_pullable_nolock(
							kctx->kbdev,
							kctx, js);
				else if (pullable)
					timer_sync |=
					kbase_js_ctx_list_add_pullable_head_nolock(
							kctx->kbdev,
							kctx, js);
				else
					timer_sync |=
					kbase_js_ctx_list_add_unpullable_nolock(
								kctx->kbdev,
								kctx, js);

				/* If this context is not the active context,
				 * but the active context is pullable on this
				 * slot, then we need to remove the active
				 * marker to prevent it from submitting atoms in
				 * the IRQ handler, which would prevent this
				 * context from making progress.
				 */
				if (last_active[js] && kctx != last_active[js]
						&& kbase_js_ctx_pullable(
						last_active[js], js, true))
					ctx_waiting[js] = true;

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

			dev_dbg(kbdev->dev, "Push kctx %pK to back of list\n",
				(void *)kctx);
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

	for (js = 0; js < BASE_JM_MAX_NR_SLOTS; js++) {
		if (kbdev->hwaccess.active_kctx[js] == last_active[js] &&
				ctx_waiting[js]) {
			dev_dbg(kbdev->dev, "Marking kctx %pK as inactive (s:%d)\n",
					(void *)last_active[js], js);
			kbdev->hwaccess.active_kctx[js] = NULL;
		}
	}

	mutex_unlock(&js_devdata->queue_mutex);
	up(&js_devdata->schedule_sem);
	KBASE_TLSTREAM_TL_JS_SCHED_END(kbdev, 0);
}

void kbase_js_zap_context(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
	struct kbasep_js_kctx_info *js_kctx_info = &kctx->jctx.sched_info;

	/*
	 * Critical assumption: No more submission is possible outside of the
	 * workqueue. This is because the OS *must* prevent U/K calls (IOCTLs)
	 * whilst the struct kbase_context is terminating.
	 */

	/* First, atomically do the following:
	 * - mark the context as dying
	 * - try to evict it from the queue
	 */
	mutex_lock(&kctx->jctx.lock);
	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	kbase_ctx_flag_set(kctx, KCTX_DYING);

	dev_dbg(kbdev->dev, "Zap: Try Evict Ctx %pK", kctx);

	/*
	 * At this point we know:
	 * - If eviction succeeded, it was in the queue, but now no
	 *   longer is
	 *  - We must cancel the jobs here. No Power Manager active reference to
	 *    release.
	 *  - This happens asynchronously - kbase_jd_zap_context() will wait for
	 *    those jobs to be killed.
	 * - If eviction failed, then it wasn't in the queue. It is one
	 *   of the following:
	 *  - a. it didn't have any jobs, and so is not in the Queue or
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
	 * and will not requeue the context in the queue.
	 * kbase_jd_zap_context() will wait for those jobs to be killed.
	 *  - Hence, work required just to make it leave the runpool. Cancelling
	 *    jobs and releasing the Power manager active reference will be
	 *    handled when it leaves the runpool.
	 */
	if (!kbase_ctx_flag(kctx, KCTX_SCHEDULED)) {
		unsigned long flags;
		int js;

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
			if (!list_empty(
				&kctx->jctx.sched_info.ctx.ctx_list_entry[js]))
				list_del_init(
				&kctx->jctx.sched_info.ctx.ctx_list_entry[js]);
		}
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		/* The following events require us to kill off remaining jobs
		 * and update PM book-keeping:
		 * - we evicted it correctly (it must have jobs to be in the
		 *   Queue)
		 *
		 * These events need no action, but take this path anyway:
		 * - Case a: it didn't have any jobs, and was never in the Queue
		 * - Case b: scheduling transaction will be partially rolled-
		 *           back (this already cancels the jobs)
		 */

		KBASE_KTRACE_ADD_JM(kbdev, JM_ZAP_NON_SCHEDULED, kctx, NULL, 0u, kbase_ctx_flag(kctx, KCTX_SCHEDULED));

		dev_dbg(kbdev->dev, "Zap: Ctx %pK scheduled=0", kctx);

		/* Only cancel jobs when we evicted from the
		 * queue. No Power Manager active reference was held.
		 *
		 * Having is_dying set ensures that this kills, and doesn't
		 * requeue
		 */
		kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx, false);

		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);
		mutex_unlock(&kctx->jctx.lock);
	} else {
		unsigned long flags;
		bool was_retained;
		CSTD_UNUSED(was_retained);

		/* Case c: didn't evict, but it is scheduled - it's in the Run
		 * Pool
		 */
		KBASE_KTRACE_ADD_JM(kbdev, JM_ZAP_SCHEDULED, kctx, NULL, 0u, kbase_ctx_flag(kctx, KCTX_SCHEDULED));
		dev_dbg(kbdev->dev, "Zap: Ctx %pK is in RunPool", kctx);

		/* Disable the ctx from submitting any more jobs */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

		kbasep_js_clear_submit_allowed(js_devdata, kctx);

		/* Retain and (later) release the context whilst it is now
		 * disallowed from submitting jobs - ensures that someone
		 * somewhere will be removing the context later on
		 */
		was_retained = kbase_ctx_sched_inc_refcount_nolock(kctx);

		/* Since it's scheduled and we have the jsctx_mutex, it must be
		 * retained successfully
		 */
		KBASE_DEBUG_ASSERT(was_retained);

		dev_dbg(kbdev->dev, "Zap: Ctx %pK Kill Any Running jobs", kctx);

		/* Cancel any remaining running jobs for this kctx - if any.
		 * Submit is disallowed which takes effect immediately, so no
		 * more new jobs will appear after we do this.
		 */
		kbase_backend_jm_kill_running_jobs_from_kctx(kctx);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);
		mutex_unlock(&kctx->jctx.lock);

		dev_dbg(kbdev->dev, "Zap: Ctx %pK Release (may or may not schedule out immediately)",
									kctx);

		kbasep_js_runpool_release_ctx(kbdev, kctx);
	}

	KBASE_KTRACE_ADD_JM(kbdev, JM_ZAP_DONE, kctx, NULL, 0u, 0u);

	/* After this, you must wait on both the
	 * kbase_jd_context::zero_jobs_wait and the
	 * kbasep_js_kctx_info::ctx::is_scheduled_waitq - to wait for the jobs
	 * to be destroyed, and the context to be de-scheduled (if it was on the
	 * runpool).
	 *
	 * kbase_jd_zap_context() will do this.
	 */
}

static inline int trace_get_refcnt(struct kbase_device *kbdev,
					struct kbase_context *kctx)
{
	return atomic_read(&kctx->refcount);
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
				     kbasep_js_ctx_job_cb *callback)
{
	struct kbase_device *kbdev;
	unsigned long flags;
	u32 js;

	kbdev = kctx->kbdev;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	KBASE_KTRACE_ADD_JM_REFCOUNT(kbdev, JS_POLICY_FOREACH_CTX_JOBS, kctx, NULL,
					0u, trace_get_refcnt(kbdev, kctx));

	/* Invoke callback on jobs on each slot in turn */
	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++)
		jsctx_queue_foreach(kctx, js, callback);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

base_jd_prio kbase_js_priority_check(struct kbase_device *kbdev, base_jd_prio priority)
{
	struct priority_control_manager_device *pcm_device = kbdev->pcm_dev;
	int req_priority, out_priority;

	req_priority = kbasep_js_atom_prio_to_sched_prio(priority);
	out_priority = req_priority;
	/* Does not use pcm defined priority check if PCM not defined or if
	 * kbasep_js_atom_prio_to_sched_prio returns an error
	 * (KBASE_JS_ATOM_SCHED_PRIO_INVALID).
	 */
	if (pcm_device && (req_priority != KBASE_JS_ATOM_SCHED_PRIO_INVALID))
		out_priority = pcm_device->ops.pcm_scheduler_priority_check(pcm_device, current,
									    req_priority);
	return kbasep_js_sched_prio_to_atom_prio(kbdev, out_priority);
}

