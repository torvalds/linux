// SPDX-License-Identifier: GPL-2.0
/*
 * RISC-V performance counter support.
 *
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 *
 * This implementation is based on old RISC-V perf and ARM perf event code
 * which are in turn based on sparc64 and x86 code.
 */

#include <linux/mod_devicetable.h>
#include <linux/perf/riscv_pmu.h>
#include <linux/platform_device.h>

#define RISCV_PMU_LEGACY_CYCLE		0
#define RISCV_PMU_LEGACY_INSTRET	2

static bool pmu_init_done;

static int pmu_legacy_ctr_get_idx(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;

	if (event->attr.type != PERF_TYPE_HARDWARE)
		return -ENOENT;
	if (attr->config == PERF_COUNT_HW_CPU_CYCLES)
		return RISCV_PMU_LEGACY_CYCLE;
	else if (attr->config == PERF_COUNT_HW_INSTRUCTIONS)
		return RISCV_PMU_LEGACY_INSTRET;
	else
		return -ENOENT;
}

/* For legacy config & counter index are same */
static int pmu_legacy_event_map(struct perf_event *event, u64 *config)
{
	return pmu_legacy_ctr_get_idx(event);
}

/* cycle & instret are always 64 bit, one bit less according to SBI spec */
static int pmu_legacy_ctr_get_width(int idx)
{
	return 63;
}

static u64 pmu_legacy_read_ctr(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	u64 val;

	if (idx == RISCV_PMU_LEGACY_CYCLE) {
		val = riscv_pmu_ctr_read_csr(CSR_CYCLE);
		if (IS_ENABLED(CONFIG_32BIT))
			val = (u64)riscv_pmu_ctr_read_csr(CSR_CYCLEH) << 32 | val;
	} else if (idx == RISCV_PMU_LEGACY_INSTRET) {
		val = riscv_pmu_ctr_read_csr(CSR_INSTRET);
		if (IS_ENABLED(CONFIG_32BIT))
			val = ((u64)riscv_pmu_ctr_read_csr(CSR_INSTRETH)) << 32 | val;
	} else
		return 0;

	return val;
}

static void pmu_legacy_ctr_start(struct perf_event *event, u64 ival)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 initial_val = pmu_legacy_read_ctr(event);

	/**
	 * The legacy method doesn't really have a start/stop method.
	 * It also can not update the counter with a initial value.
	 * But we still need to set the prev_count so that read() can compute
	 * the delta. Just use the current counter value to set the prev_count.
	 */
	local64_set(&hwc->prev_count, initial_val);
}

static uint8_t pmu_legacy_csr_index(struct perf_event *event)
{
	return event->hw.idx;
}

static void pmu_legacy_event_mapped(struct perf_event *event, struct mm_struct *mm)
{
	if (event->attr.config != PERF_COUNT_HW_CPU_CYCLES &&
	    event->attr.config != PERF_COUNT_HW_INSTRUCTIONS)
		return;

	event->hw.flags |= PERF_EVENT_FLAG_USER_READ_CNT;
}

static void pmu_legacy_event_unmapped(struct perf_event *event, struct mm_struct *mm)
{
	if (event->attr.config != PERF_COUNT_HW_CPU_CYCLES &&
	    event->attr.config != PERF_COUNT_HW_INSTRUCTIONS)
		return;

	event->hw.flags &= ~PERF_EVENT_FLAG_USER_READ_CNT;
}

/*
 * This is just a simple implementation to allow legacy implementations
 * compatible with new RISC-V PMU driver framework.
 * This driver only allows reading two counters i.e CYCLE & INSTRET.
 * However, it can not start or stop the counter. Thus, it is not very useful
 * will be removed in future.
 */
static void pmu_legacy_init(struct riscv_pmu *pmu)
{
	pr_info("Legacy PMU implementation is available\n");

	pmu->cmask = BIT(RISCV_PMU_LEGACY_CYCLE) |
		BIT(RISCV_PMU_LEGACY_INSTRET);
	pmu->ctr_start = pmu_legacy_ctr_start;
	pmu->ctr_stop = NULL;
	pmu->event_map = pmu_legacy_event_map;
	pmu->ctr_get_idx = pmu_legacy_ctr_get_idx;
	pmu->ctr_get_width = pmu_legacy_ctr_get_width;
	pmu->ctr_clear_idx = NULL;
	pmu->ctr_read = pmu_legacy_read_ctr;
	pmu->event_mapped = pmu_legacy_event_mapped;
	pmu->event_unmapped = pmu_legacy_event_unmapped;
	pmu->csr_index = pmu_legacy_csr_index;
	pmu->pmu.capabilities |= PERF_PMU_CAP_NO_INTERRUPT;
	pmu->pmu.capabilities |= PERF_PMU_CAP_NO_EXCLUDE;

	perf_pmu_register(&pmu->pmu, "cpu", PERF_TYPE_RAW);
}

static int pmu_legacy_device_probe(struct platform_device *pdev)
{
	struct riscv_pmu *pmu = NULL;

	pmu = riscv_pmu_alloc();
	if (!pmu)
		return -ENOMEM;
	pmu->pmu.parent = &pdev->dev;
	pmu_legacy_init(pmu);

	return 0;
}

static struct platform_driver pmu_legacy_driver = {
	.probe		= pmu_legacy_device_probe,
	.driver		= {
		.name	= RISCV_PMU_LEGACY_PDEV_NAME,
	},
};

static int __init riscv_pmu_legacy_devinit(void)
{
	int ret;
	struct platform_device *pdev;

	if (likely(pmu_init_done))
		return 0;

	ret = platform_driver_register(&pmu_legacy_driver);
	if (ret)
		return ret;

	pdev = platform_device_register_simple(RISCV_PMU_LEGACY_PDEV_NAME, -1, NULL, 0);
	if (IS_ERR(pdev)) {
		platform_driver_unregister(&pmu_legacy_driver);
		return PTR_ERR(pdev);
	}

	return ret;
}
late_initcall(riscv_pmu_legacy_devinit);

void riscv_pmu_legacy_skip_init(void)
{
	pmu_init_done = true;
}
