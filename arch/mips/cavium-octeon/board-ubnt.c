// SPDX-License-Identifier: GPL-2.0-only

#include <asm/machine.h>
#include <asm/octeon/octeon.h>

static __init bool ubnt_e100_detect(void)
{
	return cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_UBNT_E100;
}

extern const char __dtb_ubnt_e100_begin[];

MIPS_MACHINE(ubnt_e100) = {
	.fdt = __dtb_ubnt_e100_begin,
	.detect = ubnt_e100_detect,
};
