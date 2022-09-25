// SPDX-License-Identifier: GPL-2.0
/*
 * IXP4xx Device Tree boot support
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

/*
 * These are the only fixed phys to virt mappings we ever need
 * we put it right after the UART mapping at 0xffc80000-0xffc81fff
 */
#define IXP4XX_EXP_CFG_BASE_PHYS	0xC4000000
#define IXP4XX_EXP_CFG_BASE_VIRT	0xFEC14000

static struct map_desc ixp4xx_of_io_desc[] __initdata = {
	/*
	 * This is needed for runtime system configuration checks,
	 * such as reading if hardware so-and-so is present. This
	 * could eventually be converted into a syscon once all boards
	 * are converted to device tree.
	 */
	{
		.virtual = IXP4XX_EXP_CFG_BASE_VIRT,
		.pfn = __phys_to_pfn(IXP4XX_EXP_CFG_BASE_PHYS),
		.length = SZ_4K,
		.type = MT_DEVICE,
	},
#ifdef CONFIG_DEBUG_UART_8250
	/* This is needed for LL-debug/earlyprintk/debug-macro.S */
	{
		.virtual = CONFIG_DEBUG_UART_VIRT,
		.pfn = __phys_to_pfn(CONFIG_DEBUG_UART_PHYS),
		.length = SZ_4K,
		.type = MT_DEVICE,
	},
#endif
};

static void __init ixp4xx_of_map_io(void)
{
	iotable_init(ixp4xx_of_io_desc, ARRAY_SIZE(ixp4xx_of_io_desc));
}

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
