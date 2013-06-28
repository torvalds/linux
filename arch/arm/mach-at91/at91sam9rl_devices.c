/*
 *  Copyright (C) 2007 Atmel Corporation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/i2c-gpio.h>

#include <linux/fb.h>
#include <video/atmel_lcdc.h>

#include <mach/at91sam9rl.h>
#include <mach/at91sam9rl_matrix.h>
#include <mach/at91_matrix.h>
#include <mach/at91sam9_smc.h>
#include <linux/platform_data/dma-atmel.h>

#include "board.h"
#include "generic.h"


/* --------------------------------------------------------------------
 *  HDMAC - AHB DMA Controller
 * -------------------------------------------------------------------- */

#if defined(CONFIG_AT_HDMAC) || defined(CONFIG_AT_HDMAC_MODULE)
static u64 hdmac_dmamask = DMA_BIT_MASK(32);

static struct resource hdmac_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_DMA,
		.end	= AT91SAM9RL_BASE_DMA + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_DMA,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_DMA,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at_hdmac_device = {
	.name		= "at91sam9rl_dma",
	.id		= -1,
	.dev		= {
				.dma_mask		= &hdmac_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= hdmac_resources,
	.num_resources	= ARRAY_SIZE(hdmac_resources),
};

void __init at91_add_device_hdmac(void)
{
	platform_device_register(&at_hdmac_device);
}
#else
void __init at91_add_device_hdmac(void) {}
#endif

/* --------------------------------------------------------------------
 *  USB HS Device (Gadget)
 * -------------------------------------------------------------------- */

#if defined(CONFIG_USB_ATMEL_USBA) || defined(CONFIG_USB_ATMEL_USBA_MODULE)

static struct resource usba_udc_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_UDPHS_FIFO,
		.end	= AT91SAM9RL_UDPHS_FIFO + SZ_512K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9RL_BASE_UDPHS,
		.end	= AT91SAM9RL_BASE_UDPHS + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_UDPHS,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_UDPHS,
		.flags	= IORESOURCE_IRQ,
	},
};

#define EP(nam, idx, maxpkt, maxbk, dma, isoc)			\
	[idx] = {						\
		.name		= nam,				\
		.index		= idx,				\
		.fifo_size	= maxpkt,			\
		.nr_banks	= maxbk,			\
		.can_dma	= dma,				\
		.can_isoc	= isoc,				\
	}

static struct usba_ep_data usba_udc_ep[] __initdata = {
	EP("ep0", 0, 64, 1, 0, 0),
	EP("ep1", 1, 1024, 2, 1, 1),
	EP("ep2", 2, 1024, 2, 1, 1),
	EP("ep3", 3, 1024, 3, 1, 0),
	EP("ep4", 4, 1024, 3, 1, 0),
	EP("ep5", 5, 1024, 3, 1, 1),
	EP("ep6", 6, 1024, 3, 1, 1),
};

#undef EP

/*
 * pdata doesn't have room for any endpoints, so we need to
 * append room for the ones we need right after it.
 */
static struct {
	struct usba_platform_data pdata;
	struct usba_ep_data ep[7];
} usba_udc_data;

static struct platform_device at91_usba_udc_device = {
	.name		= "atmel_usba_udc",
	.id		= -1,
	.dev		= {
				.platform_data	= &usba_udc_data.pdata,
	},
	.resource	= usba_udc_resources,
	.num_resources	= ARRAY_SIZE(usba_udc_resources),
};

void __init at91_add_device_usba(struct usba_platform_data *data)
{
	/*
	 * Invalid pins are 0 on AT91, but the usba driver is shared
	 * with AVR32, which use negative values instead. Once/if
	 * gpio_is_valid() is ported to AT91, revisit this code.
	 */
	usba_udc_data.pdata.vbus_pin = -EINVAL;
	usba_udc_data.pdata.num_ep = ARRAY_SIZE(usba_udc_ep);
	memcpy(usba_udc_data.ep, usba_udc_ep, sizeof(usba_udc_ep));

	if (data && gpio_is_valid(data->vbus_pin)) {
		at91_set_gpio_input(data->vbus_pin, 0);
		at91_set_deglitch(data->vbus_pin, 1);
		usba_udc_data.pdata.vbus_pin = data->vbus_pin;
	}

	/* Pullup pin is handled internally by USB device peripheral */

	platform_device_register(&at91_usba_udc_device);
}
#else
void __init at91_add_device_usba(struct usba_platform_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  MMC / SD
 * -------------------------------------------------------------------- */

#if IS_ENABLED(CONFIG_MMC_ATMELMCI)
static u64 mmc_dmamask = DMA_BIT_MASK(32);
static struct mci_platform_data mmc_data;

static struct resource mmc_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_MCI,
		.end	= AT91SAM9RL_BASE_MCI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_MCI,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_MCI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9rl_mmc_device = {
	.name		= "atmel_mci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &mmc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &mmc_data,
	},
	.resource	= mmc_resources,
	.num_resources	= ARRAY_SIZE(mmc_resources),
};

void __init at91_add_device_mci(short mmc_id, struct mci_platform_data *data)
{
	if (!data)
		return;

	if (data->slot[0].bus_width) {
		/* input/irq */
		if (gpio_is_valid(data->slot[0].detect_pin)) {
			at91_set_gpio_input(data->slot[0].detect_pin, 1);
			at91_set_deglitch(data->slot[0].detect_pin, 1);
		}
		if (gpio_is_valid(data->slot[0].wp_pin))
			at91_set_gpio_input(data->slot[0].wp_pin, 1);

		/* CLK */
		at91_set_A_periph(AT91_PIN_PA2, 0);

		/* CMD */
		at91_set_A_periph(AT91_PIN_PA1, 1);

		/* DAT0, maybe DAT1..DAT3 */
		at91_set_A_periph(AT91_PIN_PA0, 1);
		if (data->slot[0].bus_width == 4) {
			at91_set_A_periph(AT91_PIN_PA3, 1);
			at91_set_A_periph(AT91_PIN_PA4, 1);
			at91_set_A_periph(AT91_PIN_PA5, 1);
		}

		mmc_data = *data;
		platform_device_register(&at91sam9rl_mmc_device);
	}
}
#else
void __init at91_add_device_mci(short mmc_id, struct mci_platform_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  NAND / SmartMedia
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MTD_NAND_ATMEL) || defined(CONFIG_MTD_NAND_ATMEL_MODULE)
static struct atmel_nand_data nand_data;

#define NAND_BASE	AT91_CHIPSELECT_3

static struct resource nand_resources[] = {
	[0] = {
		.start	= NAND_BASE,
		.end	= NAND_BASE + SZ_256M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9RL_BASE_ECC,
		.end	= AT91SAM9RL_BASE_ECC + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device atmel_nand_device = {
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

	csa = at91_matrix_read(AT91_MATRIX_EBICSA);
	at91_matrix_write(AT91_MATRIX_EBICSA, csa | AT91_MATRIX_CS3A_SMC_SMARTMEDIA);

	/* enable pin */
	if (gpio_is_valid(data->enable_pin))
		at91_set_gpio_output(data->enable_pin, 1);

	/* ready/busy pin */
	if (gpio_is_valid(data->rdy_pin))
		at91_set_gpio_input(data->rdy_pin, 1);

	/* card detect pin */
	if (gpio_is_valid(data->det_pin))
		at91_set_gpio_input(data->det_pin, 1);

	at91_set_A_periph(AT91_PIN_PB4, 0);		/* NANDOE */
	at91_set_A_periph(AT91_PIN_PB5, 0);		/* NANDWE */

	nand_data = *data;
	platform_device_register(&atmel_nand_device);
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
	.sda_pin		= AT91_PIN_PA23,
	.sda_is_open_drain	= 1,
	.scl_pin		= AT91_PIN_PA24,
	.scl_is_open_drain	= 1,
	.udelay			= 2,		/* ~100 kHz */
};

static struct platform_device at91sam9rl_twi_device = {
	.name			= "i2c-gpio",
	.id			= 0,
	.dev.platform_data	= &pdata,
};

void __init at91_add_device_i2c(struct i2c_board_info *devices, int nr_devices)
{
	at91_set_GPIO_periph(AT91_PIN_PA23, 1);		/* TWD (SDA) */
	at91_set_multi_drive(AT91_PIN_PA23, 1);

	at91_set_GPIO_periph(AT91_PIN_PA24, 1);		/* TWCK (SCL) */
	at91_set_multi_drive(AT91_PIN_PA24, 1);

	i2c_register_board_info(0, devices, nr_devices);
	platform_device_register(&at91sam9rl_twi_device);
}

#elif defined(CONFIG_I2C_AT91) || defined(CONFIG_I2C_AT91_MODULE)

static struct resource twi_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_TWI0,
		.end	= AT91SAM9RL_BASE_TWI0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_TWI0,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_TWI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9rl_twi_device = {
	.name		= "i2c-at91sam9g20",
	.id		= 0,
	.resource	= twi_resources,
	.num_resources	= ARRAY_SIZE(twi_resources),
};

void __init at91_add_device_i2c(struct i2c_board_info *devices, int nr_devices)
{
	/* pins used for TWI interface */
	at91_set_A_periph(AT91_PIN_PA23, 0);		/* TWD */
	at91_set_multi_drive(AT91_PIN_PA23, 1);

	at91_set_A_periph(AT91_PIN_PA24, 0);		/* TWCK */
	at91_set_multi_drive(AT91_PIN_PA24, 1);

	i2c_register_board_info(0, devices, nr_devices);
	platform_device_register(&at91sam9rl_twi_device);
}
#else
void __init at91_add_device_i2c(struct i2c_board_info *devices, int nr_devices) {}
#endif


/* --------------------------------------------------------------------
 *  SPI
 * -------------------------------------------------------------------- */

#if defined(CONFIG_SPI_ATMEL) || defined(CONFIG_SPI_ATMEL_MODULE)
static u64 spi_dmamask = DMA_BIT_MASK(32);

static struct resource spi_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_SPI,
		.end	= AT91SAM9RL_BASE_SPI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_SPI,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_SPI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9rl_spi_device = {
	.name		= "atmel_spi",
	.id		= 0,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= spi_resources,
	.num_resources	= ARRAY_SIZE(spi_resources),
};

static const unsigned spi_standard_cs[4] = { AT91_PIN_PA28, AT91_PIN_PB7, AT91_PIN_PD8, AT91_PIN_PD9 };


void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices)
{
	int i;
	unsigned long cs_pin;

	at91_set_A_periph(AT91_PIN_PA25, 0);	/* MISO */
	at91_set_A_periph(AT91_PIN_PA26, 0);	/* MOSI */
	at91_set_A_periph(AT91_PIN_PA27, 0);	/* SPCK */

	/* Enable SPI chip-selects */
	for (i = 0; i < nr_devices; i++) {
		if (devices[i].controller_data)
			cs_pin = (unsigned long) devices[i].controller_data;
		else
			cs_pin = spi_standard_cs[devices[i].chip_select];

		if (!gpio_is_valid(cs_pin))
			continue;

		/* enable chip-select pin */
		at91_set_gpio_output(cs_pin, 1);

		/* pass chip-select pin to driver */
		devices[i].controller_data = (void *) cs_pin;
	}

	spi_register_board_info(devices, nr_devices);
	platform_device_register(&at91sam9rl_spi_device);
}
#else
void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices) {}
#endif


/* --------------------------------------------------------------------
 *  AC97
 * -------------------------------------------------------------------- */

#if defined(CONFIG_SND_ATMEL_AC97C) || defined(CONFIG_SND_ATMEL_AC97C_MODULE)
static u64 ac97_dmamask = DMA_BIT_MASK(32);
static struct ac97c_platform_data ac97_data;

static struct resource ac97_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_AC97C,
		.end	= AT91SAM9RL_BASE_AC97C + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_AC97C,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_AC97C,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9rl_ac97_device = {
	.name		= "atmel_ac97c",
	.id		= 0,
	.dev		= {
				.dma_mask		= &ac97_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &ac97_data,
	},
	.resource	= ac97_resources,
	.num_resources	= ARRAY_SIZE(ac97_resources),
};

void __init at91_add_device_ac97(struct ac97c_platform_data *data)
{
	if (!data)
		return;

	at91_set_A_periph(AT91_PIN_PD1, 0);	/* AC97FS */
	at91_set_A_periph(AT91_PIN_PD2, 0);	/* AC97CK */
	at91_set_A_periph(AT91_PIN_PD3, 0);	/* AC97TX */
	at91_set_A_periph(AT91_PIN_PD4, 0);	/* AC97RX */

	/* reset */
	if (gpio_is_valid(data->reset_pin))
		at91_set_gpio_output(data->reset_pin, 0);

	ac97_data = *data;
	platform_device_register(&at91sam9rl_ac97_device);
}
#else
void __init at91_add_device_ac97(struct ac97c_platform_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  LCD Controller
 * -------------------------------------------------------------------- */

#if defined(CONFIG_FB_ATMEL) || defined(CONFIG_FB_ATMEL_MODULE)
static u64 lcdc_dmamask = DMA_BIT_MASK(32);
static struct atmel_lcdfb_info lcdc_data;

static struct resource lcdc_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_LCDC_BASE,
		.end	= AT91SAM9RL_LCDC_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_LCDC,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_LCDC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91_lcdc_device = {
	.name		= "at91sam9rl-lcdfb",
	.id		= 0,
	.dev		= {
				.dma_mask		= &lcdc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &lcdc_data,
	},
	.resource	= lcdc_resources,
	.num_resources	= ARRAY_SIZE(lcdc_resources),
};

void __init at91_add_device_lcdc(struct atmel_lcdfb_info *data)
{
	if (!data) {
		return;
	}

	at91_set_B_periph(AT91_PIN_PC1, 0);	/* LCDPWR */
	at91_set_A_periph(AT91_PIN_PC5, 0);	/* LCDHSYNC */
	at91_set_A_periph(AT91_PIN_PC6, 0);	/* LCDDOTCK */
	at91_set_A_periph(AT91_PIN_PC7, 0);	/* LCDDEN */
	at91_set_A_periph(AT91_PIN_PC3, 0);	/* LCDCC */
	at91_set_B_periph(AT91_PIN_PC9, 0);	/* LCDD3 */
	at91_set_B_periph(AT91_PIN_PC10, 0);	/* LCDD4 */
	at91_set_B_periph(AT91_PIN_PC11, 0);	/* LCDD5 */
	at91_set_B_periph(AT91_PIN_PC12, 0);	/* LCDD6 */
	at91_set_B_periph(AT91_PIN_PC13, 0);	/* LCDD7 */
	at91_set_B_periph(AT91_PIN_PC15, 0);	/* LCDD11 */
	at91_set_B_periph(AT91_PIN_PC16, 0);	/* LCDD12 */
	at91_set_B_periph(AT91_PIN_PC17, 0);	/* LCDD13 */
	at91_set_B_periph(AT91_PIN_PC18, 0);	/* LCDD14 */
	at91_set_B_periph(AT91_PIN_PC19, 0);	/* LCDD15 */
	at91_set_B_periph(AT91_PIN_PC20, 0);	/* LCDD18 */
	at91_set_B_periph(AT91_PIN_PC21, 0);	/* LCDD19 */
	at91_set_B_periph(AT91_PIN_PC22, 0);	/* LCDD20 */
	at91_set_B_periph(AT91_PIN_PC23, 0);	/* LCDD21 */
	at91_set_B_periph(AT91_PIN_PC24, 0);	/* LCDD22 */
	at91_set_B_periph(AT91_PIN_PC25, 0);	/* LCDD23 */

	lcdc_data = *data;
	platform_device_register(&at91_lcdc_device);
}
#else
void __init at91_add_device_lcdc(struct atmel_lcdfb_info *data) {}
#endif


/* --------------------------------------------------------------------
 *  Timer/Counter block
 * -------------------------------------------------------------------- */

#ifdef CONFIG_ATMEL_TCLIB

static struct resource tcb_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_TCB0,
		.end	= AT91SAM9RL_BASE_TCB0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_TC0,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_TC0,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_TC1,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_TC1,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_TC2,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_TC2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9rl_tcb_device = {
	.name		= "atmel_tcb",
	.id		= 0,
	.resource	= tcb_resources,
	.num_resources	= ARRAY_SIZE(tcb_resources),
};

static void __init at91_add_device_tc(void)
{
	platform_device_register(&at91sam9rl_tcb_device);
}
#else
static void __init at91_add_device_tc(void) { }
#endif


/* --------------------------------------------------------------------
 *  Touchscreen
 * -------------------------------------------------------------------- */

#if defined(CONFIG_TOUCHSCREEN_ATMEL_TSADCC) || defined(CONFIG_TOUCHSCREEN_ATMEL_TSADCC_MODULE)
static u64 tsadcc_dmamask = DMA_BIT_MASK(32);
static struct at91_tsadcc_data tsadcc_data;

static struct resource tsadcc_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_TSC,
		.end	= AT91SAM9RL_BASE_TSC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_TSC,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_TSC,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device at91sam9rl_tsadcc_device = {
	.name		= "atmel_tsadcc",
	.id		= -1,
	.dev		= {
				.dma_mask		= &tsadcc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &tsadcc_data,
	},
	.resource	= tsadcc_resources,
	.num_resources	= ARRAY_SIZE(tsadcc_resources),
};

void __init at91_add_device_tsadcc(struct at91_tsadcc_data *data)
{
	if (!data)
		return;

	at91_set_A_periph(AT91_PIN_PA17, 0);	/* AD0_XR */
	at91_set_A_periph(AT91_PIN_PA18, 0);	/* AD1_XL */
	at91_set_A_periph(AT91_PIN_PA19, 0);	/* AD2_YT */
	at91_set_A_periph(AT91_PIN_PA20, 0);	/* AD3_TB */

	tsadcc_data = *data;
	platform_device_register(&at91sam9rl_tsadcc_device);
}
#else
void __init at91_add_device_tsadcc(struct at91_tsadcc_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  RTC
 * -------------------------------------------------------------------- */

#if defined(CONFIG_RTC_DRV_AT91RM9200) || defined(CONFIG_RTC_DRV_AT91RM9200_MODULE)
static struct platform_device at91sam9rl_rtc_device = {
	.name		= "at91_rtc",
	.id		= -1,
	.num_resources	= 0,
};

static void __init at91_add_device_rtc(void)
{
	platform_device_register(&at91sam9rl_rtc_device);
}
#else
static void __init at91_add_device_rtc(void) {}
#endif


/* --------------------------------------------------------------------
 *  RTT
 * -------------------------------------------------------------------- */

static struct resource rtt_resources[] = {
	{
		.start	= AT91SAM9RL_BASE_RTT,
		.end	= AT91SAM9RL_BASE_RTT + SZ_16 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.flags	= IORESOURCE_MEM,
	}, {
		.flags  = IORESOURCE_IRQ,
	}
};

static struct platform_device at91sam9rl_rtt_device = {
	.name		= "at91_rtt",
	.id		= 0,
	.resource	= rtt_resources,
};

#if IS_ENABLED(CONFIG_RTC_DRV_AT91SAM9)
static void __init at91_add_device_rtt_rtc(void)
{
	at91sam9rl_rtt_device.name = "rtc-at91sam9";
	/*
	 * The second resource is needed:
	 * GPBR will serve as the storage for RTC time offset
	 */
	at91sam9rl_rtt_device.num_resources = 3;
	rtt_resources[1].start = AT91SAM9RL_BASE_GPBR +
				 4 * CONFIG_RTC_DRV_AT91SAM9_GPBR;
	rtt_resources[1].end = rtt_resources[1].start + 3;
	rtt_resources[2].start = NR_IRQS_LEGACY + AT91_ID_SYS;
	rtt_resources[2].end = NR_IRQS_LEGACY + AT91_ID_SYS;
}
#else
static void __init at91_add_device_rtt_rtc(void)
{
	/* Only one resource is needed: RTT not used as RTC */
	at91sam9rl_rtt_device.num_resources = 1;
}
#endif

static void __init at91_add_device_rtt(void)
{
	at91_add_device_rtt_rtc();
	platform_device_register(&at91sam9rl_rtt_device);
}


/* --------------------------------------------------------------------
 *  Watchdog
 * -------------------------------------------------------------------- */

#if defined(CONFIG_AT91SAM9X_WATCHDOG) || defined(CONFIG_AT91SAM9X_WATCHDOG_MODULE)
static struct resource wdt_resources[] = {
	{
		.start	= AT91SAM9RL_BASE_WDT,
		.end	= AT91SAM9RL_BASE_WDT + SZ_16 - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device at91sam9rl_wdt_device = {
	.name		= "at91_wdt",
	.id		= -1,
	.resource	= wdt_resources,
	.num_resources	= ARRAY_SIZE(wdt_resources),
};

static void __init at91_add_device_watchdog(void)
{
	platform_device_register(&at91sam9rl_wdt_device);
}
#else
static void __init at91_add_device_watchdog(void) {}
#endif


/* --------------------------------------------------------------------
 *  PWM
 * --------------------------------------------------------------------*/

#if defined(CONFIG_ATMEL_PWM)
static u32 pwm_mask;

static struct resource pwm_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_PWMC,
		.end	= AT91SAM9RL_BASE_PWMC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_PWMC,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_PWMC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9rl_pwm0_device = {
	.name	= "atmel_pwm",
	.id	= -1,
	.dev	= {
		.platform_data		= &pwm_mask,
	},
	.resource	= pwm_resources,
	.num_resources	= ARRAY_SIZE(pwm_resources),
};

void __init at91_add_device_pwm(u32 mask)
{
	if (mask & (1 << AT91_PWM0))
		at91_set_B_periph(AT91_PIN_PB8, 1);	/* enable PWM0 */

	if (mask & (1 << AT91_PWM1))
		at91_set_B_periph(AT91_PIN_PB9, 1);	/* enable PWM1 */

	if (mask & (1 << AT91_PWM2))
		at91_set_B_periph(AT91_PIN_PD5, 1);	/* enable PWM2 */

	if (mask & (1 << AT91_PWM3))
		at91_set_B_periph(AT91_PIN_PD8, 1);	/* enable PWM3 */

	pwm_mask = mask;

	platform_device_register(&at91sam9rl_pwm0_device);
}
#else
void __init at91_add_device_pwm(u32 mask) {}
#endif


/* --------------------------------------------------------------------
 *  SSC -- Synchronous Serial Controller
 * -------------------------------------------------------------------- */

#if defined(CONFIG_ATMEL_SSC) || defined(CONFIG_ATMEL_SSC_MODULE)
static u64 ssc0_dmamask = DMA_BIT_MASK(32);

static struct resource ssc0_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_SSC0,
		.end	= AT91SAM9RL_BASE_SSC0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_SSC0,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_SSC0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9rl_ssc0_device = {
	.name	= "at91rm9200_ssc",
	.id	= 0,
	.dev	= {
		.dma_mask		= &ssc0_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= ssc0_resources,
	.num_resources	= ARRAY_SIZE(ssc0_resources),
};

static inline void configure_ssc0_pins(unsigned pins)
{
	if (pins & ATMEL_SSC_TF)
		at91_set_A_periph(AT91_PIN_PC0, 1);
	if (pins & ATMEL_SSC_TK)
		at91_set_A_periph(AT91_PIN_PC1, 1);
	if (pins & ATMEL_SSC_TD)
		at91_set_A_periph(AT91_PIN_PA15, 1);
	if (pins & ATMEL_SSC_RD)
		at91_set_A_periph(AT91_PIN_PA16, 1);
	if (pins & ATMEL_SSC_RK)
		at91_set_B_periph(AT91_PIN_PA10, 1);
	if (pins & ATMEL_SSC_RF)
		at91_set_B_periph(AT91_PIN_PA22, 1);
}

static u64 ssc1_dmamask = DMA_BIT_MASK(32);

static struct resource ssc1_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_SSC1,
		.end	= AT91SAM9RL_BASE_SSC1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_SSC1,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_SSC1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9rl_ssc1_device = {
	.name	= "at91rm9200_ssc",
	.id	= 1,
	.dev	= {
		.dma_mask		= &ssc1_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= ssc1_resources,
	.num_resources	= ARRAY_SIZE(ssc1_resources),
};

static inline void configure_ssc1_pins(unsigned pins)
{
	if (pins & ATMEL_SSC_TF)
		at91_set_B_periph(AT91_PIN_PA29, 1);
	if (pins & ATMEL_SSC_TK)
		at91_set_B_periph(AT91_PIN_PA30, 1);
	if (pins & ATMEL_SSC_TD)
		at91_set_B_periph(AT91_PIN_PA13, 1);
	if (pins & ATMEL_SSC_RD)
		at91_set_B_periph(AT91_PIN_PA14, 1);
	if (pins & ATMEL_SSC_RK)
		at91_set_B_periph(AT91_PIN_PA9, 1);
	if (pins & ATMEL_SSC_RF)
		at91_set_B_periph(AT91_PIN_PA8, 1);
}

/*
 * SSC controllers are accessed through library code, instead of any
 * kind of all-singing/all-dancing driver.  For example one could be
 * used by a particular I2S audio codec's driver, while another one
 * on the same system might be used by a custom data capture driver.
 */
void __init at91_add_device_ssc(unsigned id, unsigned pins)
{
	struct platform_device *pdev;

	/*
	 * NOTE: caller is responsible for passing information matching
	 * "pins" to whatever will be using each particular controller.
	 */
	switch (id) {
	case AT91SAM9RL_ID_SSC0:
		pdev = &at91sam9rl_ssc0_device;
		configure_ssc0_pins(pins);
		break;
	case AT91SAM9RL_ID_SSC1:
		pdev = &at91sam9rl_ssc1_device;
		configure_ssc1_pins(pins);
		break;
	default:
		return;
	}

	platform_device_register(pdev);
}

#else
void __init at91_add_device_ssc(unsigned id, unsigned pins) {}
#endif


/* --------------------------------------------------------------------
 *  UART
 * -------------------------------------------------------------------- */

#if defined(CONFIG_SERIAL_ATMEL)
static struct resource dbgu_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_DBGU,
		.end	= AT91SAM9RL_BASE_DBGU + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91_ID_SYS,
		.end	= NR_IRQS_LEGACY + AT91_ID_SYS,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data dbgu_data = {
	.use_dma_tx	= 0,
	.use_dma_rx	= 0,		/* DBGU not capable of receive DMA */
};

static u64 dbgu_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9rl_dbgu_device = {
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
	at91_set_A_periph(AT91_PIN_PA21, 0);		/* DRXD */
	at91_set_A_periph(AT91_PIN_PA22, 1);		/* DTXD */
}

static struct resource uart0_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_US0,
		.end	= AT91SAM9RL_BASE_US0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_US0,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_US0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart0_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart0_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9rl_uart0_device = {
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
	at91_set_A_periph(AT91_PIN_PA6, 1);		/* TXD0 */
	at91_set_A_periph(AT91_PIN_PA7, 0);		/* RXD0 */

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PA9, 0);	/* RTS0 */
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PA10, 0);	/* CTS0 */
	if (pins & ATMEL_UART_DSR)
		at91_set_A_periph(AT91_PIN_PD14, 0);	/* DSR0 */
	if (pins & ATMEL_UART_DTR)
		at91_set_A_periph(AT91_PIN_PD15, 0);	/* DTR0 */
	if (pins & ATMEL_UART_DCD)
		at91_set_A_periph(AT91_PIN_PD16, 0);	/* DCD0 */
	if (pins & ATMEL_UART_RI)
		at91_set_A_periph(AT91_PIN_PD17, 0);	/* RI0 */
}

static struct resource uart1_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_US1,
		.end	= AT91SAM9RL_BASE_US1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_US1,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_US1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart1_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart1_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9rl_uart1_device = {
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
	at91_set_A_periph(AT91_PIN_PA11, 1);		/* TXD1 */
	at91_set_A_periph(AT91_PIN_PA12, 0);		/* RXD1 */

	if (pins & ATMEL_UART_RTS)
		at91_set_B_periph(AT91_PIN_PA18, 0);	/* RTS1 */
	if (pins & ATMEL_UART_CTS)
		at91_set_B_periph(AT91_PIN_PA19, 0);	/* CTS1 */
}

static struct resource uart2_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_US2,
		.end	= AT91SAM9RL_BASE_US2 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_US2,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_US2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart2_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart2_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9rl_uart2_device = {
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
	at91_set_A_periph(AT91_PIN_PA13, 1);		/* TXD2 */
	at91_set_A_periph(AT91_PIN_PA14, 0);		/* RXD2 */

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PA29, 0);	/* RTS2 */
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PA30, 0);	/* CTS2 */
}

static struct resource uart3_resources[] = {
	[0] = {
		.start	= AT91SAM9RL_BASE_US3,
		.end	= AT91SAM9RL_BASE_US3 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= NR_IRQS_LEGACY + AT91SAM9RL_ID_US3,
		.end	= NR_IRQS_LEGACY + AT91SAM9RL_ID_US3,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart3_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart3_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9rl_uart3_device = {
	.name		= "atmel_usart",
	.id		= 4,
	.dev		= {
				.dma_mask		= &uart3_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart3_data,
	},
	.resource	= uart3_resources,
	.num_resources	= ARRAY_SIZE(uart3_resources),
};

static inline void configure_usart3_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PB0, 1);		/* TXD3 */
	at91_set_A_periph(AT91_PIN_PB1, 0);		/* RXD3 */

	if (pins & ATMEL_UART_RTS)
		at91_set_B_periph(AT91_PIN_PD4, 0);	/* RTS3 */
	if (pins & ATMEL_UART_CTS)
		at91_set_B_periph(AT91_PIN_PD3, 0);	/* CTS3 */
}

static struct platform_device *__initdata at91_uarts[ATMEL_MAX_UART];	/* the UARTs to use */

void __init at91_register_uart(unsigned id, unsigned portnr, unsigned pins)
{
	struct platform_device *pdev;
	struct atmel_uart_data *pdata;

	switch (id) {
		case 0:		/* DBGU */
			pdev = &at91sam9rl_dbgu_device;
			configure_dbgu_pins();
			break;
		case AT91SAM9RL_ID_US0:
			pdev = &at91sam9rl_uart0_device;
			configure_usart0_pins(pins);
			break;
		case AT91SAM9RL_ID_US1:
			pdev = &at91sam9rl_uart1_device;
			configure_usart1_pins(pins);
			break;
		case AT91SAM9RL_ID_US2:
			pdev = &at91sam9rl_uart2_device;
			configure_usart2_pins(pins);
			break;
		case AT91SAM9RL_ID_US3:
			pdev = &at91sam9rl_uart3_device;
			configure_usart3_pins(pins);
			break;
		default:
			return;
	}
	pdata = pdev->dev.platform_data;
	pdata->num = portnr;		/* update to mapped ID */

	if (portnr < ATMEL_MAX_UART)
		at91_uarts[portnr] = pdev;
}

void __init at91_add_device_serial(void)
{
	int i;

	for (i = 0; i < ATMEL_MAX_UART; i++) {
		if (at91_uarts[i])
			platform_device_register(at91_uarts[i]);
	}
}
#else
void __init at91_register_uart(unsigned id, unsigned portnr, unsigned pins) {}
void __init at91_add_device_serial(void) {}
#endif


/* -------------------------------------------------------------------- */

/*
 * These devices are always present and don't need any board-specific
 * setup.
 */
static int __init at91_add_standard_devices(void)
{
	at91_add_device_hdmac();
	at91_add_device_rtc();
	at91_add_device_rtt();
	at91_add_device_watchdog();
	at91_add_device_tc();
	return 0;
}

arch_initcall(at91_add_standard_devices);
