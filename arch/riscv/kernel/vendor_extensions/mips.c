// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 MIPS.
 */

#include <asm/cpufeature.h>
#include <asm/vendor_extensions.h>
#include <asm/vendor_extensions/mips.h>

#include <linux/array_size.h>
#include <linux/cpumask.h>
#include <linux/types.h>

/* All MIPS vendor extensions supported in Linux */
static const struct riscv_isa_ext_data riscv_isa_vendor_ext_mips[] = {
	__RISCV_ISA_EXT_DATA(xmipsexectl, RISCV_ISA_VENDOR_EXT_XMIPSEXECTL),
};

struct riscv_isa_vendor_ext_data_list riscv_isa_vendor_ext_list_mips = {
	.ext_data_count = ARRAY_SIZE(riscv_isa_vendor_ext_mips),
	.ext_data = riscv_isa_vendor_ext_mips,
};
