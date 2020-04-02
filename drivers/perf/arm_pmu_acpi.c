/*
 * ACPI probing code for ARM performance counters.
 *
 * Copyright (C) 2017 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/percpu.h>
#include <linux/perf/arm_pmu.h>

#include <asm/cputype.h>

static DEFINE_PER_CPU(struct arm_pmu *, probed_pmus);
static DEFINE_PER_CPU(int, pmu_irqs);

static int arm_pmu_acpi_register_irq(int cpu)
{
	struct acpi_madt_generic_interrupt *gicc;
	int gsi, trigger;

	gicc = acpi_cpu_get_madt_gicc(cpu);

	gsi = gicc->performance_interrupt;

	/*
	 * Per the ACPI spec, the MADT cannot describe a PMU that doesn't
	 * have an interrupt. QEMU advertises this by using a GSI of zero,
	 * which is not known to be valid on any hardware despite being
	 * valid per the spec. Take the pragmatic approach and reject a
	 * GSI of zero for now.
	 */
	if (!gsi)
		return 0;

	if (gicc->flags & ACPI_MADT_PERFORMANCE_IRQ_MODE)
		trigger = ACPI_EDGE_SENSITIVE;
	else
		trigger = ACPI_LEVEL_SENSITIVE;

	/*
	 * Helpfully, the MADT GICC doesn't have a polarity flag for the
	 * "performance interrupt". Luckily, on compliant GICs the polarity is
	 * a fixed value in HW (for both SPIs and PPIs) that we cannot change
	 * from SW.
	 *
	 * Here we pass in ACPI_ACTIVE_HIGH to keep the core code happy. This
	 * may not match the real polarity, but that should not matter.
	 *
	 * Other interrupt controllers are not supported with ACPI.
	 */
	return acpi_register_gsi(NULL, gsi, trigger, ACPI_ACTIVE_HIGH);
}

static void arm_pmu_acpi_unregister_irq(int cpu)
{
	struct acpi_madt_generic_interrupt *gicc;
	int gsi;

	gicc = acpi_cpu_get_madt_gicc(cpu);

	gsi = gicc->performance_interrupt;
	if (gsi)
		acpi_unregister_gsi(gsi);
}

static int arm_pmu_acpi_parse_irqs(void)
{
	int irq, cpu, irq_cpu, err;

	for_each_possible_cpu(cpu) {
		irq = arm_pmu_acpi_register_irq(cpu);
		if (irq < 0) {
			err = irq;
			pr_warn("Unable to parse ACPI PMU IRQ for CPU%d: %d\n",
				cpu, err);
			goto out_err;
		} else if (irq == 0) {
			pr_warn("No ACPI PMU IRQ for CPU%d\n", cpu);
		}

		/*
		 * Log and request the IRQ so the core arm_pmu code can manage
		 * it. We'll have to sanity-check IRQs later when we associate
		 * them with their PMUs.
		 */
		per_cpu(pmu_irqs, cpu) = irq;
		armpmu_request_irq(irq, cpu);
	}

	return 0;

out_err:
	for_each_possible_cpu(cpu) {
		irq = per_cpu(pmu_irqs, cpu);
		if (!irq)
			continue;

		arm_pmu_acpi_unregister_irq(cpu);

		/*
		 * Blat all copies of the IRQ so that we only unregister the
		 * corresponding GSI once (e.g. when we have PPIs).
		 */
		for_each_possible_cpu(irq_cpu) {
			if (per_cpu(pmu_irqs, irq_cpu) == irq)
				per_cpu(pmu_irqs, irq_cpu) = 0;
		}
	}

	return err;
}

static struct arm_pmu *arm_pmu_acpi_find_alloc_pmu(void)
{
	unsigned long cpuid = read_cpuid_id();
	struct arm_pmu *pmu;
	int cpu;

	for_each_possible_cpu(cpu) {
		pmu = per_cpu(probed_pmus, cpu);
		if (!pmu || pmu->acpi_cpuid != cpuid)
			continue;

		return pmu;
	}

	pmu = armpmu_alloc_atomic();
	if (!pmu) {
		pr_warn("Unable to allocate PMU for CPU%d\n",
			smp_processor_id());
		return NULL;
	}

	pmu->acpi_cpuid = cpuid;

	return pmu;
}

/*
 * Check whether the new IRQ is compatible with those already associated with
 * the PMU (e.g. we don't have mismatched PPIs).
 */
static bool pmu_irq_matches(struct arm_pmu *pmu, int irq)
{
	struct pmu_hw_events __percpu *hw_events = pmu->hw_events;
	int cpu;

	if (!irq)
		return true;

	for_each_cpu(cpu, &pmu->supported_cpus) {
		int other_irq = per_cpu(hw_events->irq, cpu);
		if (!other_irq)
			continue;

		if (irq == other_irq)
			continue;
		if (!irq_is_percpu_devid(irq) && !irq_is_percpu_devid(other_irq))
			continue;

		pr_warn("mismatched PPIs detected\n");
		return false;
	}

	return true;
}

/*
 * This must run before the common arm_pmu hotplug logic, so that we can
 * associate a CPU and its interrupt before the common code tries to manage the
 * affinity and so on.
 *
 * Note that hotplug events are serialized, so we cannot race with another CPU
 * coming up. The perf core won't open events while a hotplug event is in
 * progress.
 */
static int arm_pmu_acpi_cpu_starting(unsigned int cpu)
{
	struct arm_pmu *pmu;
	struct pmu_hw_events __percpu *hw_events;
	int irq;

	/* If we've already probed this CPU, we have nothing to do */
	if (per_cpu(probed_pmus, cpu))
		return 0;

	irq = per_cpu(pmu_irqs, cpu);

	pmu = arm_pmu_acpi_find_alloc_pmu();
	if (!pmu)
		return -ENOMEM;

	per_cpu(probed_pmus, cpu) = pmu;

	if (pmu_irq_matches(pmu, irq)) {
		hw_events = pmu->hw_events;
		per_cpu(hw_events->irq, cpu) = irq;
	}

	cpumask_set_cpu(cpu, &pmu->supported_cpus);

	/*
	 * Ideally, we'd probe the PMU here when we find the first matching
	 * CPU. We can't do that for several reasons; see the comment in
	 * arm_pmu_acpi_init().
	 *
	 * So for the time being, we're done.
	 */
	return 0;
}

int arm_pmu_acpi_probe(armpmu_init_fn init_fn)
{
	int pmu_idx = 0;
	int cpu, ret;

	/*
	 * Initialise and register the set of PMUs which we know about right
	 * now. Ideally we'd do this in arm_pmu_acpi_cpu_starting() so that we
	 * could handle late hotplug, but this may lead to deadlock since we
	 * might try to register a hotplug notifier instance from within a
	 * hotplug notifier.
	 *
	 * There's also the problem of having access to the right init_fn,
	 * without tying this too deeply into the "real" PMU driver.
	 *
	 * For the moment, as with the platform/DT case, we need at least one
	 * of a PMU's CPUs to be online at probe time.
	 */
	for_each_possible_cpu(cpu) {
		struct arm_pmu *pmu = per_cpu(probed_pmus, cpu);
		char *base_name;

		if (!pmu || pmu->name)
			continue;

		ret = init_fn(pmu);
		if (ret == -ENODEV) {
			/* PMU not handled by this driver, or not present */
			continue;
		} else if (ret) {
			pr_warn("Unable to initialise PMU for CPU%d\n", cpu);
			return ret;
		}

		base_name = pmu->name;
		pmu->name = kasprintf(GFP_KERNEL, "%s_%d", base_name, pmu_idx++);
		if (!pmu->name) {
			pr_warn("Unable to allocate PMU name for CPU%d\n", cpu);
			return -ENOMEM;
		}

		ret = armpmu_register(pmu);
		if (ret) {
			pr_warn("Failed to register PMU for CPU%d\n", cpu);
			kfree(pmu->name);
			return ret;
		}
	}

	return 0;
}

static int arm_pmu_acpi_init(void)
{
	int ret;

	if (acpi_disabled)
		return 0;

	ret = arm_pmu_acpi_parse_irqs();
	if (ret)
		return ret;

	ret = cpuhp_setup_state(CPUHP_AP_PERF_ARM_ACPI_STARTING,
				"perf/arm/pmu_acpi:starting",
				arm_pmu_acpi_cpu_starting, NULL);

	return ret;
}
subsys_initcall(arm_pmu_acpi_init)
