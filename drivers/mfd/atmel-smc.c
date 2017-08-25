/*
 * Atmel SMC (Static Memory Controller) helper functions.
 *
 * Copyright (C) 2017 Atmel
 * Copyright (C) 2017 Free Electrons
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mfd/syscon/atmel-smc.h>

/**
 * atmel_smc_cs_conf_init - initialize a SMC CS conf
 * @conf: the SMC CS conf to initialize
 *
 * Set all fields to 0 so that one can start defining a new config.
 */
void atmel_smc_cs_conf_init(struct atmel_smc_cs_conf *conf)
{
	memset(conf, 0, sizeof(*conf));
}
EXPORT_SYMBOL_GPL(atmel_smc_cs_conf_init);

/**
 * atmel_smc_cs_encode_ncycles - encode a number of MCK clk cycles in the
 *				 format expected by the SMC engine
 * @ncycles: number of MCK clk cycles
 * @msbpos: position of the MSB part of the timing field
 * @msbwidth: width of the MSB part of the timing field
 * @msbfactor: factor applied to the MSB
 * @encodedval: param used to store the encoding result
 *
 * This function encodes the @ncycles value as described in the datasheet
 * (section "SMC Setup/Pulse/Cycle/Timings Register"). This is a generic
 * helper which called with different parameter depending on the encoding
 * scheme.
 *
 * If the @ncycles value is too big to be encoded, -ERANGE is returned and
 * the encodedval is contains the maximum val. Otherwise, 0 is returned.
 */
static int atmel_smc_cs_encode_ncycles(unsigned int ncycles,
				       unsigned int msbpos,
				       unsigned int msbwidth,
				       unsigned int msbfactor,
				       unsigned int *encodedval)
{
	unsigned int lsbmask = GENMASK(msbpos - 1, 0);
	unsigned int msbmask = GENMASK(msbwidth - 1, 0);
	unsigned int msb, lsb;
	int ret = 0;

	msb = ncycles / msbfactor;
	lsb = ncycles % msbfactor;

	if (lsb > lsbmask) {
		lsb = 0;
		msb++;
	}

	/*
	 * Let's just put the maximum we can if the requested setting does
	 * not fit in the register field.
	 * We still return -ERANGE in case the caller cares.
	 */
	if (msb > msbmask) {
		msb = msbmask;
		lsb = lsbmask;
		ret = -ERANGE;
	}

	*encodedval = (msb << msbpos) | lsb;

	return ret;
}

/**
 * atmel_smc_cs_conf_set_timing - set the SMC CS conf Txx parameter to a
 *				  specific value
 * @conf: SMC CS conf descriptor
 * @shift: the position of the Txx field in the TIMINGS register
 * @ncycles: value (expressed in MCK clk cycles) to assign to this Txx
 *	     parameter
 *
 * This function encodes the @ncycles value as described in the datasheet
 * (section "SMC Timings Register"), and then stores the result in the
 * @conf->timings field at @shift position.
 *
 * Returns -EINVAL if shift is invalid, -ERANGE if ncycles does not fit in
 * the field, and 0 otherwise.
 */
int atmel_smc_cs_conf_set_timing(struct atmel_smc_cs_conf *conf,
				 unsigned int shift, unsigned int ncycles)
{
	unsigned int val;
	int ret;

	if (shift != ATMEL_HSMC_TIMINGS_TCLR_SHIFT &&
	    shift != ATMEL_HSMC_TIMINGS_TADL_SHIFT &&
	    shift != ATMEL_HSMC_TIMINGS_TAR_SHIFT &&
	    shift != ATMEL_HSMC_TIMINGS_TRR_SHIFT &&
	    shift != ATMEL_HSMC_TIMINGS_TWB_SHIFT)
		return -EINVAL;

	/*
	 * The formula described in atmel datasheets (section "HSMC Timings
	 * Register"):
	 *
	 * ncycles = (Txx[3] * 64) + Txx[2:0]
	 */
	ret = atmel_smc_cs_encode_ncycles(ncycles, 3, 1, 64, &val);
	conf->timings &= ~GENMASK(shift + 3, shift);
	conf->timings |= val << shift;

	return ret;
}
EXPORT_SYMBOL_GPL(atmel_smc_cs_conf_set_timing);

/**
 * atmel_smc_cs_conf_set_setup - set the SMC CS conf xx_SETUP parameter to a
 *				 specific value
 * @conf: SMC CS conf descriptor
 * @shift: the position of the xx_SETUP field in the SETUP register
 * @ncycles: value (expressed in MCK clk cycles) to assign to this xx_SETUP
 *	     parameter
 *
 * This function encodes the @ncycles value as described in the datasheet
 * (section "SMC Setup Register"), and then stores the result in the
 * @conf->setup field at @shift position.
 *
 * Returns -EINVAL if @shift is invalid, -ERANGE if @ncycles does not fit in
 * the field, and 0 otherwise.
 */
int atmel_smc_cs_conf_set_setup(struct atmel_smc_cs_conf *conf,
				unsigned int shift, unsigned int ncycles)
{
	unsigned int val;
	int ret;

	if (shift != ATMEL_SMC_NWE_SHIFT && shift != ATMEL_SMC_NCS_WR_SHIFT &&
	    shift != ATMEL_SMC_NRD_SHIFT && shift != ATMEL_SMC_NCS_RD_SHIFT)
		return -EINVAL;

	/*
	 * The formula described in atmel datasheets (section "SMC Setup
	 * Register"):
	 *
	 * ncycles = (128 * xx_SETUP[5]) + xx_SETUP[4:0]
	 */
	ret = atmel_smc_cs_encode_ncycles(ncycles, 5, 1, 128, &val);
	conf->setup &= ~GENMASK(shift + 7, shift);
	conf->setup |= val << shift;

	return ret;
}
EXPORT_SYMBOL_GPL(atmel_smc_cs_conf_set_setup);

/**
 * atmel_smc_cs_conf_set_pulse - set the SMC CS conf xx_PULSE parameter to a
 *				 specific value
 * @conf: SMC CS conf descriptor
 * @shift: the position of the xx_PULSE field in the PULSE register
 * @ncycles: value (expressed in MCK clk cycles) to assign to this xx_PULSE
 *	     parameter
 *
 * This function encodes the @ncycles value as described in the datasheet
 * (section "SMC Pulse Register"), and then stores the result in the
 * @conf->setup field at @shift position.
 *
 * Returns -EINVAL if @shift is invalid, -ERANGE if @ncycles does not fit in
 * the field, and 0 otherwise.
 */
int atmel_smc_cs_conf_set_pulse(struct atmel_smc_cs_conf *conf,
				unsigned int shift, unsigned int ncycles)
{
	unsigned int val;
	int ret;

	if (shift != ATMEL_SMC_NWE_SHIFT && shift != ATMEL_SMC_NCS_WR_SHIFT &&
	    shift != ATMEL_SMC_NRD_SHIFT && shift != ATMEL_SMC_NCS_RD_SHIFT)
		return -EINVAL;

	/*
	 * The formula described in atmel datasheets (section "SMC Pulse
	 * Register"):
	 *
	 * ncycles = (256 * xx_PULSE[6]) + xx_PULSE[5:0]
	 */
	ret = atmel_smc_cs_encode_ncycles(ncycles, 6, 1, 256, &val);
	conf->pulse &= ~GENMASK(shift + 7, shift);
	conf->pulse |= val << shift;

	return ret;
}
EXPORT_SYMBOL_GPL(atmel_smc_cs_conf_set_pulse);

/**
 * atmel_smc_cs_conf_set_cycle - set the SMC CS conf xx_CYCLE parameter to a
 *				 specific value
 * @conf: SMC CS conf descriptor
 * @shift: the position of the xx_CYCLE field in the CYCLE register
 * @ncycles: value (expressed in MCK clk cycles) to assign to this xx_CYCLE
 *	     parameter
 *
 * This function encodes the @ncycles value as described in the datasheet
 * (section "SMC Cycle Register"), and then stores the result in the
 * @conf->setup field at @shift position.
 *
 * Returns -EINVAL if @shift is invalid, -ERANGE if @ncycles does not fit in
 * the field, and 0 otherwise.
 */
int atmel_smc_cs_conf_set_cycle(struct atmel_smc_cs_conf *conf,
				unsigned int shift, unsigned int ncycles)
{
	unsigned int val;
	int ret;

	if (shift != ATMEL_SMC_NWE_SHIFT && shift != ATMEL_SMC_NRD_SHIFT)
		return -EINVAL;

	/*
	 * The formula described in atmel datasheets (section "SMC Cycle
	 * Register"):
	 *
	 * ncycles = (xx_CYCLE[8:7] * 256) + xx_CYCLE[6:0]
	 */
	ret = atmel_smc_cs_encode_ncycles(ncycles, 7, 2, 256, &val);
	conf->cycle &= ~GENMASK(shift + 15, shift);
	conf->cycle |= val << shift;

	return ret;
}
EXPORT_SYMBOL_GPL(atmel_smc_cs_conf_set_cycle);

/**
 * atmel_smc_cs_conf_apply - apply an SMC CS conf
 * @regmap: the SMC regmap
 * @cs: the CS id
 * @conf the SMC CS conf to apply
 *
 * Applies an SMC CS configuration.
 * Only valid on at91sam9/avr32 SoCs.
 */
void atmel_smc_cs_conf_apply(struct regmap *regmap, int cs,
			     const struct atmel_smc_cs_conf *conf)
{
	regmap_write(regmap, ATMEL_SMC_SETUP(cs), conf->setup);
	regmap_write(regmap, ATMEL_SMC_PULSE(cs), conf->pulse);
	regmap_write(regmap, ATMEL_SMC_CYCLE(cs), conf->cycle);
	regmap_write(regmap, ATMEL_SMC_MODE(cs), conf->mode);
}
EXPORT_SYMBOL_GPL(atmel_smc_cs_conf_apply);

/**
 * atmel_hsmc_cs_conf_apply - apply an SMC CS conf
 * @regmap: the HSMC regmap
 * @cs: the CS id
 * @conf the SMC CS conf to apply
 *
 * Applies an SMC CS configuration.
 * Only valid on post-sama5 SoCs.
 */
void atmel_hsmc_cs_conf_apply(struct regmap *regmap, int cs,
			      const struct atmel_smc_cs_conf *conf)
{
	regmap_write(regmap, ATMEL_HSMC_SETUP(cs), conf->setup);
	regmap_write(regmap, ATMEL_HSMC_PULSE(cs), conf->pulse);
	regmap_write(regmap, ATMEL_HSMC_CYCLE(cs), conf->cycle);
	regmap_write(regmap, ATMEL_HSMC_TIMINGS(cs), conf->timings);
	regmap_write(regmap, ATMEL_HSMC_MODE(cs), conf->mode);
}
EXPORT_SYMBOL_GPL(atmel_hsmc_cs_conf_apply);

/**
 * atmel_smc_cs_conf_get - retrieve the current SMC CS conf
 * @regmap: the SMC regmap
 * @cs: the CS id
 * @conf: the SMC CS conf object to store the current conf
 *
 * Retrieve the SMC CS configuration.
 * Only valid on at91sam9/avr32 SoCs.
 */
void atmel_smc_cs_conf_get(struct regmap *regmap, int cs,
			   struct atmel_smc_cs_conf *conf)
{
	regmap_read(regmap, ATMEL_SMC_SETUP(cs), &conf->setup);
	regmap_read(regmap, ATMEL_SMC_PULSE(cs), &conf->pulse);
	regmap_read(regmap, ATMEL_SMC_CYCLE(cs), &conf->cycle);
	regmap_read(regmap, ATMEL_SMC_MODE(cs), &conf->mode);
}
EXPORT_SYMBOL_GPL(atmel_smc_cs_conf_get);

/**
 * atmel_hsmc_cs_conf_get - retrieve the current SMC CS conf
 * @regmap: the HSMC regmap
 * @cs: the CS id
 * @conf: the SMC CS conf object to store the current conf
 *
 * Retrieve the SMC CS configuration.
 * Only valid on post-sama5 SoCs.
 */
void atmel_hsmc_cs_conf_get(struct regmap *regmap, int cs,
			    struct atmel_smc_cs_conf *conf)
{
	regmap_read(regmap, ATMEL_HSMC_SETUP(cs), &conf->setup);
	regmap_read(regmap, ATMEL_HSMC_PULSE(cs), &conf->pulse);
	regmap_read(regmap, ATMEL_HSMC_CYCLE(cs), &conf->cycle);
	regmap_read(regmap, ATMEL_HSMC_TIMINGS(cs), &conf->timings);
	regmap_read(regmap, ATMEL_HSMC_MODE(cs), &conf->mode);
}
EXPORT_SYMBOL_GPL(atmel_hsmc_cs_conf_get);
