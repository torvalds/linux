// SPDX-License-Identifier: GPL-2.0
/*
 * Performance event support - Processor Activity Instrumentation Facility
 *
 *  Copyright IBM Corp. 2026
 *  Author(s): Thomas Richter <tmricht@linux.ibm.com>
 */
#define pr_fmt(fmt) "pai: " fmt

#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/perf_event.h>
#include <asm/ctlreg.h>
#include <asm/pai.h>
#include <asm/debug.h>

static debug_info_t *paidbg;

DEFINE_STATIC_KEY_FALSE(pai_key);

enum {
	PAI_PMU_CRYPTO,			/* Index of PMU pai_crypto */
	PAI_PMU_EXT,			/* Index of PMU pai_ext */
	PAI_PMU_MAX			/* # of PAI PMUs */
};

enum {
	PAIE1_CB_SZ = 0x200,		/* Size of PAIE1 control block */
	PAIE1_CTRBLOCK_SZ = 0x400	/* Size of PAIE1 counter blocks */
};

struct pai_userdata {
	u16 num;
	u64 value;
} __packed;

/* Create the PAI extension 1 control block area.
 * The PAI extension control block 1 is pointed to by lowcore
 * address 0x1508 for each CPU. This control block is 512 bytes in size
 * and requires a 512 byte boundary alignment.
 */
struct paiext_cb {		/* PAI extension 1 control block */
	u64 header;		/* Not used */
	u64 reserved1;
	u64 acc;		/* Addr to analytics counter control block */
	u8 reserved2[PAIE1_CTRBLOCK_SZ - 3 * sizeof(u64)];
} __packed;

struct pai_map {
	unsigned long *area;		/* Area for CPU to store counters */
	struct pai_userdata *save;	/* Page to store no-zero counters */
	unsigned int active_events;	/* # of PAI crypto users */
	refcount_t refcnt;		/* Reference count mapped buffers */
	struct perf_event *event;	/* Perf event for sampling */
	struct list_head syswide_list;	/* List system-wide sampling events */
	struct paiext_cb *paiext_cb;	/* PAI extension control block area */
	bool fullpage;			/* True: counter area is a full page */
};

struct pai_mapptr {
	struct pai_map *mapptr;
};

static struct pai_root {		/* Anchor to per CPU data */
	refcount_t refcnt;		/* Overall active events */
	struct pai_mapptr __percpu *mapptr;
} pai_root[PAI_PMU_MAX];

/* This table defines the different parameters of the PAI PMUs. During
 * initialization the machine dependent values are extracted and saved.
 * However most of the values are static and do not change.
 * There is one table entry per PAI PMU.
 */
struct pai_pmu {			/* Define PAI PMU characteristics */
	const char *pmuname;		/* Name of PMU */
	const int facility_nr;		/* Facility number to check for support */
	unsigned int num_avail;		/* # Counters defined by hardware */
	unsigned int num_named;		/* # Counters known by name */
	unsigned long base;		/* Counter set base number */
	unsigned long kernel_offset;	/* Offset to kernel part in counter page */
	unsigned long area_size;	/* Size of counter area */
	const char * const *names;	/* List of counter names */
	struct pmu *pmu;		/* Ptr to supporting PMU */
	int (*init)(struct pai_pmu *p);		/* PMU support init function */
	void (*exit)(struct pai_pmu *p);	/* PMU support exit function */
	struct attribute_group	*event_group;	/* Ptr to attribute of events */
};

static struct pai_pmu pai_pmu[];	/* Forward declaration */

/* Free per CPU data when the last event is removed. */
static void pai_root_free(int idx)
{
	if (refcount_dec_and_test(&pai_root[idx].refcnt)) {
		free_percpu(pai_root[idx].mapptr);
		pai_root[idx].mapptr = NULL;
	}
	debug_sprintf_event(paidbg, 5, "%s root[%d].refcount %d\n", __func__,
			    idx, refcount_read(&pai_root[idx].refcnt));
}

/*
 * On initialization of first event also allocate per CPU data dynamically.
 * Start with an array of pointers, the array size is the maximum number of
 * CPUs possible, which might be larger than the number of CPUs currently
 * online.
 */
static int pai_root_alloc(int idx)
{
	if (!refcount_inc_not_zero(&pai_root[idx].refcnt)) {
		/* The memory is already zeroed. */
		pai_root[idx].mapptr = alloc_percpu(struct pai_mapptr);
		if (!pai_root[idx].mapptr)
			return -ENOMEM;
		refcount_set(&pai_root[idx].refcnt, 1);
	}
	return 0;
}

/* Release the PMU if event is the last perf event */
static DEFINE_MUTEX(pai_reserve_mutex);

/* Free all memory allocated for event counting/sampling setup */
static void pai_free(struct pai_mapptr *mp)
{
	if (mp->mapptr->fullpage)
		free_page((unsigned long)mp->mapptr->area);
	else
		kfree(mp->mapptr->area);
	kfree(mp->mapptr->paiext_cb);
	kvfree(mp->mapptr->save);
	kfree(mp->mapptr);
	mp->mapptr = NULL;
}

/* Adjust usage counters and remove allocated memory when all users are
 * gone.
 */
static void pai_event_destroy_cpu(struct perf_event *event, int cpu)
{
	int idx = PAI_PMU_IDX(event);
	struct pai_mapptr *mp = per_cpu_ptr(pai_root[idx].mapptr, cpu);
	struct pai_map *cpump = mp->mapptr;

	mutex_lock(&pai_reserve_mutex);
	debug_sprintf_event(paidbg, 5, "%s event %#llx idx %d cpu %d users %d "
			    "refcnt %u\n", __func__, event->attr.config, idx,
			    event->cpu, cpump->active_events,
			    refcount_read(&cpump->refcnt));
	if (refcount_dec_and_test(&cpump->refcnt))
		pai_free(mp);
	pai_root_free(idx);
	mutex_unlock(&pai_reserve_mutex);
}

static void pai_event_destroy(struct perf_event *event)
{
	int cpu;

	free_page(PAI_SAVE_AREA(event));
	if (event->cpu == -1) {
		struct cpumask *mask = PAI_CPU_MASK(event);

		for_each_cpu(cpu, mask)
			pai_event_destroy_cpu(event, cpu);
		kfree(mask);
	} else {
		pai_event_destroy_cpu(event, event->cpu);
	}
}

static void paicrypt_event_destroy(struct perf_event *event)
{
	static_branch_dec(&pai_key);
	pai_event_destroy(event);
}

static u64 pai_getctr(unsigned long *page, int nr, unsigned long offset)
{
	if (offset)
		nr += offset / sizeof(*page);
	return page[nr];
}

/* Read the counter values. Return value from location in CMP. For base
 * event xxx_ALL sum up all events. Returns counter value.
 */
static u64 pai_getdata(struct perf_event *event, bool kernel)
{
	int idx = PAI_PMU_IDX(event);
	struct pai_mapptr *mp = this_cpu_ptr(pai_root[idx].mapptr);
	struct pai_pmu *pp = &pai_pmu[idx];
	struct pai_map *cpump = mp->mapptr;
	unsigned int i;
	u64 sum = 0;

	if (event->attr.config != pp->base) {
		return pai_getctr(cpump->area,
				       event->attr.config - pp->base,
				       kernel ? pp->kernel_offset : 0);
	}

	for (i = 1; i <= pp->num_avail; i++) {
		u64 val = pai_getctr(cpump->area, i,
				     kernel ? pp->kernel_offset : 0);

		if (!val)
			continue;
		sum += val;
	}
	return sum;
}

static u64 paicrypt_getall(struct perf_event *event)
{
	u64 sum = 0;

	if (!event->attr.exclude_kernel)
		sum += pai_getdata(event, true);
	if (!event->attr.exclude_user)
		sum += pai_getdata(event, false);

	return sum;
}

/* Check concurrent access of counting and sampling for crypto events.
 * This function is called in process context and it is save to block.
 * When the event initialization functions fails, no other call back will
 * be invoked.
 *
 * Allocate the memory for the event.
 */
static int pai_alloc_cpu(struct perf_event *event, int cpu)
{
	int rc, idx = PAI_PMU_IDX(event);
	struct pai_map *cpump = NULL;
	bool need_paiext_cb = false;
	struct pai_mapptr *mp;

	mutex_lock(&pai_reserve_mutex);
	/* Allocate root node */
	rc = pai_root_alloc(idx);
	if (rc)
		goto unlock;

	/* Allocate node for this event */
	mp = per_cpu_ptr(pai_root[idx].mapptr, cpu);
	cpump = mp->mapptr;
	if (!cpump) {			/* Paicrypt_map allocated? */
		rc = -ENOMEM;
		cpump = kzalloc(sizeof(*cpump), GFP_KERNEL);
		if (!cpump)
			goto undo;
		/* Allocate memory for counter page and counter extraction.
		 * Only the first counting event has to allocate a page.
		 */
		mp->mapptr = cpump;
		if (idx == PAI_PMU_CRYPTO) {
			cpump->area = (unsigned long *)get_zeroed_page(GFP_KERNEL);
			/* free_page() can handle 0x0 address */
			cpump->fullpage = true;
		} else {			/* PAI_PMU_EXT */
			/*
			 * Allocate memory for counter area and counter extraction.
			 * These are
			 * - a 512 byte block and requires 512 byte boundary
			 *   alignment.
			 * - a 1KB byte block and requires 1KB boundary
			 *   alignment.
			 * Only the first counting event has to allocate the area.
			 *
			 * Note: This works with commit 59bb47985c1d by default.
			 * Backporting this to kernels without this commit might
			 * needs adjustment.
			 */
			cpump->area = kzalloc(pai_pmu[idx].area_size, GFP_KERNEL);
			cpump->paiext_cb = kzalloc(PAIE1_CB_SZ, GFP_KERNEL);
			need_paiext_cb = true;
		}
		cpump->save = kvmalloc_array(pai_pmu[idx].num_avail + 1,
					     sizeof(struct pai_userdata),
					     GFP_KERNEL);
		if (!cpump->area || !cpump->save ||
		    (need_paiext_cb && !cpump->paiext_cb)) {
			pai_free(mp);
			goto undo;
		}
		INIT_LIST_HEAD(&cpump->syswide_list);
		refcount_set(&cpump->refcnt, 1);
		rc = 0;
	} else {
		refcount_inc(&cpump->refcnt);
	}

undo:
	if (rc) {
		/* Error in allocation of event, decrement anchor. Since
		 * the event in not created, its destroy() function is never
		 * invoked. Adjust the reference counter for the anchor.
		 */
		pai_root_free(idx);
	}
unlock:
	mutex_unlock(&pai_reserve_mutex);
	/* If rc is non-zero, no increment of counter/sampler was done. */
	return rc;
}

static int pai_alloc(struct perf_event *event)
{
	struct cpumask *maskptr;
	int cpu, rc = -ENOMEM;

	maskptr = kzalloc(sizeof(*maskptr), GFP_KERNEL);
	if (!maskptr)
		goto out;

	for_each_online_cpu(cpu) {
		rc = pai_alloc_cpu(event, cpu);
		if (rc) {
			for_each_cpu(cpu, maskptr)
				pai_event_destroy_cpu(event, cpu);
			kfree(maskptr);
			goto out;
		}
		cpumask_set_cpu(cpu, maskptr);
	}

	/*
	 * On error all cpumask are freed and all events have been destroyed.
	 * Save of which CPUs data structures have been allocated for.
	 * Release them in pai_event_destroy call back function
	 * for this event.
	 */
	PAI_CPU_MASK(event) = maskptr;
	rc = 0;
out:
	return rc;
}

/* Validate event number and return error if event is not supported.
 * On successful return, PAI_PMU_IDX(event) is set to the index of
 * the supporting paing_support[] array element.
 */
static int pai_event_valid(struct perf_event *event, int idx)
{
	struct perf_event_attr *a = &event->attr;
	struct pai_pmu *pp = &pai_pmu[idx];

	/* PAI crypto PMU registered as PERF_TYPE_RAW, check event type */
	if (a->type != PERF_TYPE_RAW && event->pmu->type != a->type)
		return -ENOENT;
	/* Allow only CRYPTO_ALL/NNPA_ALL for sampling */
	if (a->sample_period && a->config != pp->base)
		return -EINVAL;
	/* PAI crypto event must be in valid range, try others if not */
	if (a->config < pp->base || a->config > pp->base + pp->num_avail)
		return -ENOENT;
	if (idx == PAI_PMU_EXT && a->exclude_user)
		return -EINVAL;
	PAI_PMU_IDX(event) = idx;
	return 0;
}

/* Might be called on different CPU than the one the event is intended for. */
static int pai_event_init(struct perf_event *event, int idx)
{
	struct perf_event_attr *a = &event->attr;
	int rc;

	/* PAI event must be valid and in supported range */
	rc = pai_event_valid(event, idx);
	if (rc)
		goto out;
	/* Get a page to store last counter values for sampling */
	if (a->sample_period) {
		PAI_SAVE_AREA(event) = get_zeroed_page(GFP_KERNEL);
		if (!PAI_SAVE_AREA(event)) {
			rc = -ENOMEM;
			goto out;
		}
	}

	if (event->cpu >= 0)
		rc = pai_alloc_cpu(event, event->cpu);
	else
		rc = pai_alloc(event);
	if (rc) {
		free_page(PAI_SAVE_AREA(event));
		goto out;
	}

	if (a->sample_period) {
		a->sample_period = 1;
		a->freq = 0;
		/* Register for paicrypt_sched_task() to be called */
		event->attach_state |= PERF_ATTACH_SCHED_CB;
		/* Add raw data which contain the memory mapped counters */
		a->sample_type |= PERF_SAMPLE_RAW;
		/* Turn off inheritance */
		a->inherit = 0;
	}
out:
	return rc;
}

static int paicrypt_event_init(struct perf_event *event)
{
	int rc = pai_event_init(event, PAI_PMU_CRYPTO);

	if (!rc) {
		event->destroy = paicrypt_event_destroy;
		static_branch_inc(&pai_key);
	}
	return rc;
}

static void pai_read(struct perf_event *event,
		     u64 (*fct)(struct perf_event *event))
{
	u64 prev, new, delta;

	prev = local64_read(&event->hw.prev_count);
	new = fct(event);
	local64_set(&event->hw.prev_count, new);
	delta = (prev <= new) ? new - prev : (-1ULL - prev) + new + 1;
	local64_add(delta, &event->count);
}

static void paicrypt_read(struct perf_event *event)
{
	pai_read(event, paicrypt_getall);
}

static void pai_start(struct perf_event *event, int flags,
		      u64 (*fct)(struct perf_event *event))
{
	int idx = PAI_PMU_IDX(event);
	struct pai_pmu *pp = &pai_pmu[idx];
	struct pai_mapptr *mp = this_cpu_ptr(pai_root[idx].mapptr);
	struct pai_map *cpump = mp->mapptr;
	u64 sum;

	if (!event->attr.sample_period) {	/* Counting */
		sum = fct(event);		/* Get current value */
		local64_set(&event->hw.prev_count, sum);
	} else {				/* Sampling */
		memcpy((void *)PAI_SAVE_AREA(event), cpump->area, pp->area_size);
		/* Enable context switch callback for system-wide sampling */
		if (!(event->attach_state & PERF_ATTACH_TASK)) {
			list_add_tail(PAI_SWLIST(event), &cpump->syswide_list);
			perf_sched_cb_inc(event->pmu);
		} else {
			cpump->event = event;
		}
	}
}

static void paicrypt_start(struct perf_event *event, int flags)
{
	pai_start(event, flags, paicrypt_getall);
}

static int pai_add(struct perf_event *event, int flags)
{
	int idx = PAI_PMU_IDX(event);
	struct pai_mapptr *mp = this_cpu_ptr(pai_root[idx].mapptr);
	struct pai_map *cpump = mp->mapptr;
	struct paiext_cb *pcb = cpump->paiext_cb;
	unsigned long ccd;

	if (++cpump->active_events == 1) {
		if (!pcb) {		/* PAI crypto */
			ccd = virt_to_phys(cpump->area) | PAI_CRYPTO_KERNEL_OFFSET;
			WRITE_ONCE(get_lowcore()->ccd, ccd);
			local_ctl_set_bit(0, CR0_CRYPTOGRAPHY_COUNTER_BIT);
		} else {		/* PAI extension 1 */
			ccd = virt_to_phys(pcb);
			WRITE_ONCE(get_lowcore()->aicd, ccd);
			pcb->acc = virt_to_phys(cpump->area) | 0x1;
			/* Enable CPU instruction lookup for PAIE1 control block */
			local_ctl_set_bit(0, CR0_PAI_EXTENSION_BIT);
		}
	}
	if (flags & PERF_EF_START)
		pai_pmu[idx].pmu->start(event, PERF_EF_RELOAD);
	event->hw.state = 0;
	return 0;
}

static int paicrypt_add(struct perf_event *event, int flags)
{
	return pai_add(event, flags);
}

static void pai_have_sample(struct perf_event *, struct pai_map *);
static void pai_stop(struct perf_event *event, int flags)
{
	int idx = PAI_PMU_IDX(event);
	struct pai_mapptr *mp = this_cpu_ptr(pai_root[idx].mapptr);
	struct pai_map *cpump = mp->mapptr;

	if (!event->attr.sample_period) {	/* Counting */
		pai_pmu[idx].pmu->read(event);
	} else {				/* Sampling */
		if (!(event->attach_state & PERF_ATTACH_TASK)) {
			perf_sched_cb_dec(event->pmu);
			list_del(PAI_SWLIST(event));
		} else {
			pai_have_sample(event, cpump);
			cpump->event = NULL;
		}
	}
	event->hw.state = PERF_HES_STOPPED;
}

static void paicrypt_stop(struct perf_event *event, int flags)
{
	pai_stop(event, flags);
}

static void pai_del(struct perf_event *event, int flags)
{
	int idx = PAI_PMU_IDX(event);
	struct pai_mapptr *mp = this_cpu_ptr(pai_root[idx].mapptr);
	struct pai_map *cpump = mp->mapptr;
	struct paiext_cb *pcb = cpump->paiext_cb;

	pai_pmu[idx].pmu->stop(event, PERF_EF_UPDATE);
	if (--cpump->active_events == 0) {
		if (!pcb) {		/* PAI crypto */
			local_ctl_clear_bit(0, CR0_CRYPTOGRAPHY_COUNTER_BIT);
			WRITE_ONCE(get_lowcore()->ccd, 0);
		} else {		/* PAI extension 1 */
			/* Disable CPU instruction lookup for PAIE1 control block */
			local_ctl_clear_bit(0, CR0_PAI_EXTENSION_BIT);
			pcb->acc = 0;
			WRITE_ONCE(get_lowcore()->aicd, 0);
		}
	}
}

static void paicrypt_del(struct perf_event *event, int flags)
{
	pai_del(event, flags);
}

/* Create raw data and save it in buffer. Calculate the delta for each
 * counter between this invocation and the last invocation.
 * Returns number of bytes copied.
 * Saves only entries with positive counter difference of the form
 * 2 bytes: Number of counter
 * 8 bytes: Value of counter
 */
static size_t pai_copy(struct pai_userdata *userdata, unsigned long *page,
		       struct pai_pmu *pp, unsigned long *page_old,
		       bool exclude_user, bool exclude_kernel)
{
	int i, outidx = 0;

	for (i = 1; i <= pp->num_avail; i++) {
		u64 val = 0, val_old = 0;

		if (!exclude_kernel) {
			val += pai_getctr(page, i, pp->kernel_offset);
			val_old += pai_getctr(page_old, i, pp->kernel_offset);
		}
		if (!exclude_user) {
			val += pai_getctr(page, i, 0);
			val_old += pai_getctr(page_old, i, 0);
		}
		if (val >= val_old)
			val -= val_old;
		else
			val = (~0ULL - val_old) + val + 1;
		if (val) {
			userdata[outidx].num = i;
			userdata[outidx].value = val;
			outidx++;
		}
	}
	return outidx * sizeof(*userdata);
}

/* Write sample when one or more counters values are nonzero.
 *
 * Note: The function paicrypt_sched_task() and pai_push_sample() are not
 * invoked after function paicrypt_del() has been called because of function
 * perf_sched_cb_dec(). Both functions are only
 * called when sampling is active. Function perf_sched_cb_inc()
 * has been invoked to install function paicrypt_sched_task() as call back
 * to run at context switch time.
 *
 * This causes function perf_event_context_sched_out() and
 * perf_event_context_sched_in() to check whether the PMU has installed an
 * sched_task() callback. That callback is not active after paicrypt_del()
 * returns and has deleted the event on that CPU.
 */
static int pai_push_sample(size_t rawsize, struct pai_map *cpump,
			   struct perf_event *event)
{
	int idx = PAI_PMU_IDX(event);
	struct pai_pmu *pp = &pai_pmu[idx];
	struct perf_sample_data data;
	struct perf_raw_record raw;
	struct pt_regs regs;
	int overflow;

	/* Setup perf sample */
	memset(&regs, 0, sizeof(regs));
	memset(&raw, 0, sizeof(raw));
	memset(&data, 0, sizeof(data));
	perf_sample_data_init(&data, 0, event->hw.last_period);
	if (event->attr.sample_type & PERF_SAMPLE_TID) {
		data.tid_entry.pid = task_tgid_nr(current);
		data.tid_entry.tid = task_pid_nr(current);
	}
	if (event->attr.sample_type & PERF_SAMPLE_TIME)
		data.time = event->clock();
	if (event->attr.sample_type & (PERF_SAMPLE_ID | PERF_SAMPLE_IDENTIFIER))
		data.id = event->id;
	if (event->attr.sample_type & PERF_SAMPLE_CPU) {
		data.cpu_entry.cpu = smp_processor_id();
		data.cpu_entry.reserved = 0;
	}
	if (event->attr.sample_type & PERF_SAMPLE_RAW) {
		raw.frag.size = rawsize;
		raw.frag.data = cpump->save;
		perf_sample_save_raw_data(&data, event, &raw);
	}

	overflow = perf_event_overflow(event, &data, &regs);
	perf_event_update_userpage(event);
	/* Save crypto counter lowcore page after reading event data. */
	memcpy((void *)PAI_SAVE_AREA(event), cpump->area, pp->area_size);
	return overflow;
}

/* Check if there is data to be saved on schedule out of a task. */
static void pai_have_sample(struct perf_event *event, struct pai_map *cpump)
{
	struct pai_pmu *pp;
	size_t rawsize;

	if (!event)		/* No event active */
		return;
	pp = &pai_pmu[PAI_PMU_IDX(event)];
	rawsize = pai_copy(cpump->save, cpump->area, pp,
			   (unsigned long *)PAI_SAVE_AREA(event),
			   event->attr.exclude_user,
			   event->attr.exclude_kernel);
	if (rawsize)			/* No incremented counters */
		pai_push_sample(rawsize, cpump, event);
}

/* Check if there is data to be saved on schedule out of a task. */
static void pai_have_samples(int idx)
{
	struct pai_mapptr *mp = this_cpu_ptr(pai_root[idx].mapptr);
	struct pai_map *cpump = mp->mapptr;
	struct perf_event *event;

	list_for_each_entry(event, &cpump->syswide_list, hw.tp_list)
		pai_have_sample(event, cpump);
}

/* Called on schedule-in and schedule-out. No access to event structure,
 * but for sampling only event CRYPTO_ALL is allowed.
 */
static void paicrypt_sched_task(struct perf_event_pmu_context *pmu_ctx,
				struct task_struct *task, bool sched_in)
{
	/* We started with a clean page on event installation. So read out
	 * results on schedule_out and if page was dirty, save old values.
	 */
	if (!sched_in)
		pai_have_samples(PAI_PMU_CRYPTO);
}

/* ============================= paiext ====================================*/

static void paiext_event_destroy(struct perf_event *event)
{
	pai_event_destroy(event);
}

/* Might be called on different CPU than the one the event is intended for. */
static int paiext_event_init(struct perf_event *event)
{
	int rc = pai_event_init(event, PAI_PMU_EXT);

	if (!rc) {
		event->attr.exclude_kernel = true;	/* No kernel space part */
		event->destroy = paiext_event_destroy;
		/* Offset of NNPA in paiext_cb */
		event->hw.config_base = offsetof(struct paiext_cb, acc);
	}
	return rc;
}

static u64 paiext_getall(struct perf_event *event)
{
	return pai_getdata(event, false);
}

static void paiext_read(struct perf_event *event)
{
	pai_read(event, paiext_getall);
}

static void paiext_start(struct perf_event *event, int flags)
{
	pai_start(event, flags, paiext_getall);
}

static int paiext_add(struct perf_event *event, int flags)
{
	return pai_add(event, flags);
}

static void paiext_stop(struct perf_event *event, int flags)
{
	pai_stop(event, flags);
}

static void paiext_del(struct perf_event *event, int flags)
{
	pai_del(event, flags);
}

/* Called on schedule-in and schedule-out. No access to event structure,
 * but for sampling only event NNPA_ALL is allowed.
 */
static void paiext_sched_task(struct perf_event_pmu_context *pmu_ctx,
			      struct task_struct *task, bool sched_in)
{
	/* We started with a clean page on event installation. So read out
	 * results on schedule_out and if page was dirty, save old values.
	 */
	if (!sched_in)
		pai_have_samples(PAI_PMU_EXT);
}

/* Attribute definitions for paicrypt interface. As with other CPU
 * Measurement Facilities, there is one attribute per mapped counter.
 * The number of mapped counters may vary per machine generation. Use
 * the QUERY PROCESSOR ACTIVITY COUNTER INFORMATION (QPACI) instruction
 * to determine the number of mapped counters. The instructions returns
 * a positive number, which is the highest number of supported counters.
 * All counters less than this number are also supported, there are no
 * holes. A returned number of zero means no support for mapped counters.
 *
 * The identification of the counter is a unique number. The chosen range
 * is 0x1000 + offset in mapped kernel page.
 * All CPU Measurement Facility counters identifiers must be unique and
 * the numbers from 0 to 496 are already used for the CPU Measurement
 * Counter facility. Numbers 0xb0000, 0xbc000 and 0xbd000 are already
 * used for the CPU Measurement Sampling facility.
 */
PMU_FORMAT_ATTR(event, "config:0-63");

static struct attribute *paicrypt_format_attr[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group paicrypt_events_group = {
	.name = "events",
	.attrs = NULL			/* Filled in attr_event_init() */
};

static struct attribute_group paicrypt_format_group = {
	.name = "format",
	.attrs = paicrypt_format_attr,
};

static const struct attribute_group *paicrypt_attr_groups[] = {
	&paicrypt_events_group,
	&paicrypt_format_group,
	NULL,
};

/* Performance monitoring unit for mapped counters */
static struct pmu paicrypt = {
	.task_ctx_nr  = perf_hw_context,
	.event_init   = paicrypt_event_init,
	.add	      = paicrypt_add,
	.del	      = paicrypt_del,
	.start	      = paicrypt_start,
	.stop	      = paicrypt_stop,
	.read	      = paicrypt_read,
	.sched_task   = paicrypt_sched_task,
	.attr_groups  = paicrypt_attr_groups
};

/* List of symbolic PAI counter names. */
static const char * const paicrypt_ctrnames[] = {
	[0] = "CRYPTO_ALL",
	[1] = "KM_DEA",
	[2] = "KM_TDEA_128",
	[3] = "KM_TDEA_192",
	[4] = "KM_ENCRYPTED_DEA",
	[5] = "KM_ENCRYPTED_TDEA_128",
	[6] = "KM_ENCRYPTED_TDEA_192",
	[7] = "KM_AES_128",
	[8] = "KM_AES_192",
	[9] = "KM_AES_256",
	[10] = "KM_ENCRYPTED_AES_128",
	[11] = "KM_ENCRYPTED_AES_192",
	[12] = "KM_ENCRYPTED_AES_256",
	[13] = "KM_XTS_AES_128",
	[14] = "KM_XTS_AES_256",
	[15] = "KM_XTS_ENCRYPTED_AES_128",
	[16] = "KM_XTS_ENCRYPTED_AES_256",
	[17] = "KMC_DEA",
	[18] = "KMC_TDEA_128",
	[19] = "KMC_TDEA_192",
	[20] = "KMC_ENCRYPTED_DEA",
	[21] = "KMC_ENCRYPTED_TDEA_128",
	[22] = "KMC_ENCRYPTED_TDEA_192",
	[23] = "KMC_AES_128",
	[24] = "KMC_AES_192",
	[25] = "KMC_AES_256",
	[26] = "KMC_ENCRYPTED_AES_128",
	[27] = "KMC_ENCRYPTED_AES_192",
	[28] = "KMC_ENCRYPTED_AES_256",
	[29] = "KMC_PRNG",
	[30] = "KMA_GCM_AES_128",
	[31] = "KMA_GCM_AES_192",
	[32] = "KMA_GCM_AES_256",
	[33] = "KMA_GCM_ENCRYPTED_AES_128",
	[34] = "KMA_GCM_ENCRYPTED_AES_192",
	[35] = "KMA_GCM_ENCRYPTED_AES_256",
	[36] = "KMF_DEA",
	[37] = "KMF_TDEA_128",
	[38] = "KMF_TDEA_192",
	[39] = "KMF_ENCRYPTED_DEA",
	[40] = "KMF_ENCRYPTED_TDEA_128",
	[41] = "KMF_ENCRYPTED_TDEA_192",
	[42] = "KMF_AES_128",
	[43] = "KMF_AES_192",
	[44] = "KMF_AES_256",
	[45] = "KMF_ENCRYPTED_AES_128",
	[46] = "KMF_ENCRYPTED_AES_192",
	[47] = "KMF_ENCRYPTED_AES_256",
	[48] = "KMCTR_DEA",
	[49] = "KMCTR_TDEA_128",
	[50] = "KMCTR_TDEA_192",
	[51] = "KMCTR_ENCRYPTED_DEA",
	[52] = "KMCTR_ENCRYPTED_TDEA_128",
	[53] = "KMCTR_ENCRYPTED_TDEA_192",
	[54] = "KMCTR_AES_128",
	[55] = "KMCTR_AES_192",
	[56] = "KMCTR_AES_256",
	[57] = "KMCTR_ENCRYPTED_AES_128",
	[58] = "KMCTR_ENCRYPTED_AES_192",
	[59] = "KMCTR_ENCRYPTED_AES_256",
	[60] = "KMO_DEA",
	[61] = "KMO_TDEA_128",
	[62] = "KMO_TDEA_192",
	[63] = "KMO_ENCRYPTED_DEA",
	[64] = "KMO_ENCRYPTED_TDEA_128",
	[65] = "KMO_ENCRYPTED_TDEA_192",
	[66] = "KMO_AES_128",
	[67] = "KMO_AES_192",
	[68] = "KMO_AES_256",
	[69] = "KMO_ENCRYPTED_AES_128",
	[70] = "KMO_ENCRYPTED_AES_192",
	[71] = "KMO_ENCRYPTED_AES_256",
	[72] = "KIMD_SHA_1",
	[73] = "KIMD_SHA_256",
	[74] = "KIMD_SHA_512",
	[75] = "KIMD_SHA3_224",
	[76] = "KIMD_SHA3_256",
	[77] = "KIMD_SHA3_384",
	[78] = "KIMD_SHA3_512",
	[79] = "KIMD_SHAKE_128",
	[80] = "KIMD_SHAKE_256",
	[81] = "KIMD_GHASH",
	[82] = "KLMD_SHA_1",
	[83] = "KLMD_SHA_256",
	[84] = "KLMD_SHA_512",
	[85] = "KLMD_SHA3_224",
	[86] = "KLMD_SHA3_256",
	[87] = "KLMD_SHA3_384",
	[88] = "KLMD_SHA3_512",
	[89] = "KLMD_SHAKE_128",
	[90] = "KLMD_SHAKE_256",
	[91] = "KMAC_DEA",
	[92] = "KMAC_TDEA_128",
	[93] = "KMAC_TDEA_192",
	[94] = "KMAC_ENCRYPTED_DEA",
	[95] = "KMAC_ENCRYPTED_TDEA_128",
	[96] = "KMAC_ENCRYPTED_TDEA_192",
	[97] = "KMAC_AES_128",
	[98] = "KMAC_AES_192",
	[99] = "KMAC_AES_256",
	[100] = "KMAC_ENCRYPTED_AES_128",
	[101] = "KMAC_ENCRYPTED_AES_192",
	[102] = "KMAC_ENCRYPTED_AES_256",
	[103] = "PCC_COMPUTE_LAST_BLOCK_CMAC_USING_DEA",
	[104] = "PCC_COMPUTE_LAST_BLOCK_CMAC_USING_TDEA_128",
	[105] = "PCC_COMPUTE_LAST_BLOCK_CMAC_USING_TDEA_192",
	[106] = "PCC_COMPUTE_LAST_BLOCK_CMAC_USING_ENCRYPTED_DEA",
	[107] = "PCC_COMPUTE_LAST_BLOCK_CMAC_USING_ENCRYPTED_TDEA_128",
	[108] = "PCC_COMPUTE_LAST_BLOCK_CMAC_USING_ENCRYPTED_TDEA_192",
	[109] = "PCC_COMPUTE_LAST_BLOCK_CMAC_USING_AES_128",
	[110] = "PCC_COMPUTE_LAST_BLOCK_CMAC_USING_AES_192",
	[111] = "PCC_COMPUTE_LAST_BLOCK_CMAC_USING_AES_256",
	[112] = "PCC_COMPUTE_LAST_BLOCK_CMAC_USING_ENCRYPTED_AES_128",
	[113] = "PCC_COMPUTE_LAST_BLOCK_CMAC_USING_ENCRYPTED_AES_192",
	[114] = "PCC_COMPUTE_LAST_BLOCK_CMAC_USING_ENCRYPTED_AES_256",
	[115] = "PCC_COMPUTE_XTS_PARAMETER_USING_AES_128",
	[116] = "PCC_COMPUTE_XTS_PARAMETER_USING_AES_256",
	[117] = "PCC_COMPUTE_XTS_PARAMETER_USING_ENCRYPTED_AES_128",
	[118] = "PCC_COMPUTE_XTS_PARAMETER_USING_ENCRYPTED_AES_256",
	[119] = "PCC_SCALAR_MULTIPLY_P256",
	[120] = "PCC_SCALAR_MULTIPLY_P384",
	[121] = "PCC_SCALAR_MULTIPLY_P521",
	[122] = "PCC_SCALAR_MULTIPLY_ED25519",
	[123] = "PCC_SCALAR_MULTIPLY_ED448",
	[124] = "PCC_SCALAR_MULTIPLY_X25519",
	[125] = "PCC_SCALAR_MULTIPLY_X448",
	[126] = "PRNO_SHA_512_DRNG",
	[127] = "PRNO_TRNG_QUERY_RAW_TO_CONDITIONED_RATIO",
	[128] = "PRNO_TRNG",
	[129] = "KDSA_ECDSA_VERIFY_P256",
	[130] = "KDSA_ECDSA_VERIFY_P384",
	[131] = "KDSA_ECDSA_VERIFY_P521",
	[132] = "KDSA_ECDSA_SIGN_P256",
	[133] = "KDSA_ECDSA_SIGN_P384",
	[134] = "KDSA_ECDSA_SIGN_P521",
	[135] = "KDSA_ENCRYPTED_ECDSA_SIGN_P256",
	[136] = "KDSA_ENCRYPTED_ECDSA_SIGN_P384",
	[137] = "KDSA_ENCRYPTED_ECDSA_SIGN_P521",
	[138] = "KDSA_EDDSA_VERIFY_ED25519",
	[139] = "KDSA_EDDSA_VERIFY_ED448",
	[140] = "KDSA_EDDSA_SIGN_ED25519",
	[141] = "KDSA_EDDSA_SIGN_ED448",
	[142] = "KDSA_ENCRYPTED_EDDSA_SIGN_ED25519",
	[143] = "KDSA_ENCRYPTED_EDDSA_SIGN_ED448",
	[144] = "PCKMO_ENCRYPT_DEA_KEY",
	[145] = "PCKMO_ENCRYPT_TDEA_128_KEY",
	[146] = "PCKMO_ENCRYPT_TDEA_192_KEY",
	[147] = "PCKMO_ENCRYPT_AES_128_KEY",
	[148] = "PCKMO_ENCRYPT_AES_192_KEY",
	[149] = "PCKMO_ENCRYPT_AES_256_KEY",
	[150] = "PCKMO_ENCRYPT_ECC_P256_KEY",
	[151] = "PCKMO_ENCRYPT_ECC_P384_KEY",
	[152] = "PCKMO_ENCRYPT_ECC_P521_KEY",
	[153] = "PCKMO_ENCRYPT_ECC_ED25519_KEY",
	[154] = "PCKMO_ENCRYPT_ECC_ED448_KEY",
	[155] = "IBM_RESERVED_155",
	[156] = "IBM_RESERVED_156",
	[157] = "KM_FULL_XTS_AES_128",
	[158] = "KM_FULL_XTS_AES_256",
	[159] = "KM_FULL_XTS_ENCRYPTED_AES_128",
	[160] = "KM_FULL_XTS_ENCRYPTED_AES_256",
	[161] = "KMAC_HMAC_SHA_224",
	[162] = "KMAC_HMAC_SHA_256",
	[163] = "KMAC_HMAC_SHA_384",
	[164] = "KMAC_HMAC_SHA_512",
	[165] = "KMAC_HMAC_ENCRYPTED_SHA_224",
	[166] = "KMAC_HMAC_ENCRYPTED_SHA_256",
	[167] = "KMAC_HMAC_ENCRYPTED_SHA_384",
	[168] = "KMAC_HMAC_ENCRYPTED_SHA_512",
	[169] = "PCKMO_ENCRYPT_HMAC_512_KEY",
	[170] = "PCKMO_ENCRYPT_HMAC_1024_KEY",
	[171] = "PCKMO_ENCRYPT_AES_XTS_128",
	[172] = "PCKMO_ENCRYPT_AES_XTS_256",
};

static struct attribute *paiext_format_attr[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group paiext_events_group = {
	.name = "events",
	.attrs = NULL,			/* Filled in attr_event_init() */
};

static struct attribute_group paiext_format_group = {
	.name = "format",
	.attrs = paiext_format_attr,
};

static const struct attribute_group *paiext_attr_groups[] = {
	&paiext_events_group,
	&paiext_format_group,
	NULL,
};

/* Performance monitoring unit for mapped counters */
static struct pmu paiext = {
	.task_ctx_nr  = perf_hw_context,
	.event_init   = paiext_event_init,
	.add	      = paiext_add,
	.del	      = paiext_del,
	.start	      = paiext_start,
	.stop	      = paiext_stop,
	.read	      = paiext_read,
	.sched_task   = paiext_sched_task,
	.attr_groups  = paiext_attr_groups,
};

/* List of symbolic PAI extension 1 NNPA counter names. */
static const char * const paiext_ctrnames[] = {
	[0] = "NNPA_ALL",
	[1] = "NNPA_ADD",
	[2] = "NNPA_SUB",
	[3] = "NNPA_MUL",
	[4] = "NNPA_DIV",
	[5] = "NNPA_MIN",
	[6] = "NNPA_MAX",
	[7] = "NNPA_LOG",
	[8] = "NNPA_EXP",
	[9] = "NNPA_IBM_RESERVED_9",
	[10] = "NNPA_RELU",
	[11] = "NNPA_TANH",
	[12] = "NNPA_SIGMOID",
	[13] = "NNPA_SOFTMAX",
	[14] = "NNPA_BATCHNORM",
	[15] = "NNPA_MAXPOOL2D",
	[16] = "NNPA_AVGPOOL2D",
	[17] = "NNPA_LSTMACT",
	[18] = "NNPA_GRUACT",
	[19] = "NNPA_CONVOLUTION",
	[20] = "NNPA_MATMUL_OP",
	[21] = "NNPA_MATMUL_OP_BCAST23",
	[22] = "NNPA_SMALLBATCH",
	[23] = "NNPA_LARGEDIM",
	[24] = "NNPA_SMALLTENSOR",
	[25] = "NNPA_1MFRAME",
	[26] = "NNPA_2GFRAME",
	[27] = "NNPA_ACCESSEXCEPT",
	[28] = "NNPA_TRANSFORM",
	[29] = "NNPA_GELU",
	[30] = "NNPA_MOMENTS",
	[31] = "NNPA_LAYERNORM",
	[32] = "NNPA_MATMUL_OP_BCAST1",
	[33] = "NNPA_SQRT",
	[34] = "NNPA_INVSQRT",
	[35] = "NNPA_NORM",
	[36] = "NNPA_REDUCE",
};

static void __init attr_event_free(struct attribute **attrs)
{
	struct perf_pmu_events_attr *pa;
	unsigned int i;

	for (i = 0; attrs[i]; i++) {
		struct device_attribute *dap;

		dap = container_of(attrs[i], struct device_attribute, attr);
		pa = container_of(dap, struct perf_pmu_events_attr, attr);
		kfree(pa);
	}
	kfree(attrs);
}

static struct attribute * __init attr_event_init_one(int num,
						     unsigned long base,
						     const char *name)
{
	struct perf_pmu_events_attr *pa;

	pa = kzalloc(sizeof(*pa), GFP_KERNEL);
	if (!pa)
		return NULL;

	sysfs_attr_init(&pa->attr.attr);
	pa->id = base + num;
	pa->attr.attr.name = name;
	pa->attr.attr.mode = 0444;
	pa->attr.show = cpumf_events_sysfs_show;
	pa->attr.store = NULL;
	return &pa->attr.attr;
}

static struct attribute ** __init attr_event_init(struct pai_pmu *p)
{
	unsigned int min_attr = min_t(unsigned int, p->num_named, p->num_avail);
	struct attribute **attrs;
	unsigned int i;

	attrs = kmalloc_array(min_attr + 1, sizeof(*attrs), GFP_KERNEL | __GFP_ZERO);
	if (!attrs)
		goto out;
	for (i = 0; i < min_attr; i++) {
		attrs[i] = attr_event_init_one(i, p->base, p->names[i]);
		if (!attrs[i]) {
			attr_event_free(attrs);
			attrs = NULL;
			goto out;
		}
	}
	attrs[i] = NULL;
out:
	return attrs;
}

static void __init pai_pmu_exit(struct pai_pmu *p)
{
	attr_event_free(p->event_group->attrs);
	p->event_group->attrs = NULL;
}

/* Add a PMU. Install its events and register the PMU device driver
 * call back functions.
 */
static int __init pai_pmu_init(struct pai_pmu *p)
{
	int rc = -ENOMEM;


	/* Export known PAI events */
	p->event_group->attrs = attr_event_init(p);
	if (!p->event_group->attrs) {
		pr_err("Creation of PMU %s /sysfs failed\n", p->pmuname);
		goto out;
	}

	rc = perf_pmu_register(p->pmu, p->pmuname, -1);
	if (rc) {
		pai_pmu_exit(p);
		pr_err("Registering PMU %s failed with rc=%i\n", p->pmuname,
		       rc);
	}
out:
	return rc;
}

/* PAI PMU characteristics table */
static struct pai_pmu pai_pmu[] __refdata = {
	[PAI_PMU_CRYPTO] = {
		.pmuname = "pai_crypto",
		.facility_nr = 196,
		.num_named = ARRAY_SIZE(paicrypt_ctrnames),
		.names = paicrypt_ctrnames,
		.base = PAI_CRYPTO_BASE,
		.kernel_offset = PAI_CRYPTO_KERNEL_OFFSET,
		.area_size = PAGE_SIZE,
		.init = pai_pmu_init,
		.exit = pai_pmu_exit,
		.pmu = &paicrypt,
		.event_group = &paicrypt_events_group
	},
	[PAI_PMU_EXT] = {
		.pmuname = "pai_ext",
		.facility_nr = 197,
		.num_named = ARRAY_SIZE(paiext_ctrnames),
		.names = paiext_ctrnames,
		.base = PAI_NNPA_BASE,
		.kernel_offset = 0,
		.area_size = PAIE1_CTRBLOCK_SZ,
		.init = pai_pmu_init,
		.exit = pai_pmu_exit,
		.pmu = &paiext,
		.event_group = &paiext_events_group
	}
};

/*
 * Check if the PMU (via facility) is supported by machine. Try all of the
 * supported PAI PMUs.
 * Return number of successfully installed PMUs.
 */
static int __init paipmu_setup(void)
{
	struct qpaci_info_block ib;
	int install_ok = 0, rc;
	struct pai_pmu *p;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(pai_pmu); ++i) {
		p = &pai_pmu[i];

		if (!test_facility(p->facility_nr))
			continue;

		qpaci(&ib);
		switch (i) {
		case PAI_PMU_CRYPTO:
			p->num_avail = ib.num_cc;
			if (p->num_avail >= PAI_CRYPTO_MAXCTR) {
				pr_err("Too many PMU %s counters %d\n",
				       p->pmuname, p->num_avail);
				continue;
			}
			break;
		case PAI_PMU_EXT:
			p->num_avail = ib.num_nnpa;
			break;
		}
		p->num_avail += 1;		/* Add xxx_ALL event */
		if (p->init) {
			rc = p->init(p);
			if (!rc)
				++install_ok;
		}
	}
	return install_ok;
}

static int __init pai_init(void)
{
	/* Setup s390dbf facility */
	paidbg = debug_register("pai", 32, 256, 128);
	if (!paidbg) {
		pr_err("Registration of s390dbf pai failed\n");
		return -ENOMEM;
	}
	debug_register_view(paidbg, &debug_sprintf_view);

	if (!paipmu_setup()) {
		/* No PMU registration, no need for debug buffer */
		debug_unregister_view(paidbg, &debug_sprintf_view);
		debug_unregister(paidbg);
		return -ENODEV;
	}
	return 0;
}

device_initcall(pai_init);
