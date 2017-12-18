/*
 * ImgTec IR Hardware Decoder found in PowerDown Controller.
 *
 * Copyright 2010-2014 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This ties into the input subsystem using the RC-core. Protocol support is
 * provided in separate modules which provide the parameters and scancode
 * translation functions to set up the hardware decoder and interpret the
 * resulting input.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <media/rc-core.h>
#include "img-ir.h"

/* Decoders lock (only modified to preprocess them) */
static DEFINE_SPINLOCK(img_ir_decoders_lock);

static bool img_ir_decoders_preprocessed;
static struct img_ir_decoder *img_ir_decoders[] = {
#ifdef CONFIG_IR_IMG_NEC
	&img_ir_nec,
#endif
#ifdef CONFIG_IR_IMG_JVC
	&img_ir_jvc,
#endif
#ifdef CONFIG_IR_IMG_SONY
	&img_ir_sony,
#endif
#ifdef CONFIG_IR_IMG_SHARP
	&img_ir_sharp,
#endif
#ifdef CONFIG_IR_IMG_SANYO
	&img_ir_sanyo,
#endif
#ifdef CONFIG_IR_IMG_RC5
	&img_ir_rc5,
#endif
#ifdef CONFIG_IR_IMG_RC6
	&img_ir_rc6,
#endif
	NULL
};

#define IMG_IR_F_FILTER		BIT(RC_FILTER_NORMAL)	/* enable filtering */
#define IMG_IR_F_WAKE		BIT(RC_FILTER_WAKEUP)	/* enable waking */

/* code type quirks */

#define IMG_IR_QUIRK_CODE_BROKEN	0x1	/* Decode is broken */
#define IMG_IR_QUIRK_CODE_LEN_INCR	0x2	/* Bit length needs increment */
/*
 * The decoder generates rapid interrupts without actually having
 * received any new data after an incomplete IR code is decoded.
 */
#define IMG_IR_QUIRK_CODE_IRQ		0x4

/* functions for preprocessing timings, ensuring max is set */

static void img_ir_timing_preprocess(struct img_ir_timing_range *range,
				     unsigned int unit)
{
	if (range->max < range->min)
		range->max = range->min;
	if (unit) {
		/* multiply by unit and convert to microseconds */
		range->min = (range->min*unit)/1000;
		range->max = (range->max*unit + 999)/1000; /* round up */
	}
}

static void img_ir_symbol_timing_preprocess(struct img_ir_symbol_timing *timing,
					    unsigned int unit)
{
	img_ir_timing_preprocess(&timing->pulse, unit);
	img_ir_timing_preprocess(&timing->space, unit);
}

static void img_ir_timings_preprocess(struct img_ir_timings *timings,
				      unsigned int unit)
{
	img_ir_symbol_timing_preprocess(&timings->ldr, unit);
	img_ir_symbol_timing_preprocess(&timings->s00, unit);
	img_ir_symbol_timing_preprocess(&timings->s01, unit);
	img_ir_symbol_timing_preprocess(&timings->s10, unit);
	img_ir_symbol_timing_preprocess(&timings->s11, unit);
	/* default s10 and s11 to s00 and s01 if no leader */
	if (unit)
		/* multiply by unit and convert to microseconds (round up) */
		timings->ft.ft_min = (timings->ft.ft_min*unit + 999)/1000;
}

/* functions for filling empty fields with defaults */

static void img_ir_timing_defaults(struct img_ir_timing_range *range,
				   struct img_ir_timing_range *defaults)
{
	if (!range->min)
		range->min = defaults->min;
	if (!range->max)
		range->max = defaults->max;
}

static void img_ir_symbol_timing_defaults(struct img_ir_symbol_timing *timing,
					  struct img_ir_symbol_timing *defaults)
{
	img_ir_timing_defaults(&timing->pulse, &defaults->pulse);
	img_ir_timing_defaults(&timing->space, &defaults->space);
}

static void img_ir_timings_defaults(struct img_ir_timings *timings,
				    struct img_ir_timings *defaults)
{
	img_ir_symbol_timing_defaults(&timings->ldr, &defaults->ldr);
	img_ir_symbol_timing_defaults(&timings->s00, &defaults->s00);
	img_ir_symbol_timing_defaults(&timings->s01, &defaults->s01);
	img_ir_symbol_timing_defaults(&timings->s10, &defaults->s10);
	img_ir_symbol_timing_defaults(&timings->s11, &defaults->s11);
	if (!timings->ft.ft_min)
		timings->ft.ft_min = defaults->ft.ft_min;
}

/* functions for converting timings to register values */

/**
 * img_ir_control() - Convert control struct to control register value.
 * @control:	Control data
 *
 * Returns:	The control register value equivalent of @control.
 */
static u32 img_ir_control(const struct img_ir_control *control)
{
	u32 ctrl = control->code_type << IMG_IR_CODETYPE_SHIFT;
	if (control->decoden)
		ctrl |= IMG_IR_DECODEN;
	if (control->hdrtog)
		ctrl |= IMG_IR_HDRTOG;
	if (control->ldrdec)
		ctrl |= IMG_IR_LDRDEC;
	if (control->decodinpol)
		ctrl |= IMG_IR_DECODINPOL;
	if (control->bitorien)
		ctrl |= IMG_IR_BITORIEN;
	if (control->d1validsel)
		ctrl |= IMG_IR_D1VALIDSEL;
	if (control->bitinv)
		ctrl |= IMG_IR_BITINV;
	if (control->decodend2)
		ctrl |= IMG_IR_DECODEND2;
	if (control->bitoriend2)
		ctrl |= IMG_IR_BITORIEND2;
	if (control->bitinvd2)
		ctrl |= IMG_IR_BITINVD2;
	return ctrl;
}

/**
 * img_ir_timing_range_convert() - Convert microsecond range.
 * @out:	Output timing range in clock cycles with a shift.
 * @in:		Input timing range in microseconds.
 * @tolerance:	Tolerance as a fraction of 128 (roughly percent).
 * @clock_hz:	IR clock rate in Hz.
 * @shift:	Shift of output units.
 *
 * Converts min and max from microseconds to IR clock cycles, applies a
 * tolerance, and shifts for the register, rounding in the right direction.
 * Note that in and out can safely be the same object.
 */
static void img_ir_timing_range_convert(struct img_ir_timing_range *out,
					const struct img_ir_timing_range *in,
					unsigned int tolerance,
					unsigned long clock_hz,
					unsigned int shift)
{
	unsigned int min = in->min;
	unsigned int max = in->max;
	/* add a tolerance */
	min = min - (min*tolerance >> 7);
	max = max + (max*tolerance >> 7);
	/* convert from microseconds into clock cycles */
	min = min*clock_hz / 1000000;
	max = (max*clock_hz + 999999) / 1000000; /* round up */
	/* apply shift and copy to output */
	out->min = min >> shift;
	out->max = (max + ((1 << shift) - 1)) >> shift; /* round up */
}

/**
 * img_ir_symbol_timing() - Convert symbol timing struct to register value.
 * @timing:	Symbol timing data
 * @tolerance:	Timing tolerance where 0-128 represents 0-100%
 * @clock_hz:	Frequency of source clock in Hz
 * @pd_shift:	Shift to apply to symbol period
 * @w_shift:	Shift to apply to symbol width
 *
 * Returns:	Symbol timing register value based on arguments.
 */
static u32 img_ir_symbol_timing(const struct img_ir_symbol_timing *timing,
				unsigned int tolerance,
				unsigned long clock_hz,
				unsigned int pd_shift,
				unsigned int w_shift)
{
	struct img_ir_timing_range hw_pulse, hw_period;
	/* we calculate period in hw_period, then convert in place */
	hw_period.min = timing->pulse.min + timing->space.min;
	hw_period.max = timing->pulse.max + timing->space.max;
	img_ir_timing_range_convert(&hw_period, &hw_period,
			tolerance, clock_hz, pd_shift);
	img_ir_timing_range_convert(&hw_pulse, &timing->pulse,
			tolerance, clock_hz, w_shift);
	/* construct register value */
	return	(hw_period.max	<< IMG_IR_PD_MAX_SHIFT)	|
		(hw_period.min	<< IMG_IR_PD_MIN_SHIFT)	|
		(hw_pulse.max	<< IMG_IR_W_MAX_SHIFT)	|
		(hw_pulse.min	<< IMG_IR_W_MIN_SHIFT);
}

/**
 * img_ir_free_timing() - Convert free time timing struct to register value.
 * @timing:	Free symbol timing data
 * @clock_hz:	Source clock frequency in Hz
 *
 * Returns:	Free symbol timing register value.
 */
static u32 img_ir_free_timing(const struct img_ir_free_timing *timing,
			      unsigned long clock_hz)
{
	unsigned int minlen, maxlen, ft_min;
	/* minlen is only 5 bits, and round minlen to multiple of 2 */
	if (timing->minlen < 30)
		minlen = timing->minlen & -2;
	else
		minlen = 30;
	/* maxlen has maximum value of 48, and round maxlen to multiple of 2 */
	if (timing->maxlen < 48)
		maxlen = (timing->maxlen + 1) & -2;
	else
		maxlen = 48;
	/* convert and shift ft_min, rounding upwards */
	ft_min = (timing->ft_min*clock_hz + 999999) / 1000000;
	ft_min = (ft_min + 7) >> 3;
	/* construct register value */
	return	(maxlen << IMG_IR_MAXLEN_SHIFT)	|
		(minlen << IMG_IR_MINLEN_SHIFT)	|
		(ft_min << IMG_IR_FT_MIN_SHIFT);
}

/**
 * img_ir_free_timing_dynamic() - Update free time register value.
 * @st_ft:	Static free time register value from img_ir_free_timing.
 * @filter:	Current filter which may additionally restrict min/max len.
 *
 * Returns:	Updated free time register value based on the current filter.
 */
static u32 img_ir_free_timing_dynamic(u32 st_ft, struct img_ir_filter *filter)
{
	unsigned int minlen, maxlen, newminlen, newmaxlen;

	/* round minlen, maxlen to multiple of 2 */
	newminlen = filter->minlen & -2;
	newmaxlen = (filter->maxlen + 1) & -2;
	/* extract min/max len from register */
	minlen = (st_ft & IMG_IR_MINLEN) >> IMG_IR_MINLEN_SHIFT;
	maxlen = (st_ft & IMG_IR_MAXLEN) >> IMG_IR_MAXLEN_SHIFT;
	/* if the new values are more restrictive, update the register value */
	if (newminlen > minlen) {
		st_ft &= ~IMG_IR_MINLEN;
		st_ft |= newminlen << IMG_IR_MINLEN_SHIFT;
	}
	if (newmaxlen < maxlen) {
		st_ft &= ~IMG_IR_MAXLEN;
		st_ft |= newmaxlen << IMG_IR_MAXLEN_SHIFT;
	}
	return st_ft;
}

/**
 * img_ir_timings_convert() - Convert timings to register values
 * @regs:	Output timing register values
 * @timings:	Input timing data
 * @tolerance:	Timing tolerance where 0-128 represents 0-100%
 * @clock_hz:	Source clock frequency in Hz
 */
static void img_ir_timings_convert(struct img_ir_timing_regvals *regs,
				   const struct img_ir_timings *timings,
				   unsigned int tolerance,
				   unsigned int clock_hz)
{
	/* leader symbol timings are divided by 16 */
	regs->ldr = img_ir_symbol_timing(&timings->ldr, tolerance, clock_hz,
			4, 4);
	/* other symbol timings, pd fields only are divided by 2 */
	regs->s00 = img_ir_symbol_timing(&timings->s00, tolerance, clock_hz,
			1, 0);
	regs->s01 = img_ir_symbol_timing(&timings->s01, tolerance, clock_hz,
			1, 0);
	regs->s10 = img_ir_symbol_timing(&timings->s10, tolerance, clock_hz,
			1, 0);
	regs->s11 = img_ir_symbol_timing(&timings->s11, tolerance, clock_hz,
			1, 0);
	regs->ft = img_ir_free_timing(&timings->ft, clock_hz);
}

/**
 * img_ir_decoder_preprocess() - Preprocess timings in decoder.
 * @decoder:	Decoder to be preprocessed.
 *
 * Ensures that the symbol timing ranges are valid with respect to ordering, and
 * does some fixed conversion on them.
 */
static void img_ir_decoder_preprocess(struct img_ir_decoder *decoder)
{
	/* default tolerance */
	if (!decoder->tolerance)
		decoder->tolerance = 10; /* percent */
	/* and convert tolerance to fraction out of 128 */
	decoder->tolerance = decoder->tolerance * 128 / 100;

	/* fill in implicit fields */
	img_ir_timings_preprocess(&decoder->timings, decoder->unit);

	/* do the same for repeat timings if applicable */
	if (decoder->repeat) {
		img_ir_timings_preprocess(&decoder->rtimings, decoder->unit);
		img_ir_timings_defaults(&decoder->rtimings, &decoder->timings);
	}
}

/**
 * img_ir_decoder_convert() - Generate internal timings in decoder.
 * @decoder:	Decoder to be converted to internal timings.
 * @timings:	Timing register values.
 * @clock_hz:	IR clock rate in Hz.
 *
 * Fills out the repeat timings and timing register values for a specific clock
 * rate.
 */
static void img_ir_decoder_convert(const struct img_ir_decoder *decoder,
				   struct img_ir_reg_timings *reg_timings,
				   unsigned int clock_hz)
{
	/* calculate control value */
	reg_timings->ctrl = img_ir_control(&decoder->control);

	/* fill in implicit fields and calculate register values */
	img_ir_timings_convert(&reg_timings->timings, &decoder->timings,
			       decoder->tolerance, clock_hz);

	/* do the same for repeat timings if applicable */
	if (decoder->repeat)
		img_ir_timings_convert(&reg_timings->rtimings,
				       &decoder->rtimings, decoder->tolerance,
				       clock_hz);
}

/**
 * img_ir_write_timings() - Write timings to the hardware now
 * @priv:	IR private data
 * @regs:	Timing register values to write
 * @type:	RC filter type (RC_FILTER_*)
 *
 * Write timing register values @regs to the hardware, taking into account the
 * current filter which may impose restrictions on the length of the expected
 * data.
 */
static void img_ir_write_timings(struct img_ir_priv *priv,
				 struct img_ir_timing_regvals *regs,
				 enum rc_filter_type type)
{
	struct img_ir_priv_hw *hw = &priv->hw;

	/* filter may be more restrictive to minlen, maxlen */
	u32 ft = regs->ft;
	if (hw->flags & BIT(type))
		ft = img_ir_free_timing_dynamic(regs->ft, &hw->filters[type]);
	/* write to registers */
	img_ir_write(priv, IMG_IR_LEAD_SYMB_TIMING, regs->ldr);
	img_ir_write(priv, IMG_IR_S00_SYMB_TIMING, regs->s00);
	img_ir_write(priv, IMG_IR_S01_SYMB_TIMING, regs->s01);
	img_ir_write(priv, IMG_IR_S10_SYMB_TIMING, regs->s10);
	img_ir_write(priv, IMG_IR_S11_SYMB_TIMING, regs->s11);
	img_ir_write(priv, IMG_IR_FREE_SYMB_TIMING, ft);
	dev_dbg(priv->dev, "timings: ldr=%#x, s=[%#x, %#x, %#x, %#x], ft=%#x\n",
		regs->ldr, regs->s00, regs->s01, regs->s10, regs->s11, ft);
}

static void img_ir_write_filter(struct img_ir_priv *priv,
				struct img_ir_filter *filter)
{
	if (filter) {
		dev_dbg(priv->dev, "IR filter=%016llx & %016llx\n",
			(unsigned long long)filter->data,
			(unsigned long long)filter->mask);
		img_ir_write(priv, IMG_IR_IRQ_MSG_DATA_LW, (u32)filter->data);
		img_ir_write(priv, IMG_IR_IRQ_MSG_DATA_UP, (u32)(filter->data
									>> 32));
		img_ir_write(priv, IMG_IR_IRQ_MSG_MASK_LW, (u32)filter->mask);
		img_ir_write(priv, IMG_IR_IRQ_MSG_MASK_UP, (u32)(filter->mask
									>> 32));
	} else {
		dev_dbg(priv->dev, "IR clearing filter\n");
		img_ir_write(priv, IMG_IR_IRQ_MSG_MASK_LW, 0);
		img_ir_write(priv, IMG_IR_IRQ_MSG_MASK_UP, 0);
	}
}

/* caller must have lock */
static void _img_ir_set_filter(struct img_ir_priv *priv,
			       struct img_ir_filter *filter)
{
	struct img_ir_priv_hw *hw = &priv->hw;
	u32 irq_en, irq_on;

	irq_en = img_ir_read(priv, IMG_IR_IRQ_ENABLE);
	if (filter) {
		/* Only use the match interrupt */
		hw->filters[RC_FILTER_NORMAL] = *filter;
		hw->flags |= IMG_IR_F_FILTER;
		irq_on = IMG_IR_IRQ_DATA_MATCH;
		irq_en &= ~(IMG_IR_IRQ_DATA_VALID | IMG_IR_IRQ_DATA2_VALID);
	} else {
		/* Only use the valid interrupt */
		hw->flags &= ~IMG_IR_F_FILTER;
		irq_en &= ~IMG_IR_IRQ_DATA_MATCH;
		irq_on = IMG_IR_IRQ_DATA_VALID | IMG_IR_IRQ_DATA2_VALID;
	}
	irq_en |= irq_on;

	img_ir_write_filter(priv, filter);
	/* clear any interrupts we're enabling so we don't handle old ones */
	img_ir_write(priv, IMG_IR_IRQ_CLEAR, irq_on);
	img_ir_write(priv, IMG_IR_IRQ_ENABLE, irq_en);
}

/* caller must have lock */
static void _img_ir_set_wake_filter(struct img_ir_priv *priv,
				    struct img_ir_filter *filter)
{
	struct img_ir_priv_hw *hw = &priv->hw;
	if (filter) {
		/* Enable wake, and copy filter for later */
		hw->filters[RC_FILTER_WAKEUP] = *filter;
		hw->flags |= IMG_IR_F_WAKE;
	} else {
		/* Disable wake */
		hw->flags &= ~IMG_IR_F_WAKE;
	}
}

/* Callback for setting scancode filter */
static int img_ir_set_filter(struct rc_dev *dev, enum rc_filter_type type,
			     struct rc_scancode_filter *sc_filter)
{
	struct img_ir_priv *priv = dev->priv;
	struct img_ir_priv_hw *hw = &priv->hw;
	struct img_ir_filter filter, *filter_ptr = &filter;
	int ret = 0;

	dev_dbg(priv->dev, "IR scancode %sfilter=%08x & %08x\n",
		type == RC_FILTER_WAKEUP ? "wake " : "",
		sc_filter->data,
		sc_filter->mask);

	spin_lock_irq(&priv->lock);

	/* filtering can always be disabled */
	if (!sc_filter->mask) {
		filter_ptr = NULL;
		goto set_unlock;
	}

	/* current decoder must support scancode filtering */
	if (!hw->decoder || !hw->decoder->filter) {
		ret = -EINVAL;
		goto unlock;
	}

	/* convert scancode filter to raw filter */
	filter.minlen = 0;
	filter.maxlen = ~0;
	if (type == RC_FILTER_NORMAL) {
		/* guess scancode from protocol */
		ret = hw->decoder->filter(sc_filter, &filter,
					  dev->enabled_protocols);
	} else {
		/* for wakeup user provided exact protocol variant */
		ret = hw->decoder->filter(sc_filter, &filter,
					  1ULL << dev->wakeup_protocol);
	}
	if (ret)
		goto unlock;
	dev_dbg(priv->dev, "IR raw %sfilter=%016llx & %016llx\n",
		type == RC_FILTER_WAKEUP ? "wake " : "",
		(unsigned long long)filter.data,
		(unsigned long long)filter.mask);

set_unlock:
	/* apply raw filters */
	switch (type) {
	case RC_FILTER_NORMAL:
		_img_ir_set_filter(priv, filter_ptr);
		break;
	case RC_FILTER_WAKEUP:
		_img_ir_set_wake_filter(priv, filter_ptr);
		break;
	default:
		ret = -EINVAL;
	}

unlock:
	spin_unlock_irq(&priv->lock);
	return ret;
}

static int img_ir_set_normal_filter(struct rc_dev *dev,
				    struct rc_scancode_filter *sc_filter)
{
	return img_ir_set_filter(dev, RC_FILTER_NORMAL, sc_filter);
}

static int img_ir_set_wakeup_filter(struct rc_dev *dev,
				    struct rc_scancode_filter *sc_filter)
{
	return img_ir_set_filter(dev, RC_FILTER_WAKEUP, sc_filter);
}

/**
 * img_ir_set_decoder() - Set the current decoder.
 * @priv:	IR private data.
 * @decoder:	Decoder to use with immediate effect.
 * @proto:	Protocol bitmap (or 0 to use decoder->type).
 */
static void img_ir_set_decoder(struct img_ir_priv *priv,
			       const struct img_ir_decoder *decoder,
			       u64 proto)
{
	struct img_ir_priv_hw *hw = &priv->hw;
	struct rc_dev *rdev = hw->rdev;
	u32 ir_status, irq_en;
	spin_lock_irq(&priv->lock);

	/*
	 * First record that the protocol is being stopped so that the end timer
	 * isn't restarted while we're trying to stop it.
	 */
	hw->stopping = true;

	/*
	 * Release the lock to stop the end timer, since the end timer handler
	 * acquires the lock and we don't want to deadlock waiting for it.
	 */
	spin_unlock_irq(&priv->lock);
	del_timer_sync(&hw->end_timer);
	del_timer_sync(&hw->suspend_timer);
	spin_lock_irq(&priv->lock);

	hw->stopping = false;

	/* switch off and disable interrupts */
	img_ir_write(priv, IMG_IR_CONTROL, 0);
	irq_en = img_ir_read(priv, IMG_IR_IRQ_ENABLE);
	img_ir_write(priv, IMG_IR_IRQ_ENABLE, irq_en & IMG_IR_IRQ_EDGE);
	img_ir_write(priv, IMG_IR_IRQ_CLEAR, IMG_IR_IRQ_ALL & ~IMG_IR_IRQ_EDGE);

	/* ack any data already detected */
	ir_status = img_ir_read(priv, IMG_IR_STATUS);
	if (ir_status & (IMG_IR_RXDVAL | IMG_IR_RXDVALD2)) {
		ir_status &= ~(IMG_IR_RXDVAL | IMG_IR_RXDVALD2);
		img_ir_write(priv, IMG_IR_STATUS, ir_status);
	}

	/* always read data to clear buffer if IR wakes the device */
	img_ir_read(priv, IMG_IR_DATA_LW);
	img_ir_read(priv, IMG_IR_DATA_UP);

	/* switch back to normal mode */
	hw->mode = IMG_IR_M_NORMAL;

	/* clear the wakeup scancode filter */
	rdev->scancode_wakeup_filter.data = 0;
	rdev->scancode_wakeup_filter.mask = 0;
	rdev->wakeup_protocol = RC_PROTO_UNKNOWN;

	/* clear raw filters */
	_img_ir_set_filter(priv, NULL);
	_img_ir_set_wake_filter(priv, NULL);

	/* clear the enabled protocols */
	hw->enabled_protocols = 0;

	/* switch decoder */
	hw->decoder = decoder;
	if (!decoder)
		goto unlock;

	/* set the enabled protocols */
	if (!proto)
		proto = decoder->type;
	hw->enabled_protocols = proto;

	/* write the new timings */
	img_ir_decoder_convert(decoder, &hw->reg_timings, hw->clk_hz);
	img_ir_write_timings(priv, &hw->reg_timings.timings, RC_FILTER_NORMAL);

	/* set up and enable */
	img_ir_write(priv, IMG_IR_CONTROL, hw->reg_timings.ctrl);


unlock:
	spin_unlock_irq(&priv->lock);
}

/**
 * img_ir_decoder_compatable() - Find whether a decoder will work with a device.
 * @priv:	IR private data.
 * @dec:	Decoder to check.
 *
 * Returns:	true if @dec is compatible with the device @priv refers to.
 */
static bool img_ir_decoder_compatible(struct img_ir_priv *priv,
				      const struct img_ir_decoder *dec)
{
	unsigned int ct;

	/* don't accept decoders using code types which aren't supported */
	ct = dec->control.code_type;
	if (priv->hw.ct_quirks[ct] & IMG_IR_QUIRK_CODE_BROKEN)
		return false;

	return true;
}

/**
 * img_ir_allowed_protos() - Get allowed protocols from global decoder list.
 * @priv:	IR private data.
 *
 * Returns:	Mask of protocols supported by the device @priv refers to.
 */
static u64 img_ir_allowed_protos(struct img_ir_priv *priv)
{
	u64 protos = 0;
	struct img_ir_decoder **decp;

	for (decp = img_ir_decoders; *decp; ++decp) {
		const struct img_ir_decoder *dec = *decp;
		if (img_ir_decoder_compatible(priv, dec))
			protos |= dec->type;
	}
	return protos;
}

/* Callback for changing protocol using sysfs */
static int img_ir_change_protocol(struct rc_dev *dev, u64 *ir_type)
{
	struct img_ir_priv *priv = dev->priv;
	struct img_ir_priv_hw *hw = &priv->hw;
	struct rc_dev *rdev = hw->rdev;
	struct img_ir_decoder **decp;
	u64 wakeup_protocols;

	if (!*ir_type) {
		/* disable all protocols */
		img_ir_set_decoder(priv, NULL, 0);
		goto success;
	}
	for (decp = img_ir_decoders; *decp; ++decp) {
		const struct img_ir_decoder *dec = *decp;
		if (!img_ir_decoder_compatible(priv, dec))
			continue;
		if (*ir_type & dec->type) {
			*ir_type &= dec->type;
			img_ir_set_decoder(priv, dec, *ir_type);
			goto success;
		}
	}
	return -EINVAL;

success:
	/*
	 * Only allow matching wakeup protocols for now, and only if filtering
	 * is supported.
	 */
	wakeup_protocols = *ir_type;
	if (!hw->decoder || !hw->decoder->filter)
		wakeup_protocols = 0;
	rdev->allowed_wakeup_protocols = wakeup_protocols;
	return 0;
}

/* Changes ir-core protocol device attribute */
static void img_ir_set_protocol(struct img_ir_priv *priv, u64 proto)
{
	struct rc_dev *rdev = priv->hw.rdev;

	mutex_lock(&rdev->lock);
	rdev->enabled_protocols = proto;
	rdev->allowed_wakeup_protocols = proto;
	mutex_unlock(&rdev->lock);
}

/* Set up IR decoders */
static void img_ir_init_decoders(void)
{
	struct img_ir_decoder **decp;

	spin_lock(&img_ir_decoders_lock);
	if (!img_ir_decoders_preprocessed) {
		for (decp = img_ir_decoders; *decp; ++decp)
			img_ir_decoder_preprocess(*decp);
		img_ir_decoders_preprocessed = true;
	}
	spin_unlock(&img_ir_decoders_lock);
}

#ifdef CONFIG_PM_SLEEP
/**
 * img_ir_enable_wake() - Switch to wake mode.
 * @priv:	IR private data.
 *
 * Returns:	non-zero if the IR can wake the system.
 */
static int img_ir_enable_wake(struct img_ir_priv *priv)
{
	struct img_ir_priv_hw *hw = &priv->hw;
	int ret = 0;

	spin_lock_irq(&priv->lock);
	if (hw->flags & IMG_IR_F_WAKE) {
		/* interrupt only on a match */
		hw->suspend_irqen = img_ir_read(priv, IMG_IR_IRQ_ENABLE);
		img_ir_write(priv, IMG_IR_IRQ_ENABLE, IMG_IR_IRQ_DATA_MATCH);
		img_ir_write_filter(priv, &hw->filters[RC_FILTER_WAKEUP]);
		img_ir_write_timings(priv, &hw->reg_timings.timings,
				     RC_FILTER_WAKEUP);
		hw->mode = IMG_IR_M_WAKE;
		ret = 1;
	}
	spin_unlock_irq(&priv->lock);
	return ret;
}

/**
 * img_ir_disable_wake() - Switch out of wake mode.
 * @priv:	IR private data
 *
 * Returns:	1 if the hardware should be allowed to wake from a sleep state.
 *		0 otherwise.
 */
static int img_ir_disable_wake(struct img_ir_priv *priv)
{
	struct img_ir_priv_hw *hw = &priv->hw;
	int ret = 0;

	spin_lock_irq(&priv->lock);
	if (hw->flags & IMG_IR_F_WAKE) {
		/* restore normal filtering */
		if (hw->flags & IMG_IR_F_FILTER) {
			img_ir_write(priv, IMG_IR_IRQ_ENABLE,
				     (hw->suspend_irqen & IMG_IR_IRQ_EDGE) |
				     IMG_IR_IRQ_DATA_MATCH);
			img_ir_write_filter(priv,
					    &hw->filters[RC_FILTER_NORMAL]);
		} else {
			img_ir_write(priv, IMG_IR_IRQ_ENABLE,
				     (hw->suspend_irqen & IMG_IR_IRQ_EDGE) |
				     IMG_IR_IRQ_DATA_VALID |
				     IMG_IR_IRQ_DATA2_VALID);
			img_ir_write_filter(priv, NULL);
		}
		img_ir_write_timings(priv, &hw->reg_timings.timings,
				     RC_FILTER_NORMAL);
		hw->mode = IMG_IR_M_NORMAL;
		ret = 1;
	}
	spin_unlock_irq(&priv->lock);
	return ret;
}
#endif /* CONFIG_PM_SLEEP */

/* lock must be held */
static void img_ir_begin_repeat(struct img_ir_priv *priv)
{
	struct img_ir_priv_hw *hw = &priv->hw;
	if (hw->mode == IMG_IR_M_NORMAL) {
		/* switch to repeat timings */
		img_ir_write(priv, IMG_IR_CONTROL, 0);
		hw->mode = IMG_IR_M_REPEATING;
		img_ir_write_timings(priv, &hw->reg_timings.rtimings,
				     RC_FILTER_NORMAL);
		img_ir_write(priv, IMG_IR_CONTROL, hw->reg_timings.ctrl);
	}
}

/* lock must be held */
static void img_ir_end_repeat(struct img_ir_priv *priv)
{
	struct img_ir_priv_hw *hw = &priv->hw;
	if (hw->mode == IMG_IR_M_REPEATING) {
		/* switch to normal timings */
		img_ir_write(priv, IMG_IR_CONTROL, 0);
		hw->mode = IMG_IR_M_NORMAL;
		img_ir_write_timings(priv, &hw->reg_timings.timings,
				     RC_FILTER_NORMAL);
		img_ir_write(priv, IMG_IR_CONTROL, hw->reg_timings.ctrl);
	}
}

/* lock must be held */
static void img_ir_handle_data(struct img_ir_priv *priv, u32 len, u64 raw)
{
	struct img_ir_priv_hw *hw = &priv->hw;
	const struct img_ir_decoder *dec = hw->decoder;
	int ret = IMG_IR_SCANCODE;
	struct img_ir_scancode_req request;

	request.protocol = RC_PROTO_UNKNOWN;
	request.toggle   = 0;

	if (dec->scancode)
		ret = dec->scancode(len, raw, hw->enabled_protocols, &request);
	else if (len >= 32)
		request.scancode = (u32)raw;
	else if (len < 32)
		request.scancode = (u32)raw & ((1 << len)-1);
	dev_dbg(priv->dev, "data (%u bits) = %#llx\n",
		len, (unsigned long long)raw);
	if (ret == IMG_IR_SCANCODE) {
		dev_dbg(priv->dev, "decoded scan code %#x, toggle %u\n",
			request.scancode, request.toggle);
		rc_keydown(hw->rdev, request.protocol, request.scancode,
			   request.toggle);
		img_ir_end_repeat(priv);
	} else if (ret == IMG_IR_REPEATCODE) {
		if (hw->mode == IMG_IR_M_REPEATING) {
			dev_dbg(priv->dev, "decoded repeat code\n");
			rc_repeat(hw->rdev);
		} else {
			dev_dbg(priv->dev, "decoded unexpected repeat code, ignoring\n");
		}
	} else {
		dev_dbg(priv->dev, "decode failed (%d)\n", ret);
		return;
	}


	/* we mustn't update the end timer while trying to stop it */
	if (dec->repeat && !hw->stopping) {
		unsigned long interval;

		img_ir_begin_repeat(priv);

		/* update timer, but allowing for 1/8th tolerance */
		interval = dec->repeat + (dec->repeat >> 3);
		mod_timer(&hw->end_timer,
			  jiffies + msecs_to_jiffies(interval));
	}
}

/* timer function to end waiting for repeat. */
static void img_ir_end_timer(struct timer_list *t)
{
	struct img_ir_priv *priv = from_timer(priv, t, hw.end_timer);

	spin_lock_irq(&priv->lock);
	img_ir_end_repeat(priv);
	spin_unlock_irq(&priv->lock);
}

/*
 * Timer function to re-enable the current protocol after it had been
 * cleared when invalid interrupts were generated due to a quirk in the
 * img-ir decoder.
 */
static void img_ir_suspend_timer(struct timer_list *t)
{
	struct img_ir_priv *priv = from_timer(priv, t, hw.suspend_timer);

	spin_lock_irq(&priv->lock);
	/*
	 * Don't overwrite enabled valid/match IRQs if they have already been
	 * changed by e.g. a filter change.
	 */
	if ((priv->hw.quirk_suspend_irq & IMG_IR_IRQ_EDGE) ==
				img_ir_read(priv, IMG_IR_IRQ_ENABLE))
		img_ir_write(priv, IMG_IR_IRQ_ENABLE,
					priv->hw.quirk_suspend_irq);
	/* enable */
	img_ir_write(priv, IMG_IR_CONTROL, priv->hw.reg_timings.ctrl);
	spin_unlock_irq(&priv->lock);
}

#ifdef CONFIG_COMMON_CLK
static void img_ir_change_frequency(struct img_ir_priv *priv,
				    struct clk_notifier_data *change)
{
	struct img_ir_priv_hw *hw = &priv->hw;

	dev_dbg(priv->dev, "clk changed %lu HZ -> %lu HZ\n",
		change->old_rate, change->new_rate);

	spin_lock_irq(&priv->lock);
	if (hw->clk_hz == change->new_rate)
		goto unlock;
	hw->clk_hz = change->new_rate;
	/* refresh current timings */
	if (hw->decoder) {
		img_ir_decoder_convert(hw->decoder, &hw->reg_timings,
				       hw->clk_hz);
		switch (hw->mode) {
		case IMG_IR_M_NORMAL:
			img_ir_write_timings(priv, &hw->reg_timings.timings,
					     RC_FILTER_NORMAL);
			break;
		case IMG_IR_M_REPEATING:
			img_ir_write_timings(priv, &hw->reg_timings.rtimings,
					     RC_FILTER_NORMAL);
			break;
#ifdef CONFIG_PM_SLEEP
		case IMG_IR_M_WAKE:
			img_ir_write_timings(priv, &hw->reg_timings.timings,
					     RC_FILTER_WAKEUP);
			break;
#endif
		}
	}
unlock:
	spin_unlock_irq(&priv->lock);
}

static int img_ir_clk_notify(struct notifier_block *self, unsigned long action,
			     void *data)
{
	struct img_ir_priv *priv = container_of(self, struct img_ir_priv,
						hw.clk_nb);
	switch (action) {
	case POST_RATE_CHANGE:
		img_ir_change_frequency(priv, data);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}
#endif /* CONFIG_COMMON_CLK */

/* called with priv->lock held */
void img_ir_isr_hw(struct img_ir_priv *priv, u32 irq_status)
{
	struct img_ir_priv_hw *hw = &priv->hw;
	u32 ir_status, len, lw, up;
	unsigned int ct;

	/* use the current decoder */
	if (!hw->decoder)
		return;

	ct = hw->decoder->control.code_type;

	ir_status = img_ir_read(priv, IMG_IR_STATUS);
	if (!(ir_status & (IMG_IR_RXDVAL | IMG_IR_RXDVALD2))) {
		if (!(priv->hw.ct_quirks[ct] & IMG_IR_QUIRK_CODE_IRQ) ||
				hw->stopping)
			return;
		/*
		 * The below functionality is added as a work around to stop
		 * multiple Interrupts generated when an incomplete IR code is
		 * received by the decoder.
		 * The decoder generates rapid interrupts without actually
		 * having received any new data. After a single interrupt it's
		 * expected to clear up, but instead multiple interrupts are
		 * rapidly generated. only way to get out of this loop is to
		 * reset the control register after a short delay.
		 */
		img_ir_write(priv, IMG_IR_CONTROL, 0);
		hw->quirk_suspend_irq = img_ir_read(priv, IMG_IR_IRQ_ENABLE);
		img_ir_write(priv, IMG_IR_IRQ_ENABLE,
			     hw->quirk_suspend_irq & IMG_IR_IRQ_EDGE);

		/* Timer activated to re-enable the protocol. */
		mod_timer(&hw->suspend_timer,
			  jiffies + msecs_to_jiffies(5));
		return;
	}
	ir_status &= ~(IMG_IR_RXDVAL | IMG_IR_RXDVALD2);
	img_ir_write(priv, IMG_IR_STATUS, ir_status);

	len = (ir_status & IMG_IR_RXDLEN) >> IMG_IR_RXDLEN_SHIFT;
	/* some versions report wrong length for certain code types */
	if (hw->ct_quirks[ct] & IMG_IR_QUIRK_CODE_LEN_INCR)
		++len;

	lw = img_ir_read(priv, IMG_IR_DATA_LW);
	up = img_ir_read(priv, IMG_IR_DATA_UP);
	img_ir_handle_data(priv, len, (u64)up << 32 | lw);
}

void img_ir_setup_hw(struct img_ir_priv *priv)
{
	struct img_ir_decoder **decp;

	if (!priv->hw.rdev)
		return;

	/* Use the first available decoder (or disable stuff if NULL) */
	for (decp = img_ir_decoders; *decp; ++decp) {
		const struct img_ir_decoder *dec = *decp;
		if (img_ir_decoder_compatible(priv, dec)) {
			img_ir_set_protocol(priv, dec->type);
			img_ir_set_decoder(priv, dec, 0);
			return;
		}
	}
	img_ir_set_decoder(priv, NULL, 0);
}

/**
 * img_ir_probe_hw_caps() - Probe capabilities of the hardware.
 * @priv:	IR private data.
 */
static void img_ir_probe_hw_caps(struct img_ir_priv *priv)
{
	struct img_ir_priv_hw *hw = &priv->hw;
	/*
	 * When a version of the block becomes available without these quirks,
	 * they'll have to depend on the core revision.
	 */
	hw->ct_quirks[IMG_IR_CODETYPE_PULSELEN]
		|= IMG_IR_QUIRK_CODE_LEN_INCR;
	hw->ct_quirks[IMG_IR_CODETYPE_BIPHASE]
		|= IMG_IR_QUIRK_CODE_IRQ;
	hw->ct_quirks[IMG_IR_CODETYPE_2BITPULSEPOS]
		|= IMG_IR_QUIRK_CODE_BROKEN;
}

int img_ir_probe_hw(struct img_ir_priv *priv)
{
	struct img_ir_priv_hw *hw = &priv->hw;
	struct rc_dev *rdev;
	int error;

	/* Ensure hardware decoders have been preprocessed */
	img_ir_init_decoders();

	/* Probe hardware capabilities */
	img_ir_probe_hw_caps(priv);

	/* Set up the end timer */
	timer_setup(&hw->end_timer, img_ir_end_timer, 0);
	timer_setup(&hw->suspend_timer, img_ir_suspend_timer, 0);

	/* Register a clock notifier */
	if (!IS_ERR(priv->clk)) {
		hw->clk_hz = clk_get_rate(priv->clk);
#ifdef CONFIG_COMMON_CLK
		hw->clk_nb.notifier_call = img_ir_clk_notify;
		error = clk_notifier_register(priv->clk, &hw->clk_nb);
		if (error)
			dev_warn(priv->dev,
				 "failed to register clock notifier\n");
#endif
	} else {
		hw->clk_hz = 32768;
	}

	/* Allocate hardware decoder */
	hw->rdev = rdev = rc_allocate_device(RC_DRIVER_SCANCODE);
	if (!rdev) {
		dev_err(priv->dev, "cannot allocate input device\n");
		error = -ENOMEM;
		goto err_alloc_rc;
	}
	rdev->priv = priv;
	rdev->map_name = RC_MAP_EMPTY;
	rdev->allowed_protocols = img_ir_allowed_protos(priv);
	rdev->device_name = "IMG Infrared Decoder";
	rdev->s_filter = img_ir_set_normal_filter;
	rdev->s_wakeup_filter = img_ir_set_wakeup_filter;

	/* Register hardware decoder */
	error = rc_register_device(rdev);
	if (error) {
		dev_err(priv->dev, "failed to register IR input device\n");
		goto err_register_rc;
	}

	/*
	 * Set this after rc_register_device as no protocols have been
	 * registered yet.
	 */
	rdev->change_protocol = img_ir_change_protocol;

	device_init_wakeup(priv->dev, 1);

	return 0;

err_register_rc:
	img_ir_set_decoder(priv, NULL, 0);
	hw->rdev = NULL;
	rc_free_device(rdev);
err_alloc_rc:
#ifdef CONFIG_COMMON_CLK
	if (!IS_ERR(priv->clk))
		clk_notifier_unregister(priv->clk, &hw->clk_nb);
#endif
	return error;
}

void img_ir_remove_hw(struct img_ir_priv *priv)
{
	struct img_ir_priv_hw *hw = &priv->hw;
	struct rc_dev *rdev = hw->rdev;
	if (!rdev)
		return;
	img_ir_set_decoder(priv, NULL, 0);
	hw->rdev = NULL;
	rc_unregister_device(rdev);
#ifdef CONFIG_COMMON_CLK
	if (!IS_ERR(priv->clk))
		clk_notifier_unregister(priv->clk, &hw->clk_nb);
#endif
}

#ifdef CONFIG_PM_SLEEP
int img_ir_suspend(struct device *dev)
{
	struct img_ir_priv *priv = dev_get_drvdata(dev);

	if (device_may_wakeup(dev) && img_ir_enable_wake(priv))
		enable_irq_wake(priv->irq);
	return 0;
}

int img_ir_resume(struct device *dev)
{
	struct img_ir_priv *priv = dev_get_drvdata(dev);

	if (device_may_wakeup(dev) && img_ir_disable_wake(priv))
		disable_irq_wake(priv->irq);
	return 0;
}
#endif	/* CONFIG_PM_SLEEP */
