/*
 * linux/arch/arm/mach-sa1100/lart.c
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <video/sa1100fb.h>

#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/page.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <mach/mcp.h>
#include <mach/irqs.h>

#include "generic.h"


#warning "include/asm/arch-sa1100/ide.h needs fixing for lart"

static struct mcp_plat_data lart_mcp_data = {
	.mccr0		= MCCR0_ADM,
	.sclk_rate	= 11981000,
};

#ifdef LART_GREY_LCD
static struct sa1100fb_mach_info lart_grey_info = {
	.pixclock	= 150000,	.bpp		= 4,
	.xres		= 320,		.yres		= 240,

	.hsync_len	= 1,		.vsync_len	= 1,
	.left_margin	= 4,		.upper_margin	= 0,
	.right_margin	= 2,		.lower_margin	= 0,

	.cmap_greyscale	= 1,
	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,

	.lccr0		= LCCR0_Mono | LCCR0_Sngl | LCCR0_Pas | LCCR0_4PixMono,
	.lccr3		= LCCR3_OutEnH | LCCR3_PixRsEdg | LCCR3_ACBsDiv(512),
};
#endif
#ifdef LART_COLOR_LCD
static struct sa1100fb_mach_info lart_color_info = {
	.pixclock	= 150000,	.bpp		= 16,
	.xres		= 320,		.yres		= 240,

	.hsync_len	= 2,		.vsync_len	= 3,
	.left_margin	= 69,		.upper_margin	= 14,
	.right_margin	= 8,		.lower_margin	= 4,

	.lccr0		= LCCR0_Color | LCCR0_Sngl | LCCR0_Act,
	.lccr3		= LCCR3_OutEnH | LCCR3_PixFlEdg | LCCR3_ACBsDiv(512),
};
#endif
#ifdef LART_VIDEO_OUT
static struct sa1100fb_mach_info lart_video_info = {
	.pixclock	= 39721,	.bpp		= 16,
	.xres		= 640,		.yres		= 480,

	.hsync_len	= 95,		.vsync_len	= 2,
	.left_margin	= 40,		.upper_margin	= 32,
	.right_margin	= 24,		.lower_margin	= 11,

	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,

	.lccr0		= LCCR0_Color | LCCR0_Sngl | LCCR0_Act,
	.lccr3		= LCCR3_OutEnL | LCCR3_PixFlEdg | LCCR3_ACBsDiv(512),
};
#endif

#ifdef LART_KIT01_LCD
static struct sa1100fb_mach_info lart_kit01_info = {
	.pixclock	= 63291,	.bpp		= 16,
	.xres		= 640,		.yres		= 480,

	.hsync_len	= 64,		.vsync_len	= 3,
	.left_margin	= 122,		.upper_margin	= 45,
	.right_margin	= 10,		.lower_margin	= 10,

	.lccr0		= LCCR0_Color | LCCR0_Sngl | LCCR0_Act,
	.lccr3		= LCCR3_OutEnH | LCCR3_PixFlEdg
};
#endif

static void __init lart_init(void)
{
	struct sa1100fb_mach_info *inf = NULL;

#ifdef LART_GREY_LCD
	inf = &lart_grey_info;
#endif
#ifdef LART_COLOR_LCD
	inf = &lart_color_info;
#endif
#ifdef LART_VIDEO_OUT
	inf = &lart_video_info;
#endif
#ifdef LART_KIT01_LCD
	inf = &lart_kit01_info;
#endif

	if (inf)
		sa11x0_register_lcd(inf);

	sa11x0_ppc_configure_mcp();
	sa11x0_register_mcp(&lart_mcp_data);
}

static struct map_desc lart_io_desc[] __initdata = {
	{	/* main flash memory */
		.virtual	=  0xe8000000,
		.pfn		= __phys_to_pfn(0x00000000),
		.length		= 0x00400000,
		.type		= MT_DEVICE
	}, {	/* main flash, alternative location */
		.virtual	=  0xec000000,
		.pfn		= __phys_to_pfn(0x08000000),
		.length		= 0x00400000,
		.type		= MT_DEVICE
	}
};

static void __init lart_map_io(void)
{
	sa1100_map_io();
	iotable_init(lart_io_desc, ARRAY_SIZE(lart_io_desc));

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
	sa1100_register_uart(2, 2);

	GAFR |= (GPIO_UART_TXD | GPIO_UART_RXD);
	GPDR |= GPIO_UART_TXD;
	GPDR &= ~GPIO_UART_RXD;
	PPAR |= PPAR_UPR;
}

MACHINE_START(LART, "LART")
	.atag_offset	= 0x100,
	.map_io		= lart_map_io,
	.nr_irqs	= SA1100_NR_IRQS,
	.init_irq	= sa1100_init_irq,
	.init_machine	= lart_init,
	.timer		= &sa1100_timer,
	.restart	= sa11x0_restart,
MACHINE_END
