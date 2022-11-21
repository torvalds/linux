// SPDX-License-Identifier: GPL-2.0
/*
 * IXP4xx Device Tree boot support
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#ifdef CONFIG_DEBUG_UART_8250
static struct map_desc ixp4xx_of_io_desc[] __initdata = {
	/* This is needed for LL-debug/earlyprintk/debug-macro.S */
	{
		.virtual = CONFIG_DEBUG_UART_VIRT,
		.pfn = __phys_to_pfn(CONFIG_DEBUG_UART_PHYS),
		.length = SZ_4K,
		.type = MT_DEVICE,
	},
};

static void __init ixp4xx_of_map_io(void)
{
	iotable_init(ixp4xx_of_io_desc, ARRAY_SIZE(ixp4xx_of_io_desc));
}
#else
#define ixp4xx_of_map_io NULL
#endif

/*
 * We handle 4 different SoC families. These compatible strings are enough
 * to provide the core so that different boards can add their more detailed
 * specifics.
 */
static const char *ixp4xx_of_board_compat[] = {
	"intel,ixp42x",
	"intel,ixp43x",
	"intel,ixp45x",
	"intel,ixp46x",
	NULL,
};

DT_MACHINE_START(IXP4XX_DT, "IXP4xx (Device Tree)")
	.map_io		= ixp4xx_of_map_io,
	.dt_compat	= ixp4xx_of_board_compat,
MACHINE_END
