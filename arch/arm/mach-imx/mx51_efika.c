/*
 * based on code from the following
 * Copyright 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2009-2010 Pegatron Corporation. All Rights Reserved.
 * Copyright 2009-2010 Genesi USA, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <linux/mfd/mc13892.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-mx51.h>

#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <mach/ulpi.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "devices-imx51.h"
#include "efika.h"
#include "cpu_op-mx51.h"

#define MX51_USB_CTRL_1_OFFSET          0x10
#define MX51_USB_CTRL_UH1_EXT_CLK_EN    (1 << 25)
#define	MX51_USB_PLL_DIV_19_2_MHZ	0x01

#define EFIKAMX_USB_HUB_RESET	IMX_GPIO_NR(1, 5)
#define EFIKAMX_USBH1_STP	IMX_GPIO_NR(1, 27)

#define EFIKAMX_SPI_CS0		IMX_GPIO_NR(4, 24)
#define EFIKAMX_SPI_CS1		IMX_GPIO_NR(4, 25)

#define EFIKAMX_PMIC		IMX_GPIO_NR(1, 6)

static iomux_v3_cfg_t mx51efika_pads[] = {
	/* UART1 */
	MX51_PAD_UART1_RXD__UART1_RXD,
	MX51_PAD_UART1_TXD__UART1_TXD,
	MX51_PAD_UART1_RTS__UART1_RTS,
	MX51_PAD_UART1_CTS__UART1_CTS,

	/* SD 1 */
	MX51_PAD_SD1_CMD__SD1_CMD,
	MX51_PAD_SD1_CLK__SD1_CLK,
	MX51_PAD_SD1_DATA0__SD1_DATA0,
	MX51_PAD_SD1_DATA1__SD1_DATA1,
	MX51_PAD_SD1_DATA2__SD1_DATA2,
	MX51_PAD_SD1_DATA3__SD1_DATA3,

	/* SD 2 */
	MX51_PAD_SD2_CMD__SD2_CMD,
	MX51_PAD_SD2_CLK__SD2_CLK,
	MX51_PAD_SD2_DATA0__SD2_DATA0,
	MX51_PAD_SD2_DATA1__SD2_DATA1,
	MX51_PAD_SD2_DATA2__SD2_DATA2,
	MX51_PAD_SD2_DATA3__SD2_DATA3,

	/* SD/MMC WP/CD */
	MX51_PAD_GPIO1_0__SD1_CD,
	MX51_PAD_GPIO1_1__SD1_WP,
	MX51_PAD_GPIO1_7__SD2_WP,
	MX51_PAD_GPIO1_8__SD2_CD,

	/* spi */
	MX51_PAD_CSPI1_MOSI__ECSPI1_MOSI,
	MX51_PAD_CSPI1_MISO__ECSPI1_MISO,
	MX51_PAD_CSPI1_SS0__GPIO4_24,
	MX51_PAD_CSPI1_SS1__GPIO4_25,
	MX51_PAD_CSPI1_RDY__ECSPI1_RDY,
	MX51_PAD_CSPI1_SCLK__ECSPI1_SCLK,
	MX51_PAD_GPIO1_6__GPIO1_6,

	/* USB HOST1 */
	MX51_PAD_USBH1_CLK__USBH1_CLK,
	MX51_PAD_USBH1_DIR__USBH1_DIR,
	MX51_PAD_USBH1_NXT__USBH1_NXT,
	MX51_PAD_USBH1_DATA0__USBH1_DATA0,
	MX51_PAD_USBH1_DATA1__USBH1_DATA1,
	MX51_PAD_USBH1_DATA2__USBH1_DATA2,
	MX51_PAD_USBH1_DATA3__USBH1_DATA3,
	MX51_PAD_USBH1_DATA4__USBH1_DATA4,
	MX51_PAD_USBH1_DATA5__USBH1_DATA5,
	MX51_PAD_USBH1_DATA6__USBH1_DATA6,
	MX51_PAD_USBH1_DATA7__USBH1_DATA7,

	/* USB HUB RESET */
	MX51_PAD_GPIO1_5__GPIO1_5,

	/* WLAN */
	MX51_PAD_EIM_A22__GPIO2_16,
	MX51_PAD_EIM_A16__GPIO2_10,

	/* USB PHY RESET */
	MX51_PAD_EIM_D27__GPIO2_9,
};

/* Serial ports */
static const struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

/* This function is board specific as the bit mask for the plldiv will also
 * be different for other Freescale SoCs, thus a common bitmask is not
 * possible and cannot get place in /plat-mxc/ehci.c.
 */
static int initialize_otg_port(struct platform_device *pdev)
{
	u32 v;
	void __iomem *usb_base;
	void __iomem *usbother_base;
	usb_base = ioremap(MX51_USB_OTG_BASE_ADDR, SZ_4K);
	if (!usb_base)
		return -ENOMEM;
	usbother_base = (void __iomem *)(usb_base + MX5_USBOTHER_REGS_OFFSET);

	/* Set the PHY clock to 19.2MHz */
	v = __raw_readl(usbother_base + MXC_USB_PHY_CTR_FUNC2_OFFSET);
	v &= ~MX5_USB_UTMI_PHYCTRL1_PLLDIV_MASK;
	v |= MX51_USB_PLL_DIV_19_2_MHZ;
	__raw_writel(v, usbother_base + MXC_USB_PHY_CTR_FUNC2_OFFSET);
	iounmap(usb_base);

	mdelay(10);

	return mx51_initialize_usb_hw(pdev->id, MXC_EHCI_INTERNAL_PHY);
}

static const struct mxc_usbh_platform_data dr_utmi_config __initconst = {
	.init   = initialize_otg_port,
	.portsc = MXC_EHCI_UTMI_16BIT,
};

static int initialize_usbh1_port(struct platform_device *pdev)
{
	iomux_v3_cfg_t usbh1stp = MX51_PAD_USBH1_STP__USBH1_STP;
	iomux_v3_cfg_t usbh1gpio = MX51_PAD_USBH1_STP__GPIO1_27;
	u32 v;
	void __iomem *usb_base;
	void __iomem *socregs_base;

	mxc_iomux_v3_setup_pad(usbh1gpio);
	gpio_request(EFIKAMX_USBH1_STP, "usbh1_stp");
	gpio_direction_output(EFIKAMX_USBH1_STP, 0);
	msleep(1);
	gpio_set_value(EFIKAMX_USBH1_STP, 1);
	msleep(1);

	usb_base = ioremap(MX51_USB_OTG_BASE_ADDR, SZ_4K);
	socregs_base = (void __iomem *)(usb_base + MX5_USBOTHER_REGS_OFFSET);

	/* The clock for the USBH1 ULPI port will come externally */
	/* from the PHY. */
	v = __raw_readl(socregs_base + MX51_USB_CTRL_1_OFFSET);
	__raw_writel(v | MX51_USB_CTRL_UH1_EXT_CLK_EN,
			socregs_base + MX51_USB_CTRL_1_OFFSET);

	iounmap(usb_base);

	gpio_free(EFIKAMX_USBH1_STP);
	mxc_iomux_v3_setup_pad(usbh1stp);

	mdelay(10);

	return mx51_initialize_usb_hw(pdev->id, MXC_EHCI_ITC_NO_THRESHOLD);
}

static struct mxc_usbh_platform_data usbh1_config __initdata = {
	.init   = initialize_usbh1_port,
	.portsc = MXC_EHCI_MODE_ULPI,
};

static void mx51_efika_hubreset(void)
{
	gpio_request(EFIKAMX_USB_HUB_RESET, "usb_hub_rst");
	gpio_direction_output(EFIKAMX_USB_HUB_RESET, 1);
	msleep(1);
	gpio_set_value(EFIKAMX_USB_HUB_RESET, 0);
	msleep(1);
	gpio_set_value(EFIKAMX_USB_HUB_RESET, 1);
}

static void __init mx51_efika_usb(void)
{
	mx51_efika_hubreset();

	/* pulling it low, means no USB at all... */
	gpio_request(EFIKA_USB_PHY_RESET, "usb_phy_reset");
	gpio_direction_output(EFIKA_USB_PHY_RESET, 0);
	msleep(1);
	gpio_set_value(EFIKA_USB_PHY_RESET, 1);

	usbh1_config.otg = imx_otg_ulpi_create(ULPI_OTG_DRVVBUS |
			ULPI_OTG_DRVVBUS_EXT | ULPI_OTG_EXTVBUSIND);

	imx51_add_mxc_ehci_otg(&dr_utmi_config);
	if (usbh1_config.otg)
		imx51_add_mxc_ehci_hs(1, &usbh1_config);
}

static struct mtd_partition mx51_efika_spi_nor_partitions[] = {
	{
	 .name = "u-boot",
	 .offset = 0,
	 .size = SZ_256K,
	},
	{
	  .name = "config",
	  .offset = MTDPART_OFS_APPEND,
	  .size = SZ_64K,
	},
};

static struct flash_platform_data mx51_efika_spi_flash_data = {
	.name		= "spi_flash",
	.parts		= mx51_efika_spi_nor_partitions,
	.nr_parts	= ARRAY_SIZE(mx51_efika_spi_nor_partitions),
	.type		= "sst25vf032b",
};

static struct regulator_consumer_supply sw1_consumers[] = {
	{
		.supply = "cpu_vcc",
	}
};

static struct regulator_consumer_supply vdig_consumers[] = {
	/* sgtl5000 */
	REGULATOR_SUPPLY("VDDA", "1-000a"),
	REGULATOR_SUPPLY("VDDD", "1-000a"),
};

static struct regulator_consumer_supply vvideo_consumers[] = {
	/* sgtl5000 */
	REGULATOR_SUPPLY("VDDIO", "1-000a"),
};

static struct regulator_consumer_supply vsd_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "sdhci-esdhc-imx51.0"),
	REGULATOR_SUPPLY("vmmc", "sdhci-esdhc-imx51.1"),
};

static struct regulator_consumer_supply pwgt1_consumer[] = {
	{
		.supply = "pwgt1",
	}
};

static struct regulator_consumer_supply pwgt2_consumer[] = {
	{
		.supply = "pwgt2",
	}
};

static struct regulator_consumer_supply coincell_consumer[] = {
	{
		.supply = "coincell",
	}
};

static struct regulator_init_data sw1_init = {
	.constraints = {
		.name = "SW1",
		.min_uV = 600000,
		.max_uV = 1375000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		.valid_modes_mask = 0,
		.always_on = 1,
		.boot_on = 1,
		.state_mem = {
			.uV = 850000,
			.mode = REGULATOR_MODE_NORMAL,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(sw1_consumers),
	.consumer_supplies = sw1_consumers,
};

static struct regulator_init_data sw2_init = {
	.constraints = {
		.name = "SW2",
		.min_uV = 900000,
		.max_uV = 1850000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.boot_on = 1,
		.state_mem = {
			.uV = 950000,
			.mode = REGULATOR_MODE_NORMAL,
			.enabled = 1,
		},
	}
};

static struct regulator_init_data sw3_init = {
	.constraints = {
		.name = "SW3",
		.min_uV = 1100000,
		.max_uV = 1850000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.boot_on = 1,
	}
};

static struct regulator_init_data sw4_init = {
	.constraints = {
		.name = "SW4",
		.min_uV = 1100000,
		.max_uV = 1850000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		.always_on = 1,
		.boot_on = 1,
	}
};

static struct regulator_init_data viohi_init = {
	.constraints = {
		.name = "VIOHI",
		.boot_on = 1,
		.always_on = 1,
	}
};

static struct regulator_init_data vusb_init = {
	.constraints = {
		.name = "VUSB",
		.boot_on = 1,
		.always_on = 1,
	}
};

static struct regulator_init_data swbst_init = {
	.constraints = {
		.name = "SWBST",
	}
};

static struct regulator_init_data vdig_init = {
	.constraints = {
		.name = "VDIG",
		.min_uV = 1050000,
		.max_uV = 1800000,
		.valid_ops_mask =
			REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.boot_on = 1,
		.always_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(vdig_consumers),
	.consumer_supplies = vdig_consumers,
};

static struct regulator_init_data vpll_init = {
	.constraints = {
		.name = "VPLL",
		.min_uV = 1050000,
		.max_uV = 1800000,
		.valid_ops_mask =
			REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.boot_on = 1,
		.always_on = 1,
	}
};

static struct regulator_init_data vusb2_init = {
	.constraints = {
		.name = "VUSB2",
		.min_uV = 2400000,
		.max_uV = 2775000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		.boot_on = 1,
		.always_on = 1,
	}
};

static struct regulator_init_data vvideo_init = {
	.constraints = {
		.name = "VVIDEO",
		.min_uV = 2775000,
		.max_uV = 2775000,
		.valid_ops_mask =
			REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.boot_on = 1,
		.apply_uV = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(vvideo_consumers),
	.consumer_supplies = vvideo_consumers,
};

static struct regulator_init_data vaudio_init = {
	.constraints = {
		.name = "VAUDIO",
		.min_uV = 2300000,
		.max_uV = 3000000,
		.valid_ops_mask =
			REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.boot_on = 1,
	}
};

static struct regulator_init_data vsd_init = {
	.constraints = {
		.name = "VSD",
		.min_uV = 1800000,
		.max_uV = 3150000,
		.valid_ops_mask =
			REGULATOR_CHANGE_VOLTAGE,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(vsd_consumers),
	.consumer_supplies = vsd_consumers,
};

static struct regulator_init_data vcam_init = {
	.constraints = {
		.name = "VCAM",
		.min_uV = 2500000,
		.max_uV = 3000000,
		.valid_ops_mask =
			REGULATOR_CHANGE_VOLTAGE |
			REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL,
		.boot_on = 1,
	}
};

static struct regulator_init_data vgen1_init = {
	.constraints = {
		.name = "VGEN1",
		.min_uV = 1200000,
		.max_uV = 3150000,
		.valid_ops_mask =
			REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.boot_on = 1,
		.always_on = 1,
	}
};

static struct regulator_init_data vgen2_init = {
	.constraints = {
		.name = "VGEN2",
		.min_uV = 1200000,
		.max_uV = 3150000,
		.valid_ops_mask =
			REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.boot_on = 1,
		.always_on = 1,
	}
};

static struct regulator_init_data vgen3_init = {
	.constraints = {
		.name = "VGEN3",
		.min_uV = 1800000,
		.max_uV = 2900000,
		.valid_ops_mask =
			REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.boot_on = 1,
		.always_on = 1,
	}
};

static struct regulator_init_data gpo1_init = {
	.constraints = {
		.name = "GPO1",
	}
};

static struct regulator_init_data gpo2_init = {
	.constraints = {
		.name = "GPO2",
	}
};

static struct regulator_init_data gpo3_init = {
	.constraints = {
		.name = "GPO3",
	}
};

static struct regulator_init_data gpo4_init = {
	.constraints = {
		.name = "GPO4",
	}
};

static struct regulator_init_data pwgt1_init = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.boot_on        = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(pwgt1_consumer),
	.consumer_supplies = pwgt1_consumer,
};

static struct regulator_init_data pwgt2_init = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.boot_on        = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(pwgt2_consumer),
	.consumer_supplies = pwgt2_consumer,
};

static struct regulator_init_data vcoincell_init = {
	.constraints = {
		.name = "COINCELL",
		.min_uV = 3000000,
		.max_uV = 3000000,
		.valid_ops_mask =
			REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(coincell_consumer),
	.consumer_supplies = coincell_consumer,
};

static struct mc13xxx_regulator_init_data mx51_efika_regulators[] = {
	{ .id = MC13892_SW1,		.init_data =  &sw1_init },
	{ .id = MC13892_SW2,		.init_data =  &sw2_init },
	{ .id = MC13892_SW3,		.init_data =  &sw3_init },
	{ .id = MC13892_SW4,		.init_data =  &sw4_init },
	{ .id = MC13892_SWBST,		.init_data =  &swbst_init },
	{ .id = MC13892_VIOHI,		.init_data =  &viohi_init },
	{ .id = MC13892_VPLL,		.init_data =  &vpll_init },
	{ .id = MC13892_VDIG,		.init_data =  &vdig_init },
	{ .id = MC13892_VSD,		.init_data =  &vsd_init },
	{ .id = MC13892_VUSB2,		.init_data =  &vusb2_init },
	{ .id = MC13892_VVIDEO,		.init_data =  &vvideo_init },
	{ .id = MC13892_VAUDIO,		.init_data =  &vaudio_init },
	{ .id = MC13892_VCAM,		.init_data =  &vcam_init },
	{ .id = MC13892_VGEN1,		.init_data =  &vgen1_init },
	{ .id = MC13892_VGEN2,		.init_data =  &vgen2_init },
	{ .id = MC13892_VGEN3,		.init_data =  &vgen3_init },
	{ .id = MC13892_VUSB,		.init_data =  &vusb_init },
	{ .id = MC13892_GPO1,		.init_data =  &gpo1_init },
	{ .id = MC13892_GPO2,		.init_data =  &gpo2_init },
	{ .id = MC13892_GPO3,		.init_data =  &gpo3_init },
	{ .id = MC13892_GPO4,		.init_data =  &gpo4_init },
	{ .id = MC13892_PWGT1SPI,	.init_data = &pwgt1_init },
	{ .id = MC13892_PWGT2SPI,	.init_data = &pwgt2_init },
	{ .id = MC13892_VCOINCELL,	.init_data = &vcoincell_init },
};

static struct mc13xxx_platform_data mx51_efika_mc13892_data = {
	.flags = MC13XXX_USE_RTC,
	.regulators = {
		.num_regulators = ARRAY_SIZE(mx51_efika_regulators),
		.regulators = mx51_efika_regulators,
	},
};

static struct spi_board_info mx51_efika_spi_board_info[] __initdata = {
	{
		.modalias = "m25p80",
		.max_speed_hz = 25000000,
		.bus_num = 0,
		.chip_select = 1,
		.platform_data = &mx51_efika_spi_flash_data,
		.irq = -1,
	},
	{
		.modalias = "mc13892",
		.max_speed_hz = 1000000,
		.bus_num = 0,
		.chip_select = 0,
		.platform_data = &mx51_efika_mc13892_data,
		.irq = IMX_GPIO_TO_IRQ(EFIKAMX_PMIC),
	},
};

static int mx51_efika_spi_cs[] = {
	EFIKAMX_SPI_CS0,
	EFIKAMX_SPI_CS1,
};

static const struct spi_imx_master mx51_efika_spi_pdata __initconst = {
	.chipselect     = mx51_efika_spi_cs,
	.num_chipselect = ARRAY_SIZE(mx51_efika_spi_cs),
};

void __init efika_board_common_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(mx51efika_pads,
					ARRAY_SIZE(mx51efika_pads));
	imx51_add_imx_uart(0, &uart_pdata);
	mx51_efika_usb();

	/* FIXME: comes from original code. check this. */
	if (mx51_revision() < IMX_CHIP_REVISION_2_0)
		sw2_init.constraints.state_mem.uV = 1100000;
	else if (mx51_revision() == IMX_CHIP_REVISION_2_0) {
		sw2_init.constraints.state_mem.uV = 1250000;
		sw1_init.constraints.state_mem.uV = 1000000;
	}
	if (machine_is_mx51_efikasb())
		vgen1_init.constraints.max_uV = 1200000;

	gpio_request(EFIKAMX_PMIC, "pmic irq");
	gpio_direction_input(EFIKAMX_PMIC);
	spi_register_board_info(mx51_efika_spi_board_info,
		ARRAY_SIZE(mx51_efika_spi_board_info));
	imx51_add_ecspi(0, &mx51_efika_spi_pdata);

	imx51_add_pata_imx();

#if defined(CONFIG_CPU_FREQ_IMX)
	get_cpu_op = mx51_get_cpu_op;
#endif
}
