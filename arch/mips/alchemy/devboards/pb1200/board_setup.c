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

#include <prom.h>
#include <au1xxx.h>


const char *get_system_type(void)
{
	return "Alchemy Pb1200";
}

void board_reset(void)
{
	bcsr->resets = 0;
	bcsr->system = 0;
}

void __init board_setup(void)
{
	char *argptr;

	argptr = prom_getcmdline();
#ifdef CONFIG_SERIAL_8250_CONSOLE
	argptr = strstr(argptr, "console=");
	if (argptr == NULL) {
		argptr = prom_getcmdline();
		strcat(argptr, " console=ttyS0,115200");
	}
#endif
#ifdef CONFIG_FB_AU1200
	strcat(argptr, " video=au1200fb:panel:bs");
#endif

#if 0
	{
		u32 pin_func;

		/*
		 * Enable PSC1 SYNC for AC97.  Normaly done in audio driver,
		 * but it is board specific code, so put it here.
		 */
		pin_func = au_readl(SYS_PINFUNC);
		au_sync();
		pin_func |= SYS_PF_MUST_BE_SET | SYS_PF_PSC1_S1;
		au_writel(pin_func, SYS_PINFUNC);

		au_writel(0, (u32)bcsr | 0x10); /* turn off PCMCIA power */
		au_sync();
	}
#endif

#if defined(CONFIG_I2C_AU1550)
	{
		u32 freq0, clksrc;
		u32 pin_func;

		/* Select SMBus in CPLD */
		bcsr->resets &= ~BCSR_RESETS_PCS0MUX;

		pin_func = au_readl(SYS_PINFUNC);
		au_sync();
		pin_func &= ~(SYS_PINFUNC_P0A | SYS_PINFUNC_P0B);
		/* Set GPIOs correctly */
		pin_func |= 2 << 17;
		au_writel(pin_func, SYS_PINFUNC);
		au_sync();

		/* The I2C driver depends on 50 MHz clock */
		freq0 = au_readl(SYS_FREQCTRL0);
		au_sync();
		freq0 &= ~(SYS_FC_FRDIV1_MASK | SYS_FC_FS1 | SYS_FC_FE1);
		freq0 |= 3 << SYS_FC_FRDIV1_BIT;
		/* 396 MHz / (3 + 1) * 2 == 49.5 MHz */
		au_writel(freq0, SYS_FREQCTRL0);
		au_sync();
		freq0 |= SYS_FC_FE1;
		au_writel(freq0, SYS_FREQCTRL0);
		au_sync();

		clksrc = au_readl(SYS_CLKSRC);
		au_sync();
		clksrc &= ~(SYS_CS_CE0 | SYS_CS_DE0 | SYS_CS_ME0_MASK);
		/* Bit 22 is EXTCLK0 for PSC0 */
		clksrc |= SYS_CS_MUX_FQ1 << SYS_CS_ME0_BIT;
		au_writel(clksrc, SYS_CLKSRC);
		au_sync();
	}
#endif

	/*
	 * The Pb1200 development board uses external MUX for PSC0 to
	 * support SMB/SPI. bcsr->resets bit 12: 0=SMB 1=SPI
	 */
#ifdef CONFIG_I2C_AU1550
	bcsr->resets &= ~BCSR_RESETS_PCS0MUX;
#endif
	au_sync();

#ifdef CONFIG_MIPS_PB1200
	printk(KERN_INFO "AMD Alchemy Pb1200 Board\n");
#endif
#ifdef CONFIG_MIPS_DB1200
	printk(KERN_INFO "AMD Alchemy Db1200 Board\n");
#endif
}

int board_au1200fb_panel(void)
{
	BCSR *bcsr = (BCSR *)BCSR_KSEG1_ADDR;
	int p;

	p = bcsr->switches;
	p >>= 8;
	p &= 0x0F;
	return p;
}

int board_au1200fb_panel_init(void)
{
	/* Apply power */
	BCSR *bcsr = (BCSR *)BCSR_KSEG1_ADDR;

	bcsr->board |= BCSR_BOARD_LCDVEE | BCSR_BOARD_LCDVDD | BCSR_BOARD_LCDBL;
	/* printk(KERN_DEBUG "board_au1200fb_panel_init()\n"); */
	return 0;
}

int board_au1200fb_panel_shutdown(void)
{
	/* Remove power */
	BCSR *bcsr = (BCSR *)BCSR_KSEG1_ADDR;

	bcsr->board &= ~(BCSR_BOARD_LCDVEE | BCSR_BOARD_LCDVDD |
			 BCSR_BOARD_LCDBL);
	/* printk(KERN_DEBUG "board_au1200fb_panel_shutdown()\n"); */
	return 0;
}
