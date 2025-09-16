// SPDX-License-Identifier: GPL-2.0
/*
 * Description: PMUs specific to running nested KVM-HV guests
 * on Book3S processors (specifically POWER9 and later).
 */

#define pr_fmt(fmt)  "kvmppc-pmu: " fmt

#include "asm-generic/local64.h"
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ratelimit.h>
#include <linux/kvm_host.h>
#include <linux/gfp_types.h>
#include <linux/pgtable.h>
#include <linux/perf_event.h>
#include <linux/spinlock_types.h>
#include <linux/spinlock.h>

#include <asm/types.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu.h>
#include <asm/pgalloc.h>
#include <asm/pte-walk.h>
#include <asm/reg.h>
#include <asm/plpar_wrappers.h>
#include <asm/firmware.h>

#include "asm/guest-state-buffer.h"

enum kvmppc_pmu_eventid {
	KVMPPC_EVENT_HOST_HEAP,
	KVMPPC_EVENT_HOST_HEAP_MAX,
	KVMPPC_EVENT_HOST_PGTABLE,
	KVMPPC_EVENT_HOST_PGTABLE_MAX,
	KVMPPC_EVENT_HOST_PGTABLE_RECLAIM,
	KVMPPC_EVENT_MAX,
};

#define KVMPPC_PMU_EVENT_ATTR(_name, _id) \
	PMU_EVENT_ATTR_ID(_name, kvmppc_events_sysfs_show, _id)

static ssize_t kvmppc_events_sysfs_show(struct device *dev,
					struct device_attribute *attr,
					char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sprintf(page, "event=0x%02llx\n", pmu_attr->id);
}

/* Holds the hostwide stats */
static struct kvmppc_hostwide_stats {
	u64 guest_heap;
	u64 guest_heap_max;
	u64 guest_pgtable_size;
	u64 guest_pgtable_size_max;
	u64 guest_pgtable_reclaim;
} l0_stats;

/* Protect access to l0_stats */
static DEFINE_SPINLOCK(lock_l0_stats);

/* GSB related structs needed to talk to L0 */
static struct kvmppc_gs_msg *gsm_l0_stats;
static struct kvmppc_gs_buff *gsb_l0_stats;
static struct kvmppc_gs_parser gsp_l0_stats;

static struct attribute *kvmppc_pmu_events_attr[] = {
	KVMPPC_PMU_EVENT_ATTR(host_heap, KVMPPC_EVENT_HOST_HEAP),
	KVMPPC_PMU_EVENT_ATTR(host_heap_max, KVMPPC_EVENT_HOST_HEAP_MAX),
	KVMPPC_PMU_EVENT_ATTR(host_pagetable, KVMPPC_EVENT_HOST_PGTABLE),
	KVMPPC_PMU_EVENT_ATTR(host_pagetable_max, KVMPPC_EVENT_HOST_PGTABLE_MAX),
	KVMPPC_PMU_EVENT_ATTR(host_pagetable_reclaim, KVMPPC_EVENT_HOST_PGTABLE_RECLAIM),
	NULL,
};

static const struct attribute_group kvmppc_pmu_events_group = {
	.name = "events",
	.attrs = kvmppc_pmu_events_attr,
};

PMU_FORMAT_ATTR(event, "config:0-5");
static struct attribute *kvmppc_pmu_format_attr[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group kvmppc_pmu_format_group = {
	.name = "format",
	.attrs = kvmppc_pmu_format_attr,
};

static const struct attribute_group *kvmppc_pmu_attr_groups[] = {
	&kvmppc_pmu_events_group,
	&kvmppc_pmu_format_group,
	NULL,
};

/*
 * Issue the hcall to get the L0-host stats.
 * Should be called with l0-stat lock held
 */
static int kvmppc_update_l0_stats(void)
{
	int rc;

	/* With HOST_WIDE flags guestid and vcpuid will be ignored */
	rc = kvmppc_gsb_recv(gsb_l0_stats, KVMPPC_GS_FLAGS_HOST_WIDE);
	if (rc)
		goto out;

	/* Parse the guest state buffer is successful */
	rc = kvmppc_gse_parse(&gsp_l0_stats, gsb_l0_stats);
	if (rc)
		goto out;

	/* Update the l0 returned stats*/
	memset(&l0_stats, 0, sizeof(l0_stats));
	rc = kvmppc_gsm_refresh_info(gsm_l0_stats, gsb_l0_stats);

out:
	return rc;
}

/* Update the value of the given perf_event */
static int kvmppc_pmu_event_update(struct perf_event *event)
{
	int rc;
	u64 curr_val, prev_val;
	unsigned long flags;
	unsigned int config = event->attr.config;

	/* Ensure no one else is modifying the l0_stats */
	spin_lock_irqsave(&lock_l0_stats, flags);

	rc = kvmppc_update_l0_stats();
	if (!rc) {
		switch (config) {
		case KVMPPC_EVENT_HOST_HEAP:
			curr_val = l0_stats.guest_heap;
			break;
		case KVMPPC_EVENT_HOST_HEAP_MAX:
			curr_val = l0_stats.guest_heap_max;
			break;
		case KVMPPC_EVENT_HOST_PGTABLE:
			curr_val = l0_stats.guest_pgtable_size;
			break;
		case KVMPPC_EVENT_HOST_PGTABLE_MAX:
			curr_val = l0_stats.guest_pgtable_size_max;
			break;
		case KVMPPC_EVENT_HOST_PGTABLE_RECLAIM:
			curr_val = l0_stats.guest_pgtable_reclaim;
			break;
		default:
			rc = -ENOENT;
			break;
		}
	}

	spin_unlock_irqrestore(&lock_l0_stats, flags);

	/* If no error than update the perf event */
	if (!rc) {
		prev_val = local64_xchg(&event->hw.prev_count, curr_val);
		if (curr_val > prev_val)
			local64_add(curr_val - prev_val, &event->count);
	}

	return rc;
}

static int kvmppc_pmu_event_init(struct perf_event *event)
{
	unsigned int config = event->attr.config;

	pr_debug("%s: Event(%p) id=%llu cpu=%x on_cpu=%x config=%u",
		 __func__, event, event->id, event->cpu,
		 event->oncpu, config);

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (config >= KVMPPC_EVENT_MAX)
		return -EINVAL;

	local64_set(&event->hw.prev_count, 0);
	local64_set(&event->count, 0);

	return 0;
}

static void kvmppc_pmu_del(struct perf_event *event, int flags)
{
	kvmppc_pmu_event_update(event);
}

static int kvmppc_pmu_add(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_START)
		return kvmppc_pmu_event_update(event);
	return 0;
}

static void kvmppc_pmu_read(struct perf_event *event)
{
	kvmppc_pmu_event_update(event);
}

/* Return the size of the needed guest state buffer */
static size_t hostwide_get_size(struct kvmppc_gs_msg *gsm)

{
	size_t size = 0;
	const u16 ids[] = {
		KVMPPC_GSID_L0_GUEST_HEAP,
		KVMPPC_GSID_L0_GUEST_HEAP_MAX,
		KVMPPC_GSID_L0_GUEST_PGTABLE_SIZE,
		KVMPPC_GSID_L0_GUEST_PGTABLE_SIZE_MAX,
		KVMPPC_GSID_L0_GUEST_PGTABLE_RECLAIM
	};

	for (int i = 0; i < ARRAY_SIZE(ids); i++)
		size += kvmppc_gse_total_size(kvmppc_gsid_size(ids[i]));
	return size;
}

/* Populate the request guest state buffer */
static int hostwide_fill_info(struct kvmppc_gs_buff *gsb,
			      struct kvmppc_gs_msg *gsm)
{
	int rc = 0;
	struct kvmppc_hostwide_stats  *stats = gsm->data;

	/*
	 * It doesn't matter what values are put into request buffer as
	 * they are going to be overwritten anyways. But for the sake of
	 * testcode and symmetry contents of existing stats are put
	 * populated into the request guest state buffer.
	 */
	if (kvmppc_gsm_includes(gsm, KVMPPC_GSID_L0_GUEST_HEAP))
		rc = kvmppc_gse_put_u64(gsb,
					KVMPPC_GSID_L0_GUEST_HEAP,
					stats->guest_heap);

	if (!rc && kvmppc_gsm_includes(gsm, KVMPPC_GSID_L0_GUEST_HEAP_MAX))
		rc = kvmppc_gse_put_u64(gsb,
					KVMPPC_GSID_L0_GUEST_HEAP_MAX,
					stats->guest_heap_max);

	if (!rc && kvmppc_gsm_includes(gsm, KVMPPC_GSID_L0_GUEST_PGTABLE_SIZE))
		rc = kvmppc_gse_put_u64(gsb,
					KVMPPC_GSID_L0_GUEST_PGTABLE_SIZE,
					stats->guest_pgtable_size);
	if (!rc &&
	    kvmppc_gsm_includes(gsm, KVMPPC_GSID_L0_GUEST_PGTABLE_SIZE_MAX))
		rc = kvmppc_gse_put_u64(gsb,
					KVMPPC_GSID_L0_GUEST_PGTABLE_SIZE_MAX,
					stats->guest_pgtable_size_max);
	if (!rc &&
	    kvmppc_gsm_includes(gsm, KVMPPC_GSID_L0_GUEST_PGTABLE_RECLAIM))
		rc = kvmppc_gse_put_u64(gsb,
					KVMPPC_GSID_L0_GUEST_PGTABLE_RECLAIM,
					stats->guest_pgtable_reclaim);

	return rc;
}

/* Parse and update the host wide stats from returned gsb */
static int hostwide_refresh_info(struct kvmppc_gs_msg *gsm,
				 struct kvmppc_gs_buff *gsb)
{
	struct kvmppc_gs_parser gsp = { 0 };
	struct kvmppc_hostwide_stats *stats = gsm->data;
	struct kvmppc_gs_elem *gse;
	int rc;

	rc = kvmppc_gse_parse(&gsp, gsb);
	if (rc < 0)
		return rc;

	gse = kvmppc_gsp_lookup(&gsp, KVMPPC_GSID_L0_GUEST_HEAP);
	if (gse)
		stats->guest_heap = kvmppc_gse_get_u64(gse);

	gse = kvmppc_gsp_lookup(&gsp, KVMPPC_GSID_L0_GUEST_HEAP_MAX);
	if (gse)
		stats->guest_heap_max = kvmppc_gse_get_u64(gse);

	gse = kvmppc_gsp_lookup(&gsp, KVMPPC_GSID_L0_GUEST_PGTABLE_SIZE);
	if (gse)
		stats->guest_pgtable_size = kvmppc_gse_get_u64(gse);

	gse = kvmppc_gsp_lookup(&gsp, KVMPPC_GSID_L0_GUEST_PGTABLE_SIZE_MAX);
	if (gse)
		stats->guest_pgtable_size_max = kvmppc_gse_get_u64(gse);

	gse = kvmppc_gsp_lookup(&gsp, KVMPPC_GSID_L0_GUEST_PGTABLE_RECLAIM);
	if (gse)
		stats->guest_pgtable_reclaim = kvmppc_gse_get_u64(gse);

	return 0;
}

/* gsb-message ops for setting up/parsing */
static struct kvmppc_gs_msg_ops gsb_ops_l0_stats = {
	.get_size = hostwide_get_size,
	.fill_info = hostwide_fill_info,
	.refresh_info = hostwide_refresh_info,
};

static int kvmppc_init_hostwide(void)
{
	int rc = 0;
	unsigned long flags;

	spin_lock_irqsave(&lock_l0_stats, flags);

	/* already registered ? */
	if (gsm_l0_stats) {
		rc = 0;
		goto out;
	}

	/* setup the Guest state message/buffer to talk to L0 */
	gsm_l0_stats = kvmppc_gsm_new(&gsb_ops_l0_stats, &l0_stats,
				      GSM_SEND, GFP_KERNEL);
	if (!gsm_l0_stats) {
		rc = -ENOMEM;
		goto out;
	}

	/* Populate the Idents */
	kvmppc_gsm_include(gsm_l0_stats, KVMPPC_GSID_L0_GUEST_HEAP);
	kvmppc_gsm_include(gsm_l0_stats, KVMPPC_GSID_L0_GUEST_HEAP_MAX);
	kvmppc_gsm_include(gsm_l0_stats, KVMPPC_GSID_L0_GUEST_PGTABLE_SIZE);
	kvmppc_gsm_include(gsm_l0_stats, KVMPPC_GSID_L0_GUEST_PGTABLE_SIZE_MAX);
	kvmppc_gsm_include(gsm_l0_stats, KVMPPC_GSID_L0_GUEST_PGTABLE_RECLAIM);

	/* allocate GSB. Guest/Vcpu Id is ignored */
	gsb_l0_stats = kvmppc_gsb_new(kvmppc_gsm_size(gsm_l0_stats), 0, 0,
				      GFP_KERNEL);
	if (!gsb_l0_stats) {
		rc = -ENOMEM;
		goto out;
	}

	/* ask the ops to fill in the info */
	rc = kvmppc_gsm_fill_info(gsm_l0_stats, gsb_l0_stats);

out:
	if (rc) {
		if (gsm_l0_stats)
			kvmppc_gsm_free(gsm_l0_stats);
		if (gsb_l0_stats)
			kvmppc_gsb_free(gsb_l0_stats);
		gsm_l0_stats = NULL;
		gsb_l0_stats = NULL;
	}
	spin_unlock_irqrestore(&lock_l0_stats, flags);
	return rc;
}

static void kvmppc_cleanup_hostwide(void)
{
	unsigned long flags;

	spin_lock_irqsave(&lock_l0_stats, flags);

	if (gsm_l0_stats)
		kvmppc_gsm_free(gsm_l0_stats);
	if (gsb_l0_stats)
		kvmppc_gsb_free(gsb_l0_stats);
	gsm_l0_stats = NULL;
	gsb_l0_stats = NULL;

	spin_unlock_irqrestore(&lock_l0_stats, flags);
}

/* L1 wide counters PMU */
static struct pmu kvmppc_pmu = {
	.module = THIS_MODULE,
	.task_ctx_nr = perf_sw_context,
	.name = "kvm-hv",
	.event_init = kvmppc_pmu_event_init,
	.add = kvmppc_pmu_add,
	.del = kvmppc_pmu_del,
	.read = kvmppc_pmu_read,
	.attr_groups = kvmppc_pmu_attr_groups,
	.type = -1,
	.scope = PERF_PMU_SCOPE_SYS_WIDE,
	.capabilities = PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_NO_INTERRUPT,
};

static int __init kvmppc_register_pmu(void)
{
	int rc = -EOPNOTSUPP;

	/* only support events for nestedv2 right now */
	if (kvmhv_is_nestedv2()) {
		rc = kvmppc_init_hostwide();
		if (rc)
			goto out;

		/* Register the pmu */
		rc = perf_pmu_register(&kvmppc_pmu, kvmppc_pmu.name, -1);
		if (rc)
			goto out;

		pr_info("Registered kvm-hv pmu");
	}

out:
	return rc;
}

static void __exit kvmppc_unregister_pmu(void)
{
	if (kvmhv_is_nestedv2()) {
		kvmppc_cleanup_hostwide();

		if (kvmppc_pmu.type != -1)
			perf_pmu_unregister(&kvmppc_pmu);

		pr_info("kvmhv_pmu unregistered.\n");
	}
}

module_init(kvmppc_register_pmu);
module_exit(kvmppc_unregister_pmu);
MODULE_DESCRIPTION("KVM PPC Book3s-hv PMU");
MODULE_AUTHOR("Vaibhav Jain <vaibhav@linux.ibm.com>");
MODULE_LICENSE("GPL");
