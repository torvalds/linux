// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright STMicroelectronics, 2007.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>

/*
 * These are the only hard-coded address offsets we still have to use.
 */
#define NOMADIK_FSMC_BASE	0x10100000	/* FSMC registers */
#define NOMADIK_SDRAMC_BASE	0x10110000	/* SDRAM Controller */
#define NOMADIK_CLCDC_BASE	0x10120000	/* CLCD Controller */
#define NOMADIK_MDIF_BASE	0x10120000	/* MDIF */
#define NOMADIK_DMA0_BASE	0x10130000	/* DMA0 Controller */
#define NOMADIK_IC_BASE		0x10140000	/* Vectored Irq Controller */
#define NOMADIK_DMA1_BASE	0x10150000	/* DMA1 Controller */
#define NOMADIK_USB_BASE	0x10170000	/* USB-OTG conf reg base */
#define NOMADIK_CRYP_BASE	0x10180000	/* Crypto processor */
#define NOMADIK_SHA1_BASE	0x10190000	/* SHA-1 Processor */
#define NOMADIK_XTI_BASE	0x101A0000	/* XTI */
#define NOMADIK_RNG_BASE	0x101B0000	/* Random number generator */
#define NOMADIK_SRC_BASE	0x101E0000	/* SRC base */
#define NOMADIK_WDOG_BASE	0x101E1000	/* Watchdog */
#define NOMADIK_MTU0_BASE	0x101E2000	/* Multiple Timer 0 */
#define NOMADIK_MTU1_BASE	0x101E3000	/* Multiple Timer 1 */
#define NOMADIK_GPIO0_BASE	0x101E4000	/* GPIO0 */
#define NOMADIK_GPIO1_BASE	0x101E5000	/* GPIO1 */
#define NOMADIK_GPIO2_BASE	0x101E6000	/* GPIO2 */
#define NOMADIK_GPIO3_BASE	0x101E7000	/* GPIO3 */
#define NOMADIK_RTC_BASE	0x101E8000	/* Real Time Clock base */
#define NOMADIK_PMU_BASE	0x101E9000	/* Power Management Unit */
#define NOMADIK_OWM_BASE	0x101EA000	/* One wire master */
#define NOMADIK_SCR_BASE	0x101EF000	/* Secure Control registers */
#define NOMADIK_MSP2_BASE	0x101F0000	/* MSP 2 interface */
#define NOMADIK_MSP1_BASE	0x101F1000	/* MSP 1 interface */
#define NOMADIK_UART2_BASE	0x101F2000	/* UART 2 interface */
#define NOMADIK_SSIRx_BASE	0x101F3000	/* SSI 8-ch rx interface */
#define NOMADIK_SSITx_BASE	0x101F4000	/* SSI 8-ch tx interface */
#define NOMADIK_MSHC_BASE	0x101F5000	/* Memory Stick(Pro) Host */
#define NOMADIK_SDI_BASE	0x101F6000	/* SD-card/MM-Card */
#define NOMADIK_I2C1_BASE	0x101F7000	/* I2C1 interface */
#define NOMADIK_I2C0_BASE	0x101F8000	/* I2C0 interface */
#define NOMADIK_MSP0_BASE	0x101F9000	/* MSP 0 interface */
#define NOMADIK_FIRDA_BASE	0x101FA000	/* FIrDA interface */
#define NOMADIK_UART1_BASE	0x101FB000	/* UART 1 interface */
#define NOMADIK_SSP_BASE	0x101FC000	/* SSP interface */
#define NOMADIK_UART0_BASE	0x101FD000	/* UART 0 interface */
#define NOMADIK_SGA_BASE	0x101FE000	/* SGA interface */
#define NOMADIK_L2CC_BASE	0x10210000	/* L2 Cache controller */
#define NOMADIK_UART1_VBASE	0xF01FB000

/* This is needed for LL-debug/earlyprintk/debug-macro.S */
static struct map_desc cpu8815_io_desc[] __initdata = {
	{
		.virtual =	NOMADIK_UART1_VBASE,
		.pfn =		__phys_to_pfn(NOMADIK_UART1_BASE),
		.length =	SZ_4K,
		.type =		MT_DEVICE,
	},
};

static void __init cpu8815_map_io(void)
{
	iotable_init(cpu8815_io_desc, ARRAY_SIZE(cpu8815_io_desc));
}

static void cpu8815_restart(enum reboot_mode mode, const char *cmd)
{
	void __iomem *srcbase = ioremap(NOMADIK_SRC_BASE, SZ_4K);

	/* FIXME: use egpio when implemented */

	/* Write anything to Reset status register */
	writel(1, srcbase + 0x18);
}

static const char * cpu8815_board_compat[] = {
	"st,nomadik-nhk-15",
	"calaosystems,usb-s8815",
	NULL,
};

DT_MACHINE_START(NOMADIK_DT, "Nomadik STn8815")
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.map_io		= cpu8815_map_io,
	.restart	= cpu8815_restart,
	.dt_compat      = cpu8815_board_compat,
MACHINE_END
