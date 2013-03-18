/*
 * DBAu1200/PBAu1200 board platform device registration
 *
 * Copyright (C) 2008-2011 Manuel Lauss
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
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/mmc/host.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/smc91x.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1100_mmc.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#include <asm/mach-au1x00/au1200fb.h>
#include <asm/mach-au1x00/au1550_spi.h>
#include <asm/mach-db1x00/bcsr.h>
#include <asm/mach-db1x00/db1200.h>

#include "platform.h"

const char *get_system_type(void);

static int __init db1200_detect_board(void)
{
	int bid;

	/* try the DB1200 first */
	bcsr_init(DB1200_BCSR_PHYS_ADDR,
		  DB1200_BCSR_PHYS_ADDR + DB1200_BCSR_HEXLED_OFS);
	if (BCSR_WHOAMI_DB1200 == BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI))) {
		unsigned short t = bcsr_read(BCSR_HEXLEDS);
		bcsr_write(BCSR_HEXLEDS, ~t);
		if (bcsr_read(BCSR_HEXLEDS) != t) {
			bcsr_write(BCSR_HEXLEDS, t);
			return 0;
		}
	}

	/* okay, try the PB1200 then */
	bcsr_init(PB1200_BCSR_PHYS_ADDR,
		  PB1200_BCSR_PHYS_ADDR + PB1200_BCSR_HEXLED_OFS);
	bid = BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI));
	if ((bid == BCSR_WHOAMI_PB1200_DDR1) ||
	    (bid == BCSR_WHOAMI_PB1200_DDR2)) {
		unsigned short t = bcsr_read(BCSR_HEXLEDS);
		bcsr_write(BCSR_HEXLEDS, ~t);
		if (bcsr_read(BCSR_HEXLEDS) != t) {
			bcsr_write(BCSR_HEXLEDS, t);
			return 0;
		}
	}

	return 1;	/* it's neither */
}

int __init db1200_board_setup(void)
{
	unsigned long freq0, clksrc, div, pfc;
	unsigned short whoami;

	if (db1200_detect_board())
		return -ENODEV;

	whoami = bcsr_read(BCSR_WHOAMI);
	printk(KERN_INFO "Alchemy/AMD/RMI %s Board, CPLD Rev %d"
		"  Board-ID %d	Daughtercard ID %d\n", get_system_type(),
		(whoami >> 4) & 0xf, (whoami >> 8) & 0xf, whoami & 0xf);

	/* SMBus/SPI on PSC0, Audio on PSC1 */
	pfc = __raw_readl((void __iomem *)SYS_PINFUNC);
	pfc &= ~(SYS_PINFUNC_P0A | SYS_PINFUNC_P0B);
	pfc &= ~(SYS_PINFUNC_P1A | SYS_PINFUNC_P1B | SYS_PINFUNC_FS3);
	pfc |= SYS_PINFUNC_P1C; /* SPI is configured later */
	__raw_writel(pfc, (void __iomem *)SYS_PINFUNC);
	wmb();

	/* Clock configurations: PSC0: ~50MHz via Clkgen0, derived from
	 * CPU clock; all other clock generators off/unused.
	 */
	div = (get_au1x00_speed() + 25000000) / 50000000;
	if (div & 1)
		div++;
	div = ((div >> 1) - 1) & 0xff;

	freq0 = div << SYS_FC_FRDIV0_BIT;
	__raw_writel(freq0, (void __iomem *)SYS_FREQCTRL0);
	wmb();
	freq0 |= SYS_FC_FE0;	/* enable F0 */
	__raw_writel(freq0, (void __iomem *)SYS_FREQCTRL0);
	wmb();

	/* psc0_intclk comes 1:1 from F0 */
	clksrc = SYS_CS_MUX_FQ0 << SYS_CS_ME0_BIT;
	__raw_writel(clksrc, (void __iomem *)SYS_CLKSRC);
	wmb();

	return 0;
}

/******************************************************************************/

static struct mtd_partition db1200_spiflash_parts[] = {
	{
		.name	= "spi_flash",
		.offset = 0,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct flash_platform_data db1200_spiflash_data = {
	.name		= "s25fl001",
	.parts		= db1200_spiflash_parts,
	.nr_parts	= ARRAY_SIZE(db1200_spiflash_parts),
	.type		= "m25p10",
};

static struct spi_board_info db1200_spi_devs[] __initdata = {
	{
		/* TI TMP121AIDBVR temp sensor */
		.modalias	= "tmp121",
		.max_speed_hz	= 2000000,
		.bus_num	= 0,
		.chip_select	= 0,
		.mode		= 0,
	},
	{
		/* Spansion S25FL001D0FMA SPI flash */
		.modalias	= "m25p80",
		.max_speed_hz	= 50000000,
		.bus_num	= 0,
		.chip_select	= 1,
		.mode		= 0,
		.platform_data	= &db1200_spiflash_data,
	},
};

static struct i2c_board_info db1200_i2c_devs[] __initdata = {
	{ I2C_BOARD_INFO("24c04", 0x52),  }, /* AT24C04-10 I2C eeprom */
	{ I2C_BOARD_INFO("ne1619", 0x2d), }, /* adm1025-compat hwmon */
	{ I2C_BOARD_INFO("wm8731", 0x1b), }, /* I2S audio codec WM8731 */
};

/**********************************************************************/

static void au1200_nand_cmd_ctrl(struct mtd_info *mtd, int cmd,
				 unsigned int ctrl)
{
	struct nand_chip *this = mtd->priv;
	unsigned long ioaddr = (unsigned long)this->IO_ADDR_W;

	ioaddr &= 0xffffff00;

	if (ctrl & NAND_CLE) {
		ioaddr += MEM_STNAND_CMD;
	} else if (ctrl & NAND_ALE) {
		ioaddr += MEM_STNAND_ADDR;
	} else {
		/* assume we want to r/w real data  by default */
		ioaddr += MEM_STNAND_DATA;
	}
	this->IO_ADDR_R = this->IO_ADDR_W = (void __iomem *)ioaddr;
	if (cmd != NAND_CMD_NONE) {
		__raw_writeb(cmd, this->IO_ADDR_W);
		wmb();
	}
}

static int au1200_nand_device_ready(struct mtd_info *mtd)
{
	return __raw_readl((void __iomem *)MEM_STSTAT) & 1;
}

static struct mtd_partition db1200_nand_parts[] = {
	{
		.name	= "NAND FS 0",
		.offset = 0,
		.size	= 8 * 1024 * 1024,
	},
	{
		.name	= "NAND FS 1",
		.offset = MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL
	},
};

struct platform_nand_data db1200_nand_platdata = {
	.chip = {
		.nr_chips	= 1,
		.chip_offset	= 0,
		.nr_partitions	= ARRAY_SIZE(db1200_nand_parts),
		.partitions	= db1200_nand_parts,
		.chip_delay	= 20,
	},
	.ctrl = {
		.dev_ready	= au1200_nand_device_ready,
		.cmd_ctrl	= au1200_nand_cmd_ctrl,
	},
};

static struct resource db1200_nand_res[] = {
	[0] = {
		.start	= DB1200_NAND_PHYS_ADDR,
		.end	= DB1200_NAND_PHYS_ADDR + 0xff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device db1200_nand_dev = {
	.name		= "gen_nand",
	.num_resources	= ARRAY_SIZE(db1200_nand_res),
	.resource	= db1200_nand_res,
	.id		= -1,
	.dev		= {
		.platform_data = &db1200_nand_platdata,
	}
};

/**********************************************************************/

static struct smc91x_platdata db1200_eth_data = {
	.flags	= SMC91X_NOWAIT | SMC91X_USE_16BIT,
	.leda	= RPC_LED_100_10,
	.ledb	= RPC_LED_TX_RX,
};

static struct resource db1200_eth_res[] = {
	[0] = {
		.start	= DB1200_ETH_PHYS_ADDR,
		.end	= DB1200_ETH_PHYS_ADDR + 0xf,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DB1200_ETH_INT,
		.end	= DB1200_ETH_INT,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device db1200_eth_dev = {
	.dev	= {
		.platform_data	= &db1200_eth_data,
	},
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(db1200_eth_res),
	.resource	= db1200_eth_res,
};

/**********************************************************************/

static struct resource db1200_ide_res[] = {
	[0] = {
		.start	= DB1200_IDE_PHYS_ADDR,
		.end	= DB1200_IDE_PHYS_ADDR + DB1200_IDE_PHYS_LEN - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DB1200_IDE_INT,
		.end	= DB1200_IDE_INT,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AU1200_DSCR_CMD0_DMA_REQ1,
		.end	= AU1200_DSCR_CMD0_DMA_REQ1,
		.flags	= IORESOURCE_DMA,
	},
};

static u64 au1200_ide_dmamask = DMA_BIT_MASK(32);

static struct platform_device db1200_ide_dev = {
	.name		= "au1200-ide",
	.id		= 0,
	.dev = {
		.dma_mask		= &au1200_ide_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(db1200_ide_res),
	.resource	= db1200_ide_res,
};

/**********************************************************************/

/* SD carddetects:  they're supposed to be edge-triggered, but ack
 * doesn't seem to work (CPLD Rev 2).  Instead, the screaming one
 * is disabled and its counterpart enabled.  The 500ms timeout is
 * because the carddetect isn't debounced in hardware.
 */
static irqreturn_t db1200_mmc_cd(int irq, void *ptr)
{
	void(*mmc_cd)(struct mmc_host *, unsigned long);

	if (irq == DB1200_SD0_INSERT_INT) {
		disable_irq_nosync(DB1200_SD0_INSERT_INT);
		enable_irq(DB1200_SD0_EJECT_INT);
	} else {
		disable_irq_nosync(DB1200_SD0_EJECT_INT);
		enable_irq(DB1200_SD0_INSERT_INT);
	}

	/* link against CONFIG_MMC=m */
	mmc_cd = symbol_get(mmc_detect_change);
	if (mmc_cd) {
		mmc_cd(ptr, msecs_to_jiffies(500));
		symbol_put(mmc_detect_change);
	}

	return IRQ_HANDLED;
}

static int db1200_mmc_cd_setup(void *mmc_host, int en)
{
	int ret;

	if (en) {
		ret = request_irq(DB1200_SD0_INSERT_INT, db1200_mmc_cd,
				  0, "sd_insert", mmc_host);
		if (ret)
			goto out;

		ret = request_irq(DB1200_SD0_EJECT_INT, db1200_mmc_cd,
				  0, "sd_eject", mmc_host);
		if (ret) {
			free_irq(DB1200_SD0_INSERT_INT, mmc_host);
			goto out;
		}

		if (bcsr_read(BCSR_SIGSTAT) & BCSR_INT_SD0INSERT)
			enable_irq(DB1200_SD0_EJECT_INT);
		else
			enable_irq(DB1200_SD0_INSERT_INT);

	} else {
		free_irq(DB1200_SD0_INSERT_INT, mmc_host);
		free_irq(DB1200_SD0_EJECT_INT, mmc_host);
	}
	ret = 0;
out:
	return ret;
}

static void db1200_mmc_set_power(void *mmc_host, int state)
{
	if (state) {
		bcsr_mod(BCSR_BOARD, 0, BCSR_BOARD_SD0PWR);
		msleep(400);	/* stabilization time */
	} else
		bcsr_mod(BCSR_BOARD, BCSR_BOARD_SD0PWR, 0);
}

static int db1200_mmc_card_readonly(void *mmc_host)
{
	return (bcsr_read(BCSR_STATUS) & BCSR_STATUS_SD0WP) ? 1 : 0;
}

static int db1200_mmc_card_inserted(void *mmc_host)
{
	return (bcsr_read(BCSR_SIGSTAT) & BCSR_INT_SD0INSERT) ? 1 : 0;
}

static void db1200_mmcled_set(struct led_classdev *led,
			      enum led_brightness brightness)
{
	if (brightness != LED_OFF)
		bcsr_mod(BCSR_LEDS, BCSR_LEDS_LED0, 0);
	else
		bcsr_mod(BCSR_LEDS, 0, BCSR_LEDS_LED0);
}

static struct led_classdev db1200_mmc_led = {
	.brightness_set = db1200_mmcled_set,
};

/* -- */

static irqreturn_t pb1200_mmc1_cd(int irq, void *ptr)
{
	void(*mmc_cd)(struct mmc_host *, unsigned long);

	if (irq == PB1200_SD1_INSERT_INT) {
		disable_irq_nosync(PB1200_SD1_INSERT_INT);
		enable_irq(PB1200_SD1_EJECT_INT);
	} else {
		disable_irq_nosync(PB1200_SD1_EJECT_INT);
		enable_irq(PB1200_SD1_INSERT_INT);
	}

	/* link against CONFIG_MMC=m */
	mmc_cd = symbol_get(mmc_detect_change);
	if (mmc_cd) {
		mmc_cd(ptr, msecs_to_jiffies(500));
		symbol_put(mmc_detect_change);
	}

	return IRQ_HANDLED;
}

static int pb1200_mmc1_cd_setup(void *mmc_host, int en)
{
	int ret;

	if (en) {
		ret = request_irq(PB1200_SD1_INSERT_INT, pb1200_mmc1_cd, 0,
				  "sd1_insert", mmc_host);
		if (ret)
			goto out;

		ret = request_irq(PB1200_SD1_EJECT_INT, pb1200_mmc1_cd, 0,
				  "sd1_eject", mmc_host);
		if (ret) {
			free_irq(PB1200_SD1_INSERT_INT, mmc_host);
			goto out;
		}

		if (bcsr_read(BCSR_SIGSTAT) & BCSR_INT_SD1INSERT)
			enable_irq(PB1200_SD1_EJECT_INT);
		else
			enable_irq(PB1200_SD1_INSERT_INT);

	} else {
		free_irq(PB1200_SD1_INSERT_INT, mmc_host);
		free_irq(PB1200_SD1_EJECT_INT, mmc_host);
	}
	ret = 0;
out:
	return ret;
}

static void pb1200_mmc1led_set(struct led_classdev *led,
			enum led_brightness brightness)
{
	if (brightness != LED_OFF)
			bcsr_mod(BCSR_LEDS, BCSR_LEDS_LED1, 0);
	else
			bcsr_mod(BCSR_LEDS, 0, BCSR_LEDS_LED1);
}

static struct led_classdev pb1200_mmc1_led = {
	.brightness_set = pb1200_mmc1led_set,
};

static void pb1200_mmc1_set_power(void *mmc_host, int state)
{
	if (state) {
		bcsr_mod(BCSR_BOARD, 0, BCSR_BOARD_SD1PWR);
		msleep(400);	/* stabilization time */
	} else
		bcsr_mod(BCSR_BOARD, BCSR_BOARD_SD1PWR, 0);
}

static int pb1200_mmc1_card_readonly(void *mmc_host)
{
	return (bcsr_read(BCSR_STATUS) & BCSR_STATUS_SD1WP) ? 1 : 0;
}

static int pb1200_mmc1_card_inserted(void *mmc_host)
{
	return (bcsr_read(BCSR_SIGSTAT) & BCSR_INT_SD1INSERT) ? 1 : 0;
}


static struct au1xmmc_platform_data db1200_mmc_platdata[2] = {
	[0] = {
		.cd_setup	= db1200_mmc_cd_setup,
		.set_power	= db1200_mmc_set_power,
		.card_inserted	= db1200_mmc_card_inserted,
		.card_readonly	= db1200_mmc_card_readonly,
		.led		= &db1200_mmc_led,
	},
	[1] = {
		.cd_setup	= pb1200_mmc1_cd_setup,
		.set_power	= pb1200_mmc1_set_power,
		.card_inserted	= pb1200_mmc1_card_inserted,
		.card_readonly	= pb1200_mmc1_card_readonly,
		.led		= &pb1200_mmc1_led,
	},
};

static struct resource au1200_mmc0_resources[] = {
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

static u64 au1xxx_mmc_dmamask =	 DMA_BIT_MASK(32);

static struct platform_device db1200_mmc0_dev = {
	.name		= "au1xxx-mmc",
	.id		= 0,
	.dev = {
		.dma_mask		= &au1xxx_mmc_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &db1200_mmc_platdata[0],
	},
	.num_resources	= ARRAY_SIZE(au1200_mmc0_resources),
	.resource	= au1200_mmc0_resources,
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
		.platform_data		= &db1200_mmc_platdata[1],
	},
	.num_resources	= ARRAY_SIZE(au1200_mmc1_res),
	.resource	= au1200_mmc1_res,
};

/**********************************************************************/

static int db1200fb_panel_index(void)
{
	return (bcsr_read(BCSR_SWITCHES) >> 8) & 0x0f;
}

static int db1200fb_panel_init(void)
{
	/* Apply power */
	bcsr_mod(BCSR_BOARD, 0, BCSR_BOARD_LCDVEE | BCSR_BOARD_LCDVDD |
				BCSR_BOARD_LCDBL);
	return 0;
}

static int db1200fb_panel_shutdown(void)
{
	/* Remove power */
	bcsr_mod(BCSR_BOARD, BCSR_BOARD_LCDVEE | BCSR_BOARD_LCDVDD |
			     BCSR_BOARD_LCDBL, 0);
	return 0;
}

static struct au1200fb_platdata db1200fb_pd = {
	.panel_index	= db1200fb_panel_index,
	.panel_init	= db1200fb_panel_init,
	.panel_shutdown = db1200fb_panel_shutdown,
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

static struct platform_device au1200_lcd_dev = {
	.name		= "au1200-lcd",
	.id		= 0,
	.dev = {
		.dma_mask		= &au1200_lcd_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &db1200fb_pd,
	},
	.num_resources	= ARRAY_SIZE(au1200_lcd_res),
	.resource	= au1200_lcd_res,
};

/**********************************************************************/

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

static struct platform_device db1200_i2c_dev = {
	.name		= "au1xpsc_smbus",
	.id		= 0,	/* bus number */
	.num_resources	= ARRAY_SIZE(au1200_psc0_res),
	.resource	= au1200_psc0_res,
};

static void db1200_spi_cs_en(struct au1550_spi_info *spi, int cs, int pol)
{
	if (cs)
		bcsr_mod(BCSR_RESETS, 0, BCSR_RESETS_SPISEL);
	else
		bcsr_mod(BCSR_RESETS, BCSR_RESETS_SPISEL, 0);
}

static struct au1550_spi_info db1200_spi_platdata = {
	.mainclk_hz	= 50000000,	/* PSC0 clock */
	.num_chipselect = 2,
	.activate_cs	= db1200_spi_cs_en,
};

static u64 spi_dmamask = DMA_BIT_MASK(32);

static struct platform_device db1200_spi_dev = {
	.dev	= {
		.dma_mask		= &spi_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &db1200_spi_platdata,
	},
	.name		= "au1550-spi",
	.id		= 0,	/* bus number */
	.num_resources	= ARRAY_SIZE(au1200_psc0_res),
	.resource	= au1200_psc0_res,
};

static struct resource au1200_psc1_res[] = {
	[0] = {
		.start	= AU1550_PSC1_PHYS_ADDR,
		.end	= AU1550_PSC1_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1200_PSC1_INT,
		.end	= AU1200_PSC1_INT,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AU1200_DSCR_CMD0_PSC1_TX,
		.end	= AU1200_DSCR_CMD0_PSC1_TX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= AU1200_DSCR_CMD0_PSC1_RX,
		.end	= AU1200_DSCR_CMD0_PSC1_RX,
		.flags	= IORESOURCE_DMA,
	},
};

/* AC97 or I2S device */
static struct platform_device db1200_audio_dev = {
	/* name assigned later based on switch setting */
	.id		= 1,	/* PSC ID */
	.num_resources	= ARRAY_SIZE(au1200_psc1_res),
	.resource	= au1200_psc1_res,
};

/* DB1200 ASoC card device */
static struct platform_device db1200_sound_dev = {
	/* name assigned later based on switch setting */
	.id		= 1,	/* PSC ID */
};

static struct platform_device db1200_stac_dev = {
	.name		= "ac97-codec",
	.id		= 1,	/* on PSC1 */
};

static struct platform_device db1200_audiodma_dev = {
	.name		= "au1xpsc-pcm",
	.id		= 1,	/* PSC ID */
};

static struct platform_device *db1200_devs[] __initdata = {
	NULL,		/* PSC0, selected by S6.8 */
	&db1200_ide_dev,
	&db1200_mmc0_dev,
	&au1200_lcd_dev,
	&db1200_eth_dev,
	&db1200_nand_dev,
	&db1200_audiodma_dev,
	&db1200_audio_dev,
	&db1200_stac_dev,
	&db1200_sound_dev,
};

static struct platform_device *pb1200_devs[] __initdata = {
	&pb1200_mmc1_dev,
};

/* Some peripheral base addresses differ on the PB1200 */
static int __init pb1200_res_fixup(void)
{
	/* CPLD Revs earlier than 4 cause problems */
	if (BCSR_WHOAMI_CPLD(bcsr_read(BCSR_WHOAMI)) <= 3) {
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "PB1200 must be at CPLD rev 4. Please have\n");
		printk(KERN_ERR "the board updated to latest revisions.\n");
		printk(KERN_ERR "This software will not work reliably\n");
		printk(KERN_ERR "on anything older than CPLD rev 4.!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		return 1;
	}

	db1200_nand_res[0].start = PB1200_NAND_PHYS_ADDR;
	db1200_nand_res[0].end	 = PB1200_NAND_PHYS_ADDR + 0xff;
	db1200_ide_res[0].start = PB1200_IDE_PHYS_ADDR;
	db1200_ide_res[0].end	= PB1200_IDE_PHYS_ADDR + DB1200_IDE_PHYS_LEN - 1;
	db1200_eth_res[0].start = PB1200_ETH_PHYS_ADDR;
	db1200_eth_res[0].end	= PB1200_ETH_PHYS_ADDR + 0xff;
	return 0;
}

int __init db1200_dev_setup(void)
{
	unsigned long pfc;
	unsigned short sw;
	int swapped, bid;

	bid = BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI));
	if ((bid == BCSR_WHOAMI_PB1200_DDR1) ||
	    (bid == BCSR_WHOAMI_PB1200_DDR2)) {
		if (pb1200_res_fixup())
			return -ENODEV;
	}

	/* GPIO7 is low-level triggered CPLD cascade */
	irq_set_irq_type(AU1200_GPIO7_INT, IRQ_TYPE_LEVEL_LOW);
	bcsr_init_irq(DB1200_INT_BEGIN, DB1200_INT_END, AU1200_GPIO7_INT);

	/* insert/eject pairs: one of both is always screaming.	 To avoid
	 * issues they must not be automatically enabled when initially
	 * requested.
	 */
	irq_set_status_flags(DB1200_SD0_INSERT_INT, IRQ_NOAUTOEN);
	irq_set_status_flags(DB1200_SD0_EJECT_INT, IRQ_NOAUTOEN);
	irq_set_status_flags(DB1200_PC0_INSERT_INT, IRQ_NOAUTOEN);
	irq_set_status_flags(DB1200_PC0_EJECT_INT, IRQ_NOAUTOEN);
	irq_set_status_flags(DB1200_PC1_INSERT_INT, IRQ_NOAUTOEN);
	irq_set_status_flags(DB1200_PC1_EJECT_INT, IRQ_NOAUTOEN);

	i2c_register_board_info(0, db1200_i2c_devs,
				ARRAY_SIZE(db1200_i2c_devs));
	spi_register_board_info(db1200_spi_devs,
				ARRAY_SIZE(db1200_i2c_devs));

	/* SWITCHES:	S6.8 I2C/SPI selector  (OFF=I2C	 ON=SPI)
	 *		S6.7 AC97/I2S selector (OFF=AC97 ON=I2S)
	 *		or S12 on the PB1200.
	 */

	/* NOTE: GPIO215 controls OTG VBUS supply.  In SPI mode however
	 * this pin is claimed by PSC0 (unused though, but pinmux doesn't
	 * allow to free it without crippling the SPI interface).
	 * As a result, in SPI mode, OTG simply won't work (PSC0 uses
	 * it as an input pin which is pulled high on the boards).
	 */
	pfc = __raw_readl((void __iomem *)SYS_PINFUNC) & ~SYS_PINFUNC_P0A;

	/* switch off OTG VBUS supply */
	gpio_request(215, "otg-vbus");
	gpio_direction_output(215, 1);

	printk(KERN_INFO "%s device configuration:\n", get_system_type());

	sw = bcsr_read(BCSR_SWITCHES);
	if (sw & BCSR_SWITCHES_DIP_8) {
		db1200_devs[0] = &db1200_i2c_dev;
		bcsr_mod(BCSR_RESETS, BCSR_RESETS_PSC0MUX, 0);

		pfc |= (2 << 17);	/* GPIO2 block owns GPIO215 */

		printk(KERN_INFO " S6.8 OFF: PSC0 mode I2C\n");
		printk(KERN_INFO "   OTG port VBUS supply available!\n");
	} else {
		db1200_devs[0] = &db1200_spi_dev;
		bcsr_mod(BCSR_RESETS, 0, BCSR_RESETS_PSC0MUX);

		pfc |= (1 << 17);	/* PSC0 owns GPIO215 */

		printk(KERN_INFO " S6.8 ON : PSC0 mode SPI\n");
		printk(KERN_INFO "   OTG port VBUS supply disabled\n");
	}
	__raw_writel(pfc, (void __iomem *)SYS_PINFUNC);
	wmb();

	/* Audio: DIP7 selects I2S(0)/AC97(1), but need I2C for I2S!
	 * so: DIP7=1 || DIP8=0 => AC97, DIP7=0 && DIP8=1 => I2S
	 */
	sw &= BCSR_SWITCHES_DIP_8 | BCSR_SWITCHES_DIP_7;
	if (sw == BCSR_SWITCHES_DIP_8) {
		bcsr_mod(BCSR_RESETS, 0, BCSR_RESETS_PSC1MUX);
		db1200_audio_dev.name = "au1xpsc_i2s";
		db1200_sound_dev.name = "db1200-i2s";
		printk(KERN_INFO " S6.7 ON : PSC1 mode I2S\n");
	} else {
		bcsr_mod(BCSR_RESETS, BCSR_RESETS_PSC1MUX, 0);
		db1200_audio_dev.name = "au1xpsc_ac97";
		db1200_sound_dev.name = "db1200-ac97";
		printk(KERN_INFO " S6.7 OFF: PSC1 mode AC97\n");
	}

	/* Audio PSC clock is supplied externally. (FIXME: platdata!!) */
	__raw_writel(PSC_SEL_CLK_SERCLK,
	    (void __iomem *)KSEG1ADDR(AU1550_PSC1_PHYS_ADDR) + PSC_SEL_OFFSET);
	wmb();

	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x000400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x000400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x000010000 - 1,
		DB1200_PC0_INT, DB1200_PC0_INSERT_INT,
		/*DB1200_PC0_STSCHG_INT*/0, DB1200_PC0_EJECT_INT, 0);

	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x004000000,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x004400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x004000000,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x004400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x004000000,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x004010000 - 1,
		DB1200_PC1_INT, DB1200_PC1_INSERT_INT,
		/*DB1200_PC1_STSCHG_INT*/0, DB1200_PC1_EJECT_INT, 1);

	swapped = bcsr_read(BCSR_STATUS) & BCSR_STATUS_DB1200_SWAPBOOT;
	db1x_register_norflash(64 << 20, 2, swapped);

	platform_add_devices(db1200_devs, ARRAY_SIZE(db1200_devs));

	/* PB1200 is a DB1200 with a 2nd MMC and Camera connector */
	if ((bid == BCSR_WHOAMI_PB1200_DDR1) ||
	    (bid == BCSR_WHOAMI_PB1200_DDR2))
		platform_add_devices(pb1200_devs, ARRAY_SIZE(pb1200_devs));

	return 0;
}
