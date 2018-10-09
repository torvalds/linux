/*
 * Copied from arch/arm64/kernel/cpufeature.c
 *
 * Copyright (C) 2015 ARM Ltd.
 * Copyright (C) 2017 SiFive
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/of.h>
#include <asm/processor.h>
#include <asm/hwcap.h>

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

	/*
	 * We don't support running Linux on hertergenous ISA systems.  For
	 * now, we just check the ISA of the first processor.
	 */
	node = of_find_node_by_type(NULL, "cpu");
	if (!node) {
		pr_warning("Unable to find \"cpu\" devicetree entry");
		return;
	}

	if (of_property_read_string(node, "riscv,isa", &isa)) {
		pr_warning("Unable to find \"riscv,isa\" devicetree entry");
		return;
	}

	for (i = 0; i < strlen(isa); ++i)
		elf_hwcap |= isa2hwcap[(unsigned char)(isa[i])];

	pr_info("elf_hwcap is 0x%lx", elf_hwcap);

#ifdef CONFIG_FPU
	if (elf_hwcap & (COMPAT_HWCAP_ISA_F | COMPAT_HWCAP_ISA_D))
		has_fpu = true;
#endif
}
