/*
 *  Copyright (C) 2007-2009, OpenWrt.org, Florian Fainelli <florian@openwrt.org>
 *	GPIOLIB support for Alchemy chips.
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
 *
 *  Notes :
 *	au1000 SoC have only one GPIO block : GPIO1
 *	Au1100, Au15x0, Au12x0 have a second one : GPIO2
 *	Au1300 is totally different: 1 block with up to 128 GPIOs
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/gpio/driver.h>
#include <asm/mach-au1x00/gpio-au1000.h>
#include <asm/mach-au1x00/gpio-au1300.h>

static int gpio2_get(struct gpio_chip *chip, unsigned offset)
{
	return !!alchemy_gpio2_get_value(offset + ALCHEMY_GPIO2_BASE);
}

static int gpio2_set(struct gpio_chip *chip, unsigned offset, int value)
{
	alchemy_gpio2_set_value(offset + ALCHEMY_GPIO2_BASE, value);

	return 0;
}

static int gpio2_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return alchemy_gpio2_direction_input(offset + ALCHEMY_GPIO2_BASE);
}

static int gpio2_direction_output(struct gpio_chip *chip, unsigned offset,
				  int value)
{
	return alchemy_gpio2_direction_output(offset + ALCHEMY_GPIO2_BASE,
						value);
}

static int gpio2_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return alchemy_gpio2_to_irq(offset + ALCHEMY_GPIO2_BASE);
}


static int gpio1_get(struct gpio_chip *chip, unsigned offset)
{
	return !!alchemy_gpio1_get_value(offset + ALCHEMY_GPIO1_BASE);
}

static int gpio1_set(struct gpio_chip *chip,
				unsigned offset, int value)
{
	alchemy_gpio1_set_value(offset + ALCHEMY_GPIO1_BASE, value);

	return 0;
}

static int gpio1_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return alchemy_gpio1_direction_input(offset + ALCHEMY_GPIO1_BASE);
}

static int gpio1_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	return alchemy_gpio1_direction_output(offset + ALCHEMY_GPIO1_BASE,
					     value);
}

static int gpio1_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return alchemy_gpio1_to_irq(offset + ALCHEMY_GPIO1_BASE);
}

struct gpio_chip alchemy_gpio_chip[] = {
	[0] = {
		.label			= "alchemy-gpio1",
		.direction_input	= gpio1_direction_input,
		.direction_output	= gpio1_direction_output,
		.get			= gpio1_get,
		.set			= gpio1_set,
		.to_irq			= gpio1_to_irq,
		.base			= ALCHEMY_GPIO1_BASE,
		.ngpio			= ALCHEMY_GPIO1_NUM,
	},
	[1] = {
		.label			= "alchemy-gpio2",
		.direction_input	= gpio2_direction_input,
		.direction_output	= gpio2_direction_output,
		.get			= gpio2_get,
		.set			= gpio2_set,
		.to_irq			= gpio2_to_irq,
		.base			= ALCHEMY_GPIO2_BASE,
		.ngpio			= ALCHEMY_GPIO2_NUM,
	},
};

static int alchemy_gpic_get(struct gpio_chip *chip, unsigned int off)
{
	return !!au1300_gpio_get_value(off + AU1300_GPIO_BASE);
}

static int alchemy_gpic_set(struct gpio_chip *chip, unsigned int off, int v)
{
	au1300_gpio_set_value(off + AU1300_GPIO_BASE, v);

	return 0;
}

static int alchemy_gpic_dir_input(struct gpio_chip *chip, unsigned int off)
{
	return au1300_gpio_direction_input(off + AU1300_GPIO_BASE);
}

static int alchemy_gpic_dir_output(struct gpio_chip *chip, unsigned int off,
				   int v)
{
	return au1300_gpio_direction_output(off + AU1300_GPIO_BASE, v);
}

static int alchemy_gpic_gpio_to_irq(struct gpio_chip *chip, unsigned int off)
{
	return au1300_gpio_to_irq(off + AU1300_GPIO_BASE);
}

static struct gpio_chip au1300_gpiochip = {
	.label			= "alchemy-gpic",
	.direction_input	= alchemy_gpic_dir_input,
	.direction_output	= alchemy_gpic_dir_output,
	.get			= alchemy_gpic_get,
	.set			= alchemy_gpic_set,
	.to_irq			= alchemy_gpic_gpio_to_irq,
	.base			= AU1300_GPIO_BASE,
	.ngpio			= AU1300_GPIO_NUM,
};

static int __init alchemy_gpiochip_init(void)
{
	int ret = 0;

	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1000:
		ret = gpiochip_add_data(&alchemy_gpio_chip[0], NULL);
		break;
	case ALCHEMY_CPU_AU1500...ALCHEMY_CPU_AU1200:
		ret = gpiochip_add_data(&alchemy_gpio_chip[0], NULL);
		ret |= gpiochip_add_data(&alchemy_gpio_chip[1], NULL);
		break;
	case ALCHEMY_CPU_AU1300:
		ret = gpiochip_add_data(&au1300_gpiochip, NULL);
		break;
	}
	return ret;
}
arch_initcall(alchemy_gpiochip_init);
