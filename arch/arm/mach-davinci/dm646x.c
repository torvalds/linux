/*
 * TI DaVinci DM644x chip specific setup
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * 2007 (c) Deep Root Systems, LLC. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/clk-provider.h>
#include <linux/clk/davinci.h>
#include <linux/clkdev.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/init.h>
#include <linux/platform_data/edma.h>
#include <linux/platform_data/gpio-davinci.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>

#include <asm/mach/map.h>

#include <mach/common.h>
#include <mach/cputype.h>
#include <mach/irqs.h>
#include <mach/mux.h>
#include <mach/serial.h>
#include <mach/time.h>

#include "asp.h"
#include "davinci.h"
#include "mux.h"

#define DAVINCI_VPIF_BASE       (0x01C12000)

#define VDD3P3V_VID_MASK	(BIT_MASK(3) | BIT_MASK(2) | BIT_MASK(1) |\
					BIT_MASK(0))
#define VSCLKDIS_MASK		(BIT_MASK(11) | BIT_MASK(10) | BIT_MASK(9) |\
					BIT_MASK(8))

#define DM646X_EMAC_BASE		0x01c80000
#define DM646X_EMAC_MDIO_BASE		(DM646X_EMAC_BASE + 0x4000)
#define DM646X_EMAC_CNTRL_OFFSET	0x0000
#define DM646X_EMAC_CNTRL_MOD_OFFSET	0x1000
#define DM646X_EMAC_CNTRL_RAM_OFFSET	0x2000
#define DM646X_EMAC_CNTRL_RAM_SIZE	0x2000

static struct emac_platform_data dm646x_emac_pdata = {
	.ctrl_reg_offset	= DM646X_EMAC_CNTRL_OFFSET,
	.ctrl_mod_reg_offset	= DM646X_EMAC_CNTRL_MOD_OFFSET,
	.ctrl_ram_offset	= DM646X_EMAC_CNTRL_RAM_OFFSET,
	.ctrl_ram_size		= DM646X_EMAC_CNTRL_RAM_SIZE,
	.version		= EMAC_VERSION_2,
};

static struct resource dm646x_emac_resources[] = {
	{
		.start	= DM646X_EMAC_BASE,
		.end	= DM646X_EMAC_BASE + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_DM646X_EMACRXTHINT,
		.end	= IRQ_DM646X_EMACRXTHINT,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DM646X_EMACRXINT,
		.end	= IRQ_DM646X_EMACRXINT,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DM646X_EMACTXINT,
		.end	= IRQ_DM646X_EMACTXINT,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DM646X_EMACMISCINT,
		.end	= IRQ_DM646X_EMACMISCINT,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dm646x_emac_device = {
	.name		= "davinci_emac",
	.id		= 1,
	.dev = {
		.platform_data	= &dm646x_emac_pdata,
	},
	.num_resources	= ARRAY_SIZE(dm646x_emac_resources),
	.resource	= dm646x_emac_resources,
};

static struct resource dm646x_mdio_resources[] = {
	{
		.start	= DM646X_EMAC_MDIO_BASE,
		.end	= DM646X_EMAC_MDIO_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device dm646x_mdio_device = {
	.name		= "davinci_mdio",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dm646x_mdio_resources),
	.resource	= dm646x_mdio_resources,
};

/*
 * Device specific mux setup
 *
 *	soc	description	mux  mode   mode  mux	 dbg
 *				reg  offset mask  mode
 */
static const struct mux_config dm646x_pins[] = {
#ifdef CONFIG_DAVINCI_MUX
MUX_CFG(DM646X, ATAEN,		0,   0,     5,	  1,	 true)

MUX_CFG(DM646X, AUDCK1,		0,   29,    1,	  0,	 false)

MUX_CFG(DM646X, AUDCK0,		0,   28,    1,	  0,	 false)

MUX_CFG(DM646X, CRGMUX,			0,   24,    7,    5,	 true)

MUX_CFG(DM646X, STSOMUX_DISABLE,	0,   22,    3,    0,	 true)

MUX_CFG(DM646X, STSIMUX_DISABLE,	0,   20,    3,    0,	 true)

MUX_CFG(DM646X, PTSOMUX_DISABLE,	0,   18,    3,    0,	 true)

MUX_CFG(DM646X, PTSIMUX_DISABLE,	0,   16,    3,    0,	 true)

MUX_CFG(DM646X, STSOMUX,		0,   22,    3,    2,	 true)

MUX_CFG(DM646X, STSIMUX,		0,   20,    3,    2,	 true)

MUX_CFG(DM646X, PTSOMUX_PARALLEL,	0,   18,    3,    2,	 true)

MUX_CFG(DM646X, PTSIMUX_PARALLEL,	0,   16,    3,    2,	 true)

MUX_CFG(DM646X, PTSOMUX_SERIAL,		0,   18,    3,    3,	 true)

MUX_CFG(DM646X, PTSIMUX_SERIAL,		0,   16,    3,    3,	 true)
#endif
};

static u8 dm646x_default_priorities[DAVINCI_N_AINTC_IRQ] = {
	[IRQ_DM646X_VP_VERTINT0]        = 7,
	[IRQ_DM646X_VP_VERTINT1]        = 7,
	[IRQ_DM646X_VP_VERTINT2]        = 7,
	[IRQ_DM646X_VP_VERTINT3]        = 7,
	[IRQ_DM646X_VP_ERRINT]          = 7,
	[IRQ_DM646X_RESERVED_1]         = 7,
	[IRQ_DM646X_RESERVED_2]         = 7,
	[IRQ_DM646X_WDINT]              = 7,
	[IRQ_DM646X_CRGENINT0]          = 7,
	[IRQ_DM646X_CRGENINT1]          = 7,
	[IRQ_DM646X_TSIFINT0]           = 7,
	[IRQ_DM646X_TSIFINT1]           = 7,
	[IRQ_DM646X_VDCEINT]            = 7,
	[IRQ_DM646X_USBINT]             = 7,
	[IRQ_DM646X_USBDMAINT]          = 7,
	[IRQ_DM646X_PCIINT]             = 7,
	[IRQ_CCINT0]                    = 7,    /* dma */
	[IRQ_CCERRINT]                  = 7,    /* dma */
	[IRQ_TCERRINT0]                 = 7,    /* dma */
	[IRQ_TCERRINT]                  = 7,    /* dma */
	[IRQ_DM646X_TCERRINT2]          = 7,
	[IRQ_DM646X_TCERRINT3]          = 7,
	[IRQ_DM646X_IDE]                = 7,
	[IRQ_DM646X_HPIINT]             = 7,
	[IRQ_DM646X_EMACRXTHINT]        = 7,
	[IRQ_DM646X_EMACRXINT]          = 7,
	[IRQ_DM646X_EMACTXINT]          = 7,
	[IRQ_DM646X_EMACMISCINT]        = 7,
	[IRQ_DM646X_MCASP0TXINT]        = 7,
	[IRQ_DM646X_MCASP0RXINT]        = 7,
	[IRQ_DM646X_RESERVED_3]         = 7,
	[IRQ_DM646X_MCASP1TXINT]        = 7,
	[IRQ_TINT0_TINT12]              = 7,    /* clockevent */
	[IRQ_TINT0_TINT34]              = 7,    /* clocksource */
	[IRQ_TINT1_TINT12]              = 7,    /* DSP timer */
	[IRQ_TINT1_TINT34]              = 7,    /* system tick */
	[IRQ_PWMINT0]                   = 7,
	[IRQ_PWMINT1]                   = 7,
	[IRQ_DM646X_VLQINT]             = 7,
	[IRQ_I2C]                       = 7,
	[IRQ_UARTINT0]                  = 7,
	[IRQ_UARTINT1]                  = 7,
	[IRQ_DM646X_UARTINT2]           = 7,
	[IRQ_DM646X_SPINT0]             = 7,
	[IRQ_DM646X_SPINT1]             = 7,
	[IRQ_DM646X_DSP2ARMINT]         = 7,
	[IRQ_DM646X_RESERVED_4]         = 7,
	[IRQ_DM646X_PSCINT]             = 7,
	[IRQ_DM646X_GPIO0]              = 7,
	[IRQ_DM646X_GPIO1]              = 7,
	[IRQ_DM646X_GPIO2]              = 7,
	[IRQ_DM646X_GPIO3]              = 7,
	[IRQ_DM646X_GPIO4]              = 7,
	[IRQ_DM646X_GPIO5]              = 7,
	[IRQ_DM646X_GPIO6]              = 7,
	[IRQ_DM646X_GPIO7]              = 7,
	[IRQ_DM646X_GPIOBNK0]           = 7,
	[IRQ_DM646X_GPIOBNK1]           = 7,
	[IRQ_DM646X_GPIOBNK2]           = 7,
	[IRQ_DM646X_DDRINT]             = 7,
	[IRQ_DM646X_AEMIFINT]           = 7,
	[IRQ_COMMTX]                    = 7,
	[IRQ_COMMRX]                    = 7,
	[IRQ_EMUINT]                    = 7,
};

/*----------------------------------------------------------------------*/

/* Four Transfer Controllers on DM646x */
static s8 dm646x_queue_priority_mapping[][2] = {
	/* {event queue no, Priority} */
	{0, 4},
	{1, 0},
	{2, 5},
	{3, 1},
	{-1, -1},
};

static const struct dma_slave_map dm646x_edma_map[] = {
	{ "davinci-mcasp.0", "tx", EDMA_FILTER_PARAM(0, 6) },
	{ "davinci-mcasp.0", "rx", EDMA_FILTER_PARAM(0, 9) },
	{ "davinci-mcasp.1", "tx", EDMA_FILTER_PARAM(0, 12) },
	{ "spi_davinci", "tx", EDMA_FILTER_PARAM(0, 16) },
	{ "spi_davinci", "rx", EDMA_FILTER_PARAM(0, 17) },
};

static struct edma_soc_info dm646x_edma_pdata = {
	.queue_priority_mapping	= dm646x_queue_priority_mapping,
	.default_queue		= EVENTQ_1,
	.slave_map		= dm646x_edma_map,
	.slavecnt		= ARRAY_SIZE(dm646x_edma_map),
};

static struct resource edma_resources[] = {
	{
		.name	= "edma3_cc",
		.start	= 0x01c00000,
		.end	= 0x01c00000 + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma3_tc0",
		.start	= 0x01c10000,
		.end	= 0x01c10000 + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma3_tc1",
		.start	= 0x01c10400,
		.end	= 0x01c10400 + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma3_tc2",
		.start	= 0x01c10800,
		.end	= 0x01c10800 + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma3_tc3",
		.start	= 0x01c10c00,
		.end	= 0x01c10c00 + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma3_ccint",
		.start	= IRQ_CCINT0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "edma3_ccerrint",
		.start	= IRQ_CCERRINT,
		.flags	= IORESOURCE_IRQ,
	},
	/* not using TC*_ERR */
};

static const struct platform_device_info dm646x_edma_device __initconst = {
	.name		= "edma",
	.id		= 0,
	.dma_mask	= DMA_BIT_MASK(32),
	.res		= edma_resources,
	.num_res	= ARRAY_SIZE(edma_resources),
	.data		= &dm646x_edma_pdata,
	.size_data	= sizeof(dm646x_edma_pdata),
};

static struct resource dm646x_mcasp0_resources[] = {
	{
		.name	= "mpu",
		.start 	= DAVINCI_DM646X_MCASP0_REG_BASE,
		.end 	= DAVINCI_DM646X_MCASP0_REG_BASE + (SZ_1K << 1) - 1,
		.flags 	= IORESOURCE_MEM,
	},
	{
		.name	= "tx",
		.start	= DAVINCI_DM646X_DMA_MCASP0_AXEVT0,
		.end	= DAVINCI_DM646X_DMA_MCASP0_AXEVT0,
		.flags	= IORESOURCE_DMA,
	},
	{
		.name	= "rx",
		.start	= DAVINCI_DM646X_DMA_MCASP0_AREVT0,
		.end	= DAVINCI_DM646X_DMA_MCASP0_AREVT0,
		.flags	= IORESOURCE_DMA,
	},
	{
		.name	= "tx",
		.start	= IRQ_DM646X_MCASP0TXINT,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "rx",
		.start	= IRQ_DM646X_MCASP0RXINT,
		.flags	= IORESOURCE_IRQ,
	},
};

/* DIT mode only, rx is not supported */
static struct resource dm646x_mcasp1_resources[] = {
	{
		.name	= "mpu",
		.start	= DAVINCI_DM646X_MCASP1_REG_BASE,
		.end	= DAVINCI_DM646X_MCASP1_REG_BASE + (SZ_1K << 1) - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "tx",
		.start	= DAVINCI_DM646X_DMA_MCASP1_AXEVT1,
		.end	= DAVINCI_DM646X_DMA_MCASP1_AXEVT1,
		.flags	= IORESOURCE_DMA,
	},
	{
		.name	= "tx",
		.start	= IRQ_DM646X_MCASP1TXINT,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dm646x_mcasp0_device = {
	.name		= "davinci-mcasp",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dm646x_mcasp0_resources),
	.resource	= dm646x_mcasp0_resources,
};

static struct platform_device dm646x_mcasp1_device = {
	.name		= "davinci-mcasp",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(dm646x_mcasp1_resources),
	.resource	= dm646x_mcasp1_resources,
};

static struct platform_device dm646x_dit_device = {
	.name	= "spdif-dit",
	.id	= -1,
};

static u64 vpif_dma_mask = DMA_BIT_MASK(32);

static struct resource vpif_resource[] = {
	{
		.start	= DAVINCI_VPIF_BASE,
		.end	= DAVINCI_VPIF_BASE + 0x03ff,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device vpif_dev = {
	.name		= "vpif",
	.id		= -1,
	.dev		= {
			.dma_mask 		= &vpif_dma_mask,
			.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= vpif_resource,
	.num_resources	= ARRAY_SIZE(vpif_resource),
};

static struct resource vpif_display_resource[] = {
	{
		.start = IRQ_DM646X_VP_VERTINT2,
		.end   = IRQ_DM646X_VP_VERTINT2,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_DM646X_VP_VERTINT3,
		.end   = IRQ_DM646X_VP_VERTINT3,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device vpif_display_dev = {
	.name		= "vpif_display",
	.id		= -1,
	.dev		= {
			.dma_mask 		= &vpif_dma_mask,
			.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= vpif_display_resource,
	.num_resources	= ARRAY_SIZE(vpif_display_resource),
};

static struct resource vpif_capture_resource[] = {
	{
		.start = IRQ_DM646X_VP_VERTINT0,
		.end   = IRQ_DM646X_VP_VERTINT0,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_DM646X_VP_VERTINT1,
		.end   = IRQ_DM646X_VP_VERTINT1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device vpif_capture_dev = {
	.name		= "vpif_capture",
	.id		= -1,
	.dev		= {
			.dma_mask 		= &vpif_dma_mask,
			.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= vpif_capture_resource,
	.num_resources	= ARRAY_SIZE(vpif_capture_resource),
};

static struct resource dm646x_gpio_resources[] = {
	{	/* registers */
		.start	= DAVINCI_GPIO_BASE,
		.end	= DAVINCI_GPIO_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{	/* interrupt */
		.start	= IRQ_DM646X_GPIOBNK0,
		.end	= IRQ_DM646X_GPIOBNK0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DM646X_GPIOBNK1,
		.end	= IRQ_DM646X_GPIOBNK1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DM646X_GPIOBNK2,
		.end	= IRQ_DM646X_GPIOBNK2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct davinci_gpio_platform_data dm646x_gpio_platform_data = {
	.ngpio		= 43,
};

int __init dm646x_gpio_register(void)
{
	return davinci_gpio_register(dm646x_gpio_resources,
				     ARRAY_SIZE(dm646x_gpio_resources),
				     &dm646x_gpio_platform_data);
}
/*----------------------------------------------------------------------*/

static struct map_desc dm646x_io_desc[] = {
	{
		.virtual	= IO_VIRT,
		.pfn		= __phys_to_pfn(IO_PHYS),
		.length		= IO_SIZE,
		.type		= MT_DEVICE
	},
};

/* Contents of JTAG ID register used to identify exact cpu type */
static struct davinci_id dm646x_ids[] = {
	{
		.variant	= 0x0,
		.part_no	= 0xb770,
		.manufacturer	= 0x017,
		.cpu_id		= DAVINCI_CPU_ID_DM6467,
		.name		= "dm6467_rev1.x",
	},
	{
		.variant	= 0x1,
		.part_no	= 0xb770,
		.manufacturer	= 0x017,
		.cpu_id		= DAVINCI_CPU_ID_DM6467,
		.name		= "dm6467_rev3.x",
	},
};

/*
 * T0_BOT: Timer 0, bottom:  clockevent source for hrtimers
 * T0_TOP: Timer 0, top   :  clocksource for generic timekeeping
 * T1_BOT: Timer 1, bottom:  (used by DSP in TI DSPLink code)
 * T1_TOP: Timer 1, top   :  <unused>
 */
static struct davinci_timer_info dm646x_timer_info = {
	.timers		= davinci_timer_instance,
	.clockevent_id	= T0_BOT,
	.clocksource_id	= T0_TOP,
};

static struct plat_serial8250_port dm646x_serial0_platform_data[] = {
	{
		.mapbase	= DAVINCI_UART0_BASE,
		.irq		= IRQ_UARTINT0,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
	},
	{
		.flags	= 0,
	}
};
static struct plat_serial8250_port dm646x_serial1_platform_data[] = {
	{
		.mapbase	= DAVINCI_UART1_BASE,
		.irq		= IRQ_UARTINT1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
	},
	{
		.flags	= 0,
	}
};
static struct plat_serial8250_port dm646x_serial2_platform_data[] = {
	{
		.mapbase	= DAVINCI_UART2_BASE,
		.irq		= IRQ_DM646X_UARTINT2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
	},
	{
		.flags	= 0,
	}
};

struct platform_device dm646x_serial_device[] = {
	{
		.name			= "serial8250",
		.id			= PLAT8250_DEV_PLATFORM,
		.dev			= {
			.platform_data	= dm646x_serial0_platform_data,
		}
	},
	{
		.name			= "serial8250",
		.id			= PLAT8250_DEV_PLATFORM1,
		.dev			= {
			.platform_data	= dm646x_serial1_platform_data,
		}
	},
	{
		.name			= "serial8250",
		.id			= PLAT8250_DEV_PLATFORM2,
		.dev			= {
			.platform_data	= dm646x_serial2_platform_data,
		}
	},
	{
	}
};

static const struct davinci_soc_info davinci_soc_info_dm646x = {
	.io_desc		= dm646x_io_desc,
	.io_desc_num		= ARRAY_SIZE(dm646x_io_desc),
	.jtag_id_reg		= 0x01c40028,
	.ids			= dm646x_ids,
	.ids_num		= ARRAY_SIZE(dm646x_ids),
	.pinmux_base		= DAVINCI_SYSTEM_MODULE_BASE,
	.pinmux_pins		= dm646x_pins,
	.pinmux_pins_num	= ARRAY_SIZE(dm646x_pins),
	.intc_base		= DAVINCI_ARM_INTC_BASE,
	.intc_type		= DAVINCI_INTC_TYPE_AINTC,
	.intc_irq_prios		= dm646x_default_priorities,
	.intc_irq_num		= DAVINCI_N_AINTC_IRQ,
	.timer_info		= &dm646x_timer_info,
	.emac_pdata		= &dm646x_emac_pdata,
	.sram_dma		= 0x10010000,
	.sram_len		= SZ_32K,
};

void __init dm646x_init_mcasp0(struct snd_platform_data *pdata)
{
	dm646x_mcasp0_device.dev.platform_data = pdata;
	platform_device_register(&dm646x_mcasp0_device);
}

void __init dm646x_init_mcasp1(struct snd_platform_data *pdata)
{
	dm646x_mcasp1_device.dev.platform_data = pdata;
	platform_device_register(&dm646x_mcasp1_device);
	platform_device_register(&dm646x_dit_device);
}

void dm646x_setup_vpif(struct vpif_display_config *display_config,
		       struct vpif_capture_config *capture_config)
{
	unsigned int value;

	value = __raw_readl(DAVINCI_SYSMOD_VIRT(SYSMOD_VSCLKDIS));
	value &= ~VSCLKDIS_MASK;
	__raw_writel(value, DAVINCI_SYSMOD_VIRT(SYSMOD_VSCLKDIS));

	value = __raw_readl(DAVINCI_SYSMOD_VIRT(SYSMOD_VDD3P3VPWDN));
	value &= ~VDD3P3V_VID_MASK;
	__raw_writel(value, DAVINCI_SYSMOD_VIRT(SYSMOD_VDD3P3VPWDN));

	davinci_cfg_reg(DM646X_STSOMUX_DISABLE);
	davinci_cfg_reg(DM646X_STSIMUX_DISABLE);
	davinci_cfg_reg(DM646X_PTSOMUX_DISABLE);
	davinci_cfg_reg(DM646X_PTSIMUX_DISABLE);

	vpif_display_dev.dev.platform_data = display_config;
	vpif_capture_dev.dev.platform_data = capture_config;
	platform_device_register(&vpif_dev);
	platform_device_register(&vpif_display_dev);
	platform_device_register(&vpif_capture_dev);
}

int __init dm646x_init_edma(struct edma_rsv_info *rsv)
{
	struct platform_device *edma_pdev;

	dm646x_edma_pdata.rsv = rsv;

	edma_pdev = platform_device_register_full(&dm646x_edma_device);
	return PTR_ERR_OR_ZERO(edma_pdev);
}

void __init dm646x_init(void)
{
	davinci_common_init(&davinci_soc_info_dm646x);
	davinci_map_sysmod();
}

void __init dm646x_init_time(unsigned long ref_clk_rate,
			     unsigned long aux_clkin_rate)
{
	void __iomem *pll1, *psc;
	struct clk *clk;

	clk_register_fixed_rate(NULL, "ref_clk", NULL, 0, ref_clk_rate);
	clk_register_fixed_rate(NULL, "aux_clkin", NULL, 0, aux_clkin_rate);

	pll1 = ioremap(DAVINCI_PLL1_BASE, SZ_1K);
	dm646x_pll1_init(NULL, pll1, NULL);

	psc = ioremap(DAVINCI_PWR_SLEEP_CNTRL_BASE, SZ_4K);
	dm646x_psc_init(NULL, psc);

	clk = clk_get(NULL, "timer0");

	davinci_timer_init(clk);
}

static struct resource dm646x_pll2_resources[] = {
	{
		.start	= DAVINCI_PLL2_BASE,
		.end	= DAVINCI_PLL2_BASE + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device dm646x_pll2_device = {
	.name		= "dm646x-pll2",
	.id		= -1,
	.resource	= dm646x_pll2_resources,
	.num_resources	= ARRAY_SIZE(dm646x_pll2_resources),
};

void __init dm646x_register_clocks(void)
{
	/* PLL1 and PSC are registered in dm646x_init_time() */
	platform_device_register(&dm646x_pll2_device);
}

static int __init dm646x_init_devices(void)
{
	int ret = 0;

	if (!cpu_is_davinci_dm646x())
		return 0;

	platform_device_register(&dm646x_mdio_device);
	platform_device_register(&dm646x_emac_device);

	ret = davinci_init_wdt();
	if (ret)
		pr_warn("%s: watchdog init failed: %d\n", __func__, ret);

	return ret;
}
postcore_initcall(dm646x_init_devices);
