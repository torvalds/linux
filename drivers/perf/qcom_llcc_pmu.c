// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <soc/qcom/qcom_llcc_pmu.h>
static uint32_t phys_cpu[NR_CPUS];

enum llcc_pmu_version {
	LLCC_PMU_VER1 = 1,
	LLCC_PMU_VER2,
};

struct llcc_pmu {
	struct pmu		pmu;
	struct hlist_node	node;
	void __iomem		*lagg_base;
	struct perf_event	event;
	enum llcc_pmu_version	ver;
};

#define MON_CFG(m) ((m)->lagg_base + 0x200)
#define MON_CNT(m, cpu) ((m)->lagg_base + 0x220 + 0x4 * cpu)

#define LLCC_RD_EV QCOM_LLCC_PMU_RD_EV
#define ENABLE 0x1
#define CLEAR 0x10
#define CLEAR_POS 16
#define DISABLE 0x0
#define SCALING_FACTOR 0x3
#define NUM_COUNTERS NR_CPUS
#define VALUE_MASK 0xFFFFFF

static u64 llcc_stats[NUM_COUNTERS];
static unsigned int users;
static raw_spinlock_t counter_lock;
static raw_spinlock_t users_lock;
static ktime_t last_read;
static DEFINE_PER_CPU(unsigned int, users_alive);
static struct llcc_pmu *llccpmu;

int qcom_llcc_pmu_hw_type(u32 *type)
{
	if (!llccpmu || !llccpmu->pmu.type)
		return -EPROBE_DEFER;

	*type = llccpmu->pmu.type;

	return 0;
}
EXPORT_SYMBOL(qcom_llcc_pmu_hw_type);

static void mon_disable(int cpu)
{
	u32 reg;
	cpu = phys_cpu[cpu];

	if (!llccpmu->ver) {
		pr_err("LLCCPMU version not correct\n");
		return;
	}

	switch (llccpmu->ver) {
	case LLCC_PMU_VER1:
		writel_relaxed(DISABLE, MON_CFG(llccpmu));
		break;
	case LLCC_PMU_VER2:
		reg = readl_relaxed(MON_CFG(llccpmu));
		reg &= ~(ENABLE << cpu);
		writel_relaxed(reg, MON_CFG(llccpmu));
		break;
	}
}

static void mon_clear(int cpu)
{
	int clear_bit;
	u32 reg;

	cpu = phys_cpu[cpu];
	clear_bit = CLEAR_POS + cpu;

	if (!llccpmu->ver) {
		pr_err("LLCCPMU version not correct\n");
		return;
	}

	switch (llccpmu->ver) {
	case LLCC_PMU_VER1:
		writel_relaxed(CLEAR, MON_CFG(llccpmu));
		break;
	case LLCC_PMU_VER2:
		reg = readl_relaxed(MON_CFG(llccpmu));
		reg |= (ENABLE << clear_bit);
		writel_relaxed(reg, MON_CFG(llccpmu));
		reg &= ~(ENABLE << clear_bit);
		writel_relaxed(reg, MON_CFG(llccpmu));
		break;
	}
}

static void mon_enable(int cpu)
{
	u32 reg;
	cpu = phys_cpu[cpu];

	if (!llccpmu->ver) {
		pr_err("LLCCPMU version not correct\n");
		return;
	}

	switch (llccpmu->ver) {
	case LLCC_PMU_VER1:
		writel_relaxed(ENABLE, MON_CFG(llccpmu));
		break;
	case LLCC_PMU_VER2:
		reg = readl_relaxed(MON_CFG(llccpmu));
		reg |= (ENABLE << cpu);
		writel_relaxed(reg, MON_CFG(llccpmu));
		break;
	}
}

static unsigned long read_cnt(int cpu)
{
	unsigned long value;
	cpu = phys_cpu[cpu];

	if (!llccpmu->ver) {
		pr_err("LLCCPMU version not correct\n");
		return -EINVAL;
	}

	switch (llccpmu->ver) {
	case LLCC_PMU_VER1:
		value = readl_relaxed(MON_CNT(llccpmu, cpu));
		break;
	case LLCC_PMU_VER2:
		value = readl_relaxed(MON_CNT(llccpmu, cpu));
		break;
	}
	return value;
}

static int qcom_llcc_event_init(struct perf_event *event)
{
	u64 config = event->attr.config;

	if (config == LLCC_RD_EV) {
		event->hw.config_base = event->attr.config;
		return 0;
	} else
		return -ENOENT;
}

static void qcom_llcc_event_read(struct perf_event *event)
{
	int i = 0, cpu = event->cpu;
	unsigned long raw, irq_flags;
	ktime_t cur;

	raw_spin_lock_irqsave(&counter_lock, irq_flags);
	if (llccpmu->ver == LLCC_PMU_VER1) {
		cur = ktime_get();
		if (ktime_ms_delta(cur, last_read) > 1) {
			mon_disable(cpu);
			for (i = 0; i < NUM_COUNTERS; i++) {
				raw = read_cnt(i);
				raw &= VALUE_MASK;
				llcc_stats[i] += (u64) raw << SCALING_FACTOR;
			}
			last_read = cur;
			mon_clear(cpu);
			mon_enable(cpu);
		}
	} else {
		mon_disable(cpu);
		raw = read_cnt(cpu);
		raw &= VALUE_MASK;
		llcc_stats[cpu] += (u64) raw << SCALING_FACTOR;
		mon_clear(cpu);
		mon_enable(cpu);
	}

	if (!(event->hw.state & PERF_HES_STOPPED))
		local64_set(&event->count, llcc_stats[cpu]);
	raw_spin_unlock_irqrestore(&counter_lock, irq_flags);
}

static void qcom_llcc_event_start(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_RELOAD)
		WARN_ON(!(event->hw.state & PERF_HES_UPTODATE));
	event->hw.state = 0;
}

static void qcom_llcc_event_stop(struct perf_event *event, int flags)
{
	qcom_llcc_event_read(event);
	event->hw.state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
}

static int qcom_llcc_event_add(struct perf_event *event, int flags)
{
	unsigned int cpu_users;

	raw_spin_lock(&users_lock);

	if (llccpmu->ver == LLCC_PMU_VER1) {
		if (!users)
			mon_enable(event->cpu);
		users++;
	} else {
		cpu_users = per_cpu(users_alive, event->cpu);
		if (!cpu_users)
			mon_enable(event->cpu);
		cpu_users++;
		per_cpu(users_alive, event->cpu) = cpu_users;
	}

	raw_spin_unlock(&users_lock);

	event->hw.state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	if (flags & PERF_EF_START)
		qcom_llcc_event_start(event, PERF_EF_RELOAD);

	return 0;
}

static void qcom_llcc_event_del(struct perf_event *event, int flags)
{
	unsigned int cpu_users;

	raw_spin_lock(&users_lock);

	if (llccpmu->ver == LLCC_PMU_VER1) {
		users--;
		if (!users)
			mon_disable(event->cpu);
	} else {
		cpu_users = per_cpu(users_alive, event->cpu);
		cpu_users--;
		if (!cpu_users)
			mon_disable(event->cpu);
		per_cpu(users_alive, event->cpu) = cpu_users;
	}

	raw_spin_unlock(&users_lock);
}

static void get_mpidr_cpu(void *cpu)
{
	u64 mpidr = read_cpuid_mpidr() & MPIDR_HWID_BITMASK;

	*((uint32_t *)cpu) = MPIDR_AFFINITY_LEVEL(mpidr, 1);
}

static int qcom_llcc_pmu_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;
	uint32_t cpu, pcpu;

	if (llccpmu) {
		dev_err(&pdev->dev, "Only one LLCC PMU allowed!\n");
		return -ENODEV;
	}
	llccpmu = devm_kzalloc(&pdev->dev, sizeof(struct llcc_pmu), GFP_KERNEL);
	if (!llccpmu)
		return -ENOMEM;

	llccpmu->ver = (enum llcc_pmu_version)
			of_device_get_match_data(&pdev->dev);
	if (!llccpmu->ver) {
		pr_err("Unknown device type!\n");
		return -ENODEV;
	}

	llccpmu->pmu = (struct pmu) {
		.task_ctx_nr = perf_invalid_context,
		.type		= 0,
		.event_init	= qcom_llcc_event_init,
		.add		= qcom_llcc_event_add,
		.del		= qcom_llcc_event_del,
		.start		= qcom_llcc_event_start,
		.stop		= qcom_llcc_event_stop,
		.read		= qcom_llcc_event_read,
	};

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lagg-base");
	llccpmu->lagg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(llccpmu->lagg_base)) {
		dev_err(&pdev->dev, "Can't map PMU lagg base: @%pa\n",
			&res->start);
		return PTR_ERR(llccpmu->lagg_base);
	}

	raw_spin_lock_init(&counter_lock);
	raw_spin_lock_init(&users_lock);

	ret = perf_pmu_register(&llccpmu->pmu, "llcc-pmu", -1);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to register LLCC PMU (%d)\n", ret);

	for_each_possible_cpu(cpu) {
		smp_call_function_single(cpu, get_mpidr_cpu,
							 &pcpu, true);
		phys_cpu[cpu] = pcpu;
	}

	dev_info(&pdev->dev, "Registered llcc_pmu, type: %d\n",
		 llccpmu->pmu.type);

	return 0;
}

static const struct of_device_id qcom_llcc_pmu_match_table[] = {
	{ .compatible = "qcom,llcc-pmu-ver1", .data = (void *) LLCC_PMU_VER1 },
	{ .compatible = "qcom,llcc-pmu-ver2", .data = (void *) LLCC_PMU_VER2 },
	{}
};

static struct platform_driver qcom_llcc_pmu_driver = {
	.driver = {
		.name = "qcom-llcc-pmu",
		.of_match_table = qcom_llcc_pmu_match_table,
		.suppress_bind_attrs = true,
	},
	.probe = qcom_llcc_pmu_probe,
};
module_platform_driver(qcom_llcc_pmu_driver);

MODULE_DESCRIPTION("QCOM LLCC PMU");
MODULE_LICENSE("GPL");
