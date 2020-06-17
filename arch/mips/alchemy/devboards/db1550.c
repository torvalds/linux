// SPDX-License-Identifier: GPL-2.0
/*
 * Alchemy Db1550/Pb1550 board support
 *
 * (c) 2011 Manuel Lauss <manuel.lauss@googlemail.com>
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/platnand.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <asm/bootinfo.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/gpio-au1000.h>
#include <asm/mach-au1x00/au1xxx_eth.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#include <asm/mach-au1x00/au1xxx_psc.h>
#include <asm/mach-au1x00/au1550_spi.h>
#include <asm/mach-au1x00/au1550nd.h>
#include <asm/mach-db1x00/bcsr.h>
#include <prom.h>
#include "platform.h"

static void __init db1550_hw_setup(void)
{
	void __iomem *base;
	unsigned long v;

	/* complete pin setup: assign GPIO16 to PSC0_SYNC1 (SPI cs# line)
	 * as well as PSC1_SYNC for AC97 on PB1550.
	 */
	v = alchemy_rdsys(AU1000_SYS_PINFUNC);
	alchemy_wrsys(v | 1 | SYS_PF_PSC1_S1, AU1000_SYS_PINFUNC);

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
}

int __init db1550_board_setup(void)
{
	unsigned short whoami;

	bcsr_init(DB1550_BCSR_PHYS_ADDR,
		  DB1550_BCSR_PHYS_ADDR + DB1550_BCSR_HEXLED_OFS);

	whoami = bcsr_read(BCSR_WHOAMI); /* PB1550 hexled offset differs */
	switch (BCSR_WHOAMI_BOARD(whoami)) {
	case BCSR_WHOAMI_PB1550_SDR:
	case BCSR_WHOAMI_PB1550_DDR:
		bcsr_init(PB1550_BCSR_PHYS_ADDR,
			  PB1550_BCSR_PHYS_ADDR + PB1550_BCSR_HEXLED_OFS);
	case BCSR_WHOAMI_DB1550:
		break;
	default:
		return -ENODEV;
	}

	pr_info("Alchemy/AMD %s Board, CPLD Rev %d Board-ID %d	"	\
		"Daughtercard ID %d\n", get_system_type(),
		(whoami >> 4) & 0xf, (whoami >> 8) & 0xf, whoami & 0xf);

	db1550_hw_setup();
	return 0;
}

/*****************************************************************************/

static u64 au1550_all_dmamask = DMA_BIT_MASK(32);

static struct mtd_partition db1550_spiflash_parts[] = {
	{
		.name	= "spi_flash",
		.offset = 0,
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

static void au1550_nand_cmd_ctrl(struct nand_chip *this, int cmd,
				 unsigned int ctrl)
{
	unsigned long ioaddr = (unsigned long)this->legacy.IO_ADDR_W;

	ioaddr &= 0xffffff00;

	if (ctrl & NAND_CLE) {
		ioaddr += MEM_STNAND_CMD;
	} else if (ctrl & NAND_ALE) {
		ioaddr += MEM_STNAND_ADDR;
	} else {
		/* assume we want to r/w real data  by default */
		ioaddr += MEM_STNAND_DATA;
	}
	this->legacy.IO_ADDR_R = this->legacy.IO_ADDR_W = (void __iomem *)ioaddr;
	if (cmd != NAND_CMD_NONE) {
		__raw_writeb(cmd, this->legacy.IO_ADDR_W);
		wmb();
	}
}

static int au1550_nand_device_ready(struct nand_chip *this)
{
	return alchemy_rdsmem(AU1000_MEM_STSTAT) & 1;
}

static struct mtd_partition db1550_nand_parts[] = {
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

static struct au1550nd_platdata pb1550_nand_pd = {
	.parts		= db1550_nand_parts,
	.num_parts	= ARRAY_SIZE(db1550_nand_parts),
	.devwidth	= 0,	/* x8 NAND default, needs fixing up */
};

static struct platform_device pb1550_nand_dev = {
	.name		= "au1550-nand",
	.id		= -1,
	.resource	= db1550_nand_res,
	.num_resources	= ARRAY_SIZE(db1550_nand_res),
	.dev		= {
		.platform_data	= &pb1550_nand_pd,
	},
};

static void __init pb1550_nand_setup(void)
{
	int boot_swapboot = (alchemy_rdsmem(AU1000_MEM_STSTAT) & (0x7 << 1)) |
			    ((bcsr_read(BCSR_STATUS) >> 6) & 0x1);

	gpio_direction_input(206);	/* de-assert NAND CS# */
	switch (boot_swapboot) {
	case 0: case 2: case 8: case 0xC: case 0xD:
		/* x16 NAND Flash */
		pb1550_nand_pd.devwidth = 1;
		fallthrough;
	case 1: case 3: case 9: case 0xE: case 0xF:
		/* x8 NAND, already set up */
		platform_device_register(&pb1550_nand_dev);
	}
}

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


static struct platform_device db1550_spi_dev = {
	.dev	= {
		.dma_mask		= &au1550_all_dmamask,
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
	.dev = {
		.dma_mask		= &au1550_all_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

static struct platform_device db1550_sndi2s_dev = {
	.name		= "db1550-i2s",
	.dev = {
		.dma_mask		= &au1550_all_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
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

static int pb1550_map_pci_irq(const struct pci_dev *d, u8 slot, u8 pin)
{
	if ((slot < 12) || (slot > 13) || pin == 0)
		return -1;
	if (slot == 12) {
		switch (pin) {
		case 1: return AU1500_PCI_INTB;
		case 2: return AU1500_PCI_INTC;
		case 3: return AU1500_PCI_INTD;
		case 4: return AU1500_PCI_INTA;
		}
	}
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
int __init db1550_pci_setup(int id)
{
	if (id)
		db1550_pci_pd.board_map_irq = pb1550_map_pci_irq;
	return platform_device_register(&db1550_pci_host_dev);
}

static void __init db1550_devices(void)
{
	alchemy_gpio_direction_output(203, 0);	/* red led on */

	irq_set_irq_type(AU1550_GPIO0_INT, IRQ_TYPE_EDGE_BOTH);	 /* CD0# */
	irq_set_irq_type(AU1550_GPIO1_INT, IRQ_TYPE_EDGE_BOTH);	 /* CD1# */
	irq_set_irq_type(AU1550_GPIO3_INT, IRQ_TYPE_LEVEL_LOW);	 /* CARD0# */
	irq_set_irq_type(AU1550_GPIO5_INT, IRQ_TYPE_LEVEL_LOW);	 /* CARD1# */
	irq_set_irq_type(AU1550_GPIO21_INT, IRQ_TYPE_LEVEL_LOW); /* STSCHG0# */
	irq_set_irq_type(AU1550_GPIO22_INT, IRQ_TYPE_LEVEL_LOW); /* STSCHG1# */

	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x000400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x000400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x000010000 - 1,
		AU1550_GPIO3_INT, 0,
		/*AU1550_GPIO21_INT*/0, 0, 0);

	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x004000000,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x004400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x004000000,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x004400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x004000000,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x004010000 - 1,
		AU1550_GPIO5_INT, 1,
		/*AU1550_GPIO22_INT*/0, 0, 1);

	platform_device_register(&db1550_nand_dev);

	alchemy_gpio_direction_output(202, 0);	/* green led on */
}

static void __init pb1550_devices(void)
{
	irq_set_irq_type(AU1550_GPIO0_INT, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(AU1550_GPIO1_INT, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(AU1550_GPIO201_205_INT, IRQ_TYPE_LEVEL_HIGH);

	/* enable both PCMCIA card irqs in the shared line */
	alchemy_gpio2_enable_int(201);	/* socket 0 card irq */
	alchemy_gpio2_enable_int(202);	/* socket 1 card irq */

	/* Pb1550, like all others, also has statuschange irqs; however they're
	* wired up on one of the Au1550's shared GPIO201_205 line, which also
	* services the PCMCIA card interrupts.	So we ignore statuschange and
	* use the GPIO201_205 exclusively for card interrupts, since a) pcmcia
	* drivers are used to shared irqs and b) statuschange isn't really use-
	* ful anyway.
	*/
	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x000400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x000400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x000010000 - 1,
		AU1550_GPIO201_205_INT, AU1550_GPIO0_INT, 0, 0, 0);

	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x008000000,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x008400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x008000000,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x008400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x008000000,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x008010000 - 1,
		AU1550_GPIO201_205_INT, AU1550_GPIO1_INT, 0, 0, 1);

	pb1550_nand_setup();
}

int __init db1550_dev_setup(void)
{
	int swapped, id;
	struct clk *c;

	id = (BCSR_WHOAMI_BOARD(bcsr_read(BCSR_WHOAMI)) != BCSR_WHOAMI_DB1550);

	i2c_register_board_info(0, db1550_i2c_devs,
				ARRAY_SIZE(db1550_i2c_devs));
	spi_register_board_info(db1550_spi_devs,
				ARRAY_SIZE(db1550_i2c_devs));

	c = clk_get(NULL, "psc0_intclk");
	if (!IS_ERR(c)) {
		clk_set_rate(c, 50000000);
		clk_prepare_enable(c);
		clk_put(c);
	}
	c = clk_get(NULL, "psc2_intclk");
	if (!IS_ERR(c)) {
		clk_set_rate(c, db1550_spi_platdata.mainclk_hz);
		clk_prepare_enable(c);
		clk_put(c);
	}

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

	id ? pb1550_devices() : db1550_devices();

	swapped = bcsr_read(BCSR_STATUS) &
	       (id ? BCSR_STATUS_PB1550_SWAPBOOT : BCSR_STATUS_DB1000_SWAPBOOT);
	db1x_register_norflash(128 << 20, 4, swapped);

	return platform_add_devices(db1550_devs, ARRAY_SIZE(db1550_devs));
}
