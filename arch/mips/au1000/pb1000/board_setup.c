/*
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
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

#include <linux/init.h>
#include <linux/delay.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-pb1x00/pb1000.h>

void board_reset(void)
{
}

void __init board_setup(void)
{
	u32 pin_func, static_cfg0;
	u32 sys_freqctrl, sys_clksrc;
	u32 prid = read_c0_prid();

	// set AUX clock to 12MHz * 8 = 96 MHz
	au_writel(8, SYS_AUXPLL);
	au_writel(0, SYS_PINSTATERD);
	udelay(100);

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	/* zero and disable FREQ2 */
	sys_freqctrl = au_readl(SYS_FREQCTRL0);
	sys_freqctrl &= ~0xFFF00000;
	au_writel(sys_freqctrl, SYS_FREQCTRL0);

	/* zero and disable USBH/USBD clocks */
	sys_clksrc = au_readl(SYS_CLKSRC);
	sys_clksrc &= ~0x00007FE0;
	au_writel(sys_clksrc, SYS_CLKSRC);

	sys_freqctrl = au_readl(SYS_FREQCTRL0);
	sys_freqctrl &= ~0xFFF00000;

	sys_clksrc = au_readl(SYS_CLKSRC);
	sys_clksrc &= ~0x00007FE0;

	switch (prid & 0x000000FF)
	{
	case 0x00: /* DA */
	case 0x01: /* HA */
	case 0x02: /* HB */
	/* CPU core freq to 48MHz to slow it way down... */
	au_writel(4, SYS_CPUPLL);

	/*
	 * Setup 48MHz FREQ2 from CPUPLL for USB Host
	 */
	/* FRDIV2=3 -> div by 8 of 384MHz -> 48MHz */
	sys_freqctrl |= ((3<<22) | (1<<21) | (0<<20));
	au_writel(sys_freqctrl, SYS_FREQCTRL0);

	/* CPU core freq to 384MHz */
	au_writel(0x20, SYS_CPUPLL);

	printk("Au1000: 48MHz OHCI workaround enabled\n");
		break;

	default:  /* HC and newer */
	// FREQ2 = aux/2 = 48 MHz
	sys_freqctrl |= ((0<<22) | (1<<21) | (1<<20));
	au_writel(sys_freqctrl, SYS_FREQCTRL0);
		break;
	}

	/*
	 * Route 48MHz FREQ2 into USB Host and/or Device
	 */
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	sys_clksrc |= ((4<<12) | (0<<11) | (0<<10));
#endif
	au_writel(sys_clksrc, SYS_CLKSRC);

	// configure pins GPIO[14:9] as GPIO
	pin_func = au_readl(SYS_PINFUNC) & (u32)(~0x8080);

	// 2nd USB port is USB host
	pin_func |= 0x8000;

	au_writel(pin_func, SYS_PINFUNC);
	au_writel(0x2800, SYS_TRIOUTCLR);
	au_writel(0x0030, SYS_OUTPUTCLR);
#endif /* defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE) */

	// make gpio 15 an input (for interrupt line)
	pin_func = au_readl(SYS_PINFUNC) & (u32)(~0x100);
	// we don't need I2S, so make it available for GPIO[31:29]
	pin_func |= (1<<5);
	au_writel(pin_func, SYS_PINFUNC);

	au_writel(0x8000, SYS_TRIOUTCLR);

	static_cfg0 = au_readl(MEM_STCFG0) & (u32)(~0xc00);
	au_writel(static_cfg0, MEM_STCFG0);

	// configure RCE2* for LCD
	au_writel(0x00000004, MEM_STCFG2);

	// MEM_STTIME2
	au_writel(0x09000000, MEM_STTIME2);

	// Set 32-bit base address decoding for RCE2*
	au_writel(0x10003ff0, MEM_STADDR2);

	// PCI CPLD setup
	// expand CE0 to cover PCI
	au_writel(0x11803e40, MEM_STADDR1);

	// burst visibility on
	au_writel(au_readl(MEM_STCFG0) | 0x1000, MEM_STCFG0);

	au_writel(0x83, MEM_STCFG1);         // ewait enabled, flash timing
	au_writel(0x33030a10, MEM_STTIME1);   // slower timing for FPGA

	/* setup the static bus controller */
	au_writel(0x00000002, MEM_STCFG3);  /* type = PCMCIA */
	au_writel(0x280E3D07, MEM_STTIME3); /* 250ns cycle time */
	au_writel(0x10000000, MEM_STADDR3); /* any PCMCIA select */

#ifdef CONFIG_PCI
	au_writel(0, PCI_BRIDGE_CONFIG); // set extend byte to 0
	au_writel(0, SDRAM_MBAR);        // set mbar to 0
	au_writel(0x2, SDRAM_CMD);       // enable memory accesses
	au_sync_delay(1);
#endif

	/* Enable Au1000 BCLK switching - note: sed1356 must not use
	 * its BCLK (Au1000 LCLK) for any timings */
	switch (prid & 0x000000FF)
	{
	case 0x00: /* DA */
	case 0x01: /* HA */
	case 0x02: /* HB */
		break;
	default:  /* HC and newer */
		/* Enable sys bus clock divider when IDLE state or no bus
		   activity. */
		au_writel(au_readl(SYS_POWERCTRL) | (0x3 << 5), SYS_POWERCTRL);
		break;
	}
}
