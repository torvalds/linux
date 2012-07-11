/*
 * Texas Instruments TNETV107X EVM Board Support
 *
 * Copyright (C) 2010 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/ratelimit.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/spi/spi.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <mach/irqs.h>
#include <mach/edma.h>
#include <mach/mux.h>
#include <mach/cp_intc.h>
#include <mach/tnetv107x.h>

#define EVM_MMC_WP_GPIO		21
#define EVM_MMC_CD_GPIO		24
#define EVM_SPI_CS_GPIO		54

static int initialize_gpio(int gpio, char *desc)
{
	int ret;

	ret = gpio_request(gpio, desc);
	if (ret < 0) {
		pr_err_ratelimited("cannot open %s gpio\n", desc);
		return -ENOSYS;
	}
	gpio_direction_input(gpio);
	return gpio;
}

static int mmc_get_cd(int index)
{
	static int gpio;

	if (!gpio)
		gpio = initialize_gpio(EVM_MMC_CD_GPIO, "mmc card detect");

	if (gpio < 0)
		return gpio;

	return gpio_get_value(gpio) ? 0 : 1;
}

static int mmc_get_ro(int index)
{
	static int gpio;

	if (!gpio)
		gpio = initialize_gpio(EVM_MMC_WP_GPIO, "mmc write protect");

	if (gpio < 0)
		return gpio;

	return gpio_get_value(gpio) ? 1 : 0;
}

static struct davinci_mmc_config mmc_config = {
	.get_cd		= mmc_get_cd,
	.get_ro		= mmc_get_ro,
	.wires		= 4,
	.max_freq	= 50000000,
	.caps		= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
	.version	= MMC_CTLR_VERSION_1,
};

static const short sdio1_pins[] __initdata = {
	TNETV107X_SDIO1_CLK_1,		TNETV107X_SDIO1_CMD_1,
	TNETV107X_SDIO1_DATA0_1,	TNETV107X_SDIO1_DATA1_1,
	TNETV107X_SDIO1_DATA2_1,	TNETV107X_SDIO1_DATA3_1,
	TNETV107X_GPIO21,		TNETV107X_GPIO24,
	-1
};

static const short uart1_pins[] __initdata = {
	TNETV107X_UART1_RD,		TNETV107X_UART1_TD,
	-1
};

static const short ssp_pins[] __initdata = {
	TNETV107X_SSP0_0, TNETV107X_SSP0_1, TNETV107X_SSP0_2,
	TNETV107X_SSP1_0, TNETV107X_SSP1_1, TNETV107X_SSP1_2,
	TNETV107X_SSP1_3, -1
};

static struct mtd_partition nand_partitions[] = {
	/* bootloader (U-Boot, etc) in first 12 sectors */
	{
		.name		= "bootloader",
		.offset		= 0,
		.size		= (12*SZ_128K),
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	/* bootloader params in the next sector */
	{
		.name		= "params",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	/* kernel */
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= SZ_4M,
		.mask_flags	= 0,
	},
	/* file system */
	{
		.name		= "filesystem",
		.offset		= MTDPART_OFS_NXTBLK,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0,
	}
};

static struct davinci_nand_pdata nand_config = {
	.mask_cle	= 0x4000,
	.mask_ale	= 0x2000,
	.parts		= nand_partitions,
	.nr_parts	= ARRAY_SIZE(nand_partitions),
	.ecc_mode	= NAND_ECC_HW,
	.bbt_options	= NAND_BBT_USE_FLASH,
	.ecc_bits	= 1,
};

static struct davinci_uart_config serial_config __initconst = {
	.enabled_uarts	= BIT(1),
};

static const uint32_t keymap[] = {
	KEY(0, 0, KEY_NUMERIC_1),
	KEY(0, 1, KEY_NUMERIC_2),
	KEY(0, 2, KEY_NUMERIC_3),
	KEY(0, 3, KEY_FN_F1),
	KEY(0, 4, KEY_MENU),

	KEY(1, 0, KEY_NUMERIC_4),
	KEY(1, 1, KEY_NUMERIC_5),
	KEY(1, 2, KEY_NUMERIC_6),
	KEY(1, 3, KEY_UP),
	KEY(1, 4, KEY_FN_F2),

	KEY(2, 0, KEY_NUMERIC_7),
	KEY(2, 1, KEY_NUMERIC_8),
	KEY(2, 2, KEY_NUMERIC_9),
	KEY(2, 3, KEY_LEFT),
	KEY(2, 4, KEY_ENTER),

	KEY(3, 0, KEY_NUMERIC_STAR),
	KEY(3, 1, KEY_NUMERIC_0),
	KEY(3, 2, KEY_NUMERIC_POUND),
	KEY(3, 3, KEY_DOWN),
	KEY(3, 4, KEY_RIGHT),

	KEY(4, 0, KEY_FN_F3),
	KEY(4, 1, KEY_FN_F4),
	KEY(4, 2, KEY_MUTE),
	KEY(4, 3, KEY_HOME),
	KEY(4, 4, KEY_BACK),

	KEY(5, 0, KEY_VOLUMEDOWN),
	KEY(5, 1, KEY_VOLUMEUP),
	KEY(5, 2, KEY_F1),
	KEY(5, 3, KEY_F2),
	KEY(5, 4, KEY_F3),
};

static const struct matrix_keymap_data keymap_data = {
	.keymap		= keymap,
	.keymap_size	= ARRAY_SIZE(keymap),
};

static struct matrix_keypad_platform_data keypad_config = {
	.keymap_data	= &keymap_data,
	.num_row_gpios	= 6,
	.num_col_gpios	= 5,
	.debounce_ms	= 0, /* minimum */
	.active_low	= 0, /* pull up realization */
	.no_autorepeat	= 0,
};

static void spi_select_device(int cs)
{
	static int gpio;

	if (!gpio) {
		int ret;
		ret = gpio_request(EVM_SPI_CS_GPIO, "spi chipsel");
		if (ret < 0) {
			pr_err("cannot open spi chipsel gpio\n");
			gpio = -ENOSYS;
			return;
		} else {
			gpio = EVM_SPI_CS_GPIO;
			gpio_direction_output(gpio, 0);
		}
	}

	if (gpio < 0)
		return;

	return gpio_set_value(gpio, cs ? 1 : 0);
}

static struct ti_ssp_spi_data spi_master_data = {
	.num_cs	= 2,
	.select	= spi_select_device,
	.iosel	= SSP_PIN_SEL(0, SSP_CLOCK)	| SSP_PIN_SEL(1, SSP_DATA) |
		  SSP_PIN_SEL(2, SSP_CHIPSEL)	| SSP_PIN_SEL(3, SSP_IN)   |
		  SSP_INPUT_SEL(3),
};

static struct ti_ssp_data ssp_config = {
	.out_clock	= 250 * 1000,
	.dev_data	= {
		[1] = {
			.dev_name = "ti-ssp-spi",
			.pdata = &spi_master_data,
			.pdata_size = sizeof(spi_master_data),
		},
	},
};

static struct tnetv107x_device_info evm_device_info __initconst = {
	.serial_config		= &serial_config,
	.mmc_config[1]		= &mmc_config,	/* controller 1 */
	.nand_config[0]		= &nand_config,	/* chip select 0 */
	.keypad_config		= &keypad_config,
	.ssp_config		= &ssp_config,
};

static struct spi_board_info spi_info[] __initconst = {
};

static __init void tnetv107x_evm_board_init(void)
{
	davinci_cfg_reg_list(sdio1_pins);
	davinci_cfg_reg_list(uart1_pins);
	davinci_cfg_reg_list(ssp_pins);

	tnetv107x_devices_init(&evm_device_info);

	spi_register_board_info(spi_info, ARRAY_SIZE(spi_info));
}

#ifdef CONFIG_SERIAL_8250_CONSOLE
static int __init tnetv107x_evm_console_init(void)
{
	return add_preferred_console("ttyS", 0, "115200");
}
console_initcall(tnetv107x_evm_console_init);
#endif

MACHINE_START(TNETV107X, "TNETV107X EVM")
	.atag_offset	= 0x100,
	.map_io		= tnetv107x_init,
	.init_irq	= cp_intc_init,
	.timer		= &davinci_timer,
	.init_machine	= tnetv107x_evm_board_init,
	.init_late	= davinci_init_late,
	.dma_zone_size	= SZ_128M,
	.restart	= tnetv107x_restart,
MACHINE_END
