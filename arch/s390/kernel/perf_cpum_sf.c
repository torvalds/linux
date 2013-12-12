/*
 * Performance event support for the System z CPU-measurement Sampling Facility
 *
 * Copyright IBM Corp. 2013
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 */
#define KMSG_COMPONENT	"cpum_sf"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/perf_event.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/export.h>
#include <asm/cpu_mf.h>
#include <asm/irq.h>
#include <asm/debug.h>
#include <asm/timex.h>

/* Minimum number of sample-data-block-tables:
 * At least one table is required for the sampling buffer structure.
 * A single table contains up to 511 pointers to sample-data-blocks.
 */
#define CPUM_SF_MIN_SDBT    1

/* Minimum number of sample-data-blocks:
 * The minimum designates a single page for sample-data-block, i.e.,
 * up to 126 sample-data-blocks with a size of 32 bytes (bsdes).
 */
#define CPUM_SF_MIN_SDB	    126

/* Maximum number of sample-data-blocks:
 * The maximum number designates approx. 256K per CPU including
 * the given number of sample-data-blocks and taking the number
 * of sample-data-block tables into account.
 *
 * Later, this number can be increased for extending the sampling
 * buffer, for example, by factor 2 (512K) or 4 (1M).
 */
#define CPUM_SF_MAX_SDB	    6471

struct sf_buffer {
	unsigned long	 sdbt;	    /* Sample-data-block-table origin */
	/* buffer characteristics (required for buffer increments) */
	unsigned long num_sdb;	    /* Number of sample-data-blocks */
	unsigned long	 tail;	    /* last sample-data-block-table */
};

struct cpu_hw_sf {
	/* CPU-measurement sampling information block */
	struct hws_qsi_info_block qsi;
	struct hws_lsctl_request_block lsctl;
	struct sf_buffer sfb;	    /* Sampling buffer */
	unsigned int flags;	    /* Status flags */
	struct perf_event *event;   /* Scheduled perf event */
};
static DEFINE_PER_CPU(struct cpu_hw_sf, cpu_hw_sf);

/* Debug feature */
static debug_info_t *sfdbg;

/*
 * sf_buffer_available() - Check for an allocated sampling buffer
 */
static int sf_buffer_available(struct cpu_hw_sf *cpuhw)
{
	return (cpuhw->sfb.sdbt) ? 1 : 0;
}

/*
 * deallocate sampling facility buffer
 */
static void free_sampling_buffer(struct sf_buffer *sfb)
{
	unsigned long sdbt, *curr;

	if (!sfb->sdbt)
		return;

	sdbt = sfb->sdbt;
	curr = (unsigned long *) sdbt;

	/* we'll free the SDBT after all SDBs are processed... */
	while (1) {
		if (!*curr || !sdbt)
			break;

		/* watch for link entry reset if found */
		if (is_link_entry(curr)) {
			curr = get_next_sdbt(curr);
			if (sdbt)
				free_page(sdbt);

			/* we are done if we reach the origin */
			if ((unsigned long) curr == sfb->sdbt)
				break;
			else
				sdbt = (unsigned long) curr;
		} else {
			/* process SDB pointer */
			if (*curr) {
				free_page(*curr);
				curr++;
			}
		}
	}

	debug_sprintf_event(sfdbg, 5,
			    "free_sampling_buffer: freed sdbt=%0lx\n", sfb->sdbt);
	memset(sfb, 0, sizeof(*sfb));
}

/*
 * allocate_sampling_buffer() - allocate sampler memory
 *
 * Allocates and initializes a sampling buffer structure using the
 * specified number of sample-data-blocks (SDB).  For each allocation,
 * a 4K page is used.  The number of sample-data-block-tables (SDBT)
 * are calculated from SDBs.
 * Also set the ALERT_REQ mask in each SDBs trailer.
 *
 * Returns zero on success, non-zero otherwise.
 */
static int alloc_sampling_buffer(struct sf_buffer *sfb, unsigned long num_sdb)
{
	int j, k, rc;
	unsigned long *sdbt, *tail, *trailer;
	unsigned long sdb;
	unsigned long num_sdbt, sdb_per_table;

	if (sfb->sdbt)
		return -EINVAL;
	sfb->num_sdb = 0;

	/* Compute the number of required sample-data-block-tables (SDBT) */
	num_sdbt = num_sdb / ((PAGE_SIZE - 8) / 8);
	if (num_sdbt < CPUM_SF_MIN_SDBT)
		num_sdbt = CPUM_SF_MIN_SDBT;
	sdb_per_table = (PAGE_SIZE - 8) / 8;

	debug_sprintf_event(sfdbg, 4, "alloc_sampling_buffer: num_sdbt=%lu "
			    "num_sdb=%lu sdb_per_table=%lu\n",
			    num_sdbt, num_sdb, sdb_per_table);
	sdbt = NULL;
	tail = sdbt;

	for (j = 0; j < num_sdbt; j++) {
		sdbt = (unsigned long *) get_zeroed_page(GFP_KERNEL);
		if (!sdbt) {
			rc = -ENOMEM;
			goto allocate_sdbt_error;
		}

		/* save origin of sample-data-block-table */
		if (!sfb->sdbt)
			sfb->sdbt = (unsigned long) sdbt;

		/* link current page to tail of chain */
		if (tail)
			*tail = (unsigned long)(void *) sdbt + 1;

		for (k = 0; k < num_sdb && k < sdb_per_table; k++) {
			/* get and set SDB page */
			sdb = get_zeroed_page(GFP_KERNEL);
			if (!sdb) {
				rc = -ENOMEM;
				goto allocate_sdbt_error;
			}
			*sdbt = sdb;
			trailer = trailer_entry_ptr(*sdbt);
			*trailer = SDB_TE_ALERT_REQ_MASK;
			sdbt++;
		}
		num_sdb -= k;
		sfb->num_sdb += k;	/* count allocated sdb's */
		tail = sdbt;
	}

	rc = 0;
	if (tail)
		*tail = sfb->sdbt + 1;
	sfb->tail = (unsigned long) (void *)tail;

allocate_sdbt_error:
	if (rc)
		free_sampling_buffer(sfb);
	else
		debug_sprintf_event(sfdbg, 4,
			"alloc_sampling_buffer: tear=%0lx dear=%0lx\n",
			sfb->sdbt, *(unsigned long *) sfb->sdbt);
	return rc;
}

static int allocate_sdbt(struct cpu_hw_sf *cpuhw, const struct hw_perf_event *hwc)
{
	unsigned long n_sdb, freq;
	unsigned long factor;

	/* Calculate sampling buffers using 4K pages
	 *
	 *    1. Use frequency as input.  The samping buffer is designed for
	 *	 a complete second.  This can be adjusted through the "factor"
	 *	 variable.
	 *	 In any case, alloc_sampling_buffer() sets the Alert Request
	 *	 Control indicator to trigger measurement-alert to harvest
	 *	 sample-data-blocks (sdb).
	 *
	 *    2. Compute the number of sample-data-blocks and ensure a minimum
	 *	 of CPUM_SF_MIN_SDB.  Also ensure the upper limit does not
	 *	 exceed CPUM_SF_MAX_SDB.  See also the remarks for these
	 *	 symbolic constants.
	 *
	 *    3. Compute number of pages used for the sample-data-block-table
	 *	 and ensure a minimum of CPUM_SF_MIN_SDBT (at minimum one table
	 *	 to manage up to 511 sample-data-blocks).
	 */
	freq = sample_rate_to_freq(&cpuhw->qsi, SAMPL_RATE(hwc));
	factor = 1;
	n_sdb = DIV_ROUND_UP(freq, factor * ((PAGE_SIZE-64) / cpuhw->qsi.bsdes));
	if (n_sdb < CPUM_SF_MIN_SDB)
		n_sdb = CPUM_SF_MIN_SDB;

	/* Return if there is already a sampling buffer allocated.
	 * XXX Remove this later and check number of available and
	 * required sdb's and, if necessary, increase the sampling buffer.
	 */
	if (sf_buffer_available(cpuhw))
		return 0;

	debug_sprintf_event(sfdbg, 3,
			    "allocate_sdbt: rate=%lu f=%lu sdb=%lu/%i cpuhw=%p\n",
			    SAMPL_RATE(hwc), freq, n_sdb, CPUM_SF_MAX_SDB, cpuhw);

	return alloc_sampling_buffer(&cpuhw->sfb,
			       min_t(unsigned long, n_sdb, CPUM_SF_MAX_SDB));
}


/* Number of perf events counting hardware events */
static atomic_t num_events;
/* Used to avoid races in calling reserve/release_cpumf_hardware */
static DEFINE_MUTEX(pmc_reserve_mutex);

/*
 * sf_disable() - Switch off sampling facility
 */
static int sf_disable(void)
{
	struct hws_lsctl_request_block sreq;

	memset(&sreq, 0, sizeof(sreq));
	return lsctl(&sreq);
}


#define PMC_INIT      0
#define PMC_RELEASE   1
#define PMC_FAILURE   2
static void setup_pmc_cpu(void *flags)
{
	int err;
	struct cpu_hw_sf *cpusf = &__get_cpu_var(cpu_hw_sf);

	err = 0;
	switch (*((int *) flags)) {
	case PMC_INIT:
		memset(cpusf, 0, sizeof(*cpusf));
		err = qsi(&cpusf->qsi);
		if (err)
			break;
		cpusf->flags |= PMU_F_RESERVED;
		err = sf_disable();
		if (err)
			pr_err("Switching off the sampling facility failed "
			       "with rc=%i\n", err);
		debug_sprintf_event(sfdbg, 5,
				    "setup_pmc_cpu: initialized: cpuhw=%p\n", cpusf);
		break;
	case PMC_RELEASE:
		cpusf->flags &= ~PMU_F_RESERVED;
		err = sf_disable();
		if (err) {
			pr_err("Switching off the sampling facility failed "
			       "with rc=%i\n", err);
		} else {
			if (cpusf->sfb.sdbt)
				free_sampling_buffer(&cpusf->sfb);
		}
		debug_sprintf_event(sfdbg, 5,
				    "setup_pmc_cpu: released: cpuhw=%p\n", cpusf);
		break;
	}
	if (err)
		*((int *) flags) |= PMC_FAILURE;
}

static void release_pmc_hardware(void)
{
	int flags = PMC_RELEASE;

	irq_subclass_unregister(IRQ_SUBCLASS_MEASUREMENT_ALERT);
	on_each_cpu(setup_pmc_cpu, &flags, 1);
	perf_release_sampling();
}

static int reserve_pmc_hardware(void)
{
	int flags = PMC_INIT;
	int err;

	err = perf_reserve_sampling();
	if (err)
		return err;
	on_each_cpu(setup_pmc_cpu, &flags, 1);
	if (flags & PMC_FAILURE) {
		release_pmc_hardware();
		return -ENODEV;
	}
	irq_subclass_register(IRQ_SUBCLASS_MEASUREMENT_ALERT);

	return 0;
}

static void hw_perf_event_destroy(struct perf_event *event)
{
	/* Release PMC if this is the last perf event */
	if (!atomic_add_unless(&num_events, -1, 1)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_dec_return(&num_events) == 0)
			release_pmc_hardware();
		mutex_unlock(&pmc_reserve_mutex);
	}
}

static void hw_init_period(struct hw_perf_event *hwc, u64 period)
{
	hwc->sample_period = period;
	hwc->last_period = hwc->sample_period;
	local64_set(&hwc->period_left, hwc->sample_period);
}

static void hw_reset_registers(struct hw_perf_event *hwc,
			       unsigned long sdbt_origin)
{
	TEAR_REG(hwc) = sdbt_origin;	      /* (re)set to first sdb table */
}

static unsigned long hw_limit_rate(const struct hws_qsi_info_block *si,
				   unsigned long rate)
{
	if (rate < si->min_sampl_rate)
		return si->min_sampl_rate;
	if (rate > si->max_sampl_rate)
		return si->max_sampl_rate;
	return rate;
}

static int __hw_perf_event_init(struct perf_event *event)
{
	struct cpu_hw_sf *cpuhw;
	struct hws_qsi_info_block si;
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	unsigned long rate;
	int cpu, err;

	/* Reserve CPU-measurement sampling facility */
	err = 0;
	if (!atomic_inc_not_zero(&num_events)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_read(&num_events) == 0 && reserve_pmc_hardware())
			err = -EBUSY;
		else
			atomic_inc(&num_events);
		mutex_unlock(&pmc_reserve_mutex);
	}
	event->destroy = hw_perf_event_destroy;

	if (err)
		goto out;

	/* Access per-CPU sampling information (query sampling info) */
	/*
	 * The event->cpu value can be -1 to count on every CPU, for example,
	 * when attaching to a task.  If this is specified, use the query
	 * sampling info from the current CPU, otherwise use event->cpu to
	 * retrieve the per-CPU information.
	 * Later, cpuhw indicates whether to allocate sampling buffers for a
	 * particular CPU (cpuhw!=NULL) or each online CPU (cpuw==NULL).
	 */
	memset(&si, 0, sizeof(si));
	cpuhw = NULL;
	if (event->cpu == -1)
		qsi(&si);
	else {
		/* Event is pinned to a particular CPU, retrieve the per-CPU
		 * sampling structure for accessing the CPU-specific QSI.
		 */
		cpuhw = &per_cpu(cpu_hw_sf, event->cpu);
		si = cpuhw->qsi;
	}

	/* Check sampling facility authorization and, if not authorized,
	 * fall back to other PMUs.  It is safe to check any CPU because
	 * the authorization is identical for all configured CPUs.
	 */
	if (!si.as) {
		err = -ENOENT;
		goto out;
	}

	/* The sampling information (si) contains information about the
	 * min/max sampling intervals and the CPU speed.  So calculate the
	 * correct sampling interval and avoid the whole period adjust
	 * feedback loop.
	 */
	rate = 0;
	if (attr->freq) {
		rate = freq_to_sample_rate(&si, attr->sample_freq);
		rate = hw_limit_rate(&si, rate);
		attr->freq = 0;
		attr->sample_period = rate;
	} else {
		/* The min/max sampling rates specifies the valid range
		 * of sample periods.  If the specified sample period is
		 * out of range, limit the period to the range boundary.
		 */
		rate = hw_limit_rate(&si, hwc->sample_period);

		/* The perf core maintains a maximum sample rate that is
		 * configurable through the sysctl interface.  Ensure the
		 * sampling rate does not exceed this value.  This also helps
		 * to avoid throttling when pushing samples with
		 * perf_event_overflow().
		 */
		if (sample_rate_to_freq(&si, rate) >
		      sysctl_perf_event_sample_rate) {
			err = -EINVAL;
			debug_sprintf_event(sfdbg, 1, "Sampling rate exceeds maximum perf sample rate\n");
			goto out;
		}
	}
	SAMPL_RATE(hwc) = rate;
	hw_init_period(hwc, SAMPL_RATE(hwc));

	/* Allocate the per-CPU sampling buffer using the CPU information
	 * from the event.  If the event is not pinned to a particular
	 * CPU (event->cpu == -1; or cpuhw == NULL), allocate sampling
	 * buffers for each online CPU.
	 */
	if (cpuhw)
		/* Event is pinned to a particular CPU */
		err = allocate_sdbt(cpuhw, hwc);
	else {
		/* Event is not pinned, allocate sampling buffer on
		 * each online CPU
		 */
		for_each_online_cpu(cpu) {
			cpuhw = &per_cpu(cpu_hw_sf, cpu);
			err = allocate_sdbt(cpuhw, hwc);
			if (err)
				break;
		}
	}
out:
	return err;
}

static int cpumsf_pmu_event_init(struct perf_event *event)
{
	int err;

	/* No support for taken branch sampling */
	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	switch (event->attr.type) {
	case PERF_TYPE_RAW:
		if (event->attr.config != PERF_EVENT_CPUM_SF)
			return -ENOENT;
		break;
	case PERF_TYPE_HARDWARE:
		/* Support sampling of CPU cycles in addition to the
		 * counter facility.  However, the counter facility
		 * is more precise and, hence, restrict this PMU to
		 * sampling events only.
		 */
		if (event->attr.config != PERF_COUNT_HW_CPU_CYCLES)
			return -ENOENT;
		if (!is_sampling_event(event))
			return -ENOENT;
		break;
	default:
		return -ENOENT;
	}

	if (event->cpu >= nr_cpumask_bits ||
	    (event->cpu >= 0 && !cpu_online(event->cpu)))
		return -ENODEV;

	err = __hw_perf_event_init(event);
	if (unlikely(err))
		if (event->destroy)
			event->destroy(event);
	return err;
}

static void cpumsf_pmu_enable(struct pmu *pmu)
{
	struct cpu_hw_sf *cpuhw = &__get_cpu_var(cpu_hw_sf);
	int err;

	if (cpuhw->flags & PMU_F_ENABLED)
		return;

	if (cpuhw->flags & PMU_F_ERR_MASK)
		return;

	cpuhw->flags |= PMU_F_ENABLED;
	barrier();

	err = lsctl(&cpuhw->lsctl);
	if (err) {
		cpuhw->flags &= ~PMU_F_ENABLED;
		pr_err("Loading sampling controls failed: op=%i err=%i\n",
			1, err);
		return;
	}

	debug_sprintf_event(sfdbg, 6, "pmu_enable: es=%i cs=%i tear=%p dear=%p\n",
			    cpuhw->lsctl.es, cpuhw->lsctl.cs,
			    (void *) cpuhw->lsctl.tear, (void *) cpuhw->lsctl.dear);
}

static void cpumsf_pmu_disable(struct pmu *pmu)
{
	struct cpu_hw_sf *cpuhw = &__get_cpu_var(cpu_hw_sf);
	struct hws_lsctl_request_block inactive;
	struct hws_qsi_info_block si;
	int err;

	if (!(cpuhw->flags & PMU_F_ENABLED))
		return;

	if (cpuhw->flags & PMU_F_ERR_MASK)
		return;

	/* Switch off sampling activation control */
	inactive = cpuhw->lsctl;
	inactive.cs = 0;

	err = lsctl(&inactive);
	if (err) {
		pr_err("Loading sampling controls failed: op=%i err=%i\n",
			2, err);
		return;
	}

	/* Save state of TEAR and DEAR register contents */
	if (!qsi(&si)) {
		/* TEAR/DEAR values are valid only if the sampling facility is
		 * enabled.  Note that cpumsf_pmu_disable() might be called even
		 * for a disabled sampling facility because cpumsf_pmu_enable()
		 * controls the enable/disable state.
		 */
		if (si.es) {
			cpuhw->lsctl.tear = si.tear;
			cpuhw->lsctl.dear = si.dear;
		}
	} else
		debug_sprintf_event(sfdbg, 3, "cpumsf_pmu_disable: "
				    "qsi() failed with err=%i\n", err);

	cpuhw->flags &= ~PMU_F_ENABLED;
}

/* perf_push_sample() - Push samples to perf
 * @event:	The perf event
 * @sample:	Hardware sample data
 *
 * Use the hardware sample data to create perf event sample.  The sample
 * is the pushed to the event subsystem and the function checks for
 * possible event overflows.  If an event overflow occurs, the PMU is
 * stopped.
 *
 * Return non-zero if an event overflow occurred.
 */
static int perf_push_sample(struct perf_event *event,
			    struct hws_data_entry *sample)
{
	int overflow;
	struct pt_regs regs;
	struct perf_sample_data data;

	/* Skip samples that are invalid or for which the instruction address
	 * is not predictable.	For the latter, the wait-state bit is set.
	 */
	if (sample->I || sample->W)
		return 0;

	perf_sample_data_init(&data, 0, event->hw.last_period);

	memset(&regs, 0, sizeof(regs));
	regs.psw.addr = sample->ia;
	if (sample->T)
		regs.psw.mask |= PSW_MASK_DAT;
	if (sample->W)
		regs.psw.mask |= PSW_MASK_WAIT;
	if (sample->P)
		regs.psw.mask |= PSW_MASK_PSTATE;
	switch (sample->AS) {
	case 0x0:
		regs.psw.mask |= PSW_ASC_PRIMARY;
		break;
	case 0x1:
		regs.psw.mask |= PSW_ASC_ACCREG;
		break;
	case 0x2:
		regs.psw.mask |= PSW_ASC_SECONDARY;
		break;
	case 0x3:
		regs.psw.mask |= PSW_ASC_HOME;
		break;
	}

	overflow = 0;
	if (perf_event_overflow(event, &data, &regs)) {
		overflow = 1;
		event->pmu->stop(event, 0);
		debug_sprintf_event(sfdbg, 4, "perf_push_sample: PMU stopped"
				    " because of an event overflow\n");
	}
	perf_event_update_userpage(event);

	return overflow;
}

static void perf_event_count_update(struct perf_event *event, u64 count)
{
	local64_add(count, &event->count);
}

/* hw_collect_samples() - Walk through a sample-data-block and collect samples
 * @event:	The perf event
 * @sdbt:	Sample-data-block table
 * @overflow:	Event overflow counter
 *
 * Walks through a sample-data-block and collects hardware sample-data that is
 * pushed to the perf event subsystem.	The overflow reports the number of
 * samples that has been discarded due to an event overflow.
 */
static void hw_collect_samples(struct perf_event *event, unsigned long *sdbt,
			       unsigned long long *overflow)
{
	struct hws_data_entry *sample;
	unsigned long *trailer;

	trailer = trailer_entry_ptr(*sdbt);
	sample = (struct hws_data_entry *) *sdbt;
	while ((unsigned long *) sample < trailer) {
		/* Check for an empty sample */
		if (!sample->def)
			break;

		/* Update perf event period */
		perf_event_count_update(event, SAMPL_RATE(&event->hw));

		/* Check for basic sampling mode */
		if (sample->def == 0x0001) {
			/* If an event overflow occurred, the PMU is stopped to
			 * throttle event delivery.  Remaining sample data is
			 * discarded.
			 */
			if (!*overflow)
				*overflow = perf_push_sample(event, sample);
			else
				/* Count discarded samples */
				*overflow += 1;
		} else
			/* Sample slot is not yet written or other record */
			debug_sprintf_event(sfdbg, 5, "hw_collect_samples: "
					    "Unknown sample data entry format:"
					    " %i\n", sample->def);

		/* Reset sample slot and advance to next sample */
		sample->def = 0;
		sample++;
	}
}

/* hw_perf_event_update() - Process sampling buffer
 * @event:	The perf event
 * @flush_all:	Flag to also flush partially filled sample-data-blocks
 *
 * Processes the sampling buffer and create perf event samples.
 * The sampling buffer position are retrieved and saved in the TEAR_REG
 * register of the specified perf event.
 *
 * Only full sample-data-blocks are processed.	Specify the flash_all flag
 * to also walk through partially filled sample-data-blocks.
 *
 */
static void hw_perf_event_update(struct perf_event *event, int flush_all)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hws_trailer_entry *te;
	unsigned long *sdbt;
	unsigned long long event_overflow, sampl_overflow;
	int done;

	sdbt = (unsigned long *) TEAR_REG(hwc);
	done = event_overflow = sampl_overflow = 0;
	while (!done) {
		/* Get the trailer entry of the sample-data-block */
		te = (struct hws_trailer_entry *) trailer_entry_ptr(*sdbt);

		/* Leave loop if no more work to do (block full indicator) */
		if (!te->f) {
			done = 1;
			if (!flush_all)
				break;
		}

		/* Check sample overflow count */
		if (te->overflow) {
			/* Increment sample overflow counter */
			sampl_overflow += te->overflow;

			/* XXX: If an sample overflow occurs, increase the
			 *	sampling buffer.  Set a "realloc" flag because
			 *	the sampler must be re-enabled for changing
			 *	the sample-data-block-table content.
			 */
		}

		/* Timestamps are valid for full sample-data-blocks only */
		debug_sprintf_event(sfdbg, 6, "hw_perf_event_update: sdbt=%p "
				    "overflow=%llu timestamp=0x%llx\n",
				    sdbt, te->overflow,
				    (te->f) ? te->timestamp : 0ULL);

		/* Collect all samples from a single sample-data-block and
		 * flag if an (perf) event overflow happened.  If so, the PMU
		 * is stopped and remaining samples will be discarded.
		 */
		hw_collect_samples(event, sdbt, &event_overflow);

		/* Reset trailer */
		xchg(&te->overflow, 0);
		xchg((unsigned char *) te, 0x40);

		/* Advance to next sample-data-block */
		sdbt++;
		if (is_link_entry(sdbt))
			sdbt = get_next_sdbt(sdbt);

		/* Update event hardware registers */
		TEAR_REG(hwc) = (unsigned long) sdbt;

		/* Stop processing sample-data if all samples of the current
		 * sample-data-block were flushed even if it was not full.
		 */
		if (flush_all && done)
			break;

		/* If an event overflow happened, discard samples by
		 * processing any remaining sample-data-blocks.
		 */
		if (event_overflow)
			flush_all = 1;
	}

	if (sampl_overflow || event_overflow)
		debug_sprintf_event(sfdbg, 4, "hw_perf_event_update: "
				    "overflow stats: sample=%llu event=%llu\n",
				    sampl_overflow, event_overflow);
}

static void cpumsf_pmu_read(struct perf_event *event)
{
	/* Nothing to do ... updates are interrupt-driven */
}

/* Activate sampling control.
 * Next call of pmu_enable() starts sampling.
 */
static void cpumsf_pmu_start(struct perf_event *event, int flags)
{
	struct cpu_hw_sf *cpuhw = &__get_cpu_var(cpu_hw_sf);

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

	perf_pmu_disable(event->pmu);
	event->hw.state = 0;
	cpuhw->lsctl.cs = 1;
	perf_pmu_enable(event->pmu);
}

/* Deactivate sampling control.
 * Next call of pmu_enable() stops sampling.
 */
static void cpumsf_pmu_stop(struct perf_event *event, int flags)
{
	struct cpu_hw_sf *cpuhw = &__get_cpu_var(cpu_hw_sf);

	if (event->hw.state & PERF_HES_STOPPED)
		return;

	perf_pmu_disable(event->pmu);
	cpuhw->lsctl.cs = 0;
	event->hw.state |= PERF_HES_STOPPED;

	if ((flags & PERF_EF_UPDATE) && !(event->hw.state & PERF_HES_UPTODATE)) {
		hw_perf_event_update(event, 1);
		event->hw.state |= PERF_HES_UPTODATE;
	}
	perf_pmu_enable(event->pmu);
}

static int cpumsf_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_sf *cpuhw = &__get_cpu_var(cpu_hw_sf);
	int err;

	if (cpuhw->flags & PMU_F_IN_USE)
		return -EAGAIN;

	if (!cpuhw->sfb.sdbt)
		return -EINVAL;

	err = 0;
	perf_pmu_disable(event->pmu);

	event->hw.state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	/* Set up sampling controls.  Always program the sampling register
	 * using the SDB-table start.  Reset TEAR_REG event hardware register
	 * that is used by hw_perf_event_update() to store the sampling buffer
	 * position after samples have been flushed.
	 */
	cpuhw->lsctl.s = 0;
	cpuhw->lsctl.h = 1;
	cpuhw->lsctl.tear = cpuhw->sfb.sdbt;
	cpuhw->lsctl.dear = *(unsigned long *) cpuhw->sfb.sdbt;
	cpuhw->lsctl.interval = SAMPL_RATE(&event->hw);
	hw_reset_registers(&event->hw, cpuhw->sfb.sdbt);

	/* Ensure sampling functions are in the disabled state.  If disabled,
	 * switch on sampling enable control. */
	if (WARN_ON_ONCE(cpuhw->lsctl.es == 1)) {
		err = -EAGAIN;
		goto out;
	}
	cpuhw->lsctl.es = 1;

	/* Set in_use flag and store event */
	event->hw.idx = 0;	  /* only one sampling event per CPU supported */
	cpuhw->event = event;
	cpuhw->flags |= PMU_F_IN_USE;

	if (flags & PERF_EF_START)
		cpumsf_pmu_start(event, PERF_EF_RELOAD);
out:
	perf_event_update_userpage(event);
	perf_pmu_enable(event->pmu);
	return err;
}

static void cpumsf_pmu_del(struct perf_event *event, int flags)
{
	struct cpu_hw_sf *cpuhw = &__get_cpu_var(cpu_hw_sf);

	perf_pmu_disable(event->pmu);
	cpumsf_pmu_stop(event, PERF_EF_UPDATE);

	cpuhw->lsctl.es = 0;
	cpuhw->flags &= ~PMU_F_IN_USE;
	cpuhw->event = NULL;

	perf_event_update_userpage(event);
	perf_pmu_enable(event->pmu);
}

static int cpumsf_pmu_event_idx(struct perf_event *event)
{
	return event->hw.idx;
}

CPUMF_EVENT_ATTR(SF, SF_CYCLES_BASIC, PERF_EVENT_CPUM_SF);

static struct attribute *cpumsf_pmu_events_attr[] = {
	CPUMF_EVENT_PTR(SF, SF_CYCLES_BASIC),
	NULL,
};

PMU_FORMAT_ATTR(event, "config:0-63");

static struct attribute *cpumsf_pmu_format_attr[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group cpumsf_pmu_events_group = {
	.name = "events",
	.attrs = cpumsf_pmu_events_attr,
};
static struct attribute_group cpumsf_pmu_format_group = {
	.name = "format",
	.attrs = cpumsf_pmu_format_attr,
};
static const struct attribute_group *cpumsf_pmu_attr_groups[] = {
	&cpumsf_pmu_events_group,
	&cpumsf_pmu_format_group,
	NULL,
};

static struct pmu cpumf_sampling = {
	.pmu_enable   = cpumsf_pmu_enable,
	.pmu_disable  = cpumsf_pmu_disable,

	.event_init   = cpumsf_pmu_event_init,
	.add	      = cpumsf_pmu_add,
	.del	      = cpumsf_pmu_del,

	.start	      = cpumsf_pmu_start,
	.stop	      = cpumsf_pmu_stop,
	.read	      = cpumsf_pmu_read,

	.event_idx    = cpumsf_pmu_event_idx,
	.attr_groups  = cpumsf_pmu_attr_groups,
};

static void cpumf_measurement_alert(struct ext_code ext_code,
				    unsigned int alert, unsigned long unused)
{
	struct cpu_hw_sf *cpuhw;

	if (!(alert & CPU_MF_INT_SF_MASK))
		return;
	inc_irq_stat(IRQEXT_CMS);
	cpuhw = &__get_cpu_var(cpu_hw_sf);

	/* Measurement alerts are shared and might happen when the PMU
	 * is not reserved.  Ignore these alerts in this case. */
	if (!(cpuhw->flags & PMU_F_RESERVED))
		return;

	/* The processing below must take care of multiple alert events that
	 * might be indicated concurrently. */

	/* Program alert request */
	if (alert & CPU_MF_INT_SF_PRA) {
		if (cpuhw->flags & PMU_F_IN_USE)
			hw_perf_event_update(cpuhw->event, 0);
		else
			WARN_ON_ONCE(!(cpuhw->flags & PMU_F_IN_USE));
	}

	/* Report measurement alerts only for non-PRA codes */
	if (alert != CPU_MF_INT_SF_PRA)
		debug_sprintf_event(sfdbg, 6, "measurement alert: 0x%x\n", alert);

	/* Sampling authorization change request */
	if (alert & CPU_MF_INT_SF_SACA)
		qsi(&cpuhw->qsi);

	/* Loss of sample data due to high-priority machine activities */
	if (alert & CPU_MF_INT_SF_LSDA) {
		pr_err("Sample data was lost\n");
		cpuhw->flags |= PMU_F_ERR_LSDA;
		sf_disable();
	}

	/* Invalid sampling buffer entry */
	if (alert & (CPU_MF_INT_SF_IAE|CPU_MF_INT_SF_ISE)) {
		pr_err("A sampling buffer entry is incorrect (alert=0x%x)\n",
		       alert);
		cpuhw->flags |= PMU_F_ERR_IBE;
		sf_disable();
	}
}

static int __cpuinit cpumf_pmu_notifier(struct notifier_block *self,
					unsigned long action, void *hcpu)
{
	unsigned int cpu = (long) hcpu;
	int flags;

	/* Ignore the notification if no events are scheduled on the PMU.
	 * This might be racy...
	 */
	if (!atomic_read(&num_events))
		return NOTIFY_OK;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
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

static int __init init_cpum_sampling_pmu(void)
{
	int err;

	if (!cpum_sf_avail())
		return -ENODEV;

	sfdbg = debug_register(KMSG_COMPONENT, 2, 1, 80);
	if (!sfdbg)
		pr_err("Registering for s390dbf failed\n");
	debug_register_view(sfdbg, &debug_sprintf_view);

	err = register_external_interrupt(0x1407, cpumf_measurement_alert);
	if (err) {
		pr_err("Failed to register for CPU-measurement alerts\n");
		goto out;
	}

	err = perf_pmu_register(&cpumf_sampling, "cpum_sf", PERF_TYPE_RAW);
	if (err) {
		pr_err("Failed to register cpum_sf pmu\n");
		unregister_external_interrupt(0x1407, cpumf_measurement_alert);
		goto out;
	}
	perf_cpu_notifier(cpumf_pmu_notifier);
out:
	return err;
}
arch_initcall(init_cpum_sampling_pmu);
