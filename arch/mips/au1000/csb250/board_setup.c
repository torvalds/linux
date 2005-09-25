/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Cogent CSB250 board setup.
 *
 * Copyright 2002 Cogent Computer Systems, Inc.
 *	dan@embeddededge.com
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
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/mc146818rtc.h>
#include <linux/delay.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/keyboard.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/pgtable.h>
#include <asm/au1000.h>
#include <asm/csb250.h>

extern int (*board_pci_idsel)(unsigned int devsel, int assert);
int	csb250_pci_idsel(unsigned int devsel, int assert);

void __init board_setup(void)
{
	u32 pin_func, pin_val;
	u32 sys_freqctrl, sys_clksrc;


	// set AUX clock to 12MHz * 8 = 96 MHz
	au_writel(8, SYS_AUXPLL);
	au_writel(0, SYS_PINSTATERD);
	udelay(100);

#if defined (CONFIG_USB_OHCI) || defined (CONFIG_AU1X00_USB_DEVICE)

	/* GPIO201 is input for PCMCIA card detect */
	/* GPIO203 is input for PCMCIA interrupt request */
	au_writel(au_readl(GPIO2_DIR) & (u32)(~((1<<1)|(1<<3))), GPIO2_DIR);

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

	// FREQ2 = aux/2 = 48 MHz
	sys_freqctrl |= ((0<<22) | (1<<21) | (1<<20));
	au_writel(sys_freqctrl, SYS_FREQCTRL0);

	/*
	 * Route 48MHz FREQ2 into USB Host and/or Device
	 */
#ifdef CONFIG_USB_OHCI
	sys_clksrc |= ((4<<12) | (0<<11) | (0<<10));
#endif
#ifdef CONFIG_AU1X00_USB_DEVICE
	sys_clksrc |= ((4<<7) | (0<<6) | (0<<5));
#endif
	au_writel(sys_clksrc, SYS_CLKSRC);


	pin_func = au_readl(SYS_PINFUNC) & (u32)(~0x8000);
#ifndef CONFIG_AU1X00_USB_DEVICE
	// 2nd USB port is USB host
	pin_func |= 0x8000;
#endif
	au_writel(pin_func, SYS_PINFUNC);
#endif // defined (CONFIG_USB_OHCI) || defined (CONFIG_AU1X00_USB_DEVICE)

	/* Configure GPIO2....it's used by PCI among other things.
	*/

	/* Make everything but GP200 (PCI RST) an input until we get
	 * the pins set correctly.
	 */
	au_writel(0x00000001, GPIO2_DIR);

	/* Set the pins used for output.
	 * A zero bit will leave PCI reset, LEDs off, power up USB,
	 * IDSEL disabled.
	 */
	pin_val = ((3 << 30) | (7 << 19) | (1 << 17) | (1 << 16));
	au_writel(pin_val, GPIO2_OUTPUT);

	/* Set the output direction.
	*/
	pin_val = ((3 << 14) | (7 << 3) | (1 << 1) | (1 << 0));
	au_writel(pin_val, GPIO2_DIR);

#ifdef CONFIG_PCI
	/* Use FREQ1 for the PCI output clock.  We use the
	 * CPU clock of 384 MHz divided by 12 to get 32 MHz PCI.
	 * If Michael changes the CPU speed, we need to adjust
	 * that here as well :-).
	 */

	/* zero and disable FREQ1
	*/
	sys_freqctrl = au_readl(SYS_FREQCTRL0);
	sys_freqctrl &= ~0x000ffc00;
	au_writel(sys_freqctrl, SYS_FREQCTRL0);

	/* zero and disable PCI clock
	*/
	sys_clksrc = au_readl(SYS_CLKSRC);
	sys_clksrc &= ~0x000f8000;
	au_writel(sys_clksrc, SYS_CLKSRC);

	/* Get current values (which really should match above).
	*/
	sys_freqctrl = au_readl(SYS_FREQCTRL0);
	sys_freqctrl &= ~0x000ffc00;

	sys_clksrc = au_readl(SYS_CLKSRC);
	sys_clksrc &= ~0x000f8000;

	/* FREQ1 = cpu/12 = 32 MHz
	*/
	sys_freqctrl |= ((5<<12) | (1<<11) | (0<<10));
	au_writel(sys_freqctrl, SYS_FREQCTRL0);

	/* Just connect the clock without further dividing.
	*/
	sys_clksrc |= ((3<<17) | (0<<16) | (0<<15));
	au_writel(sys_clksrc, SYS_CLKSRC);

	udelay(1);

	/* Now that clocks should be running, take PCI out of reset.
	*/
	pin_val = au_readl(GPIO2_OUTPUT);
	pin_val |= ((1 << 16) | 1);
	au_writel(pin_val, GPIO2_OUTPUT);

	// Setup PCI bus controller
	au_writel(0, Au1500_PCI_CMEM);
	au_writel(0x00003fff, Au1500_CFG_BASE);

	/* We run big endian without any of the software byte swapping,
	 * so configure the PCI bridge to help us out.
	 */
	au_writel(0xf | (2<<6) | (1<<5) | (1<<4), Au1500_PCI_CFG);

	au_writel(0xf0000000, Au1500_PCI_MWMASK_DEV);
	au_writel(0, Au1500_PCI_MWBASE_REV_CCL);
	au_writel(0x02a00356, Au1500_PCI_STATCMD);
	au_writel(0x00003c04, Au1500_PCI_HDRTYPE);
	au_writel(0x00000008, Au1500_PCI_MBAR);
	au_sync();

	board_pci_idsel = csb250_pci_idsel;
#endif

	/* Enable sys bus clock divider when IDLE state or no bus activity. */
	au_writel(au_readl(SYS_POWERCTRL) | (0x3 << 5), SYS_POWERCTRL);

#ifdef CONFIG_RTC
	// Enable the RTC if not already enabled
	if (!(au_readl(0xac000028) & 0x20)) {
		printk("enabling clock ...\n");
		au_writel((au_readl(0xac000028) | 0x20), 0xac000028);
	}
	// Put the clock in BCD mode
	if (readl(0xac00002C) & 0x4) { /* reg B */
		au_writel(au_readl(0xac00002c) & ~0x4, 0xac00002c);
		au_sync();
	}
#endif
}

/* The IDSEL is selected in the GPIO2 register.  We will make device
 * 12 appear in slot 0 and device 13 appear in slot 1.
 */
int
csb250_pci_idsel(unsigned int devsel, int assert)
{
	int		retval;
	unsigned int	gpio2_pins;

	retval = 1;

	/* First, disable both selects, then assert the one requested.
	*/
	au_writel(0xc000c000, GPIO2_OUTPUT);
	au_sync();

	if (assert) {
		if (devsel == 12)
			gpio2_pins = 0x40000000;
		else if (devsel == 13)
			gpio2_pins = 0x80000000;
		else {
			gpio2_pins = 0xc000c000;
			retval = 0;
		}
		au_writel(gpio2_pins, GPIO2_OUTPUT);
	}
	au_sync();

	return retval;
}
