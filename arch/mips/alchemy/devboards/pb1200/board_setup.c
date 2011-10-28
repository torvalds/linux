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
#include <linux/interrupt.h>
#include <linux/sched.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-db1x00/bcsr.h>

#ifdef CONFIG_MIPS_PB1200
#include <asm/mach-pb1x00/pb1200.h>
#endif

#ifdef CONFIG_MIPS_DB1200
#include <asm/mach-db1x00/db1200.h>
#define PB1200_INT_BEGIN DB1200_INT_BEGIN
#define PB1200_INT_END DB1200_INT_END
#endif

#include <prom.h>

const char *get_system_type(void)
{
	return "Alchemy Pb1200";
}

void __init board_setup(void)
{
	printk(KERN_INFO "AMD Alchemy Pb1200 Board\n");
	bcsr_init(PB1200_BCSR_PHYS_ADDR,
		  PB1200_BCSR_PHYS_ADDR + PB1200_BCSR_HEXLED_OFS);

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
		bcsr_mod(BCSR_RESETS, BCSR_RESETS_PSC0MUX, 0);

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
	 * support SMB/SPI. bcsr_resets bit 12: 0=SMB 1=SPI
	 */
#ifdef CONFIG_I2C_AU1550
	bcsr_mod(BCSR_RESETS, BCSR_RESETS_PSC0MUX, 0);
#endif
	au_sync();
}

static int __init pb1200_init_irq(void)
{
	/* We have a problem with CPLD rev 3. */
	if (BCSR_WHOAMI_CPLD(bcsr_read(BCSR_WHOAMI)) <= 3) {
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "Pb1200 must be at CPLD rev 4. Please have Pb1200\n");
		printk(KERN_ERR "updated to latest revision. This software will\n");
		printk(KERN_ERR "not work on anything less than CPLD rev 4.\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		printk(KERN_ERR "WARNING!!!\n");
		panic("Game over.  Your score is 0.");
	}

	irq_set_irq_type(AU1200_GPIO7_INT, IRQF_TRIGGER_LOW);
	bcsr_init_irq(PB1200_INT_BEGIN, PB1200_INT_END, AU1200_GPIO7_INT);

	return 0;
}
arch_initcall(pb1200_init_irq);


int board_au1200fb_panel(void)
{
	return (bcsr_read(BCSR_SWITCHES) >> 8) & 0x0f;
}

int board_au1200fb_panel_init(void)
{
	/* Apply power */
	bcsr_mod(BCSR_BOARD, 0, BCSR_BOARD_LCDVEE | BCSR_BOARD_LCDVDD |
				BCSR_BOARD_LCDBL);
	/* printk(KERN_DEBUG "board_au1200fb_panel_init()\n"); */
	return 0;
}

int board_au1200fb_panel_shutdown(void)
{
	/* Remove power */
	bcsr_mod(BCSR_BOARD, BCSR_BOARD_LCDVEE | BCSR_BOARD_LCDVDD |
			     BCSR_BOARD_LCDBL, 0);
	/* printk(KERN_DEBUG "board_au1200fb_panel_shutdown()\n"); */
	return 0;
}
