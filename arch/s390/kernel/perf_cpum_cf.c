/*
 * Performance event support for s390x - CPU-measurement Counter Facility
 *
 *  Copyright IBM Corp. 2012
 *  Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 */
#define KMSG_COMPONENT	"cpum_cf"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/perf_event.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/export.h>
#include <asm/ctl_reg.h>
#include <asm/irq.h>
#include <asm/cpu_mf.h>

/* CPU-measurement counter facility supports these CPU counter sets:
 * For CPU counter sets:
 *    Basic counter set:	     0-31
 *    Problem-state counter set:    32-63
 *    Crypto-activity counter set:  64-127
 *    Extented counter set:	   128-159
 */
enum cpumf_ctr_set {
	/* CPU counter sets */
	CPUMF_CTR_SET_BASIC   = 0,
	CPUMF_CTR_SET_USER    = 1,
	CPUMF_CTR_SET_CRYPTO  = 2,
	CPUMF_CTR_SET_EXT     = 3,

	/* Maximum number of counter sets */
	CPUMF_CTR_SET_MAX,
};

#define CPUMF_LCCTL_ENABLE_SHIFT    16
#define CPUMF_LCCTL_ACTCTL_SHIFT     0
static const u64 cpumf_state_ctl[CPUMF_CTR_SET_MAX] = {
	[CPUMF_CTR_SET_BASIC]	= 0x02,
	[CPUMF_CTR_SET_USER]	= 0x04,
	[CPUMF_CTR_SET_CRYPTO]	= 0x08,
	[CPUMF_CTR_SET_EXT]	= 0x01,
};

static void ctr_set_enable(u64 *state, int ctr_set)
{
	*state |= cpumf_state_ctl[ctr_set] << CPUMF_LCCTL_ENABLE_SHIFT;
}
static void ctr_set_disable(u64 *state, int ctr_set)
{
	*state &= ~(cpumf_state_ctl[ctr_set] << CPUMF_LCCTL_ENABLE_SHIFT);
}
static void ctr_set_start(u64 *state, int ctr_set)
{
	*state |= cpumf_state_ctl[ctr_set] << CPUMF_LCCTL_ACTCTL_SHIFT;
}
static void ctr_set_stop(u64 *state, int ctr_set)
{
	*state &= ~(cpumf_state_ctl[ctr_set] << CPUMF_LCCTL_ACTCTL_SHIFT);
}

/* Local CPUMF event structure */
struct cpu_hw_events {
	struct cpumf_ctr_info	info;
	atomic_t		ctr_set[CPUMF_CTR_SET_MAX];
	u64			state, tx_state;
	unsigned int		flags;
};
static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events) = {
	.ctr_set = {
		[CPUMF_CTR_SET_BASIC]  = ATOMIC_INIT(0),
		[CPUMF_CTR_SET_USER]   = ATOMIC_INIT(0),
		[CPUMF_CTR_SET_CRYPTO] = ATOMIC_INIT(0),
		[CPUMF_CTR_SET_EXT]    = ATOMIC_INIT(0),
	},
	.state = 0,
	.flags = 0,
};

static int get_counter_set(u64 event)
{
	int set = -1;

	if (event < 32)
		set = CPUMF_CTR_SET_BASIC;
	else if (event < 64)
		set = CPUMF_CTR_SET_USER;
	else if (event < 128)
		set = CPUMF_CTR_SET_CRYPTO;
	else if (event < 256)
		set = CPUMF_CTR_SET_EXT;

	return set;
}

static int validate_event(const struct hw_perf_event *hwc)
{
	switch (hwc->config_base) {
	case CPUMF_CTR_SET_BASIC:
	case CPUMF_CTR_SET_USER:
	case CPUMF_CTR_SET_CRYPTO:
	case CPUMF_CTR_SET_EXT:
		/* check for reserved counters */
		if ((hwc->config >=  6 && hwc->config <=  31) ||
		    (hwc->config >= 38 && hwc->config <=  63) ||
		    (hwc->config >= 80 && hwc->config <= 127))
			return -EOPNOTSUPP;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int validate_ctr_version(const struct hw_perf_event *hwc)
{
	struct cpu_hw_events *cpuhw;
	int err = 0;

	cpuhw = &get_cpu_var(cpu_hw_events);

	/* check required version for counter sets */
	switch (hwc->config_base) {
	case CPUMF_CTR_SET_BASIC:
	case CPUMF_CTR_SET_USER:
		if (cpuhw->info.cfvn < 1)
			err = -EOPNOTSUPP;
		break;
	case CPUMF_CTR_SET_CRYPTO:
	case CPUMF_CTR_SET_EXT:
		if (cpuhw->info.csvn < 1)
			err = -EOPNOTSUPP;
		if ((cpuhw->info.csvn == 1 && hwc->config > 159) ||
		    (cpuhw->info.csvn == 2 && hwc->config > 175) ||
		    (cpuhw->info.csvn  > 2 && hwc->config > 255))
			err = -EOPNOTSUPP;
		break;
	}

	put_cpu_var(cpu_hw_events);
	return err;
}

static int validate_ctr_auth(const struct hw_perf_event *hwc)
{
	struct cpu_hw_events *cpuhw;
	u64 ctrs_state;
	int err = 0;

	cpuhw = &get_cpu_var(cpu_hw_events);

	/* Check authorization for cpu counter sets.
	 * If the particular CPU counter set is not authorized,
	 * return with -ENOENT in order to fall back to other
	 * PMUs that might suffice the event request.
	 */
	ctrs_state = cpumf_state_ctl[hwc->config_base];
	if (!(ctrs_state & cpuhw->info.auth_ctl))
		err = -ENOENT;

	put_cpu_var(cpu_hw_events);
	return err;
}

/*
 * Change the CPUMF state to active.
 * Enable and activate the CPU-counter sets according
 * to the per-cpu control state.
 */
static void cpumf_pmu_enable(struct pmu *pmu)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);
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
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);
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

/* CPU-measurement alerts for the counter facility */
static void cpumf_measurement_alert(struct ext_code ext_code,
				    unsigned int alert, unsigned long unused)
{
	struct cpu_hw_events *cpuhw;

	if (!(alert & CPU_MF_INT_CF_MASK))
		return;

	inc_irq_stat(IRQEXT_CMC);
	cpuhw = this_cpu_ptr(&cpu_hw_events);

	/* Measurement alerts are shared and might happen when the PMU
	 * is not reserved.  Ignore these alerts in this case. */
	if (!(cpuhw->flags & PMU_F_RESERVED))
		return;

	/* counter authorization change alert */
	if (alert & CPU_MF_INT_CF_CACA)
		qctri(&cpuhw->info);

	/* loss of counter data alert */
	if (alert & CPU_MF_INT_CF_LCDA)
		pr_err("CPU[%i] Counter data was lost\n", smp_processor_id());
}

#define PMC_INIT      0
#define PMC_RELEASE   1
static void setup_pmc_cpu(void *flags)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);

	switch (*((int *) flags)) {
	case PMC_INIT:
		memset(&cpuhw->info, 0, sizeof(cpuhw->info));
		qctri(&cpuhw->info);
		cpuhw->flags |= PMU_F_RESERVED;
		break;

	case PMC_RELEASE:
		cpuhw->flags &= ~PMU_F_RESERVED;
		break;
	}

	/* Disable CPU counter sets */
	lcctl(0);
}

/* Initialize the CPU-measurement facility */
static int reserve_pmc_hardware(void)
{
	int flags = PMC_INIT;

	on_each_cpu(setup_pmc_cpu, &flags, 1);
	irq_subclass_register(IRQ_SUBCLASS_MEASUREMENT_ALERT);

	return 0;
}

/* Release the CPU-measurement facility */
static void release_pmc_hardware(void)
{
	int flags = PMC_RELEASE;

	on_each_cpu(setup_pmc_cpu, &flags, 1);
	irq_subclass_unregister(IRQ_SUBCLASS_MEASUREMENT_ALERT);
}

/* Release the PMU if event is the last perf event */
static void hw_perf_event_destroy(struct perf_event *event)
{
	if (!atomic_add_unless(&num_events, -1, 1)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_dec_return(&num_events) == 0)
			release_pmc_hardware();
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

static int __hw_perf_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	int err;
	u64 ev;

	switch (attr->type) {
	case PERF_TYPE_RAW:
		/* Raw events are used to access counters directly,
		 * hence do not permit excludes */
		if (attr->exclude_kernel || attr->exclude_user ||
		    attr->exclude_hv)
			return -EOPNOTSUPP;
		ev = attr->config;
		break;

	case PERF_TYPE_HARDWARE:
		ev = attr->config;
		/* Count user space (problem-state) only */
		if (!attr->exclude_user && attr->exclude_kernel) {
			if (ev >= ARRAY_SIZE(cpumf_generic_events_user))
				return -EOPNOTSUPP;
			ev = cpumf_generic_events_user[ev];

		/* No support for kernel space counters only */
		} else if (!attr->exclude_kernel && attr->exclude_user) {
			return -EOPNOTSUPP;

		/* Count user and kernel space */
		} else {
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

	if (ev >= PERF_CPUM_CF_MAX_CTR)
		return -EINVAL;

	/* Use the hardware perf event structure to store the counter number
	 * in 'config' member and the counter set to which the counter belongs
	 * in the 'config_base'.  The counter set (config_base) is then used
	 * to enable/disable the counters.
	 */
	hwc->config = ev;
	hwc->config_base = get_counter_set(ev);

	/* Validate the counter that is assigned to this event.
	 * Because the counter facility can use numerous counters at the
	 * same time without constraints, it is not necessary to explicity
	 * validate event groups (event->group_leader != event).
	 */
	err = validate_event(hwc);
	if (err)
		return err;

	/* Initialize for using the CPU-measurement counter facility */
	if (!atomic_inc_not_zero(&num_events)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_read(&num_events) == 0 && reserve_pmc_hardware())
			err = -EBUSY;
		else
			atomic_inc(&num_events);
		mutex_unlock(&pmc_reserve_mutex);
	}
	event->destroy = hw_perf_event_destroy;

	/* Finally, validate version and authorization of the counter set */
	err = validate_ctr_auth(hwc);
	if (!err)
		err = validate_ctr_version(hwc);

	return err;
}

static int cpumf_pmu_event_init(struct perf_event *event)
{
	int err;

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
	case PERF_TYPE_HW_CACHE:
	case PERF_TYPE_RAW:
		err = __hw_perf_event_init(event);
		break;
	default:
		return -ENOENT;
	}

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

static int hw_perf_event_update(struct perf_event *event)
{
	u64 prev, new, delta;
	int err;

	do {
		prev = local64_read(&event->hw.prev_count);
		err = ecctr(event->hw.config, &new);
		if (err)
			goto out;
	} while (local64_cmpxchg(&event->hw.prev_count, prev, new) != prev);

	delta = (prev <= new) ? new - prev
			      : (-1ULL - prev) + new + 1;	 /* overflow */
	local64_add(delta, &event->count);
out:
	return err;
}

static void cpumf_pmu_read(struct perf_event *event)
{
	if (event->hw.state & PERF_HES_STOPPED)
		return;

	hw_perf_event_update(event);
}

static void cpumf_pmu_start(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	if (WARN_ON_ONCE(hwc->config == -1))
		return;

	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));

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
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	if (!(hwc->state & PERF_HES_STOPPED)) {
		/* Decrement reference count for this counter set and if this
		 * is the last used counter in the set, clear activation
		 * control and set the counter set state to inactive.
		 */
		if (!atomic_dec_return(&cpuhw->ctr_set[hwc->config_base]))
			ctr_set_stop(&cpuhw->state, hwc->config_base);
		event->hw.state |= PERF_HES_STOPPED;
	}

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		hw_perf_event_update(event);
		event->hw.state |= PERF_HES_UPTODATE;
	}
}

static int cpumf_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);

	/* Check authorization for the counter set to which this
	 * counter belongs.
	 * For group events transaction, the authorization check is
	 * done in cpumf_pmu_commit_txn().
	 */
	if (!(cpuhw->flags & PERF_EVENT_TXN))
		if (validate_ctr_auth(&event->hw))
			return -ENOENT;

	ctr_set_enable(&cpuhw->state, event->hw.config_base);
	event->hw.state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		cpumf_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);

	return 0;
}

static void cpumf_pmu_del(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);

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

	perf_event_update_userpage(event);
}

/*
 * Start group events scheduling transaction.
 * Set flags to perform a single test at commit time.
 */
static void cpumf_pmu_start_txn(struct pmu *pmu)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);

	perf_pmu_disable(pmu);
	cpuhw->flags |= PERF_EVENT_TXN;
	cpuhw->tx_state = cpuhw->state;
}

/*
 * Stop and cancel a group events scheduling tranctions.
 * Assumes cpumf_pmu_del() is called for each successful added
 * cpumf_pmu_add() during the transaction.
 */
static void cpumf_pmu_cancel_txn(struct pmu *pmu)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);

	WARN_ON(cpuhw->tx_state != cpuhw->state);

	cpuhw->flags &= ~PERF_EVENT_TXN;
	perf_pmu_enable(pmu);
}

/*
 * Commit the group events scheduling transaction.  On success, the
 * transaction is closed.   On error, the transaction is kept open
 * until cpumf_pmu_cancel_txn() is called.
 */
static int cpumf_pmu_commit_txn(struct pmu *pmu)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);
	u64 state;

	/* check if the updated state can be scheduled */
	state = cpuhw->state & ~((1 << CPUMF_LCCTL_ENABLE_SHIFT) - 1);
	state >>= CPUMF_LCCTL_ENABLE_SHIFT;
	if ((state & cpuhw->info.auth_ctl) != state)
		return -ENOENT;

	cpuhw->flags &= ~PERF_EVENT_TXN;
	perf_pmu_enable(pmu);
	return 0;
}

/* Performance monitoring unit for s390x */
static struct pmu cpumf_pmu = {
	.pmu_enable   = cpumf_pmu_enable,
	.pmu_disable  = cpumf_pmu_disable,
	.event_init   = cpumf_pmu_event_init,
	.add	      = cpumf_pmu_add,
	.del	      = cpumf_pmu_del,
	.start	      = cpumf_pmu_start,
	.stop	      = cpumf_pmu_stop,
	.read	      = cpumf_pmu_read,
	.start_txn    = cpumf_pmu_start_txn,
	.commit_txn   = cpumf_pmu_commit_txn,
	.cancel_txn   = cpumf_pmu_cancel_txn,
};

static int cpumf_pmu_notifier(struct notifier_block *self, unsigned long action,
			      void *hcpu)
{
	unsigned int cpu = (long) hcpu;
	int flags;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_ONLINE:
		flags = PMC_INIT;
		smp_call_function_single(cpu, setup_pmc_cpu, &flags, 1);
		break;
	case CPU_DOWN_PREPARE:
		flags = PMC_RELEASE;
		smp_call_function_single(cpu, setup_pmc_cpu, &flags, 1);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int __init cpumf_pmu_init(void)
{
	int rc;

	if (!cpum_cf_avail())
		return -ENODEV;

	/* clear bit 15 of cr0 to unauthorize problem-state to
	 * extract measurement counters */
	ctl_clear_bit(0, 48);

	/* register handler for measurement-alert interruptions */
	rc = register_external_irq(EXT_IRQ_MEASURE_ALERT,
				   cpumf_measurement_alert);
	if (rc) {
		pr_err("Registering for CPU-measurement alerts "
		       "failed with rc=%i\n", rc);
		goto out;
	}

	/* The CPU measurement counter facility does not have overflow
	 * interrupts to do sampling.  Sampling must be provided by
	 * external means, for example, by timers.
	 */
	cpumf_pmu.capabilities |= PERF_PMU_CAP_NO_INTERRUPT;

	cpumf_pmu.attr_groups = cpumf_cf_event_group();
	rc = perf_pmu_register(&cpumf_pmu, "cpum_cf", PERF_TYPE_RAW);
	if (rc) {
		pr_err("Registering the cpum_cf PMU failed with rc=%i\n", rc);
		unregister_external_irq(EXT_IRQ_MEASURE_ALERT,
					cpumf_measurement_alert);
		goto out;
	}
	perf_cpu_notifier(cpumf_pmu_notifier);
out:
	return rc;
}
early_initcall(cpumf_pmu_init);
