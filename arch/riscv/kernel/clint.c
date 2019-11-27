// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Christoph Hellwig.
 */

#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/types.h>
#include <asm/clint.h>
#include <asm/csr.h>
#include <asm/timex.h>
#include <asm/smp.h>

/*
 * This is the layout used by the SiFive clint, which is also shared by the qemu
 * virt platform, and the Kendryte KD210 at least.
 */
#define CLINT_IPI_OFF		0
#define CLINT_TIME_CMP_OFF	0x4000
#define CLINT_TIME_VAL_OFF	0xbff8

u32 __iomem *clint_ipi_base;

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

	clint_clear_ipi(boot_cpu_hartid);
}
