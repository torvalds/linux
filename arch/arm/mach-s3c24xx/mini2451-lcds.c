/*
 * linux/arch/arm/mach-s3c24xx/mini2451-lcds.c
 *
 * Copyright (c) 2012 FriendlyARM (www.arm9.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#include <plat/ctouch.h>
#include <mach/s3cfb.h>


/* s3cfb configs for supported LCD */

static struct s3cfb_lcd wvga_w50 = {
	.width= 800,
	.height = 480,
	.p_width = 108,
	.p_height = 64,
	.bpp = 16,
	.freq = 65,

	.timing = {
		.h_fp = 40,
		.h_bp = 40,
		.h_sw = 48,
		.v_fp = 20,
		.v_fpe = 1,
		.v_bp = 20,
		.v_bpe = 1,
		.v_sw = 12,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_a70 = {
	.width = 800,
	.height = 480,
	.p_width = 152,
	.p_height = 90,
	.bpp = 16,
	.freq = 40,

	.timing = {
		.h_fp = 40,
		.h_bp = 40,
		.h_sw = 48,
		.v_fp = 17,
		.v_fpe = 1,
		.v_bp = 29,
		.v_bpe = 1,
		.v_sw = 24,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_s70 = {
	.width = 800,
	.height = 480,
	.p_width = 154,
	.p_height = 96,
	.bpp = 16,
	.freq = 65,

	.timing = {
		.h_fp = 80,
		.h_bp = 36,
		.h_sw = 10,
		.v_fp = 22,
		.v_fpe = 1,
		.v_bp = 15,
		.v_bpe = 1,
		.v_sw = 8,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_h43 = {
	.width = 480,
	.height = 272,
	.p_width = 96,
	.p_height = 54,
	.bpp = 16,
	.freq = 65,

	.timing = {
		.h_fp =  5,
		.h_bp = 40,
		.h_sw =  2,
		.v_fp =  8,
		.v_fpe = 1,
		.v_bp =  8,
		.v_bpe = 1,
		.v_sw =  2,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_a97 = {
	.width = 1024,
	.height = 768,
	.p_width = 200,
	.p_height = 150,
	.bpp = 16,
	.freq = 53,

	.timing = {
		.h_fp = 12,
		.h_bp = 12,
		.h_sw = 4,
		.v_fp = 8,
		.v_fpe = 1,
		.v_bp = 8,
		.v_bpe = 1,
		.v_sw =  4,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

/* VGAs */
static struct s3cfb_lcd wvga_l80 = {
	.width= 640,
	.height = 480,
	.p_width = 160,
	.p_height = 120,
	.bpp = 16,
	.freq = 65,

	.timing = {
		.h_fp = 35,
		.h_bp = 53,
		.h_sw = 73,
		.v_fp = 3,
		.v_fpe = 1,
		.v_bp = 29,
		.v_bpe = 1,
		.v_sw = 6,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_g10 = {
	.width= 640,
	.height = 480,
	.p_width = 213,
	.p_height = 160,
	.bpp = 16,
	.freq = 65,

	.timing = {
		.h_fp = 0x3c,
		.h_bp = 0x63,
		.h_sw = 1,
		.v_fp = 0x0a,
		.v_fpe = 1,
		.v_bp = 0x22,
		.v_bpe = 1,
		.v_sw = 1,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 0,
		.inv_vsync = 0,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_a56 = {
	.width= 640,
	.height = 480,
	.p_width = 112,
	.p_height = 84,
	.bpp = 16,
	.freq = 65,

	.timing = {
		.h_fp = 16,
		.h_bp = 134,
		.h_sw = 10,
		.v_fp = 32,
		.v_fpe = 1,
		.v_bp = 11,
		.v_bpe = 1,
		.v_sw = 2,
	},
	.polarity = {
		.rise_vclk = 1,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_w101 = {
	.width= 1024,
	.height = 600,
	.p_width = 204,
	.p_height = 120,
	.bpp = 16,
	.freq = 59,

	.timing = {
		.h_fp = 40,
		.h_bp = 40,
		.h_sw = 112,
		.v_fp =  4,
		.v_fpe = 1,
		.v_bp =  4,
		.v_bpe = 1,
		.v_sw =  8,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_w35 = {
	.width= 320,
	.height = 240,
	.p_width = 70,
	.p_height = 52,
	.bpp = 16,
	.freq = 65,

	.timing = {
		.h_fp =  4,
		.h_bp = 70,
		.h_sw =  4,
		.v_fp =  4,
		.v_fpe = 1,
		.v_bp = 12,
		.v_bpe = 1,
		.v_sw =  4,
	},
	.polarity = {
		.rise_vclk = 1,
		.inv_hsync = 0,
		.inv_vsync = 0,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_p43 = {
	.width = 480,
	.height = 272,
	.p_width = 96,
	.p_height = 54,
	.bpp = 16,
	.freq = 65,

	.timing = {
		.h_fp =  5,
		.h_bp = 40,
		.h_sw =  2,
		.v_fp =  8,
		.v_fpe = 1,
		.v_bp =  9,
		.v_bpe = 1,
		.v_sw =  2,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_p35 = {
	.width= 320,
	.height = 240,
	.p_width = 70,
	.p_height = 52,
	.bpp = 16,
	.freq = 65,

	.timing = {
		.h_fp =  4,
		.h_bp =  4,
		.h_sw =  4,
		.v_fp =  4,
		.v_fpe = 1,
		.v_bp =  9,
		.v_bpe = 1,
		.v_sw =  4,
	},
	.polarity = {
		.rise_vclk = 1,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_td35 = {
	.width= 240,
	.height = 320,
	.p_width = 70,
	.p_height = 52,
	.bpp = 16,
	.freq = 50,

	.timing = {
		.h_fp =  101,
		.h_bp =  1,
		.h_sw =  3,
		.v_fp =  1,
		.v_fpe = 1,
		.v_bp =  1,
		.v_bpe = 1,
		.v_sw =  10,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_vga1024x768 = {
	.width = 1024,
	.height = 768,
	.p_width = 96,
	.p_height = 96,
	.bpp = 16,
	.freq = 30,
	.timing = {
		.h_fp = 2,
		.h_bp = 2,
		.h_sw = 0x2a,
		.v_fp = 2,
		.v_fpe = 1,
		.v_bp = 2,
		.v_bpe = 1,
		.v_sw = 0x10,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 0,
		.inv_vsync = 0,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_vga800x600 = {
	.width = 800,
	.height = 600,
	.p_width = 160,
	.p_height = 120,
	.bpp = 32,
	.freq = 30,

	.timing = {
		.h_fp = 2,
		.h_bp = 2,
		.h_sw = 0x2a,
		.v_fp = 2,
		.v_fpe = 1,
		.v_bp = 2,
		.v_bpe = 1,
		.v_sw = 0x10,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 0,
		.inv_vsync = 0,
		.inv_vden = 0,
	},
};

static struct s3cfb_lcd wvga_vga640x480 = {
	.width = 640,
	.height = 480,
	.p_width = 160,
	.p_height = 120,
	.bpp = 32,
	.freq = 30,

	.timing = {
		.h_fp = 2,
		.h_bp = 2,
		.h_sw = 0x2a,
		.v_fp = 2,
		.v_fpe = 1,
		.v_bp = 2,
		.v_bpe = 1,
		.v_sw = 0x10,
	},
	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 0,
		.inv_vsync = 0,
		.inv_vden = 0,
	},
};

/* Try to guess LCD panel by kernel command line, or
 * using *H43* as default */

static struct {
	char *name;
	struct s3cfb_lcd *lcd;
	int ctp;
} mini2451_lcd_config[] = {
	{ "H43",  &wvga_h43,  1 },
	{ "S70",  &wvga_s70,  1 },
	{ "A70",  &wvga_a70,  0 },
	{ "W50",  &wvga_w50,  0 },
	{ "A97",  &wvga_a97,  0 },
	{ "L80",  &wvga_l80,  0 },
	{ "G10",  &wvga_g10,  0 },
	{ "A56",  &wvga_a56,  0 },
	{ "W101", &wvga_w101, 0 },
	{ "W35",  &wvga_w35,  0 },
	{ "P43",  &wvga_p43,  1 },
	{ "P35",  &wvga_p35,  1 },
	{ "TD35", &wvga_td35, 1 },
	{ "VGA1024X768",  &wvga_vga1024x768,  0 },
	{ "VGA800X600",   &wvga_vga800x600,   0 },
	{ "VGA640X480",   &wvga_vga640x480,   0 },
};

static int lcd_idx = 0;

static int __init mini2451_setup_lcd(char *str)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mini2451_lcd_config); i++) {
		if (!strcasecmp(mini2451_lcd_config[i].name, str)) {
			lcd_idx = i;
			break;
		}
	}

	printk("MINI2451: %s selected\n", mini2451_lcd_config[lcd_idx].name);
	return 0;
}
early_param("lcd", mini2451_setup_lcd);


struct s3cfb_lcd *mini2451_get_lcd(void)
{
	return mini2451_lcd_config[lcd_idx].lcd;
}

void mini2451_get_lcd_res(int *w, int *h)
{
	struct s3cfb_lcd *lcd = mini2451_lcd_config[lcd_idx].lcd;

	if (w)
		*w = lcd->width;
	if (h)
		*h = lcd->height;

	return;
}
EXPORT_SYMBOL(mini2451_get_lcd_res);


#if defined(CONFIG_TOUCHSCREEN_GOODIX) || defined(CONFIG_TOUCHSCREEN_FT5X0X) || \
	defined(CONFIG_TOUCHSCREEN_1WIRE)
static unsigned int ctp_type = CTP_NONE;

static int __init mini2451_set_ctp(char *str)
{
	unsigned int val;
	char *p = str, *end;

	val = simple_strtoul(p, &end, 10);
	if (end <= p) {
		return 1;
	}

	if (val < CTP_MAX && mini2451_lcd_config[lcd_idx].ctp) {
		ctp_type = val;
	} else if (val == CTP_NONE) {
		ctp_type = CTP_NONE;
	}

	return 1;
}
__setup("ctp=", mini2451_set_ctp);

unsigned int mini2451_get_ctp(void)
{
	return ctp_type;
}
EXPORT_SYMBOL(mini2451_get_ctp);
#endif

