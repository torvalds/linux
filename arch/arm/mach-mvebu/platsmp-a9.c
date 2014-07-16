/*
 * Symmetric Multi Processing (SMP) support for Marvell EBU Cortex-A9
 * based SOCs (Armada 375/38x).
 *
 * Copyright (C) 2014 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <linux/mbus.h>
#include <asm/smp_scu.h>
#include <asm/smp_plat.h>
#include "common.h"
#include "mvebu-soc-id.h"
#include "pmsu.h"

#define CRYPT0_ENG_ID   41
#define CRYPT0_ENG_ATTR 0x1
#define SRAM_PHYS_BASE  0xFFFF0000

#define BOOTROM_BASE    0xFFF00000
#define BOOTROM_SIZE    0x100000

extern unsigned char armada_375_smp_cpu1_enable_code_end;
extern unsigned char armada_375_smp_cpu1_enable_code_start;

void armada_375_smp_cpu1_enable_wa(void)
{
	void __iomem *sram_virt_base;

	mvebu_mbus_del_window(BOOTROM_BASE, BOOTROM_SIZE);
	mvebu_mbus_add_window_by_id(CRYPT0_ENG_ID, CRYPT0_ENG_ATTR,
				SRAM_PHYS_BASE, SZ_64K);
	sram_virt_base = ioremap(SRAM_PHYS_BASE, SZ_64K);

	memcpy(sram_virt_base, &armada_375_smp_cpu1_enable_code_start,
	       &armada_375_smp_cpu1_enable_code_end
	       - &armada_375_smp_cpu1_enable_code_start);
}

extern void mvebu_cortex_a9_secondary_startup(void);

static int __cpuinit mvebu_cortex_a9_boot_secondary(unsigned int cpu,
						    struct task_struct *idle)
{
	int ret, hw_cpu;

	pr_info("Booting CPU %d\n", cpu);

	/*
	 * Write the address of secondary startup into the system-wide
	 * flags register. The boot monitor waits until it receives a
	 * soft interrupt, and then the secondary CPU branches to this
	 * address.
	 */
	hw_cpu = cpu_logical_map(cpu);

	if (of_machine_is_compatible("marvell,armada375")) {
		u32 dev, rev;

		if (mvebu_get_soc_id(&dev, &rev) == 0 &&
		    rev == ARMADA_375_Z1_REV)
			armada_375_smp_cpu1_enable_wa();

		mvebu_system_controller_set_cpu_boot_addr(mvebu_cortex_a9_secondary_startup);
	}
	else {
		mvebu_pmsu_set_cpu_boot_addr(hw_cpu,
					     mvebu_cortex_a9_secondary_startup);
	}

	smp_wmb();
	ret = mvebu_cpu_reset_deassert(hw_cpu);
	if (ret) {
		pr_err("Could not start the secondary CPU: %d\n", ret);
		return ret;
	}
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	return 0;
}

static struct smp_operations mvebu_cortex_a9_smp_ops __initdata = {
	.smp_boot_secondary	= mvebu_cortex_a9_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= armada_xp_cpu_die,
#endif
};

CPU_METHOD_OF_DECLARE(mvebu_armada_375_smp, "marvell,armada-375-smp",
		      &mvebu_cortex_a9_smp_ops);
CPU_METHOD_OF_DECLARE(mvebu_armada_380_smp, "marvell,armada-380-smp",
		      &mvebu_cortex_a9_smp_ops);
