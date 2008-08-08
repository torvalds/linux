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
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#include <asm/addrspace.h>

#include <asm/mach-rc32434/rb.h>

struct rb532_gpio_reg __iomem *rb532_gpio_reg0;
EXPORT_SYMBOL(rb532_gpio_reg0);

struct mpmc_device dev3;

static struct resource rb532_gpio_reg0_res[] = {
	{
		.name 	= "gpio_reg0",
		.start 	= (u32)(IDT434_REG_BASE + GPIOBASE),
		.end 	= (u32)(IDT434_REG_BASE + GPIOBASE + sizeof(struct rb532_gpio_reg)),
		.flags 	= IORESOURCE_MEM,
	}
};

static struct resource rb532_dev3_ctl_res[] = {
	{
		.name	= "dev3_ctl",
		.start	= (u32)(IDT434_REG_BASE + DEV3BASE),
		.end	= (u32)(IDT434_REG_BASE + DEV3BASE + sizeof(struct dev_reg)),
		.flags	= IORESOURCE_MEM,
	}
};

void set_434_reg(unsigned reg_offs, unsigned bit, unsigned len, unsigned val)
{
	unsigned long flags;
	unsigned data;
	unsigned i = 0;

	spin_lock_irqsave(&dev3.lock, flags);

	data = *(volatile unsigned *) (IDT434_REG_BASE + reg_offs);
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

int rb532_gpio_get_value(unsigned gpio)
{
	return readl(&rb532_gpio_reg0->gpiod) & (1 << gpio);
}
EXPORT_SYMBOL(rb532_gpio_get_value);

void rb532_gpio_set_value(unsigned gpio, int value)
{
	unsigned tmp;

	tmp = readl(&rb532_gpio_reg0->gpiod) & ~(1 << gpio);
	if (value)
		tmp |= 1 << gpio;

	writel(tmp, (void *)&rb532_gpio_reg0->gpiod);
}
EXPORT_SYMBOL(rb532_gpio_set_value);

int rb532_gpio_direction_input(unsigned gpio)
{
	writel(readl(&rb532_gpio_reg0->gpiocfg) & ~(1 << gpio),
	       (void *)&rb532_gpio_reg0->gpiocfg);

	return 0;
}
EXPORT_SYMBOL(rb532_gpio_direction_input);

int rb532_gpio_direction_output(unsigned gpio, int value)
{
	gpio_set_value(gpio, value);
	writel(readl(&rb532_gpio_reg0->gpiocfg) | (1 << gpio),
	       (void *)&rb532_gpio_reg0->gpiocfg);

	return 0;
}
EXPORT_SYMBOL(rb532_gpio_direction_output);

void rb532_gpio_set_int_level(unsigned gpio, int value)
{
	unsigned tmp;

	tmp = readl(&rb532_gpio_reg0->gpioilevel) & ~(1 << gpio);
	if (value)
		tmp |= 1 << gpio;
	writel(tmp, (void *)&rb532_gpio_reg0->gpioilevel);
}
EXPORT_SYMBOL(rb532_gpio_set_int_level);

int rb532_gpio_get_int_level(unsigned gpio)
{
	return readl(&rb532_gpio_reg0->gpioilevel) & (1 << gpio);
}
EXPORT_SYMBOL(rb532_gpio_get_int_level);

void rb532_gpio_set_int_status(unsigned gpio, int value)
{
	unsigned tmp;

	tmp = readl(&rb532_gpio_reg0->gpioistat);
	if (value)
		tmp |= 1 << gpio;
	writel(tmp, (void *)&rb532_gpio_reg0->gpioistat);
}
EXPORT_SYMBOL(rb532_gpio_set_int_status);

int rb532_gpio_get_int_status(unsigned gpio)
{
	return readl(&rb532_gpio_reg0->gpioistat) & (1 << gpio);
}
EXPORT_SYMBOL(rb532_gpio_get_int_status);

void rb532_gpio_set_func(unsigned gpio, int value)
{
	unsigned tmp;

	tmp = readl(&rb532_gpio_reg0->gpiofunc);
	if (value)
		tmp |= 1 << gpio;
	writel(tmp, (void *)&rb532_gpio_reg0->gpiofunc);
}
EXPORT_SYMBOL(rb532_gpio_set_func);

int rb532_gpio_get_func(unsigned gpio)
{
	return readl(&rb532_gpio_reg0->gpiofunc) & (1 << gpio);
}
EXPORT_SYMBOL(rb532_gpio_get_func);

int __init rb532_gpio_init(void)
{
	rb532_gpio_reg0 = ioremap_nocache(rb532_gpio_reg0_res[0].start,
				rb532_gpio_reg0_res[0].end -
				rb532_gpio_reg0_res[0].start);

	if (!rb532_gpio_reg0) {
		printk(KERN_ERR "rb532: cannot remap GPIO register 0\n");
		return -ENXIO;
	}

	dev3.base = ioremap_nocache(rb532_dev3_ctl_res[0].start,
				rb532_dev3_ctl_res[0].end -
				rb532_dev3_ctl_res[0].start);

	if (!dev3.base) {
		printk(KERN_ERR "rb532: cannot remap device controller 3\n");
		return -ENXIO;
	}

	return 0;
}
arch_initcall(rb532_gpio_init);
