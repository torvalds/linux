/* linux/arch/arm/mach-vt8500/devices.c
 *
 * Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/pwm_backlight.h>
#include <linux/memblock.h>

#include <asm/mach/arch.h>

#include <mach/vt8500fb.h>
#include <mach/i8042.h>
#include "devices.h"

/* These can't use resources currently */
unsigned long wmt_ic_base __initdata;
unsigned long wmt_sic_base __initdata;
unsigned long wmt_gpio_base __initdata;
unsigned long wmt_pmc_base __initdata;
unsigned long wmt_i8042_base __initdata;

int wmt_nr_irqs __initdata;
int wmt_timer_irq __initdata;
int wmt_gpio_ext_irq[8] __initdata;

/* Should remain accessible after init.
 * i8042 driver desperately calls for attention...
 */
int wmt_i8042_kbd_irq;
int wmt_i8042_aux_irq;

static u64 fb_dma_mask = DMA_BIT_MASK(32);

struct platform_device vt8500_device_lcdc = {
	.name           = "vt8500-lcd",
	.id             = 0,
	.dev		= {
		.dma_mask	= &fb_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

struct platform_device vt8500_device_wm8505_fb = {
	.name           = "wm8505-fb",
	.id             = 0,
};

/* Smallest to largest */
static struct vt8500fb_platform_data panels[] = {
#ifdef CONFIG_WMT_PANEL_800X480
{
	.xres_virtual	= 800,
	.yres_virtual	= 480 * 2,
	.mode		= {
		.name		= "800x480",
		.xres		= 800,
		.yres		= 480,
		.left_margin	= 88,
		.right_margin	= 40,
		.upper_margin	= 32,
		.lower_margin	= 11,
		.hsync_len	= 0,
		.vsync_len	= 1,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
},
#endif
#ifdef CONFIG_WMT_PANEL_800X600
{
	.xres_virtual	= 800,
	.yres_virtual	= 600 * 2,
	.mode		= {
		.name		= "800x600",
		.xres		= 800,
		.yres		= 600,
		.left_margin	= 88,
		.right_margin	= 40,
		.upper_margin	= 32,
		.lower_margin	= 11,
		.hsync_len	= 0,
		.vsync_len	= 1,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
},
#endif
#ifdef CONFIG_WMT_PANEL_1024X576
{
	.xres_virtual	= 1024,
	.yres_virtual	= 576 * 2,
	.mode		= {
		.name		= "1024x576",
		.xres		= 1024,
		.yres		= 576,
		.left_margin	= 40,
		.right_margin	= 24,
		.upper_margin	= 32,
		.lower_margin	= 11,
		.hsync_len	= 96,
		.vsync_len	= 2,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
},
#endif
#ifdef CONFIG_WMT_PANEL_1024X600
{
	.xres_virtual	= 1024,
	.yres_virtual	= 600 * 2,
	.mode		= {
		.name		= "1024x600",
		.xres		= 1024,
		.yres		= 600,
		.left_margin	= 66,
		.right_margin	= 2,
		.upper_margin	= 19,
		.lower_margin	= 1,
		.hsync_len	= 23,
		.vsync_len	= 8,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
},
#endif
};

static int current_panel_idx __initdata = ARRAY_SIZE(panels) - 1;

static int __init panel_setup(char *str)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(panels); i++) {
		if (strcmp(panels[i].mode.name, str) == 0) {
			current_panel_idx = i;
			break;
		}
	}
	return 0;
}

early_param("panel", panel_setup);

static inline void preallocate_fb(struct vt8500fb_platform_data *p,
				  unsigned long align) {
	p->video_mem_len = (p->xres_virtual * p->yres_virtual * 4) >>
			(p->bpp > 16 ? 0 : (p->bpp > 8 ? 1 :
					(8 / p->bpp) + 1));
	p->video_mem_phys = (unsigned long)memblock_alloc(p->video_mem_len,
							  align);
	p->video_mem_virt = phys_to_virt(p->video_mem_phys);
}

struct platform_device vt8500_device_uart0 = {
	.name		= "vt8500_serial",
	.id		= 0,
};

struct platform_device vt8500_device_uart1 = {
	.name		= "vt8500_serial",
	.id		= 1,
};

struct platform_device vt8500_device_uart2 = {
	.name		= "vt8500_serial",
	.id		= 2,
};

struct platform_device vt8500_device_uart3 = {
	.name		= "vt8500_serial",
	.id		= 3,
};

struct platform_device vt8500_device_uart4 = {
	.name		= "vt8500_serial",
	.id		= 4,
};

struct platform_device vt8500_device_uart5 = {
	.name		= "vt8500_serial",
	.id		= 5,
};

static u64 ehci_dma_mask = DMA_BIT_MASK(32);

struct platform_device vt8500_device_ehci = {
	.name		= "vt8500-ehci",
	.id		= 0,
	.dev		= {
		.dma_mask	= &ehci_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

struct platform_device vt8500_device_ge_rops = {
	.name		= "wmt_ge_rops",
	.id		= -1,
};

struct platform_device vt8500_device_pwm = {
	.name		= "vt8500-pwm",
	.id		= 0,
};

static struct platform_pwm_backlight_data vt8500_pwmbl_data = {
	.pwm_id		= 0,
	.max_brightness	= 128,
	.dft_brightness = 70,
	.pwm_period_ns	= 250000, /* revisit when clocks are implemented */
};

struct platform_device vt8500_device_pwmbl = {
	.name		= "pwm-backlight",
	.id		= 0,
	.dev		= {
		.platform_data = &vt8500_pwmbl_data,
	},
};

struct platform_device vt8500_device_rtc = {
	.name		= "vt8500-rtc",
	.id		= 0,
};

struct map_desc wmt_io_desc[] __initdata = {
	/* SoC MMIO registers */
	[0] = {
		.virtual	= 0xf8000000,
		.pfn		= __phys_to_pfn(0xd8000000),
		.length		= 0x00390000, /* max of all chip variants */
		.type		= MT_DEVICE
	},
	/* PCI I/O space, numbers tied to those in <mach/io.h> */
	[1] = {
		.virtual	= 0xf0000000,
		.pfn		= __phys_to_pfn(0xc0000000),
		.length		= SZ_64K,
		.type		= MT_DEVICE
	},
};

void __init vt8500_reserve_mem(void)
{
#ifdef CONFIG_FB_VT8500
	panels[current_panel_idx].bpp = 16; /* Always use RGB565 */
	preallocate_fb(&panels[current_panel_idx], SZ_4M);
	vt8500_device_lcdc.dev.platform_data = &panels[current_panel_idx];
#endif
}

void __init wm8505_reserve_mem(void)
{
#if defined CONFIG_FB_WM8505
	panels[current_panel_idx].bpp = 32; /* Always use RGB888 */
	preallocate_fb(&panels[current_panel_idx], 32);
	vt8500_device_wm8505_fb.dev.platform_data = &panels[current_panel_idx];
#endif
}
