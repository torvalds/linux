// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024 Linus Walleij <linus.walleij@linaro.org>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

/* This is needed for LL-debug/earlyprintk/debug-macro.S */
static struct map_desc bcmbca_io_desc[] __initdata = {
	{
		.virtual = CONFIG_DEBUG_UART_VIRT,
		.pfn = __phys_to_pfn(CONFIG_DEBUG_UART_PHYS),
		.length = SZ_4K,
		.type = MT_DEVICE,
	},
};

static void __init bcmbca_map_io(void)
{
	iotable_init(bcmbca_io_desc, ARRAY_SIZE(bcmbca_io_desc));
}

static const char * const bcmbca_dt_compat[] = {
	/* TODO: Add other BCMBCA SoCs here to get debug UART support */
	"brcm,bcm6846",
	NULL,
};

DT_MACHINE_START(BCMBCA_DT, "BCMBCA Broadband Access Processors")
	.map_io = bcmbca_map_io,
	.dt_compat = bcmbca_dt_compat,
MACHINE_END
