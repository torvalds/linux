// SPDX-License-Identifier: GPL-2.0
/*
 * Performance event support for s390x - CPU-measurement Counter Sets
 *
 *  Copyright IBM Corp. 2019
 *  Author(s): Hendrik Brueckner <brueckner@linux.ibm.com>
 *	       Thomas Richer <tmricht@linux.ibm.com>
 */
#define KMSG_COMPONENT	"cpum_cf_diag"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/processor.h>

#include <asm/ctl_reg.h>
#include <asm/irq.h>
#include <asm/cpu_mcf.h>
#include <asm/timex.h>
#include <asm/debug.h>

#define	CF_DIAG_CTRSET_DEF		0xfeef	/* Counter set header mark */

static unsigned int cf_diag_cpu_speed;
static debug_info_t *cf_diag_dbg;

struct cf_diag_csd {		/* Counter set data per CPU */
	size_t used;			/* Bytes used in data/start */
	unsigned char start[PAGE_SIZE];	/* Counter set at event start */
	unsigned char data[PAGE_SIZE];	/* Counter set at event delete */
};
static DEFINE_PER_CPU(struct cf_diag_csd, cf_diag_csd);

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
static void cf_diag_trailer(struct cf_trailer_entry *te)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	struct cpuid cpuid;

	te->cfvn = cpuhw->info.cfvn;		/* Counter version numbers */
	te->csvn = cpuhw->info.csvn;

	get_cpu_id(&cpuid);			/* Machine type */
	te->mach_type = cpuid.machine;
	te->cpu_speed = cf_diag_cpu_speed;
	if (te->cpu_speed)
		te->speed = 1;
	te->clock_base = 1;			/* Save clock base */
	te->tod_base = tod_clock_base.tod;
	te->timestamp = get_tod_clock_fast();
}

/*
 * Change the CPUMF state to active.
 * Enable and activate the CPU-counter sets according
 * to the per-cpu control state.
 */
static void cf_diag_enable(struct pmu *pmu)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	int err;

	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s pmu %p cpu %d flags %#x state %#llx\n",
			    __func__, pmu, smp_processor_id(), cpuhw->flags,
			    cpuhw->state);
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
static void cf_diag_disable(struct pmu *pmu)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	u64 inactive;
	int err;

	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s pmu %p cpu %d flags %#x state %#llx\n",
			    __func__, pmu, smp_processor_id(), cpuhw->flags,
			    cpuhw->state);
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
static atomic_t cf_diag_events = ATOMIC_INIT(0);

/* Release the PMU if event is the last perf event */
static void cf_diag_perf_event_destroy(struct perf_event *event)
{
	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s event %p cpu %d cf_diag_events %d\n",
			    __func__, event, event->cpu,
			    atomic_read(&cf_diag_events));
	if (atomic_dec_return(&cf_diag_events) == 0)
		__kernel_cpumcf_end();
}

/* Setup the event. Test for authorized counter sets and only include counter
 * sets which are authorized at the time of the setup. Including unauthorized
 * counter sets result in specification exception (and panic).
 */
static int __hw_perf_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct cpu_cf_events *cpuhw;
	enum cpumf_ctr_set i;
	int err = 0;

	debug_sprintf_event(cf_diag_dbg, 5, "%s event %p cpu %d\n", __func__,
			    event, event->cpu);

	event->hw.config = attr->config;
	event->hw.config_base = 0;

	/* Add all authorized counter sets to config_base. The
	 * the hardware init function is either called per-cpu or just once
	 * for all CPUS (event->cpu == -1).  This depends on the whether
	 * counting is started for all CPUs or on a per workload base where
	 * the perf event moves from one CPU to another CPU.
	 * Checking the authorization on any CPU is fine as the hardware
	 * applies the same authorization settings to all CPUs.
	 */
	cpuhw = &get_cpu_var(cpu_cf_events);
	for (i = CPUMF_CTR_SET_BASIC; i < CPUMF_CTR_SET_MAX; ++i)
		if (cpuhw->info.auth_ctl & cpumf_ctr_ctl[i])
			event->hw.config_base |= cpumf_ctr_ctl[i];
	put_cpu_var(cpu_cf_events);

	/* No authorized counter sets, nothing to count/sample */
	if (!event->hw.config_base) {
		err = -EINVAL;
		goto out;
	}

	/* Set sample_period to indicate sampling */
	event->hw.sample_period = attr->sample_period;
	local64_set(&event->hw.period_left, event->hw.sample_period);
	event->hw.last_period  = event->hw.sample_period;
out:
	debug_sprintf_event(cf_diag_dbg, 5, "%s err %d config_base %#lx\n",
			    __func__, err, event->hw.config_base);
	return err;
}

static int cf_diag_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	int err = -ENOENT;

	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s event %p cpu %d config %#llx type:%u "
			    "sample_type %#llx cf_diag_events %d\n", __func__,
			    event, event->cpu, attr->config, event->pmu->type,
			    attr->sample_type, atomic_read(&cf_diag_events));

	if (event->attr.config != PERF_EVENT_CPUM_CF_DIAG ||
	    event->attr.type != event->pmu->type)
		goto out;

	/* Raw events are used to access counters directly,
	 * hence do not permit excludes.
	 * This event is usesless without PERF_SAMPLE_RAW to return counter set
	 * values as raw data.
	 */
	if (attr->exclude_kernel || attr->exclude_user || attr->exclude_hv ||
	    !(attr->sample_type & (PERF_SAMPLE_CPU | PERF_SAMPLE_RAW))) {
		err = -EOPNOTSUPP;
		goto out;
	}

	/* Initialize for using the CPU-measurement counter facility */
	if (atomic_inc_return(&cf_diag_events) == 1) {
		if (__kernel_cpumcf_begin()) {
			atomic_dec(&cf_diag_events);
			err = -EBUSY;
			goto out;
		}
	}
	event->destroy = cf_diag_perf_event_destroy;

	err = __hw_perf_event_init(event);
	if (unlikely(err))
		event->destroy(event);
out:
	debug_sprintf_event(cf_diag_dbg, 5, "%s err %d\n", __func__, err);
	return err;
}

static void cf_diag_read(struct perf_event *event)
{
	debug_sprintf_event(cf_diag_dbg, 5, "%s event %p\n", __func__, event);
}

/* Return the maximum possible counter set size (in number of 8 byte counters)
 * depending on type and model number.
 */
static size_t cf_diag_ctrset_size(enum cpumf_ctr_set ctrset,
				 struct cpumf_ctr_info *info)
{
	size_t ctrset_size = 0;

	switch (ctrset) {
	case CPUMF_CTR_SET_BASIC:
		if (info->cfvn >= 1)
			ctrset_size = 6;
		break;
	case CPUMF_CTR_SET_USER:
		if (info->cfvn == 1)
			ctrset_size = 6;
		else if (info->cfvn >= 3)
			ctrset_size = 2;
		break;
	case CPUMF_CTR_SET_CRYPTO:
		if (info->csvn >= 1 && info->csvn <= 5)
			ctrset_size = 16;
		else if (info->csvn == 6)
			ctrset_size = 20;
		break;
	case CPUMF_CTR_SET_EXT:
		if (info->csvn == 1)
			ctrset_size = 32;
		else if (info->csvn == 2)
			ctrset_size = 48;
		else if (info->csvn >= 3 && info->csvn <= 5)
			ctrset_size = 128;
		else if (info->csvn == 6)
			ctrset_size = 160;
		break;
	case CPUMF_CTR_SET_MT_DIAG:
		if (info->csvn > 3)
			ctrset_size = 48;
		break;
	case CPUMF_CTR_SET_MAX:
		break;
	}

	return ctrset_size;
}

/* Calculate memory needed to store all counter sets together with header and
 * trailer data. This is independend of the counter set authorization which
 * can vary depending on the configuration.
 */
static size_t cf_diag_ctrset_maxsize(struct cpumf_ctr_info *info)
{
	size_t max_size = sizeof(struct cf_trailer_entry);
	enum cpumf_ctr_set i;

	for (i = CPUMF_CTR_SET_BASIC; i < CPUMF_CTR_SET_MAX; ++i) {
		size_t size = cf_diag_ctrset_size(i, info);

		if (size)
			max_size += size * sizeof(u64) +
				    sizeof(struct cf_ctrset_entry);
	}
	debug_sprintf_event(cf_diag_dbg, 5, "%s max_size %zu\n", __func__,
			    max_size);

	return max_size;
}

/* Read a counter set. The counter set number determines which counter set and
 * the CPUM-CF first and second version number determine the number of
 * available counters in this counter set.
 * Each counter set starts with header containing the counter set number and
 * the number of 8 byte counters.
 *
 * The functions returns the number of bytes occupied by this counter set
 * including the header.
 * If there is no counter in the counter set, this counter set is useless and
 * zero is returned on this case.
 */
static size_t cf_diag_getctrset(struct cf_ctrset_entry *ctrdata, int ctrset,
				size_t room)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	size_t ctrset_size, need = 0;
	int rc = 3;				/* Assume write failure */

	ctrdata->def = CF_DIAG_CTRSET_DEF;
	ctrdata->set = ctrset;
	ctrdata->res1 = 0;
	ctrset_size = cf_diag_ctrset_size(ctrset, &cpuhw->info);

	if (ctrset_size) {			/* Save data */
		need = ctrset_size * sizeof(u64) + sizeof(*ctrdata);
		if (need <= room)
			rc = ctr_stcctm(ctrset, ctrset_size,
					(u64 *)(ctrdata + 1));
		if (rc != 3)
			ctrdata->ctr = ctrset_size;
		else
			need = 0;
	}

	debug_sprintf_event(cf_diag_dbg, 6,
			    "%s ctrset %d ctrset_size %zu cfvn %d csvn %d"
			    " need %zd rc %d\n",
			    __func__, ctrset, ctrset_size, cpuhw->info.cfvn,
			    cpuhw->info.csvn, need, rc);
	return need;
}

/* Read out all counter sets and save them in the provided data buffer.
 * The last 64 byte host an artificial trailer entry.
 */
static size_t cf_diag_getctr(void *data, size_t sz, unsigned long auth)
{
	struct cf_trailer_entry *trailer;
	size_t offset = 0, done;
	int i;

	memset(data, 0, sz);
	sz -= sizeof(*trailer);			/* Always room for trailer */
	for (i = CPUMF_CTR_SET_BASIC; i < CPUMF_CTR_SET_MAX; ++i) {
		struct cf_ctrset_entry *ctrdata = data + offset;

		if (!(auth & cpumf_ctr_ctl[i]))
			continue;	/* Counter set not authorized */

		done = cf_diag_getctrset(ctrdata, i, sz - offset);
		offset += done;
		debug_sprintf_event(cf_diag_dbg, 6,
				    "%s ctrset %d offset %zu done %zu\n",
				     __func__, i, offset, done);
	}
	trailer = data + offset;
	cf_diag_trailer(trailer);
	return offset + sizeof(*trailer);
}

/* Calculate the difference for each counter in a counter set. */
static void cf_diag_diffctrset(u64 *pstart, u64 *pstop, int counters)
{
	for (; --counters >= 0; ++pstart, ++pstop)
		if (*pstop >= *pstart)
			*pstop -= *pstart;
		else
			*pstop = *pstart - *pstop;
}

/* Scan the counter sets and calculate the difference of each counter
 * in each set. The result is the increment of each counter during the
 * period the counter set has been activated.
 *
 * Return true on success.
 */
static int cf_diag_diffctr(struct cf_diag_csd *csd, unsigned long auth)
{
	struct cf_trailer_entry *trailer_start, *trailer_stop;
	struct cf_ctrset_entry *ctrstart, *ctrstop;
	size_t offset = 0;

	auth &= (1 << CPUMF_LCCTL_ENABLE_SHIFT) - 1;
	do {
		ctrstart = (struct cf_ctrset_entry *)(csd->start + offset);
		ctrstop = (struct cf_ctrset_entry *)(csd->data + offset);

		if (memcmp(ctrstop, ctrstart, sizeof(*ctrstop))) {
			pr_err("cpum_cf_diag counter set compare error "
				"in set %i\n", ctrstart->set);
			return 0;
		}
		auth &= ~cpumf_ctr_ctl[ctrstart->set];
		if (ctrstart->def == CF_DIAG_CTRSET_DEF) {
			cf_diag_diffctrset((u64 *)(ctrstart + 1),
					  (u64 *)(ctrstop + 1), ctrstart->ctr);
			offset += ctrstart->ctr * sizeof(u64) +
				  sizeof(*ctrstart);
		}
		debug_sprintf_event(cf_diag_dbg, 6,
				    "%s set %d ctr %d offset %zu auth %lx\n",
				    __func__, ctrstart->set, ctrstart->ctr,
				    offset, auth);
	} while (ctrstart->def && auth);

	/* Save time_stamp from start of event in stop's trailer */
	trailer_start = (struct cf_trailer_entry *)(csd->start + offset);
	trailer_stop = (struct cf_trailer_entry *)(csd->data + offset);
	trailer_stop->progusage[0] = trailer_start->timestamp;

	return 1;
}

/* Create perf event sample with the counter sets as raw data.	The sample
 * is then pushed to the event subsystem and the function checks for
 * possible event overflows. If an event overflow occurs, the PMU is
 * stopped.
 *
 * Return non-zero if an event overflow occurred.
 */
static int cf_diag_push_sample(struct perf_event *event,
			       struct cf_diag_csd *csd)
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
		raw.frag.size = csd->used;
		raw.frag.data = csd->data;
		raw.size = csd->used;
		data.raw = &raw;
	}

	overflow = perf_event_overflow(event, &data, &regs);
	debug_sprintf_event(cf_diag_dbg, 6,
			    "%s event %p cpu %d sample_type %#llx raw %d "
			    "ov %d\n", __func__, event, event->cpu,
			    event->attr.sample_type, raw.size, overflow);
	if (overflow)
		event->pmu->stop(event, 0);

	perf_event_update_userpage(event);
	return overflow;
}

static void cf_diag_start(struct perf_event *event, int flags)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	struct cf_diag_csd *csd = this_cpu_ptr(&cf_diag_csd);
	struct hw_perf_event *hwc = &event->hw;

	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s event %p cpu %d flags %#x hwc-state %#x\n",
			    __func__, event, event->cpu, flags, hwc->state);
	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	/* (Re-)enable and activate all counter sets */
	lcctl(0);		/* Reset counter sets */
	hwc->state = 0;
	ctr_set_multiple_enable(&cpuhw->state, hwc->config_base);
	lcctl(cpuhw->state);	/* Enable counter sets */
	csd->used = cf_diag_getctr(csd->start, sizeof(csd->start),
				   event->hw.config_base);
	ctr_set_multiple_start(&cpuhw->state, hwc->config_base);
	/* Function cf_diag_enable() starts the counter sets. */
}

static void cf_diag_stop(struct perf_event *event, int flags)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	struct cf_diag_csd *csd = this_cpu_ptr(&cf_diag_csd);
	struct hw_perf_event *hwc = &event->hw;

	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s event %p cpu %d flags %#x hwc-state %#x\n",
			    __func__, event, event->cpu, flags, hwc->state);

	/* Deactivate all counter sets */
	ctr_set_multiple_stop(&cpuhw->state, hwc->config_base);
	local64_inc(&event->count);
	csd->used = cf_diag_getctr(csd->data, sizeof(csd->data),
				   event->hw.config_base);
	if (cf_diag_diffctr(csd, event->hw.config_base))
		cf_diag_push_sample(event, csd);
	hwc->state |= PERF_HES_STOPPED;
}

static int cf_diag_add(struct perf_event *event, int flags)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	int err = 0;

	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s event %p cpu %d flags %#x cpuhw %p\n",
			    __func__, event, event->cpu, flags, cpuhw);

	if (cpuhw->flags & PMU_F_IN_USE) {
		err = -EAGAIN;
		goto out;
	}

	event->hw.state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	cpuhw->flags |= PMU_F_IN_USE;
	if (flags & PERF_EF_START)
		cf_diag_start(event, PERF_EF_RELOAD);
out:
	debug_sprintf_event(cf_diag_dbg, 5, "%s err %d\n", __func__, err);
	return err;
}

static void cf_diag_del(struct perf_event *event, int flags)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);

	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s event %p cpu %d flags %#x\n",
			   __func__, event, event->cpu, flags);

	cf_diag_stop(event, PERF_EF_UPDATE);
	ctr_set_multiple_stop(&cpuhw->state, event->hw.config_base);
	ctr_set_multiple_disable(&cpuhw->state, event->hw.config_base);
	cpuhw->flags &= ~PMU_F_IN_USE;
}

CPUMF_EVENT_ATTR(CF_DIAG, CF_DIAG, PERF_EVENT_CPUM_CF_DIAG);

static struct attribute *cf_diag_events_attr[] = {
	CPUMF_EVENT_PTR(CF_DIAG, CF_DIAG),
	NULL,
};

PMU_FORMAT_ATTR(event, "config:0-63");

static struct attribute *cf_diag_format_attr[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group cf_diag_events_group = {
	.name = "events",
	.attrs = cf_diag_events_attr,
};
static struct attribute_group cf_diag_format_group = {
	.name = "format",
	.attrs = cf_diag_format_attr,
};
static const struct attribute_group *cf_diag_attr_groups[] = {
	&cf_diag_events_group,
	&cf_diag_format_group,
	NULL,
};

/* Performance monitoring unit for s390x */
static struct pmu cf_diag = {
	.task_ctx_nr  = perf_sw_context,
	.pmu_enable   = cf_diag_enable,
	.pmu_disable  = cf_diag_disable,
	.event_init   = cf_diag_event_init,
	.add	      = cf_diag_add,
	.del	      = cf_diag_del,
	.start	      = cf_diag_start,
	.stop	      = cf_diag_stop,
	.read	      = cf_diag_read,

	.attr_groups  = cf_diag_attr_groups
};

/* Get the CPU speed, try sampling facility first and CPU attributes second. */
static void cf_diag_get_cpu_speed(void)
{
	if (cpum_sf_avail()) {			/* Sampling facility first */
		struct hws_qsi_info_block si;

		memset(&si, 0, sizeof(si));
		if (!qsi(&si)) {
			cf_diag_cpu_speed = si.cpu_speed;
			return;
		}
	}

	if (test_facility(34)) {		/* CPU speed extract static part */
		unsigned long mhz = __ecag(ECAG_CPU_ATTRIBUTE, 0);

		if (mhz != -1UL)
			cf_diag_cpu_speed = mhz & 0xffffffff;
	}
}

/* Initialize the counter set PMU to generate complete counter set data as
 * event raw data. This relies on the CPU Measurement Counter Facility device
 * already being loaded and initialized.
 */
static int __init cf_diag_init(void)
{
	struct cpumf_ctr_info info;
	size_t need;
	int rc;

	if (!kernel_cpumcf_avail() || !stccm_avail() || qctri(&info))
		return -ENODEV;
	cf_diag_get_cpu_speed();

	/* Make sure the counter set data fits into predefined buffer. */
	need = cf_diag_ctrset_maxsize(&info);
	if (need > sizeof(((struct cf_diag_csd *)0)->start)) {
		pr_err("Insufficient memory for PMU(cpum_cf_diag) need=%zu\n",
		       need);
		return -ENOMEM;
	}

	/* Setup s390dbf facility */
	cf_diag_dbg = debug_register(KMSG_COMPONENT, 2, 1, 128);
	if (!cf_diag_dbg) {
		pr_err("Registration of s390dbf(cpum_cf_diag) failed\n");
		return -ENOMEM;
	}
	debug_register_view(cf_diag_dbg, &debug_sprintf_view);

	rc = perf_pmu_register(&cf_diag, "cpum_cf_diag", -1);
	if (rc) {
		debug_unregister_view(cf_diag_dbg, &debug_sprintf_view);
		debug_unregister(cf_diag_dbg);
		pr_err("Registration of PMU(cpum_cf_diag) failed with rc=%i\n",
		       rc);
	}
	return rc;
}
arch_initcall(cf_diag_init);
