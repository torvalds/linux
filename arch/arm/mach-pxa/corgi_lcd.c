/*
 * linux/drivers/video/w100fb.c
 *
 * Corgi/Spitz LCD Specific Code
 *
 * Copyright (C) 2005 Richard Purdie
 *
 * Connectivity:
 *   Corgi - LCD to ATI Imageon w100 (Wallaby)
 *   Spitz - LCD to PXA Framebuffer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <asm/arch/akita.h>
#include <asm/arch/corgi.h>
#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/sharpsl.h>
#include <asm/arch/spitz.h>
#include <asm/hardware/scoop.h>
#include <asm/mach/sharpsl_param.h>
#include "generic.h"

/* Register Addresses */
#define RESCTL_ADRS     0x00
#define PHACTRL_ADRS    0x01
#define DUTYCTRL_ADRS   0x02
#define POWERREG0_ADRS  0x03
#define POWERREG1_ADRS  0x04
#define GPOR3_ADRS      0x05
#define PICTRL_ADRS     0x06
#define POLCTRL_ADRS    0x07

/* Resgister Bit Definitions */
#define RESCTL_QVGA     0x01
#define RESCTL_VGA      0x00

#define POWER1_VW_ON    0x01  /* VW Supply FET ON */
#define POWER1_GVSS_ON  0x02  /* GVSS(-8V) Power Supply ON */
#define POWER1_VDD_ON   0x04  /* VDD(8V),SVSS(-4V) Power Supply ON */

#define POWER1_VW_OFF   0x00  /* VW Supply FET OFF */
#define POWER1_GVSS_OFF 0x00  /* GVSS(-8V) Power Supply OFF */
#define POWER1_VDD_OFF  0x00  /* VDD(8V),SVSS(-4V) Power Supply OFF */

#define POWER0_COM_DCLK 0x01  /* COM Voltage DC Bias DAC Serial Data Clock */
#define POWER0_COM_DOUT 0x02  /* COM Voltage DC Bias DAC Serial Data Out */
#define POWER0_DAC_ON   0x04  /* DAC Power Supply ON */
#define POWER0_COM_ON   0x08  /* COM Powewr Supply ON */
#define POWER0_VCC5_ON  0x10  /* VCC5 Power Supply ON */

#define POWER0_DAC_OFF  0x00  /* DAC Power Supply OFF */
#define POWER0_COM_OFF  0x00  /* COM Powewr Supply OFF */
#define POWER0_VCC5_OFF 0x00  /* VCC5 Power Supply OFF */

#define PICTRL_INIT_STATE      0x01
#define PICTRL_INIOFF          0x02
#define PICTRL_POWER_DOWN      0x04
#define PICTRL_COM_SIGNAL_OFF  0x08
#define PICTRL_DAC_SIGNAL_OFF  0x10

#define POLCTRL_SYNC_POL_FALL  0x01
#define POLCTRL_EN_POL_FALL    0x02
#define POLCTRL_DATA_POL_FALL  0x04
#define POLCTRL_SYNC_ACT_H     0x08
#define POLCTRL_EN_ACT_L       0x10

#define POLCTRL_SYNC_POL_RISE  0x00
#define POLCTRL_EN_POL_RISE    0x00
#define POLCTRL_DATA_POL_RISE  0x00
#define POLCTRL_SYNC_ACT_L     0x00
#define POLCTRL_EN_ACT_H       0x00

#define PHACTRL_PHASE_MANUAL   0x01
#define DEFAULT_PHAD_QVGA     (9)
#define DEFAULT_COMADJ        (125)

/*
 * This is only a psuedo I2C interface. We can't use the standard kernel
 * routines as the interface is write only. We just assume the data is acked...
 */
static void lcdtg_ssp_i2c_send(u8 data)
{
	corgi_ssp_lcdtg_send(POWERREG0_ADRS, data);
	udelay(10);
}

static void lcdtg_i2c_send_bit(u8 data)
{
	lcdtg_ssp_i2c_send(data);
	lcdtg_ssp_i2c_send(data | POWER0_COM_DCLK);
	lcdtg_ssp_i2c_send(data);
}

static void lcdtg_i2c_send_start(u8 base)
{
	lcdtg_ssp_i2c_send(base | POWER0_COM_DCLK | POWER0_COM_DOUT);
	lcdtg_ssp_i2c_send(base | POWER0_COM_DCLK);
	lcdtg_ssp_i2c_send(base);
}

static void lcdtg_i2c_send_stop(u8 base)
{
	lcdtg_ssp_i2c_send(base);
	lcdtg_ssp_i2c_send(base | POWER0_COM_DCLK);
	lcdtg_ssp_i2c_send(base | POWER0_COM_DCLK | POWER0_COM_DOUT);
}

static void lcdtg_i2c_send_byte(u8 base, u8 data)
{
	int i;
	for (i = 0; i < 8; i++) {
		if (data & 0x80)
			lcdtg_i2c_send_bit(base | POWER0_COM_DOUT);
		else
			lcdtg_i2c_send_bit(base);
		data <<= 1;
	}
}

static void lcdtg_i2c_wait_ack(u8 base)
{
	lcdtg_i2c_send_bit(base);
}

static void lcdtg_set_common_voltage(u8 base_data, u8 data)
{
	/* Set Common Voltage to M62332FP via I2C */
	lcdtg_i2c_send_start(base_data);
	lcdtg_i2c_send_byte(base_data, 0x9c);
	lcdtg_i2c_wait_ack(base_data);
	lcdtg_i2c_send_byte(base_data, 0x00);
	lcdtg_i2c_wait_ack(base_data);
	lcdtg_i2c_send_byte(base_data, data);
	lcdtg_i2c_wait_ack(base_data);
	lcdtg_i2c_send_stop(base_data);
}

/* Set Phase Adjuct */
static void lcdtg_set_phadadj(int mode)
{
	int adj;
	switch(mode) {
		case 480:
		case 640:
			/* Setting for VGA */
			adj = sharpsl_param.phadadj;
			if (adj < 0) {
				adj = PHACTRL_PHASE_MANUAL;
			} else {
				adj = ((adj & 0x0f) << 1) | PHACTRL_PHASE_MANUAL;
			}
			break;
		case 240:
		case 320:
		default:
			/* Setting for QVGA */
			adj = (DEFAULT_PHAD_QVGA << 1) | PHACTRL_PHASE_MANUAL;
			break;
	}

	corgi_ssp_lcdtg_send(PHACTRL_ADRS, adj);
}

static int lcd_inited;

static void lcdtg_hw_init(int mode)
{
	if (!lcd_inited) {
		int comadj;

		/* Initialize Internal Logic & Port */
		corgi_ssp_lcdtg_send(PICTRL_ADRS, PICTRL_POWER_DOWN | PICTRL_INIOFF | PICTRL_INIT_STATE
	  			| PICTRL_COM_SIGNAL_OFF | PICTRL_DAC_SIGNAL_OFF);

		corgi_ssp_lcdtg_send(POWERREG0_ADRS, POWER0_COM_DCLK | POWER0_COM_DOUT | POWER0_DAC_OFF
				| POWER0_COM_OFF | POWER0_VCC5_OFF);

		corgi_ssp_lcdtg_send(POWERREG1_ADRS, POWER1_VW_OFF | POWER1_GVSS_OFF | POWER1_VDD_OFF);

		/* VDD(+8V), SVSS(-4V) ON */
		corgi_ssp_lcdtg_send(POWERREG1_ADRS, POWER1_VW_OFF | POWER1_GVSS_OFF | POWER1_VDD_ON);
		mdelay(3);

		/* DAC ON */
		corgi_ssp_lcdtg_send(POWERREG0_ADRS, POWER0_COM_DCLK | POWER0_COM_DOUT | POWER0_DAC_ON
				| POWER0_COM_OFF | POWER0_VCC5_OFF);

		/* INIB = H, INI = L  */
		/* PICTL[0] = H , PICTL[1] = PICTL[2] = PICTL[4] = L */
		corgi_ssp_lcdtg_send(PICTRL_ADRS, PICTRL_INIT_STATE | PICTRL_COM_SIGNAL_OFF);

		/* Set Common Voltage */
		comadj = sharpsl_param.comadj;
		if (comadj < 0)
			comadj = DEFAULT_COMADJ;
		lcdtg_set_common_voltage((POWER0_DAC_ON | POWER0_COM_OFF | POWER0_VCC5_OFF), comadj);

		/* VCC5 ON, DAC ON */
		corgi_ssp_lcdtg_send(POWERREG0_ADRS, POWER0_COM_DCLK | POWER0_COM_DOUT | POWER0_DAC_ON |
				POWER0_COM_OFF | POWER0_VCC5_ON);

		/* GVSS(-8V) ON, VDD ON */
		corgi_ssp_lcdtg_send(POWERREG1_ADRS, POWER1_VW_OFF | POWER1_GVSS_ON | POWER1_VDD_ON);
		mdelay(2);

		/* COM SIGNAL ON (PICTL[3] = L) */
		corgi_ssp_lcdtg_send(PICTRL_ADRS, PICTRL_INIT_STATE);

		/* COM ON, DAC ON, VCC5_ON */
		corgi_ssp_lcdtg_send(POWERREG0_ADRS, POWER0_COM_DCLK | POWER0_COM_DOUT | POWER0_DAC_ON
				| POWER0_COM_ON | POWER0_VCC5_ON);

		/* VW ON, GVSS ON, VDD ON */
		corgi_ssp_lcdtg_send(POWERREG1_ADRS, POWER1_VW_ON | POWER1_GVSS_ON | POWER1_VDD_ON);

		/* Signals output enable */
		corgi_ssp_lcdtg_send(PICTRL_ADRS, 0);

		/* Set Phase Adjuct */
		lcdtg_set_phadadj(mode);

		/* Initialize for Input Signals from ATI */
		corgi_ssp_lcdtg_send(POLCTRL_ADRS, POLCTRL_SYNC_POL_RISE | POLCTRL_EN_POL_RISE
				| POLCTRL_DATA_POL_RISE | POLCTRL_SYNC_ACT_L | POLCTRL_EN_ACT_H);
		udelay(1000);

		lcd_inited=1;
	} else {
		lcdtg_set_phadadj(mode);
	}

	switch(mode) {
		case 480:
		case 640:
			/* Set Lcd Resolution (VGA) */
			corgi_ssp_lcdtg_send(RESCTL_ADRS, RESCTL_VGA);
			break;
		case 240:
		case 320:
		default:
			/* Set Lcd Resolution (QVGA) */
			corgi_ssp_lcdtg_send(RESCTL_ADRS, RESCTL_QVGA);
			break;
	}
}

static void lcdtg_suspend(void)
{
	/* 60Hz x 2 frame = 16.7msec x 2 = 33.4 msec */
	mdelay(34);

	/* (1)VW OFF */
	corgi_ssp_lcdtg_send(POWERREG1_ADRS, POWER1_VW_OFF | POWER1_GVSS_ON | POWER1_VDD_ON);

	/* (2)COM OFF */
	corgi_ssp_lcdtg_send(PICTRL_ADRS, PICTRL_COM_SIGNAL_OFF);
	corgi_ssp_lcdtg_send(POWERREG0_ADRS, POWER0_DAC_ON | POWER0_COM_OFF | POWER0_VCC5_ON);

	/* (3)Set Common Voltage Bias 0V */
	lcdtg_set_common_voltage(POWER0_DAC_ON | POWER0_COM_OFF | POWER0_VCC5_ON, 0);

	/* (4)GVSS OFF */
	corgi_ssp_lcdtg_send(POWERREG1_ADRS, POWER1_VW_OFF | POWER1_GVSS_OFF | POWER1_VDD_ON);

	/* (5)VCC5 OFF */
	corgi_ssp_lcdtg_send(POWERREG0_ADRS, POWER0_DAC_ON | POWER0_COM_OFF | POWER0_VCC5_OFF);

	/* (6)Set PDWN, INIOFF, DACOFF */
	corgi_ssp_lcdtg_send(PICTRL_ADRS, PICTRL_INIOFF | PICTRL_DAC_SIGNAL_OFF |
			PICTRL_POWER_DOWN | PICTRL_COM_SIGNAL_OFF);

	/* (7)DAC OFF */
	corgi_ssp_lcdtg_send(POWERREG0_ADRS, POWER0_DAC_OFF | POWER0_COM_OFF | POWER0_VCC5_OFF);

	/* (8)VDD OFF */
	corgi_ssp_lcdtg_send(POWERREG1_ADRS, POWER1_VW_OFF | POWER1_GVSS_OFF | POWER1_VDD_OFF);

	lcd_inited = 0;
}


/*
 * Corgi w100 Frame Buffer Device
 */
#ifdef CONFIG_PXA_SHARP_C7xx

#include <video/w100fb.h>

static void w100_lcdtg_suspend(struct w100fb_par *par)
{
	lcdtg_suspend();
}

static void w100_lcdtg_init(struct w100fb_par *par)
{
	lcdtg_hw_init(par->xres);
}


static struct w100_tg_info corgi_lcdtg_info = {
	.change  = w100_lcdtg_init,
	.suspend = w100_lcdtg_suspend,
	.resume  = w100_lcdtg_init,
};

static struct w100_mem_info corgi_fb_mem = {
	.ext_cntl          = 0x00040003,
	.sdram_mode_reg    = 0x00650021,
	.ext_timing_cntl   = 0x10002a4a,
	.io_cntl           = 0x7ff87012,
	.size              = 0x1fffff,
};

static struct w100_gen_regs corgi_fb_regs = {
	.lcd_format    = 0x00000003,
	.lcdd_cntl1    = 0x01CC0000,
	.lcdd_cntl2    = 0x0003FFFF,
	.genlcd_cntl1  = 0x00FFFF0D,
	.genlcd_cntl2  = 0x003F3003,
	.genlcd_cntl3  = 0x000102aa,
};

static struct w100_gpio_regs corgi_fb_gpio = {
	.init_data1   = 0x000000bf,
	.init_data2   = 0x00000000,
	.gpio_dir1    = 0x00000000,
	.gpio_oe1     = 0x03c0feff,
	.gpio_dir2    = 0x00000000,
	.gpio_oe2     = 0x00000000,
};

static struct w100_mode corgi_fb_modes[] = {
{
	.xres            = 480,
	.yres            = 640,
	.left_margin     = 0x56,
	.right_margin    = 0x55,
	.upper_margin    = 0x03,
	.lower_margin    = 0x00,
	.crtc_ss         = 0x82360056,
	.crtc_ls         = 0xA0280000,
	.crtc_gs         = 0x80280028,
	.crtc_vpos_gs    = 0x02830002,
	.crtc_rev        = 0x00400008,
	.crtc_dclk       = 0xA0000000,
	.crtc_gclk       = 0x8015010F,
	.crtc_goe        = 0x80100110,
	.crtc_ps1_active = 0x41060010,
	.pll_freq        = 75,
	.fast_pll_freq   = 100,
	.sysclk_src      = CLK_SRC_PLL,
	.sysclk_divider  = 0,
	.pixclk_src      = CLK_SRC_PLL,
	.pixclk_divider  = 2,
	.pixclk_divider_rotated = 6,
},{
	.xres            = 240,
	.yres            = 320,
	.left_margin     = 0x27,
	.right_margin    = 0x2e,
	.upper_margin    = 0x01,
	.lower_margin    = 0x00,
	.crtc_ss         = 0x81170027,
	.crtc_ls         = 0xA0140000,
	.crtc_gs         = 0xC0140014,
	.crtc_vpos_gs    = 0x00010141,
	.crtc_rev        = 0x00400008,
	.crtc_dclk       = 0xA0000000,
	.crtc_gclk       = 0x8015010F,
	.crtc_goe        = 0x80100110,
	.crtc_ps1_active = 0x41060010,
	.pll_freq        = 0,
	.fast_pll_freq   = 0,
	.sysclk_src      = CLK_SRC_XTAL,
	.sysclk_divider  = 0,
	.pixclk_src      = CLK_SRC_XTAL,
	.pixclk_divider  = 1,
	.pixclk_divider_rotated = 1,
},

};

static struct w100fb_mach_info corgi_fb_info = {
	.tg         = &corgi_lcdtg_info,
	.init_mode  = INIT_MODE_ROTATED,
	.mem        = &corgi_fb_mem,
	.regs       = &corgi_fb_regs,
	.modelist   = &corgi_fb_modes[0],
	.num_modes  = 2,
	.gpio       = &corgi_fb_gpio,
	.xtal_freq  = 12500000,
	.xtal_dbl   = 0,
};

static struct resource corgi_fb_resources[] = {
	[0] = {
		.start   = 0x08000000,
		.end     = 0x08ffffff,
		.flags   = IORESOURCE_MEM,
	},
};

struct platform_device corgifb_device = {
	.name           = "w100fb",
	.id             = -1,
	.num_resources	= ARRAY_SIZE(corgi_fb_resources),
	.resource	= corgi_fb_resources,
	.dev            = {
 		.platform_data = &corgi_fb_info,
 		.parent = &corgissp_device.dev,
	},

};
#endif


/*
 * Spitz PXA Frame Buffer Device
 */
#ifdef CONFIG_PXA_SHARP_Cxx00

#include <asm/arch/pxafb.h>

void spitz_lcd_power(int on)
{
	if (on)
		lcdtg_hw_init(480);
	else
		lcdtg_suspend();
}

#endif


/*
 * Corgi/Spitz Touchscreen to LCD interface
 */
static unsigned long (*get_hsync_time)(struct device *dev);

static void inline sharpsl_wait_sync(int gpio)
{
	while((GPLR(gpio) & GPIO_bit(gpio)) == 0);
	while((GPLR(gpio) & GPIO_bit(gpio)) != 0);
}

#ifdef CONFIG_PXA_SHARP_C7xx
unsigned long corgi_get_hsync_len(void)
{
	if (!get_hsync_time)
		get_hsync_time = symbol_get(w100fb_get_hsynclen);
	if (!get_hsync_time)
		return 0;

	return get_hsync_time(&corgifb_device.dev);
}

void corgi_put_hsync(void)
{
	if (get_hsync_time)
		symbol_put(w100fb_get_hsynclen);
}

void corgi_wait_hsync(void)
{
	sharpsl_wait_sync(CORGI_GPIO_HSYNC);
}
#endif

#ifdef CONFIG_PXA_SHARP_Cxx00
unsigned long spitz_get_hsync_len(void)
{
	if (!get_hsync_time)
		get_hsync_time = symbol_get(pxafb_get_hsync_time);
	if (!get_hsync_time)
		return 0;

	return pxafb_get_hsync_time(&pxafb_device.dev);
}

void spitz_put_hsync(void)
{
	if (get_hsync_time)
		symbol_put(pxafb_get_hsync_time);
}

void spitz_wait_hsync(void)
{
	sharpsl_wait_sync(SPITZ_GPIO_HSYNC);
}
#endif

/*
 * Corgi/Spitz Backlight Power
 */
#ifdef CONFIG_PXA_SHARP_C7xx
void corgi_bl_set_intensity(int intensity)
{
	if (intensity > 0x10)
		intensity += 0x10;

	/* Bits 0-4 are accessed via the SSP interface */
	corgi_ssp_blduty_set(intensity & 0x1f);

	/* Bit 5 is via SCOOP */
	if (intensity & 0x0020)
		set_scoop_gpio(&corgiscoop_device.dev, CORGI_SCP_BACKLIGHT_CONT);
	else
		reset_scoop_gpio(&corgiscoop_device.dev, CORGI_SCP_BACKLIGHT_CONT);
}
#endif


#if defined(CONFIG_MACH_SPITZ) || defined(CONFIG_MACH_BORZOI)
void spitz_bl_set_intensity(int intensity)
{
	if (intensity > 0x10)
		intensity += 0x10;

	/* Bits 0-4 are accessed via the SSP interface */
	corgi_ssp_blduty_set(intensity & 0x1f);

	/* Bit 5 is via SCOOP */
	if (intensity & 0x0020)
		reset_scoop_gpio(&spitzscoop2_device.dev, SPITZ_SCP2_BACKLIGHT_CONT);
	else
		set_scoop_gpio(&spitzscoop2_device.dev, SPITZ_SCP2_BACKLIGHT_CONT);

	if (intensity)
		set_scoop_gpio(&spitzscoop2_device.dev, SPITZ_SCP2_BACKLIGHT_ON);
	else
		reset_scoop_gpio(&spitzscoop2_device.dev, SPITZ_SCP2_BACKLIGHT_ON);
}
#endif

#ifdef CONFIG_MACH_AKITA
void akita_bl_set_intensity(int intensity)
{
	if (intensity > 0x10)
		intensity += 0x10;

	/* Bits 0-4 are accessed via the SSP interface */
	corgi_ssp_blduty_set(intensity & 0x1f);

	/* Bit 5 is via IO-Expander */
	if (intensity & 0x0020)
		akita_reset_ioexp(&akitaioexp_device.dev, AKITA_IOEXP_BACKLIGHT_CONT);
	else
		akita_set_ioexp(&akitaioexp_device.dev, AKITA_IOEXP_BACKLIGHT_CONT);

	if (intensity)
		akita_set_ioexp(&akitaioexp_device.dev, AKITA_IOEXP_BACKLIGHT_ON);
	else
		akita_reset_ioexp(&akitaioexp_device.dev, AKITA_IOEXP_BACKLIGHT_ON);
}
#endif
