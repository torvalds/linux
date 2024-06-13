// SPDX-License-Identifier: GPL-2.0
/*
 * R9A06G032 Second CA7 enabler.
 *
 * Copyright (C) 2018 Renesas Electronics Europe Limited
 *
 * Michel Pollet <michel.pollet@bp.renesas.com>, <buserror@gmail.com>
 * Derived from actions,s500-smp
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/smp.h>

/*
 * The second CPU is parked in ROM at boot time. It requires waking it after
 * writing an address into the BOOTADDR register of sysctrl.
 *
 * So the default value of the "cpu-release-addr" corresponds to BOOTADDR...
 *
 * *However* the BOOTADDR register is not available when the kernel
 * starts in NONSEC mode.
 *
 * So for NONSEC mode, the bootloader re-parks the second CPU into a pen
 * in SRAM, and changes the "cpu-release-addr" of linux's DT to a SRAM address,
 * which is not restricted.
 */

static void __iomem *cpu_bootaddr;

static DEFINE_SPINLOCK(cpu_lock);

static int
r9a06g032_smp_boot_secondary(unsigned int cpu,
			     struct task_struct *idle)
{
	if (!cpu_bootaddr)
		return -ENODEV;

	spin_lock(&cpu_lock);

	writel(__pa_symbol(secondary_startup), cpu_bootaddr);
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	spin_unlock(&cpu_lock);

	return 0;
}

static void __init r9a06g032_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *dn;
	int ret = -EINVAL, dns;
	u32 bootaddr;

	dn = of_get_cpu_node(1, NULL);
	if (!dn) {
		pr_err("CPU#1: missing device tree node\n");
		return;
	}
	/*
	 * Determine the address from which the CPU is polling.
	 * The bootloader *does* change this property.
	 * Note: The property can be either 64 or 32 bits, so handle both cases
	 */
	if (of_find_property(dn, "cpu-release-addr", &dns)) {
		if (dns == sizeof(u64)) {
			u64 temp;

			ret = of_property_read_u64(dn,
						   "cpu-release-addr", &temp);
			bootaddr = temp;
		} else {
			ret = of_property_read_u32(dn,
						   "cpu-release-addr",
						   &bootaddr);
		}
	}
	of_node_put(dn);
	if (ret) {
		pr_err("CPU#1: invalid cpu-release-addr property\n");
		return;
	}
	pr_info("CPU#1: cpu-release-addr %08x\n", bootaddr);

	cpu_bootaddr = ioremap(bootaddr, sizeof(bootaddr));
}

static const struct smp_operations r9a06g032_smp_ops __initconst = {
	.smp_prepare_cpus = r9a06g032_smp_prepare_cpus,
	.smp_boot_secondary = r9a06g032_smp_boot_secondary,
};

CPU_METHOD_OF_DECLARE(r9a06g032_smp,
		      "renesas,r9a06g032-smp", &r9a06g032_smp_ops);
