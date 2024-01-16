// SPDX-License-Identifier: GPL-2.0+
/*
 * FB driver for the ST7789V LCD Controller
 *
 * Copyright (C) 2015 Dennis Menschel
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/module.h>

#include <video/mipi_display.h>

#include "fbtft.h"

#define DRVNAME "fb_st7789v"

#define DEFAULT_GAMMA \
	"70 2C 2E 15 10 09 48 33 53 0B 19 18 20 25\n" \
	"70 2C 2E 15 10 09 48 33 53 0B 19 18 20 25"

#define HSD20_IPS_GAMMA \
	"D0 05 0A 09 08 05 2E 44 45 0F 17 16 2B 33\n" \
	"D0 05 0A 09 08 05 2E 43 45 0F 16 16 2B 33"

#define HSD20_IPS 1

/**
 * enum st7789v_command - ST7789V display controller commands
 *
 * @PORCTRL: porch setting
 * @GCTRL: gate control
 * @VCOMS: VCOM setting
 * @VDVVRHEN: VDV and VRH command enable
 * @VRHS: VRH set
 * @VDVS: VDV set
 * @VCMOFSET: VCOM offset set
 * @PWCTRL1: power control 1
 * @PVGAMCTRL: positive voltage gamma control
 * @NVGAMCTRL: negative voltage gamma control
 *
 * The command names are the same as those found in the datasheet to ease
 * looking up their semantics and usage.
 *
 * Note that the ST7789V display controller offers quite a few more commands
 * which have been omitted from this list as they are not used at the moment.
 * Furthermore, commands that are compliant with the MIPI DCS have been left
 * out as well to avoid duplicate entries.
 */
enum st7789v_command {
	PORCTRL = 0xB2,
	GCTRL = 0xB7,
	VCOMS = 0xBB,
	VDVVRHEN = 0xC2,
	VRHS = 0xC3,
	VDVS = 0xC4,
	VCMOFSET = 0xC5,
	PWCTRL1 = 0xD0,
	PVGAMCTRL = 0xE0,
	NVGAMCTRL = 0xE1,
};

#define MADCTL_BGR BIT(3) /* bitmask for RGB/BGR order */
#define MADCTL_MV BIT(5) /* bitmask for page/column order */
#define MADCTL_MX BIT(6) /* bitmask for column address order */
#define MADCTL_MY BIT(7) /* bitmask for page address order */

/* 60Hz for 16.6ms, configured as 2*16.6ms */
#define PANEL_TE_TIMEOUT_MS  33

static struct completion panel_te; /* completion for panel TE line */
static int irq_te; /* Linux IRQ for LCD TE line */

static irqreturn_t panel_te_handler(int irq, void *data)
{
	complete(&panel_te);
	return IRQ_HANDLED;
}

/*
 * init_tearing_effect_line() - init tearing effect line.
 * @par: FBTFT parameter object.
 *
 * Return: 0 on success, or a negative error code otherwise.
 */
static int init_tearing_effect_line(struct fbtft_par *par)
{
	struct device *dev = par->info->device;
	struct gpio_desc *te;
	int rc, irq;

	te = gpiod_get_optional(dev, "te", GPIOD_IN);
	if (IS_ERR(te))
		return dev_err_probe(dev, PTR_ERR(te), "Failed to request te GPIO\n");

	/* if te is NULL, indicating no configuration, directly return success */
	if (!te) {
		irq_te = 0;
		return 0;
	}

	irq = gpiod_to_irq(te);

	/* GPIO is locked as an IRQ, we may drop the reference */
	gpiod_put(te);

	if (irq < 0)
		return irq;

	irq_te = irq;
	init_completion(&panel_te);

	/* The effective state is high and lasts no more than 1000 microseconds */
	rc = devm_request_irq(dev, irq_te, panel_te_handler,
			      IRQF_TRIGGER_RISING, "TE_GPIO", par);
	if (rc)
		return dev_err_probe(dev, rc, "TE IRQ request failed.\n");

	disable_irq_nosync(irq_te);

	return 0;
}

/**
 * init_display() - initialize the display controller
 *
 * @par: FBTFT parameter object
 *
 * Most of the commands in this init function set their parameters to the
 * same default values which are already in place after the display has been
 * powered up. (The main exception to this rule is the pixel format which
 * would default to 18 instead of 16 bit per pixel.)
 * Nonetheless, this sequence can be used as a template for concrete
 * displays which usually need some adjustments.
 *
 * Return: 0 on success, < 0 if error occurred.
 */
static int init_display(struct fbtft_par *par)
{
	int rc;

	par->fbtftops.reset(par);

	rc = init_tearing_effect_line(par);
	if (rc)
		return rc;

	/* turn off sleep mode */
	write_reg(par, MIPI_DCS_EXIT_SLEEP_MODE);
	mdelay(120);

	/* set pixel format to RGB-565 */
	write_reg(par, MIPI_DCS_SET_PIXEL_FORMAT, MIPI_DCS_PIXEL_FMT_16BIT);
	if (HSD20_IPS)
		write_reg(par, PORCTRL, 0x05, 0x05, 0x00, 0x33, 0x33);

	else
		write_reg(par, PORCTRL, 0x08, 0x08, 0x00, 0x22, 0x22);

	/*
	 * VGH = 13.26V
	 * VGL = -10.43V
	 */
	if (HSD20_IPS)
		write_reg(par, GCTRL, 0x75);
	else
		write_reg(par, GCTRL, 0x35);

	/*
	 * VDV and VRH register values come from command write
	 * (instead of NVM)
	 */
	write_reg(par, VDVVRHEN, 0x01, 0xFF);

	/*
	 * VAP =  4.1V + (VCOM + VCOM offset + 0.5 * VDV)
	 * VAN = -4.1V + (VCOM + VCOM offset + 0.5 * VDV)
	 */
	if (HSD20_IPS)
		write_reg(par, VRHS, 0x13);
	else
		write_reg(par, VRHS, 0x0B);

	/* VDV = 0V */
	write_reg(par, VDVS, 0x20);

	/* VCOM = 0.9V */
	if (HSD20_IPS)
		write_reg(par, VCOMS, 0x22);
	else
		write_reg(par, VCOMS, 0x20);

	/* VCOM offset = 0V */
	write_reg(par, VCMOFSET, 0x20);

	/*
	 * AVDD = 6.8V
	 * AVCL = -4.8V
	 * VDS = 2.3V
	 */
	write_reg(par, PWCTRL1, 0xA4, 0xA1);

	/* TE line output is off by default when powering on */
	if (irq_te)
		write_reg(par, MIPI_DCS_SET_TEAR_ON, 0x00);

	write_reg(par, MIPI_DCS_SET_DISPLAY_ON);

	if (HSD20_IPS)
		write_reg(par, MIPI_DCS_ENTER_INVERT_MODE);

	return 0;
}

/*
 * write_vmem() - write data to display.
 * @par: FBTFT parameter object.
 * @offset: offset from screen_buffer.
 * @len: the length of data to be writte.
 *
 * Return: 0 on success, or a negative error code otherwise.
 */
static int write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	struct device *dev = par->info->device;
	int ret;

	if (irq_te) {
		enable_irq(irq_te);
		reinit_completion(&panel_te);
		ret = wait_for_completion_timeout(&panel_te,
						  msecs_to_jiffies(PANEL_TE_TIMEOUT_MS));
		if (ret == 0)
			dev_err(dev, "wait panel TE timeout\n");

		disable_irq(irq_te);
	}

	switch (par->pdata->display.buswidth) {
	case 8:
		ret = fbtft_write_vmem16_bus8(par, offset, len);
		break;
	case 9:
		ret = fbtft_write_vmem16_bus9(par, offset, len);
		break;
	case 16:
		ret = fbtft_write_vmem16_bus16(par, offset, len);
		break;
	default:
		dev_err(dev, "Unsupported buswidth %d\n",
			par->pdata->display.buswidth);
		ret = 0;
		break;
	}

	return ret;
}

/**
 * set_var() - apply LCD properties like rotation and BGR mode
 *
 * @par: FBTFT parameter object
 *
 * Return: 0 on success, < 0 if error occurred.
 */
static int set_var(struct fbtft_par *par)
{
	u8 madctl_par = 0;

	if (par->bgr)
		madctl_par |= MADCTL_BGR;
	switch (par->info->var.rotate) {
	case 0:
		break;
	case 90:
		madctl_par |= (MADCTL_MV | MADCTL_MY);
		break;
	case 180:
		madctl_par |= (MADCTL_MX | MADCTL_MY);
		break;
	case 270:
		madctl_par |= (MADCTL_MV | MADCTL_MX);
		break;
	default:
		return -EINVAL;
	}
	write_reg(par, MIPI_DCS_SET_ADDRESS_MODE, madctl_par);
	return 0;
}

/**
 * set_gamma() - set gamma curves
 *
 * @par: FBTFT parameter object
 * @curves: gamma curves
 *
 * Before the gamma curves are applied, they are preprocessed with a bitmask
 * to ensure syntactically correct input for the display controller.
 * This implies that the curves input parameter might be changed by this
 * function and that illegal gamma values are auto-corrected and not
 * reported as errors.
 *
 * Return: 0 on success, < 0 if error occurred.
 */
static int set_gamma(struct fbtft_par *par, u32 *curves)
{
	int i;
	int j;
	int c; /* curve index offset */

	/*
	 * Bitmasks for gamma curve command parameters.
	 * The masks are the same for both positive and negative voltage
	 * gamma curves.
	 */
	static const u8 gamma_par_mask[] = {
		0xFF, /* V63[3:0], V0[3:0]*/
		0x3F, /* V1[5:0] */
		0x3F, /* V2[5:0] */
		0x1F, /* V4[4:0] */
		0x1F, /* V6[4:0] */
		0x3F, /* J0[1:0], V13[3:0] */
		0x7F, /* V20[6:0] */
		0x77, /* V36[2:0], V27[2:0] */
		0x7F, /* V43[6:0] */
		0x3F, /* J1[1:0], V50[3:0] */
		0x1F, /* V57[4:0] */
		0x1F, /* V59[4:0] */
		0x3F, /* V61[5:0] */
		0x3F, /* V62[5:0] */
	};

	for (i = 0; i < par->gamma.num_curves; i++) {
		c = i * par->gamma.num_values;
		for (j = 0; j < par->gamma.num_values; j++)
			curves[c + j] &= gamma_par_mask[j];
		write_reg(par, PVGAMCTRL + i,
			  curves[c + 0],  curves[c + 1],  curves[c + 2],
			  curves[c + 3],  curves[c + 4],  curves[c + 5],
			  curves[c + 6],  curves[c + 7],  curves[c + 8],
			  curves[c + 9],  curves[c + 10], curves[c + 11],
			  curves[c + 12], curves[c + 13]);
	}
	return 0;
}

/**
 * blank() - blank the display
 *
 * @par: FBTFT parameter object
 * @on: whether to enable or disable blanking the display
 *
 * Return: 0 on success, < 0 if error occurred.
 */
static int blank(struct fbtft_par *par, bool on)
{
	if (on)
		write_reg(par, MIPI_DCS_SET_DISPLAY_OFF);
	else
		write_reg(par, MIPI_DCS_SET_DISPLAY_ON);
	return 0;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = 240,
	.height = 320,
	.gamma_num = 2,
	.gamma_len = 14,
	.gamma = HSD20_IPS_GAMMA,
	.fbtftops = {
		.init_display = init_display,
		.write_vmem = write_vmem,
		.set_var = set_var,
		.set_gamma = set_gamma,
		.blank = blank,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "sitronix,st7789v", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:st7789v");
MODULE_ALIAS("platform:st7789v");

MODULE_DESCRIPTION("FB driver for the ST7789V LCD Controller");
MODULE_AUTHOR("Dennis Menschel");
MODULE_LICENSE("GPL");
