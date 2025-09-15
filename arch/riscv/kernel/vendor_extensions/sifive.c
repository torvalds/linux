// SPDX-License-Identifier: GPL-2.0-only

#include <asm/cpufeature.h>
#include <asm/vendor_extensions.h>
#include <asm/vendor_extensions/sifive.h>

#include <linux/array_size.h>
#include <linux/types.h>

/* All SiFive vendor extensions supported in Linux */
static const struct riscv_isa_ext_data riscv_isa_vendor_ext_sifive[] = {
	__RISCV_ISA_EXT_DATA(xsfvfnrclipxfqf, RISCV_ISA_VENDOR_EXT_XSFVFNRCLIPXFQF),
	__RISCV_ISA_EXT_DATA(xsfvfwmaccqqq, RISCV_ISA_VENDOR_EXT_XSFVFWMACCQQQ),
	__RISCV_ISA_EXT_DATA(xsfvqmaccdod, RISCV_ISA_VENDOR_EXT_XSFVQMACCDOD),
	__RISCV_ISA_EXT_DATA(xsfvqmaccqoq, RISCV_ISA_VENDOR_EXT_XSFVQMACCQOQ),
};

struct riscv_isa_vendor_ext_data_list riscv_isa_vendor_ext_list_sifive = {
	.ext_data_count = ARRAY_SIZE(riscv_isa_vendor_ext_sifive),
	.ext_data = riscv_isa_vendor_ext_sifive,
};
