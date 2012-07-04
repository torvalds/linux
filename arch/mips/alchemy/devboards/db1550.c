/*
 * Alchemy Db1550 board support
 *
 * (c) 2011 Manuel Lauss <manuel.lauss@googlemail.com>
 */

#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_eth.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#include <asm/mach-au1x00/au1xxx_psc.h>
#include <asm/mach-au1x00/au1550_spi.h>
#include <asm/mach-db1x00/bcsr.h>
#include <prom.h>
#include "platform.h"


const char *get_system_type(void)
{
	return "DB1550";
}

static void __init db1550_hw_setup(void)
{
	void __iomem *base;

	alchemy_gpio_direction_output(203, 0);	/* red led on */

	/* complete SPI setup: link psc0_intclk to a 48MHz source,
	 * and assign GPIO16 to PSC0_SYNC1 (SPI cs# line)
	 */
	base = (void __iomem *)SYS_CLKSRC;
	__raw_writel(__raw_readl(base) | 0x000001e0, base);
	base = (void __iomem *)SYS_PINFUNC;
	__raw_writel(__raw_readl(base) | 1, base);
	wmb();

	/* reset the AC97 codec now, the reset time in the psc-ac97 driver
	 * is apparently too short although it's ridiculous as it is.
	 */
	base = (void __iomem *)KSEG1ADDR(AU1550_PSC1_PHYS_ADDR);
	__raw_writel(PSC_SEL_CLK_SERCLK | PSC_SEL_PS_AC97MODE,
		     base + PSC_SEL_OFFSET);
	__raw_writel(PSC_CTRL_DISABLE, base + PSC_CTRL_OFFSET);
	wmb();
	__raw_writel(PSC_AC97RST_RST, base + PSC_AC97RST_OFFSET);
	wmb();

	alchemy_gpio_direction_output(202, 0);	/* green led on */
}

void __init board_setup(void)
{
	unsigned short whoami;

	bcsr_init(DB1550_BCSR_PHYS_ADDR,
		  DB1550_BCSR_PHYS_ADDR + DB1550_BCSR_HEXLED_OFS);

	whoami = bcsr_read(BCSR_WHOAMI);
	printk(KERN_INFO "Alchemy/AMD DB1550 Board, CPLD Rev %d"
		"  Board-ID %d  Daughtercard ID %d\n",
		(whoami >> 4) & 0xf, (whoami >> 8) & 0xf, whoami & 0xf);

	db1550_hw_setup();
}

/*****************************************************************************/

static struct mtd_partition db1550_spiflash_parts[] = {
	{
		.name	= "spi_flash",
		.offset	= 0,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct flash_platform_data db1550_spiflash_data = {
	.name		= "s25fl010",
	.parts		= db1550_spiflash_parts,
	.nr_parts	= ARRAY_SIZE(db1550_spiflash_parts),
	.type		= "m25p10",
};

static struct spi_board_info db1550_spi_devs[] __initdata = {
	{
		/* TI TMP121AIDBVR temp sensor */
		.modalias	= "tmp121",
		.max_speed_hz	= 2400000,
		.bus_num	= 0,
		.chip_select	= 0,
		.mode		= SPI_MODE_0,
	},
	{
		/* Spansion S25FL001D0FMA SPI flash */
		.modalias	= "m25p80",
		.max_speed_hz	= 2400000,
		.bus_num	= 0,
		.chip_select	= 1,
		.mode		= SPI_MODE_0,
		.platform_data	= &db1550_spiflash_data,
	},
};

static struct i2c_board_info db1550_i2c_devs[] __initdata = {
	{ I2C_BOARD_INFO("24c04",  0x52),}, /* AT24C04-10 I2C eeprom */
	{ I2C_BOARD_INFO("ne1619", 0x2d),}, /* adm1025-compat hwmon */
	{ I2C_BOARD_INFO("wm8731", 0x1b),}, /* I2S audio codec WM8731 */
};

/**********************************************************************/

static void au1550_nand_cmd_ctrl(struct mtd_info *mtd, int cmd,
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

static int au1550_nand_device_ready(struct mtd_info *mtd)
{
	return __raw_readl((void __iomem *)MEM_STSTAT) & 1;
}

static struct mtd_partition db1550_nand_parts[] = {
	{
		.name	= "NAND FS 0",
		.offset	= 0,
		.size	= 8 * 1024 * 1024,
	},
	{
		.name	= "NAND FS 1",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL
	},
};

struct platform_nand_data db1550_nand_platdata = {
	.chip = {
		.nr_chips	= 1,
		.chip_offset	= 0,
		.nr_partitions	= ARRAY_SIZE(db1550_nand_parts),
		.partitions	= db1550_nand_parts,
		.chip_delay	= 20,
	},
	.ctrl = {
		.dev_ready	= au1550_nand_device_ready,
		.cmd_ctrl	= au1550_nand_cmd_ctrl,
	},
};

static struct resource db1550_nand_res[] = {
	[0] = {
		.start	= 0x20000000,
		.end	= 0x200000ff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device db1550_nand_dev = {
	.name		= "gen_nand",
	.num_resources	= ARRAY_SIZE(db1550_nand_res),
	.resource	= db1550_nand_res,
	.id		= -1,
	.dev		= {
		.platform_data = &db1550_nand_platdata,
	}
};

/**********************************************************************/

static struct resource au1550_psc0_res[] = {
	[0] = {
		.start	= AU1550_PSC0_PHYS_ADDR,
		.end	= AU1550_PSC0_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1550_PSC0_INT,
		.end	= AU1550_PSC0_INT,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AU1550_DSCR_CMD0_PSC0_TX,
		.end	= AU1550_DSCR_CMD0_PSC0_TX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= AU1550_DSCR_CMD0_PSC0_RX,
		.end	= AU1550_DSCR_CMD0_PSC0_RX,
		.flags	= IORESOURCE_DMA,
	},
};

static void db1550_spi_cs_en(struct au1550_spi_info *spi, int cs, int pol)
{
	if (cs)
		bcsr_mod(BCSR_BOARD, 0, BCSR_BOARD_SPISEL);
	else
		bcsr_mod(BCSR_BOARD, BCSR_BOARD_SPISEL, 0);
}

static struct au1550_spi_info db1550_spi_platdata = {
	.mainclk_hz	= 48000000,	/* PSC0 clock: max. 2.4MHz SPI clk */
	.num_chipselect = 2,
	.activate_cs	= db1550_spi_cs_en,
};

static u64 spi_dmamask = DMA_BIT_MASK(32);

static struct platform_device db1550_spi_dev = {
	.dev	= {
		.dma_mask		= &spi_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &db1550_spi_platdata,
	},
	.name		= "au1550-spi",
	.id		= 0,	/* bus number */
	.num_resources	= ARRAY_SIZE(au1550_psc0_res),
	.resource	= au1550_psc0_res,
};

/**********************************************************************/

static struct resource au1550_psc1_res[] = {
	[0] = {
		.start	= AU1550_PSC1_PHYS_ADDR,
		.end	= AU1550_PSC1_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1550_PSC1_INT,
		.end	= AU1550_PSC1_INT,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AU1550_DSCR_CMD0_PSC1_TX,
		.end	= AU1550_DSCR_CMD0_PSC1_TX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= AU1550_DSCR_CMD0_PSC1_RX,
		.end	= AU1550_DSCR_CMD0_PSC1_RX,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device db1550_ac97_dev = {
	.name		= "au1xpsc_ac97",
	.id		= 1,	/* PSC ID */
	.num_resources	= ARRAY_SIZE(au1550_psc1_res),
	.resource	= au1550_psc1_res,
};


static struct resource au1550_psc2_res[] = {
	[0] = {
		.start	= AU1550_PSC2_PHYS_ADDR,
		.end	= AU1550_PSC2_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1550_PSC2_INT,
		.end	= AU1550_PSC2_INT,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AU1550_DSCR_CMD0_PSC2_TX,
		.end	= AU1550_DSCR_CMD0_PSC2_TX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= AU1550_DSCR_CMD0_PSC2_RX,
		.end	= AU1550_DSCR_CMD0_PSC2_RX,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device db1550_i2c_dev = {
	.name		= "au1xpsc_smbus",
	.id		= 0,	/* bus number */
	.num_resources	= ARRAY_SIZE(au1550_psc2_res),
	.resource	= au1550_psc2_res,
};

/**********************************************************************/

static struct resource au1550_psc3_res[] = {
	[0] = {
		.start	= AU1550_PSC3_PHYS_ADDR,
		.end	= AU1550_PSC3_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1550_PSC3_INT,
		.end	= AU1550_PSC3_INT,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AU1550_DSCR_CMD0_PSC3_TX,
		.end	= AU1550_DSCR_CMD0_PSC3_TX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= AU1550_DSCR_CMD0_PSC3_RX,
		.end	= AU1550_DSCR_CMD0_PSC3_RX,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device db1550_i2s_dev = {
	.name		= "au1xpsc_i2s",
	.id		= 3,	/* PSC ID */
	.num_resources	= ARRAY_SIZE(au1550_psc3_res),
	.resource	= au1550_psc3_res,
};

/**********************************************************************/

static struct platform_device db1550_stac_dev = {
	.name		= "ac97-codec",
	.id		= 1,	/* on PSC1 */
};

static struct platform_device db1550_ac97dma_dev = {
	.name		= "au1xpsc-pcm",
	.id		= 1,	/* on PSC3 */
};

static struct platform_device db1550_i2sdma_dev = {
	.name		= "au1xpsc-pcm",
	.id		= 3,	/* on PSC3 */
};

static struct platform_device db1550_sndac97_dev = {
	.name		= "db1550-ac97",
};

static struct platform_device db1550_sndi2s_dev = {
	.name		= "db1550-i2s",
};

/**********************************************************************/

static int db1550_map_pci_irq(const struct pci_dev *d, u8 slot, u8 pin)
{
	if ((slot < 11) || (slot > 13) || pin == 0)
		return -1;
	if (slot == 11)
		return (pin == 1) ? AU1550_PCI_INTC : 0xff;
	if (slot == 12) {
		switch (pin) {
		case 1: return AU1550_PCI_INTB;
		case 2: return AU1550_PCI_INTC;
		case 3: return AU1550_PCI_INTD;
		case 4: return AU1550_PCI_INTA;
		}
	}
	if (slot == 13) {
		switch (pin) {
		case 1: return AU1550_PCI_INTA;
		case 2: return AU1550_PCI_INTB;
		case 3: return AU1550_PCI_INTC;
		case 4: return AU1550_PCI_INTD;
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

static struct alchemy_pci_platdata db1550_pci_pd = {
	.board_map_irq	= db1550_map_pci_irq,
};

static struct platform_device db1550_pci_host_dev = {
	.dev.platform_data = &db1550_pci_pd,
	.name		= "alchemy-pci",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(alchemy_pci_host_res),
	.resource	= alchemy_pci_host_res,
};

/**********************************************************************/

static struct platform_device *db1550_devs[] __initdata = {
	&db1550_nand_dev,
	&db1550_i2c_dev,
	&db1550_ac97_dev,
	&db1550_spi_dev,
	&db1550_i2s_dev,
	&db1550_stac_dev,
	&db1550_ac97dma_dev,
	&db1550_i2sdma_dev,
	&db1550_sndac97_dev,
	&db1550_sndi2s_dev,
};

/* must be arch_initcall; MIPS PCI scans busses in a subsys_initcall */
static int __init db1550_pci_init(void)
{
	return platform_device_register(&db1550_pci_host_dev);
}
arch_initcall(db1550_pci_init);

static int __init db1550_dev_init(void)
{
	int swapped;

	irq_set_irq_type(AU1550_GPIO0_INT, IRQ_TYPE_EDGE_BOTH);  /* CD0# */
	irq_set_irq_type(AU1550_GPIO1_INT, IRQ_TYPE_EDGE_BOTH);  /* CD1# */
	irq_set_irq_type(AU1550_GPIO3_INT, IRQ_TYPE_LEVEL_LOW);  /* CARD0# */
	irq_set_irq_type(AU1550_GPIO5_INT, IRQ_TYPE_LEVEL_LOW);  /* CARD1# */
	irq_set_irq_type(AU1550_GPIO21_INT, IRQ_TYPE_LEVEL_LOW); /* STSCHG0# */
	irq_set_irq_type(AU1550_GPIO22_INT, IRQ_TYPE_LEVEL_LOW); /* STSCHG1# */

	i2c_register_board_info(0, db1550_i2c_devs,
				ARRAY_SIZE(db1550_i2c_devs));
	spi_register_board_info(db1550_spi_devs,
				ARRAY_SIZE(db1550_i2c_devs));

	/* Audio PSC clock is supplied by codecs (PSC1, 3) FIXME: platdata!! */
	__raw_writel(PSC_SEL_CLK_SERCLK,
	    (void __iomem *)KSEG1ADDR(AU1550_PSC1_PHYS_ADDR) + PSC_SEL_OFFSET);
	wmb();
	__raw_writel(PSC_SEL_CLK_SERCLK,
	    (void __iomem *)KSEG1ADDR(AU1550_PSC3_PHYS_ADDR) + PSC_SEL_OFFSET);
	wmb();
	/* SPI/I2C use internally supplied 50MHz source */
	__raw_writel(PSC_SEL_CLK_INTCLK,
	    (void __iomem *)KSEG1ADDR(AU1550_PSC0_PHYS_ADDR) + PSC_SEL_OFFSET);
	wmb();
	__raw_writel(PSC_SEL_CLK_INTCLK,
	    (void __iomem *)KSEG1ADDR(AU1550_PSC2_PHYS_ADDR) + PSC_SEL_OFFSET);
	wmb();

	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x000400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x000400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x000010000 - 1,
		AU1550_GPIO3_INT, AU1550_GPIO0_INT,
		/*AU1550_GPIO21_INT*/0, 0, 0);

	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x004000000,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x004400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x004000000,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x004400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x004000000,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x004010000 - 1,
		AU1550_GPIO5_INT, AU1550_GPIO1_INT,
		/*AU1550_GPIO22_INT*/0, 0, 1);

	swapped = bcsr_read(BCSR_STATUS) & BCSR_STATUS_DB1000_SWAPBOOT;
	db1x_register_norflash(128 << 20, 4, swapped);

	return platform_add_devices(db1550_devs, ARRAY_SIZE(db1550_devs));
}
device_initcall(db1550_dev_init);
