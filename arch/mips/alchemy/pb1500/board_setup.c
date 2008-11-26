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

#include <linux/init.h>
#include <linux/delay.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-pb1x00/pb1500.h>

void board_reset(void)
{
	/* Hit BCSR.RST_VDDI[SOFT_RESET] */
	au_writel(0x00000000, PB1500_RST_VDDI);
}

void __init board_setup(void)
{
	u32 pin_func;
	u32 sys_freqctrl, sys_clksrc;

	sys_clksrc = sys_freqctrl = pin_func = 0;
	/* Set AUX clock to 12 MHz * 8 = 96 MHz */
	au_writel(8, SYS_AUXPLL);
	au_writel(0, SYS_PINSTATERD);
	udelay(100);

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)

	/* GPIO201 is input for PCMCIA card detect */
	/* GPIO203 is input for PCMCIA interrupt request */
	au_writel(au_readl(GPIO2_DIR) & ~((1 << 1) | (1 << 3)), GPIO2_DIR);

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
	/* Setup PCI bus controller */
	au_writel(0, Au1500_PCI_CMEM);
	au_writel(0x00003fff, Au1500_CFG_BASE);
#if defined(__MIPSEB__)
	au_writel(0xf | (2 << 6) | (1 << 4), Au1500_PCI_CFG);
#else
	au_writel(0xf, Au1500_PCI_CFG);
#endif
	au_writel(0xf0000000, Au1500_PCI_MWMASK_DEV);
	au_writel(0, Au1500_PCI_MWBASE_REV_CCL);
	au_writel(0x02a00356, Au1500_PCI_STATCMD);
	au_writel(0x00003c04, Au1500_PCI_HDRTYPE);
	au_writel(0x00000008, Au1500_PCI_MBAR);
	au_sync();
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
