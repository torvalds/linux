/*
 * Copyright 2002, 2008 MontaVista Software Inc.
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

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-pb1x00/pb1100.h>

#include <prom.h>


struct au1xxx_irqmap __initdata au1xxx_irq_map[] = {
	{ AU1000_GPIO_9,  IRQF_TRIGGER_LOW, 0 }, /* PCMCIA Card Fully_Inserted# */
	{ AU1000_GPIO_10, IRQF_TRIGGER_LOW, 0 }, /* PCMCIA Card STSCHG# */
	{ AU1000_GPIO_11, IRQF_TRIGGER_LOW, 0 }, /* PCMCIA Card IRQ# */
	{ AU1000_GPIO_13, IRQF_TRIGGER_LOW, 0 }, /* DC_IRQ# */
};


const char *get_system_type(void)
{
	return "Alchemy Pb1100";
}

void board_reset(void)
{
	/* Hit BCSR.RST_VDDI[SOFT_RESET] */
	au_writel(0x00000000, PB1100_RST_VDDI);
}

void __init board_init_irq(void)
{
	au1xxx_setup_irqmap(au1xxx_irq_map, ARRAY_SIZE(au1xxx_irq_map));
}

void __init board_setup(void)
{
	volatile void __iomem *base = (volatile void __iomem *)0xac000000UL;
	char *argptr;

	argptr = prom_getcmdline();
#ifdef CONFIG_SERIAL_8250_CONSOLE
	argptr = strstr(argptr, "console=");
	if (argptr == NULL) {
		argptr = prom_getcmdline();
		strcat(argptr, " console=ttyS0,115200");
	}
#endif

#ifdef CONFIG_FB_AU1100
	argptr = strstr(argptr, "video=");
	if (argptr == NULL) {
		argptr = prom_getcmdline();
		/* default panel */
		/*strcat(argptr, " video=au1100fb:panel:Sharp_320x240_16");*/
	}
#endif

#if defined(CONFIG_SOUND_AU1X00) && !defined(CONFIG_SOC_AU1000)
	/* au1000 does not support vra, au1500 and au1100 do */
	strcat(argptr, " au1000_audio=vra");
	argptr = prom_getcmdline();
#endif

	/* Set AUX clock to 12 MHz * 8 = 96 MHz */
	au_writel(8, SYS_AUXPLL);
	alchemy_gpio1_input_enable();
	udelay(100);

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	{
		u32 pin_func, sys_freqctrl, sys_clksrc;

		/* Configure pins GPIO[14:9] as GPIO */
		pin_func = au_readl(SYS_PINFUNC) & ~SYS_PF_UR3;

		/* Zero and disable FREQ2 */
		sys_freqctrl = au_readl(SYS_FREQCTRL0);
		sys_freqctrl &= ~0xFFF00000;
		au_writel(sys_freqctrl, SYS_FREQCTRL0);

		/* Zero and disable USBH/USBD/IrDA clock */
		sys_clksrc = au_readl(SYS_CLKSRC);
		sys_clksrc &= ~(SYS_CS_CIR | SYS_CS_DIR | SYS_CS_MIR_MASK);
		au_writel(sys_clksrc, SYS_CLKSRC);

		sys_freqctrl = au_readl(SYS_FREQCTRL0);
		sys_freqctrl &= ~0xFFF00000;

		sys_clksrc = au_readl(SYS_CLKSRC);
		sys_clksrc &= ~(SYS_CS_CIR | SYS_CS_DIR | SYS_CS_MIR_MASK);

		/* FREQ2 = aux / 2 = 48 MHz */
		sys_freqctrl |= (0 << SYS_FC_FRDIV2_BIT) |
				SYS_FC_FE2 | SYS_FC_FS2;
		au_writel(sys_freqctrl, SYS_FREQCTRL0);

		/*
		 * Route 48 MHz FREQ2 into USBH/USBD/IrDA
		 */
		sys_clksrc |= SYS_CS_MUX_FQ2 << SYS_CS_MIR_BIT;
		au_writel(sys_clksrc, SYS_CLKSRC);

		/* Setup the static bus controller */
		au_writel(0x00000002, MEM_STCFG3);  /* type = PCMCIA */
		au_writel(0x280E3D07, MEM_STTIME3); /* 250ns cycle time */
		au_writel(0x10000000, MEM_STADDR3); /* any PCMCIA select */

		/*
		 * Get USB Functionality pin state (device vs host drive pins).
		 */
		pin_func = au_readl(SYS_PINFUNC) & ~SYS_PF_USB;
		/* 2nd USB port is USB host. */
		pin_func |= SYS_PF_USB;
		au_writel(pin_func, SYS_PINFUNC);
	}
#endif /* defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE) */

	/* Enable sys bus clock divider when IDLE state or no bus activity. */
	au_writel(au_readl(SYS_POWERCTRL) | (0x3 << 5), SYS_POWERCTRL);

	/* Enable the RTC if not already enabled. */
	if (!(readb(base + 0x28) & 0x20)) {
		writeb(readb(base + 0x28) | 0x20, base + 0x28);
		au_sync();
	}
	/* Put the clock in BCD mode. */
	if (readb(base + 0x2C) & 0x4) { /* reg B */
		writeb(readb(base + 0x2c) & ~0x4, base + 0x2c);
		au_sync();
	}
}
