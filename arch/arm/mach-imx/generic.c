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

#include <asm/arch/imxfb.h>
#include <asm/hardware.h>
#include <asm/arch/imx-regs.h>

#include <asm/mach/map.h>
#include <asm/arch/mmc.h>

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

/*
 *  get the system pll clock in Hz
 *
 *                  mfi + mfn / (mfd +1)
 *  f = 2 * f_ref * --------------------
 *                        pd + 1
 */
static unsigned int imx_decode_pll(unsigned int pll, u32 f_ref)
{
	unsigned long long ll;
	unsigned long quot;

	u32 mfi = (pll >> 10) & 0xf;
	u32 mfn = pll & 0x3ff;
	u32 mfd = (pll >> 16) & 0x3ff;
	u32 pd =  (pll >> 26) & 0xf;

	mfi = mfi <= 5 ? 5 : mfi;

	ll = 2 * (unsigned long long)f_ref * ( (mfi<<16) + (mfn<<16) / (mfd+1) );
	quot = (pd+1) * (1<<16);
	ll += quot / 2;
	do_div(ll, quot);
	return (unsigned int) ll;
}

unsigned int imx_get_system_clk(void)
{
	u32 f_ref = (CSCR & CSCR_SYSTEM_SEL) ? 16000000 : (CLK32 * 512);

	return imx_decode_pll(SPCTL0, f_ref);
}
EXPORT_SYMBOL(imx_get_system_clk);

unsigned int imx_get_mcu_clk(void)
{
	return imx_decode_pll(MPCTL0, CLK32 * 512);
}
EXPORT_SYMBOL(imx_get_mcu_clk);

/*
 *  get peripheral clock 1 ( UART[12], Timer[12], PWM )
 */
unsigned int imx_get_perclk1(void)
{
	return imx_get_system_clk() / (((PCDR) & 0xf)+1);
}
EXPORT_SYMBOL(imx_get_perclk1);

/*
 *  get peripheral clock 2 ( LCD, SD, SPI[12] )
 */
unsigned int imx_get_perclk2(void)
{
	return imx_get_system_clk() / (((PCDR>>4) & 0xf)+1);
}
EXPORT_SYMBOL(imx_get_perclk2);

/*
 *  get peripheral clock 3 ( SSI )
 */
unsigned int imx_get_perclk3(void)
{
	return imx_get_system_clk() / (((PCDR>>16) & 0x7f)+1);
}
EXPORT_SYMBOL(imx_get_perclk3);

/*
 *  get hclk ( SDRAM, CSI, Memory Stick, I2C, DMA )
 */
unsigned int imx_get_hclk(void)
{
	return imx_get_system_clk() / (((CSCR>>10) & 0xf)+1);
}
EXPORT_SYMBOL(imx_get_hclk);

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
EXPORT_SYMBOL(imx_set_mmc_info);

static struct imxfb_mach_info imx_fb_info;

void __init set_imx_fb_info(struct imxfb_mach_info *hard_imx_fb_info)
{
	memcpy(&imx_fb_info,hard_imx_fb_info,sizeof(struct imxfb_mach_info));
}
EXPORT_SYMBOL(set_imx_fb_info);

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
