/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_CORE_H
#define _ZL3073X_CORE_H

#include <linux/bitfield.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "regs.h"

struct device;
struct regmap;
struct zl3073x_dpll;

/*
 * Hardware limits for ZL3073x chip family
 */
#define ZL3073X_MAX_CHANNELS	5
#define ZL3073X_NUM_REFS	10
#define ZL3073X_NUM_OUTS	10
#define ZL3073X_NUM_SYNTHS	5
#define ZL3073X_NUM_INPUT_PINS	ZL3073X_NUM_REFS
#define ZL3073X_NUM_OUTPUT_PINS	(ZL3073X_NUM_OUTS * 2)
#define ZL3073X_NUM_PINS	(ZL3073X_NUM_INPUT_PINS + \
				 ZL3073X_NUM_OUTPUT_PINS)

/**
 * struct zl3073x_ref - input reference invariant info
 * @enabled: input reference is enabled or disabled
 * @diff: true if input reference is differential
 * @ffo: current fractional frequency offset
 */
struct zl3073x_ref {
	bool	enabled;
	bool	diff;
	s64	ffo;
};

/**
 * struct zl3073x_out - output invariant info
 * @enabled: out is enabled or disabled
 * @synth: synthesizer the out is connected to
 * @signal_format: out signal format
 */
struct zl3073x_out {
	bool	enabled;
	u8	synth;
	u8	signal_format;
};

/**
 * struct zl3073x_synth - synthesizer invariant info
 * @freq: synthesizer frequency
 * @dpll: ID of DPLL the synthesizer is driven by
 * @enabled: synth is enabled or disabled
 */
struct zl3073x_synth {
	u32	freq;
	u8	dpll;
	bool	enabled;
};

/**
 * struct zl3073x_dev - zl3073x device
 * @dev: pointer to device
 * @regmap: regmap to access device registers
 * @multiop_lock: to serialize multiple register operations
 * @ref: array of input references' invariants
 * @out: array of outs' invariants
 * @synth: array of synths' invariants
 * @dplls: list of DPLLs
 * @kworker: thread for periodic work
 * @work: periodic work
 * @clock_id: clock id of the device
 * @phase_avg_factor: phase offset measurement averaging factor
 */
struct zl3073x_dev {
	struct device		*dev;
	struct regmap		*regmap;
	struct mutex		multiop_lock;

	/* Invariants */
	struct zl3073x_ref	ref[ZL3073X_NUM_REFS];
	struct zl3073x_out	out[ZL3073X_NUM_OUTS];
	struct zl3073x_synth	synth[ZL3073X_NUM_SYNTHS];

	/* DPLL channels */
	struct list_head	dplls;

	/* Monitor */
	struct kthread_worker		*kworker;
	struct kthread_delayed_work	work;

	/* Devlink parameters */
	u64			clock_id;
	u8			phase_avg_factor;
};

struct zl3073x_chip_info {
	const u16	*ids;
	size_t		num_ids;
	int		num_channels;
};

extern const struct zl3073x_chip_info zl30731_chip_info;
extern const struct zl3073x_chip_info zl30732_chip_info;
extern const struct zl3073x_chip_info zl30733_chip_info;
extern const struct zl3073x_chip_info zl30734_chip_info;
extern const struct zl3073x_chip_info zl30735_chip_info;
extern const struct regmap_config zl3073x_regmap_config;

struct zl3073x_dev *zl3073x_devm_alloc(struct device *dev);
int zl3073x_dev_probe(struct zl3073x_dev *zldev,
		      const struct zl3073x_chip_info *chip_info);

int zl3073x_dev_start(struct zl3073x_dev *zldev, bool full);
void zl3073x_dev_stop(struct zl3073x_dev *zldev);

static inline u8 zl3073x_dev_phase_avg_factor_get(struct zl3073x_dev *zldev)
{
	return zldev->phase_avg_factor;
}

int zl3073x_dev_phase_avg_factor_set(struct zl3073x_dev *zldev, u8 factor);

/**********************
 * Registers operations
 **********************/

/**
 * struct zl3073x_hwreg_seq_item - HW register write sequence item
 * @addr: HW register to be written
 * @value: value to be written to HW register
 * @mask: bitmask indicating bits to be updated
 * @wait: number of ms to wait after register write
 */
struct zl3073x_hwreg_seq_item {
	u32	addr;
	u32	value;
	u32	mask;
	u32	wait;
};

#define HWREG_SEQ_ITEM(_addr, _value, _mask, _wait)	\
{							\
	.addr	= _addr,				\
	.value	= FIELD_PREP_CONST(_mask, _value),	\
	.mask	= _mask,				\
	.wait	= _wait,				\
}

int zl3073x_mb_op(struct zl3073x_dev *zldev, unsigned int op_reg, u8 op_val,
		  unsigned int mask_reg, u16 mask_val);
int zl3073x_poll_zero_u8(struct zl3073x_dev *zldev, unsigned int reg, u8 mask);
int zl3073x_read_u8(struct zl3073x_dev *zldev, unsigned int reg, u8 *val);
int zl3073x_read_u16(struct zl3073x_dev *zldev, unsigned int reg, u16 *val);
int zl3073x_read_u32(struct zl3073x_dev *zldev, unsigned int reg, u32 *val);
int zl3073x_read_u48(struct zl3073x_dev *zldev, unsigned int reg, u64 *val);
int zl3073x_write_u8(struct zl3073x_dev *zldev, unsigned int reg, u8 val);
int zl3073x_write_u16(struct zl3073x_dev *zldev, unsigned int reg, u16 val);
int zl3073x_write_u32(struct zl3073x_dev *zldev, unsigned int reg, u32 val);
int zl3073x_write_u48(struct zl3073x_dev *zldev, unsigned int reg, u64 val);
int zl3073x_read_hwreg(struct zl3073x_dev *zldev, u32 addr, u32 *value);
int zl3073x_write_hwreg(struct zl3073x_dev *zldev, u32 addr, u32 value);
int zl3073x_update_hwreg(struct zl3073x_dev *zldev, u32 addr, u32 value,
			 u32 mask);
int zl3073x_write_hwreg_seq(struct zl3073x_dev *zldev,
			    const struct zl3073x_hwreg_seq_item *seq,
			    size_t num_items);

/*****************
 * Misc operations
 *****************/

int zl3073x_ref_freq_factorize(u32 freq, u16 *base, u16 *mult);
int zl3073x_ref_phase_offsets_update(struct zl3073x_dev *zldev, int channel);

static inline bool
zl3073x_is_n_pin(u8 id)
{
	/* P-pins ids are even while N-pins are odd */
	return id & 1;
}

static inline bool
zl3073x_is_p_pin(u8 id)
{
	return !zl3073x_is_n_pin(id);
}

/**
 * zl3073x_input_pin_ref_get - get reference for given input pin
 * @id: input pin id
 *
 * Return: reference id for the given input pin
 */
static inline u8
zl3073x_input_pin_ref_get(u8 id)
{
	return id;
}

/**
 * zl3073x_output_pin_out_get - get output for the given output pin
 * @id: output pin id
 *
 * Return: output id for the given output pin
 */
static inline u8
zl3073x_output_pin_out_get(u8 id)
{
	/* Output pin pair shares the single output */
	return id / 2;
}

/**
 * zl3073x_ref_ffo_get - get current fractional frequency offset
 * @zldev: pointer to zl3073x device
 * @index: input reference index
 *
 * Return: the latest measured fractional frequency offset
 */
static inline s64
zl3073x_ref_ffo_get(struct zl3073x_dev *zldev, u8 index)
{
	return zldev->ref[index].ffo;
}

/**
 * zl3073x_ref_is_diff - check if the given input reference is differential
 * @zldev: pointer to zl3073x device
 * @index: input reference index
 *
 * Return: true if reference is differential, false if reference is single-ended
 */
static inline bool
zl3073x_ref_is_diff(struct zl3073x_dev *zldev, u8 index)
{
	return zldev->ref[index].diff;
}

/**
 * zl3073x_ref_is_enabled - check if the given input reference is enabled
 * @zldev: pointer to zl3073x device
 * @index: input reference index
 *
 * Return: true if input refernce is enabled, false otherwise
 */
static inline bool
zl3073x_ref_is_enabled(struct zl3073x_dev *zldev, u8 index)
{
	return zldev->ref[index].enabled;
}

/**
 * zl3073x_synth_dpll_get - get DPLL ID the synth is driven by
 * @zldev: pointer to zl3073x device
 * @index: synth index
 *
 * Return: ID of DPLL the given synthetizer is driven by
 */
static inline u8
zl3073x_synth_dpll_get(struct zl3073x_dev *zldev, u8 index)
{
	return zldev->synth[index].dpll;
}

/**
 * zl3073x_synth_freq_get - get synth current freq
 * @zldev: pointer to zl3073x device
 * @index: synth index
 *
 * Return: frequency of given synthetizer
 */
static inline u32
zl3073x_synth_freq_get(struct zl3073x_dev *zldev, u8 index)
{
	return zldev->synth[index].freq;
}

/**
 * zl3073x_synth_is_enabled - check if the given synth is enabled
 * @zldev: pointer to zl3073x device
 * @index: synth index
 *
 * Return: true if synth is enabled, false otherwise
 */
static inline bool
zl3073x_synth_is_enabled(struct zl3073x_dev *zldev, u8 index)
{
	return zldev->synth[index].enabled;
}

/**
 * zl3073x_out_synth_get - get synth connected to given output
 * @zldev: pointer to zl3073x device
 * @index: output index
 *
 * Return: index of synth connected to given output.
 */
static inline u8
zl3073x_out_synth_get(struct zl3073x_dev *zldev, u8 index)
{
	return zldev->out[index].synth;
}

/**
 * zl3073x_out_is_enabled - check if the given output is enabled
 * @zldev: pointer to zl3073x device
 * @index: output index
 *
 * Return: true if the output is enabled, false otherwise
 */
static inline bool
zl3073x_out_is_enabled(struct zl3073x_dev *zldev, u8 index)
{
	u8 synth;

	/* Output is enabled only if associated synth is enabled */
	synth = zl3073x_out_synth_get(zldev, index);
	if (zl3073x_synth_is_enabled(zldev, synth))
		return zldev->out[index].enabled;

	return false;
}

/**
 * zl3073x_out_signal_format_get - get output signal format
 * @zldev: pointer to zl3073x device
 * @index: output index
 *
 * Return: signal format of given output
 */
static inline u8
zl3073x_out_signal_format_get(struct zl3073x_dev *zldev, u8 index)
{
	return zldev->out[index].signal_format;
}

/**
 * zl3073x_out_dpll_get - get DPLL ID the output is driven by
 * @zldev: pointer to zl3073x device
 * @index: output index
 *
 * Return: ID of DPLL the given output is driven by
 */
static inline
u8 zl3073x_out_dpll_get(struct zl3073x_dev *zldev, u8 index)
{
	u8 synth;

	/* Get synthesizer connected to given output */
	synth = zl3073x_out_synth_get(zldev, index);

	/* Return DPLL that drives the synth */
	return zl3073x_synth_dpll_get(zldev, synth);
}

/**
 * zl3073x_out_is_diff - check if the given output is differential
 * @zldev: pointer to zl3073x device
 * @index: output index
 *
 * Return: true if output is differential, false if output is single-ended
 */
static inline bool
zl3073x_out_is_diff(struct zl3073x_dev *zldev, u8 index)
{
	switch (zl3073x_out_signal_format_get(zldev, index)) {
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_LVDS:
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_DIFF:
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_LOWVCM:
		return true;
	default:
		break;
	}

	return false;
}

/**
 * zl3073x_output_pin_is_enabled - check if the given output pin is enabled
 * @zldev: pointer to zl3073x device
 * @id: output pin id
 *
 * Checks if the output of the given output pin is enabled and also that
 * its signal format also enables the given pin.
 *
 * Return: true if output pin is enabled, false if output pin is disabled
 */
static inline bool
zl3073x_output_pin_is_enabled(struct zl3073x_dev *zldev, u8 id)
{
	u8 output = zl3073x_output_pin_out_get(id);

	/* Check if the whole output is enabled */
	if (!zl3073x_out_is_enabled(zldev, output))
		return false;

	/* Check signal format */
	switch (zl3073x_out_signal_format_get(zldev, output)) {
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_DISABLED:
		/* Both output pins are disabled by signal format */
		return false;

	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_1P:
		/* Output is one single ended P-pin output */
		if (zl3073x_is_n_pin(id))
			return false;
		break;
	case ZL_OUTPUT_MODE_SIGNAL_FORMAT_1N:
		/* Output is one single ended N-pin output */
		if (zl3073x_is_p_pin(id))
			return false;
		break;
	default:
		/* For other format both pins are enabled */
		break;
	}

	return true;
}

#endif /* _ZL3073X_CORE_H */
