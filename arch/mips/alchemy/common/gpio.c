/*
 *  Copyright (C) 2007-2009, OpenWrt.org, Florian Fainelli <florian@openwrt.org>
 *  	Architecture specific GPIO support
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
 * 	au1000 SoC have only one GPIO line : GPIO1
 * 	others have a second one : GPIO2
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/gpio.h>

struct au1000_gpio_chip {
	struct gpio_chip	chip;
	void __iomem		*regbase;
};

#if !defined(CONFIG_SOC_AU1000)
static int au1000_gpio2_get(struct gpio_chip *chip, unsigned offset)
{
	u32 mask = 1 << offset;
	struct au1000_gpio_chip *gpch;

	gpch = container_of(chip, struct au1000_gpio_chip, chip);
	return readl(gpch->regbase + AU1000_GPIO2_ST) & mask;
}

static void au1000_gpio2_set(struct gpio_chip *chip,
				unsigned offset, int value)
{
	u32 mask = ((GPIO2_OUT_EN_MASK << offset) | (!!value << offset));
	struct au1000_gpio_chip *gpch;
	unsigned long flags;

	gpch = container_of(chip, struct au1000_gpio_chip, chip);

	local_irq_save(flags);
	writel(mask, gpch->regbase + AU1000_GPIO2_OUT);
	local_irq_restore(flags);
}

static int au1000_gpio2_direction_input(struct gpio_chip *chip, unsigned offset)
{
	u32 mask = 1 << offset;
	u32 tmp;
	struct au1000_gpio_chip *gpch;
	unsigned long flags;

	gpch = container_of(chip, struct au1000_gpio_chip, chip);

	local_irq_save(flags);
	tmp = readl(gpch->regbase + AU1000_GPIO2_DIR);
	tmp &= ~mask;
	writel(tmp, gpch->regbase + AU1000_GPIO2_DIR);
	local_irq_restore(flags);

	return 0;
}

static int au1000_gpio2_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	u32 mask = 1 << offset;
	u32 out_mask = ((GPIO2_OUT_EN_MASK << offset) | (!!value << offset));
	u32 tmp;
	struct au1000_gpio_chip *gpch;
	unsigned long flags;

	gpch = container_of(chip, struct au1000_gpio_chip, chip);

	local_irq_save(flags);
	tmp = readl(gpch->regbase + AU1000_GPIO2_DIR);
	tmp |= mask;
	writel(tmp, gpch->regbase + AU1000_GPIO2_DIR);
	writel(out_mask, gpch->regbase + AU1000_GPIO2_OUT);
	local_irq_restore(flags);

	return 0;
}
#endif /* !defined(CONFIG_SOC_AU1000) */

static int au1000_gpio1_get(struct gpio_chip *chip, unsigned offset)
{
	u32 mask = 1 << offset;
	struct au1000_gpio_chip *gpch;

	gpch = container_of(chip, struct au1000_gpio_chip, chip);
	return readl(gpch->regbase + AU1000_GPIO1_ST) & mask;
}

static void au1000_gpio1_set(struct gpio_chip *chip,
				unsigned offset, int value)
{
	u32 mask = 1 << offset;
	u32 reg_offset;
	struct au1000_gpio_chip *gpch;
	unsigned long flags;

	gpch = container_of(chip, struct au1000_gpio_chip, chip);

	if (value)
		reg_offset = AU1000_GPIO1_OUT;
	else
		reg_offset = AU1000_GPIO1_CLR;

	local_irq_save(flags);
	writel(mask, gpch->regbase + reg_offset);
	local_irq_restore(flags);
}

static int au1000_gpio1_direction_input(struct gpio_chip *chip, unsigned offset)
{
	u32 mask = 1 << offset;
	struct au1000_gpio_chip *gpch;

	gpch = container_of(chip, struct au1000_gpio_chip, chip);
	writel(mask, gpch->regbase + AU1000_GPIO1_ST);

	return 0;
}

static int au1000_gpio1_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	u32 mask = 1 << offset;
	struct au1000_gpio_chip *gpch;

	gpch = container_of(chip, struct au1000_gpio_chip, chip);

	writel(mask, gpch->regbase + AU1000_GPIO1_TRI_OUT);
	au1000_gpio1_set(chip, offset, value);

	return 0;
}

struct au1000_gpio_chip au1000_gpio_chip[] = {
	[0] = {
		.regbase			= (void __iomem *)SYS_BASE,
		.chip = {
			.label			= "au1000-gpio1",
			.direction_input	= au1000_gpio1_direction_input,
			.direction_output	= au1000_gpio1_direction_output,
			.get			= au1000_gpio1_get,
			.set			= au1000_gpio1_set,
			.base			= 0,
			.ngpio			= 32,
		},
	},
#if !defined(CONFIG_SOC_AU1000)
	[1] = {
		.regbase                        = (void __iomem *)GPIO2_BASE,
		.chip = {
			.label                  = "au1000-gpio2",
			.direction_input        = au1000_gpio2_direction_input,
			.direction_output       = au1000_gpio2_direction_output,
			.get                    = au1000_gpio2_get,
			.set                    = au1000_gpio2_set,
			.base                   = AU1XXX_GPIO_BASE,
			.ngpio                  = 32,
		},
	},
#endif
};

static int __init au1000_gpio_init(void)
{
	gpiochip_add(&au1000_gpio_chip[0].chip);
#if !defined(CONFIG_SOC_AU1000)
	gpiochip_add(&au1000_gpio_chip[1].chip);
#endif

	return 0;
}
arch_initcall(au1000_gpio_init);

