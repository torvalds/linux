#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sysdev.h>
#include <linux/serial_core.h>

#include <mach/dma.h>
#include <mach/system.h>

static struct sw_dma_map __initdata sw_dma_mappings[DMACH_MAX] = {
	[DMACH_NSPI0] = {
		.name		= "spi0",
		.channels = {DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID,
				0,0,0,0,0,0,0,0,},
	},
	[DMACH_NSPI1] = {
		.name		= "spi1",
		.channels = {DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID,
				0,0,0,0,0,0,0,0,},
	},
	[DMACH_NUART0] = {
		.name		= "uart0",
	},
	[DMACH_NUART1] = {
		.name		= "uart1",
	},
	[DMACH_NUART2] = {
		.name		= "uart2",
	},
	[DMACH_NUART3] = {
		.name		= "uart0",
	},
	[DMACH_NXRAM] = {
		.name		= "sram_dram",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,0,0,0,0,0,0,0,DMA_CH_VALID},
	},
	[DMACH_NADDA] = {
		.name		= "adda",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
				0,0,0,0,0,0,0,0,},
	},
  	[DMACH_NIIS] = {
		.name		= "iis",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
				0,0,0,0,0,0,0,0,},
	},
	[DMACH_NIR] = {
		.name		= "ir",
	},
	[DMACH_NSPDIF] = {
		.name		= "spdif",
		.channels = {DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
				0,0,0,0,0,0,0,0,},
	},
	[DMACH_NAC97] = {
		.name		= "ac97",
	},
	[DMACH_DMMC1] = {
		.name		= "mmc1",
		.channels = {0,0,0,0,0,0,0,0,
			DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DMMC2] = {
		.name		= "mmc2",
		.channels = {0,0,0,0,0,0,0,0,
			DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DMMC3] = {
		.name		= "mmc3",
		.channels = {0,0,0,0,0,0,0,0,
			DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DEMACR] = {
		.name		= "emac_rx",
		.channels = {0,0,0,0,0,0,0,0,
			DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DEMACT] = {
		.name		= "emac_tx",
		.channels = {0,0,0,0,0,0,0,0,
			DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DNAND] = {
		.name		= "nand",
		.channels = {0,0,0,0,0,0,0,0,
			DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID, DMA_CH_VALID},
	},
	[DMACH_DUSB0] = {
		.name 	= "usb0",
		.channels = {0,0,0,0,0,0,0,0,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,},
	},
	[DMACH_DUSB1] = {
		.name 	= "usb1",
		.channels = {0,0,0,0,0,0,0,0,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,},
	},
	[DMACH_DUSB2] = {
		.name 	= "usb2",
		.channels = {0,0,0,0,0,0,0,0,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,
			DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,DMA_CH_VALID,},
	},
};

static struct sw_dma_selection __initdata sw_dma_sel = {
	.dcon_mask	= 0xffffffff,
	.map		= sw_dma_mappings,
	.map_size	= ARRAY_SIZE(sw_dma_mappings),
};

static int __init sw_dma_add(struct sys_device *sysdev)
{
	sw15_dma_init();
	return sw_dma_init_map(&sw_dma_sel);
}

static struct sysdev_driver sw_dma_driver = {
	.add	= sw_dma_add,
};

static int __init sw_dma_drvinit(void)
{
	return sysdev_driver_register(&sw_sysclass, &sw_dma_driver);
}

arch_initcall(sw_dma_drvinit);


