/*
 *  arch/arm/mach-imx/generic.c
 *
 *  author: Sascha Hauer
 *  Created: april 20th, 2004
 *  Copyright: Synertronixx GmbH
 *
 *  Common code for i.MX machines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include <asm/errno.h>
#include <mach/imxfb.h>
#include <mach/hardware.h>
#include <mach/imx-regs.h>

#include <asm/mach/map.h>
#include <mach/mmc.h>
#include <mach/gpio.h>

unsigned long imx_gpio_alloc_map[(GPIO_PORT_MAX + 1) * 32 / BITS_PER_LONG];

void imx_gpio_mode(int gpio_mode)
{
	unsigned int pin = gpio_mode & GPIO_PIN_MASK;
	unsigned int port = (gpio_mode & GPIO_PORT_MASK) >> GPIO_PORT_SHIFT;
	unsigned int ocr = (gpio_mode & GPIO_OCR_MASK) >> GPIO_OCR_SHIFT;
	unsigned int tmp;

	/* Pullup enable */
	if(gpio_mode & GPIO_PUEN)
		PUEN(port) |= (1<<pin);
	else
		PUEN(port) &= ~(1<<pin);

	/* Data direction */
	if(gpio_mode & GPIO_OUT)
		DDIR(port) |= 1<<pin;
	else
		DDIR(port) &= ~(1<<pin);

	/* Primary / alternate function */
	if(gpio_mode & GPIO_AF)
		GPR(port) |= (1<<pin);
	else
		GPR(port) &= ~(1<<pin);

	/* use as gpio? */
	if(gpio_mode &  GPIO_GIUS)
		GIUS(port) |= (1<<pin);
	else
		GIUS(port) &= ~(1<<pin);

	/* Output / input configuration */
	/* FIXME: I'm not very sure about OCR and ICONF, someone
	 * should have a look over it
	 */
	if(pin<16) {
		tmp = OCR1(port);
		tmp &= ~( 3<<(pin*2));
		tmp |= (ocr << (pin*2));
		OCR1(port) = tmp;

		ICONFA1(port) &= ~( 3<<(pin*2));
		ICONFA1(port) |= ((gpio_mode >> GPIO_AOUT_SHIFT) & 3) << (pin * 2);
		ICONFB1(port) &= ~( 3<<(pin*2));
		ICONFB1(port) |= ((gpio_mode >> GPIO_BOUT_SHIFT) & 3) << (pin * 2);
	} else {
		tmp = OCR2(port);
		tmp &= ~( 3<<((pin-16)*2));
		tmp |= (ocr << ((pin-16)*2));
		OCR2(port) = tmp;

		ICONFA2(port) &= ~( 3<<((pin-16)*2));
		ICONFA2(port) |= ((gpio_mode >> GPIO_AOUT_SHIFT) & 3) << ((pin-16) * 2);
		ICONFB2(port) &= ~( 3<<((pin-16)*2));
		ICONFB2(port) |= ((gpio_mode >> GPIO_BOUT_SHIFT) & 3) << ((pin-16) * 2);
	}
}

EXPORT_SYMBOL(imx_gpio_mode);

int imx_gpio_request(unsigned gpio, const char *label)
{
	if(gpio >= (GPIO_PORT_MAX + 1) * 32) {
		printk(KERN_ERR "imx_gpio: Attempt to request nonexistent GPIO %d for \"%s\"\n",
			gpio, label ? label : "?");
		return -EINVAL;
	}

	if(test_and_set_bit(gpio, imx_gpio_alloc_map)) {
		printk(KERN_ERR "imx_gpio: GPIO %d already used. Allocation for \"%s\" failed\n",
			gpio, label ? label : "?");
		return -EBUSY;
	}

	return 0;
}

EXPORT_SYMBOL(imx_gpio_request);

void imx_gpio_free(unsigned gpio)
{
	if(gpio >= (GPIO_PORT_MAX + 1) * 32)
		return;

	clear_bit(gpio, imx_gpio_alloc_map);
}

EXPORT_SYMBOL(imx_gpio_free);

int imx_gpio_direction_input(unsigned gpio)
{
	imx_gpio_mode(gpio | GPIO_IN | GPIO_GIUS | GPIO_DR);
	return 0;
}

EXPORT_SYMBOL(imx_gpio_direction_input);

int imx_gpio_direction_output(unsigned gpio, int value)
{
	imx_gpio_set_value(gpio, value);
	imx_gpio_mode(gpio | GPIO_OUT | GPIO_GIUS | GPIO_DR);
	return 0;
}

EXPORT_SYMBOL(imx_gpio_direction_output);

int imx_gpio_setup_multiple_pins(const int *pin_list, unsigned count,
				int alloc_mode, const char *label)
{
	const int *p = pin_list;
	int i;
	unsigned gpio;
	unsigned mode;

	for (i = 0; i < count; i++) {
		gpio = *p & (GPIO_PIN_MASK | GPIO_PORT_MASK);
		mode = *p & ~(GPIO_PIN_MASK | GPIO_PORT_MASK);

		if (gpio >= (GPIO_PORT_MAX + 1) * 32)
			goto setup_error;

		if (alloc_mode & IMX_GPIO_ALLOC_MODE_RELEASE)
			imx_gpio_free(gpio);
		else if (!(alloc_mode & IMX_GPIO_ALLOC_MODE_NO_ALLOC))
			if (imx_gpio_request(gpio, label))
				if (!(alloc_mode & IMX_GPIO_ALLOC_MODE_TRY_ALLOC))
					goto setup_error;

		if (!(alloc_mode & (IMX_GPIO_ALLOC_MODE_ALLOC_ONLY |
				    IMX_GPIO_ALLOC_MODE_RELEASE)))
			imx_gpio_mode(gpio | mode);

		p++;
	}
	return 0;

setup_error:
	if(alloc_mode & (IMX_GPIO_ALLOC_MODE_NO_ALLOC |
		         IMX_GPIO_ALLOC_MODE_TRY_ALLOC))
		return -EINVAL;

	while (p != pin_list) {
		p--;
		gpio = *p & (GPIO_PIN_MASK | GPIO_PORT_MASK);
		imx_gpio_free(gpio);
	}

	return -EINVAL;
}

EXPORT_SYMBOL(imx_gpio_setup_multiple_pins);

void __imx_gpio_set_value(unsigned gpio, int value)
{
	imx_gpio_set_value_inline(gpio, value);
}

EXPORT_SYMBOL(__imx_gpio_set_value);

int imx_gpio_to_irq(unsigned gpio)
{
	return IRQ_GPIOA(0) + gpio;
}

EXPORT_SYMBOL(imx_gpio_to_irq);

int imx_irq_to_gpio(unsigned irq)
{
	if (irq < IRQ_GPIOA(0))
		return -EINVAL;
	return irq - IRQ_GPIOA(0);
}

EXPORT_SYMBOL(imx_irq_to_gpio);

static struct resource imx_mmc_resources[] = {
	[0] = {
		.start	= 0x00214000,
		.end	= 0x002140FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= (SDHC_INT),
		.end	= (SDHC_INT),
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 imxmmmc_dmamask = 0xffffffffUL;

static struct platform_device imx_mmc_device = {
	.name		= "imx-mmc",
	.id		= 0,
	.dev		= {
		.dma_mask = &imxmmmc_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(imx_mmc_resources),
	.resource	= imx_mmc_resources,
};

void __init imx_set_mmc_info(struct imxmmc_platform_data *info)
{
	imx_mmc_device.dev.platform_data = info;
}

static struct imx_fb_platform_data imx_fb_info;

void __init set_imx_fb_info(struct imx_fb_platform_data *hard_imx_fb_info)
{
	memcpy(&imx_fb_info,hard_imx_fb_info,sizeof(struct imx_fb_platform_data));
}

static struct resource imxfb_resources[] = {
	[0] = {
		.start	= 0x00205000,
		.end	= 0x002050FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= LCDC_INT,
		.end	= LCDC_INT,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 fb_dma_mask = ~(u64)0;

static struct platform_device imxfb_device = {
	.name		= "imx-fb",
	.id		= 0,
	.dev		= {
 		.platform_data	= &imx_fb_info,
		.dma_mask	= &fb_dma_mask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(imxfb_resources),
	.resource	= imxfb_resources,
};

static struct platform_device *devices[] __initdata = {
	&imx_mmc_device,
	&imxfb_device,
};

static struct map_desc imx_io_desc[] __initdata = {
	{
		.virtual	= IMX_IO_BASE,
		.pfn		= __phys_to_pfn(IMX_IO_PHYS),
		.length		= IMX_IO_SIZE,
		.type		= MT_DEVICE
	}
};

void __init
imx_map_io(void)
{
	iotable_init(imx_io_desc, ARRAY_SIZE(imx_io_desc));
}

static int __init imx_init(void)
{
	return platform_add_devices(devices, ARRAY_SIZE(devices));
}

subsys_initcall(imx_init);
