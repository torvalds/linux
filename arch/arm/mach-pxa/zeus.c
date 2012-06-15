/*
 *  Support for the Arcom ZEUS.
 *
 *  Copyright (C) 2006 Arcom Control Systems Ltd.
 *
 *  Loosely based on Arcom's 2.6.16.28.
 *  Maintained by Marc Zyngier <maz@misterjones.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/cpufreq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/gpio.h>
#include <linux/serial_8250.h>
#include <linux/dm9000.h>
#include <linux/mmc/host.h>
#include <linux/spi/spi.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/i2c.h>
#include <linux/i2c/pxa-i2c.h>
#include <linux/i2c/pca953x.h>
#include <linux/apm-emulation.h>
#include <linux/can/platform/mcp251x.h>

#include <asm/mach-types.h>
#include <asm/suspend.h>
#include <asm/system_info.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/pxa27x.h>
#include <mach/regs-uart.h>
#include <mach/ohci.h>
#include <mach/mmc.h>
#include <mach/pxa27x-udc.h>
#include <mach/udc.h>
#include <mach/pxafb.h>
#include <mach/pm.h>
#include <mach/audio.h>
#include <mach/arcom-pcmcia.h>
#include <mach/zeus.h>
#include <mach/smemc.h>

#include "generic.h"

/*
 * Interrupt handling
 */

static unsigned long zeus_irq_enabled_mask;
static const int zeus_isa_irqs[] = { 3, 4, 5, 6, 7, 10, 11, 12, };
static const int zeus_isa_irq_map[] = {
	0,		/* ISA irq #0, invalid */
	0,		/* ISA irq #1, invalid */
	0,		/* ISA irq #2, invalid */
	1 << 0,		/* ISA irq #3 */
	1 << 1,		/* ISA irq #4 */
	1 << 2,		/* ISA irq #5 */
	1 << 3,		/* ISA irq #6 */
	1 << 4,		/* ISA irq #7 */
	0,		/* ISA irq #8, invalid */
	0,		/* ISA irq #9, invalid */
	1 << 5,		/* ISA irq #10 */
	1 << 6,		/* ISA irq #11 */
	1 << 7,		/* ISA irq #12 */
};

static inline int zeus_irq_to_bitmask(unsigned int irq)
{
	return zeus_isa_irq_map[irq - PXA_ISA_IRQ(0)];
}

static inline int zeus_bit_to_irq(int bit)
{
	return zeus_isa_irqs[bit] + PXA_ISA_IRQ(0);
}

static void zeus_ack_irq(struct irq_data *d)
{
	__raw_writew(zeus_irq_to_bitmask(d->irq), ZEUS_CPLD_ISA_IRQ);
}

static void zeus_mask_irq(struct irq_data *d)
{
	zeus_irq_enabled_mask &= ~(zeus_irq_to_bitmask(d->irq));
}

static void zeus_unmask_irq(struct irq_data *d)
{
	zeus_irq_enabled_mask |= zeus_irq_to_bitmask(d->irq);
}

static inline unsigned long zeus_irq_pending(void)
{
	return __raw_readw(ZEUS_CPLD_ISA_IRQ) & zeus_irq_enabled_mask;
}

static void zeus_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned long pending;

	pending = zeus_irq_pending();
	do {
		/* we're in a chained irq handler,
		 * so ack the interrupt by hand */
		desc->irq_data.chip->irq_ack(&desc->irq_data);

		if (likely(pending)) {
			irq = zeus_bit_to_irq(__ffs(pending));
			generic_handle_irq(irq);
		}
		pending = zeus_irq_pending();
	} while (pending);
}

static struct irq_chip zeus_irq_chip = {
	.name		= "ISA",
	.irq_ack	= zeus_ack_irq,
	.irq_mask	= zeus_mask_irq,
	.irq_unmask	= zeus_unmask_irq,
};

static void __init zeus_init_irq(void)
{
	int level;
	int isa_irq;

	pxa27x_init_irq();

	/* Peripheral IRQs. It would be nice to move those inside driver
	   configuration, but it is not supported at the moment. */
	irq_set_irq_type(gpio_to_irq(ZEUS_AC97_GPIO), IRQ_TYPE_EDGE_RISING);
	irq_set_irq_type(gpio_to_irq(ZEUS_WAKEUP_GPIO), IRQ_TYPE_EDGE_RISING);
	irq_set_irq_type(gpio_to_irq(ZEUS_PTT_GPIO), IRQ_TYPE_EDGE_RISING);
	irq_set_irq_type(gpio_to_irq(ZEUS_EXTGPIO_GPIO),
			 IRQ_TYPE_EDGE_FALLING);
	irq_set_irq_type(gpio_to_irq(ZEUS_CAN_GPIO), IRQ_TYPE_EDGE_FALLING);

	/* Setup ISA IRQs */
	for (level = 0; level < ARRAY_SIZE(zeus_isa_irqs); level++) {
		isa_irq = zeus_bit_to_irq(level);
		irq_set_chip_and_handler(isa_irq, &zeus_irq_chip,
					 handle_edge_irq);
		set_irq_flags(isa_irq, IRQF_VALID | IRQF_PROBE);
	}

	irq_set_irq_type(gpio_to_irq(ZEUS_ISA_GPIO), IRQ_TYPE_EDGE_RISING);
	irq_set_chained_handler(gpio_to_irq(ZEUS_ISA_GPIO), zeus_irq_handler);
}


/*
 * Platform devices
 */

/* Flash */
static struct resource zeus_mtd_resources[] = {
	[0] = { /* NOR Flash (up to 64MB) */
		.start	= ZEUS_FLASH_PHYS,
		.end	= ZEUS_FLASH_PHYS + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = { /* SRAM */
		.start	= ZEUS_SRAM_PHYS,
		.end	= ZEUS_SRAM_PHYS + SZ_512K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct physmap_flash_data zeus_flash_data[] = {
	[0] = {
		.width		= 2,
		.parts		= NULL,
		.nr_parts	= 0,
	},
};

static struct platform_device zeus_mtd_devices[] = {
	[0] = {
		.name		= "physmap-flash",
		.id		= 0,
		.dev		= {
			.platform_data = &zeus_flash_data[0],
		},
		.resource	= &zeus_mtd_resources[0],
		.num_resources	= 1,
	},
};

/* Serial */
static struct resource zeus_serial_resources[] = {
	{
		.start	= 0x10000000,
		.end	= 0x1000000f,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= 0x10800000,
		.end	= 0x1080000f,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= 0x11000000,
		.end	= 0x1100000f,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= 0x40100000,
		.end	= 0x4010001f,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= 0x40200000,
		.end	= 0x4020001f,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= 0x40700000,
		.end	= 0x4070001f,
		.flags	= IORESOURCE_MEM,
	},
};

static struct plat_serial8250_port serial_platform_data[] = {
	/* External UARTs */
	/* FIXME: Shared IRQs on COM1-COM4 will not work properly on v1i1 hardware. */
	{ /* COM1 */
		.mapbase	= 0x10000000,
		.irq		= PXA_GPIO_TO_IRQ(ZEUS_UARTA_GPIO),
		.irqflags	= IRQF_TRIGGER_RISING,
		.uartclk	= 14745600,
		.regshift	= 1,
		.flags		= UPF_IOREMAP | UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
	},
	{ /* COM2 */
		.mapbase	= 0x10800000,
		.irq		= PXA_GPIO_TO_IRQ(ZEUS_UARTB_GPIO),
		.irqflags	= IRQF_TRIGGER_RISING,
		.uartclk	= 14745600,
		.regshift	= 1,
		.flags		= UPF_IOREMAP | UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
	},
	{ /* COM3 */
		.mapbase	= 0x11000000,
		.irq		= PXA_GPIO_TO_IRQ(ZEUS_UARTC_GPIO),
		.irqflags	= IRQF_TRIGGER_RISING,
		.uartclk	= 14745600,
		.regshift	= 1,
		.flags		= UPF_IOREMAP | UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
	},
	{ /* COM4 */
		.mapbase	= 0x11800000,
		.irq		= PXA_GPIO_TO_IRQ(ZEUS_UARTD_GPIO),
		.irqflags	= IRQF_TRIGGER_RISING,
		.uartclk	= 14745600,
		.regshift	= 1,
		.flags		= UPF_IOREMAP | UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
	},
	/* Internal UARTs */
	{ /* FFUART */
		.membase	= (void *)&FFUART,
		.mapbase	= __PREG(FFUART),
		.irq		= IRQ_FFUART,
		.uartclk	= 921600 * 16,
		.regshift	= 2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
	},
	{ /* BTUART */
		.membase	= (void *)&BTUART,
		.mapbase	= __PREG(BTUART),
		.irq		= IRQ_BTUART,
		.uartclk	= 921600 * 16,
		.regshift	= 2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
	},
	{ /* STUART */
		.membase	= (void *)&STUART,
		.mapbase	= __PREG(STUART),
		.irq		= IRQ_STUART,
		.uartclk	= 921600 * 16,
		.regshift	= 2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
	},
	{ },
};

static struct platform_device zeus_serial_device = {
	.name = "serial8250",
	.id   = PLAT8250_DEV_PLATFORM,
	.dev  = {
		.platform_data = serial_platform_data,
	},
	.num_resources	= ARRAY_SIZE(zeus_serial_resources),
	.resource	= zeus_serial_resources,
};

/* Ethernet */
static struct resource zeus_dm9k0_resource[] = {
	[0] = {
		.start = ZEUS_ETH0_PHYS,
		.end   = ZEUS_ETH0_PHYS + 1,
		.flags = IORESOURCE_MEM
	},
	[1] = {
		.start = ZEUS_ETH0_PHYS + 2,
		.end   = ZEUS_ETH0_PHYS + 3,
		.flags = IORESOURCE_MEM
	},
	[2] = {
		.start = PXA_GPIO_TO_IRQ(ZEUS_ETH0_GPIO),
		.end   = PXA_GPIO_TO_IRQ(ZEUS_ETH0_GPIO),
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

static struct resource zeus_dm9k1_resource[] = {
	[0] = {
		.start = ZEUS_ETH1_PHYS,
		.end   = ZEUS_ETH1_PHYS + 1,
		.flags = IORESOURCE_MEM
	},
	[1] = {
		.start = ZEUS_ETH1_PHYS + 2,
		.end   = ZEUS_ETH1_PHYS + 3,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = PXA_GPIO_TO_IRQ(ZEUS_ETH1_GPIO),
		.end   = PXA_GPIO_TO_IRQ(ZEUS_ETH1_GPIO),
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

static struct dm9000_plat_data zeus_dm9k_platdata = {
	.flags		= DM9000_PLATF_16BITONLY,
};

static struct platform_device zeus_dm9k0_device = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(zeus_dm9k0_resource),
	.resource	= zeus_dm9k0_resource,
	.dev		= {
		.platform_data = &zeus_dm9k_platdata,
	}
};

static struct platform_device zeus_dm9k1_device = {
	.name		= "dm9000",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(zeus_dm9k1_resource),
	.resource	= zeus_dm9k1_resource,
	.dev		= {
		.platform_data = &zeus_dm9k_platdata,
	}
};

/* External SRAM */
static struct resource zeus_sram_resource = {
	.start		= ZEUS_SRAM_PHYS,
	.end		= ZEUS_SRAM_PHYS + ZEUS_SRAM_SIZE * 2 - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device zeus_sram_device = {
	.name		= "pxa2xx-8bit-sram",
	.id		= 0,
	.num_resources	= 1,
	.resource	= &zeus_sram_resource,
};

/* SPI interface on SSP3 */
static struct pxa2xx_spi_master pxa2xx_spi_ssp3_master_info = {
	.num_chipselect = 1,
	.enable_dma     = 1,
};

/* CAN bus on SPI */
static int zeus_mcp2515_setup(struct spi_device *sdev)
{
	int err;

	err = gpio_request(ZEUS_CAN_SHDN_GPIO, "CAN shutdown");
	if (err)
		return err;

	err = gpio_direction_output(ZEUS_CAN_SHDN_GPIO, 1);
	if (err) {
		gpio_free(ZEUS_CAN_SHDN_GPIO);
		return err;
	}

	return 0;
}

static int zeus_mcp2515_transceiver_enable(int enable)
{
	gpio_set_value(ZEUS_CAN_SHDN_GPIO, !enable);
	return 0;
}

static struct mcp251x_platform_data zeus_mcp2515_pdata = {
	.oscillator_frequency	= 16*1000*1000,
	.board_specific_setup	= zeus_mcp2515_setup,
	.power_enable		= zeus_mcp2515_transceiver_enable,
};

static struct spi_board_info zeus_spi_board_info[] = {
	[0] = {
		.modalias	= "mcp2515",
		.platform_data	= &zeus_mcp2515_pdata,
		.irq		= PXA_GPIO_TO_IRQ(ZEUS_CAN_GPIO),
		.max_speed_hz	= 1*1000*1000,
		.bus_num	= 3,
		.mode		= SPI_MODE_0,
		.chip_select	= 0,
	},
};

/* Leds */
static struct gpio_led zeus_leds[] = {
	[0] = {
		.name		 = "zeus:yellow:1",
		.default_trigger = "heartbeat",
		.gpio		 = ZEUS_EXT0_GPIO(3),
		.active_low	 = 1,
	},
	[1] = {
		.name		 = "zeus:yellow:2",
		.default_trigger = "default-on",
		.gpio		 = ZEUS_EXT0_GPIO(4),
		.active_low	 = 1,
	},
	[2] = {
		.name		 = "zeus:yellow:3",
		.default_trigger = "default-on",
		.gpio		 = ZEUS_EXT0_GPIO(5),
		.active_low	 = 1,
	},
};

static struct gpio_led_platform_data zeus_leds_info = {
	.leds		= zeus_leds,
	.num_leds	= ARRAY_SIZE(zeus_leds),
};

static struct platform_device zeus_leds_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &zeus_leds_info,
	},
};

static void zeus_cf_reset(int state)
{
	u16 cpld_state = __raw_readw(ZEUS_CPLD_CONTROL);

	if (state)
		cpld_state |= ZEUS_CPLD_CONTROL_CF_RST;
	else
		cpld_state &= ~ZEUS_CPLD_CONTROL_CF_RST;

	__raw_writew(cpld_state, ZEUS_CPLD_CONTROL);
}

static struct arcom_pcmcia_pdata zeus_pcmcia_info = {
	.cd_gpio	= ZEUS_CF_CD_GPIO,
	.rdy_gpio	= ZEUS_CF_RDY_GPIO,
	.pwr_gpio	= ZEUS_CF_PWEN_GPIO,
	.reset		= zeus_cf_reset,
};

static struct platform_device zeus_pcmcia_device = {
	.name		= "zeus-pcmcia",
	.id		= -1,
	.dev		= {
		.platform_data	= &zeus_pcmcia_info,
	},
};

static struct resource zeus_max6369_resource = {
	.start		= ZEUS_CPLD_EXTWDOG_PHYS,
	.end		= ZEUS_CPLD_EXTWDOG_PHYS,
	.flags		= IORESOURCE_MEM,
};

struct platform_device zeus_max6369_device = {
	.name		= "max6369_wdt",
	.id		= -1,
	.resource	= &zeus_max6369_resource,
	.num_resources	= 1,
};

static struct platform_device *zeus_devices[] __initdata = {
	&zeus_serial_device,
	&zeus_mtd_devices[0],
	&zeus_dm9k0_device,
	&zeus_dm9k1_device,
	&zeus_sram_device,
	&zeus_leds_device,
	&zeus_pcmcia_device,
	&zeus_max6369_device,
};

/* AC'97 */
static pxa2xx_audio_ops_t zeus_ac97_info = {
	.reset_gpio = 95,
};


/*
 * USB host
 */

static int zeus_ohci_init(struct device *dev)
{
	int err;

	/* Switch on port 2. */
	if ((err = gpio_request(ZEUS_USB2_PWREN_GPIO, "USB2_PWREN"))) {
		dev_err(dev, "Can't request USB2_PWREN\n");
		return err;
	}

	if ((err = gpio_direction_output(ZEUS_USB2_PWREN_GPIO, 1))) {
		gpio_free(ZEUS_USB2_PWREN_GPIO);
		dev_err(dev, "Can't enable USB2_PWREN\n");
		return err;
	}

	/* Port 2 is shared between host and client interface. */
	UP2OCR = UP2OCR_HXOE | UP2OCR_HXS | UP2OCR_DMPDE | UP2OCR_DPPDE;

	return 0;
}

static void zeus_ohci_exit(struct device *dev)
{
	/* Power-off port 2 */
	gpio_direction_output(ZEUS_USB2_PWREN_GPIO, 0);
	gpio_free(ZEUS_USB2_PWREN_GPIO);
}

static struct pxaohci_platform_data zeus_ohci_platform_data = {
	.port_mode	= PMM_NPS_MODE,
	/* Clear Power Control Polarity Low and set Power Sense
	 * Polarity Low. Supply power to USB ports. */
	.flags		= ENABLE_PORT_ALL | POWER_SENSE_LOW,
	.init		= zeus_ohci_init,
	.exit		= zeus_ohci_exit,
};

/*
 * Flat Panel
 */

static void zeus_lcd_power(int on, struct fb_var_screeninfo *si)
{
	gpio_set_value(ZEUS_LCD_EN_GPIO, on);
}

static void zeus_backlight_power(int on)
{
	gpio_set_value(ZEUS_BKLEN_GPIO, on);
}

static int zeus_setup_fb_gpios(void)
{
	int err;

	if ((err = gpio_request(ZEUS_LCD_EN_GPIO, "LCD_EN")))
		goto out_err;

	if ((err = gpio_direction_output(ZEUS_LCD_EN_GPIO, 0)))
		goto out_err_lcd;

	if ((err = gpio_request(ZEUS_BKLEN_GPIO, "BKLEN")))
		goto out_err_lcd;

	if ((err = gpio_direction_output(ZEUS_BKLEN_GPIO, 0)))
		goto out_err_bkl;

	return 0;

out_err_bkl:
	gpio_free(ZEUS_BKLEN_GPIO);
out_err_lcd:
	gpio_free(ZEUS_LCD_EN_GPIO);
out_err:
	return err;
}

static struct pxafb_mode_info zeus_fb_mode_info[] = {
	{
		.pixclock       = 39722,

		.xres           = 640,
		.yres           = 480,

		.bpp            = 16,

		.hsync_len      = 63,
		.left_margin    = 16,
		.right_margin   = 81,

		.vsync_len      = 2,
		.upper_margin   = 12,
		.lower_margin   = 31,

		.sync		= 0,
	},
};

static struct pxafb_mach_info zeus_fb_info = {
	.modes			= zeus_fb_mode_info,
	.num_modes		= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
	.pxafb_lcd_power	= zeus_lcd_power,
	.pxafb_backlight_power	= zeus_backlight_power,
};

/*
 * MMC/SD Device
 *
 * The card detect interrupt isn't debounced so we delay it by 250ms
 * to give the card a chance to fully insert/eject.
 */

static struct pxamci_platform_data zeus_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33|MMC_VDD_33_34,
	.detect_delay_ms	= 250,
	.gpio_card_detect       = ZEUS_MMC_CD_GPIO,
	.gpio_card_ro           = ZEUS_MMC_WP_GPIO,
	.gpio_card_ro_invert	= 1,
	.gpio_power             = -1
};

/*
 * USB Device Controller
 */
static void zeus_udc_command(int cmd)
{
	switch (cmd) {
	case PXA2XX_UDC_CMD_DISCONNECT:
		pr_info("zeus: disconnecting USB client\n");
		UP2OCR = UP2OCR_HXOE | UP2OCR_HXS | UP2OCR_DMPDE | UP2OCR_DPPDE;
		break;

	case PXA2XX_UDC_CMD_CONNECT:
		pr_info("zeus: connecting USB client\n");
		UP2OCR = UP2OCR_HXOE | UP2OCR_DPPUE;
		break;
	}
}

static struct pxa2xx_udc_mach_info zeus_udc_info = {
	.udc_command = zeus_udc_command,
};

#ifdef CONFIG_PM
static void zeus_power_off(void)
{
	local_irq_disable();
	cpu_suspend(PWRMODE_DEEPSLEEP, pxa27x_finish_suspend);
}
#else
#define zeus_power_off   NULL
#endif

#ifdef CONFIG_APM_EMULATION
static void zeus_get_power_status(struct apm_power_info *info)
{
	/* Power supply is always present */
	info->ac_line_status	= APM_AC_ONLINE;
	info->battery_status	= APM_BATTERY_STATUS_NOT_PRESENT;
	info->battery_flag	= APM_BATTERY_FLAG_NOT_PRESENT;
}

static inline void zeus_setup_apm(void)
{
	apm_get_power_status = zeus_get_power_status;
}
#else
static inline void zeus_setup_apm(void)
{
}
#endif

static int zeus_get_pcb_info(struct i2c_client *client, unsigned gpio,
			     unsigned ngpio, void *context)
{
	int i;
	u8 pcb_info = 0;

	for (i = 0; i < 8; i++) {
		int pcb_bit = gpio + i + 8;

		if (gpio_request(pcb_bit, "pcb info")) {
			dev_err(&client->dev, "Can't request pcb info %d\n", i);
			continue;
		}

		if (gpio_direction_input(pcb_bit)) {
			dev_err(&client->dev, "Can't read pcb info %d\n", i);
			gpio_free(pcb_bit);
			continue;
		}

		pcb_info |= !!gpio_get_value(pcb_bit) << i;

		gpio_free(pcb_bit);
	}

	dev_info(&client->dev, "Zeus PCB version %d issue %d\n",
		 pcb_info >> 4, pcb_info & 0xf);

	return 0;
}

static struct pca953x_platform_data zeus_pca953x_pdata[] = {
	[0] = { .gpio_base	= ZEUS_EXT0_GPIO_BASE, },
	[1] = {
		.gpio_base	= ZEUS_EXT1_GPIO_BASE,
		.setup		= zeus_get_pcb_info,
	},
	[2] = { .gpio_base = ZEUS_USER_GPIO_BASE, },
};

static struct i2c_board_info __initdata zeus_i2c_devices[] = {
	{
		I2C_BOARD_INFO("pca9535",	0x21),
		.platform_data	= &zeus_pca953x_pdata[0],
	},
	{
		I2C_BOARD_INFO("pca9535",	0x22),
		.platform_data	= &zeus_pca953x_pdata[1],
	},
	{
		I2C_BOARD_INFO("pca9535",	0x20),
		.platform_data	= &zeus_pca953x_pdata[2],
		.irq		= PXA_GPIO_TO_IRQ(ZEUS_EXTGPIO_GPIO),
	},
	{ I2C_BOARD_INFO("lm75a",	0x48) },
	{ I2C_BOARD_INFO("24c01",	0x50) },
	{ I2C_BOARD_INFO("isl1208",	0x6f) },
};

static mfp_cfg_t zeus_pin_config[] __initdata = {
	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,

	GPIO15_nCS_1,
	GPIO78_nCS_2,
	GPIO80_nCS_4,
	GPIO33_nCS_5,

	GPIO22_GPIO,
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,

	GPIO88_USBH1_PWR,
	GPIO89_USBH1_PEN,
	GPIO119_USBH2_PWR,
	GPIO120_USBH2_PEN,

	GPIO86_LCD_LDD_16,
	GPIO87_LCD_LDD_17,

	GPIO102_GPIO,
	GPIO104_CIF_DD_2,
	GPIO105_CIF_DD_1,

	GPIO81_SSP3_TXD,
	GPIO82_SSP3_RXD,
	GPIO83_SSP3_SFRM,
	GPIO84_SSP3_SCLK,

	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO85_nPCE_1,
	GPIO54_nPCE_2,
	GPIO79_PSKTSEL,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,
	GPIO36_GPIO,		/* CF CD */
	GPIO97_GPIO,		/* CF PWREN */
	GPIO99_GPIO,		/* CF RDY */
};

/*
 * DM9k MSCx settings:	SRAM, 16 bits
 *			17 cycles delay first access
 *			 5 cycles delay next access
 *			13 cycles recovery time
 *			faster device
 */
#define DM9K_MSC_VALUE		0xe4c9

static void __init zeus_init(void)
{
	u16 dm9000_msc = DM9K_MSC_VALUE;
	u32 msc0, msc1;

	system_rev = __raw_readw(ZEUS_CPLD_VERSION);
	pr_info("Zeus CPLD V%dI%d\n", (system_rev & 0xf0) >> 4, (system_rev & 0x0f));

	/* Fix timings for dm9000s (CS1/CS2)*/
	msc0 = (__raw_readl(MSC0) & 0x0000ffff) | (dm9000_msc << 16);
	msc1 = (__raw_readl(MSC1) & 0xffff0000) | dm9000_msc;
	__raw_writel(msc0, MSC0);
	__raw_writel(msc1, MSC1);

	pm_power_off = zeus_power_off;
	zeus_setup_apm();

	pxa2xx_mfp_config(ARRAY_AND_SIZE(zeus_pin_config));

	platform_add_devices(zeus_devices, ARRAY_SIZE(zeus_devices));

	pxa_set_ohci_info(&zeus_ohci_platform_data);

	if (zeus_setup_fb_gpios())
		pr_err("Failed to setup fb gpios\n");
	else
		pxa_set_fb_info(NULL, &zeus_fb_info);

	pxa_set_mci_info(&zeus_mci_platform_data);
	pxa_set_udc_info(&zeus_udc_info);
	pxa_set_ac97_info(&zeus_ac97_info);
	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(zeus_i2c_devices));
	pxa2xx_set_spi_info(3, &pxa2xx_spi_ssp3_master_info);
	spi_register_board_info(zeus_spi_board_info, ARRAY_SIZE(zeus_spi_board_info));
}

static struct map_desc zeus_io_desc[] __initdata = {
	{
		.virtual = (unsigned long)ZEUS_CPLD_VERSION,
		.pfn     = __phys_to_pfn(ZEUS_CPLD_VERSION_PHYS),
		.length  = 0x1000,
		.type    = MT_DEVICE,
	},
	{
		.virtual = (unsigned long)ZEUS_CPLD_ISA_IRQ,
		.pfn     = __phys_to_pfn(ZEUS_CPLD_ISA_IRQ_PHYS),
		.length  = 0x1000,
		.type    = MT_DEVICE,
	},
	{
		.virtual = (unsigned long)ZEUS_CPLD_CONTROL,
		.pfn     = __phys_to_pfn(ZEUS_CPLD_CONTROL_PHYS),
		.length  = 0x1000,
		.type    = MT_DEVICE,
	},
	{
		.virtual = (unsigned long)ZEUS_PC104IO,
		.pfn     = __phys_to_pfn(ZEUS_PC104IO_PHYS),
		.length  = 0x00800000,
		.type    = MT_DEVICE,
	},
};

static void __init zeus_map_io(void)
{
	pxa27x_map_io();

	iotable_init(zeus_io_desc, ARRAY_SIZE(zeus_io_desc));

	/* Clear PSPR to ensure a full restart on wake-up. */
	PMCR = PSPR = 0;

	/* enable internal 32.768Khz oscillator (ignore OSCC_OOK) */
	OSCC |= OSCC_OON;

	/* Some clock cycles later (from OSCC_ON), programme PCFR (OPDE...).
	 * float chip selects and PCMCIA */
	PCFR = PCFR_OPDE | PCFR_DC_EN | PCFR_FS | PCFR_FP;
}

MACHINE_START(ARCOM_ZEUS, "Arcom/Eurotech ZEUS")
	/* Maintainer: Marc Zyngier <maz@misterjones.org> */
	.atag_offset	= 0x100,
	.map_io		= zeus_map_io,
	.nr_irqs	= ZEUS_NR_IRQS,
	.init_irq	= zeus_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.timer		= &pxa_timer,
	.init_machine	= zeus_init,
	.restart	= pxa_restart,
MACHINE_END

