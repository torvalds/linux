/*
 * Pb1500 board support.
 *
 * Copyright (C) 2009 Manuel Lauss
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

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-db1x00/bcsr.h>
#include <prom.h>
#include "platform.h"

const char *get_system_type(void)
{
	return "PB1500";
}

void __init board_setup(void)
{
	u32 pin_func;
	u32 sys_freqctrl, sys_clksrc;

	bcsr_init(DB1000_BCSR_PHYS_ADDR,
		  DB1000_BCSR_PHYS_ADDR + DB1000_BCSR_HEXLED_OFS);

	sys_clksrc = sys_freqctrl = pin_func = 0;
	/* Set AUX clock to 12 MHz * 8 = 96 MHz */
	au_writel(8, SYS_AUXPLL);
	alchemy_gpio1_input_enable();
	udelay(100);

	/* GPIO201 is input for PCMCIA card detect */
	/* GPIO203 is input for PCMCIA interrupt request */
	alchemy_gpio_direction_input(201);
	alchemy_gpio_direction_input(203);

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)

	/* Zero and disable FREQ2 */
	sys_freqctrl = au_readl(SYS_FREQCTRL0);
	sys_freqctrl &= ~0xFFF00000;
	au_writel(sys_freqctrl, SYS_FREQCTRL0);

	/* zero and disable USBH/USBD clocks */
	sys_clksrc = au_readl(SYS_CLKSRC);
	sys_clksrc &= ~(SYS_CS_CUD | SYS_CS_DUD | SYS_CS_MUD_MASK |
			SYS_CS_CUH | SYS_CS_DUH | SYS_CS_MUH_MASK);
	au_writel(sys_clksrc, SYS_CLKSRC);

	sys_freqctrl = au_readl(SYS_FREQCTRL0);
	sys_freqctrl &= ~0xFFF00000;

	sys_clksrc = au_readl(SYS_CLKSRC);
	sys_clksrc &= ~(SYS_CS_CUD | SYS_CS_DUD | SYS_CS_MUD_MASK |
			SYS_CS_CUH | SYS_CS_DUH | SYS_CS_MUH_MASK);

	/* FREQ2 = aux/2 = 48 MHz */
	sys_freqctrl |= (0 << SYS_FC_FRDIV2_BIT) | SYS_FC_FE2 | SYS_FC_FS2;
	au_writel(sys_freqctrl, SYS_FREQCTRL0);

	/*
	 * Route 48MHz FREQ2 into USB Host and/or Device
	 */
	sys_clksrc |= SYS_CS_MUX_FQ2 << SYS_CS_MUH_BIT;
	au_writel(sys_clksrc, SYS_CLKSRC);

	pin_func = au_readl(SYS_PINFUNC) & ~SYS_PF_USB;
	/* 2nd USB port is USB host */
	pin_func |= SYS_PF_USB;
	au_writel(pin_func, SYS_PINFUNC);
#endif /* defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE) */

#ifdef CONFIG_PCI
	{
		void __iomem *base =
				(void __iomem *)KSEG1ADDR(AU1500_PCI_PHYS_ADDR);
		/* Setup PCI bus controller */
		__raw_writel(0x00003fff, base + PCI_REG_CMEM);
		__raw_writel(0xf0000000, base + PCI_REG_MWMASK_DEV);
		__raw_writel(0, base + PCI_REG_MWBASE_REV_CCL);
		__raw_writel(0x02a00356, base + PCI_REG_STATCMD);
		__raw_writel(0x00003c04, base + PCI_REG_PARAM);
		__raw_writel(0x00000008, base + PCI_REG_MBAR);
		wmb();
	}
#endif

	/* Enable sys bus clock divider when IDLE state or no bus activity. */
	au_writel(au_readl(SYS_POWERCTRL) | (0x3 << 5), SYS_POWERCTRL);

	/* Enable the RTC if not already enabled */
	if (!(au_readl(0xac000028) & 0x20)) {
		printk(KERN_INFO "enabling clock ...\n");
		au_writel((au_readl(0xac000028) | 0x20), 0xac000028);
	}
	/* Put the clock in BCD mode */
	if (au_readl(0xac00002c) & 0x4) { /* reg B */
		au_writel(au_readl(0xac00002c) & ~0x4, 0xac00002c);
		au_sync();
	}
}

/******************************************************************************/

static int pb1500_map_pci_irq(const struct pci_dev *d, u8 slot, u8 pin)
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

static struct alchemy_pci_platdata pb1500_pci_pd = {
	.board_map_irq	= pb1500_map_pci_irq,
	.pci_cfg_set	= PCI_CONFIG_AEN | PCI_CONFIG_R2H | PCI_CONFIG_R1H |
			  PCI_CONFIG_CH |
#if defined(__MIPSEB__)
			  PCI_CONFIG_SIC_HWA_DAT | PCI_CONFIG_SM,
#else
			  0,
#endif
};

static struct platform_device pb1500_pci_host = {
	.dev.platform_data = &pb1500_pci_pd,
	.name		= "alchemy-pci",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(alchemy_pci_host_res),
	.resource	= alchemy_pci_host_res,
};

static int __init pb1500_dev_init(void)
{
	int swapped;

	irq_set_irq_type(AU1500_GPIO9_INT,   IRQF_TRIGGER_LOW);   /* CD0# */
	irq_set_irq_type(AU1500_GPIO10_INT,  IRQF_TRIGGER_LOW);  /* CARD0 */
	irq_set_irq_type(AU1500_GPIO11_INT,  IRQF_TRIGGER_LOW);  /* STSCHG0# */
	irq_set_irq_type(AU1500_GPIO204_INT, IRQF_TRIGGER_HIGH);
	irq_set_irq_type(AU1500_GPIO201_INT, IRQF_TRIGGER_LOW);
	irq_set_irq_type(AU1500_GPIO202_INT, IRQF_TRIGGER_LOW);
	irq_set_irq_type(AU1500_GPIO203_INT, IRQF_TRIGGER_LOW);
	irq_set_irq_type(AU1500_GPIO205_INT, IRQF_TRIGGER_LOW);

	/* PCMCIA. single socket, identical to Pb1100 */
	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x000400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x000400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x000010000 - 1,
		AU1500_GPIO11_INT, AU1500_GPIO9_INT,	 /* card / insert */
		/*AU1500_GPIO10_INT*/0, 0, 0); /* stschg / eject / id */

	swapped = bcsr_read(BCSR_STATUS) &  BCSR_STATUS_DB1000_SWAPBOOT;
	db1x_register_norflash(64 * 1024 * 1024, 4, swapped);
	platform_device_register(&pb1500_pci_host);

	return 0;
}
arch_initcall(pb1500_dev_init);
