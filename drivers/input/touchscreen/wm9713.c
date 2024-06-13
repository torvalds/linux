// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * wm9713.c  --  Codec touch driver for Wolfson WM9713 AC97 Codec.
 *
 * Copyright 2003, 2004, 2005, 2006, 2007, 2008 Wolfson Microelectronics PLC.
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 * Parts Copyright : Ian Molton <spyro@f2s.com>
 *                   Andrew Zabolotny <zap@homelink.ru>
 *                   Russell King <rmk@arm.linux.org.uk>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/wm97xx.h>

#define TS_NAME			"wm97xx"
#define WM9713_VERSION		"1.00"
#define DEFAULT_PRESSURE	0xb0c0

/*
 * Module parameters
 */

/*
 * Set internal pull up for pen detect.
 *
 * Pull up is in the range 1.02k (least sensitive) to 64k (most sensitive)
 * i.e. pull up resistance = 64k Ohms / rpu.
 *
 * Adjust this value if you are having problems with pen detect not
 * detecting any down event.
 */
static int rpu = 8;
module_param(rpu, int, 0);
MODULE_PARM_DESC(rpu, "Set internal pull up resistor for pen detect.");

/*
 * Set current used for pressure measurement.
 *
 * Set pil = 2 to use 400uA
 *     pil = 1 to use 200uA and
 *     pil = 0 to disable pressure measurement.
 *
 * This is used to increase the range of values returned by the adc
 * when measureing touchpanel pressure.
 */
static int pil;
module_param(pil, int, 0);
MODULE_PARM_DESC(pil, "Set current used for pressure measurement.");

/*
 * Set threshold for pressure measurement.
 *
 * Pen down pressure below threshold is ignored.
 */
static int pressure = DEFAULT_PRESSURE & 0xfff;
module_param(pressure, int, 0);
MODULE_PARM_DESC(pressure, "Set threshold for pressure measurement.");

/*
 * Set adc sample delay.
 *
 * For accurate touchpanel measurements, some settling time may be
 * required between the switch matrix applying a voltage across the
 * touchpanel plate and the ADC sampling the signal.
 *
 * This delay can be set by setting delay = n, where n is the array
 * position of the delay in the array delay_table below.
 * Long delays > 1ms are supported for completeness, but are not
 * recommended.
 */
static int delay = 4;
module_param(delay, int, 0);
MODULE_PARM_DESC(delay, "Set adc sample delay.");

/*
 * Set five_wire = 1 to use a 5 wire touchscreen.
 *
 * NOTE: Five wire mode does not allow for readback of pressure.
 */
static int five_wire;
module_param(five_wire, int, 0);
MODULE_PARM_DESC(five_wire, "Set to '1' to use 5-wire touchscreen.");

/*
 * Set adc mask function.
 *
 * Sources of glitch noise, such as signals driving an LCD display, may feed
 * through to the touch screen plates and affect measurement accuracy. In
 * order to minimise this, a signal may be applied to the MASK pin to delay or
 * synchronise the sampling.
 *
 * 0 = No delay or sync
 * 1 = High on pin stops conversions
 * 2 = Edge triggered, edge on pin delays conversion by delay param (above)
 * 3 = Edge triggered, edge on pin starts conversion after delay param
 */
static int mask;
module_param(mask, int, 0);
MODULE_PARM_DESC(mask, "Set adc mask function.");

/*
 * Coordinate Polling Enable.
 *
 * Set to 1 to enable coordinate polling. e.g. x,y[,p] is sampled together
 * for every poll.
 */
static int coord;
module_param(coord, int, 0);
MODULE_PARM_DESC(coord, "Polling coordinate mode");

/*
 * ADC sample delay times in uS
 */
static const int delay_table[] = {
	21,    /* 1 AC97 Link frames */
	42,    /* 2 */
	84,    /* 4 */
	167,   /* 8 */
	333,   /* 16 */
	667,   /* 32 */
	1000,  /* 48 */
	1333,  /* 64 */
	2000,  /* 96 */
	2667,  /* 128 */
	3333,  /* 160 */
	4000,  /* 192 */
	4667,  /* 224 */
	5333,  /* 256 */
	6000,  /* 288 */
	0      /* No delay, switch matrix always on */
};

/*
 * Delay after issuing a POLL command.
 *
 * The delay is 3 AC97 link frames + the touchpanel settling delay
 */
static inline void poll_delay(int d)
{
	udelay(3 * AC97_LINK_FRAME + delay_table[d]);
}

/*
 * set up the physical settings of the WM9713
 */
static void wm9713_phy_init(struct wm97xx *wm)
{
	u16 dig1 = 0, dig2, dig3;

	/* default values */
	dig2 = WM97XX_DELAY(4) | WM97XX_SLT(5);
	dig3 = WM9712_RPU(1);

	/* rpu */
	if (rpu) {
		dig3 &= 0xffc0;
		dig3 |= WM9712_RPU(rpu);
		dev_info(wm->dev, "setting pen detect pull-up to %d Ohms\n",
			 64000 / rpu);
	}

	/* Five wire panel? */
	if (five_wire) {
		dig3 |= WM9713_45W;
		dev_info(wm->dev, "setting 5-wire touchscreen mode.");

		if (pil) {
			dev_warn(wm->dev,
				 "Pressure measurement not supported in 5 "
				 "wire mode, disabling\n");
			pil = 0;
		}
	}

	/* touchpanel pressure */
	if (pil == 2) {
		dig3 |= WM9712_PIL;
		dev_info(wm->dev,
			 "setting pressure measurement current to 400uA.");
	} else if (pil)
		dev_info(wm->dev,
			 "setting pressure measurement current to 200uA.");
	if (!pil)
		pressure = 0;

	/* sample settling delay */
	if (delay < 0 || delay > 15) {
		dev_info(wm->dev, "supplied delay out of range.");
		delay = 4;
		dev_info(wm->dev, "setting adc sample delay to %d u Secs.",
			 delay_table[delay]);
	}
	dig2 &= 0xff0f;
	dig2 |= WM97XX_DELAY(delay);

	/* mask */
	dig3 |= ((mask & 0x3) << 4);
	if (coord)
		dig3 |= WM9713_WAIT;

	wm->misc = wm97xx_reg_read(wm, 0x5a);

	wm97xx_reg_write(wm, AC97_WM9713_DIG1, dig1);
	wm97xx_reg_write(wm, AC97_WM9713_DIG2, dig2);
	wm97xx_reg_write(wm, AC97_WM9713_DIG3, dig3);
	wm97xx_reg_write(wm, AC97_GPIO_STICKY, 0x0);
}

static void wm9713_dig_enable(struct wm97xx *wm, int enable)
{
	u16 val;

	if (enable) {
		val = wm97xx_reg_read(wm, AC97_EXTENDED_MID);
		wm97xx_reg_write(wm, AC97_EXTENDED_MID, val & 0x7fff);
		wm97xx_reg_write(wm, AC97_WM9713_DIG3, wm->dig[2] |
				 WM97XX_PRP_DET_DIG);
		wm97xx_reg_read(wm, AC97_WM97XX_DIGITISER_RD); /* dummy read */
	} else {
		wm97xx_reg_write(wm, AC97_WM9713_DIG3, wm->dig[2] &
					~WM97XX_PRP_DET_DIG);
		val = wm97xx_reg_read(wm, AC97_EXTENDED_MID);
		wm97xx_reg_write(wm, AC97_EXTENDED_MID, val | 0x8000);
	}
}

static void wm9713_dig_restore(struct wm97xx *wm)
{
	wm97xx_reg_write(wm, AC97_WM9713_DIG1, wm->dig_save[0]);
	wm97xx_reg_write(wm, AC97_WM9713_DIG2, wm->dig_save[1]);
	wm97xx_reg_write(wm, AC97_WM9713_DIG3, wm->dig_save[2]);
}

static void wm9713_aux_prepare(struct wm97xx *wm)
{
	memcpy(wm->dig_save, wm->dig, sizeof(wm->dig));
	wm97xx_reg_write(wm, AC97_WM9713_DIG1, 0);
	wm97xx_reg_write(wm, AC97_WM9713_DIG2, 0);
	wm97xx_reg_write(wm, AC97_WM9713_DIG3, WM97XX_PRP_DET_DIG);
}

static inline int is_pden(struct wm97xx *wm)
{
	return wm->dig[2] & WM9713_PDEN;
}

/*
 * Read a sample from the WM9713 adc in polling mode.
 */
static int wm9713_poll_sample(struct wm97xx *wm, int adcsel, int *sample)
{
	u16 dig1;
	int timeout = 5 * delay;
	bool wants_pen = adcsel & WM97XX_PEN_DOWN;

	if (wants_pen && !wm->pen_probably_down) {
		u16 data = wm97xx_reg_read(wm, AC97_WM97XX_DIGITISER_RD);
		if (!(data & WM97XX_PEN_DOWN))
			return RC_PENUP;
		wm->pen_probably_down = 1;
	}

	/* set up digitiser */
	dig1 = wm97xx_reg_read(wm, AC97_WM9713_DIG1);
	dig1 &= ~WM9713_ADCSEL_MASK;
	/* WM97XX_ADCSEL_* channels need to be converted to WM9713 format */
	dig1 |= 1 << ((adcsel & WM97XX_ADCSEL_MASK) >> 12);

	if (wm->mach_ops && wm->mach_ops->pre_sample)
		wm->mach_ops->pre_sample(adcsel);
	wm97xx_reg_write(wm, AC97_WM9713_DIG1, dig1 | WM9713_POLL);

	/* wait 3 AC97 time slots + delay for conversion */
	poll_delay(delay);

	/* wait for POLL to go low */
	while ((wm97xx_reg_read(wm, AC97_WM9713_DIG1) & WM9713_POLL) &&
		timeout) {
		udelay(AC97_LINK_FRAME);
		timeout--;
	}

	if (timeout <= 0) {
		/* If PDEN is set, we can get a timeout when pen goes up */
		if (is_pden(wm))
			wm->pen_probably_down = 0;
		else
			dev_dbg(wm->dev, "adc sample timeout");
		return RC_PENUP;
	}

	*sample = wm97xx_reg_read(wm, AC97_WM97XX_DIGITISER_RD);
	if (wm->mach_ops && wm->mach_ops->post_sample)
		wm->mach_ops->post_sample(adcsel);

	/* check we have correct sample */
	if ((*sample ^ adcsel) & WM97XX_ADCSEL_MASK) {
		dev_dbg(wm->dev, "adc wrong sample, wanted %x got %x",
			adcsel & WM97XX_ADCSEL_MASK,
			*sample & WM97XX_ADCSEL_MASK);
		return RC_PENUP;
	}

	if (wants_pen && !(*sample & WM97XX_PEN_DOWN)) {
		wm->pen_probably_down = 0;
		return RC_PENUP;
	}

	return RC_VALID;
}

/*
 * Read a coordinate from the WM9713 adc in polling mode.
 */
static int wm9713_poll_coord(struct wm97xx *wm, struct wm97xx_data *data)
{
	u16 dig1;
	int timeout = 5 * delay;

	if (!wm->pen_probably_down) {
		u16 val = wm97xx_reg_read(wm, AC97_WM97XX_DIGITISER_RD);
		if (!(val & WM97XX_PEN_DOWN))
			return RC_PENUP;
		wm->pen_probably_down = 1;
	}

	/* set up digitiser */
	dig1 = wm97xx_reg_read(wm, AC97_WM9713_DIG1);
	dig1 &= ~WM9713_ADCSEL_MASK;
	if (pil)
		dig1 |= WM9713_ADCSEL_PRES;

	if (wm->mach_ops && wm->mach_ops->pre_sample)
		wm->mach_ops->pre_sample(WM97XX_ADCSEL_X | WM97XX_ADCSEL_Y);
	wm97xx_reg_write(wm, AC97_WM9713_DIG1,
			 dig1 | WM9713_POLL | WM9713_COO);

	/* wait 3 AC97 time slots + delay for conversion */
	poll_delay(delay);
	data->x = wm97xx_reg_read(wm, AC97_WM97XX_DIGITISER_RD);
	/* wait for POLL to go low */
	while ((wm97xx_reg_read(wm, AC97_WM9713_DIG1) & WM9713_POLL)
	       && timeout) {
		udelay(AC97_LINK_FRAME);
		timeout--;
	}

	if (timeout <= 0) {
		/* If PDEN is set, we can get a timeout when pen goes up */
		if (is_pden(wm))
			wm->pen_probably_down = 0;
		else
			dev_dbg(wm->dev, "adc sample timeout");
		return RC_PENUP;
	}

	/* read back data */
	data->y = wm97xx_reg_read(wm, AC97_WM97XX_DIGITISER_RD);
	if (pil)
		data->p = wm97xx_reg_read(wm, AC97_WM97XX_DIGITISER_RD);
	else
		data->p = DEFAULT_PRESSURE;

	if (wm->mach_ops && wm->mach_ops->post_sample)
		wm->mach_ops->post_sample(WM97XX_ADCSEL_X | WM97XX_ADCSEL_Y);

	/* check we have correct sample */
	if (!(data->x & WM97XX_ADCSEL_X) || !(data->y & WM97XX_ADCSEL_Y))
		goto err;
	if (pil && !(data->p & WM97XX_ADCSEL_PRES))
		goto err;

	if (!(data->x & WM97XX_PEN_DOWN) || !(data->y & WM97XX_PEN_DOWN)) {
		wm->pen_probably_down = 0;
		return RC_PENUP;
	}
	return RC_VALID;
err:
	return 0;
}

/*
 * Sample the WM9713 touchscreen in polling mode
 */
static int wm9713_poll_touch(struct wm97xx *wm, struct wm97xx_data *data)
{
	int rc;

	if (coord) {
		rc = wm9713_poll_coord(wm, data);
		if (rc != RC_VALID)
			return rc;
	} else {
		rc = wm9713_poll_sample(wm, WM97XX_ADCSEL_X | WM97XX_PEN_DOWN, &data->x);
		if (rc != RC_VALID)
			return rc;
		rc = wm9713_poll_sample(wm, WM97XX_ADCSEL_Y | WM97XX_PEN_DOWN, &data->y);
		if (rc != RC_VALID)
			return rc;
		if (pil) {
			rc = wm9713_poll_sample(wm, WM97XX_ADCSEL_PRES | WM97XX_PEN_DOWN,
						&data->p);
			if (rc != RC_VALID)
				return rc;
		} else
			data->p = DEFAULT_PRESSURE;
	}
	return RC_VALID;
}

/*
 * Enable WM9713 continuous mode, i.e. touch data is streamed across
 * an AC97 slot
 */
static int wm9713_acc_enable(struct wm97xx *wm, int enable)
{
	u16 dig1, dig2, dig3;
	int ret = 0;

	dig1 = wm->dig[0];
	dig2 = wm->dig[1];
	dig3 = wm->dig[2];

	if (enable) {
		/* continuous mode */
		if (wm->mach_ops->acc_startup &&
			(ret = wm->mach_ops->acc_startup(wm)) < 0)
			return ret;

		dig1 &= ~WM9713_ADCSEL_MASK;
		dig1 |= WM9713_CTC | WM9713_COO | WM9713_ADCSEL_X |
			WM9713_ADCSEL_Y;
		if (pil)
			dig1 |= WM9713_ADCSEL_PRES;
		dig2 &= ~(WM97XX_DELAY_MASK | WM97XX_SLT_MASK  |
			WM97XX_CM_RATE_MASK);
		dig2 |= WM97XX_SLEN | WM97XX_DELAY(delay) |
		WM97XX_SLT(wm->acc_slot) | WM97XX_RATE(wm->acc_rate);
		dig3 |= WM9713_PDEN;
	} else {
		dig1 &= ~(WM9713_CTC | WM9713_COO);
		dig2 &= ~WM97XX_SLEN;
		dig3 &= ~WM9713_PDEN;
		if (wm->mach_ops->acc_shutdown)
			wm->mach_ops->acc_shutdown(wm);
	}

	wm97xx_reg_write(wm, AC97_WM9713_DIG1, dig1);
	wm97xx_reg_write(wm, AC97_WM9713_DIG2, dig2);
	wm97xx_reg_write(wm, AC97_WM9713_DIG3, dig3);

	return ret;
}

struct wm97xx_codec_drv wm9713_codec = {
	.id = WM9713_ID2,
	.name = "wm9713",
	.poll_sample = wm9713_poll_sample,
	.poll_touch = wm9713_poll_touch,
	.acc_enable = wm9713_acc_enable,
	.phy_init = wm9713_phy_init,
	.dig_enable = wm9713_dig_enable,
	.dig_restore = wm9713_dig_restore,
	.aux_prepare = wm9713_aux_prepare,
};
EXPORT_SYMBOL_GPL(wm9713_codec);

/* Module information */
MODULE_AUTHOR("Liam Girdwood <lrg@slimlogic.co.uk>");
MODULE_DESCRIPTION("WM9713 Touch Screen Driver");
MODULE_LICENSE("GPL");
