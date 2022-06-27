// SPDX-License-Identifier: GPL-2.0
/*
 * Performance event support for s390x - CPU-measurement Counter Facility
 *
 *  Copyright IBM Corp. 2012, 2021
 *  Author(s): Hendrik Brueckner <brueckner@linux.ibm.com>
 *	       Thomas Richter <tmricht@linux.ibm.com>
 */
#define KMSG_COMPONENT	"cpum_cf"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/miscdevice.h>

#include <asm/cpu_mcf.h>
#include <asm/hwctrset.h>
#include <asm/debug.h>

static unsigned int cfdiag_cpu_speed;	/* CPU speed for CF_DIAG trailer */
static debug_info_t *cf_dbg;

#define	CF_DIAG_CTRSET_DEF		0xfeef	/* Counter set header mark */
						/* interval in seconds */

/* Counter sets are stored as data stream in a page sized memory buffer and
 * exported to user space via raw data attached to the event sample data.
 * Each counter set starts with an eight byte header consisting of:
 * - a two byte eye catcher (0xfeef)
 * - a one byte counter set number
 * - a two byte counter set size (indicates the number of counters in this set)
 * - a three byte reserved value (must be zero) to make the header the same
 *   size as a counter value.
 * All counter values are eight byte in size.
 *
 * All counter sets are followed by a 64 byte trailer.
 * The trailer consists of a:
 * - flag field indicating valid fields when corresponding bit set
 * - the counter facility first and second version number
 * - the CPU speed if nonzero
 * - the time stamp the counter sets have been collected
 * - the time of day (TOD) base value
 * - the machine type.
 *
 * The counter sets are saved when the process is prepared to be executed on a
 * CPU and saved again when the process is going to be removed from a CPU.
 * The difference of both counter sets are calculated and stored in the event
 * sample data area.
 */
struct cf_ctrset_entry {	/* CPU-M CF counter set entry (8 byte) */
	unsigned int def:16;	/* 0-15  Data Entry Format */
	unsigned int set:16;	/* 16-31 Counter set identifier */
	unsigned int ctr:16;	/* 32-47 Number of stored counters */
	unsigned int res1:16;	/* 48-63 Reserved */
};

struct cf_trailer_entry {	/* CPU-M CF_DIAG trailer (64 byte) */
	/* 0 - 7 */
	union {
		struct {
			unsigned int clock_base:1;	/* TOD clock base set */
			unsigned int speed:1;		/* CPU speed set */
			/* Measurement alerts */
			unsigned int mtda:1;	/* Loss of MT ctr. data alert */
			unsigned int caca:1;	/* Counter auth. change alert */
			unsigned int lcda:1;	/* Loss of counter data alert */
		};
		unsigned long flags;	/* 0-63    All indicators */
	};
	/* 8 - 15 */
	unsigned int cfvn:16;			/* 64-79   Ctr First Version */
	unsigned int csvn:16;			/* 80-95   Ctr Second Version */
	unsigned int cpu_speed:32;		/* 96-127  CPU speed */
	/* 16 - 23 */
	unsigned long timestamp;		/* 128-191 Timestamp (TOD) */
	/* 24 - 55 */
	union {
		struct {
			unsigned long progusage1;
			unsigned long progusage2;
			unsigned long progusage3;
			unsigned long tod_base;
		};
		unsigned long progusage[4];
	};
	/* 56 - 63 */
	unsigned int mach_type:16;		/* Machine type */
	unsigned int res1:16;			/* Reserved */
	unsigned int res2:32;			/* Reserved */
};

/* Create the trailer data at the end of a page. */
static void cfdiag_trailer(struct cf_trailer_entry *te)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	struct cpuid cpuid;

	te->cfvn = cpuhw->info.cfvn;		/* Counter version numbers */
	te->csvn = cpuhw->info.csvn;

	get_cpu_id(&cpuid);			/* Machine type */
	te->mach_type = cpuid.machine;
	te->cpu_speed = cfdiag_cpu_speed;
	if (te->cpu_speed)
		te->speed = 1;
	te->clock_base = 1;			/* Save clock base */
	te->tod_base = tod_clock_base.tod;
	te->timestamp = get_tod_clock_fast();
}

/* Read a counter set. The counter set number determines the counter set and
 * the CPUM-CF first and second version number determine the number of
 * available counters in each counter set.
 * Each counter set starts with header containing the counter set number and
 * the number of eight byte counters.
 *
 * The functions returns the number of bytes occupied by this counter set
 * including the header.
 * If there is no counter in the counter set, this counter set is useless and
 * zero is returned on this case.
 *
 * Note that the counter sets may not be enabled or active and the stcctm
 * instruction might return error 3. Depending on error_ok value this is ok,
 * for example when called from cpumf_pmu_start() call back function.
 */
static size_t cfdiag_getctrset(struct cf_ctrset_entry *ctrdata, int ctrset,
			       size_t room, bool error_ok)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	size_t ctrset_size, need = 0;
	int rc = 3;				/* Assume write failure */

	ctrdata->def = CF_DIAG_CTRSET_DEF;
	ctrdata->set = ctrset;
	ctrdata->res1 = 0;
	ctrset_size = cpum_cf_ctrset_size(ctrset, &cpuhw->info);

	if (ctrset_size) {			/* Save data */
		need = ctrset_size * sizeof(u64) + sizeof(*ctrdata);
		if (need <= room) {
			rc = ctr_stcctm(ctrset, ctrset_size,
					(u64 *)(ctrdata + 1));
		}
		if (rc != 3 || error_ok)
			ctrdata->ctr = ctrset_size;
		else
			need = 0;
	}

	debug_sprintf_event(cf_dbg, 3,
			    "%s ctrset %d ctrset_size %zu cfvn %d csvn %d"
			    " need %zd rc %d\n", __func__, ctrset, ctrset_size,
			    cpuhw->info.cfvn, cpuhw->info.csvn, need, rc);
	return need;
}

static const u64 cpumf_ctr_ctl[CPUMF_CTR_SET_MAX] = {
	[CPUMF_CTR_SET_BASIC]	= 0x02,
	[CPUMF_CTR_SET_USER]	= 0x04,
	[CPUMF_CTR_SET_CRYPTO]	= 0x08,
	[CPUMF_CTR_SET_EXT]	= 0x01,
	[CPUMF_CTR_SET_MT_DIAG] = 0x20,
};

/* Read out all counter sets and save them in the provided data buffer.
 * The last 64 byte host an artificial trailer entry.
 */
static size_t cfdiag_getctr(void *data, size_t sz, unsigned long auth,
			    bool error_ok)
{
	struct cf_trailer_entry *trailer;
	size_t offset = 0, done;
	int i;

	memset(data, 0, sz);
	sz -= sizeof(*trailer);		/* Always room for trailer */
	for (i = CPUMF_CTR_SET_BASIC; i < CPUMF_CTR_SET_MAX; ++i) {
		struct cf_ctrset_entry *ctrdata = data + offset;

		if (!(auth & cpumf_ctr_ctl[i]))
			continue;	/* Counter set not authorized */

		done = cfdiag_getctrset(ctrdata, i, sz - offset, error_ok);
		offset += done;
	}
	trailer = data + offset;
	cfdiag_trailer(trailer);
	return offset + sizeof(*trailer);
}

/* Calculate the difference for each counter in a counter set. */
static void cfdiag_diffctrset(u64 *pstart, u64 *pstop, int counters)
{
	for (; --counters >= 0; ++pstart, ++pstop)
		if (*pstop >= *pstart)
			*pstop -= *pstart;
		else
			*pstop = *pstart - *pstop + 1;
}

/* Scan the counter sets and calculate the difference of each counter
 * in each set. The result is the increment of each counter during the
 * period the counter set has been activated.
 *
 * Return true on success.
 */
static int cfdiag_diffctr(struct cpu_cf_events *cpuhw, unsigned long auth)
{
	struct cf_trailer_entry *trailer_start, *trailer_stop;
	struct cf_ctrset_entry *ctrstart, *ctrstop;
	size_t offset = 0;

	auth &= (1 << CPUMF_LCCTL_ENABLE_SHIFT) - 1;
	do {
		ctrstart = (struct cf_ctrset_entry *)(cpuhw->start + offset);
		ctrstop = (struct cf_ctrset_entry *)(cpuhw->stop + offset);

		if (memcmp(ctrstop, ctrstart, sizeof(*ctrstop))) {
			pr_err_once("cpum_cf_diag counter set compare error "
				    "in set %i\n", ctrstart->set);
			return 0;
		}
		auth &= ~cpumf_ctr_ctl[ctrstart->set];
		if (ctrstart->def == CF_DIAG_CTRSET_DEF) {
			cfdiag_diffctrset((u64 *)(ctrstart + 1),
					  (u64 *)(ctrstop + 1), ctrstart->ctr);
			offset += ctrstart->ctr * sizeof(u64) +
							sizeof(*ctrstart);
		}
	} while (ctrstart->def && auth);

	/* Save time_stamp from start of event in stop's trailer */
	trailer_start = (struct cf_trailer_entry *)(cpuhw->start + offset);
	trailer_stop = (struct cf_trailer_entry *)(cpuhw->stop + offset);
	trailer_stop->progusage[0] = trailer_start->timestamp;

	return 1;
}

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

static int validate_ctr_version(const struct hw_perf_event *hwc,
				enum cpumf_ctr_set set)
{
	struct cpu_cf_events *cpuhw;
	int err = 0;
	u16 mtdiag_ctl;

	cpuhw = &get_cpu_var(cpu_cf_events);

	/* check required version for counter sets */
	switch (set) {
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
	case CPUMF_CTR_SET_MAX:
		err = -EOPNOTSUPP;
	}

	put_cpu_var(cpu_cf_events);
	return err;
}

static int validate_ctr_auth(const struct hw_perf_event *hwc)
{
	struct cpu_cf_events *cpuhw;
	int err = 0;

	cpuhw = &get_cpu_var(cpu_cf_events);

	/* Check authorization for cpu counter sets.
	 * If the particular CPU counter set is not authorized,
	 * return with -ENOENT in order to fall back to other
	 * PMUs that might suffice the event request.
	 */
	if (!(hwc->config_base & cpuhw->info.auth_ctl))
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

	err = lcctl(cpuhw->state | cpuhw->dev_state);
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
	inactive |= cpuhw->dev_state;
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

static void cpumf_hw_inuse(void)
{
	mutex_lock(&pmc_reserve_mutex);
	if (atomic_inc_return(&num_events) == 1)
		__kernel_cpumcf_begin();
	mutex_unlock(&pmc_reserve_mutex);
}

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
		 * set number in the 'config_base' as bit mask.
		 * It is later used to enable/disable the counter(s).
		 */
		hwc->config = ev;
		hwc->config_base = cpumf_ctr_ctl[set];
		break;
	case CPUMF_CTR_SET_MAX:
		/* The counter could not be associated to a counter set */
		return -EINVAL;
	}

	/* Initialize for using the CPU-measurement counter facility */
	cpumf_hw_inuse();
	event->destroy = hw_perf_event_destroy;

	/* Finally, validate version and authorization of the counter set */
	err = validate_ctr_auth(hwc);
	if (!err)
		err = validate_ctr_version(hwc, set);

	return err;
}

/* Events CPU_CYLCES and INSTRUCTIONS can be submitted with two different
 * attribute::type values:
 * - PERF_TYPE_HARDWARE:
 * - pmu->type:
 * Handle both type of invocations identical. They address the same hardware.
 * The result is different when event modifiers exclude_kernel and/or
 * exclude_user are also set.
 */
static int cpumf_pmu_event_type(struct perf_event *event)
{
	u64 ev = event->attr.config;

	if (cpumf_generic_events_basic[PERF_COUNT_HW_CPU_CYCLES] == ev ||
	    cpumf_generic_events_basic[PERF_COUNT_HW_INSTRUCTIONS] == ev ||
	    cpumf_generic_events_user[PERF_COUNT_HW_CPU_CYCLES] == ev ||
	    cpumf_generic_events_user[PERF_COUNT_HW_INSTRUCTIONS] == ev)
		return PERF_TYPE_HARDWARE;
	return PERF_TYPE_RAW;
}

static int cpumf_pmu_event_init(struct perf_event *event)
{
	unsigned int type = event->attr.type;
	int err;

	if (type == PERF_TYPE_HARDWARE || type == PERF_TYPE_RAW)
		err = __hw_perf_event_init(event, type);
	else if (event->pmu->type == type)
		/* Registered as unknown PMU */
		err = __hw_perf_event_init(event, cpumf_pmu_event_type(event));
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
	int i;

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
	if (hwc->config == PERF_EVENT_CPUM_CF_DIAG) {
		cpuhw->usedss = cfdiag_getctr(cpuhw->start,
					      sizeof(cpuhw->start),
					      hwc->config_base, true);
	} else {
		hw_perf_event_reset(event);
	}

	/* Increment refcount for counter sets */
	for (i = CPUMF_CTR_SET_BASIC; i < CPUMF_CTR_SET_MAX; ++i)
		if ((hwc->config_base & cpumf_ctr_ctl[i]))
			atomic_inc(&cpuhw->ctr_set[i]);
}

/* Create perf event sample with the counter sets as raw data.	The sample
 * is then pushed to the event subsystem and the function checks for
 * possible event overflows. If an event overflow occurs, the PMU is
 * stopped.
 *
 * Return non-zero if an event overflow occurred.
 */
static int cfdiag_push_sample(struct perf_event *event,
			      struct cpu_cf_events *cpuhw)
{
	struct perf_sample_data data;
	struct perf_raw_record raw;
	struct pt_regs regs;
	int overflow;

	/* Setup perf sample */
	perf_sample_data_init(&data, 0, event->hw.last_period);
	memset(&regs, 0, sizeof(regs));
	memset(&raw, 0, sizeof(raw));

	if (event->attr.sample_type & PERF_SAMPLE_CPU)
		data.cpu_entry.cpu = event->cpu;
	if (event->attr.sample_type & PERF_SAMPLE_RAW) {
		raw.frag.size = cpuhw->usedss;
		raw.frag.data = cpuhw->stop;
		raw.size = raw.frag.size;
		data.raw = &raw;
	}

	overflow = perf_event_overflow(event, &data, &regs);
	debug_sprintf_event(cf_dbg, 3,
			    "%s event %#llx sample_type %#llx raw %d ov %d\n",
			    __func__, event->hw.config,
			    event->attr.sample_type, raw.size, overflow);
	if (overflow)
		event->pmu->stop(event, 0);

	perf_event_update_userpage(event);
	return overflow;
}

static void cpumf_pmu_stop(struct perf_event *event, int flags)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	struct hw_perf_event *hwc = &event->hw;
	int i;

	if (!(hwc->state & PERF_HES_STOPPED)) {
		/* Decrement reference count for this counter set and if this
		 * is the last used counter in the set, clear activation
		 * control and set the counter set state to inactive.
		 */
		for (i = CPUMF_CTR_SET_BASIC; i < CPUMF_CTR_SET_MAX; ++i) {
			if (!(hwc->config_base & cpumf_ctr_ctl[i]))
				continue;
			if (!atomic_dec_return(&cpuhw->ctr_set[i]))
				ctr_set_stop(&cpuhw->state, cpumf_ctr_ctl[i]);
		}
		hwc->state |= PERF_HES_STOPPED;
	}

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		if (hwc->config == PERF_EVENT_CPUM_CF_DIAG) {
			local64_inc(&event->count);
			cpuhw->usedss = cfdiag_getctr(cpuhw->stop,
						      sizeof(cpuhw->stop),
						      event->hw.config_base,
						      false);
			if (cfdiag_diffctr(cpuhw, event->hw.config_base))
				cfdiag_push_sample(event, cpuhw);
		} else if (cpuhw->flags & PMU_F_RESERVED) {
			/* Only update when PMU not hotplugged off */
			hw_perf_event_update(event);
		}
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
	int i;

	cpumf_pmu_stop(event, PERF_EF_UPDATE);

	/* Check if any counter in the counter set is still used.  If not used,
	 * change the counter set to the disabled state.  This also clears the
	 * content of all counters in the set.
	 *
	 * When a new perf event has been added but not yet started, this can
	 * clear enable control and resets all counters in a set.  Therefore,
	 * cpumf_pmu_start() always has to reenable a counter set.
	 */
	for (i = CPUMF_CTR_SET_BASIC; i < CPUMF_CTR_SET_MAX; ++i)
		if (!atomic_read(&cpuhw->ctr_set[i]))
			ctr_set_disable(&cpuhw->state, cpumf_ctr_ctl[i]);
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

static int cfset_init(void);
static int __init cpumf_pmu_init(void)
{
	int rc;

	if (!kernel_cpumcf_avail())
		return -ENODEV;

	/* Setup s390dbf facility */
	cf_dbg = debug_register(KMSG_COMPONENT, 2, 1, 128);
	if (!cf_dbg) {
		pr_err("Registration of s390dbf(cpum_cf) failed\n");
		return -ENOMEM;
	}
	debug_register_view(cf_dbg, &debug_sprintf_view);

	cpumf_pmu.attr_groups = cpumf_cf_event_group();
	rc = perf_pmu_register(&cpumf_pmu, "cpum_cf", -1);
	if (rc) {
		debug_unregister_view(cf_dbg, &debug_sprintf_view);
		debug_unregister(cf_dbg);
		pr_err("Registering the cpum_cf PMU failed with rc=%i\n", rc);
	} else if (stccm_avail()) {	/* Setup counter set device */
		cfset_init();
	}
	return rc;
}

/* Support for the CPU Measurement Facility counter set extraction using
 * device /dev/hwctr. This allows user space programs to extract complete
 * counter set via normal file operations.
 */

static atomic_t cfset_opencnt = ATOMIC_INIT(0);		/* Access count */
static DEFINE_MUTEX(cfset_ctrset_mutex);/* Synchronize access to hardware */
struct cfset_call_on_cpu_parm {		/* Parm struct for smp_call_on_cpu */
	unsigned int sets;		/* Counter set bit mask */
	atomic_t cpus_ack;		/* # CPUs successfully executed func */
};

static struct cfset_session {		/* CPUs and counter set bit mask */
	struct list_head head;		/* Head of list of active processes */
} cfset_session = {
	.head = LIST_HEAD_INIT(cfset_session.head)
};

struct cfset_request {			/* CPUs and counter set bit mask */
	unsigned long ctrset;		/* Bit mask of counter set to read */
	cpumask_t mask;			/* CPU mask to read from */
	struct list_head node;		/* Chain to cfset_session.head */
};

static void cfset_session_init(void)
{
	INIT_LIST_HEAD(&cfset_session.head);
}

/* Remove current request from global bookkeeping. Maintain a counter set bit
 * mask on a per CPU basis.
 * Done in process context under mutex protection.
 */
static void cfset_session_del(struct cfset_request *p)
{
	list_del(&p->node);
}

/* Add current request to global bookkeeping. Maintain a counter set bit mask
 * on a per CPU basis.
 * Done in process context under mutex protection.
 */
static void cfset_session_add(struct cfset_request *p)
{
	list_add(&p->node, &cfset_session.head);
}

/* The /dev/hwctr device access uses PMU_F_IN_USE to mark the device access
 * path is currently used.
 * The cpu_cf_events::dev_state is used to denote counter sets in use by this
 * interface. It is always or'ed in. If this interface is not active, its
 * value is zero and no additional counter sets will be included.
 *
 * The cpu_cf_events::state is used by the perf_event_open SVC and remains
 * unchanged.
 *
 * perf_pmu_enable() and perf_pmu_enable() and its call backs
 * cpumf_pmu_enable() and  cpumf_pmu_disable() are called by the
 * performance measurement subsystem to enable per process
 * CPU Measurement counter facility.
 * The XXX_enable() and XXX_disable functions are used to turn off
 * x86 performance monitoring interrupt (PMI) during scheduling.
 * s390 uses these calls to temporarily stop and resume the active CPU
 * counters sets during scheduling.
 *
 * We do allow concurrent access of perf_event_open() SVC and /dev/hwctr
 * device access.  The perf_event_open() SVC interface makes a lot of effort
 * to only run the counters while the calling process is actively scheduled
 * to run.
 * When /dev/hwctr interface is also used at the same time, the counter sets
 * will keep running, even when the process is scheduled off a CPU.
 * However this is not a problem and does not lead to wrong counter values
 * for the perf_event_open() SVC. The current counter value will be recorded
 * during schedule-in. At schedule-out time the current counter value is
 * extracted again and the delta is calculated and added to the event.
 */
/* Stop all counter sets via ioctl interface */
static void cfset_ioctl_off(void *parm)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	struct cfset_call_on_cpu_parm *p = parm;
	int rc;

	/* Check if any counter set used by /dev/hwc */
	for (rc = CPUMF_CTR_SET_BASIC; rc < CPUMF_CTR_SET_MAX; ++rc)
		if ((p->sets & cpumf_ctr_ctl[rc])) {
			if (!atomic_dec_return(&cpuhw->ctr_set[rc])) {
				ctr_set_disable(&cpuhw->dev_state,
						cpumf_ctr_ctl[rc]);
				ctr_set_stop(&cpuhw->dev_state,
					     cpumf_ctr_ctl[rc]);
			}
		}
	/* Keep perf_event_open counter sets */
	rc = lcctl(cpuhw->dev_state | cpuhw->state);
	if (rc)
		pr_err("Counter set stop %#llx of /dev/%s failed rc=%i\n",
		       cpuhw->state, S390_HWCTR_DEVICE, rc);
	if (!cpuhw->dev_state)
		cpuhw->flags &= ~PMU_F_IN_USE;
	debug_sprintf_event(cf_dbg, 4, "%s rc %d state %#llx dev_state %#llx\n",
			    __func__, rc, cpuhw->state, cpuhw->dev_state);
}

/* Start counter sets on particular CPU */
static void cfset_ioctl_on(void *parm)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	struct cfset_call_on_cpu_parm *p = parm;
	int rc;

	cpuhw->flags |= PMU_F_IN_USE;
	ctr_set_enable(&cpuhw->dev_state, p->sets);
	ctr_set_start(&cpuhw->dev_state, p->sets);
	for (rc = CPUMF_CTR_SET_BASIC; rc < CPUMF_CTR_SET_MAX; ++rc)
		if ((p->sets & cpumf_ctr_ctl[rc]))
			atomic_inc(&cpuhw->ctr_set[rc]);
	rc = lcctl(cpuhw->dev_state | cpuhw->state);	/* Start counter sets */
	if (!rc)
		atomic_inc(&p->cpus_ack);
	else
		pr_err("Counter set start %#llx of /dev/%s failed rc=%i\n",
		       cpuhw->dev_state | cpuhw->state, S390_HWCTR_DEVICE, rc);
	debug_sprintf_event(cf_dbg, 4, "%s rc %d state %#llx dev_state %#llx\n",
			    __func__, rc, cpuhw->state, cpuhw->dev_state);
}

static void cfset_release_cpu(void *p)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	int rc;

	debug_sprintf_event(cf_dbg, 4, "%s state %#llx dev_state %#llx\n",
			    __func__, cpuhw->state, cpuhw->dev_state);
	cpuhw->dev_state = 0;
	rc = lcctl(cpuhw->state);	/* Keep perf_event_open counter sets */
	if (rc)
		pr_err("Counter set release %#llx of /dev/%s failed rc=%i\n",
		       cpuhw->state, S390_HWCTR_DEVICE, rc);
}

/* This modifies the process CPU mask to adopt it to the currently online
 * CPUs. Offline CPUs can not be addresses. This call terminates the access
 * and is usually followed by close() or a new iotcl(..., START, ...) which
 * creates a new request structure.
 */
static void cfset_all_stop(struct cfset_request *req)
{
	struct cfset_call_on_cpu_parm p = {
		.sets = req->ctrset,
	};

	cpumask_and(&req->mask, &req->mask, cpu_online_mask);
	on_each_cpu_mask(&req->mask, cfset_ioctl_off, &p, 1);
}

/* Release function is also called when application gets terminated without
 * doing a proper ioctl(..., S390_HWCTR_STOP, ...) command.
 */
static int cfset_release(struct inode *inode, struct file *file)
{
	mutex_lock(&cfset_ctrset_mutex);
	/* Open followed by close/exit has no private_data */
	if (file->private_data) {
		cfset_all_stop(file->private_data);
		cfset_session_del(file->private_data);
		kfree(file->private_data);
		file->private_data = NULL;
	}
	if (!atomic_dec_return(&cfset_opencnt))
		on_each_cpu(cfset_release_cpu, NULL, 1);
	mutex_unlock(&cfset_ctrset_mutex);

	hw_perf_event_destroy(NULL);
	return 0;
}

static int cfset_open(struct inode *inode, struct file *file)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	mutex_lock(&cfset_ctrset_mutex);
	if (atomic_inc_return(&cfset_opencnt) == 1)
		cfset_session_init();
	mutex_unlock(&cfset_ctrset_mutex);

	cpumf_hw_inuse();
	file->private_data = NULL;
	/* nonseekable_open() never fails */
	return nonseekable_open(inode, file);
}

static int cfset_all_start(struct cfset_request *req)
{
	struct cfset_call_on_cpu_parm p = {
		.sets = req->ctrset,
		.cpus_ack = ATOMIC_INIT(0),
	};
	cpumask_var_t mask;
	int rc = 0;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;
	cpumask_and(mask, &req->mask, cpu_online_mask);
	on_each_cpu_mask(mask, cfset_ioctl_on, &p, 1);
	if (atomic_read(&p.cpus_ack) != cpumask_weight(mask)) {
		on_each_cpu_mask(mask, cfset_ioctl_off, &p, 1);
		rc = -EIO;
		debug_sprintf_event(cf_dbg, 4, "%s CPUs missing", __func__);
	}
	free_cpumask_var(mask);
	return rc;
}


/* Return the maximum required space for all possible CPUs in case one
 * CPU will be onlined during the START, READ, STOP cycles.
 * To find out the size of the counter sets, any one CPU will do. They
 * all have the same counter sets.
 */
static size_t cfset_needspace(unsigned int sets)
{
	struct cpu_cf_events *cpuhw = get_cpu_ptr(&cpu_cf_events);
	size_t bytes = 0;
	int i;

	for (i = CPUMF_CTR_SET_BASIC; i < CPUMF_CTR_SET_MAX; ++i) {
		if (!(sets & cpumf_ctr_ctl[i]))
			continue;
		bytes += cpum_cf_ctrset_size(i, &cpuhw->info) * sizeof(u64) +
			 sizeof(((struct s390_ctrset_setdata *)0)->set) +
			 sizeof(((struct s390_ctrset_setdata *)0)->no_cnts);
	}
	bytes = sizeof(((struct s390_ctrset_read *)0)->no_cpus) + nr_cpu_ids *
		(bytes + sizeof(((struct s390_ctrset_cpudata *)0)->cpu_nr) +
		     sizeof(((struct s390_ctrset_cpudata *)0)->no_sets));
	put_cpu_ptr(&cpu_cf_events);
	return bytes;
}

static int cfset_all_copy(unsigned long arg, cpumask_t *mask)
{
	struct s390_ctrset_read __user *ctrset_read;
	unsigned int cpu, cpus, rc;
	void __user *uptr;

	ctrset_read = (struct s390_ctrset_read __user *)arg;
	uptr = ctrset_read->data;
	for_each_cpu(cpu, mask) {
		struct cpu_cf_events *cpuhw = per_cpu_ptr(&cpu_cf_events, cpu);
		struct s390_ctrset_cpudata __user *ctrset_cpudata;

		ctrset_cpudata = uptr;
		rc  = put_user(cpu, &ctrset_cpudata->cpu_nr);
		rc |= put_user(cpuhw->sets, &ctrset_cpudata->no_sets);
		rc |= copy_to_user(ctrset_cpudata->data, cpuhw->data,
				   cpuhw->used);
		if (rc)
			return -EFAULT;
		uptr += sizeof(struct s390_ctrset_cpudata) + cpuhw->used;
		cond_resched();
	}
	cpus = cpumask_weight(mask);
	if (put_user(cpus, &ctrset_read->no_cpus))
		return -EFAULT;
	debug_sprintf_event(cf_dbg, 4, "%s copied %ld\n", __func__,
			    uptr - (void __user *)ctrset_read->data);
	return 0;
}

static size_t cfset_cpuset_read(struct s390_ctrset_setdata *p, int ctrset,
				int ctrset_size, size_t room)
{
	size_t need = 0;
	int rc = -1;

	need = sizeof(*p) + sizeof(u64) * ctrset_size;
	if (need <= room) {
		p->set = cpumf_ctr_ctl[ctrset];
		p->no_cnts = ctrset_size;
		rc = ctr_stcctm(ctrset, ctrset_size, (u64 *)p->cv);
		if (rc == 3)		/* Nothing stored */
			need = 0;
	}
	return need;
}

/* Read all counter sets. */
static void cfset_cpu_read(void *parm)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	struct cfset_call_on_cpu_parm *p = parm;
	int set, set_size;
	size_t space;

	/* No data saved yet */
	cpuhw->used = 0;
	cpuhw->sets = 0;
	memset(cpuhw->data, 0, sizeof(cpuhw->data));

	/* Scan the counter sets */
	for (set = CPUMF_CTR_SET_BASIC; set < CPUMF_CTR_SET_MAX; ++set) {
		struct s390_ctrset_setdata *sp = (void *)cpuhw->data +
						 cpuhw->used;

		if (!(p->sets & cpumf_ctr_ctl[set]))
			continue;	/* Counter set not in list */
		set_size = cpum_cf_ctrset_size(set, &cpuhw->info);
		space = sizeof(cpuhw->data) - cpuhw->used;
		space = cfset_cpuset_read(sp, set, set_size, space);
		if (space) {
			cpuhw->used += space;
			cpuhw->sets += 1;
		}
	}
	debug_sprintf_event(cf_dbg, 4, "%s sets %d used %zd\n", __func__,
			    cpuhw->sets, cpuhw->used);
}

static int cfset_all_read(unsigned long arg, struct cfset_request *req)
{
	struct cfset_call_on_cpu_parm p;
	cpumask_var_t mask;
	int rc;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	p.sets = req->ctrset;
	cpumask_and(mask, &req->mask, cpu_online_mask);
	on_each_cpu_mask(mask, cfset_cpu_read, &p, 1);
	rc = cfset_all_copy(arg, mask);
	free_cpumask_var(mask);
	return rc;
}

static long cfset_ioctl_read(unsigned long arg, struct cfset_request *req)
{
	struct s390_ctrset_read read;
	int ret = -ENODATA;

	if (req && req->ctrset) {
		if (copy_from_user(&read, (char __user *)arg, sizeof(read)))
			return -EFAULT;
		ret = cfset_all_read(arg, req);
	}
	return ret;
}

static long cfset_ioctl_stop(struct file *file)
{
	struct cfset_request *req = file->private_data;
	int ret = -ENXIO;

	if (req) {
		cfset_all_stop(req);
		cfset_session_del(req);
		kfree(req);
		file->private_data = NULL;
		ret = 0;
	}
	return ret;
}

static long cfset_ioctl_start(unsigned long arg, struct file *file)
{
	struct s390_ctrset_start __user *ustart;
	struct s390_ctrset_start start;
	struct cfset_request *preq;
	void __user *umask;
	unsigned int len;
	int ret = 0;
	size_t need;

	if (file->private_data)
		return -EBUSY;
	ustart = (struct s390_ctrset_start __user *)arg;
	if (copy_from_user(&start, ustart, sizeof(start)))
		return -EFAULT;
	if (start.version != S390_HWCTR_START_VERSION)
		return -EINVAL;
	if (start.counter_sets & ~(cpumf_ctr_ctl[CPUMF_CTR_SET_BASIC] |
				   cpumf_ctr_ctl[CPUMF_CTR_SET_USER] |
				   cpumf_ctr_ctl[CPUMF_CTR_SET_CRYPTO] |
				   cpumf_ctr_ctl[CPUMF_CTR_SET_EXT] |
				   cpumf_ctr_ctl[CPUMF_CTR_SET_MT_DIAG]))
		return -EINVAL;		/* Invalid counter set */
	if (!start.counter_sets)
		return -EINVAL;		/* No counter set at all? */

	preq = kzalloc(sizeof(*preq), GFP_KERNEL);
	if (!preq)
		return -ENOMEM;
	cpumask_clear(&preq->mask);
	len = min_t(u64, start.cpumask_len, cpumask_size());
	umask = (void __user *)start.cpumask;
	if (copy_from_user(&preq->mask, umask, len)) {
		kfree(preq);
		return -EFAULT;
	}
	if (cpumask_empty(&preq->mask)) {
		kfree(preq);
		return -EINVAL;
	}
	need = cfset_needspace(start.counter_sets);
	if (put_user(need, &ustart->data_bytes)) {
		kfree(preq);
		return -EFAULT;
	}
	preq->ctrset = start.counter_sets;
	ret = cfset_all_start(preq);
	if (!ret) {
		cfset_session_add(preq);
		file->private_data = preq;
		debug_sprintf_event(cf_dbg, 4, "%s set %#lx need %ld ret %d\n",
				    __func__, preq->ctrset, need, ret);
	} else {
		kfree(preq);
	}
	return ret;
}

/* Entry point to the /dev/hwctr device interface.
 * The ioctl system call supports three subcommands:
 * S390_HWCTR_START: Start the specified counter sets on a CPU list. The
 *    counter set keeps running until explicitly stopped. Returns the number
 *    of bytes needed to store the counter values. If another S390_HWCTR_START
 *    ioctl subcommand is called without a previous S390_HWCTR_STOP stop
 *    command on the same file descriptor, -EBUSY is returned.
 * S390_HWCTR_READ: Read the counter set values from specified CPU list given
 *    with the S390_HWCTR_START command.
 * S390_HWCTR_STOP: Stops the counter sets on the CPU list given with the
 *    previous S390_HWCTR_START subcommand.
 */
static long cfset_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;

	cpus_read_lock();
	mutex_lock(&cfset_ctrset_mutex);
	switch (cmd) {
	case S390_HWCTR_START:
		ret = cfset_ioctl_start(arg, file);
		break;
	case S390_HWCTR_STOP:
		ret = cfset_ioctl_stop(file);
		break;
	case S390_HWCTR_READ:
		ret = cfset_ioctl_read(arg, file->private_data);
		break;
	default:
		ret = -ENOTTY;
		break;
	}
	mutex_unlock(&cfset_ctrset_mutex);
	cpus_read_unlock();
	return ret;
}

static const struct file_operations cfset_fops = {
	.owner = THIS_MODULE,
	.open = cfset_open,
	.release = cfset_release,
	.unlocked_ioctl	= cfset_ioctl,
	.compat_ioctl = cfset_ioctl,
	.llseek = no_llseek
};

static struct miscdevice cfset_dev = {
	.name	= S390_HWCTR_DEVICE,
	.minor	= MISC_DYNAMIC_MINOR,
	.fops	= &cfset_fops,
};

/* Hotplug add of a CPU. Scan through all active processes and add
 * that CPU to the list of CPUs supplied with ioctl(..., START, ...).
 */
int cfset_online_cpu(unsigned int cpu)
{
	struct cfset_call_on_cpu_parm p;
	struct cfset_request *rp;

	mutex_lock(&cfset_ctrset_mutex);
	if (!list_empty(&cfset_session.head)) {
		list_for_each_entry(rp, &cfset_session.head, node) {
			p.sets = rp->ctrset;
			cfset_ioctl_on(&p);
			cpumask_set_cpu(cpu, &rp->mask);
		}
	}
	mutex_unlock(&cfset_ctrset_mutex);
	return 0;
}

/* Hotplug remove of a CPU. Scan through all active processes and clear
 * that CPU from the list of CPUs supplied with ioctl(..., START, ...).
 */
int cfset_offline_cpu(unsigned int cpu)
{
	struct cfset_call_on_cpu_parm p;
	struct cfset_request *rp;

	mutex_lock(&cfset_ctrset_mutex);
	if (!list_empty(&cfset_session.head)) {
		list_for_each_entry(rp, &cfset_session.head, node) {
			p.sets = rp->ctrset;
			cfset_ioctl_off(&p);
			cpumask_clear_cpu(cpu, &rp->mask);
		}
	}
	mutex_unlock(&cfset_ctrset_mutex);
	return 0;
}

static void cfdiag_read(struct perf_event *event)
{
	debug_sprintf_event(cf_dbg, 3, "%s event %#llx count %ld\n", __func__,
			    event->attr.config, local64_read(&event->count));
}

static int get_authctrsets(void)
{
	struct cpu_cf_events *cpuhw;
	unsigned long auth = 0;
	enum cpumf_ctr_set i;

	cpuhw = &get_cpu_var(cpu_cf_events);
	for (i = CPUMF_CTR_SET_BASIC; i < CPUMF_CTR_SET_MAX; ++i) {
		if (cpuhw->info.auth_ctl & cpumf_ctr_ctl[i])
			auth |= cpumf_ctr_ctl[i];
	}
	put_cpu_var(cpu_cf_events);
	return auth;
}

/* Setup the event. Test for authorized counter sets and only include counter
 * sets which are authorized at the time of the setup. Including unauthorized
 * counter sets result in specification exception (and panic).
 */
static int cfdiag_event_init2(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	int err = 0;

	/* Set sample_period to indicate sampling */
	event->hw.config = attr->config;
	event->hw.sample_period = attr->sample_period;
	local64_set(&event->hw.period_left, event->hw.sample_period);
	local64_set(&event->count, 0);
	event->hw.last_period = event->hw.sample_period;

	/* Add all authorized counter sets to config_base. The
	 * the hardware init function is either called per-cpu or just once
	 * for all CPUS (event->cpu == -1).  This depends on the whether
	 * counting is started for all CPUs or on a per workload base where
	 * the perf event moves from one CPU to another CPU.
	 * Checking the authorization on any CPU is fine as the hardware
	 * applies the same authorization settings to all CPUs.
	 */
	event->hw.config_base = get_authctrsets();

	/* No authorized counter sets, nothing to count/sample */
	if (!event->hw.config_base)
		err = -EINVAL;

	debug_sprintf_event(cf_dbg, 5, "%s err %d config_base %#lx\n",
			    __func__, err, event->hw.config_base);
	return err;
}

static int cfdiag_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	int err = -ENOENT;

	if (event->attr.config != PERF_EVENT_CPUM_CF_DIAG ||
	    event->attr.type != event->pmu->type)
		goto out;

	/* Raw events are used to access counters directly,
	 * hence do not permit excludes.
	 * This event is useless without PERF_SAMPLE_RAW to return counter set
	 * values as raw data.
	 */
	if (attr->exclude_kernel || attr->exclude_user || attr->exclude_hv ||
	    !(attr->sample_type & (PERF_SAMPLE_CPU | PERF_SAMPLE_RAW))) {
		err = -EOPNOTSUPP;
		goto out;
	}

	/* Initialize for using the CPU-measurement counter facility */
	cpumf_hw_inuse();
	event->destroy = hw_perf_event_destroy;

	err = cfdiag_event_init2(event);
	if (unlikely(err))
		event->destroy(event);
out:
	return err;
}

/* Create cf_diag/events/CF_DIAG event sysfs file. This counter is used
 * to collect the complete counter sets for a scheduled process. Target
 * are complete counter sets attached as raw data to the artificial event.
 * This results in complete counter sets available when a process is
 * scheduled. Contains the delta of every counter while the process was
 * running.
 */
CPUMF_EVENT_ATTR(CF_DIAG, CF_DIAG, PERF_EVENT_CPUM_CF_DIAG);

static struct attribute *cfdiag_events_attr[] = {
	CPUMF_EVENT_PTR(CF_DIAG, CF_DIAG),
	NULL,
};

PMU_FORMAT_ATTR(event, "config:0-63");

static struct attribute *cfdiag_format_attr[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group cfdiag_events_group = {
	.name = "events",
	.attrs = cfdiag_events_attr,
};
static struct attribute_group cfdiag_format_group = {
	.name = "format",
	.attrs = cfdiag_format_attr,
};
static const struct attribute_group *cfdiag_attr_groups[] = {
	&cfdiag_events_group,
	&cfdiag_format_group,
	NULL,
};

/* Performance monitoring unit for event CF_DIAG. Since this event
 * is also started and stopped via the perf_event_open() system call, use
 * the same event enable/disable call back functions. They do not
 * have a pointer to the perf_event strcture as first parameter.
 *
 * The functions XXX_add, XXX_del, XXX_start and XXX_stop are also common.
 * Reuse them and distinguish the event (always first parameter) via
 * 'config' member.
 */
static struct pmu cf_diag = {
	.task_ctx_nr  = perf_sw_context,
	.event_init   = cfdiag_event_init,
	.pmu_enable   = cpumf_pmu_enable,
	.pmu_disable  = cpumf_pmu_disable,
	.add	      = cpumf_pmu_add,
	.del	      = cpumf_pmu_del,
	.start	      = cpumf_pmu_start,
	.stop	      = cpumf_pmu_stop,
	.read	      = cfdiag_read,

	.attr_groups  = cfdiag_attr_groups
};

/* Calculate memory needed to store all counter sets together with header and
 * trailer data. This is independent of the counter set authorization which
 * can vary depending on the configuration.
 */
static size_t cfdiag_maxsize(struct cpumf_ctr_info *info)
{
	size_t max_size = sizeof(struct cf_trailer_entry);
	enum cpumf_ctr_set i;

	for (i = CPUMF_CTR_SET_BASIC; i < CPUMF_CTR_SET_MAX; ++i) {
		size_t size = cpum_cf_ctrset_size(i, info);

		if (size)
			max_size += size * sizeof(u64) +
				    sizeof(struct cf_ctrset_entry);
	}
	return max_size;
}

/* Get the CPU speed, try sampling facility first and CPU attributes second. */
static void cfdiag_get_cpu_speed(void)
{
	unsigned long mhz;

	if (cpum_sf_avail()) {			/* Sampling facility first */
		struct hws_qsi_info_block si;

		memset(&si, 0, sizeof(si));
		if (!qsi(&si)) {
			cfdiag_cpu_speed = si.cpu_speed;
			return;
		}
	}

	/* Fallback: CPU speed extract static part. Used in case
	 * CPU Measurement Sampling Facility is turned off.
	 */
	mhz = __ecag(ECAG_CPU_ATTRIBUTE, 0);
	if (mhz != -1UL)
		cfdiag_cpu_speed = mhz & 0xffffffff;
}

static int cfset_init(void)
{
	struct cpumf_ctr_info info;
	size_t need;
	int rc;

	if (qctri(&info))
		return -ENODEV;

	cfdiag_get_cpu_speed();
	/* Make sure the counter set data fits into predefined buffer. */
	need = cfdiag_maxsize(&info);
	if (need > sizeof(((struct cpu_cf_events *)0)->start)) {
		pr_err("Insufficient memory for PMU(cpum_cf_diag) need=%zu\n",
		       need);
		return -ENOMEM;
	}

	rc = misc_register(&cfset_dev);
	if (rc) {
		pr_err("Registration of /dev/%s failed rc=%i\n",
		       cfset_dev.name, rc);
		goto out;
	}

	rc = perf_pmu_register(&cf_diag, "cpum_cf_diag", -1);
	if (rc) {
		misc_deregister(&cfset_dev);
		pr_err("Registration of PMU(cpum_cf_diag) failed with rc=%i\n",
		       rc);
	}
out:
	return rc;
}

device_initcall(cpumf_pmu_init);
