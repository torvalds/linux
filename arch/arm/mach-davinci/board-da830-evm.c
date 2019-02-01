/*
 * TI DA830/OMAP L137 EVM board
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 * Derived from: arch/arm/mach-davinci/board-dm644x-evm.c
 *
 * 2007, 2009 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/gpio/machine.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/platform_data/pcf857x.h>
#include <linux/platform_data/at24.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/platform_data/gpio-davinci.h>
#include <linux/platform_data/mtd-davinci.h>
#include <linux/platform_data/mtd-davinci-aemif.h>
#include <linux/platform_data/spi-davinci.h>
#include <linux/platform_data/usb-davinci.h>
#include <linux/platform_data/ti-aemif.h>
#include <linux/regulator/machine.h>
#include <linux/nvmem-provider.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/common.h>
#include "cp_intc.h"
#include <mach/mux.h>
#include <mach/da8xx.h>

#define DA830_EVM_PHY_ID		""
/*
 * USB1 VBUS is controlled by GPIO1[15], over-current is reported on GPIO2[4].
 */
#define ON_BD_USB_DRV	GPIO_TO_PIN(1, 15)
#define ON_BD_USB_OVC	GPIO_TO_PIN(2, 4)

static const short da830_evm_usb11_pins[] = {
	DA830_GPIO1_15, DA830_GPIO2_4,
	-1
};

static da8xx_ocic_handler_t da830_evm_usb_ocic_handler;

static int da830_evm_usb_set_power(unsigned port, int on)
{
	gpio_set_value(ON_BD_USB_DRV, on);
	return 0;
}

static int da830_evm_usb_get_power(unsigned port)
{
	return gpio_get_value(ON_BD_USB_DRV);
}

static int da830_evm_usb_get_oci(unsigned port)
{
	return !gpio_get_value(ON_BD_USB_OVC);
}

static irqreturn_t da830_evm_usb_ocic_irq(int, void *);

static int da830_evm_usb_ocic_notify(da8xx_ocic_handler_t handler)
{
	int irq 	= gpio_to_irq(ON_BD_USB_OVC);
	int error	= 0;

	if (handler != NULL) {
		da830_evm_usb_ocic_handler = handler;

		error = request_irq(irq, da830_evm_usb_ocic_irq,
				    IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				    "OHCI over-current indicator", NULL);
		if (error)
			pr_err("%s: could not request IRQ to watch over-current indicator changes\n",
			       __func__);
	} else
		free_irq(irq, NULL);

	return error;
}

static struct da8xx_ohci_root_hub da830_evm_usb11_pdata = {
	.set_power	= da830_evm_usb_set_power,
	.get_power	= da830_evm_usb_get_power,
	.get_oci	= da830_evm_usb_get_oci,
	.ocic_notify	= da830_evm_usb_ocic_notify,

	/* TPS2065 switch @ 5V */
	.potpgt		= (3 + 1) / 2,	/* 3 ms max */
};

static irqreturn_t da830_evm_usb_ocic_irq(int irq, void *dev_id)
{
	da830_evm_usb_ocic_handler(&da830_evm_usb11_pdata, 1);
	return IRQ_HANDLED;
}

static __init void da830_evm_usb_init(void)
{
	int ret;

	ret = da8xx_register_usb_phy_clocks();
	if (ret)
		pr_warn("%s: USB PHY CLK registration failed: %d\n",
			__func__, ret);

	ret = da8xx_register_usb_phy();
	if (ret)
		pr_warn("%s: USB PHY registration failed: %d\n",
			__func__, ret);

	ret = davinci_cfg_reg(DA830_USB0_DRVVBUS);
	if (ret)
		pr_warn("%s: USB 2.0 PinMux setup failed: %d\n", __func__, ret);
	else {
		/*
		 * TPS2065 switch @ 5V supplies 1 A (sustains 1.5 A),
		 * with the power on to power good time of 3 ms.
		 */
		ret = da8xx_register_usb20(1000, 3);
		if (ret)
			pr_warn("%s: USB 2.0 registration failed: %d\n",
				__func__, ret);
	}

	ret = davinci_cfg_reg_list(da830_evm_usb11_pins);
	if (ret) {
		pr_warn("%s: USB 1.1 PinMux setup failed: %d\n", __func__, ret);
		return;
	}

	ret = gpio_request(ON_BD_USB_DRV, "ON_BD_USB_DRV");
	if (ret) {
		pr_err("%s: failed to request GPIO for USB 1.1 port power control: %d\n",
		       __func__, ret);
		return;
	}
	gpio_direction_output(ON_BD_USB_DRV, 0);

	ret = gpio_request(ON_BD_USB_OVC, "ON_BD_USB_OVC");
	if (ret) {
		pr_err("%s: failed to request GPIO for USB 1.1 port over-current indicator: %d\n",
		       __func__, ret);
		return;
	}
	gpio_direction_input(ON_BD_USB_OVC);

	ret = da8xx_register_usb11(&da830_evm_usb11_pdata);
	if (ret)
		pr_warn("%s: USB 1.1 registration failed: %d\n", __func__, ret);
}

static const short da830_evm_mcasp1_pins[] = {
	DA830_AHCLKX1, DA830_ACLKX1, DA830_AFSX1, DA830_AHCLKR1, DA830_AFSR1,
	DA830_AMUTE1, DA830_AXR1_0, DA830_AXR1_1, DA830_AXR1_2, DA830_AXR1_5,
	DA830_ACLKR1, DA830_AXR1_6, DA830_AXR1_7, DA830_AXR1_8, DA830_AXR1_10,
	DA830_AXR1_11,
	-1
};

static u8 da830_iis_serializer_direction[] = {
	RX_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	TX_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
};

static struct snd_platform_data da830_evm_snd_data = {
	.tx_dma_offset  = 0x2000,
	.rx_dma_offset  = 0x2000,
	.op_mode        = DAVINCI_MCASP_IIS_MODE,
	.num_serializer = ARRAY_SIZE(da830_iis_serializer_direction),
	.tdm_slots      = 2,
	.serial_dir     = da830_iis_serializer_direction,
	.asp_chan_q     = EVENTQ_0,
	.version	= MCASP_VERSION_2,
	.txnumevt	= 1,
	.rxnumevt	= 1,
};

/*
 * GPIO2[1] is used as MMC_SD_WP and GPIO2[2] as MMC_SD_INS.
 */
static const short da830_evm_mmc_sd_pins[] = {
	DA830_MMCSD_DAT_0, DA830_MMCSD_DAT_1, DA830_MMCSD_DAT_2,
	DA830_MMCSD_DAT_3, DA830_MMCSD_DAT_4, DA830_MMCSD_DAT_5,
	DA830_MMCSD_DAT_6, DA830_MMCSD_DAT_7, DA830_MMCSD_CLK,
	DA830_MMCSD_CMD,   DA830_GPIO2_1,     DA830_GPIO2_2,
	-1
};

#define DA830_MMCSD_WP_PIN		GPIO_TO_PIN(2, 1)
#define DA830_MMCSD_CD_PIN		GPIO_TO_PIN(2, 2)

static struct gpiod_lookup_table mmc_gpios_table = {
	.dev_id = "da830-mmc.0",
	.table = {
		/* gpio chip 1 contains gpio range 32-63 */
		GPIO_LOOKUP("davinci_gpio", DA830_MMCSD_CD_PIN, "cd",
			    GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("davinci_gpio", DA830_MMCSD_WP_PIN, "wp",
			    GPIO_ACTIVE_LOW),
	},
};

static struct davinci_mmc_config da830_evm_mmc_config = {
	.wires			= 8,
	.max_freq		= 50000000,
	.caps			= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
};

static inline void da830_evm_init_mmc(void)
{
	int ret;

	ret = davinci_cfg_reg_list(da830_evm_mmc_sd_pins);
	if (ret) {
		pr_warn("%s: mmc/sd mux setup failed: %d\n", __func__, ret);
		return;
	}

	gpiod_add_lookup_table(&mmc_gpios_table);

	ret = da8xx_register_mmcsd0(&da830_evm_mmc_config);
	if (ret) {
		pr_warn("%s: mmc/sd registration failed: %d\n", __func__, ret);
		gpiod_remove_lookup_table(&mmc_gpios_table);
	}
}

#define HAS_MMC		IS_ENABLED(CONFIG_MMC_DAVINCI)

#ifdef CONFIG_DA830_UI_NAND
static struct mtd_partition da830_evm_nand_partitions[] = {
	/* bootloader (U-Boot, etc) in first sector */
	[0] = {
		.name		= "bootloader",
		.offset		= 0,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	/* bootloader params in the next sector */
	[1] = {
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	/* kernel */
	[2] = {
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_2M,
		.mask_flags	= 0,
	},
	/* file system */
	[3] = {
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0,
	}
};

/* flash bbt decriptors */
static uint8_t da830_evm_nand_bbt_pattern[] = { 'B', 'b', 't', '0' };
static uint8_t da830_evm_nand_mirror_pattern[] = { '1', 't', 'b', 'B' };

static struct nand_bbt_descr da830_evm_nand_bbt_main_descr = {
	.options	= NAND_BBT_LASTBLOCK | NAND_BBT_CREATE |
			  NAND_BBT_WRITE | NAND_BBT_2BIT |
			  NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs		= 2,
	.len		= 4,
	.veroffs	= 16,
	.maxblocks	= 4,
	.pattern	= da830_evm_nand_bbt_pattern
};

static struct nand_bbt_descr da830_evm_nand_bbt_mirror_descr = {
	.options	= NAND_BBT_LASTBLOCK | NAND_BBT_CREATE |
			  NAND_BBT_WRITE | NAND_BBT_2BIT |
			  NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs		= 2,
	.len		= 4,
	.veroffs	= 16,
	.maxblocks	= 4,
	.pattern	= da830_evm_nand_mirror_pattern
};

static struct davinci_aemif_timing da830_evm_nandflash_timing = {
	.wsetup         = 24,
	.wstrobe        = 21,
	.whold          = 14,
	.rsetup         = 19,
	.rstrobe        = 50,
	.rhold          = 0,
	.ta             = 20,
};

static struct davinci_nand_pdata da830_evm_nand_pdata = {
	.core_chipsel	= 1,
	.parts		= da830_evm_nand_partitions,
	.nr_parts	= ARRAY_SIZE(da830_evm_nand_partitions),
	.ecc_mode	= NAND_ECC_HW,
	.ecc_bits	= 4,
	.bbt_options	= NAND_BBT_USE_FLASH,
	.bbt_td		= &da830_evm_nand_bbt_main_descr,
	.bbt_md		= &da830_evm_nand_bbt_mirror_descr,
	.timing         = &da830_evm_nandflash_timing,
};

static struct resource da830_evm_nand_resources[] = {
	[0] = {		/* First memory resource is NAND I/O window */
		.start	= DA8XX_AEMIF_CS3_BASE,
		.end	= DA8XX_AEMIF_CS3_BASE + PAGE_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {		/* Second memory resource is AEMIF control registers */
		.start	= DA8XX_AEMIF_CTL_BASE,
		.end	= DA8XX_AEMIF_CTL_BASE + SZ_32K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device da830_evm_aemif_devices[] = {
	{
		.name		= "davinci_nand",
		.id		= 1,
		.dev		= {
			.platform_data	= &da830_evm_nand_pdata,
		},
		.num_resources	= ARRAY_SIZE(da830_evm_nand_resources),
		.resource	= da830_evm_nand_resources,
	},
};

static struct resource da830_evm_aemif_resource[] = {
	{
		.start	= DA8XX_AEMIF_CTL_BASE,
		.end	= DA8XX_AEMIF_CTL_BASE + SZ_32K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct aemif_abus_data da830_evm_aemif_abus_data[] = {
	{
		.cs	= 3,
	},
};

static struct aemif_platform_data da830_evm_aemif_pdata = {
	.abus_data		= da830_evm_aemif_abus_data,
	.num_abus_data		= ARRAY_SIZE(da830_evm_aemif_abus_data),
	.sub_devices		= da830_evm_aemif_devices,
	.num_sub_devices	= ARRAY_SIZE(da830_evm_aemif_devices),
	.cs_offset		= 2,
};

static struct platform_device da830_evm_aemif_device = {
	.name		= "ti-aemif",
	.id		= -1,
	.dev = {
		.platform_data = &da830_evm_aemif_pdata,
	},
	.resource	= da830_evm_aemif_resource,
	.num_resources	= ARRAY_SIZE(da830_evm_aemif_resource),
};

/*
 * UI board NAND/NOR flashes only use 8-bit data bus.
 */
static const short da830_evm_emif25_pins[] = {
	DA830_EMA_D_0, DA830_EMA_D_1, DA830_EMA_D_2, DA830_EMA_D_3,
	DA830_EMA_D_4, DA830_EMA_D_5, DA830_EMA_D_6, DA830_EMA_D_7,
	DA830_EMA_A_0, DA830_EMA_A_1, DA830_EMA_A_2, DA830_EMA_A_3,
	DA830_EMA_A_4, DA830_EMA_A_5, DA830_EMA_A_6, DA830_EMA_A_7,
	DA830_EMA_A_8, DA830_EMA_A_9, DA830_EMA_A_10, DA830_EMA_A_11,
	DA830_EMA_A_12, DA830_EMA_BA_0, DA830_EMA_BA_1, DA830_NEMA_WE,
	DA830_NEMA_CS_2, DA830_NEMA_CS_3, DA830_NEMA_OE, DA830_EMA_WAIT_0,
	-1
};

static inline void da830_evm_init_nand(int mux_mode)
{
	int ret;

	if (HAS_MMC) {
		pr_warn("WARNING: both MMC/SD and NAND are enabled, but they share AEMIF pins\n"
			"\tDisable MMC/SD for NAND support\n");
		return;
	}

	ret = davinci_cfg_reg_list(da830_evm_emif25_pins);
	if (ret)
		pr_warn("%s: emif25 mux setup failed: %d\n", __func__, ret);

	ret = platform_device_register(&da830_evm_aemif_device);
	if (ret)
		pr_warn("%s: AEMIF device not registered\n", __func__);

	gpio_direction_output(mux_mode, 1);
}
#else
static inline void da830_evm_init_nand(int mux_mode) { }
#endif

#ifdef CONFIG_DA830_UI_LCD
static inline void da830_evm_init_lcdc(int mux_mode)
{
	int ret;

	ret = davinci_cfg_reg_list(da830_lcdcntl_pins);
	if (ret)
		pr_warn("%s: lcdcntl mux setup failed: %d\n", __func__, ret);

	ret = da8xx_register_lcdc(&sharp_lcd035q3dg01_pdata);
	if (ret)
		pr_warn("%s: lcd setup failed: %d\n", __func__, ret);

	gpio_direction_output(mux_mode, 0);
}
#else
static inline void da830_evm_init_lcdc(int mux_mode) { }
#endif

static struct nvmem_cell_info da830_evm_nvmem_cells[] = {
	{
		.name		= "macaddr",
		.offset		= 0x7f00,
		.bytes		= ETH_ALEN,
	}
};

static struct nvmem_cell_table da830_evm_nvmem_cell_table = {
	.nvmem_name	= "1-00500",
	.cells		= da830_evm_nvmem_cells,
	.ncells		= ARRAY_SIZE(da830_evm_nvmem_cells),
};

static struct nvmem_cell_lookup da830_evm_nvmem_cell_lookup = {
	.nvmem_name	= "1-00500",
	.cell_name	= "macaddr",
	.dev_id		= "davinci_emac.1",
	.con_id		= "mac-address",
};

static struct at24_platform_data da830_evm_i2c_eeprom_info = {
	.byte_len	= SZ_256K / 8,
	.page_size	= 64,
	.flags		= AT24_FLAG_ADDR16,
	.setup		= davinci_get_mac_addr,
	.context	= (void *)0x7f00,
};

static int __init da830_evm_ui_expander_setup(struct i2c_client *client,
		int gpio, unsigned ngpio, void *context)
{
	gpio_request(gpio + 6, "UI MUX_MODE");

	/* Drive mux mode low to match the default without UI card */
	gpio_direction_output(gpio + 6, 0);

	da830_evm_init_lcdc(gpio + 6);

	da830_evm_init_nand(gpio + 6);

	return 0;
}

static int da830_evm_ui_expander_teardown(struct i2c_client *client, int gpio,
		unsigned ngpio, void *context)
{
	gpio_free(gpio + 6);
	return 0;
}

static struct pcf857x_platform_data __initdata da830_evm_ui_expander_info = {
	.gpio_base	= DAVINCI_N_GPIO,
	.setup		= da830_evm_ui_expander_setup,
	.teardown	= da830_evm_ui_expander_teardown,
};

static struct i2c_board_info __initdata da830_evm_i2c_devices[] = {
	{
		I2C_BOARD_INFO("24c256", 0x50),
		.platform_data	= &da830_evm_i2c_eeprom_info,
	},
	{
		I2C_BOARD_INFO("tlv320aic3x", 0x18),
	},
	{
		I2C_BOARD_INFO("pcf8574", 0x3f),
		.platform_data	= &da830_evm_ui_expander_info,
	},
};

static struct davinci_i2c_platform_data da830_evm_i2c_0_pdata = {
	.bus_freq	= 100,	/* kHz */
	.bus_delay	= 0,	/* usec */
};

/*
 * The following EDMA channels/slots are not being used by drivers (for
 * example: Timer, GPIO, UART events etc) on da830/omap-l137 EVM, hence
 * they are being reserved for codecs on the DSP side.
 */
static const s16 da830_dma_rsv_chans[][2] = {
	/* (offset, number) */
	{ 8,  2},
	{12,  2},
	{24,  4},
	{30,  2},
	{-1, -1}
};

static const s16 da830_dma_rsv_slots[][2] = {
	/* (offset, number) */
	{ 8,  2},
	{12,  2},
	{24,  4},
	{30, 26},
	{-1, -1}
};

static struct edma_rsv_info da830_edma_rsv[] = {
	{
		.rsv_chans	= da830_dma_rsv_chans,
		.rsv_slots	= da830_dma_rsv_slots,
	},
};

static struct mtd_partition da830evm_spiflash_part[] = {
	[0] = {
		.name = "DSP-UBL",
		.offset = 0,
		.size = SZ_8K,
		.mask_flags = MTD_WRITEABLE,
	},
	[1] = {
		.name = "ARM-UBL",
		.offset = MTDPART_OFS_APPEND,
		.size = SZ_16K + SZ_8K,
		.mask_flags = MTD_WRITEABLE,
	},
	[2] = {
		.name = "U-Boot",
		.offset = MTDPART_OFS_APPEND,
		.size = SZ_256K - SZ_32K,
		.mask_flags = MTD_WRITEABLE,
	},
	[3] = {
		.name = "U-Boot-Environment",
		.offset = MTDPART_OFS_APPEND,
		.size = SZ_16K,
		.mask_flags = 0,
	},
	[4] = {
		.name = "Kernel",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
		.mask_flags = 0,
	},
};

static struct flash_platform_data da830evm_spiflash_data = {
	.name		= "m25p80",
	.parts		= da830evm_spiflash_part,
	.nr_parts	= ARRAY_SIZE(da830evm_spiflash_part),
	.type		= "w25x32",
};

static struct davinci_spi_config da830evm_spiflash_cfg = {
	.io_type	= SPI_IO_TYPE_DMA,
	.c2tdelay	= 8,
	.t2cdelay	= 8,
};

static struct spi_board_info da830evm_spi_info[] = {
	{
		.modalias		= "m25p80",
		.platform_data		= &da830evm_spiflash_data,
		.controller_data	= &da830evm_spiflash_cfg,
		.mode			= SPI_MODE_0,
		.max_speed_hz		= 30000000,
		.bus_num		= 0,
		.chip_select		= 0,
	},
};

static __init void da830_evm_init(void)
{
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	int ret;

	da830_register_clocks();

	ret = da830_register_gpio();
	if (ret)
		pr_warn("%s: GPIO init failed: %d\n", __func__, ret);

	ret = da830_register_edma(da830_edma_rsv);
	if (ret)
		pr_warn("%s: edma registration failed: %d\n", __func__, ret);

	ret = davinci_cfg_reg_list(da830_i2c0_pins);
	if (ret)
		pr_warn("%s: i2c0 mux setup failed: %d\n", __func__, ret);

	ret = da8xx_register_i2c(0, &da830_evm_i2c_0_pdata);
	if (ret)
		pr_warn("%s: i2c0 registration failed: %d\n", __func__, ret);

	da830_evm_usb_init();

	soc_info->emac_pdata->rmii_en = 1;
	soc_info->emac_pdata->phy_id = DA830_EVM_PHY_ID;

	ret = davinci_cfg_reg_list(da830_cpgmac_pins);
	if (ret)
		pr_warn("%s: cpgmac mux setup failed: %d\n", __func__, ret);

	ret = da8xx_register_emac();
	if (ret)
		pr_warn("%s: emac registration failed: %d\n", __func__, ret);

	ret = da8xx_register_watchdog();
	if (ret)
		pr_warn("%s: watchdog registration failed: %d\n",
			__func__, ret);

	davinci_serial_init(da8xx_serial_device);

	nvmem_add_cell_table(&da830_evm_nvmem_cell_table);
	nvmem_add_cell_lookups(&da830_evm_nvmem_cell_lookup, 1);

	i2c_register_board_info(1, da830_evm_i2c_devices,
			ARRAY_SIZE(da830_evm_i2c_devices));

	ret = davinci_cfg_reg_list(da830_evm_mcasp1_pins);
	if (ret)
		pr_warn("%s: mcasp1 mux setup failed: %d\n", __func__, ret);

	da8xx_register_mcasp(1, &da830_evm_snd_data);

	da830_evm_init_mmc();

	ret = da8xx_register_rtc();
	if (ret)
		pr_warn("%s: rtc setup failed: %d\n", __func__, ret);

	ret = spi_register_board_info(da830evm_spi_info,
				      ARRAY_SIZE(da830evm_spi_info));
	if (ret)
		pr_warn("%s: spi info registration failed: %d\n",
			__func__, ret);

	ret = da8xx_register_spi_bus(0, ARRAY_SIZE(da830evm_spi_info));
	if (ret)
		pr_warn("%s: spi 0 registration failed: %d\n", __func__, ret);

	regulator_has_full_constraints();
}

#ifdef CONFIG_SERIAL_8250_CONSOLE
static int __init da830_evm_console_init(void)
{
	if (!machine_is_davinci_da830_evm())
		return 0;

	return add_preferred_console("ttyS", 2, "115200");
}
console_initcall(da830_evm_console_init);
#endif

static void __init da830_evm_map_io(void)
{
	da830_init();
}

MACHINE_START(DAVINCI_DA830_EVM, "DaVinci DA830/OMAP-L137/AM17x EVM")
	.atag_offset	= 0x100,
	.map_io		= da830_evm_map_io,
	.init_irq	= cp_intc_init,
	.init_time	= da830_init_time,
	.init_machine	= da830_evm_init,
	.init_late	= davinci_init_late,
	.dma_zone_size	= SZ_128M,
MACHINE_END
