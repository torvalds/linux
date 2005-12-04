/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Alchemy Db1x00 board setup.
 *
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
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/pgtable.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-db1x00/db1x00.h>

static BCSR * const bcsr = (BCSR *)BCSR_KSEG1_ADDR;

void board_reset (void)
{
	/* Hit BCSR.SYSTEM_CONTROL[SW_RST] */
	bcsr->swreset = 0x0000;
}

void __init board_setup(void)
{
	u32 pin_func;

	pin_func = 0;
	/* not valid for 1550 */
#ifdef CONFIG_AU1X00_USB_DEVICE
	// 2nd USB port is USB device
	pin_func = au_readl(SYS_PINFUNC) & (u32)(~0x8000);
	au_writel(pin_func, SYS_PINFUNC);
#endif

#if defined(CONFIG_IRDA) && (defined(CONFIG_SOC_AU1000) || defined(CONFIG_SOC_AU1100))
	/* set IRFIRSEL instead of GPIO15 */
	pin_func = au_readl(SYS_PINFUNC) | (u32)((1<<8));
	au_writel(pin_func, SYS_PINFUNC);
	/* power off until the driver is in use */
	bcsr->resets &= ~BCSR_RESETS_IRDA_MODE_MASK;
	bcsr->resets |= BCSR_RESETS_IRDA_MODE_OFF;
	au_sync();
#endif
	bcsr->pcmcia = 0x0000; /* turn off PCMCIA power */

#ifdef CONFIG_MIPS_MIRAGE
	/* enable GPIO[31:0] inputs */
	au_writel(0, SYS_PININPUTEN);

	/* GPIO[20] is output, tristate the other input primary GPIO's */
	au_writel((u32)(~(1<<20)), SYS_TRIOUTCLR);

	/* set GPIO[210:208] instead of SSI_0 */
	pin_func = au_readl(SYS_PINFUNC) | (u32)(1);

	/* set GPIO[215:211] for LED's */
	pin_func |= (u32)((5<<2));

	/* set GPIO[214:213] for more LED's */
	pin_func |= (u32)((5<<12));

	/* set GPIO[207:200] instead of PCMCIA/LCD */
	pin_func |= (u32)((3<<17));
	au_writel(pin_func, SYS_PINFUNC);

	/* Enable speaker amplifier.  This should
	 * be part of the audio driver.
	 */
	au_writel(au_readl(GPIO2_DIR) | 0x200, GPIO2_DIR);
	au_writel(0x02000200, GPIO2_OUTPUT);
#endif

	au_sync();

#ifdef CONFIG_MIPS_DB1000
    printk("AMD Alchemy Au1000/Db1000 Board\n");
#endif
#ifdef CONFIG_MIPS_DB1500
    printk("AMD Alchemy Au1500/Db1500 Board\n");
#endif
#ifdef CONFIG_MIPS_DB1100
    printk("AMD Alchemy Au1100/Db1100 Board\n");
#endif
#ifdef CONFIG_MIPS_BOSPORUS
    printk("AMD Alchemy Bosporus Board\n");
#endif
#ifdef CONFIG_MIPS_MIRAGE
    printk("AMD Alchemy Mirage Board\n");
#endif
#ifdef CONFIG_MIPS_DB1550
    printk("AMD Alchemy Au1550/Db1550 Board\n");
#endif
}
