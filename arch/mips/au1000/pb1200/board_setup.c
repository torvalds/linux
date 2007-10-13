/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Alchemy Pb1200/Db1200 board setup.
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
#include <linux/mc146818rtc.h>
#include <linux/delay.h>

#if defined(CONFIG_BLK_DEV_IDE_AU1XXX)
#include <linux/ide.h>
#endif

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/pgtable.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>

#ifdef CONFIG_MIPS_PB1200
#include <asm/mach-pb1x00/pb1200.h>
#endif

#ifdef CONFIG_MIPS_DB1200
#include <asm/mach-db1x00/db1200.h>
#define PB1200_ETH_INT DB1200_ETH_INT
#define PB1200_IDE_INT DB1200_IDE_INT
#endif

extern void _board_init_irq(void);
extern void (*board_init_irq)(void);

void board_reset(void)
{
	bcsr->resets = 0;
	bcsr->system = 0;
}

void __init board_setup(void)
{
	char *argptr = NULL;
	u32 pin_func;

#if 0
	/* Enable PSC1 SYNC for AC97.  Normaly done in audio driver,
	 * but it is board specific code, so put it here.
	 */
	pin_func = au_readl(SYS_PINFUNC);
	au_sync();
	pin_func |= SYS_PF_MUST_BE_SET | SYS_PF_PSC1_S1;
	au_writel(pin_func, SYS_PINFUNC);

	au_writel(0, (u32)bcsr|0x10); /* turn off pcmcia power */
	au_sync();
#endif

#if defined(CONFIG_I2C_AU1550)
	{
	u32 freq0, clksrc;

	/* Select SMBUS in CPLD */
	bcsr->resets &= ~(BCSR_RESETS_PCS0MUX);

	pin_func = au_readl(SYS_PINFUNC);
	au_sync();
	pin_func &= ~(3<<17 | 1<<4);
	/* Set GPIOs correctly */
	pin_func |= 2<<17;
	au_writel(pin_func, SYS_PINFUNC);
	au_sync();

	/* The i2c driver depends on 50Mhz clock */
	freq0 = au_readl(SYS_FREQCTRL0);
	au_sync();
	freq0 &= ~(SYS_FC_FRDIV1_MASK | SYS_FC_FS1 | SYS_FC_FE1);
	freq0 |= (3<<SYS_FC_FRDIV1_BIT);
	/* 396Mhz / (3+1)*2 == 49.5Mhz */
	au_writel(freq0, SYS_FREQCTRL0);
	au_sync();
	freq0 |= SYS_FC_FE1;
	au_writel(freq0, SYS_FREQCTRL0);
	au_sync();

	clksrc = au_readl(SYS_CLKSRC);
	au_sync();
	clksrc &= ~0x01f00000;
	/* bit 22 is EXTCLK0 for PSC0 */
	clksrc |= (0x3 << 22);
	au_writel(clksrc, SYS_CLKSRC);
	au_sync();
	}
#endif

#ifdef CONFIG_FB_AU1200
	argptr = prom_getcmdline();
#ifdef CONFIG_MIPS_PB1200
	strcat(argptr, " video=au1200fb:panel:bs");
#endif
#ifdef CONFIG_MIPS_DB1200
	strcat(argptr, " video=au1200fb:panel:bs");
#endif
#endif

	/* The Pb1200 development board uses external MUX for PSC0 to
	support SMB/SPI. bcsr->resets bit 12: 0=SMB 1=SPI
	*/
#ifdef CONFIG_I2C_AU1550
	bcsr->resets &= (~BCSR_RESETS_PCS0MUX);
#endif
	au_sync();

#ifdef CONFIG_MIPS_PB1200
	printk("AMD Alchemy Pb1200 Board\n");
#endif
#ifdef CONFIG_MIPS_DB1200
	printk("AMD Alchemy Db1200 Board\n");
#endif

	/* Setup Pb1200 External Interrupt Controller */
	board_init_irq = _board_init_irq;
}

int
board_au1200fb_panel(void)
{
	BCSR *bcsr = (BCSR *)BCSR_KSEG1_ADDR;
	int p;

	p = bcsr->switches;
	p >>= 8;
	p &= 0x0F;
	return p;
}

int
board_au1200fb_panel_init(void)
{
	/* Apply power */
    BCSR *bcsr = (BCSR *)BCSR_KSEG1_ADDR;
	bcsr->board |= (BCSR_BOARD_LCDVEE | BCSR_BOARD_LCDVDD | BCSR_BOARD_LCDBL);
	/*printk("board_au1200fb_panel_init()\n"); */
	return 0;
}

int
board_au1200fb_panel_shutdown(void)
{
	/* Remove power */
    BCSR *bcsr = (BCSR *)BCSR_KSEG1_ADDR;
	bcsr->board &= ~(BCSR_BOARD_LCDVEE | BCSR_BOARD_LCDVDD | BCSR_BOARD_LCDBL);
	/*printk("board_au1200fb_panel_shutdown()\n"); */
	return 0;
}

