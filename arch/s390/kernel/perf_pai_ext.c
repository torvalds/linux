// SPDX-License-Identifier: GPL-2.0
/*
 * Performance event support - Processor Activity Instrumentation Extension
 * Facility
 *
 *  Copyright IBM Corp. 2022
 *  Author(s): Thomas Richter <tmricht@linux.ibm.com>
 */
#define KMSG_COMPONENT	"pai_ext"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/io.h>

#include <asm/cpu_mcf.h>
#include <asm/ctl_reg.h>
#include <asm/pai.h>
#include <asm/debug.h>

#define	PAIE1_CB_SZ		0x200	/* Size of PAIE1 control block */
#define	PAIE1_CTRBLOCK_SZ	0x400	/* Size of PAIE1 counter blocks */

static debug_info_t *paiext_dbg;
static unsigned int paiext_cnt;	/* Extracted with QPACI instruction */

enum paiext_mode {
	PAI_MODE_NONE,
	PAI_MODE_SAMPLING,
	PAI_MODE_COUNTER,
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
	u8 reserved2[488];
} __packed;

struct paiext_map {
	unsigned long *area;		/* Area for CPU to store counters */
	struct pai_userdata *save;	/* Area to store non-zero counters */
	enum paiext_mode mode;		/* Type of event */
	unsigned int active_events;	/* # of PAI Extension users */
	unsigned int refcnt;
	struct perf_event *event;	/* Perf event for sampling */
	struct paiext_cb *paiext_cb;	/* PAI extension control block area */
};

struct paiext_mapptr {
	struct paiext_map *mapptr;
};

static struct paiext_root {		/* Anchor to per CPU data */
	int refcnt;			/* Overall active events */
	struct paiext_mapptr __percpu *mapptr;
} paiext_root;

/* Free per CPU data when the last event is removed. */
static void paiext_root_free(void)
{
	if (!--paiext_root.refcnt) {
		free_percpu(paiext_root.mapptr);
		paiext_root.mapptr = NULL;
	}
}

/* On initialization of first event also allocate per CPU data dynamically.
 * Start with an array of pointers, the array size is the maximum number of
 * CPUs possible, which might be larger than the number of CPUs currently
 * online.
 */
static int paiext_root_alloc(void)
{
	if (++paiext_root.refcnt == 1) {
		/* The memory is already zeroed. */
		paiext_root.mapptr = alloc_percpu(struct paiext_mapptr);
		if (!paiext_root.mapptr) {
			/* Returing without refcnt adjustment is ok. The
			 * error code is handled by paiext_alloc() which
			 * decrements refcnt when an event can not be
			 * created.
			 */
			return -ENOMEM;
		}
	}
	return 0;
}

/* Protects against concurrent increment of sampler and counter member
 * increments at the same time and prohibits concurrent execution of
 * counting and sampling events.
 * Ensures that analytics counter block is deallocated only when the
 * sampling and counting on that cpu is zero.
 * For details see paiext_alloc().
 */
static DEFINE_MUTEX(paiext_reserve_mutex);

/* Free all memory allocated for event counting/sampling setup */
static void paiext_free(struct paiext_mapptr *mp)
{
	kfree(mp->mapptr->area);
	kfree(mp->mapptr->paiext_cb);
	kvfree(mp->mapptr->save);
	kfree(mp->mapptr);
	mp->mapptr = NULL;
}

/* Release the PMU if event is the last perf event */
static void paiext_event_destroy(struct perf_event *event)
{
	struct paiext_mapptr *mp = per_cpu_ptr(paiext_root.mapptr, event->cpu);
	struct paiext_map *cpump = mp->mapptr;

	mutex_lock(&paiext_reserve_mutex);
	cpump->event = NULL;
	if (!--cpump->refcnt)		/* Last reference gone */
		paiext_free(mp);
	paiext_root_free();
	mutex_unlock(&paiext_reserve_mutex);
	debug_sprintf_event(paiext_dbg, 4, "%s cpu %d mapptr %p\n", __func__,
			    event->cpu, mp->mapptr);

}

/* Used to avoid races in checking concurrent access of counting and
 * sampling for pai_extension events.
 *
 * Only one instance of event pai_ext/NNPA_ALL/ for sampling is
 * allowed and when this event is running, no counting event is allowed.
 * Several counting events are allowed in parallel, but no sampling event
 * is allowed while one (or more) counting events are running.
 *
 * This function is called in process context and it is safe to block.
 * When the event initialization functions fails, no other call back will
 * be invoked.
 *
 * Allocate the memory for the event.
 */
static int paiext_alloc(struct perf_event_attr *a, struct perf_event *event)
{
	struct paiext_mapptr *mp;
	struct paiext_map *cpump;
	int rc;

	mutex_lock(&paiext_reserve_mutex);

	rc = paiext_root_alloc();
	if (rc)
		goto unlock;

	mp = per_cpu_ptr(paiext_root.mapptr, event->cpu);
	cpump = mp->mapptr;
	if (!cpump) {			/* Paiext_map allocated? */
		rc = -ENOMEM;
		cpump = kzalloc(sizeof(*cpump), GFP_KERNEL);
		if (!cpump)
			goto unlock;

		/* Allocate memory for counter area and counter extraction.
		 * These are
		 * - a 512 byte block and requires 512 byte boundary alignment.
		 * - a 1KB byte block and requires 1KB boundary alignment.
		 * Only the first counting event has to allocate the area.
		 *
		 * Note: This works with commit 59bb47985c1d by default.
		 * Backporting this to kernels without this commit might
		 * need adjustment.
		 */
		mp->mapptr = cpump;
		cpump->area = kzalloc(PAIE1_CTRBLOCK_SZ, GFP_KERNEL);
		cpump->paiext_cb = kzalloc(PAIE1_CB_SZ, GFP_KERNEL);
		cpump->save = kvmalloc_array(paiext_cnt + 1,
					     sizeof(struct pai_userdata),
					     GFP_KERNEL);
		if (!cpump->save || !cpump->area || !cpump->paiext_cb) {
			paiext_free(mp);
			goto unlock;
		}
		cpump->mode = a->sample_period ? PAI_MODE_SAMPLING
					       : PAI_MODE_COUNTER;
	} else {
		/* Multiple invocation, check whats active.
		 * Supported are multiple counter events or only one sampling
		 * event concurrently at any one time.
		 */
		if (cpump->mode == PAI_MODE_SAMPLING ||
		    (cpump->mode == PAI_MODE_COUNTER && a->sample_period)) {
			rc = -EBUSY;
			goto unlock;
		}
	}

	rc = 0;
	cpump->event = event;
	++cpump->refcnt;

unlock:
	if (rc) {
		/* Error in allocation of event, decrement anchor. Since
		 * the event in not created, its destroy() function is never
		 * invoked. Adjust the reference counter for the anchor.
		 */
		paiext_root_free();
	}
	mutex_unlock(&paiext_reserve_mutex);
	/* If rc is non-zero, no increment of counter/sampler was done. */
	return rc;
}

/* The PAI extension 1 control block supports up to 128 entries. Return
 * the index within PAIE1_CB given the event number. Also validate event
 * number.
 */
static int paiext_event_valid(struct perf_event *event)
{
	u64 cfg = event->attr.config;

	if (cfg >= PAI_NNPA_BASE && cfg <= PAI_NNPA_BASE + paiext_cnt) {
		/* Offset NNPA in paiext_cb */
		event->hw.config_base = offsetof(struct paiext_cb, acc);
		return 0;
	}
	return -EINVAL;
}

/* Might be called on different CPU than the one the event is intended for. */
static int paiext_event_init(struct perf_event *event)
{
	struct perf_event_attr *a = &event->attr;
	int rc;

	/* PMU pai_ext registered as PERF_TYPE_RAW, check event type */
	if (a->type != PERF_TYPE_RAW && event->pmu->type != a->type)
		return -ENOENT;
	/* PAI extension event must be valid and in supported range */
	rc = paiext_event_valid(event);
	if (rc)
		return rc;
	/* Allow only CPU wide operation, no process context for now. */
	if (event->hw.target || event->cpu == -1)
		return -ENOENT;
	/* Allow only event NNPA_ALL for sampling. */
	if (a->sample_period && a->config != PAI_NNPA_BASE)
		return -EINVAL;
	/* Prohibit exclude_user event selection */
	if (a->exclude_user)
		return -EINVAL;

	rc = paiext_alloc(a, event);
	if (rc)
		return rc;
	event->hw.last_tag = 0;
	event->destroy = paiext_event_destroy;

	if (a->sample_period) {
		a->sample_period = 1;
		a->freq = 0;
		/* Register for paicrypt_sched_task() to be called */
		event->attach_state |= PERF_ATTACH_SCHED_CB;
		/* Add raw data which are the memory mapped counters */
		a->sample_type |= PERF_SAMPLE_RAW;
		/* Turn off inheritance */
		a->inherit = 0;
	}

	return 0;
}

static u64 paiext_getctr(struct paiext_map *cpump, int nr)
{
	return cpump->area[nr];
}

/* Read the counter values. Return value from location in buffer. For event
 * NNPA_ALL sum up all events.
 */
static u64 paiext_getdata(struct perf_event *event)
{
	struct paiext_mapptr *mp = this_cpu_ptr(paiext_root.mapptr);
	struct paiext_map *cpump = mp->mapptr;
	u64 sum = 0;
	int i;

	if (event->attr.config != PAI_NNPA_BASE)
		return paiext_getctr(cpump, event->attr.config - PAI_NNPA_BASE);

	for (i = 1; i <= paiext_cnt; i++)
		sum += paiext_getctr(cpump, i);

	return sum;
}

static u64 paiext_getall(struct perf_event *event)
{
	return paiext_getdata(event);
}

static void paiext_read(struct perf_event *event)
{
	u64 prev, new, delta;

	prev = local64_read(&event->hw.prev_count);
	new = paiext_getall(event);
	local64_set(&event->hw.prev_count, new);
	delta = new - prev;
	local64_add(delta, &event->count);
}

static void paiext_start(struct perf_event *event, int flags)
{
	u64 sum;

	if (event->hw.last_tag)
		return;
	event->hw.last_tag = 1;
	sum = paiext_getall(event);		/* Get current value */
	local64_set(&event->hw.prev_count, sum);
	local64_set(&event->count, 0);
}

static int paiext_add(struct perf_event *event, int flags)
{
	struct paiext_mapptr *mp = this_cpu_ptr(paiext_root.mapptr);
	struct paiext_map *cpump = mp->mapptr;
	struct paiext_cb *pcb = cpump->paiext_cb;

	if (++cpump->active_events == 1) {
		S390_lowcore.aicd = virt_to_phys(cpump->paiext_cb);
		pcb->acc = virt_to_phys(cpump->area) | 0x1;
		/* Enable CPU instruction lookup for PAIE1 control block */
		__ctl_set_bit(0, 49);
		debug_sprintf_event(paiext_dbg, 4, "%s 1508 %llx acc %llx\n",
				    __func__, S390_lowcore.aicd, pcb->acc);
	}
	if (flags & PERF_EF_START && !event->attr.sample_period) {
		/* Only counting needs initial counter value */
		paiext_start(event, PERF_EF_RELOAD);
	}
	event->hw.state = 0;
	if (event->attr.sample_period) {
		cpump->event = event;
		perf_sched_cb_inc(event->pmu);
	}
	return 0;
}

static void paiext_stop(struct perf_event *event, int flags)
{
	paiext_read(event);
	event->hw.state = PERF_HES_STOPPED;
}

static void paiext_del(struct perf_event *event, int flags)
{
	struct paiext_mapptr *mp = this_cpu_ptr(paiext_root.mapptr);
	struct paiext_map *cpump = mp->mapptr;
	struct paiext_cb *pcb = cpump->paiext_cb;

	if (event->attr.sample_period)
		perf_sched_cb_dec(event->pmu);
	if (!event->attr.sample_period) {
		/* Only counting needs to read counter */
		paiext_stop(event, PERF_EF_UPDATE);
	}
	if (--cpump->active_events == 0) {
		/* Disable CPU instruction lookup for PAIE1 control block */
		__ctl_clear_bit(0, 49);
		pcb->acc = 0;
		S390_lowcore.aicd = 0;
		debug_sprintf_event(paiext_dbg, 4, "%s 1508 %llx acc %llx\n",
				    __func__, S390_lowcore.aicd, pcb->acc);
	}
}

/* Create raw data and save it in buffer. Returns number of bytes copied.
 * Saves only positive counter entries of the form
 * 2 bytes: Number of counter
 * 8 bytes: Value of counter
 */
static size_t paiext_copy(struct paiext_map *cpump)
{
	struct pai_userdata *userdata = cpump->save;
	int i, outidx = 0;

	for (i = 1; i <= paiext_cnt; i++) {
		u64 val = paiext_getctr(cpump, i);

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
 * Note: The function paiext_sched_task() and paiext_push_sample() are not
 * invoked after function paiext_del() has been called because of function
 * perf_sched_cb_dec().
 * The function paiext_sched_task() and paiext_push_sample() are only
 * called when sampling is active. Function perf_sched_cb_inc()
 * has been invoked to install function paiext_sched_task() as call back
 * to run at context switch time (see paiext_add()).
 *
 * This causes function perf_event_context_sched_out() and
 * perf_event_context_sched_in() to check whether the PMU has installed an
 * sched_task() callback. That callback is not active after paiext_del()
 * returns and has deleted the event on that CPU.
 */
static int paiext_push_sample(void)
{
	struct paiext_mapptr *mp = this_cpu_ptr(paiext_root.mapptr);
	struct paiext_map *cpump = mp->mapptr;
	struct perf_event *event = cpump->event;
	struct perf_sample_data data;
	struct perf_raw_record raw;
	struct pt_regs regs;
	size_t rawsize;
	int overflow;

	rawsize = paiext_copy(cpump);
	if (!rawsize)			/* No incremented counters */
		return 0;

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
	if (event->attr.sample_type & PERF_SAMPLE_CPU)
		data.cpu_entry.cpu = smp_processor_id();
	if (event->attr.sample_type & PERF_SAMPLE_RAW) {
		raw.frag.size = rawsize;
		raw.frag.data = cpump->save;
		raw.size = raw.frag.size;
		data.raw = &raw;
	}

	overflow = perf_event_overflow(event, &data, &regs);
	perf_event_update_userpage(event);
	/* Clear lowcore area after read */
	memset(cpump->area, 0, PAIE1_CTRBLOCK_SZ);
	return overflow;
}

/* Called on schedule-in and schedule-out. No access to event structure,
 * but for sampling only event NNPA_ALL is allowed.
 */
static void paiext_sched_task(struct perf_event_pmu_context *pmu_ctx, bool sched_in)
{
	/* We started with a clean page on event installation. So read out
	 * results on schedule_out and if page was dirty, clear values.
	 */
	if (!sched_in)
		paiext_push_sample();
}

/* Attribute definitions for pai extension1 interface. As with other CPU
 * Measurement Facilities, there is one attribute per mapped counter.
 * The number of mapped counters may vary per machine generation. Use
 * the QUERY PROCESSOR ACTIVITY COUNTER INFORMATION (QPACI) instruction
 * to determine the number of mapped counters. The instructions returns
 * a positive number, which is the highest number of supported counters.
 * All counters less than this number are also supported, there are no
 * holes. A returned number of zero means no support for mapped counters.
 *
 * The identification of the counter is a unique number. The chosen range
 * is 0x1800 + offset in mapped kernel page.
 * All CPU Measurement Facility counters identifiers must be unique and
 * the numbers from 0 to 496 are already used for the CPU Measurement
 * Counter facility. Number 0x1000 to 0x103e are used for PAI cryptography
 * counters.
 * Numbers 0xb0000, 0xbc000 and 0xbd000 are already
 * used for the CPU Measurement Sampling facility.
 */
PMU_FORMAT_ATTR(event, "config:0-63");

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
	.task_ctx_nr  = perf_invalid_context,
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
};

static void __init attr_event_free(struct attribute **attrs, int num)
{
	struct perf_pmu_events_attr *pa;
	struct device_attribute *dap;
	int i;

	for (i = 0; i < num; i++) {
		dap = container_of(attrs[i], struct device_attribute, attr);
		pa = container_of(dap, struct perf_pmu_events_attr, attr);
		kfree(pa);
	}
	kfree(attrs);
}

static int __init attr_event_init_one(struct attribute **attrs, int num)
{
	struct perf_pmu_events_attr *pa;

	pa = kzalloc(sizeof(*pa), GFP_KERNEL);
	if (!pa)
		return -ENOMEM;

	sysfs_attr_init(&pa->attr.attr);
	pa->id = PAI_NNPA_BASE + num;
	pa->attr.attr.name = paiext_ctrnames[num];
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

	attrs = kmalloc_array(ARRAY_SIZE(paiext_ctrnames) + 1, sizeof(*attrs),
			      GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(paiext_ctrnames); i++) {
		ret = attr_event_init_one(attrs, i);
		if (ret) {
			attr_event_free(attrs, i - 1);
			return ret;
		}
	}
	attrs[i] = NULL;
	paiext_events_group.attrs = attrs;
	return 0;
}

static int __init paiext_init(void)
{
	struct qpaci_info_block ib;
	int rc = -ENOMEM;

	if (!test_facility(197))
		return 0;

	qpaci(&ib);
	paiext_cnt = ib.num_nnpa;
	if (paiext_cnt >= PAI_NNPA_MAXCTR)
		paiext_cnt = PAI_NNPA_MAXCTR;
	if (!paiext_cnt)
		return 0;

	rc = attr_event_init();
	if (rc) {
		pr_err("Creation of PMU " KMSG_COMPONENT " /sysfs failed\n");
		return rc;
	}

	/* Setup s390dbf facility */
	paiext_dbg = debug_register(KMSG_COMPONENT, 2, 256, 128);
	if (!paiext_dbg) {
		pr_err("Registration of s390dbf " KMSG_COMPONENT " failed\n");
		rc = -ENOMEM;
		goto out_init;
	}
	debug_register_view(paiext_dbg, &debug_sprintf_view);

	rc = perf_pmu_register(&paiext, KMSG_COMPONENT, -1);
	if (rc) {
		pr_err("Registration of " KMSG_COMPONENT " PMU failed with "
		       "rc=%i\n", rc);
		goto out_pmu;
	}

	return 0;

out_pmu:
	debug_unregister_view(paiext_dbg, &debug_sprintf_view);
	debug_unregister(paiext_dbg);
out_init:
	attr_event_free(paiext_events_group.attrs,
			ARRAY_SIZE(paiext_ctrnames) + 1);
	return rc;
}

device_initcall(paiext_init);
