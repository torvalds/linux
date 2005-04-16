/*
 * Copyright 2000-2003 MontaVista Software Inc.
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
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/delay.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/keyboard.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/pgtable.h>
#include <asm/au1000.h>

void board_reset (void)
{
	/* Hit BCSR.SYSTEM_CONTROL[SW_RST] */
	au_writel(0x00000000, 0xAE00001C);
}

void __init board_setup(void)
{
	u32 pin_func;
	
	// set multiple use pins (UART3/GPIO) to UART (it's used as UART too)
	pin_func = au_readl(SYS_PINFUNC) & (u32)(~SYS_PF_UR3);
	pin_func |= SYS_PF_UR3;
	au_writel(pin_func, SYS_PINFUNC);

	// enable UART
	au_writel(0x01, UART3_ADDR+UART_MOD_CNTRL); // clock enable (CE)
	mdelay(10);
	au_writel(0x03, UART3_ADDR+UART_MOD_CNTRL); // CE and "enable"
	mdelay(10);

	// enable DTR = USB power up
	au_writel(0x01, UART3_ADDR+UART_MCR); //? UART_MCR_DTR is 0x01???

#ifdef CONFIG_PCMCIA_XXS1500
	/* setup pcmcia signals */
	au_writel(0, SYS_PININPUTEN);

	/* gpio 0, 1, and 4 are inputs */
	au_writel(1 | (1<<1) | (1<<4), SYS_TRIOUTCLR);

	/* enable GPIO2 if not already enabled */
	au_writel(1, GPIO2_ENABLE);
	/* gpio2 208/9/10/11 are inputs */
	au_writel((1<<8) | (1<<9) | (1<<10) | (1<<11), GPIO2_DIR);
	
	/* turn off power */
	au_writel((au_readl(GPIO2_PINSTATE) & ~(1<<14))|(1<<30), GPIO2_OUTPUT);
#endif
	

#ifdef CONFIG_PCI
#if defined(__MIPSEB__)
	au_writel(0xf | (2<<6) | (1<<4), Au1500_PCI_CFG);
#else
	au_writel(0xf, Au1500_PCI_CFG);
#endif
#endif
}
