// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <linux/delay.h>

#include <drm/drm_managed.h>

#include "lsdc_drv.h"

/*
 * The structure of the pixel PLL registers is evolved with times,
 * it can be different across different chip also.
 */

/* size is u64, note that all loongson's cpu is little endian.
 * This structure is same for ls7a2000, ls7a1000 and ls2k2000.
 */
struct lsdc_pixpll_reg {
	/* Byte 0 ~ Byte 3 */
	unsigned div_out       : 7;   /*  6 : 0     Output clock divider  */
	unsigned _reserved_1_  : 14;  /* 20 : 7                           */
	unsigned loopc         : 9;   /* 29 : 21    Clock multiplier      */
	unsigned _reserved_2_  : 2;   /* 31 : 30                          */

	/* Byte 4 ~ Byte 7 */
	unsigned div_ref       : 7;   /* 38 : 32    Input clock divider   */
	unsigned locked        : 1;   /* 39         PLL locked indicator  */
	unsigned sel_out       : 1;   /* 40         output clk selector   */
	unsigned _reserved_3_  : 2;   /* 42 : 41                          */
	unsigned set_param     : 1;   /* 43         Trigger the update    */
	unsigned bypass        : 1;   /* 44                               */
	unsigned powerdown     : 1;   /* 45                               */
	unsigned _reserved_4_  : 18;  /* 46 : 63    no use                */
};

union lsdc_pixpll_reg_bitmap {
	struct lsdc_pixpll_reg bitmap;
	u32 w[2];
	u64 d;
};

struct clk_to_pixpll_parms_lookup_t {
	unsigned int clock;        /* kHz */

	unsigned short width;
	unsigned short height;
	unsigned short vrefresh;

	/* Stores parameters for programming the Hardware PLLs */
	unsigned short div_out;
	unsigned short loopc;
	unsigned short div_ref;
};

static const struct clk_to_pixpll_parms_lookup_t pixpll_parms_table[] = {
	{148500, 1920, 1080, 60,  11, 49,  3},   /* 1920x1080@60Hz */
	{141750, 1920, 1080, 60,  11, 78,  5},   /* 1920x1080@60Hz */
						 /* 1920x1080@50Hz */
	{174500, 1920, 1080, 75,  17, 89,  3},   /* 1920x1080@75Hz */
	{181250, 2560, 1080, 75,  8,  58,  4},   /* 2560x1080@75Hz */
	{297000, 2560, 1080, 30,  8,  95,  4},   /* 3840x2160@30Hz */
	{301992, 1920, 1080, 100, 10, 151, 5},   /* 1920x1080@100Hz */
	{146250, 1680, 1050, 60,  16, 117, 5},   /* 1680x1050@60Hz */
	{135000, 1280, 1024, 75,  10, 54,  4},   /* 1280x1024@75Hz */
	{119000, 1680, 1050, 60,  20, 119, 5},   /* 1680x1050@60Hz */
	{108000, 1600, 900,  60,  15, 81,  5},   /* 1600x900@60Hz  */
						 /* 1280x1024@60Hz */
						 /* 1280x960@60Hz */
						 /* 1152x864@75Hz */

	{106500, 1440, 900,  60,  19, 81,  4},   /* 1440x900@60Hz */
	{88750,  1440, 900,  60,  16, 71,  5},   /* 1440x900@60Hz */
	{83500,  1280, 800,  60,  17, 71,  5},   /* 1280x800@60Hz */
	{71000,  1280, 800,  60,  20, 71,  5},   /* 1280x800@60Hz */

	{74250,  1280, 720,  60,  22, 49,  3},   /* 1280x720@60Hz */
						 /* 1280x720@50Hz */

	{78750,  1024, 768,  75,  16, 63,  5},   /* 1024x768@75Hz */
	{75000,  1024, 768,  70,  29, 87,  4},   /* 1024x768@70Hz */
	{65000,  1024, 768,  60,  20, 39,  3},   /* 1024x768@60Hz */

	{51200,  1024, 600,  60,  25, 64,  5},   /* 1024x600@60Hz */

	{57284,  832,  624,  75,  24, 55,  4},   /* 832x624@75Hz */
	{49500,  800,  600,  75,  40, 99,  5},   /* 800x600@75Hz */
	{50000,  800,  600,  72,  44, 88,  4},   /* 800x600@72Hz */
	{40000,  800,  600,  60,  30, 36,  3},   /* 800x600@60Hz */
	{36000,  800,  600,  56,  50, 72,  4},   /* 800x600@56Hz */
	{31500,  640,  480,  75,  40, 63,  5},   /* 640x480@75Hz */
						 /* 640x480@73Hz */

	{30240,  640,  480,  67,  62, 75,  4},   /* 640x480@67Hz */
	{27000,  720,  576,  50,  50, 54,  4},   /* 720x576@60Hz */
	{25175,  640,  480,  60,  85, 107, 5},   /* 640x480@60Hz */
	{25200,  640,  480,  60,  50, 63,  5},   /* 640x480@60Hz */
						 /* 720x480@60Hz */
};

static void lsdc_pixel_pll_free(struct drm_device *ddev, void *data)
{
	struct lsdc_pixpll *this = (struct lsdc_pixpll *)data;

	iounmap(this->mmio);

	kfree(this->priv);

	drm_dbg(ddev, "pixpll private data freed\n");
}

/*
 * ioremap the device dependent PLL registers
 *
 * @this: point to the object where this function is called from
 */
static int lsdc_pixel_pll_setup(struct lsdc_pixpll * const this)
{
	struct lsdc_pixpll_parms *pparms;

	this->mmio = ioremap(this->reg_base, this->reg_size);
	if (IS_ERR_OR_NULL(this->mmio))
		return -ENOMEM;

	pparms = kzalloc(sizeof(*pparms), GFP_KERNEL);
	if (IS_ERR_OR_NULL(pparms))
		return -ENOMEM;

	pparms->ref_clock = LSDC_PLL_REF_CLK_KHZ;

	this->priv = pparms;

	return drmm_add_action_or_reset(this->ddev, lsdc_pixel_pll_free, this);
}

/*
 * Find a set of pll parameters from a static local table which avoid
 * computing the pll parameter eachtime a modeset is triggered.
 *
 * @this: point to the object where this function is called from
 * @clock: the desired output pixel clock, the unit is kHz
 * @pout: point to where the parameters to store if found
 *
 * Return 0 if success, return -1 if not found.
 */
static int lsdc_pixpll_find(struct lsdc_pixpll * const this,
			    unsigned int clock,
			    struct lsdc_pixpll_parms *pout)
{
	unsigned int num = ARRAY_SIZE(pixpll_parms_table);
	const struct clk_to_pixpll_parms_lookup_t *pt;
	unsigned int i;

	for (i = 0; i < num; ++i) {
		pt = &pixpll_parms_table[i];

		if (clock == pt->clock) {
			pout->div_ref = pt->div_ref;
			pout->loopc   = pt->loopc;
			pout->div_out = pt->div_out;

			return 0;
		}
	}

	drm_dbg_kms(this->ddev, "pixel clock %u: miss\n", clock);

	return -1;
}

/*
 * Find a set of pll parameters which have minimal difference with the
 * desired pixel clock frequency. It does that by computing all of the
 * possible combination. Compute the diff and find the combination with
 * minimal diff.
 *
 * clock_out = refclk / div_ref * loopc / div_out
 *
 * refclk is determined by the oscillator mounted on motherboard(100MHz
 * in almost all board)
 *
 * @this: point to the object from where this function is called
 * @clock: the desired output pixel clock, the unit is kHz
 * @pout: point to the out struct of lsdc_pixpll_parms
 *
 * Return 0 if a set of parameter is found, otherwise return the error
 * between clock_kHz we wanted and the most closest candidate with it.
 */
static int lsdc_pixel_pll_compute(struct lsdc_pixpll * const this,
				  unsigned int clock,
				  struct lsdc_pixpll_parms *pout)
{
	struct lsdc_pixpll_parms *pparms = this->priv;
	unsigned int refclk = pparms->ref_clock;
	const unsigned int tolerance = 1000;
	unsigned int min = tolerance;
	unsigned int div_out, loopc, div_ref;
	unsigned int computed;

	if (!lsdc_pixpll_find(this, clock, pout))
		return 0;

	for (div_out = 6; div_out < 64; div_out++) {
		for (div_ref = 3; div_ref < 6; div_ref++) {
			for (loopc = 6; loopc < 161; loopc++) {
				unsigned int diff = 0;

				if (loopc < 12 * div_ref)
					continue;
				if (loopc > 32 * div_ref)
					continue;

				computed = refclk / div_ref * loopc / div_out;

				if (clock >= computed)
					diff = clock - computed;
				else
					diff = computed - clock;

				if (diff < min) {
					min = diff;
					pparms->div_ref = div_ref;
					pparms->div_out = div_out;
					pparms->loopc = loopc;

					if (diff == 0) {
						*pout = *pparms;
						return 0;
					}
				}
			}
		}
	}

	/* still acceptable */
	if (min < tolerance) {
		*pout = *pparms;
		return 0;
	}

	drm_dbg(this->ddev, "can't find suitable params for %u khz\n", clock);

	return min;
}

/* Pixel pll hardware related ops, per display pipe */

static void __pixpll_rreg(struct lsdc_pixpll *this,
			  union lsdc_pixpll_reg_bitmap *dst)
{
#if defined(CONFIG_64BIT)
	dst->d = readq(this->mmio);
#else
	dst->w[0] = readl(this->mmio);
	dst->w[1] = readl(this->mmio + 4);
#endif
}

static void __pixpll_wreg(struct lsdc_pixpll *this,
			  union lsdc_pixpll_reg_bitmap *src)
{
#if defined(CONFIG_64BIT)
	writeq(src->d, this->mmio);
#else
	writel(src->w[0], this->mmio);
	writel(src->w[1], this->mmio + 4);
#endif
}

static void __pixpll_ops_powerup(struct lsdc_pixpll * const this)
{
	union lsdc_pixpll_reg_bitmap pixpll_reg;

	__pixpll_rreg(this, &pixpll_reg);

	pixpll_reg.bitmap.powerdown = 0;

	__pixpll_wreg(this, &pixpll_reg);
}

static void __pixpll_ops_powerdown(struct lsdc_pixpll * const this)
{
	union lsdc_pixpll_reg_bitmap pixpll_reg;

	__pixpll_rreg(this, &pixpll_reg);

	pixpll_reg.bitmap.powerdown = 1;

	__pixpll_wreg(this, &pixpll_reg);
}

static void __pixpll_ops_on(struct lsdc_pixpll * const this)
{
	union lsdc_pixpll_reg_bitmap pixpll_reg;

	__pixpll_rreg(this, &pixpll_reg);

	pixpll_reg.bitmap.sel_out = 1;

	__pixpll_wreg(this, &pixpll_reg);
}

static void __pixpll_ops_off(struct lsdc_pixpll * const this)
{
	union lsdc_pixpll_reg_bitmap pixpll_reg;

	__pixpll_rreg(this, &pixpll_reg);

	pixpll_reg.bitmap.sel_out = 0;

	__pixpll_wreg(this, &pixpll_reg);
}

static void __pixpll_ops_bypass(struct lsdc_pixpll * const this)
{
	union lsdc_pixpll_reg_bitmap pixpll_reg;

	__pixpll_rreg(this, &pixpll_reg);

	pixpll_reg.bitmap.bypass = 1;

	__pixpll_wreg(this, &pixpll_reg);
}

static void __pixpll_ops_unbypass(struct lsdc_pixpll * const this)
{
	union lsdc_pixpll_reg_bitmap pixpll_reg;

	__pixpll_rreg(this, &pixpll_reg);

	pixpll_reg.bitmap.bypass = 0;

	__pixpll_wreg(this, &pixpll_reg);
}

static void __pixpll_ops_untoggle_param(struct lsdc_pixpll * const this)
{
	union lsdc_pixpll_reg_bitmap pixpll_reg;

	__pixpll_rreg(this, &pixpll_reg);

	pixpll_reg.bitmap.set_param = 0;

	__pixpll_wreg(this, &pixpll_reg);
}

static void __pixpll_ops_set_param(struct lsdc_pixpll * const this,
				   struct lsdc_pixpll_parms const *p)
{
	union lsdc_pixpll_reg_bitmap pixpll_reg;

	__pixpll_rreg(this, &pixpll_reg);

	pixpll_reg.bitmap.div_ref = p->div_ref;
	pixpll_reg.bitmap.loopc = p->loopc;
	pixpll_reg.bitmap.div_out = p->div_out;

	__pixpll_wreg(this, &pixpll_reg);
}

static void __pixpll_ops_toggle_param(struct lsdc_pixpll * const this)
{
	union lsdc_pixpll_reg_bitmap pixpll_reg;

	__pixpll_rreg(this, &pixpll_reg);

	pixpll_reg.bitmap.set_param = 1;

	__pixpll_wreg(this, &pixpll_reg);
}

static void __pixpll_ops_wait_locked(struct lsdc_pixpll * const this)
{
	union lsdc_pixpll_reg_bitmap pixpll_reg;
	unsigned int counter = 0;

	do {
		__pixpll_rreg(this, &pixpll_reg);

		if (pixpll_reg.bitmap.locked)
			break;

		++counter;
	} while (counter < 2000);

	drm_dbg(this->ddev, "%u loop waited\n", counter);
}

/*
 * Update the PLL parameters to the PLL hardware
 *
 * @this: point to the object from which this function is called
 * @pin: point to the struct of lsdc_pixpll_parms passed in
 *
 * return 0 if successful.
 */
static int lsdc_pixpll_update(struct lsdc_pixpll * const this,
			      struct lsdc_pixpll_parms const *pin)
{
	__pixpll_ops_bypass(this);

	__pixpll_ops_off(this);

	__pixpll_ops_powerdown(this);

	__pixpll_ops_toggle_param(this);

	__pixpll_ops_set_param(this, pin);

	__pixpll_ops_untoggle_param(this);

	__pixpll_ops_powerup(this);

	udelay(2);

	__pixpll_ops_wait_locked(this);

	__pixpll_ops_on(this);

	__pixpll_ops_unbypass(this);

	return 0;
}

static unsigned int lsdc_pixpll_get_freq(struct lsdc_pixpll * const this)
{
	struct lsdc_pixpll_parms *ppar = this->priv;
	union lsdc_pixpll_reg_bitmap pix_pll_reg;
	unsigned int freq;

	__pixpll_rreg(this, &pix_pll_reg);

	ppar->div_ref = pix_pll_reg.bitmap.div_ref;
	ppar->loopc = pix_pll_reg.bitmap.loopc;
	ppar->div_out = pix_pll_reg.bitmap.div_out;

	freq = ppar->ref_clock / ppar->div_ref * ppar->loopc / ppar->div_out;

	return freq;
}

static void lsdc_pixpll_print(struct lsdc_pixpll * const this,
			      struct drm_printer *p)
{
	struct lsdc_pixpll_parms *parms = this->priv;

	drm_printf(p, "div_ref: %u, loopc: %u, div_out: %u\n",
		   parms->div_ref, parms->loopc, parms->div_out);
}

/*
 * LS7A1000, LS7A2000 and ls2k2000's pixel pll setting register is same,
 * we take this as default, create a new instance if a different model is
 * introduced.
 */
static const struct lsdc_pixpll_funcs __pixpll_default_funcs = {
	.setup = lsdc_pixel_pll_setup,
	.compute = lsdc_pixel_pll_compute,
	.update = lsdc_pixpll_update,
	.get_rate = lsdc_pixpll_get_freq,
	.print = lsdc_pixpll_print,
};

/* pixel pll initialization */

int lsdc_pixpll_init(struct lsdc_pixpll * const this,
		     struct drm_device *ddev,
		     unsigned int index)
{
	struct lsdc_device *ldev = to_lsdc(ddev);
	const struct lsdc_desc *descp = ldev->descp;
	const struct loongson_gfx_desc *gfx = to_loongson_gfx(descp);

	this->ddev = ddev;
	this->reg_size = 8;
	this->reg_base = gfx->conf_reg_base + gfx->pixpll[index].reg_offset;
	this->funcs = &__pixpll_default_funcs;

	return this->funcs->setup(this);
}
