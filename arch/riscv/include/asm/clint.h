/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_CLINT_H
#define _ASM_RISCV_CLINT_H 1

#include <linux/io.h>
#include <linux/smp.h>

#ifdef CONFIG_RISCV_M_MODE
extern u32 __iomem *clint_ipi_base;

void clint_init_boot_cpu(void);

static inline void clint_send_ipi_single(unsigned long hartid)
{
	writel(1, clint_ipi_base + hartid);
}

static inline void clint_send_ipi_mask(const struct cpumask *hartid_mask)
{
	int hartid;

	for_each_cpu(hartid, hartid_mask)
		clint_send_ipi_single(hartid);
}

static inline void clint_clear_ipi(unsigned long hartid)
{
	writel(0, clint_ipi_base + hartid);
}
#else /* CONFIG_RISCV_M_MODE */
#define clint_init_boot_cpu()	do { } while (0)

/* stubs to for code is only reachable under IS_ENABLED(CONFIG_RISCV_M_MODE): */
void clint_send_ipi_single(unsigned long hartid);
void clint_send_ipi_mask(const struct cpumask *hartid_mask);
void clint_clear_ipi(unsigned long hartid);
#endif /* CONFIG_RISCV_M_MODE */

#endif /* _ASM_RISCV_CLINT_H */
