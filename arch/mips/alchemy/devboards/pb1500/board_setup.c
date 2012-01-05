/*
 * Copyright 2000, 2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-db1x00/bcsr.h>

#include <prom.h>

const char *get_system_type(void)
{
	return "Alchemy Pb1500";
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

static int __init pb1500_init_irq(void)
{
	irq_set_irq_type(AU1500_GPIO9_INT, IRQF_TRIGGER_LOW);   /* CD0# */
	irq_set_irq_type(AU1500_GPIO10_INT, IRQF_TRIGGER_LOW);  /* CARD0 */
	irq_set_irq_type(AU1500_GPIO11_INT, IRQF_TRIGGER_LOW);  /* STSCHG0# */
	irq_set_irq_type(AU1500_GPIO204_INT, IRQF_TRIGGER_HIGH);
	irq_set_irq_type(AU1500_GPIO201_INT, IRQF_TRIGGER_LOW);
	irq_set_irq_type(AU1500_GPIO202_INT, IRQF_TRIGGER_LOW);
	irq_set_irq_type(AU1500_GPIO203_INT, IRQF_TRIGGER_LOW);
	irq_set_irq_type(AU1500_GPIO205_INT, IRQF_TRIGGER_LOW);

	return 0;
}
arch_initcall(pb1500_init_irq);
