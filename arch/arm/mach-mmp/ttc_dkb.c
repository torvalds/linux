/*
 *  linux/arch/arm/mach-mmp/ttc_dkb.c
 *
 *  Support for the Marvell PXA910-based TTC_DKB Development Platform.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/onenand.h>
#include <linux/interrupt.h>
#include <linux/platform_data/pca953x.h>
#include <linux/gpio.h>
#include <linux/gpio-pxa.h>
#include <linux/mfd/88pm860x.h>
#include <linux/platform_data/mv_usb.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <mach/addr-map.h>
#include <mach/mfp-pxa910.h>
#include <mach/pxa910.h>
#include <mach/irqs.h>
#include <mach/regs-usb.h>

#include "common.h"

#define TTCDKB_GPIO_EXT0(x)	(MMP_NR_BUILTIN_GPIO + ((x < 0) ? 0 :	\
				((x < 16) ? x : 15)))
#define TTCDKB_GPIO_EXT1(x)	(MMP_NR_BUILTIN_GPIO + 16 + ((x < 0) ? 0 : \
				((x < 16) ? x : 15)))

/*
 * 16 board interrupts -- MAX7312 GPIO expander
 * 16 board interrupts -- PCA9575 GPIO expander
 * 24 board interrupts -- 88PM860x PMIC
 */
#define TTCDKB_NR_IRQS		(MMP_NR_IRQS + 16 + 16 + 24)

static unsigned long ttc_dkb_pin_config[] __initdata = {
	/* UART2 */
	GPIO47_UART2_RXD,
	GPIO48_UART2_TXD,

	/* DFI */
	DF_IO0_ND_IO0,
	DF_IO1_ND_IO1,
	DF_IO2_ND_IO2,
	DF_IO3_ND_IO3,
	DF_IO4_ND_IO4,
	DF_IO5_ND_IO5,
	DF_IO6_ND_IO6,
	DF_IO7_ND_IO7,
	DF_IO8_ND_IO8,
	DF_IO9_ND_IO9,
	DF_IO10_ND_IO10,
	DF_IO11_ND_IO11,
	DF_IO12_ND_IO12,
	DF_IO13_ND_IO13,
	DF_IO14_ND_IO14,
	DF_IO15_ND_IO15,
	DF_nCS0_SM_nCS2_nCS0,
	DF_ALE_SM_WEn_ND_ALE,
	DF_CLE_SM_OEn_ND_CLE,
	DF_WEn_DF_WEn,
	DF_REn_DF_REn,
	DF_RDY0_DF_RDY0,
};

static struct pxa_gpio_platform_data pxa910_gpio_pdata = {
	.irq_base	= MMP_GPIO_TO_IRQ(0),
};

static struct mtd_partition ttc_dkb_onenand_partitions[] = {
	{
		.name		= "bootloader",
		.offset		= 0,
		.size		= SZ_1M,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "reserved",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "reserved",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_8M,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= (SZ_2M + SZ_1M),
		.mask_flags	= 0,
	}, {
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_32M + SZ_16M,
		.mask_flags	= 0,
	}
};

static struct onenand_platform_data ttc_dkb_onenand_info = {
	.parts		= ttc_dkb_onenand_partitions,
	.nr_parts	= ARRAY_SIZE(ttc_dkb_onenand_partitions),
};

static struct resource ttc_dkb_resource_onenand[] = {
	[0] = {
		.start	= SMC_CS0_PHYS_BASE,
		.end	= SMC_CS0_PHYS_BASE + SZ_1M,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device ttc_dkb_device_onenand = {
	.name		= "onenand-flash",
	.id		= -1,
	.resource	= ttc_dkb_resource_onenand,
	.num_resources	= ARRAY_SIZE(ttc_dkb_resource_onenand),
	.dev		= {
		.platform_data	= &ttc_dkb_onenand_info,
	},
};

static struct platform_device *ttc_dkb_devices[] = {
	&pxa910_device_gpio,
	&pxa910_device_rtc,
	&ttc_dkb_device_onenand,
};

static struct pca953x_platform_data max7312_data[] = {
	{
		.gpio_base	= TTCDKB_GPIO_EXT0(0),
		.irq_base	= MMP_NR_IRQS,
	},
};

static struct pm860x_platform_data ttc_dkb_pm8607_info = {
	.irq_base       = IRQ_BOARD_START,
};

static struct i2c_board_info ttc_dkb_i2c_info[] = {
	{
		.type           = "88PM860x",
		.addr           = 0x34,
		.platform_data  = &ttc_dkb_pm8607_info,
		.irq            = IRQ_PXA910_PMIC_INT,
	},
	{
		.type		= "max7312",
		.addr		= 0x23,
		.irq		= MMP_GPIO_TO_IRQ(80),
		.platform_data	= &max7312_data,
	},
};

#if IS_ENABLED(CONFIG_USB_SUPPORT)
#if IS_ENABLED(CONFIG_USB_MV_UDC) || IS_ENABLED(CONFIG_USB_EHCI_MV_U2O)

static struct mv_usb_platform_data ttc_usb_pdata = {
	.vbus		= NULL,
	.mode		= MV_USB_MODE_OTG,
	.otg_force_a_bus_req = 1,
	.phy_init	= pxa_usb_phy_init,
	.phy_deinit	= pxa_usb_phy_deinit,
	.set_vbus	= NULL,
};
#endif
#endif

#if IS_ENABLED(CONFIG_MTD_NAND_PXA3xx)
static struct pxa3xx_nand_platform_data dkb_nand_info = {
	.enable_arbiter = 1,
	.num_cs = 1,
};
#endif

#if IS_ENABLED(CONFIG_MMP_DISP)
/* path config */
#define CFG_IOPADMODE(iopad)   (iopad)  /* 0x0 ~ 0xd */
#define SCLK_SOURCE_SELECT(x)  (x << 30) /* 0x0 ~ 0x3 */
/* link config */
#define CFG_DUMBMODE(mode)     (mode << 28) /* 0x0 ~ 0x6*/
static struct mmp_mach_path_config dkb_disp_config[] = {
	[0] = {
		.name = "mmp-parallel",
		.overlay_num = 2,
		.output_type = PATH_OUT_PARALLEL,
		.path_config = CFG_IOPADMODE(0x1)
			| SCLK_SOURCE_SELECT(0x1),
		.link_config = CFG_DUMBMODE(0x2),
	},
};

static struct mmp_mach_plat_info dkb_disp_info = {
	.name = "mmp-disp",
	.clk_name = "disp0",
	.path_num = 1,
	.paths = dkb_disp_config,
};

static struct mmp_buffer_driver_mach_info dkb_fb_info = {
	.name = "mmp-fb",
	.path_name = "mmp-parallel",
	.overlay_id = 0,
	.dmafetch_id = 1,
	.default_pixfmt = PIXFMT_RGB565,
};

static void dkb_tpo_panel_power(int on)
{
	int err;
	u32 spi_reset = mfp_to_gpio(MFP_PIN_GPIO106);

	if (on) {
		err = gpio_request(spi_reset, "TPO_LCD_SPI_RESET");
		if (err) {
			pr_err("failed to request GPIO for TPO LCD RESET\n");
			return;
		}
		gpio_direction_output(spi_reset, 0);
		udelay(100);
		gpio_set_value(spi_reset, 1);
		gpio_free(spi_reset);
	} else {
		err = gpio_request(spi_reset, "TPO_LCD_SPI_RESET");
		if (err) {
			pr_err("failed to request LCD RESET gpio\n");
			return;
		}
		gpio_set_value(spi_reset, 0);
		gpio_free(spi_reset);
	}
}

static struct mmp_mach_panel_info dkb_tpo_panel_info = {
	.name = "tpo-hvga",
	.plat_path_name = "mmp-parallel",
	.plat_set_onoff = dkb_tpo_panel_power,
};

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias       = "tpo-hvga",
		.platform_data  = &dkb_tpo_panel_info,
		.bus_num        = 5,
	}
};

static void __init add_disp(void)
{
	pxa_register_device(&pxa910_device_disp,
		&dkb_disp_info, sizeof(dkb_disp_info));
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
	pxa_register_device(&pxa910_device_fb,
		&dkb_fb_info, sizeof(dkb_fb_info));
	pxa_register_device(&pxa910_device_panel,
		&dkb_tpo_panel_info, sizeof(dkb_tpo_panel_info));
}
#endif

static void __init ttc_dkb_init(void)
{
	mfp_config(ARRAY_AND_SIZE(ttc_dkb_pin_config));

	/* on-chip devices */
	pxa910_add_uart(1);
#if IS_ENABLED(CONFIG_MTD_NAND_PXA3xx)
	pxa910_add_nand(&dkb_nand_info);
#endif

	/* off-chip devices */
	pxa910_add_twsi(0, NULL, ARRAY_AND_SIZE(ttc_dkb_i2c_info));
	platform_device_add_data(&pxa910_device_gpio, &pxa910_gpio_pdata,
				 sizeof(struct pxa_gpio_platform_data));
	platform_add_devices(ARRAY_AND_SIZE(ttc_dkb_devices));

#if IS_ENABLED(CONFIG_USB_MV_UDC)
	pxa168_device_u2o.dev.platform_data = &ttc_usb_pdata;
	platform_device_register(&pxa168_device_u2o);
#endif

#if IS_ENABLED(CONFIG_USB_EHCI_MV_U2O)
	pxa168_device_u2oehci.dev.platform_data = &ttc_usb_pdata;
	platform_device_register(&pxa168_device_u2oehci);
#endif

#if IS_ENABLED(CONFIG_USB_MV_OTG)
	pxa168_device_u2ootg.dev.platform_data = &ttc_usb_pdata;
	platform_device_register(&pxa168_device_u2ootg);
#endif

#if IS_ENABLED(CONFIG_MMP_DISP)
	add_disp();
#endif
}

MACHINE_START(TTC_DKB, "PXA910-based TTC_DKB Development Platform")
	.map_io		= mmp_map_io,
	.nr_irqs	= TTCDKB_NR_IRQS,
	.init_irq       = pxa910_init_irq,
	.init_time	= pxa910_timer_init,
	.init_machine   = ttc_dkb_init,
	.restart	= mmp_restart,
MACHINE_END
