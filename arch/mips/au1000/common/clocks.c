/*
 * BRIEF MODULE DESCRIPTION
 *	Simple Au1000 clocks routines.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *		ppopov@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <asm/mach-au1x00/au1000.h>

static unsigned int au1x00_clock; // Hz
static unsigned int lcd_clock;    // KHz
static unsigned long uart_baud_base;

/*
 * Set the au1000_clock
 */
void set_au1x00_speed(unsigned int new_freq)
{
	au1x00_clock = new_freq;
}

unsigned int get_au1x00_speed(void)
{
	return au1x00_clock;
}



/*
 * The UART baud base is not known at compile time ... if
 * we want to be able to use the same code on different
 * speed CPUs.
 */
unsigned long get_au1x00_uart_baud_base(void)
{
	return uart_baud_base;
}

void set_au1x00_uart_baud_base(unsigned long new_baud_base)
{
	uart_baud_base = new_baud_base;
}

/*
 * Calculate the Au1x00's LCD clock based on the current
 * cpu clock and the system bus clock, and try to keep it
 * below 40 MHz (the Pb1000 board can lock-up if the LCD
 * clock is over 40 MHz).
 */
void set_au1x00_lcd_clock(void)
{
	unsigned int static_cfg0;
	unsigned int sys_busclk =
		(get_au1x00_speed()/1000) /
		((int)(au_readl(SYS_POWERCTRL)&0x03) + 2);

	static_cfg0 = au_readl(MEM_STCFG0);

	if (static_cfg0 & (1<<11))
		lcd_clock = sys_busclk / 5; /* note: BCLK switching fails with D5 */
	else
		lcd_clock = sys_busclk / 4;

	if (lcd_clock > 50000) /* Epson MAX */
		printk("warning: LCD clock too high (%d KHz)\n", lcd_clock);
}

unsigned int get_au1x00_lcd_clock(void)
{
	return lcd_clock;
}

EXPORT_SYMBOL(get_au1x00_lcd_clock);
