/*
 * ImgTec IR Hardware Decoder found in PowerDown Controller.
 *
 * Copyright 2010-2014 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef _IMG_IR_HW_H_
#define _IMG_IR_HW_H_

#include <linux/kernel.h>
#include <media/rc-core.h>

/* constants */

#define IMG_IR_CODETYPE_PULSELEN	0x0	/* Sony */
#define IMG_IR_CODETYPE_PULSEDIST	0x1	/* NEC, Toshiba, Micom, Sharp */
#define IMG_IR_CODETYPE_BIPHASE		0x2	/* RC-5/6 */
#define IMG_IR_CODETYPE_2BITPULSEPOS	0x3	/* RC-MM */


/* Timing information */

/**
 * struct img_ir_control - Decoder control settings
 * @decoden:	Primary decoder enable
 * @code_type:	Decode type (see IMG_IR_CODETYPE_*)
 * @hdrtog:	Detect header toggle symbol after leader symbol
 * @ldrdec:	Don't discard leader if maximum width reached
 * @decodinpol:	Decoder input polarity (1=active high)
 * @bitorien:	Bit orientation (1=MSB first)
 * @d1validsel:	Decoder 2 takes over if it detects valid data
 * @bitinv:	Bit inversion switch (1=don't invert)
 * @decodend2:	Secondary decoder enable (no leader symbol)
 * @bitoriend2:	Bit orientation (1=MSB first)
 * @bitinvd2:	Secondary decoder bit inversion switch (1=don't invert)
 */
struct img_ir_control {
	unsigned decoden:1;
	unsigned code_type:2;
	unsigned hdrtog:1;
	unsigned ldrdec:1;
	unsigned decodinpol:1;
	unsigned bitorien:1;
	unsigned d1validsel:1;
	unsigned bitinv:1;
	unsigned decodend2:1;
	unsigned bitoriend2:1;
	unsigned bitinvd2:1;
};

/**
 * struct img_ir_timing_range - range of timing values
 * @min:	Minimum timing value
 * @max:	Maximum timing value (if < @min, this will be set to @min during
 *		preprocessing step, so it is normally not explicitly initialised
 *		and is taken care of by the tolerance)
 */
struct img_ir_timing_range {
	u16 min;
	u16 max;
};

/**
 * struct img_ir_symbol_timing - timing data for a symbol
 * @pulse:	Timing range for the length of the pulse in this symbol
 * @space:	Timing range for the length of the space in this symbol
 */
struct img_ir_symbol_timing {
	struct img_ir_timing_range pulse;
	struct img_ir_timing_range space;
};

/**
 * struct img_ir_free_timing - timing data for free time symbol
 * @minlen:	Minimum number of bits of data
 * @maxlen:	Maximum number of bits of data
 * @ft_min:	Minimum free time after message
 */
struct img_ir_free_timing {
	/* measured in bits */
	u8 minlen;
	u8 maxlen;
	u16 ft_min;
};

/**
 * struct img_ir_timings - Timing values.
 * @ldr:	Leader symbol timing data
 * @s00:	Zero symbol timing data for primary decoder
 * @s01:	One symbol timing data for primary decoder
 * @s10:	Zero symbol timing data for secondary (no leader symbol) decoder
 * @s11:	One symbol timing data for secondary (no leader symbol) decoder
 * @ft:		Free time symbol timing data
 */
struct img_ir_timings {
	struct img_ir_symbol_timing ldr, s00, s01, s10, s11;
	struct img_ir_free_timing ft;
};

/**
 * struct img_ir_filter - Filter IR events.
 * @data:	Data to match.
 * @mask:	Mask of bits to compare.
 * @minlen:	Additional minimum number of bits.
 * @maxlen:	Additional maximum number of bits.
 */
struct img_ir_filter {
	u64 data;
	u64 mask;
	u8 minlen;
	u8 maxlen;
};

/**
 * struct img_ir_timing_regvals - Calculated timing register values.
 * @ldr:	Leader symbol timing register value
 * @s00:	Zero symbol timing register value for primary decoder
 * @s01:	One symbol timing register value for primary decoder
 * @s10:	Zero symbol timing register value for secondary decoder
 * @s11:	One symbol timing register value for secondary decoder
 * @ft:		Free time symbol timing register value
 */
struct img_ir_timing_regvals {
	u32 ldr, s00, s01, s10, s11, ft;
};

#define IMG_IR_SCANCODE		0	/* new scancode */
#define IMG_IR_REPEATCODE	1	/* repeat the previous code */

/**
 * struct img_ir_decoder - Decoder settings for an IR protocol.
 * @type:	Protocol types bitmap.
 * @tolerance:	Timing tolerance as a percentage (default 10%).
 * @unit:	Unit of timings in nanoseconds (default 1 us).
 * @timings:	Primary timings
 * @rtimings:	Additional override timings while waiting for repeats.
 * @repeat:	Maximum repeat interval (always in milliseconds).
 * @control:	Control flags.
 *
 * @scancode:	Pointer to function to convert the IR data into a scancode (it
 *		must be safe to execute in interrupt context).
 *		Returns IMG_IR_SCANCODE to emit new scancode.
 *		Returns IMG_IR_REPEATCODE to repeat previous code.
 *		Returns -errno (e.g. -EINVAL) on error.
 * @filter:	Pointer to function to convert scancode filter to raw hardware
 *		filter. The minlen and maxlen fields will have been initialised
 *		to the maximum range.
 */
struct img_ir_decoder {
	/* core description */
	u64				type;
	unsigned int			tolerance;
	unsigned int			unit;
	struct img_ir_timings		timings;
	struct img_ir_timings		rtimings;
	unsigned int			repeat;
	struct img_ir_control		control;

	/* scancode logic */
	int (*scancode)(int len, u64 raw, int *scancode, u64 protocols);
	int (*filter)(const struct rc_scancode_filter *in,
		      struct img_ir_filter *out, u64 protocols);
};

/**
 * struct img_ir_reg_timings - Reg values for decoder timings at clock rate.
 * @ctrl:	Processed control register value.
 * @timings:	Processed primary timings.
 * @rtimings:	Processed repeat timings.
 */
struct img_ir_reg_timings {
	u32				ctrl;
	struct img_ir_timing_regvals	timings;
	struct img_ir_timing_regvals	rtimings;
};

int img_ir_register_decoder(struct img_ir_decoder *dec);
void img_ir_unregister_decoder(struct img_ir_decoder *dec);

struct img_ir_priv;

#ifdef CONFIG_IR_IMG_HW

enum img_ir_mode {
	IMG_IR_M_NORMAL,
	IMG_IR_M_REPEATING,
#ifdef CONFIG_PM_SLEEP
	IMG_IR_M_WAKE,
#endif
};

/**
 * struct img_ir_priv_hw - Private driver data for hardware decoder.
 * @ct_quirks:		Quirk bits for each code type.
 * @rdev:		Remote control device
 * @clk_nb:		Notifier block for clock notify events.
 * @end_timer:		Timer until repeat timeout.
 * @decoder:		Current decoder settings.
 * @enabled_protocols:	Currently enabled protocols.
 * @clk_hz:		Current core clock rate in Hz.
 * @reg_timings:	Timing reg values for decoder at clock rate.
 * @flags:		IMG_IR_F_*.
 * @filters:		HW filters (derived from scancode filters).
 * @mode:		Current decode mode.
 * @suspend_irqen:	Saved IRQ enable mask over suspend.
 */
struct img_ir_priv_hw {
	unsigned int			ct_quirks[4];
	struct rc_dev			*rdev;
	struct notifier_block		clk_nb;
	struct timer_list		end_timer;
	const struct img_ir_decoder	*decoder;
	u64				enabled_protocols;
	unsigned long			clk_hz;
	struct img_ir_reg_timings	reg_timings;
	unsigned int			flags;
	struct img_ir_filter		filters[RC_FILTER_MAX];

	enum img_ir_mode		mode;
	u32				suspend_irqen;
};

static inline bool img_ir_hw_enabled(struct img_ir_priv_hw *hw)
{
	return hw->rdev;
};

void img_ir_isr_hw(struct img_ir_priv *priv, u32 irq_status);
void img_ir_setup_hw(struct img_ir_priv *priv);
int img_ir_probe_hw(struct img_ir_priv *priv);
void img_ir_remove_hw(struct img_ir_priv *priv);

#ifdef CONFIG_PM_SLEEP
int img_ir_suspend(struct device *dev);
int img_ir_resume(struct device *dev);
#else
#define img_ir_suspend NULL
#define img_ir_resume NULL
#endif

#else

struct img_ir_priv_hw {
};

static inline bool img_ir_hw_enabled(struct img_ir_priv_hw *hw)
{
	return false;
};
static inline void img_ir_isr_hw(struct img_ir_priv *priv, u32 irq_status)
{
}
static inline void img_ir_setup_hw(struct img_ir_priv *priv)
{
}
static inline int img_ir_probe_hw(struct img_ir_priv *priv)
{
	return -ENODEV;
}
static inline void img_ir_remove_hw(struct img_ir_priv *priv)
{
}

#define img_ir_suspend NULL
#define img_ir_resume NULL

#endif /* CONFIG_IR_IMG_HW */

#endif /* _IMG_IR_HW_H_ */
