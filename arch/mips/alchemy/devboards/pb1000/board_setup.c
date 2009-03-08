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
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-pb1x00/pb1000.h>
#include <prom.h>


struct au1xxx_irqmap __initdata au1xxx_irq_map[] = {
	{ AU1000_GPIO_15, IRQF_TRIGGER_LOW, 0 },
};


const char *get_system_type(void)
{
	return "Alchemy Pb1000";
}

void board_reset(void)
{
}

void __init board_init_irq(void)
{
	au1xxx_setup_irqmap(au1xxx_irq_map, ARRAY_SIZE(au1xxx_irq_map));
}

void __init board_setup(void)
{
	u32 pin_func, static_cfg0;
	u32 sys_freqctrl, sys_clksrc;
	u32 prid = read_c0_prid();

#ifdef CONFIG_SERIAL_8250_CONSOLE
	char *argptr = prom_getcmdline();
	argptr = strstr(argptr, "console=");
	if (argptr == NULL) {
		argptr = prom_getcmdline();
		strcat(argptr, " console=ttyS0,115200");
	}
#endif

	/* Set AUX clock to 12 MHz * 8 = 96 MHz */
	au_writel(8, SYS_AUXPLL);
	au_writel(0, SYS_PINSTATERD);
	udelay(100);

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	/* Zero and disable FREQ2 */
	sys_freqctrl = au_readl(SYS_FREQCTRL0);
	sys_freqctrl &= ~0xFFF00000;
	au_writel(sys_freqctrl, SYS_FREQCTRL0);

	/* Zero and disable USBH/USBD clocks */
	sys_clksrc = au_readl(SYS_CLKSRC);
	sys_clksrc &= ~(SYS_CS_CUD | SYS_CS_DUD | SYS_CS_MUD_MASK |
		        SYS_CS_CUH | SYS_CS_DUH | SYS_CS_MUH_MASK);
	au_writel(sys_clksrc, SYS_CLKSRC);

	sys_freqctrl = au_readl(SYS_FREQCTRL0);
	sys_freqctrl &= ~0xFFF00000;

	sys_clksrc = au_readl(SYS_CLKSRC);
	sys_clksrc &= ~(SYS_CS_CUD | SYS_CS_DUD | SYS_CS_MUD_MASK |
		        SYS_CS_CUH | SYS_CS_DUH | SYS_CS_MUH_MASK);

	switch (prid & 0x000000FF) {
	case 0x00: /* DA */
	case 0x01: /* HA */
	case 0x02: /* HB */
		/* CPU core freq to 48 MHz to slow it way down... */
		au_writel(4, SYS_CPUPLL);

		/*
		 * Setup 48 MHz FREQ2 from CPUPLL for USB Host
		 * FRDIV2 = 3 -> div by 8 of 384 MHz -> 48 MHz
		 */
		sys_freqctrl |= (3 << SYS_FC_FRDIV2_BIT) | SYS_FC_FE2;
		au_writel(sys_freqctrl, SYS_FREQCTRL0);

		/* CPU core freq to 384 MHz */
		au_writel(0x20, SYS_CPUPLL);

		printk(KERN_INFO "Au1000: 48 MHz OHCI workaround enabled\n");
		break;

	default: /* HC and newer */
		/* FREQ2 = aux / 2 = 48 MHz */
		sys_freqctrl |= (0 << SYS_FC_FRDIV2_BIT) |
				 SYS_FC_FE2 | SYS_FC_FS2;
		au_writel(sys_freqctrl, SYS_FREQCTRL0);
		break;
	}

	/*
	 * Route 48 MHz FREQ2 into USB Host and/or Device
	 */
	sys_clksrc |= SYS_CS_MUX_FQ2 << SYS_CS_MUH_BIT;
	au_writel(sys_clksrc, SYS_CLKSRC);

	/* Configure pins GPIO[14:9] as GPIO */
	pin_func = au_readl(SYS_PINFUNC) & ~(SYS_PF_UR3 | SYS_PF_USB);

	/* 2nd USB port is USB host */
	pin_func |= SYS_PF_USB;

	au_writel(pin_func, SYS_PINFUNC);
	au_writel(0x2800, SYS_TRIOUTCLR);
	au_writel(0x0030, SYS_OUTPUTCLR);
#endif /* defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE) */

	/* Make GPIO 15 an input (for interrupt line) */
	pin_func = au_readl(SYS_PINFUNC) & ~SYS_PF_IRF;
	/* We don't need I2S, so make it available for GPIO[31:29] */
	pin_func |= SYS_PF_I2S;
	au_writel(pin_func, SYS_PINFUNC);

	au_writel(0x8000, SYS_TRIOUTCLR);

	static_cfg0 = au_readl(MEM_STCFG0) & ~0xc00;
	au_writel(static_cfg0, MEM_STCFG0);

	/* configure RCE2* for LCD */
	au_writel(0x00000004, MEM_STCFG2);

	/* MEM_STTIME2 */
	au_writel(0x09000000, MEM_STTIME2);

	/* Set 32-bit base address decoding for RCE2* */
	au_writel(0x10003ff0, MEM_STADDR2);

	/*
	 * PCI CPLD setup
	 * Expand CE0 to cover PCI
	 */
	au_writel(0x11803e40, MEM_STADDR1);

	/* Burst visibility on */
	au_writel(au_readl(MEM_STCFG0) | 0x1000, MEM_STCFG0);

	au_writel(0x83, MEM_STCFG1);	     /* ewait enabled, flash timing */
	au_writel(0x33030a10, MEM_STTIME1);  /* slower timing for FPGA */

	/* Setup the static bus controller */
	au_writel(0x00000002, MEM_STCFG3);  /* type = PCMCIA */
	au_writel(0x280E3D07, MEM_STTIME3); /* 250ns cycle time */
	au_writel(0x10000000, MEM_STADDR3); /* any PCMCIA select */

	/*
	 * Enable Au1000 BCLK switching - note: sed1356 must not use
	 * its BCLK (Au1000 LCLK) for any timings
	 */
	switch (prid & 0x000000FF) {
	case 0x00: /* DA */
	case 0x01: /* HA */
	case 0x02: /* HB */
		break;
	default:  /* HC and newer */
		/*
		 * Enable sys bus clock divider when IDLE state or no bus
		 * activity.
		 */
		au_writel(au_readl(SYS_POWERCTRL) | (0x3 << 5), SYS_POWERCTRL);
		break;
	}
}
