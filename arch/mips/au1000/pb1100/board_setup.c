/*
 * Copyright 2002 MontaVista Software Inc.
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
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/delay.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/pgtable.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-pb1x00/pb1100.h>

void board_reset (void)
{
    /* Hit BCSR.SYSTEM_CONTROL[SW_RST] */
    au_writel(0x00000000, 0xAE00001C);
}

void __init board_setup(void)
{
	u32 pin_func;
	u32 sys_freqctrl, sys_clksrc;

	// set AUX clock to 12MHz * 8 = 96 MHz
	au_writel(8, SYS_AUXPLL);
	au_writel(0, SYS_PININPUTEN);
	udelay(100);

#ifdef CONFIG_USB_OHCI
	// configure pins GPIO[14:9] as GPIO
	pin_func = au_readl(SYS_PINFUNC) & (u32)(~0x80);

	/* zero and disable FREQ2 */
	sys_freqctrl = au_readl(SYS_FREQCTRL0);
	sys_freqctrl &= ~0xFFF00000;
	au_writel(sys_freqctrl, SYS_FREQCTRL0);

	/* zero and disable USBH/USBD/IrDA clock */
	sys_clksrc = au_readl(SYS_CLKSRC);
	sys_clksrc &= ~0x0000001F;
	au_writel(sys_clksrc, SYS_CLKSRC);

	sys_freqctrl = au_readl(SYS_FREQCTRL0);
	sys_freqctrl &= ~0xFFF00000;

	sys_clksrc = au_readl(SYS_CLKSRC);
	sys_clksrc &= ~0x0000001F;

	// FREQ2 = aux/2 = 48 MHz
	sys_freqctrl |= ((0<<22) | (1<<21) | (1<<20));
	au_writel(sys_freqctrl, SYS_FREQCTRL0);

	/*
	 * Route 48MHz FREQ2 into USBH/USBD/IrDA
	 */
	sys_clksrc |= ((4<<2) | (0<<1) | 0 );
	au_writel(sys_clksrc, SYS_CLKSRC);

	/* setup the static bus controller */
	au_writel(0x00000002, MEM_STCFG3);  /* type = PCMCIA */
	au_writel(0x280E3D07, MEM_STTIME3); /* 250ns cycle time */
	au_writel(0x10000000, MEM_STADDR3); /* any PCMCIA select */

	// get USB Functionality pin state (device vs host drive pins)
	pin_func = au_readl(SYS_PINFUNC) & (u32)(~0x8000);
	// 2nd USB port is USB host
	pin_func |= 0x8000;
	au_writel(pin_func, SYS_PINFUNC);
#endif // defined (CONFIG_USB_OHCI)

	/* Enable sys bus clock divider when IDLE state or no bus activity. */
	au_writel(au_readl(SYS_POWERCTRL) | (0x3 << 5), SYS_POWERCTRL);

	// Enable the RTC if not already enabled
	if (!(readb(0xac000028) & 0x20)) {
		writeb(readb(0xac000028) | 0x20, 0xac000028);
		au_sync();
	}
	// Put the clock in BCD mode
	if (readb(0xac00002C) & 0x4) { /* reg B */
		writeb(readb(0xac00002c) & ~0x4, 0xac00002c);
		au_sync();
	}
}
