// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Christoph Hellwig.
 */

#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <asm/clint.h>
#include <asm/csr.h>
#include <asm/timex.h>

/*
 * This is the layout used by the SiFive clint, which is also shared by the qemu
 * virt platform, and the Kendryte KD210 at least.
 */
#define CLINT_IPI_OFF		0
#define CLINT_TIME_CMP_OFF	0x4000
#define CLINT_TIME_VAL_OFF	0xbff8

u32 __iomem *clint_ipi_base;

static void clint_send_ipi(const struct cpumask *target)
{
	unsigned int cpu;

	for_each_cpu(cpu, target)
		writel(1, clint_ipi_base + cpuid_to_hartid_map(cpu));
}

static void clint_clear_ipi(void)
{
	writel(0, clint_ipi_base + cpuid_to_hartid_map(smp_processor_id()));
}

static struct riscv_ipi_ops clint_ipi_ops = {
	.ipi_inject = clint_send_ipi,
	.ipi_clear = clint_clear_ipi,
};

void clint_init_boot_cpu(void)
{
	struct device_node *np;
	void __iomem *base;

	np = of_find_compatible_node(NULL, NULL, "riscv,clint0");
	if (!np) {
		panic("clint not found");
		return;
	}

	base = of_iomap(np, 0);
	if (!base)
		panic("could not map CLINT");

	clint_ipi_base = base + CLINT_IPI_OFF;
	riscv_time_cmp = base + CLINT_TIME_CMP_OFF;
	riscv_time_val = base + CLINT_TIME_VAL_OFF;

	clint_clear_ipi();
	riscv_set_ipi_ops(&clint_ipi_ops);
}
