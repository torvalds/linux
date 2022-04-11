// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copied from arch/arm64/kernel/cpufeature.c
 *
 * Copyright (C) 2015 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */

#include <linux/of.h>
#include <asm/processor.h>
#include <asm/hwcap.h>
#include <asm/smp.h>
#include <asm/switch_to.h>

unsigned long elf_hwcap __read_mostly;
#ifdef CONFIG_FPU
bool has_fpu __read_mostly;
#endif

void riscv_fill_hwcap(void)
{
	struct device_node *node;
	const char *isa;
	size_t i;
	static unsigned long isa2hwcap[256] = {0};

	isa2hwcap['i'] = isa2hwcap['I'] = COMPAT_HWCAP_ISA_I;
	isa2hwcap['m'] = isa2hwcap['M'] = COMPAT_HWCAP_ISA_M;
	isa2hwcap['a'] = isa2hwcap['A'] = COMPAT_HWCAP_ISA_A;
	isa2hwcap['f'] = isa2hwcap['F'] = COMPAT_HWCAP_ISA_F;
	isa2hwcap['d'] = isa2hwcap['D'] = COMPAT_HWCAP_ISA_D;
	isa2hwcap['c'] = isa2hwcap['C'] = COMPAT_HWCAP_ISA_C;

	elf_hwcap = 0;

	for_each_of_cpu_node(node) {
		unsigned long this_hwcap = 0;

		if (riscv_of_processor_hartid(node) < 0)
			continue;

		if (of_property_read_string(node, "riscv,isa", &isa)) {
			pr_warn("Unable to find \"riscv,isa\" devicetree entry\n");
			continue;
		}

		for (i = 0; i < strlen(isa); ++i)
			this_hwcap |= isa2hwcap[(unsigned char)(isa[i])];

		/*
		 * All "okay" hart should have same isa. Set HWCAP based on
		 * common capabilities of every "okay" hart, in case they don't
		 * have.
		 */
		if (elf_hwcap)
			elf_hwcap &= this_hwcap;
		else
			elf_hwcap = this_hwcap;
	}

	/* We don't support systems with F but without D, so mask those out
	 * here. */
	if ((elf_hwcap & COMPAT_HWCAP_ISA_F) && !(elf_hwcap & COMPAT_HWCAP_ISA_D)) {
		pr_info("This kernel does not support systems with F but not D\n");
		elf_hwcap &= ~COMPAT_HWCAP_ISA_F;
	}

	pr_info("elf_hwcap is 0x%lx\n", elf_hwcap);

#ifdef CONFIG_FPU
	if (elf_hwcap & (COMPAT_HWCAP_ISA_F | COMPAT_HWCAP_ISA_D))
		has_fpu = true;
#endif
}
