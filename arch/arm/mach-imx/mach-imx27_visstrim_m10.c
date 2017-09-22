/*
 * mach-imx27_visstrim_m10.c
 *
 * Copyright 2010  Javier Martin <javier.martin@vista-silicon.com>
 *
 * Based on mach-pcm038.c, mach-pca100.c, mach-mx27ads.c and others.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/i2c.h>
#include <linux/platform_data/pca953x.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/leds.h>
#include <linux/platform_data/asoc-mx27vis.h>
#include <media/soc_camera.h>
#include <sound/tlv320aic32x4.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/system_info.h>
#include <asm/memblock.h>

#include "common.h"
#include "devices-imx27.h"
#include "ehci.h"
#include "hardware.h"
#include "iomux-mx27.h"

#define TVP5150_RSTN (GPIO_PORTC + 18)
#define TVP5150_PWDN (GPIO_PORTC + 19)
#define OTG_PHY_CS_GPIO (GPIO_PORTF + 17)
#define SDHC1_IRQ_GPIO IMX_GPIO_NR(2, 25)

#define VERSION_MASK		0x7
#define MOTHERBOARD_SHIFT	4
#define EXPBOARD_SHIFT		0

#define MOTHERBOARD_BIT2	(GPIO_PORTD + 31)
#define MOTHERBOARD_BIT1	(GPIO_PORTD + 30)
#define MOTHERBOARD_BIT0	(GPIO_PORTD + 29)

#define EXPBOARD_BIT2		(GPIO_PORTD + 25)
#define EXPBOARD_BIT1		(GPIO_PORTD + 27)
#define EXPBOARD_BIT0		(GPIO_PORTD + 28)

#define AMP_GAIN_0		(GPIO_PORTF + 9)
#define AMP_GAIN_1		(GPIO_PORTF + 8)
#define AMP_MUTE_SDL		(GPIO_PORTE + 5)
#define AMP_MUTE_SDR		(GPIO_PORTF + 7)

static const int visstrim_m10_pins[] __initconst = {
	/* UART1 (console) */
	PE12_PF_UART1_TXD,
	PE13_PF_UART1_RXD,
	PE14_PF_UART1_CTS,
	PE15_PF_UART1_RTS,
	/* FEC */
	PD0_AIN_FEC_TXD0,
	PD1_AIN_FEC_TXD1,
	PD2_AIN_FEC_TXD2,
	PD3_AIN_FEC_TXD3,
	PD4_AOUT_FEC_RX_ER,
	PD5_AOUT_FEC_RXD1,
	PD6_AOUT_FEC_RXD2,
	PD7_AOUT_FEC_RXD3,
	PD8_AF_FEC_MDIO,
	PD9_AIN_FEC_MDC,
	PD10_AOUT_FEC_CRS,
	PD11_AOUT_FEC_TX_CLK,
	PD12_AOUT_FEC_RXD0,
	PD13_AOUT_FEC_RX_DV,
	PD14_AOUT_FEC_RX_CLK,
	PD15_AOUT_FEC_COL,
	PD16_AIN_FEC_TX_ER,
	PF23_AIN_FEC_TX_EN,
	/* SSI1 */
	PC20_PF_SSI1_FS,
	PC21_PF_SSI1_RXD,
	PC22_PF_SSI1_TXD,
	PC23_PF_SSI1_CLK,
	/* SDHC1 */
	PE18_PF_SD1_D0,
	PE19_PF_SD1_D1,
	PE20_PF_SD1_D2,
	PE21_PF_SD1_D3,
	PE22_PF_SD1_CMD,
	PE23_PF_SD1_CLK,
	/* Both I2Cs */
	PD17_PF_I2C_DATA,
	PD18_PF_I2C_CLK,
	PC5_PF_I2C2_SDA,
	PC6_PF_I2C2_SCL,
	/* USB OTG */
	OTG_PHY_CS_GPIO | GPIO_GPIO | GPIO_OUT,
	PC9_PF_USBOTG_DATA0,
	PC11_PF_USBOTG_DATA1,
	PC10_PF_USBOTG_DATA2,
	PC13_PF_USBOTG_DATA3,
	PC12_PF_USBOTG_DATA4,
	PC7_PF_USBOTG_DATA5,
	PC8_PF_USBOTG_DATA6,
	PE25_PF_USBOTG_DATA7,
	PE24_PF_USBOTG_CLK,
	PE2_PF_USBOTG_DIR,
	PE0_PF_USBOTG_NXT,
	PE1_PF_USBOTG_STP,
	PB23_PF_USB_PWR,
	PB24_PF_USB_OC,
	/* CSI */
	TVP5150_RSTN | GPIO_GPIO | GPIO_OUT,
	TVP5150_PWDN | GPIO_GPIO | GPIO_OUT,
	PB10_PF_CSI_D0,
	PB11_PF_CSI_D1,
	PB12_PF_CSI_D2,
	PB13_PF_CSI_D3,
	PB14_PF_CSI_D4,
	PB15_PF_CSI_MCLK,
	PB16_PF_CSI_PIXCLK,
	PB17_PF_CSI_D5,
	PB18_PF_CSI_D6,
	PB19_PF_CSI_D7,
	PB20_PF_CSI_VSYNC,
	PB21_PF_CSI_HSYNC,
	/* mother board version */
	MOTHERBOARD_BIT2 | GPIO_GPIO | GPIO_IN | GPIO_PUEN,
	MOTHERBOARD_BIT1 | GPIO_GPIO | GPIO_IN | GPIO_PUEN,
	MOTHERBOARD_BIT0 | GPIO_GPIO | GPIO_IN | GPIO_PUEN,
	/* expansion board version */
	EXPBOARD_BIT2 | GPIO_GPIO | GPIO_IN | GPIO_PUEN,
	EXPBOARD_BIT1 | GPIO_GPIO | GPIO_IN | GPIO_PUEN,
	EXPBOARD_BIT0 | GPIO_GPIO | GPIO_IN | GPIO_PUEN,
	/* Audio AMP control */
	AMP_GAIN_0 | GPIO_GPIO | GPIO_OUT,
	AMP_GAIN_1 | GPIO_GPIO | GPIO_OUT,
	AMP_MUTE_SDL | GPIO_GPIO | GPIO_OUT,
	AMP_MUTE_SDR | GPIO_GPIO | GPIO_OUT,
};

static struct gpio visstrim_m10_version_gpios[] = {
	{ EXPBOARD_BIT0, GPIOF_IN, "exp-version-0" },
	{ EXPBOARD_BIT1, GPIOF_IN, "exp-version-1" },
	{ EXPBOARD_BIT2, GPIOF_IN, "exp-version-2" },
	{ MOTHERBOARD_BIT0, GPIOF_IN, "mother-version-0" },
	{ MOTHERBOARD_BIT1, GPIOF_IN, "mother-version-1" },
	{ MOTHERBOARD_BIT2, GPIOF_IN, "mother-version-2" },
};

static const struct gpio visstrim_m10_gpios[] __initconst = {
	{
		.gpio = TVP5150_RSTN,
		.flags = GPIOF_DIR_OUT | GPIOF_INIT_HIGH,
		.label = "tvp5150_rstn",
	},
	{
		.gpio = TVP5150_PWDN,
		.flags = GPIOF_DIR_OUT | GPIOF_INIT_LOW,
		.label = "tvp5150_pwdn",
	},
	{
		.gpio = OTG_PHY_CS_GPIO,
		.flags = GPIOF_DIR_OUT | GPIOF_INIT_LOW,
		.label = "usbotg_cs",
	},
	{
		.gpio = AMP_GAIN_0,
		.flags = GPIOF_DIR_OUT,
		.label = "amp-gain-0",
	},
	{
		.gpio = AMP_GAIN_1,
		.flags = GPIOF_DIR_OUT,
		.label = "amp-gain-1",
	},
	{
		.gpio = AMP_MUTE_SDL,
		.flags = GPIOF_DIR_OUT,
		.label = "amp-mute-sdl",
	},
	{
		.gpio = AMP_MUTE_SDR,
		.flags = GPIOF_DIR_OUT,
		.label = "amp-mute-sdr",
	},
};

/* Camera */
static int visstrim_camera_power(struct device *dev, int on)
{
	gpio_set_value(TVP5150_PWDN, on);

	return 0;
};

static int visstrim_camera_reset(struct device *dev)
{
	gpio_set_value(TVP5150_RSTN, 0);
	ndelay(500);
	gpio_set_value(TVP5150_RSTN, 1);

	return 0;
};

static struct i2c_board_info visstrim_i2c_camera =  {
	I2C_BOARD_INFO("tvp5150", 0x5d),
};

static struct soc_camera_link iclink_tvp5150 = {
	.bus_id         = 0,
	.board_info     = &visstrim_i2c_camera,
	.i2c_adapter_id = 0,
	.power = visstrim_camera_power,
	.reset = visstrim_camera_reset,
};

static struct mx2_camera_platform_data visstrim_camera = {
	.flags = MX2_CAMERA_CCIR | MX2_CAMERA_CCIR_INTERLACE |
		 MX2_CAMERA_PCLK_SAMPLE_RISING,
	.clk = 100000,
};

static phys_addr_t mx2_camera_base __initdata;
#define MX2_CAMERA_BUF_SIZE SZ_8M

static void __init visstrim_analog_camera_init(void)
{
	struct platform_device *pdev;

	gpio_set_value(TVP5150_PWDN, 1);
	ndelay(1);
	gpio_set_value(TVP5150_RSTN, 0);
	ndelay(500);
	gpio_set_value(TVP5150_RSTN, 1);
	ndelay(200000);

	pdev = imx27_add_mx2_camera(&visstrim_camera);
	if (IS_ERR(pdev))
		return;

	dma_declare_coherent_memory(&pdev->dev, mx2_camera_base,
				    mx2_camera_base, MX2_CAMERA_BUF_SIZE,
				    DMA_MEMORY_EXCLUSIVE);
}

static void __init visstrim_reserve(void)
{
	/* reserve 4 MiB for mx2-camera */
	mx2_camera_base = arm_memblock_steal(3 * MX2_CAMERA_BUF_SIZE,
			MX2_CAMERA_BUF_SIZE);
}

/* GPIOs used as events for applications */
static struct gpio_keys_button visstrim_gpio_keys[] = {
	{
		.type	= EV_KEY,
		.code	= KEY_RESTART,
		.gpio	= (GPIO_PORTC + 15),
		.desc	= "Default config",
		.active_low = 0,
		.wakeup = 1,
	},
	{
		.type	= EV_KEY,
		.code	= KEY_RECORD,
		.gpio	= (GPIO_PORTF + 14),
		.desc	= "Record",
		.active_low = 0,
		.wakeup = 1,
	},
	{
		.type   = EV_KEY,
		.code   = KEY_STOP,
		.gpio   = (GPIO_PORTF + 13),
		.desc   = "Stop",
		.active_low = 0,
		.wakeup = 1,
	}
};

static const struct gpio_keys_platform_data
		visstrim_gpio_keys_platform_data __initconst = {
	.buttons	= visstrim_gpio_keys,
	.nbuttons	= ARRAY_SIZE(visstrim_gpio_keys),
};

/* led */
static const struct gpio_led visstrim_m10_leds[] __initconst = {
	{
		.name = "visstrim:ld0",
		.default_trigger = "nand-disk",
		.gpio = (GPIO_PORTC + 29),
	},
	{
		.name = "visstrim:ld1",
		.default_trigger = "nand-disk",
		.gpio = (GPIO_PORTC + 24),
	},
	{
		.name = "visstrim:ld2",
		.default_trigger = "nand-disk",
		.gpio = (GPIO_PORTC + 28),
	},
	{
		.name = "visstrim:ld3",
		.default_trigger = "nand-disk",
		.gpio = (GPIO_PORTC + 25),
	},
};

static const struct gpio_led_platform_data visstrim_m10_led_data __initconst = {
	.leds = visstrim_m10_leds,
	.num_leds = ARRAY_SIZE(visstrim_m10_leds),
};

/* Visstrim_SM10 has a microSD slot connected to sdhc1 */
static int visstrim_m10_sdhc1_init(struct device *dev,
		irq_handler_t detect_irq, void *data)
{
	int ret;

	ret = request_irq(gpio_to_irq(SDHC1_IRQ_GPIO), detect_irq,
			  IRQF_TRIGGER_FALLING, "mmc-detect", data);
	return ret;
}

static void visstrim_m10_sdhc1_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(SDHC1_IRQ_GPIO), data);
}

static const struct imxmmc_platform_data visstrim_m10_sdhc_pdata __initconst = {
	.init = visstrim_m10_sdhc1_init,
	.exit = visstrim_m10_sdhc1_exit,
};

/* Visstrim_SM10 NOR flash */
static struct physmap_flash_data visstrim_m10_flash_data = {
	.width = 2,
};

static struct resource visstrim_m10_flash_resource = {
	.start = 0xc0000000,
	.end = 0xc0000000 + SZ_64M - 1,
	.flags = IORESOURCE_MEM,
};

static struct platform_device visstrim_m10_nor_mtd_device = {
	.name = "physmap-flash",
	.id = 0,
	.dev = {
		.platform_data = &visstrim_m10_flash_data,
	},
	.num_resources = 1,
	.resource = &visstrim_m10_flash_resource,
};

static struct platform_device *platform_devices[] __initdata = {
	&visstrim_m10_nor_mtd_device,
};

/* Visstrim_M10 uses UART0 as console */
static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

/* I2C */
static const struct imxi2c_platform_data visstrim_m10_i2c_data __initconst = {
	.bitrate = 100000,
};

static struct pca953x_platform_data visstrim_m10_pca9555_pdata = {
	.gpio_base = 240, /* After MX27 internal GPIOs */
	.invert = 0,
};

static struct aic32x4_pdata visstrim_m10_aic32x4_pdata = {
	.power_cfg = AIC32X4_PWR_MICBIAS_2075_LDOIN |
		     AIC32X4_PWR_AVDD_DVDD_WEAK_DISABLE |
		     AIC32X4_PWR_AIC32X4_LDO_ENABLE |
		     AIC32X4_PWR_CMMODE_LDOIN_RANGE_18_36 |
		     AIC32X4_PWR_CMMODE_HP_LDOIN_POWERED,
	.micpga_routing = AIC32X4_MICPGA_ROUTE_LMIC_IN2R_10K |
			 AIC32X4_MICPGA_ROUTE_RMIC_IN1L_10K,
	.swapdacs = false,
};

static struct i2c_board_info visstrim_m10_i2c_devices[] = {
	{
		I2C_BOARD_INFO("pca9555", 0x20),
		.platform_data = &visstrim_m10_pca9555_pdata,
	},
	{
		I2C_BOARD_INFO("tlv320aic32x4", 0x18),
		.platform_data = &visstrim_m10_aic32x4_pdata,
	},
	{
		 I2C_BOARD_INFO("m41t00", 0x68),
	}
};

/* USB OTG */
static int otg_phy_init(struct platform_device *pdev)
{
	return mx27_initialize_usb_hw(pdev->id, MXC_EHCI_POWER_PINS_ENABLED);
}

static const struct mxc_usbh_platform_data
visstrim_m10_usbotg_pdata __initconst = {
	.init = otg_phy_init,
	.portsc	= MXC_EHCI_MODE_ULPI | MXC_EHCI_UTMI_8BIT,
};

/* SSI */
static const struct imx_ssi_platform_data visstrim_m10_ssi_pdata __initconst = {
	.flags			= IMX_SSI_DMA | IMX_SSI_SYN,
};

/* coda */

static void __init visstrim_coda_init(void)
{
	struct platform_device *pdev;

	pdev = imx27_add_coda();
	dma_declare_coherent_memory(&pdev->dev,
				    mx2_camera_base + MX2_CAMERA_BUF_SIZE,
				    mx2_camera_base + MX2_CAMERA_BUF_SIZE,
				    MX2_CAMERA_BUF_SIZE,
				    DMA_MEMORY_EXCLUSIVE);
}

/* DMA deinterlace */
static struct platform_device visstrim_deinterlace = {
	.name = "m2m-deinterlace",
	.id = 0,
};

static void __init visstrim_deinterlace_init(void)
{
	int ret = -ENOMEM;
	struct platform_device *pdev = &visstrim_deinterlace;

	ret = platform_device_register(pdev);

	dma_declare_coherent_memory(&pdev->dev,
				    mx2_camera_base + 2 * MX2_CAMERA_BUF_SIZE,
				    mx2_camera_base + 2 * MX2_CAMERA_BUF_SIZE,
				    MX2_CAMERA_BUF_SIZE,
				    DMA_MEMORY_EXCLUSIVE);
}

/* Emma-PrP for format conversion */
static void __init visstrim_emmaprp_init(void)
{
	struct platform_device *pdev;
	int ret;

	pdev = imx27_add_mx2_emmaprp();
	if (IS_ERR(pdev))
		return;

	/*
	 * Use the same memory area as the analog camera since both
	 * devices are, by nature, exclusive.
	 */
	ret = dma_declare_coherent_memory(&pdev->dev,
				mx2_camera_base, mx2_camera_base,
				MX2_CAMERA_BUF_SIZE,
				DMA_MEMORY_EXCLUSIVE);
	if (ret)
		pr_err("Failed to declare memory for emmaprp\n");
}

/* Audio */
static const struct snd_mx27vis_platform_data snd_mx27vis_pdata __initconst = {
	.amp_gain0_gpio = AMP_GAIN_0,
	.amp_gain1_gpio = AMP_GAIN_1,
	.amp_mutel_gpio = AMP_MUTE_SDL,
	.amp_muter_gpio = AMP_MUTE_SDR,
};

static void __init visstrim_m10_revision(void)
{
	int exp_version = 0;
	int mo_version = 0;
	int ret;

	ret = gpio_request_array(visstrim_m10_version_gpios,
				 ARRAY_SIZE(visstrim_m10_version_gpios));
	if (ret) {
		pr_err("Failed to request version gpios");
		return;
	}

	/* Get expansion board version (negative logic) */
	exp_version |= !gpio_get_value(EXPBOARD_BIT2) << 2;
	exp_version |= !gpio_get_value(EXPBOARD_BIT1) << 1;
	exp_version |= !gpio_get_value(EXPBOARD_BIT0);

	/* Get mother board version (negative logic) */
	mo_version |= !gpio_get_value(MOTHERBOARD_BIT2) << 2;
	mo_version |= !gpio_get_value(MOTHERBOARD_BIT1) << 1;
	mo_version |= !gpio_get_value(MOTHERBOARD_BIT0);

	system_rev = 0x27000;
	system_rev |= (mo_version << MOTHERBOARD_SHIFT);
	system_rev |= (exp_version << EXPBOARD_SHIFT);
}

static void __init visstrim_m10_board_init(void)
{
	int ret;

	imx27_soc_init();
	visstrim_m10_revision();

	ret = mxc_gpio_setup_multiple_pins(visstrim_m10_pins,
			ARRAY_SIZE(visstrim_m10_pins), "VISSTRIM_M10");
	if (ret)
		pr_err("Failed to setup pins (%d)\n", ret);

	imx27_add_imx_ssi(0, &visstrim_m10_ssi_pdata);
	imx27_add_imx_uart0(&uart_pdata);

	imx27_add_imx_i2c(0, &visstrim_m10_i2c_data);
	imx27_add_imx_i2c(1, &visstrim_m10_i2c_data);
	i2c_register_board_info(0, visstrim_m10_i2c_devices,
				ARRAY_SIZE(visstrim_m10_i2c_devices));

	imx27_add_mxc_mmc(0, &visstrim_m10_sdhc_pdata);
	imx27_add_mxc_ehci_otg(&visstrim_m10_usbotg_pdata);
	imx27_add_fec(NULL);

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));
}

static void __init visstrim_m10_late_init(void)
{
	int mo_version, ret;

	ret = gpio_request_array(visstrim_m10_gpios,
				 ARRAY_SIZE(visstrim_m10_gpios));
	if (ret)
		pr_err("Failed to request gpios (%d)\n", ret);

	imx_add_gpio_keys(&visstrim_gpio_keys_platform_data);

	imx_add_platform_device("mx27vis", 0, NULL, 0, &snd_mx27vis_pdata,
				sizeof(snd_mx27vis_pdata));
	platform_device_register_resndata(NULL, "soc-camera-pdrv", 0, NULL, 0,
				      &iclink_tvp5150, sizeof(iclink_tvp5150));

	gpio_led_register_device(0, &visstrim_m10_led_data);

	/* Use mother board version to decide what video devices we shall use */
	mo_version = (system_rev >> MOTHERBOARD_SHIFT) & VERSION_MASK;
	if (mo_version & 0x1) {
		visstrim_emmaprp_init();

		/*
		 * Despite not being used, tvp5150 must be
		 * powered on to avoid I2C problems. To minimize
		 * power consupmtion keep reset enabled.
		 */
		gpio_set_value(TVP5150_PWDN, 1);
		ndelay(1);
		gpio_set_value(TVP5150_RSTN, 0);
	} else {
		visstrim_deinterlace_init();
		visstrim_analog_camera_init();
	}

	visstrim_coda_init();
}

static void __init visstrim_m10_timer_init(void)
{
	mx27_clocks_init((unsigned long)25000000);
}

MACHINE_START(IMX27_VISSTRIM_M10, "Vista Silicon Visstrim_M10")
	.atag_offset = 0x100,
	.reserve = visstrim_reserve,
	.map_io = mx27_map_io,
	.init_early = imx27_init_early,
	.init_irq = mx27_init_irq,
	.init_time	= visstrim_m10_timer_init,
	.init_machine = visstrim_m10_board_init,
	.init_late	= visstrim_m10_late_init,
	.restart	= mxc_restart,
MACHINE_END
