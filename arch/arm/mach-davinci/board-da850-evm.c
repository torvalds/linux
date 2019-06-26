/*
 * TI DA850/OMAP-L138 EVM board
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Derived from: arch/arm/mach-davinci/board-da830-evm.c
 * Original Copyrights follow:
 *
 * 2007, 2009 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/gpio/machine.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/i2c.h>
#include <linux/platform_data/pca953x.h>
#include <linux/input.h>
#include <linux/input/tps6507x-ts.h>
#include <linux/mfd/tps6507x.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/nvmem-provider.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <linux/platform_data/gpio-davinci.h>
#include <linux/platform_data/mtd-davinci.h>
#include <linux/platform_data/mtd-davinci-aemif.h>
#include <linux/platform_data/ti-aemif.h>
#include <linux/platform_data/spi-davinci.h>
#include <linux/platform_data/uio_pruss.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/tps6507x.h>
#include <linux/regulator/fixed.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>

#include <mach/common.h>
#include <mach/da8xx.h>
#include <mach/mux.h>

#include "irqs.h"
#include "sram.h"

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/system_info.h>

#include <media/i2c/tvp514x.h>
#include <media/i2c/adv7343.h>

#define DA850_EVM_PHY_ID		"davinci_mdio-0:00"
#define DA850_LCD_PWR_PIN		GPIO_TO_PIN(2, 8)
#define DA850_LCD_BL_PIN		GPIO_TO_PIN(2, 15)

#define DA850_MII_MDIO_CLKEN_PIN	GPIO_TO_PIN(2, 6)

static struct mtd_partition da850evm_spiflash_part[] = {
	[0] = {
		.name = "UBL",
		.offset = 0,
		.size = SZ_64K,
		.mask_flags = MTD_WRITEABLE,
	},
	[1] = {
		.name = "U-Boot",
		.offset = MTDPART_OFS_APPEND,
		.size = SZ_512K,
		.mask_flags = MTD_WRITEABLE,
	},
	[2] = {
		.name = "U-Boot-Env",
		.offset = MTDPART_OFS_APPEND,
		.size = SZ_64K,
		.mask_flags = MTD_WRITEABLE,
	},
	[3] = {
		.name = "Kernel",
		.offset = MTDPART_OFS_APPEND,
		.size = SZ_2M + SZ_512K,
		.mask_flags = 0,
	},
	[4] = {
		.name = "Filesystem",
		.offset = MTDPART_OFS_APPEND,
		.size = SZ_4M,
		.mask_flags = 0,
	},
	[5] = {
		.name = "MAC-Address",
		.offset = SZ_8M - SZ_64K,
		.size = SZ_64K,
		.mask_flags = MTD_WRITEABLE,
	},
};

static struct nvmem_cell_info da850evm_nvmem_cells[] = {
	{
		.name		= "macaddr",
		.offset		= 0x0,
		.bytes		= ETH_ALEN,
	}
};

static struct nvmem_cell_table da850evm_nvmem_cell_table = {
	/*
	 * The nvmem name differs from the partition name because of the
	 * internal works of the nvmem framework.
	 */
	.nvmem_name	= "MAC-Address0",
	.cells		= da850evm_nvmem_cells,
	.ncells		= ARRAY_SIZE(da850evm_nvmem_cells),
};

static struct nvmem_cell_lookup da850evm_nvmem_cell_lookup = {
	.nvmem_name	= "MAC-Address0",
	.cell_name	= "macaddr",
	.dev_id		= "davinci_emac.1",
	.con_id		= "mac-address",
};

static struct flash_platform_data da850evm_spiflash_data = {
	.name		= "m25p80",
	.parts		= da850evm_spiflash_part,
	.nr_parts	= ARRAY_SIZE(da850evm_spiflash_part),
	.type		= "m25p64",
};

static struct davinci_spi_config da850evm_spiflash_cfg = {
	.io_type	= SPI_IO_TYPE_DMA,
	.c2tdelay	= 8,
	.t2cdelay	= 8,
};

static struct spi_board_info da850evm_spi_info[] = {
	{
		.modalias		= "m25p80",
		.platform_data		= &da850evm_spiflash_data,
		.controller_data	= &da850evm_spiflash_cfg,
		.mode			= SPI_MODE_0,
		.max_speed_hz		= 30000000,
		.bus_num		= 1,
		.chip_select		= 0,
	},
};

static struct mtd_partition da850_evm_norflash_partition[] = {
	{
		.name           = "bootloaders + env",
		.offset         = 0,
		.size           = SZ_512K,
		.mask_flags     = MTD_WRITEABLE,
	},
	{
		.name           = "kernel",
		.offset         = MTDPART_OFS_APPEND,
		.size           = SZ_2M,
		.mask_flags     = 0,
	},
	{
		.name           = "filesystem",
		.offset         = MTDPART_OFS_APPEND,
		.size           = MTDPART_SIZ_FULL,
		.mask_flags     = 0,
	},
};

static struct physmap_flash_data da850_evm_norflash_data = {
	.width		= 2,
	.parts		= da850_evm_norflash_partition,
	.nr_parts	= ARRAY_SIZE(da850_evm_norflash_partition),
};

static struct resource da850_evm_norflash_resource[] = {
	{
		.start	= DA8XX_AEMIF_CS2_BASE,
		.end	= DA8XX_AEMIF_CS2_BASE + SZ_32M - 1,
		.flags	= IORESOURCE_MEM,
	},
};

/* DA850/OMAP-L138 EVM includes a 512 MByte large-page NAND flash
 * (128K blocks). It may be used instead of the (default) SPI flash
 * to boot, using TI's tools to install the secondary boot loader
 * (UBL) and U-Boot.
 */
static struct mtd_partition da850_evm_nandflash_partition[] = {
	{
		.name		= "u-boot env",
		.offset		= 0,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE,
	 },
	{
		.name		= "UBL",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "u-boot",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 4 * SZ_128K,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "kernel",
		.offset		= 0x200000,
		.size		= SZ_2M,
		.mask_flags	= 0,
	},
	{
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0,
	},
};

static struct davinci_aemif_timing da850_evm_nandflash_timing = {
	.wsetup		= 24,
	.wstrobe	= 21,
	.whold		= 14,
	.rsetup		= 19,
	.rstrobe	= 50,
	.rhold		= 0,
	.ta		= 20,
};

static struct davinci_nand_pdata da850_evm_nandflash_data = {
	.core_chipsel	= 1,
	.parts		= da850_evm_nandflash_partition,
	.nr_parts	= ARRAY_SIZE(da850_evm_nandflash_partition),
	.ecc_mode	= NAND_ECC_HW,
	.ecc_bits	= 4,
	.bbt_options	= NAND_BBT_USE_FLASH,
	.timing		= &da850_evm_nandflash_timing,
};

static struct resource da850_evm_nandflash_resource[] = {
	{
		.start	= DA8XX_AEMIF_CS3_BASE,
		.end	= DA8XX_AEMIF_CS3_BASE + SZ_512K + 2 * SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= DA8XX_AEMIF_CTL_BASE,
		.end	= DA8XX_AEMIF_CTL_BASE + SZ_32K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource da850_evm_aemif_resource[] = {
	{
		.start	= DA8XX_AEMIF_CTL_BASE,
		.end	= DA8XX_AEMIF_CTL_BASE + SZ_32K,
		.flags	= IORESOURCE_MEM,
	}
};

static struct aemif_abus_data da850_evm_aemif_abus_data[] = {
	{
		.cs	= 3,
	}
};

static struct platform_device da850_evm_aemif_devices[] = {
	{
		.name		= "davinci_nand",
		.id		= 1,
		.dev		= {
			.platform_data	= &da850_evm_nandflash_data,
		},
		.num_resources	= ARRAY_SIZE(da850_evm_nandflash_resource),
		.resource	= da850_evm_nandflash_resource,
	},
	{
		.name		= "physmap-flash",
		.id		= 0,
		.dev		= {
			.platform_data  = &da850_evm_norflash_data,
		},
		.num_resources	= 1,
		.resource	= da850_evm_norflash_resource,
	}
};

static struct aemif_platform_data da850_evm_aemif_pdata = {
	.cs_offset = 2,
	.abus_data = da850_evm_aemif_abus_data,
	.num_abus_data = ARRAY_SIZE(da850_evm_aemif_abus_data),
	.sub_devices = da850_evm_aemif_devices,
	.num_sub_devices = ARRAY_SIZE(da850_evm_aemif_devices),
};

static struct platform_device da850_evm_aemif_device = {
	.name		= "ti-aemif",
	.id		= -1,
	.dev = {
		.platform_data	= &da850_evm_aemif_pdata,
	},
	.resource	= da850_evm_aemif_resource,
	.num_resources	= ARRAY_SIZE(da850_evm_aemif_resource),
};

static const short da850_evm_nand_pins[] = {
	DA850_EMA_D_0, DA850_EMA_D_1, DA850_EMA_D_2, DA850_EMA_D_3,
	DA850_EMA_D_4, DA850_EMA_D_5, DA850_EMA_D_6, DA850_EMA_D_7,
	DA850_EMA_A_1, DA850_EMA_A_2, DA850_NEMA_CS_3, DA850_NEMA_CS_4,
	DA850_NEMA_WE, DA850_NEMA_OE,
	-1
};

static const short da850_evm_nor_pins[] = {
	DA850_EMA_BA_1, DA850_EMA_CLK, DA850_EMA_WAIT_1, DA850_NEMA_CS_2,
	DA850_NEMA_WE, DA850_NEMA_OE, DA850_EMA_D_0, DA850_EMA_D_1,
	DA850_EMA_D_2, DA850_EMA_D_3, DA850_EMA_D_4, DA850_EMA_D_5,
	DA850_EMA_D_6, DA850_EMA_D_7, DA850_EMA_D_8, DA850_EMA_D_9,
	DA850_EMA_D_10, DA850_EMA_D_11, DA850_EMA_D_12, DA850_EMA_D_13,
	DA850_EMA_D_14, DA850_EMA_D_15, DA850_EMA_A_0, DA850_EMA_A_1,
	DA850_EMA_A_2, DA850_EMA_A_3, DA850_EMA_A_4, DA850_EMA_A_5,
	DA850_EMA_A_6, DA850_EMA_A_7, DA850_EMA_A_8, DA850_EMA_A_9,
	DA850_EMA_A_10, DA850_EMA_A_11, DA850_EMA_A_12, DA850_EMA_A_13,
	DA850_EMA_A_14, DA850_EMA_A_15, DA850_EMA_A_16, DA850_EMA_A_17,
	DA850_EMA_A_18, DA850_EMA_A_19, DA850_EMA_A_20, DA850_EMA_A_21,
	DA850_EMA_A_22, DA850_EMA_A_23,
	-1
};

#define HAS_MMC		IS_ENABLED(CONFIG_MMC_DAVINCI)

static inline void da850_evm_setup_nor_nand(void)
{
	int ret = 0;

	if (!HAS_MMC) {
		ret = davinci_cfg_reg_list(da850_evm_nand_pins);
		if (ret)
			pr_warn("%s: NAND mux setup failed: %d\n",
				__func__, ret);

		ret = davinci_cfg_reg_list(da850_evm_nor_pins);
		if (ret)
			pr_warn("%s: NOR mux setup failed: %d\n",
				__func__, ret);

		ret = platform_device_register(&da850_evm_aemif_device);
		if (ret)
			pr_warn("%s: registering aemif failed: %d\n",
				__func__, ret);
	}
}

#ifdef CONFIG_DA850_UI_RMII
static inline void da850_evm_setup_emac_rmii(int rmii_sel)
{
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	soc_info->emac_pdata->rmii_en = 1;
	gpio_set_value_cansleep(rmii_sel, 0);
}
#else
static inline void da850_evm_setup_emac_rmii(int rmii_sel) { }
#endif


#define DA850_KEYS_DEBOUNCE_MS	10
/*
 * At 200ms polling interval it is possible to miss an
 * event by tapping very lightly on the push button but most
 * pushes do result in an event; longer intervals require the
 * user to hold the button whereas shorter intervals require
 * more CPU time for polling.
 */
#define DA850_GPIO_KEYS_POLL_MS	200

enum da850_evm_ui_exp_pins {
	DA850_EVM_UI_EXP_SEL_C = 5,
	DA850_EVM_UI_EXP_SEL_B,
	DA850_EVM_UI_EXP_SEL_A,
	DA850_EVM_UI_EXP_PB8,
	DA850_EVM_UI_EXP_PB7,
	DA850_EVM_UI_EXP_PB6,
	DA850_EVM_UI_EXP_PB5,
	DA850_EVM_UI_EXP_PB4,
	DA850_EVM_UI_EXP_PB3,
	DA850_EVM_UI_EXP_PB2,
	DA850_EVM_UI_EXP_PB1,
};

static const char * const da850_evm_ui_exp[] = {
	[DA850_EVM_UI_EXP_SEL_C]        = "sel_c",
	[DA850_EVM_UI_EXP_SEL_B]        = "sel_b",
	[DA850_EVM_UI_EXP_SEL_A]        = "sel_a",
	[DA850_EVM_UI_EXP_PB8]          = "pb8",
	[DA850_EVM_UI_EXP_PB7]          = "pb7",
	[DA850_EVM_UI_EXP_PB6]          = "pb6",
	[DA850_EVM_UI_EXP_PB5]          = "pb5",
	[DA850_EVM_UI_EXP_PB4]          = "pb4",
	[DA850_EVM_UI_EXP_PB3]          = "pb3",
	[DA850_EVM_UI_EXP_PB2]          = "pb2",
	[DA850_EVM_UI_EXP_PB1]          = "pb1",
};

#define DA850_N_UI_PB		8

static struct gpio_keys_button da850_evm_ui_keys[] = {
	[0 ... DA850_N_UI_PB - 1] = {
		.type			= EV_KEY,
		.active_low		= 1,
		.wakeup			= 0,
		.debounce_interval	= DA850_KEYS_DEBOUNCE_MS,
		.code			= -1, /* assigned at runtime */
		.gpio			= -1, /* assigned at runtime */
		.desc			= NULL, /* assigned at runtime */
	},
};

static struct gpio_keys_platform_data da850_evm_ui_keys_pdata = {
	.buttons = da850_evm_ui_keys,
	.nbuttons = ARRAY_SIZE(da850_evm_ui_keys),
	.poll_interval = DA850_GPIO_KEYS_POLL_MS,
};

static struct platform_device da850_evm_ui_keys_device = {
	.name = "gpio-keys-polled",
	.id = 0,
	.dev = {
		.platform_data = &da850_evm_ui_keys_pdata
	},
};

static void da850_evm_ui_keys_init(unsigned gpio)
{
	int i;
	struct gpio_keys_button *button;

	for (i = 0; i < DA850_N_UI_PB; i++) {
		button = &da850_evm_ui_keys[i];
		button->code = KEY_F8 - i;
		button->desc = da850_evm_ui_exp[DA850_EVM_UI_EXP_PB8 + i];
		button->gpio = gpio + DA850_EVM_UI_EXP_PB8 + i;
	}
}

#ifdef CONFIG_DA850_UI_SD_VIDEO_PORT
static inline void da850_evm_setup_video_port(int video_sel)
{
	gpio_set_value_cansleep(video_sel, 0);
}
#else
static inline void da850_evm_setup_video_port(int video_sel) { }
#endif

static int da850_evm_ui_expander_setup(struct i2c_client *client, unsigned gpio,
						unsigned ngpio, void *c)
{
	int sel_a, sel_b, sel_c, ret;

	sel_a = gpio + DA850_EVM_UI_EXP_SEL_A;
	sel_b = gpio + DA850_EVM_UI_EXP_SEL_B;
	sel_c = gpio + DA850_EVM_UI_EXP_SEL_C;

	ret = gpio_request(sel_a, da850_evm_ui_exp[DA850_EVM_UI_EXP_SEL_A]);
	if (ret) {
		pr_warn("Cannot open UI expander pin %d\n", sel_a);
		goto exp_setup_sela_fail;
	}

	ret = gpio_request(sel_b, da850_evm_ui_exp[DA850_EVM_UI_EXP_SEL_B]);
	if (ret) {
		pr_warn("Cannot open UI expander pin %d\n", sel_b);
		goto exp_setup_selb_fail;
	}

	ret = gpio_request(sel_c, da850_evm_ui_exp[DA850_EVM_UI_EXP_SEL_C]);
	if (ret) {
		pr_warn("Cannot open UI expander pin %d\n", sel_c);
		goto exp_setup_selc_fail;
	}

	/* deselect all functionalities */
	gpio_direction_output(sel_a, 1);
	gpio_direction_output(sel_b, 1);
	gpio_direction_output(sel_c, 1);

	da850_evm_ui_keys_init(gpio);
	ret = platform_device_register(&da850_evm_ui_keys_device);
	if (ret) {
		pr_warn("Could not register UI GPIO expander push-buttons");
		goto exp_setup_keys_fail;
	}

	pr_info("DA850/OMAP-L138 EVM UI card detected\n");

	da850_evm_setup_nor_nand();

	da850_evm_setup_emac_rmii(sel_a);

	da850_evm_setup_video_port(sel_c);

	return 0;

exp_setup_keys_fail:
	gpio_free(sel_c);
exp_setup_selc_fail:
	gpio_free(sel_b);
exp_setup_selb_fail:
	gpio_free(sel_a);
exp_setup_sela_fail:
	return ret;
}

static int da850_evm_ui_expander_teardown(struct i2c_client *client,
					unsigned gpio, unsigned ngpio, void *c)
{
	platform_device_unregister(&da850_evm_ui_keys_device);

	/* deselect all functionalities */
	gpio_set_value_cansleep(gpio + DA850_EVM_UI_EXP_SEL_C, 1);
	gpio_set_value_cansleep(gpio + DA850_EVM_UI_EXP_SEL_B, 1);
	gpio_set_value_cansleep(gpio + DA850_EVM_UI_EXP_SEL_A, 1);

	gpio_free(gpio + DA850_EVM_UI_EXP_SEL_C);
	gpio_free(gpio + DA850_EVM_UI_EXP_SEL_B);
	gpio_free(gpio + DA850_EVM_UI_EXP_SEL_A);

	return 0;
}

/* assign the baseboard expander's GPIOs after the UI board's */
#define DA850_UI_EXPANDER_N_GPIOS ARRAY_SIZE(da850_evm_ui_exp)
#define DA850_BB_EXPANDER_GPIO_BASE (DAVINCI_N_GPIO + DA850_UI_EXPANDER_N_GPIOS)

enum da850_evm_bb_exp_pins {
	DA850_EVM_BB_EXP_DEEP_SLEEP_EN = 0,
	DA850_EVM_BB_EXP_SW_RST,
	DA850_EVM_BB_EXP_TP_23,
	DA850_EVM_BB_EXP_TP_22,
	DA850_EVM_BB_EXP_TP_21,
	DA850_EVM_BB_EXP_USER_PB1,
	DA850_EVM_BB_EXP_USER_LED2,
	DA850_EVM_BB_EXP_USER_LED1,
	DA850_EVM_BB_EXP_USER_SW1,
	DA850_EVM_BB_EXP_USER_SW2,
	DA850_EVM_BB_EXP_USER_SW3,
	DA850_EVM_BB_EXP_USER_SW4,
	DA850_EVM_BB_EXP_USER_SW5,
	DA850_EVM_BB_EXP_USER_SW6,
	DA850_EVM_BB_EXP_USER_SW7,
	DA850_EVM_BB_EXP_USER_SW8
};

static const char * const da850_evm_bb_exp[] = {
	[DA850_EVM_BB_EXP_DEEP_SLEEP_EN]	= "deep_sleep_en",
	[DA850_EVM_BB_EXP_SW_RST]		= "sw_rst",
	[DA850_EVM_BB_EXP_TP_23]		= "tp_23",
	[DA850_EVM_BB_EXP_TP_22]		= "tp_22",
	[DA850_EVM_BB_EXP_TP_21]		= "tp_21",
	[DA850_EVM_BB_EXP_USER_PB1]		= "user_pb1",
	[DA850_EVM_BB_EXP_USER_LED2]		= "user_led2",
	[DA850_EVM_BB_EXP_USER_LED1]		= "user_led1",
	[DA850_EVM_BB_EXP_USER_SW1]		= "user_sw1",
	[DA850_EVM_BB_EXP_USER_SW2]		= "user_sw2",
	[DA850_EVM_BB_EXP_USER_SW3]		= "user_sw3",
	[DA850_EVM_BB_EXP_USER_SW4]		= "user_sw4",
	[DA850_EVM_BB_EXP_USER_SW5]		= "user_sw5",
	[DA850_EVM_BB_EXP_USER_SW6]		= "user_sw6",
	[DA850_EVM_BB_EXP_USER_SW7]		= "user_sw7",
	[DA850_EVM_BB_EXP_USER_SW8]		= "user_sw8",
};

#define DA850_N_BB_USER_SW	8

static struct gpio_keys_button da850_evm_bb_keys[] = {
	[0] = {
		.type			= EV_KEY,
		.active_low		= 1,
		.wakeup			= 0,
		.debounce_interval	= DA850_KEYS_DEBOUNCE_MS,
		.code			= KEY_PROG1,
		.desc			= NULL, /* assigned at runtime */
		.gpio			= -1, /* assigned at runtime */
	},
	[1 ... DA850_N_BB_USER_SW] = {
		.type			= EV_SW,
		.active_low		= 1,
		.wakeup			= 0,
		.debounce_interval	= DA850_KEYS_DEBOUNCE_MS,
		.code			= -1, /* assigned at runtime */
		.desc			= NULL, /* assigned at runtime */
		.gpio			= -1, /* assigned at runtime */
	},
};

static struct gpio_keys_platform_data da850_evm_bb_keys_pdata = {
	.buttons = da850_evm_bb_keys,
	.nbuttons = ARRAY_SIZE(da850_evm_bb_keys),
	.poll_interval = DA850_GPIO_KEYS_POLL_MS,
};

static struct platform_device da850_evm_bb_keys_device = {
	.name = "gpio-keys-polled",
	.id = 1,
	.dev = {
		.platform_data = &da850_evm_bb_keys_pdata
	},
};

static void da850_evm_bb_keys_init(unsigned gpio)
{
	int i;
	struct gpio_keys_button *button;

	button = &da850_evm_bb_keys[0];
	button->desc = da850_evm_bb_exp[DA850_EVM_BB_EXP_USER_PB1];
	button->gpio = gpio + DA850_EVM_BB_EXP_USER_PB1;

	for (i = 0; i < DA850_N_BB_USER_SW; i++) {
		button = &da850_evm_bb_keys[i + 1];
		button->code = SW_LID + i;
		button->desc = da850_evm_bb_exp[DA850_EVM_BB_EXP_USER_SW1 + i];
		button->gpio = gpio + DA850_EVM_BB_EXP_USER_SW1 + i;
	}
}

#define DA850_N_BB_USER_LED	2

static struct gpio_led da850_evm_bb_leds[] = {
	[0 ... DA850_N_BB_USER_LED - 1] = {
		.active_low = 1,
		.gpio = -1, /* assigned at runtime */
		.name = NULL, /* assigned at runtime */
	},
};

static struct gpio_led_platform_data da850_evm_bb_leds_pdata = {
	.leds = da850_evm_bb_leds,
	.num_leds = ARRAY_SIZE(da850_evm_bb_leds),
};

static struct platform_device da850_evm_bb_leds_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev = {
		.platform_data = &da850_evm_bb_leds_pdata
	}
};

static void da850_evm_bb_leds_init(unsigned gpio)
{
	int i;
	struct gpio_led *led;

	for (i = 0; i < DA850_N_BB_USER_LED; i++) {
		led = &da850_evm_bb_leds[i];

		led->gpio = gpio + DA850_EVM_BB_EXP_USER_LED2 + i;
		led->name =
			da850_evm_bb_exp[DA850_EVM_BB_EXP_USER_LED2 + i];
	}
}

static int da850_evm_bb_expander_setup(struct i2c_client *client,
						unsigned gpio, unsigned ngpio,
						void *c)
{
	int ret;

	/*
	 * Register the switches and pushbutton on the baseboard as a gpio-keys
	 * device.
	 */
	da850_evm_bb_keys_init(gpio);
	ret = platform_device_register(&da850_evm_bb_keys_device);
	if (ret) {
		pr_warn("Could not register baseboard GPIO expander keys");
		goto io_exp_setup_sw_fail;
	}

	da850_evm_bb_leds_init(gpio);
	ret = platform_device_register(&da850_evm_bb_leds_device);
	if (ret) {
		pr_warn("Could not register baseboard GPIO expander LEDs");
		goto io_exp_setup_leds_fail;
	}

	return 0;

io_exp_setup_leds_fail:
	platform_device_unregister(&da850_evm_bb_keys_device);
io_exp_setup_sw_fail:
	return ret;
}

static int da850_evm_bb_expander_teardown(struct i2c_client *client,
					unsigned gpio, unsigned ngpio, void *c)
{
	platform_device_unregister(&da850_evm_bb_leds_device);
	platform_device_unregister(&da850_evm_bb_keys_device);

	return 0;
}

static struct pca953x_platform_data da850_evm_ui_expander_info = {
	.gpio_base	= DAVINCI_N_GPIO,
	.setup		= da850_evm_ui_expander_setup,
	.teardown	= da850_evm_ui_expander_teardown,
	.names		= da850_evm_ui_exp,
};

static struct pca953x_platform_data da850_evm_bb_expander_info = {
	.gpio_base	= DA850_BB_EXPANDER_GPIO_BASE,
	.setup		= da850_evm_bb_expander_setup,
	.teardown	= da850_evm_bb_expander_teardown,
	.names		= da850_evm_bb_exp,
};

static struct i2c_board_info __initdata da850_evm_i2c_devices[] = {
	{
		I2C_BOARD_INFO("tlv320aic3x", 0x18),
	},
	{
		I2C_BOARD_INFO("tca6416", 0x20),
		.platform_data = &da850_evm_ui_expander_info,
	},
	{
		I2C_BOARD_INFO("tca6416", 0x21),
		.platform_data = &da850_evm_bb_expander_info,
	},
};

static struct davinci_i2c_platform_data da850_evm_i2c_0_pdata = {
	.bus_freq	= 100,	/* kHz */
	.bus_delay	= 0,	/* usec */
};

/* davinci da850 evm audio machine driver */
static u8 da850_iis_serializer_direction[] = {
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	TX_MODE,
	RX_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
};

static struct snd_platform_data da850_evm_snd_data = {
	.tx_dma_offset		= 0x2000,
	.rx_dma_offset		= 0x2000,
	.op_mode		= DAVINCI_MCASP_IIS_MODE,
	.num_serializer		= ARRAY_SIZE(da850_iis_serializer_direction),
	.tdm_slots		= 2,
	.serial_dir		= da850_iis_serializer_direction,
	.asp_chan_q		= EVENTQ_0,
	.ram_chan_q		= EVENTQ_1,
	.version		= MCASP_VERSION_2,
	.txnumevt		= 1,
	.rxnumevt		= 1,
	.sram_size_playback	= SZ_8K,
	.sram_size_capture	= SZ_8K,
};

static const short da850_evm_mcasp_pins[] __initconst = {
	DA850_AHCLKX, DA850_ACLKX, DA850_AFSX,
	DA850_AHCLKR, DA850_ACLKR, DA850_AFSR, DA850_AMUTE,
	DA850_AXR_11, DA850_AXR_12,
	-1
};

#define DA850_MMCSD_CD_PIN		GPIO_TO_PIN(4, 0)
#define DA850_MMCSD_WP_PIN		GPIO_TO_PIN(4, 1)

static struct gpiod_lookup_table mmc_gpios_table = {
	.dev_id = "da830-mmc.0",
	.table = {
		/* gpio chip 2 contains gpio range 64-95 */
		GPIO_LOOKUP("davinci_gpio", DA850_MMCSD_CD_PIN, "cd",
			    GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("davinci_gpio", DA850_MMCSD_WP_PIN, "wp",
			    GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct davinci_mmc_config da850_mmc_config = {
	.wires		= 4,
	.max_freq	= 50000000,
	.caps		= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
};

static const short da850_evm_mmcsd0_pins[] __initconst = {
	DA850_MMCSD0_DAT_0, DA850_MMCSD0_DAT_1, DA850_MMCSD0_DAT_2,
	DA850_MMCSD0_DAT_3, DA850_MMCSD0_CLK, DA850_MMCSD0_CMD,
	DA850_GPIO4_0, DA850_GPIO4_1,
	-1
};

static void da850_panel_power_ctrl(int val)
{
	/* lcd backlight */
	gpio_set_value(DA850_LCD_BL_PIN, val);

	/* lcd power */
	gpio_set_value(DA850_LCD_PWR_PIN, val);
}

static int da850_lcd_hw_init(void)
{
	int status;

	status = gpio_request(DA850_LCD_BL_PIN, "lcd bl");
	if (status < 0)
		return status;

	status = gpio_request(DA850_LCD_PWR_PIN, "lcd pwr");
	if (status < 0) {
		gpio_free(DA850_LCD_BL_PIN);
		return status;
	}

	gpio_direction_output(DA850_LCD_BL_PIN, 0);
	gpio_direction_output(DA850_LCD_PWR_PIN, 0);

	/* Switch off panel power and backlight */
	da850_panel_power_ctrl(0);

	/* Switch on panel power and backlight */
	da850_panel_power_ctrl(1);

	return 0;
}

/* Fixed regulator support */
static struct regulator_consumer_supply fixed_supplies[] = {
	/* Baseboard 3.3V: 5V -> TPS73701DCQ -> 3.3V */
	REGULATOR_SUPPLY("AVDD", "1-0018"),
	REGULATOR_SUPPLY("DRVDD", "1-0018"),

	/* Baseboard 1.8V: 5V -> TPS73701DCQ -> 1.8V */
	REGULATOR_SUPPLY("DVDD", "1-0018"),

	/* UI card 3.3V: 5V -> TPS73701DCQ -> 3.3V */
	REGULATOR_SUPPLY("vcc", "1-0020"),
};

/* TPS65070 voltage regulator support */

/* 3.3V */
static struct regulator_consumer_supply tps65070_dcdc1_consumers[] = {
	{
		.supply = "usb0_vdda33",
	},
	{
		.supply = "usb1_vdda33",
	},
};

/* 3.3V or 1.8V */
static struct regulator_consumer_supply tps65070_dcdc2_consumers[] = {
	{
		.supply = "dvdd3318_a",
	},
	{
		.supply = "dvdd3318_b",
	},
	{
		.supply = "dvdd3318_c",
	},
	REGULATOR_SUPPLY("IOVDD", "1-0018"),
};

/* 1.2V */
static struct regulator_consumer_supply tps65070_dcdc3_consumers[] = {
	{
		.supply = "cvdd",
	},
};

/* 1.8V LDO */
static struct regulator_consumer_supply tps65070_ldo1_consumers[] = {
	{
		.supply = "sata_vddr",
	},
	{
		.supply = "usb0_vdda18",
	},
	{
		.supply = "usb1_vdda18",
	},
	{
		.supply = "ddr_dvdd18",
	},
};

/* 1.2V LDO */
static struct regulator_consumer_supply tps65070_ldo2_consumers[] = {
	{
		.supply = "sata_vdd",
	},
	{
		.supply = "pll0_vdda",
	},
	{
		.supply = "pll1_vdda",
	},
	{
		.supply = "usbs_cvdd",
	},
	{
		.supply = "vddarnwa1",
	},
};

/* We take advantage of the fact that both defdcdc{2,3} are tied high */
static struct tps6507x_reg_platform_data tps6507x_platform_data = {
	.defdcdc_default = true,
};

static struct regulator_init_data tps65070_regulator_data[] = {
	/* dcdc1 */
	{
		.constraints = {
			.min_uV = 3150000,
			.max_uV = 3450000,
			.valid_ops_mask = (REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_STATUS),
			.boot_on = 1,
		},
		.num_consumer_supplies = ARRAY_SIZE(tps65070_dcdc1_consumers),
		.consumer_supplies = tps65070_dcdc1_consumers,
	},

	/* dcdc2 */
	{
		.constraints = {
			.min_uV = 1710000,
			.max_uV = 3450000,
			.valid_ops_mask = (REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_STATUS),
			.boot_on = 1,
			.always_on = 1,
		},
		.num_consumer_supplies = ARRAY_SIZE(tps65070_dcdc2_consumers),
		.consumer_supplies = tps65070_dcdc2_consumers,
		.driver_data = &tps6507x_platform_data,
	},

	/* dcdc3 */
	{
		.constraints = {
			.min_uV = 950000,
			.max_uV = 1350000,
			.valid_ops_mask = (REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_STATUS),
			.boot_on = 1,
		},
		.num_consumer_supplies = ARRAY_SIZE(tps65070_dcdc3_consumers),
		.consumer_supplies = tps65070_dcdc3_consumers,
		.driver_data = &tps6507x_platform_data,
	},

	/* ldo1 */
	{
		.constraints = {
			.min_uV = 1710000,
			.max_uV = 1890000,
			.valid_ops_mask = (REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_STATUS),
			.boot_on = 1,
		},
		.num_consumer_supplies = ARRAY_SIZE(tps65070_ldo1_consumers),
		.consumer_supplies = tps65070_ldo1_consumers,
	},

	/* ldo2 */
	{
		.constraints = {
			.min_uV = 1140000,
			.max_uV = 1320000,
			.valid_ops_mask = (REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_STATUS),
			.boot_on = 1,
		},
		.num_consumer_supplies = ARRAY_SIZE(tps65070_ldo2_consumers),
		.consumer_supplies = tps65070_ldo2_consumers,
	},
};

static struct touchscreen_init_data tps6507x_touchscreen_data = {
	.poll_period =  30,	/* ms between touch samples */
	.min_pressure = 0x30,	/* minimum pressure to trigger touch */
	.vendor = 0,		/* /sys/class/input/input?/id/vendor */
	.product = 65070,	/* /sys/class/input/input?/id/product */
	.version = 0x100,	/* /sys/class/input/input?/id/version */
};

static struct tps6507x_board tps_board = {
	.tps6507x_pmic_init_data = &tps65070_regulator_data[0],
	.tps6507x_ts_init_data = &tps6507x_touchscreen_data,
};

static struct i2c_board_info __initdata da850_evm_tps65070_info[] = {
	{
		I2C_BOARD_INFO("tps6507x", 0x48),
		.platform_data = &tps_board,
	},
};

static int __init pmic_tps65070_init(void)
{
	return i2c_register_board_info(1, da850_evm_tps65070_info,
					ARRAY_SIZE(da850_evm_tps65070_info));
}

static const short da850_evm_lcdc_pins[] = {
	DA850_GPIO2_8, DA850_GPIO2_15,
	-1
};

static const short da850_evm_mii_pins[] = {
	DA850_MII_TXEN, DA850_MII_TXCLK, DA850_MII_COL, DA850_MII_TXD_3,
	DA850_MII_TXD_2, DA850_MII_TXD_1, DA850_MII_TXD_0, DA850_MII_RXER,
	DA850_MII_CRS, DA850_MII_RXCLK, DA850_MII_RXDV, DA850_MII_RXD_3,
	DA850_MII_RXD_2, DA850_MII_RXD_1, DA850_MII_RXD_0, DA850_MDIO_CLK,
	DA850_MDIO_D,
	-1
};

static const short da850_evm_rmii_pins[] = {
	DA850_RMII_TXD_0, DA850_RMII_TXD_1, DA850_RMII_TXEN,
	DA850_RMII_CRS_DV, DA850_RMII_RXD_0, DA850_RMII_RXD_1,
	DA850_RMII_RXER, DA850_RMII_MHZ_50_CLK, DA850_MDIO_CLK,
	DA850_MDIO_D,
	-1
};

static struct gpiod_hog da850_evm_emac_gpio_hogs[] = {
	{
		.chip_label	= "davinci_gpio",
		.chip_hwnum	= DA850_MII_MDIO_CLKEN_PIN,
		.line_name	= "mdio_clk_en",
		.lflags		= 0,
		/* dflags set in da850_evm_config_emac() */
	},
	{ }
};

static int __init da850_evm_config_emac(void)
{
	void __iomem *cfg_chip3_base;
	int ret;
	u32 val;
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	u8 rmii_en = soc_info->emac_pdata->rmii_en;

	if (!machine_is_davinci_da850_evm())
		return 0;

	cfg_chip3_base = DA8XX_SYSCFG0_VIRT(DA8XX_CFGCHIP3_REG);

	val = __raw_readl(cfg_chip3_base);

	if (rmii_en) {
		val |= BIT(8);
		ret = davinci_cfg_reg_list(da850_evm_rmii_pins);
		pr_info("EMAC: RMII PHY configured, MII PHY will not be"
							" functional\n");
	} else {
		val &= ~BIT(8);
		ret = davinci_cfg_reg_list(da850_evm_mii_pins);
		pr_info("EMAC: MII PHY configured, RMII PHY will not be"
							" functional\n");
	}

	if (ret)
		pr_warn("%s: CPGMAC/RMII mux setup failed: %d\n",
			__func__, ret);

	/* configure the CFGCHIP3 register for RMII or MII */
	__raw_writel(val, cfg_chip3_base);

	ret = davinci_cfg_reg(DA850_GPIO2_6);
	if (ret)
		pr_warn("%s:GPIO(2,6) mux setup failed\n", __func__);

	da850_evm_emac_gpio_hogs[0].dflags = rmii_en ? GPIOD_OUT_HIGH
						     : GPIOD_OUT_LOW;
	gpiod_add_hogs(da850_evm_emac_gpio_hogs);

	soc_info->emac_pdata->phy_id = DA850_EVM_PHY_ID;

	ret = da8xx_register_emac();
	if (ret)
		pr_warn("%s: EMAC registration failed: %d\n", __func__, ret);

	return 0;
}
device_initcall(da850_evm_config_emac);

/*
 * The following EDMA channels/slots are not being used by drivers (for
 * example: Timer, GPIO, UART events etc) on da850/omap-l138 EVM, hence
 * they are being reserved for codecs on the DSP side.
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

#ifdef CONFIG_CPU_FREQ
static __init int da850_evm_init_cpufreq(void)
{
	switch (system_rev & 0xF) {
	case 3:
		da850_max_speed = 456000;
		break;
	case 2:
		da850_max_speed = 408000;
		break;
	case 1:
		da850_max_speed = 372000;
		break;
	}

	return da850_register_cpufreq("pll0_sysclk3");
}
#else
static __init int da850_evm_init_cpufreq(void) { return 0; }
#endif

#if defined(CONFIG_DA850_UI_SD_VIDEO_PORT)

#define TVP5147_CH0		"tvp514x-0"
#define TVP5147_CH1		"tvp514x-1"

/* VPIF capture configuration */
static struct tvp514x_platform_data tvp5146_pdata = {
		.clk_polarity = 0,
		.hs_polarity  = 1,
		.vs_polarity  = 1,
};

#define TVP514X_STD_ALL (V4L2_STD_NTSC | V4L2_STD_PAL)

static struct vpif_input da850_ch0_inputs[] = {
	{
		.input = {
			.index = 0,
			.name  = "Composite",
			.type  = V4L2_INPUT_TYPE_CAMERA,
			.capabilities = V4L2_IN_CAP_STD,
			.std   = TVP514X_STD_ALL,
		},
		.input_route = INPUT_CVBS_VI2B,
		.output_route = OUTPUT_10BIT_422_EMBEDDED_SYNC,
		.subdev_name = TVP5147_CH0,
	},
};

static struct vpif_input da850_ch1_inputs[] = {
	{
		.input = {
			.index = 0,
			.name  = "S-Video",
			.type  = V4L2_INPUT_TYPE_CAMERA,
			.capabilities = V4L2_IN_CAP_STD,
			.std   = TVP514X_STD_ALL,
		},
		.input_route = INPUT_SVIDEO_VI2C_VI1C,
		.output_route = OUTPUT_10BIT_422_EMBEDDED_SYNC,
		.subdev_name = TVP5147_CH1,
	},
};

static struct vpif_subdev_info da850_vpif_capture_sdev_info[] = {
	{
		.name = TVP5147_CH0,
		.board_info = {
			I2C_BOARD_INFO("tvp5146", 0x5d),
			.platform_data = &tvp5146_pdata,
		},
	},
	{
		.name = TVP5147_CH1,
		.board_info = {
			I2C_BOARD_INFO("tvp5146", 0x5c),
			.platform_data = &tvp5146_pdata,
		},
	},
};

static struct vpif_capture_config da850_vpif_capture_config = {
	.subdev_info = da850_vpif_capture_sdev_info,
	.subdev_count = ARRAY_SIZE(da850_vpif_capture_sdev_info),
	.i2c_adapter_id = 1,
	.chan_config[0] = {
		.inputs = da850_ch0_inputs,
		.input_count = ARRAY_SIZE(da850_ch0_inputs),
		.vpif_if = {
			.if_type = VPIF_IF_BT656,
			.hd_pol  = 1,
			.vd_pol  = 1,
			.fid_pol = 0,
		},
	},
	.chan_config[1] = {
		.inputs = da850_ch1_inputs,
		.input_count = ARRAY_SIZE(da850_ch1_inputs),
		.vpif_if = {
			.if_type = VPIF_IF_BT656,
			.hd_pol  = 1,
			.vd_pol  = 1,
			.fid_pol = 0,
		},
	},
	.card_name = "DA850/OMAP-L138 Video Capture",
};

/* VPIF display configuration */

static struct adv7343_platform_data adv7343_pdata = {
	.mode_config = {
		.dac = { 1, 1, 1 },
	},
	.sd_config = {
		.sd_dac_out = { 1 },
	},
};

static struct vpif_subdev_info da850_vpif_subdev[] = {
	{
		.name = "adv7343",
		.board_info = {
			I2C_BOARD_INFO("adv7343", 0x2a),
			.platform_data = &adv7343_pdata,
		},
	},
};

static const struct vpif_output da850_ch0_outputs[] = {
	{
		.output = {
			.index = 0,
			.name = "Composite",
			.type = V4L2_OUTPUT_TYPE_ANALOG,
			.capabilities = V4L2_OUT_CAP_STD,
			.std = V4L2_STD_ALL,
		},
		.subdev_name = "adv7343",
		.output_route = ADV7343_COMPOSITE_ID,
	},
	{
		.output = {
			.index = 1,
			.name = "S-Video",
			.type = V4L2_OUTPUT_TYPE_ANALOG,
			.capabilities = V4L2_OUT_CAP_STD,
			.std = V4L2_STD_ALL,
		},
		.subdev_name = "adv7343",
		.output_route = ADV7343_SVIDEO_ID,
	},
};

static struct vpif_display_config da850_vpif_display_config = {
	.subdevinfo   = da850_vpif_subdev,
	.subdev_count = ARRAY_SIZE(da850_vpif_subdev),
	.chan_config[0] = {
		.outputs = da850_ch0_outputs,
		.output_count = ARRAY_SIZE(da850_ch0_outputs),
	},
	.card_name    = "DA850/OMAP-L138 Video Display",
	.i2c_adapter_id = 1,
};

static __init void da850_vpif_init(void)
{
	int ret;

	ret = da850_register_vpif();
	if (ret)
		pr_warn("da850_evm_init: VPIF setup failed: %d\n", ret);

	ret = davinci_cfg_reg_list(da850_vpif_capture_pins);
	if (ret)
		pr_warn("da850_evm_init: VPIF capture mux setup failed: %d\n",
			ret);

	ret = da850_register_vpif_capture(&da850_vpif_capture_config);
	if (ret)
		pr_warn("da850_evm_init: VPIF capture setup failed: %d\n", ret);

	ret = davinci_cfg_reg_list(da850_vpif_display_pins);
	if (ret)
		pr_warn("da850_evm_init: VPIF display mux setup failed: %d\n",
			ret);

	ret = da850_register_vpif_display(&da850_vpif_display_config);
	if (ret)
		pr_warn("da850_evm_init: VPIF display setup failed: %d\n", ret);
}

#else
static __init void da850_vpif_init(void) {}
#endif

#define DA850EVM_SATA_REFCLKPN_RATE	(100 * 1000 * 1000)

static __init void da850_evm_init(void)
{
	int ret;

	da850_register_clocks();

	ret = da850_register_gpio();
	if (ret)
		pr_warn("%s: GPIO init failed: %d\n", __func__, ret);

	regulator_register_fixed(0, fixed_supplies, ARRAY_SIZE(fixed_supplies));

	ret = pmic_tps65070_init();
	if (ret)
		pr_warn("%s: TPS65070 PMIC init failed: %d\n", __func__, ret);

	ret = da850_register_edma(da850_edma_rsv);
	if (ret)
		pr_warn("%s: EDMA registration failed: %d\n", __func__, ret);

	ret = davinci_cfg_reg_list(da850_i2c0_pins);
	if (ret)
		pr_warn("%s: I2C0 mux setup failed: %d\n", __func__, ret);

	ret = da8xx_register_i2c(0, &da850_evm_i2c_0_pdata);
	if (ret)
		pr_warn("%s: I2C0 registration failed: %d\n", __func__, ret);


	ret = da8xx_register_watchdog();
	if (ret)
		pr_warn("%s: watchdog registration failed: %d\n",
			__func__, ret);

	if (HAS_MMC) {
		ret = davinci_cfg_reg_list(da850_evm_mmcsd0_pins);
		if (ret)
			pr_warn("%s: MMCSD0 mux setup failed: %d\n",
				__func__, ret);

		gpiod_add_lookup_table(&mmc_gpios_table);

		ret = da8xx_register_mmcsd0(&da850_mmc_config);
		if (ret)
			pr_warn("%s: MMCSD0 registration failed: %d\n",
				__func__, ret);
	}

	davinci_serial_init(da8xx_serial_device);

	nvmem_add_cell_table(&da850evm_nvmem_cell_table);
	nvmem_add_cell_lookups(&da850evm_nvmem_cell_lookup, 1);

	i2c_register_board_info(1, da850_evm_i2c_devices,
			ARRAY_SIZE(da850_evm_i2c_devices));

	/*
	 * shut down uart 0 and 1; they are not used on the board and
	 * accessing them causes endless "too much work in irq53" messages
	 * with arago fs
	 */
	__raw_writel(0, IO_ADDRESS(DA8XX_UART1_BASE) + 0x30);
	__raw_writel(0, IO_ADDRESS(DA8XX_UART0_BASE) + 0x30);

	ret = davinci_cfg_reg_list(da850_evm_mcasp_pins);
	if (ret)
		pr_warn("%s: McASP mux setup failed: %d\n", __func__, ret);

	da850_evm_snd_data.sram_pool = sram_get_gen_pool();
	da8xx_register_mcasp(0, &da850_evm_snd_data);

	ret = davinci_cfg_reg_list(da850_lcdcntl_pins);
	if (ret)
		pr_warn("%s: LCDC mux setup failed: %d\n", __func__, ret);

	ret = da8xx_register_uio_pruss();
	if (ret)
		pr_warn("da850_evm_init: pruss initialization failed: %d\n",
				ret);

	/* Handle board specific muxing for LCD here */
	ret = davinci_cfg_reg_list(da850_evm_lcdc_pins);
	if (ret)
		pr_warn("%s: EVM specific LCD mux setup failed: %d\n",
			__func__, ret);

	ret = da850_lcd_hw_init();
	if (ret)
		pr_warn("%s: LCD initialization failed: %d\n", __func__, ret);

	sharp_lk043t1dg01_pdata.panel_power_ctrl = da850_panel_power_ctrl,
	ret = da8xx_register_lcdc(&sharp_lk043t1dg01_pdata);
	if (ret)
		pr_warn("%s: LCDC registration failed: %d\n", __func__, ret);

	ret = da8xx_register_rtc();
	if (ret)
		pr_warn("%s: RTC setup failed: %d\n", __func__, ret);

	ret = da850_evm_init_cpufreq();
	if (ret)
		pr_warn("%s: cpufreq registration failed: %d\n", __func__, ret);

	ret = da8xx_register_cpuidle();
	if (ret)
		pr_warn("%s: cpuidle registration failed: %d\n", __func__, ret);

	davinci_pm_init();
	da850_vpif_init();

	ret = spi_register_board_info(da850evm_spi_info,
				      ARRAY_SIZE(da850evm_spi_info));
	if (ret)
		pr_warn("%s: spi info registration failed: %d\n", __func__,
			ret);

	ret = da8xx_register_spi_bus(1, ARRAY_SIZE(da850evm_spi_info));
	if (ret)
		pr_warn("%s: SPI 1 registration failed: %d\n", __func__, ret);

	ret = da850_register_sata(DA850EVM_SATA_REFCLKPN_RATE);
	if (ret)
		pr_warn("%s: SATA registration failed: %d\n", __func__, ret);

	ret = da8xx_register_rproc();
	if (ret)
		pr_warn("%s: dsp/rproc registration failed: %d\n",
			__func__, ret);
}

#ifdef CONFIG_SERIAL_8250_CONSOLE
static int __init da850_evm_console_init(void)
{
	if (!machine_is_davinci_da850_evm())
		return 0;

	return add_preferred_console("ttyS", 2, "115200");
}
console_initcall(da850_evm_console_init);
#endif

static void __init da850_evm_map_io(void)
{
	da850_init();
}

MACHINE_START(DAVINCI_DA850_EVM, "DaVinci DA850/OMAP-L138/AM18x EVM")
	.atag_offset	= 0x100,
	.map_io		= da850_evm_map_io,
	.init_irq	= da850_init_irq,
	.init_time	= da850_init_time,
	.init_machine	= da850_evm_init,
	.init_late	= davinci_init_late,
	.dma_zone_size	= SZ_128M,
	.reserve	= da8xx_rproc_reserve_cma,
MACHINE_END
