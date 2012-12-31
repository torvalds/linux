/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_uku.h>

#define beenthere(f, a...)  OSK_PRINT_INFO(OSK_BASE_JD, "%s:" f, __func__, ##a)

/*
 * This is the kernel side of the API. Only entry points are:
 * - kbase_jd_submit(): Called from userspace to submit a single bag
 * - kbase_jd_done(): Called from interrupt context to track the
 *   completion of a job.
 * Callouts:
 * - to the job manager (enqueue a job)
 * - to the event subsystem (signals the completion/failure of bag/job-chains).
 */

STATIC INLINE void dep_raise_sem(u32 *sem, u8 dep)
{
	if (!dep)
		return;

	sem[BASEP_JD_SEM_WORD_NR(dep)] |= BASEP_JD_SEM_MASK_IN_WORD(dep);
}
KBASE_EXPORT_TEST_API(dep_raise_sem)

STATIC INLINE void dep_clear_sem(u32 *sem, u8 dep)
{
	if (!dep)
		return;

	sem[BASEP_JD_SEM_WORD_NR(dep)] &= ~BASEP_JD_SEM_MASK_IN_WORD(dep);
}
KBASE_EXPORT_TEST_API(dep_clear_sem)

STATIC INLINE int dep_get_sem(u32 *sem, u8 dep)
{
	if (!dep)
		return 0;

	return !!(sem[BASEP_JD_SEM_WORD_NR(dep)] & BASEP_JD_SEM_MASK_IN_WORD(dep));
}
KBASE_EXPORT_TEST_API(dep_get_sem)

STATIC INLINE mali_bool jd_add_dep(kbase_jd_context *ctx,
                                   kbase_jd_atom *katom, u8 d)
{
	kbase_jd_dep_queue *dq = &ctx->dep_queue;
	u8 s = katom->pre_dep.dep[d];

	if (!dep_get_sem(ctx->dep_queue.sem, s))
		return MALI_FALSE;

	/*
	 * The slot must be free already. If not, then something went
	 * wrong in the validate path.
	 */
	OSK_ASSERT(!dq->queue[s]);

	dq->queue[s] = katom;
	beenthere("queued %p slot %d", (void *)katom, s);

	return MALI_TRUE;
}
KBASE_EXPORT_TEST_API(jd_add_dep)

/*
 * This function only computes the address of the first possible
 * atom. It doesn't mean it's actually valid (jd_validate_atom takes
 * care of that).
 */
STATIC INLINE base_jd_atom *jd_get_first_atom(kbase_jd_context *ctx,
                                              kbase_jd_bag *bag)
{
	/* Check that offset is within pool */
	if ((bag->offset + sizeof(base_jd_atom)) > ctx->pool_size)
		return NULL;

	return  (base_jd_atom *)((char *)ctx->pool + bag->offset);
}
KBASE_EXPORT_TEST_API(jd_get_first_atom)

/*
 * Same as with jd_get_first_atom, but for any subsequent atom.
 */
STATIC INLINE base_jd_atom *jd_get_next_atom(kbase_jd_atom *katom)
{
	/* Think of adding extra padding for userspace */
	return (base_jd_atom *)base_jd_get_atom_syncset(katom->user_atom, katom->nr_syncsets);
}
KBASE_EXPORT_TEST_API(jd_get_next_atom)

/*
 * This will check atom for correctness and if so, initialize its js policy.
 */
STATIC INLINE kbase_jd_atom *jd_validate_atom(struct kbase_context *kctx,
                                              kbase_jd_bag *bag,
                                              base_jd_atom *atom,
                                              u32 *sem)
{
	kbase_jd_context *jctx = &kctx->jctx;
	kbase_jd_atom *katom;
	u32 nr_syncsets;
	base_jd_dep pre_dep;
	int nice_priority;

	/* Check the atom struct fits in the pool before we attempt to access it
	   Note: a bad bag->nr_atom could trigger this condition */
	if(((char *)atom + sizeof(base_jd_atom)) > ((char *)jctx->pool + jctx->pool_size))
		return NULL;

	nr_syncsets = atom->nr_syncsets;
	pre_dep = atom->pre_dep;

	/* Check that the whole atom fits within the pool.
	 * syncsets integrity will be performed as we execute them */
	if ((char *)base_jd_get_atom_syncset(atom, nr_syncsets) > ((char *)jctx->pool + jctx->pool_size))
		return NULL;

	/*
	 * Check that dependencies are sensible: the atom cannot have
	 * pre-dependencies that are already in use by another atom.
	 */
	if (jctx->dep_queue.queue[pre_dep.dep[0]] ||
	    jctx->dep_queue.queue[pre_dep.dep[1]])
		return NULL;

	/* Check for conflicting dependencies inside the bag */
	if (dep_get_sem(sem, pre_dep.dep[0]) ||
	    dep_get_sem(sem, pre_dep.dep[1]))
		return NULL;

	dep_raise_sem(sem, pre_dep.dep[0]);
	dep_raise_sem(sem, pre_dep.dep[1]);

	/* We surely want to preallocate a pool of those, or have some
	 * kind of slab allocator around */
	katom = osk_calloc(sizeof(*katom));
	if (!katom)
		return NULL;    /* Ideally we should handle OOM more gracefully */

	katom->user_atom    = atom;
	katom->pre_dep      = pre_dep;
	katom->post_dep     = atom->post_dep;
	katom->bag          = bag;
	katom->kctx         = kctx;
	katom->nr_syncsets  = nr_syncsets;
	katom->affinity     = 0;
	katom->core_req     = atom->core_req;
	katom->jc           = atom->jc;

	/*
	 * If the priority is increased we need to check the caller has security caps to do this, if
	 * prioirty is decreased then this is ok as the result will have no negative impact on other
	 * processes running.
	 */
	katom->nice_prio = atom->prio;
	if( 0 > katom->nice_prio)
	{
		mali_bool access_allowed;
		access_allowed = kbase_security_has_capability(kctx, KBASE_SEC_MODIFY_PRIORITY, KBASE_SEC_FLAG_NOAUDIT);
		if(!access_allowed)
		{
			/* For unprivileged processes - a negative priority is interpreted as zero */
			katom->nice_prio = 0;
		}
	}

	/* Scale priority range to use NICE range */
	if(katom->nice_prio)
	{
		/* Remove sign for calculation */
		nice_priority = katom->nice_prio+128;
		/* Fixed point maths to scale from ..255 to 0..39 (NICE range with +20 offset) */
		katom->nice_prio = (((20<<16)/128)*nice_priority)>>16;
	}

	/* pre-fill the event */
	katom->event.event_code = BASE_JD_EVENT_DONE;
	katom->event.data       = katom;

	/* Initialize the jobscheduler policy for this atom. Function will
	 * return error if the atom is malformed. Then inmediatelly terminate
	 * the policy to free allocated resources and return error.
	 *
	 * Soft-jobs never enter the job scheduler so we don't initialise the policy for these
	 */
	if ((katom->core_req & BASE_JD_REQ_SOFT_JOB) == 0)
	{
		kbasep_js_policy *js_policy = &(kctx->kbdev->js_data.policy);
		if (MALI_ERROR_NONE != kbasep_js_policy_init_job( js_policy, katom ))
		{
			osk_free( katom );
			return NULL;
		}
	}

	return katom;
}
KBASE_EXPORT_TEST_API(jd_validate_atom)

static void kbase_jd_cancel_bag(kbase_context *kctx, kbase_jd_bag *bag,
                                base_jd_event_code code)
{
	bag->event.event_code = code;
	kbase_event_post(kctx, &bag->event);
}

STATIC void kbase_jd_katom_dtor(kbase_event *event)
{
	kbase_jd_atom *katom = CONTAINER_OF(event, kbase_jd_atom, event);
	kbasep_js_policy *js_policy = &(katom->kctx->kbdev->js_data.policy);

	kbasep_js_policy_term_job( js_policy, katom );
	osk_free(katom);
}
KBASE_EXPORT_TEST_API(kbase_jd_katom_dtor)

STATIC mali_error kbase_jd_validate_bag(kbase_context *kctx,
                                        kbase_jd_bag *bag,
                                        osk_dlist *klistp)
{
	kbase_jd_context *jctx = &kctx->jctx;
	kbase_jd_atom *katom;
	base_jd_atom *atom;
	mali_error err = MALI_ERROR_NONE;
	u32 sem[BASEP_JD_SEM_ARRAY_SIZE] = { 0 };
	u32 i;

	atom = jd_get_first_atom(jctx, bag);
	if (!atom)
	{
		/* Bad start... */
		kbase_jd_cancel_bag(kctx, bag, BASE_JD_EVENT_BAG_INVALID);
		err = MALI_ERROR_FUNCTION_FAILED;
		goto out;
	}

	for (i = 0; i < bag->nr_atoms; i++)
	{
		katom = jd_validate_atom(kctx, bag, atom, sem);
		if (!katom)
		{
			OSK_DLIST_EMPTY_LIST(klistp, kbase_event,
			                     entry, kbase_jd_katom_dtor);
			kbase_jd_cancel_bag(kctx, bag, BASE_JD_EVENT_BAG_INVALID);
			err = MALI_ERROR_FUNCTION_FAILED;
			goto out;
		}

		OSK_DLIST_PUSH_BACK(klistp, &katom->event,
		                    kbase_event, entry);
		atom = jd_get_next_atom(katom);
	}

out:
	return err;
}
KBASE_EXPORT_TEST_API(kbase_jd_validate_bag)

STATIC INLINE kbase_jd_atom *jd_resolve_dep(kbase_jd_atom *katom, u8 d, int zapping)
{
	u8 other_dep;
	u8 dep;
	kbase_jd_atom *dep_katom;
	kbase_jd_context *ctx = &katom->kctx->jctx;

	dep = katom->post_dep.dep[d];

	if (!dep)
		return NULL;

	dep_clear_sem(ctx->dep_queue.sem, dep);

	/* Get the atom that's waiting for us (if any), and remove it
	 * from this particular dependency queue */
	dep_katom = ctx->dep_queue.queue[dep];

	/* Case of a dangling dependency */
	if (!dep_katom)
		return NULL;
	
	ctx->dep_queue.queue[dep] = NULL;

	beenthere("removed %p from slot %d",
		  (void *)dep_katom, dep);

	/* Find out if this atom is waiting for another job to be done.
	 * If it's not waiting anymore, put it on the run queue. */
	if (dep_katom->pre_dep.dep[0] == dep)
		other_dep = dep_katom->pre_dep.dep[1];
	else
		other_dep = dep_katom->pre_dep.dep[0];

	/*
	 * The following line seem to confuse people, so here's the
	 * rational behind it:
	 *
	 * The queue hold pointers to atoms waiting for a single
	 * pre-dependency to be satisfied. Above, we've already
	 * satisfied a pre-dep for an atom (dep_katom). The next step
	 * is to check whether this atom is now free to run, or has to
	 * wait for another pre-dep to be satisfied.
	 *
	 * For a single entry, 3 possibilities:
	 *
	 * - It's a pointer to dep_katom -> the pre-dep has not been
	 *   satisfied yet, and it cannot run immediately.
	 *
	 * - It's NULL -> the atom can be scheduled immediately, as
	 *   the dependency has already been satisfied.
	 *
	 * - Neither of the above: this is the case of a dependency
	 *   that has already been satisfied, and the slot reused by
	 *   an incoming atom -> dep_katom can be run immediately.
	 */
	if (ctx->dep_queue.queue[other_dep] != dep_katom)
		return dep_katom;

	/*
	 * We're on a killing spree. Cancel the additionnal
	 * dependency, and return the atom anyway. An unfortunate
	 * consequence is that userpace may receive notifications out
	 * of order WRT the dependency tree.
	 */
	if (zapping)
	{
		ctx->dep_queue.queue[other_dep] = NULL;
		return dep_katom;
	}

	beenthere("katom %p waiting for slot %d",
		  (void *)dep_katom, other_dep);
	return NULL;
}
KBASE_EXPORT_TEST_API(jd_resolve_dep)

/*
 * Perform the necessary handling of an atom that has finished running
 * on the GPU. The @a zapping parameter instruct the function to
 * propagate the state of the completed atom to all the atoms that
 * depend on it, directly or indirectly.
 *
 * This flag is used for error propagation in the "failed job", or
 * when destroying a context.
 *
 * When not zapping, the caller must hold the kbasep_js_kctx_info::ctx::jsctx_mutex.
 */
STATIC mali_bool jd_done_nolock(kbase_jd_atom *katom, int zapping)
{
	kbase_jd_atom *dep_katom;
	struct kbase_context *kctx = katom->kctx;
	osk_dlist ts;   /* traversal stack */
	osk_dlist *tsp = &ts;
	osk_dlist vl;   /* visited list */
	osk_dlist *vlp = &vl;
	kbase_jd_atom *node;
	base_jd_event_code event_code = katom->event.event_code;
	mali_bool need_to_try_schedule_context = MALI_FALSE;

	/*
	 * We're trying to achieve two goals here:
	 * - Eliminate dependency atoms very early so we can push real
	 *    jobs to the HW
	 * - Avoid recursion which could result in a nice DoS from
	 *    user-space.
	 *
	 * We use two lists here:
	 * - One as a stack (ts) to get rid of the recursion
	 * - The other to queue jobs that are either done or ready to
	 *   run.
	 */
	OSK_DLIST_INIT(tsp);
	OSK_DLIST_INIT(vlp);

	/* push */
	OSK_DLIST_PUSH_BACK(tsp, &katom->event, kbase_event, entry);

	while(!OSK_DLIST_IS_EMPTY(tsp))
	{
		/* pop */
		node = OSK_DLIST_POP_BACK(tsp, kbase_jd_atom, event.entry);

		if (node == katom ||
		    node->core_req == BASE_JD_REQ_DEP ||
		    zapping)
		{
			int i;
			for (i = 0; i < 2; i++)
			{
				dep_katom = jd_resolve_dep(node, i, zapping);
				if (dep_katom) /* push */
					OSK_DLIST_PUSH_BACK(tsp,
					                    &dep_katom->event,
					                    kbase_event,
					                    entry);
			}
		}

		OSK_DLIST_PUSH_BACK(vlp, &node->event,
		                    kbase_event, entry);
	}

	while(!OSK_DLIST_IS_EMPTY(vlp))
	{
		node = OSK_DLIST_POP_FRONT(vlp, kbase_jd_atom, event.entry);

		if (node == katom ||
		    node->core_req == BASE_JD_REQ_DEP ||
		    (node->core_req & BASE_JD_REQ_SOFT_JOB) ||
		    zapping)
		{
			kbase_jd_bag *bag = node->bag;

			/* If we're zapping stuff, propagate the event code */
			if (zapping)
			{
				node->event.event_code = event_code;
			}
			else if (node->core_req & BASE_JD_REQ_SOFT_JOB)
			{
				kbase_process_soft_job( kctx, node );
			}

			/* This will signal our per-context worker
			 * thread that we're done with this katom. Any
			 * use of this katom after that point IS A
			 * ERROR!!! */
			kbase_event_post(kctx, &node->event);
			beenthere("done atom %p\n", (void*)node);

			if (--bag->nr_atoms == 0)
			{
				/* This atom was the last, signal userspace */
				kbase_event_post(kctx, &bag->event);
				beenthere("done bag %p\n", (void*)bag);
			}

			/* Decrement and check the TOTAL number of jobs. This includes
			 * those not tracked by the scheduler: 'not ready to run' and
			 * 'dependency-only' jobs. */
			if (--kctx->jctx.job_nr == 0)
			{
				/* All events are safely queued now, and we can signal any waiter
				 * that we've got no more jobs (so we can be safely terminated) */
				osk_waitq_set(&kctx->jctx.zero_jobs_waitq);
			}
		}
		else
		{
			/* Queue an action about whether we should try scheduling a context */
			need_to_try_schedule_context |= kbasep_js_add_job( kctx, node );
		}
	}

	return need_to_try_schedule_context;
}
KBASE_EXPORT_TEST_API(jd_done_nolock)

mali_error kbase_jd_submit(kbase_context *kctx, const kbase_uk_job_submit *user_bag)
{
	osk_dlist klist;
	osk_dlist *klistp = &klist;
	kbase_jd_context *jctx = &kctx->jctx;
	kbase_jd_atom *katom;
	kbase_jd_bag *bag;
	mali_error err = MALI_ERROR_NONE;
	int i = -1;
	mali_bool need_to_try_schedule_context = MALI_FALSE;
	kbase_device *kbdev;

	/*
	 * kbase_jd_submit isn't expected to fail and so all errors with the jobs
	 * are reported by immediately falling them (through event system)
	 */

	kbdev = kctx->kbdev;

	beenthere("%s", "Enter");
	bag = osk_malloc(sizeof(*bag));
	if (NULL == bag)
	{
		err = MALI_ERROR_OUT_OF_MEMORY;
		goto out_bag;
	}

	bag->core_restriction       = user_bag->core_restriction;
	bag->offset                 = user_bag->offset;
	bag->nr_atoms               = user_bag->nr_atoms;
	bag->event.event_code       = BASE_JD_EVENT_BAG_DONE;
	bag->event.data             = (void *)(uintptr_t)user_bag->bag_uaddr;

	osk_mutex_lock(&jctx->lock);

	/*
	 * Use a transient list to store all the validated atoms.
	 * Once we're sure nothing is wrong, there's no going back.
	 */
	OSK_DLIST_INIT(klistp);

	if (kbase_jd_validate_bag(kctx, bag, klistp))
	{
		err = MALI_ERROR_FUNCTION_FAILED;
		goto out;
	}

	while(!OSK_DLIST_IS_EMPTY(klistp))
	{

		katom = OSK_DLIST_POP_FRONT(klistp,
		                            kbase_jd_atom, event.entry);
		i++;

		/* This is crucial. As jobs are processed in-order, we must
		 * indicate that any job with a pre-dep on this particular job
		 * must wait for its completion (indicated as a post-dep).
		 */
		dep_raise_sem(jctx->dep_queue.sem, katom->post_dep.dep[0]);
		dep_raise_sem(jctx->dep_queue.sem, katom->post_dep.dep[1]);

		/* Process pre-exec syncsets before queueing */
		kbase_pre_job_sync(kctx,
		                   base_jd_get_atom_syncset(katom->user_atom, 0),
		                   katom->nr_syncsets);
		/* Update the TOTAL number of jobs. This includes those not tracked by
		 * the scheduler: 'not ready to run' and 'dependency-only' jobs. */
		jctx->job_nr++;
		/* Cause any future waiter-on-termination to wait until the jobs are
		 * finished */
		osk_waitq_clear(&jctx->zero_jobs_waitq);
		/* If no pre-dep has been set, then we're free to run
		 * the job immediately */
		if ((jd_add_dep(jctx, katom, 0) | jd_add_dep(jctx, katom, 1)))
		{
			beenthere("queuing atom #%d(%p %p)", i,
			          (void *)katom, (void *)katom->user_atom);
			continue;
		}

		beenthere("running atom #%d(%p %p)", i,
		          (void *)katom, (void *)katom->user_atom);

		/* Lock the JS Context, for submitting jobs, and queue an action about
		 * whether we need to try scheduling the context */
		osk_mutex_lock( &kctx->jctx.sched_info.ctx.jsctx_mutex );

		if (katom->core_req & BASE_JD_REQ_SOFT_JOB)
		{
			kbase_process_soft_job( kctx, katom );
			/* Pure software job, so resolve it immediately */
			need_to_try_schedule_context |= jd_done_nolock(katom, 0);
		}
		else if (katom->core_req != BASE_JD_REQ_DEP)
		{
			need_to_try_schedule_context |= kbasep_js_add_job( kctx, katom );
		}
		else
		{
			/* This is a pure dependency. Resolve it immediately */
			need_to_try_schedule_context |= jd_done_nolock(katom, 0);
		}
		osk_mutex_unlock( &kctx->jctx.sched_info.ctx.jsctx_mutex );
	}

	/* Only whilst we've dropped the JS context lock can we schedule a new
	 * context.
	 *
	 * As an optimization, we only need to do this after processing all jobs
	 * resolved from this context. */
	if ( need_to_try_schedule_context != MALI_FALSE )
	{
		kbasep_js_try_schedule_head_ctx( kbdev );
	}
out:
	osk_mutex_unlock(&jctx->lock);
out_bag:
	beenthere("%s", "Exit");
	return err;
}
KBASE_EXPORT_TEST_API(kbase_jd_submit)

STATIC void kbasep_jd_check_deref_cores(struct kbase_device *kbdev, struct kbase_jd_atom *katom)
{
	if (katom->affinity != 0)
	{
		u64 tiler_affinity = 0;
		if (katom->core_req & BASE_JD_REQ_T)
		{
			tiler_affinity = kbdev->tiler_present_bitmap;
		}
		kbase_pm_release_cores(kbdev, katom->affinity, tiler_affinity);
		katom->affinity = 0;
	}
}

/**
 * This function:
 * - requeues the job from the runpool (if it was soft-stopped/removed from NEXT registers)
 * - removes it from the system if it finished/failed/was cancelled.
 * - resolves dependencies to add dependent jobs to the context, potentially starting them if necessary (which may add more references to the context)
 * - releases the reference to the context from the no-longer-running job.
 * - Handles retrying submission outside of IRQ context if it failed from within IRQ context.
 */
static void jd_done_worker(osk_workq_work *data)
{
	kbase_jd_atom *katom = CONTAINER_OF(data, kbase_jd_atom, work);
	kbase_jd_context *jctx;
	kbase_context *kctx;
	kbasep_js_kctx_info *js_kctx_info;
	kbasep_js_policy *js_policy;
	kbase_device *kbdev;
	kbasep_js_device_data *js_devdata;
	int zapping;
	u64 cache_jc = katom->jc;
	base_jd_atom *cache_user_atom = katom->user_atom;

	mali_bool retry_submit;
	int retry_jobslot;

	kctx = katom->kctx;
	jctx = &kctx->jctx;
	kbdev = kctx->kbdev;
	js_kctx_info = &kctx->jctx.sched_info;

	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;

	KBASE_TRACE_ADD( kbdev, JD_DONE_WORKER, kctx, katom->user_atom, katom->jc, 0 );
	/*
	 * Begin transaction on JD context and JS context
	 */
	osk_mutex_lock( &jctx->lock );
	osk_mutex_lock( &js_kctx_info->ctx.jsctx_mutex );

	/* This worker only gets called on contexts that are scheduled *in*. This is
	 * because it only happens in response to an IRQ from a job that was
	 * running.
	 */
	OSK_ASSERT( js_kctx_info->ctx.is_scheduled != MALI_FALSE );

	/* Release cores this job was using (this might power down unused cores) */ 
	kbasep_jd_check_deref_cores(kbdev, katom);

	/* Grab the retry_submit state before the katom disappears */
	retry_submit = kbasep_js_get_job_retry_submit_slot( katom, &retry_jobslot );

	if (katom->event.event_code == BASE_JD_EVENT_STOPPED
	    || katom->event.event_code == BASE_JD_EVENT_REMOVED_FROM_NEXT )
	{
		/* Requeue the atom on soft-stop / removed from NEXT registers */
		OSK_PRINT_INFO(OSK_BASE_JM, "JS: Soft Stopped/Removed from next %p on Ctx %p; Requeuing", kctx );

		osk_mutex_lock( &js_devdata->runpool_mutex );
		kbasep_js_clear_job_retry_submit( katom );

		osk_spinlock_irq_lock( &js_devdata->runpool_irq.lock );
		kbasep_js_policy_enqueue_job( js_policy, katom );
		osk_spinlock_irq_unlock( &js_devdata->runpool_irq.lock );

		/* This may now be the only job present (work queues can run items out of order
		 * e.g. on different CPUs), so we must try to run it, otherwise it might not get
		 * run at all after this. */
		kbasep_js_try_run_next_job( kbdev );

		osk_mutex_unlock( &js_devdata->runpool_mutex );
	}
	else
	{
		/* Remove the job from the system for all other reasons */
		mali_bool need_to_try_schedule_context;

		kbasep_js_remove_job( kctx, katom );

		zapping = (katom->event.event_code != BASE_JD_EVENT_DONE);
		need_to_try_schedule_context = jd_done_nolock(katom, zapping);

		/* This ctx is already scheduled in, so return value guarenteed FALSE */
		OSK_ASSERT( need_to_try_schedule_context == MALI_FALSE );
	}
	/* katom may have been freed now, do not use! */

	/*
	 * Transaction complete
	 */
	osk_mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );
	osk_mutex_unlock( &jctx->lock );

	/* Job is now no longer running, so can now safely release the context reference
	 * This potentially schedules out the context, schedules in a new one, and
	 * runs a new job on the new one */
	kbasep_js_runpool_release_ctx( kbdev, kctx );

	/* If the IRQ handler failed to get a job from the policy, try again from
	 * outside the IRQ handler */
	if ( retry_submit != MALI_FALSE )
	{
		KBASE_TRACE_ADD_SLOT( kbdev, JD_DONE_TRY_RUN_NEXT_JOB, kctx, cache_user_atom, cache_jc, retry_jobslot );
		osk_mutex_lock( &js_devdata->runpool_mutex );
		kbasep_js_try_run_next_job_on_slot( kbdev, retry_jobslot );
		osk_mutex_unlock( &js_devdata->runpool_mutex );
	}
	KBASE_TRACE_ADD( kbdev, JD_DONE_WORKER_END, kctx, cache_user_atom, cache_jc, 0 );
}

/*
 * Work queue job cancel function
 * Only called as part of 'Zapping' a context (which occurs on termination)
 * Operates serially with the jd_done_worker() on the work queue
 */
static void jd_cancel_worker(osk_workq_work *data)
{
	kbase_jd_atom *katom = CONTAINER_OF(data, kbase_jd_atom, work);
	kbase_jd_context *jctx;
	kbase_context *kctx;
	kbasep_js_kctx_info *js_kctx_info;
	mali_bool need_to_try_schedule_context;

	kctx = katom->kctx;
	jctx = &kctx->jctx;
	js_kctx_info = &kctx->jctx.sched_info;

	{
		kbase_device *kbdev = kctx->kbdev;
		KBASE_TRACE_ADD( kbdev, JD_CANCEL_WORKER, kctx, katom->user_atom, katom->jc, 0 );
	}

	/* This only gets called on contexts that are scheduled out. Hence, we must
	 * make sure we don't de-ref the number of running jobs (there aren't
	 * any), nor must we try to schedule out the context (it's already
	 * scheduled out).
	 */
	OSK_ASSERT( js_kctx_info->ctx.is_scheduled == MALI_FALSE );

	/* Release cores this job was using (this might power down unused cores) */ 
	kbasep_jd_check_deref_cores(kctx->kbdev, katom);

	/* Scheduler: Remove the job from the system */
	osk_mutex_lock( &js_kctx_info->ctx.jsctx_mutex );
	kbasep_js_remove_job( kctx, katom );
	osk_mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );

	osk_mutex_lock(&jctx->lock);

	/* Always enable zapping */
	need_to_try_schedule_context = jd_done_nolock(katom, 1);
	/* Because we're zapping, we're not adding any more jobs to this ctx, so no need to
	 * schedule the context. There's also no need for the jsctx_mutex to have been taken
	 * around this too. */
	OSK_ASSERT( need_to_try_schedule_context == MALI_FALSE );

	/* katom may have been freed now, do not use! */
	osk_mutex_unlock(&jctx->lock);

}


void kbase_jd_done(kbase_jd_atom *katom)
{
	kbase_context *kctx;
	kbase_device *kbdev;
	OSK_ASSERT(katom);
	kctx = katom->kctx;
	OSK_ASSERT(kctx);
	kbdev = kctx->kbdev;
	OSK_ASSERT(kbdev);

	KBASE_TRACE_ADD( kbdev, JD_DONE, kctx, katom->user_atom, katom->jc, 0 );

	osk_workq_work_init(&katom->work, jd_done_worker);
	osk_workq_submit(&kctx->jctx.job_done_wq, &katom->work);
}
KBASE_EXPORT_TEST_API(kbase_jd_done)

void kbase_jd_cancel(kbase_jd_atom *katom)
{
	kbase_context *kctx;
	kbasep_js_kctx_info *js_kctx_info;
	kbase_device *kbdev;

	kctx = katom->kctx;
	js_kctx_info = &kctx->jctx.sched_info;
	kbdev = kctx->kbdev;

	KBASE_TRACE_ADD( kbdev, JD_CANCEL, kctx, katom->user_atom, katom->jc, 0 );

	/* This should only be done from a context that is not scheduled */
	OSK_ASSERT( js_kctx_info->ctx.is_scheduled == MALI_FALSE );

	katom->event.event_code = BASE_JD_EVENT_JOB_CANCELLED;

	osk_workq_work_init(&katom->work, jd_cancel_worker);
	osk_workq_submit(&kctx->jctx.job_done_wq, &katom->work);
}

void kbase_jd_flush_workqueues(kbase_context *kctx)
{
	kbase_device *kbdev;
	int i;

	OSK_ASSERT( kctx );

	kbdev = kctx->kbdev;
	OSK_ASSERT( kbdev );

	osk_workq_flush( &kctx->jctx.job_done_wq );

	/* Flush all workqueues, for simplicity */
	for (i = 0; i < kbdev->nr_address_spaces; i++)
	{
		osk_workq_flush( &kbdev->as[i].pf_wq );
	}
}

typedef struct zap_reset_data
{
	/* The stages are:
	 * 1. The timer has never been called
	 * 2. The zap has timed out, all slots are soft-stopped - the GPU reset will happen.
	 *    The GPU has been reset when kbdev->reset_waitq is signalled
	 *
	 * (-1 - The timer has been cancelled)
	 */
	int             stage;
	kbase_device    *kbdev;
	osk_timer       *timer;
	osk_spinlock    lock;
} zap_reset_data;

static void zap_timeout_callback(void *data)
{
	zap_reset_data *reset_data = (zap_reset_data*)data;
	kbase_device *kbdev = reset_data->kbdev;

	osk_spinlock_lock(&reset_data->lock);

	if (reset_data->stage == -1)
	{
		goto out;
	}

	if (kbase_prepare_to_reset_gpu(kbdev))
	{
		kbase_reset_gpu(kbdev);
	}

	reset_data->stage = 2;

out:
	osk_spinlock_unlock(&reset_data->lock);
}

void kbase_jd_zap_context(kbase_context *kctx)
{
	kbase_device *kbdev;
	osk_timer zap_timeout;
	osk_error ret;
	zap_reset_data reset_data;

	OSK_ASSERT(kctx);

	kbdev = kctx->kbdev;

	KBASE_TRACE_ADD( kbdev, JD_ZAP_CONTEXT, kctx, NULL, 0u, 0u );
	kbase_job_zap_context(kctx);

	ret = osk_timer_on_stack_init(&zap_timeout);
	if (ret != OSK_ERR_NONE)
	{
		goto skip_timeout;
	}

	ret = osk_spinlock_init(&reset_data.lock, OSK_LOCK_ORDER_JD_ZAP_CONTEXT);
	if (ret != OSK_ERR_NONE)
	{
		osk_timer_on_stack_term(&zap_timeout);
		goto skip_timeout;
	}

	reset_data.kbdev = kbdev;
	reset_data.timer = &zap_timeout;
	reset_data.stage = 1;
	osk_timer_callback_set(&zap_timeout, zap_timeout_callback, &reset_data);
	ret = osk_timer_start(&zap_timeout, ZAP_TIMEOUT);

	if (ret != OSK_ERR_NONE)
	{
		osk_spinlock_term(&reset_data.lock);
		osk_timer_on_stack_term(&zap_timeout);
		goto skip_timeout;
	}

	/* If we jump to here then the zap timeout will not be active,
	 * so if the GPU hangs the driver will also hang. This will only
	 * happen if the driver is very resource starved.
	 */
skip_timeout:

	/* Wait for all jobs to finish, and for the context to be not-scheduled
	 * (due to kbase_job_zap_context(), we also guarentee it's not in the JS
	 * policy queue either */
	osk_waitq_wait(&kctx->jctx.zero_jobs_waitq);
	osk_waitq_wait(&kctx->jctx.sched_info.ctx.not_scheduled_waitq);

	if (ret == OSK_ERR_NONE)
	{
		osk_spinlock_lock(&reset_data.lock);
		if (reset_data.stage == 1)
		{
			/* The timer hasn't run yet - so cancel it */
			reset_data.stage = -1;
		}
		osk_spinlock_unlock(&reset_data.lock);

		osk_timer_stop(&zap_timeout);

		if (reset_data.stage == 2)
		{
			/* The reset has already started.
			 * Wait for the reset to complete
			 */
			osk_waitq_wait(&kbdev->reset_waitq);
		}
		osk_timer_on_stack_term(&zap_timeout);
		osk_spinlock_term(&reset_data.lock);
	}

	OSK_PRINT_INFO(OSK_BASE_JM, "Zap: Finished Context %p", kctx );

	/* Ensure that the signallers of the waitqs have finished */
	osk_mutex_lock(&kctx->jctx.lock);
	osk_mutex_lock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	osk_mutex_unlock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	osk_mutex_unlock(&kctx->jctx.lock);
}
KBASE_EXPORT_TEST_API(kbase_jd_zap_context)

mali_error kbase_jd_init(struct kbase_context *kctx)
{
	void *kaddr;
	int i;
	mali_error mali_err;
	osk_error osk_err;

	OSK_ASSERT(kctx);
	OSK_ASSERT(NULL == kctx->jctx.pool);

	kaddr = osk_vmalloc(BASEP_JCTX_RB_NRPAGES * OSK_PAGE_SIZE);
	if (!kaddr)
	{
		mali_err = MALI_ERROR_OUT_OF_MEMORY;
		goto out;
	}
	osk_err = osk_workq_init(&kctx->jctx.job_done_wq, "mali_jd", 0);
	if (OSK_ERR_NONE != osk_err)
	{
		mali_err = MALI_ERROR_OUT_OF_MEMORY;
		goto out1;
	}

	for (i = 0; i < 256; i++)
		kctx->jctx.dep_queue.queue[i] = NULL;

	for (i = 0; i < BASEP_JD_SEM_ARRAY_SIZE; i++)
		kctx->jctx.dep_queue.sem[i] = 0;

	osk_err = osk_mutex_init(&kctx->jctx.lock, OSK_LOCK_ORDER_JCTX);
	if (OSK_ERR_NONE != osk_err)
	{
		mali_err = MALI_ERROR_FUNCTION_FAILED;
		goto out2;
	}

	osk_err = osk_waitq_init(&kctx->jctx.zero_jobs_waitq);
	if (OSK_ERR_NONE != osk_err)
	{
		mali_err = MALI_ERROR_FUNCTION_FAILED;
		goto out3;
	}

	osk_err = osk_spinlock_irq_init(&kctx->jctx.tb_lock, OSK_LOCK_ORDER_TB);
	if (OSK_ERR_NONE != osk_err)
	{
		mali_err = MALI_ERROR_FUNCTION_FAILED;
		goto out4;
	}

	osk_waitq_set(&kctx->jctx.zero_jobs_waitq);

	kctx->jctx.pool         = kaddr;
	kctx->jctx.pool_size    = BASEP_JCTX_RB_NRPAGES * OSK_PAGE_SIZE;
	kctx->jctx.job_nr       = 0;

	return MALI_ERROR_NONE;

out4:
	osk_waitq_term(&kctx->jctx.zero_jobs_waitq);
out3:
	osk_mutex_term(&kctx->jctx.lock);
out2:
	osk_workq_term(&kctx->jctx.job_done_wq);
out1:
	osk_vfree(kaddr);
out:
	return mali_err;
}
KBASE_EXPORT_TEST_API(kbase_jd_init)

void kbase_jd_exit(struct kbase_context *kctx)
{
	OSK_ASSERT(kctx);
	/* Assert if kbase_jd_init has not been called before this function
	   (kbase_jd_init initializes the pool) */
	OSK_ASSERT(kctx->jctx.pool);

	osk_spinlock_irq_term(&kctx->jctx.tb_lock);
	/* Work queue is emptied by this */
	osk_workq_term(&kctx->jctx.job_done_wq);
	osk_waitq_term(&kctx->jctx.zero_jobs_waitq);
	osk_vfree(kctx->jctx.pool);
	osk_mutex_term(&kctx->jctx.lock);
}
KBASE_EXPORT_TEST_API(kbase_jd_exit)
