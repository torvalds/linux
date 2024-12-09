// SPDX-License-Identifier: GPL-2.0
/*
 * Performance event support - Processor Activity Instrumentation Facility
 *
 *  Copyright IBM Corp. 2022
 *  Author(s): Thomas Richter <tmricht@linux.ibm.com>
 */
#define KMSG_COMPONENT	"pai_crypto"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/perf_event.h>
#include <asm/ctlreg.h>
#include <asm/pai.h>
#include <asm/debug.h>

static debug_info_t *cfm_dbg;
static unsigned int paicrypt_cnt;	/* Size of the mapped counter sets */
					/* extracted with QPACI instruction */

DEFINE_STATIC_KEY_FALSE(pai_key);

struct pai_userdata {
	u16 num;
	u64 value;
} __packed;

struct paicrypt_map {
	unsigned long *page;		/* Page for CPU to store counters */
	struct pai_userdata *save;	/* Page to store no-zero counters */
	unsigned int active_events;	/* # of PAI crypto users */
	refcount_t refcnt;		/* Reference count mapped buffers */
	struct perf_event *event;	/* Perf event for sampling */
	struct list_head syswide_list;	/* List system-wide sampling events */
};

struct paicrypt_mapptr {
	struct paicrypt_map *mapptr;
};

static struct paicrypt_root {		/* Anchor to per CPU data */
	refcount_t refcnt;		/* Overall active events */
	struct paicrypt_mapptr __percpu *mapptr;
} paicrypt_root;

/* Free per CPU data when the last event is removed. */
static void paicrypt_root_free(void)
{
	if (refcount_dec_and_test(&paicrypt_root.refcnt)) {
		free_percpu(paicrypt_root.mapptr);
		paicrypt_root.mapptr = NULL;
	}
	debug_sprintf_event(cfm_dbg, 5, "%s root.refcount %d\n", __func__,
			    refcount_read(&paicrypt_root.refcnt));
}

/*
 * On initialization of first event also allocate per CPU data dynamically.
 * Start with an array of pointers, the array size is the maximum number of
 * CPUs possible, which might be larger than the number of CPUs currently
 * online.
 */
static int paicrypt_root_alloc(void)
{
	if (!refcount_inc_not_zero(&paicrypt_root.refcnt)) {
		/* The memory is already zeroed. */
		paicrypt_root.mapptr = alloc_percpu(struct paicrypt_mapptr);
		if (!paicrypt_root.mapptr)
			return -ENOMEM;
		refcount_set(&paicrypt_root.refcnt, 1);
	}
	return 0;
}

/* Release the PMU if event is the last perf event */
static DEFINE_MUTEX(pai_reserve_mutex);

/* Adjust usage counters and remove allocated memory when all users are
 * gone.
 */
static void paicrypt_event_destroy_cpu(struct perf_event *event, int cpu)
{
	struct paicrypt_mapptr *mp = per_cpu_ptr(paicrypt_root.mapptr, cpu);
	struct paicrypt_map *cpump = mp->mapptr;

	mutex_lock(&pai_reserve_mutex);
	debug_sprintf_event(cfm_dbg, 5, "%s event %#llx cpu %d users %d "
			    "refcnt %u\n", __func__, event->attr.config,
			    event->cpu, cpump->active_events,
			    refcount_read(&cpump->refcnt));
	if (refcount_dec_and_test(&cpump->refcnt)) {
		debug_sprintf_event(cfm_dbg, 4, "%s page %#lx save %p\n",
				    __func__, (unsigned long)cpump->page,
				    cpump->save);
		free_page((unsigned long)cpump->page);
		kvfree(cpump->save);
		kfree(cpump);
		mp->mapptr = NULL;
	}
	paicrypt_root_free();
	mutex_unlock(&pai_reserve_mutex);
}

static void paicrypt_event_destroy(struct perf_event *event)
{
	int cpu;

	static_branch_dec(&pai_key);
	free_page(PAI_SAVE_AREA(event));
	if (event->cpu == -1) {
		struct cpumask *mask = PAI_CPU_MASK(event);

		for_each_cpu(cpu, mask)
			paicrypt_event_destroy_cpu(event, cpu);
		kfree(mask);
	} else {
		paicrypt_event_destroy_cpu(event, event->cpu);
	}
}

static u64 paicrypt_getctr(unsigned long *page, int nr, bool kernel)
{
	if (kernel)
		nr += PAI_CRYPTO_MAXCTR;
	return page[nr];
}

/* Read the counter values. Return value from location in CMP. For event
 * CRYPTO_ALL sum up all events.
 */
static u64 paicrypt_getdata(struct perf_event *event, bool kernel)
{
	struct paicrypt_mapptr *mp = this_cpu_ptr(paicrypt_root.mapptr);
	struct paicrypt_map *cpump = mp->mapptr;
	u64 sum = 0;
	int i;

	if (event->attr.config != PAI_CRYPTO_BASE) {
		return paicrypt_getctr(cpump->page,
				       event->attr.config - PAI_CRYPTO_BASE,
				       kernel);
	}

	for (i = 1; i <= paicrypt_cnt; i++) {
		u64 val = paicrypt_getctr(cpump->page, i, kernel);

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
		sum += paicrypt_getdata(event, true);
	if (!event->attr.exclude_user)
		sum += paicrypt_getdata(event, false);

	return sum;
}

/* Check concurrent access of counting and sampling for crypto events.
 * This function is called in process context and it is save to block.
 * When the event initialization functions fails, no other call back will
 * be invoked.
 *
 * Allocate the memory for the event.
 */
static struct paicrypt_map *paicrypt_busy(struct perf_event *event, int cpu)
{
	struct paicrypt_map *cpump = NULL;
	struct paicrypt_mapptr *mp;
	int rc;

	mutex_lock(&pai_reserve_mutex);

	/* Allocate root node */
	rc = paicrypt_root_alloc();
	if (rc)
		goto unlock;

	/* Allocate node for this event */
	mp = per_cpu_ptr(paicrypt_root.mapptr, cpu);
	cpump = mp->mapptr;
	if (!cpump) {			/* Paicrypt_map allocated? */
		cpump = kzalloc(sizeof(*cpump), GFP_KERNEL);
		if (!cpump) {
			rc = -ENOMEM;
			goto free_root;
		}
		INIT_LIST_HEAD(&cpump->syswide_list);
	}

	/* Allocate memory for counter page and counter extraction.
	 * Only the first counting event has to allocate a page.
	 */
	if (cpump->page) {
		refcount_inc(&cpump->refcnt);
		goto unlock;
	}

	rc = -ENOMEM;
	cpump->page = (unsigned long *)get_zeroed_page(GFP_KERNEL);
	if (!cpump->page)
		goto free_paicrypt_map;
	cpump->save = kvmalloc_array(paicrypt_cnt + 1,
				     sizeof(struct pai_userdata), GFP_KERNEL);
	if (!cpump->save) {
		free_page((unsigned long)cpump->page);
		cpump->page = NULL;
		goto free_paicrypt_map;
	}

	/* Set mode and reference count */
	rc = 0;
	refcount_set(&cpump->refcnt, 1);
	mp->mapptr = cpump;
	debug_sprintf_event(cfm_dbg, 5, "%s users %d refcnt %u page %#lx "
			    "save %p rc %d\n", __func__, cpump->active_events,
			    refcount_read(&cpump->refcnt),
			    (unsigned long)cpump->page, cpump->save, rc);
	goto unlock;

free_paicrypt_map:
	/* Undo memory allocation */
	kfree(cpump);
	mp->mapptr = NULL;
free_root:
	paicrypt_root_free();
unlock:
	mutex_unlock(&pai_reserve_mutex);
	return rc ? ERR_PTR(rc) : cpump;
}

static int paicrypt_event_init_all(struct perf_event *event)
{
	struct paicrypt_map *cpump;
	struct cpumask *maskptr;
	int cpu, rc = -ENOMEM;

	maskptr = kzalloc(sizeof(*maskptr), GFP_KERNEL);
	if (!maskptr)
		goto out;

	for_each_online_cpu(cpu) {
		cpump = paicrypt_busy(event, cpu);
		if (IS_ERR(cpump)) {
			for_each_cpu(cpu, maskptr)
				paicrypt_event_destroy_cpu(event, cpu);
			kfree(maskptr);
			rc = PTR_ERR(cpump);
			goto out;
		}
		cpumask_set_cpu(cpu, maskptr);
	}

	/*
	 * On error all cpumask are freed and all events have been destroyed.
	 * Save of which CPUs data structures have been allocated for.
	 * Release them in paicrypt_event_destroy call back function
	 * for this event.
	 */
	PAI_CPU_MASK(event) = maskptr;
	rc = 0;
out:
	return rc;
}

/* Might be called on different CPU than the one the event is intended for. */
static int paicrypt_event_init(struct perf_event *event)
{
	struct perf_event_attr *a = &event->attr;
	struct paicrypt_map *cpump;
	int rc = 0;

	/* PAI crypto PMU registered as PERF_TYPE_RAW, check event type */
	if (a->type != PERF_TYPE_RAW && event->pmu->type != a->type)
		return -ENOENT;
	/* PAI crypto event must be in valid range */
	if (a->config < PAI_CRYPTO_BASE ||
	    a->config > PAI_CRYPTO_BASE + paicrypt_cnt)
		return -EINVAL;
	/* Allow only CRYPTO_ALL for sampling */
	if (a->sample_period && a->config != PAI_CRYPTO_BASE)
		return -EINVAL;
	/* Get a page to store last counter values for sampling */
	if (a->sample_period) {
		PAI_SAVE_AREA(event) = get_zeroed_page(GFP_KERNEL);
		if (!PAI_SAVE_AREA(event)) {
			rc = -ENOMEM;
			goto out;
		}
	}

	if (event->cpu >= 0) {
		cpump = paicrypt_busy(event, event->cpu);
		if (IS_ERR(cpump))
			rc = PTR_ERR(cpump);
	} else {
		rc = paicrypt_event_init_all(event);
	}
	if (rc) {
		free_page(PAI_SAVE_AREA(event));
		goto out;
	}
	event->destroy = paicrypt_event_destroy;

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

	static_branch_inc(&pai_key);
out:
	return rc;
}

static void paicrypt_read(struct perf_event *event)
{
	u64 prev, new, delta;

	prev = local64_read(&event->hw.prev_count);
	new = paicrypt_getall(event);
	local64_set(&event->hw.prev_count, new);
	delta = (prev <= new) ? new - prev
			      : (-1ULL - prev) + new + 1;	 /* overflow */
	local64_add(delta, &event->count);
}

static void paicrypt_start(struct perf_event *event, int flags)
{
	struct paicrypt_mapptr *mp = this_cpu_ptr(paicrypt_root.mapptr);
	struct paicrypt_map *cpump = mp->mapptr;
	u64 sum;

	if (!event->attr.sample_period) {	/* Counting */
		sum = paicrypt_getall(event);	/* Get current value */
		local64_set(&event->hw.prev_count, sum);
	} else {				/* Sampling */
		memcpy((void *)PAI_SAVE_AREA(event), cpump->page, PAGE_SIZE);
		/* Enable context switch callback for system-wide sampling */
		if (!(event->attach_state & PERF_ATTACH_TASK)) {
			list_add_tail(PAI_SWLIST(event), &cpump->syswide_list);
			perf_sched_cb_inc(event->pmu);
		} else {
			cpump->event = event;
		}
	}
}

static int paicrypt_add(struct perf_event *event, int flags)
{
	struct paicrypt_mapptr *mp = this_cpu_ptr(paicrypt_root.mapptr);
	struct paicrypt_map *cpump = mp->mapptr;
	unsigned long ccd;

	if (++cpump->active_events == 1) {
		ccd = virt_to_phys(cpump->page) | PAI_CRYPTO_KERNEL_OFFSET;
		WRITE_ONCE(get_lowcore()->ccd, ccd);
		local_ctl_set_bit(0, CR0_CRYPTOGRAPHY_COUNTER_BIT);
	}
	if (flags & PERF_EF_START)
		paicrypt_start(event, PERF_EF_RELOAD);
	event->hw.state = 0;
	return 0;
}

static void paicrypt_have_sample(struct perf_event *, struct paicrypt_map *);
static void paicrypt_stop(struct perf_event *event, int flags)
{
	struct paicrypt_mapptr *mp = this_cpu_ptr(paicrypt_root.mapptr);
	struct paicrypt_map *cpump = mp->mapptr;

	if (!event->attr.sample_period) {	/* Counting */
		paicrypt_read(event);
	} else {				/* Sampling */
		if (!(event->attach_state & PERF_ATTACH_TASK)) {
			perf_sched_cb_dec(event->pmu);
			list_del(PAI_SWLIST(event));
		} else {
			paicrypt_have_sample(event, cpump);
			cpump->event = NULL;
		}
	}
	event->hw.state = PERF_HES_STOPPED;
}

static void paicrypt_del(struct perf_event *event, int flags)
{
	struct paicrypt_mapptr *mp = this_cpu_ptr(paicrypt_root.mapptr);
	struct paicrypt_map *cpump = mp->mapptr;

	paicrypt_stop(event, PERF_EF_UPDATE);
	if (--cpump->active_events == 0) {
		local_ctl_clear_bit(0, CR0_CRYPTOGRAPHY_COUNTER_BIT);
		WRITE_ONCE(get_lowcore()->ccd, 0);
	}
}

/* Create raw data and save it in buffer. Calculate the delta for each
 * counter between this invocation and the last invocation.
 * Returns number of bytes copied.
 * Saves only entries with positive counter difference of the form
 * 2 bytes: Number of counter
 * 8 bytes: Value of counter
 */
static size_t paicrypt_copy(struct pai_userdata *userdata, unsigned long *page,
			    unsigned long *page_old, bool exclude_user,
			    bool exclude_kernel)
{
	int i, outidx = 0;

	for (i = 1; i <= paicrypt_cnt; i++) {
		u64 val = 0, val_old = 0;

		if (!exclude_kernel) {
			val += paicrypt_getctr(page, i, true);
			val_old += paicrypt_getctr(page_old, i, true);
		}
		if (!exclude_user) {
			val += paicrypt_getctr(page, i, false);
			val_old += paicrypt_getctr(page_old, i, false);
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
	return outidx * sizeof(struct pai_userdata);
}

static int paicrypt_push_sample(size_t rawsize, struct paicrypt_map *cpump,
				struct perf_event *event)
{
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
		perf_sample_save_raw_data(&data, &raw);
	}

	overflow = perf_event_overflow(event, &data, &regs);
	perf_event_update_userpage(event);
	/* Save crypto counter lowcore page after reading event data. */
	memcpy((void *)PAI_SAVE_AREA(event), cpump->page, PAGE_SIZE);
	return overflow;
}

/* Check if there is data to be saved on schedule out of a task. */
static void paicrypt_have_sample(struct perf_event *event,
				 struct paicrypt_map *cpump)
{
	size_t rawsize;

	if (!event)		/* No event active */
		return;
	rawsize = paicrypt_copy(cpump->save, cpump->page,
				(unsigned long *)PAI_SAVE_AREA(event),
				event->attr.exclude_user,
				event->attr.exclude_kernel);
	if (rawsize)			/* No incremented counters */
		paicrypt_push_sample(rawsize, cpump, event);
}

/* Check if there is data to be saved on schedule out of a task. */
static void paicrypt_have_samples(void)
{
	struct paicrypt_mapptr *mp = this_cpu_ptr(paicrypt_root.mapptr);
	struct paicrypt_map *cpump = mp->mapptr;
	struct perf_event *event;

	list_for_each_entry(event, &cpump->syswide_list, hw.tp_list)
		paicrypt_have_sample(event, cpump);
}

/* Called on schedule-in and schedule-out. No access to event structure,
 * but for sampling only event CRYPTO_ALL is allowed.
 */
static void paicrypt_sched_task(struct perf_event_pmu_context *pmu_ctx, bool sched_in)
{
	/* We started with a clean page on event installation. So read out
	 * results on schedule_out and if page was dirty, save old values.
	 */
	if (!sched_in)
		paicrypt_have_samples();
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
	[114] = "PCC_COMPUTE_LAST_BLOCK_CMAC_USING_ENCRYPTED_AES_256A",
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

static void __init attr_event_free(struct attribute **attrs, int num)
{
	struct perf_pmu_events_attr *pa;
	int i;

	for (i = 0; i < num; i++) {
		struct device_attribute *dap;

		dap = container_of(attrs[i], struct device_attribute, attr);
		pa = container_of(dap, struct perf_pmu_events_attr, attr);
		kfree(pa);
	}
	kfree(attrs);
}

static int __init attr_event_init_one(struct attribute **attrs, int num)
{
	struct perf_pmu_events_attr *pa;

	/* Index larger than array_size, no counter name available */
	if (num >= ARRAY_SIZE(paicrypt_ctrnames)) {
		attrs[num] = NULL;
		return 0;
	}

	pa = kzalloc(sizeof(*pa), GFP_KERNEL);
	if (!pa)
		return -ENOMEM;

	sysfs_attr_init(&pa->attr.attr);
	pa->id = PAI_CRYPTO_BASE + num;
	pa->attr.attr.name = paicrypt_ctrnames[num];
	pa->attr.attr.mode = 0444;
	pa->attr.show = cpumf_events_sysfs_show;
	pa->attr.store = NULL;
	attrs[num] = &pa->attr.attr;
	return 0;
}

/* Create PMU sysfs event attributes on the fly. */
static int __init attr_event_init(void)
{
	struct attribute **attrs;
	int ret, i;

	attrs = kmalloc_array(paicrypt_cnt + 2, sizeof(*attrs), GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;
	for (i = 0; i <= paicrypt_cnt; i++) {
		ret = attr_event_init_one(attrs, i);
		if (ret) {
			attr_event_free(attrs, i);
			return ret;
		}
	}
	attrs[i] = NULL;
	paicrypt_events_group.attrs = attrs;
	return 0;
}

static int __init paicrypt_init(void)
{
	struct qpaci_info_block ib;
	int rc;

	if (!test_facility(196))
		return 0;

	qpaci(&ib);
	paicrypt_cnt = ib.num_cc;
	if (paicrypt_cnt == 0)
		return 0;
	if (paicrypt_cnt >= PAI_CRYPTO_MAXCTR) {
		pr_err("Too many PMU pai_crypto counters %d\n", paicrypt_cnt);
		return -E2BIG;
	}

	rc = attr_event_init();		/* Export known PAI crypto events */
	if (rc) {
		pr_err("Creation of PMU pai_crypto /sysfs failed\n");
		return rc;
	}

	/* Setup s390dbf facility */
	cfm_dbg = debug_register(KMSG_COMPONENT, 2, 256, 128);
	if (!cfm_dbg) {
		pr_err("Registration of s390dbf pai_crypto failed\n");
		return -ENOMEM;
	}
	debug_register_view(cfm_dbg, &debug_sprintf_view);

	rc = perf_pmu_register(&paicrypt, "pai_crypto", -1);
	if (rc) {
		pr_err("Registering the pai_crypto PMU failed with rc=%i\n",
		       rc);
		debug_unregister_view(cfm_dbg, &debug_sprintf_view);
		debug_unregister(cfm_dbg);
		return rc;
	}
	return 0;
}

device_initcall(paicrypt_init);
