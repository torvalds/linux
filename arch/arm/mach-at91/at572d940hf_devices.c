/*
 * arch/arm/mach-at91/at572d940hf_devices.c
 *
 * Copyright (C) 2008 Atmel Antonio R. Costa <costa.antonior@gmail.com>
 * Copyright (C) 2005 Thibaut VARENE <varenet@parisc-linux.org>
 * Copyright (C) 2005 David Brownell
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/at572d940hf.h>
#include <mach/at572d940hf_matrix.h>
#include <mach/at91sam9_smc.h>

#include "generic.h"
#include "sam9_smc.h"


/* --------------------------------------------------------------------
 *  USB Host
 * -------------------------------------------------------------------- */

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static u64 ohci_dmamask = DMA_BIT_MASK(32);
static struct at91_usbh_data usbh_data;

static struct resource usbh_resources[] = {
	[0] = {
		.start	= AT572D940HF_UHP_BASE,
		.end	= AT572D940HF_UHP_BASE + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT572D940HF_ID_UHP,
		.end	= AT572D940HF_ID_UHP,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at572d940hf_usbh_device = {
	.name		= "at91_ohci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &ohci_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &usbh_data,
	},
	.resource	= usbh_resources,
	.num_resources	= ARRAY_SIZE(usbh_resources),
};

void __init at91_add_device_usbh(struct at91_usbh_data *data)
{
	if (!data)
		return;

	usbh_data = *data;
	platform_device_register(&at572d940hf_usbh_device);

}
#else
void __init at91_add_device_usbh(struct at91_usbh_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  USB Device (Gadget)
 * -------------------------------------------------------------------- */

#ifdef CONFIG_USB_GADGET_AT91
static struct at91_udc_data udc_data;

static struct resource udc_resources[] = {
	[0] = {
		.start	= AT572D940HF_BASE_UDP,
		.end	= AT572D940HF_BASE_UDP + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT572D940HF_ID_UDP,
		.end	= AT572D940HF_ID_UDP,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at572d940hf_udc_device = {
	.name		= "at91_udc",
	.id		= -1,
	.dev		= {
				.platform_data		= &udc_data,
	},
	.resource	= udc_resources,
	.num_resources	= ARRAY_SIZE(udc_resources),
};

void __init at91_add_device_udc(struct at91_udc_data *data)
{
	if (!data)
		return;

	if (data->vbus_pin) {
		at91_set_gpio_input(data->vbus_pin, 0);
		at91_set_deglitch(data->vbus_pin, 1);
	}

	/* Pullup pin is handled internally */

	udc_data = *data;
	platform_device_register(&at572d940hf_udc_device);
}
#else
void __init at91_add_device_udc(struct at91_udc_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  Ethernet
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MACB) || defined(CONFIG_MACB_MODULE)
static u64 eth_dmamask = DMA_BIT_MASK(32);
static struct at91_eth_data eth_data;

static struct resource eth_resources[] = {
	[0] = {
		.start	= AT572D940HF_BASE_EMAC,
		.end	= AT572D940HF_BASE_EMAC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT572D940HF_ID_EMAC,
		.end	= AT572D940HF_ID_EMAC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at572d940hf_eth_device = {
	.name		= "macb",
	.id		= -1,
	.dev		= {
			.dma_mask		= &eth_dmamask,
			.coherent_dma_mask	= DMA_BIT_MASK(32),
			.platform_data		= &eth_data,
	},
	.resource	= eth_resources,
	.num_resources	= ARRAY_SIZE(eth_resources),
};

void __init at91_add_device_eth(struct at91_eth_data *data)
{
	if (!data)
		return;

	if (data->phy_irq_pin) {
		at91_set_gpio_input(data->phy_irq_pin, 0);
		at91_set_deglitch(data->phy_irq_pin, 1);
	}

	/* Only RMII is supported */
	data->is_rmii = 1;

	/* Pins used for RMII */
	at91_set_A_periph(AT91_PIN_PA16, 0);	/* ETXCK_EREFCK */
	at91_set_A_periph(AT91_PIN_PA17, 0);	/* ERXDV */
	at91_set_A_periph(AT91_PIN_PA18, 0);	/* ERX0 */
	at91_set_A_periph(AT91_PIN_PA19, 0);	/* ERX1 */
	at91_set_A_periph(AT91_PIN_PA20, 0);	/* ERXER */
	at91_set_A_periph(AT91_PIN_PA23, 0);	/* ETXEN */
	at91_set_A_periph(AT91_PIN_PA21, 0);	/* ETX0 */
	at91_set_A_periph(AT91_PIN_PA22, 0);	/* ETX1 */
	at91_set_A_periph(AT91_PIN_PA13, 0);	/* EMDIO */
	at91_set_A_periph(AT91_PIN_PA14, 0);	/* EMDC */

	eth_data = *data;
	platform_device_register(&at572d940hf_eth_device);
}
#else
void __init at91_add_device_eth(struct at91_eth_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  MMC / SD
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MMC_AT91) || defined(CONFIG_MMC_AT91_MODULE)
static u64 mmc_dmamask = DMA_BIT_MASK(32);
static struct at91_mmc_data mmc_data;

static struct resource mmc_resources[] = {
	[0] = {
		.start	= AT572D940HF_BASE_MCI,
		.end	= AT572D940HF_BASE_MCI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT572D940HF_ID_MCI,
		.end	= AT572D940HF_ID_MCI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at572d940hf_mmc_device = {
	.name		= "at91_mci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &mmc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &mmc_data,
	},
	.resource	= mmc_resources,
	.num_resources	= ARRAY_SIZE(mmc_resources),
};

void __init at91_add_device_mmc(short mmc_id, struct at91_mmc_data *data)
{
	if (!data)
		return;

	/* input/irq */
	if (data->det_pin) {
		at91_set_gpio_input(data->det_pin, 1);
		at91_set_deglitch(data->det_pin, 1);
	}
	if (data->wp_pin)
		at91_set_gpio_input(data->wp_pin, 1);
	if (data->vcc_pin)
		at91_set_gpio_output(data->vcc_pin, 0);

	/* CLK */
	at91_set_A_periph(AT91_PIN_PC22, 0);

	/* CMD */
	at91_set_A_periph(AT91_PIN_PC23, 1);

	/* DAT0, maybe DAT1..DAT3 */
	at91_set_A_periph(AT91_PIN_PC24, 1);
	if (data->wire4) {
		at91_set_A_periph(AT91_PIN_PC25, 1);
		at91_set_A_periph(AT91_PIN_PC26, 1);
		at91_set_A_periph(AT91_PIN_PC27, 1);
	}

	mmc_data = *data;
	platform_device_register(&at572d940hf_mmc_device);
}
#else
void __init at91_add_device_mmc(short mmc_id, struct at91_mmc_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  NAND / SmartMedia
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MTD_NAND_ATMEL) || defined(CONFIG_MTD_NAND_ATMEL_MODULE)
static struct atmel_nand_data nand_data;

#define NAND_BASE	AT91_CHIPSELECT_3

static struct resource nand_resources[] = {
	{
		.start	= NAND_BASE,
		.end	= NAND_BASE + SZ_256M - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device at572d940hf_nand_device = {
	.name		= "atmel_nand",
	.id		= -1,
	.dev		= {
				.platform_data	= &nand_data,
	},
	.resource	= nand_resources,
	.num_resources	= ARRAY_SIZE(nand_resources),
};

void __init at91_add_device_nand(struct atmel_nand_data *data)
{
	unsigned long csa;

	if (!data)
		return;

	csa = at91_sys_read(AT91_MATRIX_EBICSA);
	at91_sys_write(AT91_MATRIX_EBICSA, csa | AT91_MATRIX_CS3A_SMC_SMARTMEDIA);

	/* enable pin */
	if (data->enable_pin)
		at91_set_gpio_output(data->enable_pin, 1);

	/* ready/busy pin */
	if (data->rdy_pin)
		at91_set_gpio_input(data->rdy_pin, 1);

	/* card detect pin */
	if (data->det_pin)
		at91_set_gpio_input(data->det_pin, 1);

	at91_set_A_periph(AT91_PIN_PB28, 0);		/* A[22] */
	at91_set_B_periph(AT91_PIN_PA28, 0);		/* NANDOE */
	at91_set_B_periph(AT91_PIN_PA29, 0);		/* NANDWE */

	nand_data = *data;
	platform_device_register(&at572d940hf_nand_device);
}

#else
void __init at91_add_device_nand(struct atmel_nand_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  TWI (i2c)
 * -------------------------------------------------------------------- */

/*
 * Prefer the GPIO code since the TWI controller isn't robust
 * (gets overruns and underruns under load) and can only issue
 * repeated STARTs in one scenario (the driver doesn't yet handle them).
 */

#if defined(CONFIG_I2C_GPIO) || defined(CONFIG_I2C_GPIO_MODULE)

static struct i2c_gpio_platform_data pdata = {
	.sda_pin		= AT91_PIN_PC7,
	.sda_is_open_drain	= 1,
	.scl_pin		= AT91_PIN_PC8,
	.scl_is_open_drain	= 1,
	.udelay			= 2,		/* ~100 kHz */
};

static struct platform_device at572d940hf_twi_device {
	.name			= "i2c-gpio",
	.id			= -1,
	.dev.platform_data	= &pdata,
};

void __init at91_add_device_i2c(struct i2c_board_info *devices, int nr_devices)
{
	at91_set_GPIO_periph(AT91_PIN_PC7, 1);		/* TWD (SDA) */
	at91_set_multi_drive(AT91_PIN_PC7, 1);

	at91_set_GPIO_periph(AT91_PIN_PA8, 1);		/* TWCK (SCL) */
	at91_set_multi_drive(AT91_PIN_PC8, 1);

	i2c_register_board_info(0, devices, nr_devices);
	platform_device_register(&at572d940hf_twi_device);
}

#elif defined(CONFIG_I2C_AT91) || defined(CONFIG_I2C_AT91_MODULE)

static struct resource twi0_resources[] = {
	[0] = {
		.start	= AT572D940HF_BASE_TWI0,
		.end	= AT572D940HF_BASE_TWI0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT572D940HF_ID_TWI0,
		.end	= AT572D940HF_ID_TWI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at572d940hf_twi0_device = {
	.name		= "at91_i2c",
	.id		= 0,
	.resource	= twi0_resources,
	.num_resources	= ARRAY_SIZE(twi0_resources),
};

static struct resource twi1_resources[] = {
	[0] = {
		.start	= AT572D940HF_BASE_TWI1,
		.end	= AT572D940HF_BASE_TWI1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT572D940HF_ID_TWI1,
		.end	= AT572D940HF_ID_TWI1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at572d940hf_twi1_device = {
	.name		= "at91_i2c",
	.id		= 1,
	.resource	= twi1_resources,
	.num_resources	= ARRAY_SIZE(twi1_resources),
};

void __init at91_add_device_i2c(struct i2c_board_info *devices, int nr_devices)
{
	/* pins used for TWI0 interface */
	at91_set_A_periph(AT91_PIN_PC7, 0);		/* TWD */
	at91_set_multi_drive(AT91_PIN_PC7, 1);

	at91_set_A_periph(AT91_PIN_PC8, 0);		/* TWCK */
	at91_set_multi_drive(AT91_PIN_PC8, 1);

	/* pins used for TWI1 interface */
	at91_set_A_periph(AT91_PIN_PC20, 0);		/* TWD */
	at91_set_multi_drive(AT91_PIN_PC20, 1);

	at91_set_A_periph(AT91_PIN_PC21, 0);		/* TWCK */
	at91_set_multi_drive(AT91_PIN_PC21, 1);

	i2c_register_board_info(0, devices, nr_devices);
	platform_device_register(&at572d940hf_twi0_device);
	platform_device_register(&at572d940hf_twi1_device);
}
#else
void __init at91_add_device_i2c(struct i2c_board_info *devices, int nr_devices) {}
#endif


/* --------------------------------------------------------------------
 *  SPI
 * -------------------------------------------------------------------- */

#if defined(CONFIG_SPI_ATMEL) || defined(CONFIG_SPI_ATMEL_MODULE)
static u64 spi_dmamask = DMA_BIT_MASK(32);

static struct resource spi0_resources[] = {
	[0] = {
		.start	= AT572D940HF_BASE_SPI0,
		.end	= AT572D940HF_BASE_SPI0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT572D940HF_ID_SPI0,
		.end	= AT572D940HF_ID_SPI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at572d940hf_spi0_device = {
	.name		= "atmel_spi",
	.id		= 0,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= spi0_resources,
	.num_resources	= ARRAY_SIZE(spi0_resources),
};

static const unsigned spi0_standard_cs[4] = { AT91_PIN_PA3, AT91_PIN_PA4, AT91_PIN_PA5, AT91_PIN_PA6 };

static struct resource spi1_resources[] = {
	[0] = {
		.start	= AT572D940HF_BASE_SPI1,
		.end	= AT572D940HF_BASE_SPI1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT572D940HF_ID_SPI1,
		.end	= AT572D940HF_ID_SPI1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at572d940hf_spi1_device = {
	.name		= "atmel_spi",
	.id		= 1,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= spi1_resources,
	.num_resources	= ARRAY_SIZE(spi1_resources),
};

static const unsigned spi1_standard_cs[4] = { AT91_PIN_PC3, AT91_PIN_PC4, AT91_PIN_PC5, AT91_PIN_PC6 };

void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices)
{
	int i;
	unsigned long cs_pin;
	short enable_spi0 = 0;
	short enable_spi1 = 0;

	/* Choose SPI chip-selects */
	for (i = 0; i < nr_devices; i++) {
		if (devices[i].controller_data)
			cs_pin = (unsigned long) devices[i].controller_data;
		else if (devices[i].bus_num == 0)
			cs_pin = spi0_standard_cs[devices[i].chip_select];
		else
			cs_pin = spi1_standard_cs[devices[i].chip_select];

		if (devices[i].bus_num == 0)
			enable_spi0 = 1;
		else
			enable_spi1 = 1;

		/* enable chip-select pin */
		at91_set_gpio_output(cs_pin, 1);

		/* pass chip-select pin to driver */
		devices[i].controller_data = (void *) cs_pin;
	}

	spi_register_board_info(devices, nr_devices);

	/* Configure SPI bus(es) */
	if (enable_spi0) {
		at91_set_A_periph(AT91_PIN_PA0, 0);	/* SPI0_MISO */
		at91_set_A_periph(AT91_PIN_PA1, 0);	/* SPI0_MOSI */
		at91_set_A_periph(AT91_PIN_PA2, 0);	/* SPI0_SPCK */

		at91_clock_associate("spi0_clk", &at572d940hf_spi0_device.dev, "spi_clk");
		platform_device_register(&at572d940hf_spi0_device);
	}
	if (enable_spi1) {
		at91_set_A_periph(AT91_PIN_PC0, 0);	/* SPI1_MISO */
		at91_set_A_periph(AT91_PIN_PC1, 0);	/* SPI1_MOSI */
		at91_set_A_periph(AT91_PIN_PC2, 0);	/* SPI1_SPCK */

		at91_clock_associate("spi1_clk", &at572d940hf_spi1_device.dev, "spi_clk");
		platform_device_register(&at572d940hf_spi1_device);
	}
}
#else
void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices) {}
#endif


/* --------------------------------------------------------------------
 *  Timer/Counter blocks
 * -------------------------------------------------------------------- */

#ifdef CONFIG_ATMEL_TCLIB

static struct resource tcb_resources[] = {
	[0] = {
		.start	= AT572D940HF_BASE_TCB,
		.end	= AT572D940HF_BASE_TCB + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT572D940HF_ID_TC0,
		.end	= AT572D940HF_ID_TC0,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AT572D940HF_ID_TC1,
		.end	= AT572D940HF_ID_TC1,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= AT572D940HF_ID_TC2,
		.end	= AT572D940HF_ID_TC2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at572d940hf_tcb_device = {
	.name		= "atmel_tcb",
	.id		= 0,
	.resource	= tcb_resources,
	.num_resources	= ARRAY_SIZE(tcb_resources),
};

static void __init at91_add_device_tc(void)
{
	/* this chip has a separate clock and irq for each TC channel */
	at91_clock_associate("tc0_clk", &at572d940hf_tcb_device.dev, "t0_clk");
	at91_clock_associate("tc1_clk", &at572d940hf_tcb_device.dev, "t1_clk");
	at91_clock_associate("tc2_clk", &at572d940hf_tcb_device.dev, "t2_clk");
	platform_device_register(&at572d940hf_tcb_device);
}
#else
static void __init at91_add_device_tc(void) { }
#endif


/* --------------------------------------------------------------------
 *  RTT
 * -------------------------------------------------------------------- */

static struct resource rtt_resources[] = {
	{
		.start	= AT91_BASE_SYS + AT91_RTT,
		.end	= AT91_BASE_SYS + AT91_RTT + SZ_16 - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device at572d940hf_rtt_device = {
	.name		= "at91_rtt",
	.id		= 0,
	.resource	= rtt_resources,
	.num_resources	= ARRAY_SIZE(rtt_resources),
};

static void __init at91_add_device_rtt(void)
{
	platform_device_register(&at572d940hf_rtt_device);
}


/* --------------------------------------------------------------------
 *  Watchdog
 * -------------------------------------------------------------------- */

#if defined(CONFIG_AT91SAM9X_WATCHDOG) || defined(CONFIG_AT91SAM9X_WATCHDOG_MODULE)
static struct platform_device at572d940hf_wdt_device = {
	.name		= "at91_wdt",
	.id		= -1,
	.num_resources	= 0,
};

static void __init at91_add_device_watchdog(void)
{
	platform_device_register(&at572d940hf_wdt_device);
}
#else
static void __init at91_add_device_watchdog(void) {}
#endif


/* --------------------------------------------------------------------
 *  UART
 * -------------------------------------------------------------------- */

#if defined(CONFIG_SERIAL_ATMEL)
static struct resource dbgu_resources[] = {
	[0] = {
		.start	= AT91_VA_BASE_SYS + AT91_DBGU,
		.end	= AT91_VA_BASE_SYS + AT91_DBGU + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91_ID_SYS,
		.end	= AT91_ID_SYS,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data dbgu_data = {
	.use_dma_tx	= 0,
	.use_dma_rx	= 0,		/* DBGU not capable of receive DMA */
	.regs		= (void __iomem *)(AT91_VA_BASE_SYS + AT91_DBGU),
};

static u64 dbgu_dmamask = DMA_BIT_MASK(32);

static struct platform_device at572d940hf_dbgu_device = {
	.name		= "atmel_usart",
	.id		= 0,
	.dev		= {
				.dma_mask		= &dbgu_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &dbgu_data,
	},
	.resource	= dbgu_resources,
	.num_resources	= ARRAY_SIZE(dbgu_resources),
};

static inline void configure_dbgu_pins(void)
{
	at91_set_A_periph(AT91_PIN_PC31, 1);		/* DTXD */
	at91_set_A_periph(AT91_PIN_PC30, 0);		/* DRXD */
}

static struct resource uart0_resources[] = {
	[0] = {
		.start	= AT572D940HF_BASE_US0,
		.end	= AT572D940HF_BASE_US0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT572D940HF_ID_US0,
		.end	= AT572D940HF_ID_US0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart0_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart0_dmamask = DMA_BIT_MASK(32);

static struct platform_device at572d940hf_uart0_device = {
	.name		= "atmel_usart",
	.id		= 1,
	.dev		= {
				.dma_mask		= &uart0_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart0_data,
	},
	.resource	= uart0_resources,
	.num_resources	= ARRAY_SIZE(uart0_resources),
};

static inline void configure_usart0_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PA8, 1);		/* TXD0 */
	at91_set_A_periph(AT91_PIN_PA7, 0);		/* RXD0 */

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PA10, 0);	/* RTS0 */
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PA9, 0);	/* CTS0 */
}

static struct resource uart1_resources[] = {
	[0] = {
		.start	= AT572D940HF_BASE_US1,
		.end	= AT572D940HF_BASE_US1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT572D940HF_ID_US1,
		.end	= AT572D940HF_ID_US1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart1_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart1_dmamask = DMA_BIT_MASK(32);

static struct platform_device at572d940hf_uart1_device = {
	.name		= "atmel_usart",
	.id		= 2,
	.dev		= {
				.dma_mask		= &uart1_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart1_data,
	},
	.resource	= uart1_resources,
	.num_resources	= ARRAY_SIZE(uart1_resources),
};

static inline void configure_usart1_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PC10, 1);		/* TXD1 */
	at91_set_A_periph(AT91_PIN_PC9 , 0);		/* RXD1 */

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PC12, 0);	/* RTS1 */
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PC11, 0);	/* CTS1 */
}

static struct resource uart2_resources[] = {
	[0] = {
		.start	= AT572D940HF_BASE_US2,
		.end	= AT572D940HF_BASE_US2 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT572D940HF_ID_US2,
		.end	= AT572D940HF_ID_US2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart2_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart2_dmamask = DMA_BIT_MASK(32);

static struct platform_device at572d940hf_uart2_device = {
	.name		= "atmel_usart",
	.id		= 3,
	.dev		= {
				.dma_mask		= &uart2_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart2_data,
	},
	.resource	= uart2_resources,
	.num_resources	= ARRAY_SIZE(uart2_resources),
};

static inline void configure_usart2_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PC15, 1);		/* TXD2 */
	at91_set_A_periph(AT91_PIN_PC14, 0);		/* RXD2 */

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PC17, 0);	/* RTS2 */
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PC16, 0);	/* CTS2 */
}

static struct platform_device *__initdata at91_uarts[ATMEL_MAX_UART];	/* the UARTs to use */
struct platform_device *atmel_default_console_device;	/* the serial console device */

void __init at91_register_uart(unsigned id, unsigned portnr, unsigned pins)
{
	struct platform_device *pdev;

	switch (id) {
		case 0:		/* DBGU */
			pdev = &at572d940hf_dbgu_device;
			configure_dbgu_pins();
			at91_clock_associate("mck", &pdev->dev, "usart");
			break;
		case AT572D940HF_ID_US0:
			pdev = &at572d940hf_uart0_device;
			configure_usart0_pins(pins);
			at91_clock_associate("usart0_clk", &pdev->dev, "usart");
			break;
		case AT572D940HF_ID_US1:
			pdev = &at572d940hf_uart1_device;
			configure_usart1_pins(pins);
			at91_clock_associate("usart1_clk", &pdev->dev, "usart");
			break;
		case AT572D940HF_ID_US2:
			pdev = &at572d940hf_uart2_device;
			configure_usart2_pins(pins);
			at91_clock_associate("usart2_clk", &pdev->dev, "usart");
			break;
		default:
			return;
	}
	pdev->id = portnr;		/* update to mapped ID */

	if (portnr < ATMEL_MAX_UART)
		at91_uarts[portnr] = pdev;
}

void __init at91_set_serial_console(unsigned portnr)
{
	if (portnr < ATMEL_MAX_UART)
		atmel_default_console_device = at91_uarts[portnr];
}

void __init at91_add_device_serial(void)
{
	int i;

	for (i = 0; i < ATMEL_MAX_UART; i++) {
		if (at91_uarts[i])
			platform_device_register(at91_uarts[i]);
	}

	if (!atmel_default_console_device)
		printk(KERN_INFO "AT91: No default serial console defined.\n");
}

#else
void __init at91_register_uart(unsigned id, unsigned portnr, unsigned pins) {}
void __init at91_set_serial_console(unsigned portnr) {}
void __init at91_add_device_serial(void) {}
#endif


/* --------------------------------------------------------------------
 *  mAgic
 * -------------------------------------------------------------------- */

#ifdef CONFIG_MAGICV
static struct resource mAgic_resources[] = {
	{
		.start = AT91_MAGIC_PM_BASE,
		.end   = AT91_MAGIC_PM_BASE + AT91_MAGIC_PM_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = AT91_MAGIC_DM_I_BASE,
		.end   = AT91_MAGIC_DM_I_BASE + AT91_MAGIC_DM_I_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = AT91_MAGIC_DM_F_BASE,
		.end   = AT91_MAGIC_DM_F_BASE + AT91_MAGIC_DM_F_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = AT91_MAGIC_DM_DB_BASE,
		.end   = AT91_MAGIC_DM_DB_BASE + AT91_MAGIC_DM_DB_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = AT91_MAGIC_REGS_BASE,
		.end   = AT91_MAGIC_REGS_BASE + AT91_MAGIC_REGS_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = AT91_MAGIC_EXTPAGE_BASE,
		.end   = AT91_MAGIC_EXTPAGE_BASE + AT91_MAGIC_EXTPAGE_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start  = AT572D940HF_ID_MSIRQ0,
		.end    = AT572D940HF_ID_MSIRQ0,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.start  = AT572D940HF_ID_MHALT,
		.end    = AT572D940HF_ID_MHALT,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.start  = AT572D940HF_ID_MEXC,
		.end    = AT572D940HF_ID_MEXC,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.start  = AT572D940HF_ID_MEDMA,
		.end    = AT572D940HF_ID_MEDMA,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device mAgic_device = {
	.name           = "mAgic",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(mAgic_resources),
	.resource       = mAgic_resources,
};

void __init at91_add_device_mAgic(void)
{
	platform_device_register(&mAgic_device);
}
#else
void __init at91_add_device_mAgic(void) {}
#endif


/* -------------------------------------------------------------------- */

/*
 * These devices are always present and don't need any board-specific
 * setup.
 */
static int __init at91_add_standard_devices(void)
{
	at91_add_device_rtt();
	at91_add_device_watchdog();
	at91_add_device_tc();
	return 0;
}

arch_initcall(at91_add_standard_devices);
