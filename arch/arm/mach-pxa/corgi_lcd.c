/*
 * linux/arch/arm/mach-pxa/corgi_lcd.c
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
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/string.h>
#include <mach/corgi.h>
#include <mach/hardware.h>
#include <mach/sharpsl.h>
#include <mach/spitz.h>
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

/* Register Bit Definitions */
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
#define POWER0_COM_ON   0x08  /* COM Power Supply ON */
#define POWER0_VCC5_ON  0x10  /* VCC5 Power Supply ON */

#define POWER0_DAC_OFF  0x00  /* DAC Power Supply OFF */
#define POWER0_COM_OFF  0x00  /* COM Power Supply OFF */
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

/* Set Phase Adjust */
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

void corgi_lcdtg_hw_init(int mode)
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

		/* Set Phase Adjust */
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

void corgi_lcdtg_suspend(void)
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

