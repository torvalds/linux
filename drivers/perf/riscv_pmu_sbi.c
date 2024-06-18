// SPDX-License-Identifier: GPL-2.0
/*
 * RISC-V performance counter support.
 *
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 *
 * This code is based on ARM perf event code which is in turn based on
 * sparc64 and x86 code.
 */

#define pr_fmt(fmt) "riscv-pmu-sbi: " fmt

#include <linux/mod_devicetable.h>
#include <linux/perf/riscv_pmu.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/cpu_pm.h>
#include <linux/sched/clock.h>
#include <linux/soc/andes/irq.h>

#include <asm/errata_list.h>
#include <asm/sbi.h>
#include <asm/cpufeature.h>

#define ALT_SBI_PMU_OVERFLOW(__ovl)					\
asm volatile(ALTERNATIVE_2(						\
	"csrr %0, " __stringify(CSR_SCOUNTOVF),				\
	"csrr %0, " __stringify(THEAD_C9XX_CSR_SCOUNTEROF),		\
		THEAD_VENDOR_ID, ERRATA_THEAD_PMU,			\
		CONFIG_ERRATA_THEAD_PMU,				\
	"csrr %0, " __stringify(ANDES_CSR_SCOUNTEROF),			\
		0, RISCV_ISA_EXT_XANDESPMU,				\
		CONFIG_ANDES_CUSTOM_PMU)				\
	: "=r" (__ovl) :						\
	: "memory")

#define ALT_SBI_PMU_OVF_CLEAR_PENDING(__irq_mask)			\
asm volatile(ALTERNATIVE(						\
	"csrc " __stringify(CSR_IP) ", %0\n\t",				\
	"csrc " __stringify(ANDES_CSR_SLIP) ", %0\n\t",			\
		0, RISCV_ISA_EXT_XANDESPMU,				\
		CONFIG_ANDES_CUSTOM_PMU)				\
	: : "r"(__irq_mask)						\
	: "memory")

#define SYSCTL_NO_USER_ACCESS	0
#define SYSCTL_USER_ACCESS	1
#define SYSCTL_LEGACY		2

#define PERF_EVENT_FLAG_NO_USER_ACCESS	BIT(SYSCTL_NO_USER_ACCESS)
#define PERF_EVENT_FLAG_USER_ACCESS	BIT(SYSCTL_USER_ACCESS)
#define PERF_EVENT_FLAG_LEGACY		BIT(SYSCTL_LEGACY)

PMU_FORMAT_ATTR(event, "config:0-47");
PMU_FORMAT_ATTR(firmware, "config:63");

static bool sbi_v2_available;
static DEFINE_STATIC_KEY_FALSE(sbi_pmu_snapshot_available);
#define sbi_pmu_snapshot_available() \
	static_branch_unlikely(&sbi_pmu_snapshot_available)

static struct attribute *riscv_arch_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_firmware.attr,
	NULL,
};

static struct attribute_group riscv_pmu_format_group = {
	.name = "format",
	.attrs = riscv_arch_formats_attr,
};

static const struct attribute_group *riscv_pmu_attr_groups[] = {
	&riscv_pmu_format_group,
	NULL,
};

/* Allow user mode access by default */
static int sysctl_perf_user_access __read_mostly = SYSCTL_USER_ACCESS;

/*
 * RISC-V doesn't have heterogeneous harts yet. This need to be part of
 * per_cpu in case of harts with different pmu counters
 */
static union sbi_pmu_ctr_info *pmu_ctr_list;
static bool riscv_pmu_use_irq;
static unsigned int riscv_pmu_irq_num;
static unsigned int riscv_pmu_irq_mask;
static unsigned int riscv_pmu_irq;

/* Cache the available counters in a bitmask */
static unsigned long cmask;

struct sbi_pmu_event_data {
	union {
		union {
			struct hw_gen_event {
				uint32_t event_code:16;
				uint32_t event_type:4;
				uint32_t reserved:12;
			} hw_gen_event;
			struct hw_cache_event {
				uint32_t result_id:1;
				uint32_t op_id:2;
				uint32_t cache_id:13;
				uint32_t event_type:4;
				uint32_t reserved:12;
			} hw_cache_event;
		};
		uint32_t event_idx;
	};
};

static const struct sbi_pmu_event_data pmu_hw_event_map[] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= {.hw_gen_event = {
							SBI_PMU_HW_CPU_CYCLES,
							SBI_PMU_EVENT_TYPE_HW, 0}},
	[PERF_COUNT_HW_INSTRUCTIONS]		= {.hw_gen_event = {
							SBI_PMU_HW_INSTRUCTIONS,
							SBI_PMU_EVENT_TYPE_HW, 0}},
	[PERF_COUNT_HW_CACHE_REFERENCES]	= {.hw_gen_event = {
							SBI_PMU_HW_CACHE_REFERENCES,
							SBI_PMU_EVENT_TYPE_HW, 0}},
	[PERF_COUNT_HW_CACHE_MISSES]		= {.hw_gen_event = {
							SBI_PMU_HW_CACHE_MISSES,
							SBI_PMU_EVENT_TYPE_HW, 0}},
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= {.hw_gen_event = {
							SBI_PMU_HW_BRANCH_INSTRUCTIONS,
							SBI_PMU_EVENT_TYPE_HW, 0}},
	[PERF_COUNT_HW_BRANCH_MISSES]		= {.hw_gen_event = {
							SBI_PMU_HW_BRANCH_MISSES,
							SBI_PMU_EVENT_TYPE_HW, 0}},
	[PERF_COUNT_HW_BUS_CYCLES]		= {.hw_gen_event = {
							SBI_PMU_HW_BUS_CYCLES,
							SBI_PMU_EVENT_TYPE_HW, 0}},
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= {.hw_gen_event = {
							SBI_PMU_HW_STALLED_CYCLES_FRONTEND,
							SBI_PMU_EVENT_TYPE_HW, 0}},
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= {.hw_gen_event = {
							SBI_PMU_HW_STALLED_CYCLES_BACKEND,
							SBI_PMU_EVENT_TYPE_HW, 0}},
	[PERF_COUNT_HW_REF_CPU_CYCLES]		= {.hw_gen_event = {
							SBI_PMU_HW_REF_CPU_CYCLES,
							SBI_PMU_EVENT_TYPE_HW, 0}},
};

#define C(x) PERF_COUNT_HW_CACHE_##x
static const struct sbi_pmu_event_data pmu_cache_event_map[PERF_COUNT_HW_CACHE_MAX]
[PERF_COUNT_HW_CACHE_OP_MAX]
[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_READ), C(L1D), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_READ), C(L1D), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_WRITE), C(L1D), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_WRITE), C(L1D), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_PREFETCH), C(L1D), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_PREFETCH), C(L1D), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event =	{C(RESULT_ACCESS),
					C(OP_READ), C(L1I), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS), C(OP_READ),
					C(L1I), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_WRITE), C(L1I), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_WRITE), C(L1I), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_PREFETCH), C(L1I), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_PREFETCH), C(L1I), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_READ), C(LL), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_READ), C(LL), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_WRITE), C(LL), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_WRITE), C(LL), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_PREFETCH), C(LL), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_PREFETCH), C(LL), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
	},
	[C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_READ), C(DTLB), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_READ), C(DTLB), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_WRITE), C(DTLB), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_WRITE), C(DTLB), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_PREFETCH), C(DTLB), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_PREFETCH), C(DTLB), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_READ), C(ITLB), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_READ), C(ITLB), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_WRITE), C(ITLB), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_WRITE), C(ITLB), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_PREFETCH), C(ITLB), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_PREFETCH), C(ITLB), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_READ), C(BPU), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_READ), C(BPU), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_WRITE), C(BPU), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_WRITE), C(BPU), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_PREFETCH), C(BPU), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_PREFETCH), C(BPU), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
	},
	[C(NODE)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_READ), C(NODE), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_READ), C(NODE), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_WRITE), C(NODE), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_WRITE), C(NODE), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = {.hw_cache_event = {C(RESULT_ACCESS),
					C(OP_PREFETCH), C(NODE), SBI_PMU_EVENT_TYPE_CACHE, 0}},
			[C(RESULT_MISS)] = {.hw_cache_event = {C(RESULT_MISS),
					C(OP_PREFETCH), C(NODE), SBI_PMU_EVENT_TYPE_CACHE, 0}},
		},
	},
};

static int pmu_sbi_ctr_get_width(int idx)
{
	return pmu_ctr_list[idx].width;
}

static bool pmu_sbi_ctr_is_fw(int cidx)
{
	union sbi_pmu_ctr_info *info;

	info = &pmu_ctr_list[cidx];
	if (!info)
		return false;

	return (info->type == SBI_PMU_CTR_TYPE_FW) ? true : false;
}

/*
 * Returns the counter width of a programmable counter and number of hardware
 * counters. As we don't support heterogeneous CPUs yet, it is okay to just
 * return the counter width of the first programmable counter.
 */
int riscv_pmu_get_hpm_info(u32 *hw_ctr_width, u32 *num_hw_ctr)
{
	int i;
	union sbi_pmu_ctr_info *info;
	u32 hpm_width = 0, hpm_count = 0;

	if (!cmask)
		return -EINVAL;

	for_each_set_bit(i, &cmask, RISCV_MAX_COUNTERS) {
		info = &pmu_ctr_list[i];
		if (!info)
			continue;
		if (!hpm_width && info->csr != CSR_CYCLE && info->csr != CSR_INSTRET)
			hpm_width = info->width;
		if (info->type == SBI_PMU_CTR_TYPE_HW)
			hpm_count++;
	}

	*hw_ctr_width = hpm_width;
	*num_hw_ctr = hpm_count;

	return 0;
}
EXPORT_SYMBOL_GPL(riscv_pmu_get_hpm_info);

static uint8_t pmu_sbi_csr_index(struct perf_event *event)
{
	return pmu_ctr_list[event->hw.idx].csr - CSR_CYCLE;
}

static unsigned long pmu_sbi_get_filter_flags(struct perf_event *event)
{
	unsigned long cflags = 0;
	bool guest_events = false;

	if (event->attr.config1 & RISCV_PMU_CONFIG1_GUEST_EVENTS)
		guest_events = true;
	if (event->attr.exclude_kernel)
		cflags |= guest_events ? SBI_PMU_CFG_FLAG_SET_VSINH : SBI_PMU_CFG_FLAG_SET_SINH;
	if (event->attr.exclude_user)
		cflags |= guest_events ? SBI_PMU_CFG_FLAG_SET_VUINH : SBI_PMU_CFG_FLAG_SET_UINH;
	if (guest_events && event->attr.exclude_hv)
		cflags |= SBI_PMU_CFG_FLAG_SET_SINH;
	if (event->attr.exclude_host)
		cflags |= SBI_PMU_CFG_FLAG_SET_UINH | SBI_PMU_CFG_FLAG_SET_SINH;
	if (event->attr.exclude_guest)
		cflags |= SBI_PMU_CFG_FLAG_SET_VSINH | SBI_PMU_CFG_FLAG_SET_VUINH;

	return cflags;
}

static int pmu_sbi_ctr_get_idx(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct riscv_pmu *rvpmu = to_riscv_pmu(event->pmu);
	struct cpu_hw_events *cpuc = this_cpu_ptr(rvpmu->hw_events);
	struct sbiret ret;
	int idx;
	uint64_t cbase = 0, cmask = rvpmu->cmask;
	unsigned long cflags = 0;

	cflags = pmu_sbi_get_filter_flags(event);

	/*
	 * In legacy mode, we have to force the fixed counters for those events
	 * but not in the user access mode as we want to use the other counters
	 * that support sampling/filtering.
	 */
	if (hwc->flags & PERF_EVENT_FLAG_LEGACY) {
		if (event->attr.config == PERF_COUNT_HW_CPU_CYCLES) {
			cflags |= SBI_PMU_CFG_FLAG_SKIP_MATCH;
			cmask = 1;
		} else if (event->attr.config == PERF_COUNT_HW_INSTRUCTIONS) {
			cflags |= SBI_PMU_CFG_FLAG_SKIP_MATCH;
			cmask = BIT(CSR_INSTRET - CSR_CYCLE);
		}
	}

	/* retrieve the available counter index */
#if defined(CONFIG_32BIT)
	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_CFG_MATCH, cbase,
			cmask, cflags, hwc->event_base, hwc->config,
			hwc->config >> 32);
#else
	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_CFG_MATCH, cbase,
			cmask, cflags, hwc->event_base, hwc->config, 0);
#endif
	if (ret.error) {
		pr_debug("Not able to find a counter for event %lx config %llx\n",
			hwc->event_base, hwc->config);
		return sbi_err_map_linux_errno(ret.error);
	}

	idx = ret.value;
	if (!test_bit(idx, &rvpmu->cmask) || !pmu_ctr_list[idx].value)
		return -ENOENT;

	/* Additional sanity check for the counter id */
	if (pmu_sbi_ctr_is_fw(idx)) {
		if (!test_and_set_bit(idx, cpuc->used_fw_ctrs))
			return idx;
	} else {
		if (!test_and_set_bit(idx, cpuc->used_hw_ctrs))
			return idx;
	}

	return -ENOENT;
}

static void pmu_sbi_ctr_clear_idx(struct perf_event *event)
{

	struct hw_perf_event *hwc = &event->hw;
	struct riscv_pmu *rvpmu = to_riscv_pmu(event->pmu);
	struct cpu_hw_events *cpuc = this_cpu_ptr(rvpmu->hw_events);
	int idx = hwc->idx;

	if (pmu_sbi_ctr_is_fw(idx))
		clear_bit(idx, cpuc->used_fw_ctrs);
	else
		clear_bit(idx, cpuc->used_hw_ctrs);
}

static int pmu_event_find_cache(u64 config)
{
	unsigned int cache_type, cache_op, cache_result, ret;

	cache_type = (config >>  0) & 0xff;
	if (cache_type >= PERF_COUNT_HW_CACHE_MAX)
		return -EINVAL;

	cache_op = (config >>  8) & 0xff;
	if (cache_op >= PERF_COUNT_HW_CACHE_OP_MAX)
		return -EINVAL;

	cache_result = (config >> 16) & 0xff;
	if (cache_result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	ret = pmu_cache_event_map[cache_type][cache_op][cache_result].event_idx;

	return ret;
}

static bool pmu_sbi_is_fw_event(struct perf_event *event)
{
	u32 type = event->attr.type;
	u64 config = event->attr.config;

	if ((type == PERF_TYPE_RAW) && ((config >> 63) == 1))
		return true;
	else
		return false;
}

static int pmu_sbi_event_map(struct perf_event *event, u64 *econfig)
{
	u32 type = event->attr.type;
	u64 config = event->attr.config;
	int bSoftware;
	u64 raw_config_val;
	int ret;

	switch (type) {
	case PERF_TYPE_HARDWARE:
		if (config >= PERF_COUNT_HW_MAX)
			return -EINVAL;
		ret = pmu_hw_event_map[event->attr.config].event_idx;
		break;
	case PERF_TYPE_HW_CACHE:
		ret = pmu_event_find_cache(config);
		break;
	case PERF_TYPE_RAW:
		/*
		 * As per SBI specification, the upper 16 bits must be unused for
		 * a raw event. Use the MSB (63b) to distinguish between hardware
		 * raw event and firmware events.
		 */
		bSoftware = config >> 63;
		raw_config_val = config & RISCV_PMU_RAW_EVENT_MASK;
		if (bSoftware) {
			ret = (raw_config_val & 0xFFFF) |
				(SBI_PMU_EVENT_TYPE_FW << 16);
		} else {
			ret = RISCV_PMU_RAW_EVENT_IDX;
			*econfig = raw_config_val;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static void pmu_sbi_snapshot_free(struct riscv_pmu *pmu)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct cpu_hw_events *cpu_hw_evt = per_cpu_ptr(pmu->hw_events, cpu);

		if (!cpu_hw_evt->snapshot_addr)
			continue;

		free_page((unsigned long)cpu_hw_evt->snapshot_addr);
		cpu_hw_evt->snapshot_addr = NULL;
		cpu_hw_evt->snapshot_addr_phys = 0;
	}
}

static int pmu_sbi_snapshot_alloc(struct riscv_pmu *pmu)
{
	int cpu;
	struct page *snapshot_page;

	for_each_possible_cpu(cpu) {
		struct cpu_hw_events *cpu_hw_evt = per_cpu_ptr(pmu->hw_events, cpu);

		snapshot_page = alloc_page(GFP_ATOMIC | __GFP_ZERO);
		if (!snapshot_page) {
			pmu_sbi_snapshot_free(pmu);
			return -ENOMEM;
		}
		cpu_hw_evt->snapshot_addr = page_to_virt(snapshot_page);
		cpu_hw_evt->snapshot_addr_phys = page_to_phys(snapshot_page);
	}

	return 0;
}

static int pmu_sbi_snapshot_disable(void)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_SNAPSHOT_SET_SHMEM, SBI_SHMEM_DISABLE,
			SBI_SHMEM_DISABLE, 0, 0, 0, 0);
	if (ret.error) {
		pr_warn("failed to disable snapshot shared memory\n");
		return sbi_err_map_linux_errno(ret.error);
	}

	return 0;
}

static int pmu_sbi_snapshot_setup(struct riscv_pmu *pmu, int cpu)
{
	struct cpu_hw_events *cpu_hw_evt;
	struct sbiret ret = {0};

	cpu_hw_evt = per_cpu_ptr(pmu->hw_events, cpu);
	if (!cpu_hw_evt->snapshot_addr_phys)
		return -EINVAL;

	if (cpu_hw_evt->snapshot_set_done)
		return 0;

	if (IS_ENABLED(CONFIG_32BIT))
		ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_SNAPSHOT_SET_SHMEM,
				cpu_hw_evt->snapshot_addr_phys,
				(u64)(cpu_hw_evt->snapshot_addr_phys) >> 32, 0, 0, 0, 0);
	else
		ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_SNAPSHOT_SET_SHMEM,
				cpu_hw_evt->snapshot_addr_phys, 0, 0, 0, 0, 0);

	/* Free up the snapshot area memory and fall back to SBI PMU calls without snapshot */
	if (ret.error) {
		if (ret.error != SBI_ERR_NOT_SUPPORTED)
			pr_warn("pmu snapshot setup failed with error %ld\n", ret.error);
		return sbi_err_map_linux_errno(ret.error);
	}

	memset(cpu_hw_evt->snapshot_cval_shcopy, 0, sizeof(u64) * RISCV_MAX_COUNTERS);
	cpu_hw_evt->snapshot_set_done = true;

	return 0;
}

static u64 pmu_sbi_ctr_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	struct sbiret ret;
	u64 val = 0;
	struct riscv_pmu *pmu = to_riscv_pmu(event->pmu);
	struct cpu_hw_events *cpu_hw_evt = this_cpu_ptr(pmu->hw_events);
	struct riscv_pmu_snapshot_data *sdata = cpu_hw_evt->snapshot_addr;
	union sbi_pmu_ctr_info info = pmu_ctr_list[idx];

	/* Read the value from the shared memory directly only if counter is stopped */
	if (sbi_pmu_snapshot_available() && (hwc->state & PERF_HES_STOPPED)) {
		val = sdata->ctr_values[idx];
		return val;
	}

	if (pmu_sbi_is_fw_event(event)) {
		ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_FW_READ,
				hwc->idx, 0, 0, 0, 0, 0);
		if (ret.error)
			return 0;

		val = ret.value;
		if (IS_ENABLED(CONFIG_32BIT) && sbi_v2_available && info.width >= 32) {
			ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_FW_READ_HI,
					hwc->idx, 0, 0, 0, 0, 0);
			if (!ret.error)
				val |= ((u64)ret.value << 32);
			else
				WARN_ONCE(1, "Unable to read upper 32 bits of firmware counter error: %ld\n",
					  ret.error);
		}
	} else {
		val = riscv_pmu_ctr_read_csr(info.csr);
		if (IS_ENABLED(CONFIG_32BIT))
			val |= ((u64)riscv_pmu_ctr_read_csr(info.csr + 0x80)) << 32;
	}

	return val;
}

static void pmu_sbi_set_scounteren(void *arg)
{
	struct perf_event *event = (struct perf_event *)arg;

	if (event->hw.idx != -1)
		csr_write(CSR_SCOUNTEREN,
			  csr_read(CSR_SCOUNTEREN) | BIT(pmu_sbi_csr_index(event)));
}

static void pmu_sbi_reset_scounteren(void *arg)
{
	struct perf_event *event = (struct perf_event *)arg;

	if (event->hw.idx != -1)
		csr_write(CSR_SCOUNTEREN,
			  csr_read(CSR_SCOUNTEREN) & ~BIT(pmu_sbi_csr_index(event)));
}

static void pmu_sbi_ctr_start(struct perf_event *event, u64 ival)
{
	struct sbiret ret;
	struct hw_perf_event *hwc = &event->hw;
	unsigned long flag = SBI_PMU_START_FLAG_SET_INIT_VALUE;

	/* There is no benefit setting SNAPSHOT FLAG for a single counter */
#if defined(CONFIG_32BIT)
	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_START, hwc->idx,
			1, flag, ival, ival >> 32, 0);
#else
	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_START, hwc->idx,
			1, flag, ival, 0, 0);
#endif
	if (ret.error && (ret.error != SBI_ERR_ALREADY_STARTED))
		pr_err("Starting counter idx %d failed with error %d\n",
			hwc->idx, sbi_err_map_linux_errno(ret.error));

	if ((hwc->flags & PERF_EVENT_FLAG_USER_ACCESS) &&
	    (hwc->flags & PERF_EVENT_FLAG_USER_READ_CNT))
		pmu_sbi_set_scounteren((void *)event);
}

static void pmu_sbi_ctr_stop(struct perf_event *event, unsigned long flag)
{
	struct sbiret ret;
	struct hw_perf_event *hwc = &event->hw;
	struct riscv_pmu *pmu = to_riscv_pmu(event->pmu);
	struct cpu_hw_events *cpu_hw_evt = this_cpu_ptr(pmu->hw_events);
	struct riscv_pmu_snapshot_data *sdata = cpu_hw_evt->snapshot_addr;

	if ((hwc->flags & PERF_EVENT_FLAG_USER_ACCESS) &&
	    (hwc->flags & PERF_EVENT_FLAG_USER_READ_CNT))
		pmu_sbi_reset_scounteren((void *)event);

	if (sbi_pmu_snapshot_available())
		flag |= SBI_PMU_STOP_FLAG_TAKE_SNAPSHOT;

	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_STOP, hwc->idx, 1, flag, 0, 0, 0);
	if (!ret.error && sbi_pmu_snapshot_available()) {
		/*
		 * The counter snapshot is based on the index base specified by hwc->idx.
		 * The actual counter value is updated in shared memory at index 0 when counter
		 * mask is 0x01. To ensure accurate counter values, it's necessary to transfer
		 * the counter value to shared memory. However, if hwc->idx is zero, the counter
		 * value is already correctly updated in shared memory, requiring no further
		 * adjustment.
		 */
		if (hwc->idx > 0) {
			sdata->ctr_values[hwc->idx] = sdata->ctr_values[0];
			sdata->ctr_values[0] = 0;
		}
	} else if (ret.error && (ret.error != SBI_ERR_ALREADY_STOPPED) &&
		flag != SBI_PMU_STOP_FLAG_RESET) {
		pr_err("Stopping counter idx %d failed with error %d\n",
			hwc->idx, sbi_err_map_linux_errno(ret.error));
	}
}

static int pmu_sbi_find_num_ctrs(void)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_NUM_COUNTERS, 0, 0, 0, 0, 0, 0);
	if (!ret.error)
		return ret.value;
	else
		return sbi_err_map_linux_errno(ret.error);
}

static int pmu_sbi_get_ctrinfo(int nctr, unsigned long *mask)
{
	struct sbiret ret;
	int i, num_hw_ctr = 0, num_fw_ctr = 0;
	union sbi_pmu_ctr_info cinfo;

	pmu_ctr_list = kcalloc(nctr, sizeof(*pmu_ctr_list), GFP_KERNEL);
	if (!pmu_ctr_list)
		return -ENOMEM;

	for (i = 0; i < nctr; i++) {
		ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_GET_INFO, i, 0, 0, 0, 0, 0);
		if (ret.error)
			/* The logical counter ids are not expected to be contiguous */
			continue;

		*mask |= BIT(i);

		cinfo.value = ret.value;
		if (cinfo.type == SBI_PMU_CTR_TYPE_FW)
			num_fw_ctr++;
		else
			num_hw_ctr++;
		pmu_ctr_list[i].value = cinfo.value;
	}

	pr_info("%d firmware and %d hardware counters\n", num_fw_ctr, num_hw_ctr);

	return 0;
}

static inline void pmu_sbi_stop_all(struct riscv_pmu *pmu)
{
	/*
	 * No need to check the error because we are disabling all the counters
	 * which may include counters that are not enabled yet.
	 */
	sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_STOP,
		  0, pmu->cmask, 0, 0, 0, 0);
}

static inline void pmu_sbi_stop_hw_ctrs(struct riscv_pmu *pmu)
{
	struct cpu_hw_events *cpu_hw_evt = this_cpu_ptr(pmu->hw_events);
	struct riscv_pmu_snapshot_data *sdata = cpu_hw_evt->snapshot_addr;
	unsigned long flag = 0;
	int i, idx;
	struct sbiret ret;
	u64 temp_ctr_overflow_mask = 0;

	if (sbi_pmu_snapshot_available())
		flag = SBI_PMU_STOP_FLAG_TAKE_SNAPSHOT;

	/* Reset the shadow copy to avoid save/restore any value from previous overflow */
	memset(cpu_hw_evt->snapshot_cval_shcopy, 0, sizeof(u64) * RISCV_MAX_COUNTERS);

	for (i = 0; i < BITS_TO_LONGS(RISCV_MAX_COUNTERS); i++) {
		/* No need to check the error here as we can't do anything about the error */
		ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_STOP, i * BITS_PER_LONG,
				cpu_hw_evt->used_hw_ctrs[i], flag, 0, 0, 0);
		if (!ret.error && sbi_pmu_snapshot_available()) {
			/* Save the counter values to avoid clobbering */
			for_each_set_bit(idx, &cpu_hw_evt->used_hw_ctrs[i], BITS_PER_LONG)
				cpu_hw_evt->snapshot_cval_shcopy[i * BITS_PER_LONG + idx] =
							sdata->ctr_values[idx];
			/* Save the overflow mask to avoid clobbering */
			temp_ctr_overflow_mask |= sdata->ctr_overflow_mask << (i * BITS_PER_LONG);
		}
	}

	/* Restore the counter values to the shared memory for used hw counters */
	if (sbi_pmu_snapshot_available()) {
		for_each_set_bit(idx, cpu_hw_evt->used_hw_ctrs, RISCV_MAX_COUNTERS)
			sdata->ctr_values[idx] = cpu_hw_evt->snapshot_cval_shcopy[idx];
		if (temp_ctr_overflow_mask)
			sdata->ctr_overflow_mask = temp_ctr_overflow_mask;
	}
}

/*
 * This function starts all the used counters in two step approach.
 * Any counter that did not overflow can be start in a single step
 * while the overflowed counters need to be started with updated initialization
 * value.
 */
static inline void pmu_sbi_start_ovf_ctrs_sbi(struct cpu_hw_events *cpu_hw_evt,
					      u64 ctr_ovf_mask)
{
	int idx = 0, i;
	struct perf_event *event;
	unsigned long flag = SBI_PMU_START_FLAG_SET_INIT_VALUE;
	unsigned long ctr_start_mask = 0;
	uint64_t max_period;
	struct hw_perf_event *hwc;
	u64 init_val = 0;

	for (i = 0; i < BITS_TO_LONGS(RISCV_MAX_COUNTERS); i++) {
		ctr_start_mask = cpu_hw_evt->used_hw_ctrs[i] & ~ctr_ovf_mask;
		/* Start all the counters that did not overflow in a single shot */
		sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_START, i * BITS_PER_LONG, ctr_start_mask,
			0, 0, 0, 0);
	}

	/* Reinitialize and start all the counter that overflowed */
	while (ctr_ovf_mask) {
		if (ctr_ovf_mask & 0x01) {
			event = cpu_hw_evt->events[idx];
			hwc = &event->hw;
			max_period = riscv_pmu_ctr_get_width_mask(event);
			init_val = local64_read(&hwc->prev_count) & max_period;
#if defined(CONFIG_32BIT)
			sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_START, idx, 1,
				  flag, init_val, init_val >> 32, 0);
#else
			sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_START, idx, 1,
				  flag, init_val, 0, 0);
#endif
			perf_event_update_userpage(event);
		}
		ctr_ovf_mask = ctr_ovf_mask >> 1;
		idx++;
	}
}

static inline void pmu_sbi_start_ovf_ctrs_snapshot(struct cpu_hw_events *cpu_hw_evt,
						   u64 ctr_ovf_mask)
{
	int i, idx = 0;
	struct perf_event *event;
	unsigned long flag = SBI_PMU_START_FLAG_INIT_SNAPSHOT;
	u64 max_period, init_val = 0;
	struct hw_perf_event *hwc;
	struct riscv_pmu_snapshot_data *sdata = cpu_hw_evt->snapshot_addr;

	for_each_set_bit(idx, cpu_hw_evt->used_hw_ctrs, RISCV_MAX_COUNTERS) {
		if (ctr_ovf_mask & BIT(idx)) {
			event = cpu_hw_evt->events[idx];
			hwc = &event->hw;
			max_period = riscv_pmu_ctr_get_width_mask(event);
			init_val = local64_read(&hwc->prev_count) & max_period;
			cpu_hw_evt->snapshot_cval_shcopy[idx] = init_val;
		}
		/*
		 * We do not need to update the non-overflow counters the previous
		 * value should have been there already.
		 */
	}

	for (i = 0; i < BITS_TO_LONGS(RISCV_MAX_COUNTERS); i++) {
		/* Restore the counter values to relative indices for used hw counters */
		for_each_set_bit(idx, &cpu_hw_evt->used_hw_ctrs[i], BITS_PER_LONG)
			sdata->ctr_values[idx] =
					cpu_hw_evt->snapshot_cval_shcopy[idx + i * BITS_PER_LONG];
		/* Start all the counters in a single shot */
		sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_START, idx * BITS_PER_LONG,
			  cpu_hw_evt->used_hw_ctrs[i], flag, 0, 0, 0);
	}
}

static void pmu_sbi_start_overflow_mask(struct riscv_pmu *pmu,
					u64 ctr_ovf_mask)
{
	struct cpu_hw_events *cpu_hw_evt = this_cpu_ptr(pmu->hw_events);

	if (sbi_pmu_snapshot_available())
		pmu_sbi_start_ovf_ctrs_snapshot(cpu_hw_evt, ctr_ovf_mask);
	else
		pmu_sbi_start_ovf_ctrs_sbi(cpu_hw_evt, ctr_ovf_mask);
}

static irqreturn_t pmu_sbi_ovf_handler(int irq, void *dev)
{
	struct perf_sample_data data;
	struct pt_regs *regs;
	struct hw_perf_event *hw_evt;
	union sbi_pmu_ctr_info *info;
	int lidx, hidx, fidx;
	struct riscv_pmu *pmu;
	struct perf_event *event;
	u64 overflow;
	u64 overflowed_ctrs = 0;
	struct cpu_hw_events *cpu_hw_evt = dev;
	u64 start_clock = sched_clock();
	struct riscv_pmu_snapshot_data *sdata = cpu_hw_evt->snapshot_addr;

	if (WARN_ON_ONCE(!cpu_hw_evt))
		return IRQ_NONE;

	/* Firmware counter don't support overflow yet */
	fidx = find_first_bit(cpu_hw_evt->used_hw_ctrs, RISCV_MAX_COUNTERS);
	if (fidx == RISCV_MAX_COUNTERS) {
		csr_clear(CSR_SIP, BIT(riscv_pmu_irq_num));
		return IRQ_NONE;
	}

	event = cpu_hw_evt->events[fidx];
	if (!event) {
		ALT_SBI_PMU_OVF_CLEAR_PENDING(riscv_pmu_irq_mask);
		return IRQ_NONE;
	}

	pmu = to_riscv_pmu(event->pmu);
	pmu_sbi_stop_hw_ctrs(pmu);

	/* Overflow status register should only be read after counter are stopped */
	if (sbi_pmu_snapshot_available())
		overflow = sdata->ctr_overflow_mask;
	else
		ALT_SBI_PMU_OVERFLOW(overflow);

	/*
	 * Overflow interrupt pending bit should only be cleared after stopping
	 * all the counters to avoid any race condition.
	 */
	ALT_SBI_PMU_OVF_CLEAR_PENDING(riscv_pmu_irq_mask);

	/* No overflow bit is set */
	if (!overflow)
		return IRQ_NONE;

	regs = get_irq_regs();

	for_each_set_bit(lidx, cpu_hw_evt->used_hw_ctrs, RISCV_MAX_COUNTERS) {
		struct perf_event *event = cpu_hw_evt->events[lidx];

		/* Skip if invalid event or user did not request a sampling */
		if (!event || !is_sampling_event(event))
			continue;

		info = &pmu_ctr_list[lidx];
		/* Do a sanity check */
		if (!info || info->type != SBI_PMU_CTR_TYPE_HW)
			continue;

		if (sbi_pmu_snapshot_available())
			/* SBI implementation already updated the logical indicies */
			hidx = lidx;
		else
			/* compute hardware counter index */
			hidx = info->csr - CSR_CYCLE;

		/* check if the corresponding bit is set in sscountovf or overflow mask in shmem */
		if (!(overflow & BIT(hidx)))
			continue;

		/*
		 * Keep a track of overflowed counters so that they can be started
		 * with updated initial value.
		 */
		overflowed_ctrs |= BIT(lidx);
		hw_evt = &event->hw;
		/* Update the event states here so that we know the state while reading */
		hw_evt->state |= PERF_HES_STOPPED;
		riscv_pmu_event_update(event);
		hw_evt->state |= PERF_HES_UPTODATE;
		perf_sample_data_init(&data, 0, hw_evt->last_period);
		if (riscv_pmu_event_set_period(event)) {
			/*
			 * Unlike other ISAs, RISC-V don't have to disable interrupts
			 * to avoid throttling here. As per the specification, the
			 * interrupt remains disabled until the OF bit is set.
			 * Interrupts are enabled again only during the start.
			 * TODO: We will need to stop the guest counters once
			 * virtualization support is added.
			 */
			perf_event_overflow(event, &data, regs);
		}
		/* Reset the state as we are going to start the counter after the loop */
		hw_evt->state = 0;
	}

	pmu_sbi_start_overflow_mask(pmu, overflowed_ctrs);
	perf_sample_event_took(sched_clock() - start_clock);

	return IRQ_HANDLED;
}

static int pmu_sbi_starting_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct riscv_pmu *pmu = hlist_entry_safe(node, struct riscv_pmu, node);
	struct cpu_hw_events *cpu_hw_evt = this_cpu_ptr(pmu->hw_events);

	/*
	 * We keep enabling userspace access to CYCLE, TIME and INSTRET via the
	 * legacy option but that will be removed in the future.
	 */
	if (sysctl_perf_user_access == SYSCTL_LEGACY)
		csr_write(CSR_SCOUNTEREN, 0x7);
	else
		csr_write(CSR_SCOUNTEREN, 0x2);

	/* Stop all the counters so that they can be enabled from perf */
	pmu_sbi_stop_all(pmu);

	if (riscv_pmu_use_irq) {
		cpu_hw_evt->irq = riscv_pmu_irq;
		ALT_SBI_PMU_OVF_CLEAR_PENDING(riscv_pmu_irq_mask);
		enable_percpu_irq(riscv_pmu_irq, IRQ_TYPE_NONE);
	}

	if (sbi_pmu_snapshot_available())
		return pmu_sbi_snapshot_setup(pmu, cpu);

	return 0;
}

static int pmu_sbi_dying_cpu(unsigned int cpu, struct hlist_node *node)
{
	if (riscv_pmu_use_irq) {
		disable_percpu_irq(riscv_pmu_irq);
	}

	/* Disable all counters access for user mode now */
	csr_write(CSR_SCOUNTEREN, 0x0);

	if (sbi_pmu_snapshot_available())
		return pmu_sbi_snapshot_disable();

	return 0;
}

static int pmu_sbi_setup_irqs(struct riscv_pmu *pmu, struct platform_device *pdev)
{
	int ret;
	struct cpu_hw_events __percpu *hw_events = pmu->hw_events;
	struct irq_domain *domain = NULL;

	if (riscv_isa_extension_available(NULL, SSCOFPMF)) {
		riscv_pmu_irq_num = RV_IRQ_PMU;
		riscv_pmu_use_irq = true;
	} else if (IS_ENABLED(CONFIG_ERRATA_THEAD_PMU) &&
		   riscv_cached_mvendorid(0) == THEAD_VENDOR_ID &&
		   riscv_cached_marchid(0) == 0 &&
		   riscv_cached_mimpid(0) == 0) {
		riscv_pmu_irq_num = THEAD_C9XX_RV_IRQ_PMU;
		riscv_pmu_use_irq = true;
	} else if (riscv_isa_extension_available(NULL, XANDESPMU) &&
		   IS_ENABLED(CONFIG_ANDES_CUSTOM_PMU)) {
		riscv_pmu_irq_num = ANDES_SLI_CAUSE_BASE + ANDES_RV_IRQ_PMOVI;
		riscv_pmu_use_irq = true;
	}

	riscv_pmu_irq_mask = BIT(riscv_pmu_irq_num % BITS_PER_LONG);

	if (!riscv_pmu_use_irq)
		return -EOPNOTSUPP;

	domain = irq_find_matching_fwnode(riscv_get_intc_hwnode(),
					  DOMAIN_BUS_ANY);
	if (!domain) {
		pr_err("Failed to find INTC IRQ root domain\n");
		return -ENODEV;
	}

	riscv_pmu_irq = irq_create_mapping(domain, riscv_pmu_irq_num);
	if (!riscv_pmu_irq) {
		pr_err("Failed to map PMU interrupt for node\n");
		return -ENODEV;
	}

	ret = request_percpu_irq(riscv_pmu_irq, pmu_sbi_ovf_handler, "riscv-pmu", hw_events);
	if (ret) {
		pr_err("registering percpu irq failed [%d]\n", ret);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_CPU_PM
static int riscv_pm_pmu_notify(struct notifier_block *b, unsigned long cmd,
				void *v)
{
	struct riscv_pmu *rvpmu = container_of(b, struct riscv_pmu, riscv_pm_nb);
	struct cpu_hw_events *cpuc = this_cpu_ptr(rvpmu->hw_events);
	int enabled = bitmap_weight(cpuc->used_hw_ctrs, RISCV_MAX_COUNTERS);
	struct perf_event *event;
	int idx;

	if (!enabled)
		return NOTIFY_OK;

	for (idx = 0; idx < RISCV_MAX_COUNTERS; idx++) {
		event = cpuc->events[idx];
		if (!event)
			continue;

		switch (cmd) {
		case CPU_PM_ENTER:
			/*
			 * Stop and update the counter
			 */
			riscv_pmu_stop(event, PERF_EF_UPDATE);
			break;
		case CPU_PM_EXIT:
		case CPU_PM_ENTER_FAILED:
			/*
			 * Restore and enable the counter.
			 */
			riscv_pmu_start(event, PERF_EF_RELOAD);
			break;
		default:
			break;
		}
	}

	return NOTIFY_OK;
}

static int riscv_pm_pmu_register(struct riscv_pmu *pmu)
{
	pmu->riscv_pm_nb.notifier_call = riscv_pm_pmu_notify;
	return cpu_pm_register_notifier(&pmu->riscv_pm_nb);
}

static void riscv_pm_pmu_unregister(struct riscv_pmu *pmu)
{
	cpu_pm_unregister_notifier(&pmu->riscv_pm_nb);
}
#else
static inline int riscv_pm_pmu_register(struct riscv_pmu *pmu) { return 0; }
static inline void riscv_pm_pmu_unregister(struct riscv_pmu *pmu) { }
#endif

static void riscv_pmu_destroy(struct riscv_pmu *pmu)
{
	if (sbi_v2_available) {
		if (sbi_pmu_snapshot_available()) {
			pmu_sbi_snapshot_disable();
			pmu_sbi_snapshot_free(pmu);
		}
	}
	riscv_pm_pmu_unregister(pmu);
	cpuhp_state_remove_instance(CPUHP_AP_PERF_RISCV_STARTING, &pmu->node);
}

static void pmu_sbi_event_init(struct perf_event *event)
{
	/*
	 * The permissions are set at event_init so that we do not depend
	 * on the sysctl value that can change.
	 */
	if (sysctl_perf_user_access == SYSCTL_NO_USER_ACCESS)
		event->hw.flags |= PERF_EVENT_FLAG_NO_USER_ACCESS;
	else if (sysctl_perf_user_access == SYSCTL_USER_ACCESS)
		event->hw.flags |= PERF_EVENT_FLAG_USER_ACCESS;
	else
		event->hw.flags |= PERF_EVENT_FLAG_LEGACY;
}

static void pmu_sbi_event_mapped(struct perf_event *event, struct mm_struct *mm)
{
	if (event->hw.flags & PERF_EVENT_FLAG_NO_USER_ACCESS)
		return;

	if (event->hw.flags & PERF_EVENT_FLAG_LEGACY) {
		if (event->attr.config != PERF_COUNT_HW_CPU_CYCLES &&
		    event->attr.config != PERF_COUNT_HW_INSTRUCTIONS) {
			return;
		}
	}

	/*
	 * The user mmapped the event to directly access it: this is where
	 * we determine based on sysctl_perf_user_access if we grant userspace
	 * the direct access to this event. That means that within the same
	 * task, some events may be directly accessible and some other may not,
	 * if the user changes the value of sysctl_perf_user_accesss in the
	 * meantime.
	 */

	event->hw.flags |= PERF_EVENT_FLAG_USER_READ_CNT;

	/*
	 * We must enable userspace access *before* advertising in the user page
	 * that it is possible to do so to avoid any race.
	 * And we must notify all cpus here because threads that currently run
	 * on other cpus will try to directly access the counter too without
	 * calling pmu_sbi_ctr_start.
	 */
	if (event->hw.flags & PERF_EVENT_FLAG_USER_ACCESS)
		on_each_cpu_mask(mm_cpumask(mm),
				 pmu_sbi_set_scounteren, (void *)event, 1);
}

static void pmu_sbi_event_unmapped(struct perf_event *event, struct mm_struct *mm)
{
	if (event->hw.flags & PERF_EVENT_FLAG_NO_USER_ACCESS)
		return;

	if (event->hw.flags & PERF_EVENT_FLAG_LEGACY) {
		if (event->attr.config != PERF_COUNT_HW_CPU_CYCLES &&
		    event->attr.config != PERF_COUNT_HW_INSTRUCTIONS) {
			return;
		}
	}

	/*
	 * Here we can directly remove user access since the user does not have
	 * access to the user page anymore so we avoid the racy window where the
	 * user could have read cap_user_rdpmc to true right before we disable
	 * it.
	 */
	event->hw.flags &= ~PERF_EVENT_FLAG_USER_READ_CNT;

	if (event->hw.flags & PERF_EVENT_FLAG_USER_ACCESS)
		on_each_cpu_mask(mm_cpumask(mm),
				 pmu_sbi_reset_scounteren, (void *)event, 1);
}

static void riscv_pmu_update_counter_access(void *info)
{
	if (sysctl_perf_user_access == SYSCTL_LEGACY)
		csr_write(CSR_SCOUNTEREN, 0x7);
	else
		csr_write(CSR_SCOUNTEREN, 0x2);
}

static int riscv_pmu_proc_user_access_handler(struct ctl_table *table,
					      int write, void *buffer,
					      size_t *lenp, loff_t *ppos)
{
	int prev = sysctl_perf_user_access;
	int ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);

	/*
	 * Test against the previous value since we clear SCOUNTEREN when
	 * sysctl_perf_user_access is set to SYSCTL_USER_ACCESS, but we should
	 * not do that if that was already the case.
	 */
	if (ret || !write || prev == sysctl_perf_user_access)
		return ret;

	on_each_cpu(riscv_pmu_update_counter_access, NULL, 1);

	return 0;
}

static struct ctl_table sbi_pmu_sysctl_table[] = {
	{
		.procname       = "perf_user_access",
		.data		= &sysctl_perf_user_access,
		.maxlen		= sizeof(unsigned int),
		.mode           = 0644,
		.proc_handler	= riscv_pmu_proc_user_access_handler,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
	},
};

static int pmu_sbi_device_probe(struct platform_device *pdev)
{
	struct riscv_pmu *pmu = NULL;
	int ret = -ENODEV;
	int num_counters;

	pr_info("SBI PMU extension is available\n");
	pmu = riscv_pmu_alloc();
	if (!pmu)
		return -ENOMEM;

	num_counters = pmu_sbi_find_num_ctrs();
	if (num_counters < 0) {
		pr_err("SBI PMU extension doesn't provide any counters\n");
		goto out_free;
	}

	/* It is possible to get from SBI more than max number of counters */
	if (num_counters > RISCV_MAX_COUNTERS) {
		num_counters = RISCV_MAX_COUNTERS;
		pr_info("SBI returned more than maximum number of counters. Limiting the number of counters to %d\n", num_counters);
	}

	/* cache all the information about counters now */
	if (pmu_sbi_get_ctrinfo(num_counters, &cmask))
		goto out_free;

	ret = pmu_sbi_setup_irqs(pmu, pdev);
	if (ret < 0) {
		pr_info("Perf sampling/filtering is not supported as sscof extension is not available\n");
		pmu->pmu.capabilities |= PERF_PMU_CAP_NO_INTERRUPT;
		pmu->pmu.capabilities |= PERF_PMU_CAP_NO_EXCLUDE;
	}

	pmu->pmu.attr_groups = riscv_pmu_attr_groups;
	pmu->pmu.parent = &pdev->dev;
	pmu->cmask = cmask;
	pmu->ctr_start = pmu_sbi_ctr_start;
	pmu->ctr_stop = pmu_sbi_ctr_stop;
	pmu->event_map = pmu_sbi_event_map;
	pmu->ctr_get_idx = pmu_sbi_ctr_get_idx;
	pmu->ctr_get_width = pmu_sbi_ctr_get_width;
	pmu->ctr_clear_idx = pmu_sbi_ctr_clear_idx;
	pmu->ctr_read = pmu_sbi_ctr_read;
	pmu->event_init = pmu_sbi_event_init;
	pmu->event_mapped = pmu_sbi_event_mapped;
	pmu->event_unmapped = pmu_sbi_event_unmapped;
	pmu->csr_index = pmu_sbi_csr_index;

	ret = riscv_pm_pmu_register(pmu);
	if (ret)
		goto out_unregister;

	ret = perf_pmu_register(&pmu->pmu, "cpu", PERF_TYPE_RAW);
	if (ret)
		goto out_unregister;

	/* SBI PMU Snapsphot is only available in SBI v2.0 */
	if (sbi_v2_available) {
		ret = pmu_sbi_snapshot_alloc(pmu);
		if (ret)
			goto out_unregister;

		ret = pmu_sbi_snapshot_setup(pmu, smp_processor_id());
		if (ret) {
			/* Snapshot is an optional feature. Continue if not available */
			pmu_sbi_snapshot_free(pmu);
		} else {
			pr_info("SBI PMU snapshot detected\n");
			/*
			 * We enable it once here for the boot cpu. If snapshot shmem setup
			 * fails during cpu hotplug process, it will fail to start the cpu
			 * as we can not handle hetergenous PMUs with different snapshot
			 * capability.
			 */
			static_branch_enable(&sbi_pmu_snapshot_available);
		}
	}

	register_sysctl("kernel", sbi_pmu_sysctl_table);

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_RISCV_STARTING, &pmu->node);
	if (ret)
		goto out_unregister;

	return 0;

out_unregister:
	riscv_pmu_destroy(pmu);

out_free:
	kfree(pmu);
	return ret;
}

static struct platform_driver pmu_sbi_driver = {
	.probe		= pmu_sbi_device_probe,
	.driver		= {
		.name	= RISCV_PMU_SBI_PDEV_NAME,
	},
};

static int __init pmu_sbi_devinit(void)
{
	int ret;
	struct platform_device *pdev;

	if (sbi_spec_version < sbi_mk_version(0, 3) ||
	    !sbi_probe_extension(SBI_EXT_PMU)) {
		return 0;
	}

	if (sbi_spec_version >= sbi_mk_version(2, 0))
		sbi_v2_available = true;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_RISCV_STARTING,
				      "perf/riscv/pmu:starting",
				      pmu_sbi_starting_cpu, pmu_sbi_dying_cpu);
	if (ret) {
		pr_err("CPU hotplug notifier could not be registered: %d\n",
		       ret);
		return ret;
	}

	ret = platform_driver_register(&pmu_sbi_driver);
	if (ret)
		return ret;

	pdev = platform_device_register_simple(RISCV_PMU_SBI_PDEV_NAME, -1, NULL, 0);
	if (IS_ERR(pdev)) {
		platform_driver_unregister(&pmu_sbi_driver);
		return PTR_ERR(pdev);
	}

	/* Notify legacy implementation that SBI pmu is available*/
	riscv_pmu_legacy_skip_init();

	return ret;
}
device_initcall(pmu_sbi_devinit)
