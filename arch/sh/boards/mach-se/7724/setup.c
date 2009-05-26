/*
 * linux/arch/sh/boards/se/7724/setup.c
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 *
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/delay.h>
#include <linux/smc91x.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <video/sh_mobile_lcdc.h>
#include <media/sh_mobile_ceu.h>
#include <asm/io.h>
#include <asm/heartbeat.h>
#include <asm/sh_keysc.h>
#include <cpu/sh7724.h>
#include <mach-se/mach/se7724.h>

/*
 * SWx    1234 5678
 * ------------------------------------
 * SW31 : 1001 1100    : default
 * SW32 : 0111 1111    : use on board flash
 *
 * SW41 : abxx xxxx  -> a = 0 : Analog  monitor
 *                          1 : Digital monitor
 *                      b = 0 : VGA
 *                          1 : SVGA
 */

/* Heartbeat */
static struct heartbeat_data heartbeat_data = {
	.regsize = 16,
};

static struct resource heartbeat_resources[] = {
	[0] = {
		.start  = PA_LED,
		.end    = PA_LED,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name           = "heartbeat",
	.id             = -1,
	.dev = {
		.platform_data = &heartbeat_data,
	},
	.num_resources  = ARRAY_SIZE(heartbeat_resources),
	.resource       = heartbeat_resources,
};

/* LAN91C111 */
static struct smc91x_platdata smc91x_info = {
	.flags = SMC91X_USE_16BIT | SMC91X_NOWAIT,
};

static struct resource smc91x_eth_resources[] = {
	[0] = {
		.name   = "SMC91C111" ,
		.start  = 0x1a300300,
		.end    = 0x1a30030f,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ0_SMC,
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

static struct platform_device smc91x_eth_device = {
	.name	= "smc91x",
	.num_resources  = ARRAY_SIZE(smc91x_eth_resources),
	.resource       = smc91x_eth_resources,
	.dev	= {
		.platform_data	= &smc91x_info,
	},
};

/* MTD */
static struct mtd_partition nor_flash_partitions[] = {
	{
		.name = "uboot",
		.offset = 0,
		.size = (1 * 1024 * 1024),
		.mask_flags = MTD_WRITEABLE,	/* Read-only */
	}, {
		.name = "kernel",
		.offset = MTDPART_OFS_APPEND,
		.size = (2 * 1024 * 1024),
	}, {
		.name = "free-area",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data nor_flash_data = {
	.width		= 2,
	.parts		= nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(nor_flash_partitions),
};

static struct resource nor_flash_resources[] = {
	[0] = {
		.name	= "NOR Flash",
		.start	= 0x00000000,
		.end	= 0x01ffffff,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device nor_flash_device = {
	.name		= "physmap-flash",
	.resource	= nor_flash_resources,
	.num_resources	= ARRAY_SIZE(nor_flash_resources),
	.dev		= {
		.platform_data = &nor_flash_data,
	},
};

/* LCDC */
static struct sh_mobile_lcdc_info lcdc_info = {
	.clock_source = LCDC_CLK_EXTERNAL,
	.ch[0] = {
		.chan = LCDC_CHAN_MAINLCD,
		.bpp = 16,
		.clock_divider = 1,
		.lcd_cfg = {
			.name = "LB070WV1",
			.sync = 0, /* hsync and vsync are active low */
		},
		.lcd_size_cfg = { /* 7.0 inch */
			.width = 152,
			.height = 91,
		},
		.board_cfg = {
		},
	}
};

static struct resource lcdc_resources[] = {
	[0] = {
		.name	= "LCDC",
		.start	= 0xfe940000,
		.end	= 0xfe941fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 106,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device lcdc_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(lcdc_resources),
	.resource	= lcdc_resources,
	.dev		= {
		.platform_data	= &lcdc_info,
	},
};

/* CEU0 */
static struct sh_mobile_ceu_info sh_mobile_ceu0_info = {
	.flags = SH_CEU_FLAG_USE_8BIT_BUS,
};

static struct resource ceu0_resources[] = {
	[0] = {
		.name	= "CEU0",
		.start	= 0xfe910000,
		.end	= 0xfe91009f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = 52,
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device ceu0_device = {
	.name		= "sh_mobile_ceu",
	.id             = 0, /* "ceu0" clock */
	.num_resources	= ARRAY_SIZE(ceu0_resources),
	.resource	= ceu0_resources,
	.dev	= {
		.platform_data	= &sh_mobile_ceu0_info,
	},
};

/* CEU1 */
static struct sh_mobile_ceu_info sh_mobile_ceu1_info = {
	.flags = SH_CEU_FLAG_USE_8BIT_BUS,
};

static struct resource ceu1_resources[] = {
	[0] = {
		.name	= "CEU1",
		.start	= 0xfe914000,
		.end	= 0xfe91409f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = 63,
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device ceu1_device = {
	.name		= "sh_mobile_ceu",
	.id             = 1, /* "ceu1" clock */
	.num_resources	= ARRAY_SIZE(ceu1_resources),
	.resource	= ceu1_resources,
	.dev	= {
		.platform_data	= &sh_mobile_ceu1_info,
	},
};

/* KEYSC */
static struct sh_keysc_info keysc_info = {
	.mode = SH_KEYSC_MODE_1,
	.scan_timing = 10,
	.delay = 50,
	.keycodes = {
		KEY_1, KEY_2, KEY_3, KEY_4, KEY_5,
		KEY_6, KEY_7, KEY_8, KEY_9, KEY_A,
		KEY_B, KEY_C, KEY_D, KEY_E, KEY_F,
		KEY_G, KEY_H, KEY_I, KEY_K, KEY_L,
		KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q,
		KEY_R, KEY_S, KEY_T, KEY_U, KEY_V,
	},
};

static struct resource keysc_resources[] = {
	[0] = {
		.start  = 0x1a204000,
		.end    = 0x1a20400f,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ0_KEY,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device keysc_device = {
	.name           = "sh_keysc",
	.id             = 0, /* "keysc0" clock */
	.num_resources  = ARRAY_SIZE(keysc_resources),
	.resource       = keysc_resources,
	.dev	= {
		.platform_data	= &keysc_info,
	},
};

static struct platform_device *ms7724se_devices[] __initdata = {
	&heartbeat_device,
	&smc91x_eth_device,
	&lcdc_device,
	&nor_flash_device,
	&ceu0_device,
	&ceu1_device,
	&keysc_device,
};

#define SW4140    0xBA201000
#define FPGA_OUT  0xBA200400
#define PORT_HIZA 0xA4050158

#define SW41_A    0x0100
#define SW41_B    0x0200
#define SW41_C    0x0400
#define SW41_D    0x0800
#define SW41_E    0x1000
#define SW41_F    0x2000
#define SW41_G    0x4000
#define SW41_H    0x8000
static int __init devices_setup(void)
{
	u16 sw = ctrl_inw(SW4140); /* select camera, monitor */

	/* Reset Release */
	ctrl_outw(ctrl_inw(FPGA_OUT) &
		  ~((1 << 1)  | /* LAN */
		    (1 << 6)  | /* VIDEO DAC */
		    (1 << 12)), /* USB0 */
		  FPGA_OUT);

	/* enable IRQ 0,1,2 */
	gpio_request(GPIO_FN_INTC_IRQ0, NULL);
	gpio_request(GPIO_FN_INTC_IRQ1, NULL);
	gpio_request(GPIO_FN_INTC_IRQ2, NULL);

	/* enable SCIFA3 */
	gpio_request(GPIO_FN_SCIF3_I_SCK, NULL);
	gpio_request(GPIO_FN_SCIF3_I_RXD, NULL);
	gpio_request(GPIO_FN_SCIF3_I_TXD, NULL);
	gpio_request(GPIO_FN_SCIF3_I_CTS, NULL);
	gpio_request(GPIO_FN_SCIF3_I_RTS, NULL);

	/* enable LCDC */
	gpio_request(GPIO_FN_LCDD23,   NULL);
	gpio_request(GPIO_FN_LCDD22,   NULL);
	gpio_request(GPIO_FN_LCDD21,   NULL);
	gpio_request(GPIO_FN_LCDD20,   NULL);
	gpio_request(GPIO_FN_LCDD19,   NULL);
	gpio_request(GPIO_FN_LCDD18,   NULL);
	gpio_request(GPIO_FN_LCDD17,   NULL);
	gpio_request(GPIO_FN_LCDD16,   NULL);
	gpio_request(GPIO_FN_LCDD15,   NULL);
	gpio_request(GPIO_FN_LCDD14,   NULL);
	gpio_request(GPIO_FN_LCDD13,   NULL);
	gpio_request(GPIO_FN_LCDD12,   NULL);
	gpio_request(GPIO_FN_LCDD11,   NULL);
	gpio_request(GPIO_FN_LCDD10,   NULL);
	gpio_request(GPIO_FN_LCDD9,    NULL);
	gpio_request(GPIO_FN_LCDD8,    NULL);
	gpio_request(GPIO_FN_LCDD7,    NULL);
	gpio_request(GPIO_FN_LCDD6,    NULL);
	gpio_request(GPIO_FN_LCDD5,    NULL);
	gpio_request(GPIO_FN_LCDD4,    NULL);
	gpio_request(GPIO_FN_LCDD3,    NULL);
	gpio_request(GPIO_FN_LCDD2,    NULL);
	gpio_request(GPIO_FN_LCDD1,    NULL);
	gpio_request(GPIO_FN_LCDD0,    NULL);
	gpio_request(GPIO_FN_LCDDISP,  NULL);
	gpio_request(GPIO_FN_LCDHSYN,  NULL);
	gpio_request(GPIO_FN_LCDDCK,   NULL);
	gpio_request(GPIO_FN_LCDVSYN,  NULL);
	gpio_request(GPIO_FN_LCDDON,   NULL);
	gpio_request(GPIO_FN_LCDVEPWC, NULL);
	gpio_request(GPIO_FN_LCDVCPWC, NULL);
	gpio_request(GPIO_FN_LCDRD,    NULL);
	gpio_request(GPIO_FN_LCDLCLK,  NULL);
	ctrl_outw((ctrl_inw(PORT_HIZA) & ~0x0001), PORT_HIZA);

	/* enable CEU0 */
	gpio_request(GPIO_FN_VIO0_D15, NULL);
	gpio_request(GPIO_FN_VIO0_D14, NULL);
	gpio_request(GPIO_FN_VIO0_D13, NULL);
	gpio_request(GPIO_FN_VIO0_D12, NULL);
	gpio_request(GPIO_FN_VIO0_D11, NULL);
	gpio_request(GPIO_FN_VIO0_D10, NULL);
	gpio_request(GPIO_FN_VIO0_D9,  NULL);
	gpio_request(GPIO_FN_VIO0_D8,  NULL);
	gpio_request(GPIO_FN_VIO0_D7,  NULL);
	gpio_request(GPIO_FN_VIO0_D6,  NULL);
	gpio_request(GPIO_FN_VIO0_D5,  NULL);
	gpio_request(GPIO_FN_VIO0_D4,  NULL);
	gpio_request(GPIO_FN_VIO0_D3,  NULL);
	gpio_request(GPIO_FN_VIO0_D2,  NULL);
	gpio_request(GPIO_FN_VIO0_D1,  NULL);
	gpio_request(GPIO_FN_VIO0_D0,  NULL);
	gpio_request(GPIO_FN_VIO0_VD,  NULL);
	gpio_request(GPIO_FN_VIO0_CLK, NULL);
	gpio_request(GPIO_FN_VIO0_FLD, NULL);
	gpio_request(GPIO_FN_VIO0_HD,  NULL);
	platform_resource_setup_memory(&ceu0_device, "ceu", 4 << 20);

	/* enable CEU1 */
	gpio_request(GPIO_FN_VIO1_D7,  NULL);
	gpio_request(GPIO_FN_VIO1_D6,  NULL);
	gpio_request(GPIO_FN_VIO1_D5,  NULL);
	gpio_request(GPIO_FN_VIO1_D4,  NULL);
	gpio_request(GPIO_FN_VIO1_D3,  NULL);
	gpio_request(GPIO_FN_VIO1_D2,  NULL);
	gpio_request(GPIO_FN_VIO1_D1,  NULL);
	gpio_request(GPIO_FN_VIO1_D0,  NULL);
	gpio_request(GPIO_FN_VIO1_FLD, NULL);
	gpio_request(GPIO_FN_VIO1_HD,  NULL);
	gpio_request(GPIO_FN_VIO1_VD,  NULL);
	gpio_request(GPIO_FN_VIO1_CLK, NULL);
	platform_resource_setup_memory(&ceu1_device, "ceu", 4 << 20);

	/* KEYSC */
	gpio_request(GPIO_FN_KEYOUT5_IN5, NULL);
	gpio_request(GPIO_FN_KEYOUT4_IN6, NULL);
	gpio_request(GPIO_FN_KEYIN4,      NULL);
	gpio_request(GPIO_FN_KEYIN3,      NULL);
	gpio_request(GPIO_FN_KEYIN2,      NULL);
	gpio_request(GPIO_FN_KEYIN1,      NULL);
	gpio_request(GPIO_FN_KEYIN0,      NULL);
	gpio_request(GPIO_FN_KEYOUT3,     NULL);
	gpio_request(GPIO_FN_KEYOUT2,     NULL);
	gpio_request(GPIO_FN_KEYOUT1,     NULL);
	gpio_request(GPIO_FN_KEYOUT0,     NULL);

	if (sw & SW41_B) {
		/* SVGA */
		lcdc_info.ch[0].lcd_cfg.xres         = 800;
		lcdc_info.ch[0].lcd_cfg.yres         = 600;
		lcdc_info.ch[0].lcd_cfg.left_margin  = 142;
		lcdc_info.ch[0].lcd_cfg.right_margin = 52;
		lcdc_info.ch[0].lcd_cfg.hsync_len    = 96;
		lcdc_info.ch[0].lcd_cfg.upper_margin = 24;
		lcdc_info.ch[0].lcd_cfg.lower_margin = 2;
		lcdc_info.ch[0].lcd_cfg.vsync_len    = 2;
	} else {
		/* VGA */
		lcdc_info.ch[0].lcd_cfg.xres         = 640;
		lcdc_info.ch[0].lcd_cfg.yres         = 480;
		lcdc_info.ch[0].lcd_cfg.left_margin  = 105;
		lcdc_info.ch[0].lcd_cfg.right_margin = 50;
		lcdc_info.ch[0].lcd_cfg.hsync_len    = 96;
		lcdc_info.ch[0].lcd_cfg.upper_margin = 33;
		lcdc_info.ch[0].lcd_cfg.lower_margin = 10;
		lcdc_info.ch[0].lcd_cfg.vsync_len    = 2;
	}

	if (sw & SW41_A) {
		/* Digital monitor */
		lcdc_info.ch[0].interface_type = RGB18;
		lcdc_info.ch[0].flags          = 0;
	} else {
		/* Analog monitor */
		lcdc_info.ch[0].interface_type = RGB24;
		lcdc_info.ch[0].flags          = LCDC_FLAGS_DWPOL;
	}

	return platform_add_devices(ms7724se_devices,
				ARRAY_SIZE(ms7724se_devices));
}
device_initcall(devices_setup);

static struct sh_machine_vector mv_ms7724se __initmv = {
	.mv_name	= "ms7724se",
	.mv_init_irq	= init_se7724_IRQ,
	.mv_nr_irqs	= SE7724_FPGA_IRQ_BASE + SE7724_FPGA_IRQ_NR,
};
