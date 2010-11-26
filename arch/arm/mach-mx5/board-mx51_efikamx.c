/*
 * Copyright (C) 2010 Linaro Limited
 *
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
#include <linux/fsl_devices.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-mx51.h>
#include <mach/i2c.h>
#include <mach/mxc_ehci.h>

#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "devices-imx51.h"
#include "devices.h"

#define	MX51_USB_PLL_DIV_24_MHZ	0x01

#define EFIKAMX_PCBID0		IMX_GPIO_NR(3, 16)
#define EFIKAMX_PCBID1		IMX_GPIO_NR(3, 17)
#define EFIKAMX_PCBID2		IMX_GPIO_NR(3, 11)

#define EFIKAMX_BLUE_LED	IMX_GPIO_NR(3, 13)
#define EFIKAMX_GREEN_LED	IMX_GPIO_NR(3, 14)
#define EFIKAMX_RED_LED		IMX_GPIO_NR(3, 15)

#define EFIKAMX_POWER_KEY	IMX_GPIO_NR(2, 31)

#define EFIKAMX_SPI_CS0		IMX_GPIO_NR(4, 24)
#define EFIKAMX_SPI_CS1		IMX_GPIO_NR(4, 25)

/* board 1.1 doesn't have same reset gpio */
#define EFIKAMX_RESET1_1	IMX_GPIO_NR(3, 2)
#define EFIKAMX_RESET		IMX_GPIO_NR(1, 4)

/* the pci ids pin have pull up. they're driven low according to board id */
#define MX51_PAD_PCBID0	IOMUX_PAD(0x518, 0x130, 3, 0x0,   0, PAD_CTL_PUS_100K_UP)
#define MX51_PAD_PCBID1	IOMUX_PAD(0x51C, 0x134, 3, 0x0,   0, PAD_CTL_PUS_100K_UP)
#define MX51_PAD_PCBID2	IOMUX_PAD(0x504, 0x128, 3, 0x0,   0, PAD_CTL_PUS_100K_UP)
#define MX51_PAD_PWRKEY	IOMUX_PAD(0x48c, 0x0f8, 1, 0x0,   0, PAD_CTL_PUS_100K_UP | PAD_CTL_PKE)

static iomux_v3_cfg_t mx51efikamx_pads[] = {
	/* UART1 */
	MX51_PAD_UART1_RXD__UART1_RXD,
	MX51_PAD_UART1_TXD__UART1_TXD,
	MX51_PAD_UART1_RTS__UART1_RTS,
	MX51_PAD_UART1_CTS__UART1_CTS,
	/* board id */
	MX51_PAD_PCBID0,
	MX51_PAD_PCBID1,
	MX51_PAD_PCBID2,

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
	MX51_PAD_GPIO_1_0__ESDHC1_CD,
	MX51_PAD_GPIO_1_1__ESDHC1_WP,
	MX51_PAD_GPIO_1_7__ESDHC2_WP,
	MX51_PAD_GPIO_1_8__ESDHC2_CD,

	/* leds */
	MX51_PAD_CSI1_D9__GPIO_3_13,
	MX51_PAD_CSI1_VSYNC__GPIO_3_14,
	MX51_PAD_CSI1_HSYNC__GPIO_3_15,

	/* power key */
	MX51_PAD_PWRKEY,

	/* spi */
	MX51_PAD_CSPI1_MOSI__ECSPI1_MOSI,
	MX51_PAD_CSPI1_MISO__ECSPI1_MISO,
	MX51_PAD_CSPI1_SS0__GPIO_4_24,
	MX51_PAD_CSPI1_SS1__GPIO_4_25,
	MX51_PAD_CSPI1_RDY__ECSPI1_RDY,
	MX51_PAD_CSPI1_SCLK__ECSPI1_SCLK,

	/* reset */
	MX51_PAD_DI1_PIN13__GPIO_3_2,
	MX51_PAD_GPIO_1_4__GPIO_1_4,
};

/* Serial ports */
#if defined(CONFIG_SERIAL_IMX) || defined(CONFIG_SERIAL_IMX_MODULE)
static const struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static inline void mxc_init_imx_uart(void)
{
	imx51_add_imx_uart(0, &uart_pdata);
	imx51_add_imx_uart(1, &uart_pdata);
	imx51_add_imx_uart(2, &uart_pdata);
}
#else /* !SERIAL_IMX */
static inline void mxc_init_imx_uart(void)
{
}
#endif /* SERIAL_IMX */

/* This function is board specific as the bit mask for the plldiv will also
 * be different for other Freescale SoCs, thus a common bitmask is not
 * possible and cannot get place in /plat-mxc/ehci.c.
 */
static int initialize_otg_port(struct platform_device *pdev)
{
	u32 v;
	void __iomem *usb_base;
	void __iomem *usbother_base;
	usb_base = ioremap(MX51_OTG_BASE_ADDR, SZ_4K);
	usbother_base = (void __iomem *)(usb_base + MX5_USBOTHER_REGS_OFFSET);

	/* Set the PHY clock to 19.2MHz */
	v = __raw_readl(usbother_base + MXC_USB_PHY_CTR_FUNC2_OFFSET);
	v &= ~MX5_USB_UTMI_PHYCTRL1_PLLDIV_MASK;
	v |= MX51_USB_PLL_DIV_24_MHZ;
	__raw_writel(v, usbother_base + MXC_USB_PHY_CTR_FUNC2_OFFSET);
	iounmap(usb_base);
	return 0;
}

static struct mxc_usbh_platform_data dr_utmi_config = {
	.init   = initialize_otg_port,
	.portsc = MXC_EHCI_UTMI_16BIT,
	.flags  = MXC_EHCI_INTERNAL_PHY,
};

/*   PCBID2  PCBID1 PCBID0  STATE
	1       1      1    ER1:rev1.1
	1       1      0    ER2:rev1.2
	1       0      1    ER3:rev1.3
	1       0      0    ER4:rev1.4
*/
static void __init mx51_efikamx_board_id(void)
{
	int id;

	/* things are taking time to settle */
	msleep(150);

	gpio_request(EFIKAMX_PCBID0, "pcbid0");
	gpio_direction_input(EFIKAMX_PCBID0);
	gpio_request(EFIKAMX_PCBID1, "pcbid1");
	gpio_direction_input(EFIKAMX_PCBID1);
	gpio_request(EFIKAMX_PCBID2, "pcbid2");
	gpio_direction_input(EFIKAMX_PCBID2);

	id = gpio_get_value(EFIKAMX_PCBID0);
	id |= gpio_get_value(EFIKAMX_PCBID1) << 1;
	id |= gpio_get_value(EFIKAMX_PCBID2) << 2;

	switch (id) {
	case 7:
		system_rev = 0x11;
		break;
	case 6:
		system_rev = 0x12;
		break;
	case 5:
		system_rev = 0x13;
		break;
	case 4:
		system_rev = 0x14;
		break;
	default:
		system_rev = 0x10;
		break;
	}

	if ((system_rev == 0x10)
		|| (system_rev == 0x12)
		|| (system_rev == 0x14)) {
		printk(KERN_WARNING
			"EfikaMX: Unsupported board revision 1.%u!\n",
			system_rev & 0xf);
	}
}

static struct gpio_led mx51_efikamx_leds[] = {
	{
		.name = "efikamx:green",
		.default_trigger = "default-on",
		.gpio = EFIKAMX_GREEN_LED,
	},
	{
		.name = "efikamx:red",
		.default_trigger = "ide-disk",
		.gpio = EFIKAMX_RED_LED,
	},
	{
		.name = "efikamx:blue",
		.default_trigger = "mmc0",
		.gpio = EFIKAMX_BLUE_LED,
	},
};

static struct gpio_led_platform_data mx51_efikamx_leds_data = {
	.leds = mx51_efikamx_leds,
	.num_leds = ARRAY_SIZE(mx51_efikamx_leds),
};

static struct platform_device mx51_efikamx_leds_device = {
	.name = "leds-gpio",
	.id = -1,
	.dev = {
		.platform_data = &mx51_efikamx_leds_data,
	},
};

static struct gpio_keys_button mx51_efikamx_powerkey[] = {
	{
		.code = KEY_POWER,
		.gpio = EFIKAMX_POWER_KEY,
		.type = EV_PWR,
		.desc = "Power Button (CM)",
		.wakeup = 1,
		.debounce_interval = 10, /* ms */
	},
};

static const struct gpio_keys_platform_data mx51_efikamx_powerkey_data __initconst = {
	.buttons = mx51_efikamx_powerkey,
	.nbuttons = ARRAY_SIZE(mx51_efikamx_powerkey),
};

static struct mtd_partition mx51_efikamx_spi_nor_partitions[] = {
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

static struct flash_platform_data mx51_efikamx_spi_flash_data = {
	.name		= "spi_flash",
	.parts		= mx51_efikamx_spi_nor_partitions,
	.nr_parts	= ARRAY_SIZE(mx51_efikamx_spi_nor_partitions),
	.type		= "sst25vf032b",
};

static struct spi_board_info mx51_efikamx_spi_board_info[] __initdata = {
	{
		.modalias = "m25p80",
		.max_speed_hz = 25000000,
		.bus_num = 0,
		.chip_select = 1,
		.platform_data = &mx51_efikamx_spi_flash_data,
		.irq = -1,
	},
};

static int mx51_efikamx_spi_cs[] = {
	EFIKAMX_SPI_CS0,
	EFIKAMX_SPI_CS1,
};

static const struct spi_imx_master mx51_efikamx_spi_pdata __initconst = {
	.chipselect     = mx51_efikamx_spi_cs,
	.num_chipselect = ARRAY_SIZE(mx51_efikamx_spi_cs),
};

void mx51_efikamx_reset(void)
{
	if (system_rev == 0x11)
		gpio_direction_output(EFIKAMX_RESET1_1, 0);
	else
		gpio_direction_output(EFIKAMX_RESET, 0);
}

static void __init mxc_board_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(mx51efikamx_pads,
					ARRAY_SIZE(mx51efikamx_pads));
	mx51_efikamx_board_id();
	mxc_register_device(&mxc_usbdr_host_device, &dr_utmi_config);
	mxc_init_imx_uart();
	imx51_add_sdhci_esdhc_imx(0, NULL);

	/* on < 1.2 boards both SD controllers are used */
	if (system_rev < 0x12) {
		imx51_add_sdhci_esdhc_imx(1, NULL);
		mx51_efikamx_leds[2].default_trigger = "mmc1";
	}

	platform_device_register(&mx51_efikamx_leds_device);
	imx51_add_gpio_keys(&mx51_efikamx_powerkey_data);

	spi_register_board_info(mx51_efikamx_spi_board_info,
		ARRAY_SIZE(mx51_efikamx_spi_board_info));
	imx51_add_ecspi(0, &mx51_efikamx_spi_pdata);

	if (system_rev == 0x11) {
		gpio_request(EFIKAMX_RESET1_1, "reset");
		gpio_direction_output(EFIKAMX_RESET1_1, 1);
	} else {
		gpio_request(EFIKAMX_RESET, "reset");
		gpio_direction_output(EFIKAMX_RESET, 1);
	}
}

static void __init mx51_efikamx_timer_init(void)
{
	mx51_clocks_init(32768, 24000000, 22579200, 24576000);
}

static struct sys_timer mxc_timer = {
	.init	= mx51_efikamx_timer_init,
};

MACHINE_START(MX51_EFIKAMX, "Genesi EfikaMX nettop")
	/* Maintainer: Amit Kucheria <amit.kucheria@linaro.org> */
	.boot_params = MX51_PHYS_OFFSET + 0x100,
	.map_io = mx51_map_io,
	.init_irq = mx51_init_irq,
	.init_machine =  mxc_board_init,
	.timer = &mxc_timer,
MACHINE_END
