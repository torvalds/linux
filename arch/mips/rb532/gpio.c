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
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <asm/mach-rc32434/rb.h>
#include <asm/mach-rc32434/gpio.h>

struct rb532_gpio_chip {
	struct gpio_chip chip;
	void __iomem	 *regbase;
	void		(*set_int_level)(struct gpio_chip *chip, unsigned offset, int value);
	int		(*get_int_level)(struct gpio_chip *chip, unsigned offset);
	void		(*set_int_status)(struct gpio_chip *chip, unsigned offset, int value);
	int		(*get_int_status)(struct gpio_chip *chip, unsigned offset);
};

struct mpmc_device dev3;

static struct resource rb532_gpio_reg0_res[] = {
	{
		.name 	= "gpio_reg0",
		.start 	= REGBASE + GPIOBASE,
		.end 	= REGBASE + GPIOBASE + sizeof(struct rb532_gpio_reg) - 1,
		.flags 	= IORESOURCE_MEM,
	}
};

static struct resource rb532_dev3_ctl_res[] = {
	{
		.name	= "dev3_ctl",
		.start	= REGBASE + DEV3BASE,
		.end	= REGBASE + DEV3BASE + sizeof(struct dev_reg) - 1,
		.flags	= IORESOURCE_MEM,
	}
};

void set_434_reg(unsigned reg_offs, unsigned bit, unsigned len, unsigned val)
{
	unsigned long flags;
	unsigned data;
	unsigned i = 0;

	spin_lock_irqsave(&dev3.lock, flags);

	data = readl(IDT434_REG_BASE + reg_offs);
	for (i = 0; i != len; ++i) {
		if (val & (1 << i))
			data |= (1 << (i + bit));
		else
			data &= ~(1 << (i + bit));
	}
	writel(data, (IDT434_REG_BASE + reg_offs));

	spin_unlock_irqrestore(&dev3.lock, flags);
}
EXPORT_SYMBOL(set_434_reg);

unsigned get_434_reg(unsigned reg_offs)
{
	return readl(IDT434_REG_BASE + reg_offs);
}
EXPORT_SYMBOL(get_434_reg);

void set_latch_u5(unsigned char or_mask, unsigned char nand_mask)
{
	unsigned long flags;

	spin_lock_irqsave(&dev3.lock, flags);

	dev3.state = (dev3.state | or_mask) & ~nand_mask;
	writel(dev3.state, &dev3.base);

	spin_unlock_irqrestore(&dev3.lock, flags);
}
EXPORT_SYMBOL(set_latch_u5);

unsigned char get_latch_u5(void)
{
	return dev3.state;
}
EXPORT_SYMBOL(get_latch_u5);

/*
 * Return GPIO level */
static int rb532_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	u32			mask = 1 << offset;
	struct rb532_gpio_chip	*gpch;

	gpch = container_of(chip, struct rb532_gpio_chip, chip);
	return readl(gpch->regbase + GPIOD) & mask;
}

/*
 * Set output GPIO level
 */
static void rb532_gpio_set(struct gpio_chip *chip,
				unsigned offset, int value)
{
	unsigned long		flags;
	u32			mask = 1 << offset;
	u32			tmp;
	struct rb532_gpio_chip	*gpch;
	void __iomem		*gpvr;

	gpch = container_of(chip, struct rb532_gpio_chip, chip);
	gpvr = gpch->regbase + GPIOD;

	local_irq_save(flags);
	tmp = readl(gpvr);
	if (value)
		tmp |= mask;
	else
		tmp &= ~mask;
	writel(tmp, gpvr);
	local_irq_restore(flags);
}

/*
 * Set GPIO direction to input
 */
static int rb532_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	unsigned long		flags;
	u32			mask = 1 << offset;
	u32			value;
	struct rb532_gpio_chip	*gpch;
	void __iomem		*gpdr;

	gpch = container_of(chip, struct rb532_gpio_chip, chip);
	gpdr = gpch->regbase + GPIOCFG;

	local_irq_save(flags);
	value = readl(gpdr);
	value &= ~mask;
	writel(value, gpdr);
	local_irq_restore(flags);

	return 0;
}

/*
 * Set GPIO direction to output
 */
static int rb532_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	unsigned long		flags;
	u32			mask = 1 << offset;
	u32			tmp;
	struct rb532_gpio_chip	*gpch;
	void __iomem		*gpdr;

	gpch = container_of(chip, struct rb532_gpio_chip, chip);
	writel(mask, gpch->regbase + GPIOD);
	gpdr = gpch->regbase + GPIOCFG;

	local_irq_save(flags);
	tmp = readl(gpdr);
	tmp |= mask;
	writel(tmp, gpdr);
	local_irq_restore(flags);

	return 0;
}

/*
 * Set the GPIO interrupt level
 */
static void rb532_gpio_set_int_level(struct gpio_chip *chip,
					unsigned offset, int value)
{
	unsigned long		flags;
	u32			mask = 1 << offset;
	u32			tmp;
	struct rb532_gpio_chip	*gpch;
	void __iomem		*gpil;

	gpch = container_of(chip, struct rb532_gpio_chip, chip);
	gpil = gpch->regbase + GPIOILEVEL;

	local_irq_save(flags);
	tmp = readl(gpil);
	if (value)
		tmp |= mask;
	else
		tmp &= ~mask;
	writel(tmp, gpil);
	local_irq_restore(flags);
}

/*
 * Get the GPIO interrupt level
 */
static int rb532_gpio_get_int_level(struct gpio_chip *chip, unsigned offset)
{
	u32			mask = 1 << offset;
	struct rb532_gpio_chip	*gpch;

	gpch = container_of(chip, struct rb532_gpio_chip, chip);
	return readl(gpch->regbase + GPIOILEVEL) & mask;
}

/*
 * Set the GPIO interrupt status
 */
static void rb532_gpio_set_int_status(struct gpio_chip *chip,
				unsigned offset, int value)
{
	unsigned long		flags;
	u32			mask = 1 << offset;
	u32			tmp;
	struct rb532_gpio_chip	*gpch;
	void __iomem		*gpis;

	gpch = container_of(chip, struct rb532_gpio_chip, chip);
	gpis = gpch->regbase + GPIOISTAT;

	local_irq_save(flags);
	tmp = readl(gpis);
	if (value)
		tmp |= mask;
	else
		tmp &= ~mask;
	writel(tmp, gpis);
	local_irq_restore(flags);
}

/*
 * Get the GPIO interrupt status
 */
static int rb532_gpio_get_int_status(struct gpio_chip *chip, unsigned offset)
{
	u32			mask = 1 << offset;
	struct rb532_gpio_chip	*gpch;

	gpch = container_of(chip, struct rb532_gpio_chip, chip);
	return readl(gpch->regbase + GPIOISTAT) & mask;
}

static struct rb532_gpio_chip rb532_gpio_chip[] = {
	[0] = {
		.chip = {
			.label			= "gpio0",
			.direction_input	= rb532_gpio_direction_input,
			.direction_output	= rb532_gpio_direction_output,
			.get			= rb532_gpio_get,
			.set			= rb532_gpio_set,
			.base			= 0,
			.ngpio			= 32,
		},
		.get_int_level		= rb532_gpio_get_int_level,
		.set_int_level		= rb532_gpio_set_int_level,
		.get_int_status		= rb532_gpio_get_int_status,
		.set_int_status		= rb532_gpio_set_int_status,
	},
};

int __init rb532_gpio_init(void)
{
	struct resource *r;

	r = rb532_gpio_reg0_res;
	rb532_gpio_chip->regbase = ioremap_nocache(r->start, r->end - r->start);

	if (!rb532_gpio_chip->regbase) {
		printk(KERN_ERR "rb532: cannot remap GPIO register 0\n");
		return -ENXIO;
	}

	/* Register our GPIO chip */
	gpiochip_add(&rb532_gpio_chip->chip);

	r = rb532_dev3_ctl_res;
	dev3.base = ioremap_nocache(r->start, r->end - r->start);

	if (!dev3.base) {
		printk(KERN_ERR "rb532: cannot remap device controller 3\n");
		return -ENXIO;
	}

	return 0;
}
arch_initcall(rb532_gpio_init);
