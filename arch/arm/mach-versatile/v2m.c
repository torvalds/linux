// SPDX-License-Identifier: GPL-2.0
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/mach/arch.h>

#include "vexpress.h"

#define SYS_FLAGSSET		0x030
#define SYS_FLAGSCLR		0x034

void vexpress_flags_set(u32 data)
{
	static void __iomem *base;

	if (!base) {
		struct device_node *node = of_find_compatible_node(NULL, NULL,
				"arm,vexpress-sysreg");

		base = of_iomap(node, 0);
	}

	if (WARN_ON(!base))
		return;

	writel(~0, base + SYS_FLAGSCLR);
	writel(data, base + SYS_FLAGSSET);
}

static const char * const v2m_dt_match[] __initconst = {
	"arm,vexpress",
	NULL,
};

DT_MACHINE_START(VEXPRESS_DT, "ARM-Versatile Express")
	.dt_compat	= v2m_dt_match,
	.l2c_aux_val	= 0x00400000,
	.l2c_aux_mask	= 0xfe0fffff,
	.smp		= smp_ops(vexpress_smp_dt_ops),
	.smp_init	= smp_init_ops(vexpress_smp_init_ops),
MACHINE_END
