/*
 * arch/arm/mach-sun3i/dma/dma_15.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Huang Xin <huangxin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sysdev.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>

#include <mach/dma.h>
#include <mach/dma_regs.h>
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

static int __init sw_dmac_probe(struct platform_device *dev)
{
	int ret;

	sw15_dma_init();

	ret = sw_dma_init_map(&sw_dma_sel);

	if (ret) {
		printk("DMAC: failed to init map\n");
	} else {
		pr_info("Initialize DMAC OK\n");
	}

	return ret;
}

static int __devexit sw_dmac_remove(struct platform_device *dev)
{
	printk("[%s] enter\n", __FUNCTION__);
	return 0;
}

#ifdef CONFIG_PM

static inline struct sw_dma_chan *to_dma_chan(struct platform_device *dev)
{
	return container_of(dev, struct sw_dma_chan, dev);
}
#define dma_rdreg(chan, reg) readl((chan)->regs + (reg))

static int sw_dmac_suspend(struct platform_device *dev, pm_message_t state)
{
	struct sw_dma_chan *cp = to_dma_chan(dev);

	printk(KERN_DEBUG "suspending dma channel %d\n", cp->number);

	if (dma_rdreg(cp, SW_DMA_DCONF) & SW_DCONF_BUSY) {
		/* the dma channel is still working, which is probably
		 * a bad thing to do over suspend/resume. We stop the
		 * channel and assume that the client is either going to
		 * retry after resume, or that it is broken.
		 */

		printk(KERN_INFO "dma: stopping channel %d due to suspend\n",
		       cp->number);

		sw_dma_dostop(cp);
	}

	return 0;
}

static int sw_dmac_resume(struct platform_device *dev)
{
#if 0
	struct sw_dma_chan *cp = to_dma_chan(dev);
	unsigned int no = cp->number | DMACH_LOW_LEVEL;

	/* restore channel's hardware configuration */

	if (!cp->in_use)
		return 0;

	printk(KERN_INFO "dma%d: restoring configuration\n",
	       cp->number);

	sw_dma_config(no, NULL);
#endif
	return 0;
}

#else
#define sw_dmac_suspend NULL
#define sw_dmca_resume  NULL
#endif /* CONFIG_PM */

static struct platform_driver __initdata sw_dmac_driver = {
	.probe		= sw_dmac_probe,
	.remove		= __devexit_p(sw_dmac_remove),
	.suspend	= sw_dmac_suspend,
	.resume		= sw_dmac_resume,
	.driver		= {
		.name	= "sw_dmac",
		.owner	= THIS_MODULE,
	},
};

static int __init sw_dma_drvinit(void)
{
	platform_driver_register(&sw_dmac_driver);
	return 0;
}

arch_initcall(sw_dma_drvinit);
