// SPDX-License-Identifier: GPL-2.0
/*
 * Performance event support for s390x - CPU-measurement Counter Sets
 *
 *  Copyright IBM Corp. 2019, 2021
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
#include <linux/miscdevice.h>
#include <linux/mutex.h>

#include <asm/ctl_reg.h>
#include <asm/irq.h>
#include <asm/cpu_mcf.h>
#include <asm/timex.h>
#include <asm/debug.h>

#include <asm/hwctrset.h>

#define	CF_DIAG_CTRSET_DEF		0xfeef	/* Counter set header mark */
						/* interval in seconds */
static unsigned int cf_diag_cpu_speed;
static debug_info_t *cf_diag_dbg;

struct cf_diag_csd {			/* Counter set data per CPU */
	size_t used;			/* Bytes used in data/start */
	unsigned char start[PAGE_SIZE];	/* Counter set at event start */
	unsigned char data[PAGE_SIZE];	/* Counter set at event delete */
	unsigned int sets;		/* # Counter set saved in data */
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
/* Used to avoid races in calling reserve/release_cpumf_hardware */
static DEFINE_MUTEX(cf_diag_reserve_mutex);

/* Release the PMU if event is the last perf event */
static void cf_diag_perf_event_destroy(struct perf_event *event)
{
	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s event %p cpu %d cf_diag_events %d\n",
			    __func__, event, smp_processor_id(),
			    atomic_read(&cf_diag_events));
	if (atomic_dec_return(&cf_diag_events) == 0)
		__kernel_cpumcf_end();
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
static int __hw_perf_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	int err = 0;

	debug_sprintf_event(cf_diag_dbg, 5, "%s event %p cpu %d\n", __func__,
			    event, event->cpu);

	event->hw.config = attr->config;

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

/* Return 0 if the CPU-measurement counter facility is currently free
 * and an error otherwise.
 */
static int cf_diag_perf_event_inuse(void)
{
	int err = 0;

	if (!atomic_inc_not_zero(&cf_diag_events)) {
		mutex_lock(&cf_diag_reserve_mutex);
		if (atomic_read(&cf_diag_events) == 0 &&
		    __kernel_cpumcf_begin())
			err = -EBUSY;
		else
			err = atomic_inc_return(&cf_diag_events);
		mutex_unlock(&cf_diag_reserve_mutex);
	}
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
	err = cf_diag_perf_event_inuse();
	if (err < 0)
		goto out;
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

/* Calculate memory needed to store all counter sets together with header and
 * trailer data. This is independend of the counter set authorization which
 * can vary depending on the configuration.
 */
static size_t cf_diag_ctrset_maxsize(struct cpumf_ctr_info *info)
{
	size_t max_size = sizeof(struct cf_trailer_entry);
	enum cpumf_ctr_set i;

	for (i = CPUMF_CTR_SET_BASIC; i < CPUMF_CTR_SET_MAX; ++i) {
		size_t size = cpum_cf_ctrset_size(i, info);

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
	ctrset_size = cpum_cf_ctrset_size(ctrset, &cpuhw->info);

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

/* Default counter set events and format attribute groups */

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

/* Code to create device and file I/O operations */
static atomic_t ctrset_opencnt = ATOMIC_INIT(0);	/* Excl. access */

static int cf_diag_open(struct inode *inode, struct file *file)
{
	int err = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (atomic_xchg(&ctrset_opencnt, 1))
		return -EBUSY;

	/* Avoid concurrent access with perf_event_open() system call */
	mutex_lock(&cf_diag_reserve_mutex);
	if (atomic_read(&cf_diag_events) || __kernel_cpumcf_begin())
		err = -EBUSY;
	mutex_unlock(&cf_diag_reserve_mutex);
	if (err) {
		atomic_set(&ctrset_opencnt, 0);
		return err;
	}
	file->private_data = NULL;
	debug_sprintf_event(cf_diag_dbg, 2, "%s\n", __func__);
	/* nonseekable_open() never fails */
	return nonseekable_open(inode, file);
}

/* Variables for ioctl() interface support */
static DEFINE_MUTEX(cf_diag_ctrset_mutex);
static struct cf_diag_ctrset {
	unsigned long ctrset;		/* Bit mask of counter set to read */
	cpumask_t mask;			/* CPU mask to read from */
} cf_diag_ctrset;

static void cf_diag_ctrset_clear(void)
{
	cpumask_clear(&cf_diag_ctrset.mask);
	cf_diag_ctrset.ctrset = 0;
}

static void cf_diag_release_cpu(void *p)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);

	debug_sprintf_event(cf_diag_dbg, 3, "%s cpu %d\n", __func__,
			    smp_processor_id());
	lcctl(0);		/* Reset counter sets */
	cpuhw->state = 0;	/* Save state in CPU hardware state */
}

/* Release function is also called when application gets terminated without
 * doing a proper ioctl(..., S390_HWCTR_STOP, ...) command.
 * Since only one application is allowed to open the device, simple stop all
 * CPU counter sets.
 */
static int cf_diag_release(struct inode *inode, struct file *file)
{
	on_each_cpu(cf_diag_release_cpu, NULL, 1);
	cf_diag_ctrset_clear();
	atomic_set(&ctrset_opencnt, 0);
	__kernel_cpumcf_end();
	debug_sprintf_event(cf_diag_dbg, 2, "%s\n", __func__);
	return 0;
}

struct cf_diag_call_on_cpu_parm {	/* Parm struct for smp_call_on_cpu */
	unsigned int sets;		/* Counter set bit mask */
	atomic_t cpus_ack;		/* # CPUs successfully executed func */
};

static int cf_diag_all_copy(unsigned long arg, cpumask_t *mask)
{
	struct s390_ctrset_read __user *ctrset_read;
	unsigned int cpu, cpus, rc;
	void __user *uptr;

	ctrset_read = (struct s390_ctrset_read __user *)arg;
	uptr = ctrset_read->data;
	for_each_cpu(cpu, mask) {
		struct cf_diag_csd *csd = per_cpu_ptr(&cf_diag_csd, cpu);
		struct s390_ctrset_cpudata __user *ctrset_cpudata;

		ctrset_cpudata = uptr;
		debug_sprintf_event(cf_diag_dbg, 5, "%s cpu %d used %zd\n",
				    __func__, cpu, csd->used);
		rc  = put_user(cpu, &ctrset_cpudata->cpu_nr);
		rc |= put_user(csd->sets, &ctrset_cpudata->no_sets);
		rc |= copy_to_user(ctrset_cpudata->data, csd->data, csd->used);
		if (rc)
			return -EFAULT;
		uptr += sizeof(struct s390_ctrset_cpudata) + csd->used;
		cond_resched();
	}
	cpus = cpumask_weight(mask);
	if (put_user(cpus, &ctrset_read->no_cpus))
		return -EFAULT;
	debug_sprintf_event(cf_diag_dbg, 5, "%s copied %ld\n",
			    __func__, uptr - (void __user *)ctrset_read->data);
	return 0;
}

static size_t cf_diag_cpuset_read(struct s390_ctrset_setdata *p, int ctrset,
				  int ctrset_size, size_t room)
{
	size_t need = 0;
	int rc = -1;

	need = sizeof(*p) + sizeof(u64) * ctrset_size;
	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s room %zd need %zd set %#x set_size %d\n",
			    __func__, room, need, ctrset, ctrset_size);
	if (need <= room) {
		p->set = cpumf_ctr_ctl[ctrset];
		p->no_cnts = ctrset_size;
		rc = ctr_stcctm(ctrset, ctrset_size, (u64 *)p->cv);
		if (rc == 3)		/* Nothing stored */
			need = 0;
	}
	debug_sprintf_event(cf_diag_dbg, 5, "%s need %zd rc %d\n", __func__,
			    need, rc);
	return need;
}

/* Read all counter sets. Since the perf_event_open() system call with
 * event cpum_cf_diag/.../ is blocked when this interface is active, reuse
 * the perf_event_open() data buffer to store the counter sets.
 */
static void cf_diag_cpu_read(void *parm)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	struct cf_diag_csd *csd = this_cpu_ptr(&cf_diag_csd);
	struct cf_diag_call_on_cpu_parm *p = parm;
	int set, set_size;
	size_t space;

	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s new %#x flags %#x state %#llx\n",
			    __func__, p->sets, cpuhw->flags,
			    cpuhw->state);
	/* No data saved yet */
	csd->used = 0;
	csd->sets = 0;
	memset(csd->data, 0, sizeof(csd->data));

	/* Scan the counter sets */
	for (set = CPUMF_CTR_SET_BASIC; set < CPUMF_CTR_SET_MAX; ++set) {
		struct s390_ctrset_setdata *sp = (void *)csd->data + csd->used;

		if (!(p->sets & cpumf_ctr_ctl[set]))
			continue;	/* Counter set not in list */
		set_size = cpum_cf_ctrset_size(set, &cpuhw->info);
		space = sizeof(csd->data) - csd->used;
		space = cf_diag_cpuset_read(sp, set, set_size, space);
		if (space) {
			csd->used += space;
			csd->sets += 1;
		}
		debug_sprintf_event(cf_diag_dbg, 5, "%s sp %px space %zd\n",
				    __func__, sp, space);
	}
	debug_sprintf_event(cf_diag_dbg, 5, "%s sets %d used %zd\n", __func__,
			    csd->sets, csd->used);
}

static int cf_diag_all_read(unsigned long arg)
{
	struct cf_diag_call_on_cpu_parm p;
	cpumask_var_t mask;
	int rc;

	debug_sprintf_event(cf_diag_dbg, 5, "%s\n", __func__);
	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	p.sets = cf_diag_ctrset.ctrset;
	cpumask_and(mask, &cf_diag_ctrset.mask, cpu_online_mask);
	on_each_cpu_mask(mask, cf_diag_cpu_read, &p, 1);
	rc = cf_diag_all_copy(arg, mask);
	free_cpumask_var(mask);
	debug_sprintf_event(cf_diag_dbg, 5, "%s rc %d\n", __func__, rc);
	return rc;
}

/* Stop all counter sets via ioctl interface */
static void cf_diag_ioctl_off(void *parm)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	struct cf_diag_call_on_cpu_parm *p = parm;
	int rc;

	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s new %#x flags %#x state %#llx\n",
			    __func__, p->sets, cpuhw->flags,
			    cpuhw->state);

	ctr_set_multiple_disable(&cpuhw->state, p->sets);
	ctr_set_multiple_stop(&cpuhw->state, p->sets);
	rc = lcctl(cpuhw->state);		/* Stop counter sets */
	if (!cpuhw->state)
		cpuhw->flags &= ~PMU_F_IN_USE;
	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s rc %d flags %#x state %#llx\n", __func__,
			     rc, cpuhw->flags, cpuhw->state);
}

/* Start counter sets on particular CPU */
static void cf_diag_ioctl_on(void *parm)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	struct cf_diag_call_on_cpu_parm *p = parm;
	int rc;

	debug_sprintf_event(cf_diag_dbg, 5,
			    "%s new %#x flags %#x state %#llx\n",
			    __func__, p->sets, cpuhw->flags,
			    cpuhw->state);

	if (!(cpuhw->flags & PMU_F_IN_USE))
		cpuhw->state = 0;
	cpuhw->flags |= PMU_F_IN_USE;
	rc = lcctl(cpuhw->state);		/* Reset unused counter sets */
	ctr_set_multiple_enable(&cpuhw->state, p->sets);
	ctr_set_multiple_start(&cpuhw->state, p->sets);
	rc |= lcctl(cpuhw->state);		/* Start counter sets */
	if (!rc)
		atomic_inc(&p->cpus_ack);
	debug_sprintf_event(cf_diag_dbg, 5, "%s rc %d state %#llx\n",
			    __func__, rc, cpuhw->state);
}

static int cf_diag_all_stop(void)
{
	struct cf_diag_call_on_cpu_parm p = {
		.sets = cf_diag_ctrset.ctrset,
	};
	cpumask_var_t mask;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;
	cpumask_and(mask, &cf_diag_ctrset.mask, cpu_online_mask);
	on_each_cpu_mask(mask, cf_diag_ioctl_off, &p, 1);
	free_cpumask_var(mask);
	return 0;
}

static int cf_diag_all_start(void)
{
	struct cf_diag_call_on_cpu_parm p = {
		.sets = cf_diag_ctrset.ctrset,
		.cpus_ack = ATOMIC_INIT(0),
	};
	cpumask_var_t mask;
	int rc = 0;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;
	cpumask_and(mask, &cf_diag_ctrset.mask, cpu_online_mask);
	on_each_cpu_mask(mask, cf_diag_ioctl_on, &p, 1);
	if (atomic_read(&p.cpus_ack) != cpumask_weight(mask)) {
		on_each_cpu_mask(mask, cf_diag_ioctl_off, &p, 1);
		rc = -EIO;
	}
	free_cpumask_var(mask);
	return rc;
}

/* Return the maximum required space for all possible CPUs in case one
 * CPU will be onlined during the START, READ, STOP cycles.
 * To find out the size of the counter sets, any one CPU will do. They
 * all have the same counter sets.
 */
static size_t cf_diag_needspace(unsigned int sets)
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
	debug_sprintf_event(cf_diag_dbg, 5, "%s bytes %ld\n", __func__,
			    bytes);
	put_cpu_ptr(&cpu_cf_events);
	return bytes;
}

static long cf_diag_ioctl_read(unsigned long arg)
{
	struct s390_ctrset_read read;
	int ret = 0;

	debug_sprintf_event(cf_diag_dbg, 5, "%s\n", __func__);
	if (copy_from_user(&read, (char __user *)arg, sizeof(read)))
		return -EFAULT;
	ret = cf_diag_all_read(arg);
	debug_sprintf_event(cf_diag_dbg, 5, "%s ret %d\n", __func__, ret);
	return ret;
}

static long cf_diag_ioctl_stop(void)
{
	int ret;

	debug_sprintf_event(cf_diag_dbg, 5, "%s\n", __func__);
	ret = cf_diag_all_stop();
	cf_diag_ctrset_clear();
	debug_sprintf_event(cf_diag_dbg, 5, "%s ret %d\n", __func__, ret);
	return ret;
}

static long cf_diag_ioctl_start(unsigned long arg)
{
	struct s390_ctrset_start __user *ustart;
	struct s390_ctrset_start start;
	void __user *umask;
	unsigned int len;
	int ret = 0;
	size_t need;

	if (cf_diag_ctrset.ctrset)
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
	cpumask_clear(&cf_diag_ctrset.mask);
	len = min_t(u64, start.cpumask_len, cpumask_size());
	umask = (void __user *)start.cpumask;
	if (copy_from_user(&cf_diag_ctrset.mask, umask, len))
		return -EFAULT;
	if (cpumask_empty(&cf_diag_ctrset.mask))
		return -EINVAL;
	need = cf_diag_needspace(start.counter_sets);
	if (put_user(need, &ustart->data_bytes))
		ret = -EFAULT;
	if (ret)
		goto out;
	cf_diag_ctrset.ctrset = start.counter_sets;
	ret = cf_diag_all_start();
out:
	if (ret)
		cf_diag_ctrset_clear();
	debug_sprintf_event(cf_diag_dbg, 2, "%s sets %#lx need %ld ret %d\n",
			    __func__, cf_diag_ctrset.ctrset, need, ret);
	return ret;
}

static long cf_diag_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	int ret;

	debug_sprintf_event(cf_diag_dbg, 2, "%s cmd %#x arg %lx\n", __func__,
			    cmd, arg);
	get_online_cpus();
	mutex_lock(&cf_diag_ctrset_mutex);
	switch (cmd) {
	case S390_HWCTR_START:
		ret = cf_diag_ioctl_start(arg);
		break;
	case S390_HWCTR_STOP:
		ret = cf_diag_ioctl_stop();
		break;
	case S390_HWCTR_READ:
		ret = cf_diag_ioctl_read(arg);
		break;
	default:
		ret = -ENOTTY;
		break;
	}
	mutex_unlock(&cf_diag_ctrset_mutex);
	put_online_cpus();
	debug_sprintf_event(cf_diag_dbg, 2, "%s ret %d\n", __func__, ret);
	return ret;
}

static const struct file_operations cf_diag_fops = {
	.owner = THIS_MODULE,
	.open = cf_diag_open,
	.release = cf_diag_release,
	.unlocked_ioctl	= cf_diag_ioctl,
	.compat_ioctl = cf_diag_ioctl,
	.llseek = no_llseek
};

static struct miscdevice cf_diag_dev = {
	.name	= S390_HWCTR_DEVICE,
	.minor	= MISC_DYNAMIC_MINOR,
	.fops	= &cf_diag_fops,
};

static int cf_diag_online_cpu(unsigned int cpu)
{
	struct cf_diag_call_on_cpu_parm p;

	mutex_lock(&cf_diag_ctrset_mutex);
	if (!cf_diag_ctrset.ctrset)
		goto out;
	p.sets = cf_diag_ctrset.ctrset;
	cf_diag_ioctl_on(&p);
out:
	mutex_unlock(&cf_diag_ctrset_mutex);
	return 0;
}

static int cf_diag_offline_cpu(unsigned int cpu)
{
	struct cf_diag_call_on_cpu_parm p;

	mutex_lock(&cf_diag_ctrset_mutex);
	if (!cf_diag_ctrset.ctrset)
		goto out;
	p.sets = cf_diag_ctrset.ctrset;
	cf_diag_ioctl_off(&p);
out:
	mutex_unlock(&cf_diag_ctrset_mutex);
	return 0;
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

	rc = misc_register(&cf_diag_dev);
	if (rc) {
		pr_err("Registration of /dev/" S390_HWCTR_DEVICE
		       "failed rc=%d\n", rc);
		goto out;
	}

	/* Setup s390dbf facility */
	cf_diag_dbg = debug_register(KMSG_COMPONENT, 2, 1, 128);
	if (!cf_diag_dbg) {
		pr_err("Registration of s390dbf(cpum_cf_diag) failed\n");
		rc = -ENOMEM;
		goto out_dbf;
	}
	debug_register_view(cf_diag_dbg, &debug_sprintf_view);

	rc = perf_pmu_register(&cf_diag, "cpum_cf_diag", -1);
	if (rc) {
		pr_err("Registration of PMU(cpum_cf_diag) failed with rc=%i\n",
		       rc);
		goto out_perf;
	}
	rc = cpuhp_setup_state_nocalls(CPUHP_AP_PERF_S390_CFD_ONLINE,
				       "perf/s390/cfd:online",
				       cf_diag_online_cpu, cf_diag_offline_cpu);
	if (!rc)
		goto out;

	pr_err("Registration of CPUHP_AP_PERF_S390_CFD_ONLINE failed rc=%i\n",
	       rc);
	perf_pmu_unregister(&cf_diag);
out_perf:
	debug_unregister_view(cf_diag_dbg, &debug_sprintf_view);
	debug_unregister(cf_diag_dbg);
out_dbf:
	misc_deregister(&cf_diag_dev);
out:
	return rc;
}
device_initcall(cf_diag_init);
