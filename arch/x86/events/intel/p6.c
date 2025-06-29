// SPDX-License-Identifier: GPL-2.0
#include <linux/perf_event.h>
#include <linux/types.h>

#include <asm/cpu_device_id.h>
#include <asm/msr.h>

#include "../perf_event.h"

/*
 * Not sure about some of these
 */
static const u64 p6_perfmon_event_map[] =
{
  [PERF_COUNT_HW_CPU_CYCLES]		= 0x0079,	/* CPU_CLK_UNHALTED */
  [PERF_COUNT_HW_INSTRUCTIONS]		= 0x00c0,	/* INST_RETIRED     */
  [PERF_COUNT_HW_CACHE_REFERENCES]	= 0x0f2e,	/* L2_RQSTS:M:E:S:I */
  [PERF_COUNT_HW_CACHE_MISSES]		= 0x012e,	/* L2_RQSTS:I       */
  [PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= 0x00c4,	/* BR_INST_RETIRED  */
  [PERF_COUNT_HW_BRANCH_MISSES]		= 0x00c5,	/* BR_MISS_PRED_RETIRED */
  [PERF_COUNT_HW_BUS_CYCLES]		= 0x0062,	/* BUS_DRDY_CLOCKS  */
  [PERF_COUNT_HW_STALLED_CYCLES_FRONTEND] = 0x00a2,	/* RESOURCE_STALLS  */

};

static const u64 __initconst p6_hw_cache_event_ids
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX] =
{
 [ C(L1D) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0043,	/* DATA_MEM_REFS       */
                [ C(RESULT_MISS)   ] = 0x0045,	/* DCU_LINES_IN        */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0x0f29,	/* L2_LD:M:E:S:I       */
	},
        [ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
        },
 },
 [ C(L1I ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0080,	/* IFU_IFETCH         */
		[ C(RESULT_MISS)   ] = 0x0f28,	/* L2_IFETCH:M:E:S:I  */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(LL  ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0x0025,	/* L2_M_LINES_INM     */
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(DTLB) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0043,	/* DATA_MEM_REFS      */
		[ C(RESULT_MISS)   ] = 0,
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = 0,
		[ C(RESULT_MISS)   ] = 0,
	},
 },
 [ C(ITLB) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x0080,	/* IFU_IFETCH         */
		[ C(RESULT_MISS)   ] = 0x0085,	/* ITLB_MISS          */
	},
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
 },
 [ C(BPU ) ] = {
	[ C(OP_READ) ] = {
		[ C(RESULT_ACCESS) ] = 0x00c4,	/* BR_INST_RETIRED      */
		[ C(RESULT_MISS)   ] = 0x00c5,	/* BR_MISS_PRED_RETIRED */
        },
	[ C(OP_WRITE) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
	[ C(OP_PREFETCH) ] = {
		[ C(RESULT_ACCESS) ] = -1,
		[ C(RESULT_MISS)   ] = -1,
	},
 },
};

static u64 p6_pmu_event_map(int hw_event)
{
	return p6_perfmon_event_map[hw_event];
}

/*
 * Event setting that is specified not to count anything.
 * We use this to effectively disable a counter.
 *
 * L2_RQSTS with 0 MESI unit mask.
 */
#define P6_NOP_EVENT			0x0000002EULL

static struct event_constraint p6_event_constraints[] =
{
	INTEL_EVENT_CONSTRAINT(0xc1, 0x1),	/* FLOPS */
	INTEL_EVENT_CONSTRAINT(0x10, 0x1),	/* FP_COMP_OPS_EXE */
	INTEL_EVENT_CONSTRAINT(0x11, 0x2),	/* FP_ASSIST */
	INTEL_EVENT_CONSTRAINT(0x12, 0x2),	/* MUL */
	INTEL_EVENT_CONSTRAINT(0x13, 0x2),	/* DIV */
	INTEL_EVENT_CONSTRAINT(0x14, 0x1),	/* CYCLES_DIV_BUSY */
	EVENT_CONSTRAINT_END
};

static void p6_pmu_disable_all(void)
{
	u64 val;

	/* p6 only has one enable register */
	rdmsrq(MSR_P6_EVNTSEL0, val);
	val &= ~ARCH_PERFMON_EVENTSEL_ENABLE;
	wrmsrq(MSR_P6_EVNTSEL0, val);
}

static void p6_pmu_enable_all(int added)
{
	unsigned long val;

	/* p6 only has one enable register */
	rdmsrq(MSR_P6_EVNTSEL0, val);
	val |= ARCH_PERFMON_EVENTSEL_ENABLE;
	wrmsrq(MSR_P6_EVNTSEL0, val);
}

static inline void
p6_pmu_disable_event(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 val = P6_NOP_EVENT;

	(void)wrmsrq_safe(hwc->config_base, val);
}

static void p6_pmu_enable_event(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 val;

	val = hwc->config;

	/*
	 * p6 only has a global event enable, set on PerfEvtSel0
	 * We "disable" events by programming P6_NOP_EVENT
	 * and we rely on p6_pmu_enable_all() being called
	 * to actually enable the events.
	 */

	(void)wrmsrq_safe(hwc->config_base, val);
}

PMU_FORMAT_ATTR(event,	"config:0-7"	);
PMU_FORMAT_ATTR(umask,	"config:8-15"	);
PMU_FORMAT_ATTR(edge,	"config:18"	);
PMU_FORMAT_ATTR(pc,	"config:19"	);
PMU_FORMAT_ATTR(inv,	"config:23"	);
PMU_FORMAT_ATTR(cmask,	"config:24-31"	);

static struct attribute *intel_p6_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_pc.attr,
	&format_attr_inv.attr,
	&format_attr_cmask.attr,
	NULL,
};

static __initconst const struct x86_pmu p6_pmu = {
	.name			= "p6",
	.handle_irq		= x86_pmu_handle_irq,
	.disable_all		= p6_pmu_disable_all,
	.enable_all		= p6_pmu_enable_all,
	.enable			= p6_pmu_enable_event,
	.disable		= p6_pmu_disable_event,
	.hw_config		= x86_pmu_hw_config,
	.schedule_events	= x86_schedule_events,
	.eventsel		= MSR_P6_EVNTSEL0,
	.perfctr		= MSR_P6_PERFCTR0,
	.event_map		= p6_pmu_event_map,
	.max_events		= ARRAY_SIZE(p6_perfmon_event_map),
	.apic			= 1,
	.max_period		= (1ULL << 31) - 1,
	.version		= 0,
	.cntr_mask64		= 0x3,
	/*
	 * Events have 40 bits implemented. However they are designed such
	 * that bits [32-39] are sign extensions of bit 31. As such the
	 * effective width of a event for P6-like PMU is 32 bits only.
	 *
	 * See IA-32 Intel Architecture Software developer manual Vol 3B
	 */
	.cntval_bits		= 32,
	.cntval_mask		= (1ULL << 32) - 1,
	.get_event_constraints	= x86_get_event_constraints,
	.event_constraints	= p6_event_constraints,

	.format_attrs		= intel_p6_formats_attr,
	.events_sysfs_show	= intel_event_sysfs_show,

};

static __init void p6_pmu_rdpmc_quirk(void)
{
	if (boot_cpu_data.x86_stepping < 9) {
		/*
		 * PPro erratum 26; fixed in stepping 9 and above.
		 */
		pr_warn("Userspace RDPMC support disabled due to a CPU erratum\n");
		x86_pmu.attr_rdpmc_broken = 1;
		x86_pmu.attr_rdpmc = 0;
	}
}

__init int p6_pmu_init(void)
{
	x86_pmu = p6_pmu;

	if (boot_cpu_data.x86_vfm == INTEL_PENTIUM_PRO)
		x86_add_quirk(p6_pmu_rdpmc_quirk);

	memcpy(hw_cache_event_ids, p6_hw_cache_event_ids,
		sizeof(hw_cache_event_ids));

	return 0;
}
