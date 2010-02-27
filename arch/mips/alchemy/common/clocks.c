/*
 * BRIEF MODULE DESCRIPTION
 *	Simple Au1xx0 clocks routines.
 *
 * Copyright 2001, 2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com>
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
#include <linux/spinlock.h>
#include <asm/time.h>
#include <asm/mach-au1x00/au1000.h>

/*
 * I haven't found anyone that doesn't use a 12 MHz source clock,
 * but just in case.....
 */
#define AU1000_SRC_CLK	12000000

static unsigned int au1x00_clock; /*  Hz */
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
EXPORT_SYMBOL(get_au1x00_speed);

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
 * We read the real processor speed from the PLL.  This is important
 * because it is more accurate than computing it from the 32 KHz
 * counter, if it exists.  If we don't have an accurate processor
 * speed, all of the peripherals that derive their clocks based on
 * this advertised speed will introduce error and sometimes not work
 * properly.  This function is futher convoluted to still allow configurations
 * to do that in case they have really, really old silicon with a
 * write-only PLL register.			-- Dan
 */
unsigned long au1xxx_calc_clock(void)
{
	unsigned long cpu_speed;

	/*
	 * On early Au1000, sys_cpupll was write-only. Since these
	 * silicon versions of Au1000 are not sold by AMD, we don't bend
	 * over backwards trying to determine the frequency.
	 */
	if (au1xxx_cpu_has_pll_wo())
#ifdef CONFIG_SOC_AU1000_FREQUENCY
		cpu_speed = CONFIG_SOC_AU1000_FREQUENCY;
#else
		cpu_speed = 396000000;
#endif
	else
		cpu_speed = (au_readl(SYS_CPUPLL) & 0x0000003f) * AU1000_SRC_CLK;

	/* On Alchemy CPU:counter ratio is 1:1 */
	mips_hpt_frequency = cpu_speed;
	/* Equation: Baudrate = CPU / (SD * 2 * CLKDIV * 16) */
	set_au1x00_uart_baud_base(cpu_speed / (2 * ((int)(au_readl(SYS_POWERCTRL)
							  & 0x03) + 2) * 16));

	set_au1x00_speed(cpu_speed);

	return cpu_speed;
}
