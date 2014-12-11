/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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





/**
 * @file mali_kbase_js_policy.h
 * Job Scheduler Policy APIs.
 */

#ifndef _KBASE_JS_POLICY_H_
#define _KBASE_JS_POLICY_H_

/**
 * @page page_kbase_js_policy Job Scheduling Policies
 * The Job Scheduling system is described in the following:
 * - @subpage page_kbase_js_policy_overview
 * - @subpage page_kbase_js_policy_operation
 *
 * The API details are as follows:
 * - @ref kbase_jm
 * - @ref kbase_js
 * - @ref kbase_js_policy
 */

/**
 * @page page_kbase_js_policy_overview Overview of the Policy System
 *
 * The Job Scheduler Policy manages:
 * - The assigning of KBase Contexts to GPU Address Spaces (\em ASs)
 * - The choosing of Job Chains (\em Jobs) from a KBase context, to run on the
 * GPU's Job Slots (\em JSs).
 * - The amount of \em time a context is assigned to (<em>scheduled on</em>) an
 * Address Space
 * - The amount of \em time a Job spends running on the GPU
 *
 * The Policy implements this management via 2 components:
 * - A Policy Queue, which manages a set of contexts that are ready to run,
 * but not currently running.
 * - A Policy Run Pool, which manages the currently running contexts (one per Address
 * Space) and the jobs to run on the Job Slots.
 *
 * Each Graphics Process in the system has at least one KBase Context. Therefore,
 * the Policy Queue can be seen as a queue of Processes waiting to run Jobs on
 * the GPU.
 *
 * <!-- The following needs to be all on one line, due to doxygen's parser -->
 * @dotfile policy_overview.dot "Diagram showing a very simplified overview of the Policy System. IRQ handling, soft/hard-stopping, contexts re-entering the system and Policy details are omitted"
 *
 * The main operations on the queue are:
 * - Enqueuing a Context to it
 * - Dequeuing a Context from it, to run it.
 * - Note: requeuing a context is much the same as enqueuing a context, but
 * occurs when a context is scheduled out of the system to allow other contexts
 * to run.
 *
 * These operations have much the same meaning for the Run Pool - Jobs are
 * dequeued to run on a Jobslot, and requeued when they are scheduled out of
 * the GPU.
 *
 * @note This is an over-simplification of the Policy APIs - there are more
 * operations than 'Enqueue'/'Dequeue', and a Dequeue from the Policy Queue
 * takes at least two function calls: one to Dequeue from the Queue, one to add
 * to the Run Pool.
 *
 * As indicated on the diagram, Jobs permanently leave the scheduling system
 * when they are completed, otherwise they get dequeued/requeued until this
 * happens. Similarly, Contexts leave the scheduling system when their jobs
 * have all completed. However, Contexts may later return to the scheduling
 * system (not shown on the diagram) if more Bags of Jobs are submitted to
 * them.
 */

/**
 * @page page_kbase_js_policy_operation Policy Operation
 *
 * We describe the actions that the Job Scheduler Core takes on the Policy in
 * the following cases:
 * - The IRQ Path
 * - The Job Submission Path
 * - The High Priority Job Submission Path
 *
 * This shows how the Policy APIs will be used by the Job Scheduler core.
 *
 * The following diagram shows an example Policy that contains a Low Priority
 * queue, and a Real-time (High Priority) Queue. The RT queue is examined
 * before the LowP one on dequeuing from the head. The Low Priority Queue is
 * ordered by time, and the RT queue is ordered by RT-priority, and then by
 * time. In addition, it shows that the Job Scheduler Core will start a
 * Soft-Stop Timer (SS-Timer) when it dequeue's and submits a job. The
 * Soft-Stop time is set by a global configuration value, and must be a value
 * appropriate for the policy. For example, this could include "don't run a
 * soft-stop timer" for a First-Come-First-Served (FCFS) policy.
 *
 * <!-- The following needs to be all on one line, due to doxygen's parser -->
 * @dotfile policy_operation_diagram.dot "Diagram showing the objects managed by an Example Policy, and the operations made upon these objects by the Job Scheduler Core."
 *
 * @section sec_kbase_js_policy_operation_prio Dealing with Priority
 *
 * Priority applies both to a context as a whole, and to the jobs within a
 * context. The jobs specify a priority in the base_jd_atom::prio member, which
 * is relative to that of the context. A positive setting indicates a reduction
 * in priority, whereas a negative setting indicates a boost in priority. Of
 * course, the boost in priority should only be honoured when the originating
 * process has sufficient priviledges, and should be ignored for unpriviledged
 * processes. The meaning of the combined priority value is up to the policy
 * itself, and could be a logarithmic scale instead of a linear scale (e.g. the
 * policy could implement an increase/decrease in priority by 1 results in an
 * increase/decrease in \em proportion of time spent scheduled in by 25%, an
 * effective change in timeslice by 11%).
 *
 * It is up to the policy whether a boost in priority boosts the priority of
 * the entire context (e.g. to such an extent where it may pre-empt other
 * running contexts). If it chooses to do this, the Policy must make sure that
 * only the high-priority jobs are run, and that the context is scheduled out
 * once only low priority jobs remain. This ensures that the low priority jobs
 * within the context do not gain from the priority boost, yet they still get
 * scheduled correctly with respect to other low priority contexts.
 *
 *
 * @section sec_kbase_js_policy_operation_irq IRQ Path
 *
 * The following happens on the IRQ path from the Job Scheduler Core:
 * - Note the slot that completed (for later)
 * - Log the time spent by the job (and implicitly, the time spent by the
 * context)
 *  - call kbasep_js_policy_log_job_result() <em>in the context of the irq
 * handler.</em>
 *  - This must happen regardless of whether the job completed successfully or
 * not (otherwise the context gets away with DoS'ing the system with faulty jobs)
 * - What was the result of the job?
 *  - If Completed: job is just removed from the system
 *  - If Hard-stop or failure: job is removed from the system
 *  - If Soft-stop: queue the book-keeping work onto a work-queue: have a
 * work-queue call kbasep_js_policy_enqueue_job()
 * - Check the timeslice used by the owning context
 *  - call kbasep_js_policy_should_remove_ctx() <em>in the context of the irq
 * handler.</em>
 *  - If this returns true, clear the "allowed" flag.
 * - Check the ctx's flags for "allowed", "has jobs to run" and "is running
 * jobs"
 * - And so, should the context stay scheduled in?
 *  - If No, push onto a work-queue the work of scheduling out the old context,
 * and getting a new one. That is:
 *   - kbasep_js_policy_runpool_remove_ctx() on old_ctx
 *   - kbasep_js_policy_enqueue_ctx() on old_ctx
 *   - kbasep_js_policy_dequeue_head_ctx() to get new_ctx
 *   - kbasep_js_policy_runpool_add_ctx() on new_ctx
 *   - (all of this work is deferred on a work-queue to keep the IRQ handler quick)
 * - If there is space in the completed job slots' HEAD/NEXT registers, run the next job:
 *  - kbasep_js_policy_dequeue_job() <em>in the context of the irq
 * handler</em> with core_req set to that of the completing slot
 *  - if this returned MALI_TRUE, submit the job to the completed slot.
 *  - This is repeated until kbasep_js_policy_dequeue_job() returns
 * MALI_FALSE, or the job slot has a job queued on both the HEAD and NEXT registers.
 *  - If kbasep_js_policy_dequeue_job() returned false, submit some work to
 * the work-queue to retry from outside of IRQ context (calling
 * kbasep_js_policy_dequeue_job() from a work-queue).
 *
 * Since the IRQ handler submits new jobs \em and re-checks the IRQ_RAWSTAT,
 * this sequence could loop a large number of times: this could happen if
 * the jobs submitted completed on the GPU very quickly (in a few cycles), such
 * as GPU NULL jobs. Then, the HEAD/NEXT registers will always be free to take
 * more jobs, causing us to loop until we run out of jobs.
 *
 * To mitigate this, we must limit the number of jobs submitted per slot during
 * the IRQ handler - for example, no more than 2 jobs per slot per IRQ should
 * be sufficient (to fill up the HEAD + NEXT registers in normal cases). For
 * Mali-T600 with 3 job slots, this means that up to 6 jobs could be submitted per
 * slot. Note that IRQ Throttling can make this situation commonplace: 6 jobs
 * could complete but the IRQ for each of them is delayed by the throttling. By
 * the time you get the IRQ, all 6 jobs could've completed, meaning you can
 * submit jobs to fill all 6 HEAD+NEXT registers again.
 *
 * @note As much work is deferred as possible, which includes the scheduling
 * out of a context and scheduling in a new context. However, we can still make
 * starting a single high-priorty context quick despite this:
 * - On Mali-T600 family, there is one more AS than JSs.
 * - This means we can very quickly schedule out one AS, no matter what the
 * situation (because there will always be one AS that's not currently running
 * on the job slot - it can only have a job in the NEXT register).
 *  - Even with this scheduling out, fair-share can still be guaranteed e.g. by
 * a timeline-based Completely Fair Scheduler.
 * - When our high-priority context comes in, we can do this quick-scheduling
 * out immediately, and then schedule in the high-priority context without having to block.
 * - This all assumes that the context to schedule out is of lower
 * priority. Otherwise, we will have to block waiting for some other low
 * priority context to finish its jobs. Note that it's likely (but not
 * impossible) that the high-priority context \b is running jobs, by virtue of
 * it being high priority.
 * - Therefore, we can give a high liklihood that on Mali-T600 at least one
 * high-priority context can be started very quickly. For the general case, we
 * can guarantee starting (no. ASs) - (no. JSs) high priority contexts
 * quickly. In any case, there is a high likelihood that we're able to start
 * more than one high priority context quickly.
 *
 * In terms of the functions used in the IRQ handler directly, these are the
 * perfomance considerations:
 * - kbase_js_policy_log_job_result():
 *  - This is just adding to a 64-bit value (possibly even a 32-bit value if we
 * only store the time the job's recently spent - see below on 'priority weighting')
 *  - For priority weighting, a divide operation ('div') could happen, but
 * this can happen in a deferred context (outside of IRQ) when scheduling out
 * the ctx; as per our Engineering Specification, the contexts of different
 * priority still stay scheduled in for the same timeslice, but higher priority
 * ones scheduled back in more often.
 *  - That is, the weighted and unweighted times must be stored separately, and
 * the weighted time is only updated \em outside of IRQ context.
 *  - Of course, this divide is more likely to be a 'multiply by inverse of the
 * weight', assuming that the weight (priority) doesn't change.
 * - kbasep_js_policy_should_remove_ctx():
 *  - This is usually just a comparison of the stored time value against some
 * maximum value.
 *
 * @note all deferred work can be wrapped up into one call - we usually need to
 * indicate that a job/bag is done outside of IRQ context anyway.
 *
 *
 *
 * @section sec_kbase_js_policy_operation_submit Submission path
 *
 * Start with a Context with no jobs present, and assume equal priority of all
 * contexts in the system. The following work all happens outside of IRQ
 * Context :
 * - As soon as job is made 'ready to 'run', then is must be registerd with the Job
 * Scheduler Policy:
 *  - 'Ready to run' means they've satisified their dependencies in the
 * Kernel-side Job Dispatch system.
 *  - Call kbasep_js_policy_enqueue_job()
 *  - This indicates that the job should be scheduled (it is ready to run).
 * - As soon as a ctx changes from having 0 jobs 'ready to run' to >0 jobs
 * 'ready to run', we enqueue the context on the policy queue:
 *  - Call kbasep_js_policy_enqueue_ctx()
 *  - This indicates that the \em ctx should be scheduled (it is ready to run)
 *
 * Next, we need to handle adding a context to the Run Pool - if it's sensible
 * to do so. This can happen due to two reasons:
 * -# A context is enqueued as above, and there are ASs free for it to run on
 * (e.g. it is the first context to be run, in which case it can be added to
 * the Run Pool immediately after enqueuing on the Policy Queue)
 * -# A previous IRQ caused another ctx to be scheduled out, requiring that the
 * context at the head of the queue be scheduled in. Such steps would happen in
 * a work queue (work deferred from the IRQ context).
 *
 * In both cases, we'd handle it as follows:
 * - Get the context at the Head of the Policy Queue:
 *  - Call kbasep_js_policy_dequeue_head_ctx()
 * - Assign the Context an Address Space (Assert that there will be one free,
 * given the above two reasons)
 * - Add this context to the Run Pool:
 *  - Call kbasep_js_policy_runpool_add_ctx()
 * - Now see if a job should be run:
 *  - Mostly, this will be done in the IRQ handler at the completion of a
 * previous job.
 *  - However, there are two cases where this cannot be done: a) The first job
 * enqueued to the system (there is no previous IRQ to act upon) b) When jobs
 * are submitted at a low enough rate to not fill up all Job Slots (or, not to
 * fill both the 'HEAD' and 'NEXT' registers in the job-slots)
 *  - Hence, on each ctx <b>and job</b> submission we should try to see if we
 * can run a job:
 *  - For each job slot that has free space (in NEXT or HEAD+NEXT registers):
 *   - Call kbasep_js_policy_dequeue_job() with core_req set to that of the
 * slot
 *   - if we got one, submit it to the job slot.
 *   - This is repeated until kbasep_js_policy_dequeue_job() returns
 * MALI_FALSE, or the job slot has a job queued on both the HEAD and NEXT registers.
 *
 * The above case shows that we should attempt to run jobs in cases where a) a ctx
 * has been added to the Run Pool, and b) new jobs have been added to a context
 * in the Run Pool:
 * - In the latter case, the context is in the runpool because it's got a job
 * ready to run, or is already running a job
 * - We could just wait until the IRQ handler fires, but for certain types of
 * jobs this can take comparatively a long time to complete, e.g. GLES FS jobs
 * generally take much longer to run that GLES CS jobs, which are vertex shader
 * jobs.
 * - Therefore, when a new job appears in the ctx, we must check the job-slots
 * to see if they're free, and run the jobs as before.
 *
 *
 *
 * @section sec_kbase_js_policy_operation_submit_hipri Submission path for High Priority Contexts
 *
 * For High Priority Contexts on Mali-T600, we can make sure that at least 1 of
 * them can be scheduled in immediately to start high prioriy jobs. In general,
 * (no. ASs) - (no JSs) high priority contexts may be started immediately. The
 * following describes how this happens:
 *
 * Similar to the previous section, consider what happens with a high-priority
 * context (a context with a priority higher than that of any in the Run Pool)
 * that starts out with no jobs:
 * - A job becomes ready to run on the context, and so we enqueue the context
 * on the Policy's Queue.
 * - However, we'd like to schedule in this context immediately, instead of
 * waiting for one of the Run Pool contexts' timeslice to expire
 * - The policy's Enqueue function must detect this (because it is the policy
 * that embodies the concept of priority), and take appropriate action
 *  - That is, kbasep_js_policy_enqueue_ctx() should check the Policy's Run
 * Pool to see if a lower priority context should be scheduled out, and then
 * schedule in the High Priority context.
 *  - For Mali-T600, we can always pick a context to schedule out immediately
 * (because there are more ASs than JSs), and so scheduling out a victim context
 * and scheduling in the high priority context can happen immediately.
 *   - If a policy implements fair-sharing, then this can still ensure the
 * victim later on gets a fair share of the GPU.
 *   - As a note, consider whether the victim can be of equal/higher priority
 * than the incoming context:
 *   - Usually, higher priority contexts will be the ones currently running
 * jobs, and so the context with the lowest priority is usually not running
 * jobs.
 *   - This makes it likely that the victim context is low priority, but
 * it's not impossible for it to be a high priority one:
 *    - Suppose 3 high priority contexts are submitting only FS jobs, and one low
 * priority context submitting CS jobs. Then, the context not running jobs will
 * be one of the hi priority contexts (because only 2 FS jobs can be
 * queued/running on the GPU HW for Mali-T600).
 *   - The problem can be mitigated by extra action, but it's questionable
 * whether we need to: we already have a high likelihood that there's at least
 * one high priority context - that should be good enough.
 *   - And so, this method makes sure that at least one high priority context
 * can be started very quickly, but more than one high priority contexts could be
 * delayed (up to one timeslice).
 *   - To improve this, use a GPU with a higher number of Address Spaces vs Job
 * Slots.
 * - At this point, let's assume this high priority context has been scheduled
 * in immediately. The next step is to ensure it can start some jobs quickly.
 *  - It must do this by Soft-Stopping jobs on any of the Job Slots that it can
 * submit to.
 *  - The rest of the logic for starting the jobs is taken care of by the IRQ
 * handler. All the policy needs to do is ensure that
 * kbasep_js_policy_dequeue_job() will return the jobs from the high priority
 * context.
 *
 * @note in SS state, we currently only use 2 job-slots (even for T608, but
 * this might change in future). In this case, it's always possible to schedule
 * out 2 ASs quickly (their jobs won't be in the HEAD registers). At the same
 * time, this maximizes usage of the job-slots (only 2 are in use), because you
 * can guarantee starting of the jobs from the High Priority contexts immediately too.
 *
 *
 *
 * @section sec_kbase_js_policy_operation_notes Notes
 *
 * - In this design, a separate 'init' is needed from dequeue/requeue, so that
 * information can be retained between the dequeue/requeue calls. For example,
 * the total time spent for a context/job could be logged between
 * dequeue/requeuing, to implement Fair Sharing. In this case, 'init' just
 * initializes that information to some known state.
 *
 *
 *
 */

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_kbase_api
 * @{
 */

/**
 * @addtogroup kbase_js_policy Job Scheduler Policy APIs
 * @{
 *
 * <b>Refer to @ref page_kbase_js_policy for an overview and detailed operation of
 * the Job Scheduler Policy and its use from the Job Scheduler Core</b>.
 */

/**
 * @brief Job Scheduler Policy structure
 */
union kbasep_js_policy;

/**
 * @brief Initialize the Job Scheduler Policy
 */
mali_error kbasep_js_policy_init(struct kbase_device *kbdev);

/**
 * @brief Terminate the Job Scheduler Policy
 */
void kbasep_js_policy_term(union kbasep_js_policy *js_policy);

/**
 * @addtogroup kbase_js_policy_ctx Job Scheduler Policy, Context Management API
 * @{
 *
 * <b>Refer to @ref page_kbase_js_policy for an overview and detailed operation of
 * the Job Scheduler Policy and its use from the Job Scheduler Core</b>.
 */

/**
 * @brief Job Scheduler Policy Ctx Info structure
 *
 * This structure is embedded in the struct kbase_context structure. It is used to:
 * - track information needed for the policy to schedule the context (e.g. time
 * used, OS priority etc.)
 * - link together kbase_contexts into a queue, so that a struct kbase_context can be
 * obtained as the container of the policy ctx info. This allows the API to
 * return what "the next context" should be.
 * - obtain other information already stored in the struct kbase_context for
 * scheduling purposes (e.g process ID to get the priority of the originating
 * process)
 */
union kbasep_js_policy_ctx_info;

/**
 * @brief Initialize a ctx for use with the Job Scheduler Policy
 *
 * This effectively initializes the union kbasep_js_policy_ctx_info structure within
 * the struct kbase_context (itself located within the kctx->jctx.sched_info structure).
 */
mali_error kbasep_js_policy_init_ctx(struct kbase_device *kbdev, struct kbase_context *kctx);

/**
 * @brief Terminate resources associated with using a ctx in the Job Scheduler
 * Policy.
 */
void kbasep_js_policy_term_ctx(union kbasep_js_policy *js_policy, struct kbase_context *kctx);

/**
 * @brief Enqueue a context onto the Job Scheduler Policy Queue
 *
 * If the context enqueued has a priority higher than any in the Run Pool, then
 * it is the Policy's responsibility to decide whether to schedule out a low
 * priority context from the Run Pool to allow the high priority context to be
 * scheduled in.
 *
 * If the context has the privileged flag set, it will always be kept at the
 * head of the queue.
 *
 * The caller will be holding kbasep_js_kctx_info::ctx::jsctx_mutex.
 * The caller will be holding kbasep_js_device_data::queue_mutex.
 */
void kbasep_js_policy_enqueue_ctx(union kbasep_js_policy *js_policy, struct kbase_context *kctx);

/**
 * @brief Dequeue a context from the Head of the Job Scheduler Policy Queue
 *
 * The caller will be holding kbasep_js_device_data::queue_mutex.
 *
 * @return MALI_TRUE if a context was available, and *kctx_ptr points to
 * the kctx dequeued.
 * @return MALI_FALSE if no contexts were available.
 */
mali_bool kbasep_js_policy_dequeue_head_ctx(union kbasep_js_policy *js_policy, struct kbase_context ** const kctx_ptr);

/**
 * @brief Evict a context from the Job Scheduler Policy Queue
 *
 * This is only called as part of destroying a kbase_context.
 *
 * There are many reasons why this might fail during the lifetime of a
 * context. For example, the context is in the process of being scheduled. In
 * that case a thread doing the scheduling might have a pointer to it, but the
 * context is neither in the Policy Queue, nor is it in the Run
 * Pool. Crucially, neither the Policy Queue, Run Pool, or the Context itself
 * are locked.
 *
 * Hence to find out where in the system the context is, it is important to do
 * more than just check the kbasep_js_kctx_info::ctx::is_scheduled member.
 *
 * The caller will be holding kbasep_js_device_data::queue_mutex.
 *
 * @return MALI_TRUE if the context was evicted from the Policy Queue
 * @return MALI_FALSE if the context was not found in the Policy Queue
 */
mali_bool kbasep_js_policy_try_evict_ctx(union kbasep_js_policy *js_policy, struct kbase_context *kctx);

/**
 * @brief Call a function on all jobs belonging to a non-queued, non-running
 * context, optionally detaching the jobs from the context as it goes.
 *
 * At the time of the call, the context is guarenteed to be not-currently
 * scheduled on the Run Pool (is_scheduled == MALI_FALSE), and not present in
 * the Policy Queue. This is because one of the following functions was used
 * recently on the context:
 * - kbasep_js_policy_evict_ctx()
 * - kbasep_js_policy_runpool_remove_ctx()
 *
 * In both cases, no subsequent call was made on the context to any of:
 * - kbasep_js_policy_runpool_add_ctx()
 * - kbasep_js_policy_enqueue_ctx()
 *
 * Due to the locks that might be held at the time of the call, the callback
 * may need to defer work on a workqueue to complete its actions (e.g. when
 * cancelling jobs)
 *
 * \a detach_jobs must only be set when cancelling jobs (which occurs as part
 * of context destruction).
 *
 * The locking conditions on the caller are as follows:
 * - it will be holding kbasep_js_kctx_info::ctx::jsctx_mutex.
 */
void kbasep_js_policy_foreach_ctx_job(union kbasep_js_policy *js_policy, struct kbase_context *kctx,
	kbasep_js_policy_ctx_job_cb callback, mali_bool detach_jobs);

/**
 * @brief Add a context to the Job Scheduler Policy's Run Pool
 *
 * If the context enqueued has a priority higher than any in the Run Pool, then
 * it is the Policy's responsibility to decide whether to schedule out low
 * priority jobs that are currently running on the GPU.
 *
 * The number of contexts present in the Run Pool will never be more than the
 * number of Address Spaces.
 *
 * The following guarentees are made about the state of the system when this
 * is called:
 * - kctx->as_nr member is valid
 * - the context has its submit_allowed flag set
 * - kbasep_js_device_data::runpool_irq::per_as_data[kctx->as_nr] is valid
 * - The refcount of the context is guarenteed to be zero.
 * - kbasep_js_kctx_info::ctx::is_scheduled will be MALI_TRUE.
 *
 * The locking conditions on the caller are as follows:
 * - it will be holding kbasep_js_kctx_info::ctx::jsctx_mutex.
 * - it will be holding kbasep_js_device_data::runpool_mutex.
 * - it will be holding kbasep_js_device_data::runpool_irq::lock (a spinlock)
 *
 * Due to a spinlock being held, this function must not call any APIs that sleep.
 */
void kbasep_js_policy_runpool_add_ctx(union kbasep_js_policy *js_policy, struct kbase_context *kctx);

/**
 * @brief Remove a context from the Job Scheduler Policy's Run Pool
 *
 * The kctx->as_nr member is valid and the context has its submit_allowed flag
 * set when this is called. The state of
 * kbasep_js_device_data::runpool_irq::per_as_data[kctx->as_nr] is also
 * valid. The refcount of the context is guarenteed to be zero.
 *
 * The locking conditions on the caller are as follows:
 * - it will be holding kbasep_js_kctx_info::ctx::jsctx_mutex.
 * - it will be holding kbasep_js_device_data::runpool_mutex.
 * - it will be holding kbasep_js_device_data::runpool_irq::lock (a spinlock)
 *
 * Due to a spinlock being held, this function must not call any APIs that sleep.
 */
void kbasep_js_policy_runpool_remove_ctx(union kbasep_js_policy *js_policy, struct kbase_context *kctx);

/**
 * @brief Indicate whether a context should be removed from the Run Pool
 * (should be scheduled out).
 *
 * The kbasep_js_device_data::runpool_irq::lock will be held by the caller.
 *
 * @note This API is called from IRQ context.
 */
mali_bool kbasep_js_policy_should_remove_ctx(union kbasep_js_policy *js_policy, struct kbase_context *kctx);

/**
 * @brief Synchronize with any timers acting upon the runpool
 *
 * The policy should check whether any timers it owns should be running. If
 * they should not, the policy must cancel such timers and ensure they are not
 * re-run by the time this function finishes.
 *
 * In particular, the timers must not be running when there are no more contexts
 * on the runpool, because the GPU could be powered off soon after this call.
 *
 * The locking conditions on the caller are as follows:
 * - it will be holding kbasep_js_kctx_info::ctx::jsctx_mutex.
 * - it will be holding kbasep_js_device_data::runpool_mutex.
 */
void kbasep_js_policy_runpool_timers_sync(union kbasep_js_policy *js_policy);


/**
 * @brief Indicate whether a new context has an higher priority than the current context.
 *
 *
 * The caller has the following conditions on locking:
 * - kbasep_js_kctx_info::ctx::jsctx_mutex will be held for \a new_ctx
 *
 * This function must not sleep, because an IRQ spinlock might be held whilst
 * this is called.
 *
 * @note There is nothing to stop the priority of \a current_ctx changing
 * during or immediately after this function is called (because its jsctx_mutex
 * cannot be held). Therefore, this function should only be seen as a heuristic
 * guide as to whether \a new_ctx is higher priority than \a current_ctx
 */
mali_bool kbasep_js_policy_ctx_has_priority(union kbasep_js_policy *js_policy, struct kbase_context *current_ctx, struct kbase_context *new_ctx);

	  /** @} *//* end group kbase_js_policy_ctx */

/**
 * @addtogroup kbase_js_policy_job Job Scheduler Policy, Job Chain Management API
 * @{
 *
 * <b>Refer to @ref page_kbase_js_policy for an overview and detailed operation of
 * the Job Scheduler Policy and its use from the Job Scheduler Core</b>.
 */

/**
 * @brief Job Scheduler Policy Job Info structure
 *
 * This structure is embedded in the struct kbase_jd_atom structure. It is used to:
 * - track information needed for the policy to schedule the job (e.g. time
 * used, OS priority etc.)
 * - link together jobs into a queue/buffer, so that a struct kbase_jd_atom can be
 * obtained as the container of the policy job info. This allows the API to
 * return what "the next job" should be.
 * - obtain other information already stored in the struct kbase_context for
 * scheduling purposes (e.g user-side relative priority)
 */
union kbasep_js_policy_job_info;

/**
 * @brief Initialize a job for use with the Job Scheduler Policy
 *
 * This function initializes the union kbasep_js_policy_job_info structure within the
 * kbase_jd_atom. It will only initialize/allocate resources that are specific
 * to the job.
 *
 * That is, this function makes \b no attempt to:
 * - initialize any context/policy-wide information
 * - enqueue the job on the policy.
 *
 * At some later point, the following functions must be called on the job, in this order:
 * - kbasep_js_policy_register_job() to register the job and initialize policy/context wide data.
 * - kbasep_js_policy_enqueue_job() to enqueue the job
 *
 * A job must only ever be initialized on the Policy once, and must be
 * terminated on the Policy before the job is freed.
 *
 * The caller will not be holding any locks, and so this function will not
 * modify any information in \a kctx or \a js_policy.
 *
 * @return MALI_ERROR_NONE if initialization was correct.
 */
mali_error kbasep_js_policy_init_job(const union kbasep_js_policy *js_policy, const struct kbase_context *kctx, struct kbase_jd_atom *katom);

/**
 * @brief Register context/policy-wide information for a job on the Job Scheduler Policy.
 *
 * Registers the job with the policy. This is used to track the job before it
 * has been enqueued/requeued by kbasep_js_policy_enqueue_job(). Specifically,
 * it is used to update information under a lock that could not be updated at
 * kbasep_js_policy_init_job() time (such as context/policy-wide data).
 *
 * @note This function will not fail, and hence does not allocate any
 * resources. Any failures that could occur on registration will be caught
 * during kbasep_js_policy_init_job() instead.
 *
 * A job must only ever be registerd on the Policy once, and must be
 * deregistered on the Policy on completion (whether or not that completion was
 * success/failure).
 *
 * The caller has the following conditions on locking:
 * - kbasep_js_kctx_info::ctx::jsctx_mutex will be held.
 */
void kbasep_js_policy_register_job(union kbasep_js_policy *js_policy, struct kbase_context *kctx, struct kbase_jd_atom *katom);

/**
 * @brief De-register context/policy-wide information for a on the Job Scheduler Policy.
 *
 * This must be used before terminating the resources associated with using a
 * job in the Job Scheduler Policy. This function does not itself terminate any
 * resources, at most it just updates information in the policy and context.
 *
 * The caller has the following conditions on locking:
 * - kbasep_js_kctx_info::ctx::jsctx_mutex will be held.
 */
void kbasep_js_policy_deregister_job(union kbasep_js_policy *js_policy, struct kbase_context *kctx, struct kbase_jd_atom *katom);

/**
 * @brief Dequeue a Job for a job slot from the Job Scheduler Policy Run Pool
 *
 * The job returned by the policy will match at least one of the bits in the
 * job slot's core requirements (but it may match more than one, or all @ref
 * base_jd_core_req bits supported by the job slot).
 *
 * In addition, the requirements of the job returned will be a subset of those
 * requested - the job returned will not have requirements that \a job_slot_idx
 * cannot satisfy.
 *
 * The caller will submit the job to the GPU as soon as the GPU's NEXT register
 * for the corresponding slot is empty. Of course, the GPU will then only run
 * this new job when the currently executing job (in the jobslot's HEAD
 * register) has completed.
 *
 * @return MALI_TRUE if a job was available, and *kctx_ptr points to
 * the kctx dequeued.
 * @return MALI_FALSE if no jobs were available among all ctxs in the Run Pool.
 *
 * @note base_jd_core_req is currently a u8 - beware of type conversion.
 *
 * The caller has the following conditions on locking:
 * - kbasep_js_device_data::runpool_lock::irq will be held.
 * - kbasep_js_device_data::runpool_mutex will be held.
 * - kbasep_js_kctx_info::ctx::jsctx_mutex. will be held
 */
mali_bool kbasep_js_policy_dequeue_job(struct kbase_device *kbdev, int job_slot_idx, struct kbase_jd_atom ** const katom_ptr);

/**
 * @brief Requeue a Job back into the the Job Scheduler Policy Run Pool
 *
 * This will be used to enqueue a job after its creation and also to requeue
 * a job into the Run Pool that was previously dequeued (running). It notifies
 * the policy that the job should be run again at some point later.
 *
 * The caller has the following conditions on locking:
 * - kbasep_js_device_data::runpool_irq::lock (a spinlock) will be held.
 * - kbasep_js_device_data::runpool_mutex will be held.
 * - kbasep_js_kctx_info::ctx::jsctx_mutex will be held.
 */
void kbasep_js_policy_enqueue_job(union kbasep_js_policy *js_policy, struct kbase_jd_atom *katom);

/**
 * @brief Log the result of a job: the time spent on a job/context, and whether
 * the job failed or not.
 *
 * Since a struct kbase_jd_atom contains a pointer to the struct kbase_context owning it,
 * then this can also be used to log time on either/both the job and the
 * containing context.
 *
 * The completion state of the job can be found by examining \a katom->event.event_code
 *
 * If the Job failed and the policy is implementing fair-sharing, then the
 * policy must penalize the failing job/context:
 * - At the very least, it should penalize the time taken by the amount of
 * time spent processing the IRQ in SW. This because a job in the NEXT slot
 * waiting to run will be delayed until the failing job has had the IRQ
 * cleared.
 * - \b Optionally, the policy could apply other penalties. For example, based
 * on a threshold of a number of failing jobs, after which a large penalty is
 * applied.
 *
 * The kbasep_js_device_data::runpool_mutex will be held by the caller.
 *
 * @note This API is called from IRQ context.
 *
 * The caller has the following conditions on locking:
 * - kbasep_js_device_data::runpool_irq::lock will be held.
 *
 * @param js_policy     job scheduler policy
 * @param katom         job dispatch atom
 * @param time_spent_us the time spent by the job, in microseconds (10^-6 seconds).
 */
void kbasep_js_policy_log_job_result(union kbasep_js_policy *js_policy, struct kbase_jd_atom *katom, u64 time_spent_us);

	  /** @} *//* end group kbase_js_policy_job */

	  /** @} *//* end group kbase_js_policy */
	  /** @} *//* end group base_kbase_api */
	  /** @} *//* end group base_api */

#endif				/* _KBASE_JS_POLICY_H_ */
