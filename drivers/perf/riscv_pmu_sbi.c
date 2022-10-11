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

#include <asm/errata_list.h>
#include <asm/sbi.h>
#include <asm/hwcap.h>

PMU_FORMAT_ATTR(event, "config:0-47");
PMU_FORMAT_ATTR(firmware, "config:63");

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

/*
 * RISC-V doesn't have hetergenous harts yet. This need to be part of
 * per_cpu in case of harts with different pmu counters
 */
static union sbi_pmu_ctr_info *pmu_ctr_list;
static bool riscv_pmu_use_irq;
static unsigned int riscv_pmu_irq_num;
static unsigned int riscv_pmu_irq;

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

static int pmu_sbi_ctr_get_idx(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct riscv_pmu *rvpmu = to_riscv_pmu(event->pmu);
	struct cpu_hw_events *cpuc = this_cpu_ptr(rvpmu->hw_events);
	struct sbiret ret;
	int idx;
	uint64_t cbase = 0;
	unsigned long cflags = 0;

	if (event->attr.exclude_kernel)
		cflags |= SBI_PMU_CFG_FLAG_SET_SINH;
	if (event->attr.exclude_user)
		cflags |= SBI_PMU_CFG_FLAG_SET_UINH;

	/* retrieve the available counter index */
#if defined(CONFIG_32BIT)
	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_CFG_MATCH, cbase,
			rvpmu->cmask, cflags, hwc->event_base, hwc->config,
			hwc->config >> 32);
#else
	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_CFG_MATCH, cbase,
			rvpmu->cmask, cflags, hwc->event_base, hwc->config, 0);
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
			if (raw_config_val < SBI_PMU_FW_MAX)
				ret = (raw_config_val & 0xFFFF) |
				      (SBI_PMU_EVENT_TYPE_FW << 16);
			else
				return -EINVAL;
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

static u64 pmu_sbi_ctr_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	struct sbiret ret;
	union sbi_pmu_ctr_info info;
	u64 val = 0;

	if (pmu_sbi_is_fw_event(event)) {
		ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_FW_READ,
				hwc->idx, 0, 0, 0, 0, 0);
		if (!ret.error)
			val = ret.value;
	} else {
		info = pmu_ctr_list[idx];
		val = riscv_pmu_ctr_read_csr(info.csr);
		if (IS_ENABLED(CONFIG_32BIT))
			val = ((u64)riscv_pmu_ctr_read_csr(info.csr + 0x80)) << 31 | val;
	}

	return val;
}

static void pmu_sbi_ctr_start(struct perf_event *event, u64 ival)
{
	struct sbiret ret;
	struct hw_perf_event *hwc = &event->hw;
	unsigned long flag = SBI_PMU_START_FLAG_SET_INIT_VALUE;

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
}

static void pmu_sbi_ctr_stop(struct perf_event *event, unsigned long flag)
{
	struct sbiret ret;
	struct hw_perf_event *hwc = &event->hw;

	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_STOP, hwc->idx, 1, flag, 0, 0, 0);
	if (ret.error && (ret.error != SBI_ERR_ALREADY_STOPPED) &&
		flag != SBI_PMU_STOP_FLAG_RESET)
		pr_err("Stopping counter idx %d failed with error %d\n",
			hwc->idx, sbi_err_map_linux_errno(ret.error));
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

	/* No need to check the error here as we can't do anything about the error */
	sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_STOP, 0,
		  cpu_hw_evt->used_hw_ctrs[0], 0, 0, 0, 0);
}

/*
 * This function starts all the used counters in two step approach.
 * Any counter that did not overflow can be start in a single step
 * while the overflowed counters need to be started with updated initialization
 * value.
 */
static inline void pmu_sbi_start_overflow_mask(struct riscv_pmu *pmu,
					       unsigned long ctr_ovf_mask)
{
	int idx = 0;
	struct cpu_hw_events *cpu_hw_evt = this_cpu_ptr(pmu->hw_events);
	struct perf_event *event;
	unsigned long flag = SBI_PMU_START_FLAG_SET_INIT_VALUE;
	unsigned long ctr_start_mask = 0;
	uint64_t max_period;
	struct hw_perf_event *hwc;
	u64 init_val = 0;

	ctr_start_mask = cpu_hw_evt->used_hw_ctrs[0] & ~ctr_ovf_mask;

	/* Start all the counters that did not overflow in a single shot */
	sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_START, 0, ctr_start_mask,
		  0, 0, 0, 0);

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

static irqreturn_t pmu_sbi_ovf_handler(int irq, void *dev)
{
	struct perf_sample_data data;
	struct pt_regs *regs;
	struct hw_perf_event *hw_evt;
	union sbi_pmu_ctr_info *info;
	int lidx, hidx, fidx;
	struct riscv_pmu *pmu;
	struct perf_event *event;
	unsigned long overflow;
	unsigned long overflowed_ctrs = 0;
	struct cpu_hw_events *cpu_hw_evt = dev;
	u64 start_clock = sched_clock();

	if (WARN_ON_ONCE(!cpu_hw_evt))
		return IRQ_NONE;

	/* Firmware counter don't support overflow yet */
	fidx = find_first_bit(cpu_hw_evt->used_hw_ctrs, RISCV_MAX_COUNTERS);
	event = cpu_hw_evt->events[fidx];
	if (!event) {
		csr_clear(CSR_SIP, BIT(riscv_pmu_irq_num));
		return IRQ_NONE;
	}

	pmu = to_riscv_pmu(event->pmu);
	pmu_sbi_stop_hw_ctrs(pmu);

	/* Overflow status register should only be read after counter are stopped */
	ALT_SBI_PMU_OVERFLOW(overflow);

	/*
	 * Overflow interrupt pending bit should only be cleared after stopping
	 * all the counters to avoid any race condition.
	 */
	csr_clear(CSR_SIP, BIT(riscv_pmu_irq_num));

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

		/* compute hardware counter index */
		hidx = info->csr - CSR_CYCLE;
		/* check if the corresponding bit is set in sscountovf */
		if (!(overflow & (1 << hidx)))
			continue;

		/*
		 * Keep a track of overflowed counters so that they can be started
		 * with updated initial value.
		 */
		overflowed_ctrs |= 1 << lidx;
		hw_evt = &event->hw;
		riscv_pmu_event_update(event);
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
	 * Enable the access for CYCLE, TIME, and INSTRET CSRs from userspace,
	 * as is necessary to maintain uABI compatibility.
	 */
	csr_write(CSR_SCOUNTEREN, 0x7);

	/* Stop all the counters so that they can be enabled from perf */
	pmu_sbi_stop_all(pmu);

	if (riscv_pmu_use_irq) {
		cpu_hw_evt->irq = riscv_pmu_irq;
		csr_clear(CSR_IP, BIT(riscv_pmu_irq_num));
		csr_set(CSR_IE, BIT(riscv_pmu_irq_num));
		enable_percpu_irq(riscv_pmu_irq, IRQ_TYPE_NONE);
	}

	return 0;
}

static int pmu_sbi_dying_cpu(unsigned int cpu, struct hlist_node *node)
{
	if (riscv_pmu_use_irq) {
		disable_percpu_irq(riscv_pmu_irq);
		csr_clear(CSR_IE, BIT(riscv_pmu_irq_num));
	}

	/* Disable all counters access for user mode now */
	csr_write(CSR_SCOUNTEREN, 0x0);

	return 0;
}

static int pmu_sbi_setup_irqs(struct riscv_pmu *pmu, struct platform_device *pdev)
{
	int ret;
	struct cpu_hw_events __percpu *hw_events = pmu->hw_events;
	struct device_node *cpu, *child;
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
	}

	if (!riscv_pmu_use_irq)
		return -EOPNOTSUPP;

	for_each_of_cpu_node(cpu) {
		child = of_get_compatible_child(cpu, "riscv,cpu-intc");
		if (!child) {
			pr_err("Failed to find INTC node\n");
			of_node_put(cpu);
			return -ENODEV;
		}
		domain = irq_find_host(child);
		of_node_put(child);
		if (domain) {
			of_node_put(cpu);
			break;
		}
	}
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
			 *
			 * Requires RCU read locking to be functional,
			 * wrap the call within RCU_NONIDLE to make the
			 * RCU subsystem aware this cpu is not idle from
			 * an RCU perspective for the riscv_pmu_start() call
			 * duration.
			 */
			RCU_NONIDLE(riscv_pmu_start(event, PERF_EF_RELOAD));
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
	riscv_pm_pmu_unregister(pmu);
	cpuhp_state_remove_instance(CPUHP_AP_PERF_RISCV_STARTING, &pmu->node);
}

static int pmu_sbi_device_probe(struct platform_device *pdev)
{
	struct riscv_pmu *pmu = NULL;
	unsigned long cmask = 0;
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
	pmu->cmask = cmask;
	pmu->ctr_start = pmu_sbi_ctr_start;
	pmu->ctr_stop = pmu_sbi_ctr_stop;
	pmu->event_map = pmu_sbi_event_map;
	pmu->ctr_get_idx = pmu_sbi_ctr_get_idx;
	pmu->ctr_get_width = pmu_sbi_ctr_get_width;
	pmu->ctr_clear_idx = pmu_sbi_ctr_clear_idx;
	pmu->ctr_read = pmu_sbi_ctr_read;

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_RISCV_STARTING, &pmu->node);
	if (ret)
		return ret;

	ret = riscv_pm_pmu_register(pmu);
	if (ret)
		goto out_unregister;

	ret = perf_pmu_register(&pmu->pmu, "cpu", PERF_TYPE_RAW);
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
		.name	= RISCV_PMU_PDEV_NAME,
	},
};

static int __init pmu_sbi_devinit(void)
{
	int ret;
	struct platform_device *pdev;

	if (sbi_spec_version < sbi_mk_version(0, 3) ||
	    sbi_probe_extension(SBI_EXT_PMU) <= 0) {
		return 0;
	}

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

	pdev = platform_device_register_simple(RISCV_PMU_PDEV_NAME, -1, NULL, 0);
	if (IS_ERR(pdev)) {
		platform_driver_unregister(&pmu_sbi_driver);
		return PTR_ERR(pdev);
	}

	/* Notify legacy implementation that SBI pmu is available*/
	riscv_pmu_legacy_skip_init();

	return ret;
}
device_initcall(pmu_sbi_devinit)
