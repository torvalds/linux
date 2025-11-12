// SPDX-License-Identifier: GPL-2.0
#include <asm/paravirt.h>
#include <linux/smp.h>
#include <linux/export.h>
#include <linux/kvm_types.h>

static void __wbinvd(void *dummy)
{
	wbinvd();
}

void wbinvd_on_cpu(int cpu)
{
	smp_call_function_single(cpu, __wbinvd, NULL, 1);
}
EXPORT_SYMBOL_FOR_KVM(wbinvd_on_cpu);

void wbinvd_on_all_cpus(void)
{
	on_each_cpu(__wbinvd, NULL, 1);
}
EXPORT_SYMBOL(wbinvd_on_all_cpus);

void wbinvd_on_cpus_mask(struct cpumask *cpus)
{
	on_each_cpu_mask(cpus, __wbinvd, NULL, 1);
}
EXPORT_SYMBOL_FOR_KVM(wbinvd_on_cpus_mask);

static void __wbnoinvd(void *dummy)
{
	wbnoinvd();
}

void wbnoinvd_on_all_cpus(void)
{
	on_each_cpu(__wbnoinvd, NULL, 1);
}
EXPORT_SYMBOL_FOR_KVM(wbnoinvd_on_all_cpus);

void wbnoinvd_on_cpus_mask(struct cpumask *cpus)
{
	on_each_cpu_mask(cpus, __wbnoinvd, NULL, 1);
}
EXPORT_SYMBOL_FOR_KVM(wbnoinvd_on_cpus_mask);
