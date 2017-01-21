/*
 * Gemini Device Tree boot support
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#ifdef CONFIG_DEBUG_GEMINI
/* This is needed for LL-debug/earlyprintk/debug-macro.S */
static struct map_desc gemini_io_desc[] __initdata = {
	{
		.virtual = CONFIG_DEBUG_UART_VIRT,
		.pfn = __phys_to_pfn(CONFIG_DEBUG_UART_PHYS),
		.length = SZ_4K,
		.type = MT_DEVICE,
	},
};

static void __init gemini_map_io(void)
{
	iotable_init(gemini_io_desc, ARRAY_SIZE(gemini_io_desc));
}
#else
#define gemini_map_io NULL
#endif

static const char *gemini_board_compat[] = {
	"cortina,gemini",
	NULL,
};

DT_MACHINE_START(GEMINI_DT, "Gemini (Device Tree)")
	.map_io		= gemini_map_io,
	.dt_compat	= gemini_board_compat,
MACHINE_END
