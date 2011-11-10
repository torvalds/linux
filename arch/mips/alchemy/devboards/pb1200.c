/*
 * Pb1200/DBAu1200 board platform device registration
 *
 * Copyright (C) 2008 MontaVista Software Inc. <source@mvista.com>
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

#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/smc91x.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1100_mmc.h>
#include <asm/mach-au1x00/au1200fb.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#include <asm/mach-db1x00/bcsr.h>
#include <asm/mach-pb1x00/pb1200.h>
#include <prom.h>
#include "platform.h"


const char *get_system_type(void)
{
	return "PB1200";
}

void __init board_setup(void)
{
	printk(KERN_INFO "AMD Alchemy Pb1200 Board\n");
	bcsr_init(PB1200_BCSR_PHYS_ADDR,
		  PB1200_BCSR_PHYS_ADDR + PB1200_BCSR_HEXLED_OFS);

#if 0
	{
		u32 pin_func;

		/*
		 * Enable PSC1 SYNC for AC97.  Normaly done in audio driver,
		 * but it is board specific code, so put it here.
		 */
		pin_func = au_readl(SYS_PINFUNC);
		au_sync();
		pin_func |= SYS_PF_MUST_BE_SET | SYS_PF_PSC1_S1;
		au_writel(pin_func, SYS_PINFUNC);

		au_writel(0, (u32)bcsr | 0x10); /* turn off PCMCIA power */
		au_sync();
	}
#endif

#if defined(CONFIG_I2C_AU1550)
	{
		u32 freq0, clksrc;
		u32 pin_func;

		/* Select SMBus in CPLD */
		bcsr_mod(BCSR_RESETS, BCSR_RESETS_PSC0MUX, 0);

		pin_func = au_readl(SYS_PINFUNC);
		au_sync();
		pin_func &= ~(SYS_PINFUNC_P0A | SYS_PINFUNC_P0B);
		/* Set GPIOs correctly */
		pin_func |= 2 << 17;
		au_writel(pin_func, SYS_PINFUNC);
		au_sync();

		/* The I2C driver depends on 50 MHz clock */
		freq0 = au_readl(SYS_FREQCTRL0);
		au_sync();
		freq0 &= ~(SYS_FC_FRDIV1_MASK | SYS_FC_FS1 | SYS_FC_FE1);
		freq0 |= 3 << SYS_FC_FRDIV1_BIT;
		/* 396 MHz / (3 + 1) * 2 == 49.5 MHz */
		au_writel(freq0, SYS_FREQCTRL0);
		au_sync();
		freq0 |= SYS_FC_FE1;
		au_writel(freq0, SYS_FREQCTRL0);
		au_sync();

		clksrc = au_readl(SYS_CLKSRC);
		au_sync();
		clksrc &= ~(SYS_CS_CE0 | SYS_CS_DE0 | SYS_CS_ME0_MASK);
		/* Bit 22 is EXTCLK0 for PSC0 */
		clksrc |= SYS_CS_MUX_FQ1 << SYS_CS_ME0_BIT;
		au_writel(clksrc, SYS_CLKSRC);
		au_sync();
	}
#endif

	/*
	 * The Pb1200 development board uses external MUX for PSC0 to
	 * support SMB/SPI. bcsr_resets bit 12: 0=SMB 1=SPI
	 */
#ifdef CONFIG_I2C_AU1550
	bcsr_mod(BCSR_RESETS, BCSR_RESETS_PSC0MUX, 0);
#endif
	au_sync();
}

/******************************************************************************/

static int mmc_activity;

static void pb1200mmc0_set_power(void *mmc_host, int state)
{
	if (state)
		bcsr_mod(BCSR_BOARD, 0, BCSR_BOARD_SD0PWR);
	else
		bcsr_mod(BCSR_BOARD, BCSR_BOARD_SD0PWR, 0);

	msleep(1);
}

static int pb1200mmc0_card_readonly(void *mmc_host)
{
	return (bcsr_read(BCSR_STATUS) & BCSR_STATUS_SD0WP) ? 1 : 0;
}

static int pb1200mmc0_card_inserted(void *mmc_host)
{
	return (bcsr_read(BCSR_SIGSTAT) & BCSR_INT_SD0INSERT) ? 1 : 0;
}

static void pb1200_mmcled_set(struct led_classdev *led,
			enum led_brightness brightness)
{
	if (brightness != LED_OFF) {
		if (++mmc_activity == 1)
			bcsr_mod(BCSR_LEDS, BCSR_LEDS_LED0, 0);
	} else {
		if (--mmc_activity == 0)
			bcsr_mod(BCSR_LEDS, 0, BCSR_LEDS_LED0);
	}
}

static struct led_classdev pb1200mmc_led = {
	.brightness_set	= pb1200_mmcled_set,
};

static void pb1200mmc1_set_power(void *mmc_host, int state)
{
	if (state)
		bcsr_mod(BCSR_BOARD, 0, BCSR_BOARD_SD1PWR);
	else
		bcsr_mod(BCSR_BOARD, BCSR_BOARD_SD1PWR, 0);

	msleep(1);
}

static int pb1200mmc1_card_readonly(void *mmc_host)
{
	return (bcsr_read(BCSR_STATUS) & BCSR_STATUS_SD1WP) ? 1 : 0;
}

static int pb1200mmc1_card_inserted(void *mmc_host)
{
	return (bcsr_read(BCSR_SIGSTAT) & BCSR_INT_SD1INSERT) ? 1 : 0;
}

static struct au1xmmc_platform_data pb1200mmc_platdata[2] = {
	[0] = {
		.set_power	= pb1200mmc0_set_power,
		.card_inserted	= pb1200mmc0_card_inserted,
		.card_readonly	= pb1200mmc0_card_readonly,
		.cd_setup	= NULL,		/* use poll-timer in driver */
		.led		= &pb1200mmc_led,
	},
	[1] = {
		.set_power	= pb1200mmc1_set_power,
		.card_inserted	= pb1200mmc1_card_inserted,
		.card_readonly	= pb1200mmc1_card_readonly,
		.cd_setup	= NULL,		/* use poll-timer in driver */
		.led		= &pb1200mmc_led,
	},
};

static u64 au1xxx_mmc_dmamask =  DMA_BIT_MASK(32);

static struct resource au1200_mmc0_res[] = {
	[0] = {
		.start	= AU1100_SD0_PHYS_ADDR,
		.end	= AU1100_SD0_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1200_SD_INT,
		.end	= AU1200_SD_INT,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AU1200_DSCR_CMD0_SDMS_TX0,
		.end	= AU1200_DSCR_CMD0_SDMS_TX0,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= AU1200_DSCR_CMD0_SDMS_RX0,
		.end	= AU1200_DSCR_CMD0_SDMS_RX0,
		.flags	= IORESOURCE_DMA,
	}
};

static struct platform_device pb1200_mmc0_dev = {
	.name		= "au1xxx-mmc",
	.id		= 0,
	.dev = {
		.dma_mask		= &au1xxx_mmc_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &pb1200mmc_platdata[0],
	},
	.num_resources	= ARRAY_SIZE(au1200_mmc0_res),
	.resource	= au1200_mmc0_res,
};

static struct resource au1200_mmc1_res[] = {
	[0] = {
		.start	= AU1100_SD1_PHYS_ADDR,
		.end	= AU1100_SD1_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1200_SD_INT,
		.end	= AU1200_SD_INT,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AU1200_DSCR_CMD0_SDMS_TX1,
		.end	= AU1200_DSCR_CMD0_SDMS_TX1,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= AU1200_DSCR_CMD0_SDMS_RX1,
		.end	= AU1200_DSCR_CMD0_SDMS_RX1,
		.flags	= IORESOURCE_DMA,
	}
};

static struct platform_device pb1200_mmc1_dev = {
	.name		= "au1xxx-mmc",
	.id		= 1,
	.dev = {
		.dma_mask		= &au1xxx_mmc_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &pb1200mmc_platdata[1],
	},
	.num_resources	= ARRAY_SIZE(au1200_mmc1_res),
	.resource	= au1200_mmc1_res,
};


static struct resource ide_resources[] = {
	[0] = {
		.start	= IDE_PHYS_ADDR,
		.end	= IDE_PHYS_ADDR + IDE_PHYS_LEN - 1,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start	= IDE_INT,
		.end	= IDE_INT,
		.flags	= IORESOURCE_IRQ
	},
	[2] = {
		.start	= AU1200_DSCR_CMD0_DMA_REQ1,
		.end	= AU1200_DSCR_CMD0_DMA_REQ1,
		.flags	= IORESOURCE_DMA,
	},
};

static u64 au1200_ide_dmamask = DMA_BIT_MASK(32);

static struct platform_device ide_device = {
	.name		= "au1200-ide",
	.id		= 0,
	.dev = {
		.dma_mask		= &au1200_ide_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(ide_resources),
	.resource	= ide_resources
};

static struct smc91x_platdata smc_data = {
	.flags	= SMC91X_NOWAIT | SMC91X_USE_16BIT,
	.leda	= RPC_LED_100_10,
	.ledb	= RPC_LED_TX_RX,
};

static struct resource smc91c111_resources[] = {
	[0] = {
		.name	= "smc91x-regs",
		.start	= SMC91C111_PHYS_ADDR,
		.end	= SMC91C111_PHYS_ADDR + 0xf,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start	= SMC91C111_INT,
		.end	= SMC91C111_INT,
		.flags	= IORESOURCE_IRQ
	},
};

static struct platform_device smc91c111_device = {
	.dev	= {
		.platform_data	= &smc_data,
	},
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc91c111_resources),
	.resource	= smc91c111_resources
};

static struct resource au1200_psc0_res[] = {
	[0] = {
		.start	= AU1550_PSC0_PHYS_ADDR,
		.end	= AU1550_PSC0_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1200_PSC0_INT,
		.end	= AU1200_PSC0_INT,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AU1200_DSCR_CMD0_PSC0_TX,
		.end	= AU1200_DSCR_CMD0_PSC0_TX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= AU1200_DSCR_CMD0_PSC0_RX,
		.end	= AU1200_DSCR_CMD0_PSC0_RX,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device pb1200_i2c_dev = {
	.name		= "au1xpsc_smbus",
	.id		= 0,	/* bus number */
	.num_resources	= ARRAY_SIZE(au1200_psc0_res),
	.resource	= au1200_psc0_res,
};

static int pb1200fb_panel_index(void)
{
	return (bcsr_read(BCSR_SWITCHES) >> 8) & 0x0f;
}

static int pb1200fb_panel_init(void)
{
	/* Apply power */
	bcsr_mod(BCSR_BOARD, 0, BCSR_BOARD_LCDVEE | BCSR_BOARD_LCDVDD |
				BCSR_BOARD_LCDBL);
	return 0;
}

static int pb1200fb_panel_shutdown(void)
{
	/* Remove power */
	bcsr_mod(BCSR_BOARD, BCSR_BOARD_LCDVEE | BCSR_BOARD_LCDVDD |
			     BCSR_BOARD_LCDBL, 0);
	return 0;
}

static struct au1200fb_platdata pb1200fb_pd = {
	.panel_index	= pb1200fb_panel_index,
	.panel_init	= pb1200fb_panel_init,
	.panel_shutdown	= pb1200fb_panel_shutdown,
};

static struct resource au1200_lcd_res[] = {
	[0] = {
		.start	= AU1200_LCD_PHYS_ADDR,
		.end	= AU1200_LCD_PHYS_ADDR + 0x800 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1200_LCD_INT,
		.end	= AU1200_LCD_INT,
		.flags	= IORESOURCE_IRQ,
	}
};

static u64 au1200_lcd_dmamask = DMA_BIT_MASK(32);

static struct platform_device pb1200_lcd_dev = {
	.name		= "au1200-lcd",
	.id		= 0,
	.dev = {
		.dma_mask		= &au1200_lcd_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &pb1200fb_pd,
	},
	.num_resources	= ARRAY_SIZE(au1200_lcd_res),
	.resource	= au1200_lcd_res,
};

static struct platform_device *board_platform_devices[] __initdata = {
	&ide_device,
	&smc91c111_device,
	&pb1200_i2c_dev,
	&pb1200_mmc0_dev,
	&pb1200_mmc1_dev,
	&pb1200_lcd_dev,
};

static int __init board_register_devices(void)
{
	int swapped;

	/* We have a problem with CPLD rev 3. */
	if (BCSR_WHOAMI_CPLD(bcsr_read(BCSR_WHOAMI)) <= 3) {
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "Pb1200 must be at CPLD rev 4. Please have Pb1200\n");
		printk(KERN_ERR "updated to latest revision. This software will\n");
		printk(KERN_ERR "not work on anything less than CPLD rev 4.\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		panic("Game over.  Your score is 0.");
	}

	irq_set_irq_type(AU1200_GPIO7_INT, IRQF_TRIGGER_LOW);
	bcsr_init_irq(PB1200_INT_BEGIN, PB1200_INT_END, AU1200_GPIO7_INT);

	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x000400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x000400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x000010000 - 1,
		PB1200_PC0_INT, PB1200_PC0_INSERT_INT,
		/*PB1200_PC0_STSCHG_INT*/0, PB1200_PC0_EJECT_INT, 0);

	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x008000000,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x008400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x008000000,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x008400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x008000000,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x008010000 - 1,
		PB1200_PC1_INT, PB1200_PC1_INSERT_INT,
		/*PB1200_PC1_STSCHG_INT*/0, PB1200_PC1_EJECT_INT, 1);

	swapped = bcsr_read(BCSR_STATUS) &  BCSR_STATUS_DB1200_SWAPBOOT;
	db1x_register_norflash(128 * 1024 * 1024, 2, swapped);

	return platform_add_devices(board_platform_devices,
				    ARRAY_SIZE(board_platform_devices));
}
device_initcall(board_register_devices);
