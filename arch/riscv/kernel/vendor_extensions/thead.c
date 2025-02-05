// SPDX-License-Identifier: GPL-2.0-only

#include <asm/cpufeature.h>
#include <asm/vendor_extensions.h>
#include <asm/vendor_extensions/thead.h>

#include <linux/array_size.h>
#include <linux/cpumask.h>
#include <linux/types.h>

/* All T-Head vendor extensions supported in Linux */
static const struct riscv_isa_ext_data riscv_isa_vendor_ext_thead[] = {
	__RISCV_ISA_EXT_DATA(xtheadvector, RISCV_ISA_VENDOR_EXT_XTHEADVECTOR),
};

struct riscv_isa_vendor_ext_data_list riscv_isa_vendor_ext_list_thead = {
	.ext_data_count = ARRAY_SIZE(riscv_isa_vendor_ext_thead),
	.ext_data = riscv_isa_vendor_ext_thead,
};

void disable_xtheadvector(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		clear_bit(RISCV_ISA_VENDOR_EXT_XTHEADVECTOR, riscv_isa_vendor_ext_list_thead.per_hart_isa_bitmap[cpu].isa);

	clear_bit(RISCV_ISA_VENDOR_EXT_XTHEADVECTOR, riscv_isa_vendor_ext_list_thead.all_harts_isa_bitmap.isa);
}
