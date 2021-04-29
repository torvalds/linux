// SPDX-License-Identifier: GPL-2.0
/*
 * Performance event support for s390x - CPU-measurement Counter Facility
 *
 *  Copyright IBM Corp. 2012, 2019
 *  Author(s): Hendrik Brueckner <brueckner@linux.ibm.com>
 */
#define KMSG_COMPONENT	"cpum_cf"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/export.h>
#include <asm/cpu_mcf.h>

static enum cpumf_ctr_set get_counter_set(u64 event)
{
	int set = CPUMF_CTR_SET_MAX;

	if (event < 32)
		set = CPUMF_CTR_SET_BASIC;
	else if (event < 64)
		set = CPUMF_CTR_SET_USER;
	else if (event < 128)
		set = CPUMF_CTR_SET_CRYPTO;
	else if (event < 288)
		set = CPUMF_CTR_SET_EXT;
	else if (event >= 448 && event < 496)
		set = CPUMF_CTR_SET_MT_DIAG;

	return set;
}

static int validate_ctr_version(const struct hw_perf_event *hwc)
{
	struct cpu_cf_events *cpuhw;
	int err = 0;
	u16 mtdiag_ctl;

	cpuhw = &get_cpu_var(cpu_cf_events);

	/* check required version for counter sets */
	switch (hwc->config_base) {
	case CPUMF_CTR_SET_BASIC:
	case CPUMF_CTR_SET_USER:
		if (cpuhw->info.cfvn < 1)
			err = -EOPNOTSUPP;
		break;
	case CPUMF_CTR_SET_CRYPTO:
		if ((cpuhw->info.csvn >= 1 && cpuhw->info.csvn <= 5 &&
		     hwc->config > 79) ||
		    (cpuhw->info.csvn >= 6 && hwc->config > 83))
			err = -EOPNOTSUPP;
		break;
	case CPUMF_CTR_SET_EXT:
		if (cpuhw->info.csvn < 1)
			err = -EOPNOTSUPP;
		if ((cpuhw->info.csvn == 1 && hwc->config > 159) ||
		    (cpuhw->info.csvn == 2 && hwc->config > 175) ||
		    (cpuhw->info.csvn >= 3 && cpuhw->info.csvn <= 5
		     && hwc->config > 255) ||
		    (cpuhw->info.csvn >= 6 && hwc->config > 287))
			err = -EOPNOTSUPP;
		break;
	case CPUMF_CTR_SET_MT_DIAG:
		if (cpuhw->info.csvn <= 3)
			err = -EOPNOTSUPP;
		/*
		 * MT-diagnostic counters are read-only.  The counter set
		 * is automatically enabled and activated on all CPUs with
		 * multithreading (SMT).  Deactivation of multithreading
		 * also disables the counter set.  State changes are ignored
		 * by lcctl().	Because Linux controls SMT enablement through
		 * a kernel parameter only, the counter set is either disabled
		 * or enabled and active.
		 *
		 * Thus, the counters can only be used if SMT is on and the
		 * counter set is enabled and active.
		 */
		mtdiag_ctl = cpumf_ctr_ctl[CPUMF_CTR_SET_MT_DIAG];
		if (!((cpuhw->info.auth_ctl & mtdiag_ctl) &&
		      (cpuhw->info.enable_ctl & mtdiag_ctl) &&
		      (cpuhw->info.act_ctl & mtdiag_ctl)))
			err = -EOPNOTSUPP;
		break;
	}

	put_cpu_var(cpu_cf_events);
	return err;
}

static int validate_ctr_auth(const struct hw_perf_event *hwc)
{
	struct cpu_cf_events *cpuhw;
	u64 ctrs_state;
	int err = 0;

	cpuhw = &get_cpu_var(cpu_cf_events);

	/* Check authorization for cpu counter sets.
	 * If the particular CPU counter set is not authorized,
	 * return with -ENOENT in order to fall back to other
	 * PMUs that might suffice the event request.
	 */
	ctrs_state = cpumf_ctr_ctl[hwc->config_base];
	if (!(ctrs_state & cpuhw->info.auth_ctl))
		err = -ENOENT;

	put_cpu_var(cpu_cf_events);
	return err;
}

/*
 * Change the CPUMF state to active.
 * Enable and activate the CPU-counter sets according
 * to the per-cpu control state.
 */
static void cpumf_pmu_enable(struct pmu *pmu)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	int err;

	if (cpuhw->flags & PMU_F_ENABLED)
		return;

	err = lcctl(cpuhw->state);
	if (err) {
		pr_err("Enabling the performance measuring unit "
		       "failed with rc=%x\n", err);
		return;
	}

	cpuhw->flags |= PMU_F_ENABLED;
}

/*
 * Change the CPUMF state to inactive.
 * Disable and enable (inactive) the CPU-counter sets according
 * to the per-cpu control state.
 */
static void cpumf_pmu_disable(struct pmu *pmu)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	int err;
	u64 inactive;

	if (!(cpuhw->flags & PMU_F_ENABLED))
		return;

	inactive = cpuhw->state & ~((1 << CPUMF_LCCTL_ENABLE_SHIFT) - 1);
	err = lcctl(inactive);
	if (err) {
		pr_err("Disabling the performance measuring unit "
		       "failed with rc=%x\n", err);
		return;
	}

	cpuhw->flags &= ~PMU_F_ENABLED;
}


/* Number of perf events counting hardware events */
static atomic_t num_events = ATOMIC_INIT(0);
/* Used to avoid races in calling reserve/release_cpumf_hardware */
static DEFINE_MUTEX(pmc_reserve_mutex);

/* Release the PMU if event is the last perf event */
static void hw_perf_event_destroy(struct perf_event *event)
{
	if (!atomic_add_unless(&num_events, -1, 1)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_dec_return(&num_events) == 0)
			__kernel_cpumcf_end();
		mutex_unlock(&pmc_reserve_mutex);
	}
}

/* CPUMF <-> perf event mappings for kernel+userspace (basic set) */
static const int cpumf_generic_events_basic[] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = 0,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = 1,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = -1,
	[PERF_COUNT_HW_CACHE_MISSES]	    = -1,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = -1,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = -1,
	[PERF_COUNT_HW_BUS_CYCLES]	    = -1,
};
/* CPUMF <-> perf event mappings for userspace (problem-state set) */
static const int cpumf_generic_events_user[] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = 32,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = 33,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = -1,
	[PERF_COUNT_HW_CACHE_MISSES]	    = -1,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = -1,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = -1,
	[PERF_COUNT_HW_BUS_CYCLES]	    = -1,
};

static int __hw_perf_event_init(struct perf_event *event, unsigned int type)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	enum cpumf_ctr_set set;
	int err = 0;
	u64 ev;

	switch (type) {
	case PERF_TYPE_RAW:
		/* Raw events are used to access counters directly,
		 * hence do not permit excludes */
		if (attr->exclude_kernel || attr->exclude_user ||
		    attr->exclude_hv)
			return -EOPNOTSUPP;
		ev = attr->config;
		break;

	case PERF_TYPE_HARDWARE:
		if (is_sampling_event(event))	/* No sampling support */
			return -ENOENT;
		ev = attr->config;
		/* Count user space (problem-state) only */
		if (!attr->exclude_user && attr->exclude_kernel) {
			if (ev >= ARRAY_SIZE(cpumf_generic_events_user))
				return -EOPNOTSUPP;
			ev = cpumf_generic_events_user[ev];

		/* No support for kernel space counters only */
		} else if (!attr->exclude_kernel && attr->exclude_user) {
			return -EOPNOTSUPP;
		} else {	/* Count user and kernel space */
			if (ev >= ARRAY_SIZE(cpumf_generic_events_basic))
				return -EOPNOTSUPP;
			ev = cpumf_generic_events_basic[ev];
		}
		break;

	default:
		return -ENOENT;
	}

	if (ev == -1)
		return -ENOENT;

	if (ev > PERF_CPUM_CF_MAX_CTR)
		return -ENOENT;

	/* Obtain the counter set to which the specified counter belongs */
	set = get_counter_set(ev);
	switch (set) {
	case CPUMF_CTR_SET_BASIC:
	case CPUMF_CTR_SET_USER:
	case CPUMF_CTR_SET_CRYPTO:
	case CPUMF_CTR_SET_EXT:
	case CPUMF_CTR_SET_MT_DIAG:
		/*
		 * Use the hardware perf event structure to store the
		 * counter number in the 'config' member and the counter
		 * set number in the 'config_base'.  The counter set number
		 * is then later used to enable/disable the counter(s).
		 */
		hwc->config = ev;
		hwc->config_base = set;
		break;
	case CPUMF_CTR_SET_MAX:
		/* The counter could not be associated to a counter set */
		return -EINVAL;
	}

	/* Initialize for using the CPU-measurement counter facility */
	if (!atomic_inc_not_zero(&num_events)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_read(&num_events) == 0 && __kernel_cpumcf_begin())
			err = -EBUSY;
		else
			atomic_inc(&num_events);
		mutex_unlock(&pmc_reserve_mutex);
	}
	if (err)
		return err;
	event->destroy = hw_perf_event_destroy;

	/* Finally, validate version and authorization of the counter set */
	err = validate_ctr_auth(hwc);
	if (!err)
		err = validate_ctr_version(hwc);

	return err;
}

static int cpumf_pmu_event_init(struct perf_event *event)
{
	unsigned int type = event->attr.type;
	int err;

	if (type == PERF_TYPE_HARDWARE || type == PERF_TYPE_RAW)
		err = __hw_perf_event_init(event, type);
	else if (event->pmu->type == type)
		/* Registered as unknown PMU */
		err = __hw_perf_event_init(event, PERF_TYPE_RAW);
	else
		return -ENOENT;

	if (unlikely(err) && event->destroy)
		event->destroy(event);

	return err;
}

static int hw_perf_event_reset(struct perf_event *event)
{
	u64 prev, new;
	int err;

	do {
		prev = local64_read(&event->hw.prev_count);
		err = ecctr(event->hw.config, &new);
		if (err) {
			if (err != 3)
				break;
			/* The counter is not (yet) available. This
			 * might happen if the counter set to which
			 * this counter belongs is in the disabled
			 * state.
			 */
			new = 0;
		}
	} while (local64_cmpxchg(&event->hw.prev_count, prev, new) != prev);

	return err;
}

static void hw_perf_event_update(struct perf_event *event)
{
	u64 prev, new, delta;
	int err;

	do {
		prev = local64_read(&event->hw.prev_count);
		err = ecctr(event->hw.config, &new);
		if (err)
			return;
	} while (local64_cmpxchg(&event->hw.prev_count, prev, new) != prev);

	delta = (prev <= new) ? new - prev
			      : (-1ULL - prev) + new + 1;	 /* overflow */
	local64_add(delta, &event->count);
}

static void cpumf_pmu_read(struct perf_event *event)
{
	if (event->hw.state & PERF_HES_STOPPED)
		return;

	hw_perf_event_update(event);
}

static void cpumf_pmu_start(struct perf_event *event, int flags)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	struct hw_perf_event *hwc = &event->hw;

	if (!(hwc->state & PERF_HES_STOPPED))
		return;

	hwc->state = 0;

	/* (Re-)enable and activate the counter set */
	ctr_set_enable(&cpuhw->state, hwc->config_base);
	ctr_set_start(&cpuhw->state, hwc->config_base);

	/* The counter set to which this counter belongs can be already active.
	 * Because all counters in a set are active, the event->hw.prev_count
	 * needs to be synchronized.  At this point, the counter set can be in
	 * the inactive or disabled state.
	 */
	hw_perf_event_reset(event);

	/* increment refcount for this counter set */
	atomic_inc(&cpuhw->ctr_set[hwc->config_base]);
}

static void cpumf_pmu_stop(struct perf_event *event, int flags)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	struct hw_perf_event *hwc = &event->hw;

	if (!(hwc->state & PERF_HES_STOPPED)) {
		/* Decrement reference count for this counter set and if this
		 * is the last used counter in the set, clear activation
		 * control and set the counter set state to inactive.
		 */
		if (!atomic_dec_return(&cpuhw->ctr_set[hwc->config_base]))
			ctr_set_stop(&cpuhw->state, hwc->config_base);
		hwc->state |= PERF_HES_STOPPED;
	}

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		hw_perf_event_update(event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

static int cpumf_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);

	ctr_set_enable(&cpuhw->state, event->hw.config_base);
	event->hw.state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		cpumf_pmu_start(event, PERF_EF_RELOAD);

	return 0;
}

static void cpumf_pmu_del(struct perf_event *event, int flags)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);

	cpumf_pmu_stop(event, PERF_EF_UPDATE);

	/* Check if any counter in the counter set is still used.  If not used,
	 * change the counter set to the disabled state.  This also clears the
	 * content of all counters in the set.
	 *
	 * When a new perf event has been added but not yet started, this can
	 * clear enable control and resets all counters in a set.  Therefore,
	 * cpumf_pmu_start() always has to reenable a counter set.
	 */
	if (!atomic_read(&cpuhw->ctr_set[event->hw.config_base]))
		ctr_set_disable(&cpuhw->state, event->hw.config_base);
}

/* Performance monitoring unit for s390x */
static struct pmu cpumf_pmu = {
	.task_ctx_nr  = perf_sw_context,
	.capabilities = PERF_PMU_CAP_NO_INTERRUPT,
	.pmu_enable   = cpumf_pmu_enable,
	.pmu_disable  = cpumf_pmu_disable,
	.event_init   = cpumf_pmu_event_init,
	.add	      = cpumf_pmu_add,
	.del	      = cpumf_pmu_del,
	.start	      = cpumf_pmu_start,
	.stop	      = cpumf_pmu_stop,
	.read	      = cpumf_pmu_read,
};

static int __init cpumf_pmu_init(void)
{
	int rc;

	if (!kernel_cpumcf_avail())
		return -ENODEV;

	cpumf_pmu.attr_groups = cpumf_cf_event_group();
	rc = perf_pmu_register(&cpumf_pmu, "cpum_cf", -1);
	if (rc)
		pr_err("Registering the cpum_cf PMU failed with rc=%i\n", rc);
	return rc;
}
subsys_initcall(cpumf_pmu_init);
