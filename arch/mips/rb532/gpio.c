/*
 *  Miscellaneous functions for IDT EB434 board
 *
 *  Copyright 2004 IDT Inc. (rischelp@idt.com)
 *  Copyright 2006 Phil Sutter <n0-1@freewrt.org>
 *  Copyright 2007 Florian Fainelli <florian@openwrt.org>
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/gpio/driver.h>

#include <asm/mach-rc32434/rb.h>
#include <asm/mach-rc32434/gpio.h>

struct rb532_gpio_chip {
	struct gpio_chip chip;
	void __iomem	 *regbase;
};

static struct resource rb532_gpio_reg0_res[] = {
	{
		.name	= "gpio_reg0",
		.start	= REGBASE + GPIOBASE,
		.end	= REGBASE + GPIOBASE + sizeof(struct rb532_gpio_reg) - 1,
		.flags	= IORESOURCE_MEM,
	}
};

/* rb532_set_bit - sanely set a bit
 *
 * bitval: new value for the bit
 * offset: bit index in the 4 byte address range
 * ioaddr: 4 byte aligned address being altered
 */
static inline void rb532_set_bit(unsigned bitval,
		unsigned offset, void __iomem *ioaddr)
{
	unsigned long flags;
	u32 val;

	local_irq_save(flags);

	val = readl(ioaddr);
	val &= ~(!bitval << offset);   /* unset bit if bitval == 0 */
	val |= (!!bitval << offset);   /* set bit if bitval == 1 */
	writel(val, ioaddr);

	local_irq_restore(flags);
}

/* rb532_get_bit - read a bit
 *
 * returns the boolean state of the bit, which may be > 1
 */
static inline int rb532_get_bit(unsigned offset, void __iomem *ioaddr)
{
	return readl(ioaddr) & (1 << offset);
}

/*
 * Return GPIO level */
static int rb532_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct rb532_gpio_chip	*gpch;

	gpch = gpiochip_get_data(chip);
	return !!rb532_get_bit(offset, gpch->regbase + GPIOD);
}

/*
 * Set output GPIO level
 */
static void rb532_gpio_set(struct gpio_chip *chip,
				unsigned offset, int value)
{
	struct rb532_gpio_chip	*gpch;

	gpch = gpiochip_get_data(chip);
	rb532_set_bit(value, offset, gpch->regbase + GPIOD);
}

/*
 * Set GPIO direction to input
 */
static int rb532_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct rb532_gpio_chip	*gpch;

	gpch = gpiochip_get_data(chip);

	/* disable alternate function in case it's set */
	rb532_set_bit(0, offset, gpch->regbase + GPIOFUNC);

	rb532_set_bit(0, offset, gpch->regbase + GPIOCFG);
	return 0;
}

/*
 * Set GPIO direction to output
 */
static int rb532_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	struct rb532_gpio_chip	*gpch;

	gpch = gpiochip_get_data(chip);

	/* disable alternate function in case it's set */
	rb532_set_bit(0, offset, gpch->regbase + GPIOFUNC);

	/* set the initial output value */
	rb532_set_bit(value, offset, gpch->regbase + GPIOD);

	rb532_set_bit(1, offset, gpch->regbase + GPIOCFG);
	return 0;
}

static int rb532_gpio_to_irq(struct gpio_chip *chip, unsigned gpio)
{
	return 8 + 4 * 32 + gpio;
}

static struct rb532_gpio_chip rb532_gpio_chip[] = {
	[0] = {
		.chip = {
			.label			= "gpio0",
			.direction_input	= rb532_gpio_direction_input,
			.direction_output	= rb532_gpio_direction_output,
			.get			= rb532_gpio_get,
			.set			= rb532_gpio_set,
			.to_irq			= rb532_gpio_to_irq,
			.base			= 0,
			.ngpio			= 32,
		},
	},
};

/*
 * Set GPIO interrupt level
 */
void rb532_gpio_set_ilevel(int bit, unsigned gpio)
{
	rb532_set_bit(bit, gpio, rb532_gpio_chip->regbase + GPIOILEVEL);
}
EXPORT_SYMBOL(rb532_gpio_set_ilevel);

/*
 * Set GPIO interrupt status
 */
void rb532_gpio_set_istat(int bit, unsigned gpio)
{
	rb532_set_bit(bit, gpio, rb532_gpio_chip->regbase + GPIOISTAT);
}
EXPORT_SYMBOL(rb532_gpio_set_istat);

/*
 * Configure GPIO alternate function
 */
void rb532_gpio_set_func(unsigned gpio)
{
       rb532_set_bit(1, gpio, rb532_gpio_chip->regbase + GPIOFUNC);
}
EXPORT_SYMBOL(rb532_gpio_set_func);

int __init rb532_gpio_init(void)
{
	struct resource *r;

	r = rb532_gpio_reg0_res;
	rb532_gpio_chip->regbase = ioremap(r->start, resource_size(r));

	if (!rb532_gpio_chip->regbase) {
		printk(KERN_ERR "rb532: cannot remap GPIO register 0\n");
		return -ENXIO;
	}

	/* Register our GPIO chip */
	gpiochip_add_data(&rb532_gpio_chip->chip, rb532_gpio_chip);

	return 0;
}
arch_initcall(rb532_gpio_init);
