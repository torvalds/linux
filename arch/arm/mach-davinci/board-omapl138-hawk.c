/*
 * Hawkboard.org based on TI's OMAP-L138 Platform
 *
 * Initial code: Syed Mohammed Khasim
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of
 * any kind, whether express or implied.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/gpio/machine.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/rawnand.h>
#include <linux/platform_data/gpio-davinci.h>
#include <linux/platform_data/mtd-davinci.h>
#include <linux/platform_data/mtd-davinci-aemif.h>
#include <linux/platform_data/ti-aemif.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/common.h>
#include <mach/da8xx.h>
#include <mach/mux.h>

#define HAWKBOARD_PHY_ID		"davinci_mdio-0:07"

#define DA850_USB1_VBUS_PIN		GPIO_TO_PIN(2, 4)
#define DA850_USB1_OC_PIN		GPIO_TO_PIN(6, 13)

static short omapl138_hawk_mii_pins[] __initdata = {
	DA850_MII_TXEN, DA850_MII_TXCLK, DA850_MII_COL, DA850_MII_TXD_3,
	DA850_MII_TXD_2, DA850_MII_TXD_1, DA850_MII_TXD_0, DA850_MII_RXER,
	DA850_MII_CRS, DA850_MII_RXCLK, DA850_MII_RXDV, DA850_MII_RXD_3,
	DA850_MII_RXD_2, DA850_MII_RXD_1, DA850_MII_RXD_0, DA850_MDIO_CLK,
	DA850_MDIO_D,
	-1
};

static __init void omapl138_hawk_config_emac(void)
{
	void __iomem *cfgchip3 = DA8XX_SYSCFG0_VIRT(DA8XX_CFGCHIP3_REG);
	int ret;
	u32 val;
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	val = __raw_readl(cfgchip3);
	val &= ~BIT(8);
	ret = davinci_cfg_reg_list(omapl138_hawk_mii_pins);
	if (ret) {
		pr_warn("%s: CPGMAC/MII mux setup failed: %d\n", __func__, ret);
		return;
	}

	/* configure the CFGCHIP3 register for MII */
	__raw_writel(val, cfgchip3);
	pr_info("EMAC: MII PHY configured\n");

	soc_info->emac_pdata->phy_id = HAWKBOARD_PHY_ID;

	ret = da8xx_register_emac();
	if (ret)
		pr_warn("%s: EMAC registration failed: %d\n", __func__, ret);
}

/*
 * The following EDMA channels/slots are not being used by drivers (for
 * example: Timer, GPIO, UART events etc) on da850/omap-l138 EVM/Hawkboard,
 * hence they are being reserved for codecs on the DSP side.
 */
static const s16 da850_dma0_rsv_chans[][2] = {
	/* (offset, number) */
	{ 8,  6},
	{24,  4},
	{30,  2},
	{-1, -1}
};

static const s16 da850_dma0_rsv_slots[][2] = {
	/* (offset, number) */
	{ 8,  6},
	{24,  4},
	{30, 50},
	{-1, -1}
};

static const s16 da850_dma1_rsv_chans[][2] = {
	/* (offset, number) */
	{ 0, 28},
	{30,  2},
	{-1, -1}
};

static const s16 da850_dma1_rsv_slots[][2] = {
	/* (offset, number) */
	{ 0, 28},
	{30, 90},
	{-1, -1}
};

static struct edma_rsv_info da850_edma_cc0_rsv = {
	.rsv_chans	= da850_dma0_rsv_chans,
	.rsv_slots	= da850_dma0_rsv_slots,
};

static struct edma_rsv_info da850_edma_cc1_rsv = {
	.rsv_chans	= da850_dma1_rsv_chans,
	.rsv_slots	= da850_dma1_rsv_slots,
};

static struct edma_rsv_info *da850_edma_rsv[2] = {
	&da850_edma_cc0_rsv,
	&da850_edma_cc1_rsv,
};

static const short hawk_mmcsd0_pins[] = {
	DA850_MMCSD0_DAT_0, DA850_MMCSD0_DAT_1, DA850_MMCSD0_DAT_2,
	DA850_MMCSD0_DAT_3, DA850_MMCSD0_CLK, DA850_MMCSD0_CMD,
	DA850_GPIO3_12, DA850_GPIO3_13,
	-1
};

#define DA850_HAWK_MMCSD_CD_PIN		GPIO_TO_PIN(3, 12)
#define DA850_HAWK_MMCSD_WP_PIN		GPIO_TO_PIN(3, 13)

static struct gpiod_lookup_table mmc_gpios_table = {
	.dev_id = "da830-mmc.0",
	.table = {
		GPIO_LOOKUP("davinci_gpio", DA850_HAWK_MMCSD_CD_PIN, "cd",
			    GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("davinci_gpio", DA850_HAWK_MMCSD_WP_PIN, "wp",
			    GPIO_ACTIVE_LOW),
	},
};

static struct davinci_mmc_config da850_mmc_config = {
	.wires		= 4,
	.max_freq	= 50000000,
	.caps		= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
};

static __init void omapl138_hawk_mmc_init(void)
{
	int ret;

	ret = davinci_cfg_reg_list(hawk_mmcsd0_pins);
	if (ret) {
		pr_warn("%s: MMC/SD0 mux setup failed: %d\n", __func__, ret);
		return;
	}

	gpiod_add_lookup_table(&mmc_gpios_table);

	ret = da8xx_register_mmcsd0(&da850_mmc_config);
	if (ret) {
		pr_warn("%s: MMC/SD0 registration failed: %d\n", __func__, ret);
		goto mmc_setup_mmcsd_fail;
	}

	return;

mmc_setup_mmcsd_fail:
	gpiod_remove_lookup_table(&mmc_gpios_table);
}

static struct mtd_partition omapl138_hawk_nandflash_partition[] = {
	{
		.name		= "u-boot env",
		.offset		= 0,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE,
	 },
	{
		.name		= "u-boot",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_512K,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "free space",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0,
	},
};

static struct davinci_aemif_timing omapl138_hawk_nandflash_timing = {
	.wsetup		= 24,
	.wstrobe	= 21,
	.whold		= 14,
	.rsetup		= 19,
	.rstrobe	= 50,
	.rhold		= 0,
	.ta		= 20,
};

static struct davinci_nand_pdata omapl138_hawk_nandflash_data = {
	.core_chipsel	= 1,
	.parts		= omapl138_hawk_nandflash_partition,
	.nr_parts	= ARRAY_SIZE(omapl138_hawk_nandflash_partition),
	.ecc_mode	= NAND_ECC_HW,
	.ecc_bits	= 4,
	.bbt_options	= NAND_BBT_USE_FLASH,
	.options	= NAND_BUSWIDTH_16,
	.timing		= &omapl138_hawk_nandflash_timing,
	.mask_chipsel	= 0,
	.mask_ale	= 0,
	.mask_cle	= 0,
};

static struct resource omapl138_hawk_nandflash_resource[] = {
	{
		.start	= DA8XX_AEMIF_CS3_BASE,
		.end	= DA8XX_AEMIF_CS3_BASE + SZ_32M,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= DA8XX_AEMIF_CTL_BASE,
		.end	= DA8XX_AEMIF_CTL_BASE + SZ_32K,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource omapl138_hawk_aemif_resource[] = {
	{
		.start	= DA8XX_AEMIF_CTL_BASE,
		.end	= DA8XX_AEMIF_CTL_BASE + SZ_32K,
		.flags	= IORESOURCE_MEM,
	}
};

static struct aemif_abus_data omapl138_hawk_aemif_abus_data[] = {
	{
		.cs	= 3,
	}
};

static struct platform_device omapl138_hawk_aemif_devices[] = {
	{
		.name		= "davinci_nand",
		.id		= -1,
		.dev		= {
			.platform_data	= &omapl138_hawk_nandflash_data,
		},
		.resource	= omapl138_hawk_nandflash_resource,
		.num_resources	= ARRAY_SIZE(omapl138_hawk_nandflash_resource),
	}
};

static struct aemif_platform_data omapl138_hawk_aemif_pdata = {
	.cs_offset = 2,
	.abus_data = omapl138_hawk_aemif_abus_data,
	.num_abus_data = ARRAY_SIZE(omapl138_hawk_aemif_abus_data),
	.sub_devices = omapl138_hawk_aemif_devices,
	.num_sub_devices = ARRAY_SIZE(omapl138_hawk_aemif_devices),
};

static struct platform_device omapl138_hawk_aemif_device = {
	.name		= "ti-aemif",
	.id		= -1,
	.dev = {
		.platform_data	= &omapl138_hawk_aemif_pdata,
	},
	.resource	= omapl138_hawk_aemif_resource,
	.num_resources	= ARRAY_SIZE(omapl138_hawk_aemif_resource),
};

static const short omapl138_hawk_nand_pins[] = {
	DA850_EMA_WAIT_1, DA850_NEMA_OE, DA850_NEMA_WE, DA850_NEMA_CS_3,
	DA850_EMA_D_0, DA850_EMA_D_1, DA850_EMA_D_2, DA850_EMA_D_3,
	DA850_EMA_D_4, DA850_EMA_D_5, DA850_EMA_D_6, DA850_EMA_D_7,
	DA850_EMA_D_8, DA850_EMA_D_9, DA850_EMA_D_10, DA850_EMA_D_11,
	DA850_EMA_D_12, DA850_EMA_D_13, DA850_EMA_D_14, DA850_EMA_D_15,
	DA850_EMA_A_1, DA850_EMA_A_2,
	-1
};

static int omapl138_hawk_register_aemif(void)
{
	int ret;

	ret = davinci_cfg_reg_list(omapl138_hawk_nand_pins);
	if (ret)
		pr_warn("%s: NAND mux setup failed: %d\n", __func__, ret);

	return platform_device_register(&omapl138_hawk_aemif_device);
}

static const short da850_hawk_usb11_pins[] = {
	DA850_GPIO2_4, DA850_GPIO6_13,
	-1
};

static struct regulator_consumer_supply hawk_usb_supplies[] = {
	REGULATOR_SUPPLY("vbus", NULL),
};

static struct regulator_init_data hawk_usb_vbus_data = {
	.consumer_supplies	= hawk_usb_supplies,
	.num_consumer_supplies	= ARRAY_SIZE(hawk_usb_supplies),
	.constraints    = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

static struct fixed_voltage_config hawk_usb_vbus = {
	.supply_name		= "vbus",
	.microvolts		= 3300000,
	.init_data		= &hawk_usb_vbus_data,
};

static struct platform_device hawk_usb_vbus_device = {
	.name		= "reg-fixed-voltage",
	.id		= 0,
	.dev		= {
		.platform_data = &hawk_usb_vbus,
	},
};

static struct gpiod_lookup_table hawk_usb_oc_gpio_lookup = {
	.dev_id		= "ohci-da8xx",
	.table = {
		GPIO_LOOKUP("davinci_gpio", DA850_USB1_OC_PIN, "oc", 0),
		{ }
	},
};

static struct gpiod_lookup_table hawk_usb_vbus_gpio_lookup = {
	.dev_id		= "reg-fixed-voltage.0",
	.table = {
		GPIO_LOOKUP("davinci_gpio", DA850_USB1_VBUS_PIN, NULL, 0),
		{ }
	},
};

static struct gpiod_lookup_table *hawk_usb_gpio_lookups[] = {
	&hawk_usb_oc_gpio_lookup,
	&hawk_usb_vbus_gpio_lookup,
};

static struct da8xx_ohci_root_hub omapl138_hawk_usb11_pdata = {
	/* TPS2087 switch @ 5V */
	.potpgt         = (3 + 1) / 2,  /* 3 ms max */
};

static __init void omapl138_hawk_usb_init(void)
{
	int ret;

	ret = davinci_cfg_reg_list(da850_hawk_usb11_pins);
	if (ret) {
		pr_warn("%s: USB 1.1 PinMux setup failed: %d\n", __func__, ret);
		return;
	}

	ret = da8xx_register_usb_phy_clocks();
	if (ret)
		pr_warn("%s: USB PHY CLK registration failed: %d\n",
			__func__, ret);

	gpiod_add_lookup_tables(hawk_usb_gpio_lookups,
				ARRAY_SIZE(hawk_usb_gpio_lookups));

	ret = da8xx_register_usb_phy();
	if (ret)
		pr_warn("%s: USB PHY registration failed: %d\n",
			__func__, ret);

	ret = platform_device_register(&hawk_usb_vbus_device);
	if (ret) {
		pr_warn("%s: Unable to register the vbus supply\n", __func__);
		return;
	}

	ret = da8xx_register_usb11(&omapl138_hawk_usb11_pdata);
	if (ret)
		pr_warn("%s: USB 1.1 registration failed: %d\n", __func__, ret);

	return;
}

static __init void omapl138_hawk_init(void)
{
	int ret;

	da850_register_clocks();

	ret = da850_register_gpio();
	if (ret)
		pr_warn("%s: GPIO init failed: %d\n", __func__, ret);

	davinci_serial_init(da8xx_serial_device);

	omapl138_hawk_config_emac();

	ret = da850_register_edma(da850_edma_rsv);
	if (ret)
		pr_warn("%s: EDMA registration failed: %d\n", __func__, ret);

	omapl138_hawk_mmc_init();

	omapl138_hawk_usb_init();

	ret = omapl138_hawk_register_aemif();
	if (ret)
		pr_warn("%s: aemif registration failed: %d\n", __func__, ret);

	ret = da8xx_register_watchdog();
	if (ret)
		pr_warn("%s: watchdog registration failed: %d\n",
			__func__, ret);

	ret = da8xx_register_rproc();
	if (ret)
		pr_warn("%s: dsp/rproc registration failed: %d\n",
			__func__, ret);

	regulator_has_full_constraints();
}

#ifdef CONFIG_SERIAL_8250_CONSOLE
static int __init omapl138_hawk_console_init(void)
{
	if (!machine_is_omapl138_hawkboard())
		return 0;

	return add_preferred_console("ttyS", 2, "115200");
}
console_initcall(omapl138_hawk_console_init);
#endif

static void __init omapl138_hawk_map_io(void)
{
	da850_init();
}

MACHINE_START(OMAPL138_HAWKBOARD, "AM18x/OMAP-L138 Hawkboard")
	.atag_offset	= 0x100,
	.map_io		= omapl138_hawk_map_io,
	.init_irq	= da850_init_irq,
	.init_time	= da850_init_time,
	.init_machine	= omapl138_hawk_init,
	.init_late	= davinci_init_late,
	.dma_zone_size	= SZ_128M,
	.reserve	= da8xx_rproc_reserve_cma,
MACHINE_END
