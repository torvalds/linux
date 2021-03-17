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

#include <asm/sbi.h>

union sbi_pmu_ctr_info {
	unsigned long value;
	struct {
		unsigned long csr:12;
		unsigned long width:6;
#if __riscv_xlen == 32
		unsigned long reserved:13;
#else
		unsigned long reserved:45;
#endif
		unsigned long type:1;
	};
};

/**
 * RISC-V doesn't have hetergenous harts yet. This need to be part of
 * per_cpu in case of harts with different pmu counters
 */
static union sbi_pmu_ctr_info *pmu_ctr_list;

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
	uint64_t cmask = GENMASK_ULL(rvpmu->num_counters - 1, 0);
	unsigned long cflags = 0;

	if (event->attr.exclude_kernel)
		cflags |= SBI_PMU_CFG_FLAG_SET_SINH;
	if (event->attr.exclude_user)
		cflags |= SBI_PMU_CFG_FLAG_SET_UINH;

	/* retrieve the available counter index */
	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_CFG_MATCH, cbase, cmask,
			cflags, hwc->event_base, hwc->config, 0);
	if (ret.error) {
		pr_debug("Not able to find a counter for event %lx config %llx\n",
			hwc->event_base, hwc->config);
		return sbi_err_map_linux_errno(ret.error);
	}

	idx = ret.value;
	if (idx >= rvpmu->num_counters || !pmu_ctr_list[idx].value)
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

	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_START, hwc->idx,
			1, flag, ival, ival >> 32, 0);
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

static int pmu_sbi_get_ctrinfo(int nctr)
{
	struct sbiret ret;
	int i, num_hw_ctr = 0, num_fw_ctr = 0;
	union sbi_pmu_ctr_info cinfo;

	pmu_ctr_list = kcalloc(nctr, sizeof(*pmu_ctr_list), GFP_KERNEL);
	if (!pmu_ctr_list)
		return -ENOMEM;

	for (i = 0; i <= nctr; i++) {
		ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_GET_INFO, i, 0, 0, 0, 0, 0);
		if (ret.error)
			/* The logical counter ids are not expected to be contiguous */
			continue;
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

static int pmu_sbi_starting_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct riscv_pmu *pmu = hlist_entry_safe(node, struct riscv_pmu, node);

	/* Enable the access for TIME csr only from the user mode now */
	csr_write(CSR_SCOUNTEREN, 0x2);

	/* Stop all the counters so that they can be enabled from perf */
	sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_STOP,
		  0, GENMASK_ULL(pmu->num_counters - 1, 0), 0, 0, 0, 0);

	return 0;
}

static int pmu_sbi_dying_cpu(unsigned int cpu, struct hlist_node *node)
{
	/* Disable all counters access for user mode now */
	csr_write(CSR_SCOUNTEREN, 0x0);

	return 0;
}

static int pmu_sbi_device_probe(struct platform_device *pdev)
{
	struct riscv_pmu *pmu = NULL;
	int num_counters;
	int ret;

	pr_info("SBI PMU extension is available\n");
	pmu = riscv_pmu_alloc();
	if (!pmu)
		return -ENOMEM;

	num_counters = pmu_sbi_find_num_ctrs();
	if (num_counters < 0) {
		pr_err("SBI PMU extension doesn't provide any counters\n");
		return -ENODEV;
	}

	/* cache all the information about counters now */
	if (pmu_sbi_get_ctrinfo(num_counters))
		return -ENODEV;

	pmu->num_counters = num_counters;
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

	ret = perf_pmu_register(&pmu->pmu, "cpu", PERF_TYPE_RAW);
	if (ret) {
		cpuhp_state_remove_instance(CPUHP_AP_PERF_RISCV_STARTING, &pmu->node);
		return ret;
	}

	return 0;
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
