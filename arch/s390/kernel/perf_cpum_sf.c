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
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <asm/cpu_mf.h>
#include <asm/irq.h>
#include <asm/debug.h>
#include <asm/timex.h>

/* Minimum number of sample-data-block-tables:
 * At least one table is required for the sampling buffer structure.
 * A single table contains up to 511 pointers to sample-data-blocks.
 */
#define CPUM_SF_MIN_SDBT	1

/* Number of sample-data-blocks per sample-data-block-table (SDBT):
 * A table contains SDB pointers (8 bytes) and one table-link entry
 * that points to the origin of the next SDBT.
 */
#define CPUM_SF_SDB_PER_TABLE	((PAGE_SIZE - 8) / 8)

/* Maximum page offset for an SDBT table-link entry:
 * If this page offset is reached, a table-link entry to the next SDBT
 * must be added.
 */
#define CPUM_SF_SDBT_TL_OFFSET	(CPUM_SF_SDB_PER_TABLE * 8)
static inline int require_table_link(const void *sdbt)
{
	return ((unsigned long) sdbt & ~PAGE_MASK) == CPUM_SF_SDBT_TL_OFFSET;
}

/* Minimum and maximum sampling buffer sizes:
 *
 * This number represents the maximum size of the sampling buffer taking
 * the number of sample-data-block-tables into account.  Note that these
 * numbers apply to the basic-sampling function only.
 * The maximum number of SDBs is increased by CPUM_SF_SDB_DIAG_FACTOR if
 * the diagnostic-sampling function is active.
 *
 * Sampling buffer size		Buffer characteristics
 * ---------------------------------------------------
 *	 64KB		    ==	  16 pages (4KB per page)
 *				   1 page  for SDB-tables
 *				  15 pages for SDBs
 *
 *  32MB		    ==	8192 pages (4KB per page)
 *				  16 pages for SDB-tables
 *				8176 pages for SDBs
 */
static unsigned long __read_mostly CPUM_SF_MIN_SDB = 15;
static unsigned long __read_mostly CPUM_SF_MAX_SDB = 8176;
static unsigned long __read_mostly CPUM_SF_SDB_DIAG_FACTOR = 1;

struct sf_buffer {
	unsigned long	 *sdbt;	    /* Sample-data-block-table origin */
	/* buffer characteristics (required for buffer increments) */
	unsigned long  num_sdb;	    /* Number of sample-data-blocks */
	unsigned long num_sdbt;	    /* Number of sample-data-block-tables */
	unsigned long	 *tail;	    /* last sample-data-block-table */
};

struct aux_buffer {
	struct sf_buffer sfb;
	unsigned long head;	   /* index of SDB of buffer head */
	unsigned long alert_mark;  /* index of SDB of alert request position */
	unsigned long empty_mark;  /* mark of SDB not marked full */
	unsigned long *sdb_index;  /* SDB address for fast lookup */
	unsigned long *sdbt_index; /* SDBT address for fast lookup */
};

struct cpu_hw_sf {
	/* CPU-measurement sampling information block */
	struct hws_qsi_info_block qsi;
	/* CPU-measurement sampling control block */
	struct hws_lsctl_request_block lsctl;
	struct sf_buffer sfb;	    /* Sampling buffer */
	unsigned int flags;	    /* Status flags */
	struct perf_event *event;   /* Scheduled perf event */
	struct perf_output_handle handle; /* AUX buffer output handle */
};
static DEFINE_PER_CPU(struct cpu_hw_sf, cpu_hw_sf);

/* Debug feature */
static debug_info_t *sfdbg;

/*
 * sf_disable() - Switch off sampling facility
 */
static int sf_disable(void)
{
	struct hws_lsctl_request_block sreq;

	memset(&sreq, 0, sizeof(sreq));
	return lsctl(&sreq);
}

/*
 * sf_buffer_available() - Check for an allocated sampling buffer
 */
static int sf_buffer_available(struct cpu_hw_sf *cpuhw)
{
	return !!cpuhw->sfb.sdbt;
}

/*
 * deallocate sampling facility buffer
 */
static void free_sampling_buffer(struct sf_buffer *sfb)
{
	unsigned long *sdbt, *curr;

	if (!sfb->sdbt)
		return;

	sdbt = sfb->sdbt;
	curr = sdbt;

	/* Free the SDBT after all SDBs are processed... */
	while (1) {
		if (!*curr || !sdbt)
			break;

		/* Process table-link entries */
		if (is_link_entry(curr)) {
			curr = get_next_sdbt(curr);
			if (sdbt)
				free_page((unsigned long) sdbt);

			/* If the origin is reached, sampling buffer is freed */
			if (curr == sfb->sdbt)
				break;
			else
				sdbt = curr;
		} else {
			/* Process SDB pointer */
			if (*curr) {
				free_page(*curr);
				curr++;
			}
		}
	}

	debug_sprintf_event(sfdbg, 5,
			    "free_sampling_buffer: freed sdbt=%p\n", sfb->sdbt);
	memset(sfb, 0, sizeof(*sfb));
}

static int alloc_sample_data_block(unsigned long *sdbt, gfp_t gfp_flags)
{
	unsigned long sdb, *trailer;

	/* Allocate and initialize sample-data-block */
	sdb = get_zeroed_page(gfp_flags);
	if (!sdb)
		return -ENOMEM;
	trailer = trailer_entry_ptr(sdb);
	*trailer = SDB_TE_ALERT_REQ_MASK;

	/* Link SDB into the sample-data-block-table */
	*sdbt = sdb;

	return 0;
}

/*
 * realloc_sampling_buffer() - extend sampler memory
 *
 * Allocates new sample-data-blocks and adds them to the specified sampling
 * buffer memory.
 *
 * Important: This modifies the sampling buffer and must be called when the
 *	      sampling facility is disabled.
 *
 * Returns zero on success, non-zero otherwise.
 */
static int realloc_sampling_buffer(struct sf_buffer *sfb,
				   unsigned long num_sdb, gfp_t gfp_flags)
{
	int i, rc;
	unsigned long *new, *tail;

	if (!sfb->sdbt || !sfb->tail)
		return -EINVAL;

	if (!is_link_entry(sfb->tail))
		return -EINVAL;

	/* Append to the existing sampling buffer, overwriting the table-link
	 * register.
	 * The tail variables always points to the "tail" (last and table-link)
	 * entry in an SDB-table.
	 */
	tail = sfb->tail;

	/* Do a sanity check whether the table-link entry points to
	 * the sampling buffer origin.
	 */
	if (sfb->sdbt != get_next_sdbt(tail)) {
		debug_sprintf_event(sfdbg, 3, "realloc_sampling_buffer: "
				    "sampling buffer is not linked: origin=%p"
				    "tail=%p\n",
				    (void *) sfb->sdbt, (void *) tail);
		return -EINVAL;
	}

	/* Allocate remaining SDBs */
	rc = 0;
	for (i = 0; i < num_sdb; i++) {
		/* Allocate a new SDB-table if it is full. */
		if (require_table_link(tail)) {
			new = (unsigned long *) get_zeroed_page(gfp_flags);
			if (!new) {
				rc = -ENOMEM;
				break;
			}
			sfb->num_sdbt++;
			/* Link current page to tail of chain */
			*tail = (unsigned long)(void *) new + 1;
			tail = new;
		}

		/* Allocate a new sample-data-block.
		 * If there is not enough memory, stop the realloc process
		 * and simply use what was allocated.  If this is a temporary
		 * issue, a new realloc call (if required) might succeed.
		 */
		rc = alloc_sample_data_block(tail, gfp_flags);
		if (rc)
			break;
		sfb->num_sdb++;
		tail++;
	}

	/* Link sampling buffer to its origin */
	*tail = (unsigned long) sfb->sdbt + 1;
	sfb->tail = tail;

	debug_sprintf_event(sfdbg, 4, "realloc_sampling_buffer: new buffer"
			    " settings: sdbt=%lu sdb=%lu\n",
			    sfb->num_sdbt, sfb->num_sdb);
	return rc;
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
	int rc;

	if (sfb->sdbt)
		return -EINVAL;

	/* Allocate the sample-data-block-table origin */
	sfb->sdbt = (unsigned long *) get_zeroed_page(GFP_KERNEL);
	if (!sfb->sdbt)
		return -ENOMEM;
	sfb->num_sdb = 0;
	sfb->num_sdbt = 1;

	/* Link the table origin to point to itself to prepare for
	 * realloc_sampling_buffer() invocation.
	 */
	sfb->tail = sfb->sdbt;
	*sfb->tail = (unsigned long)(void *) sfb->sdbt + 1;

	/* Allocate requested number of sample-data-blocks */
	rc = realloc_sampling_buffer(sfb, num_sdb, GFP_KERNEL);
	if (rc) {
		free_sampling_buffer(sfb);
		debug_sprintf_event(sfdbg, 4, "alloc_sampling_buffer: "
			"realloc_sampling_buffer failed with rc=%i\n", rc);
	} else
		debug_sprintf_event(sfdbg, 4,
			"alloc_sampling_buffer: tear=%p dear=%p\n",
			sfb->sdbt, (void *) *sfb->sdbt);
	return rc;
}

static void sfb_set_limits(unsigned long min, unsigned long max)
{
	struct hws_qsi_info_block si;

	CPUM_SF_MIN_SDB = min;
	CPUM_SF_MAX_SDB = max;

	memset(&si, 0, sizeof(si));
	if (!qsi(&si))
		CPUM_SF_SDB_DIAG_FACTOR = DIV_ROUND_UP(si.dsdes, si.bsdes);
}

static unsigned long sfb_max_limit(struct hw_perf_event *hwc)
{
	return SAMPL_DIAG_MODE(hwc) ? CPUM_SF_MAX_SDB * CPUM_SF_SDB_DIAG_FACTOR
				    : CPUM_SF_MAX_SDB;
}

static unsigned long sfb_pending_allocs(struct sf_buffer *sfb,
					struct hw_perf_event *hwc)
{
	if (!sfb->sdbt)
		return SFB_ALLOC_REG(hwc);
	if (SFB_ALLOC_REG(hwc) > sfb->num_sdb)
		return SFB_ALLOC_REG(hwc) - sfb->num_sdb;
	return 0;
}

static int sfb_has_pending_allocs(struct sf_buffer *sfb,
				   struct hw_perf_event *hwc)
{
	return sfb_pending_allocs(sfb, hwc) > 0;
}

static void sfb_account_allocs(unsigned long num, struct hw_perf_event *hwc)
{
	/* Limit the number of SDBs to not exceed the maximum */
	num = min_t(unsigned long, num, sfb_max_limit(hwc) - SFB_ALLOC_REG(hwc));
	if (num)
		SFB_ALLOC_REG(hwc) += num;
}

static void sfb_init_allocs(unsigned long num, struct hw_perf_event *hwc)
{
	SFB_ALLOC_REG(hwc) = 0;
	sfb_account_allocs(num, hwc);
}

static size_t event_sample_size(struct hw_perf_event *hwc)
{
	struct sf_raw_sample *sfr = (struct sf_raw_sample *) RAWSAMPLE_REG(hwc);
	size_t sample_size;

	/* The sample size depends on the sampling function: The basic-sampling
	 * function must be always enabled, diagnostic-sampling function is
	 * optional.
	 */
	sample_size = sfr->bsdes;
	if (SAMPL_DIAG_MODE(hwc))
		sample_size += sfr->dsdes;

	return sample_size;
}

static void deallocate_buffers(struct cpu_hw_sf *cpuhw)
{
	if (cpuhw->sfb.sdbt)
		free_sampling_buffer(&cpuhw->sfb);
}

static int allocate_buffers(struct cpu_hw_sf *cpuhw, struct hw_perf_event *hwc)
{
	unsigned long n_sdb, freq, factor;
	size_t sfr_size, sample_size;
	struct sf_raw_sample *sfr;

	/* Allocate raw sample buffer
	 *
	 *    The raw sample buffer is used to temporarily store sampling data
	 *    entries for perf raw sample processing.  The buffer size mainly
	 *    depends on the size of diagnostic-sampling data entries which is
	 *    machine-specific.  The exact size calculation includes:
	 *	1. The first 4 bytes of diagnostic-sampling data entries are
	 *	   already reflected in the sf_raw_sample structure.  Subtract
	 *	   these bytes.
	 *	2. The perf raw sample data must be 8-byte aligned (u64) and
	 *	   perf's internal data size must be considered too.  So add
	 *	   an additional u32 for correct alignment and subtract before
	 *	   allocating the buffer.
	 *	3. Store the raw sample buffer pointer in the perf event
	 *	   hardware structure.
	 */
	sfr_size = ALIGN((sizeof(*sfr) - sizeof(sfr->diag) + cpuhw->qsi.dsdes) +
			 sizeof(u32), sizeof(u64));
	sfr_size -= sizeof(u32);
	sfr = kzalloc(sfr_size, GFP_KERNEL);
	if (!sfr)
		return -ENOMEM;
	sfr->size = sfr_size;
	sfr->bsdes = cpuhw->qsi.bsdes;
	sfr->dsdes = cpuhw->qsi.dsdes;
	RAWSAMPLE_REG(hwc) = (unsigned long) sfr;

	/* Calculate sampling buffers using 4K pages
	 *
	 *    1. Determine the sample data size which depends on the used
	 *	 sampling functions, for example, basic-sampling or
	 *	 basic-sampling with diagnostic-sampling.
	 *
	 *    2. Use the sampling frequency as input.  The sampling buffer is
	 *	 designed for almost one second.  This can be adjusted through
	 *	 the "factor" variable.
	 *	 In any case, alloc_sampling_buffer() sets the Alert Request
	 *	 Control indicator to trigger a measurement-alert to harvest
	 *	 sample-data-blocks (sdb).
	 *
	 *    3. Compute the number of sample-data-blocks and ensure a minimum
	 *	 of CPUM_SF_MIN_SDB.  Also ensure the upper limit does not
	 *	 exceed a "calculated" maximum.  The symbolic maximum is
	 *	 designed for basic-sampling only and needs to be increased if
	 *	 diagnostic-sampling is active.
	 *	 See also the remarks for these symbolic constants.
	 *
	 *    4. Compute the number of sample-data-block-tables (SDBT) and
	 *	 ensure a minimum of CPUM_SF_MIN_SDBT (one table can manage up
	 *	 to 511 SDBs).
	 */
	sample_size = event_sample_size(hwc);
	freq = sample_rate_to_freq(&cpuhw->qsi, SAMPL_RATE(hwc));
	factor = 1;
	n_sdb = DIV_ROUND_UP(freq, factor * ((PAGE_SIZE-64) / sample_size));
	if (n_sdb < CPUM_SF_MIN_SDB)
		n_sdb = CPUM_SF_MIN_SDB;

	/* If there is already a sampling buffer allocated, it is very likely
	 * that the sampling facility is enabled too.  If the event to be
	 * initialized requires a greater sampling buffer, the allocation must
	 * be postponed.  Changing the sampling buffer requires the sampling
	 * facility to be in the disabled state.  So, account the number of
	 * required SDBs and let cpumsf_pmu_enable() resize the buffer just
	 * before the event is started.
	 */
	sfb_init_allocs(n_sdb, hwc);
	if (sf_buffer_available(cpuhw))
		return 0;

	debug_sprintf_event(sfdbg, 3,
			    "allocate_buffers: rate=%lu f=%lu sdb=%lu/%lu"
			    " sample_size=%lu cpuhw=%p\n",
			    SAMPL_RATE(hwc), freq, n_sdb, sfb_max_limit(hwc),
			    sample_size, cpuhw);

	return alloc_sampling_buffer(&cpuhw->sfb,
				     sfb_pending_allocs(&cpuhw->sfb, hwc));
}

static unsigned long min_percent(unsigned int percent, unsigned long base,
				 unsigned long min)
{
	return min_t(unsigned long, min, DIV_ROUND_UP(percent * base, 100));
}

static unsigned long compute_sfb_extent(unsigned long ratio, unsigned long base)
{
	/* Use a percentage-based approach to extend the sampling facility
	 * buffer.  Accept up to 5% sample data loss.
	 * Vary the extents between 1% to 5% of the current number of
	 * sample-data-blocks.
	 */
	if (ratio <= 5)
		return 0;
	if (ratio <= 25)
		return min_percent(1, base, 1);
	if (ratio <= 50)
		return min_percent(1, base, 1);
	if (ratio <= 75)
		return min_percent(2, base, 2);
	if (ratio <= 100)
		return min_percent(3, base, 3);
	if (ratio <= 250)
		return min_percent(4, base, 4);

	return min_percent(5, base, 8);
}

static void sfb_account_overflows(struct cpu_hw_sf *cpuhw,
				  struct hw_perf_event *hwc)
{
	unsigned long ratio, num;

	if (!OVERFLOW_REG(hwc))
		return;

	/* The sample_overflow contains the average number of sample data
	 * that has been lost because sample-data-blocks were full.
	 *
	 * Calculate the total number of sample data entries that has been
	 * discarded.  Then calculate the ratio of lost samples to total samples
	 * per second in percent.
	 */
	ratio = DIV_ROUND_UP(100 * OVERFLOW_REG(hwc) * cpuhw->sfb.num_sdb,
			     sample_rate_to_freq(&cpuhw->qsi, SAMPL_RATE(hwc)));

	/* Compute number of sample-data-blocks */
	num = compute_sfb_extent(ratio, cpuhw->sfb.num_sdb);
	if (num)
		sfb_account_allocs(num, hwc);

	debug_sprintf_event(sfdbg, 5, "sfb: overflow: overflow=%llu ratio=%lu"
			    " num=%lu\n", OVERFLOW_REG(hwc), ratio, num);
	OVERFLOW_REG(hwc) = 0;
}

/* extend_sampling_buffer() - Extend sampling buffer
 * @sfb:	Sampling buffer structure (for local CPU)
 * @hwc:	Perf event hardware structure
 *
 * Use this function to extend the sampling buffer based on the overflow counter
 * and postponed allocation extents stored in the specified Perf event hardware.
 *
 * Important: This function disables the sampling facility in order to safely
 *	      change the sampling buffer structure.  Do not call this function
 *	      when the PMU is active.
 */
static void extend_sampling_buffer(struct sf_buffer *sfb,
				   struct hw_perf_event *hwc)
{
	unsigned long num, num_old;
	int rc;

	num = sfb_pending_allocs(sfb, hwc);
	if (!num)
		return;
	num_old = sfb->num_sdb;

	/* Disable the sampling facility to reset any states and also
	 * clear pending measurement alerts.
	 */
	sf_disable();

	/* Extend the sampling buffer.
	 * This memory allocation typically happens in an atomic context when
	 * called by perf.  Because this is a reallocation, it is fine if the
	 * new SDB-request cannot be satisfied immediately.
	 */
	rc = realloc_sampling_buffer(sfb, num, GFP_ATOMIC);
	if (rc)
		debug_sprintf_event(sfdbg, 5, "sfb: extend: realloc "
				    "failed with rc=%i\n", rc);

	if (sfb_has_pending_allocs(sfb, hwc))
		debug_sprintf_event(sfdbg, 5, "sfb: extend: "
				    "req=%lu alloc=%lu remaining=%lu\n",
				    num, sfb->num_sdb - num_old,
				    sfb_pending_allocs(sfb, hwc));
}


/* Number of perf events counting hardware events */
static atomic_t num_events;
/* Used to avoid races in calling reserve/release_cpumf_hardware */
static DEFINE_MUTEX(pmc_reserve_mutex);

#define PMC_INIT      0
#define PMC_RELEASE   1
#define PMC_FAILURE   2
static void setup_pmc_cpu(void *flags)
{
	int err;
	struct cpu_hw_sf *cpusf = this_cpu_ptr(&cpu_hw_sf);

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
		} else
			deallocate_buffers(cpusf);
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
}

static int reserve_pmc_hardware(void)
{
	int flags = PMC_INIT;

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
	/* Free raw sample buffer */
	if (RAWSAMPLE_REG(&event->hw))
		kfree((void *) RAWSAMPLE_REG(&event->hw));

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
			       unsigned long *sdbt_origin)
{
	struct sf_raw_sample *sfr;

	/* (Re)set to first sample-data-block-table */
	TEAR_REG(hwc) = (unsigned long) sdbt_origin;

	/* (Re)set raw sampling buffer register */
	sfr = (struct sf_raw_sample *) RAWSAMPLE_REG(hwc);
	memset(&sfr->basic, 0, sizeof(sfr->basic));
	memset(&sfr->diag, 0, sfr->dsdes);
}

static unsigned long hw_limit_rate(const struct hws_qsi_info_block *si,
				   unsigned long rate)
{
	return clamp_t(unsigned long, rate,
		       si->min_sampl_rate, si->max_sampl_rate);
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

	/* Always enable basic sampling */
	SAMPL_FLAGS(hwc) = PERF_CPUM_SF_BASIC_MODE;

	/* Check if diagnostic sampling is requested.  Deny if the required
	 * sampling authorization is missing.
	 */
	if (attr->config == PERF_EVENT_CPUM_SF_DIAG) {
		if (!si.ad) {
			err = -EPERM;
			goto out;
		}
		SAMPL_FLAGS(hwc) |= PERF_CPUM_SF_DIAG_MODE;
	}

	/* Check and set other sampling flags */
	if (attr->config1 & PERF_CPUM_SF_FULL_BLOCKS)
		SAMPL_FLAGS(hwc) |= PERF_CPUM_SF_FULL_BLOCKS;

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

	/* Initialize sample data overflow accounting */
	hwc->extra_reg.reg = REG_OVERFLOW;
	OVERFLOW_REG(hwc) = 0;

	/* Use AUX buffer. No need to allocate it by ourself */
	if (attr->config == PERF_EVENT_CPUM_SF_DIAG)
		return 0;

	/* Allocate the per-CPU sampling buffer using the CPU information
	 * from the event.  If the event is not pinned to a particular
	 * CPU (event->cpu == -1; or cpuhw == NULL), allocate sampling
	 * buffers for each online CPU.
	 */
	if (cpuhw)
		/* Event is pinned to a particular CPU */
		err = allocate_buffers(cpuhw, hwc);
	else {
		/* Event is not pinned, allocate sampling buffer on
		 * each online CPU
		 */
		for_each_online_cpu(cpu) {
			cpuhw = &per_cpu(cpu_hw_sf, cpu);
			err = allocate_buffers(cpuhw, hwc);
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
		if ((event->attr.config != PERF_EVENT_CPUM_SF) &&
		    (event->attr.config != PERF_EVENT_CPUM_SF_DIAG))
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

	/* Check online status of the CPU to which the event is pinned */
	if (event->cpu >= 0 && !cpu_online(event->cpu))
			return -ENODEV;

	/* Force reset of idle/hv excludes regardless of what the
	 * user requested.
	 */
	if (event->attr.exclude_hv)
		event->attr.exclude_hv = 0;
	if (event->attr.exclude_idle)
		event->attr.exclude_idle = 0;

	err = __hw_perf_event_init(event);
	if (unlikely(err))
		if (event->destroy)
			event->destroy(event);
	return err;
}

static void cpumsf_pmu_enable(struct pmu *pmu)
{
	struct cpu_hw_sf *cpuhw = this_cpu_ptr(&cpu_hw_sf);
	struct hw_perf_event *hwc;
	int err;

	if (cpuhw->flags & PMU_F_ENABLED)
		return;

	if (cpuhw->flags & PMU_F_ERR_MASK)
		return;

	/* Check whether to extent the sampling buffer.
	 *
	 * Two conditions trigger an increase of the sampling buffer for a
	 * perf event:
	 *    1. Postponed buffer allocations from the event initialization.
	 *    2. Sampling overflows that contribute to pending allocations.
	 *
	 * Note that the extend_sampling_buffer() function disables the sampling
	 * facility, but it can be fully re-enabled using sampling controls that
	 * have been saved in cpumsf_pmu_disable().
	 */
	if (cpuhw->event) {
		hwc = &cpuhw->event->hw;
		if (!(SAMPL_DIAG_MODE(hwc))) {
			/*
			 * Account number of overflow-designated
			 * buffer extents
			 */
			sfb_account_overflows(cpuhw, hwc);
			if (sfb_has_pending_allocs(&cpuhw->sfb, hwc))
				extend_sampling_buffer(&cpuhw->sfb, hwc);
		}
	}

	/* (Re)enable the PMU and sampling facility */
	cpuhw->flags |= PMU_F_ENABLED;
	barrier();

	err = lsctl(&cpuhw->lsctl);
	if (err) {
		cpuhw->flags &= ~PMU_F_ENABLED;
		pr_err("Loading sampling controls failed: op=%i err=%i\n",
			1, err);
		return;
	}

	debug_sprintf_event(sfdbg, 6, "pmu_enable: es=%i cs=%i ed=%i cd=%i "
			    "tear=%p dear=%p\n", cpuhw->lsctl.es, cpuhw->lsctl.cs,
			    cpuhw->lsctl.ed, cpuhw->lsctl.cd,
			    (void *) cpuhw->lsctl.tear, (void *) cpuhw->lsctl.dear);
}

static void cpumsf_pmu_disable(struct pmu *pmu)
{
	struct cpu_hw_sf *cpuhw = this_cpu_ptr(&cpu_hw_sf);
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
	inactive.cd = 0;

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

/* perf_exclude_event() - Filter event
 * @event:	The perf event
 * @regs:	pt_regs structure
 * @sde_regs:	Sample-data-entry (sde) regs structure
 *
 * Filter perf events according to their exclude specification.
 *
 * Return non-zero if the event shall be excluded.
 */
static int perf_exclude_event(struct perf_event *event, struct pt_regs *regs,
			      struct perf_sf_sde_regs *sde_regs)
{
	if (event->attr.exclude_user && user_mode(regs))
		return 1;
	if (event->attr.exclude_kernel && !user_mode(regs))
		return 1;
	if (event->attr.exclude_guest && sde_regs->in_guest)
		return 1;
	if (event->attr.exclude_host && !sde_regs->in_guest)
		return 1;
	return 0;
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
static int perf_push_sample(struct perf_event *event, struct sf_raw_sample *sfr)
{
	int overflow;
	struct pt_regs regs;
	struct perf_sf_sde_regs *sde_regs;
	struct perf_sample_data data;
	struct perf_raw_record raw = {
		.frag = {
			.size = sfr->size,
			.data = sfr,
		},
	};

	/* Setup perf sample */
	perf_sample_data_init(&data, 0, event->hw.last_period);
	data.raw = &raw;

	/* Setup pt_regs to look like an CPU-measurement external interrupt
	 * using the Program Request Alert code.  The regs.int_parm_long
	 * field which is unused contains additional sample-data-entry related
	 * indicators.
	 */
	memset(&regs, 0, sizeof(regs));
	regs.int_code = 0x1407;
	regs.int_parm = CPU_MF_INT_SF_PRA;
	sde_regs = (struct perf_sf_sde_regs *) &regs.int_parm_long;

	psw_bits(regs.psw).ia	= sfr->basic.ia;
	psw_bits(regs.psw).dat	= sfr->basic.T;
	psw_bits(regs.psw).wait = sfr->basic.W;
	psw_bits(regs.psw).pstate = sfr->basic.P;
	psw_bits(regs.psw).as	= sfr->basic.AS;

	/*
	 * Use the hardware provided configuration level to decide if the
	 * sample belongs to a guest or host. If that is not available,
	 * fall back to the following heuristics:
	 * A non-zero guest program parameter always indicates a guest
	 * sample. Some early samples or samples from guests without
	 * lpp usage would be misaccounted to the host. We use the asn
	 * value as an addon heuristic to detect most of these guest samples.
	 * If the value differs from 0xffff (the host value), we assume to
	 * be a KVM guest.
	 */
	switch (sfr->basic.CL) {
	case 1: /* logical partition */
		sde_regs->in_guest = 0;
		break;
	case 2: /* virtual machine */
		sde_regs->in_guest = 1;
		break;
	default: /* old machine, use heuristics */
		if (sfr->basic.gpp || sfr->basic.prim_asn != 0xffff)
			sde_regs->in_guest = 1;
		break;
	}

	overflow = 0;
	if (perf_exclude_event(event, &regs, sde_regs))
		goto out;
	if (perf_event_overflow(event, &data, &regs)) {
		overflow = 1;
		event->pmu->stop(event, 0);
	}
	perf_event_update_userpage(event);
out:
	return overflow;
}

static void perf_event_count_update(struct perf_event *event, u64 count)
{
	local64_add(count, &event->count);
}

static int sample_format_is_valid(struct hws_combined_entry *sample,
				   unsigned int flags)
{
	if (likely(flags & PERF_CPUM_SF_BASIC_MODE))
		/* Only basic-sampling data entries with data-entry-format
		 * version of 0x0001 can be processed.
		 */
		if (sample->basic.def != 0x0001)
			return 0;
	if (flags & PERF_CPUM_SF_DIAG_MODE)
		/* The data-entry-format number of diagnostic-sampling data
		 * entries can vary.  Because diagnostic data is just passed
		 * through, do only a sanity check on the DEF.
		 */
		if (sample->diag.def < 0x8001)
			return 0;
	return 1;
}

static int sample_is_consistent(struct hws_combined_entry *sample,
				unsigned long flags)
{
	/* This check applies only to basic-sampling data entries of potentially
	 * combined-sampling data entries.  Invalid entries cannot be processed
	 * by the PMU and, thus, do not deliver an associated
	 * diagnostic-sampling data entry.
	 */
	if (unlikely(!(flags & PERF_CPUM_SF_BASIC_MODE)))
		return 0;
	/*
	 * Samples are skipped, if they are invalid or for which the
	 * instruction address is not predictable, i.e., the wait-state bit is
	 * set.
	 */
	if (sample->basic.I || sample->basic.W)
		return 0;
	return 1;
}

static void reset_sample_slot(struct hws_combined_entry *sample,
			      unsigned long flags)
{
	if (likely(flags & PERF_CPUM_SF_BASIC_MODE))
		sample->basic.def = 0;
	if (flags & PERF_CPUM_SF_DIAG_MODE)
		sample->diag.def = 0;
}

static void sfr_store_sample(struct sf_raw_sample *sfr,
			     struct hws_combined_entry *sample)
{
	if (likely(sfr->format & PERF_CPUM_SF_BASIC_MODE))
		sfr->basic = sample->basic;
	if (sfr->format & PERF_CPUM_SF_DIAG_MODE)
		memcpy(&sfr->diag, &sample->diag, sfr->dsdes);
}

static void debug_sample_entry(struct hws_combined_entry *sample,
			       struct hws_trailer_entry *te,
			       unsigned long flags)
{
	debug_sprintf_event(sfdbg, 4, "hw_collect_samples: Found unknown "
			    "sampling data entry: te->f=%i basic.def=%04x (%p)"
			    " diag.def=%04x (%p)\n", te->f,
			    sample->basic.def, &sample->basic,
			    (flags & PERF_CPUM_SF_DIAG_MODE)
					? sample->diag.def : 0xFFFF,
			    (flags & PERF_CPUM_SF_DIAG_MODE)
					?  &sample->diag : NULL);
}

/* hw_collect_samples() - Walk through a sample-data-block and collect samples
 * @event:	The perf event
 * @sdbt:	Sample-data-block table
 * @overflow:	Event overflow counter
 *
 * Walks through a sample-data-block and collects sampling data entries that are
 * then pushed to the perf event subsystem.  Depending on the sampling function,
 * there can be either basic-sampling or combined-sampling data entries.  A
 * combined-sampling data entry consists of a basic- and a diagnostic-sampling
 * data entry.	The sampling function is determined by the flags in the perf
 * event hardware structure.  The function always works with a combined-sampling
 * data entry but ignores the the diagnostic portion if it is not available.
 *
 * Note that the implementation focuses on basic-sampling data entries and, if
 * such an entry is not valid, the entire combined-sampling data entry is
 * ignored.
 *
 * The overflow variables counts the number of samples that has been discarded
 * due to a perf event overflow.
 */
static void hw_collect_samples(struct perf_event *event, unsigned long *sdbt,
			       unsigned long long *overflow)
{
	unsigned long flags = SAMPL_FLAGS(&event->hw);
	struct hws_combined_entry *sample;
	struct hws_trailer_entry *te;
	struct sf_raw_sample *sfr;
	size_t sample_size;

	/* Prepare and initialize raw sample data */
	sfr = (struct sf_raw_sample *) RAWSAMPLE_REG(&event->hw);
	sfr->format = flags & PERF_CPUM_SF_MODE_MASK;

	sample_size = event_sample_size(&event->hw);
	te = (struct hws_trailer_entry *) trailer_entry_ptr(*sdbt);
	sample = (struct hws_combined_entry *) *sdbt;
	while ((unsigned long *) sample < (unsigned long *) te) {
		/* Check for an empty sample */
		if (!sample->basic.def)
			break;

		/* Update perf event period */
		perf_event_count_update(event, SAMPL_RATE(&event->hw));

		/* Check sampling data entry */
		if (sample_format_is_valid(sample, flags)) {
			/* If an event overflow occurred, the PMU is stopped to
			 * throttle event delivery.  Remaining sample data is
			 * discarded.
			 */
			if (!*overflow) {
				if (sample_is_consistent(sample, flags)) {
					/* Deliver sample data to perf */
					sfr_store_sample(sfr, sample);
					*overflow = perf_push_sample(event, sfr);
				}
			} else
				/* Count discarded samples */
				*overflow += 1;
		} else {
			debug_sample_entry(sample, te, flags);
			/* Sample slot is not yet written or other record.
			 *
			 * This condition can occur if the buffer was reused
			 * from a combined basic- and diagnostic-sampling.
			 * If only basic-sampling is then active, entries are
			 * written into the larger diagnostic entries.
			 * This is typically the case for sample-data-blocks
			 * that are not full.  Stop processing if the first
			 * invalid format was detected.
			 */
			if (!te->f)
				break;
		}

		/* Reset sample slot and advance to next sample */
		reset_sample_slot(sample, flags);
		sample += sample_size;
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
 * to also walk through partially filled sample-data-blocks.  It is ignored
 * if PERF_CPUM_SF_FULL_BLOCKS is set.	The PERF_CPUM_SF_FULL_BLOCKS flag
 * enforces the processing of full sample-data-blocks only (trailer entries
 * with the block-full-indicator bit set).
 */
static void hw_perf_event_update(struct perf_event *event, int flush_all)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hws_trailer_entry *te;
	unsigned long *sdbt;
	unsigned long long event_overflow, sampl_overflow, num_sdb, te_flags;
	int done;

	/*
	 * AUX buffer is used when in diagnostic sampling mode.
	 * No perf events/samples are created.
	 */
	if (SAMPL_DIAG_MODE(&event->hw))
		return;

	if (flush_all && SDB_FULL_BLOCKS(hwc))
		flush_all = 0;

	sdbt = (unsigned long *) TEAR_REG(hwc);
	done = event_overflow = sampl_overflow = num_sdb = 0;
	while (!done) {
		/* Get the trailer entry of the sample-data-block */
		te = (struct hws_trailer_entry *) trailer_entry_ptr(*sdbt);

		/* Leave loop if no more work to do (block full indicator) */
		if (!te->f) {
			done = 1;
			if (!flush_all)
				break;
		}

		/* Check the sample overflow count */
		if (te->overflow)
			/* Account sample overflows and, if a particular limit
			 * is reached, extend the sampling buffer.
			 * For details, see sfb_account_overflows().
			 */
			sampl_overflow += te->overflow;

		/* Timestamps are valid for full sample-data-blocks only */
		debug_sprintf_event(sfdbg, 6, "hw_perf_event_update: sdbt=%p "
				    "overflow=%llu timestamp=0x%llx\n",
				    sdbt, te->overflow,
				    (te->f) ? trailer_timestamp(te) : 0ULL);

		/* Collect all samples from a single sample-data-block and
		 * flag if an (perf) event overflow happened.  If so, the PMU
		 * is stopped and remaining samples will be discarded.
		 */
		hw_collect_samples(event, sdbt, &event_overflow);
		num_sdb++;

		/* Reset trailer (using compare-double-and-swap) */
		do {
			te_flags = te->flags & ~SDB_TE_BUFFER_FULL_MASK;
			te_flags |= SDB_TE_ALERT_REQ_MASK;
		} while (!cmpxchg_double(&te->flags, &te->overflow,
					 te->flags, te->overflow,
					 te_flags, 0ULL));

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

	/* Account sample overflows in the event hardware structure */
	if (sampl_overflow)
		OVERFLOW_REG(hwc) = DIV_ROUND_UP(OVERFLOW_REG(hwc) +
						 sampl_overflow, 1 + num_sdb);
	if (sampl_overflow || event_overflow)
		debug_sprintf_event(sfdbg, 4, "hw_perf_event_update: "
				    "overflow stats: sample=%llu event=%llu\n",
				    sampl_overflow, event_overflow);
}

#define AUX_SDB_INDEX(aux, i) ((i) % aux->sfb.num_sdb)
#define AUX_SDB_NUM(aux, start, end) (end >= start ? end - start + 1 : 0)
#define AUX_SDB_NUM_ALERT(aux) AUX_SDB_NUM(aux, aux->head, aux->alert_mark)
#define AUX_SDB_NUM_EMPTY(aux) AUX_SDB_NUM(aux, aux->head, aux->empty_mark)

/*
 * Get trailer entry by index of SDB.
 */
static struct hws_trailer_entry *aux_sdb_trailer(struct aux_buffer *aux,
						 unsigned long index)
{
	unsigned long sdb;

	index = AUX_SDB_INDEX(aux, index);
	sdb = aux->sdb_index[index];
	return (struct hws_trailer_entry *)trailer_entry_ptr(sdb);
}

/*
 * Finish sampling on the cpu. Called by cpumsf_pmu_del() with pmu
 * disabled. Collect the full SDBs in AUX buffer which have not reached
 * the point of alert indicator. And ignore the SDBs which are not
 * full.
 *
 * 1. Scan SDBs to see how much data is there and consume them.
 * 2. Remove alert indicator in the buffer.
 */
static void aux_output_end(struct perf_output_handle *handle)
{
	unsigned long i, range_scan, idx;
	struct aux_buffer *aux;
	struct hws_trailer_entry *te;

	aux = perf_get_aux(handle);
	if (!aux)
		return;

	range_scan = AUX_SDB_NUM_ALERT(aux);
	for (i = 0, idx = aux->head; i < range_scan; i++, idx++) {
		te = aux_sdb_trailer(aux, idx);
		if (!(te->flags & SDB_TE_BUFFER_FULL_MASK))
			break;
	}
	/* i is num of SDBs which are full */
	perf_aux_output_end(handle, i << PAGE_SHIFT);

	/* Remove alert indicators in the buffer */
	te = aux_sdb_trailer(aux, aux->alert_mark);
	te->flags &= ~SDB_TE_ALERT_REQ_MASK;

	debug_sprintf_event(sfdbg, 6, "aux_output_end: collect %lx SDBs\n", i);
}

/*
 * Start sampling on the CPU. Called by cpumsf_pmu_add() when an event
 * is first added to the CPU or rescheduled again to the CPU. It is called
 * with pmu disabled.
 *
 * 1. Reset the trailer of SDBs to get ready for new data.
 * 2. Tell the hardware where to put the data by reset the SDBs buffer
 *    head(tear/dear).
 */
static int aux_output_begin(struct perf_output_handle *handle,
			    struct aux_buffer *aux,
			    struct cpu_hw_sf *cpuhw)
{
	unsigned long range;
	unsigned long i, range_scan, idx;
	unsigned long head, base, offset;
	struct hws_trailer_entry *te;

	if (WARN_ON_ONCE(handle->head & ~PAGE_MASK))
		return -EINVAL;

	aux->head = handle->head >> PAGE_SHIFT;
	range = (handle->size + 1) >> PAGE_SHIFT;
	if (range <= 1)
		return -ENOMEM;

	/*
	 * SDBs between aux->head and aux->empty_mark are already ready
	 * for new data. range_scan is num of SDBs not within them.
	 */
	if (range > AUX_SDB_NUM_EMPTY(aux)) {
		range_scan = range - AUX_SDB_NUM_EMPTY(aux);
		idx = aux->empty_mark + 1;
		for (i = 0; i < range_scan; i++, idx++) {
			te = aux_sdb_trailer(aux, idx);
			te->flags = te->flags & ~SDB_TE_BUFFER_FULL_MASK;
			te->flags = te->flags & ~SDB_TE_ALERT_REQ_MASK;
			te->overflow = 0;
		}
		/* Save the position of empty SDBs */
		aux->empty_mark = aux->head + range - 1;
	}

	/* Set alert indicator */
	aux->alert_mark = aux->head + range/2 - 1;
	te = aux_sdb_trailer(aux, aux->alert_mark);
	te->flags = te->flags | SDB_TE_ALERT_REQ_MASK;

	/* Reset hardware buffer head */
	head = AUX_SDB_INDEX(aux, aux->head);
	base = aux->sdbt_index[head / CPUM_SF_SDB_PER_TABLE];
	offset = head % CPUM_SF_SDB_PER_TABLE;
	cpuhw->lsctl.tear = base + offset * sizeof(unsigned long);
	cpuhw->lsctl.dear = aux->sdb_index[head];

	debug_sprintf_event(sfdbg, 6, "aux_output_begin: "
			    "head->alert_mark->empty_mark (num_alert, range)"
			    "[%lx -> %lx -> %lx] (%lx, %lx) "
			    "tear index %lx, tear %lx dear %lx\n",
			    aux->head, aux->alert_mark, aux->empty_mark,
			    AUX_SDB_NUM_ALERT(aux), range,
			    head / CPUM_SF_SDB_PER_TABLE,
			    cpuhw->lsctl.tear,
			    cpuhw->lsctl.dear);

	return 0;
}

/*
 * Set alert indicator on SDB at index @alert_index while sampler is running.
 *
 * Return true if successfully.
 * Return false if full indicator is already set by hardware sampler.
 */
static bool aux_set_alert(struct aux_buffer *aux, unsigned long alert_index,
			  unsigned long long *overflow)
{
	unsigned long long orig_overflow, orig_flags, new_flags;
	struct hws_trailer_entry *te;

	te = aux_sdb_trailer(aux, alert_index);
	do {
		orig_flags = te->flags;
		orig_overflow = te->overflow;
		*overflow = orig_overflow;
		if (orig_flags & SDB_TE_BUFFER_FULL_MASK) {
			/*
			 * SDB is already set by hardware.
			 * Abort and try to set somewhere
			 * behind.
			 */
			return false;
		}
		new_flags = orig_flags | SDB_TE_ALERT_REQ_MASK;
	} while (!cmpxchg_double(&te->flags, &te->overflow,
				 orig_flags, orig_overflow,
				 new_flags, 0ULL));
	return true;
}

/*
 * aux_reset_buffer() - Scan and setup SDBs for new samples
 * @aux:	The AUX buffer to set
 * @range:	The range of SDBs to scan started from aux->head
 * @overflow:	Set to overflow count
 *
 * Set alert indicator on the SDB at index of aux->alert_mark. If this SDB is
 * marked as empty, check if it is already set full by the hardware sampler.
 * If yes, that means new data is already there before we can set an alert
 * indicator. Caller should try to set alert indicator to some position behind.
 *
 * Scan the SDBs in AUX buffer from behind aux->empty_mark. They are used
 * previously and have already been consumed by user space. Reset these SDBs
 * (clear full indicator and alert indicator) for new data.
 * If aux->alert_mark fall in this area, just set it. Overflow count is
 * recorded while scanning.
 *
 * SDBs between aux->head and aux->empty_mark are already reset at last time.
 * and ready for new samples. So scanning on this area could be skipped.
 *
 * Return true if alert indicator is set successfully and false if not.
 */
static bool aux_reset_buffer(struct aux_buffer *aux, unsigned long range,
			     unsigned long long *overflow)
{
	unsigned long long orig_overflow, orig_flags, new_flags;
	unsigned long i, range_scan, idx;
	struct hws_trailer_entry *te;

	if (range <= AUX_SDB_NUM_EMPTY(aux))
		/*
		 * No need to scan. All SDBs in range are marked as empty.
		 * Just set alert indicator. Should check race with hardware
		 * sampler.
		 */
		return aux_set_alert(aux, aux->alert_mark, overflow);

	if (aux->alert_mark <= aux->empty_mark)
		/*
		 * Set alert indicator on empty SDB. Should check race
		 * with hardware sampler.
		 */
		if (!aux_set_alert(aux, aux->alert_mark, overflow))
			return false;

	/*
	 * Scan the SDBs to clear full and alert indicator used previously.
	 * Start scanning from one SDB behind empty_mark. If the new alert
	 * indicator fall into this range, set it.
	 */
	range_scan = range - AUX_SDB_NUM_EMPTY(aux);
	idx = aux->empty_mark + 1;
	for (i = 0; i < range_scan; i++, idx++) {
		te = aux_sdb_trailer(aux, idx);
		do {
			orig_flags = te->flags;
			orig_overflow = te->overflow;
			new_flags = orig_flags & ~SDB_TE_BUFFER_FULL_MASK;
			if (idx == aux->alert_mark)
				new_flags |= SDB_TE_ALERT_REQ_MASK;
			else
				new_flags &= ~SDB_TE_ALERT_REQ_MASK;
		} while (!cmpxchg_double(&te->flags, &te->overflow,
					 orig_flags, orig_overflow,
					 new_flags, 0ULL));
		*overflow += orig_overflow;
	}

	/* Update empty_mark to new position */
	aux->empty_mark = aux->head + range - 1;

	return true;
}

/*
 * Measurement alert handler for diagnostic mode sampling.
 */
static void hw_collect_aux(struct cpu_hw_sf *cpuhw)
{
	struct aux_buffer *aux;
	int done = 0;
	unsigned long range = 0, size;
	unsigned long long overflow = 0;
	struct perf_output_handle *handle = &cpuhw->handle;
	unsigned long num_sdb;

	aux = perf_get_aux(handle);
	if (WARN_ON_ONCE(!aux))
		return;

	/* Inform user space new data arrived */
	size = AUX_SDB_NUM_ALERT(aux) << PAGE_SHIFT;
	perf_aux_output_end(handle, size);
	num_sdb = aux->sfb.num_sdb;

	while (!done) {
		/* Get an output handle */
		aux = perf_aux_output_begin(handle, cpuhw->event);
		if (handle->size == 0) {
			pr_err("The AUX buffer with %lu pages for the "
			       "diagnostic-sampling mode is full\n",
				num_sdb);
			debug_sprintf_event(sfdbg, 1, "AUX buffer used up\n");
			break;
		}
		if (WARN_ON_ONCE(!aux))
			return;

		/* Update head and alert_mark to new position */
		aux->head = handle->head >> PAGE_SHIFT;
		range = (handle->size + 1) >> PAGE_SHIFT;
		if (range == 1)
			aux->alert_mark = aux->head;
		else
			aux->alert_mark = aux->head + range/2 - 1;

		if (aux_reset_buffer(aux, range, &overflow)) {
			if (!overflow) {
				done = 1;
				break;
			}
			size = range << PAGE_SHIFT;
			perf_aux_output_end(&cpuhw->handle, size);
			pr_err("Sample data caused the AUX buffer with %lu "
			       "pages to overflow\n", num_sdb);
			debug_sprintf_event(sfdbg, 1, "head %lx range %lx "
					    "overflow %llx\n",
					    aux->head, range, overflow);
		} else {
			size = AUX_SDB_NUM_ALERT(aux) << PAGE_SHIFT;
			perf_aux_output_end(&cpuhw->handle, size);
			debug_sprintf_event(sfdbg, 6, "head %lx alert %lx "
					    "already full, try another\n",
					    aux->head, aux->alert_mark);
		}
	}

	if (done)
		debug_sprintf_event(sfdbg, 6, "aux_reset_buffer: "
				    "[%lx -> %lx -> %lx] (%lx, %lx)\n",
				    aux->head, aux->alert_mark, aux->empty_mark,
				    AUX_SDB_NUM_ALERT(aux), range);
}

/*
 * Callback when freeing AUX buffers.
 */
static void aux_buffer_free(void *data)
{
	struct aux_buffer *aux = data;
	unsigned long i, num_sdbt;

	if (!aux)
		return;

	/* Free SDBT. SDB is freed by the caller */
	num_sdbt = aux->sfb.num_sdbt;
	for (i = 0; i < num_sdbt; i++)
		free_page(aux->sdbt_index[i]);

	kfree(aux->sdbt_index);
	kfree(aux->sdb_index);
	kfree(aux);

	debug_sprintf_event(sfdbg, 4, "aux_buffer_free: free "
			    "%lu SDBTs\n", num_sdbt);
}

/*
 * aux_buffer_setup() - Setup AUX buffer for diagnostic mode sampling
 * @cpu:	On which to allocate, -1 means current
 * @pages:	Array of pointers to buffer pages passed from perf core
 * @nr_pages:	Total pages
 * @snapshot:	Flag for snapshot mode
 *
 * This is the callback when setup an event using AUX buffer. Perf tool can
 * trigger this by an additional mmap() call on the event. Unlike the buffer
 * for basic samples, AUX buffer belongs to the event. It is scheduled with
 * the task among online cpus when it is a per-thread event.
 *
 * Return the private AUX buffer structure if success or NULL if fails.
 */
static void *aux_buffer_setup(int cpu, void **pages, int nr_pages,
			      bool snapshot)
{
	struct sf_buffer *sfb;
	struct aux_buffer *aux;
	unsigned long *new, *tail;
	int i, n_sdbt;

	if (!nr_pages || !pages)
		return NULL;

	if (nr_pages > CPUM_SF_MAX_SDB * CPUM_SF_SDB_DIAG_FACTOR) {
		pr_err("AUX buffer size (%i pages) is larger than the "
		       "maximum sampling buffer limit\n",
		       nr_pages);
		return NULL;
	} else if (nr_pages < CPUM_SF_MIN_SDB * CPUM_SF_SDB_DIAG_FACTOR) {
		pr_err("AUX buffer size (%i pages) is less than the "
		       "minimum sampling buffer limit\n",
		       nr_pages);
		return NULL;
	}

	/* Allocate aux_buffer struct for the event */
	aux = kmalloc(sizeof(struct aux_buffer), GFP_KERNEL);
	if (!aux)
		goto no_aux;
	sfb = &aux->sfb;

	/* Allocate sdbt_index for fast reference */
	n_sdbt = (nr_pages + CPUM_SF_SDB_PER_TABLE - 1) / CPUM_SF_SDB_PER_TABLE;
	aux->sdbt_index = kmalloc_array(n_sdbt, sizeof(void *), GFP_KERNEL);
	if (!aux->sdbt_index)
		goto no_sdbt_index;

	/* Allocate sdb_index for fast reference */
	aux->sdb_index = kmalloc_array(nr_pages, sizeof(void *), GFP_KERNEL);
	if (!aux->sdb_index)
		goto no_sdb_index;

	/* Allocate the first SDBT */
	sfb->num_sdbt = 0;
	sfb->sdbt = (unsigned long *) get_zeroed_page(GFP_KERNEL);
	if (!sfb->sdbt)
		goto no_sdbt;
	aux->sdbt_index[sfb->num_sdbt++] = (unsigned long)sfb->sdbt;
	tail = sfb->tail = sfb->sdbt;

	/*
	 * Link the provided pages of AUX buffer to SDBT.
	 * Allocate SDBT if needed.
	 */
	for (i = 0; i < nr_pages; i++, tail++) {
		if (require_table_link(tail)) {
			new = (unsigned long *) get_zeroed_page(GFP_KERNEL);
			if (!new)
				goto no_sdbt;
			aux->sdbt_index[sfb->num_sdbt++] = (unsigned long)new;
			/* Link current page to tail of chain */
			*tail = (unsigned long)(void *) new + 1;
			tail = new;
		}
		/* Tail is the entry in a SDBT */
		*tail = (unsigned long)pages[i];
		aux->sdb_index[i] = (unsigned long)pages[i];
	}
	sfb->num_sdb = nr_pages;

	/* Link the last entry in the SDBT to the first SDBT */
	*tail = (unsigned long) sfb->sdbt + 1;
	sfb->tail = tail;

	/*
	 * Initial all SDBs are zeroed. Mark it as empty.
	 * So there is no need to clear the full indicator
	 * when this event is first added.
	 */
	aux->empty_mark = sfb->num_sdb - 1;

	debug_sprintf_event(sfdbg, 4, "aux_buffer_setup: setup %lu SDBTs"
			    " and %lu SDBs\n",
			    sfb->num_sdbt, sfb->num_sdb);

	return aux;

no_sdbt:
	/* SDBs (AUX buffer pages) are freed by caller */
	for (i = 0; i < sfb->num_sdbt; i++)
		free_page(aux->sdbt_index[i]);
	kfree(aux->sdb_index);
no_sdb_index:
	kfree(aux->sdbt_index);
no_sdbt_index:
	kfree(aux);
no_aux:
	return NULL;
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
	struct cpu_hw_sf *cpuhw = this_cpu_ptr(&cpu_hw_sf);

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

	perf_pmu_disable(event->pmu);
	event->hw.state = 0;
	cpuhw->lsctl.cs = 1;
	if (SAMPL_DIAG_MODE(&event->hw))
		cpuhw->lsctl.cd = 1;
	perf_pmu_enable(event->pmu);
}

/* Deactivate sampling control.
 * Next call of pmu_enable() stops sampling.
 */
static void cpumsf_pmu_stop(struct perf_event *event, int flags)
{
	struct cpu_hw_sf *cpuhw = this_cpu_ptr(&cpu_hw_sf);

	if (event->hw.state & PERF_HES_STOPPED)
		return;

	perf_pmu_disable(event->pmu);
	cpuhw->lsctl.cs = 0;
	cpuhw->lsctl.cd = 0;
	event->hw.state |= PERF_HES_STOPPED;

	if ((flags & PERF_EF_UPDATE) && !(event->hw.state & PERF_HES_UPTODATE)) {
		hw_perf_event_update(event, 1);
		event->hw.state |= PERF_HES_UPTODATE;
	}
	perf_pmu_enable(event->pmu);
}

static int cpumsf_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_sf *cpuhw = this_cpu_ptr(&cpu_hw_sf);
	struct aux_buffer *aux;
	int err;

	if (cpuhw->flags & PMU_F_IN_USE)
		return -EAGAIN;

	if (!SAMPL_DIAG_MODE(&event->hw) && !cpuhw->sfb.sdbt)
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
	cpuhw->lsctl.interval = SAMPL_RATE(&event->hw);
	if (!SAMPL_DIAG_MODE(&event->hw)) {
		cpuhw->lsctl.tear = (unsigned long) cpuhw->sfb.sdbt;
		cpuhw->lsctl.dear = *(unsigned long *) cpuhw->sfb.sdbt;
		hw_reset_registers(&event->hw, cpuhw->sfb.sdbt);
	}

	/* Ensure sampling functions are in the disabled state.  If disabled,
	 * switch on sampling enable control. */
	if (WARN_ON_ONCE(cpuhw->lsctl.es == 1 || cpuhw->lsctl.ed == 1)) {
		err = -EAGAIN;
		goto out;
	}
	if (SAMPL_DIAG_MODE(&event->hw)) {
		aux = perf_aux_output_begin(&cpuhw->handle, event);
		if (!aux) {
			err = -EINVAL;
			goto out;
		}
		err = aux_output_begin(&cpuhw->handle, aux, cpuhw);
		if (err)
			goto out;
		cpuhw->lsctl.ed = 1;
	}
	cpuhw->lsctl.es = 1;

	/* Set in_use flag and store event */
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
	struct cpu_hw_sf *cpuhw = this_cpu_ptr(&cpu_hw_sf);

	perf_pmu_disable(event->pmu);
	cpumsf_pmu_stop(event, PERF_EF_UPDATE);

	cpuhw->lsctl.es = 0;
	cpuhw->lsctl.ed = 0;
	cpuhw->flags &= ~PMU_F_IN_USE;
	cpuhw->event = NULL;

	if (SAMPL_DIAG_MODE(&event->hw))
		aux_output_end(&cpuhw->handle);
	perf_event_update_userpage(event);
	perf_pmu_enable(event->pmu);
}

CPUMF_EVENT_ATTR(SF, SF_CYCLES_BASIC, PERF_EVENT_CPUM_SF);
CPUMF_EVENT_ATTR(SF, SF_CYCLES_BASIC_DIAG, PERF_EVENT_CPUM_SF_DIAG);

static struct attribute *cpumsf_pmu_events_attr[] = {
	CPUMF_EVENT_PTR(SF, SF_CYCLES_BASIC),
	NULL,
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

	.attr_groups  = cpumsf_pmu_attr_groups,

	.setup_aux    = aux_buffer_setup,
	.free_aux     = aux_buffer_free,
};

static void cpumf_measurement_alert(struct ext_code ext_code,
				    unsigned int alert, unsigned long unused)
{
	struct cpu_hw_sf *cpuhw;

	if (!(alert & CPU_MF_INT_SF_MASK))
		return;
	inc_irq_stat(IRQEXT_CMS);
	cpuhw = this_cpu_ptr(&cpu_hw_sf);

	/* Measurement alerts are shared and might happen when the PMU
	 * is not reserved.  Ignore these alerts in this case. */
	if (!(cpuhw->flags & PMU_F_RESERVED))
		return;

	/* The processing below must take care of multiple alert events that
	 * might be indicated concurrently. */

	/* Program alert request */
	if (alert & CPU_MF_INT_SF_PRA) {
		if (cpuhw->flags & PMU_F_IN_USE)
			if (SAMPL_DIAG_MODE(&cpuhw->event->hw))
				hw_collect_aux(cpuhw);
			else
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
static int cpusf_pmu_setup(unsigned int cpu, int flags)
{
	/* Ignore the notification if no events are scheduled on the PMU.
	 * This might be racy...
	 */
	if (!atomic_read(&num_events))
		return 0;

	local_irq_disable();
	setup_pmc_cpu(&flags);
	local_irq_enable();
	return 0;
}

static int s390_pmu_sf_online_cpu(unsigned int cpu)
{
	return cpusf_pmu_setup(cpu, PMC_INIT);
}

static int s390_pmu_sf_offline_cpu(unsigned int cpu)
{
	return cpusf_pmu_setup(cpu, PMC_RELEASE);
}

static int param_get_sfb_size(char *buffer, const struct kernel_param *kp)
{
	if (!cpum_sf_avail())
		return -ENODEV;
	return sprintf(buffer, "%lu,%lu", CPUM_SF_MIN_SDB, CPUM_SF_MAX_SDB);
}

static int param_set_sfb_size(const char *val, const struct kernel_param *kp)
{
	int rc;
	unsigned long min, max;

	if (!cpum_sf_avail())
		return -ENODEV;
	if (!val || !strlen(val))
		return -EINVAL;

	/* Valid parameter values: "min,max" or "max" */
	min = CPUM_SF_MIN_SDB;
	max = CPUM_SF_MAX_SDB;
	if (strchr(val, ','))
		rc = (sscanf(val, "%lu,%lu", &min, &max) == 2) ? 0 : -EINVAL;
	else
		rc = kstrtoul(val, 10, &max);

	if (min < 2 || min >= max || max > get_num_physpages())
		rc = -EINVAL;
	if (rc)
		return rc;

	sfb_set_limits(min, max);
	pr_info("The sampling buffer limits have changed to: "
		"min=%lu max=%lu (diag=x%lu)\n",
		CPUM_SF_MIN_SDB, CPUM_SF_MAX_SDB, CPUM_SF_SDB_DIAG_FACTOR);
	return 0;
}

#define param_check_sfb_size(name, p) __param_check(name, p, void)
static const struct kernel_param_ops param_ops_sfb_size = {
	.set = param_set_sfb_size,
	.get = param_get_sfb_size,
};

#define RS_INIT_FAILURE_QSI	  0x0001
#define RS_INIT_FAILURE_BSDES	  0x0002
#define RS_INIT_FAILURE_ALRT	  0x0003
#define RS_INIT_FAILURE_PERF	  0x0004
static void __init pr_cpumsf_err(unsigned int reason)
{
	pr_err("Sampling facility support for perf is not available: "
	       "reason=%04x\n", reason);
}

static int __init init_cpum_sampling_pmu(void)
{
	struct hws_qsi_info_block si;
	int err;

	if (!cpum_sf_avail())
		return -ENODEV;

	memset(&si, 0, sizeof(si));
	if (qsi(&si)) {
		pr_cpumsf_err(RS_INIT_FAILURE_QSI);
		return -ENODEV;
	}

	if (si.bsdes != sizeof(struct hws_basic_entry)) {
		pr_cpumsf_err(RS_INIT_FAILURE_BSDES);
		return -EINVAL;
	}

	if (si.ad) {
		sfb_set_limits(CPUM_SF_MIN_SDB, CPUM_SF_MAX_SDB);
		cpumsf_pmu_events_attr[1] =
			CPUMF_EVENT_PTR(SF, SF_CYCLES_BASIC_DIAG);
	}

	sfdbg = debug_register(KMSG_COMPONENT, 2, 1, 80);
	if (!sfdbg)
		pr_err("Registering for s390dbf failed\n");
	debug_register_view(sfdbg, &debug_sprintf_view);

	err = register_external_irq(EXT_IRQ_MEASURE_ALERT,
				    cpumf_measurement_alert);
	if (err) {
		pr_cpumsf_err(RS_INIT_FAILURE_ALRT);
		goto out;
	}

	err = perf_pmu_register(&cpumf_sampling, "cpum_sf", PERF_TYPE_RAW);
	if (err) {
		pr_cpumsf_err(RS_INIT_FAILURE_PERF);
		unregister_external_irq(EXT_IRQ_MEASURE_ALERT,
					cpumf_measurement_alert);
		goto out;
	}

	cpuhp_setup_state(CPUHP_AP_PERF_S390_SF_ONLINE, "perf/s390/sf:online",
			  s390_pmu_sf_online_cpu, s390_pmu_sf_offline_cpu);
out:
	return err;
}
arch_initcall(init_cpum_sampling_pmu);
core_param(cpum_sfb_size, CPUM_SF_MAX_SDB, sfb_size, 0640);
