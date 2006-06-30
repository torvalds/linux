/*
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
#include <au1000.h>
#include <au1xxx_gpio.h>

#define gpio1 sys
#if !defined(CONFIG_SOC_AU1000)
static AU1X00_GPIO2 * const gpio2 = (AU1X00_GPIO2 *)GPIO2_BASE;

#define GPIO2_OUTPUT_ENABLE_MASK 0x00010000

int au1xxx_gpio2_read(int signal)
{
	signal -= 200;
/*	gpio2->dir &= ~(0x01 << signal);						//Set GPIO to input */
	return ((gpio2->pinstate >> signal) & 0x01);
}

void au1xxx_gpio2_write(int signal, int value)
{
	signal -= 200;

	gpio2->output = (GPIO2_OUTPUT_ENABLE_MASK << signal) |
		(value << signal);
}

void au1xxx_gpio2_tristate(int signal)
{
	signal -= 200;
	gpio2->dir &= ~(0x01 << signal); 	/* Set GPIO to input */
}
#endif

int au1xxx_gpio1_read(int signal)
{
/*	gpio1->trioutclr |= (0x01 << signal); */
	return ((gpio1->pinstaterd >> signal) & 0x01);
}

void au1xxx_gpio1_write(int signal, int value)
{
	if(value)
		gpio1->outputset = (0x01 << signal);
	else
		gpio1->outputclr = (0x01 << signal);	/* Output a Zero */
}

void au1xxx_gpio1_tristate(int signal)
{
	gpio1->trioutclr = (0x01 << signal);		/* Tristate signal */
}


int au1xxx_gpio_read(int signal)
{
	if(signal >= 200)
#if defined(CONFIG_SOC_AU1000)
		return 0;
#else
		return au1xxx_gpio2_read(signal);
#endif
	else
		return au1xxx_gpio1_read(signal);
}

void au1xxx_gpio_write(int signal, int value)
{
	if(signal >= 200)
#if defined(CONFIG_SOC_AU1000)
		;
#else
		au1xxx_gpio2_write(signal, value);
#endif
	else
		au1xxx_gpio1_write(signal, value);
}

void au1xxx_gpio_tristate(int signal)
{
	if(signal >= 200)
#if defined(CONFIG_SOC_AU1000)
		;
#else
		au1xxx_gpio2_tristate(signal);
#endif
	else
		au1xxx_gpio1_tristate(signal);
}

void au1xxx_gpio1_set_inputs(void)
{
	gpio1->pininputen = 0;
}

EXPORT_SYMBOL(au1xxx_gpio1_set_inputs);
EXPORT_SYMBOL(au1xxx_gpio_tristate);
EXPORT_SYMBOL(au1xxx_gpio_write);
EXPORT_SYMBOL(au1xxx_gpio_read);
