/*
 * DBAu1000/1500/1100 PBAu1100/1500 board support
 *
 * Copyright 2000, 2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/leds.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/spi/ads7846.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/gpio-au1000.h>
#include <asm/mach-au1x00/au1000_dma.h>
#include <asm/mach-au1x00/au1100_mmc.h>
#include <asm/mach-db1x00/bcsr.h>
#include <asm/reboot.h>
#include <prom.h>
#include "platform.h"

#define F_SWAPPED (bcsr_read(BCSR_STATUS) & BCSR_STATUS_DB1000_SWAPBOOT)

const char *get_system_type(void);

int __init db1000_board_setup(void)
{
	/* initialize board register space */
	bcsr_init(DB1000_BCSR_PHYS_ADDR,
		  DB1000_BCSR_PHYS_ADDR + DB1000_BCSR_HEXLED_OFS);

	switch (BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI))) {
	case BCSR_WHOAMI_DB1000:
	case BCSR_WHOAMI_DB1500:
	case BCSR_WHOAMI_DB1100:
	case BCSR_WHOAMI_PB1500:
	case BCSR_WHOAMI_PB1500R2:
	case BCSR_WHOAMI_PB1100:
		pr_info("AMD Alchemy %s Board\n", get_system_type());
		return 0;
	}
	return -ENODEV;
}

static int db1500_map_pci_irq(const struct pci_dev *d, u8 slot, u8 pin)
{
	if ((slot < 12) || (slot > 13) || pin == 0)
		return -1;
	if (slot == 12)
		return (pin == 1) ? AU1500_PCI_INTA : 0xff;
	if (slot == 13) {
		switch (pin) {
		case 1: return AU1500_PCI_INTA;
		case 2: return AU1500_PCI_INTB;
		case 3: return AU1500_PCI_INTC;
		case 4: return AU1500_PCI_INTD;
		}
	}
	return -1;
}

static struct resource alchemy_pci_host_res[] = {
	[0] = {
		.start	= AU1500_PCI_PHYS_ADDR,
		.end	= AU1500_PCI_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct alchemy_pci_platdata db1500_pci_pd = {
	.board_map_irq	= db1500_map_pci_irq,
};

static struct platform_device db1500_pci_host_dev = {
	.dev.platform_data = &db1500_pci_pd,
	.name		= "alchemy-pci",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(alchemy_pci_host_res),
	.resource	= alchemy_pci_host_res,
};

int __init db1500_pci_setup(void)
{
	return platform_device_register(&db1500_pci_host_dev);
}

static struct resource au1100_lcd_resources[] = {
	[0] = {
		.start	= AU1100_LCD_PHYS_ADDR,
		.end	= AU1100_LCD_PHYS_ADDR + 0x800 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1100_LCD_INT,
		.end	= AU1100_LCD_INT,
		.flags	= IORESOURCE_IRQ,
	}
};

static u64 au1100_lcd_dmamask = DMA_BIT_MASK(32);

static struct platform_device au1100_lcd_device = {
	.name		= "au1100-lcd",
	.id		= 0,
	.dev = {
		.dma_mask		= &au1100_lcd_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(au1100_lcd_resources),
	.resource	= au1100_lcd_resources,
};

static struct resource alchemy_ac97c_res[] = {
	[0] = {
		.start	= AU1000_AC97_PHYS_ADDR,
		.end	= AU1000_AC97_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DMA_ID_AC97C_TX,
		.end	= DMA_ID_AC97C_TX,
		.flags	= IORESOURCE_DMA,
	},
	[2] = {
		.start	= DMA_ID_AC97C_RX,
		.end	= DMA_ID_AC97C_RX,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device alchemy_ac97c_dev = {
	.name		= "alchemy-ac97c",
	.id		= -1,
	.resource	= alchemy_ac97c_res,
	.num_resources	= ARRAY_SIZE(alchemy_ac97c_res),
};

static struct platform_device alchemy_ac97c_dma_dev = {
	.name		= "alchemy-pcm-dma",
	.id		= 0,
};

static struct platform_device db1x00_codec_dev = {
	.name		= "ac97-codec",
	.id		= -1,
};

static struct platform_device db1x00_audio_dev = {
	.name		= "db1000-audio",
};

/******************************************************************************/

static irqreturn_t db1100_mmc_cd(int irq, void *ptr)
{
	void (*mmc_cd)(struct mmc_host *, unsigned long);
	/* link against CONFIG_MMC=m */
	mmc_cd = symbol_get(mmc_detect_change);
	mmc_cd(ptr, msecs_to_jiffies(500));
	symbol_put(mmc_detect_change);

	return IRQ_HANDLED;
}

static int db1100_mmc_cd_setup(void *mmc_host, int en)
{
	int ret = 0, irq;

	if (BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI)) == BCSR_WHOAMI_DB1100)
		irq = AU1100_GPIO19_INT;
	else
		irq = AU1100_GPIO14_INT;	/* PB1100 SD0 CD# */

	if (en) {
		irq_set_irq_type(irq, IRQ_TYPE_EDGE_BOTH);
		ret = request_irq(irq, db1100_mmc_cd, 0,
				  "sd0_cd", mmc_host);
	} else
		free_irq(irq, mmc_host);
	return ret;
}

static int db1100_mmc1_cd_setup(void *mmc_host, int en)
{
	int ret = 0, irq;

	if (BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI)) == BCSR_WHOAMI_DB1100)
		irq = AU1100_GPIO20_INT;
	else
		irq = AU1100_GPIO15_INT;	/* PB1100 SD1 CD# */

	if (en) {
		irq_set_irq_type(irq, IRQ_TYPE_EDGE_BOTH);
		ret = request_irq(irq, db1100_mmc_cd, 0,
				  "sd1_cd", mmc_host);
	} else
		free_irq(irq, mmc_host);
	return ret;
}

static int db1100_mmc_card_readonly(void *mmc_host)
{
	/* testing suggests that this bit is inverted */
	return (bcsr_read(BCSR_STATUS) & BCSR_STATUS_SD0WP) ? 0 : 1;
}

static int db1100_mmc_card_inserted(void *mmc_host)
{
	return !alchemy_gpio_get_value(19);
}

static void db1100_mmc_set_power(void *mmc_host, int state)
{
	int bit;

	if (BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI)) == BCSR_WHOAMI_DB1100)
		bit = BCSR_BOARD_SD0PWR;
	else
		bit = BCSR_BOARD_PB1100_SD0PWR;

	if (state) {
		bcsr_mod(BCSR_BOARD, 0, bit);
		msleep(400);	/* stabilization time */
	} else
		bcsr_mod(BCSR_BOARD, bit, 0);
}

static void db1100_mmcled_set(struct led_classdev *led, enum led_brightness b)
{
	if (b != LED_OFF)
		bcsr_mod(BCSR_LEDS, BCSR_LEDS_LED0, 0);
	else
		bcsr_mod(BCSR_LEDS, 0, BCSR_LEDS_LED0);
}

static struct led_classdev db1100_mmc_led = {
	.brightness_set = db1100_mmcled_set,
};

static int db1100_mmc1_card_readonly(void *mmc_host)
{
	return (bcsr_read(BCSR_BOARD) & BCSR_BOARD_SD1WP) ? 1 : 0;
}

static int db1100_mmc1_card_inserted(void *mmc_host)
{
	return !alchemy_gpio_get_value(20);
}

static void db1100_mmc1_set_power(void *mmc_host, int state)
{
	int bit;

	if (BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI)) == BCSR_WHOAMI_DB1100)
		bit = BCSR_BOARD_SD1PWR;
	else
		bit = BCSR_BOARD_PB1100_SD1PWR;

	if (state) {
		bcsr_mod(BCSR_BOARD, 0, bit);
		msleep(400);	/* stabilization time */
	} else
		bcsr_mod(BCSR_BOARD, bit, 0);
}

static void db1100_mmc1led_set(struct led_classdev *led, enum led_brightness b)
{
	if (b != LED_OFF)
		bcsr_mod(BCSR_LEDS, BCSR_LEDS_LED1, 0);
	else
		bcsr_mod(BCSR_LEDS, 0, BCSR_LEDS_LED1);
}

static struct led_classdev db1100_mmc1_led = {
	.brightness_set = db1100_mmc1led_set,
};

static struct au1xmmc_platform_data db1100_mmc_platdata[2] = {
	[0] = {
		.cd_setup	= db1100_mmc_cd_setup,
		.set_power	= db1100_mmc_set_power,
		.card_inserted	= db1100_mmc_card_inserted,
		.card_readonly	= db1100_mmc_card_readonly,
		.led		= &db1100_mmc_led,
	},
	[1] = {
		.cd_setup	= db1100_mmc1_cd_setup,
		.set_power	= db1100_mmc1_set_power,
		.card_inserted	= db1100_mmc1_card_inserted,
		.card_readonly	= db1100_mmc1_card_readonly,
		.led		= &db1100_mmc1_led,
	},
};

static struct resource au1100_mmc0_resources[] = {
	[0] = {
		.start	= AU1100_SD0_PHYS_ADDR,
		.end	= AU1100_SD0_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1100_SD_INT,
		.end	= AU1100_SD_INT,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= DMA_ID_SD0_TX,
		.end	= DMA_ID_SD0_TX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= DMA_ID_SD0_RX,
		.end	= DMA_ID_SD0_RX,
		.flags	= IORESOURCE_DMA,
	}
};

static u64 au1xxx_mmc_dmamask =	 DMA_BIT_MASK(32);

static struct platform_device db1100_mmc0_dev = {
	.name		= "au1xxx-mmc",
	.id		= 0,
	.dev = {
		.dma_mask		= &au1xxx_mmc_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &db1100_mmc_platdata[0],
	},
	.num_resources	= ARRAY_SIZE(au1100_mmc0_resources),
	.resource	= au1100_mmc0_resources,
};

static struct resource au1100_mmc1_res[] = {
	[0] = {
		.start	= AU1100_SD1_PHYS_ADDR,
		.end	= AU1100_SD1_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1100_SD_INT,
		.end	= AU1100_SD_INT,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= DMA_ID_SD1_TX,
		.end	= DMA_ID_SD1_TX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= DMA_ID_SD1_RX,
		.end	= DMA_ID_SD1_RX,
		.flags	= IORESOURCE_DMA,
	}
};

static struct platform_device db1100_mmc1_dev = {
	.name		= "au1xxx-mmc",
	.id		= 1,
	.dev = {
		.dma_mask		= &au1xxx_mmc_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &db1100_mmc_platdata[1],
	},
	.num_resources	= ARRAY_SIZE(au1100_mmc1_res),
	.resource	= au1100_mmc1_res,
};

/******************************************************************************/

static void db1000_irda_set_phy_mode(int mode)
{
	unsigned short mask = BCSR_RESETS_IRDA_MODE_MASK | BCSR_RESETS_FIR_SEL;

	switch (mode) {
	case AU1000_IRDA_PHY_MODE_OFF:
		bcsr_mod(BCSR_RESETS, mask, BCSR_RESETS_IRDA_MODE_OFF);
		break;
	case AU1000_IRDA_PHY_MODE_SIR:
		bcsr_mod(BCSR_RESETS, mask, BCSR_RESETS_IRDA_MODE_FULL);
		break;
	case AU1000_IRDA_PHY_MODE_FIR:
		bcsr_mod(BCSR_RESETS, mask, BCSR_RESETS_IRDA_MODE_FULL |
					    BCSR_RESETS_FIR_SEL);
		break;
	}
}

static struct au1k_irda_platform_data db1000_irda_platdata = {
	.set_phy_mode	= db1000_irda_set_phy_mode,
};

static struct resource au1000_irda_res[] = {
	[0] = {
		.start	= AU1000_IRDA_PHYS_ADDR,
		.end	= AU1000_IRDA_PHYS_ADDR + 0x0fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1000_IRDA_TX_INT,
		.end	= AU1000_IRDA_TX_INT,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AU1000_IRDA_RX_INT,
		.end	= AU1000_IRDA_RX_INT,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device db1000_irda_dev = {
	.name	= "au1000-irda",
	.id	= -1,
	.dev	= {
		.platform_data = &db1000_irda_platdata,
	},
	.resource	= au1000_irda_res,
	.num_resources	= ARRAY_SIZE(au1000_irda_res),
};

/******************************************************************************/

static struct ads7846_platform_data db1100_touch_pd = {
	.model		= 7846,
	.vref_mv	= 3300,
	.gpio_pendown	= 21,
};

static struct spi_gpio_platform_data db1100_spictl_pd = {
	.sck		= 209,
	.mosi		= 208,
	.miso		= 207,
	.num_chipselect = 1,
};

static struct spi_board_info db1100_spi_info[] __initdata = {
	[0] = {
		.modalias	 = "ads7846",
		.max_speed_hz	 = 3250000,
		.bus_num	 = 0,
		.chip_select	 = 0,
		.mode		 = 0,
		.irq		 = AU1100_GPIO21_INT,
		.platform_data	 = &db1100_touch_pd,
		.controller_data = (void *)210, /* for spi_gpio: CS# GPIO210 */
	},
};

static struct platform_device db1100_spi_dev = {
	.name		= "spi_gpio",
	.id		= 0,
	.dev		= {
		.platform_data	= &db1100_spictl_pd,
	},
};


static struct platform_device *db1x00_devs[] = {
	&db1x00_codec_dev,
	&alchemy_ac97c_dma_dev,
	&alchemy_ac97c_dev,
	&db1x00_audio_dev,
};

static struct platform_device *db1000_devs[] = {
	&db1000_irda_dev,
};

static struct platform_device *db1100_devs[] = {
	&au1100_lcd_device,
	&db1100_mmc0_dev,
	&db1100_mmc1_dev,
	&db1000_irda_dev,
};

int __init db1000_dev_setup(void)
{
	int board = BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI));
	int c0, c1, d0, d1, s0, s1, flashsize = 32,  twosocks = 1;
	unsigned long pfc;
	struct clk *c, *p;

	if (board == BCSR_WHOAMI_DB1500) {
		c0 = AU1500_GPIO2_INT;
		c1 = AU1500_GPIO5_INT;
		d0 = AU1500_GPIO0_INT;
		d1 = AU1500_GPIO3_INT;
		s0 = AU1500_GPIO1_INT;
		s1 = AU1500_GPIO4_INT;
	} else if (board == BCSR_WHOAMI_DB1100) {
		c0 = AU1100_GPIO2_INT;
		c1 = AU1100_GPIO5_INT;
		d0 = AU1100_GPIO0_INT;
		d1 = AU1100_GPIO3_INT;
		s0 = AU1100_GPIO1_INT;
		s1 = AU1100_GPIO4_INT;

		gpio_request(19, "sd0_cd");
		gpio_request(20, "sd1_cd");
		gpio_direction_input(19);	/* sd0 cd# */
		gpio_direction_input(20);	/* sd1 cd# */

		/* spi_gpio on SSI0 pins */
		pfc = alchemy_rdsys(AU1000_SYS_PINFUNC);
		pfc |= (1 << 0);	/* SSI0 pins as GPIOs */
		alchemy_wrsys(pfc, AU1000_SYS_PINFUNC);

		spi_register_board_info(db1100_spi_info,
					ARRAY_SIZE(db1100_spi_info));

		/* link LCD clock to AUXPLL */
		p = clk_get(NULL, "auxpll_clk");
		c = clk_get(NULL, "lcd_intclk");
		if (!IS_ERR(c) && !IS_ERR(p)) {
			clk_set_parent(c, p);
			clk_set_rate(c, clk_get_rate(p));
		}
		if (!IS_ERR(c))
			clk_put(c);
		if (!IS_ERR(p))
			clk_put(p);

		platform_add_devices(db1100_devs, ARRAY_SIZE(db1100_devs));
		platform_device_register(&db1100_spi_dev);
	} else if (board == BCSR_WHOAMI_DB1000) {
		c0 = AU1000_GPIO2_INT;
		c1 = AU1000_GPIO5_INT;
		d0 = AU1000_GPIO0_INT;
		d1 = AU1000_GPIO3_INT;
		s0 = AU1000_GPIO1_INT;
		s1 = AU1000_GPIO4_INT;
		platform_add_devices(db1000_devs, ARRAY_SIZE(db1000_devs));
	} else if ((board == BCSR_WHOAMI_PB1500) ||
		   (board == BCSR_WHOAMI_PB1500R2)) {
		c0 = AU1500_GPIO203_INT;
		d0 = AU1500_GPIO201_INT;
		s0 = AU1500_GPIO202_INT;
		twosocks = 0;
		flashsize = 64;
		/* RTC and daughtercard irqs */
		irq_set_irq_type(AU1500_GPIO204_INT, IRQ_TYPE_LEVEL_LOW);
		irq_set_irq_type(AU1500_GPIO205_INT, IRQ_TYPE_LEVEL_LOW);
		/* EPSON S1D13806 0x1b000000
		 * SRAM 1MB/2MB	  0x1a000000
		 * DS1693 RTC	  0x0c000000
		 */
	} else if (board == BCSR_WHOAMI_PB1100) {
		c0 = AU1100_GPIO11_INT;
		d0 = AU1100_GPIO9_INT;
		s0 = AU1100_GPIO10_INT;
		twosocks = 0;
		flashsize = 64;
		/* pendown, rtc, daughtercard irqs */
		irq_set_irq_type(AU1100_GPIO8_INT, IRQ_TYPE_LEVEL_LOW);
		irq_set_irq_type(AU1100_GPIO12_INT, IRQ_TYPE_LEVEL_LOW);
		irq_set_irq_type(AU1100_GPIO13_INT, IRQ_TYPE_LEVEL_LOW);
		/* EPSON S1D13806 0x1b000000
		 * SRAM 1MB/2MB	  0x1a000000
		 * DiskOnChip	  0x0d000000
		 * DS1693 RTC	  0x0c000000
		 */
		platform_add_devices(db1100_devs, ARRAY_SIZE(db1100_devs));
	} else
		return 0; /* unknown board, no further dev setup to do */

	irq_set_irq_type(d0, IRQ_TYPE_EDGE_BOTH);
	irq_set_irq_type(c0, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(s0, IRQ_TYPE_LEVEL_LOW);

	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x000400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x000400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x000010000 - 1,
		c0, d0, /*s0*/0, 0, 0);

	if (twosocks) {
		irq_set_irq_type(d1, IRQ_TYPE_EDGE_BOTH);
		irq_set_irq_type(c1, IRQ_TYPE_LEVEL_LOW);
		irq_set_irq_type(s1, IRQ_TYPE_LEVEL_LOW);

		db1x_register_pcmcia_socket(
			AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x004000000,
			AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x004400000 - 1,
			AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x004000000,
			AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x004400000 - 1,
			AU1000_PCMCIA_IO_PHYS_ADDR   + 0x004000000,
			AU1000_PCMCIA_IO_PHYS_ADDR   + 0x004010000 - 1,
			c1, d1, /*s1*/0, 0, 1);
	}

	platform_add_devices(db1x00_devs, ARRAY_SIZE(db1x00_devs));
	db1x_register_norflash(flashsize << 20, 4 /* 32bit */, F_SWAPPED);
	return 0;
}
